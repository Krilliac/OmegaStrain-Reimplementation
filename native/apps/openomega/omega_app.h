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
#include "omega/gameplay/character_controller.h"
#include "omega/gameplay/combat.h"
#include "omega/gameplay/npc_ai.h"
#include "omega/gameplay/mission_trigger.h"
#include "omega/gameplay/objective_tracker.h"
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
#include "omega/runtime/free_fly_camera.h"
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
#include <span>
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
    // screenshot key. A non-positive value disables headless capture.
    [[nodiscard]] std::expected<RunResult, std::string> Run(
        int frame_limit, int screenshot_frame = -1);

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

    [[nodiscard]] RunLoopResult RunLoop(
        int frame_limit, runtime::RunCaptureSession* capture_session,
        std::optional<std::chrono::nanoseconds> first_elapsed_override,
        int screenshot_frame = -1);
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
    // active-profile confirmation, and project diagnostic-start preparation may
    // touch persistence; none mutates GPU state. A failed command leaves the
    // prior front-end state and session activation unpublished.
    [[nodiscard]] std::expected<void, std::string> ApplyFrontEndCommand(
        FrontEndCommand command);
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
    // [game/main thread; no concurrent use] The current project-authored
    // presentation owns only the explicit developer capability. Normal mode
    // remains unavailable until a retail-data decoder supplies the distinct,
    // GameDataService-minted capability with its owned presentation.
    [[nodiscard]] std::expected<void,
        runtime::FrontEndPresentationGateError>
    AuthorizeCurrentFrontEndPresentation() const noexcept;
    // [game/main thread; no concurrent use] Gap A of the retail front-end
    // wiring (docs/08). One-time, on first host-loop entry: in RetailRequired
    // mode, when the OPENOMEGA_ENABLE_RETAIL_FRONT_END guard is set, loads the
    // GameDataService-minted Title screen bundle so the gate can authorize on
    // its retail capability. Guarded off by default because the retail
    // compositor (Gap B) does not yet render the bundle; until then default
    // launches stay fail-closed. Never throws: a failure just leaves the bundle
    // empty and the gate unavailable.
    void LoadRetailFrontEndBundleIfEnabled() noexcept;
    // [game/main thread; no concurrent use] Gap B retail front-end (docs/08).
    // Composites one decoded retail screen bundle into a canonical frame with the
    // named widget highlighted, uploads it, and caches a full-screen blit draw
    // list so retail mode presents real retail pixels. Never throws: a failure
    // leaves the retail presentation as-is (last good frame or project fallback).
    void ComposeRetailScreenPresentation(
        const content::FrontEndScreenBundle& bundle,
        std::string_view selected_identifier) noexcept;
    // [game/main thread] Returns the cached retail bundle for a screen, lazily
    // loading+caching it via GameDataService on first use. nullptr if unavailable.
    [[nodiscard]] const content::FrontEndScreenBundle* RetailBundleForScreen(
        content::FrontEndScreenKey screen) noexcept;
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
    // [game/main/render thread] Recomposes the project multiplayer menu overlay
    // draw list from mp_menu_state_ (uploading a fresh card and releasing the
    // prior texture). Called only while mp_menu_active_.
    void UpdateMultiplayerMenuPresentation() noexcept;
    // [game/main thread] Logs a stub session request (host/connect/join). This is
    // the seam a future transport layer implements; it performs no networking.
    void DispatchMultiplayerSessionRequest(
        const multiplayer::MpSessionRequest& request) noexcept;
    // [game/main/render thread; no concurrent use] Atomically rebuilds fixed CPU
    // texture fallback/overlay commands and, when a scene is resident, the
    // environment-plus-actor mesh commands for the final post-step position.
    // Target/fire cues use the current normalized host pointer sample, with a
    // deterministic center fallback. All texture and mesh resources remain
    // immutable and resident.
    // `fire_held` is the existing kDebugFireAction binding (SPACE / left mouse)
    // held this frame; it drives the Combat S2 player weapon.
    [[nodiscard]] std::expected<void, std::string>
    RefreshDiagnosticActorDrawList(
        const std::optional<runtime::PointerPositionQ16>& pointer_position,
        const runtime::FreeFlyInput& camera_input = {},
        bool fire_held = false);
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
        // Albedo texture bound to the environment meshes (Gap-A textured level
        // slice). A default handle leaves the environment flat-shaded.
        runtime::RenderTextureHandle environment_texture{};
        // Objective HUD panel texture blitted into overlay_draw_list over the 3D
        // level (uploaded once at scene build, like environment_texture; released
        // by the host at teardown). Default = no HUD blit.
        runtime::RenderTextureHandle hud_texture{};
        asset::SceneCameraIR camera;
        runtime::RenderMeshDrawList environment_draw_list;
        runtime::RenderMeshDrawList draw_list;
        runtime::RenderDrawList overlay_draw_list;
        // Live free-fly camera state. When free_fly_active, the per-frame actor
        // refresh advances free_fly_pose from input, rebuilds camera.world_to_view
        // from it (keeping camera.view_to_clip), and recomputes every environment
        // command's object_to_clip from environment_local_to_world so the whole
        // scene reprojects as the camera moves. free_fly_script (from
        // OPENOMEGA_CAMERA_SCRIPT) is a constant per-frame input for headless
        // motion demos; when its magnitude is zero the live held input is used.
        bool free_fly_active = false;
        runtime::FreeFlyPose free_fly_pose{};
        float free_fly_move_speed = 1.0F;
        runtime::FreeFlyInput free_fly_script{};
        std::array<asset::Matrix4x4IR,
            runtime::kMaximumRenderMeshDrawsPerFrame> environment_local_to_world{};
        // Kinematic player (OPENOMEGA_PLAYER=1). When player_active, the per-frame
        // actor refresh steps player_state against the level COL (player_collision,
        // nearby-culled), renders the actor mesh at the player's float position,
        // and drives a Z-up follow camera -- so the player walks the 3D level.
        // Takes precedence over the detached free-fly camera when both are set.
        bool player_active = false;
        gameplay::CharacterState player_state{};
        gameplay::CharacterControllerParams player_params{};
        std::vector<gameplay::CollisionTriangle> player_collision{};
        // Objective triggers (OPENOMEGA_PLAYER): the live objective state + the
        // project-placed world-space trigger volumes. When the player enters a
        // trigger, the linked objective completes and hud_texture is rebuilt.
        gameplay::ObjectiveState objective_state{};
        std::vector<gameplay::MissionTrigger> mission_triggers{};
        // When a trigger rebuilds hud_texture mid-frame, the just-built overlay
        // still references the OLD handle; it is stashed here and released at the
        // top of the next refresh, after this frame's render consumed it.
        runtime::RenderTextureHandle pending_hud_release{};
        // Enemy NPC (OPENOMEGA_NPC=1, with the player). A kinematic patrolling AI
        // that spots the player by vision cone + line-of-sight against the level
        // COL and latches to Alerted (rendered blue -> red). Placement is
        // project-placed -- authentic spawns/nodes live in the undecoded POP GOB:
        // section (a later decode slice), not the .SO.
        // One project-placed enemy. Each patrols its own route, sees the player
        // via cone+LOS vs the COL, and runs the full stealth loop (Patrol ->
        // Alerted -> Chasing -> Searching -> Patrol); rendered green/yellow/red
        // by state. Placement is project-placed pending the POP GOB: decode.
        struct NpcRuntime
        {
            gameplay::CharacterState state{};
            gameplay::CharacterControllerParams params{};
            std::vector<asset::Float3IR> waypoints{};
            std::uint32_t waypoint = 0U;
            asset::Float3IR facing{.x = 0.0F, .y = 1.0F, .z = 0.0F};
            gameplay::NpcVisionParams vision{};
            gameplay::NpcAwarenessState awareness{};
            gameplay::NpcAwarenessParams awareness_params{};
            // The sight result from the previous refresh, retained so the next
            // one can decide (via NpcHoldsPositionToAim) whether this guard
            // plants itself to aim BEFORE it plans and takes its move. The NPC
            // has not moved since that test, so this is its sight at its current
            // position; only the player has advanced one step.
            bool sees_player = false;
            // Combat S2 runtime state: the retained weapon this guard aims and
            // fires at the player with, and its hitpoints. A dead guard stops
            // planning, moving, seeing and firing, is excluded from the player's
            // hitscan, and is drawn in the dead-actor colour.
            gameplay::WeaponState weapon{};
            gameplay::HealthState health{};
        };
        bool npc_active = false;
        std::vector<NpcRuntime> npcs{};
        // Combat S2, player side: the player's hitpoints and its retained weapon
        // (no aim ramp -- the human aims), plus the weapon parameters BOTH sides
        // fire with for now. Every number here is the combat.h WeaponParams
        // PROJECT default; authentic stats await the WEAPONS.WDB decode. Respawn,
        // a health HUD, and the mission fail/complete flow are a later slice --
        // at zero hitpoints the player is simply dead.
        gameplay::HealthState player_health{};
        gameplay::WeaponState player_weapon{};
        gameplay::WeaponParams weapon_params{};
    };

    // [game/main/render thread, startup] Validates the complete scene-to-command projection before
    // publishing any app-owned presentation. Every successfully uploaded prefix is released if a
    // later upload or draw-list validation fails.
    [[nodiscard]] static std::expected<std::unique_ptr<DiagnosticScenePresentation>,
        std::string>
    BuildDiagnosticScenePresentation(SdlGpuHost& host, const asset::SceneIR& scene,
        const content::LevelTextureStore* level_texture_store,
        const content::GameDataService* game_data,
        // Real decoded per-vertex UVs for scene.render_meshes.front(), or empty when the scene
        // carries none. The spatial scene builder emits exactly one render mesh whose vertices are
        // appended in terrain-cell order, so this stays index-parallel with that mesh's positions.
        std::span<const std::array<float, 2>> scene_uvs = {},
        std::optional<runtime::FreeFlyPose> free_fly_pose = std::nullopt,
        float free_fly_move_speed = 1.0F,
        runtime::FreeFlyInput free_fly_script = {},
        std::optional<gameplay::CharacterState> player_seed = std::nullopt,
        gameplay::CharacterControllerParams player_params = {},
        std::vector<gameplay::CollisionTriangle> player_collision = {});
    // [game/main/render thread; no concurrent use] Clears commands before releasing exact resident
    // generations in reverse upload order. The host remains the final cleanup authority.
    void ReleaseDiagnosticScenePresentation() noexcept;

    // [game/main/render thread; no concurrent use] Takes ownership of one complete
    // content-startup state and builds every level-derived structure from it: the
    // diagnostic scene IR (the COL collision shell or the decoded VUM visual
    // geometry), the free-fly/player camera seed, the uploaded scene meshes and
    // environment texture, the mission triggers, and the NPC runtimes. Any
    // presentation and level-scoped launch-local state owned from a previous
    // adoption is released first through ReleaseDiagnosticScenePresentation, so no
    // GPU generation is ever owned twice or leaked. This is the single entry point
    // through which level content enters the app; Create calls it exactly once and
    // nothing else calls it yet, so the app has no runtime level change.
    //
    // objective_hud_texture and scene_overlay_draw_list are the app-level overlay
    // resources the caller already built for this content; they are adopted into
    // the rebuilt presentation. On failure the app owns the new content and no
    // scene presentation at all, never a half-built one: BuildDiagnosticScenePresentation
    // releases its own uploaded prefix, and the caller decides whether to retry or
    // tear the app down.
    [[nodiscard]] std::expected<void, std::string> AdoptLevelContent(
        std::unique_ptr<runtime::ContentStartupState> content,
        runtime::RenderTextureHandle objective_hud_texture = {},
        runtime::RenderDrawList scene_overlay_draw_list = {});

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
    // Experimental retail front-end presentation source (Gap A, docs/08).
    // Populated at most once by LoadRetailFrontEndBundleIfEnabled(); empty on
    // default launches so the retail gate stays fail-closed until the compositor
    // (Gap B) can render it. std::optional default-constructs to empty, so no
    // constructor/move changes are required (the move constructor is defaulted).
    std::optional<content::FrontEndScreenBundle> retail_front_end_bundle_;
    bool retail_front_end_bundle_attempted_ = false;
    // Gap B Phase 1: cached retail Title presentation. The blit command inside
    // the draw list borrows a texture handle owned by host_'s pool for host_'s
    // lifetime. When not ready, CurrentFrontEndDrawList uses the project path.
    runtime::RenderDrawList retail_front_end_draw_list_;
    bool retail_front_end_ready_ = false;
    // Gap B Phase 3b: the composited retail frame's uploaded texture, owned here so
    // each recompose (nav change or animation tick) can release the prior handle
    // before adopting the new one -- otherwise per-frame animated recompose would
    // grow the host texture pool without bound. Advances one animation tick per
    // rendered frame; recomposes every frame only while the current screen has
    // animation tracks (else it composes once and holds).
    runtime::RenderTextureHandle retail_front_end_texture_;
    bool retail_front_end_texture_valid_ = false;
    std::uint32_t retail_animation_tick_ = 0U;
    bool retail_screen_has_animation_ = false;
    // Gap B Phase 3: retail front-end navigation. The nav state (current screen +
    // selected button) is stepped each frame from resolved input; the draw list is
    // recomposed only when it changes. CreateAgent/LoadAgent bundles are lazily
    // loaded+cached on first navigation to them. This is intentionally separate
    // from the project ReduceFrontEnd flow so the retail path never fires the
    // project's persistence commands.
    frontend::presentation::RetailFrontEndNavState retail_nav_{};
    std::optional<frontend::presentation::RetailFrontEndNavState> retail_composed_nav_{};
    std::optional<content::FrontEndScreenBundle> retail_create_agent_bundle_;
    std::optional<content::FrontEndScreenBundle> retail_load_agent_bundle_;
    std::optional<content::FrontEndScreenBundle> retail_command_center_bundle_;
    std::optional<content::FrontEndScreenBundle> retail_equipment_bundle_;
    FrontEndStartupModel front_end_startup_model_{};
    FrontEndCharacterStartupModel front_end_character_startup_model_{};
    std::optional<CharacterPresentation> character_presentation_;
    // Project multiplayer menu overlay (dev-gated by OPENOMEGA_START_SCREEN=
    // multiplayer, DeveloperDiagnostics only). Self-contained: it steps a pure
    // MP menu reducer from the same input edges and replaces the front-end draw
    // list with a project-authored MP card. Session actions are logged stubs; no
    // networking. Recompose releases the prior texture like the retail path.
    bool mp_menu_active_ = false;
    multiplayer::MpMenuState mp_menu_state_{};
    runtime::RenderDrawList mp_menu_draw_list_;
    bool mp_menu_ready_ = false;
    runtime::RenderTextureHandle mp_menu_texture_;
    bool mp_menu_texture_valid_ = false;
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
    // Dev-only headless capture aid (OPENOMEGA_START_DIAGNOSTIC_PLAY). When set,
    // the app opens the documented synthetic, persistence-free diagnostic-start
    // path (can_start=true, no profile/character confirmation gate) and boots
    // straight into DiagnosticPlay so the loaded level's flat-3D scene renders
    // without interactive menu navigation. Only honored in DeveloperDiagnostics
    // mode with a level loaded; never affects the normal interactive flow.
    bool synthetic_diagnostic_play_start_ = false;
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
