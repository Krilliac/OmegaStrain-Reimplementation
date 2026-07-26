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
#include <limits>
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
    // A moved-from capability makes a syntactically valid synthetic bundle fail
    // composition deterministically, without owner data or filesystem access.
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
        const auto outcome = app.ComposeRetailScreenPresentation(
            bundle, {}, animation_tick, has_animation);
        if (outcome ==
            frontend::presentation::RetailFrontEndPresentOutcome::Published)
        {
            app.retail_animation_tick_ = animation_tick;
            app.retail_screen_has_animation_ = has_animation;
            return true;
        }
        return false;
    }

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

    [[nodiscard]] static std::optional<
        frontend::presentation::RetailFrontEndNavState>
    RetailComposedNav(const OmegaApp& app) noexcept
    {
        return app.retail_composed_nav_;
    }

    static void InstallRetailBundle(OmegaApp& app,
        const content::FrontEndScreenKey key,
        content::FrontEndScreenBundle bundle) noexcept
    {
        if (auto* const slot = app.RetailBundleSlotForScreen(key))
            *slot = std::move(bundle);
    }

    [[nodiscard]] static std::size_t SelectableButtonCount(
        const content::FrontEndScreenBundle& bundle)
    {
        return OmegaApp::RetailScreenSelectableButtons(bundle).size();
    }

    static void ClearRetailBundle(
        OmegaApp& app, const content::FrontEndScreenKey key) noexcept
    {
        if (auto* const slot = app.RetailBundleSlotForScreen(key))
            slot->reset();
    }

    static void BeginRetailFrontEndPresentation(OmegaApp& app,
        const std::optional<content::FrontEndScreenKey> requested,
        const bool override_requested) noexcept
    {
        app.BeginRetailFrontEndPresentation(requested, override_requested);
    }

    [[nodiscard]] static bool RetailPreviewActive(
        const OmegaApp& app) noexcept
    {
        return app.RetailPreviewActive();
    }

    static void SetFrontEndMode(
        OmegaApp& app, const FrontEndMode mode) noexcept
    {
        app.front_end_state_.mode = mode;
    }

    static void LoadRetailFrontEndBundleIfEnabled(OmegaApp& app) noexcept
    {
        app.LoadRetailFrontEndBundleIfEnabled();
    }

    [[nodiscard]] static bool RetailFrontEndBundleAttempted(
        const OmegaApp& app) noexcept
    {
        return app.retail_front_end_bundle_attempted_;
    }

    static void BreakRetailTextureInvariant(OmegaApp& app) noexcept
    {
        app.retail_front_end_texture_ = runtime::RenderTextureHandle{};
    }

    static void ForceRetailTextureUpdateFailure(OmegaApp& app) noexcept
    {
        app.retail_front_end_texture_valid_ = true;
        app.retail_front_end_texture_ = runtime::RenderTextureHandle{};
    }

    static void RestoreRetailTextureInvariant(
        OmegaApp& app, const runtime::RenderTextureHandle texture) noexcept
    {
        app.retail_front_end_texture_ = texture;
    }

    [[nodiscard]] static std::expected<void,
        runtime::FrontEndPresentationGateError>
    AuthorizeFrontEnd(const OmegaApp& app) noexcept
    {
        return app.AuthorizeCurrentFrontEndPresentation();
    }

    [[nodiscard]] static std::span<const runtime::RenderTextureBlitCommand>
    CurrentFrontEndDrawCommands(const OmegaApp& app) noexcept
    {
        return app.CurrentFrontEndDrawList().commands();
    }

    [[nodiscard]] static runtime::RenderMeshDrawList
    CurrentFrontEndMeshDrawList(const OmegaApp& app) noexcept
    {
        return app.CurrentFrontEndMeshDrawList();
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

    static void ClearRetailFrontEndDrawList(OmegaApp& app) noexcept
    {
        app.retail_front_end_draw_list_ = runtime::RenderDrawList{};
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

    static void SetRetailAnimationTick(
        OmegaApp& app, const std::uint32_t tick) noexcept
    {
        app.retail_animation_tick_ = tick;
    }

    [[nodiscard]] static std::uint32_t RetailAnimationTick(
        const OmegaApp& app) noexcept
    {
        return app.retail_animation_tick_;
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

// One composable screen with a visible button routing to CreateAgent. Moving
// only the capability creates an otherwise identical uncomposable candidate.
[[nodiscard]] FrontEndScreenBundle MakeRoutingBundle(
    const bool valid_capability, const bool hidden_button_ancestor = false)
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
    if (hidden_button_ancestor)
    {
        FrontendWidgetIR hidden = MakeWidget("hidden");
        hidden.visible = false;
        hidden.children.push_back(std::move(button));
        root_widget.children.push_back(std::move(hidden));
    }
    else
    {
        root_widget.children.push_back(std::move(button));
    }
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

[[nodiscard]] std::expected<OmegaApp, std::string> CreateDiagnosticApp(
    std::unique_ptr<OpeningMoviePlayback> playback)
{
    auto config = omega::runtime::ParseConfigText("");
    if (!config)
        return std::unexpected("test config: " + config.error());
    return OmegaAppTestAccess::Create(std::move(*config), TestSettings(),
        std::move(playback),
        omega::runtime::FrontEndPresentationMode::DeveloperDiagnostics);
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

    auto app = CreateDiagnosticApp(nullptr);
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

    Check(OmegaAppTestAccess::ComposeRetailScreenPresentation(
              *app, bundle, 0U),
        "the first animated compose reports publication");
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

    Check(OmegaAppTestAccess::ComposeRetailScreenPresentation(
              *app, bundle, 20U),
        "the second animated compose reports publication");
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

void CheckRetailNavigationCommitsOnlyAfterPublication()
{
    using omega::content::FrontEndScreenKey;
    using omega::frontend::presentation::RetailFrontEndNavInput;

    const FrontEndScreenBundle hidden_route =
        MakeRoutingBundle(true, true);
    Check(OmegaAppTestAccess::SelectableButtonCount(hidden_route) == 0U,
        "a visible button below a hidden parent gets no selection ordinal");

    auto app = CreateDiagnosticApp(nullptr);
    Check(app.has_value(),
        "diagnostic host starts for transactional navigation coverage");
    if (!app)
        return;

    OmegaAppTestAccess::InstallRetailBundle(
        *app, FrontEndScreenKey::Title, MakeRoutingBundle(true));
    OmegaAppTestAccess::InstallRetailBundle(
        *app, FrontEndScreenKey::CreateAgent, MakeRoutingBundle(false));
    OmegaAppTestAccess::BeginRetailFrontEndPresentation(
        *app, std::nullopt, false);

    const bool title_live = OmegaAppTestAccess::RetailPreviewActive(*app) &&
        OmegaAppTestAccess::RetailNav(*app).screen == FrontEndScreenKey::Title &&
        OmegaAppTestAccess::RetailComposedNav(*app).has_value() &&
        *OmegaAppTestAccess::RetailComposedNav(*app) ==
            OmegaAppTestAccess::RetailNav(*app);
    Check(title_live,
        "the Title becomes active only after navigation and publication agree");
    if (!title_live)
        return;

    const auto title_texture =
        OmegaAppTestAccess::RetailFrontEndTexture(*app);
    const auto title_commands =
        OmegaAppTestAccess::RetailFrontEndDrawCommands(*app);
    Check(title_commands.size() == 1U,
        "the live Title owns one decoded-preview draw command");
    const auto title_command = title_commands.empty()
        ? omega::runtime::RenderTextureBlitCommand{}
        : title_commands.front();

    OmegaAppTestAccess::UpdateRetailFrontEndPresentation(
        *app, RetailFrontEndNavInput{.accept = true});
    const auto after_refused =
        OmegaAppTestAccess::RetailFrontEndDrawCommands(*app);
    Check(OmegaAppTestAccess::RetailNav(*app).screen ==
              FrontEndScreenKey::Title &&
          OmegaAppTestAccess::RetailComposedNav(*app).has_value() &&
          OmegaAppTestAccess::RetailComposedNav(*app)->screen ==
              FrontEndScreenKey::Title &&
          OmegaAppTestAccess::RetailFrontEndTexture(*app) == title_texture &&
          after_refused.size() == 1U &&
          after_refused.front() == title_command &&
          OmegaAppTestAccess::RetailPreviewActive(*app),
        "an uncomposable destination preserves the Title pixels and navigation");

    OmegaAppTestAccess::InstallRetailBundle(
        *app, FrontEndScreenKey::CreateAgent, MakeRoutingBundle(true));
    OmegaAppTestAccess::UpdateRetailFrontEndPresentation(
        *app, RetailFrontEndNavInput{.accept = true});
    Check(OmegaAppTestAccess::RetailNav(*app).screen ==
              FrontEndScreenKey::CreateAgent &&
          OmegaAppTestAccess::RetailComposedNav(*app).has_value() &&
          *OmegaAppTestAccess::RetailComposedNav(*app) ==
              OmegaAppTestAccess::RetailNav(*app) &&
          OmegaAppTestAccess::RetailPreviewActive(*app),
        "the same destination commits once its pixels publish");

    OmegaAppTestAccess::UpdateRetailFrontEndPresentation(
        *app, RetailFrontEndNavInput{.back = true});
    Check(OmegaAppTestAccess::RetailNav(*app).screen ==
              FrontEndScreenKey::Title &&
          OmegaAppTestAccess::RetailPreviewActive(*app),
        "Back commits only with the republished Title");

    OmegaAppTestAccess::ClearRetailBundle(
        *app, FrontEndScreenKey::CreateAgent);
    OmegaAppTestAccess::UpdateRetailFrontEndPresentation(
        *app, RetailFrontEndNavInput{.accept = true, .back = true});
    Check(OmegaAppTestAccess::RetailNav(*app).screen ==
              FrontEndScreenKey::Title,
        "Back priority prevents an Accept bundle probe");
}

void CheckStartScreenOverrideIsTransactional()
{
    using omega::content::FrontEndScreenKey;

    // Each app owns process-global SDL lifetime. End one complete app scope
    // before constructing the next scenario so SDL_Quit cannot invalidate a
    // still-live peer's audio stream during teardown.
    {
        auto refused = CreateDiagnosticApp(nullptr);
        Check(refused.has_value(),
            "diagnostic host starts for refused startup override coverage");
        if (!refused)
            return;
        OmegaAppTestAccess::InstallRetailBundle(
            *refused, FrontEndScreenKey::Title, MakeRoutingBundle(true));
        OmegaAppTestAccess::InstallRetailBundle(
            *refused, FrontEndScreenKey::CreateAgent, MakeRoutingBundle(false));
        OmegaAppTestAccess::BeginRetailFrontEndPresentation(
            *refused, FrontEndScreenKey::CreateAgent, true);
        Check(OmegaAppTestAccess::RetailNav(*refused).screen ==
                  FrontEndScreenKey::Title &&
              OmegaAppTestAccess::RetailComposedNav(*refused).has_value() &&
              OmegaAppTestAccess::RetailComposedNav(*refused)->screen ==
                  FrontEndScreenKey::Title &&
              OmegaAppTestAccess::RetailPreviewActive(*refused),
            "an uncomposable startup override falls back to a published Title");
    }

    {
        auto adopted = CreateDiagnosticApp(nullptr);
        Check(adopted.has_value(),
            "diagnostic host starts for accepted startup override coverage");
        if (!adopted)
            return;
        OmegaAppTestAccess::InstallRetailBundle(
            *adopted, FrontEndScreenKey::Title, MakeRoutingBundle(true));
        OmegaAppTestAccess::InstallRetailBundle(
            *adopted, FrontEndScreenKey::CreateAgent, MakeRoutingBundle(true));
        OmegaAppTestAccess::BeginRetailFrontEndPresentation(
            *adopted, FrontEndScreenKey::CreateAgent, true);
        Check(OmegaAppTestAccess::RetailNav(*adopted).screen ==
                  FrontEndScreenKey::CreateAgent &&
              OmegaAppTestAccess::RetailPreviewActive(*adopted),
            "a startup override commits when its frame publishes");
    }

    {
        auto unpublishable = CreateDiagnosticApp(nullptr);
        Check(unpublishable.has_value(),
            "diagnostic host starts for unpublishable startup coverage");
        if (!unpublishable)
            return;
        OmegaAppTestAccess::InstallRetailBundle(
            *unpublishable, FrontEndScreenKey::Title, MakeRoutingBundle(true));
        OmegaAppTestAccess::InstallRetailBundle(
            *unpublishable, FrontEndScreenKey::CreateAgent,
            MakeRoutingBundle(true));
        OmegaAppTestAccess::ForceRetailTextureUpdateFailure(*unpublishable);
        OmegaAppTestAccess::BeginRetailFrontEndPresentation(
            *unpublishable, FrontEndScreenKey::CreateAgent, true);
        Check(OmegaAppTestAccess::RetailNav(*unpublishable).screen ==
                  FrontEndScreenKey::Title &&
              !OmegaAppTestAccess::RetailComposedNav(*unpublishable).has_value() &&
              !OmegaAppTestAccess::RetailPreviewActive(*unpublishable) &&
              OmegaAppTestAccess::CurrentFrontEndDrawCommands(*unpublishable).data() !=
                  OmegaAppTestAccess::RetailFrontEndDrawCommands(*unpublishable).data(),
            "an unpublishable override leaves Title navigation on project developer UI");
    }

    {
        auto unknown = CreateDiagnosticApp(nullptr);
        Check(unknown.has_value(),
            "diagnostic host starts for unknown startup override coverage");
        if (!unknown)
            return;
        OmegaAppTestAccess::InstallRetailBundle(
            *unknown, FrontEndScreenKey::Title, MakeRoutingBundle(true));
        OmegaAppTestAccess::BeginRetailFrontEndPresentation(
            *unknown, std::nullopt, true);
        Check(OmegaAppTestAccess::RetailNav(*unknown).screen ==
                  FrontEndScreenKey::Title &&
              OmegaAppTestAccess::RetailPreviewActive(*unknown),
            "an unknown startup override falls back to the Title without guessing");
    }

    {
        auto saturated = CreateDiagnosticApp(nullptr);
        Check(saturated.has_value(),
            "diagnostic host starts for saturated preview tick coverage");
        if (!saturated)
            return;
        OmegaAppTestAccess::InstallRetailBundle(
            *saturated, FrontEndScreenKey::Title, MakeRoutingBundle(true));
        OmegaAppTestAccess::SetRetailAnimationTick(
            *saturated, std::numeric_limits<std::uint32_t>::max());
        OmegaAppTestAccess::BeginRetailFrontEndPresentation(
            *saturated, std::nullopt, false);
        Check(OmegaAppTestAccess::RetailPreviewActive(*saturated) &&
                  OmegaAppTestAccess::RetailAnimationTick(*saturated) ==
                      std::numeric_limits<std::uint32_t>::max(),
            "a saturated preview tick does not wrap during startup publication");
    }
}

void CheckPreviewProvenanceAndActivationBoundary()
{
    using omega::content::FrontEndScreenKey;
    using omega::runtime::FrontEndPresentationGateErrorCode;

    {
        auto retail = CreateRetailApp(nullptr);
        Check(retail.has_value(),
            "retail-required host starts for preview provenance coverage");
        if (!retail)
            return;
        OmegaAppTestAccess::InstallRetailBundle(
            *retail, FrontEndScreenKey::Title, MakeRoutingBundle(true));
        OmegaAppTestAccess::BeginRetailFrontEndPresentation(
            *retail, std::nullopt, false);
        const auto retail_gate = OmegaAppTestAccess::AuthorizeFrontEnd(*retail);
        Check(!retail_gate &&
                  retail_gate.error().code ==
                      FrontEndPresentationGateErrorCode::PresentationUnavailable &&
                  !OmegaAppTestAccess::RetailFrontEndReady(*retail) &&
                  !OmegaAppTestAccess::RetailFrontEndTexture(*retail).valid() &&
                  OmegaAppTestAccess::RetailFrontEndDrawCommands(*retail).empty() &&
                  !OmegaAppTestAccess::RetailPreviewActive(*retail) &&
                  !OmegaAppTestAccess::CurrentFrontEndDrawCommands(*retail).empty(),
            "RetailRequired helper calls cannot load, compose, or publish preview pixels");
    }

    {
        auto developer = CreateDiagnosticApp(nullptr);
        Check(developer.has_value(),
            "diagnostic host starts for preview activation coverage");
        if (!developer)
            return;
        OmegaAppTestAccess::InstallRetailBundle(
            *developer, FrontEndScreenKey::Title, MakeRoutingBundle(true));
        OmegaAppTestAccess::BeginRetailFrontEndPresentation(
            *developer, std::nullopt, false);
        const auto preview_commands =
            OmegaAppTestAccess::RetailFrontEndDrawCommands(*developer);
        Check(OmegaAppTestAccess::AuthorizeFrontEnd(*developer).has_value() &&
                  OmegaAppTestAccess::RetailPreviewActive(*developer) &&
                  OmegaAppTestAccess::CurrentFrontEndDrawCommands(*developer).data() ==
                      preview_commands.data() &&
                  OmegaAppTestAccess::CurrentFrontEndMeshDrawList(*developer).empty(),
            "developer preview is authorized on project provenance and owns both draw lanes");

        OmegaAppTestAccess::SetFrontEndMode(
            *developer, omega::app::FrontEndMode::DiagnosticPlay);
        OmegaAppTestAccess::LoadRetailFrontEndBundleIfEnabled(*developer);
        Check(!OmegaAppTestAccess::RetailPreviewActive(*developer) &&
                  !OmegaAppTestAccess::RetailFrontEndBundleAttempted(*developer) &&
                  OmegaAppTestAccess::CurrentFrontEndDrawCommands(*developer).data() !=
                      preview_commands.data(),
            "DiagnosticPlay owns presentation and defers optional discovery even "
            "when a coherent retail preview is resident");
        OmegaAppTestAccess::SetFrontEndMode(
            *developer, omega::app::FrontEndMode::Title);
        Check(OmegaAppTestAccess::RetailPreviewActive(*developer) &&
                  OmegaAppTestAccess::CurrentFrontEndDrawCommands(*developer).data() ==
                      preview_commands.data(),
            "leaving DiagnosticPlay allows the unchanged resident preview to reactivate at Title");

        const auto published_texture =
            OmegaAppTestAccess::RetailFrontEndTexture(*developer);
        OmegaAppTestAccess::BreakRetailTextureInvariant(*developer);
        Check(!OmegaAppTestAccess::RetailPreviewActive(*developer) &&
                  OmegaAppTestAccess::CurrentFrontEndDrawCommands(*developer).data() !=
                      preview_commands.data(),
            "an invalid preview texture restores the project developer UI");
        OmegaAppTestAccess::RestoreRetailTextureInvariant(
            *developer, published_texture);

        auto restored_draw_list =
            omega::runtime::RenderDrawList::Create(preview_commands);
        Check(restored_draw_list.has_value(),
            "the published preview draw-list fixture can be restored");
        OmegaAppTestAccess::ClearRetailFrontEndDrawList(*developer);
        Check(!OmegaAppTestAccess::RetailPreviewActive(*developer),
            "an empty preview draw list cannot suppress the project developer UI");
        if (restored_draw_list)
            OmegaAppTestAccess::ReplaceRetailFrontEndDrawList(
                *developer, std::move(*restored_draw_list));
        Check(OmegaAppTestAccess::RetailPreviewActive(*developer),
            "restoring the complete publication invariant reactivates the preview");
    }

    {
        auto failed = CreateDiagnosticApp(nullptr);
        Check(failed.has_value(),
            "diagnostic host starts for failed preview coverage");
        if (!failed)
            return;
        OmegaAppTestAccess::InstallRetailBundle(
            *failed, FrontEndScreenKey::Title, MakeRoutingBundle(false));
        OmegaAppTestAccess::BeginRetailFrontEndPresentation(
            *failed, std::nullopt, false);
        Check(OmegaAppTestAccess::AuthorizeFrontEnd(*failed).has_value() &&
                  !OmegaAppTestAccess::RetailPreviewActive(*failed) &&
                  OmegaAppTestAccess::CurrentFrontEndDrawCommands(*failed).data() !=
                      OmegaAppTestAccess::RetailFrontEndDrawCommands(*failed).data(),
            "a failed preview stays on the authorized project developer UI");
    }
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
} // namespace

int main()
{
    CheckAnimatedRetailFrameTextureReuse();
    CheckRetailNavigationCommitsOnlyAfterPublication();
    CheckStartScreenOverrideIsTransactional();
    CheckPreviewProvenanceAndActivationBoundary();
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
