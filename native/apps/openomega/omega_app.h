#pragma once

#include "boot_sequence.h"
#include "front_end.h"
#include "native_persistence.h"
#include "opening_movie_audio_clock.h"
#include "opening_movie_audio_fault.h"
#include "run_capture.h"
#include "sdl_audio_service.h"
#include "sdl_gpu_host.h"
#include "sdl_input_service.h"
#include "sdl_platform_service.h"

#include "omega/content/front_end_screen_bundle.h"
#include "omega/frontend_presentation/retail_front_end_nav.h"
#include "omega/gameplay/diagnostic_mission_lifecycle.h"
#include "omega/gameplay/diagnostic_proximity_trigger.h"
#include "omega/gameplay/diagnostic_target_fire.h"
#include "omega/runtime/asset_service.h"
#include "omega/runtime/config_service.h"
#include "omega/runtime/content_startup.h"
#include "omega/runtime/frame_scheduler.h"
#include "omega/runtime/front_end_presentation_gate.h"
#include "omega/runtime/input_tracker.h"
#include "omega/runtime/job_service.h"
#include "omega/runtime/log_service.h"
#include "omega/runtime/render_draw_list.h"
#include "omega/runtime/render_mesh_draw_list.h"
#include "omega/runtime/render_texture.h"
#include "omega/runtime/runtime_settings.h"
#include "omega/runtime/scene_transform.h"
#include "omega/simulation/simulation_world.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace omega::app
{
class OpeningMoviePlayback;

namespace detail
{
struct OmegaAppTestAccess;
}

enum class OmegaAppStartMode : std::uint8_t
{
    CurrentFrontEnd = 0U,
    ProjectDiagnosticPlay = 1U,
};

// Non-hot-reloadable native composition root. It is the sole owner of service lifetimes; SDL and
// service dependencies receive only non-owning references whose lifetime is guaranteed here.
class OmegaApp final
{
public:
    // [game/main thread, startup] Creates services in dependency order and the SDL/GPU host last.
    [[nodiscard]] static std::expected<OmegaApp, std::string> Create(
        runtime::ConfigStore config, const runtime::RuntimeSettings& settings,
        runtime::ContentStartupState content, NativePersistence native_persistence,
        bool debug_device, runtime::FrontEndPresentationMode presentation_mode,
        std::optional<std::filesystem::path> opening_movie_path = std::nullopt);
    [[nodiscard]] static std::expected<OmegaApp, std::string> Create(
        runtime::ConfigStore config, const runtime::RuntimeSettings& settings,
        runtime::ContentStartupState content, NativePersistence native_persistence,
        bool debug_device, runtime::FrontEndPresentationMode presentation_mode,
        asset::OpeningMovieSource opening_movie_source);

    // [game/main thread, after all worker clients have stopped]
    ~OmegaApp() noexcept;
    OmegaApp(OmegaApp&&) noexcept;
    OmegaApp& operator=(OmegaApp&&) noexcept = delete;
    OmegaApp(const OmegaApp&) = delete;
    OmegaApp& operator=(const OmegaApp&) = delete;

    // [game/main/render thread] screenshot_frame, when positive, writes one
    // headless BMP of the frame at that 1-based rendered index to the platform
    // screenshots directory, using the same capture path as the interactive
    // screenshot key. A non-positive value disables headless capture. start_mode
    // is an explicit, launch-local request; the default preserves the current
    // front-end and performs no startup transition.
    [[nodiscard]] std::expected<RunResult, std::string> Run(
        int frame_limit, int screenshot_frame = -1,
        OmegaAppStartMode start_mode = OmegaAppStartMode::CurrentFrontEnd);

    // [game/main/render thread] Runs one finite diagnostic capture. Pre-loop validation and
    // backing-allocation failures return unexpected without mutating app services; operational
    // failures after loop entry are owned by the returned outcome and are not rolled back.
    [[nodiscard]] std::expected<RunCaptureOutcome, std::string> RunWithCapture(
        int frame_limit);
    [[nodiscard]] std::string_view driver_name() const noexcept;
    [[nodiscard]] std::string_view audio_driver_name() const noexcept;
    [[nodiscard]] int audio_sample_rate() const noexcept;
    [[nodiscard]] int audio_channel_count() const noexcept;
    // [game/main thread; no concurrent use] Owned session value selected from
    // the bounded front-end model. Null means no explicit selection has occurred.
    [[nodiscard]] std::optional<profiles::ProfileId> active_profile_id() const noexcept;
    [[nodiscard]] std::optional<profiles::CharacterId> active_character_id() const noexcept;
    // [game/main thread; no concurrent use] Returns an owned snapshot of the launch-local
    // project diagnostic target/fire reducer state for exact capture/replay validation.
    [[nodiscard]] gameplay::DiagnosticTargetFireState
    diagnostic_target_fire_state() const noexcept;
    // [game/main thread; no concurrent use] Owned launch-local diagnostic
    // mission snapshots used only for exact native capture/replay validation.
    [[nodiscard]] gameplay::DiagnosticMissionLifecycleState
    diagnostic_mission_lifecycle_state() const noexcept;
    [[nodiscard]] gameplay::DiagnosticProximityTriggerState
    diagnostic_proximity_trigger_state() const noexcept;
    [[nodiscard]] std::optional<simulation::Position3>
    diagnostic_actor_position() const noexcept;
    [[nodiscard]] FrontEndState front_end_state() const noexcept;

private:
    friend struct detail::OmegaAppTestAccess;

    static constexpr std::uint32_t kQuitAction = 1U;

    // Test-only seams for exercising renderer-pool policy and generated playback. Production
    // Create always supplies the default pool configuration and accepts exactly one explicit path
    // or one already-owned archive member source.
    [[nodiscard]] static std::expected<OmegaApp, std::string> CreateWithTextureConfig(
        runtime::ConfigStore config, const runtime::RuntimeSettings& settings,
        runtime::ContentStartupState content,
        std::unique_ptr<NativePersistence> native_persistence, bool debug_device,
        runtime::RenderTexturePoolConfig texture_config,
        std::optional<std::filesystem::path> opening_movie_path = std::nullopt,
        runtime::FrontEndPresentationMode presentation_mode =
            runtime::FrontEndPresentationMode::DeveloperDiagnostics);
    [[nodiscard]] static std::expected<OmegaApp, std::string>
    CreateWithTextureConfigAndOpeningMoviePlayback(
        runtime::ConfigStore config, const runtime::RuntimeSettings& settings,
        runtime::ContentStartupState content,
        std::unique_ptr<NativePersistence> native_persistence, bool debug_device,
        runtime::RenderTexturePoolConfig texture_config,
        std::optional<std::filesystem::path> opening_movie_path,
        std::optional<asset::OpeningMovieSource> opening_movie_source,
        std::unique_ptr<OpeningMoviePlayback> opening_movie_playback,
        runtime::FrontEndPresentationMode presentation_mode =
            runtime::FrontEndPresentationMode::DeveloperDiagnostics);

    struct RunLoopResult
    {
        RunResult result;
        std::optional<std::string> operational_error;
        std::optional<runtime::RunCaptureSessionError> capture_error;
    };

    enum class FrontEndCommandPreparation : std::uint8_t
    {
        PersistenceBacked = 0U,
        ProjectDiagnosticLaunch = 1U,
    };

    [[nodiscard]] RunLoopResult RunLoop(
        int frame_limit, runtime::RunCaptureSession* capture_session,
        std::optional<std::chrono::nanoseconds> first_elapsed_override,
        int screenshot_frame, OmegaAppStartMode start_mode);
    // [game/main thread, before frame input] Transactionally prepares the one
    // explicit project diagnostic launch. It publishes neither mode nor launch
    // authorization until typed command preparation succeeds.
    [[nodiscard]] std::expected<void, std::string> PrepareRunStart(
        OmegaAppStartMode start_mode);
    // [game/main thread; no concurrent use] Consumes the launch-only gameplay
    // authorization as soon as a transition leaves DiagnosticPlay.
    void ConsumeProjectDiagnosticLaunchIfInactive() noexcept;
    [[nodiscard]] bool ContainOpeningMovieAudio() noexcept;
    [[nodiscard]] static OpeningMovieAudioFaultCounters OpeningMovieAudioFaultCountersOf(
        const AudioServiceSnapshot& snapshot) noexcept;
    // [game/main thread] Completes the front-end transition after attempting
    // audio containment. This is intentionally not noexcept: logging and GPU
    // release are ordinary operational calls and must never terminate the process.
    [[nodiscard]] bool FinishOpeningMovieFrontEndTransition(
        AudioServiceSnapshot& audio_fault_baseline);
    // [game/main thread] Releases CPU/GPU movie state after audio has already
    // been contained or the run has become fatal.
    void ReleaseOpeningMovieForFrontEnd();
    // [game/main thread; no concurrent use] Applies a bounded menu command before
    // its projected reducer state is published. Profile creation, explicit
    // active-profile confirmation, and persistence-backed diagnostic-start
    // preparation may touch persistence; the explicit project diagnostic launch
    // context does not. None mutates GPU state. A failed command leaves the prior
    // front-end state and session activation unpublished.
    [[nodiscard]] std::expected<void, std::string> ApplyFrontEndCommand(
        FrontEndCommand command,
        FrontEndCommandPreparation preparation =
            FrontEndCommandPreparation::PersistenceBacked);
    [[nodiscard]] std::expected<void, std::string> DeployDiagnosticMission();
    [[nodiscard]] std::expected<void, std::string> CreateFirstProfile();
    [[nodiscard]] std::expected<void, std::string> CreateFirstCharacter();
    // [game/main thread; no concurrent use] Explicit capability inputs for the
    // bounded front-end reducer. No allocation, I/O, or persistence work occurs.
    [[nodiscard]] FrontEndCapabilities CurrentFrontEndCapabilities() const noexcept;
    // [game/main thread; no concurrent use] True when the confirmation
    // ConfirmActiveProfile published still resolves against the current bounded
    // model. This is the only diagnostic-play authorization input; the app holds
    // no second selection value that could satisfy the gate on its own. No
    // allocation, I/O, or persistence work occurs.
    [[nodiscard]] bool ActiveProfileIsConfirmed() const noexcept;
    [[nodiscard]] bool ActiveCharacterIsConfirmed() const noexcept;
    // [game/main thread; no concurrent use] DeveloperDiagnostics authorizes the
    // project-authored UI and the explicitly enabled decoded-data preview on the
    // same developer capability: both are arranged by PROJECT policy.
    // RetailRequired remains unconditionally unavailable until the presentation
    // rules themselves, not merely their decoded inputs, are evidence-backed.
    [[nodiscard]] std::expected<void,
        runtime::FrontEndPresentationGateError>
    AuthorizeCurrentFrontEndPresentation() const noexcept;
    // [any thread; reentrant] The decoded-data compositor is an explicit
    // DeveloperDiagnostics-only preview. Its DFS ordering, text interleave,
    // submission depth, and one-tick-per-frame cadence are PROJECT experimental
    // policy, not observed retail presentation behavior.
    [[nodiscard]] bool IsRetailPreviewMode() const noexcept
    {
        return presentation_mode_ ==
            runtime::FrontEndPresentationMode::DeveloperDiagnostics;
    }
    // [any thread; reentrant] True only when the preview is coherent and ready,
    // and the current state permits it to replace the project UI. Bundle
    // presence alone is intentionally insufficient: navigation, pixels,
    // texture, and draw list must describe the same successfully published frame.
    [[nodiscard]] bool RetailPreviewActive() const noexcept
    {
        return IsRetailPreviewMode() &&
               front_end_state_.mode != FrontEndMode::DiagnosticPlay &&
               retail_front_end_ready_ &&
               retail_front_end_texture_valid_ &&
               retail_front_end_texture_.valid() &&
               !retail_front_end_draw_list_.empty() &&
               retail_composed_nav_.has_value() &&
               *retail_composed_nav_ == retail_nav_;
    }
    // [game/main thread; no concurrent use] One-time developer-preview startup.
    // OPENOMEGA_ENABLE_RETAIL_FRONT_END is an explicit per-run opt-in; it never
    // changes RetailRequired authorization. Never throws.
    void LoadRetailFrontEndBundleIfEnabled() noexcept;
    // [game/main thread; no concurrent use] Startup-only transactional adoption:
    // load, compose, and publish first; commit navigation/tick/animation only
    // after the complete operation succeeds.
    [[nodiscard]] bool TryAdoptRetailScreen(
        content::FrontEndScreenKey screen) noexcept;
    // [game/main thread; no concurrent use] Stages an optional resolved startup
    // override. Unknown, unloadable, uncomposable, and unpublishable requests all
    // warn without identity and fall back through the same Title transaction.
    void BeginRetailFrontEndPresentation(
        std::optional<content::FrontEndScreenKey> requested,
        bool override_requested) noexcept;
    // [game/main thread; no concurrent use] Applies the experimental PROJECT
    // layout policy, then publishes the result. The animation output is written
    // only on Published so callers can keep all candidate state local.
    [[nodiscard]] frontend::presentation::RetailFrontEndPresentOutcome
    ComposeRetailScreenPresentation(
        const content::FrontEndScreenBundle& bundle,
        std::string_view selected_identifier, std::uint32_t animation_tick,
        bool& out_has_animation) noexcept;
    // [game/main thread; no concurrent use] Publishes fixed-size compositor
    // pixels. The first call creates the texture/draw list; later calls update
    // the resident texture without replacing either resource. True means the
    // candidate pixels reached the live presentation.
    [[nodiscard]] bool PublishRetailFrontEndFrame(
        runtime::Rgba8TextureUploadView upload) noexcept;
    // [game/main thread] Cached-only lookup used by the per-frame path.
    [[nodiscard]] const content::FrontEndScreenBundle* CachedRetailBundleForScreen(
        content::FrontEndScreenKey screen) noexcept;
    // [game/main thread] Returns a cached bundle or performs one load attempt.
    // Only successes are cached, so a later explicit Accept retries a failure.
    [[nodiscard]] const content::FrontEndScreenBundle* LoadRetailBundleForScreen(
        content::FrontEndScreenKey screen) noexcept;
    [[nodiscard]] std::optional<content::FrontEndScreenBundle>*
    RetailBundleSlotForScreen(content::FrontEndScreenKey screen) noexcept;
    // One selectable retail menu button: its widget identifier and the screen its
    // Accept routes to (empty for buttons with no target yet, e.g. Options).
    struct RetailFrontEndButton
    {
        std::string identifier;
        std::optional<content::FrontEndScreenKey> target;
    };
    // [game/main thread] Ordered (DFS-preorder) visible Button widgets of a screen.
    [[nodiscard]] static std::vector<RetailFrontEndButton>
    RetailScreenSelectableButtons(const content::FrontEndScreenBundle& bundle);
    // [game/main thread; no concurrent use] Phase 3: steps the retail navigation
    // from resolved input edges (select move / accept-switch-screen / back) and
    // recomposes the retail draw list when the navigation state changes. Never
    // throws.
    void UpdateRetailFrontEndPresentation(
        const frontend::presentation::RetailFrontEndNavInput& input) noexcept;
    // [game/main/render thread; no concurrent use] Atomically rebuilds fixed CPU
    // texture fallback/overlay commands and, when a scene is resident, the
    // environment-plus-actor mesh commands for the final post-step position.
    // Target/fire cues use the current normalized host pointer sample, with a
    // deterministic center fallback. All texture and mesh resources remain
    // immutable and resident.
    [[nodiscard]] std::expected<void, std::string>
    RefreshDiagnosticActorDrawList(
        const std::optional<runtime::PointerPositionQ16>& pointer_position);
    [[nodiscard]] const runtime::RenderDrawList& CurrentFrontEndDrawList() const noexcept;
    [[nodiscard]] runtime::RenderMeshDrawList CurrentFrontEndMeshDrawList() const noexcept;

    using ProfileActiveDrawListMatrix =
        std::array<std::array<runtime::RenderDrawList, kFrontEndVisibleProfiles>,
            kFrontEndVisibleProfiles>;

    struct FrontEndPresentation
    {
        [[no_unique_address]]
        runtime::DeveloperDiagnosticFrontEndPresentationCapability provenance =
            runtime::DeveloperDiagnosticFrontEndPresentationCapability::
                CreateForExplicitDeveloperMode();
        runtime::RenderTextureHandle main_texture;
        runtime::RenderTextureHandle profiles_texture;
        std::array<runtime::RenderDrawList, kFrontEndMainRowCount> main_draw_lists;
        runtime::RenderDrawList profiles_draw_list;
        std::array<runtime::RenderDrawList, kFrontEndVisibleProfiles>
            profile_selection_draw_lists;
        // [selected position][confirmed active position]. Each list is the
        // selection list for its row plus the project-owned active-row cue, so
        // the cue is chosen by the position the confirmed identifier resolves to
        // and never by a separately stored second identity.
        // Startup-owned immutable presentation data. Indirection keeps the
        // complete selected-by-active matrix out of OmegaApp value/expected
        // stack storage; construction and all allocation finish before Run.
        std::unique_ptr<ProfileActiveDrawListMatrix> profile_active_draw_lists;
    };

    struct CharacterPresentation
    {
        runtime::RenderTextureHandle texture;
        runtime::RenderDrawList draw_list;
        std::array<runtime::RenderDrawList, kFrontEndVisibleCharacters>
            selection_draw_lists;
    };

    struct DiagnosticScenePresentation
    {
        std::array<runtime::RenderMeshHandle,
            runtime::kMaximumRenderMeshDrawsPerFrame> mesh_handles{};
        std::size_t mesh_count = 0U;
        std::size_t environment_command_count = 0U;
        runtime::RenderMeshHandle actor_mesh_handle;
        asset::SceneCameraIR camera;
        runtime::RenderMeshDrawList environment_draw_list;
        runtime::RenderMeshDrawList draw_list;
        runtime::RenderDrawList overlay_draw_list;
    };

    // [game/main/render thread, startup] Validates the complete scene-to-command projection before
    // publishing any app-owned presentation. Every successfully uploaded prefix is released if a
    // later upload or draw-list validation fails.
    [[nodiscard]] static std::expected<std::unique_ptr<DiagnosticScenePresentation>,
        std::string>
    BuildDiagnosticScenePresentation(SdlGpuHost& host, const asset::SceneIR& scene);
    // [game/main/render thread; no concurrent use] Clears commands before releasing exact resident
    // generations in reverse upload order. The host remains the final cleanup authority.
    void ReleaseDiagnosticScenePresentation() noexcept;

    // [game/main/render thread; no concurrent use] Builds a complete immutable
    // character card. The returned texture remains host-owned and must be
    // released through ReleaseCharacterPresentation.
    [[nodiscard]] std::expected<CharacterPresentation, std::string>
    BuildCharacterPresentation(const FrontEndCharacterStartupModel& model,
                               FrontEndCapabilities capabilities);
    void ReleaseCharacterPresentation(
        std::optional<CharacterPresentation>& presentation) noexcept;

    OmegaApp(std::unique_ptr<NativePersistence> native_persistence,
        std::unique_ptr<runtime::ConfigStore> config,
        std::unique_ptr<runtime::ContentStartupState> content,
        std::unique_ptr<runtime::StderrLogSink> stderr_sink,
        std::unique_ptr<runtime::RingLogSink> ring_sink,
        std::unique_ptr<runtime::LogService> log,
        std::unique_ptr<runtime::JobService> jobs,
        std::unique_ptr<runtime::AssetService> assets,
        std::unique_ptr<runtime::FrameScheduler> frame_scheduler,
        std::unique_ptr<runtime::InputTracker> input,
        std::unique_ptr<simulation::SimulationWorld> simulation,
        simulation::EntityId debug_locomotion_entity,
        std::unique_ptr<SdlPlatformService> platform,
        std::unique_ptr<SdlInputService> sdl_input,
        std::unique_ptr<SdlAudioService> audio,
        std::unique_ptr<SdlGpuHost> host,
        std::unique_ptr<OpeningMoviePlayback> opening_movie_player,
        runtime::RenderTextureHandle opening_movie_texture,
        runtime::RenderDrawList opening_movie_draw_list,
        BootSequenceState boot_sequence_state,
        runtime::RenderTextureHandle diagnostic_texture,
        runtime::RenderTextureHandle diagnostic_actor_marker_texture,
        runtime::RenderDrawList diagnostic_actor_draw_list,
        std::unique_ptr<DiagnosticScenePresentation> diagnostic_scene_presentation,
        FrontEndPresentation front_end_presentation,
        std::optional<FrontEndPresentation> first_profile_presentation,
        runtime::RenderTextureHandle diagnostic_controls_texture,
        runtime::RenderTextureHandle diagnostic_asset_topology_texture,
        runtime::RenderTextureHandle diagnostic_asset_transfer_texture,
        runtime::RenderDrawList diagnostic_hidden_draw_list,
        runtime::RenderDrawList diagnostic_controls_draw_list,
        runtime::RenderDrawList diagnostic_asset_topology_draw_list,
        runtime::ContentStartupStage content_stage,
        FrontEndStartupModel front_end_startup_model,
        runtime::FrontEndPresentationMode presentation_mode) noexcept;

    // Declaration order is ownership order; destruction is the required reverse order.
    std::unique_ptr<NativePersistence> native_persistence_;
    std::unique_ptr<runtime::ConfigStore> config_;
    std::unique_ptr<runtime::ContentStartupState> content_;
    std::unique_ptr<runtime::StderrLogSink> stderr_sink_;
    std::unique_ptr<runtime::RingLogSink> ring_sink_;
    std::unique_ptr<runtime::LogService> log_;
    std::unique_ptr<runtime::JobService> jobs_;
    std::unique_ptr<runtime::AssetService> assets_;
    std::unique_ptr<runtime::FrameScheduler> frame_scheduler_;
    std::unique_ptr<runtime::InputTracker> input_;
    std::unique_ptr<simulation::SimulationWorld> simulation_;
    // Non-owning world-scoped identity. SimulationWorld owns the positioned component.
    simulation::EntityId debug_locomotion_entity_{};
    std::unique_ptr<SdlPlatformService> platform_;
    std::unique_ptr<SdlInputService> sdl_input_;
    std::unique_ptr<SdlAudioService> audio_;
    std::unique_ptr<SdlGpuHost> host_;
    // The synchronous playback source is destroyed before the host. Production playback retains
    // no source path; the stable texture handle remains backend-owned.
    std::unique_ptr<OpeningMoviePlayback> opening_movie_player_;
    runtime::RenderTextureHandle opening_movie_texture_;
    runtime::RenderDrawList opening_movie_draw_list_;
    BootSequenceState boot_sequence_state_{};
    // Launch-time SDL events are swallowed until one movie frame has reached
    // the host successfully. A later fresh keyboard/mouse press (or the
    // opt-in gamepad primary action) may then complete the modal boot sequence.
    bool opening_movie_skip_armed_ = false;
    // Fixed producer scratch for one complete native audio-ring refill. It owns only decoded PCM
    // and is never accessed by the SDL callback.
    std::array<std::int16_t,
        static_cast<std::size_t>(SdlAudioService::kOpeningMovieQueueCapacityFrames) *
            SdlAudioService::kChannelCount>
        opening_movie_audio_scratch_{};
    OpeningMovieAudioClockState opening_movie_audio_clock_{};
    // Non-owning generation-scoped identity. The host remains the backend-resource owner and a
    // default-moved-from app cannot release this copied value because its host_ is null.
    runtime::RenderTextureHandle diagnostic_texture_;
    runtime::RenderTextureHandle diagnostic_actor_marker_texture_;
    // Allocation-free post-step presentation value. It retains the immutable
    // base command followed by the actor marker, one objective-or-target marker,
    // and contextual cues while DiagnosticPlay is active.
    runtime::RenderDrawList diagnostic_actor_draw_list_;
    // Heap-owned presentation storage. Indirection keeps the complete fixed mesh and overlay
    // command backing out of OmegaApp value/expected stack storage; per-frame overlay replacement
    // remains allocation-free.
    std::unique_ptr<DiagnosticScenePresentation> diagnostic_scene_presentation_;
    FrontEndPresentation front_end_presentation_;
    // When present, this retains the inactive half of the one-time empty -> first
    // profile presentation swap so both texture pairs remain explicitly releasable.
    std::optional<FrontEndPresentation> first_profile_presentation_;
    runtime::RenderTextureHandle diagnostic_controls_texture_;
    runtime::RenderTextureHandle diagnostic_asset_topology_texture_;
    runtime::RenderTextureHandle diagnostic_asset_transfer_texture_;
    // Immutable non-owning draw data, retained independently from the explicit release handles.
    runtime::RenderDrawList diagnostic_hidden_draw_list_;
    runtime::RenderDrawList diagnostic_controls_draw_list_;
    runtime::RenderDrawList diagnostic_asset_topology_draw_list_;
    // Bounded front-end model and immutable content classification. The profile
    // model begins as the startup snapshot and may perform the one supported
    // zero-to-one transition after a durable first-profile create.
    runtime::ContentStartupStage content_stage_ = runtime::ContentStartupStage::NoContent;
    // Normal launch is fail-closed until a retail-data presentation producer is
    // integrated. Only the explicit developer CLI route authorizes the
    // project-authored presentation owned below.
    runtime::FrontEndPresentationMode presentation_mode_ =
        runtime::FrontEndPresentationMode::RetailRequired;
    // Experimental DeveloperDiagnostics decoded-data preview. Holding a bundle
    // authorizes nothing; RetailRequired is always fail-closed because these
    // decoded assets are arranged by PROJECT presentation policy.
    std::optional<content::FrontEndScreenBundle> retail_front_end_bundle_;
    bool retail_front_end_bundle_attempted_ = false;
    // Published preview presentation. CurrentFrontEndDrawList selects it only
    // through RetailPreviewActive(), never through a partial ready/bundle state.
    runtime::RenderDrawList retail_front_end_draw_list_;
    bool retail_front_end_ready_ = false;
    // The first successful compose creates the resident texture; subsequent
    // candidate frames update it in place. One animation tick per rendered frame
    // is PROJECT preview cadence, not an observed retail timing claim.
    runtime::RenderTextureHandle retail_front_end_texture_;
    bool retail_front_end_texture_valid_ = false;
    std::uint32_t retail_animation_tick_ = 0U;
    bool retail_screen_has_animation_ = false;
    // Three-screen preview navigation. Navigation and composed navigation remain
    // equal whenever the preview is active; both commit only after publication.
    frontend::presentation::RetailFrontEndNavState retail_nav_{};
    std::optional<frontend::presentation::RetailFrontEndNavState> retail_composed_nav_{};
    std::optional<content::FrontEndScreenBundle> retail_create_agent_bundle_;
    std::optional<content::FrontEndScreenBundle> retail_load_agent_bundle_;
    FrontEndStartupModel front_end_startup_model_{};
    FrontEndCharacterStartupModel front_end_character_startup_model_{};
    std::optional<CharacterPresentation> character_presentation_;
    std::optional<CharacterPresentation> first_character_presentation_;
    // Project-owned app-layer state. It has no renderer, database, or retail-data lifetime.
    FrontEndState front_end_state_;
    // Capability is consumed only after the durable create succeeds. Presence of
    // the alternate presentation alone cannot express this because it then owns
    // the old empty presentation until teardown.
    bool can_create_first_profile_ = false;
    bool can_create_first_character_ = false;
    // Explicit confirmation-gate enablement. Production always composes a
    // persistence owner and enables it. A private composition without
    // persistence has no authorization source and uses the explicit synthetic,
    // persistence-free diagnostic-start path instead.
    bool requires_active_profile_for_diagnostic_play_ = false;
    bool requires_active_character_for_diagnostic_play_ = false;
    // A successful explicit launch consumes its one-shot preparation forever.
    // The active authorization is observable only while DiagnosticPlay remains
    // published and is cleared on the same frame that returns to any menu.
    bool project_diagnostic_launch_consumed_ = false;
    bool project_diagnostic_play_active_ = false;
    // Explicit per-launch activation only. The corresponding confirmation is
    // persisted before this value is published, but startup never copies a durable
    // confirmation into session state or selects a profile implicitly. This value
    // is the app's single active-profile identity: the diagnostic-play gate and
    // the active-row cue are both derived from it against the current model.
    std::optional<profiles::ProfileId> active_profile_id_;
    std::optional<profiles::CharacterId> active_character_id_;
    // Launch-local project diagnostic state. It survives front-end round trips, is never written
    // to native persistence, and latches only from successful DiagnosticPlay simulation steps.
    gameplay::DiagnosticProximityTriggerState diagnostic_proximity_trigger_state_{};
    // One project-owned target/fire reducer state. Completion is sampled once per host input frame,
    // commits only after every fixed step succeeds, survives front-end round trips, and has no GPU
    // or persistence lifetime.
    gameplay::DiagnosticTargetFireState diagnostic_target_fire_state_{};
    // Project-owned launch-local mission loop. It reports only the synthetic
    // diagnostic deploy/complete/abort policy and is never persisted.
    gameplay::DiagnosticMissionLifecycleState
        diagnostic_mission_lifecycle_state_{};
    // Contextual native playtest controls. Mouse/keyboard actions update these
    // only when DiagnosticPlay was already active at frame input time, so the
    // same physical controls remain menu select/back aliases without leaking a
    // deploy click into gameplay.
    bool debug_target_held_ = false;
    bool debug_fire_pressed_ = false;
    // Friend-only wall-clock seam. Production samples system_clock at the command
    // boundary; tests may provide one valid UTC millisecond value.
    std::optional<std::uint64_t> first_profile_timestamp_override_for_testing_;
    // Friend-only, game-thread, one-shot wall-time input. Production factories leave it empty,
    // and no production API can populate it. Run and RunWithCapture consume it before validation
    // so every exit path is leak-free and at most the first elapsed-bearing host frame observes it.
    std::optional<std::chrono::nanoseconds> next_run_elapsed_override_for_testing_;
};
} // namespace omega::app
