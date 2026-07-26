#include "opening_movie_player.h"
#include "omega_app.h"

#include "omega/asset/frontend_ir.h"
#include "omega/content/front_end_screen_bundle.h"
#include "omega/content/retail_front_end_presentation_capability.h"
#include "omega/frontend/compositor_math.h"
#include "omega/runtime/config_service.h"
#include "omega/runtime/content_startup.h"
#include "omega/runtime/input_tracker.h"
#include "omega/runtime/runtime_settings.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace omega::content::detail
{
struct RetailFrontEndPresentationCapabilityTestAccess final
{
    // `valid == false` yields a moved-from capability, which ComposeRetailFrontEndFrame
    // rejects with InvalidRetailCapability. That is the cheapest deterministic way
    // to build a bundle that loads but cannot compose -- exactly the case the
    // commit invariant exists for.
    [[nodiscard]] static RetailFrontEndPresentationCapability Make(
        const bool valid = true) noexcept
    {
        RetailFrontEndPresentationCapability capability(
            RetailFrontEndPresentationCapability::ConstructionKey{});
        if (!valid)
        {
            RetailFrontEndPresentationCapability consumed(std::move(capability));
            (void)consumed;
        }
        return capability;
    }
};

struct FrontEndScreenBundleTestAccess final
{
    [[nodiscard]] static FrontEndVisualScope MakeScope(
        asset::FrontendVisualDocumentIR document,
        FrontEndVisualScope::ResourceSet resources,
        FrontEndVisualScope::TextureMap textures)
    {
        return FrontEndVisualScope(
            std::move(document), std::move(resources), std::move(textures));
    }

    [[nodiscard]] static FrontEndScreenBundle MakeBundle(
        asset::FrontendWidgetDocumentIR widget_document,
        std::string primary_scope,
        FrontEndScreenBundle::VisualScopeMap visual_scopes,
        RetailFrontEndPresentationCapability capability)
    {
        return FrontEndScreenBundle(FrontEndScreenKey::Title,
            std::move(widget_document), std::move(primary_scope),
            std::move(visual_scopes), {}, {}, {}, std::move(capability));
    }
};
} // namespace omega::content::detail

namespace omega::app::detail
{
struct OmegaAppTestAccess final
{
    [[nodiscard]] static std::expected<OmegaApp, std::string> Create(
        runtime::ConfigStore config, const runtime::RuntimeSettings& settings,
        std::unique_ptr<OpeningMoviePlayback> opening_movie_playback,
        const runtime::FrontEndPresentationMode presentation_mode)
    {
        return OmegaApp::CreateWithTextureConfigAndOpeningMoviePlayback(
            std::move(config), settings, runtime::ContentStartupState{}, nullptr,
            false, {}, std::nullopt, std::nullopt,
            std::move(opening_movie_playback), presentation_mode);
    }

    [[nodiscard]] static GpuHostSnapshot Gpu(const OmegaApp& app) noexcept
    {
        return app.host_->Snapshot();
    }

    [[nodiscard]] static bool ComposeRetailScreenPresentation(OmegaApp& app,
        const content::FrontEndScreenBundle& bundle,
        const std::uint32_t animation_tick) noexcept
    {
        bool has_animation = false;
        const bool published = app.ComposeRetailScreenPresentation(
            bundle, {}, animation_tick, has_animation);
        if (published)
        {
            app.retail_animation_tick_ = animation_tick;
            app.retail_screen_has_animation_ = has_animation;
        }
        return published;
    }

    // Drives one whole presentation frame, which is what actually enforces the
    // commit invariant: navigation moves only through a candidate that published.
    static void UpdateRetailFrontEndPresentation(OmegaApp& app,
        const frontend::presentation::RetailFrontEndNavInput& input) noexcept
    {
        app.UpdateRetailFrontEndPresentation(input);
    }

    [[nodiscard]] static frontend::presentation::RetailFrontEndNavState RetailNav(
        const OmegaApp& app) noexcept
    {
        return app.retail_nav_;
    }

    [[nodiscard]] static std::optional<frontend::presentation::RetailFrontEndNavState>
    RetailComposedNav(const OmegaApp& app) noexcept
    {
        return app.retail_composed_nav_;
    }

    // Seeds a screen's cache slot directly so the nav/commit behaviour can be
    // exercised without an owner data root. This installs a project-generated
    // synthetic bundle; it reads no retail content.
    static void InstallRetailBundle(OmegaApp& app,
        const content::FrontEndScreenKey key,
        content::FrontEndScreenBundle bundle) noexcept
    {
        if (auto* const slot = app.RetailBundleSlotForScreen(key))
            *slot = std::move(bundle);
    }

    static void ClearRetailBundle(
        OmegaApp& app, const content::FrontEndScreenKey key) noexcept
    {
        if (auto* const slot = app.RetailBundleSlotForScreen(key))
            slot->reset();
    }

    // Drives the startup screen choice exactly as LoadRetailFrontEndBundleIfEnabled
    // does once it has parsed the environment override, so the staged-adoption
    // behaviour is exercised without setting a process environment variable.
    static void BeginRetailFrontEndPresentation(OmegaApp& app,
        const std::optional<content::FrontEndScreenKey> requested,
        const bool override_requested) noexcept
    {
        app.BeginRetailFrontEndPresentation(requested, override_requested);
    }

    [[nodiscard]] static runtime::RenderTextureHandle RetailFrontEndTexture(
        const OmegaApp& app) noexcept
    {
        return app.retail_front_end_texture_;
    }

    [[nodiscard]] static std::span<const runtime::RenderTextureBlitCommand>
    RetailFrontEndDrawCommands(const OmegaApp& app) noexcept
    {
        return app.retail_front_end_draw_list_.commands();
    }

    static void ReplaceRetailFrontEndDrawList(
        OmegaApp& app, runtime::RenderDrawList draw_list) noexcept
    {
        app.retail_front_end_draw_list_ = std::move(draw_list);
    }

    [[nodiscard]] static bool RetailFrontEndReady(
        const OmegaApp& app) noexcept
    {
        return app.retail_front_end_ready_;
    }

    [[nodiscard]] static bool RetailScreenHasAnimation(
        const OmegaApp& app) noexcept
    {
        return app.retail_screen_has_animation_;
    }

    [[nodiscard]] static simulation::SimulationState Simulation(
        const OmegaApp& app) noexcept
    {
        return app.simulation_->Snapshot();
    }

    [[nodiscard]] static BootSequenceState BootSequence(
        const OmegaApp& app) noexcept
    {
        return app.boot_sequence_state_;
    }

    [[nodiscard]] static bool HasOpeningMoviePlayback(
        const OmegaApp& app) noexcept
    {
        return app.opening_movie_player_ != nullptr;
    }

    [[nodiscard]] static bool HasOpeningMovieTexture(
        const OmegaApp& app) noexcept
    {
        return app.opening_movie_texture_.valid();
    }
};
} // namespace omega::app::detail

namespace
{
using omega::app::GpuHostSnapshot;
using omega::app::OpeningMoviePlayback;
using omega::app::OpeningMoviePlayerError;
using omega::app::OpeningMoviePlayerStatus;
using omega::app::OpeningMoviePlayerUpdate;
using omega::app::OmegaApp;
using omega::app::detail::OmegaAppTestAccess;
using omega::asset::Float3IR;
using omega::asset::FrontendColorRgba8IR;
using omega::asset::FrontendTriangleIR;
using omega::asset::FrontendVisualNodeIR;
using omega::asset::FrontendWidgetBindingIR;
using omega::asset::FrontendWidgetIR;
using omega::asset::FrontendWidgetKind;
using omega::content::FrontEndScreenBundle;
using omega::content::FrontEndVisualScope;
using omega::content::detail::FrontEndScreenBundleTestAccess;
using omega::content::detail::RetailFrontEndPresentationCapabilityTestAccess;
using omega::frontend::kIdentityAffineTransform12;

constexpr std::uint32_t kMovieWidth = 2U;
constexpr std::uint32_t kMovieHeight = 2U;
constexpr std::uint64_t kMovieLogicalBytes =
    static_cast<std::uint64_t>(kMovieWidth) * kMovieHeight * 4U;
constexpr std::uint64_t kMovieSafetyTicks = 60'000'000U;
constexpr std::string_view kUnavailableError =
    "front-end presentation [presentation-unavailable]: retail front-end presentation is unavailable";

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

struct GeneratedMovieObservation final
{
    std::size_t advance_calls = 0U;
    std::size_t destruction_count = 0U;
};

class GeneratedOpeningMovie final : public OpeningMoviePlayback
{
public:
    explicit GeneratedOpeningMovie(
        std::shared_ptr<GeneratedMovieObservation> observation)
        : observation_(std::move(observation))
    {
        frame_.width = kMovieWidth;
        frame_.height = kMovieHeight;
        frame_.pixels.resize(
            static_cast<std::size_t>(kMovieLogicalBytes), std::byte{0});
        for (std::size_t alpha = 3U; alpha < frame_.pixels.size(); alpha += 4U)
            frame_.pixels[alpha] = std::byte{255};
    }

    ~GeneratedOpeningMovie() noexcept override
    {
        ++observation_->destruction_count;
    }

    GeneratedOpeningMovie(const GeneratedOpeningMovie&) = delete;
    GeneratedOpeningMovie& operator=(const GeneratedOpeningMovie&) = delete;
    GeneratedOpeningMovie(GeneratedOpeningMovie&&) = delete;
    GeneratedOpeningMovie& operator=(GeneratedOpeningMovie&&) = delete;

    [[nodiscard]] std::expected<OpeningMoviePlayerUpdate,
        OpeningMoviePlayerError>
    Advance(const std::chrono::nanoseconds) override
    {
        ++observation_->advance_calls;
        const bool first_frame = observation_->advance_calls == 1U;
        return OpeningMoviePlayerUpdate{
            .status = first_frame ? OpeningMoviePlayerStatus::Playing
                                  : OpeningMoviePlayerStatus::Completed,
            .frame_updated = first_frame,
            .current_frame = &frame_,
        };
    }

    [[nodiscard]] std::expected<std::uint64_t, OpeningMoviePlayerError>
    ReadAudioFrames(const std::span<std::int16_t>) override
    {
        return 0U;
    }

    [[nodiscard]] bool audio_finished() const noexcept override
    {
        return true;
    }

    [[nodiscard]] std::uint32_t width() const noexcept override
    {
        return kMovieWidth;
    }

    [[nodiscard]] std::uint32_t height() const noexcept override
    {
        return kMovieHeight;
    }

    [[nodiscard]] std::uint64_t safety_duration_ticks() const noexcept override
    {
        return kMovieSafetyTicks;
    }

private:
    std::shared_ptr<GeneratedMovieObservation> observation_;
    omega::media::Rgba8VideoFrame frame_;
};

[[nodiscard]] omega::runtime::RuntimeSettings TestSettings()
{
    omega::runtime::RuntimeSettings settings;
    settings.jobs.worker_count = 1U;
    settings.jobs.max_pending_jobs = 8U;
    settings.frame.simulation_step = omega::runtime::kMinimumSimulationStep;
    settings.frame.max_steps_per_frame = 8U;
    settings.frame.max_frame_delta =
        omega::runtime::kMinimumSimulationStep * 8;
    settings.max_input_events_per_frame =
        omega::runtime::InputTracker::kMaxEventsPerFrameLimit;
    return settings;
}

[[nodiscard]] FrontendVisualNodeIR MakeQuad(std::string identifier,
    const float half_width, const float half_height,
    const FrontendColorRgba8IR color)
{
    return FrontendVisualNodeIR{
        .identifier = std::move(identifier),
        .transform_values = kIdentityAffineTransform12.column_vectors,
        .positions = {
            Float3IR{.x = -half_width, .y = half_height, .z = 17.0F},
            Float3IR{.x = half_width, .y = half_height, .z = 17.0F},
            Float3IR{.x = half_width, .y = -half_height, .z = 17.0F},
            Float3IR{.x = -half_width, .y = -half_height, .z = 17.0F},
        },
        .uvs = {{.u = 0.0F, .v = 0.0F}},
        .colors = {color},
        .triangles = {
            FrontendTriangleIR{
                .position_indices = {0U, 1U, 2U},
                .uv_indices = {0U, 0U, 0U},
                .color_indices = {0U, 0U, 0U},
            },
            FrontendTriangleIR{
                .position_indices = {0U, 2U, 3U},
                .uv_indices = {0U, 0U, 0U},
                .color_indices = {0U, 0U, 0U},
            },
        },
    };
}

[[nodiscard]] FrontendWidgetIR MakeWidget(std::string identifier)
{
    return FrontendWidgetIR{
        .kind = FrontendWidgetKind::Container,
        .identifier = std::move(identifier),
        .rectangle = {
            .left = -320.0F,
            .top = 224.0F,
            .width = 640.0F,
            .height = 448.0F,
        },
        .visible = true,
        .enabled = true,
        .binding = FrontendWidgetBindingIR{
            .transform_values = kIdentityAffineTransform12.column_vectors,
        },
    };
}

[[nodiscard]] FrontEndScreenBundle MakeAnimatedOpacityBundle()
{
    FrontendVisualNodeIR root = MakeQuad("ROOT_root", 320.0F, 224.0F,
        FrontendColorRgba8IR{
            .red = 16U,
            .green = 16U,
            .blue = 16U,
            .alpha = 255U,
        });
    FrontendVisualNodeIR child = MakeQuad("LOGO", 120.0F, 90.0F,
        FrontendColorRgba8IR{
            .red = 240U,
            .green = 48U,
            .blue = 48U,
            .alpha = 255U,
        });
    child.animation_tracks.push_back(omega::asset::FrontendScalarAnimationTrackIR{
        .target = omega::asset::FrontendScalarAnimationTarget::Opacity,
        .keys = {
            omega::asset::FrontendScalarAnimationKeyIR{
                .timeline_tick = 0.0F,
                .value = 1.0F,
            },
            omega::asset::FrontendScalarAnimationKeyIR{
                .timeline_tick = 20.0F,
                .value = 0.0F,
            },
        },
    });
    root.children.push_back(std::move(child));

    omega::asset::FrontendVisualDocumentIR document{.root = std::move(root)};
    auto scope = FrontEndScreenBundleTestAccess::MakeScope(std::move(document),
        FrontEndVisualScope::ResourceSet{"ROOT_root", "LOGO"}, {});
    FrontEndScreenBundle::VisualScopeMap scopes;
    scopes.emplace("TITLE", std::move(scope));

    FrontendWidgetIR root_widget = MakeWidget("ROOT");
    root_widget.children.push_back(MakeWidget("LOGO"));
    return FrontEndScreenBundleTestAccess::MakeBundle(
        omega::asset::FrontendWidgetDocumentIR{.root = std::move(root_widget)},
        "TITLE", std::move(scopes),
        RetailFrontEndPresentationCapabilityTestAccess::Make());
}

// A composable screen carrying one visible Button that routes to CreateAgent.
// `valid_capability == false` makes the very same screen fail to compose, so a
// test can hold the shape constant and vary only presentability.
[[nodiscard]] FrontEndScreenBundle MakeRoutingBundle(const bool valid_capability)
{
    FrontendVisualNodeIR root = MakeQuad("ROOT_root", 320.0F, 224.0F,
        FrontendColorRgba8IR{
            .red = 24U,
            .green = 24U,
            .blue = 24U,
            .alpha = 255U,
        });
    omega::asset::FrontendVisualDocumentIR document{.root = std::move(root)};
    auto scope = FrontEndScreenBundleTestAccess::MakeScope(std::move(document),
        FrontEndVisualScope::ResourceSet{"ROOT_root"}, {});
    FrontEndScreenBundle::VisualScopeMap scopes;
    scopes.emplace("TITLE", std::move(scope));

    FrontendWidgetIR root_widget = MakeWidget("ROOT");
    FrontendWidgetIR button = MakeWidget("newagent");
    button.kind = FrontendWidgetKind::Button;
    root_widget.children.push_back(std::move(button));
    return FrontEndScreenBundleTestAccess::MakeBundle(
        omega::asset::FrontendWidgetDocumentIR{.root = std::move(root_widget)},
        "TITLE", std::move(scopes),
        RetailFrontEndPresentationCapabilityTestAccess::Make(valid_capability));
}

[[nodiscard]] std::expected<OmegaApp, std::string> CreateRetailApp(
    std::unique_ptr<OpeningMoviePlayback> playback)
{
    auto config = omega::runtime::ParseConfigText("");
    if (!config)
        return std::unexpected("test config: " + config.error());
    return OmegaAppTestAccess::Create(std::move(*config), TestSettings(),
        std::move(playback),
        omega::runtime::FrontEndPresentationMode::RetailRequired);
}

[[nodiscard]] bool SameSimulationState(
    const omega::simulation::SimulationState& left,
    const omega::simulation::SimulationState& right) noexcept
{
    return left.completed_steps == right.completed_steps &&
           left.simulated_time == right.simulated_time &&
           left.alive_entities == right.alive_entities;
}

void CheckAnimatedRetailFrameTextureReuse()
{
    constexpr std::uint64_t logical_bytes =
        static_cast<std::uint64_t>(omega::frontend::kCanonicalRasterWidth) *
        omega::frontend::kCanonicalRasterHeight * 4U;

    auto app = CreateRetailApp(nullptr);
    Check(app.has_value(),
        "retail-required host starts for animated texture-reuse coverage");
    if (!app)
        return;

    const FrontEndScreenBundle bundle = MakeAnimatedOpacityBundle();
    const GpuHostSnapshot before = OmegaAppTestAccess::Gpu(*app);
    Check(!OmegaAppTestAccess::RetailFrontEndReady(*app) &&
              !OmegaAppTestAccess::RetailFrontEndTexture(*app).valid() &&
              OmegaAppTestAccess::RetailFrontEndDrawCommands(*app).empty(),
        "animated retail presentation starts without a published texture");

    Check(OmegaAppTestAccess::ComposeRetailScreenPresentation(*app, bundle, 0U),
        "the first animated compose reports that it published");
    const GpuHostSnapshot first = OmegaAppTestAccess::Gpu(*app);
    const auto first_texture = OmegaAppTestAccess::RetailFrontEndTexture(*app);
    const auto first_commands =
        OmegaAppTestAccess::RetailFrontEndDrawCommands(*app);
    const bool first_published =
        OmegaAppTestAccess::RetailFrontEndReady(*app) &&
        OmegaAppTestAccess::RetailScreenHasAnimation(*app) &&
        first_texture.valid() && first_commands.size() == 1U &&
        first_commands.front().texture == first_texture;
    Check(first_published,
        "the first animated compose publishes one texture and draw command");
    Check(first.successful_uploads == before.successful_uploads + 1U &&
              first.successful_upload_logical_bytes ==
                  before.successful_upload_logical_bytes + logical_bytes &&
              first.successful_updates == before.successful_updates &&
              first.successful_update_logical_bytes ==
                  before.successful_update_logical_bytes &&
              first.successful_releases == before.successful_releases &&
              first.textures.slot_capacity == before.textures.slot_capacity &&
              first.textures.free_slots + 1U == before.textures.free_slots &&
              first.textures.reserved_slots == before.textures.reserved_slots &&
              first.textures.resident_slots ==
                  before.textures.resident_slots + 1U &&
              first.textures.retired_slots == before.textures.retired_slots &&
              first.textures.reserved_logical_bytes ==
                  before.textures.reserved_logical_bytes &&
              first.textures.resident_logical_bytes ==
                  before.textures.resident_logical_bytes + logical_bytes,
        "the first animated compose performs exactly one resident upload");
    if (!first_published)
        return;

    const omega::runtime::RenderTextureBlitCommand first_command =
        first_commands.front();
    omega::runtime::RenderTextureBlitCommand sentinel_command = first_command;
    sentinel_command.filter_mode =
        first_command.filter_mode == omega::runtime::RenderTextureFilterMode::Nearest
        ? omega::runtime::RenderTextureFilterMode::Linear
        : omega::runtime::RenderTextureFilterMode::Nearest;
    auto sentinel_draw_list = omega::runtime::RenderDrawList::Create(
        std::span<const omega::runtime::RenderTextureBlitCommand>{
            &sentinel_command, 1U});
    Check(sentinel_draw_list.has_value(),
        "the distinctive retail draw-list sentinel is valid");
    if (!sentinel_draw_list)
        return;
    OmegaAppTestAccess::ReplaceRetailFrontEndDrawList(
        *app, std::move(*sentinel_draw_list));

    Check(OmegaAppTestAccess::ComposeRetailScreenPresentation(*app, bundle, 20U),
        "the second animated compose reports that it published");
    const GpuHostSnapshot second = OmegaAppTestAccess::Gpu(*app);
    const auto second_commands =
        OmegaAppTestAccess::RetailFrontEndDrawCommands(*app);
    Check(OmegaAppTestAccess::RetailFrontEndReady(*app) &&
              OmegaAppTestAccess::RetailScreenHasAnimation(*app) &&
              OmegaAppTestAccess::RetailFrontEndTexture(*app) ==
                  first_texture &&
              second_commands.size() == 1U &&
              second_commands.front() == sentinel_command,
        "the second animated compose preserves the handle and sentinel draw list");
    Check(second.successful_uploads == first.successful_uploads &&
              second.successful_upload_logical_bytes ==
                  first.successful_upload_logical_bytes &&
              second.successful_updates == first.successful_updates + 1U &&
              second.successful_update_logical_bytes ==
                  first.successful_update_logical_bytes + logical_bytes &&
              second.successful_releases == first.successful_releases &&
              second.textures == first.textures,
        "the second animated compose updates in place without upload, release, or residency churn");
}

void CheckBoundaryWithoutMovie()
{
    auto app = CreateRetailApp(nullptr);
    Check(app.has_value(), "retail-required host starts without a movie");
    if (!app)
        return;

    const GpuHostSnapshot before_gpu = OmegaAppTestAccess::Gpu(*app);
    const auto before_simulation = OmegaAppTestAccess::Simulation(*app);
    const auto rejected = app->Run(1);
    const GpuHostSnapshot after_gpu = OmegaAppTestAccess::Gpu(*app);
    Check(!rejected && rejected.error() == kUnavailableError,
        "normal mode fails closed with a fixed identity-free error");
    Check(after_gpu.frame_submissions == before_gpu.frame_submissions &&
              after_gpu.blit_submissions == before_gpu.blit_submissions &&
              after_gpu.successful_blit_draws ==
                  before_gpu.successful_blit_draws,
        "normal mode submits no project-authored front-end draw list");
    Check(SameSimulationState(
              OmegaAppTestAccess::Simulation(*app), before_simulation),
        "normal mode advances no project-authored simulation");
}

void CheckBoundaryAfterMovie()
{
    auto observation = std::make_shared<GeneratedMovieObservation>();
    auto app = CreateRetailApp(
        std::make_unique<GeneratedOpeningMovie>(observation));
    Check(app.has_value(),
        "retail-required host retains generated opening playback");
    if (!app)
        return;

    const GpuHostSnapshot before_gpu = OmegaAppTestAccess::Gpu(*app);
    const auto before_simulation = OmegaAppTestAccess::Simulation(*app);
    const auto movie_frame = app->Run(1);
    const GpuHostSnapshot after_movie = OmegaAppTestAccess::Gpu(*app);
    Check(movie_frame && movie_frame->rendered_frames == 1 &&
              movie_frame->input_frames == 1U &&
              movie_frame->executed_simulation_steps == 0U,
        "the opening-movie seam renders before retail UI is available");
    Check(after_movie.frame_submissions == before_gpu.frame_submissions + 1U &&
              after_movie.blit_submissions == before_gpu.blit_submissions + 1U &&
              after_movie.successful_blit_draws ==
                  before_gpu.successful_blit_draws + 1U,
        "the first submitted frame contains only the opening movie draw");

    const auto rejected = app->Run(1);
    const GpuHostSnapshot after_rejection = OmegaAppTestAccess::Gpu(*app);
    Check(!rejected && rejected.error() == kUnavailableError,
        "movie completion fails closed before diagnostic front-end submission");
    Check(after_rejection.frame_submissions == after_movie.frame_submissions &&
              after_rejection.blit_submissions == after_movie.blit_submissions &&
              after_rejection.successful_blit_draws ==
                  after_movie.successful_blit_draws,
        "the completion frame submits no project-authored menu pixels");
    Check(observation->advance_calls == 2U &&
              observation->destruction_count == 1U &&
              !OmegaAppTestAccess::HasOpeningMoviePlayback(*app) &&
              !OmegaAppTestAccess::HasOpeningMovieTexture(*app) &&
              OmegaAppTestAccess::BootSequence(*app).phase ==
                  omega::app::BootSequencePhase::Completed,
        "movie ownership completes and releases before the retail boundary reports unavailable");
    Check(SameSimulationState(
              OmegaAppTestAccess::Simulation(*app), before_simulation),
        "movie and rejected transition frames advance no project-authored simulation");
}

// The commit invariant end to end: navigation and the composed marker advance if
// and only if a candidate actually published.
void CheckRetailNavCommitsOnlyOnPublish()
{
    using omega::content::FrontEndScreenKey;
    using omega::frontend::presentation::RetailFrontEndNavInput;

    auto app = CreateRetailApp(nullptr);
    Check(app.has_value(), "retail-required host starts for nav-commit coverage");
    if (!app)
        return;

    // A composable Title, and a CreateAgent that loads but cannot compose.
    OmegaAppTestAccess::InstallRetailBundle(
        *app, FrontEndScreenKey::Title, MakeRoutingBundle(true));
    OmegaAppTestAccess::InstallRetailBundle(
        *app, FrontEndScreenKey::CreateAgent, MakeRoutingBundle(false));

    OmegaAppTestAccess::UpdateRetailFrontEndPresentation(*app, {});
    const bool title_live = OmegaAppTestAccess::RetailFrontEndReady(*app) &&
        OmegaAppTestAccess::RetailNav(*app).screen == FrontEndScreenKey::Title &&
        OmegaAppTestAccess::RetailComposedNav(*app).has_value() &&
        OmegaAppTestAccess::RetailComposedNav(*app)->screen == FrontEndScreenKey::Title;
    Check(title_live, "the Title publishes and becomes the composed navigation");
    if (!title_live)
        return;
    const auto title_texture = OmegaAppTestAccess::RetailFrontEndTexture(*app);
    const auto title_commands =
        OmegaAppTestAccess::RetailFrontEndDrawCommands(*app);
    Check(title_commands.size() == 1U, "the published Title owns one draw command");
    const auto title_command =
        title_commands.empty() ? omega::runtime::RenderTextureBlitCommand{}
                               : title_commands.front();

    // Accept into a screen that loads but cannot compose. Nothing may move.
    OmegaAppTestAccess::UpdateRetailFrontEndPresentation(
        *app, RetailFrontEndNavInput{.accept = true});
    const auto after_failed_commands =
        OmegaAppTestAccess::RetailFrontEndDrawCommands(*app);
    Check(OmegaAppTestAccess::RetailNav(*app).screen == FrontEndScreenKey::Title,
        "a candidate that cannot compose leaves navigation on the Title");
    Check(OmegaAppTestAccess::RetailComposedNav(*app).has_value() &&
              OmegaAppTestAccess::RetailComposedNav(*app)->screen ==
                  FrontEndScreenKey::Title,
        "a candidate that cannot compose leaves the composed marker on the Title");
    Check(OmegaAppTestAccess::RetailFrontEndReady(*app) &&
              OmegaAppTestAccess::RetailFrontEndTexture(*app) == title_texture &&
              after_failed_commands.size() == 1U &&
              after_failed_commands.front() == title_command,
        "a candidate that cannot compose keeps the Title's published frame");

    // The same Accept retries once the destination can compose. This is what the
    // removed failure memo used to make impossible.
    OmegaAppTestAccess::InstallRetailBundle(
        *app, FrontEndScreenKey::CreateAgent, MakeRoutingBundle(true));
    OmegaAppTestAccess::UpdateRetailFrontEndPresentation(
        *app, RetailFrontEndNavInput{.accept = true});
    Check(OmegaAppTestAccess::RetailNav(*app).screen ==
                  FrontEndScreenKey::CreateAgent &&
              OmegaAppTestAccess::RetailComposedNav(*app).has_value() &&
              OmegaAppTestAccess::RetailComposedNav(*app)->screen ==
                  FrontEndScreenKey::CreateAgent,
        "a later Accept retries and switches once the destination composes");

    // Back returns to the resident Title, and navigation follows only because
    // that publish succeeds.
    OmegaAppTestAccess::UpdateRetailFrontEndPresentation(
        *app, RetailFrontEndNavInput{.back = true});
    Check(OmegaAppTestAccess::RetailNav(*app).screen == FrontEndScreenKey::Title &&
              OmegaAppTestAccess::RetailComposedNav(*app).has_value() &&
              OmegaAppTestAccess::RetailComposedNav(*app)->screen ==
                  FrontEndScreenKey::Title,
        "Back republishes the Title and navigation follows it");

    // Accept+Back must not spend a load: clearing the destination and pressing
    // both leaves navigation on the Title with the destination still uncached.
    OmegaAppTestAccess::ClearRetailBundle(*app, FrontEndScreenKey::CreateAgent);
    OmegaAppTestAccess::UpdateRetailFrontEndPresentation(
        *app, RetailFrontEndNavInput{.accept = true, .back = true});
    Check(OmegaAppTestAccess::RetailNav(*app).screen == FrontEndScreenKey::Title,
        "Accept accompanied by Back does not probe or switch");
}

// The startup override is staged, not assigned. The interesting case is an
// override that LOADS but cannot be composed: an unknown or unloadable spelling
// never moves navigation anyway, whereas this one used to be committed to
// retail_nav_ before anything rendered, leaving navigation on a screen that
// never published and no path back to the Title.
void CheckStartScreenOverrideFallsBackToTitle()
{
    using omega::content::FrontEndScreenKey;

    auto app = CreateRetailApp(nullptr);
    Check(app.has_value(), "retail-required host starts for start-override coverage");
    if (!app)
        return;

    OmegaAppTestAccess::InstallRetailBundle(
        *app, FrontEndScreenKey::Title, MakeRoutingBundle(true));
    // Loadable, and deliberately uncomposable.
    OmegaAppTestAccess::InstallRetailBundle(
        *app, FrontEndScreenKey::CreateAgent, MakeRoutingBundle(false));

    OmegaAppTestAccess::BeginRetailFrontEndPresentation(
        *app, FrontEndScreenKey::CreateAgent, true);

    Check(OmegaAppTestAccess::RetailNav(*app).screen == FrontEndScreenKey::Title,
        "a loadable but uncomposable override leaves navigation on the Title");
    Check(OmegaAppTestAccess::RetailComposedNav(*app).has_value() &&
              OmegaAppTestAccess::RetailComposedNav(*app)->screen ==
                  FrontEndScreenKey::Title,
        "a refused override still composes the Title");
    Check(OmegaAppTestAccess::RetailFrontEndReady(*app) &&
              OmegaAppTestAccess::RetailFrontEndTexture(*app).valid() &&
              OmegaAppTestAccess::RetailFrontEndDrawCommands(*app).size() == 1U,
        "the Title fallback publishes a texture and one draw command");

    // A composable override IS adopted, so the fallback is not simply refusing
    // every override.
    auto adopting = CreateRetailApp(nullptr);
    Check(adopting.has_value(), "retail-required host starts for override adoption");
    if (!adopting)
        return;
    OmegaAppTestAccess::InstallRetailBundle(
        *adopting, FrontEndScreenKey::Title, MakeRoutingBundle(true));
    OmegaAppTestAccess::InstallRetailBundle(
        *adopting, FrontEndScreenKey::CreateAgent, MakeRoutingBundle(true));
    OmegaAppTestAccess::BeginRetailFrontEndPresentation(
        *adopting, FrontEndScreenKey::CreateAgent, true);
    Check(OmegaAppTestAccess::RetailNav(*adopting).screen ==
                  FrontEndScreenKey::CreateAgent &&
              OmegaAppTestAccess::RetailComposedNav(*adopting).has_value() &&
              OmegaAppTestAccess::RetailComposedNav(*adopting)->screen ==
                  FrontEndScreenKey::CreateAgent,
        "an override that publishes is adopted");
    Check(OmegaAppTestAccess::RetailFrontEndReady(*adopting),
        "an adopted override publishes its own frame");

    // No override at all still composes and adopts the Title.
    auto plain = CreateRetailApp(nullptr);
    Check(plain.has_value(), "retail-required host starts without an override");
    if (!plain)
        return;
    OmegaAppTestAccess::InstallRetailBundle(
        *plain, FrontEndScreenKey::Title, MakeRoutingBundle(true));
    OmegaAppTestAccess::BeginRetailFrontEndPresentation(*plain, std::nullopt, false);
    Check(OmegaAppTestAccess::RetailNav(*plain).screen == FrontEndScreenKey::Title &&
              OmegaAppTestAccess::RetailFrontEndReady(*plain),
        "startup without an override composes and adopts the Title");
}
} // namespace

int main()
{
    CheckAnimatedRetailFrameTextureReuse();
    CheckRetailNavCommitsOnlyOnPublish();
    CheckStartScreenOverrideFallsBackToTitle();
    CheckBoundaryWithoutMovie();
    CheckBoundaryAfterMovie();

    if (failures != 0)
    {
        std::cerr << failures << " retail-presentation smoke check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "omega_app_retail_presentation_smoke: all checks passed\n";
    return EXIT_SUCCESS;
}
