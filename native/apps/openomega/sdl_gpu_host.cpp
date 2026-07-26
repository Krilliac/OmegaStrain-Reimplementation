#include "sdl_gpu_host.h"

#include "sdl_gpu_exception_boundary.h"
#include "sdl_platform_service.h"

#include "omega/runtime/render_draw_list.h"
#include "omega/runtime/render_mesh_draw_list.h"
#include "omega/runtime/render_mesh_pool.h"
#include "omega/runtime/render_texture_pool.h"

#include <SDL3/SDL.h>

// These six generated headers are part of the exact SDL revision pinned by CMake. They contain
// the permissively licensed testgpu position/color shader in every shader representation requested
// when this host creates its device. The build consumes them directly; no external shader payload
// or runtime compiler is required.
#include <testgpu/cube.frag.dxil.h>
#include <testgpu/cube.frag.msl.h>
#include <testgpu/cube.frag.spv.h>
#include <testgpu/cube.vert.dxil.h>
#include <testgpu/cube.vert.msl.h>
#include <testgpu/cube.vert.spv.h>

// In-house textured mesh shaders (Gap-A / gameplay Slice 1). DXIL only: this is
// the format the SDL GPU D3D12 backend consumes on Windows. Authored HLSL under
// shaders/openomega/, compiled to DXIL by tools/compile_shaders.ps1 and committed
// as SDL-backend-owned headers so the build needs no shader compiler. On backends
// without DXIL (MSL/SPIRV) the textured pipeline is simply not created and mesh
// draws fall back to the flat solid-color pipeline (fail-soft).
#include "sdl_shaders/mesh_textured.frag.dxil.h"
#include "sdl_shaders/mesh_textured.vert.dxil.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace omega::app
{
namespace
{
[[nodiscard]] std::string SdlError(const std::string_view operation)
{
    const char* detail = SDL_GetError();
    return std::string(operation) + ": " +
           (detail != nullptr && detail[0] != '\0' ? detail : "unknown SDL error");
}

[[nodiscard]] std::string PoolError(
    const std::string_view operation, const runtime::RenderTextureError& error)
{
    return std::string(operation) + ": " +
           std::string(runtime::RenderTextureErrorCodeName(error.code));
}

[[nodiscard]] std::string PoolError(
    const std::string_view operation, const runtime::RenderMeshError& error)
{
    return std::string(operation) + ": " +
           std::string(runtime::RenderMeshErrorCodeName(error.code));
}

constexpr std::size_t kPostAcquireErrorCapacity = 512U;
constexpr std::uint32_t kMaximumFrameReadbackWidth = 640U;
constexpr std::uint32_t kMaximumFrameReadbackHeight = 448U;
constexpr std::size_t kMaximumFrameReadbackPixelCount =
    static_cast<std::size_t>(kMaximumFrameReadbackWidth) *
    kMaximumFrameReadbackHeight;
constexpr std::size_t kMaximumFrameReadbackByteCount =
    kMaximumFrameReadbackPixelCount *
    sizeof(runtime::RenderClearColorRgba8);

[[nodiscard]] constexpr float RenderColorChannelToFloat(
    const std::uint8_t channel) noexcept
{
    return static_cast<float>(channel) /
           static_cast<float>(std::numeric_limits<std::uint8_t>::max());
}

static_assert(RenderColorChannelToFloat(0U) == 0.0F);
static_assert(RenderColorChannelToFloat(
                  std::numeric_limits<std::uint8_t>::max()) == 1.0F);

[[nodiscard]] constexpr SDL_FColor ToSdlClearColor(
    const runtime::RenderClearColorRgba8 color) noexcept
{
    return SDL_FColor{
        .r = RenderColorChannelToFloat(color.red),
        .g = RenderColorChannelToFloat(color.green),
        .b = RenderColorChannelToFloat(color.blue),
        .a = RenderColorChannelToFloat(color.alpha),
    };
}

constexpr SDL_FColor kClearColorConversionProbe = ToSdlClearColor(
    runtime::RenderClearColorRgba8{.red = 0U, .green = 255U, .blue = 64U, .alpha = 128U});
static_assert(kClearColorConversionProbe.r == 0.0F &&
              kClearColorConversionProbe.g == 1.0F &&
              kClearColorConversionProbe.b == RenderColorChannelToFloat(64U) &&
              kClearColorConversionProbe.a == RenderColorChannelToFloat(128U));

[[nodiscard]] bool RecordClearPass(SDL_GPUCommandBuffer* commands,
    SDL_GPUTexture* texture, const SDL_FColor clear_color) noexcept
{
    SDL_GPUColorTargetInfo target{};
    target.texture = texture;
    target.clear_color = clear_color;
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1U, nullptr);
    if (pass == nullptr)
        return false;
    SDL_EndGPURenderPass(pass);
    return true;
}

void AppendBounded(std::string& destination, const std::string_view text) noexcept
{
    const std::size_t remaining = destination.capacity() - destination.size();
    destination.append(text.data(), std::min(remaining, text.size()));
}

void SetSdlErrorBounded(
    std::string& destination, const std::string_view operation) noexcept
{
    destination.clear();
    AppendBounded(destination, operation);
    AppendBounded(destination, ": ");
    const char* detail = SDL_GetError();
    AppendBounded(destination,
        detail != nullptr && detail[0] != '\0' ? std::string_view(detail)
                                                : std::string_view("unknown SDL error"));
}

void AppendSdlErrorBounded(
    std::string& destination, const std::string_view operation) noexcept
{
    AppendBounded(destination, "; ");
    AppendBounded(destination, operation);
    AppendBounded(destination, ": ");
    const char* detail = SDL_GetError();
    AppendBounded(destination,
        detail != nullptr && detail[0] != '\0' ? std::string_view(detail)
                                                : std::string_view("unknown SDL error"));
}

class ReservationRollbackGuard final
{
public:
    ReservationRollbackGuard(runtime::RenderTexturePool& pool,
        const runtime::RenderTextureReservation& reservation) noexcept
        : pool_(&pool), reservation_(&reservation)
    {
    }

    ~ReservationRollbackGuard()
    {
        if (pool_ != nullptr)
            static_cast<void>(pool_->Rollback(*reservation_));
    }

    ReservationRollbackGuard(const ReservationRollbackGuard&) = delete;
    ReservationRollbackGuard& operator=(const ReservationRollbackGuard&) = delete;

    void Dismiss() noexcept
    {
        pool_ = nullptr;
        reservation_ = nullptr;
    }

private:
    runtime::RenderTexturePool* pool_ = nullptr;
    const runtime::RenderTextureReservation* reservation_ = nullptr;
};

class TextureGuard final
{
public:
    TextureGuard(SDL_GPUDevice* device, SDL_GPUTexture* texture) noexcept
        : device_(device), texture_(texture)
    {
    }

    ~TextureGuard()
    {
        if (texture_ != nullptr)
            SDL_ReleaseGPUTexture(device_, texture_);
    }

    TextureGuard(const TextureGuard&) = delete;
    TextureGuard& operator=(const TextureGuard&) = delete;

    void Dismiss() noexcept { texture_ = nullptr; }

private:
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUTexture* texture_ = nullptr;
};

class BufferGuard final
{
public:
    BufferGuard(SDL_GPUDevice* device, SDL_GPUBuffer* buffer) noexcept
        : device_(device), buffer_(buffer)
    {
    }

    ~BufferGuard()
    {
        if (buffer_ != nullptr)
            SDL_ReleaseGPUBuffer(device_, buffer_);
    }

    BufferGuard(const BufferGuard&) = delete;
    BufferGuard& operator=(const BufferGuard&) = delete;

    void Dismiss() noexcept { buffer_ = nullptr; }

private:
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUBuffer* buffer_ = nullptr;
};

class ShaderGuard final
{
public:
    ShaderGuard(SDL_GPUDevice* device, SDL_GPUShader* shader) noexcept
        : device_(device), shader_(shader)
    {
    }

    ~ShaderGuard()
    {
        if (shader_ != nullptr)
            SDL_ReleaseGPUShader(device_, shader_);
    }

    ShaderGuard(const ShaderGuard&) = delete;
    ShaderGuard& operator=(const ShaderGuard&) = delete;

private:
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUShader* shader_ = nullptr;
};

class PipelineGuard final
{
public:
    PipelineGuard(SDL_GPUDevice* device, SDL_GPUGraphicsPipeline* pipeline) noexcept
        : device_(device), pipeline_(pipeline)
    {
    }

    ~PipelineGuard()
    {
        if (pipeline_ != nullptr)
            SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
    }

    PipelineGuard(const PipelineGuard&) = delete;
    PipelineGuard& operator=(const PipelineGuard&) = delete;

    void Dismiss() noexcept { pipeline_ = nullptr; }

private:
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
};

class TransferBufferGuard final
{
public:
    TransferBufferGuard(SDL_GPUDevice* device, SDL_GPUTransferBuffer* transfer) noexcept
        : device_(device), transfer_(transfer)
    {
    }

    ~TransferBufferGuard()
    {
        if (transfer_ != nullptr)
            SDL_ReleaseGPUTransferBuffer(device_, transfer_);
    }

    TransferBufferGuard(const TransferBufferGuard&) = delete;
    TransferBufferGuard& operator=(const TransferBufferGuard&) = delete;

    void Reset(SDL_GPUTransferBuffer* transfer) noexcept
    {
        if (transfer_ != nullptr)
            SDL_ReleaseGPUTransferBuffer(device_, transfer_);
        transfer_ = transfer;
    }

    void Dismiss() noexcept { transfer_ = nullptr; }

private:
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUTransferBuffer* transfer_ = nullptr;
};

class MeshReservationRollbackGuard final
{
public:
    MeshReservationRollbackGuard(runtime::RenderMeshPool& pool,
        const runtime::RenderMeshReservation& reservation) noexcept
        : pool_(&pool), reservation_(&reservation)
    {
    }

    ~MeshReservationRollbackGuard()
    {
        if (pool_ != nullptr)
            static_cast<void>(pool_->Rollback(*reservation_));
    }

    MeshReservationRollbackGuard(const MeshReservationRollbackGuard&) = delete;
    MeshReservationRollbackGuard& operator=(const MeshReservationRollbackGuard&) = delete;

    void Dismiss() noexcept
    {
        pool_ = nullptr;
        reservation_ = nullptr;
    }

private:
    runtime::RenderMeshPool* pool_ = nullptr;
    const runtime::RenderMeshReservation* reservation_ = nullptr;
};

class FenceGuard final
{
public:
    FenceGuard(SDL_GPUDevice* device, SDL_GPUFence* fence) noexcept
        : device_(device), fence_(fence)
    {
    }

    ~FenceGuard()
    {
        if (fence_ != nullptr)
            SDL_ReleaseGPUFence(device_, fence_);
    }

    FenceGuard(const FenceGuard&) = delete;
    FenceGuard& operator=(const FenceGuard&) = delete;

private:
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUFence* fence_ = nullptr;
};

class TransferBufferMapGuard final
{
public:
    TransferBufferMapGuard(SDL_GPUDevice* device,
        SDL_GPUTransferBuffer* transfer, void* mapped) noexcept
        : device_(device), transfer_(transfer), mapped_(mapped)
    {
    }

    ~TransferBufferMapGuard()
    {
        if (mapped_ != nullptr)
            SDL_UnmapGPUTransferBuffer(device_, transfer_);
    }

    TransferBufferMapGuard(const TransferBufferMapGuard&) = delete;
    TransferBufferMapGuard& operator=(const TransferBufferMapGuard&) = delete;

private:
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUTransferBuffer* transfer_ = nullptr;
    void* mapped_ = nullptr;
};

enum class CommandBufferUnwindAction
{
    Cancel,
    Submit,
};

class CommandBufferGuard final
{
public:
    explicit CommandBufferGuard(SDL_GPUCommandBuffer* commands) noexcept
        : commands_(commands)
    {
    }

    ~CommandBufferGuard()
    {
        if (commands_ == nullptr)
            return;
        if (unwind_action_ == CommandBufferUnwindAction::Submit)
            static_cast<void>(SDL_SubmitGPUCommandBuffer(commands_));
        else
            static_cast<void>(SDL_CancelGPUCommandBuffer(commands_));
    }

    CommandBufferGuard(const CommandBufferGuard&) = delete;
    CommandBufferGuard& operator=(const CommandBufferGuard&) = delete;

    void SubmitOnUnwind() noexcept
    {
        unwind_action_ = CommandBufferUnwindAction::Submit;
    }

    [[nodiscard]] bool Cancel() noexcept
    {
        SDL_GPUCommandBuffer* commands = std::exchange(commands_, nullptr);
        return commands != nullptr && SDL_CancelGPUCommandBuffer(commands);
    }

    [[nodiscard]] bool Submit() noexcept
    {
        SDL_GPUCommandBuffer* commands = std::exchange(commands_, nullptr);
        return commands != nullptr && SDL_SubmitGPUCommandBuffer(commands);
    }

    // SDL consumes the command buffer even when fence acquisition fails. Taking it first prevents
    // the guard from attempting either cancellation or a second submission during unwinding.
    [[nodiscard]] SDL_GPUCommandBuffer* Take() noexcept
    {
        return std::exchange(commands_, nullptr);
    }

private:
    SDL_GPUCommandBuffer* commands_ = nullptr;
    CommandBufferUnwindAction unwind_action_ = CommandBufferUnwindAction::Cancel;
};

void SaturatingIncrement(std::uint64_t& value) noexcept
{
    if (value != std::numeric_limits<std::uint64_t>::max())
        ++value;
}

void SaturatingAdd(std::uint64_t& value, const std::uint64_t added) noexcept
{
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    value = added > maximum - value ? maximum : value + added;
}

[[nodiscard]] constexpr bool IsDefaultHandle(
    const runtime::RenderTextureHandle& handle) noexcept
{
    return handle == runtime::RenderTextureHandle{};
}

[[nodiscard]] constexpr bool IsDefaultHandle(
    const runtime::RenderMeshHandle& handle) noexcept
{
    return handle == runtime::RenderMeshHandle{};
}

[[nodiscard]] bool TryMapTextureFilter(
    const runtime::RenderTextureFilterMode filter_mode,
    SDL_GPUFilter& mapped_filter) noexcept
{
    switch (filter_mode)
    {
    case runtime::RenderTextureFilterMode::Nearest:
        mapped_filter = SDL_GPU_FILTER_NEAREST;
        return true;
    case runtime::RenderTextureFilterMode::Linear:
        mapped_filter = SDL_GPU_FILTER_LINEAR;
        return true;
    default:
        return false;
    }
}

struct MeshBackendSlot
{
    SDL_GPUBuffer* positions = nullptr;
    SDL_GPUBuffer* triangle_indices = nullptr;
    // Per-vertex texture coordinates. ALWAYS created, one float2 per position, even for an upload
    // that carried no UVs (it is then zero-filled). The flat, wireframe and textured pipelines share
    // one vertex-input description, so the UV slot is always declared and must always be bindable:
    // binding fewer buffers than the pipeline declares fails the draw at runtime.
    SDL_GPUBuffer* uvs = nullptr;
    std::uint32_t position_count = 0U;
    std::uint32_t triangle_index_count = 0U;
    // Uploaded UV count: zero when the mesh carried none (the buffer is then the zero fill above).
    std::uint32_t uv_count = 0U;
};

// One float2 vertex texture coordinate, matching VumVisualMeshIR::uvs element layout.
struct MeshUvF
{
    float u = 0.0F;
    float v = 0.0F;
};

struct MeshColorRgbF
{
    float red = 0.0F;
    float green = 0.0F;
    float blue = 0.0F;
};

struct ResolvedMeshDraw
{
    SDL_GPUBuffer* positions = nullptr;
    SDL_GPUBuffer* triangle_indices = nullptr;
    SDL_GPUBuffer* uvs = nullptr;
    std::uint32_t triangle_index_count = 0U;
    // Resolved backend albedo texture, or nullptr when the draw has no (valid)
    // texture -> the flat solid-color pipeline is used for that draw.
    SDL_GPUTexture* texture = nullptr;
};

static_assert(sizeof(asset::Float3IR) == sizeof(float) * 3U);
static_assert(sizeof(MeshColorRgbF) == sizeof(float) * 3U);
static_assert(sizeof(MeshUvF) == sizeof(float) * 2U);
static_assert(sizeof(MeshUvF) == sizeof(std::array<float, 2U>));

[[nodiscard]] constexpr MeshColorRgbF ToMeshColor(
    const runtime::RenderMeshColorRgba8 color) noexcept
{
    // The pinned diagnostic shader is explicitly opaque. Alpha remains renderer-neutral command
    // data until a later material/blend contract assigns it observable backend semantics.
    return MeshColorRgbF{
        .red = RenderColorChannelToFloat(color.red),
        .green = RenderColorChannelToFloat(color.green),
        .blue = RenderColorChannelToFloat(color.blue),
    };
}

[[nodiscard]] constexpr std::array<float, 16U> ToShaderMatrix(
    const asset::Matrix4x4IR& matrix) noexcept
{
    std::array<float, 16U> column_major{};
    for (std::size_t row = 0U; row < 4U; ++row)
    {
        for (std::size_t column = 0U; column < 4U; ++column)
            column_major[column * 4U + row] = matrix.row_major[row * 4U + column];
    }
    return column_major;
}

constexpr asset::Matrix4x4IR kShaderMatrixConversionInput{
    .row_major = {
        0.0F, 1.0F, 2.0F, 3.0F,
        4.0F, 5.0F, 6.0F, 7.0F,
        8.0F, 9.0F, 10.0F, 11.0F,
        12.0F, 13.0F, 14.0F, 15.0F,
    },
};
static_assert(ToShaderMatrix(kShaderMatrixConversionInput) ==
              std::array<float, 16U>{
                  0.0F, 4.0F, 8.0F, 12.0F,
                  1.0F, 5.0F, 9.0F, 13.0F,
                  2.0F, 6.0F, 10.0F, 14.0F,
                  3.0F, 7.0F, 11.0F, 15.0F,
              });

[[nodiscard]] std::expected<SDL_GPUShader*, std::string> CreateMeshShader(
    SDL_GPUDevice* device, const bool vertex)
{
    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(device);
    SDL_GPUShaderCreateInfo create_info{};
    if ((supported & SDL_GPU_SHADERFORMAT_DXIL) != 0U)
    {
        create_info.format = SDL_GPU_SHADERFORMAT_DXIL;
        create_info.code = vertex ? cube_vert_dxil : cube_frag_dxil;
        create_info.code_size = vertex ? cube_vert_dxil_len : cube_frag_dxil_len;
        create_info.entrypoint = "main";
    }
    else if ((supported & SDL_GPU_SHADERFORMAT_MSL) != 0U)
    {
        create_info.format = SDL_GPU_SHADERFORMAT_MSL;
        create_info.code = vertex ? cube_vert_msl : cube_frag_msl;
        create_info.code_size = vertex ? cube_vert_msl_len : cube_frag_msl_len;
        create_info.entrypoint = "main0";
    }
    else if ((supported & SDL_GPU_SHADERFORMAT_SPIRV) != 0U)
    {
        create_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        create_info.code = vertex ? cube_vert_spv : cube_frag_spv;
        create_info.code_size = vertex ? cube_vert_spv_len : cube_frag_spv_len;
        create_info.entrypoint = "main";
    }
    else
    {
        return std::unexpected("render mesh shader format is unsupported");
    }
    create_info.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    create_info.num_uniform_buffers = vertex ? 1U : 0U;

    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &create_info);
    if (shader == nullptr)
        return std::unexpected(SdlError(vertex ? "render mesh vertex shader create"
                                                : "render mesh fragment shader create"));
    return shader;
}

// Loads the in-house textured mesh shader. DXIL only (see the header include
// note); returns nullptr fail-soft on any other backend or on create failure so
// the caller keeps the flat pipeline. The fragment shader binds one texture +
// sampler in SDL GPU fragment space2; the vertex shader keeps the same one
// uniform buffer (object_to_clip) and vertex layout as the flat mesh shader.
[[nodiscard]] SDL_GPUShader* CreateTexturedMeshShader(
    SDL_GPUDevice* device, const bool vertex) noexcept
{
    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(device);
    if ((supported & SDL_GPU_SHADERFORMAT_DXIL) == 0U)
        return nullptr;
    SDL_GPUShaderCreateInfo create_info{};
    create_info.format = SDL_GPU_SHADERFORMAT_DXIL;
    create_info.code = vertex ? mesh_textured_vert_dxil : mesh_textured_frag_dxil;
    create_info.code_size =
        vertex ? mesh_textured_vert_dxil_len : mesh_textured_frag_dxil_len;
    create_info.entrypoint = "main";
    create_info.stage =
        vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    create_info.num_uniform_buffers = vertex ? 1U : 0U;
    create_info.num_samplers = vertex ? 0U : 1U;
    return SDL_CreateGPUShader(device, &create_info);
}

// Picks the depth-stencil format the mesh pass will use, once, at device setup.
// SDL requires the render pass depth target and the bound pipeline to agree on
// this format, so it is resolved in exactly one place and stored on Impl rather
// than recomputed at each creation site. D32_FLOAT is preferred (highest depth
// precision of the three, no stencil plane to clear); D24_UNORM_S8_UINT and
// D16_UNORM are the conventional fallbacks. Returns
// SDL_GPU_TEXTUREFORMAT_INVALID when the device supports none of them, in which
// case the mesh pass runs depth-less exactly as it did before -- fail-soft, the
// same posture the textured pipeline takes on a backend without DXIL.
[[nodiscard]] SDL_GPUTextureFormat ResolveMeshDepthFormat(SDL_GPUDevice* device) noexcept
{
    constexpr std::array candidates{
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
        SDL_GPU_TEXTUREFORMAT_D16_UNORM,
    };
    for (const SDL_GPUTextureFormat candidate : candidates)
    {
        if (SDL_GPUTextureSupportsFormat(device, candidate, SDL_GPU_TEXTURETYPE_2D,
                SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
        {
            return candidate;
        }
    }
    return SDL_GPU_TEXTUREFORMAT_INVALID;
}

// True for the one ResolveMeshDepthFormat candidate that carries a stencil
// plane. The stencil load/store ops must only be filled in for that format: the
// D3D12 backend turns a CLEAR stencil load op into D3D12_CLEAR_FLAG_STENCIL,
// which is invalid against a depth-only view.
[[nodiscard]] constexpr bool MeshDepthFormatHasStencil(
    const SDL_GPUTextureFormat format) noexcept
{
    return format == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
}

[[nodiscard]] SDL_GPUGraphicsPipeline* CreateMeshPipeline(SDL_GPUDevice* device,
    SDL_GPUShader* vertex_shader, SDL_GPUShader* fragment_shader,
    const SDL_GPUTextureFormat target_format, const SDL_GPUTextureFormat depth_format,
    const SDL_GPUFillMode fill_mode) noexcept
{
    // ONE vertex-input description shared by the flat fill, wireframe and textured pipelines. The
    // three declare the same three buffer slots even though only the textured vertex shader reads
    // slot 2, because RecordMeshPass binds all three for every draw: if the declared and the bound
    // buffer counts ever disagreed, the bind would fail at runtime. Slot 2 is per-vertex UVs; a
    // mesh uploaded without UVs still owns a zero-filled slot-2 buffer (see MeshBackendSlot).
    constexpr std::array vertex_buffers{
        SDL_GPUVertexBufferDescription{
            .slot = 0U,
            .pitch = static_cast<std::uint32_t>(sizeof(asset::Float3IR)),
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
            .instance_step_rate = 0U,
        },
        SDL_GPUVertexBufferDescription{
            .slot = 1U,
            .pitch = static_cast<std::uint32_t>(sizeof(MeshColorRgbF)),
            .input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE,
            .instance_step_rate = 0U,
        },
        SDL_GPUVertexBufferDescription{
            .slot = 2U,
            .pitch = static_cast<std::uint32_t>(sizeof(MeshUvF)),
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
            .instance_step_rate = 0U,
        },
    };
    constexpr std::array vertex_attributes{
        SDL_GPUVertexAttribute{
            .location = 0U,
            .buffer_slot = 0U,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
            .offset = 0U,
        },
        SDL_GPUVertexAttribute{
            .location = 1U,
            .buffer_slot = 1U,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
            .offset = 0U,
        },
        SDL_GPUVertexAttribute{
            .location = 2U,
            .buffer_slot = 2U,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
            .offset = 0U,
        },
    };
    const SDL_GPUColorTargetDescription color_target{
        .format = target_format,
        .blend_state = {
            .color_write_mask = static_cast<SDL_GPUColorComponentFlags>(
                SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
                SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A),
        },
    };
    const SDL_GPUGraphicsPipelineCreateInfo create_info{
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
        .vertex_input_state = {
            .vertex_buffer_descriptions = vertex_buffers.data(),
            .num_vertex_buffers = static_cast<std::uint32_t>(vertex_buffers.size()),
            .vertex_attributes = vertex_attributes.data(),
            .num_vertex_attributes = static_cast<std::uint32_t>(vertex_attributes.size()),
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = {
            .fill_mode = fill_mode,
            // cull_mode stays NONE. The decoded COL/VUM geometry has unproven
            // triangle winding, so enabling backface culling could silently
            // delete surfaces. Depth testing below fixes the ordering defect on
            // its own; culling is a separate decision that needs winding
            // evidence first.
            .cull_mode = SDL_GPU_CULLMODE_NONE,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
            .enable_depth_clip = true,
        },
        .multisample_state = {
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
        },
        // Standard less-than depth test with writes enabled: without it the 3D
        // view resolves purely by submission order and distant geometry paints
        // over near geometry. Disabled only when the device supports no depth
        // format at all, which also drops the depth-stencil target below so the
        // pipeline still matches the (then depth-less) render pass.
        .depth_stencil_state = {
            .compare_op = SDL_GPU_COMPAREOP_LESS,
            .enable_depth_test = depth_format != SDL_GPU_TEXTUREFORMAT_INVALID,
            .enable_depth_write = depth_format != SDL_GPU_TEXTUREFORMAT_INVALID,
        },
        .target_info = {
            .color_target_descriptions = &color_target,
            .num_color_targets = 1U,
            .depth_stencil_format = depth_format,
            .has_depth_stencil_target = depth_format != SDL_GPU_TEXTUREFORMAT_INVALID,
        },
        .props = 0,
    };
    return SDL_CreateGPUGraphicsPipeline(device, &create_info);
}

[[nodiscard]] bool RecordMeshColorUpload(SDL_GPUCommandBuffer* commands,
    SDL_GPUTransferBuffer* transfer, SDL_GPUBuffer* destination,
    const std::uint32_t byte_count) noexcept
{
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    if (copy == nullptr)
        return false;
    const SDL_GPUTransferBufferLocation source{
        .transfer_buffer = transfer,
        .offset = 0U,
    };
    const SDL_GPUBufferRegion target{
        .buffer = destination,
        .offset = 0U,
        .size = byte_count,
    };
    SDL_UploadToGPUBuffer(copy, &source, &target, true);
    SDL_EndGPUCopyPass(copy);
    return true;
}

[[nodiscard]] bool RecordMeshPass(SDL_GPUCommandBuffer* commands,
    SDL_GPUTexture* destination, const SDL_FColor clear_color,
    const std::span<const runtime::RenderMeshDrawCommand> draw_commands,
    const std::span<const ResolvedMeshDraw> resolved_draws,
    SDL_GPUBuffer* color_buffer, SDL_GPUGraphicsPipeline* fill_pipeline,
    SDL_GPUGraphicsPipeline* wireframe_pipeline,
    SDL_GPUGraphicsPipeline* textured_pipeline, SDL_GPUSampler* sampler,
    SDL_GPUTexture* depth_texture, const SDL_GPUTextureFormat depth_format) noexcept
{
    SDL_GPUColorTargetInfo target{};
    target.texture = destination;
    target.clear_color = clear_color;
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;

    // Depth attachment for this 3D pass only; the 2D HUD/front-end passes stay
    // depth-less. Cleared to the far plane each frame and discarded afterwards
    // (STOREOP_DONT_CARE) because nothing outside the pass reads depth -- it
    // exists solely to order this pass's own triangles. depth_texture is null
    // only when the device supports no depth format, and the pipelines were
    // then built without a depth-stencil target to match.
    SDL_GPUDepthStencilTargetInfo depth_target{};
    depth_target.texture = depth_texture;
    depth_target.clear_depth = 1.0F;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_DONT_CARE;
    if (MeshDepthFormatHasStencil(depth_format))
    {
        depth_target.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
        depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    }

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1U,
        depth_texture != nullptr ? &depth_target : nullptr);
    if (pass == nullptr)
        return false;

    for (std::size_t index = 0U; index < draw_commands.size(); ++index)
    {
        const runtime::RenderMeshDrawCommand& draw = draw_commands[index];
        const ResolvedMeshDraw& resolved = resolved_draws[index];
        const std::array<float, 16U> matrix = ToShaderMatrix(draw.object_to_clip);
        SDL_PushGPUVertexUniformData(
            commands, 0U, matrix.data(), static_cast<std::uint32_t>(sizeof(matrix)));

        // A Fill draw with a resolved texture uses the textured pipeline (which
        // samples the bound texture with the mesh's per-vertex UVs); every
        // other case keeps the flat solid-color fill/wireframe pipeline.
        const bool use_textured =
            draw.raster_mode == runtime::RenderMeshRasterMode::Fill &&
            resolved.texture != nullptr && textured_pipeline != nullptr &&
            sampler != nullptr;
        SDL_GPUGraphicsPipeline* pipeline =
            use_textured ? textured_pipeline
                         : (draw.raster_mode == runtime::RenderMeshRasterMode::Fill
                                   ? fill_pipeline
                                   : wireframe_pipeline);
        SDL_BindGPUGraphicsPipeline(pass, pipeline);
        if (use_textured)
        {
            const SDL_GPUTextureSamplerBinding texture_binding{
                .texture = resolved.texture,
                .sampler = sampler,
            };
            SDL_BindGPUFragmentSamplers(pass, 0U, &texture_binding, 1U);
        }
        // Three bindings for every draw and every pipeline: the shared vertex-input description
        // declares three buffer slots, so a short bind list would fail the draw.
        const std::array vertex_bindings{
            SDL_GPUBufferBinding{
                .buffer = resolved.positions,
                .offset = 0U,
            },
            SDL_GPUBufferBinding{
                .buffer = color_buffer,
                .offset = static_cast<std::uint32_t>(index * sizeof(MeshColorRgbF)),
            },
            SDL_GPUBufferBinding{
                .buffer = resolved.uvs,
                .offset = 0U,
            },
        };
        SDL_BindGPUVertexBuffers(pass, 0U, vertex_bindings.data(),
            static_cast<std::uint32_t>(vertex_bindings.size()));
        const SDL_GPUBufferBinding index_binding{
            .buffer = resolved.triangle_indices,
            .offset = 0U,
        };
        SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_DrawGPUIndexedPrimitives(
            pass, resolved.triangle_index_count, 1U, 0U, 0, 0U);
    }
    SDL_EndGPURenderPass(pass);
    return true;
}

enum class FrameSubmissionKind
{
    Blit,
    Mesh,
    Clear,
    UnavailableSwapchain,
};

struct ResolvedTextureBlit
{
    SDL_GPUTexture* texture = nullptr;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
};

struct PreparedTextureBlit
{
    SDL_GPUTexture* source = nullptr;
    runtime::RenderTextureBlitPlan plan{};
    SDL_GPUFilter filter = SDL_GPU_FILTER_NEAREST;
};

void RecordTextureBlits(SDL_GPUCommandBuffer* commands,
    SDL_GPUTexture* destination,
    const std::span<const PreparedTextureBlit> blits) noexcept
{
    for (const PreparedTextureBlit& prepared : blits)
    {
        const runtime::RenderTextureBlitPlan& plan = prepared.plan;
        const SDL_GPUBlitInfo blit{
            .source = {
                .texture = prepared.source,
                .mip_level = 0,
                .layer_or_depth_plane = 0,
                .x = plan.source.left,
                .y = plan.source.top,
                .w = plan.source.right - plan.source.left,
                .h = plan.source.bottom - plan.source.top,
            },
            .destination = {
                .texture = destination,
                .mip_level = 0,
                .layer_or_depth_plane = 0,
                .x = plan.destination.left,
                .y = plan.destination.top,
                .w = plan.destination.right - plan.destination.left,
                .h = plan.destination.bottom - plan.destination.top,
            },
            .load_op = SDL_GPU_LOADOP_LOAD,
            .clear_color = {},
            .flip_mode = SDL_FLIP_NONE,
            .filter = prepared.filter,
            .cycle = false,
            .padding1 = 0,
            .padding2 = 0,
            .padding3 = 0,
        };
        SDL_BlitGPUTexture(commands, &blit);
    }
}

} // namespace

struct SdlGpuHost::Impl
{
    Impl(runtime::RenderTexturePool texture_pool_value,
        runtime::RenderMeshPool mesh_pool_value)
        : texture_pool(std::move(texture_pool_value)),
          texture_slots(texture_pool.Snapshot().slot_capacity, nullptr),
          mesh_pool(std::move(mesh_pool_value)),
          mesh_slots(mesh_pool.Snapshot().slot_capacity)
    {
    }

    ~Impl()
    {
        if (device != nullptr)
        {
            SDL_WaitForGPUIdle(device);
            for (SDL_GPUTexture*& texture : texture_slots)
            {
                if (texture != nullptr)
                {
                    SDL_ReleaseGPUTexture(device, texture);
                    texture = nullptr;
                }
            }
            for (MeshBackendSlot& mesh : mesh_slots)
            {
                if (mesh.positions != nullptr)
                    SDL_ReleaseGPUBuffer(device, mesh.positions);
                if (mesh.triangle_indices != nullptr)
                    SDL_ReleaseGPUBuffer(device, mesh.triangle_indices);
                if (mesh.uvs != nullptr)
                    SDL_ReleaseGPUBuffer(device, mesh.uvs);
                mesh = {};
            }
            if (mesh_color_buffer != nullptr)
            {
                SDL_ReleaseGPUBuffer(device, mesh_color_buffer);
                mesh_color_buffer = nullptr;
            }
            if (mesh_color_transfer != nullptr)
            {
                SDL_ReleaseGPUTransferBuffer(device, mesh_color_transfer);
                mesh_color_transfer = nullptr;
            }
            if (mesh_fill_pipeline != nullptr)
            {
                SDL_ReleaseGPUGraphicsPipeline(device, mesh_fill_pipeline);
                mesh_fill_pipeline = nullptr;
            }
            if (mesh_wireframe_pipeline != nullptr)
            {
                SDL_ReleaseGPUGraphicsPipeline(device, mesh_wireframe_pipeline);
                mesh_wireframe_pipeline = nullptr;
            }
            if (mesh_textured_pipeline != nullptr)
            {
                SDL_ReleaseGPUGraphicsPipeline(device, mesh_textured_pipeline);
                mesh_textured_pipeline = nullptr;
            }
            if (mesh_sampler != nullptr)
            {
                SDL_ReleaseGPUSampler(device, mesh_sampler);
                mesh_sampler = nullptr;
            }
            if (mesh_depth_texture != nullptr)
            {
                SDL_ReleaseGPUTexture(device, mesh_depth_texture);
                mesh_depth_texture = nullptr;
                mesh_depth_width = 0U;
                mesh_depth_height = 0U;
            }
            if (window_claimed && window != nullptr)
                SDL_ReleaseWindowFromGPUDevice(device, window);
            SDL_DestroyGPUDevice(device);
        }
        if (window != nullptr)
            SDL_DestroyWindow(window);
        if (subsystems_initialized)
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    // Sole owner of the device, window, textures, buffers and pipelines released
    // above. The members are individually copyable, so without these deletions a
    // copy would double-release every backend handle. Only ever reached through
    // the owning unique_ptr.
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    runtime::RenderTexturePool texture_pool;
    std::vector<SDL_GPUTexture*> texture_slots;
    runtime::RenderMeshPool mesh_pool;
    std::vector<MeshBackendSlot> mesh_slots;
    bool subsystems_initialized = false;
    bool window_claimed = false;
    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    SDL_GPUBuffer* mesh_color_buffer = nullptr;
    SDL_GPUTransferBuffer* mesh_color_transfer = nullptr;
    SDL_GPUGraphicsPipeline* mesh_fill_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* mesh_wireframe_pipeline = nullptr;
    // Textured fill variant + its sampler. Null when the backend has no DXIL
    // (see CreateTexturedMeshShader) -> textured draws fall back to flat fill.
    SDL_GPUGraphicsPipeline* mesh_textured_pipeline = nullptr;
    SDL_GPUSampler* mesh_sampler = nullptr;
    SDL_GPUTextureFormat mesh_pipeline_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    // Depth-stencil format resolved once at device setup (ResolveMeshDepthFormat)
    // and shared by pipeline creation and depth-texture creation so the two can
    // never disagree. SDL_GPU_TEXTUREFORMAT_INVALID means the device offers no
    // usable depth format and the mesh pass runs depth-less.
    SDL_GPUTextureFormat mesh_depth_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    // Depth target for the mesh pass, created on first use and recreated when
    // the required extent changes (the swapchain is resizable, and the readback
    // path picks its own extent per call). Its dimensions are tracked here
    // because SDL exposes no way to query them back from the texture.
    SDL_GPUTexture* mesh_depth_texture = nullptr;
    std::uint32_t mesh_depth_width = 0U;
    std::uint32_t mesh_depth_height = 0U;
    std::string driver;
    std::uint64_t successful_uploads = 0U;
    std::uint64_t successful_upload_logical_bytes = 0U;
    std::uint64_t successful_updates = 0U;
    std::uint64_t successful_update_logical_bytes = 0U;
    std::uint64_t successful_releases = 0U;
    std::uint64_t frame_submissions = 0U;
    std::uint64_t blit_submissions = 0U;
    std::uint64_t successful_blit_draws = 0U;
    std::uint64_t clear_submissions = 0U;
    std::uint64_t unavailable_swapchain_submissions = 0U;
    std::uint64_t rejected_nondefault_texture_handles = 0U;
    std::uint64_t successful_mesh_uploads = 0U;
    std::uint64_t successful_mesh_upload_logical_bytes = 0U;
    std::uint64_t successful_mesh_releases = 0U;
    std::uint64_t mesh_submissions = 0U;
    std::uint64_t successful_mesh_draws = 0U;
    std::uint64_t rejected_nondefault_mesh_handles = 0U;
    // RenderFrame is main-thread-only. Retaining this bounded error storage
    // removes one reserve/free pair from every successful warmed frame; an error
    // moves the message to the caller and the next frame replenishes the scratch.
    std::string render_frame_error_scratch;

    // Resolves an optional mesh-draw albedo texture handle to its backend
    // texture. Fail-soft: a default/invalid handle or any pool/slot miss returns
    // nullptr, so the draw uses the flat pipeline rather than failing the frame.
    [[nodiscard]] SDL_GPUTexture* ResolveTextureSlot(
        const runtime::RenderTextureHandle& handle) noexcept
    {
        if (!handle.valid())
            return nullptr;
        auto metadata = texture_pool.Get(handle);
        if (!metadata)
            return nullptr;
        const std::uint32_t slot_index = metadata->handle.slot_index;
        if (slot_index >= texture_slots.size())
            return nullptr;
        return texture_slots[slot_index];
    }

    [[nodiscard]] std::expected<void, std::string> EnsureMeshPipelines(
        SDL_GPUTextureFormat target_format, bool need_fill, bool need_wireframe,
        bool need_color_transfer);

    // Makes mesh_depth_texture exactly width x height, recreating it when the
    // extent changed, and hands the result back through depth_texture.
    // Returns false only for a genuine creation failure (the caller reports
    // SDL_GetError); a device with no usable depth format is not a failure and
    // yields true with a null depth_texture, matching the depth-less pipelines
    // built for that case.
    [[nodiscard]] bool EnsureMeshDepthTexture(std::uint32_t width,
        std::uint32_t height, SDL_GPUTexture*& depth_texture) noexcept;
};

bool SdlGpuHost::Impl::EnsureMeshDepthTexture(const std::uint32_t width,
    const std::uint32_t height, SDL_GPUTexture*& depth_texture) noexcept
{
    depth_texture = nullptr;
    if (mesh_depth_format == SDL_GPU_TEXTUREFORMAT_INVALID)
        return true;
    if (width == 0U || height == 0U)
        return false;

    if (mesh_depth_texture != nullptr && mesh_depth_width == width &&
        mesh_depth_height == height)
    {
        depth_texture = mesh_depth_texture;
        return true;
    }

    const SDL_GPUTextureCreateInfo depth_info{
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = mesh_depth_format,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        .width = width,
        .height = height,
        .layer_count_or_depth = 1U,
        .num_levels = 1U,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0,
    };
    SDL_GPUTexture* created = SDL_CreateGPUTexture(device, &depth_info);
    if (created == nullptr)
        return false;

    // Only drop the previous target once the replacement exists, so a failed
    // resize leaves the old (wrong-sized) texture untouched rather than leaving
    // the host with no depth target at all.
    if (mesh_depth_texture != nullptr)
        SDL_ReleaseGPUTexture(device, mesh_depth_texture);
    mesh_depth_texture = created;
    mesh_depth_width = width;
    mesh_depth_height = height;
    depth_texture = created;
    return true;
}

std::expected<void, std::string> SdlGpuHost::Impl::EnsureMeshPipelines(
    const SDL_GPUTextureFormat target_format, const bool need_fill,
    const bool need_wireframe, const bool need_color_transfer)
{
    if (target_format == SDL_GPU_TEXTUREFORMAT_INVALID)
        return std::unexpected("render mesh target format is invalid");

    const bool same_format = mesh_pipeline_format == target_format;
    const bool create_fill = need_fill && (!same_format || mesh_fill_pipeline == nullptr);
    const bool create_wireframe =
        need_wireframe && (!same_format || mesh_wireframe_pipeline == nullptr);
    const bool create_color_buffer = mesh_color_buffer == nullptr;
    const bool create_color_transfer =
        need_color_transfer && mesh_color_transfer == nullptr;
    if (!create_fill && !create_wireframe && !create_color_buffer &&
        !create_color_transfer)
        return {};

    constexpr std::size_t color_buffer_bytes =
        runtime::kMaximumRenderMeshDrawsPerFrame * sizeof(MeshColorRgbF);
    static_assert(color_buffer_bytes <= std::numeric_limits<std::uint32_t>::max());

    SDL_GPUBuffer* new_color_buffer = nullptr;
    if (create_color_buffer)
    {
        const SDL_GPUBufferCreateInfo color_buffer_info{
            .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
            .size = static_cast<std::uint32_t>(color_buffer_bytes),
            .props = 0,
        };
        new_color_buffer = SDL_CreateGPUBuffer(device, &color_buffer_info);
        if (new_color_buffer == nullptr)
            return std::unexpected(SdlError("render mesh color buffer create"));
    }
    BufferGuard color_guard(device, new_color_buffer);

    SDL_GPUTransferBuffer* new_color_transfer = nullptr;
    if (create_color_transfer)
    {
        const SDL_GPUTransferBufferCreateInfo transfer_info{
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = static_cast<std::uint32_t>(color_buffer_bytes),
            .props = 0,
        };
        new_color_transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
        if (new_color_transfer == nullptr)
        {
            return std::unexpected(
                SdlError("render mesh color transfer-buffer create"));
        }
    }
    TransferBufferGuard color_transfer_guard(device, new_color_transfer);

    SDL_GPUGraphicsPipeline* new_fill = nullptr;
    SDL_GPUGraphicsPipeline* new_wireframe = nullptr;
    if (create_fill || create_wireframe)
    {
        auto created_vertex_shader = CreateMeshShader(device, true);
        if (!created_vertex_shader)
            return std::unexpected(std::move(created_vertex_shader.error()));
        ShaderGuard vertex_guard(device, *created_vertex_shader);

        auto created_fragment_shader = CreateMeshShader(device, false);
        if (!created_fragment_shader)
            return std::unexpected(std::move(created_fragment_shader.error()));
        ShaderGuard fragment_guard(device, *created_fragment_shader);

        if (create_fill)
        {
            new_fill = CreateMeshPipeline(device, *created_vertex_shader,
                *created_fragment_shader, target_format, mesh_depth_format,
                SDL_GPU_FILLMODE_FILL);
            if (new_fill == nullptr)
                return std::unexpected(SdlError("render mesh fill pipeline create"));
        }
        PipelineGuard fill_guard(device, new_fill);

        if (create_wireframe)
        {
            new_wireframe = CreateMeshPipeline(device, *created_vertex_shader,
                *created_fragment_shader, target_format, mesh_depth_format,
                SDL_GPU_FILLMODE_LINE);
            if (new_wireframe == nullptr)
                return std::unexpected(SdlError("render mesh wireframe pipeline create"));
        }
        PipelineGuard wireframe_guard(device, new_wireframe);

        // Textured fill variant (Gap-A). Fail-soft: if the backend has no DXIL
        // textured shader or pipeline creation fails, new_textured stays null and
        // textured draws fall back to the flat fill pipeline. Tied to the fill
        // pipeline lifetime since it is format-dependent.
        SDL_GPUGraphicsPipeline* new_textured = nullptr;
        if (create_fill)
        {
            SDL_GPUShader* textured_vertex = CreateTexturedMeshShader(device, true);
            SDL_GPUShader* textured_fragment = CreateTexturedMeshShader(device, false);
            if (textured_vertex != nullptr && textured_fragment != nullptr)
            {
                new_textured = CreateMeshPipeline(device, textured_vertex,
                    textured_fragment, target_format, mesh_depth_format,
                    SDL_GPU_FILLMODE_FILL);
            }
            if (textured_vertex != nullptr)
                SDL_ReleaseGPUShader(device, textured_vertex);
            if (textured_fragment != nullptr)
                SDL_ReleaseGPUShader(device, textured_fragment);
        }
        PipelineGuard textured_guard(device, new_textured);

        // Format-independent linear/repeat sampler, created once.
        if (mesh_sampler == nullptr)
        {
            const SDL_GPUSamplerCreateInfo sampler_info{
                .min_filter = SDL_GPU_FILTER_LINEAR,
                .mag_filter = SDL_GPU_FILTER_LINEAR,
                .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
                .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
            };
            mesh_sampler = SDL_CreateGPUSampler(device, &sampler_info);
        }

        if (!same_format && (mesh_fill_pipeline != nullptr || mesh_wireframe_pipeline != nullptr ||
                                mesh_textured_pipeline != nullptr))
        {
            if (!SDL_WaitForGPUIdle(device))
                return std::unexpected(SdlError("render mesh pipeline format transition idle wait"));
            if (mesh_fill_pipeline != nullptr)
                SDL_ReleaseGPUGraphicsPipeline(device, mesh_fill_pipeline);
            if (mesh_wireframe_pipeline != nullptr)
                SDL_ReleaseGPUGraphicsPipeline(device, mesh_wireframe_pipeline);
            if (mesh_textured_pipeline != nullptr)
                SDL_ReleaseGPUGraphicsPipeline(device, mesh_textured_pipeline);
            mesh_fill_pipeline = nullptr;
            mesh_wireframe_pipeline = nullptr;
            mesh_textured_pipeline = nullptr;
        }

        if (new_fill != nullptr)
        {
            mesh_fill_pipeline = new_fill;
            fill_guard.Dismiss();
        }
        if (new_wireframe != nullptr)
        {
            mesh_wireframe_pipeline = new_wireframe;
            wireframe_guard.Dismiss();
        }
        if (new_textured != nullptr)
        {
            mesh_textured_pipeline = new_textured;
            textured_guard.Dismiss();
        }
        mesh_pipeline_format = target_format;
    }

    if (new_color_buffer != nullptr)
    {
        mesh_color_buffer = new_color_buffer;
        color_guard.Dismiss();
    }
    if (new_color_transfer != nullptr)
    {
        mesh_color_transfer = new_color_transfer;
        color_transfer_guard.Dismiss();
    }
    return {};
}

std::expected<SdlGpuHost, std::string> SdlGpuHost::Create(
    const SdlPlatformService& platform, const bool debug_device,
    const runtime::RenderTexturePoolConfig texture_config,
    const runtime::RenderMeshPoolConfig mesh_config,
    const SdlGpuWindowIdentity window_identity)
{
    if (!platform.ready())
        return std::unexpected("SDL platform service is not ready");
    if (window_identity != SdlGpuWindowIdentity::NativeRuntime &&
        window_identity != SdlGpuWindowIdentity::DeveloperDiagnostics)
        return std::unexpected("SDL/GPU window identity is invalid");

    auto created_pool = runtime::RenderTexturePool::Create(texture_config);
    if (!created_pool)
        return std::unexpected(PoolError("render texture pool creation", created_pool.error()));

    auto created_mesh_pool = runtime::RenderMeshPool::Create(mesh_config);
    if (!created_mesh_pool)
        return std::unexpected(PoolError("render mesh pool creation", created_mesh_pool.error()));

    std::unique_ptr<Impl> impl;
    try
    {
        impl = std::make_unique<Impl>(
            std::move(*created_pool), std::move(*created_mesh_pool));
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected("render backend table allocation failed");
    }

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        return std::unexpected(SdlError("SDL_InitSubSystem(video)"));
    impl->subsystems_initialized = true;

    const char* const window_title =
        window_identity == SdlGpuWindowIdentity::DeveloperDiagnostics
            ? "OpenOmega - DEVELOPER DIAGNOSTICS"
            : "OpenOmega - native runtime";
    impl->window = SDL_CreateWindow(window_title, 1280, 720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (impl->window == nullptr)
        return std::unexpected(SdlError("SDL_CreateWindow"));

    constexpr SDL_GPUShaderFormat shader_formats = static_cast<SDL_GPUShaderFormat>(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL);
    impl->device = SDL_CreateGPUDevice(shader_formats, debug_device, nullptr);
    if (impl->device == nullptr)
        return std::unexpected(SdlError("SDL_CreateGPUDevice"));
    if (!SDL_ClaimWindowForGPUDevice(impl->device, impl->window))
        return std::unexpected(SdlError("SDL_ClaimWindowForGPUDevice"));
    impl->window_claimed = true;

    // Resolved once here so every mesh pipeline and every mesh depth texture
    // agree on the format for the life of the device.
    impl->mesh_depth_format = ResolveMeshDepthFormat(impl->device);

    const char* driver = SDL_GetGPUDeviceDriver(impl->device);
    impl->driver = driver != nullptr ? driver : "unknown";
    return SdlGpuHost(std::move(impl));
}

SdlGpuHost::SdlGpuHost(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

SdlGpuHost::~SdlGpuHost() = default;
SdlGpuHost::SdlGpuHost(SdlGpuHost&&) noexcept = default;

std::expected<runtime::RenderTextureHandle, std::string> SdlGpuHost::UploadRgba8Texture(
    const runtime::Rgba8TextureUploadView upload)
{
    try
    {
        auto reservation = impl_->texture_pool.Reserve(upload);
        if (!reservation)
            return std::unexpected(PoolError("render texture reserve", reservation.error()));
        ReservationRollbackGuard reservation_guard(impl_->texture_pool, *reservation);

        const std::uint32_t slot_index = reservation->handle.slot_index;
        if (slot_index >= impl_->texture_slots.size() ||
            impl_->texture_slots[slot_index] != nullptr)
        {
            return std::unexpected("render texture backend slot invariant failed");
        }
        if (upload.pixels.size() > std::numeric_limits<std::uint32_t>::max())
        {
            return std::unexpected(
                "render texture upload exceeds the SDL transfer-buffer size limit");
        }

        const SDL_GPUTextureCreateInfo texture_info{
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
            .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
            .width = reservation->width,
            .height = reservation->height,
            .layer_count_or_depth = 1,
            .num_levels = 1,
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
            .props = 0,
        };
        SDL_GPUTexture* texture = SDL_CreateGPUTexture(impl_->device, &texture_info);
        if (texture == nullptr)
            return std::unexpected(SdlError("render texture create"));
        TextureGuard texture_guard(impl_->device, texture);

        const SDL_GPUTransferBufferCreateInfo transfer_info{
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = static_cast<std::uint32_t>(upload.pixels.size()),
            .props = 0,
        };
        SDL_GPUTransferBuffer* transfer =
            SDL_CreateGPUTransferBuffer(impl_->device, &transfer_info);
        if (transfer == nullptr)
            return std::unexpected(SdlError("render texture transfer-buffer create"));
        TransferBufferGuard transfer_guard(impl_->device, transfer);

        void* mapped = SDL_MapGPUTransferBuffer(impl_->device, transfer, false);
        if (mapped == nullptr)
            return std::unexpected(SdlError("render texture transfer-buffer map"));
        std::memcpy(mapped, upload.pixels.data(), upload.pixels.size());
        SDL_UnmapGPUTransferBuffer(impl_->device, transfer);

        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(impl_->device);
        if (commands == nullptr)
            return std::unexpected(SdlError("render texture command-buffer acquire"));
        CommandBufferGuard command_guard(commands);

        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
        if (copy == nullptr)
        {
            std::string error = SdlError("render texture copy-pass begin");
            if (!command_guard.Cancel())
                error += "; " + SdlError("render texture command-buffer cancel");
            return std::unexpected(std::move(error));
        }

        const SDL_GPUTextureTransferInfo source{
            .transfer_buffer = transfer,
            .offset = 0,
            .pixels_per_row = reservation->width,
            .rows_per_layer = reservation->height,
        };
        const SDL_GPUTextureRegion destination{
            .texture = texture,
            .mip_level = 0,
            .layer = 0,
            .x = 0,
            .y = 0,
            .z = 0,
            .w = reservation->width,
            .h = reservation->height,
            .d = 1,
        };
        SDL_UploadToGPUTexture(copy, &source, &destination, false);
        SDL_EndGPUCopyPass(copy);
        if (!command_guard.Submit())
            return std::unexpected(SdlError("render texture command-buffer submit"));

        auto published = impl_->texture_pool.Publish(*reservation);
        if (!published)
        {
            std::string error = PoolError("render texture publish", published.error());
            if (auto idle = WaitForIdle(); !idle)
                error += "; " + idle.error();
            return std::unexpected(std::move(error));
        }

        impl_->texture_slots[slot_index] = texture;
        texture_guard.Dismiss();
        reservation_guard.Dismiss();
        SaturatingIncrement(impl_->successful_uploads);
        SaturatingAdd(impl_->successful_upload_logical_bytes, reservation->logical_bytes);
        return *published;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected("render texture upload error allocation failed");
    }
    catch (...)
    {
        return std::unexpected("render texture upload failed unexpectedly");
    }
}

std::expected<void, std::string> SdlGpuHost::UpdateRgba8Texture(
    const runtime::RenderTextureHandle& handle,
    const runtime::Rgba8TextureUploadView upload)
{
    try
    {
        auto metadata = impl_->texture_pool.Get(handle);
        if (!metadata)
        {
            if (!IsDefaultHandle(handle))
                SaturatingIncrement(impl_->rejected_nondefault_texture_handles);
            return std::unexpected(PoolError("render texture resolve for update", metadata.error()));
        }

        const std::uint32_t slot_index = metadata->handle.slot_index;
        if (slot_index >= impl_->texture_slots.size() ||
            impl_->texture_slots[slot_index] == nullptr)
        {
            SaturatingIncrement(impl_->rejected_nondefault_texture_handles);
            return std::unexpected("render texture backend slot invariant failed during update");
        }
        if (upload.width != metadata->width || upload.height != metadata->height ||
            upload.pixels.size() != metadata->logical_bytes)
        {
            return std::unexpected("render texture update view does not match resident metadata");
        }
        if (upload.pixels.size() > std::numeric_limits<std::uint32_t>::max())
        {
            return std::unexpected(
                "render texture update exceeds the SDL transfer-buffer size limit");
        }

        const SDL_GPUTransferBufferCreateInfo transfer_info{
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = static_cast<std::uint32_t>(upload.pixels.size()),
            .props = 0,
        };
        SDL_GPUTransferBuffer* transfer =
            SDL_CreateGPUTransferBuffer(impl_->device, &transfer_info);
        if (transfer == nullptr)
        {
            return std::unexpected(
                SdlError("render texture update transfer-buffer create"));
        }
        TransferBufferGuard transfer_guard(impl_->device, transfer);

        void* mapped = SDL_MapGPUTransferBuffer(impl_->device, transfer, false);
        if (mapped == nullptr)
            return std::unexpected(SdlError("render texture update transfer-buffer map"));
        std::memcpy(mapped, upload.pixels.data(), upload.pixels.size());
        SDL_UnmapGPUTransferBuffer(impl_->device, transfer);

        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(impl_->device);
        if (commands == nullptr)
            return std::unexpected(SdlError("render texture update command-buffer acquire"));
        CommandBufferGuard command_guard(commands);

        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
        if (copy == nullptr)
        {
            std::string error = SdlError("render texture update copy-pass begin");
            if (!command_guard.Cancel())
                error += "; " + SdlError("render texture update command-buffer cancel");
            return std::unexpected(std::move(error));
        }

        const SDL_GPUTextureTransferInfo source{
            .transfer_buffer = transfer,
            .offset = 0,
            .pixels_per_row = metadata->width,
            .rows_per_layer = metadata->height,
        };
        const SDL_GPUTextureRegion destination{
            .texture = impl_->texture_slots[slot_index],
            .mip_level = 0,
            .layer = 0,
            .x = 0,
            .y = 0,
            .z = 0,
            .w = metadata->width,
            .h = metadata->height,
            .d = 1,
        };
        SDL_UploadToGPUTexture(copy, &source, &destination, true);
        SDL_EndGPUCopyPass(copy);
        if (!command_guard.Submit())
            return std::unexpected(SdlError("render texture update command-buffer submit"));

        SaturatingIncrement(impl_->successful_updates);
        SaturatingAdd(impl_->successful_update_logical_bytes, metadata->logical_bytes);
        return {};
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected("render texture update error allocation failed");
    }
    catch (...)
    {
        return std::unexpected("render texture update failed unexpectedly");
    }
}

std::expected<void, std::string> SdlGpuHost::ReleaseTexture(
    const runtime::RenderTextureHandle& handle)
{
    return detail::InvokeSdlGpuExceptionBoundary(
        detail::kReleaseTextureExceptionMessages,
        [this, &handle]() -> std::expected<void, std::string>
        {
            auto metadata = impl_->texture_pool.Get(handle);
            if (!metadata)
            {
                if (!IsDefaultHandle(handle))
                    SaturatingIncrement(impl_->rejected_nondefault_texture_handles);
                return std::unexpected(
                    PoolError("render texture resolve for release", metadata.error()));
            }

            const std::uint32_t slot_index = metadata->handle.slot_index;
            if (slot_index >= impl_->texture_slots.size() ||
                impl_->texture_slots[slot_index] == nullptr)
            {
                SaturatingIncrement(impl_->rejected_nondefault_texture_handles);
                return std::unexpected(
                    "render texture backend slot invariant failed during release");
            }

            auto idle = WaitForIdle();
            if (!idle)
                return idle;

            auto released = impl_->texture_pool.Release(handle);
            if (!released)
                return std::unexpected(PoolError("render texture release", released.error()));

            SDL_ReleaseGPUTexture(impl_->device, impl_->texture_slots[slot_index]);
            impl_->texture_slots[slot_index] = nullptr;
            SaturatingIncrement(impl_->successful_releases);
            return {};
        });
}

std::expected<runtime::RenderMeshHandle, std::string> SdlGpuHost::UploadRenderMesh(
    const runtime::RenderMeshUploadView upload)
{
    try
    {
        auto reservation = impl_->mesh_pool.Reserve(upload);
        if (!reservation)
            return std::unexpected(PoolError("render mesh reserve", reservation.error()));
        MeshReservationRollbackGuard reservation_guard(impl_->mesh_pool, *reservation);

        const std::uint32_t slot_index = reservation->handle.slot_index;
        if (slot_index >= impl_->mesh_slots.size())
            return std::unexpected("render mesh backend slot invariant failed");
        const MeshBackendSlot& existing = impl_->mesh_slots[slot_index];
        if (existing.positions != nullptr || existing.triangle_indices != nullptr ||
            existing.uvs != nullptr || existing.position_count != 0U ||
            existing.triangle_index_count != 0U || existing.uv_count != 0U)
        {
            return std::unexpected("render mesh backend slot invariant failed");
        }

        constexpr std::uint64_t maximum_transfer_bytes =
            std::numeric_limits<std::uint32_t>::max();
        const std::uint64_t position_bytes =
            reservation->position_count * sizeof(asset::Float3IR);
        const std::uint64_t index_bytes =
            reservation->triangle_index_count * sizeof(std::uint32_t);
        // The UV buffer is sized from the POSITION count, not the uploaded UV count: a mesh with no
        // UVs still needs a bindable, correctly strided slot-2 buffer (zero-filled below), because
        // all three mesh pipelines declare the UV slot. So the transfer staging size is computed
        // here rather than reused from the pool's logical byte total, which counts real UVs only.
        const std::uint64_t uv_bytes = reservation->position_count * sizeof(MeshUvF);
        const std::uint64_t transfer_bytes = position_bytes + index_bytes + uv_bytes;
        if (position_bytes > maximum_transfer_bytes || index_bytes > maximum_transfer_bytes ||
            uv_bytes > maximum_transfer_bytes ||
            transfer_bytes > maximum_transfer_bytes ||
            reservation->logical_bytes > maximum_transfer_bytes ||
            reservation->position_count > std::numeric_limits<std::uint32_t>::max() ||
            reservation->triangle_index_count > std::numeric_limits<std::uint32_t>::max())
        {
            return std::unexpected(
                "render mesh upload exceeds the SDL buffer size limit");
        }

        const SDL_GPUBufferCreateInfo position_info{
            .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
            .size = static_cast<std::uint32_t>(position_bytes),
            .props = 0,
        };
        SDL_GPUBuffer* positions = SDL_CreateGPUBuffer(impl_->device, &position_info);
        if (positions == nullptr)
            return std::unexpected(SdlError("render mesh position buffer create"));
        BufferGuard position_guard(impl_->device, positions);

        const SDL_GPUBufferCreateInfo index_info{
            .usage = SDL_GPU_BUFFERUSAGE_INDEX,
            .size = static_cast<std::uint32_t>(index_bytes),
            .props = 0,
        };
        SDL_GPUBuffer* triangle_indices = SDL_CreateGPUBuffer(impl_->device, &index_info);
        if (triangle_indices == nullptr)
            return std::unexpected(SdlError("render mesh index buffer create"));
        BufferGuard index_guard(impl_->device, triangle_indices);

        const SDL_GPUBufferCreateInfo uv_info{
            .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
            .size = static_cast<std::uint32_t>(uv_bytes),
            .props = 0,
        };
        SDL_GPUBuffer* uvs = SDL_CreateGPUBuffer(impl_->device, &uv_info);
        if (uvs == nullptr)
            return std::unexpected(SdlError("render mesh uv buffer create"));
        BufferGuard uv_guard(impl_->device, uvs);

        const SDL_GPUTransferBufferCreateInfo transfer_info{
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = static_cast<std::uint32_t>(transfer_bytes),
            .props = 0,
        };
        SDL_GPUTransferBuffer* transfer =
            SDL_CreateGPUTransferBuffer(impl_->device, &transfer_info);
        if (transfer == nullptr)
            return std::unexpected(SdlError("render mesh transfer-buffer create"));
        TransferBufferGuard transfer_guard(impl_->device, transfer);

        void* mapped = SDL_MapGPUTransferBuffer(impl_->device, transfer, false);
        if (mapped == nullptr)
            return std::unexpected(SdlError("render mesh transfer-buffer map"));
        std::memcpy(mapped, upload.positions.data(), static_cast<std::size_t>(position_bytes));
        std::memcpy(static_cast<std::byte*>(mapped) +
                static_cast<std::size_t>(position_bytes),
            upload.triangle_indices.data(), static_cast<std::size_t>(index_bytes));
        std::byte* const uv_staging = static_cast<std::byte*>(mapped) +
            static_cast<std::size_t>(position_bytes) + static_cast<std::size_t>(index_bytes);
        if (upload.uvs.empty())
        {
            // No decoded UVs: fill the always-present slot-2 buffer with zeroes so the bind stays
            // valid. Such a mesh is drawn by the flat pipeline, which never reads the attribute.
            std::memset(uv_staging, 0, static_cast<std::size_t>(uv_bytes));
        }
        else
        {
            std::memcpy(
                uv_staging, upload.uvs.data(), static_cast<std::size_t>(uv_bytes));
        }
        SDL_UnmapGPUTransferBuffer(impl_->device, transfer);

        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(impl_->device);
        if (commands == nullptr)
            return std::unexpected(SdlError("render mesh command-buffer acquire"));
        CommandBufferGuard command_guard(commands);
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
        if (copy == nullptr)
        {
            std::string error = SdlError("render mesh copy-pass begin");
            if (!command_guard.Cancel())
                error += "; " + SdlError("render mesh command-buffer cancel");
            return std::unexpected(std::move(error));
        }

        const SDL_GPUTransferBufferLocation position_source{
            .transfer_buffer = transfer,
            .offset = 0U,
        };
        const SDL_GPUBufferRegion position_target{
            .buffer = positions,
            .offset = 0U,
            .size = static_cast<std::uint32_t>(position_bytes),
        };
        SDL_UploadToGPUBuffer(copy, &position_source, &position_target, false);
        const SDL_GPUTransferBufferLocation index_source{
            .transfer_buffer = transfer,
            .offset = static_cast<std::uint32_t>(position_bytes),
        };
        const SDL_GPUBufferRegion index_target{
            .buffer = triangle_indices,
            .offset = 0U,
            .size = static_cast<std::uint32_t>(index_bytes),
        };
        SDL_UploadToGPUBuffer(copy, &index_source, &index_target, false);
        const SDL_GPUTransferBufferLocation uv_source{
            .transfer_buffer = transfer,
            .offset = static_cast<std::uint32_t>(position_bytes + index_bytes),
        };
        const SDL_GPUBufferRegion uv_target{
            .buffer = uvs,
            .offset = 0U,
            .size = static_cast<std::uint32_t>(uv_bytes),
        };
        SDL_UploadToGPUBuffer(copy, &uv_source, &uv_target, false);
        SDL_EndGPUCopyPass(copy);
        if (!command_guard.Submit())
            return std::unexpected(SdlError("render mesh command-buffer submit"));

        auto published = impl_->mesh_pool.Publish(*reservation);
        if (!published)
        {
            std::string error = PoolError("render mesh publish", published.error());
            if (auto idle = WaitForIdle(); !idle)
                error += "; " + idle.error();
            return std::unexpected(std::move(error));
        }

        impl_->mesh_slots[slot_index] = MeshBackendSlot{
            .positions = positions,
            .triangle_indices = triangle_indices,
            .uvs = uvs,
            .position_count = static_cast<std::uint32_t>(reservation->position_count),
            .triangle_index_count =
                static_cast<std::uint32_t>(reservation->triangle_index_count),
            .uv_count = static_cast<std::uint32_t>(reservation->uv_count),
        };
        position_guard.Dismiss();
        index_guard.Dismiss();
        uv_guard.Dismiss();
        reservation_guard.Dismiss();
        SaturatingIncrement(impl_->successful_mesh_uploads);
        SaturatingAdd(
            impl_->successful_mesh_upload_logical_bytes, reservation->logical_bytes);
        return *published;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected("render mesh upload error allocation failed");
    }
    catch (...)
    {
        return std::unexpected("render mesh upload failed unexpectedly");
    }
}

std::expected<runtime::RenderMeshHandle, std::string> SdlGpuHost::UploadRenderMesh(
    const asset::RenderMeshIR& mesh)
{
    return UploadRenderMesh(runtime::RenderMeshUploadView{
        .positions = mesh.positions,
        .triangle_indices = mesh.triangle_indices,
    });
}

std::expected<void, std::string> SdlGpuHost::ReleaseRenderMesh(
    const runtime::RenderMeshHandle& handle)
{
    return detail::InvokeSdlGpuExceptionBoundary(
        detail::kReleaseMeshExceptionMessages,
        [this, &handle]() -> std::expected<void, std::string>
        {
            auto metadata = impl_->mesh_pool.Get(handle);
            if (!metadata)
            {
                if (!IsDefaultHandle(handle))
                    SaturatingIncrement(impl_->rejected_nondefault_mesh_handles);
                return std::unexpected(
                    PoolError("render mesh resolve for release", metadata.error()));
            }

            const std::uint32_t slot_index = metadata->handle.slot_index;
            if (slot_index >= impl_->mesh_slots.size())
            {
                SaturatingIncrement(impl_->rejected_nondefault_mesh_handles);
                return std::unexpected(
                    "render mesh backend slot invariant failed during release");
            }
            const MeshBackendSlot& mesh = impl_->mesh_slots[slot_index];
            if (mesh.positions == nullptr || mesh.triangle_indices == nullptr ||
                mesh.uvs == nullptr ||
                mesh.position_count != metadata->position_count ||
                mesh.triangle_index_count != metadata->triangle_index_count ||
                mesh.uv_count != metadata->uv_count)
            {
                SaturatingIncrement(impl_->rejected_nondefault_mesh_handles);
                return std::unexpected(
                    "render mesh backend slot invariant failed during release");
            }

            auto idle = WaitForIdle();
            if (!idle)
                return idle;
            auto released = impl_->mesh_pool.Release(handle);
            if (!released)
                return std::unexpected(PoolError("render mesh release", released.error()));

            SDL_ReleaseGPUBuffer(impl_->device, mesh.positions);
            SDL_ReleaseGPUBuffer(impl_->device, mesh.triangle_indices);
            SDL_ReleaseGPUBuffer(impl_->device, mesh.uvs);
            impl_->mesh_slots[slot_index] = {};
            SaturatingIncrement(impl_->successful_mesh_releases);
            return {};
        });
}

std::expected<void, std::string> SdlGpuHost::WaitForIdle()
{
    return detail::InvokeSdlGpuExceptionBoundary(detail::kWaitForIdleExceptionMessages,
        [this]() -> std::expected<void, std::string>
        {
            if (!SDL_WaitForGPUIdle(impl_->device))
                return std::unexpected(SdlError("SDL_WaitForGPUIdle"));
            return {};
        });
}

std::expected<std::array<runtime::RenderClearColorRgba8, 4U>, std::string>
SdlGpuHost::ReadbackClearForTesting(const runtime::RenderFramePacket& packet)
{
    try
    {
        if (!packet.draw_list.empty())
        {
            return std::unexpected("clear readback requires an empty draw list");
        }
        if (!packet.mesh_draw_list.empty())
            return std::unexpected("clear readback requires an empty mesh draw list");

        constexpr SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        constexpr std::uint32_t width = 2U;
        constexpr std::uint32_t height = 2U;
        constexpr std::size_t pixel_count =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        constexpr std::size_t byte_count =
            pixel_count * sizeof(runtime::RenderClearColorRgba8);
        static_assert(byte_count == 16U);
        if (!SDL_GPUTextureSupportsFormat(impl_->device, format,
                SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET))
        {
            return std::unexpected(
                "offscreen clear readback RGBA8 color target is unsupported");
        }
        if (SDL_GPUTextureFormatTexelBlockSize(format) != 4U)
        {
            return std::unexpected(
                "offscreen clear readback RGBA8 texel size is not four bytes");
        }

        const SDL_FColor clear_color = ToSdlClearColor(packet.clear_color);

        const SDL_GPUTextureCreateInfo texture_info{
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = format,
            .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
            .width = width,
            .height = height,
            .layer_count_or_depth = 1U,
            .num_levels = 1U,
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
            .props = 0,
        };
        SDL_GPUTexture* texture = SDL_CreateGPUTexture(impl_->device, &texture_info);
        if (texture == nullptr)
            return std::unexpected(SdlError("offscreen clear target create"));
        TextureGuard texture_guard(impl_->device, texture);

        const SDL_GPUTransferBufferCreateInfo transfer_info{
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
            .size = static_cast<std::uint32_t>(byte_count),
            .props = 0,
        };
        SDL_GPUTransferBuffer* transfer =
            SDL_CreateGPUTransferBuffer(impl_->device, &transfer_info);
        if (transfer == nullptr)
        {
            return std::unexpected(
                SdlError("offscreen clear download transfer-buffer create"));
        }
        TransferBufferGuard transfer_guard(impl_->device, transfer);

        std::string post_acquire_error;
        post_acquire_error.reserve(kPostAcquireErrorCapacity);

        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(impl_->device);
        if (commands == nullptr)
        {
            SetSdlErrorBounded(
                post_acquire_error, "offscreen clear command-buffer acquire");
            return std::unexpected(std::move(post_acquire_error));
        }
        CommandBufferGuard command_guard(commands);

        if (!RecordClearPass(commands, texture, clear_color))
        {
            SetSdlErrorBounded(post_acquire_error, "offscreen clear render-pass begin");
            if (!command_guard.Cancel())
            {
                AppendSdlErrorBounded(post_acquire_error,
                    "SDL_CancelGPUCommandBuffer after offscreen render-pass failure");
            }
            return std::unexpected(std::move(post_acquire_error));
        }

        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
        if (copy == nullptr)
        {
            SetSdlErrorBounded(post_acquire_error, "offscreen clear copy-pass begin");
            if (!command_guard.Cancel())
            {
                AppendSdlErrorBounded(post_acquire_error,
                    "SDL_CancelGPUCommandBuffer after offscreen copy-pass failure");
            }
            return std::unexpected(std::move(post_acquire_error));
        }

        const SDL_GPUTextureRegion source{
            .texture = texture,
            .mip_level = 0U,
            .layer = 0U,
            .x = 0U,
            .y = 0U,
            .z = 0U,
            .w = width,
            .h = height,
            .d = 1U,
        };
        const SDL_GPUTextureTransferInfo destination{
            .transfer_buffer = transfer,
            .offset = 0U,
            .pixels_per_row = 0U,
            .rows_per_layer = 0U,
        };
        SDL_DownloadFromGPUTexture(copy, &source, &destination);
        SDL_EndGPUCopyPass(copy);

        SDL_GPUFence* fence =
            SDL_SubmitGPUCommandBufferAndAcquireFence(command_guard.Take());
        if (fence == nullptr)
        {
            SetSdlErrorBounded(post_acquire_error,
                "offscreen clear command-buffer submit and fence acquire");
            return std::unexpected(std::move(post_acquire_error));
        }
        FenceGuard fence_guard(impl_->device, fence);

        SDL_GPUFence* fence_to_wait = fence;
        if (!SDL_WaitForGPUFences(impl_->device, true, &fence_to_wait, 1U))
        {
            SetSdlErrorBounded(post_acquire_error, "offscreen clear fence wait");
            return std::unexpected(std::move(post_acquire_error));
        }

        void* mapped = SDL_MapGPUTransferBuffer(impl_->device, transfer, false);
        if (mapped == nullptr)
        {
            SetSdlErrorBounded(
                post_acquire_error, "offscreen clear download transfer-buffer map");
            return std::unexpected(std::move(post_acquire_error));
        }
        TransferBufferMapGuard map_guard(impl_->device, transfer, mapped);

        const auto* bytes = static_cast<const std::byte*>(mapped);
        std::array<runtime::RenderClearColorRgba8, pixel_count> pixels{};
        for (std::size_t index = 0U; index < pixels.size(); ++index)
        {
            const std::size_t offset = index * 4U;
            pixels[index] = runtime::RenderClearColorRgba8{
                .red = std::to_integer<std::uint8_t>(bytes[offset + 0U]),
                .green = std::to_integer<std::uint8_t>(bytes[offset + 1U]),
                .blue = std::to_integer<std::uint8_t>(bytes[offset + 2U]),
                .alpha = std::to_integer<std::uint8_t>(bytes[offset + 3U]),
            };
        }
        return pixels;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected("offscreen clear readback error allocation failed");
    }
    catch (...)
    {
        return std::unexpected("offscreen clear readback failed unexpectedly");
    }
}

std::expected<std::array<runtime::RenderClearColorRgba8, 16U>, std::string>
SdlGpuHost::ReadbackBlitsForTesting(const runtime::RenderFramePacket& packet)
{
    try
    {
        if (!packet.mesh_draw_list.empty())
            return std::unexpected("blit readback requires an empty mesh draw list");
        const std::span<const runtime::RenderTextureBlitCommand> draw_commands =
            packet.draw_list.commands();
        if (draw_commands.empty())
            return std::unexpected("blit readback requires a nonempty draw list");

        constexpr SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        constexpr std::uint32_t width = 4U;
        constexpr std::uint32_t height = 4U;
        constexpr std::size_t pixel_count =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        constexpr std::size_t byte_count =
            pixel_count * sizeof(runtime::RenderClearColorRgba8);
        static_assert(pixel_count == 16U);
        static_assert(byte_count == 64U);

        std::array<ResolvedTextureBlit,
            runtime::kMaximumRenderTextureBlitsPerFrame> resolved_blits{};
        std::array<runtime::RenderSourceRectPixels,
            runtime::kMaximumRenderTextureBlitsPerFrame> mapped_sources{};
        std::array<SDL_GPUFilter,
            runtime::kMaximumRenderTextureBlitsPerFrame> mapped_filters{};
        std::array<PreparedTextureBlit,
            runtime::kMaximumRenderTextureBlitsPerFrame> prepared_blits{};

        // Keep the test seam counter-neutral while retaining production's complete-handle
        // preflight and backend-slot invariants before any SDL resource or command work.
        for (std::size_t index = 0U; index < draw_commands.size(); ++index)
        {
            const runtime::RenderTextureBlitCommand& draw = draw_commands[index];
            auto metadata = impl_->texture_pool.Get(draw.texture);
            if (!metadata)
            {
                return std::unexpected(
                    PoolError("offscreen blit draw texture resolve", metadata.error()));
            }

            const std::uint32_t slot_index = metadata->handle.slot_index;
            if (slot_index >= impl_->texture_slots.size() ||
                impl_->texture_slots[slot_index] == nullptr)
            {
                return std::unexpected(
                    "offscreen blit draw texture backend slot invariant failed");
            }
            resolved_blits[index] = ResolvedTextureBlit{
                .texture = impl_->texture_slots[slot_index],
                .width = metadata->width,
                .height = metadata->height,
            };
        }

        // Map the complete source/filter set only after all generations resolve.
        for (std::size_t index = 0U; index < draw_commands.size(); ++index)
        {
            const runtime::RenderTextureBlitCommand& draw = draw_commands[index];
            const ResolvedTextureBlit& resolved = resolved_blits[index];
            auto source = runtime::MapTextureSourceRect(
                draw.source, resolved.width, resolved.height);
            if (!source)
            {
                return std::unexpected(std::string(
                    "offscreen blit source rectangle mapping failed: ") +
                    std::string(source.error().message));
            }
            mapped_sources[index] = *source;

            if (!TryMapTextureFilter(draw.filter_mode, mapped_filters[index]))
                return std::unexpected("offscreen blit texture filter mode is invalid");
        }

        // The offscreen extent is fixed, so every destination can also be planned before
        // target creation or command acquisition. No accepted prefix can reach the GPU.
        for (std::size_t index = 0U; index < draw_commands.size(); ++index)
        {
            const runtime::RenderTextureBlitCommand& draw = draw_commands[index];
            auto plan = runtime::PlanTextureBlit(mapped_sources[index],
                draw.destination, draw.fit_mode, width, height);
            if (!plan)
            {
                return std::unexpected(std::string(
                    "offscreen blit texture planning failed: ") +
                    std::string(plan.error().message));
            }
            prepared_blits[index] = PreparedTextureBlit{
                .source = resolved_blits[index].texture,
                .plan = *plan,
                .filter = mapped_filters[index],
            };
        }
        const std::span<const PreparedTextureBlit> active_blits{
            prepared_blits.data(), draw_commands.size()};

        if (!SDL_GPUTextureSupportsFormat(impl_->device, format,
                SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET))
        {
            return std::unexpected(
                "offscreen blit readback RGBA8 color target is unsupported");
        }
        if (SDL_GPUTextureFormatTexelBlockSize(format) != 4U)
        {
            return std::unexpected(
                "offscreen blit readback RGBA8 texel size is not four bytes");
        }

        const SDL_FColor clear_color = ToSdlClearColor(packet.clear_color);
        const SDL_GPUTextureCreateInfo texture_info{
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = format,
            .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
            .width = width,
            .height = height,
            .layer_count_or_depth = 1U,
            .num_levels = 1U,
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
            .props = 0,
        };
        SDL_GPUTexture* texture = SDL_CreateGPUTexture(impl_->device, &texture_info);
        if (texture == nullptr)
            return std::unexpected(SdlError("offscreen blit target create"));
        TextureGuard texture_guard(impl_->device, texture);

        const SDL_GPUTransferBufferCreateInfo transfer_info{
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
            .size = static_cast<std::uint32_t>(byte_count),
            .props = 0,
        };
        SDL_GPUTransferBuffer* transfer =
            SDL_CreateGPUTransferBuffer(impl_->device, &transfer_info);
        if (transfer == nullptr)
        {
            return std::unexpected(
                SdlError("offscreen blit download transfer-buffer create"));
        }
        TransferBufferGuard transfer_guard(impl_->device, transfer);

        std::string post_acquire_error;
        post_acquire_error.reserve(kPostAcquireErrorCapacity);

        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(impl_->device);
        if (commands == nullptr)
        {
            SetSdlErrorBounded(
                post_acquire_error, "offscreen blit command-buffer acquire");
            return std::unexpected(std::move(post_acquire_error));
        }
        CommandBufferGuard command_guard(commands);

        if (!RecordClearPass(commands, texture, clear_color))
        {
            SetSdlErrorBounded(post_acquire_error, "offscreen blit render-pass begin");
            if (!command_guard.Cancel())
            {
                AppendSdlErrorBounded(post_acquire_error,
                    "SDL_CancelGPUCommandBuffer after offscreen blit render-pass failure");
            }
            return std::unexpected(std::move(post_acquire_error));
        }
        RecordTextureBlits(commands, texture, active_blits);

        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
        if (copy == nullptr)
        {
            SetSdlErrorBounded(post_acquire_error, "offscreen blit copy-pass begin");
            if (!command_guard.Cancel())
            {
                AppendSdlErrorBounded(post_acquire_error,
                    "SDL_CancelGPUCommandBuffer after offscreen blit copy-pass failure");
            }
            return std::unexpected(std::move(post_acquire_error));
        }

        const SDL_GPUTextureRegion source{
            .texture = texture,
            .mip_level = 0U,
            .layer = 0U,
            .x = 0U,
            .y = 0U,
            .z = 0U,
            .w = width,
            .h = height,
            .d = 1U,
        };
        const SDL_GPUTextureTransferInfo destination{
            .transfer_buffer = transfer,
            .offset = 0U,
            .pixels_per_row = 0U,
            .rows_per_layer = 0U,
        };
        SDL_DownloadFromGPUTexture(copy, &source, &destination);
        SDL_EndGPUCopyPass(copy);

        SDL_GPUFence* fence =
            SDL_SubmitGPUCommandBufferAndAcquireFence(command_guard.Take());
        if (fence == nullptr)
        {
            SetSdlErrorBounded(post_acquire_error,
                "offscreen blit command-buffer submit and fence acquire");
            return std::unexpected(std::move(post_acquire_error));
        }
        FenceGuard fence_guard(impl_->device, fence);

        SDL_GPUFence* fence_to_wait = fence;
        if (!SDL_WaitForGPUFences(impl_->device, true, &fence_to_wait, 1U))
        {
            SetSdlErrorBounded(post_acquire_error, "offscreen blit fence wait");
            return std::unexpected(std::move(post_acquire_error));
        }

        void* mapped = SDL_MapGPUTransferBuffer(impl_->device, transfer, false);
        if (mapped == nullptr)
        {
            SetSdlErrorBounded(
                post_acquire_error, "offscreen blit download transfer-buffer map");
            return std::unexpected(std::move(post_acquire_error));
        }
        TransferBufferMapGuard map_guard(impl_->device, transfer, mapped);

        const auto* bytes = static_cast<const std::byte*>(mapped);
        std::array<runtime::RenderClearColorRgba8, pixel_count> pixels{};
        for (std::size_t index = 0U; index < pixels.size(); ++index)
        {
            const std::size_t offset = index * 4U;
            pixels[index] = runtime::RenderClearColorRgba8{
                .red = std::to_integer<std::uint8_t>(bytes[offset + 0U]),
                .green = std::to_integer<std::uint8_t>(bytes[offset + 1U]),
                .blue = std::to_integer<std::uint8_t>(bytes[offset + 2U]),
                .alpha = std::to_integer<std::uint8_t>(bytes[offset + 3U]),
            };
        }
        return pixels;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected("offscreen blit readback error allocation failed");
    }
    catch (...)
    {
        return std::unexpected("offscreen blit readback failed unexpectedly");
    }
}

std::expected<std::vector<runtime::RenderClearColorRgba8>, std::string>
SdlGpuHost::ReadbackFrameRgba8(const runtime::RenderFramePacket& packet,
    const std::uint32_t width, const std::uint32_t height)
{
    try
    {
        if (width == 0U || height == 0U ||
            width > kMaximumFrameReadbackWidth ||
            height > kMaximumFrameReadbackHeight)
        {
            return std::unexpected("frame readback extent is outside the hard limit");
        }
        const std::span<const runtime::RenderTextureBlitCommand> texture_draws =
            packet.draw_list.commands();
        const std::span<const runtime::RenderMeshDrawCommand> mesh_draws =
            packet.mesh_draw_list.commands();

        std::array<ResolvedMeshDraw,
            runtime::kMaximumRenderMeshDrawsPerFrame> resolved_draws{};
        std::array<MeshColorRgbF,
            runtime::kMaximumRenderMeshDrawsPerFrame> colors{};
        bool need_fill = false;
        bool need_wireframe = false;
        for (std::size_t index = 0U; index < mesh_draws.size(); ++index)
        {
            const runtime::RenderMeshDrawCommand& draw = mesh_draws[index];
            auto metadata = impl_->mesh_pool.Get(draw.mesh);
            if (!metadata)
            {
                return std::unexpected(
                    PoolError("mesh readback draw resolve", metadata.error()));
            }
            const std::uint32_t slot_index = metadata->handle.slot_index;
            if (slot_index >= impl_->mesh_slots.size())
                return std::unexpected("mesh readback backend slot invariant failed");
            const MeshBackendSlot& mesh = impl_->mesh_slots[slot_index];
            if (mesh.positions == nullptr || mesh.triangle_indices == nullptr ||
                mesh.uvs == nullptr ||
                mesh.position_count != metadata->position_count ||
                mesh.triangle_index_count != metadata->triangle_index_count ||
                mesh.uv_count != metadata->uv_count)
                return std::unexpected("mesh readback backend slot invariant failed");
            resolved_draws[index] = ResolvedMeshDraw{
                .positions = mesh.positions,
                .triangle_indices = mesh.triangle_indices,
                .uvs = mesh.uvs,
                .triangle_index_count = mesh.triangle_index_count,
                .texture = impl_->ResolveTextureSlot(draw.texture),
            };
            colors[index] = ToMeshColor(draw.color);
            switch (draw.raster_mode)
            {
            case runtime::RenderMeshRasterMode::Fill:
                need_fill = true;
                break;
            case runtime::RenderMeshRasterMode::Wireframe:
                need_wireframe = true;
                break;
            default:
                return std::unexpected("mesh readback raster mode is invalid");
            }
        }

        constexpr SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        const std::size_t pixel_count =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        const std::size_t byte_count =
            pixel_count * sizeof(runtime::RenderClearColorRgba8);
        if (pixel_count > kMaximumFrameReadbackPixelCount ||
            byte_count > kMaximumFrameReadbackByteCount ||
            byte_count > std::numeric_limits<std::uint32_t>::max())
        {
            return std::unexpected("frame readback byte count is outside the hard limit");
        }

        std::array<ResolvedTextureBlit,
            runtime::kMaximumRenderTextureBlitsPerFrame> resolved_blits{};
        std::array<runtime::RenderSourceRectPixels,
            runtime::kMaximumRenderTextureBlitsPerFrame> mapped_sources{};
        std::array<SDL_GPUFilter,
            runtime::kMaximumRenderTextureBlitsPerFrame> mapped_filters{};
        std::array<PreparedTextureBlit,
            runtime::kMaximumRenderTextureBlitsPerFrame> prepared_blits{};
        for (std::size_t index = 0U; index < texture_draws.size(); ++index)
        {
            const runtime::RenderTextureBlitCommand& draw = texture_draws[index];
            auto metadata = impl_->texture_pool.Get(draw.texture);
            if (!metadata)
            {
                return std::unexpected(
                    PoolError("mesh readback overlay resolve", metadata.error()));
            }
            const std::uint32_t slot_index = metadata->handle.slot_index;
            if (slot_index >= impl_->texture_slots.size() ||
                impl_->texture_slots[slot_index] == nullptr)
            {
                return std::unexpected(
                    "mesh readback overlay backend slot invariant failed");
            }
            resolved_blits[index] = ResolvedTextureBlit{
                .texture = impl_->texture_slots[slot_index],
                .width = metadata->width,
                .height = metadata->height,
            };
        }
        for (std::size_t index = 0U; index < texture_draws.size(); ++index)
        {
            const runtime::RenderTextureBlitCommand& draw = texture_draws[index];
            const ResolvedTextureBlit& resolved = resolved_blits[index];
            auto source = runtime::MapTextureSourceRect(
                draw.source, resolved.width, resolved.height);
            if (!source)
            {
                return std::unexpected(std::string(
                    "mesh readback overlay source rectangle mapping failed: ") +
                    std::string(source.error().message));
            }
            mapped_sources[index] = *source;
            if (!TryMapTextureFilter(draw.filter_mode, mapped_filters[index]))
                return std::unexpected("mesh readback overlay filter mode is invalid");

            auto plan = runtime::PlanTextureBlit(
                mapped_sources[index], draw.destination, draw.fit_mode, width, height);
            if (!plan)
            {
                return std::unexpected(std::string(
                    "mesh readback overlay planning failed: ") +
                    std::string(plan.error().message));
            }
            prepared_blits[index] = PreparedTextureBlit{
                .source = resolved.texture,
                .plan = *plan,
                .filter = mapped_filters[index],
            };
        }
        // Complete the only output-sized allocation before pipeline/resource
        // creation or command acquisition. Every post-acquisition operation is
        // then fixed-capacity and counter-neutral.
        std::vector<runtime::RenderClearColorRgba8> pixels(pixel_count);
        if (!SDL_GPUTextureSupportsFormat(impl_->device, format,
                SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET))
        {
            return std::unexpected(
                "mesh readback RGBA8 color target is unsupported");
        }
        if (SDL_GPUTextureFormatTexelBlockSize(format) != 4U)
            return std::unexpected("mesh readback RGBA8 texel size is not four bytes");
        SDL_GPUTexture* mesh_depth = nullptr;
        if (!mesh_draws.empty())
        {
            auto pipelines =
                impl_->EnsureMeshPipelines(
                    format, need_fill, need_wireframe, false);
            if (!pipelines)
                return std::unexpected(std::move(pipelines.error()));
            // The readback target extent is known here, before acquisition, so
            // the depth target is created alongside the pipelines and the
            // post-acquisition path stays allocation-free.
            if (!impl_->EnsureMeshDepthTexture(width, height, mesh_depth))
                return std::unexpected(SdlError("mesh readback depth target create"));
        }

        const SDL_GPUTextureCreateInfo texture_info{
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = format,
            .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
            .width = width,
            .height = height,
            .layer_count_or_depth = 1U,
            .num_levels = 1U,
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
            .props = 0,
        };
        SDL_GPUTexture* texture = SDL_CreateGPUTexture(impl_->device, &texture_info);
        if (texture == nullptr)
            return std::unexpected(SdlError("mesh readback target create"));
        TextureGuard texture_guard(impl_->device, texture);

        const SDL_GPUTransferBufferCreateInfo download_info{
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
            .size = static_cast<std::uint32_t>(byte_count),
            .props = 0,
        };
        SDL_GPUTransferBuffer* download =
            SDL_CreateGPUTransferBuffer(impl_->device, &download_info);
        if (download == nullptr)
            return std::unexpected(SdlError("mesh readback download transfer-buffer create"));
        TransferBufferGuard download_guard(impl_->device, download);

        const std::size_t color_bytes =
            mesh_draws.size() * sizeof(MeshColorRgbF);
        SDL_GPUTransferBuffer* color_transfer = nullptr;
        TransferBufferGuard color_guard(impl_->device, nullptr);
        if (!mesh_draws.empty())
        {
            const SDL_GPUTransferBufferCreateInfo color_info{
                .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                .size = static_cast<std::uint32_t>(color_bytes),
                .props = 0,
            };
            color_transfer =
                SDL_CreateGPUTransferBuffer(impl_->device, &color_info);
            if (color_transfer == nullptr)
            {
                return std::unexpected(
                    SdlError("frame readback color transfer-buffer create"));
            }
            color_guard.Reset(color_transfer);
            void* color_map =
                SDL_MapGPUTransferBuffer(impl_->device, color_transfer, false);
            if (color_map == nullptr)
            {
                return std::unexpected(
                    SdlError("frame readback color transfer-buffer map"));
            }
            std::memcpy(color_map, colors.data(), color_bytes);
            SDL_UnmapGPUTransferBuffer(impl_->device, color_transfer);
        }

        std::string post_acquire_error;
        post_acquire_error.reserve(kPostAcquireErrorCapacity);
        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(impl_->device);
        if (commands == nullptr)
        {
            SetSdlErrorBounded(
                post_acquire_error, "mesh readback command-buffer acquire");
            return std::unexpected(std::move(post_acquire_error));
        }
        CommandBufferGuard command_guard(commands);

        if (!mesh_draws.empty() &&
            !RecordMeshColorUpload(commands, color_transfer,
                impl_->mesh_color_buffer,
                static_cast<std::uint32_t>(color_bytes)))
        {
            SetSdlErrorBounded(
                post_acquire_error, "mesh readback color copy-pass begin");
            if (!command_guard.Cancel())
            {
                AppendSdlErrorBounded(post_acquire_error,
                    "SDL_CancelGPUCommandBuffer after mesh readback color upload failure");
            }
            return std::unexpected(std::move(post_acquire_error));
        }

        const bool recorded_target = mesh_draws.empty()
            ? RecordClearPass(
                  commands, texture, ToSdlClearColor(packet.clear_color))
            : RecordMeshPass(commands, texture,
                  ToSdlClearColor(packet.clear_color), mesh_draws,
                  std::span<const ResolvedMeshDraw>{
                      resolved_draws.data(), mesh_draws.size()},
                  impl_->mesh_color_buffer, impl_->mesh_fill_pipeline,
                  impl_->mesh_wireframe_pipeline, impl_->mesh_textured_pipeline,
                  impl_->mesh_sampler, mesh_depth, impl_->mesh_depth_format);
        if (!recorded_target)
        {
            SetSdlErrorBounded(post_acquire_error, "mesh readback render-pass begin");
            if (!command_guard.Cancel())
            {
                AppendSdlErrorBounded(post_acquire_error,
                    "SDL_CancelGPUCommandBuffer after mesh readback render-pass failure");
            }
            return std::unexpected(std::move(post_acquire_error));
        }

        RecordTextureBlits(commands, texture,
            std::span<const PreparedTextureBlit>{
                prepared_blits.data(), texture_draws.size()});

        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
        if (copy == nullptr)
        {
            SetSdlErrorBounded(post_acquire_error, "mesh readback copy-pass begin");
            if (!command_guard.Cancel())
            {
                AppendSdlErrorBounded(post_acquire_error,
                    "SDL_CancelGPUCommandBuffer after mesh readback copy-pass failure");
            }
            return std::unexpected(std::move(post_acquire_error));
        }
        const SDL_GPUTextureRegion source{
            .texture = texture,
            .mip_level = 0U,
            .layer = 0U,
            .x = 0U,
            .y = 0U,
            .z = 0U,
            .w = width,
            .h = height,
            .d = 1U,
        };
        const SDL_GPUTextureTransferInfo destination{
            .transfer_buffer = download,
            .offset = 0U,
            .pixels_per_row = 0U,
            .rows_per_layer = 0U,
        };
        SDL_DownloadFromGPUTexture(copy, &source, &destination);
        SDL_EndGPUCopyPass(copy);

        SDL_GPUFence* fence =
            SDL_SubmitGPUCommandBufferAndAcquireFence(command_guard.Take());
        if (fence == nullptr)
        {
            SetSdlErrorBounded(post_acquire_error,
                "mesh readback command-buffer submit and fence acquire");
            return std::unexpected(std::move(post_acquire_error));
        }
        FenceGuard fence_guard(impl_->device, fence);
        SDL_GPUFence* fence_to_wait = fence;
        if (!SDL_WaitForGPUFences(impl_->device, true, &fence_to_wait, 1U))
        {
            SetSdlErrorBounded(post_acquire_error, "mesh readback fence wait");
            return std::unexpected(std::move(post_acquire_error));
        }

        void* mapped = SDL_MapGPUTransferBuffer(impl_->device, download, false);
        if (mapped == nullptr)
        {
            SetSdlErrorBounded(
                post_acquire_error, "mesh readback download transfer-buffer map");
            return std::unexpected(std::move(post_acquire_error));
        }
        TransferBufferMapGuard map_guard(impl_->device, download, mapped);
        const auto* bytes = static_cast<const std::byte*>(mapped);
        for (std::size_t index = 0U; index < pixels.size(); ++index)
        {
            const std::size_t offset = index * 4U;
            pixels[index] = runtime::RenderClearColorRgba8{
                .red = std::to_integer<std::uint8_t>(bytes[offset + 0U]),
                .green = std::to_integer<std::uint8_t>(bytes[offset + 1U]),
                .blue = std::to_integer<std::uint8_t>(bytes[offset + 2U]),
                .alpha = std::to_integer<std::uint8_t>(bytes[offset + 3U]),
            };
        }
        return pixels;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected("frame readback error allocation failed");
    }
    catch (...)
    {
        return std::unexpected("frame readback failed unexpectedly");
    }
}

std::expected<std::vector<runtime::RenderClearColorRgba8>, std::string>
SdlGpuHost::CaptureFrameRgba8(const runtime::RenderFramePacket& packet)
{
    return ReadbackFrameRgba8(packet, kMaximumFrameReadbackWidth,
        kMaximumFrameReadbackHeight);
}

std::expected<std::array<runtime::RenderClearColorRgba8, 64U>, std::string>
SdlGpuHost::ReadbackMeshesForTesting(const runtime::RenderFramePacket& packet)
{
    if (packet.mesh_draw_list.empty())
        return std::unexpected("mesh readback requires a nonempty mesh draw list");
    auto pixels = ReadbackFrameRgba8(packet, 8U, 8U);
    if (!pixels)
        return std::unexpected(std::move(pixels.error()));
    if (pixels->size() != 64U)
        return std::unexpected("mesh readback pixel count invariant failed");
    std::array<runtime::RenderClearColorRgba8, 64U> fixed_pixels{};
    std::ranges::copy(*pixels, fixed_pixels.begin());
    return fixed_pixels;
}

std::expected<void, std::string> SdlGpuHost::RenderFrame(
    const runtime::RenderFramePacket& packet)
{
    try
    {
        const std::span<const runtime::RenderTextureBlitCommand> texture_draws =
            packet.draw_list.commands();
        const std::span<const runtime::RenderMeshDrawCommand> mesh_draws =
            packet.mesh_draw_list.commands();
        std::array<ResolvedTextureBlit,
            runtime::kMaximumRenderTextureBlitsPerFrame> resolved_blits{};
        std::array<runtime::RenderSourceRectPixels,
            runtime::kMaximumRenderTextureBlitsPerFrame> mapped_sources{};
        std::array<SDL_GPUFilter,
            runtime::kMaximumRenderTextureBlitsPerFrame> mapped_filters{};
        std::array<PreparedTextureBlit,
            runtime::kMaximumRenderTextureBlitsPerFrame> prepared_blits{};
        std::array<ResolvedMeshDraw,
            runtime::kMaximumRenderMeshDrawsPerFrame> resolved_mesh_draws{};
        std::array<MeshColorRgbF,
            runtime::kMaximumRenderMeshDrawsPerFrame> mesh_colors{};

        // Resolve both complete handle sets before interpreting any remaining command fields. A
        // stale later generation can never permit partial validation or any GPU-side prefix work.
        for (std::size_t index = 0U; index < texture_draws.size(); ++index)
        {
            const runtime::RenderTextureBlitCommand& draw = texture_draws[index];
            auto metadata = impl_->texture_pool.Get(draw.texture);
            if (!metadata)
            {
                SaturatingIncrement(impl_->rejected_nondefault_texture_handles);
                return std::unexpected(
                    PoolError("render frame draw texture resolve", metadata.error()));
            }

            const std::uint32_t slot_index = metadata->handle.slot_index;
            if (slot_index >= impl_->texture_slots.size() ||
                impl_->texture_slots[slot_index] == nullptr)
            {
                SaturatingIncrement(impl_->rejected_nondefault_texture_handles);
                return std::unexpected(
                    "render frame draw texture backend slot invariant failed");
            }
            resolved_blits[index] = ResolvedTextureBlit{
                .texture = impl_->texture_slots[slot_index],
                .width = metadata->width,
                .height = metadata->height,
            };
        }

        bool need_fill_pipeline = false;
        bool need_wireframe_pipeline = false;
        for (std::size_t index = 0U; index < mesh_draws.size(); ++index)
        {
            const runtime::RenderMeshDrawCommand& draw = mesh_draws[index];
            auto metadata = impl_->mesh_pool.Get(draw.mesh);
            if (!metadata)
            {
                SaturatingIncrement(impl_->rejected_nondefault_mesh_handles);
                return std::unexpected(
                    PoolError("render frame draw mesh resolve", metadata.error()));
            }
            const std::uint32_t slot_index = metadata->handle.slot_index;
            if (slot_index >= impl_->mesh_slots.size())
            {
                SaturatingIncrement(impl_->rejected_nondefault_mesh_handles);
                return std::unexpected(
                    "render frame draw mesh backend slot invariant failed");
            }
            const MeshBackendSlot& mesh = impl_->mesh_slots[slot_index];
            if (mesh.positions == nullptr || mesh.triangle_indices == nullptr ||
                mesh.uvs == nullptr ||
                mesh.position_count != metadata->position_count ||
                mesh.triangle_index_count != metadata->triangle_index_count ||
                mesh.uv_count != metadata->uv_count)
            {
                SaturatingIncrement(impl_->rejected_nondefault_mesh_handles);
                return std::unexpected(
                    "render frame draw mesh backend slot invariant failed");
            }
            resolved_mesh_draws[index] = ResolvedMeshDraw{
                .positions = mesh.positions,
                .triangle_indices = mesh.triangle_indices,
                .uvs = mesh.uvs,
                .triangle_index_count = mesh.triangle_index_count,
                .texture = impl_->ResolveTextureSlot(draw.texture),
            };
            mesh_colors[index] = ToMeshColor(draw.color);
            switch (draw.raster_mode)
            {
            case runtime::RenderMeshRasterMode::Fill:
                need_fill_pipeline = true;
                break;
            case runtime::RenderMeshRasterMode::Wireframe:
                need_wireframe_pipeline = true;
                break;
            default:
                return std::unexpected("render frame mesh raster mode is invalid");
            }
        }

        // Convert every normalized source crop and backend filter before acquiring GPU work.
        // These fixed arrays are the only command-specific storage used after acquisition.
        for (std::size_t index = 0U; index < texture_draws.size(); ++index)
        {
            const runtime::RenderTextureBlitCommand& draw = texture_draws[index];
            const ResolvedTextureBlit& resolved = resolved_blits[index];
            auto source = runtime::MapTextureSourceRect(
                draw.source, resolved.width, resolved.height);
            if (!source)
            {
                return std::unexpected(std::string(
                    "render frame source rectangle mapping failed: ") +
                    std::string(source.error().message));
            }
            mapped_sources[index] = *source;

            if (!TryMapTextureFilter(draw.filter_mode, mapped_filters[index]))
                return std::unexpected("render frame texture filter mode is invalid");
        }

        if (!mesh_draws.empty())
        {
            const SDL_GPUTextureFormat target_format =
                SDL_GetGPUSwapchainTextureFormat(impl_->device, impl_->window);
            auto pipelines = impl_->EnsureMeshPipelines(
                target_format, need_fill_pipeline, need_wireframe_pipeline, true);
            if (!pipelines)
                return pipelines;

            const std::size_t color_bytes = mesh_draws.size() * sizeof(MeshColorRgbF);
            void* mapped = SDL_MapGPUTransferBuffer(
                impl_->device, impl_->mesh_color_transfer, true);
            if (mapped == nullptr)
            {
                return std::unexpected(
                    SdlError("render frame mesh color transfer-buffer map"));
            }
            std::memcpy(mapped, mesh_colors.data(), color_bytes);
            SDL_UnmapGPUTransferBuffer(
                impl_->device, impl_->mesh_color_transfer);
        }

        // Convert the project-owned packet value before acquiring GPU work. The
        // post-acquisition path retains only this fixed SDL value.
        const SDL_FColor clear_color = ToSdlClearColor(packet.clear_color);

        // Reserve all error storage before acquiring the command buffer. The active command
        // path below uses only fixed-capacity values and bounded writes into this existing buffer.
        std::string& post_acquire_error = impl_->render_frame_error_scratch;
        post_acquire_error.clear();
        post_acquire_error.reserve(kPostAcquireErrorCapacity);

        SDL_GPUCommandBuffer* gpu_commands = SDL_AcquireGPUCommandBuffer(impl_->device);
        if (gpu_commands == nullptr)
        {
            SetSdlErrorBounded(post_acquire_error, "SDL_AcquireGPUCommandBuffer");
            return std::unexpected(std::move(post_acquire_error));
        }
        CommandBufferGuard command_guard(gpu_commands);

        SDL_GPUTexture* swapchain = nullptr;
        std::uint32_t width = 0U;
        std::uint32_t height = 0U;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(
                gpu_commands, impl_->window, &swapchain, &width, &height))
        {
            SetSdlErrorBounded(
                post_acquire_error, "SDL_WaitAndAcquireGPUSwapchainTexture");
            if (!command_guard.Cancel())
            {
                AppendSdlErrorBounded(post_acquire_error,
                    "SDL_CancelGPUCommandBuffer after acquire failure");
            }
            return std::unexpected(std::move(post_acquire_error));
        }
        command_guard.SubmitOnUnwind();

        FrameSubmissionKind submission_kind = FrameSubmissionKind::UnavailableSwapchain;
        if (swapchain != nullptr && width != 0U && height != 0U)
        {
            if (texture_draws.empty() && mesh_draws.empty())
            {
                submission_kind = FrameSubmissionKind::Clear;
            }
            else
            {
                // Plan the complete frame before recording the clear or any source-order blit.
                // A planning failure therefore submits an empty acquired buffer, with no visible
                // prefix and no successful-frame counters.
                for (std::size_t index = 0U; index < texture_draws.size(); ++index)
                {
                    const runtime::RenderTextureBlitCommand& draw = texture_draws[index];
                    auto plan = runtime::PlanTextureBlit(mapped_sources[index],
                        draw.destination, draw.fit_mode, width, height);
                    if (!plan)
                    {
                        post_acquire_error.clear();
                        AppendBounded(post_acquire_error,
                            "render frame texture blit planning failed: ");
                        AppendBounded(post_acquire_error, plan.error().message);
                        if (!command_guard.Submit())
                        {
                            AppendSdlErrorBounded(post_acquire_error,
                                "command-buffer submit after blit planning failure");
                        }
                        return std::unexpected(std::move(post_acquire_error));
                    }
                    prepared_blits[index] = PreparedTextureBlit{
                        .source = resolved_blits[index].texture,
                        .plan = *plan,
                        .filter = mapped_filters[index],
                    };
                }
                submission_kind = texture_draws.empty()
                                      ? FrameSubmissionKind::Mesh
                                      : FrameSubmissionKind::Blit;
            }

            if (!mesh_draws.empty())
            {
                const std::uint32_t color_bytes = static_cast<std::uint32_t>(
                    mesh_draws.size() * sizeof(MeshColorRgbF));
                if (!RecordMeshColorUpload(gpu_commands, impl_->mesh_color_transfer,
                        impl_->mesh_color_buffer, color_bytes))
                {
                    SetSdlErrorBounded(post_acquire_error,
                        "render frame mesh color copy-pass begin");
                    if (!command_guard.Submit())
                    {
                        AppendSdlErrorBounded(post_acquire_error,
                            "command-buffer submit after mesh color upload failure");
                    }
                    return std::unexpected(std::move(post_acquire_error));
                }
            }

            // The swapchain extent is only known after acquisition, so the mesh
            // depth target is sized here; EnsureMeshDepthTexture recreates it
            // whenever the window resizes. Failure follows the same
            // submit-the-acquired-buffer-then-report path as the neighbouring
            // post-acquisition failures.
            SDL_GPUTexture* mesh_depth = nullptr;
            if (!mesh_draws.empty() &&
                !impl_->EnsureMeshDepthTexture(width, height, mesh_depth))
            {
                SetSdlErrorBounded(
                    post_acquire_error, "render frame mesh depth target create");
                if (!command_guard.Submit())
                {
                    AppendSdlErrorBounded(post_acquire_error,
                        "command-buffer submit after mesh depth target failure");
                }
                return std::unexpected(std::move(post_acquire_error));
            }

            const bool recorded_target = mesh_draws.empty()
                                             ? RecordClearPass(
                                                   gpu_commands, swapchain, clear_color)
                                             : RecordMeshPass(gpu_commands, swapchain,
                                                   clear_color, mesh_draws,
                                                   std::span<const ResolvedMeshDraw>{
                                                       resolved_mesh_draws.data(),
                                                       mesh_draws.size()},
                                                   impl_->mesh_color_buffer,
                                                   impl_->mesh_fill_pipeline,
                                                   impl_->mesh_wireframe_pipeline,
                                                   impl_->mesh_textured_pipeline,
                                                   impl_->mesh_sampler, mesh_depth,
                                                   impl_->mesh_depth_format);
            if (!recorded_target)
            {
                SetSdlErrorBounded(post_acquire_error, "SDL_BeginGPURenderPass");
                if (!command_guard.Submit())
                {
                    AppendSdlErrorBounded(post_acquire_error,
                        "command-buffer submit after render-pass failure");
                }
                return std::unexpected(std::move(post_acquire_error));
            }

            RecordTextureBlits(gpu_commands, swapchain,
                std::span<const PreparedTextureBlit>{
                    prepared_blits.data(), texture_draws.size()});
        }

        if (!command_guard.Submit())
        {
            SetSdlErrorBounded(post_acquire_error, "SDL_SubmitGPUCommandBuffer");
            return std::unexpected(std::move(post_acquire_error));
        }

        SaturatingIncrement(impl_->frame_submissions);
        switch (submission_kind)
        {
        case FrameSubmissionKind::Blit:
            SaturatingIncrement(impl_->blit_submissions);
            SaturatingAdd(impl_->successful_blit_draws,
                static_cast<std::uint64_t>(texture_draws.size()));
            if (!mesh_draws.empty())
            {
                SaturatingIncrement(impl_->mesh_submissions);
                SaturatingAdd(impl_->successful_mesh_draws,
                    static_cast<std::uint64_t>(mesh_draws.size()));
            }
            break;
        case FrameSubmissionKind::Mesh:
            SaturatingIncrement(impl_->mesh_submissions);
            SaturatingAdd(impl_->successful_mesh_draws,
                static_cast<std::uint64_t>(mesh_draws.size()));
            break;
        case FrameSubmissionKind::Clear:
            SaturatingIncrement(impl_->clear_submissions);
            break;
        case FrameSubmissionKind::UnavailableSwapchain:
            SaturatingIncrement(impl_->unavailable_swapchain_submissions);
            break;
        }
        return {};
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected("render frame error allocation failed");
    }
    catch (...)
    {
        return std::unexpected("render frame failed unexpectedly");
    }
}

runtime::RenderTexturePoolSnapshot SdlGpuHost::TextureSnapshot() const noexcept
{
    return impl_->texture_pool.Snapshot();
}

runtime::RenderMeshPoolSnapshot SdlGpuHost::MeshSnapshot() const noexcept
{
    return impl_->mesh_pool.Snapshot();
}

GpuHostSnapshot SdlGpuHost::Snapshot() const noexcept
{
    return GpuHostSnapshot{
        .textures = impl_->texture_pool.Snapshot(),
        .meshes = impl_->mesh_pool.Snapshot(),
        .successful_uploads = impl_->successful_uploads,
        .successful_upload_logical_bytes = impl_->successful_upload_logical_bytes,
        .successful_updates = impl_->successful_updates,
        .successful_update_logical_bytes = impl_->successful_update_logical_bytes,
        .successful_releases = impl_->successful_releases,
        .frame_submissions = impl_->frame_submissions,
        .blit_submissions = impl_->blit_submissions,
        .successful_blit_draws = impl_->successful_blit_draws,
        .clear_submissions = impl_->clear_submissions,
        .unavailable_swapchain_submissions = impl_->unavailable_swapchain_submissions,
        .rejected_nondefault_texture_handles =
            impl_->rejected_nondefault_texture_handles,
        .successful_mesh_uploads = impl_->successful_mesh_uploads,
        .successful_mesh_upload_logical_bytes =
            impl_->successful_mesh_upload_logical_bytes,
        .successful_mesh_releases = impl_->successful_mesh_releases,
        .mesh_submissions = impl_->mesh_submissions,
        .successful_mesh_draws = impl_->successful_mesh_draws,
        .rejected_nondefault_mesh_handles =
            impl_->rejected_nondefault_mesh_handles,
    };
}

std::string_view SdlGpuHost::driver_name() const noexcept
{
    return impl_->driver;
}
} // namespace omega::app
