#include "omega_app.h"
#include "diagnostic_actor_marker.h"
#include "opening_movie_player.h"
#include "opening_movie_safety.h"
#include "run_replay_session.h"
#include "screenshot_capture.h"

#include "omega/gameplay/debug_locomotion.h"
#include "omega/gameplay/character_controller.h"
#include "omega/gameplay/minsk_mission.h"
#include "omega/gameplay/objective_tracker.h"
#include "omega/debug/subsystem_entry_break.h"
#include "omega/frontend_presentation/retail_front_end_frame.h"
#include "omega/retail/frontend_tdx_decoder.h"
#include "omega/runtime/diagnostic_actor_scene.h"
#include "omega/runtime/level_texture_topology_preview.h"
#include "omega/runtime/free_fly_camera.h"
#include "omega/runtime/scene_transform.h"
#include "omega/runtime/spatial_diagnostic_scene.h"

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace omega::app
{
namespace
{
// Refill when the 4,096-frame ring has at least this much space. The queued lead therefore stays
// between roughly 53 and 85 ms at 48 kHz during steady playback.
constexpr std::uint64_t kOpeningMovieAudioRefillFrames = 1'536U;
static_assert(kOpeningMovieAudioRefillFrames <=
              SdlAudioService::kOpeningMovieQueueCapacityFrames);
static_assert(kOpeningMovieAudioClockRateHz ==
              static_cast<std::uint64_t>(SdlAudioService::kSampleRate));
constexpr profiles::ProfileId kFirstProfileId = profiles::ProfileId::FromBytes(
    std::array<std::uint8_t, 16U>{0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
                                   0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U});
constexpr profiles::CharacterId kFirstCharacterId =
    profiles::CharacterId::FromBytes(
        std::array<std::uint8_t, 16U>{0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
                                      0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U});
constexpr std::array<std::byte, 4U> kDiagnosticActorMarkerRgba8{
    std::byte{255U}, std::byte{64U}, std::byte{224U}, std::byte{255U}};
constexpr runtime::RenderMeshColorRgba8 kDiagnosticActorMeshColor{
    .red = 255U,
    .green = 64U,
    .blue = 224U,
    .alpha = 255U,
};
// Combat S2: a dead actor -- the player or an enemy -- is drawn dark slate,
// deliberately distinct from the live player's magenta kDiagnosticActorMeshColor
// and from every NPC stealth-state colour, so a death is observable on screen.
constexpr runtime::RenderMeshColorRgba8 kDeadActorMeshColor{
    .red = 56U,
    .green = 56U,
    .blue = 72U,
    .alpha = 255U,
};

class DiagnosticSceneRollbackGuard final
{
public:
    DiagnosticSceneRollbackGuard(SdlGpuHost& host,
        std::array<runtime::RenderMeshHandle,
            runtime::kMaximumRenderMeshDrawsPerFrame>& handles,
        std::size_t& count) noexcept
        : host_(&host), handles_(&handles), count_(&count)
    {
    }

    ~DiagnosticSceneRollbackGuard() noexcept
    {
        if (!active_)
            return;
        while (*count_ != 0U)
        {
            --*count_;
            const runtime::RenderMeshHandle handle = (*handles_)[*count_];
            (*handles_)[*count_] = {};
            try
            {
                static_cast<void>(host_->ReleaseRenderMesh(handle));
            }
            catch (...)
            {
                // The local host remains authoritative and retries any surviving backend resource
                // when startup unwinds.
            }
        }
    }

    void Dismiss() noexcept { active_ = false; }

private:
    SdlGpuHost* host_ = nullptr;
    std::array<runtime::RenderMeshHandle,
        runtime::kMaximumRenderMeshDrawsPerFrame>* handles_ = nullptr;
    std::size_t* count_ = nullptr;
    bool active_ = true;
};

// Kinematic-player presentation constants + helpers (OPENOMEGA_PLAYER=1). The
// player is a world-space mesh at its float CharacterState.position projected
// through the same camera as the level; a Z-up look-at follows it.
constexpr float kPlayerStepDt = 1.0F / 60.0F;
constexpr float kPlayerCullRadius = 60.0F; // broadphase radius around the player
constexpr float kPlayerMeshScale = 12.0F;  // world-space size of the player marker
constexpr float kPlayerCamBack = 90.0F;    // follow-camera offset along -Y
constexpr float kPlayerCamHeight = 70.0F;  // follow-camera offset along +Z

[[nodiscard]] asset::Matrix4x4IR PlayerFollowView(
    const asset::Float3IR& p, const float radius) noexcept
{
    const asset::Float3IR eye{
        .x = p.x, .y = p.y - kPlayerCamBack, .z = p.z + kPlayerCamHeight};
    const asset::Float3IR target{.x = p.x, .y = p.y, .z = p.z + radius};
    return runtime::LookAtViewMatrix(
        eye, target, asset::Float3IR{.x = 0.0F, .y = 0.0F, .z = 1.0F});
}

[[nodiscard]] asset::Matrix4x4IR PlayerMeshTransform(
    const asset::Float3IR& p) noexcept
{
    asset::Matrix4x4IR m = asset::kIdentityMatrix4x4IR;
    m.row_major[0] = kPlayerMeshScale;
    m.row_major[5] = kPlayerMeshScale;
    m.row_major[10] = kPlayerMeshScale;
    m.row_major[3] = p.x;
    m.row_major[7] = p.y;
    m.row_major[11] = p.z;
    return m;
}

// Combat S2 player-weapon wiring constants.
//
// The player fires from eye height along the follow camera's own aim. That
// camera has a fixed orientation (eye at -Y/+Z of the player, looking back down
// at it) and the host pointer sample drives only the flat 2D target/fire cue, so
// the follow view IS this build's aim representation and there is no second one:
// the aim is the horizontal component of its eye->target vector, which is world
// +Y for every player position. It is flattened because the camera looks DOWN at
// the player -- the unflattened forward would drive every shot into the floor.
constexpr asset::Float3IR kPlayerAimDirection{.x = 0.0F, .y = 1.0F, .z = 0.0F};
// The muzzle sits at the same eye height the NPC vision ray already uses
// (NpcVisionParams::eye_height, along the world up-axis) rather than a second,
// invented constant.
constexpr float kPlayerEyeHeight = gameplay::NpcVisionParams{}.eye_height;

// The enemy NPC marker is drawn a bit larger than the player so it reads clearly
// against the dense level from the follow camera.
constexpr float kNpcMeshScale = 20.0F;
// Hitscan target sphere for an enemy: the size its marker is actually drawn at,
// so what is shot at is what is seen. A PROJECT value -- per-actor hit shapes
// live with the authentic character data, not in the .SO.
constexpr float kNpcHitRadius = kNpcMeshScale;
// The player's shot reaches exactly as far as the pre-culled collision set the
// occlusion test runs against (kPlayerCullRadius around the player), so a shot
// can never travel past geometry that was culled away and therefore could not
// have blocked it. Also a PROJECT value.
constexpr float kPlayerFireRange = kPlayerCullRadius;
[[nodiscard]] asset::Matrix4x4IR NpcMeshTransform(
    const asset::Float3IR& p) noexcept
{
    asset::Matrix4x4IR m = asset::kIdentityMatrix4x4IR;
    m.row_major[0] = kNpcMeshScale;
    m.row_major[5] = kNpcMeshScale;
    m.row_major[10] = kNpcMeshScale;
    m.row_major[3] = p.x;
    m.row_major[7] = p.y;
    m.row_major[11] = p.z;
    return m;
}

// Renders the objective HUD panel (the 88x72 slate + DrawObjectiveHudOnto) from
// the current objective state and uploads it. Returns a default (invalid) handle
// on upload failure. Used both at scene build and on a live objective change
// (a player trigger firing) to refresh the panel over the 3D level.
[[nodiscard]] runtime::RenderTextureHandle BuildObjectiveHudPanelTexture(
    SdlGpuHost& host, const gameplay::MissionData& mission,
    const gameplay::ObjectiveState& state)
{
    std::vector<ObjectiveHudEntry> entries;
    std::uint32_t complete = 0U;
    for (std::size_t index = 0U; index < mission.objectives.size(); ++index)
    {
        std::uint8_t status_code = 0U;
        switch (state.status[index])
        {
        case gameplay::ObjectiveStatus::Active:
            status_code = 1U;
            break;
        case gameplay::ObjectiveStatus::Complete:
            status_code = 2U;
            ++complete;
            break;
        case gameplay::ObjectiveStatus::Failed:
            status_code = 3U;
            break;
        case gameplay::ObjectiveStatus::Inactive:
            break;
        }
        if (status_code != 0U)
            entries.push_back(
                ObjectiveHudEntry{mission.objectives[index].id, status_code});
    }
    constexpr std::uint32_t kHudPanelWidth = 88U;
    constexpr std::uint32_t kHudPanelHeight = 72U;
    std::vector<std::byte> pixels(
        static_cast<std::size_t>(kHudPanelWidth) * kHudPanelHeight * 4U);
    for (std::size_t i = 0U; i + 3U < pixels.size(); i += 4U)
    {
        pixels[i] = std::byte{16};
        pixels[i + 1U] = std::byte{20};
        pixels[i + 2U] = std::byte{28};
        pixels[i + 3U] = std::byte{255};
    }
    runtime::DebugImage panel{
        .width = kHudPanelWidth,
        .height = kHudPanelHeight,
        .rgba8_pixels = std::move(pixels),
    };
    DrawObjectiveHudOnto(panel, entries, complete,
        static_cast<std::uint32_t>(mission.objectives.size()));
    auto uploaded = host.UploadRgba8Texture(runtime::Rgba8TextureUploadView{
        .width = panel.width,
        .height = panel.height,
        .pixels = panel.pixels(),
    });
    return uploaded ? *uploaded : runtime::RenderTextureHandle{};
}
} // namespace

std::expected<std::unique_ptr<OmegaApp::DiagnosticScenePresentation>, std::string>
OmegaApp::BuildDiagnosticScenePresentation(
    SdlGpuHost& host, const asset::SceneIR& scene,
    const content::LevelTextureStore* const level_texture_store,
    const content::GameDataService* const game_data,
    const std::optional<runtime::FreeFlyPose> free_fly_pose,
    const float free_fly_move_speed, const runtime::FreeFlyInput free_fly_script,
    const std::optional<gameplay::CharacterState> player_seed,
    const gameplay::CharacterControllerParams player_params,
    std::vector<gameplay::CollisionTriangle> player_collision)
{
    static_assert(sizeof(std::unique_ptr<DiagnosticScenePresentation>) <
                  sizeof(DiagnosticScenePresentation));
    if (scene.render_meshes.empty() != scene.mesh_instances.empty())
    {
        return std::unexpected(
            std::string{"diagnostic scene mesh and instance ownership is inconsistent"});
    }
    constexpr std::size_t maximum_environment_meshes =
        runtime::kMaximumRenderMeshDrawsPerFrame - 1U;
    if (scene.render_meshes.size() > maximum_environment_meshes ||
        scene.mesh_instances.size() > maximum_environment_meshes)
    {
        return std::unexpected(
            std::string{"diagnostic scene exceeds renderer command capacity"});
    }

    std::array<asset::Matrix4x4IR,
        runtime::kMaximumRenderMeshDrawsPerFrame> object_to_clip{};
    for (std::size_t instance_index = 0U;
         instance_index < scene.mesh_instances.size(); ++instance_index)
    {
        const asset::SceneMeshInstanceIR& instance =
            scene.mesh_instances[instance_index];
        if (instance.render_mesh_index >= scene.render_meshes.size())
        {
            return std::unexpected(
                std::string{"diagnostic scene instance references an unavailable mesh"});
        }
        auto composed = runtime::ComposeObjectToClip(
            scene.camera, instance.local_to_world);
        if (!composed)
        {
            return std::unexpected(
                std::string{"diagnostic scene transform is non-finite"});
        }
        object_to_clip[instance_index] = *composed;
    }

    std::unique_ptr<DiagnosticScenePresentation> presentation{
        new (std::nothrow) DiagnosticScenePresentation{}};
    if (!presentation)
    {
        return std::unexpected(
            std::string{"diagnostic scene presentation allocation failed"});
    }
    if (scene.render_meshes.empty())
    {
        return std::expected<std::unique_ptr<DiagnosticScenePresentation>, std::string>{
            std::in_place, std::move(presentation)};
    }

    auto actor_mesh = runtime::BuildProjectDiagnosticActorMesh();
    if (!actor_mesh)
    {
        return std::unexpected("diagnostic actor mesh creation failed: " +
                               std::string(actor_mesh.error()));
    }
    const auto actor_object_to_clip = runtime::ComposeObjectToClip(
        scene.camera,
        PlanProjectDiagnosticActorMeshTransform(simulation::Position3{}));
    if (!actor_object_to_clip)
    {
        return std::unexpected(
            std::string{"diagnostic scene transform is non-finite"});
    }
    presentation->camera = scene.camera;
    if (free_fly_pose)
    {
        presentation->free_fly_active = true;
        presentation->free_fly_pose = *free_fly_pose;
        presentation->free_fly_move_speed =
            std::isfinite(free_fly_move_speed) && free_fly_move_speed > 0.0F
                ? free_fly_move_speed
                : 1.0F;
        presentation->free_fly_script = free_fly_script;
    }
    if (player_seed)
    {
        presentation->player_active = true;
        presentation->player_state = *player_seed;
        presentation->player_params = player_params;
        presentation->player_collision = std::move(player_collision);
    }

    DiagnosticSceneRollbackGuard rollback(
        host, presentation->mesh_handles, presentation->mesh_count);
    for (const asset::RenderMeshIR& mesh : scene.render_meshes)
    {
        auto uploaded = host.UploadRenderMesh(mesh);
        if (!uploaded)
        {
            return std::unexpected(
                "diagnostic scene mesh upload failed: " + uploaded.error());
        }
        presentation->mesh_handles[presentation->mesh_count++] = *uploaded;
    }

    // Textured level slice: bind ONE real level TDX albedo texture, decoded to
    // RGBA8, to every environment mesh. The mesh pixel shader triplanar-maps it
    // from world position, so no per-vertex UVs are needed here (real per-vertex
    // UVs are the VUM visual decode, Path B; per-material texture binding is a
    // separate RE-blocked follow-up). The level texture store deliberately omits
    // display expansion, so we pull the raw member bytes and run the front-end
    // TDX decoder + indexed->RGBA8 expansion. Fail-soft: fall back to a procedural
    // dev grid if no level texture is available or decodable.
    bool bound_real_level_texture = false;
    if (level_texture_store != nullptr && game_data != nullptr)
    {
        const content::LevelTextureStore& store = *level_texture_store;
        const content::GameDataService& game_data_service = *game_data;
        const std::size_t store_size = store.size();
        const std::size_t attempt_limit = store_size < 16U ? store_size : 16U;
        for (std::size_t attempt = 0U;
             attempt < attempt_limit && !bound_real_level_texture; ++attempt)
        {
            const auto handle = store.HandleAt(attempt);
            if (!handle)
                continue;
            const auto raw_bytes = store.LoadRawBytes(game_data_service, *handle);
            if (!raw_bytes)
                continue;
            auto decoded = retail::DecodeTdxFrontEnd(*raw_bytes);
            if (!decoded)
                decoded = retail::DecodeTdxScopedFrontEnd(*raw_bytes);
            if (!decoded)
                continue;
            const auto rgba = retail::ExpandIndexedImageToRgba8(decoded->image);
            if (!rgba)
                continue;
            const auto uploaded_texture =
                host.UploadRgba8Texture(runtime::Rgba8TextureUploadView{
                    .width = rgba->width,
                    .height = rgba->height,
                    .pixels =
                        std::as_bytes(std::span<const std::uint8_t>(rgba->pixels)),
                });
            if (uploaded_texture)
            {
                presentation->environment_texture = *uploaded_texture;
                bound_real_level_texture = true;
            }
        }
    }
    if (!bound_real_level_texture)
    {
        constexpr std::uint32_t kTextureExtent = 128U;
        std::vector<std::uint8_t> pixels(
            static_cast<std::size_t>(kTextureExtent) * kTextureExtent * 4U, 0U);
        for (std::uint32_t y = 0U; y < kTextureExtent; ++y)
        {
            for (std::uint32_t x = 0U; x < kTextureExtent; ++x)
            {
                const bool grid_line = (x % 16U == 0U) || (y % 16U == 0U);
                const bool checker = (((x / 16U) + (y / 16U)) % 2U) == 0U;
                const std::uint8_t base = checker ? 225U : 165U;
                const std::uint8_t value = grid_line ? 70U : base;
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * kTextureExtent + x) * 4U;
                pixels[offset + 0U] = value;
                pixels[offset + 1U] = static_cast<std::uint8_t>(value * 7U / 8U);
                pixels[offset + 2U] = static_cast<std::uint8_t>(value * 11U / 16U);
                pixels[offset + 3U] = 255U;
            }
        }
        auto uploaded_texture =
            host.UploadRgba8Texture(runtime::Rgba8TextureUploadView{
                .width = kTextureExtent,
                .height = kTextureExtent,
                .pixels = std::as_bytes(std::span<const std::uint8_t>(pixels)),
            });
        if (uploaded_texture)
            presentation->environment_texture = *uploaded_texture;
    }

    std::array<runtime::RenderMeshDrawCommand,
        runtime::kMaximumRenderMeshDrawsPerFrame> commands{};
    for (std::size_t instance_index = 0U;
         instance_index < scene.mesh_instances.size(); ++instance_index)
    {
        const asset::SceneMeshInstanceIR& instance =
            scene.mesh_instances[instance_index];
        presentation->environment_local_to_world[instance_index] =
            instance.local_to_world;
        commands[instance_index] = runtime::RenderMeshDrawCommand{
            .mesh = presentation->mesh_handles[instance.render_mesh_index],
            .object_to_clip = object_to_clip[instance_index],
            .color = runtime::RenderMeshColorRgba8{
                .red = 112U,
                .green = 220U,
                .blue = 255U,
                .alpha = 255U,
            },
            .raster_mode = runtime::RenderMeshRasterMode::Fill,
            .texture = presentation->environment_texture,
        };
    }
    auto created_environment_draw_list = runtime::RenderMeshDrawList::Create(
        std::span<const runtime::RenderMeshDrawCommand>{
            commands.data(), scene.mesh_instances.size()});
    if (!created_environment_draw_list)
    {
        return std::unexpected("diagnostic scene draw-list creation failed: " +
                               std::string(runtime::RenderMeshDrawListErrorCodeName(
                                   created_environment_draw_list.error().code)));
    }
    presentation->environment_command_count = scene.mesh_instances.size();
    presentation->environment_draw_list =
        std::move(*created_environment_draw_list);

    auto uploaded_actor = host.UploadRenderMesh(*actor_mesh);
    if (!uploaded_actor)
    {
        return std::unexpected(
            "diagnostic actor mesh upload failed: " + uploaded_actor.error());
    }
    presentation->actor_mesh_handle = *uploaded_actor;
    presentation->mesh_handles[presentation->mesh_count++] = *uploaded_actor;
    commands[scene.mesh_instances.size()] = runtime::RenderMeshDrawCommand{
        .mesh = presentation->actor_mesh_handle,
        .object_to_clip = *actor_object_to_clip,
        .color = kDiagnosticActorMeshColor,
        .raster_mode = runtime::RenderMeshRasterMode::Fill,
    };
    auto created_draw_list = runtime::RenderMeshDrawList::Create(
        std::span<const runtime::RenderMeshDrawCommand>{
            commands.data(), scene.mesh_instances.size() + 1U});
    if (!created_draw_list)
    {
        return std::unexpected("diagnostic scene draw-list creation failed: " +
                               std::string(runtime::RenderMeshDrawListErrorCodeName(
                                   created_draw_list.error().code)));
    }
    presentation->draw_list = std::move(*created_draw_list);
    rollback.Dismiss();
    return std::expected<std::unique_ptr<DiagnosticScenePresentation>, std::string>{
        std::in_place, std::move(presentation)};
}

std::expected<OmegaApp, std::string> OmegaApp::Create(runtime::ConfigStore config,
    const runtime::RuntimeSettings& settings, runtime::ContentStartupState content,
    NativePersistence native_persistence, const bool debug_device,
    const runtime::FrontEndPresentationMode presentation_mode,
    std::optional<std::filesystem::path> opening_movie_path)
{
    OMEGA_DEBUG_BREAK_SUBSYSTEM_ENTRY("omega_app_host");
    return CreateWithTextureConfig(std::move(config), settings, std::move(content),
        std::make_unique<NativePersistence>(std::move(native_persistence)),
        debug_device, {}, std::move(opening_movie_path), presentation_mode);
}

std::expected<OmegaApp, std::string> OmegaApp::Create(runtime::ConfigStore config,
    const runtime::RuntimeSettings& settings, runtime::ContentStartupState content,
    NativePersistence native_persistence, const bool debug_device,
    const runtime::FrontEndPresentationMode presentation_mode,
    asset::OpeningMovieSource opening_movie_source)
{
    return CreateWithTextureConfigAndOpeningMoviePlayback(std::move(config), settings,
        std::move(content),
        std::make_unique<NativePersistence>(std::move(native_persistence)), debug_device, {},
        std::nullopt,
        std::optional<asset::OpeningMovieSource>{std::move(opening_movie_source)}, nullptr,
        presentation_mode);
}

std::expected<OmegaApp, std::string> OmegaApp::CreateWithTextureConfig(
    runtime::ConfigStore config, const runtime::RuntimeSettings& settings,
    runtime::ContentStartupState content,
    std::unique_ptr<NativePersistence> native_persistence, const bool debug_device,
    const runtime::RenderTexturePoolConfig texture_config,
    std::optional<std::filesystem::path> opening_movie_path,
    const runtime::FrontEndPresentationMode presentation_mode)
{
    return CreateWithTextureConfigAndOpeningMoviePlayback(std::move(config),
        settings, std::move(content), std::move(native_persistence), debug_device,
        texture_config, std::move(opening_movie_path), std::nullopt, nullptr,
        presentation_mode);
}

std::expected<OmegaApp, std::string>
OmegaApp::CreateWithTextureConfigAndOpeningMoviePlayback(
    runtime::ConfigStore config, const runtime::RuntimeSettings& settings,
    runtime::ContentStartupState content,
    std::unique_ptr<NativePersistence> native_persistence, const bool debug_device,
    const runtime::RenderTexturePoolConfig texture_config,
    std::optional<std::filesystem::path> opening_movie_path,
    std::optional<asset::OpeningMovieSource> opening_movie_source,
    std::unique_ptr<OpeningMoviePlayback> opening_movie_playback,
    const runtime::FrontEndPresentationMode presentation_mode)
{
    if (presentation_mode != runtime::FrontEndPresentationMode::RetailRequired &&
        presentation_mode !=
            runtime::FrontEndPresentationMode::DeveloperDiagnostics)
    {
        return std::unexpected(
            std::string{"front-end presentation mode is invalid"});
    }

    const unsigned source_selection_count = static_cast<unsigned>(opening_movie_path.has_value()) +
        static_cast<unsigned>(opening_movie_source.has_value()) +
        static_cast<unsigned>(opening_movie_playback != nullptr);
    if (source_selection_count > 1U)
    {
        return std::unexpected(
            std::string{"opening movie source selection is ambiguous"});
    }

    const auto classified_content = runtime::ClassifyContentStartupState(content);
    if (!classified_content)
    {
        return std::unexpected(
            std::string("content startup state: inconsistent-ownership"));
    }
    const runtime::ContentStartupStage content_stage = *classified_content;

    FrontEndStartupModel front_end_startup_model{};
    if (native_persistence != nullptr)
    {
        const auto projected = MakeFrontEndStartupModel(native_persistence->startup_profiles());
        if (!projected)
        {
            return std::unexpected("front-end startup model: " +
                                   std::string(FrontEndModelErrorMessage(projected.error())));
        }
        front_end_startup_model = *projected;
    }

    auto config_owner = std::make_unique<runtime::ConfigStore>(std::move(config));
    auto content_owner = std::make_unique<runtime::ContentStartupState>(std::move(content));
    auto stderr_sink = std::make_unique<runtime::StderrLogSink>();

    auto created_ring = runtime::RingLogSink::Create(settings.log_ring_capacity);
    if (!created_ring)
        return std::unexpected("log ring: " + created_ring.error());
    auto ring_sink = std::move(*created_ring);

    auto created_log = runtime::LogService::Create(runtime::LogServiceConfig{
        .minimum_severity = settings.minimum_log_severity,
        .max_category_bytes = 32U,
        .max_message_bytes = 512U,
        .sinks = {stderr_sink.get(), ring_sink.get()},
    });
    if (!created_log)
        return std::unexpected("logging service: " + created_log.error());
    auto log = std::make_unique<runtime::LogService>(std::move(*created_log));
    if (presentation_mode ==
        runtime::FrontEndPresentationMode::DeveloperDiagnostics)
    {
        log->Warning("presentation",
            "DEVELOPER DIAGNOSTICS enabled; presentation and gameplay are project-authored");
    }
    else
    {
        log->Info("presentation",
            "retail-derived post-launch presentation is required");
    }

    asset::SceneIR diagnostic_scene;
    // Live free-fly camera seed (captured in the camera block below, consumed at
    // the BuildDiagnosticScenePresentation call): the initial pose, a per-frame
    // move speed scaled to the level, and an optional scripted per-frame input.
    std::optional<runtime::FreeFlyPose> free_fly_initial_pose;
    float free_fly_initial_speed = 1.0F;
    runtime::FreeFlyInput free_fly_script_input{};
    // Kinematic player seed (OPENOMEGA_PLAYER=1), settled onto a real floor in the
    // camera block below and consumed at the BuildDiagnosticScenePresentation call.
    std::optional<gameplay::CharacterState> player_seed_state;
    gameplay::CharacterControllerParams player_seed_params{};
    std::vector<gameplay::CollisionTriangle> player_seed_collision;
    if (content_owner->level_content)
    {
        auto built_scene = runtime::BuildGlobalSpatialDiagnosticScene(
            content_owner->level_content->spatial);
        if (!built_scene)
        {
            const std::string error =
                "spatial diagnostic scene: " + built_scene.error();
            log->Error("startup", error);
            return std::unexpected(error);
        }
        diagnostic_scene = std::move(*built_scene);

        // OPENOMEGA_PLAYER_PROBE=1: run the native kinematic character controller
        // against this level's REAL decoded COL geometry and log the trajectory --
        // proving the controller (committed 59dbb89, unit-tested on synthetic
        // triangles) settles on a real floor and stops at a real wall on actual
        // disc data. Pure additive diagnostic: it does not move the on-screen actor
        // or camera (the visible player needs a world-space player mesh through the
        // free-fly camera -- the actor marker is a 2D screen blit -- which is the
        // next integration slice). Off by default; no behavior change.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        const char* const player_probe_flag = std::getenv("OPENOMEGA_PLAYER_PROBE");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (player_probe_flag != nullptr && player_probe_flag[0] == '1' &&
            player_probe_flag[1] == '\0')
        {
            const std::vector<gameplay::CollisionTriangle> collision =
                gameplay::BuildLevelCollisionTriangles(
                    content_owner->level_content->spatial);
            if (collision.empty())
            {
                log->Info("player", "player probe: level has no collision triangles");
            }
            else
            {
                float min_x = collision.front().a.x, max_x = min_x;
                float min_y = collision.front().a.y, max_y = min_y;
                float min_z = collision.front().a.z, max_z = min_z;
                for (const gameplay::CollisionTriangle& triangle : collision)
                {
                    for (const asset::Float3IR& v :
                         {triangle.a, triangle.b, triangle.c})
                    {
                        min_x = std::min(min_x, v.x); max_x = std::max(max_x, v.x);
                        min_y = std::min(min_y, v.y); max_y = std::max(max_y, v.y);
                        min_z = std::min(min_z, v.z); max_z = std::max(max_z, v.z);
                    }
                }
                const gameplay::CharacterControllerParams params{};
                constexpr float dt = 1.0F / 60.0F;
                // Seed above the level centre and let gravity settle it onto a floor.
                gameplay::CharacterState state{
                    .position = asset::Float3IR{.x = (min_x + max_x) * 0.5F,
                        .y = (min_y + max_y) * 0.5F, .z = max_z + 20.0F}};
                const std::span<const gameplay::CollisionTriangle> tri_span(collision);
                for (int step = 0; step < 240; ++step)
                    state = gameplay::StepCharacter(
                        state, gameplay::CharacterInput{}, params, tri_span, dt);
                const float settled_z = state.position.z;
                const bool settled = state.grounded;
                // Then drive forward (+X) and see whether a wall clamps the motion.
                const float pre_x = state.position.x;
                for (int step = 0; step < 240; ++step)
                    state = gameplay::StepCharacter(state,
                        gameplay::CharacterInput{.move = asset::Float3IR{.x = 1.0F}},
                        params, tri_span, dt);
                const float travelled_x = state.position.x - pre_x;
                const float unobstructed_x =
                    params.move_speed * dt * 240.0F; // if nothing blocked it
                log->Info("player",
                    "player probe: tris=" + std::to_string(collision.size()) +
                        " settled=" + (settled ? "yes" : "no") +
                        " floor_z=" + std::to_string(settled_z) +
                        " forward_travel=" + std::to_string(travelled_x) +
                        " (unobstructed=" + std::to_string(unobstructed_x) +
                        ") grounded_after=" + (state.grounded ? "yes" : "no"));
            }
        }

        // Prefer the real VUM VISUAL geometry over the collision hull when it decoded. The decoded
        // per-cell meshes are re-expressed as a LevelSpatialIR and fed through the SAME scene
        // builder, so they get the identical bounds-fitted projection + framed camera the collision
        // path uses (VUM positions share the collision world space, but the builder projects world
        // coords into its normalized view -- reusing them raw would land off-screen). Falls back to
        // the collision scene when nothing decoded or the VUM scene fails to build. Real per-vertex
        // UVs are decoded but not yet fed to the GPU (triplanar shading for now); a UV-attribute
        // shader variant is the next step.
        constexpr std::size_t kMaximumVisualVertices = 600000U;
        asset::LevelSpatialIR visual_spatial;
        std::size_t visual_vertices = 0;
        std::size_t visual_triangles = 0;
        bool visual_vertex_cap_hit = false;
        for (const auto& cell : content_owner->level_visual_geometry)
        {
            for (const auto& visual_mesh : cell.meshes)
            {
                if (visual_mesh.positions.empty() || visual_mesh.triangle_indices.size() < 3U)
                    continue;
                if (visual_vertices + visual_mesh.positions.size() > kMaximumVisualVertices)
                {
                    visual_vertex_cap_hit = true;
                    break;
                }
                asset::SpatialMeshIR spatial_mesh;
                spatial_mesh.vertices = visual_mesh.positions;
                const std::uint32_t vertex_count =
                    static_cast<std::uint32_t>(visual_mesh.positions.size());
                for (std::size_t base = 0U; base + 2U < visual_mesh.triangle_indices.size();
                     base += 3U)
                {
                    const std::uint32_t a = visual_mesh.triangle_indices[base];
                    const std::uint32_t b = visual_mesh.triangle_indices[base + 1U];
                    const std::uint32_t c = visual_mesh.triangle_indices[base + 2U];
                    if (a < vertex_count && b < vertex_count && c < vertex_count)
                        spatial_mesh.triangles.push_back(asset::SpatialTriangleIR{
                            .vertex_indices = {a, b, c}});
                }
                if (spatial_mesh.triangles.empty())
                    continue;
                visual_vertices += spatial_mesh.vertices.size();
                visual_triangles += spatial_mesh.triangles.size();
                visual_spatial.terrain_cells.push_back(std::move(spatial_mesh));
            }
            if (visual_vertex_cap_hit)
                break;
        }
        if (!visual_spatial.terrain_cells.empty())
        {
            auto visual_scene = runtime::BuildGlobalSpatialDiagnosticScene(visual_spatial);
            if (visual_scene)
            {
                diagnostic_scene = std::move(*visual_scene);
                log->Info("level",
                    "rendering VUM visual geometry: cells=" +
                        std::to_string(visual_spatial.terrain_cells.size()) + " vertices=" +
                        std::to_string(visual_vertices) + " triangles=" +
                        std::to_string(visual_triangles) +
                        (visual_vertex_cap_hit ? " (vertex cap hit; remaining skipped)" : ""));
            }
            else
            {
                log->Warning("level",
                    "VUM visual scene build failed; using collision hull: " +
                        visual_scene.error());
            }
        }

        // Free-fly 3D camera: view the chosen level geometry (VUM visual if it
        // decoded, else the collision hull) in true perspective from a movable
        // camera, so the level is navigable in 3D rather than shown as the flat
        // diagnostic projection. OPENOMEGA_FIXED_CAMERA=1 keeps the flat view;
        // OPENOMEGA_CAMERA_POSE="x,y,z,yaw,pitch" overrides the initial pose for
        // headless capture (live keyboard fly-through advances this pose per frame).
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        const char* const fixed_camera_env = std::getenv("OPENOMEGA_FIXED_CAMERA");
        const char* const camera_pose_env = std::getenv("OPENOMEGA_CAMERA_POSE");
        const char* const camera_script_env = std::getenv("OPENOMEGA_CAMERA_SCRIPT");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        const bool use_free_fly = fixed_camera_env == nullptr ||
                                  std::string_view(fixed_camera_env) != "1";
        if (use_free_fly && !diagnostic_scene.render_meshes.empty())
        {
            // Navigate the complete collision shell (the VUM visual geometry is
            // still sparse pending its strip-break topology follow-up, so it is
            // unsuitable for a framed fly-through).
            const asset::LevelSpatialIR& camera_spatial =
                content_owner->level_content->spatial;
            auto world_scene = runtime::BuildWorldSpaceLevelScene(camera_spatial);
            if (world_scene && !world_scene->render_meshes.empty() &&
                !world_scene->render_meshes.front().positions.empty())
            {
                asset::Float3IR minimum =
                    world_scene->render_meshes.front().positions.front();
                asset::Float3IR maximum = minimum;
                for (const asset::RenderMeshIR& mesh : world_scene->render_meshes)
                {
                    for (const asset::Float3IR& p : mesh.positions)
                    {
                        minimum.x = std::min(minimum.x, p.x);
                        minimum.y = std::min(minimum.y, p.y);
                        minimum.z = std::min(minimum.z, p.z);
                        maximum.x = std::max(maximum.x, p.x);
                        maximum.y = std::max(maximum.y, p.y);
                        maximum.z = std::max(maximum.z, p.z);
                    }
                }
                const asset::Float3IR center{
                    .x = (minimum.x + maximum.x) * 0.5F,
                    .y = (minimum.y + maximum.y) * 0.5F,
                    .z = (minimum.z + maximum.z) * 0.5F,
                };
                const float radius =
                    std::max({maximum.x - minimum.x, maximum.y - minimum.y,
                                 maximum.z - minimum.z, 1.0F}) *
                    0.5F;

                // Default: above and back (along -Z), angled down at the centre.
                runtime::FreeFlyPose pose{
                    .position = {.x = center.x,
                        .y = center.y + radius * 0.6F,
                        .z = center.z - radius * 1.9F},
                    .yaw = 0.0F,
                    .pitch = -0.30F,
                };
                if (camera_pose_env != nullptr)
                {
                    if (auto parsed = runtime::ParseFreeFlyPose(camera_pose_env))
                        pose = *parsed;
                }

                constexpr float aspect = 640.0F / 448.0F;
                const float near_plane = std::max(0.05F, radius * 0.01F);
                const float far_plane = radius * 20.0F + near_plane;
                world_scene->camera = asset::SceneCameraIR{
                    .world_to_view = runtime::FreeFlyViewMatrix(pose),
                    .view_to_clip = runtime::PerspectiveProjection(
                        1.0472F, aspect, near_plane, far_plane),
                };
                // Seed the live camera: initial pose + a per-frame move speed
                // (~1% of the level radius, so a fly-through crosses in ~100
                // frames) + an optional scripted per-frame input for headless
                // motion capture.
                free_fly_initial_pose = pose;
                free_fly_initial_speed = std::max(radius * 0.01F, 0.05F);
                free_fly_script_input = runtime::ParseFreeFlyScript(
                    camera_script_env != nullptr ? camera_script_env : "", 0.03F);

                // OPENOMEGA_PLAYER=1: spawn a kinematic player, settle it onto a
                // real floor, and switch the camera to a Z-up follow view. The
                // per-frame refresh then steps the player from input and moves the
                // camera with it, so the level is walkable (vs the detached
                // free-fly camera). Off by default -- no behavior change.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
                const char* const player_env = std::getenv("OPENOMEGA_PLAYER");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
                if (player_env != nullptr && std::string_view(player_env) == "1")
                {
                    player_seed_collision =
                        gameplay::BuildLevelCollisionTriangles(camera_spatial);
                    if (!player_seed_collision.empty())
                    {
                        gameplay::CharacterState pstate{
                            .position = asset::Float3IR{.x = center.x,
                                .y = center.y, .z = maximum.z + 20.0F}};
                        const std::span<const gameplay::CollisionTriangle> full(
                            player_seed_collision);
                        for (int step = 0; step < 240; ++step)
                            pstate = gameplay::StepCharacter(pstate,
                                gameplay::CharacterInput{}, player_seed_params,
                                full, kPlayerStepDt);
                        player_seed_state = pstate;
                        world_scene->camera.world_to_view = PlayerFollowView(
                            pstate.position, player_seed_params.radius);
                        log->Info("player",
                            "player active: spawn(" +
                                std::to_string(pstate.position.x) + "," +
                                std::to_string(pstate.position.y) + "," +
                                std::to_string(pstate.position.z) +
                                ") grounded=" + (pstate.grounded ? "yes" : "no") +
                                " tris=" + std::to_string(player_seed_collision.size()));
                    }
                }
                diagnostic_scene = std::move(*world_scene);
                log->Info("level",
                    "free-fly 3D camera active: bounds min(" +
                        std::to_string(minimum.x) + "," + std::to_string(minimum.y) +
                        "," + std::to_string(minimum.z) + ") max(" +
                        std::to_string(maximum.x) + "," + std::to_string(maximum.y) +
                        "," + std::to_string(maximum.z) + ") radius " +
                        std::to_string(radius) + " pose(" +
                        std::to_string(pose.position.x) + "," +
                        std::to_string(pose.position.y) + "," +
                        std::to_string(pose.position.z) + ")");
            }
        }
    }

    auto created_jobs = runtime::JobService::Create(settings.jobs);
    if (!created_jobs)
    {
        log->Error("startup", "job service: " + created_jobs.error());
        return std::unexpected("job service: " + created_jobs.error());
    }
    auto jobs = std::make_unique<runtime::JobService>(std::move(*created_jobs));

    std::unique_ptr<runtime::AssetService> assets;
    if (content_owner->level_texture_store)
    {
        if (!content_owner->game_data)
        {
            constexpr std::string_view error =
                "asset service requires an available game-data service";
            log->Error("startup", error);
            return std::unexpected(std::string(error));
        }

        auto created_assets = runtime::AssetService::Create(
            *jobs, *content_owner->game_data, *content_owner->level_texture_store);
        if (!created_assets)
        {
            const std::string error = "asset service: " + std::string(
                runtime::AssetServiceErrorCodeName(created_assets.error().code));
            log->Error("startup", error);
            return std::unexpected(error);
        }
        assets = std::move(*created_assets);
    }

    auto created_scheduler = runtime::FrameScheduler::Create(settings.frame);
    if (!created_scheduler)
    {
        log->Error("startup", "frame scheduler: " + created_scheduler.error());
        return std::unexpected("frame scheduler: " + created_scheduler.error());
    }
    auto frame_scheduler =
        std::make_unique<runtime::FrameScheduler>(std::move(*created_scheduler));

    constexpr std::array bindings{
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_ESCAPE),
            .action = kFrontEndCancelAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_F10),
            .action = kQuitAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::GamepadButton,
            .code = static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_BACK),
            .action = kQuitAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_BACKSPACE),
            .action = kFrontEndCancelAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::MouseButton,
            .code = static_cast<std::uint16_t>(SDL_BUTTON_RIGHT),
            .action = kDebugTargetAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::GamepadButton,
            .code = static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_EAST),
            .action = kFrontEndCancelAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_F1),
            .action = kFrontEndPrimaryAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_RETURN),
            .action = kFrontEndPrimaryAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_KP_ENTER),
            .action = kFrontEndPrimaryAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_SPACE),
            .action = kDebugFireAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::MouseButton,
            .code = static_cast<std::uint16_t>(SDL_BUTTON_LEFT),
            .action = kDebugFireAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_T),
            .action = kDebugTargetAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::GamepadButton,
            .code = static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_START),
            .action = kFrontEndPrimaryAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::GamepadButton,
            .code = static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_SOUTH),
            .action = kFrontEndPrimaryAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_W),
            .action = kDebugMoveForwardAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_UP),
            .action = kDebugMoveForwardAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::GamepadButton,
            .code = static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_DPAD_UP),
            .action = kDebugMoveForwardAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_S),
            .action = kDebugMoveBackwardAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_DOWN),
            .action = kDebugMoveBackwardAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::GamepadButton,
            .code = static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_DPAD_DOWN),
            .action = kDebugMoveBackwardAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_A),
            .action = kDebugMoveLeftAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_LEFT),
            .action = kDebugMoveLeftAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::GamepadButton,
            .code = static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_DPAD_LEFT),
            .action = kDebugMoveLeftAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_D),
            .action = kDebugMoveRightAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_RIGHT),
            .action = kDebugMoveRightAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::GamepadButton,
            .code = static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_DPAD_RIGHT),
            .action = kDebugMoveRightAction,
        },
    };
    auto binding_table = runtime::InputBindingTable::FromBindings(bindings);
    if (!binding_table)
    {
        log->Error("startup", "input bindings: " + binding_table.error());
        return std::unexpected("input bindings: " + binding_table.error());
    }
    auto created_input = runtime::InputTracker::Create(
        std::move(*binding_table), settings.max_input_events_per_frame);
    if (!created_input)
    {
        log->Error("startup", "input tracker: " + created_input.error());
        return std::unexpected("input tracker: " + created_input.error());
    }
    auto input = std::make_unique<runtime::InputTracker>(std::move(*created_input));

    auto created_simulation = simulation::SimulationWorld::Create(
        {.fixed_step = settings.frame.simulation_step});
    if (!created_simulation)
    {
        log->Error("startup", "simulation world: " + created_simulation.error());
        return std::unexpected("simulation world: " + created_simulation.error());
    }
    auto simulation =
        std::make_unique<simulation::SimulationWorld>(std::move(*created_simulation));
    auto created_debug_locomotion_entity =
        simulation->CreatePositionedEntity(simulation::Position3{});
    if (!created_debug_locomotion_entity)
    {
        constexpr std::string_view error =
            "debug locomotion positioned entity creation failed";
        log->Error("startup", error);
        return std::unexpected(std::string(error));
    }
    const simulation::EntityId debug_locomotion_entity =
        *created_debug_locomotion_entity;

    runtime::DebugImage diagnostic_image =
        content_owner->debug_image ? *content_owner->debug_image
                                   : BuildProjectFrontEndDiagnosticPlayImage();
    // Objective HUD: for a loaded level, seed its declarative mission objective
    // state (from the .SO-extracted MissionData) and draw the tracker's active/
    // completed objectives onto the diagnostic overlay image. This demonstrates
    // the ObjectiveTracker + HUD end-to-end. NOTE (follow-up): the state is
    // seeded once here at build time (a demo of obj1 completed with obj2..4
    // active), not yet advanced per frame from live gameplay triggers.
    if (content_stage == runtime::ContentStartupStage::LevelContent)
    {
        const gameplay::MissionData &mission = gameplay::MinskMissionData();
        gameplay::ObjectiveState objective_state =
            gameplay::InitialObjectiveState(mission);
        const auto apply = [&mission, &objective_state](
                               const gameplay::ObjectiveChoice choice,
                               const std::uint16_t id) {
            const auto step = gameplay::AdvanceObjectives(
                mission, objective_state, {choice, id});
            if (step)
                objective_state = step->state;
        };
        for (const std::uint16_t id : {std::uint16_t{1U}, std::uint16_t{2U},
                 std::uint16_t{3U}, std::uint16_t{4U}})
            apply(gameplay::ObjectiveChoice::Add, id);
        apply(gameplay::ObjectiveChoice::Pass, std::uint16_t{1U});

        std::vector<ObjectiveHudEntry> hud_entries;
        std::uint32_t complete_count = 0U;
        for (std::size_t index = 0U; index < mission.objectives.size(); ++index)
        {
            std::uint8_t status_code = 0U;
            switch (objective_state.status[index])
            {
            case gameplay::ObjectiveStatus::Active:
                status_code = 1U;
                break;
            case gameplay::ObjectiveStatus::Complete:
                status_code = 2U;
                ++complete_count;
                break;
            case gameplay::ObjectiveStatus::Failed:
                status_code = 3U;
                break;
            case gameplay::ObjectiveStatus::Inactive:
                break;
            }
            if (status_code != 0U)
                hud_entries.push_back(
                    ObjectiveHudEntry{mission.objectives[index].id, status_code});
        }
        DrawObjectiveHudOnto(diagnostic_image, hud_entries, complete_count,
            static_cast<std::uint32_t>(mission.objectives.size()));
    }
    const runtime::DebugImage diagnostic_actor_marker_image{
        .width = 1U,
        .height = 1U,
        .rgba8_pixels = std::vector<std::byte>(
            kDiagnosticActorMarkerRgba8.begin(), kDiagnosticActorMarkerRgba8.end()),
    };

    runtime::DebugImage asset_topology_image;
    std::optional<runtime::DebugImage> asset_transfer_image;
    if (content_stage == runtime::ContentStartupStage::LevelContent)
    {
        auto built_asset_preview = runtime::BuildFirstLevelTextureDiagnosticPreview(
            *assets, *content_owner->level_texture_store);
        if (!built_asset_preview)
        {
            const std::string error(built_asset_preview.error().message);
            log->Error("startup", error);
            return std::unexpected(error);
        }
        asset_topology_image = std::move(built_asset_preview->topology_image);
        asset_transfer_image =
            std::move(built_asset_preview->packed24_transfer_image);
        if (built_asset_preview->packed24_transfer_error_code)
        {
            const auto rejection =
                *built_asset_preview->packed24_transfer_error_code;
            log->Info("startup", "packed-24 transfer diagnostic unavailable: " +
                                     std::string(
                                         runtime::Packed24TransferDebugImageErrorCodeName(
                                             rejection)));
        }
    }
    else
    {
        auto built_asset_topology = BuildProjectFrontEndAssetTopologyImage();
        if (!built_asset_topology)
        {
            const std::string error = "project diagnostic asset topology image: " +
                                      std::string(
                                          runtime::TextureStorageTopologyDebugImageErrorCodeName(
                                              built_asset_topology.error().code));
            log->Error("startup", error);
            return std::unexpected(error);
        }
        asset_topology_image = std::move(*built_asset_topology);
    }

    auto created_platform = SdlPlatformService::Create();
    if (!created_platform)
    {
        log->Error("startup", "SDL platform service: " + created_platform.error());
        return std::unexpected("SDL platform service: " + created_platform.error());
    }
    auto platform = std::make_unique<SdlPlatformService>(std::move(*created_platform));

    auto created_sdl_input =
        SdlInputService::Create(*platform, settings.gamepad_enabled);
    if (!created_sdl_input)
    {
        log->Error("startup", "SDL input service: " + created_sdl_input.error());
        return std::unexpected("SDL input service: " + created_sdl_input.error());
    }
    auto sdl_input = std::make_unique<SdlInputService>(std::move(*created_sdl_input));

    auto created_audio = SdlAudioService::Create(*platform);
    if (!created_audio)
    {
        log->Error("startup", "SDL audio service: " + created_audio.error());
        return std::unexpected("SDL audio service: " + created_audio.error());
    }
    auto audio = std::make_unique<SdlAudioService>(std::move(*created_audio));

    const SdlGpuWindowIdentity window_identity =
        presentation_mode ==
                runtime::FrontEndPresentationMode::DeveloperDiagnostics
            ? SdlGpuWindowIdentity::DeveloperDiagnostics
            : SdlGpuWindowIdentity::NativeRuntime;
    auto created_host = SdlGpuHost::Create(
        *platform, debug_device, texture_config, {}, window_identity);
    if (!created_host)
    {
        log->Error("startup", "SDL/GPU host: " + created_host.error());
        return std::unexpected(created_host.error());
    }
    auto host = std::make_unique<SdlGpuHost>(std::move(*created_host));
    // Adopt an injected test source immediately after the host exists so every later startup
    // failure destroys playback before the GPU and process-global SDL owners.
    std::unique_ptr<OpeningMoviePlayback> opening_movie_candidate =
        std::move(opening_movie_playback);

    constexpr runtime::RenderSourceRectQ16 full_source{
        .left = 0U,
        .top = 0U,
        .right = runtime::kNormalizedRenderExtent,
        .bottom = runtime::kNormalizedRenderExtent,
    };
    constexpr runtime::RenderTargetRectQ16 full_target{
        .left = 0U,
        .top = 0U,
        .right = runtime::kNormalizedRenderExtent,
        .bottom = runtime::kNormalizedRenderExtent,
    };
    constexpr runtime::RenderTargetRectQ16 menu_target{
        .left = 2048U,
        .top = 2048U,
        .right = 26624U,
        .bottom = 15872U,
    };
    // Project-authored diagnostic split only. It assigns no retail layout or
    // texture semantics.
    constexpr runtime::RenderTargetRectQ16 asset_topology_split_target{
        .left = 2048U,
        .top = 2048U,
        .right = 13824U,
        .bottom = 15872U,
    };
    constexpr runtime::RenderTargetRectQ16 asset_transfer_target{
        .left = 14848U,
        .top = 2048U,
        .right = 26624U,
        .bottom = 15872U,
    };
    constexpr runtime::RenderSourceRectQ16 menu_selection_source{
        .left = 18432U,
        .top = 9103U,
        .right = 59392U,
        .bottom = 14563U,
    };
    // Project-owned solid cyan sample from the already-uploaded Profiles card
    // border. Startup draw lists stretch it into the three cursor markers.
    constexpr runtime::RenderSourceRectQ16 profile_selection_source{
        .left = 0U,
        .top = 0U,
        .right = 512U,
        .bottom = 512U,
    };
    constexpr std::array menu_selection_targets{
        runtime::RenderTargetRectQ16{
            .left = 3584U,
            .top = 7424U,
            .right = 4352U,
            .bottom = 8960U,
        },
        runtime::RenderTargetRectQ16{
            .left = 3584U,
            .top = 9344U,
            .right = 4352U,
            .bottom = 10880U,
        },
        runtime::RenderTargetRectQ16{
            .left = 3584U,
            .top = 11264U,
            .right = 4352U,
            .bottom = 12800U,
        },
        runtime::RenderTargetRectQ16{
            .left = 3584U,
            .top = 13184U,
            .right = 4352U,
            .bottom = 14720U,
        },
    };
    static_assert(menu_selection_targets.size() == kFrontEndMainRowCount);
    static_assert(kFrontEndVisibleProfiles <= kFrontEndMainRowCount);
    // Project-owned active-row cue. It reuses each profile row's vertical band
    // and sits just outside the card's row panel, opposite the selection cursor,
    // so the two cues never overlap. It assigns no retail layout meaning.
    constexpr std::array profile_active_targets{
        runtime::RenderTargetRectQ16{
            .left = 25'280U,
            .top = 7'424U,
            .right = 26'048U,
            .bottom = 8'960U,
        },
        runtime::RenderTargetRectQ16{
            .left = 25'280U,
            .top = 9'344U,
            .right = 26'048U,
            .bottom = 10'880U,
        },
        runtime::RenderTargetRectQ16{
            .left = 25'280U,
            .top = 11'264U,
            .right = 26'048U,
            .bottom = 12'800U,
        },
    };
    static_assert(profile_active_targets.size() == kFrontEndVisibleProfiles);

    runtime::RenderTextureHandle diagnostic_texture;
    runtime::RenderTextureHandle diagnostic_actor_marker_texture;
    runtime::RenderDrawList diagnostic_actor_draw_list;
    runtime::RenderDrawList diagnostic_scene_overlay_draw_list;
    std::unique_ptr<DiagnosticScenePresentation> diagnostic_scene_presentation;
    FrontEndPresentation front_end_presentation;
    std::optional<FrontEndPresentation> first_profile_presentation;
    runtime::RenderTextureHandle diagnostic_controls_texture;
    runtime::RenderTextureHandle diagnostic_asset_topology_texture;
    runtime::RenderTextureHandle diagnostic_asset_transfer_texture;
    constexpr std::size_t kDiagnosticBaseCommandCapacity = 1U;
    constexpr std::size_t kDiagnosticMaximumOverlayCommandCapacity = 3U;
    constexpr std::size_t kDiagnosticCommandCapacity =
        kDiagnosticBaseCommandCapacity + kDiagnosticMaximumOverlayCommandCapacity;
    static_assert(kDiagnosticCommandCapacity <=
                  runtime::kMaximumRenderTextureBlitsPerFrame);
    std::array<runtime::RenderTextureBlitCommand, kDiagnosticCommandCapacity>
        diagnostic_commands{};
    std::size_t diagnostic_command_count = 0U;
    auto uploaded = host->UploadRgba8Texture(runtime::Rgba8TextureUploadView{
        .width = diagnostic_image.width,
        .height = diagnostic_image.height,
        .pixels = diagnostic_image.pixels(),
    });
    if (!uploaded)
    {
        const std::string error =
            "SDL/GPU diagnostic texture upload: " + uploaded.error();
        log->Error("startup", error);
        return std::unexpected(error);
    }

    diagnostic_texture = *uploaded;
    diagnostic_commands[diagnostic_command_count++] =
        runtime::RenderTextureBlitCommand{
            .texture = diagnostic_texture,
            .source = full_source,
            .destination = full_target,
            .fit_mode = runtime::RenderTextureFitMode::Contain,
            .filter_mode = runtime::RenderTextureFilterMode::Nearest,
        };

    auto created_hidden_draw_list = runtime::RenderDrawList::Create(
        std::span<const runtime::RenderTextureBlitCommand>{
            diagnostic_commands.data(), diagnostic_command_count});
    if (!created_hidden_draw_list)
    {
        constexpr std::string_view error =
            "SDL/GPU diagnostic hidden draw-list creation failed";
        log->Error("startup", error);
        return std::unexpected(std::string(error));
    }
    auto diagnostic_hidden_draw_list = std::move(*created_hidden_draw_list);
    const std::size_t diagnostic_base_command_count = diagnostic_command_count;

    const auto build_front_end_presentation =
        [&](const FrontEndStartupModel& model,
            const FrontEndCapabilities capabilities)
        -> std::expected<FrontEndPresentation, std::string>
    {
        static_assert(
            std::is_nothrow_move_constructible_v<FrontEndPresentation>);
        static_assert(std::is_nothrow_move_assignable_v<FrontEndPresentation>);
        static_assert(sizeof(std::unique_ptr<ProfileActiveDrawListMatrix>) <
                      sizeof(ProfileActiveDrawListMatrix));
        FrontEndPresentation presentation;
        presentation.profile_active_draw_lists.reset(
            new (std::nothrow) ProfileActiveDrawListMatrix{});
        if (!presentation.profile_active_draw_lists)
        {
            return std::unexpected(std::string{
                "SDL/GPU front-end active profile draw-list allocation failed"});
        }
        ProfileActiveDrawListMatrix& profile_active_draw_lists =
            *presentation.profile_active_draw_lists;
        const runtime::DebugImage menu_image =
            BuildProjectFrontEndMainImage(content_stage, model.total_profiles);
        auto uploaded_menu = host->UploadRgba8Texture(runtime::Rgba8TextureUploadView{
            .width = menu_image.width,
            .height = menu_image.height,
            .pixels = menu_image.pixels(),
        });
        if (!uploaded_menu)
        {
            return std::unexpected(
                "SDL/GPU front-end main texture upload: " + uploaded_menu.error());
        }
        presentation.main_texture = *uploaded_menu;
        diagnostic_commands[diagnostic_base_command_count] =
            runtime::RenderTextureBlitCommand{
                .texture = presentation.main_texture,
                .source = full_source,
                .destination = menu_target,
                .fit_mode = runtime::RenderTextureFitMode::Stretch,
                .filter_mode = runtime::RenderTextureFilterMode::Nearest,
            };

        for (std::size_t row = 0U; row < menu_selection_targets.size(); ++row)
        {
            diagnostic_commands[diagnostic_base_command_count + 1U] =
                runtime::RenderTextureBlitCommand{
                    .texture = presentation.main_texture,
                    .source = menu_selection_source,
                    .destination = menu_selection_targets[row],
                    .fit_mode = runtime::RenderTextureFitMode::Stretch,
                    .filter_mode = runtime::RenderTextureFilterMode::Nearest,
                };
            auto created_draw_list = runtime::RenderDrawList::Create(
                std::span<const runtime::RenderTextureBlitCommand>{
                    diagnostic_commands.data(), diagnostic_base_command_count + 2U});
            if (!created_draw_list)
            {
                return std::unexpected(
                    std::string{"SDL/GPU front-end main draw-list creation failed"});
            }
            presentation.main_draw_lists[row] = std::move(*created_draw_list);
        }

        const runtime::DebugImage profiles_image =
            BuildProjectFrontEndProfilesImage(model, capabilities);
        auto uploaded_profiles = host->UploadRgba8Texture(runtime::Rgba8TextureUploadView{
            .width = profiles_image.width,
            .height = profiles_image.height,
            .pixels = profiles_image.pixels(),
        });
        if (!uploaded_profiles)
        {
            return std::unexpected(
                "SDL/GPU front-end profiles texture upload: " + uploaded_profiles.error());
        }
        presentation.profiles_texture = *uploaded_profiles;
        diagnostic_commands[diagnostic_base_command_count] =
            runtime::RenderTextureBlitCommand{
                .texture = presentation.profiles_texture,
                .source = full_source,
                .destination = menu_target,
                .fit_mode = runtime::RenderTextureFitMode::Stretch,
                .filter_mode = runtime::RenderTextureFilterMode::Nearest,
            };
        auto created_profiles_draw_list = runtime::RenderDrawList::Create(
            std::span<const runtime::RenderTextureBlitCommand>{
                diagnostic_commands.data(), diagnostic_base_command_count + 1U});
        if (!created_profiles_draw_list)
        {
            return std::unexpected(
                std::string{"SDL/GPU front-end profiles draw-list creation failed"});
        }
        presentation.profiles_draw_list = std::move(*created_profiles_draw_list);

        for (std::size_t slot = 0U;
             slot < presentation.profile_selection_draw_lists.size(); ++slot)
        {
            diagnostic_commands[diagnostic_base_command_count + 1U] =
                runtime::RenderTextureBlitCommand{
                    .texture = presentation.profiles_texture,
                    .source = profile_selection_source,
                    .destination = menu_selection_targets[slot],
                    .fit_mode = runtime::RenderTextureFitMode::Stretch,
                    .filter_mode = runtime::RenderTextureFilterMode::Nearest,
                };
            auto created_draw_list = runtime::RenderDrawList::Create(
                std::span<const runtime::RenderTextureBlitCommand>{
                    diagnostic_commands.data(), diagnostic_base_command_count + 2U});
            if (!created_draw_list)
            {
                return std::unexpected(std::string{
                    "SDL/GPU front-end profile selection draw-list creation failed"});
            }
            presentation.profile_selection_draw_lists[slot] =
                std::move(*created_draw_list);

            for (std::size_t active_slot = 0U;
                 active_slot < profile_active_draw_lists[slot].size();
                 ++active_slot)
            {
                diagnostic_commands[diagnostic_base_command_count + 2U] =
                    runtime::RenderTextureBlitCommand{
                        .texture = presentation.profiles_texture,
                        .source = profile_selection_source,
                        .destination = profile_active_targets[active_slot],
                        .fit_mode = runtime::RenderTextureFitMode::Stretch,
                        .filter_mode = runtime::RenderTextureFilterMode::Nearest,
                    };
                auto created_active_draw_list = runtime::RenderDrawList::Create(
                    std::span<const runtime::RenderTextureBlitCommand>{
                        diagnostic_commands.data(),
                        diagnostic_base_command_count + 3U});
                if (!created_active_draw_list)
                {
                    return std::unexpected(std::string{
                        "SDL/GPU front-end active profile draw-list creation failed"});
                }
                profile_active_draw_lists[slot][active_slot] =
                    std::move(*created_active_draw_list);
            }
        }
        return std::expected<FrontEndPresentation, std::string>{
            std::in_place, std::move(presentation)};
    };

    const bool can_create_first_profile = native_persistence != nullptr &&
        front_end_startup_model.total_profiles == 0U;
    auto created_front_end_presentation = build_front_end_presentation(
        front_end_startup_model,
        FrontEndCapabilities{.can_create_first_profile = can_create_first_profile});
    if (!created_front_end_presentation)
    {
        log->Error("startup", created_front_end_presentation.error());
        return std::unexpected(std::move(created_front_end_presentation.error()));
    }
    front_end_presentation = std::move(*created_front_end_presentation);

    if (can_create_first_profile)
    {
        const std::array preview_profiles{
            profiles::ProfileSummary{
                .id = kFirstProfileId,
                .metadata = profiles::ProfileMetadata{
                    .display_name = std::string{kFrontEndFirstProfileDisplayName},
                    .created_unix_milliseconds = 0U,
                    .modified_unix_milliseconds = 0U,
                },
                .metadata_revision = 1U,
            },
        };
        const auto preview_model = MakeFrontEndStartupModel(preview_profiles);
        if (!preview_model)
        {
            constexpr std::string_view error =
                "front-end first-profile preview model creation failed";
            log->Error("startup", error);
            return std::unexpected(std::string{error});
        }
        auto created_first_profile_presentation =
            build_front_end_presentation(*preview_model, {});
        if (!created_first_profile_presentation)
        {
            log->Error("startup", created_first_profile_presentation.error());
            return std::unexpected(
                std::move(created_first_profile_presentation.error()));
        }
        first_profile_presentation =
            std::move(*created_first_profile_presentation);
    }

    const runtime::DebugImage controls_image = BuildProjectFrontEndControlsImage();
    auto uploaded_controls = host->UploadRgba8Texture(runtime::Rgba8TextureUploadView{
        .width = controls_image.width,
        .height = controls_image.height,
        .pixels = controls_image.pixels(),
    });
    if (!uploaded_controls)
    {
        const std::string error =
            "SDL/GPU diagnostic controls texture upload: " + uploaded_controls.error();
        log->Error("startup", error);
        return std::unexpected(error);
    }
    diagnostic_controls_texture = *uploaded_controls;
    diagnostic_commands[diagnostic_base_command_count] =
        runtime::RenderTextureBlitCommand{
            .texture = diagnostic_controls_texture,
            .source = full_source,
            .destination = menu_target,
            .fit_mode = runtime::RenderTextureFitMode::Stretch,
            .filter_mode = runtime::RenderTextureFilterMode::Nearest,
        };
    auto created_controls_draw_list = runtime::RenderDrawList::Create(
        std::span<const runtime::RenderTextureBlitCommand>{
            diagnostic_commands.data(), diagnostic_base_command_count + 1U});
    if (!created_controls_draw_list)
    {
        constexpr std::string_view error =
            "SDL/GPU diagnostic controls draw-list creation failed";
        log->Error("startup", error);
        return std::unexpected(std::string(error));
    }
    auto diagnostic_controls_draw_list = std::move(*created_controls_draw_list);

    auto uploaded_asset_topology = host->UploadRgba8Texture(
        runtime::Rgba8TextureUploadView{
            .width = asset_topology_image.width,
            .height = asset_topology_image.height,
            .pixels = asset_topology_image.pixels(),
        });
    if (!uploaded_asset_topology)
    {
        const std::string error =
            "SDL/GPU diagnostic asset topology texture upload: " +
            uploaded_asset_topology.error();
        log->Error("startup", error);
        return std::unexpected(error);
    }
    diagnostic_asset_topology_texture = *uploaded_asset_topology;

    auto uploaded_actor_marker = host->UploadRgba8Texture(
        runtime::Rgba8TextureUploadView{
            .width = diagnostic_actor_marker_image.width,
            .height = diagnostic_actor_marker_image.height,
            .pixels = diagnostic_actor_marker_image.pixels(),
        });
    if (!uploaded_actor_marker)
    {
        const std::string error =
            "SDL/GPU diagnostic actor marker texture upload: " +
            uploaded_actor_marker.error();
        log->Error("startup", error);
        return std::unexpected(error);
    }
    diagnostic_actor_marker_texture = *uploaded_actor_marker;

    // Objective HUD overlay for the 3D level view: render the seeded Minsk
    // objective state (same demo as the menu overlay above) onto a small slate
    // panel image and upload it, so it can be blitted into the scene overlay
    // draw list on top of the level. Level content only; fail-soft.
    runtime::RenderTextureHandle diagnostic_objective_hud_texture;
    if (content_stage == runtime::ContentStartupStage::LevelContent)
    {
        const gameplay::MissionData &hud_mission = gameplay::MinskMissionData();
        gameplay::ObjectiveState hud_state =
            gameplay::InitialObjectiveState(hud_mission);
        const auto hud_apply = [&hud_mission, &hud_state](
                                   const gameplay::ObjectiveChoice choice,
                                   const std::uint16_t id) {
            const auto step =
                gameplay::AdvanceObjectives(hud_mission, hud_state, {choice, id});
            if (step)
                hud_state = step->state;
        };
        for (const std::uint16_t id : {std::uint16_t{1U}, std::uint16_t{2U},
                 std::uint16_t{3U}, std::uint16_t{4U}})
            hud_apply(gameplay::ObjectiveChoice::Add, id);
        hud_apply(gameplay::ObjectiveChoice::Pass, std::uint16_t{1U});

        std::vector<ObjectiveHudEntry> hud_entries;
        std::uint32_t hud_complete = 0U;
        for (std::size_t index = 0U; index < hud_mission.objectives.size();
             ++index)
        {
            std::uint8_t status_code = 0U;
            switch (hud_state.status[index])
            {
            case gameplay::ObjectiveStatus::Active:
                status_code = 1U;
                break;
            case gameplay::ObjectiveStatus::Complete:
                status_code = 2U;
                ++hud_complete;
                break;
            case gameplay::ObjectiveStatus::Failed:
                status_code = 3U;
                break;
            case gameplay::ObjectiveStatus::Inactive:
                break;
            }
            if (status_code != 0U)
                hud_entries.push_back(ObjectiveHudEntry{
                    hud_mission.objectives[index].id, status_code});
        }

        constexpr std::uint32_t kHudPanelWidth = 88U;
        constexpr std::uint32_t kHudPanelHeight = 72U;
        std::vector<std::byte> hud_pixels(
            static_cast<std::size_t>(kHudPanelWidth) * kHudPanelHeight * 4U);
        for (std::size_t i = 0U; i + 3U < hud_pixels.size(); i += 4U)
        {
            hud_pixels[i] = std::byte{16};
            hud_pixels[i + 1U] = std::byte{20};
            hud_pixels[i + 2U] = std::byte{28};
            hud_pixels[i + 3U] = std::byte{255};
        }
        runtime::DebugImage hud_panel_image{
            .width = kHudPanelWidth,
            .height = kHudPanelHeight,
            .rgba8_pixels = std::move(hud_pixels),
        };
        DrawObjectiveHudOnto(hud_panel_image, hud_entries, hud_complete,
            static_cast<std::uint32_t>(hud_mission.objectives.size()));
        auto uploaded_hud =
            host->UploadRgba8Texture(runtime::Rgba8TextureUploadView{
                .width = hud_panel_image.width,
                .height = hud_panel_image.height,
                .pixels = hud_panel_image.pixels(),
            });
        if (uploaded_hud)
            diagnostic_objective_hud_texture = *uploaded_hud;
        else
            log->Info(
                "startup", "objective HUD texture unavailable: upload-failed");
    }

    if (diagnostic_hidden_draw_list.size() != 1U)
    {
        constexpr std::string_view error =
            "SDL/GPU diagnostic actor draw-list creation failed";
        log->Error("startup", error);
        return std::unexpected(std::string(error));
    }
    constexpr auto diagnostic_objective_destination =
        PlanProjectDiagnosticObjectiveMarkerDestination(
            gameplay::DiagnosticProximityTriggerState{});
    static_assert(diagnostic_objective_destination.has_value());
    const std::array diagnostic_actor_commands{
        diagnostic_hidden_draw_list.commands().front(),
        runtime::RenderTextureBlitCommand{
            .texture = diagnostic_actor_marker_texture,
            .source = full_source,
            .destination = PlanProjectDiagnosticActorMarkerDestination(
                simulation::Position3{}),
            .fit_mode = runtime::RenderTextureFitMode::Stretch,
            .filter_mode = runtime::RenderTextureFilterMode::Nearest,
        },
        runtime::RenderTextureBlitCommand{
            .texture = diagnostic_actor_marker_texture,
            .source = full_source,
            .destination = *diagnostic_objective_destination,
            .fit_mode = runtime::RenderTextureFitMode::Stretch,
            .filter_mode = runtime::RenderTextureFilterMode::Nearest,
        },
    };
    auto created_actor_draw_list = runtime::RenderDrawList::Create(
        diagnostic_actor_commands);
    if (!created_actor_draw_list)
    {
        constexpr std::string_view error =
            "SDL/GPU diagnostic actor draw-list creation failed";
        log->Error("startup", error);
        return std::unexpected(std::string(error));
    }
    diagnostic_actor_draw_list = std::move(*created_actor_draw_list);
    std::array<runtime::RenderTextureBlitCommand, 2U> scene_overlay_commands{
        diagnostic_actor_commands[2U],
        runtime::RenderTextureBlitCommand{},
    };
    std::size_t scene_overlay_command_count = 1U;
    if (diagnostic_objective_hud_texture.valid())
    {
        // Objective HUD panel, top-left screen-space (Q16 normalized), on top of
        // the 3D level. Contain preserves the panel's aspect.
        scene_overlay_commands[1U] = runtime::RenderTextureBlitCommand{
            .texture = diagnostic_objective_hud_texture,
            .source = full_source,
            .destination =
                runtime::RenderTargetRectQ16{
                    .left = 512U,
                    .top = 512U,
                    .right = 19000U,
                    .bottom = 15000U,
                },
            .fit_mode = runtime::RenderTextureFitMode::Contain,
            .filter_mode = runtime::RenderTextureFilterMode::Nearest,
        };
        scene_overlay_command_count = 2U;
    }
    auto created_scene_overlay_draw_list = runtime::RenderDrawList::Create(
        std::span<const runtime::RenderTextureBlitCommand>{
            scene_overlay_commands.data(), scene_overlay_command_count});
    if (!created_scene_overlay_draw_list)
    {
        constexpr std::string_view error =
            "SDL/GPU diagnostic scene overlay draw-list creation failed";
        log->Error("startup", error);
        return std::unexpected(std::string(error));
    }
    diagnostic_scene_overlay_draw_list =
        std::move(*created_scene_overlay_draw_list);

    diagnostic_commands[diagnostic_base_command_count] =
        runtime::RenderTextureBlitCommand{
            .texture = diagnostic_asset_topology_texture,
            .source = full_source,
            .destination = asset_transfer_image
                ? asset_topology_split_target
                : menu_target,
            .fit_mode = runtime::RenderTextureFitMode::Contain,
            .filter_mode = runtime::RenderTextureFilterMode::Nearest,
        };
    std::size_t diagnostic_asset_topology_command_count =
        diagnostic_base_command_count + 1U;
    if (asset_transfer_image)
    {
        auto uploaded_asset_transfer = host->UploadRgba8Texture(
            runtime::Rgba8TextureUploadView{
                .width = asset_transfer_image->width,
                .height = asset_transfer_image->height,
                .pixels = asset_transfer_image->pixels(),
            });
        if (!uploaded_asset_transfer)
        {
            log->Info("startup",
                "packed-24 transfer diagnostic unavailable: upload-failed");
        }
        else
        {
            diagnostic_asset_transfer_texture = *uploaded_asset_transfer;
            diagnostic_commands[diagnostic_asset_topology_command_count++] =
                runtime::RenderTextureBlitCommand{
                    .texture = diagnostic_asset_transfer_texture,
                    .source = full_source,
                    .destination = asset_transfer_target,
                    .fit_mode = runtime::RenderTextureFitMode::Contain,
                    .filter_mode = runtime::RenderTextureFilterMode::Nearest,
                };
        }
    }
    diagnostic_commands[diagnostic_base_command_count].destination =
        diagnostic_asset_transfer_texture.valid() ? asset_topology_split_target : menu_target;
    auto created_asset_topology_draw_list = runtime::RenderDrawList::Create(
        std::span<const runtime::RenderTextureBlitCommand>{
            diagnostic_commands.data(), diagnostic_asset_topology_command_count});
    if (!created_asset_topology_draw_list)
    {
        constexpr std::string_view error =
            "SDL/GPU diagnostic asset topology draw-list creation failed";
        log->Error("startup", error);
        return std::unexpected(std::string(error));
    }
    auto diagnostic_asset_topology_draw_list =
        std::move(*created_asset_topology_draw_list);

    std::unique_ptr<OpeningMoviePlayback> opening_movie_player;
    runtime::RenderTextureHandle opening_movie_texture;
    runtime::RenderDrawList opening_movie_draw_list;
    BootSequenceState boot_sequence_state{};
    if (opening_movie_path || opening_movie_source)
    {
        auto created_opening_movie = opening_movie_path
            ? OpeningMoviePlayer::Create(*opening_movie_path)
            : OpeningMoviePlayer::Create(std::move(*opening_movie_source));
        if (!created_opening_movie)
        {
            log->Warning("opening_movie",
                OpeningMoviePlayerErrorMessage(created_opening_movie.error().code));
        }
        else
        {
            opening_movie_candidate =
                std::make_unique<OpeningMoviePlayer>(std::move(*created_opening_movie));
        }
    }
    if (opening_movie_candidate)
    {
        const std::uint32_t movie_width = opening_movie_candidate->width();
        const std::uint32_t movie_height = opening_movie_candidate->height();
        const std::uint64_t safety_duration_ticks =
            opening_movie_candidate->safety_duration_ticks();
        const std::uint64_t logical_bytes =
            static_cast<std::uint64_t>(movie_width) * movie_height * 4U;
        if (movie_width == 0U || movie_height == 0U ||
            movie_width > media::kMaximumNv12FrameWidth ||
            movie_height > media::kMaximumNv12FrameHeight ||
            safety_duration_ticks == 0U ||
            safety_duration_ticks > kOpeningMovieMaximumSafetyTicks ||
            logical_bytes > static_cast<std::uint64_t>(
                                std::numeric_limits<std::size_t>::max()))
        {
            log->Warning("opening_movie",
                "opening movie presentation rejected invalid metadata");
        }
        else
        {
            std::vector<std::byte> black_frame(
                static_cast<std::size_t>(logical_bytes), std::byte{0});
            for (std::size_t alpha = 3U; alpha < black_frame.size(); alpha += 4U)
                black_frame[alpha] = std::byte{255};

            auto uploaded_opening_movie =
                host->UploadRgba8Texture(runtime::Rgba8TextureUploadView{
                    .width = movie_width,
                    .height = movie_height,
                    .pixels = black_frame,
                });
            if (!uploaded_opening_movie)
            {
                log->Warning("opening_movie",
                    "opening movie presentation texture upload failed");
            }
            else
            {
                constexpr runtime::RenderTargetRectQ16 movie_target{
                    .left = 0U,
                    .top = 0U,
                    .right = runtime::kNormalizedRenderExtent,
                    .bottom = runtime::kNormalizedRenderExtent,
                };
                const std::array movie_commands{
                    runtime::RenderTextureBlitCommand{
                        .texture = *uploaded_opening_movie,
                        .source = full_source,
                        .destination = movie_target,
                        .fit_mode = runtime::RenderTextureFitMode::Contain,
                        .filter_mode = runtime::RenderTextureFilterMode::Linear,
                    },
                };
                auto created_opening_movie_draw_list =
                    runtime::RenderDrawList::Create(movie_commands);
                if (!created_opening_movie_draw_list)
                {
                    log->Warning("opening_movie",
                        "opening movie presentation draw-list creation failed");
                    static_cast<void>(host->ReleaseTexture(*uploaded_opening_movie));
                }
                else
                {
                    opening_movie_texture = *uploaded_opening_movie;
                    opening_movie_draw_list =
                        std::move(*created_opening_movie_draw_list);
                    boot_sequence_state = InitialBootSequenceState(
                        BootSequenceConfig{
                            .duration_ticks = safety_duration_ticks,
                            .source_available = true,
                        });
                    opening_movie_player = std::move(opening_movie_candidate);
                    log->Info("opening_movie",
                        "opening movie presentation is ready");
                }
            }
        }
    }

    if (content_owner->level_content)
    {
        auto created_scene_presentation =
            BuildDiagnosticScenePresentation(*host, diagnostic_scene,
                content_owner->level_texture_store
                    ? &*content_owner->level_texture_store
                    : nullptr,
                content_owner->game_data ? &*content_owner->game_data : nullptr,
                free_fly_initial_pose, free_fly_initial_speed,
                free_fly_script_input, player_seed_state, player_seed_params,
                std::move(player_seed_collision));
        if (!created_scene_presentation)
        {
            log->Error("startup", created_scene_presentation.error());
            return std::unexpected(
                std::move(created_scene_presentation.error()));
        }
        diagnostic_scene_presentation =
            std::move(*created_scene_presentation);
        diagnostic_scene_presentation->overlay_draw_list =
            std::move(diagnostic_scene_overlay_draw_list);
        diagnostic_scene_presentation->hud_texture =
            diagnostic_objective_hud_texture;
        // Objective triggers (OPENOMEGA_PLAYER): seed the live objective state to
        // match the HUD's build-time demo (obj1 complete; obj2-4 active) and place
        // one project-owned trigger volume a short walk (+Y) from the player spawn,
        // linked to obj2. Walking into it completes obj2 and the HUD refreshes.
        // Project-placed: the retail beacon/checkpoint world coordinates are not
        // recovered from the .SO (only the objective structure/ids), so the volume
        // position is project-owned, not retail.
        if (player_seed_state)
        {
            const gameplay::MissionData &trigger_mission =
                gameplay::MinskMissionData();
            gameplay::ObjectiveState seed =
                gameplay::InitialObjectiveState(trigger_mission);
            const auto seed_apply = [&trigger_mission, &seed](
                                        const gameplay::ObjectiveChoice choice,
                                        const std::uint16_t id) {
                const auto step = gameplay::AdvanceObjectives(
                    trigger_mission, seed, {choice, id});
                if (step)
                    seed = step->state;
            };
            for (const std::uint16_t id : {std::uint16_t{1U}, std::uint16_t{2U},
                     std::uint16_t{3U}, std::uint16_t{4U}})
                seed_apply(gameplay::ObjectiveChoice::Add, id);
            seed_apply(gameplay::ObjectiveChoice::Pass, std::uint16_t{1U});
            diagnostic_scene_presentation->objective_state = seed;
            const asset::Float3IR &spawn = player_seed_state->position;
            // A small walkable mini-mission: three project-placed trigger volumes
            // spaced along the +Y path from spawn, each linked to a real Minsk
            // objective id (obj2/obj3/obj4, all seeded Active). Walking through
            // them in sequence completes each objective and progresses the HUD
            // (1/12 -> 4/12). Project-placed positions (the .SO gives the
            // objective structure/ids, not world coords); radius is generous so
            // the terrain's z-climb along the path stays inside the sphere.
            const auto place_trigger = [&](const std::uint16_t id,
                                           const float forward_offset) {
                diagnostic_scene_presentation->mission_triggers.push_back(
                    gameplay::MissionTrigger{
                        .objective_id = id,
                        .position = asset::Float3IR{
                            .x = spawn.x, .y = spawn.y + forward_offset,
                            .z = spawn.z},
                        .radius = 32.0F,
                        .choice = gameplay::ObjectiveChoice::Pass,
                        .fired = false,
                    });
            };
            place_trigger(2U, 35.0F);
            place_trigger(3U, 80.0F);
            place_trigger(4U, 125.0F);

            // Enemy NPC (OPENOMEGA_NPC=1): a project-placed patrolling guard
            // ahead of the player on the +Y corridor. It patrols a short +Y
            // segment (facing the corridor) and, per frame, checks whether it
            // sees the player by vision cone + line-of-sight against the level
            // COL; on a sighting it latches to Alerted (rendered blue -> red).
            // Project-placed: authentic NPC spawns / nav Nodes live in the
            // undecoded POP GOB: section (a later decode slice), not the .SO.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            const char *const npc_flag = std::getenv("OPENOMEGA_NPC");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            if (npc_flag != nullptr && npc_flag[0] == '1' && npc_flag[1] == '\0')
            {
                diagnostic_scene_presentation->npc_active = true;
                // Real NOD: nav-graph nodes decoded from the level's DATA.POP.
                const auto &nav_nodes =
                    content_owner->level_game_objects.nav_nodes;
                // The nearest decoded nav-node positions to a spawn, forming an
                // authentic patrol route. (The NOD: adjacency is decoded too, but
                // its neighbor indices are into the full 1613-node array while we
                // decode only the cleanly-walkable subset, so spatial nearest-node
                // routing is used until the variant records are pinned; the patrol
                // points are still real retail nav nodes.)
                const auto nearest_nav_waypoints =
                    [&nav_nodes](const asset::Float3IR &at) {
                        std::vector<asset::Float3IR> route;
                        const std::size_t want =
                            nav_nodes.size() < 4U ? nav_nodes.size() : 4U;
                        std::vector<bool> used(nav_nodes.size(), false);
                        for (std::size_t k = 0U; k < want; ++k)
                        {
                            std::size_t best = nav_nodes.size();
                            double best_d = 0.0;
                            for (std::size_t n = 0U; n < nav_nodes.size(); ++n)
                            {
                                if (used[n])
                                    continue;
                                const auto &p = nav_nodes[n].position;
                                const double dx = static_cast<double>(p.x) - at.x;
                                const double dy = static_cast<double>(p.y) - at.y;
                                const double dz = static_cast<double>(p.z) - at.z;
                                const double d = dx * dx + dy * dy + dz * dz;
                                if (best == nav_nodes.size() || d < best_d)
                                {
                                    best = n;
                                    best_d = d;
                                }
                            }
                            if (best == nav_nodes.size())
                                break;
                            used[best] = true;
                            route.push_back(nav_nodes[best].position);
                        }
                        return route;
                    };
                // Seeds one patrolling guard at a world position: patrols the
                // nearest real nav-node positions (fall back to a short local
                // +/-Y segment if the nav graph is empty), running the full
                // stealth loop.
                const auto seed_guard = [&](const asset::Float3IR &at) {
                    DiagnosticScenePresentation::NpcRuntime npc;
                    npc.params = player_seed_params;
                    npc.state = gameplay::CharacterState{.position = at};
                    auto route = nearest_nav_waypoints(at);
                    if (route.size() >= 2U)
                        npc.waypoints = std::move(route);
                    else
                        npc.waypoints = {
                            asset::Float3IR{.x = at.x, .y = at.y + 5.0F, .z = at.z},
                            asset::Float3IR{.x = at.x, .y = at.y - 5.0F, .z = at.z}};
                    npc.waypoint = 1U;
                    npc.facing = asset::Float3IR{.x = 0.0F, .y = -1.0F, .z = 0.0F};
                    npc.vision = gameplay::NpcVisionParams{
                        .range = 22.0F, .cos_half_angle = 0.5F, .eye_height = 8.0F};
                    diagnostic_scene_presentation->npcs.push_back(std::move(npc));
                };
                // Authentic enemy placement decoded from the level's DATA.POP GOB
                // NPC: section (id + model/type name + world position). The player
                // character model (PC_MALE) is the player start, not an enemy, so
                // it is skipped. Fail-soft: with no decoded spawns, fall back to a
                // few project-placed guards along the player's +Y corridor.
                std::size_t authentic = 0U;
                for (const auto &sp :
                    content_owner->level_game_objects.npc_spawns)
                {
                    if (sp.model.find("PC_MALE") != std::string::npos ||
                        sp.model.find("pc_male") != std::string::npos)
                        continue;
                    seed_guard(sp.position);
                    ++authentic;
                }
                if (authentic != 0U)
                {
                    log->Info("npc",
                        "authentic enemy spawns placed from DATA.POP GOB: " +
                            std::to_string(authentic) + " of " +
                            std::to_string(content_owner->level_game_objects
                                    .npc_spawns.size()) +
                            " NPC records; patrolling " +
                            std::to_string(nav_nodes.size()) + " of " +
                            std::to_string(content_owner->level_game_objects
                                    .nav_node_count) +
                            " decoded nav nodes (hotboxes=" +
                            std::to_string(content_owner->level_game_objects
                                    .hotbox_count) +
                            ")");
                }
                else
                {
                    for (const float ahead : {50.0F, 90.0F, 130.0F})
                        seed_guard(asset::Float3IR{
                            .x = spawn.x, .y = spawn.y + ahead, .z = spawn.z});
                    log->Info("npc",
                        "enemy NPCs placed (project-placed fallback): " +
                            std::to_string(
                                diagnostic_scene_presentation->npcs.size()) +
                            " guards patrolling the +Y corridor");
                }
            }
        }
    }

    log->Info("startup", "runtime services ready with " +
                             std::to_string(jobs->worker_count()) + " workers and " +
                             std::string(audio->driver_name()) + " audio");

    return OmegaApp(std::move(native_persistence), std::move(config_owner), std::move(content_owner),
                    std::move(stderr_sink), std::move(ring_sink), std::move(log), std::move(jobs), std::move(assets),
                    std::move(frame_scheduler), std::move(input), std::move(simulation), debug_locomotion_entity,
                    std::move(platform), std::move(sdl_input), std::move(audio), std::move(host),
                    std::move(opening_movie_player), opening_movie_texture,
                     std::move(opening_movie_draw_list), boot_sequence_state, diagnostic_texture,
                     diagnostic_actor_marker_texture,
                     std::move(diagnostic_actor_draw_list),
                     std::move(diagnostic_scene_presentation),
                     std::move(front_end_presentation),
                    std::move(first_profile_presentation), diagnostic_controls_texture,
                    diagnostic_asset_topology_texture, diagnostic_asset_transfer_texture,
                    std::move(diagnostic_hidden_draw_list),
                    std::move(diagnostic_controls_draw_list),
                    std::move(diagnostic_asset_topology_draw_list), content_stage,
                    front_end_startup_model, presentation_mode);
}

OmegaApp::OmegaApp(
    std::unique_ptr<NativePersistence> native_persistence, std::unique_ptr<runtime::ConfigStore> config,
    std::unique_ptr<runtime::ContentStartupState> content, std::unique_ptr<runtime::StderrLogSink> stderr_sink,
    std::unique_ptr<runtime::RingLogSink> ring_sink, std::unique_ptr<runtime::LogService> log,
    std::unique_ptr<runtime::JobService> jobs, std::unique_ptr<runtime::AssetService> assets,
    std::unique_ptr<runtime::FrameScheduler> frame_scheduler, std::unique_ptr<runtime::InputTracker> input,
    std::unique_ptr<simulation::SimulationWorld> simulation, const simulation::EntityId debug_locomotion_entity,
    std::unique_ptr<SdlPlatformService> platform, std::unique_ptr<SdlInputService> sdl_input,
    std::unique_ptr<SdlAudioService> audio, std::unique_ptr<SdlGpuHost> host,
    std::unique_ptr<OpeningMoviePlayback> opening_movie_player,
    const runtime::RenderTextureHandle opening_movie_texture,
    runtime::RenderDrawList opening_movie_draw_list,
    const BootSequenceState boot_sequence_state,
    const runtime::RenderTextureHandle diagnostic_texture,
    const runtime::RenderTextureHandle diagnostic_actor_marker_texture,
    runtime::RenderDrawList diagnostic_actor_draw_list,
    std::unique_ptr<DiagnosticScenePresentation> diagnostic_scene_presentation,
    FrontEndPresentation front_end_presentation,
    std::optional<FrontEndPresentation> first_profile_presentation,
    const runtime::RenderTextureHandle diagnostic_controls_texture,
    const runtime::RenderTextureHandle diagnostic_asset_topology_texture,
    const runtime::RenderTextureHandle diagnostic_asset_transfer_texture,
    runtime::RenderDrawList diagnostic_hidden_draw_list,
    runtime::RenderDrawList diagnostic_controls_draw_list,
    runtime::RenderDrawList diagnostic_asset_topology_draw_list, const runtime::ContentStartupStage content_stage,
    const FrontEndStartupModel front_end_startup_model,
    const runtime::FrontEndPresentationMode presentation_mode) noexcept
    : native_persistence_(std::move(native_persistence)), config_(std::move(config)), content_(std::move(content)),
      stderr_sink_(std::move(stderr_sink)), ring_sink_(std::move(ring_sink)), log_(std::move(log)),
      jobs_(std::move(jobs)), assets_(std::move(assets)), frame_scheduler_(std::move(frame_scheduler)),
      input_(std::move(input)), simulation_(std::move(simulation)), debug_locomotion_entity_(debug_locomotion_entity),
      platform_(std::move(platform)), sdl_input_(std::move(sdl_input)), audio_(std::move(audio)),
      host_(std::move(host)), opening_movie_player_(std::move(opening_movie_player)),
      opening_movie_texture_(opening_movie_texture),
      opening_movie_draw_list_(std::move(opening_movie_draw_list)),
      boot_sequence_state_(boot_sequence_state), diagnostic_texture_(diagnostic_texture),
      diagnostic_actor_marker_texture_(diagnostic_actor_marker_texture),
      diagnostic_actor_draw_list_(std::move(diagnostic_actor_draw_list)),
      diagnostic_scene_presentation_(std::move(diagnostic_scene_presentation)),
      front_end_presentation_(std::move(front_end_presentation)),
      first_profile_presentation_(std::move(first_profile_presentation)),
      diagnostic_controls_texture_(diagnostic_controls_texture),
      diagnostic_asset_topology_texture_(diagnostic_asset_topology_texture),
      diagnostic_asset_transfer_texture_(diagnostic_asset_transfer_texture),
      diagnostic_hidden_draw_list_(std::move(diagnostic_hidden_draw_list)),
      diagnostic_controls_draw_list_(std::move(diagnostic_controls_draw_list)),
      diagnostic_asset_topology_draw_list_(std::move(diagnostic_asset_topology_draw_list)),
      content_stage_(content_stage), presentation_mode_(presentation_mode),
      front_end_startup_model_(front_end_startup_model),
      front_end_state_(PlanProjectFrontEndStartupState(
          front_end_startup_model_.total_profiles,
          front_end_startup_model_.visible_profiles,
          FrontEndCapabilities{
              .can_create_first_profile = first_profile_presentation_.has_value(),
          })),
      can_create_first_profile_(first_profile_presentation_.has_value()),
      can_create_first_character_(false),
      // native_persistence_ is the first declared member, so it is already
      // initialized here. ConfirmActiveProfile is the only way to publish
      // active_profile_id_, so a composition without persistence has no
      // authorization source and must not be gated against one.
      requires_active_profile_for_diagnostic_play_(native_persistence_ != nullptr),
      requires_active_character_for_diagnostic_play_(
          native_persistence_ != nullptr)
{
}

OmegaApp::~OmegaApp() noexcept
{
    const bool opening_movie_audio_contained = ContainOpeningMovieAudio();
    if (!opening_movie_audio_contained && log_ != nullptr)
    {
        try
        {
            log_->Warning("shutdown",
                "opening movie audio containment failed; final SDL audio cleanup will continue");
        }
        catch (...)
        {
            // Destruction remains noexcept even if bounded shutdown logging
            // cannot allocate.
        }
    }
    opening_movie_draw_list_ = {};
    opening_movie_player_.reset();
    diagnostic_asset_topology_draw_list_ = {};
    diagnostic_controls_draw_list_ = {};
    diagnostic_actor_draw_list_ = {};
    ReleaseDiagnosticScenePresentation();
    ReleaseCharacterPresentation(first_character_presentation_);
    ReleaseCharacterPresentation(character_presentation_);
    const auto clear_front_end_draw_lists = [](FrontEndPresentation& presentation) noexcept
    {
        if (presentation.profile_active_draw_lists)
        {
            for (auto& active_draw_lists :
                 *presentation.profile_active_draw_lists)
            {
                for (runtime::RenderDrawList& draw_list : active_draw_lists)
                    draw_list = {};
            }
        }
        for (runtime::RenderDrawList& draw_list :
             presentation.profile_selection_draw_lists)
            draw_list = {};
        presentation.profiles_draw_list = {};
        for (runtime::RenderDrawList& draw_list : presentation.main_draw_lists)
            draw_list = {};
    };
    if (first_profile_presentation_)
        clear_front_end_draw_lists(*first_profile_presentation_);
    clear_front_end_draw_lists(front_end_presentation_);
    diagnostic_hidden_draw_list_ = {};

    const auto release_texture = [this](const runtime::RenderTextureHandle texture,
                                     const std::string_view failure_message) noexcept
    {
        if (host_ == nullptr || !texture.valid())
            return;

        bool release_failed = false;
        try
        {
            release_failed = !host_->ReleaseTexture(texture);
        }
        catch (...)
        {
            // The host remains the authoritative owner and releases all surviving
            // resources.
            release_failed = true;
        }

        if (release_failed && log_ != nullptr)
        {
            try
            {
                log_->Warning("shutdown", failure_message);
            }
            catch (...)
            {
                // Destruction remains noexcept even if bounded shutdown logging
                // cannot allocate.
            }
        }
    };

    release_texture(opening_movie_texture_, "opening movie texture release failed; SDL/GPU host cleanup will retry");
    release_texture(diagnostic_asset_transfer_texture_, "diagnostic asset transfer texture release failed; SDL/GPU "
                                                        "host cleanup will retry");
    release_texture(diagnostic_asset_topology_texture_, "diagnostic asset topology texture release failed; SDL/GPU "
                                                        "host cleanup will retry");
    release_texture(diagnostic_controls_texture_, "diagnostic controls texture release failed; SDL/GPU host "
                                                  "cleanup will retry");
    if (first_profile_presentation_)
    {
        release_texture(first_profile_presentation_->profiles_texture,
            "inactive front-end profiles texture release failed; SDL/GPU host cleanup will retry");
        release_texture(first_profile_presentation_->main_texture,
            "inactive front-end main texture release failed; SDL/GPU host cleanup will retry");
    }
    release_texture(front_end_presentation_.profiles_texture,
        "front-end profiles texture release failed; SDL/GPU host cleanup will retry");
    release_texture(front_end_presentation_.main_texture,
        "front-end main texture release failed; SDL/GPU host cleanup will retry");
    release_texture(diagnostic_actor_marker_texture_,
        "diagnostic actor marker texture release failed; SDL/GPU host cleanup will retry");
    release_texture(diagnostic_texture_, "diagnostic texture release failed; SDL/GPU host cleanup will retry");
    opening_movie_texture_ = {};
    diagnostic_asset_topology_texture_ = {};
    diagnostic_asset_transfer_texture_ = {};
    diagnostic_controls_texture_ = {};
    if (first_profile_presentation_)
    {
        first_profile_presentation_->profiles_texture = {};
        first_profile_presentation_->main_texture = {};
    }
    front_end_presentation_.profiles_texture = {};
    front_end_presentation_.main_texture = {};
    diagnostic_actor_marker_texture_ = {};
    diagnostic_texture_ = {};
}

void OmegaApp::ReleaseDiagnosticScenePresentation() noexcept
{
    if (!diagnostic_scene_presentation_)
        return;

    diagnostic_scene_presentation_->draw_list = {};
    diagnostic_scene_presentation_->environment_draw_list = {};
    diagnostic_scene_presentation_->overlay_draw_list = {};
    diagnostic_scene_presentation_->environment_command_count = 0U;
    diagnostic_scene_presentation_->actor_mesh_handle = {};
    diagnostic_scene_presentation_->camera = {};
    while (diagnostic_scene_presentation_->mesh_count != 0U)
    {
        --diagnostic_scene_presentation_->mesh_count;
        const runtime::RenderMeshHandle handle =
            diagnostic_scene_presentation_->mesh_handles
                [diagnostic_scene_presentation_->mesh_count];
        diagnostic_scene_presentation_->mesh_handles
            [diagnostic_scene_presentation_->mesh_count] = {};
        if (host_ == nullptr || !handle.valid())
            continue;

        bool release_failed = false;
        try
        {
            release_failed = !host_->ReleaseRenderMesh(handle);
        }
        catch (...)
        {
            release_failed = true;
        }
        if (release_failed && log_ != nullptr)
        {
            try
            {
                log_->Warning("shutdown",
                    "diagnostic scene mesh release failed; SDL/GPU host cleanup will retry");
            }
            catch (...)
            {
                // Destruction remains noexcept even if bounded shutdown logging
                // cannot allocate.
            }
        }
    }
    diagnostic_scene_presentation_.reset();
}

std::expected<OmegaApp::CharacterPresentation, std::string>
OmegaApp::BuildCharacterPresentation(
    const FrontEndCharacterStartupModel& model,
    const FrontEndCapabilities capabilities)
{
    if (host_ == nullptr || !diagnostic_texture_.valid())
    {
        return std::unexpected(
            std::string{"character presentation requires the live GPU host"});
    }

    constexpr runtime::RenderSourceRectQ16 full_source{
        .left = 0U,
        .top = 0U,
        .right = runtime::kNormalizedRenderExtent,
        .bottom = runtime::kNormalizedRenderExtent,
    };
    constexpr runtime::RenderTargetRectQ16 full_target{
        .left = 0U,
        .top = 0U,
        .right = runtime::kNormalizedRenderExtent,
        .bottom = runtime::kNormalizedRenderExtent,
    };
    constexpr runtime::RenderTargetRectQ16 menu_target{
        .left = 2048U,
        .top = 2048U,
        .right = 26624U,
        .bottom = 15872U,
    };
    constexpr runtime::RenderSourceRectQ16 selection_source{
        .left = 0U,
        .top = 0U,
        .right = 512U,
        .bottom = 512U,
    };
    constexpr std::array selection_targets{
        runtime::RenderTargetRectQ16{
            .left = 3584U,
            .top = 7424U,
            .right = 4352U,
            .bottom = 8960U,
        },
        runtime::RenderTargetRectQ16{
            .left = 3584U,
            .top = 9344U,
            .right = 4352U,
            .bottom = 10880U,
        },
        runtime::RenderTargetRectQ16{
            .left = 3584U,
            .top = 11264U,
            .right = 4352U,
            .bottom = 12800U,
        },
    };
    static_assert(selection_targets.size() == kFrontEndVisibleCharacters);

    const runtime::DebugImage image =
        BuildProjectFrontEndCharactersImage(model, capabilities);
    auto uploaded = host_->UploadRgba8Texture(runtime::Rgba8TextureUploadView{
        .width = image.width,
        .height = image.height,
        .pixels = image.pixels(),
    });
    if (!uploaded)
    {
        return std::unexpected(
            "SDL/GPU front-end characters texture upload: " +
            uploaded.error());
    }

    CharacterPresentation presentation;
    presentation.texture = *uploaded;
    std::array<runtime::RenderTextureBlitCommand, 3U> commands{
        runtime::RenderTextureBlitCommand{
            .texture = diagnostic_texture_,
            .source = full_source,
            .destination = full_target,
            .fit_mode = runtime::RenderTextureFitMode::Contain,
            .filter_mode = runtime::RenderTextureFilterMode::Nearest,
        },
        runtime::RenderTextureBlitCommand{
            .texture = presentation.texture,
            .source = full_source,
            .destination = menu_target,
            .fit_mode = runtime::RenderTextureFitMode::Stretch,
            .filter_mode = runtime::RenderTextureFilterMode::Nearest,
        },
        {},
    };

    const auto fail_after_upload = [&](std::string message)
        -> std::expected<CharacterPresentation, std::string>
    {
        static_cast<void>(host_->ReleaseTexture(presentation.texture));
        presentation.texture = {};
        return std::unexpected(std::move(message));
    };

    auto base = runtime::RenderDrawList::Create(
        std::span<const runtime::RenderTextureBlitCommand>{commands.data(), 2U});
    if (!base)
    {
        return fail_after_upload(
            "SDL/GPU front-end characters draw-list creation failed");
    }
    presentation.draw_list = std::move(*base);

    for (std::size_t slot = 0U; slot < selection_targets.size(); ++slot)
    {
        commands[2U] = runtime::RenderTextureBlitCommand{
            .texture = presentation.texture,
            .source = selection_source,
            .destination = selection_targets[slot],
            .fit_mode = runtime::RenderTextureFitMode::Stretch,
            .filter_mode = runtime::RenderTextureFilterMode::Nearest,
        };
        auto selected = runtime::RenderDrawList::Create(commands);
        if (!selected)
        {
            return fail_after_upload(
                "SDL/GPU front-end character selection draw-list creation failed");
        }
        presentation.selection_draw_lists[slot] = std::move(*selected);
    }
    return presentation;
}

void OmegaApp::ReleaseCharacterPresentation(
    std::optional<CharacterPresentation>& presentation) noexcept
{
    if (!presentation)
        return;
    for (runtime::RenderDrawList& draw_list :
         presentation->selection_draw_lists)
    {
        draw_list = {};
    }
    presentation->draw_list = {};
    const runtime::RenderTextureHandle texture = presentation->texture;
    presentation->texture = {};
    presentation.reset();
    if (host_ == nullptr || !texture.valid())
        return;

    bool release_failed = false;
    try
    {
        release_failed = !host_->ReleaseTexture(texture);
    }
    catch (...)
    {
        release_failed = true;
    }
    if (release_failed && log_ != nullptr)
    {
        try
        {
            log_->Warning("front-end",
                "character texture release failed; GPU host cleanup will retry");
        }
        catch (...)
        {
            // Release remains noexcept even if bounded shutdown logging cannot
            // allocate.
        }
    }
}

bool OmegaApp::ContainOpeningMovieAudio() noexcept
{
    std::fill(opening_movie_audio_scratch_.begin(), opening_movie_audio_scratch_.end(),
        std::int16_t{0});
    opening_movie_audio_clock_ = {};
    return audio_ == nullptr || audio_->DiscardOpeningMovieAudio();
}

OpeningMovieAudioFaultCounters OmegaApp::OpeningMovieAudioFaultCountersOf(
    const AudioServiceSnapshot& snapshot) noexcept
{
    return OpeningMovieAudioFaultCounters{
        .callback_failures = snapshot.callback_failures,
        .opening_movie_control_failures = snapshot.opening_movie_control_failures,
        .opening_movie_underrun_frames = snapshot.opening_movie_underrun_frames,
        .opening_movie_queue_rejections = snapshot.opening_movie_queue_rejections,
    };
}

void OmegaApp::ReleaseOpeningMovieForFrontEnd()
{
    opening_movie_skip_armed_ = false;
    if (IsBootSequenceActive(boot_sequence_state_))
    {
        boot_sequence_state_ = BootSequenceState{
            .phase = BootSequencePhase::Failed,
            .position_ticks = boot_sequence_state_.position_ticks,
            .duration_ticks = boot_sequence_state_.duration_ticks,
        };
    }

    opening_movie_player_.reset();
    opening_movie_draw_list_ = {};
    if (!opening_movie_texture_.valid())
        return;

    auto released = host_->ReleaseTexture(opening_movie_texture_);
    if (!released)
    {
        log_->Warning("opening_movie",
            "opening movie texture release failed; host cleanup will retry");
        return;
    }
    opening_movie_texture_ = {};
}

bool OmegaApp::FinishOpeningMovieFrontEndTransition(
    AudioServiceSnapshot& audio_fault_baseline)
{
    const bool contained = ContainOpeningMovieAudio();
    if (audio_ != nullptr)
        audio_fault_baseline = audio_->Snapshot();
    ReleaseOpeningMovieForFrontEnd();
    return contained;
}

OmegaApp::OmegaApp(OmegaApp&&) noexcept = default;

std::expected<RunResult, std::string> OmegaApp::Run(
    const int frame_limit, const int screenshot_frame)
{
    auto first_elapsed_override =
        std::exchange(next_run_elapsed_override_for_testing_, std::nullopt);
    RunLoopResult loop = RunLoop(
        frame_limit, nullptr, std::move(first_elapsed_override), screenshot_frame);
    if (loop.operational_error)
        return std::unexpected(std::move(*loop.operational_error));
    return loop.result;
}

std::expected<RunCaptureOutcome, std::string> OmegaApp::RunWithCapture(
    const int frame_limit)
{
    auto first_elapsed_override =
        std::exchange(next_run_elapsed_override_for_testing_, std::nullopt);
    const auto planned = detail::PlanFiniteRunCapture(
        frame_limit, input_->next_frame_index());
    if (!planned)
    {
        return std::unexpected(std::string(
            detail::FiniteRunCapturePlanErrorMessage(planned.error())));
    }

    auto created = runtime::RunCaptureSession::Create(
        planned->session, input_->bindings().actions());
    if (!created)
        return std::unexpected(detail::FormatRunCaptureSessionError(created.error()));

    runtime::RunCaptureSession capture_session = std::move(*created);
    const runtime::FrameSchedulerState scheduler_state_before =
        frame_scheduler_->Snapshot();

    if (planned->requested_frames == 0U)
    {
        auto finished = std::move(capture_session).Finish();
        const runtime::FrameSchedulerState scheduler_state_after =
            frame_scheduler_->Snapshot();
        if (!finished)
        {
            return RunCaptureOutcome(planned->requested_frames, RunResult{},
                RunCaptureCompletion::CaptureFailure, scheduler_state_before,
                scheduler_state_after,
                detail::FormatRunCaptureSessionError(finished.error()), std::nullopt);
        }
        return RunCaptureOutcome(planned->requested_frames, RunResult{},
            RunCaptureCompletion::FrameLimitReached, scheduler_state_before,
            scheduler_state_after, std::nullopt,
            std::optional<runtime::RunCaptureTracePair>{
                std::in_place, std::move(*finished)});
    }

    RunLoopResult loop = RunLoop(
        frame_limit, &capture_session, std::move(first_elapsed_override));
    const runtime::FrameSchedulerState scheduler_state_after =
        frame_scheduler_->Snapshot();

    if (loop.capture_error)
    {
        std::optional<std::string> failure{
            std::in_place,
            detail::FormatRunCaptureSessionError(*loop.capture_error)};
        auto finished = std::move(capture_session).Finish();
        if (!finished)
        {
            return RunCaptureOutcome(planned->requested_frames, loop.result,
                RunCaptureCompletion::CaptureFailure, scheduler_state_before,
                scheduler_state_after, std::move(failure), std::nullopt);
        }
        return RunCaptureOutcome(planned->requested_frames, loop.result,
            RunCaptureCompletion::CaptureFailure, scheduler_state_before,
            scheduler_state_after, std::move(failure),
            std::optional<runtime::RunCaptureTracePair>{
                std::in_place, std::move(*finished)});
    }

    auto finished = std::move(capture_session).Finish();
    if (!finished)
    {
        return RunCaptureOutcome(planned->requested_frames, loop.result,
            RunCaptureCompletion::CaptureFailure, scheduler_state_before,
            scheduler_state_after,
            detail::FormatRunCaptureSessionError(finished.error()), std::nullopt);
    }

    RunCaptureCompletion completion = RunCaptureCompletion::FrameLimitReached;
    if (loop.operational_error)
        completion = RunCaptureCompletion::OperationalFailure;
    else if (loop.result.quit_requested)
        completion = RunCaptureCompletion::QuitRequested;

    return RunCaptureOutcome(planned->requested_frames, loop.result, completion,
        scheduler_state_before, scheduler_state_after,
        std::move(loop.operational_error),
        std::optional<runtime::RunCaptureTracePair>{
            std::in_place, std::move(*finished)});
}

OmegaApp::RunLoopResult OmegaApp::RunLoop(
    const int frame_limit, runtime::RunCaptureSession* const capture_session,
    std::optional<std::chrono::nanoseconds> first_elapsed_override,
    const int screenshot_frame)
{
    using Clock = std::chrono::steady_clock;

    log_->Info("runtime", "entering native host loop");
    LoadRetailFrontEndBundleIfEnabled();

    // Dev-only headless capture aid: OPENOMEGA_START_DIAGNOSTIC_PLAY boots the
    // project diagnostic build straight into the loaded level's flat-3D scene
    // (FrontEndMode::DiagnosticPlay) instead of the diagnostic menu, so an
    // automated --screenshot-frame captures the level, not the menu. Gated on
    // DeveloperDiagnostics + an actually-loaded level scene; it opens the
    // documented synthetic, persistence-free start (see CurrentFrontEndCapabilities)
    // so the gameplay gate is satisfied without interactive profile/character
    // confirmation. Never affects the retail path or the normal interactive flow.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* const start_diagnostic_play =
        std::getenv("OPENOMEGA_START_DIAGNOSTIC_PLAY");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (start_diagnostic_play != nullptr && start_diagnostic_play[0] == '1' &&
        start_diagnostic_play[1] == '\0' &&
        presentation_mode_ ==
            runtime::FrontEndPresentationMode::DeveloperDiagnostics &&
        diagnostic_scene_presentation_)
    {
        synthetic_diagnostic_play_start_ = true;
        front_end_state_ = InitialFrontEndState();
        front_end_state_.mode = FrontEndMode::DiagnosticPlay;
        log_->Info("runtime",
            "OPENOMEGA_START_DIAGNOSTIC_PLAY: booting into the loaded level scene");
    }

    // Dev-only: OPENOMEGA_START_SCREEN=multiplayer opens the project multiplayer
    // menu overlay (Host Game / Direct Connect / Server List with stub session
    // actions) for capture and interactive use. DeveloperDiagnostics only;
    // separate from the retail OPENOMEGA_FRONTEND_START_SCREEN override and from
    // the project ReduceFrontEnd flow. Seeds default field text so a headless
    // capture shows content; live typing edits it.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* const start_multiplayer_menu = std::getenv("OPENOMEGA_START_SCREEN");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    const std::string_view requested_mp_screen =
        start_multiplayer_menu != nullptr ? std::string_view(start_multiplayer_menu)
                                          : std::string_view{};
    const bool mp_requested =
        requested_mp_screen == "multiplayer" || requested_mp_screen == "hostgame" ||
        requested_mp_screen == "directconnect" || requested_mp_screen == "serverlist";
    if (mp_requested &&
        presentation_mode_ ==
            runtime::FrontEndPresentationMode::DeveloperDiagnostics)
    {
        mp_menu_active_ = true;
        mp_menu_state_ = multiplayer::MpMenuState{};
        if (requested_mp_screen == "hostgame")
            mp_menu_state_.screen = multiplayer::MpScreen::HostGame;
        else if (requested_mp_screen == "directconnect")
            mp_menu_state_.screen = multiplayer::MpScreen::DirectConnect;
        else if (requested_mp_screen == "serverlist")
            mp_menu_state_.screen = multiplayer::MpScreen::ServerList;
        // Seed default field text so a headless capture shows populated fields;
        // live typing edits them interactively once text input is wired.
        for (const char symbol : std::string_view("OMEGA HOST"))
            multiplayer::MpTextInsert(mp_menu_state_.server_name, symbol,
                multiplayer::MpTextFieldKind::Name);
        for (const char symbol : std::string_view("127.0.0.1:33333"))
            multiplayer::MpTextInsert(mp_menu_state_.address, symbol,
                multiplayer::MpTextFieldKind::Address);
        UpdateMultiplayerMenuPresentation();
        log_->Info("runtime",
            "OPENOMEGA_START_SCREEN multiplayer menu overlay active");
    }
    RunResult result;
    bool running = true;
    auto previous_frame = Clock::now();
    AudioServiceSnapshot audio_fault_baseline = audio_->Snapshot();
    const auto front_end_gate_failure = [this]() -> std::optional<std::string>
    {
        const auto authorized = AuthorizeCurrentFrontEndPresentation();
        if (authorized)
            return std::nullopt;
        return "front-end presentation [" +
               std::string(runtime::FrontEndPresentationGateErrorCodeName(
                   authorized.error().code)) +
               "]: " + std::string(authorized.error().message);
    };
    while (running && (frame_limit < 0 || result.rendered_frames < frame_limit))
    {
        const auto next_rendered_frame_count =
            detail::CheckedNextRenderedFrameCount(result.rendered_frames);
        if (!next_rendered_frame_count)
        {
            (void)ContainOpeningMovieAudio();
            jobs_->WaitForIdle();
            constexpr std::string_view error =
                "run-local rendered frame counter exhausted";
            log_->Error("render", error);
            return RunLoopResult{
                .result = result,
                .operational_error = std::string(error),
                .capture_error = std::nullopt,
            };
        }

        const InputPumpResult events = sdl_input_->PumpEvents(*input_, *log_);
        const runtime::InputSnapshot input_snapshot = input_->EndFrame();
        const bool movie_was_active = IsBootSequenceActive(boot_sequence_state_);
        if (!movie_was_active)
        {
            if (auto presentation_error = front_end_gate_failure())
            {
                (void)ContainOpeningMovieAudio();
                jobs_->WaitForIdle();
                log_->Error("presentation", *presentation_error);
                return RunLoopResult{
                    .result = result,
                    .operational_error = std::move(presentation_error),
                    .capture_error = std::nullopt,
                };
            }
        }
        if (capture_session != nullptr)
        {
            const auto captured = capture_session->AppendInput(input_snapshot);
            if (!captured)
            {
                (void)ContainOpeningMovieAudio();
                jobs_->WaitForIdle();
                return RunLoopResult{
                    .result = result,
                    .operational_error = std::nullopt,
                    .capture_error = captured.error(),
                };
            }
        }
        ++result.input_frames;
        if (input_snapshot.rejected_event_count() != 0U)
        {
            log_->Warning("input", "rejected " +
                                       std::to_string(input_snapshot.rejected_event_count()) +
                                       " host events in one frame");
        }
        if (capture_session != nullptr)
        {
            const bool host_quit_requested = events.quit_requested;
            const bool logical_quit_pressed =
                !movie_was_active && input_snapshot.WasPressed(kQuitAction);
            if (host_quit_requested || logical_quit_pressed)
            {
                const auto marked = capture_session->MarkTerminal(
                    host_quit_requested, logical_quit_pressed);
                if (!marked)
                {
                    (void)ContainOpeningMovieAudio();
                    jobs_->WaitForIdle();
                    return RunLoopResult{
                        .result = result,
                        .operational_error = std::nullopt,
                        .capture_error = marked.error(),
                    };
                }
                running = false;
                result.quit_requested = true;
                break;
            }
        }
        else if (events.quit_requested ||
                 (!movie_was_active && input_snapshot.WasPressed(kQuitAction)))
        {
            running = false;
            result.quit_requested = true;
            break;
        }

        const auto current_frame = Clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            current_frame - previous_frame);
        previous_frame = current_frame;
        if (first_elapsed_override)
        {
            elapsed = *first_elapsed_override;
            first_elapsed_override.reset();
        }
        if (capture_session != nullptr)
        {
            const auto captured = capture_session->AppendElapsed(elapsed);
            if (!captured)
            {
                (void)ContainOpeningMovieAudio();
                jobs_->WaitForIdle();
                return RunLoopResult{
                    .result = result,
                    .operational_error = std::nullopt,
                    .capture_error = captured.error(),
                };
            }
        }

        const bool diagnostic_play_input_context =
            !movie_was_active &&
            front_end_state_.mode == FrontEndMode::DiagnosticPlay;
        if (movie_was_active)
        {
            const bool primary_pressed = opening_movie_skip_armed_ &&
                (events.keyboard_or_mouse_pressed ||
                    input_snapshot.WasPressed(kFrontEndPrimaryAction) ||
                    input_snapshot.WasPressed(kDebugFireAction));
            bool source_failed = false;
            bool source_completed = false;
            if (!primary_pressed)
            {
                AudioServiceSnapshot movie_audio = audio_->Snapshot();
                const OpeningMovieAudioFault movie_audio_fault =
                    ClassifyOpeningMovieAudioFault(
                        OpeningMovieAudioFaultCountersOf(audio_fault_baseline),
                        OpeningMovieAudioFaultCountersOf(movie_audio));
                if (DisposeOpeningMovieAudioFault(movie_audio_fault, true) ==
                    OpeningMovieAudioFaultDisposition::FailOpen)
                {
                    log_->Warning("opening_movie",
                        OpeningMovieAudioFaultMessage(movie_audio_fault));
                    source_failed = true;
                }
                else if (opening_movie_player_ == nullptr)
                {
                    source_failed = true;
                }
                else
                {
                    std::chrono::nanoseconds presentation_elapsed{0};
                    const OpeningMovieAudioClockResult clock_step =
                        AdvanceOpeningMovieAudioClock(
                            opening_movie_audio_clock_,
                            OpeningMovieAudioClockObservation{
                                .session_generation =
                                    movie_audio.opening_movie_session_generation,
                                .timeline_frames =
                                    movie_audio.opening_movie_timeline_frames,
                            });
                    if (!clock_step)
                    {
                        log_->Warning("opening_movie",
                            OpeningMovieAudioClockErrorMessage(clock_step.error()));
                        source_failed = true;
                    }
                    else
                    {
                        opening_movie_audio_clock_ = clock_step->state;
                        presentation_elapsed = clock_step->presentation_elapsed;
                    }

                    if (!source_failed)
                    {
                        auto movie_update = opening_movie_player_->Advance(presentation_elapsed);
                        if (!movie_update)
                        {
                            log_->Warning("opening_movie",
                                OpeningMoviePlayerErrorMessage(movie_update.error().code));
                            source_failed = true;
                        }
                        else
                        {
                            const bool video_completed = movie_update->status ==
                                OpeningMoviePlayerStatus::Completed;
                            if (movie_update->frame_updated)
                            {
                                if (movie_update->current_frame == nullptr)
                                {
                                    log_->Warning("opening_movie",
                                        "opening movie published an empty frame update");
                                    source_failed = true;
                                }
                                else
                                {
                                    const media::Rgba8VideoFrame& frame =
                                        *movie_update->current_frame;
                                    auto updated = host_->UpdateRgba8Texture(
                                        opening_movie_texture_,
                                        runtime::Rgba8TextureUploadView{
                                            .width = frame.width,
                                            .height = frame.height,
                                            .pixels = frame.pixels,
                                        });
                                    if (!updated)
                                    {
                                        log_->Warning("opening_movie",
                                            "opening movie presentation texture update failed");
                                        source_failed = true;
                                    }
                                }
                            }

                            if (!source_failed && movie_update->current_frame != nullptr &&
                                !opening_movie_player_->audio_finished())
                            {
                                const std::uint64_t available_frames =
                                    audio_->OpeningMovieAvailableFrames();
                                if (available_frames >= kOpeningMovieAudioRefillFrames)
                                {
                                    const auto output = std::span<std::int16_t>(
                                        opening_movie_audio_scratch_)
                                                            .first(static_cast<std::size_t>(
                                                                available_frames *
                                                                SdlAudioService::kChannelCount));
                                    auto decoded =
                                        opening_movie_player_->ReadAudioFrames(output);
                                    if (!decoded)
                                    {
                                        log_->Warning("opening_movie",
                                            OpeningMoviePlayerErrorMessage(
                                                decoded.error().code));
                                        source_failed = true;
                                    }
                                    else if (*decoded > available_frames)
                                    {
                                        log_->Warning("opening_movie",
                                            "opening movie PCM decode exceeded the requested "
                                            "refill");
                                        source_failed = true;
                                    }
                                    else if (*decoded != 0U)
                                    {
                                        const auto samples = output.first(
                                            static_cast<std::size_t>(
                                                *decoded * SdlAudioService::kChannelCount));
                                        const AudioServiceSnapshot before_queue =
                                            audio_->Snapshot();
                                        if (!audio_->QueueOpeningMoviePcm16(samples,
                                                opening_movie_player_->audio_finished()))
                                        {
                                            log_->Warning("opening_movie",
                                                "opening movie audio queue rejected a bounded "
                                                "refill");
                                            source_failed = true;
                                        }
                                        else if (!opening_movie_audio_clock_.started)
                                        {
                                            const AudioServiceSnapshot after_queue =
                                                audio_->Snapshot();
                                            const OpeningMovieAudioClockResult clock_start =
                                                StartOpeningMovieAudioClock(
                                                    opening_movie_audio_clock_,
                                                    OpeningMovieAudioClockStartSignals{
                                                        .video_frame_available =
                                                            movie_update->current_frame != nullptr,
                                                        .pcm_queue_accepted = true,
                                                        .before_queue =
                                                            OpeningMovieAudioClockObservation{
                                                                .session_generation = before_queue
                                                                    .opening_movie_session_generation,
                                                                .timeline_frames = before_queue
                                                                    .opening_movie_timeline_frames,
                                                            },
                                                        .after_queue =
                                                            OpeningMovieAudioClockObservation{
                                                                .session_generation = after_queue
                                                                    .opening_movie_session_generation,
                                                                .timeline_frames = after_queue
                                                                    .opening_movie_timeline_frames,
                                                            },
                                                    });
                                            if (!clock_start)
                                            {
                                                log_->Warning("opening_movie",
                                                    OpeningMovieAudioClockErrorMessage(
                                                        clock_start.error()));
                                                source_failed = true;
                                            }
                                            else
                                            {
                                                opening_movie_audio_clock_ = clock_start->state;
                                            }
                                        }
                                    }
                                }
                            }

                            if (!source_failed && video_completed &&
                                opening_movie_player_->audio_finished())
                            {
                                const AudioServiceSnapshot audio = audio_->Snapshot();
                                source_completed = audio.opening_movie_queued_frames == 0U &&
                                    !audio.opening_movie_active;
                            }
                        }
                    }
                }
            }

            const auto elapsed_microseconds =
                std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
            const std::uint64_t elapsed_ticks = elapsed_microseconds > 0
                ? static_cast<std::uint64_t>(elapsed_microseconds)
                : 0U;
            const BootSequenceReduction boot = ReduceBootSequence(
                boot_sequence_state_,
                BootSequenceInput{
                    .elapsed_ticks = elapsed_ticks,
                    .primary_pressed = primary_pressed,
                    .source_failed = source_failed,
                    .source_completed = source_completed,
                });
            boot_sequence_state_ = boot.state;
            if (boot.entered_front_end)
            {
                if (boot.completion_cause == BootSequenceCompletionCause::SafetyTimeout)
                {
                    log_->Warning("opening_movie",
                        "opening movie safety timeout reached; entering front end");
                }
                const bool contained = FinishOpeningMovieFrontEndTransition(
                    audio_fault_baseline);
                if (!contained)
                {
                    jobs_->WaitForIdle();
                    constexpr std::string_view error =
                        "opening movie audio containment failed during the front-end transition";
                    log_->Error("opening_movie", error);
                    return RunLoopResult{
                        .result = result,
                        .operational_error = std::string(error),
                        .capture_error = std::nullopt,
                    };
                }
                if (auto presentation_error = front_end_gate_failure())
                {
                    jobs_->WaitForIdle();
                    log_->Error("presentation", *presentation_error);
                    return RunLoopResult{
                        .result = result,
                        .operational_error = std::move(presentation_error),
                        .capture_error = std::nullopt,
                    };
                }
            }
        }
        else if (mp_menu_active_)
        {
            // Project multiplayer menu overlay: step the pure MP reducer from the
            // resolved front-end input edges (arrows/gamepad map to up/down/select/
            // back) instead of the project ReduceFrontEnd flow, and recompose only
            // when the menu state changes. Live text entry into the fields is a
            // follow-up (needs SDL text-input events); fields carry seeded defaults.
            const auto mp_edges = ResolveFrontEndInputEdges(
                FrontEndMode::Title,
                FrontEndInputEdges{
                    .primary_pressed =
                        input_snapshot.WasPressed(kFrontEndPrimaryAction),
                    .previous_pressed =
                        input_snapshot.WasPressed(kFrontEndPreviousAction),
                    .next_pressed = input_snapshot.WasPressed(kFrontEndNextAction),
                    .cancel_pressed =
                        input_snapshot.WasPressed(kFrontEndCancelAction),
                },
                input_snapshot.WasPressed(kDebugMoveLeftAction),
                input_snapshot.WasPressed(kDebugMoveRightAction),
                input_snapshot.WasPressed(kDebugFireAction),
                input_snapshot.WasPressed(kDebugTargetAction));
            const multiplayer::MpMenuState before_state = mp_menu_state_;
            const multiplayer::MpMenuStep step = multiplayer::StepMpMenu(
                mp_menu_state_,
                multiplayer::MpMenuInput{
                    .up = mp_edges.previous_pressed,
                    .down = mp_edges.next_pressed,
                    .primary = mp_edges.primary_pressed,
                    .cancel = mp_edges.cancel_pressed,
                });
            mp_menu_state_ = step.state;
            if (step.request.type != multiplayer::SessionRequestType::None)
                DispatchMultiplayerSessionRequest(step.request);
            if (mp_menu_state_ != before_state)
                UpdateMultiplayerMenuPresentation();
        }
        else if (presentation_mode_ ==
                     runtime::FrontEndPresentationMode::RetailRequired &&
                 retail_front_end_bundle_.has_value())
        {
            // Retail front end active: drive the retail navigation (select move /
            // accept-switch-screen / back) instead of the project reducer, so the
            // retail path never fires the project's persistence commands. Reuse the
            // same input-edge resolution the project menu uses (arrows + gamepad).
            const auto retail_edges = ResolveFrontEndInputEdges(
                FrontEndMode::Title,
                FrontEndInputEdges{
                    .primary_pressed =
                        input_snapshot.WasPressed(kFrontEndPrimaryAction),
                    .previous_pressed =
                        input_snapshot.WasPressed(kFrontEndPreviousAction),
                    .next_pressed = input_snapshot.WasPressed(kFrontEndNextAction),
                    .cancel_pressed =
                        input_snapshot.WasPressed(kFrontEndCancelAction),
                },
                input_snapshot.WasPressed(kDebugMoveLeftAction),
                input_snapshot.WasPressed(kDebugMoveRightAction),
                input_snapshot.WasPressed(kDebugFireAction),
                input_snapshot.WasPressed(kDebugTargetAction));
            UpdateRetailFrontEndPresentation(
                frontend::presentation::RetailFrontEndNavInput{
                    .previous = retail_edges.previous_pressed,
                    .next = retail_edges.next_pressed,
                    .accept = retail_edges.primary_pressed,
                    .back = retail_edges.cancel_pressed,
                });
        }
        else
        {
            const FrontEndReduction front_end =
                ReduceFrontEnd(front_end_state_,
                    ResolveFrontEndInputEdges(front_end_state_.mode,
                        FrontEndInputEdges{
                            .primary_pressed =
                                input_snapshot.WasPressed(kFrontEndPrimaryAction),
                            .previous_pressed =
                                input_snapshot.WasPressed(kFrontEndPreviousAction),
                            .next_pressed =
                                input_snapshot.WasPressed(kFrontEndNextAction),
                            .cancel_pressed =
                                input_snapshot.WasPressed(kFrontEndCancelAction),
                        },
                        input_snapshot.WasPressed(kDebugMoveLeftAction),
                        input_snapshot.WasPressed(kDebugMoveRightAction),
                        input_snapshot.WasPressed(kDebugFireAction),
                        input_snapshot.WasPressed(kDebugTargetAction)),
                    front_end_startup_model_.visible_profiles,
                    CurrentFrontEndCapabilities(), ActiveProfileIsConfirmed(),
                    front_end_character_startup_model_.visible_characters,
                    ActiveCharacterIsConfirmed());
            // The command is applied, and therefore persisted, before its state
            // is published. A failed command leaves the prior front-end state and
            // the prior activation in place.
            auto applied = ApplyFrontEndCommand(front_end.command);
            if (!applied)
            {
                jobs_->WaitForIdle();
                log_->Error("profiles", applied.error());
                return RunLoopResult{
                    .result = result,
                    .operational_error = std::move(applied.error()),
                    .capture_error = std::nullopt,
                };
            }
            front_end_state_ = front_end.state;
        }
        const bool diagnostic_mission_aborted_now =
            diagnostic_play_input_context &&
            diagnostic_mission_lifecycle_state_.status ==
                gameplay::DiagnosticMissionStatus::Active &&
            front_end_state_.mode != FrontEndMode::DiagnosticPlay;
        const bool simulation_allowed =
            !movie_was_active && FrontEndAllowsSimulation(front_end_state_,
                                     CurrentFrontEndCapabilities(), ActiveProfileIsConfirmed(),
                                     ActiveCharacterIsConfirmed());
        debug_target_held_ = simulation_allowed && diagnostic_play_input_context &&
            input_snapshot.IsHeld(kDebugTargetAction);
        debug_fire_pressed_ = simulation_allowed && diagnostic_play_input_context &&
            input_snapshot.WasPressed(kDebugFireAction);
        std::optional<gameplay::DiagnosticAimPointQ16> diagnostic_aim_pointer;
        if (const auto pointer = input_snapshot.pointer_position())
        {
            diagnostic_aim_pointer = gameplay::DiagnosticAimPointQ16{
                .x = pointer->x,
                .y = pointer->y,
            };
        }
        const auto target_fire_step = gameplay::AdvanceDiagnosticTargetFire(
            gameplay::kProjectDiagnosticAimTarget,
            diagnostic_target_fire_state_,
            gameplay::DiagnosticTargetFireInput{
                .pointer = diagnostic_aim_pointer,
                .enabled = diagnostic_proximity_trigger_state_.objective_complete &&
                    simulation_allowed && diagnostic_play_input_context,
                .target_held = debug_target_held_,
                .fire_pressed = debug_fire_pressed_,
            });
        if (!target_fire_step)
        {
            (void)ContainOpeningMovieAudio();
            jobs_->WaitForIdle();
            constexpr std::string_view error =
                "diagnostic target/fire evaluation failed";
            log_->Error("simulation", error);
            return RunLoopResult{
                .result = result,
                .operational_error = std::string(error),
                .capture_error = std::nullopt,
            };
        }
        const std::chrono::nanoseconds effective_elapsed = simulation_allowed
            ? elapsed
            : std::chrono::nanoseconds::zero();
        const runtime::FramePlan plan = frame_scheduler_->BeginFrame(effective_elapsed);
        if (plan.simulation_steps >
            std::numeric_limits<std::uint64_t>::max() - result.planned_simulation_steps)
        {
            (void)ContainOpeningMovieAudio();
            jobs_->WaitForIdle();
            constexpr std::string_view error = "run-local simulation step counter exhausted";
            log_->Error("simulation", error);
            return RunLoopResult{
                .result = result,
                .operational_error = std::string(error),
                .capture_error = std::nullopt,
            };
        }
        result.planned_simulation_steps += plan.simulation_steps;
        if (plan.clamped_delta)
        {
            ++result.clamped_frame_count;
            if (result.clamped_frame_count == 1U)
                log_->Warning("frame", "wall-time delta reached the configured clamp");
        }
        if (plan.dropped_time)
        {
            ++result.dropped_time_frame_count;
            if (result.dropped_time_frame_count == 1U)
                log_->Warning(
                    "frame", "fixed-step backlog exceeded the configured frame budget");
        }

        simulation::SimulationStepInput simulation_input{};
        // Input split: when the kinematic player is active (OPENOMEGA_PLAYER), the
        // debug move controls drive the PLAYER (via camera_input, below), so the
        // old diagnostic-actor marker nudge is suppressed here -- WASD moves the
        // player, not the int64 sim marker. Without a player, the controls nudge
        // the diagnostic actor as before.
        const bool player_controls_active =
            diagnostic_scene_presentation_ != nullptr &&
            diagnostic_scene_presentation_->player_active;
        if (simulation_allowed && diagnostic_play_input_context &&
            !player_controls_active)
        {
            const auto planned_translation = gameplay::PlanDebugLocomotionStep(
                gameplay::DigitalMoveCommand{
                    .lateral = static_cast<std::int8_t>(
                        (input_snapshot.IsHeld(kDebugMoveRightAction) ? 1 : 0) -
                        (input_snapshot.IsHeld(kDebugMoveLeftAction) ? 1 : 0)),
                    .longitudinal = static_cast<std::int8_t>(
                        (input_snapshot.IsHeld(kDebugMoveForwardAction) ? 1 : 0) -
                        (input_snapshot.IsHeld(kDebugMoveBackwardAction) ? 1 : 0)),
                });
            if (!planned_translation)
            {
                (void)ContainOpeningMovieAudio();
                jobs_->WaitForIdle();
                constexpr std::string_view error = "debug locomotion planning failed";
                log_->Error("simulation", error);
                return RunLoopResult{
                    .result = result,
                    .operational_error = std::string(error),
                    .capture_error = std::nullopt,
                };
            }
            simulation_input.translation = simulation::EntityTranslation{
                .entity = debug_locomotion_entity_,
                .delta = *planned_translation,
            };
        }

        gameplay::DiagnosticProximityTriggerState next_proximity_trigger_state =
            diagnostic_proximity_trigger_state_;
        for (std::uint32_t step = 0; step < plan.simulation_steps; ++step)
        {
            if (simulation_->AdvanceOneStep(simulation_input) !=
                simulation::SimulationStepResult::Advanced)
            {
                (void)ContainOpeningMovieAudio();
                jobs_->WaitForIdle();
                constexpr std::string_view error =
                    "simulation world representation exhausted";
                log_->Error("simulation", error);
                return RunLoopResult{
                    .result = result,
                    .operational_error = std::string(error),
                    .capture_error = std::nullopt,
                };
            }
            ++result.executed_simulation_steps;
            if (simulation_allowed)
            {
                const std::optional<simulation::Position3> moved_position =
                    simulation_->PositionOf(debug_locomotion_entity_);
                if (!moved_position)
                {
                    (void)ContainOpeningMovieAudio();
                    jobs_->WaitForIdle();
                    constexpr std::string_view error =
                        "diagnostic actor position is unavailable";
                    log_->Error("simulation", error);
                    return RunLoopResult{
                        .result = result,
                        .operational_error = std::string(error),
                        .capture_error = std::nullopt,
                    };
                }
                const auto trigger_step =
                    gameplay::AdvanceDiagnosticProximityTrigger(
                        gameplay::kProjectDiagnosticObjectiveVolume,
                        next_proximity_trigger_state, *moved_position);
                if (!trigger_step)
                {
                    (void)ContainOpeningMovieAudio();
                    jobs_->WaitForIdle();
                    constexpr std::string_view error =
                        "diagnostic proximity trigger evaluation failed";
                    log_->Error("simulation", error);
                    return RunLoopResult{
                        .result = result,
                        .operational_error = std::string(error),
                        .capture_error = std::nullopt,
                    };
                }
                next_proximity_trigger_state = trigger_step->state;
            }
        }
        gameplay::DiagnosticMissionEvent mission_event =
            gameplay::DiagnosticMissionEvent::None;
        if (diagnostic_mission_aborted_now)
        {
            mission_event = gameplay::DiagnosticMissionEvent::Abort;
        }
        else if (diagnostic_mission_lifecycle_state_.status ==
                     gameplay::DiagnosticMissionStatus::Active &&
                 !diagnostic_target_fire_state_.target_complete &&
                 target_fire_step->state.target_complete)
        {
            mission_event = gameplay::DiagnosticMissionEvent::Complete;
        }
        const auto mission_step =
            gameplay::AdvanceDiagnosticMissionLifecycle(
                diagnostic_mission_lifecycle_state_, mission_event);
        if (!mission_step)
        {
            (void)ContainOpeningMovieAudio();
            jobs_->WaitForIdle();
            constexpr std::string_view error =
                "diagnostic mission lifecycle evaluation failed";
            log_->Error("simulation", error);
            return RunLoopResult{
                .result = result,
                .operational_error = std::string(error),
                .capture_error = std::nullopt,
            };
        }

        diagnostic_proximity_trigger_state_ = next_proximity_trigger_state;
        diagnostic_target_fire_state_ = target_fire_step->state;
        diagnostic_mission_lifecycle_state_ = mission_step->state;
        if (mission_step->enter_briefing_now &&
            mission_event == gameplay::DiagnosticMissionEvent::Complete)
        {
            if (CurrentFrontEndCapabilities().supports_character_selection)
            {
                front_end_state_.mode = FrontEndMode::BriefingRoom;
                front_end_state_.selected_main_row =
                    FrontEndMainRow::StartDiagnostic;
            }
            else
            {
                front_end_state_ = InitialFrontEndState();
            }
        }

        const simulation::SimulationState simulation_snapshot = simulation_->Snapshot();
        const bool movie_is_active = IsBootSequenceActive(boot_sequence_state_);
        if (!movie_is_active && FrontEndAllowsSimulation(front_end_state_,
                                    CurrentFrontEndCapabilities(), ActiveProfileIsConfirmed(),
                                    ActiveCharacterIsConfirmed()))
        {
            // Live free-fly camera input. A headless OPENOMEGA_CAMERA_SCRIPT
            // (seeded onto the presentation) drives a constant per-frame input so
            // motion is provable in a --frames capture; otherwise the held debug
            // move controls fly the camera (WASD forward/back + left/right turn).
            // These controls also drive the diagnostic actor marker (shared
            // binding); a dedicated camera-vs-actor toggle is a follow-up.
            runtime::FreeFlyInput camera_input{};
            if (diagnostic_scene_presentation_ &&
                diagnostic_scene_presentation_->free_fly_active)
            {
                const runtime::FreeFlyInput& script =
                    diagnostic_scene_presentation_->free_fly_script;
                const bool script_active =
                    script.forward != 0.0F || script.strafe != 0.0F ||
                    script.vertical != 0.0F || script.yaw_delta != 0.0F ||
                    script.pitch_delta != 0.0F;
                if (script_active)
                {
                    camera_input = script;
                }
                else
                {
                    camera_input.forward =
                        static_cast<float>(
                            input_snapshot.IsHeld(kDebugMoveForwardAction) ? 1 : 0) -
                        static_cast<float>(
                            input_snapshot.IsHeld(kDebugMoveBackwardAction) ? 1 : 0);
                    const float lateral =
                        static_cast<float>(
                            input_snapshot.IsHeld(kDebugMoveRightAction) ? 1 : 0) -
                        static_cast<float>(
                            input_snapshot.IsHeld(kDebugMoveLeftAction) ? 1 : 0);
                    if (player_controls_active)
                    {
                        // Player: A/D strafe (the player reads move = {strafe,
                        // forward}); no camera turn -- the follow camera tracks
                        // the player.
                        camera_input.strafe = lateral;
                    }
                    else
                    {
                        // Free-fly camera: A/D turn (yaw).
                        camera_input.yaw_delta = lateral * 0.03F;
                    }
                }
            }
            // Combat S2: the fire binding is HELD (not WasPressed) because the
            // player weapon is cooldown-gated -- holding it is automatic fire.
            auto refreshed_actor_draw_list = RefreshDiagnosticActorDrawList(
                input_snapshot.pointer_position(), camera_input,
                input_snapshot.IsHeld(kDebugFireAction));
            if (!refreshed_actor_draw_list)
            {
                (void)ContainOpeningMovieAudio();
                jobs_->WaitForIdle();
                log_->Error("render", refreshed_actor_draw_list.error());
                return RunLoopResult{
                    .result = result,
                    .operational_error =
                        std::move(refreshed_actor_draw_list.error()),
                    .capture_error = std::nullopt,
                };
            }
        }
        constexpr runtime::RenderClearColorRgba8 kOpeningMovieClearColor{
            .red = 0U,
            .green = 0U,
            .blue = 0U,
            .alpha = 255U,
        };
        const runtime::RenderFramePacket render_packet{
            .rendered_frame_index = static_cast<std::uint64_t>(result.rendered_frames),
            .completed_simulation_steps = simulation_snapshot.completed_steps,
            .simulated_time = simulation_snapshot.simulated_time,
            .alive_entities = simulation_snapshot.alive_entities,
            .clear_color = movie_is_active
                ? kOpeningMovieClearColor
                : runtime::kDefaultRenderClearColor,
            .draw_list = movie_is_active
                ? opening_movie_draw_list_
                : CurrentFrontEndDrawList(),
            .mesh_draw_list = movie_is_active
                ? runtime::RenderMeshDrawList{}
                : CurrentFrontEndMeshDrawList(),
        };
        auto rendered = host_->RenderFrame(render_packet);
        if (!rendered)
        {
            (void)ContainOpeningMovieAudio();
            jobs_->WaitForIdle();
            log_->Error("render", rendered.error());
            return RunLoopResult{
                .result = result,
                .operational_error = rendered.error(),
                .capture_error = std::nullopt,
            };
        }
        const bool headless_screenshot_frame =
            screenshot_frame > 0 &&
            *next_rendered_frame_count ==
                static_cast<decltype(*next_rendered_frame_count)>(screenshot_frame);
        if (events.screenshot_requested || headless_screenshot_frame)
        {
            auto screenshot_pixels = host_->CaptureFrameRgba8(render_packet);
            if (!screenshot_pixels)
            {
                // SDL diagnostics can contain host/device identity. Keep the
                // normal runtime log categorical; detailed debugging remains
                // available at the host call boundary.
                log_->Warning("screenshot", "GPU readback failed");
            }
            else
            {
                auto screenshot = WriteScreenshotBmpToDefaultDirectory(
                    *screenshot_pixels);
                if (!screenshot)
                {
                    log_->Warning("screenshot",
                        "private BMP write failed [" +
                            std::string(ScreenshotErrorCodeName(
                                screenshot.error())) +
                            "]");
                }
                else
                {
                    log_->Info("screenshot",
                        "saved one private 640x448 BMP in the platform-local "
                        "OpenOmega screenshots directory");
                }
            }
        }
        result.rendered_frames = *next_rendered_frame_count;
        if (movie_is_active)
            opening_movie_skip_armed_ = true;

        const AudioServiceSnapshot audio_health = audio_->Snapshot();
        const OpeningMovieAudioFault audio_fault =
            ClassifyOpeningMovieAudioFault(
                OpeningMovieAudioFaultCountersOf(audio_fault_baseline),
                OpeningMovieAudioFaultCountersOf(audio_health));
        const OpeningMovieAudioFaultDisposition audio_fault_disposition =
            DisposeOpeningMovieAudioFault(audio_fault, movie_was_active);
        if (audio_fault_disposition ==
            OpeningMovieAudioFaultDisposition::FailOpen)
        {
            log_->Warning("opening_movie",
                OpeningMovieAudioFaultMessage(audio_fault));
            const bool contained = FinishOpeningMovieFrontEndTransition(
                audio_fault_baseline);
            if (!contained)
            {
                jobs_->WaitForIdle();
                constexpr std::string_view error =
                    "opening movie audio containment failed during the front-end transition";
                log_->Error("opening_movie", error);
                return RunLoopResult{
                    .result = result,
                    .operational_error = std::string(error),
                    .capture_error = std::nullopt,
                };
            }
        }
        else if (audio_fault_disposition ==
            OpeningMovieAudioFaultDisposition::Fatal)
        {
            const bool contained = ContainOpeningMovieAudio();
            jobs_->WaitForIdle();
            const std::string_view error = GeneralAudioFaultMessage(audio_fault);
            log_->Error("audio", error);
            if (!contained)
                log_->Error("audio", "audio containment also reported a control failure");
            return RunLoopResult{
                .result = result,
                .operational_error = std::string(error),
                .capture_error = std::nullopt,
            };
        }
    }

    const bool movie_window_open_at_exit =
        IsBootSequenceActive(boot_sequence_state_);
    const bool contained = ContainOpeningMovieAudio();
    jobs_->WaitForIdle();
    const AudioServiceSnapshot audio = audio_->Snapshot();
    const OpeningMovieAudioFault exit_audio_fault =
        ClassifyOpeningMovieAudioFault(
            OpeningMovieAudioFaultCountersOf(audio_fault_baseline),
            OpeningMovieAudioFaultCountersOf(audio));
    const OpeningMovieAudioFaultDisposition exit_audio_fault_disposition =
        DisposeOpeningMovieAudioFault(
            exit_audio_fault, movie_window_open_at_exit);
    if (exit_audio_fault_disposition ==
        OpeningMovieAudioFaultDisposition::FailOpen)
    {
        log_->Warning("opening_movie",
            OpeningMovieAudioFaultMessage(exit_audio_fault));
        ReleaseOpeningMovieForFrontEnd();
    }
    if (!contained)
    {
        constexpr std::string_view error =
            "audio playback containment operation failed";
        log_->Error("audio", error);
        return RunLoopResult{
            .result = result,
            .operational_error = std::string(error),
            .capture_error = std::nullopt,
        };
    }
    if (exit_audio_fault_disposition ==
        OpeningMovieAudioFaultDisposition::Fatal)
    {
        const std::string_view error = GeneralAudioFaultMessage(exit_audio_fault);
        log_->Error("audio", error);
        return RunLoopResult{
            .result = result,
            .operational_error = std::string(error),
            .capture_error = std::nullopt,
        };
    }
    result.audio_callback_count = audio.callback_count;
    result.audio_frames_provided = audio.provided_frames;
    log_->Info("runtime", "host loop ended after " +
                              std::to_string(result.rendered_frames) + " rendered frames and " +
                              std::to_string(result.executed_simulation_steps) +
                              " executed simulation steps");
    return RunLoopResult{
        .result = result,
        .operational_error = std::nullopt,
        .capture_error = std::nullopt,
    };
}

std::expected<void, std::string> OmegaApp::CreateFirstProfile()
{
    if (!can_create_first_profile_ || !first_profile_presentation_ ||
        native_persistence_ == nullptr ||
        front_end_startup_model_.total_profiles != 0U ||
        front_end_startup_model_.visible_profiles != 0U)
    {
        return std::unexpected(
            std::string{"first profile creation is not available"});
    }

    auto live_profiles = native_persistence_->profiles().List();
    if (!live_profiles)
    {
        return std::unexpected(
            "first profile catalog check failed: " +
            std::string(profiles::ProfileCatalogErrorCodeName(
                live_profiles.error().code)));
    }
    if (!live_profiles->empty())
    {
        return std::unexpected(
            std::string{"first profile creation requires an empty catalog"});
    }

    std::uint64_t timestamp = 0U;
    if (first_profile_timestamp_override_for_testing_)
    {
        timestamp = *first_profile_timestamp_override_for_testing_;
        first_profile_timestamp_override_for_testing_.reset();
    }
    else
    {
        const auto elapsed = std::chrono::system_clock::now().time_since_epoch();
        if (elapsed < std::chrono::system_clock::duration::zero())
        {
            return std::unexpected(
                std::string{"system clock precedes the supported UTC epoch"});
        }
        const auto milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        if (milliseconds < 0)
        {
            return std::unexpected(
                std::string{"system clock precedes the supported UTC epoch"});
        }
        timestamp = static_cast<std::uint64_t>(milliseconds);
    }
    if (timestamp > profiles::kProfileTimestampMaxUnixMilliseconds)
    {
        return std::unexpected(
            std::string{"system clock exceeds the supported UTC range"});
    }

    try
    {
        std::array prospective_profiles{
            profiles::ProfileSummary{
                .id = kFirstProfileId,
                .metadata = profiles::ProfileMetadata{
                    .display_name = std::string{kFrontEndFirstProfileDisplayName},
                    .created_unix_milliseconds = timestamp,
                    .modified_unix_milliseconds = timestamp,
                },
                .metadata_revision = 1U,
            },
        };
        const auto projected_model =
            MakeFrontEndStartupModel(prospective_profiles);
        if (!projected_model)
        {
            return std::unexpected(
                std::string{"first profile projection failed"});
        }

        auto created = native_persistence_->profiles().Create(
            kFirstProfileId, std::move(prospective_profiles[0].metadata));
        if (!created)
        {
            return std::unexpected(
                "first profile creation failed: " +
                std::string(profiles::ProfileCatalogErrorCodeName(
                    created.error().code)));
        }

        // Every potentially failing allocation, catalog read, and GPU operation
        // completed before the durable mutation. These fixed-value swaps cannot
        // strand the database and visible presentation in different states.
        static_assert(std::is_nothrow_swappable_v<FrontEndPresentation>);
        std::swap(front_end_presentation_, *first_profile_presentation_);
        front_end_startup_model_ = *projected_model;
        can_create_first_profile_ = false;
        return {};
    }
    catch (const std::exception&)
    {
        return std::unexpected(
            std::string{"first profile preparation failed"});
    }
    catch (...)
    {
        return std::unexpected(
            std::string{"first profile preparation failed"});
    }
}

std::expected<void, std::string> OmegaApp::CreateFirstCharacter()
{
    if (!can_create_first_character_ || !character_presentation_ ||
        !first_character_presentation_ || native_persistence_ == nullptr ||
        !ActiveProfileIsConfirmed() ||
        front_end_character_startup_model_.total_characters != 0U ||
        front_end_character_startup_model_.visible_characters != 0U)
    {
        return std::unexpected(
            std::string{"first character creation is not available"});
    }

    auto live_characters = native_persistence_->characters().List(
        *active_profile_id_);
    if (!live_characters)
    {
        return std::unexpected(
            "first character catalog check failed: " +
            std::string(profiles::CharacterCatalogErrorCodeName(
                live_characters.error().code)));
    }
    if (!live_characters->empty())
    {
        return std::unexpected(
            std::string{"first character creation requires an empty catalog"});
    }

    const auto elapsed = std::chrono::system_clock::now().time_since_epoch();
    if (elapsed < std::chrono::system_clock::duration::zero())
    {
        return std::unexpected(
            std::string{"system clock precedes the supported UTC epoch"});
    }
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    if (milliseconds < 0)
    {
        return std::unexpected(
            std::string{"system clock precedes the supported UTC epoch"});
    }
    const std::uint64_t timestamp =
        static_cast<std::uint64_t>(milliseconds);
    if (timestamp > profiles::kCharacterTimestampMaxUnixMilliseconds)
    {
        return std::unexpected(
            std::string{"system clock exceeds the supported UTC range"});
    }

    try
    {
        std::array prospective_characters{
            profiles::CharacterSummary{
                .id = kFirstCharacterId,
                .metadata = profiles::CharacterMetadata{
                    .display_name =
                        std::string{kFrontEndFirstCharacterDisplayName},
                    .created_unix_milliseconds = timestamp,
                    .modified_unix_milliseconds = timestamp,
                },
                .metadata_revision = 1U,
            },
        };
        const auto projected_model =
            MakeFrontEndCharacterStartupModel(prospective_characters);
        if (!projected_model)
        {
            return std::unexpected(
                std::string{"first character projection failed"});
        }

        auto created = native_persistence_->characters().Create(
            *active_profile_id_, kFirstCharacterId,
            std::move(prospective_characters[0].metadata));
        if (!created)
        {
            return std::unexpected(
                "first character creation failed: " +
                std::string(profiles::CharacterCatalogErrorCodeName(
                    created.error().code)));
        }

        static_assert(std::is_nothrow_swappable_v<CharacterPresentation>);
        std::swap(*character_presentation_,
                  *first_character_presentation_);
        // The preview now owns the obsolete empty card. Release it immediately
        // so reselecting this or another profile does not carry a third
        // character texture into the next transactional presentation build.
        ReleaseCharacterPresentation(first_character_presentation_);
        front_end_character_startup_model_ = *projected_model;
        can_create_first_character_ = false;
        return {};
    }
    catch (const std::exception&)
    {
        return std::unexpected(
            std::string{"first character preparation failed"});
    }
    catch (...)
    {
        return std::unexpected(
            std::string{"first character preparation failed"});
    }
}

FrontEndCapabilities OmegaApp::CurrentFrontEndCapabilities() const noexcept
{
    // Dev-only synthetic, persistence-free diagnostic start (see
    // synthetic_diagnostic_play_start_): open start support with no confirmation
    // gate so the loaded level renders in DiagnosticPlay for a headless capture,
    // without mutating or requiring durable profile/character confirmation.
    if (synthetic_diagnostic_play_start_)
    {
        return FrontEndCapabilities{
            .can_create_first_profile = can_create_first_profile_,
            .can_start_diagnostic_campaign = true,
            .requires_active_profile_for_diagnostic_play = false,
            .supports_character_selection = native_persistence_ != nullptr,
            .can_create_first_character = can_create_first_character_,
            .requires_active_character_for_diagnostic_play = false,
        };
    }
    const bool active_profile_is_confirmed = ActiveProfileIsConfirmed();
    const bool active_character_is_confirmed = ActiveCharacterIsConfirmed();
    return FrontEndCapabilities{
        .can_create_first_profile = can_create_first_profile_,
        .can_start_diagnostic_campaign =
            native_persistence_ == nullptr ||
            (active_profile_is_confirmed && active_character_is_confirmed),
        .requires_active_profile_for_diagnostic_play =
            requires_active_profile_for_diagnostic_play_,
        .supports_character_selection = native_persistence_ != nullptr,
        .can_create_first_character = can_create_first_character_,
        .requires_active_character_for_diagnostic_play =
            requires_active_character_for_diagnostic_play_,
    };
}

bool OmegaApp::ActiveProfileIsConfirmed() const noexcept
{
    return FrontEndHasConfirmedActiveProfile(
        front_end_startup_model_, active_profile_id_);
}

bool OmegaApp::ActiveCharacterIsConfirmed() const noexcept
{
    return ActiveProfileIsConfirmed() &&
           FrontEndHasConfirmedActiveCharacter(
               front_end_character_startup_model_, active_character_id_);
}

std::expected<void, runtime::FrontEndPresentationGateError>
OmegaApp::AuthorizeCurrentFrontEndPresentation() const noexcept
{
    if (presentation_mode_ ==
        runtime::FrontEndPresentationMode::DeveloperDiagnostics)
    {
        return runtime::AuthorizeFrontEndPresentation(
            presentation_mode_, front_end_presentation_.provenance);
    }

    // Retail presentation is authorized only through a GameDataService-minted
    // capability carried by the owned Title screen bundle. That bundle is loaded
    // once, on first host-loop entry, and only under the experimental guard (see
    // LoadRetailFrontEndBundleIfEnabled and docs/08). Until the retail compositor
    // (Gap B) can render it, default launches leave the bundle empty and this
    // gate stays fail-closed rather than authorizing an unrenderable screen.
    if (retail_front_end_bundle_)
    {
        return runtime::AuthorizeFrontEndPresentation(
            presentation_mode_, retail_front_end_bundle_->presentation_capability());
    }
    return runtime::AuthorizeFrontEndPresentation(
        presentation_mode_, std::nullopt);
}

void OmegaApp::LoadRetailFrontEndBundleIfEnabled() noexcept
{
    if (retail_front_end_bundle_attempted_)
        return;
    retail_front_end_bundle_attempted_ = true;

    if (presentation_mode_ != runtime::FrontEndPresentationMode::RetailRequired)
        return;

    // Experimental opt-in. The retail compositor (Gap B) does not yet turn a
    // bundle into per-mode textures and draw lists, so authorizing the gate by
    // default would present an empty screen. Keep this behind an env guard until
    // that lands; see docs/08-Retail-Front-End-Presentation-Scope.md.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* const retail_front_end_opt_in =
        std::getenv("OPENOMEGA_ENABLE_RETAIL_FRONT_END");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (retail_front_end_opt_in == nullptr)
        return;

    if (!content_ || !content_->game_data.has_value())
    {
        log_->Warning("presentation",
            "retail front-end bundle requested but the game data service is unavailable");
        return;
    }

    // LoadFrontEndScreen reports domain failures through its expected error
    // channel, but is not declared noexcept; contain any allocation/decoder
    // exception here so this loader keeps its noexcept contract and stays
    // fail-closed (empty bundle, unavailable gate) rather than terminating.
    try
    {
        auto bundle = content_->game_data->LoadFrontEndScreen(
            content::FrontEndScreenKey::Title);
        if (!bundle)
        {
            // The decoder path already emits a specific stderr diagnostic; keep
            // the seam's own message host-path free and non-fabricated.
            log_->Error("presentation", "retail front-end Title bundle load failed");
            return;
        }
        retail_front_end_bundle_ = std::move(*bundle);
        log_->Info("presentation", "retail front-end Title bundle loaded (experimental)");
    }
    catch (...)
    {
        log_->Error("presentation", "retail front-end Title bundle load raised an exception");
        return;
    }

    // Optional debug start-screen override so a non-Title screen can be composed
    // and captured headlessly without driving live input. Empty/unknown = Title.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* const start_screen = std::getenv("OPENOMEGA_FRONTEND_START_SCREEN");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (start_screen != nullptr)
    {
        const std::string_view requested(start_screen);
        if (requested == "createagent")
            retail_nav_.screen = content::FrontEndScreenKey::CreateAgent;
        else if (requested == "loadagent")
            retail_nav_.screen = content::FrontEndScreenKey::LoadAgent;
        else if (requested == "commandcenter")
            retail_nav_.screen = content::FrontEndScreenKey::CommandCenter;
        else if (requested == "equipment")
            retail_nav_.screen = content::FrontEndScreenKey::Equipment;
    }

    // Compose the initial screen (default selection) through the same recompose
    // path the host loop uses each frame.
    UpdateRetailFrontEndPresentation(frontend::presentation::RetailFrontEndNavInput{});
}

const content::FrontEndScreenBundle* OmegaApp::RetailBundleForScreen(
    const content::FrontEndScreenKey screen) noexcept
{
    std::optional<content::FrontEndScreenBundle>* slot = nullptr;
    switch (screen)
    {
    case content::FrontEndScreenKey::Title:
        slot = &retail_front_end_bundle_;
        break;
    case content::FrontEndScreenKey::CreateAgent:
        slot = &retail_create_agent_bundle_;
        break;
    case content::FrontEndScreenKey::LoadAgent:
        slot = &retail_load_agent_bundle_;
        break;
    case content::FrontEndScreenKey::CommandCenter:
        slot = &retail_command_center_bundle_;
        break;
    case content::FrontEndScreenKey::Equipment:
        slot = &retail_equipment_bundle_;
        break;
    }
    if (slot == nullptr)
        return nullptr;
    if (slot->has_value())
        return &**slot;
    if (!content_ || !content_->game_data.has_value())
        return nullptr;
    // LoadFrontEndScreen is not noexcept; contain any allocation/decoder throw so
    // this loader keeps its own noexcept contract and simply reports "no bundle".
    try
    {
        auto bundle = content_->game_data->LoadFrontEndScreen(screen);
        if (!bundle)
        {
            // Identity-free: the GameData error code names a category, not owner
            // data. Helps distinguish a decode gap from a missing member/scope.
            log_->Warning("presentation",
                std::string("retail front-end screen load failed [") +
                    std::string(content::GameDataErrorCodeName(bundle.error().code)) +
                    "]");
            return nullptr;
        }
        *slot = std::move(*bundle);
        return &**slot;
    }
    catch (...)
    {
        return nullptr;
    }
}

std::vector<OmegaApp::RetailFrontEndButton> OmegaApp::RetailScreenSelectableButtons(
    const content::FrontEndScreenBundle& bundle)
{
    std::vector<RetailFrontEndButton> buttons;
    const auto visit = [&buttons](const auto& self,
                           const asset::FrontendWidgetIR& widget) -> void {
        if (widget.visible &&
            widget.kind == asset::FrontendWidgetKind::Button)
        {
            std::optional<content::FrontEndScreenKey> target;
            if (widget.identifier == "newagent")
                target = content::FrontEndScreenKey::CreateAgent;
            else if (widget.identifier == "loadagent")
                target = content::FrontEndScreenKey::LoadAgent;
            buttons.push_back(
                RetailFrontEndButton{.identifier = widget.identifier, .target = target});
        }
        for (const auto& child : widget.children)
            self(self, child);
    };
    visit(visit, bundle.widget_document().root);
    return buttons;
}

void OmegaApp::UpdateRetailFrontEndPresentation(
    const frontend::presentation::RetailFrontEndNavInput& input) noexcept
{
    if (presentation_mode_ != runtime::FrontEndPresentationMode::RetailRequired ||
        !retail_front_end_bundle_ || !host_)
        return;

    try
    {
        // Advance one animation tick per rendered frame (this runs once per frame).
        // Each compose re-clones+evaluates the timeline from scratch, so the tick is
        // a pure input with no cross-frame instance state to keep monotonic.
        ++retail_animation_tick_;

        // Derive the current screen's selectable buttons to bound navigation and
        // resolve the selected button's accept target, then step the pure nav.
        std::uint32_t button_count = 0U;
        std::optional<content::FrontEndScreenKey> accept_target;
        if (const auto* const pre = RetailBundleForScreen(retail_nav_.screen))
        {
            const auto buttons = RetailScreenSelectableButtons(*pre);
            button_count = static_cast<std::uint32_t>(buttons.size());
            if (retail_nav_.selected < buttons.size())
                accept_target = buttons[retail_nav_.selected].target;
        }
        retail_nav_ = frontend::presentation::StepRetailFrontEndNav(
            retail_nav_, input, button_count, accept_target);

        // Recompose when the navigation state changed (selection move / screen
        // switch), when nothing has composed yet, or -- for an animated screen --
        // every frame so the tick advances the tracks. A static screen composes
        // once and holds.
        const bool nav_unchanged = retail_front_end_ready_ &&
            retail_composed_nav_.has_value() &&
            *retail_composed_nav_ == retail_nav_;
        if (nav_unchanged && !retail_screen_has_animation_)
            return;

        const auto* const bundle = RetailBundleForScreen(retail_nav_.screen);
        if (bundle == nullptr)
        {
            log_->Warning("presentation",
                "retail front-end screen bundle unavailable; using project fallback");
            return;
        }
        const auto buttons = RetailScreenSelectableButtons(*bundle);
        std::string_view selected_identifier;
        if (retail_nav_.selected < buttons.size())
            selected_identifier = buttons[retail_nav_.selected].identifier;

        ComposeRetailScreenPresentation(*bundle, selected_identifier);
        retail_composed_nav_ = retail_nav_;
    }
    catch (...)
    {
        // Fail-soft: keep whatever was last composed (or the project fallback).
    }
}

void OmegaApp::ComposeRetailScreenPresentation(
    const content::FrontEndScreenBundle& bundle,
    const std::string_view selected_identifier) noexcept
{
    if (!host_)
        return;

    frontend::presentation::RetailFrontEndFrameDiagnostics diagnostics;
    const auto frame = frontend::presentation::ComposeRetailFrontEndFrame(
        bundle, {}, &diagnostics, selected_identifier, retail_animation_tick_);
    // Whether this screen animates decides if the host recomposes every frame
    // (advancing the tick) or composes once and holds -- see
    // UpdateRetailFrontEndPresentation.
    retail_screen_has_animation_ = diagnostics.animated_nodes > 0U;
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* const frontend_trace = std::getenv("OPENOMEGA_FRONTEND_TRACE");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (frontend_trace != nullptr)
    {
        // Identity-free aggregate tally (no owner strings/members): a durable,
        // rebuild-free inspection of which compositor code paths fired.
        log_->Info("presentation",
            "frontend-trace nodes=" +
                std::to_string(diagnostics.visual_nodes_visited) +
                " ie_tris=" + std::to_string(diagnostics.ie_triangles_emitted) +
                " skip_oob=" +
                std::to_string(diagnostics.triangles_skipped_out_of_range) +
                " skip_nonfinite=" +
                std::to_string(diagnostics.triangles_skipped_non_finite) +
                " skip_degenerate=" +
                std::to_string(diagnostics.triangles_skipped_degenerate) +
                " text_widgets=" +
                std::to_string(diagnostics.text_widgets_seen) + " str_ok=" +
                std::to_string(diagnostics.strings_resolved) + " str_miss=" +
                std::to_string(diagnostics.strings_missing) + " font_ok=" +
                std::to_string(diagnostics.fonts_resolved) + " font_miss=" +
                std::to_string(diagnostics.fonts_missing) + " layout_fail=" +
                std::to_string(diagnostics.text_layouts_failed) + " glyph_quads=" +
                std::to_string(diagnostics.glyph_quads_emitted) + " anim_nodes=" +
                std::to_string(diagnostics.animated_nodes) + " ring_tris=" +
                std::to_string(diagnostics.mission_ring_triangles) + " total_tris=" +
                std::to_string(diagnostics.total_triangles) + " frame=" +
                std::to_string(diagnostics.frame_width) + "x" +
                std::to_string(diagnostics.frame_height));
    }
    if (!frame)
    {
        // Identity-free: the compositor error carries no owner-corpus detail.
        // Leave the retail presentation not ready; the gate stays authorized but
        // CurrentFrontEndDrawList falls back to the project presentation until a
        // later phase composes this screen.
        log_->Warning("presentation",
            "retail front-end Title composition unsupported; using project fallback (error code " +
                std::to_string(static_cast<unsigned>(frame.error())) + ")");
        return;
    }

    auto uploaded = host_->UploadRgba8Texture(runtime::Rgba8TextureUploadView{
        .width = frame->width,
        .height = frame->height,
        .pixels = std::as_bytes(std::span<const std::uint8_t>(frame->pixels)),
    });
    if (!uploaded)
    {
        log_->Warning("presentation",
            "retail front-end Title texture upload failed; using project fallback");
        return;
    }

    constexpr runtime::RenderSourceRectQ16 full_source{
        .left = 0U,
        .top = 0U,
        .right = runtime::kNormalizedRenderExtent,
        .bottom = runtime::kNormalizedRenderExtent,
    };
    constexpr runtime::RenderTargetRectQ16 full_target{
        .left = 0U,
        .top = 0U,
        .right = runtime::kNormalizedRenderExtent,
        .bottom = runtime::kNormalizedRenderExtent,
    };
    const runtime::RenderTextureBlitCommand command{
        .texture = *uploaded,
        .source = full_source,
        .destination = full_target,
        .fit_mode = runtime::RenderTextureFitMode::Contain,
        .filter_mode = runtime::RenderTextureFilterMode::Nearest,
    };
    auto draw_list = runtime::RenderDrawList::Create(
        std::span<const runtime::RenderTextureBlitCommand>{&command, 1U});
    if (!draw_list)
    {
        // The just-uploaded texture is now orphaned (the old draw list still owns
        // the prior handle); release it so a failed recompose does not leak.
        static_cast<void>(host_->ReleaseTexture(*uploaded));
        log_->Warning("presentation",
            "retail front-end Title draw-list creation failed; using project fallback");
        return;
    }
    // Success: adopt the new texture and release the one the previous compose left
    // behind (the old draw list is about to be replaced), so per-frame animated
    // recompose holds at most one retail frame texture.
    if (retail_front_end_texture_valid_)
        static_cast<void>(host_->ReleaseTexture(retail_front_end_texture_));
    retail_front_end_texture_ = *uploaded;
    retail_front_end_texture_valid_ = true;
    retail_front_end_draw_list_ = std::move(*draw_list);
    // Log once on the first successful compose; recompose runs every frame for an
    // animated screen, so the per-compose detail lives behind OPENOMEGA_FRONTEND_TRACE.
    if (!retail_front_end_ready_)
        log_->Info("presentation",
            "retail front-end presentation composited (Gap B: retail screen live)");
    retail_front_end_ready_ = true;
}

void OmegaApp::UpdateMultiplayerMenuPresentation() noexcept
{
    if (!host_)
        return;
    const runtime::DebugImage image = BuildProjectMultiplayerImage(mp_menu_state_);
    auto uploaded = host_->UploadRgba8Texture(runtime::Rgba8TextureUploadView{
        .width = image.width,
        .height = image.height,
        .pixels = image.pixels(),
    });
    if (!uploaded)
    {
        log_->Warning("presentation", "multiplayer menu texture upload failed");
        return;
    }
    constexpr runtime::RenderSourceRectQ16 full_source{
        .left = 0U,
        .top = 0U,
        .right = runtime::kNormalizedRenderExtent,
        .bottom = runtime::kNormalizedRenderExtent,
    };
    constexpr runtime::RenderTargetRectQ16 full_target{
        .left = 0U,
        .top = 0U,
        .right = runtime::kNormalizedRenderExtent,
        .bottom = runtime::kNormalizedRenderExtent,
    };
    const runtime::RenderTextureBlitCommand command{
        .texture = *uploaded,
        .source = full_source,
        .destination = full_target,
        .fit_mode = runtime::RenderTextureFitMode::Stretch,
        .filter_mode = runtime::RenderTextureFilterMode::Nearest,
    };
    auto draw_list = runtime::RenderDrawList::Create(
        std::span<const runtime::RenderTextureBlitCommand>{&command, 1U});
    if (!draw_list)
    {
        static_cast<void>(host_->ReleaseTexture(*uploaded));
        log_->Warning("presentation", "multiplayer menu draw-list creation failed");
        return;
    }
    if (mp_menu_texture_valid_)
        static_cast<void>(host_->ReleaseTexture(mp_menu_texture_));
    mp_menu_texture_ = *uploaded;
    mp_menu_texture_valid_ = true;
    mp_menu_draw_list_ = std::move(*draw_list);
    mp_menu_ready_ = true;
}

void OmegaApp::DispatchMultiplayerSessionRequest(
    const multiplayer::MpSessionRequest& request) noexcept
{
    // Stub seam: a future transport layer implements these requests. Today the
    // app only logs the intent + parameters -- no sockets, threads, or netcode.
    switch (request.type)
    {
    case multiplayer::SessionRequestType::HostSession:
        log_->Info("multiplayer",
            std::string("host-session request: mode=") +
                std::string(multiplayer::HostModeName(request.host_mode)) +
                " name=\"" + std::string(request.server_name.view()) + "\"");
        break;
    case multiplayer::SessionRequestType::DirectConnect:
        log_->Info("multiplayer",
            std::string("direct-connect request: address=") +
                std::string(request.address.view()));
        break;
    case multiplayer::SessionRequestType::JoinServer:
        log_->Info("multiplayer", "join-server request (stub server list entry 0)");
        break;
    case multiplayer::SessionRequestType::RefreshServers:
        log_->Info("multiplayer",
            "refresh-servers request (no master server configured)");
        break;
    case multiplayer::SessionRequestType::None:
        break;
    }
}

std::expected<void, std::string> OmegaApp::DeployDiagnosticMission()
{
    const auto deployed = gameplay::AdvanceDiagnosticMissionLifecycle(
        diagnostic_mission_lifecycle_state_,
        gameplay::DiagnosticMissionEvent::Deploy);
    if (!deployed || !deployed->reset_gameplay_now ||
        deployed->enter_briefing_now)
    {
        return std::unexpected(
            std::string{"diagnostic mission deploy evaluation failed"});
    }
    if (simulation_->ResetPosition(
            debug_locomotion_entity_, simulation::Position3{}) !=
        simulation::PositionResetResult::Reset)
    {
        return std::unexpected(
            std::string{"diagnostic actor reset failed"});
    }

    diagnostic_proximity_trigger_state_ = {};
    diagnostic_target_fire_state_ = {};
    diagnostic_mission_lifecycle_state_ = deployed->state;
    debug_target_held_ = false;
    debug_fire_pressed_ = false;
    return {};
}

std::expected<void, std::string> OmegaApp::ApplyFrontEndCommand(
    const FrontEndCommand command)
{
    if (command.type == FrontEndCommandType::CreateFirstProfile)
    {
        if (command.profile_slot != FrontEndProfileSlot::First)
        {
            return std::unexpected(
                std::string{"first profile command selected an invalid slot"});
        }
        return CreateFirstProfile();
    }
    if (command.type == FrontEndCommandType::CreateFirstCharacter)
    {
        if (command.character_slot != FrontEndCharacterSlot::First)
        {
            return std::unexpected(std::string{
                "first character command selected an invalid slot"});
        }
        return CreateFirstCharacter();
    }
    if (command.type == FrontEndCommandType::SetActiveCharacter)
    {
        constexpr std::string_view confirmation_failure_prefix =
            "active character confirmation failed: ";
        const auto confirmation_failure = [confirmation_failure_prefix](
                                              const ActiveCharacterConfirmationErrorCode code) {
            return std::string(confirmation_failure_prefix) +
                   std::string(ActiveCharacterConfirmationErrorCodeName(code));
        };
        if (native_persistence_ == nullptr || !ActiveProfileIsConfirmed())
        {
            return std::unexpected(confirmation_failure(
                ActiveCharacterConfirmationErrorCode::ActiveProfileRequired));
        }
        const std::size_t slot =
            static_cast<std::size_t>(command.character_slot);
        if (slot >= kFrontEndVisibleCharacters ||
            slot >= front_end_character_startup_model_.visible_characters ||
            slot >= front_end_character_startup_model_.total_characters)
        {
            return std::unexpected(confirmation_failure(
                ActiveCharacterConfirmationErrorCode::CharacterNotFound));
        }
        const auto& character_id =
            front_end_character_startup_model_.characters[slot].id;
        if (!character_id)
        {
            return std::unexpected(confirmation_failure(
                ActiveCharacterConfirmationErrorCode::CharacterNotFound));
        }
        auto confirmed = native_persistence_->ConfirmActiveCharacter(
            *active_profile_id_, *character_id);
        if (!confirmed)
            return std::unexpected(confirmation_failure(confirmed.error().code));
        active_character_id_ = *character_id;
        return {};
    }
    if (command.type == FrontEndCommandType::StartDiagnosticCampaign)
    {
        if (command.profile_slot != FrontEndProfileSlot::First)
        {
            return std::unexpected(std::string{
                "diagnostic campaign start selected an invalid slot"});
        }
        // Only private renderer/capture tests can construct OmegaApp without
        // NativePersistence. Production Create always owns the persistence
        // boundary, so this compatibility seam cannot bypass the runtime
        // active-profile prerequisite.
        if (native_persistence_ == nullptr)
            return {};

        constexpr std::string_view start_failure_prefix =
            "game session start failed: ";
        const auto start_failure = [start_failure_prefix](
                                       const GameSessionStartErrorCode code) {
            return std::string(start_failure_prefix) +
                   std::string(GameSessionStartErrorCodeName(code));
        };
        if (!ActiveProfileIsConfirmed())
        {
            return std::unexpected(start_failure(
                GameSessionStartErrorCode::ActiveProfileRequired));
        }
        if (!ActiveCharacterIsConfirmed())
        {
            return std::unexpected(start_failure(
                GameSessionStartErrorCode::ActiveCharacterRequired));
        }

        // ActiveProfileIsConfirmed resolves this same identity against the
        // current bounded startup model on the serialized game thread.
        auto prepared = native_persistence_->PrepareGameSessionStart(
            *active_profile_id_, *active_character_id_);
        if (!prepared)
            return std::unexpected(start_failure(prepared.error().code));
        return DeployDiagnosticMission();
    }
    if (command.type != FrontEndCommandType::SetActiveProfile)
        return {};

    constexpr std::string_view confirmation_failure_prefix =
        "active profile confirmation failed: ";
    const auto confirmation_failure = [confirmation_failure_prefix](
                                          const ActiveProfileConfirmationErrorCode code) {
        return std::string(confirmation_failure_prefix) +
               std::string(ActiveProfileConfirmationErrorCodeName(code));
    };

    const std::size_t slot = static_cast<std::size_t>(command.profile_slot);
    if (slot >= kFrontEndVisibleProfiles || slot >= front_end_startup_model_.visible_profiles ||
        slot >= front_end_startup_model_.total_profiles)
    {
        return std::unexpected(confirmation_failure(
            ActiveProfileConfirmationErrorCode::ProfileNotFound));
    }

    const std::optional<profiles::ProfileId>& profile_id = front_end_startup_model_.profiles[slot].id;
    if (!profile_id || native_persistence_ == nullptr)
    {
        return std::unexpected(confirmation_failure(
            ActiveProfileConfirmationErrorCode::ProfileNotFound));
    }

    auto live_characters = native_persistence_->characters().ListBounded(
        *profile_id, kFrontEndMaximumCharacters);
    if (!live_characters)
    {
        return std::unexpected(
            "character catalog selection failed: " +
            std::string(profiles::CharacterCatalogErrorCodeName(
                live_characters.error().code)));
    }
    const auto projected_characters =
        MakeFrontEndCharacterStartupModel(*live_characters);
    if (!projected_characters)
    {
        return std::unexpected(
            "character projection failed: " +
            std::string(FrontEndModelErrorMessage(
                projected_characters.error())));
    }

    const bool can_create_first_character =
        projected_characters->total_characters == 0U;
    auto built_character_presentation = BuildCharacterPresentation(
        *projected_characters,
        FrontEndCapabilities{
            .supports_character_selection = true,
            .can_create_first_character = can_create_first_character,
        });
    if (!built_character_presentation)
        return std::unexpected(built_character_presentation.error());
    std::optional<CharacterPresentation> next_character_presentation{
        std::in_place, std::move(*built_character_presentation)};

    std::optional<CharacterPresentation> next_first_character_presentation;
    if (can_create_first_character)
    {
        const std::array preview_characters{
            profiles::CharacterSummary{
                .id = kFirstCharacterId,
                .metadata = profiles::CharacterMetadata{
                    .display_name =
                        std::string{kFrontEndFirstCharacterDisplayName},
                    .created_unix_milliseconds = 0U,
                    .modified_unix_milliseconds = 0U,
                },
                .metadata_revision = 1U,
            },
        };
        const auto preview_model =
            MakeFrontEndCharacterStartupModel(preview_characters);
        if (!preview_model)
        {
            ReleaseCharacterPresentation(next_character_presentation);
            return std::unexpected(
                std::string{"first character preview projection failed"});
        }
        auto built_preview = BuildCharacterPresentation(
            *preview_model,
            FrontEndCapabilities{.supports_character_selection = true});
        if (!built_preview)
        {
            ReleaseCharacterPresentation(next_character_presentation);
            return std::unexpected(built_preview.error());
        }
        next_first_character_presentation.emplace(
            std::move(*built_preview));
    }

    auto confirmed = native_persistence_->ConfirmActiveProfile(*profile_id);
    if (!confirmed)
    {
        ReleaseCharacterPresentation(next_first_character_presentation);
        ReleaseCharacterPresentation(next_character_presentation);
        return std::unexpected(confirmation_failure(confirmed.error().code));
    }

    ReleaseCharacterPresentation(first_character_presentation_);
    ReleaseCharacterPresentation(character_presentation_);
    character_presentation_ = std::move(next_character_presentation);
    first_character_presentation_ =
        std::move(next_first_character_presentation);
    front_end_character_startup_model_ = *projected_characters;
    can_create_first_character_ = can_create_first_character;
    active_profile_id_ = *profile_id;
    active_character_id_.reset();
    return {};
}

std::expected<void, std::string> OmegaApp::RefreshDiagnosticActorDrawList(
    const std::optional<runtime::PointerPositionQ16>& pointer_position,
    const runtime::FreeFlyInput& camera_input, const bool fire_held)
{
    if (simulation_ == nullptr)
    {
        return std::unexpected(
            std::string{"diagnostic actor position is unavailable"});
    }
    const std::optional<simulation::Position3> position =
        simulation_->PositionOf(debug_locomotion_entity_);
    if (!position)
    {
        return std::unexpected(
            std::string{"diagnostic actor position is unavailable"});
    }

    // Release the HUD texture superseded by a trigger on a prior frame: its draw
    // list has been rendered, and this frame's overlay will reference the current
    // handle. Deferring the release here avoids freeing a handle a just-built
    // overlay still references (which would fault the render).
    if (diagnostic_scene_presentation_ != nullptr &&
        diagnostic_scene_presentation_->pending_hud_release.valid())
    {
        static_cast<void>(host_->ReleaseTexture(
            diagnostic_scene_presentation_->pending_hud_release));
        diagnostic_scene_presentation_->pending_hud_release =
            runtime::RenderTextureHandle{};
    }

    constexpr runtime::RenderSourceRectQ16 full_source{
        .left = 0U,
        .top = 0U,
        .right = runtime::kNormalizedRenderExtent,
        .bottom = runtime::kNormalizedRenderExtent,
    };
    // Fixed worst case: base, actor, one objective-or-target marker, two target bars, and fire.
    std::array<runtime::RenderTextureBlitCommand, 6U> commands{};
    const std::span<const runtime::RenderTextureBlitCommand> base_commands =
        diagnostic_hidden_draw_list_.commands();
    if (base_commands.size() > 1U)
    {
        return std::unexpected(
            std::string{"diagnostic actor draw-list creation failed"});
    }

    std::size_t command_count = 0U;
    if (!base_commands.empty())
        commands[command_count++] = base_commands.front();
    commands[command_count++] = runtime::RenderTextureBlitCommand{
        .texture = diagnostic_actor_marker_texture_,
        .source = full_source,
        .destination =
            PlanProjectDiagnosticActorMarkerDestination(*position),
        .fit_mode = runtime::RenderTextureFitMode::Stretch,
        .filter_mode = runtime::RenderTextureFilterMode::Nearest,
    };
    const std::size_t overlay_command_offset = command_count;
    const auto objective_destination =
        PlanProjectDiagnosticObjectiveMarkerDestination(
            diagnostic_proximity_trigger_state_);
    if (objective_destination)
    {
        commands[command_count++] = runtime::RenderTextureBlitCommand{
            .texture = diagnostic_actor_marker_texture_,
            .source = full_source,
            .destination = *objective_destination,
            .fit_mode = runtime::RenderTextureFitMode::Stretch,
            .filter_mode = runtime::RenderTextureFilterMode::Nearest,
        };
    }
    const auto target_destination =
        PlanProjectDiagnosticTargetMarkerDestination(
            diagnostic_proximity_trigger_state_, diagnostic_target_fire_state_);
    if (target_destination)
    {
        commands[command_count++] = runtime::RenderTextureBlitCommand{
            .texture = diagnostic_actor_marker_texture_,
            .source = full_source,
            .destination = *target_destination,
            .fit_mode = runtime::RenderTextureFitMode::Stretch,
            .filter_mode = runtime::RenderTextureFilterMode::Nearest,
        };
    }
    if (debug_target_held_)
    {
        const auto target_cue_destinations =
            PlanProjectDiagnosticTargetCueRectangles(pointer_position);
        for (const runtime::RenderTargetRectQ16 destination :
             target_cue_destinations)
        {
            commands[command_count++] = runtime::RenderTextureBlitCommand{
                .texture = diagnostic_actor_marker_texture_,
                .source = full_source,
                .destination = destination,
                .fit_mode = runtime::RenderTextureFitMode::Stretch,
                .filter_mode = runtime::RenderTextureFilterMode::Nearest,
            };
        }
    }
    if (debug_fire_pressed_)
    {
        commands[command_count++] = runtime::RenderTextureBlitCommand{
            .texture = diagnostic_actor_marker_texture_,
            .source = full_source,
            .destination =
                PlanProjectDiagnosticFireCueRectangle(pointer_position),
            .fit_mode = runtime::RenderTextureFitMode::Stretch,
            .filter_mode = runtime::RenderTextureFilterMode::Nearest,
        };
    }
    auto created = runtime::RenderDrawList::Create(
        std::span<const runtime::RenderTextureBlitCommand>{
            commands.data(), command_count});
    if (!created)
    {
        return std::unexpected(
            std::string{"diagnostic actor draw-list creation failed"});
    }
    // Scene overlay (composited over the 3D level each frame): the world-anchored
    // markers, plus the screen-space objective HUD panel on top. The HUD is added
    // here (not to `commands`) so it appears only over the 3D level, not in the
    // full actor draw list.
    std::array<runtime::RenderTextureBlitCommand, commands.size() + 1U>
        scene_overlay_commands{};
    std::size_t scene_overlay_count = 0U;
    for (std::size_t index = overlay_command_offset; index < command_count;
         ++index)
        scene_overlay_commands[scene_overlay_count++] = commands[index];
    if (diagnostic_scene_presentation_ &&
        diagnostic_scene_presentation_->hud_texture.valid())
    {
        scene_overlay_commands[scene_overlay_count++] =
            runtime::RenderTextureBlitCommand{
                .texture = diagnostic_scene_presentation_->hud_texture,
                .source = full_source,
                .destination =
                    runtime::RenderTargetRectQ16{
                        .left = 512U,
                        .top = 512U,
                        .right = 19000U,
                        .bottom = 15000U,
                    },
                .fit_mode = runtime::RenderTextureFitMode::Contain,
                .filter_mode = runtime::RenderTextureFilterMode::Nearest,
            };
    }
    auto created_scene_overlay = runtime::RenderDrawList::Create(
        std::span<const runtime::RenderTextureBlitCommand>{
            scene_overlay_commands.data(), scene_overlay_count});
    if (!created_scene_overlay)
    {
        return std::unexpected(
            std::string{"diagnostic scene overlay draw-list creation failed"});
    }

    runtime::RenderMeshDrawList next_scene_draw_list;
    bool refresh_scene_draw_list = false;
    if (diagnostic_scene_presentation_ &&
        !diagnostic_scene_presentation_->draw_list.empty())
    {
        const std::span<const runtime::RenderMeshDrawCommand> environment_commands =
            diagnostic_scene_presentation_->environment_draw_list.commands();
        if (environment_commands.size() !=
                diagnostic_scene_presentation_->environment_command_count ||
            environment_commands.size() >=
                runtime::kMaximumRenderMeshDrawsPerFrame ||
            !diagnostic_scene_presentation_->actor_mesh_handle.valid())
        {
            return std::unexpected(
                std::string{"diagnostic scene draw-list creation failed: invalid-state"});
        }

        // Live free-fly camera: advance the pose from this frame's input and
        // rebuild camera.world_to_view (keeping the fixed view_to_clip), so both
        // the actor marker (below) and every environment mesh reproject from the
        // new viewpoint. Accumulates across frames (the pose lives on the
        // presentation). Inactive when free_fly_active is false (fixed camera).
        const bool free_fly = diagnostic_scene_presentation_->free_fly_active;
        const bool player_active = diagnostic_scene_presentation_->player_active;
        if (player_active)
        {
            // Step the kinematic player from this frame's input against the level
            // COL (nearby-culled), then follow it with the Z-up camera. camera_input
            // forward/strafe map to world +Y/+X horizontal move (up = +Z).
            const gameplay::CharacterInput move_input{
                .move = asset::Float3IR{
                    .x = camera_input.strafe, .y = camera_input.forward, .z = 0.0F}};
            std::vector<gameplay::CollisionTriangle> nearby;
            gameplay::SelectNearbyCollisionTriangles(
                diagnostic_scene_presentation_->player_collision,
                diagnostic_scene_presentation_->player_state.position,
                kPlayerCullRadius, nearby);
            diagnostic_scene_presentation_->player_state = gameplay::StepCharacter(
                diagnostic_scene_presentation_->player_state, move_input,
                diagnostic_scene_presentation_->player_params, nearby,
                kPlayerStepDt);
            // Objective triggers: entering a project-placed trigger volume
            // completes the linked objective and rebuilds the HUD panel texture
            // (one-shot). The refreshed hud_texture is picked up by the overlay
            // blit on the next frame; releasing the prior handle avoids a leak.
            if (!diagnostic_scene_presentation_->mission_triggers.empty())
            {
                const auto trigger_step = gameplay::StepMissionTriggers(
                    gameplay::MinskMissionData(),
                    diagnostic_scene_presentation_->objective_state,
                    diagnostic_scene_presentation_->mission_triggers,
                    diagnostic_scene_presentation_->player_state.position);
                if (trigger_step.changed)
                {
                    diagnostic_scene_presentation_->objective_state =
                        trigger_step.state;
                    const auto rebuilt_hud = BuildObjectiveHudPanelTexture(
                        *host_, gameplay::MinskMissionData(), trigger_step.state);
                    if (rebuilt_hud.valid())
                    {
                        // This frame's overlay still references the old handle;
                        // stash it for release at the top of the next refresh
                        // (after this frame's render consumes it). Any earlier
                        // pending handle is already released by then.
                        if (diagnostic_scene_presentation_->hud_texture.valid())
                            diagnostic_scene_presentation_->pending_hud_release =
                                diagnostic_scene_presentation_->hud_texture;
                        diagnostic_scene_presentation_->hud_texture = rebuilt_hud;
                        log_->Info(
                            "player", "objective trigger fired; HUD updated");
                    }
                }
            }
            diagnostic_scene_presentation_->camera.world_to_view = PlayerFollowView(
                diagnostic_scene_presentation_->player_state.position,
                diagnostic_scene_presentation_->player_params.radius);
            const gameplay::CharacterState& pp =
                diagnostic_scene_presentation_->player_state;
            log_->Info("player",
                "pos=(" + std::to_string(pp.position.x) + "," +
                    std::to_string(pp.position.y) + "," +
                    std::to_string(pp.position.z) +
                    ") grounded=" + (pp.grounded ? "yes" : "no") +
                    " nearby=" + std::to_string(nearby.size()));

            // Enemy NPCs: each patrols (or, once alerted, pursues the player's
            // last-seen position) kinematically vs the same level COL, then tests
            // sight (vision cone + LOS) and advances its stealth loop
            // (Patrol->Alerted->Chasing->Searching->Patrol). Render colour tracks
            // the state (below): patrol green, searching yellow, alerted/chasing
            // red.
            if (diagnostic_scene_presentation_->npc_active)
            {
                DiagnosticScenePresentation &np = *diagnostic_scene_presentation_;
                for (std::size_t npc_index = 0U; npc_index < np.npcs.size();
                     ++npc_index)
                {
                    DiagnosticScenePresentation::NpcRuntime &npc =
                        np.npcs[npc_index];
                    // Combat S2: a dead guard stops planning, moving, seeing and
                    // firing. It stays in the vector so every index (and the
                    // parallel hitscan target array below) keeps its identity;
                    // it is drawn in the dead-actor colour instead.
                    if (!npc.health.alive)
                        continue;
                    // Pursue the last-seen spot while Chasing/Searching; else patrol.
                    gameplay::NpcPatrolPlan plan;
                    if (gameplay::NpcPursuing(npc.awareness.state) &&
                        npc.awareness.has_last_seen)
                    {
                        plan = gameplay::PlanNpcPursuit(npc.state.position,
                            npc.awareness.last_seen_player_pos,
                            npc.params.radius + 2.0F, npc.facing);
                    }
                    else
                    {
                        plan = gameplay::PlanNpcPatrol(npc.state.position,
                            npc.waypoints, npc.waypoint, npc.params.radius + 2.0F,
                            npc.facing);
                        npc.waypoint = plan.waypoint;
                    }
                    if (plan.facing.x != 0.0F || plan.facing.y != 0.0F ||
                        plan.facing.z != 0.0F)
                        npc.facing = plan.facing;
                    std::vector<gameplay::CollisionTriangle> npc_nearby;
                    gameplay::SelectNearbyCollisionTriangles(np.player_collision,
                        npc.state.position, npc.vision.range, npc_nearby);
                    npc.state = gameplay::StepCharacter(npc.state,
                        gameplay::CharacterInput{.move = plan.move}, npc.params,
                        npc_nearby, kPlayerStepDt);
                    const bool sees = gameplay::NpcSeesPlayer(npc.state.position,
                        npc.facing, np.player_state.position, npc.vision,
                        npc_nearby);
                    const gameplay::NpcState prev_state = npc.awareness.state;
                    npc.awareness = gameplay::StepNpcAwarenessLoop(npc.awareness,
                        sees, np.player_state.position, npc.awareness_params,
                        kPlayerStepDt);
                    const auto state_name = [](gameplay::NpcState s) {
                        switch (s)
                        {
                        case gameplay::NpcState::Patrol: return "patrol";
                        case gameplay::NpcState::Alerted: return "ALERTED";
                        case gameplay::NpcState::Chasing: return "CHASING";
                        case gameplay::NpcState::Searching: return "searching";
                        }
                        return "?";
                    };
                    if (prev_state != npc.awareness.state)
                        log_->Info("npc",
                            "npc " + std::to_string(npc_index) + ": " +
                                state_name(prev_state) + " -> " +
                                state_name(npc.awareness.state));
                    // Combat S2: a guard that has COMMITTED to the chase and
                    // still has clear line of sight aims up, then fires. The gate
                    // is Chasing specifically, not NpcPursuing: Alerted is the
                    // reaction delay before it commits, and Searching means it has
                    // lost sight of the player, so neither may shoot.
                    const bool engaging = npc.health.alive && sees &&
                        npc.awareness.state == gameplay::NpcState::Chasing;
                    const gameplay::WeaponStep npc_shot = gameplay::StepNpcWeapon(
                        npc.weapon, engaging, kPlayerStepDt, np.weapon_params);
                    npc.weapon = npc_shot.state;
                    if (npc_shot.fired)
                    {
                        log_->Info("combat",
                            "npc " + std::to_string(npc_index) +
                                " FIRED at the player");
                        if (np.player_health.alive)
                        {
                            const gameplay::HealthState before = np.player_health;
                            np.player_health = gameplay::ApplyDamageToHealth(
                                before, np.weapon_params.damage);
                            log_->Info("combat",
                                "player HIT by npc " + std::to_string(npc_index) +
                                    " for " +
                                    std::to_string(np.weapon_params.damage) +
                                    "; hitpoints " +
                                    std::to_string(before.hitpoints) + " -> " +
                                    std::to_string(np.player_health.hitpoints));
                            if (!np.player_health.alive)
                                log_->Info("combat",
                                    "PLAYER DEAD (killed by npc " +
                                        std::to_string(npc_index) +
                                        "); no further damage is applied -- "
                                        "respawn and the mission fail flow are a "
                                        "later slice");
                        }
                    }
                    log_->Info("npc",
                        "npc " + std::to_string(npc_index) + " pos=(" +
                            std::to_string(npc.state.position.x) + "," +
                            std::to_string(npc.state.position.y) + "," +
                            std::to_string(npc.state.position.z) + ") sees=" +
                            (sees ? "yes" : "no") + " state=" +
                            state_name(npc.awareness.state));
                }
            }

            // Combat S2, player side: the existing fire binding (SPACE / left
            // mouse -- kDebugFireAction) held this frame runs the player weapon.
            // There is no aim ramp, only the cooldown, so holding it is automatic
            // fire. A shot hitscans along the follow camera's aim from eye height
            // against the LIVE enemies, occluded by the very same pre-culled
            // collision triangles the player was just stepped against, and damages
            // whatever it strikes. A dead player cannot shoot.
            DiagnosticScenePresentation &fp = *diagnostic_scene_presentation_;
            const gameplay::WeaponStep player_shot = gameplay::StepPlayerWeapon(
                fp.player_weapon, fire_held && fp.player_health.alive,
                kPlayerStepDt, fp.weapon_params);
            fp.player_weapon = player_shot.state;
            if (player_shot.fired)
            {
                const asset::Float3IR muzzle{
                    .x = fp.player_state.position.x,
                    .y = fp.player_state.position.y,
                    .z = fp.player_state.position.z + kPlayerEyeHeight};
                std::vector<asset::Float3IR> targets;
                targets.reserve(fp.npcs.size());
                // std::vector<bool> is a bit-proxy and cannot form the
                // std::span<const bool> ResolveHitscan takes, so the parallel
                // alive flags live in a plain contiguous buffer.
                const std::unique_ptr<bool[]> target_alive =
                    std::make_unique<bool[]>(fp.npcs.size());
                for (std::size_t i = 0U; i < fp.npcs.size(); ++i)
                {
                    targets.push_back(fp.npcs[i].state.position);
                    target_alive[i] = fp.npcs[i].health.alive;
                }
                const gameplay::HitscanResult scan = gameplay::ResolveHitscan(
                    muzzle, kPlayerAimDirection, targets,
                    std::span<const bool>{target_alive.get(), fp.npcs.size()},
                    kNpcHitRadius, kPlayerFireRange, nearby);
                log_->Info("combat",
                    "player FIRED from (" + std::to_string(muzzle.x) + "," +
                        std::to_string(muzzle.y) + "," +
                        std::to_string(muzzle.z) + ") vs " +
                        std::to_string(fp.npcs.size()) + " enemies");
                if (scan.hit && scan.target < fp.npcs.size())
                {
                    DiagnosticScenePresentation::NpcRuntime &struck =
                        fp.npcs[scan.target];
                    const gameplay::HealthState before = struck.health;
                    struck.health = gameplay::ApplyDamageToHealth(
                        before, fp.weapon_params.damage);
                    log_->Info("combat",
                        "npc " + std::to_string(scan.target) + " HIT for " +
                            std::to_string(fp.weapon_params.damage) +
                            " at range " + std::to_string(scan.distance) +
                            "; hitpoints " + std::to_string(before.hitpoints) +
                            " -> " + std::to_string(struck.health.hitpoints));
                    if (!struck.health.alive)
                        log_->Info("combat",
                            "npc " + std::to_string(scan.target) +
                                " DEAD; it stops planning, moving, seeing and "
                                "firing");
                }
            }
        }
        else if (free_fly)
        {
            diagnostic_scene_presentation_->free_fly_pose = runtime::AdvanceFreeFly(
                diagnostic_scene_presentation_->free_fly_pose, camera_input,
                diagnostic_scene_presentation_->free_fly_move_speed);
            diagnostic_scene_presentation_->camera.world_to_view =
                runtime::FreeFlyViewMatrix(
                    diagnostic_scene_presentation_->free_fly_pose);
        }
        const bool camera_moves = free_fly || player_active;

        const auto actor_object_to_clip = runtime::ComposeObjectToClip(
            diagnostic_scene_presentation_->camera,
            player_active
                ? PlayerMeshTransform(
                      diagnostic_scene_presentation_->player_state.position)
                : PlanProjectDiagnosticActorMeshTransform(*position));
        if (!actor_object_to_clip)
        {
            return std::unexpected(
                std::string{"diagnostic scene transform is non-finite"});
        }

        std::array<runtime::RenderMeshDrawCommand,
            runtime::kMaximumRenderMeshDrawsPerFrame> mesh_commands{};
        std::size_t mesh_command_count = 0U;
        for (std::size_t env_index = 0U;
             env_index < environment_commands.size(); ++env_index)
        {
            runtime::RenderMeshDrawCommand command =
                environment_commands[env_index];
            if (camera_moves)
            {
                const auto reprojected = runtime::ComposeObjectToClip(
                    diagnostic_scene_presentation_->camera,
                    diagnostic_scene_presentation_
                        ->environment_local_to_world[env_index]);
                if (!reprojected)
                {
                    return std::unexpected(
                        std::string{"diagnostic scene transform is non-finite"});
                }
                command.object_to_clip = *reprojected;
            }
            mesh_commands[mesh_command_count++] = command;
        }
        mesh_commands[mesh_command_count++] = runtime::RenderMeshDrawCommand{
            .mesh = diagnostic_scene_presentation_->actor_mesh_handle,
            .object_to_clip = *actor_object_to_clip,
            // Combat S2: a dead player is drawn in the dead-actor colour.
            .color = player_active &&
                    !diagnostic_scene_presentation_->player_health.alive
                ? kDeadActorMeshColor
                : kDiagnosticActorMeshColor,
            .raster_mode = runtime::RenderMeshRasterMode::Fill,
        };
        // Enemy NPC markers: the same cube mesh at each NPC's world position,
        // coloured by stealth state (patrol = green, searching = yellow, alerted/
        // chasing = red), through the same camera as the level/player.
        if (diagnostic_scene_presentation_->npc_active)
        {
            for (const DiagnosticScenePresentation::NpcRuntime &npc :
                 diagnostic_scene_presentation_->npcs)
            {
                if (mesh_command_count >= runtime::kMaximumRenderMeshDrawsPerFrame)
                    break;
                const auto npc_object_to_clip = runtime::ComposeObjectToClip(
                    diagnostic_scene_presentation_->camera,
                    NpcMeshTransform(npc.state.position));
                if (!npc_object_to_clip)
                    continue;
                runtime::RenderMeshColorRgba8 npc_color{
                    .red = 40U, .green = 230U, .blue = 60U, .alpha = 255U}; // patrol
                // Combat S2: death outranks the stealth-state colour, so a killed
                // guard reads as dead on screen rather than frozen mid-alert.
                if (!npc.health.alive)
                    npc_color = kDeadActorMeshColor;
                else if (npc.awareness.state == gameplay::NpcState::Searching)
                    npc_color = runtime::RenderMeshColorRgba8{
                        .red = 240U, .green = 220U, .blue = 40U, .alpha = 255U};
                else if (npc.awareness.state == gameplay::NpcState::Alerted ||
                         npc.awareness.state == gameplay::NpcState::Chasing)
                    npc_color = runtime::RenderMeshColorRgba8{
                        .red = 255U, .green = 24U, .blue = 24U, .alpha = 255U};
                mesh_commands[mesh_command_count++] =
                    runtime::RenderMeshDrawCommand{
                        .mesh = diagnostic_scene_presentation_->actor_mesh_handle,
                        .object_to_clip = *npc_object_to_clip,
                        .color = npc_color,
                        .raster_mode = runtime::RenderMeshRasterMode::Fill,
                    };
            }
        }
        auto created_scene_draw_list = runtime::RenderMeshDrawList::Create(
            std::span<const runtime::RenderMeshDrawCommand>{
                mesh_commands.data(), mesh_command_count});
        if (!created_scene_draw_list)
        {
            return std::unexpected("diagnostic scene draw-list creation failed: " +
                                   std::string(runtime::RenderMeshDrawListErrorCodeName(
                                       created_scene_draw_list.error().code)));
        }
        next_scene_draw_list = std::move(*created_scene_draw_list);
        refresh_scene_draw_list = true;
    }

    diagnostic_actor_draw_list_ = std::move(*created);
    if (diagnostic_scene_presentation_)
    {
        diagnostic_scene_presentation_->overlay_draw_list =
            std::move(*created_scene_overlay);
        if (refresh_scene_draw_list)
        {
            diagnostic_scene_presentation_->draw_list =
                std::move(next_scene_draw_list);
        }
    }
    return {};
}

const runtime::RenderDrawList &OmegaApp::CurrentFrontEndDrawList() const noexcept
{
    // Project multiplayer menu overlay (dev-gated): when active it replaces the
    // whole front end with the MP menu card. DeveloperDiagnostics only.
    if (mp_menu_active_ && mp_menu_ready_)
        return mp_menu_draw_list_;

    // Retail presentation (Gap B Phase 1): once the decoded retail screen has been
    // composited, it IS the whole front end -- a single full-frame blit of the
    // static Title -- and supersedes every project-authored per-mode draw list.
    // Built once by BuildRetailFrontEndPresentationIfPossible under the retail
    // presentation mode + experimental opt-in; until then this falls through to
    // the project presentation (developer-diagnostics mode never sets it).
    if (retail_front_end_ready_)
        return retail_front_end_draw_list_;

    const FrontEndView view = BuildFrontEndView(
        front_end_state_, content_stage_, front_end_startup_model_,
        active_profile_id_, front_end_character_startup_model_,
        active_character_id_);
    const std::size_t selected_main_row = static_cast<std::size_t>(view.selected_main_row);
    if (selected_main_row >= front_end_presentation_.main_draw_lists.size())
        return front_end_presentation_.main_draw_lists.front();

    switch (view.mode)
    {
    case FrontEndMode::Main:
    case FrontEndMode::AgentCreation:
    case FrontEndMode::BriefingRoom:
        return front_end_presentation_.main_draw_lists[selected_main_row];
    case FrontEndMode::Profiles:
    {
        const std::size_t profile_slot = static_cast<std::size_t>(view.selected_profile_slot);
        if (profile_slot < view.profiles.visible_profiles &&
            profile_slot < front_end_presentation_.profile_selection_draw_lists.size())
        {
            // The cue position comes only from the identifier the view resolved
            // against the model it publishes, so an unresolvable confirmation
            // simply falls back to the unmarked selection list.
            if (view.active_profile_slot &&
                front_end_presentation_.profile_active_draw_lists)
            {
                const std::size_t active_slot =
                    static_cast<std::size_t>(*view.active_profile_slot);
                if (active_slot <
                    (*front_end_presentation_.profile_active_draw_lists)[profile_slot]
                        .size())
                {
                    return (*front_end_presentation_.profile_active_draw_lists)
                        [profile_slot][active_slot];
                }
            }
            return front_end_presentation_.profile_selection_draw_lists[profile_slot];
        }
        return front_end_presentation_.profiles_draw_list;
    }
    case FrontEndMode::Characters:
    {
        if (!character_presentation_)
            return front_end_presentation_.main_draw_lists.front();
        const std::size_t character_slot =
            static_cast<std::size_t>(view.selected_character_slot);
        if (character_slot < view.characters.visible_characters &&
            character_slot <
                character_presentation_->selection_draw_lists.size())
        {
            return character_presentation_->selection_draw_lists[character_slot];
        }
        return character_presentation_->draw_list;
    }
    case FrontEndMode::Controls:
        return diagnostic_controls_draw_list_;
    case FrontEndMode::AssetTopology:
        return diagnostic_asset_topology_draw_list_;
    case FrontEndMode::DiagnosticPlay:
        if (diagnostic_scene_presentation_ &&
            !diagnostic_scene_presentation_->draw_list.empty())
        {
            return diagnostic_scene_presentation_->overlay_draw_list;
        }
        return diagnostic_actor_draw_list_;
    }
    return front_end_presentation_.main_draw_lists.front();
}

runtime::RenderMeshDrawList OmegaApp::CurrentFrontEndMeshDrawList() const noexcept
{
    const FrontEndView view = BuildFrontEndView(
        front_end_state_, content_stage_, front_end_startup_model_,
        active_profile_id_, front_end_character_startup_model_,
        active_character_id_);
    if (view.mode != FrontEndMode::DiagnosticPlay)
        return {};
    return diagnostic_scene_presentation_
        ? diagnostic_scene_presentation_->draw_list
        : runtime::RenderMeshDrawList{};
}

std::string_view OmegaApp::driver_name() const noexcept
{
    return host_->driver_name();
}

std::string_view OmegaApp::audio_driver_name() const noexcept
{
    return audio_->driver_name();
}

int OmegaApp::audio_sample_rate() const noexcept
{
    return SdlAudioService::kSampleRate;
}

int OmegaApp::audio_channel_count() const noexcept
{
    return SdlAudioService::kChannelCount;
}

std::optional<profiles::ProfileId> OmegaApp::active_profile_id() const noexcept
{
    return active_profile_id_;
}

std::optional<profiles::CharacterId> OmegaApp::active_character_id() const noexcept
{
    return active_character_id_;
}

gameplay::DiagnosticTargetFireState OmegaApp::diagnostic_target_fire_state() const noexcept
{
    return diagnostic_target_fire_state_;
}

gameplay::DiagnosticMissionLifecycleState
OmegaApp::diagnostic_mission_lifecycle_state() const noexcept
{
    return diagnostic_mission_lifecycle_state_;
}

gameplay::DiagnosticProximityTriggerState
OmegaApp::diagnostic_proximity_trigger_state() const noexcept
{
    return diagnostic_proximity_trigger_state_;
}

std::optional<simulation::Position3>
OmegaApp::diagnostic_actor_position() const noexcept
{
    return simulation_->PositionOf(debug_locomotion_entity_);
}

FrontEndState OmegaApp::front_end_state() const noexcept
{
    return front_end_state_;
}
} // namespace omega::app
