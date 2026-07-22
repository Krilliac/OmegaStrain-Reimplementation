#include "front_end.h"
#include "diagnostic_actor_marker.h"
#include "omega_app.h"
#include "run_replay_session.h"

#include "omega/asset/level_ir.h"
#include "omega/asset/scene_ir.h"
#include "omega/content/game_data_service.h"
#include "omega/content/level_texture_store.h"
#include "omega/runtime/config_service.h"
#include "omega/runtime/content_startup.h"
#include "omega/runtime/frame_scheduler.h"
#include "omega/runtime/input_trace.h"
#include "omega/runtime/input_tracker.h"
#include "omega/runtime/runtime_settings.h"
#include "omega/runtime/scheduler_elapsed_trace.h"
#include "omega/runtime/scene_transform.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace omega::app::detail
{
struct SdlGpuHostTestAccess final
{
    [[nodiscard]] static std::expected<
        std::array<runtime::RenderClearColorRgba8, 16U>, std::string>
        ReadbackBlitsForTesting(
            SdlGpuHost& host, const runtime::RenderFramePacket& packet)
    {
        return host.ReadbackBlitsForTesting(packet);
    }
};

struct DiagnosticScenePresentationProbe final
{
    std::array<runtime::RenderMeshHandle,
        runtime::kMaximumRenderMeshDrawsPerFrame> mesh_handles{};
    std::size_t mesh_count = 0U;
    std::size_t environment_command_count = 0U;
    runtime::RenderMeshHandle actor_mesh_handle;
    asset::SceneCameraIR camera;
    runtime::RenderMeshDrawList draw_list;
};

struct OmegaAppTestAccess final
{
    [[nodiscard]] static std::expected<OmegaApp, std::string> Create(
        runtime::ConfigStore config, const runtime::RuntimeSettings& settings,
        runtime::ContentStartupState content, const bool debug_device)
    {
        return OmegaApp::CreateWithTextureConfig(std::move(config), settings,
            std::move(content), nullptr, debug_device, {});
    }

    [[nodiscard]] static std::expected<OmegaApp, std::string> CreateWithTextureConfig(
        runtime::ConfigStore config, const runtime::RuntimeSettings& settings,
        runtime::ContentStartupState content, const bool debug_device,
        const runtime::RenderTexturePoolConfig texture_config)
    {
        return OmegaApp::CreateWithTextureConfig(std::move(config), settings,
            std::move(content), nullptr, debug_device, texture_config);
    }

    [[nodiscard]] static std::expected<OmegaApp, std::string> CreateWithPersistence(
        runtime::ConfigStore config, const runtime::RuntimeSettings& settings,
        runtime::ContentStartupState content, NativePersistence persistence,
        const bool debug_device)
    {
        return OmegaApp::CreateWithTextureConfig(std::move(config), settings,
            std::move(content),
            std::make_unique<NativePersistence>(std::move(persistence)), debug_device, {});
    }

    [[nodiscard]] static std::expected<OmegaApp, std::string>
    CreateWithPersistenceAndTextureConfig(
        runtime::ConfigStore config, const runtime::RuntimeSettings& settings,
        runtime::ContentStartupState content, NativePersistence persistence,
        const bool debug_device,
        const runtime::RenderTexturePoolConfig texture_config)
    {
        return OmegaApp::CreateWithTextureConfig(std::move(config), settings,
            std::move(content),
            std::make_unique<NativePersistence>(std::move(persistence)),
            debug_device, texture_config);
    }

    [[nodiscard]] static std::expected<DiagnosticScenePresentationProbe, std::string>
    BuildDiagnosticScenePresentation(
        SdlGpuHost& host, const asset::SceneIR& scene)
    {
        auto built = OmegaApp::BuildDiagnosticScenePresentation(host, scene);
        if (!built)
            return std::unexpected(std::move(built.error()));
        const auto& presentation = **built;
        return DiagnosticScenePresentationProbe{
            .mesh_handles = presentation.mesh_handles,
            .mesh_count = presentation.mesh_count,
            .environment_command_count =
                presentation.environment_command_count,
            .actor_mesh_handle = presentation.actor_mesh_handle,
            .camera = presentation.camera,
            .draw_list = presentation.draw_list,
        };
    }

    [[nodiscard]] static std::expected<void, std::string> ApplyFrontEndCommand(
        OmegaApp& app, const FrontEndCommand command)
    {
        return app.ApplyFrontEndCommand(command);
    }

    [[nodiscard]] static bool InstallUnownedDiagnosticDraw(OmegaApp& app)
    {
        constexpr runtime::RenderTextureHandle unowned_texture{
            .pool_identity = std::numeric_limits<std::uint64_t>::max(),
            .generation = std::numeric_limits<std::uint64_t>::max(),
            .slot_index = std::numeric_limits<std::uint32_t>::max(),
        };
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
        constexpr std::array commands{
            runtime::RenderTextureBlitCommand{
                .texture = unowned_texture,
                .source = full_source,
                .destination = full_target,
                .fit_mode = runtime::RenderTextureFitMode::Contain,
                .filter_mode = runtime::RenderTextureFilterMode::Nearest,
            },
        };
        auto created = runtime::RenderDrawList::Create(commands);
        if (!created ||
            !app.front_end_presentation_.profile_active_draw_lists)
            return false;
        app.diagnostic_hidden_draw_list_ = *created;
        for (runtime::RenderDrawList& draw_list :
             app.front_end_presentation_.main_draw_lists)
            draw_list = *created;
        app.front_end_presentation_.profiles_draw_list = *created;
        for (runtime::RenderDrawList& draw_list :
             app.front_end_presentation_.profile_selection_draw_lists)
            draw_list = *created;
        // The nested selected/active matrix is the list a confirmed Profiles
        // frame actually submits, so it must be poisoned too or the fixture
        // silently leaves one reachable surface holding owned handles.
        for (auto& active_draw_lists :
             *app.front_end_presentation_.profile_active_draw_lists)
        {
            for (runtime::RenderDrawList& draw_list : active_draw_lists)
                draw_list = *created;
        }
        app.diagnostic_controls_draw_list_ = *created;
        app.diagnostic_asset_topology_draw_list_ = *created;
        return true;
    }

    static void ClearDiagnosticDraw(OmegaApp& app) noexcept
    {
        app.diagnostic_hidden_draw_list_ = {};
        for (runtime::RenderDrawList& draw_list :
             app.front_end_presentation_.main_draw_lists)
            draw_list = {};
        app.front_end_presentation_.profiles_draw_list = {};
        for (runtime::RenderDrawList& draw_list :
             app.front_end_presentation_.profile_selection_draw_lists)
            draw_list = {};
        if (app.front_end_presentation_.profile_active_draw_lists)
        {
            for (auto& active_draw_lists :
                 *app.front_end_presentation_.profile_active_draw_lists)
            {
                for (runtime::RenderDrawList& draw_list : active_draw_lists)
                    draw_list = {};
            }
        }
        app.diagnostic_controls_draw_list_ = {};
        app.diagnostic_asset_topology_draw_list_ = {};
    }

    [[nodiscard]] static bool InstallTwoCommandDiagnosticBase(
        OmegaApp& app) noexcept
    {
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
            .texture = app.diagnostic_texture_,
            .source = full_source,
            .destination = full_target,
            .fit_mode = runtime::RenderTextureFitMode::Contain,
            .filter_mode = runtime::RenderTextureFilterMode::Nearest,
        };
        const std::array commands{command, command};
        auto created = runtime::RenderDrawList::Create(commands);
        if (!created)
            return false;
        app.diagnostic_hidden_draw_list_ = *created;
        return true;
    }

    [[nodiscard]] static bool DestroyDiagnosticActor(OmegaApp& app) noexcept
    {
        return app.simulation_ != nullptr &&
               app.simulation_->DestroyEntity(app.debug_locomotion_entity_) ==
                   simulation::EntityDestroyResult::Destroyed;
    }

    [[nodiscard]] static GpuHostSnapshot GpuSnapshot(const OmegaApp& app) noexcept
    {
        return app.host_->Snapshot();
    }

    [[nodiscard]] static std::optional<runtime::AssetServiceSnapshot> AssetSnapshot(
        const OmegaApp& app) noexcept
    {
        if (!app.assets_)
            return std::nullopt;
        return app.assets_->Snapshot();
    }

    [[nodiscard]] static std::vector<runtime::LogRecord> LogSnapshot(
        const OmegaApp& app)
    {
        return app.ring_sink_ ? app.ring_sink_->Snapshot()
                              : std::vector<runtime::LogRecord>{};
    }

    [[nodiscard]] static runtime::RenderTextureHandle DiagnosticTexture(
        const OmegaApp& app) noexcept
    {
        return app.diagnostic_texture_;
    }

    [[nodiscard]] static runtime::RenderTextureHandle
    DiagnosticActorMarkerTexture(const OmegaApp& app) noexcept
    {
        return app.diagnostic_actor_marker_texture_;
    }

    static void SetDiagnosticActorMarkerTexture(OmegaApp& app,
        const runtime::RenderTextureHandle texture) noexcept
    {
        app.diagnostic_actor_marker_texture_ = texture;
    }

    [[nodiscard]] static runtime::RenderTextureHandle FrontEndTexture(
        const OmegaApp& app) noexcept
    {
        return app.front_end_presentation_.main_texture;
    }

    [[nodiscard]] static runtime::RenderTextureHandle FrontEndProfilesTexture(
        const OmegaApp& app) noexcept
    {
        return app.front_end_presentation_.profiles_texture;
    }

    [[nodiscard]] static std::optional<
        std::array<runtime::RenderTextureHandle, 2U>>
    InactiveFrontEndTextures(const OmegaApp& app) noexcept
    {
        if (!app.first_profile_presentation_)
            return std::nullopt;
        return std::array{
            app.first_profile_presentation_->main_texture,
            app.first_profile_presentation_->profiles_texture,
        };
    }

    [[nodiscard]] static runtime::RenderTextureHandle DiagnosticControlsTexture(
        const OmegaApp& app) noexcept
    {
        return app.diagnostic_controls_texture_;
    }

    [[nodiscard]] static runtime::RenderTextureHandle DiagnosticAssetTopologyTexture(
        const OmegaApp& app) noexcept
    {
        return app.diagnostic_asset_topology_texture_;
    }

    [[nodiscard]] static runtime::RenderTextureHandle DiagnosticAssetTransferTexture(
        const OmegaApp& app) noexcept
    {
        return app.diagnostic_asset_transfer_texture_;
    }

    [[nodiscard]] static const runtime::RenderDrawList& DiagnosticHiddenDrawList(
        const OmegaApp& app) noexcept
    {
        return app.diagnostic_hidden_draw_list_;
    }

    [[nodiscard]] static const runtime::RenderDrawList& DiagnosticActorDrawList(
        const OmegaApp& app) noexcept
    {
        return app.diagnostic_actor_draw_list_;
    }

    [[nodiscard]] static const runtime::RenderDrawList&
    DiagnosticSceneOverlayDrawList(const OmegaApp& app) noexcept
    {
        static const runtime::RenderDrawList kEmptyDrawList;
        return app.diagnostic_scene_presentation_
            ? app.diagnostic_scene_presentation_->overlay_draw_list
            : kEmptyDrawList;
    }

    [[nodiscard]] static const runtime::RenderMeshDrawList&
    DiagnosticSceneMeshDrawList(const OmegaApp& app) noexcept
    {
        static const runtime::RenderMeshDrawList kEmptyDrawList;
        return app.diagnostic_scene_presentation_
            ? app.diagnostic_scene_presentation_->draw_list
            : kEmptyDrawList;
    }

    [[nodiscard]] static std::optional<runtime::RenderMeshHandle>
    DiagnosticSceneActorMeshHandle(const OmegaApp& app) noexcept
    {
        if (!app.diagnostic_scene_presentation_)
            return std::nullopt;
        return app.diagnostic_scene_presentation_->actor_mesh_handle;
    }

    [[nodiscard]] static bool SetDiagnosticSceneActorMeshHandle(
        OmegaApp& app, const runtime::RenderMeshHandle handle) noexcept
    {
        if (!app.diagnostic_scene_presentation_)
            return false;
        app.diagnostic_scene_presentation_->actor_mesh_handle = handle;
        return true;
    }

    [[nodiscard]] static std::optional<std::size_t>
    DiagnosticSceneEnvironmentCommandCount(const OmegaApp& app) noexcept
    {
        if (!app.diagnostic_scene_presentation_)
            return std::nullopt;
        return app.diagnostic_scene_presentation_->environment_command_count;
    }

    [[nodiscard]] static bool SetDiagnosticSceneEnvironmentCommandCount(
        OmegaApp& app, const std::size_t command_count) noexcept
    {
        if (!app.diagnostic_scene_presentation_)
            return false;
        app.diagnostic_scene_presentation_->environment_command_count = command_count;
        return true;
    }

    [[nodiscard]] static std::optional<asset::SceneCameraIR>
    DiagnosticSceneCamera(const OmegaApp& app) noexcept
    {
        if (!app.diagnostic_scene_presentation_)
            return std::nullopt;
        return app.diagnostic_scene_presentation_->camera;
    }

    [[nodiscard]] static bool SetDiagnosticSceneCamera(
        OmegaApp& app, const asset::SceneCameraIR& camera) noexcept
    {
        if (!app.diagnostic_scene_presentation_)
            return false;
        app.diagnostic_scene_presentation_->camera = camera;
        return true;
    }

    [[nodiscard]] static bool DuplicateDiagnosticSceneEnvironmentCommand(
        OmegaApp& app)
    {
        if (!app.diagnostic_scene_presentation_)
            return false;
        auto& presentation = *app.diagnostic_scene_presentation_;
        const auto environment_commands =
            presentation.environment_draw_list.commands();
        const auto combined_commands = presentation.draw_list.commands();
        if (environment_commands.size() != 1U || combined_commands.size() != 2U)
            return false;

        runtime::RenderMeshDrawCommand distinct_second_command =
            environment_commands.front();
        distinct_second_command.object_to_clip.row_major[3U] = 0.25F;
        distinct_second_command.color.red = 111U;
        const std::array duplicated_environment{
            environment_commands.front(),
            distinct_second_command,
        };
        const std::array duplicated_combined{
            environment_commands.front(),
            distinct_second_command,
            combined_commands.back(),
        };
        auto environment_draw_list =
            runtime::RenderMeshDrawList::Create(duplicated_environment);
        auto combined_draw_list =
            runtime::RenderMeshDrawList::Create(duplicated_combined);
        if (!environment_draw_list || !combined_draw_list)
            return false;
        presentation.environment_command_count = duplicated_environment.size();
        presentation.environment_draw_list = std::move(*environment_draw_list);
        presentation.draw_list = std::move(*combined_draw_list);
        return true;
    }

    [[nodiscard]] static runtime::RenderMeshDrawList CurrentFrontEndMeshDrawList(
        const OmegaApp& app) noexcept
    {
        return app.CurrentFrontEndMeshDrawList();
    }

    static void ReleaseDiagnosticScenePresentation(OmegaApp& app) noexcept
    {
        app.ReleaseDiagnosticScenePresentation();
    }

    [[nodiscard]] static std::expected<runtime::RenderMeshHandle, std::string>
    UploadRenderMesh(OmegaApp& app, const asset::RenderMeshIR& mesh)
    {
        return app.host_->UploadRenderMesh(mesh);
    }

    [[nodiscard]] static std::expected<void, std::string> ReleaseRenderMesh(
        OmegaApp& app, const runtime::RenderMeshHandle handle)
    {
        return app.host_->ReleaseRenderMesh(handle);
    }

    [[nodiscard]] static const std::array<runtime::RenderDrawList,
        kFrontEndMainRowCount>& FrontEndMainDrawLists(
        const OmegaApp& app) noexcept
    {
        return app.front_end_presentation_.main_draw_lists;
    }

    [[nodiscard]] static const runtime::RenderDrawList& FrontEndProfilesDrawList(
        const OmegaApp& app) noexcept
    {
        return app.front_end_presentation_.profiles_draw_list;
    }

    [[nodiscard]] static const std::array<runtime::RenderDrawList,
        kFrontEndVisibleProfiles>& FrontEndProfileSelectionDrawLists(
        const OmegaApp& app) noexcept
    {
        return app.front_end_presentation_.profile_selection_draw_lists;
    }

    // [selected position][confirmed active position]. Exposes the preloaded
    // matrix so a test can prove which exact list a confirmed Profiles frame
    // submits without reconstructing the presentation policy.
    [[nodiscard]] static const std::array<
        std::array<runtime::RenderDrawList, kFrontEndVisibleProfiles>,
        kFrontEndVisibleProfiles>&
    FrontEndProfileActiveDrawLists(const OmegaApp& app) noexcept
    {
        return *app.front_end_presentation_.profile_active_draw_lists;
    }

    [[nodiscard]] static const runtime::RenderDrawList& DiagnosticControlsDrawList(
        const OmegaApp& app) noexcept
    {
        return app.diagnostic_controls_draw_list_;
    }

    [[nodiscard]] static const runtime::RenderDrawList&
    DiagnosticAssetTopologyDrawList(const OmegaApp& app) noexcept
    {
        return app.diagnostic_asset_topology_draw_list_;
    }

    [[nodiscard]] static const runtime::RenderDrawList& CurrentFrontEndDrawList(
        const OmegaApp& app) noexcept
    {
        return app.CurrentFrontEndDrawList();
    }

    [[nodiscard]] static FrontEndState FrontEnd(
        const OmegaApp& app) noexcept
    {
        return app.front_end_state_;
    }

    static void SetFrontEndState(
        OmegaApp& app, const FrontEndState state) noexcept
    {
        app.front_end_state_ = state;
    }

    [[nodiscard]] static FrontEndStartupModel FrontEndModel(
        const OmegaApp& app) noexcept
    {
        return app.front_end_startup_model_;
    }

    [[nodiscard]] static std::optional<profiles::ProfileId> ActiveProfile(
        const OmegaApp& app) noexcept
    {
        return app.active_profile_id_;
    }

    [[nodiscard]] static std::optional<profiles::CharacterId> ActiveCharacter(
        const OmegaApp& app) noexcept
    {
        return app.active_character_id_;
    }

    [[nodiscard]] static std::optional<profiles::ProfileId>
    PersistedConfirmedProfile(const OmegaApp& app) noexcept
    {
        if (!app.native_persistence_)
            return std::nullopt;
        return app.native_persistence_->persisted_confirmed_profile_id();
    }

    [[nodiscard]] static std::optional<profiles::CharacterId>
    PersistedConfirmedCharacter(const OmegaApp& app) noexcept
    {
        if (!app.native_persistence_)
            return std::nullopt;
        return app.native_persistence_->persisted_confirmed_character_id();
    }

    [[nodiscard]] static std::optional<std::uint64_t> PersistenceGeneration(
        const OmegaApp& app) noexcept
    {
        if (!app.native_persistence_)
            return std::nullopt;
        return app.native_persistence_->database().generation();
    }

    [[nodiscard]] static std::optional<std::size_t> PersistenceRecordCount(
        const OmegaApp& app) noexcept
    {
        if (!app.native_persistence_)
            return std::nullopt;
        return app.native_persistence_->database().record_count();
    }

    [[nodiscard]] static std::optional<std::size_t> PersistenceLogicalValueBytes(
        const OmegaApp& app) noexcept
    {
        if (!app.native_persistence_)
            return std::nullopt;
        return app.native_persistence_->database().logical_value_bytes();
    }

    [[nodiscard]] static bool EraseStartupProfileId(
        OmegaApp& app, const FrontEndProfileSlot slot) noexcept
    {
        const std::size_t index = static_cast<std::size_t>(slot);
        if (index >= app.front_end_startup_model_.profiles.size())
            return false;
        app.front_end_startup_model_.profiles[index].id.reset();
        return true;
    }

    [[nodiscard]] static bool CanCreateFirstProfile(
        const OmegaApp& app) noexcept
    {
        return app.can_create_first_profile_;
    }

    [[nodiscard]] static bool ArmFirstProfileTimestamp(
        OmegaApp& app, const std::uint64_t timestamp) noexcept
    {
        if (timestamp > profiles::kProfileTimestampMaxUnixMilliseconds ||
            app.first_profile_timestamp_override_for_testing_)
        {
            return false;
        }
        app.first_profile_timestamp_override_for_testing_ = timestamp;
        return true;
    }

    [[nodiscard]] static std::optional<std::size_t> ProfileCatalogCount(
        OmegaApp& app)
    {
        if (!app.native_persistence_)
            return std::nullopt;
        auto listed = app.native_persistence_->profiles().List();
        if (!listed)
            return std::nullopt;
        return listed->size();
    }

    [[nodiscard]] static std::optional<profiles::ProfileSummary> ReadProfile(
        OmegaApp& app, const profiles::ProfileId id)
    {
        if (!app.native_persistence_)
            return std::nullopt;
        auto read = app.native_persistence_->profiles().Read(id);
        if (!read || !*read)
            return std::nullopt;
        return std::move(**read);
    }

    [[nodiscard]] static std::optional<std::size_t> CharacterCatalogCount(
        OmegaApp& app, const profiles::ProfileId profile_id)
    {
        if (!app.native_persistence_)
            return std::nullopt;
        auto listed = app.native_persistence_->characters().List(profile_id);
        if (!listed)
            return std::nullopt;
        return listed->size();
    }

    [[nodiscard]] static std::optional<profiles::CharacterSummary> ReadCharacter(
        OmegaApp& app, const profiles::ProfileId profile_id,
        const profiles::CharacterId character_id)
    {
        if (!app.native_persistence_)
            return std::nullopt;
        auto read =
            app.native_persistence_->characters().Read(profile_id, character_id);
        if (!read || !*read)
            return std::nullopt;
        return std::move(**read);
    }

    [[nodiscard]] static std::optional<simulation::Position3>
    DebugLocomotionPosition(const OmegaApp& app) noexcept
    {
        if (!app.simulation_)
            return std::nullopt;
        return app.simulation_->PositionOf(app.debug_locomotion_entity_);
    }

    [[nodiscard]] static gameplay::DiagnosticProximityTriggerState
    DiagnosticProximityTriggerState(const OmegaApp& app) noexcept
    {
        return app.diagnostic_proximity_trigger_state_;
    }

    static void SetDiagnosticProximityTriggerState(OmegaApp& app,
        const gameplay::DiagnosticProximityTriggerState state) noexcept
    {
        app.diagnostic_proximity_trigger_state_ = state;
    }

    [[nodiscard]] static gameplay::DiagnosticTargetFireState
    DiagnosticTargetFireState(const OmegaApp& app) noexcept
    {
        static_assert(noexcept(app.diagnostic_target_fire_state()));
        return app.diagnostic_target_fire_state();
    }

    static void SetDiagnosticTargetFireState(OmegaApp& app,
        const gameplay::DiagnosticTargetFireState state) noexcept
    {
        app.diagnostic_target_fire_state_ = state;
    }

    [[nodiscard]] static gameplay::DiagnosticMissionLifecycleState
    DiagnosticMissionLifecycleState(const OmegaApp& app) noexcept
    {
        static_assert(noexcept(app.diagnostic_mission_lifecycle_state()));
        return app.diagnostic_mission_lifecycle_state();
    }

    [[nodiscard]] static runtime::FrameSchedulerState SchedulerSnapshot(
        const OmegaApp& app) noexcept
    {
        return app.frame_scheduler_->Snapshot();
    }

    [[nodiscard]] static bool ArmNextRunElapsed(
        OmegaApp& app, const std::chrono::nanoseconds elapsed) noexcept
    {
        if (elapsed < std::chrono::nanoseconds::zero() ||
            app.next_run_elapsed_override_for_testing_)
        {
            return false;
        }
        app.next_run_elapsed_override_for_testing_ = elapsed;
        return true;
    }

    [[nodiscard]] static simulation::SimulationState SimulationSnapshot(
        const OmegaApp& app) noexcept
    {
        return app.simulation_->Snapshot();
    }

    [[nodiscard]] static SdlGpuHost& Host(OmegaApp& app) noexcept
    {
        return *app.host_;
    }

    [[nodiscard]] static bool HasInputBinding(const OmegaApp& app,
        const runtime::InputDevice device, const std::uint16_t code,
        const std::uint32_t action) noexcept
    {
        if (!app.input_)
            return false;
        for (const runtime::InputBinding& binding : app.input_->bindings().bindings())
        {
            if (binding.device == device && binding.code == code &&
                binding.action == action)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static std::size_t InputBindingCount(
        const OmegaApp& app) noexcept
    {
        return app.input_ ? app.input_->bindings().bindings().size() : 0U;
    }

    [[nodiscard]] static std::size_t InputActionCount(
        const OmegaApp& app) noexcept
    {
        return app.input_ ? app.input_->bindings().actions().size() : 0U;
    }

    [[nodiscard]] static constexpr std::uint32_t QuitAction() noexcept
    {
        return OmegaApp::kQuitAction;
    }

    [[nodiscard]] static std::uint64_t NextInputFrameIndex(
        const OmegaApp& app) noexcept
    {
        return app.input_ ? app.input_->next_frame_index() : 0U;
    }

    [[nodiscard]] static bool ClearPointerPosition(OmegaApp& app) noexcept
    {
        if (!app.input_)
            return false;
        app.input_->ClearPointerPosition();
        return true;
    }
};
} // namespace omega::app::detail

namespace
{
int failures = 0;

constexpr omega::runtime::PointerPositionQ16 kQuarterThreeQuarterPointer{
    .x = omega::runtime::kNormalizedInputExtent / 4U,
    .y = (omega::runtime::kNormalizedInputExtent * 3U) / 4U,
};

struct SdlWindowGeometry final
{
    SDL_WindowID id = 0U;
    int logical_width = 0;
    int logical_height = 0;
};

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] bool DrawListsEqual(const omega::runtime::RenderDrawList& left,
    const omega::runtime::RenderDrawList& right) noexcept
{
    const auto left_commands = left.commands();
    const auto right_commands = right.commands();
    if (left_commands.size() != right_commands.size())
        return false;
    for (std::size_t index = 0U; index < left_commands.size(); ++index)
    {
        if (left_commands[index] != right_commands[index])
            return false;
    }
    return true;
}

[[nodiscard]] bool MeshDrawListsEqual(
    const omega::runtime::RenderMeshDrawList& left,
    const omega::runtime::RenderMeshDrawList& right) noexcept
{
    const auto left_commands = left.commands();
    const auto right_commands = right.commands();
    if (left_commands.size() != right_commands.size())
        return false;
    for (std::size_t index = 0U; index < left_commands.size(); ++index)
    {
        if (left_commands[index] != right_commands[index])
            return false;
    }
    return true;
}

[[nodiscard]] bool DrawListArraysEqual(
    const std::array<omega::runtime::RenderDrawList,
        omega::app::kFrontEndMainRowCount>& left,
    const std::array<omega::runtime::RenderDrawList,
        omega::app::kFrontEndMainRowCount>& right) noexcept
{
    for (std::size_t index = 0U; index < left.size(); ++index)
    {
        if (!DrawListsEqual(left[index], right[index]))
            return false;
    }
    return true;
}

[[nodiscard]] bool SameTextureResidency(const omega::app::GpuHostSnapshot& left,
    const omega::app::GpuHostSnapshot& right) noexcept
{
    return left.textures == right.textures &&
           left.successful_uploads == right.successful_uploads &&
           left.successful_upload_logical_bytes ==
               right.successful_upload_logical_bytes &&
           left.successful_updates == right.successful_updates &&
           left.successful_update_logical_bytes ==
               right.successful_update_logical_bytes &&
           left.successful_releases == right.successful_releases;
}

[[nodiscard]] bool SameSimulationState(
    const omega::simulation::SimulationState& left,
    const omega::simulation::SimulationState& right) noexcept
{
    return left.completed_steps == right.completed_steps &&
           left.simulated_time == right.simulated_time &&
           left.alive_entities == right.alive_entities;
}

[[nodiscard]] constexpr bool SameProximityTriggerState(
    const omega::gameplay::DiagnosticProximityTriggerState left,
    const omega::gameplay::DiagnosticProximityTriggerState right) noexcept
{
    return left.inside == right.inside &&
           left.objective_complete == right.objective_complete;
}

[[nodiscard]] bool IsOneVisibleMenuSubmission(
    const omega::app::GpuHostSnapshot& before,
    const omega::app::GpuHostSnapshot& after) noexcept
{
    return SameTextureResidency(before, after) &&
           after.frame_submissions == before.frame_submissions + 1U &&
           after.blit_submissions == before.blit_submissions + 1U &&
           after.successful_blit_draws == before.successful_blit_draws + 3U &&
           after.clear_submissions == before.clear_submissions &&
           after.unavailable_swapchain_submissions ==
               before.unavailable_swapchain_submissions &&
           after.rejected_nondefault_texture_handles ==
               before.rejected_nondefault_texture_handles;
}

// Character cards are built transactionally when a profile is selected. The
// empty catalog needs both its current card and the preloaded first-character
// card; creation swaps the preview into service and releases the obsolete empty
// card. Keep those resource transitions explicit instead of treating them as
// ordinary menu-only frames.
[[nodiscard]] bool IsOneCharacterMenuSubmissionWithTextureDelta(
    const omega::app::GpuHostSnapshot& before,
    const omega::app::GpuHostSnapshot& after,
    const std::uint64_t uploads, const std::uint64_t releases,
    const std::uint64_t submitted_draws) noexcept
{
    constexpr std::uint64_t kCharacterCardLogicalBytes = 128ULL * 72ULL * 4ULL;
    return after.successful_uploads == before.successful_uploads + uploads &&
           after.successful_upload_logical_bytes ==
               before.successful_upload_logical_bytes +
                   uploads * kCharacterCardLogicalBytes &&
           after.successful_updates == before.successful_updates &&
           after.successful_update_logical_bytes ==
               before.successful_update_logical_bytes &&
           after.successful_releases == before.successful_releases + releases &&
           after.textures.slot_capacity == before.textures.slot_capacity &&
           after.textures.reserved_slots == before.textures.reserved_slots &&
           after.textures.retired_slots == before.textures.retired_slots &&
           after.textures.reserved_logical_bytes ==
               before.textures.reserved_logical_bytes &&
           after.textures.resident_slots + releases ==
               before.textures.resident_slots + uploads &&
           after.textures.resident_logical_bytes +
                   releases * kCharacterCardLogicalBytes ==
               before.textures.resident_logical_bytes +
                   uploads * kCharacterCardLogicalBytes &&
           after.frame_submissions == before.frame_submissions + 1U &&
           after.blit_submissions == before.blit_submissions + 1U &&
           after.successful_blit_draws ==
               before.successful_blit_draws + submitted_draws &&
           after.clear_submissions == before.clear_submissions &&
           after.unavailable_swapchain_submissions ==
               before.unavailable_swapchain_submissions &&
           after.rejected_nondefault_texture_handles ==
               before.rejected_nondefault_texture_handles;
}

[[nodiscard]] bool IsRejectedCharacterPreparationWithTextureDelta(
    const omega::app::GpuHostSnapshot& before,
    const omega::app::GpuHostSnapshot& after,
    const std::uint64_t uploads, const std::uint64_t releases) noexcept
{
    constexpr std::uint64_t kCharacterCardLogicalBytes = 128ULL * 72ULL * 4ULL;
    return after.successful_uploads == before.successful_uploads + uploads &&
           after.successful_upload_logical_bytes ==
               before.successful_upload_logical_bytes +
                   uploads * kCharacterCardLogicalBytes &&
           after.successful_updates == before.successful_updates &&
           after.successful_update_logical_bytes ==
               before.successful_update_logical_bytes &&
           after.successful_releases == before.successful_releases + releases &&
           after.textures == before.textures &&
           after.frame_submissions == before.frame_submissions &&
           after.blit_submissions == before.blit_submissions &&
           after.successful_blit_draws == before.successful_blit_draws &&
           after.clear_submissions == before.clear_submissions &&
           after.unavailable_swapchain_submissions ==
               before.unavailable_swapchain_submissions &&
           after.rejected_nondefault_texture_handles ==
               before.rejected_nondefault_texture_handles;
}

// The Profiles surface adds the project-owned active-row cue on top of its
// selection cue, so a confirmed Profiles frame submits exactly one more draw
// than the unconfirmed one and still uploads nothing.
[[nodiscard]] bool IsOneActiveProfileMenuSubmission(
    const omega::app::GpuHostSnapshot& before,
    const omega::app::GpuHostSnapshot& after) noexcept
{
    return SameTextureResidency(before, after) &&
           after.frame_submissions == before.frame_submissions + 1U &&
           after.blit_submissions == before.blit_submissions + 1U &&
           after.successful_blit_draws == before.successful_blit_draws + 4U &&
           after.clear_submissions == before.clear_submissions &&
           after.unavailable_swapchain_submissions ==
               before.unavailable_swapchain_submissions &&
           after.rejected_nondefault_texture_handles ==
               before.rejected_nondefault_texture_handles;
}

[[nodiscard]] bool IsOneDiagnosticPlaySubmission(
    const omega::app::GpuHostSnapshot& before,
    const omega::app::GpuHostSnapshot& after) noexcept
{
    return SameTextureResidency(before, after) &&
           after.frame_submissions == before.frame_submissions + 1U &&
           after.blit_submissions == before.blit_submissions + 1U &&
           after.successful_blit_draws == before.successful_blit_draws + 3U &&
           after.clear_submissions == before.clear_submissions &&
           after.unavailable_swapchain_submissions ==
               before.unavailable_swapchain_submissions &&
           after.rejected_nondefault_texture_handles ==
               before.rejected_nondefault_texture_handles;
}

[[nodiscard]] bool IsOneDiagnosticActorAndObjectiveSubmission(
    const omega::app::GpuHostSnapshot& before,
    const omega::app::GpuHostSnapshot& after) noexcept
{
    return SameTextureResidency(before, after) &&
           after.frame_submissions == before.frame_submissions + 1U &&
           after.blit_submissions == before.blit_submissions + 1U &&
           after.successful_blit_draws == before.successful_blit_draws + 2U &&
           after.clear_submissions == before.clear_submissions &&
           after.unavailable_swapchain_submissions ==
               before.unavailable_swapchain_submissions &&
           after.rejected_nondefault_texture_handles ==
               before.rejected_nondefault_texture_handles;
}

[[nodiscard]] bool IsOneModalCardSubmission(
    const omega::app::GpuHostSnapshot& before,
    const omega::app::GpuHostSnapshot& after) noexcept
{
    return SameTextureResidency(before, after) &&
           after.frame_submissions == before.frame_submissions + 1U &&
           after.blit_submissions == before.blit_submissions + 1U &&
           after.successful_blit_draws == before.successful_blit_draws + 2U &&
           after.clear_submissions == before.clear_submissions &&
           after.unavailable_swapchain_submissions ==
               before.unavailable_swapchain_submissions &&
           after.rejected_nondefault_texture_handles ==
               before.rejected_nondefault_texture_handles;
}

struct ExpectedSchedulerAdvance
{
    omega::runtime::FrameSchedulerState state;
    omega::runtime::FramePlan plan;
};

[[nodiscard]] ExpectedSchedulerAdvance AdvanceSchedulerSnapshot(
    const omega::runtime::FrameSchedulerState before,
    const std::chrono::nanoseconds elapsed) noexcept
{
    ExpectedSchedulerAdvance expected{
        .state = before,
        .plan = {},
    };
    std::chrono::nanoseconds delta = elapsed;
    if (delta < std::chrono::nanoseconds::zero())
        delta = std::chrono::nanoseconds::zero();
    if (delta > before.config.max_frame_delta)
    {
        delta = before.config.max_frame_delta;
        expected.plan.clamped_delta = true;
    }

    const std::chrono::nanoseconds accumulated = before.accumulated_remainder + delta;
    const std::int64_t step = before.config.simulation_step.count();
    const std::int64_t available = accumulated.count() / step;
    const std::int64_t budget =
        static_cast<std::int64_t>(before.config.max_steps_per_frame);
    std::int64_t planned = available;
    if (available > budget)
    {
        planned = budget;
        expected.plan.dropped_time = true;
        expected.state.total_dropped_time =
            omega::runtime::detail::SaturatingAddNanoseconds(
                before.total_dropped_time,
                std::chrono::nanoseconds{(available - budget) * step});
    }
    expected.plan.simulation_steps = static_cast<std::uint32_t>(planned);
    expected.state.accumulated_remainder =
        std::chrono::nanoseconds{accumulated.count() % step};
    expected.state.total_planned_steps += static_cast<std::uint64_t>(planned);
    expected.plan.interpolation_alpha =
        static_cast<double>(expected.state.accumulated_remainder.count()) /
        static_cast<double>(step);
    return expected;
}

void WriteFixtureU16(std::vector<std::byte>& bytes, const std::size_t offset,
    const std::uint16_t value)
{
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void WriteFixtureU32(std::vector<std::byte>& bytes, const std::size_t offset,
    const std::uint32_t value)
{
    for (unsigned shift = 0U; shift < 32U; shift += 8U)
        bytes[offset + shift / 8U] =
            static_cast<std::byte>((value >> shift) & 0xFFU);
}

[[nodiscard]] std::vector<std::byte> MakeFixtureDirectTdx(
    const std::uint16_t bits_per_pixel, const std::uint16_t header_format,
    const std::uint32_t transfer_code, const std::uint32_t bytes_per_pixel)
{
    constexpr std::uint16_t width = 16U;
    constexpr std::uint16_t height = 16U;
    constexpr std::uint32_t descriptor_bytes = 128U;
    constexpr std::uint32_t primary_base = 0x20U;
    constexpr std::uint32_t primary_start = primary_base + descriptor_bytes;
    const std::uint32_t payload_bytes = width * height * bytes_per_pixel;
    const std::uint32_t stride = primary_start + payload_bytes;

    std::vector<std::byte> bytes(64U, std::byte{0});
    WriteFixtureU16(bytes, 0x00U, 5U);
    WriteFixtureU16(bytes, 0x02U, 0U);
    WriteFixtureU16(bytes, 0x04U, width);
    WriteFixtureU16(bytes, 0x06U, height);
    WriteFixtureU16(bytes, 0x08U, bits_per_pixel);
    WriteFixtureU16(bytes, 0x0AU, header_format);
    WriteFixtureU16(bytes, 0x0CU, 1U);
    WriteFixtureU16(bytes, 0x0EU,
        static_cast<std::uint16_t>(payload_bytes / 256U));
    WriteFixtureU16(bytes, 0x22U, 1U);
    WriteFixtureU16(bytes, 0x24U, 1U);
    WriteFixtureU16(bytes, 0x26U, 0U);
    WriteFixtureU16(bytes, 0x34U, descriptor_bytes);
    WriteFixtureU16(bytes, 0x36U, 0U);
    WriteFixtureU32(bytes, 0x38U, stride);

    std::vector<std::byte> block(stride, std::byte{0});
    WriteFixtureU32(block, 0x18U, primary_base);
    WriteFixtureU32(block, 0x1CU, primary_base);
    WriteFixtureU32(block, 0x00U, 0x20U);
    constexpr std::size_t object = primary_base + 0x20U;
    WriteFixtureU32(block, object + 0x04U, transfer_code << 24U);
    WriteFixtureU32(block, object + 0x20U, width);
    WriteFixtureU32(block, object + 0x24U, height);
    WriteFixtureU32(block, object + 0x40U, payload_bytes / 4U);
    WriteFixtureU32(block, object + 0x54U, 0U);
    for (std::uint32_t index = 0U; index < payload_bytes; ++index)
    {
        block[primary_start + index] =
            static_cast<std::byte>(static_cast<std::uint8_t>(0x21U + index));
    }
    bytes.insert(bytes.end(), block.begin(), block.end());
    return bytes;
}

[[nodiscard]] std::vector<std::byte> MakeFixtureHog(
    const std::string_view member_name, const std::span<const std::byte> payload)
{
    constexpr std::size_t names_offset = 0x1CU;
    const std::size_t names_end = names_offset + member_name.size() + 1U;
    const std::size_t data_offset = (names_end + 15U) & ~std::size_t{15U};
    std::vector<std::byte> bytes(data_offset, std::byte{0});
    WriteFixtureU32(bytes, 0x00U, 0x4052673DU);
    WriteFixtureU32(bytes, 0x04U, 1U);
    WriteFixtureU32(bytes, 0x08U, 0x14U);
    WriteFixtureU32(bytes, 0x0CU, static_cast<std::uint32_t>(names_offset));
    WriteFixtureU32(bytes, 0x10U, static_cast<std::uint32_t>(data_offset));
    WriteFixtureU32(bytes, 0x14U, 0U);
    WriteFixtureU32(bytes, 0x18U, static_cast<std::uint32_t>(payload.size()));
    for (std::size_t index = 0U; index < member_name.size(); ++index)
        bytes[names_offset + index] = static_cast<std::byte>(member_name[index]);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

[[nodiscard]] bool WriteFixtureBytes(
    const std::filesystem::path& path, const std::span<const std::byte> bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    if (!bytes.empty())
    {
        output.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    return output.good();
}

[[nodiscard]] bool WriteFixtureText(
    const std::filesystem::path& path, const std::string_view text)
{
    return WriteFixtureBytes(path,
        std::span(reinterpret_cast<const std::byte*>(text.data()), text.size()));
}

enum class GeneratedTextureKind
{
    Packed24,
    Packed32,
};

class GeneratedLevelContentTree final
{
public:
    explicit GeneratedLevelContentTree(
        const GeneratedTextureKind texture_kind = GeneratedTextureKind::Packed24)
    {
        static std::atomic<std::uint64_t> next{0U};
        const auto stamp = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        root_ = std::filesystem::temp_directory_path() /
                ("omega-app-level-content-" + std::to_string(stamp) + "-" +
                    std::to_string(next.fetch_add(1U)));
        std::error_code error;
        std::filesystem::create_directories(root_ / "GAMEDATA" / "TEST", error);
        const std::vector<std::byte> texture =
            texture_kind == GeneratedTextureKind::Packed24
            ? MakeFixtureDirectTdx(24U, 0x01U, 0x01U, 3U)
            : MakeFixtureDirectTdx(32U, 0x00U, 0x00U, 4U);
        const std::vector<std::byte> hog = MakeFixtureHog("A_READY.TDX", texture);
        ready_ = !error &&
                 WriteFixtureText(root_ / "SYSTEM.CNF",
                     "BOOT2 = cdrom0:\\SCUS_972.64;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n") &&
                 WriteFixtureText(root_ / "SCUS_972.64", "synthetic placeholder") &&
                 WriteFixtureBytes(root_ / "GAMEDATA" / "TEST" / "TEX.HOG", hog);
    }

    ~GeneratedLevelContentTree()
    {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    GeneratedLevelContentTree(const GeneratedLevelContentTree&) = delete;
    GeneratedLevelContentTree& operator=(const GeneratedLevelContentTree&) = delete;

    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

private:
    std::filesystem::path root_;
    bool ready_ = false;
};

[[nodiscard]] omega::asset::SourceLocator FixtureDirectSource(
    const std::string_view game_path)
{
    return omega::asset::SourceLocator{
        .game_path = std::string(game_path), .hog_entries = {}};
}

[[nodiscard]] std::expected<omega::runtime::ContentStartupState, std::string>
BuildLevelContentStartupState(const GeneratedLevelContentTree& tree)
{
    auto opened = omega::content::GameDataService::Open({.root = tree.root()});
    if (!opened)
        return std::unexpected("synthetic level-content game-data open failed");

    omega::runtime::ContentStartupState state;
    state.game_data.emplace(std::move(*opened));
    omega::asset::LevelManifestIR manifest;
    manifest.data_hog_source = FixtureDirectSource("GAMEDATA/TEST/UNUSED.HOG");
    manifest.texture_sources = {FixtureDirectSource("GAMEDATA/TEST/TEX.HOG")};
    auto texture_store =
        omega::content::LevelTextureStore::Open(*state.game_data, manifest);
    if (!texture_store)
        return std::unexpected("synthetic level-content texture-store open failed");
    state.level_texture_store.emplace(std::move(*texture_store));
    state.level_manifest.emplace(std::move(manifest));
    state.level_content.emplace();
    state.debug_image.emplace(omega::runtime::DebugImage{
        .width = 2U,
        .height = 2U,
        .rgba8_pixels = {
            std::byte{8U}, std::byte{12U}, std::byte{24U}, std::byte{255U},
            std::byte{8U}, std::byte{12U}, std::byte{24U}, std::byte{255U},
            std::byte{8U}, std::byte{12U}, std::byte{24U}, std::byte{255U},
            std::byte{8U}, std::byte{12U}, std::byte{24U}, std::byte{255U},
        },
    });
    return state;
}

void InstallSyntheticSpatialTriangle(
    omega::runtime::ContentStartupState& state)
{
    if (!state.level_content)
        return;
    state.level_content->spatial.terrain_cells = {
        omega::asset::SpatialMeshIR{
            .vertices = {
                {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                {.x = 1.0F, .y = 0.0F, .z = 0.0F},
                {.x = 0.0F, .y = 1.0F, .z = 0.0F},
            },
            .triangles = {
                {.vertex_indices = {0U, 1U, 2U}},
            },
        },
    };
}

[[nodiscard]] omega::asset::RenderMeshIR MakePresentationTriangle(
    const float x_offset)
{
    return omega::asset::RenderMeshIR{
        .positions = {
            {.x = x_offset + 0.0F, .y = 0.0F, .z = 0.0F},
            {.x = x_offset + 1.0F, .y = 0.0F, .z = 0.0F},
            {.x = x_offset + 0.0F, .y = 1.0F, .z = 0.0F},
        },
        .triangle_indices = {0U, 1U, 2U},
    };
}

void CheckDiagnosticScenePresentationTransactions()
{
    using Access = omega::app::detail::OmegaAppTestAccess;
    constexpr omega::asset::Matrix4x4IR world_to_view{
        .row_major = {
            2.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 3.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F,
        },
    };
    constexpr omega::asset::Matrix4x4IR view_to_clip{
        .row_major = {
            1.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.5F, 0.0F, 0.0F, 1.0F,
        },
    };
    constexpr omega::asset::Matrix4x4IR local{
        .row_major = {
            1.0F, 0.0F, 0.0F, 4.0F,
            0.0F, 1.0F, 0.0F, 5.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F,
        },
    };
    constexpr omega::asset::Matrix4x4IR object_to_clip_with_local{
        .row_major = {
            2.0F, 3.0F, 0.0F, 23.0F,
            0.0F, 3.0F, 0.0F, 15.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            1.0F, 0.0F, 0.0F, 5.0F,
        },
    };
    constexpr omega::asset::Matrix4x4IR object_to_clip_without_local{
        .row_major = {
            2.0F, 3.0F, 0.0F, 0.0F,
            0.0F, 3.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            1.0F, 0.0F, 0.0F, 1.0F,
        },
    };
    omega::asset::SceneIR scene;
    scene.render_meshes = {
        MakePresentationTriangle(0.0F),
        MakePresentationTriangle(2.0F),
    };
    scene.mesh_instances = {
        omega::asset::SceneMeshInstanceIR{
            .render_mesh_index = 1U,
            .local_to_world = local,
        },
        omega::asset::SceneMeshInstanceIR{
            .render_mesh_index = 0U,
            .local_to_world = omega::asset::kIdentityMatrix4x4IR,
        },
    };
    scene.camera.world_to_view = world_to_view;
    scene.camera.view_to_clip = view_to_clip;
    const omega::asset::SceneIR original_scene = scene;

    auto created_platform = omega::app::SdlPlatformService::Create();
    Check(created_platform.has_value(),
        "the scene-presentation transaction platform initializes");
    if (!created_platform)
        return;
    auto platform = std::move(*created_platform);

    {
        auto created_host = omega::app::SdlGpuHost::Create(platform, false,
            omega::runtime::RenderTexturePoolConfig{
                .slot_capacity = 1U,
                .maximum_resident_logical_bytes = 4U,
            },
            omega::runtime::RenderMeshPoolConfig{
                .slot_capacity = 2U,
                .maximum_resident_positions = 9U,
                .maximum_resident_triangle_indices = 9U,
                .maximum_resident_logical_bytes = 144U,
            });
        Check(created_host.has_value(),
            "the actor-upload rollback host initializes");
        if (created_host)
        {
            auto host = std::move(*created_host);
            auto rejected = Access::BuildDiagnosticScenePresentation(host, scene);
            const omega::app::GpuHostSnapshot rolled_back = host.Snapshot();
            Check(!rejected && rejected.error() ==
                      "diagnostic actor mesh upload failed: render mesh reserve: slot-capacity-exceeded" &&
                      rolled_back.successful_mesh_uploads == 2U &&
                      rolled_back.successful_mesh_upload_logical_bytes == 96U &&
                      rolled_back.successful_mesh_releases == 2U &&
                      rolled_back.meshes.slot_capacity == 2U &&
                      rolled_back.meshes.free_slots == 2U &&
                      rolled_back.meshes.reserved_slots == 0U &&
                      rolled_back.meshes.resident_slots == 0U &&
                      rolled_back.meshes.resident_positions == 0U &&
                      rolled_back.meshes.resident_triangle_indices == 0U &&
                      rolled_back.meshes.resident_logical_bytes == 0U,
                "an actor upload failure releases the complete environment prefix with zero residual residency");
        }
    }

    {
        struct ActorBudgetFailureCase final
        {
            omega::runtime::RenderMeshPoolConfig config;
            std::string_view expected_error;
        };
        constexpr std::array cases{
            ActorBudgetFailureCase{
                .config = {
                    .slot_capacity = 3U,
                    .maximum_resident_positions = 8U,
                    .maximum_resident_triangle_indices = 9U,
                    .maximum_resident_logical_bytes = 144U,
                },
                .expected_error =
                    "diagnostic actor mesh upload failed: render mesh reserve: position-budget-exceeded",
            },
            ActorBudgetFailureCase{
                .config = {
                    .slot_capacity = 3U,
                    .maximum_resident_positions = 9U,
                    .maximum_resident_triangle_indices = 8U,
                    .maximum_resident_logical_bytes = 144U,
                },
                .expected_error =
                    "diagnostic actor mesh upload failed: render mesh reserve: triangle-index-budget-exceeded",
            },
            ActorBudgetFailureCase{
                .config = {
                    .slot_capacity = 3U,
                    .maximum_resident_positions = 9U,
                    .maximum_resident_triangle_indices = 9U,
                    .maximum_resident_logical_bytes = 143U,
                },
                .expected_error =
                    "diagnostic actor mesh upload failed: render mesh reserve: logical-byte-budget-exceeded",
            },
        };
        bool all_budget_failures_rolled_back = true;
        for (const ActorBudgetFailureCase& failure_case : cases)
        {
            auto created_host = omega::app::SdlGpuHost::Create(platform, false,
                omega::runtime::RenderTexturePoolConfig{
                    .slot_capacity = 1U,
                    .maximum_resident_logical_bytes = 4U,
                },
                failure_case.config);
            if (!created_host)
            {
                all_budget_failures_rolled_back = false;
                continue;
            }
            auto host = std::move(*created_host);
            auto rejected = Access::BuildDiagnosticScenePresentation(host, scene);
            const omega::app::GpuHostSnapshot rolled_back = host.Snapshot();
            all_budget_failures_rolled_back =
                all_budget_failures_rolled_back && !rejected &&
                rejected.error() == failure_case.expected_error &&
                rolled_back.successful_mesh_uploads == 2U &&
                rolled_back.successful_mesh_upload_logical_bytes == 96U &&
                rolled_back.successful_mesh_releases == 2U &&
                rolled_back.meshes.free_slots == 3U &&
                rolled_back.meshes.reserved_slots == 0U &&
                rolled_back.meshes.resident_slots == 0U &&
                rolled_back.meshes.resident_positions == 0U &&
                rolled_back.meshes.resident_triangle_indices == 0U &&
                rolled_back.meshes.resident_logical_bytes == 0U;
        }
        Check(all_budget_failures_rolled_back,
            "actor position, index, and byte budget failures release the complete environment prefix");
    }

    {
        auto created_host = omega::app::SdlGpuHost::Create(platform, false,
            omega::runtime::RenderTexturePoolConfig{
                .slot_capacity = 1U,
                .maximum_resident_logical_bytes = 4U,
            },
            omega::runtime::RenderMeshPoolConfig{
                .slot_capacity = 1U,
                .maximum_resident_positions = 3U,
                .maximum_resident_triangle_indices = 3U,
                .maximum_resident_logical_bytes = 48U,
            });
        Check(created_host.has_value(),
            "the empty-and-capacity scene host initializes");
        if (created_host)
        {
            auto host = std::move(*created_host);
            const omega::asset::SceneIR empty_scene;
            auto empty = Access::BuildDiagnosticScenePresentation(host, empty_scene);
            const omega::app::GpuHostSnapshot after_empty = host.Snapshot();

            omega::asset::SceneIR mesh_saturated_scene;
            mesh_saturated_scene.render_meshes.assign(
                omega::runtime::kMaximumRenderMeshDrawsPerFrame,
                MakePresentationTriangle(0.0F));
            mesh_saturated_scene.mesh_instances.emplace_back();
            auto mesh_saturated = Access::BuildDiagnosticScenePresentation(
                host, mesh_saturated_scene);
            const omega::app::GpuHostSnapshot after_mesh_saturated =
                host.Snapshot();

            omega::asset::SceneIR instance_saturated_scene;
            instance_saturated_scene.render_meshes = {
                MakePresentationTriangle(0.0F)};
            instance_saturated_scene.mesh_instances.resize(
                omega::runtime::kMaximumRenderMeshDrawsPerFrame);
            auto instance_saturated = Access::BuildDiagnosticScenePresentation(
                host, instance_saturated_scene);
            const omega::app::GpuHostSnapshot after_instance_saturated =
                host.Snapshot();
            Check(empty && empty->mesh_count == 0U &&
                      empty->environment_command_count == 0U &&
                      !empty->actor_mesh_handle.valid() &&
                      empty->draw_list.empty() &&
                      after_empty.successful_mesh_uploads == 0U &&
                      !mesh_saturated && mesh_saturated.error() ==
                          "diagnostic scene exceeds renderer command capacity" &&
                      !instance_saturated && instance_saturated.error() ==
                          "diagnostic scene exceeds renderer command capacity" &&
                      after_mesh_saturated.successful_mesh_uploads == 0U &&
                      after_mesh_saturated.successful_mesh_releases == 0U &&
                      after_mesh_saturated.meshes.free_slots == 1U &&
                      after_instance_saturated == after_mesh_saturated,
                "an empty scene keeps the texture fallback while 64 meshes and 64 instances reject independently before upload");
        }
    }

    {
        constexpr std::size_t environment_capacity =
            omega::runtime::kMaximumRenderMeshDrawsPerFrame - 1U;
        constexpr std::uint64_t total_mesh_count =
            omega::runtime::kMaximumRenderMeshDrawsPerFrame;
        constexpr std::uint64_t total_position_count = total_mesh_count * 3U;
        constexpr std::uint64_t total_index_count = total_mesh_count * 3U;
        constexpr std::uint64_t total_logical_bytes = total_mesh_count * 48U;
        auto created_host = omega::app::SdlGpuHost::Create(platform, false,
            omega::runtime::RenderTexturePoolConfig{
                .slot_capacity = 1U,
                .maximum_resident_logical_bytes = 4U,
            },
            omega::runtime::RenderMeshPoolConfig{
                .slot_capacity =
                    omega::runtime::kMaximumRenderMeshDrawsPerFrame,
                .maximum_resident_positions = total_position_count,
                .maximum_resident_triangle_indices = total_index_count,
                .maximum_resident_logical_bytes = total_logical_bytes,
            });
        Check(created_host.has_value(),
            "the exact actor-reserved scene-capacity host initializes");
        if (created_host)
        {
            auto host = std::move(*created_host);
            omega::asset::SceneIR boundary_scene;
            boundary_scene.render_meshes.reserve(environment_capacity);
            boundary_scene.mesh_instances.reserve(environment_capacity);
            for (std::size_t index = 0U; index < environment_capacity; ++index)
            {
                boundary_scene.render_meshes.push_back(
                    MakePresentationTriangle(static_cast<float>(index) * 2.0F));
                boundary_scene.mesh_instances.push_back(
                    omega::asset::SceneMeshInstanceIR{
                        .render_mesh_index = static_cast<std::uint32_t>(index),
                    });
            }

            auto built =
                Access::BuildDiagnosticScenePresentation(host, boundary_scene);
            const auto commands = built
                ? built->draw_list.commands()
                : std::span<const omega::runtime::RenderMeshDrawCommand>{};
            const omega::app::GpuHostSnapshot resident = host.Snapshot();
            bool exact_environment_prefix = built &&
                built->mesh_count == total_mesh_count &&
                built->environment_command_count == environment_capacity &&
                commands.size() == total_mesh_count;
            if (built && commands.size() == total_mesh_count)
            {
                for (std::size_t index = 0U; index < environment_capacity; ++index)
                {
                    exact_environment_prefix = exact_environment_prefix &&
                        commands[index].mesh == built->mesh_handles[index];
                }
                exact_environment_prefix = exact_environment_prefix &&
                    commands.back().mesh == built->actor_mesh_handle &&
                    built->actor_mesh_handle ==
                        built->mesh_handles[environment_capacity] &&
                    commands.back().color ==
                        omega::runtime::RenderMeshColorRgba8{
                            .red = 255U,
                            .green = 64U,
                            .blue = 224U,
                            .alpha = 255U,
                        };
            }
            Check(exact_environment_prefix &&
                      resident.successful_mesh_uploads == total_mesh_count &&
                      resident.successful_mesh_upload_logical_bytes ==
                          total_logical_bytes &&
                      resident.meshes.free_slots == 0U &&
                      resident.meshes.reserved_slots == 0U &&
                      resident.meshes.resident_slots == total_mesh_count &&
                      resident.meshes.resident_positions == total_position_count &&
                      resident.meshes.resident_triangle_indices == total_index_count &&
                      resident.meshes.resident_logical_bytes == total_logical_bytes,
                "63 environment meshes fill the exact 64-command and 64-resource boundary with the actor last");

            bool released_reverse = built.has_value();
            if (built)
            {
                built->draw_list = {};
                for (std::size_t index = built->mesh_count; index != 0U; --index)
                {
                    released_reverse = released_reverse &&
                        host.ReleaseRenderMesh(built->mesh_handles[index - 1U])
                            .has_value();
                }
            }
            const omega::app::GpuHostSnapshot released = host.Snapshot();
            Check(released_reverse &&
                      released.successful_mesh_releases == total_mesh_count &&
                      released.meshes.free_slots == total_mesh_count &&
                      released.meshes.reserved_slots == 0U &&
                      released.meshes.resident_slots == 0U &&
                      released.meshes.resident_positions == 0U &&
                      released.meshes.resident_triangle_indices == 0U &&
                      released.meshes.resident_logical_bytes == 0U,
                "the exact-capacity actor presentation can release every generation without residual residency");
        }
    }

    {
        auto created_host = omega::app::SdlGpuHost::Create(platform, false,
            omega::runtime::RenderTexturePoolConfig{
                .slot_capacity = 1U,
                .maximum_resident_logical_bytes = 4U,
            },
            omega::runtime::RenderMeshPoolConfig{
                .slot_capacity = 3U,
                .maximum_resident_positions = 9U,
                .maximum_resident_triangle_indices = 9U,
                .maximum_resident_logical_bytes = 144U,
            });
        Check(created_host.has_value(),
            "the three-slot environment-plus-actor presentation host initializes");
        if (created_host)
        {
            auto host = std::move(*created_host);
            auto built = Access::BuildDiagnosticScenePresentation(host, scene);
            const omega::app::GpuHostSnapshot resident = host.Snapshot();
            const auto commands = built
                ? built->draw_list.commands()
                : std::span<const omega::runtime::RenderMeshDrawCommand>{};
            Check(built && built->mesh_count == 3U &&
                      scene == original_scene &&
                      built->environment_command_count == 2U &&
                      commands.size() == 3U &&
                      built->mesh_handles[0].valid() &&
                      built->mesh_handles[1].valid() &&
                      built->mesh_handles[2].valid() &&
                      built->mesh_handles[0] != built->mesh_handles[1] &&
                      built->mesh_handles[1] != built->mesh_handles[2] &&
                      built->actor_mesh_handle == built->mesh_handles[2] &&
                      built->camera == scene.camera &&
                      commands[0].mesh == built->mesh_handles[1] &&
                      commands[0].object_to_clip == object_to_clip_with_local &&
                      commands[1].mesh == built->mesh_handles[0] &&
                      commands[1].object_to_clip == object_to_clip_without_local &&
                      commands[2].mesh == built->actor_mesh_handle &&
                      commands[2].object_to_clip == object_to_clip_without_local &&
                      commands[2].color == omega::runtime::RenderMeshColorRgba8{
                          .red = 255U,
                          .green = 64U,
                          .blue = 224U,
                          .alpha = 255U,
                      } &&
                      commands[2].raster_mode ==
                          omega::runtime::RenderMeshRasterMode::Fill &&
                      resident.successful_mesh_uploads == 3U &&
                      resident.successful_mesh_upload_logical_bytes == 144U &&
                      resident.successful_mesh_releases == 0U &&
                      resident.meshes.slot_capacity == 3U &&
                      resident.meshes.free_slots == 0U &&
                      resident.meshes.reserved_slots == 0U &&
                      resident.meshes.resident_slots == 3U &&
                      resident.meshes.retired_slots == 0U &&
                      resident.meshes.reserved_positions == 0U &&
                      resident.meshes.resident_positions == 9U &&
                      resident.meshes.reserved_triangle_indices == 0U &&
                      resident.meshes.resident_triangle_indices == 9U &&
                      resident.meshes.reserved_logical_bytes == 0U &&
                      resident.meshes.resident_logical_bytes == 144U,
                "two environment meshes remain byte-identical before one camera-composed magenta actor command");
            if (built)
            {
                built->draw_list = {};
                auto released_actor =
                    host.ReleaseRenderMesh(built->mesh_handles[2]);
                auto released_second =
                    host.ReleaseRenderMesh(built->mesh_handles[1]);
                auto released_first =
                    host.ReleaseRenderMesh(built->mesh_handles[0]);
                const omega::app::GpuHostSnapshot released = host.Snapshot();
                Check(released_actor && released_second && released_first &&
                           released.successful_mesh_releases == 3U &&
                           released.meshes.slot_capacity == 3U &&
                           released.meshes.free_slots == 3U &&
                          released.meshes.reserved_slots == 0U &&
                          released.meshes.resident_slots == 0U &&
                          released.meshes.retired_slots == 0U &&
                          released.meshes.reserved_positions == 0U &&
                          released.meshes.resident_positions == 0U &&
                          released.meshes.reserved_triangle_indices == 0U &&
                          released.meshes.resident_triangle_indices == 0U &&
                          released.meshes.reserved_logical_bytes == 0U &&
                          released.meshes.resident_logical_bytes == 0U,
                    "the successful presentation releases actor first and both environment generations in reverse order");
            }
        }
    }

    {
        auto created_host = omega::app::SdlGpuHost::Create(platform, false,
            omega::runtime::RenderTexturePoolConfig{
                .slot_capacity = 1U,
                .maximum_resident_logical_bytes = 4U,
            },
            omega::runtime::RenderMeshPoolConfig{
                .slot_capacity = 1U,
                .maximum_resident_positions = 6U,
                .maximum_resident_triangle_indices = 6U,
                .maximum_resident_logical_bytes = 96U,
            });
        Check(created_host.has_value(),
            "the one-slot rollback host initializes");
        if (created_host)
        {
            auto host = std::move(*created_host);
            auto rejected = Access::BuildDiagnosticScenePresentation(host, scene);
            const omega::app::GpuHostSnapshot rolled_back = host.Snapshot();
            Check(!rejected && rejected.error() ==
                      "diagnostic scene mesh upload failed: render mesh reserve: slot-capacity-exceeded" &&
                      rolled_back.successful_mesh_uploads == 1U &&
                      rolled_back.successful_mesh_upload_logical_bytes == 48U &&
                      rolled_back.successful_mesh_releases == 1U &&
                      rolled_back.meshes.slot_capacity == 1U &&
                      rolled_back.meshes.free_slots == 1U &&
                      rolled_back.meshes.reserved_slots == 0U &&
                      rolled_back.meshes.resident_slots == 0U &&
                      rolled_back.meshes.retired_slots == 0U &&
                      rolled_back.meshes.reserved_positions == 0U &&
                      rolled_back.meshes.resident_positions == 0U &&
                      rolled_back.meshes.reserved_triangle_indices == 0U &&
                      rolled_back.meshes.resident_triangle_indices == 0U &&
                      rolled_back.meshes.reserved_logical_bytes == 0U &&
                      rolled_back.meshes.resident_logical_bytes == 0U,
                "a forced second upload failure releases the exact successful prefix with zero residual residency");
        }
    }

    {
        auto created_host = omega::app::SdlGpuHost::Create(platform, false,
            omega::runtime::RenderTexturePoolConfig{
                .slot_capacity = 1U,
                .maximum_resident_logical_bytes = 4U,
            },
            omega::runtime::RenderMeshPoolConfig{
                .slot_capacity = 2U,
                .maximum_resident_positions = 6U,
                .maximum_resident_triangle_indices = 6U,
                .maximum_resident_logical_bytes = 96U,
            });
        Check(created_host.has_value(),
            "the non-finite scene-transform rejection host initializes");
        if (created_host)
        {
            auto host = std::move(*created_host);
            omega::asset::SceneIR invalid_scene = scene;
            invalid_scene.camera.view_to_clip.row_major[0] =
                std::numeric_limits<float>::infinity();
            auto rejected =
                Access::BuildDiagnosticScenePresentation(host, invalid_scene);
            const omega::app::GpuHostSnapshot unchanged = host.Snapshot();
            Check(!rejected &&
                      rejected.error() ==
                          "diagnostic scene transform is non-finite" &&
                      unchanged.successful_mesh_uploads == 0U &&
                      unchanged.successful_mesh_upload_logical_bytes == 0U &&
                      unchanged.successful_mesh_releases == 0U &&
                      unchanged.meshes.slot_capacity == 2U &&
                      unchanged.meshes.free_slots == 2U &&
                      unchanged.meshes.reserved_slots == 0U &&
                      unchanged.meshes.resident_slots == 0U,
                "a non-finite camera stage preserves the fixed app error and rejects before mesh upload");
        }
    }
}

[[nodiscard]] std::expected<omega::runtime::ContentStartupState, std::string>
BuildDataMountedStartupState(const GeneratedLevelContentTree& tree)
{
    auto opened = omega::content::GameDataService::Open({.root = tree.root()});
    if (!opened)
        return std::unexpected("synthetic data-mounted game-data open failed");
    omega::runtime::ContentStartupState state;
    state.game_data.emplace(std::move(*opened));
    return state;
}

[[nodiscard]] bool IsAggregateEmpty(
    const omega::runtime::AssetServiceSnapshot& snapshot,
    const std::size_t capacity) noexcept
{
    return snapshot.slot_capacity == capacity && snapshot.free_slots == capacity &&
           snapshot.active_slots == 0U && snapshot.retired_slots == 0U &&
           snapshot.queued == 0U && snapshot.loading == 0U && snapshot.ready == 0U &&
           snapshot.failed == 0U && snapshot.in_flight_requests == 0U &&
           snapshot.resident_logical_bytes == 0U;
}

void CheckLevelContentPresentation(omega::app::OmegaApp& app)
{
    using omega::app::detail::OmegaAppTestAccess;
    constexpr std::uint64_t kLevelContentPresentationLogicalBytes =
        2ULL * 2ULL * 4ULL + 128ULL * 72ULL * 4ULL * 3ULL +
        32ULL * 32ULL * 4ULL + 1ULL * 1ULL * 4ULL +
        16ULL * 16ULL * 4ULL;
    const auto assets = OmegaAppTestAccess::AssetSnapshot(app);
    const omega::app::GpuHostSnapshot initial_gpu =
        OmegaAppTestAccess::GpuSnapshot(app);
    Check(assets && IsAggregateEmpty(*assets, 64U),
        "LevelContent consumes and releases canonical texture zero before SDL upload");
    Check(initial_gpu.successful_uploads == 7U &&
              initial_gpu.successful_upload_logical_bytes ==
                  kLevelContentPresentationLogicalBytes &&
              initial_gpu.successful_releases == 0U &&
              initial_gpu.textures.reserved_slots == 0U &&
              initial_gpu.textures.resident_slots == 7U &&
              initial_gpu.textures.resident_logical_bytes ==
                  kLevelContentPresentationLogicalBytes,
        "the base, three cards, topology, actor marker, and strict transfer diagnostic own exactly 115,732 bytes");

    const auto topology_texture =
        OmegaAppTestAccess::DiagnosticAssetTopologyTexture(app);
    const auto transfer_texture =
        OmegaAppTestAccess::DiagnosticAssetTransferTexture(app);
    const auto topology_commands =
        OmegaAppTestAccess::DiagnosticAssetTopologyDrawList(app).commands();
    constexpr omega::runtime::RenderSourceRectQ16 full_source{
        .left = 0U,
        .top = 0U,
        .right = omega::runtime::kNormalizedRenderExtent,
        .bottom = omega::runtime::kNormalizedRenderExtent,
    };
    constexpr omega::runtime::RenderTargetRectQ16 full_target{
        .left = 0U,
        .top = 0U,
        .right = omega::runtime::kNormalizedRenderExtent,
        .bottom = omega::runtime::kNormalizedRenderExtent,
    };
    constexpr omega::runtime::RenderTargetRectQ16 topology_target{
        .left = 2048U, .top = 2048U, .right = 13824U, .bottom = 15872U};
    constexpr omega::runtime::RenderTargetRectQ16 transfer_target{
        .left = 14848U, .top = 2048U, .right = 26624U, .bottom = 15872U};
    Check(topology_texture.valid() && transfer_texture.valid() &&
              topology_commands.size() == 3U &&
              topology_commands[0].texture == OmegaAppTestAccess::DiagnosticTexture(app) &&
              topology_commands[0].source == full_source &&
              topology_commands[0].destination == full_target &&
              topology_commands[1].texture == topology_texture &&
              topology_commands[1].source == full_source &&
              topology_commands[1].destination == topology_target &&
              topology_commands[1].fit_mode ==
                  omega::runtime::RenderTextureFitMode::Contain &&
              topology_commands[1].filter_mode ==
                  omega::runtime::RenderTextureFilterMode::Nearest &&
              topology_commands[2].texture == transfer_texture &&
              topology_commands[2].source == full_source &&
              topology_commands[2].destination == transfer_target &&
              topology_commands[2].fit_mode ==
                  omega::runtime::RenderTextureFitMode::Contain &&
              topology_commands[2].filter_mode ==
                  omega::runtime::RenderTextureFilterMode::Nearest,
        "LevelContent retains topology and adds the strict Packed24 transfer as split nearest panels");

    constexpr std::array coordinates{
        std::array{0U, 0U}, std::array{1U, 1U}, std::array{4U, 4U},
        std::array{5U, 4U}, std::array{4U, 5U}, std::array{5U, 5U},
        std::array{8U, 8U}, std::array{9U, 8U}, std::array{8U, 9U},
        std::array{9U, 9U}, std::array{27U, 27U}, std::array{31U, 31U},
        std::array{15U, 15U}, std::array{30U, 30U}, std::array{0U, 16U},
        std::array{16U, 0U},
    };
    constexpr omega::runtime::RenderClearColorRgba8 background{
        .red = 8U, .green = 12U, .blue = 24U, .alpha = 255U};
    constexpr omega::runtime::RenderClearColorRgba8 slate{
        .red = 28U, .green = 38U, .blue = 58U, .alpha = 255U};
    constexpr omega::runtime::RenderClearColorRgba8 cyan{
        .red = 112U, .green = 220U, .blue = 255U, .alpha = 255U};
    constexpr std::array expected{
        slate, background, cyan, cyan,
        cyan, background, cyan, cyan,
        cyan, background, background, slate,
        background, background, slate, slate,
    };
    constexpr auto source_begin = [](const std::uint32_t coordinate) noexcept {
        return static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(coordinate) *
                    omega::runtime::kNormalizedRenderExtent +
                31U) /
            32U);
    };
    constexpr auto source_end = [](const std::uint32_t coordinate) noexcept {
        return static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(coordinate + 1U) *
            omega::runtime::kNormalizedRenderExtent / 32U);
    };
    constexpr auto destination_edge = [](const std::uint32_t coordinate) noexcept {
        return coordinate * (omega::runtime::kNormalizedRenderExtent / 4U);
    };
    std::array<omega::runtime::RenderTextureBlitCommand, 16U> probes{};
    for (std::size_t index = 0U; index < probes.size(); ++index)
    {
        const std::uint32_t column = static_cast<std::uint32_t>(index % 4U);
        const std::uint32_t row = static_cast<std::uint32_t>(index / 4U);
        probes[index] = omega::runtime::RenderTextureBlitCommand{
            .texture = topology_texture,
            .source = omega::runtime::RenderSourceRectQ16{
                .left = source_begin(coordinates[index][0]),
                .top = source_begin(coordinates[index][1]),
                .right = source_end(coordinates[index][0]),
                .bottom = source_end(coordinates[index][1]),
            },
            .destination = omega::runtime::RenderTargetRectQ16{
                .left = destination_edge(column),
                .top = destination_edge(row),
                .right = destination_edge(column + 1U),
                .bottom = destination_edge(row + 1U),
            },
            .fit_mode = omega::runtime::RenderTextureFitMode::Stretch,
            .filter_mode = omega::runtime::RenderTextureFilterMode::Nearest,
        };
    }
    auto draw_list = omega::runtime::RenderDrawList::Create(probes);
    Check(draw_list.has_value(),
        "the LevelContent sixteen one-pixel topology probes form a valid draw list");
    if (draw_list)
    {
        omega::runtime::RenderFramePacket packet{
            .clear_color = omega::runtime::kDefaultRenderClearColor,
            .draw_list = *draw_list,
        };
        auto readback =
            omega::app::detail::SdlGpuHostTestAccess::ReadbackBlitsForTesting(
                OmegaAppTestAccess::Host(app), packet);
        Check(readback && *readback == expected,
            "the real first-texture 32x32 topology preserves all sixteen frozen GPU probes");
        Check(OmegaAppTestAccess::GpuSnapshot(app) == initial_gpu,
            "the LevelContent topology readback leaves every GPU counter unchanged");
    }

    constexpr std::array transfer_coordinates{
        std::array{0U, 0U}, std::array{1U, 0U},
        std::array{2U, 0U}, std::array{3U, 0U},
        std::array{0U, 0U}, std::array{1U, 0U},
        std::array{2U, 0U}, std::array{3U, 0U},
        std::array{0U, 0U}, std::array{1U, 0U},
        std::array{2U, 0U}, std::array{3U, 0U},
        std::array{0U, 0U}, std::array{1U, 0U},
        std::array{2U, 0U}, std::array{3U, 0U},
    };
    constexpr std::array transfer_expected{
        omega::runtime::RenderClearColorRgba8{0x21U, 0x22U, 0x23U, 0xffU},
        omega::runtime::RenderClearColorRgba8{0x24U, 0x25U, 0x26U, 0xffU},
        omega::runtime::RenderClearColorRgba8{0x27U, 0x28U, 0x29U, 0xffU},
        omega::runtime::RenderClearColorRgba8{0x2aU, 0x2bU, 0x2cU, 0xffU},
        omega::runtime::RenderClearColorRgba8{0x21U, 0x22U, 0x23U, 0xffU},
        omega::runtime::RenderClearColorRgba8{0x24U, 0x25U, 0x26U, 0xffU},
        omega::runtime::RenderClearColorRgba8{0x27U, 0x28U, 0x29U, 0xffU},
        omega::runtime::RenderClearColorRgba8{0x2aU, 0x2bU, 0x2cU, 0xffU},
        omega::runtime::RenderClearColorRgba8{0x21U, 0x22U, 0x23U, 0xffU},
        omega::runtime::RenderClearColorRgba8{0x24U, 0x25U, 0x26U, 0xffU},
        omega::runtime::RenderClearColorRgba8{0x27U, 0x28U, 0x29U, 0xffU},
        omega::runtime::RenderClearColorRgba8{0x2aU, 0x2bU, 0x2cU, 0xffU},
        omega::runtime::RenderClearColorRgba8{0x21U, 0x22U, 0x23U, 0xffU},
        omega::runtime::RenderClearColorRgba8{0x24U, 0x25U, 0x26U, 0xffU},
        omega::runtime::RenderClearColorRgba8{0x27U, 0x28U, 0x29U, 0xffU},
        omega::runtime::RenderClearColorRgba8{0x2aU, 0x2bU, 0x2cU, 0xffU},
    };
    constexpr auto transfer_source_begin = [](const std::uint32_t coordinate) noexcept {
        return static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(coordinate) *
                    omega::runtime::kNormalizedRenderExtent +
                15U) /
            16U);
    };
    constexpr auto transfer_source_end = [](const std::uint32_t coordinate) noexcept {
        return static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(coordinate + 1U) *
            omega::runtime::kNormalizedRenderExtent / 16U);
    };
    std::array<omega::runtime::RenderTextureBlitCommand, 16U> transfer_probes{};
    for (std::size_t index = 0U; index < transfer_probes.size(); ++index)
    {
        const std::uint32_t column = static_cast<std::uint32_t>(index % 4U);
        const std::uint32_t row = static_cast<std::uint32_t>(index / 4U);
        const std::uint32_t source_x = transfer_coordinates[index][0];
        const std::uint32_t source_y = transfer_coordinates[index][1];
        transfer_probes[index] = omega::runtime::RenderTextureBlitCommand{
            .texture = transfer_texture,
            .source = omega::runtime::RenderSourceRectQ16{
                .left = transfer_source_begin(source_x),
                .top = transfer_source_begin(source_y),
                .right = transfer_source_end(source_x),
                .bottom = transfer_source_end(source_y),
            },
            .destination = omega::runtime::RenderTargetRectQ16{
                .left = destination_edge(column),
                .top = destination_edge(row),
                .right = destination_edge(column + 1U),
                .bottom = destination_edge(row + 1U),
            },
            .fit_mode = omega::runtime::RenderTextureFitMode::Stretch,
            .filter_mode = omega::runtime::RenderTextureFilterMode::Nearest,
        };
    }
    auto transfer_draw_list = omega::runtime::RenderDrawList::Create(transfer_probes);
    Check(transfer_draw_list.has_value(),
        "the LevelContent transfer source-slot probes form a valid draw list");
    if (transfer_draw_list)
    {
        omega::runtime::RenderFramePacket packet{
            .clear_color = omega::runtime::kDefaultRenderClearColor,
            .draw_list = *transfer_draw_list,
        };
        auto readback =
            omega::app::detail::SdlGpuHostTestAccess::ReadbackBlitsForTesting(
                OmegaAppTestAccess::Host(app), packet);
        Check(readback && *readback == transfer_expected,
            "the first strict Packed24 transfer preserves all twelve source slots and synthetic fourth slots on GPU");
        Check(OmegaAppTestAccess::GpuSnapshot(app) == initial_gpu,
            "the LevelContent transfer readback leaves every GPU counter unchanged");
    }
}

void CheckNonPackedLevelContentFallback(omega::app::OmegaApp& app,
    const std::filesystem::path& source_root)
{
    using omega::app::detail::OmegaAppTestAccess;
    constexpr std::uint64_t kTopologyOnlyPresentationLogicalBytes =
        2ULL * 2ULL * 4ULL + 128ULL * 72ULL * 4ULL * 3ULL +
        32ULL * 32ULL * 4ULL + 1ULL * 1ULL * 4ULL;
    const auto assets = OmegaAppTestAccess::AssetSnapshot(app);
    const omega::app::GpuHostSnapshot gpu = OmegaAppTestAccess::GpuSnapshot(app);
    const auto topology_texture =
        OmegaAppTestAccess::DiagnosticAssetTopologyTexture(app);
    const auto transfer_texture =
        OmegaAppTestAccess::DiagnosticAssetTransferTexture(app);
    const auto commands =
        OmegaAppTestAccess::DiagnosticAssetTopologyDrawList(app).commands();
    constexpr omega::runtime::RenderSourceRectQ16 full_source{
        .left = 0U,
        .top = 0U,
        .right = omega::runtime::kNormalizedRenderExtent,
        .bottom = omega::runtime::kNormalizedRenderExtent,
    };
    constexpr omega::runtime::RenderTargetRectQ16 full_target{
        .left = 0U,
        .top = 0U,
        .right = omega::runtime::kNormalizedRenderExtent,
        .bottom = omega::runtime::kNormalizedRenderExtent,
    };
    constexpr omega::runtime::RenderTargetRectQ16 card_target{
        .left = 2048U,
        .top = 2048U,
        .right = 26624U,
        .bottom = 15872U,
    };
    Check(assets && IsAggregateEmpty(*assets, 64U) && topology_texture.valid() &&
              !transfer_texture.valid(),
        "non-Packed24 LevelContent restores assets and retains only topology presentation");
    Check(gpu.successful_uploads == 6U &&
              gpu.successful_upload_logical_bytes ==
                  kTopologyOnlyPresentationLogicalBytes &&
              gpu.successful_releases == 0U &&
              gpu.textures.reserved_slots == 0U &&
              gpu.textures.resident_slots == 6U &&
              gpu.textures.resident_logical_bytes ==
                  kTopologyOnlyPresentationLogicalBytes,
        "non-Packed24 LevelContent preserves the six-upload 114,708-byte fallback");
    Check(commands.size() == 2U &&
              commands[0].texture == OmegaAppTestAccess::DiagnosticTexture(app) &&
              commands[0].source == full_source &&
              commands[0].destination == full_target &&
              commands[1].texture == topology_texture &&
              commands[1].source == full_source &&
              commands[1].destination == card_target &&
              commands[1].fit_mode ==
                  omega::runtime::RenderTextureFitMode::Contain &&
              commands[1].filter_mode ==
                  omega::runtime::RenderTextureFilterMode::Nearest,
        "non-Packed24 LevelContent keeps the full-width topology draw path");

    constexpr std::string_view rejection_prefix =
        "packed-24 transfer diagnostic unavailable:";
    constexpr std::string_view exact_rejection =
        "packed-24 transfer diagnostic unavailable: unsupported-sample-encoding";
    constexpr std::array<std::string_view, 3U> source_identity_tokens{
        "A_READY.TDX", "GAMEDATA/TEST", "TEX.HOG"};
    const std::string source_root_text = source_root.string();
    std::size_t exact_rejection_count = 0U;
    bool unexpected_rejection = false;
    bool source_identity_disclosed = false;
    for (const omega::runtime::LogRecord& record :
        OmegaAppTestAccess::LogSnapshot(app))
    {
        if (record.message.starts_with(rejection_prefix))
        {
            if (record.severity == omega::runtime::LogSeverity::Info &&
                record.category == "startup" && record.message == exact_rejection)
                ++exact_rejection_count;
            else
                unexpected_rejection = true;
        }
        if ((!source_root_text.empty() &&
                record.message.find(source_root_text) != std::string::npos) ||
            std::ranges::any_of(source_identity_tokens,
                [&record](const std::string_view token) {
                    return record.message.find(token) != std::string::npos;
                }))
            source_identity_disclosed = true;
    }
    Check(exact_rejection_count == 1U && !unexpected_rejection &&
              !source_identity_disclosed,
        "the fallback records one fixed Packed24 category and no source identity");
}

void CheckPackedTransferUploadBudgetFallback(omega::app::OmegaApp& app,
    const std::filesystem::path& source_root)
{
    using omega::app::detail::OmegaAppTestAccess;
    constexpr std::uint64_t kTopologyOnlyPresentationLogicalBytes =
        2ULL * 2ULL * 4ULL + 128ULL * 72ULL * 4ULL * 3ULL +
        32ULL * 32ULL * 4ULL + 1ULL * 1ULL * 4ULL;
    const auto assets = OmegaAppTestAccess::AssetSnapshot(app);
    const omega::app::GpuHostSnapshot gpu = OmegaAppTestAccess::GpuSnapshot(app);
    const auto topology_texture =
        OmegaAppTestAccess::DiagnosticAssetTopologyTexture(app);
    const auto transfer_texture =
        OmegaAppTestAccess::DiagnosticAssetTransferTexture(app);
    const auto commands =
        OmegaAppTestAccess::DiagnosticAssetTopologyDrawList(app).commands();
    constexpr omega::runtime::RenderTargetRectQ16 card_target{
        .left = 2048U,
        .top = 2048U,
        .right = 26624U,
        .bottom = 15872U,
    };

    Check(assets && IsAggregateEmpty(*assets, 64U) && topology_texture.valid() &&
              !transfer_texture.valid(),
        "a rejected optional transfer upload preserves owned topology and exact asset cleanup");
    Check(gpu.successful_uploads == 6U &&
              gpu.successful_upload_logical_bytes ==
                  kTopologyOnlyPresentationLogicalBytes &&
              gpu.successful_releases == 0U &&
              gpu.textures.slot_capacity == 64U &&
              gpu.textures.free_slots == 58U &&
              gpu.textures.reserved_slots == 0U &&
              gpu.textures.resident_slots == 6U &&
              gpu.textures.resident_logical_bytes ==
                  kTopologyOnlyPresentationLogicalBytes,
        "the exact topology-only budget leaves no seventh reservation state");
    Check(commands.size() == 2U && commands[1].texture == topology_texture &&
              commands[1].destination == card_target &&
              commands[1].fit_mode ==
                  omega::runtime::RenderTextureFitMode::Contain &&
              commands[1].filter_mode ==
                  omega::runtime::RenderTextureFilterMode::Nearest,
        "a rejected transfer upload restores the full-width topology draw path");

    constexpr std::string_view exact_rejection =
        "packed-24 transfer diagnostic unavailable: upload-failed";
    constexpr std::array<std::string_view, 4U> forbidden_tokens{
        "resident-budget-exceeded", "A_READY.TDX", "GAMEDATA/TEST", "TEX.HOG"};
    const std::string source_root_text = source_root.string();
    std::size_t exact_rejection_count = 0U;
    bool unexpected_detail = false;
    for (const omega::runtime::LogRecord& record :
        OmegaAppTestAccess::LogSnapshot(app))
    {
        if (record.message == exact_rejection &&
            record.severity == omega::runtime::LogSeverity::Info &&
            record.category == "startup")
        {
            ++exact_rejection_count;
        }
        if ((!source_root_text.empty() &&
                record.message.find(source_root_text) != std::string::npos) ||
            std::ranges::any_of(forbidden_tokens,
                [&record](const std::string_view token) {
                    return record.message.find(token) != std::string::npos;
                }))
        {
            unexpected_detail = true;
        }
    }
    Check(exact_rejection_count == 1U && !unexpected_detail,
        "the upload fallback records one fixed identity-free INFO category");
}

[[nodiscard]] bool PushQuit()
{
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    return SDL_PushEvent(&event);
}

[[nodiscard]] bool PushQuitKey(const bool down)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = SDL_SCANCODE_F10;
    event.key.down = down;
    return SDL_PushEvent(&event);
}

[[nodiscard]] bool PushKey(const SDL_Scancode scancode, const bool down)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = scancode;
    event.key.down = down;
    return SDL_PushEvent(&event);
}

[[nodiscard]] std::optional<SdlWindowGeometry> ResolveOpenOmegaWindow() noexcept
{
    int window_count = 0;
    SDL_Window** windows = SDL_GetWindows(&window_count);
    if (windows == nullptr)
        return std::nullopt;

    std::optional<SdlWindowGeometry> resolved;
    for (int index = 0; index < window_count && !resolved; ++index)
    {
        SDL_Window* const window = windows[index];
        if (window == nullptr)
            continue;
        const char* const title = SDL_GetWindowTitle(window);
        if (title == nullptr ||
            std::string_view{title} != "OpenOmega - native runtime")
        {
            continue;
        }

        int logical_width = 0;
        int logical_height = 0;
        const SDL_WindowID id = SDL_GetWindowID(window);
        if (id != 0U && SDL_GetWindowSize(
                            window, &logical_width, &logical_height) &&
            logical_width > 0 && logical_height > 0)
        {
            resolved = SdlWindowGeometry{
                .id = id,
                .logical_width = logical_width,
                .logical_height = logical_height,
            };
        }
    }
    SDL_free(windows);
    return resolved;
}

[[nodiscard]] bool PushMouseButton(const Uint8 button, const bool down,
    const SDL_WindowID window_id = 0U, const float x = 0.0F,
    const float y = 0.0F)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.windowID = window_id;
    event.button.button = button;
    event.button.down = down;
    event.button.x = x;
    event.button.y = y;
    return SDL_PushEvent(&event);
}

// Logical action state recorded by a one-frame capture. Physical aliases share
// logical actions here, so only the trace can show whether a later alias really
// produced a fresh press edge or merely continued a held action.
[[nodiscard]] std::optional<omega::runtime::InputTraceActionState>
CapturedActionState(
    const std::expected<omega::app::RunCaptureOutcome, std::string>& capture,
    const std::uint32_t action)
{
    if (!capture)
        return std::nullopt;
    const auto* const pair = capture->trace_pair();
    if (pair == nullptr)
        return std::nullopt;
    return pair->input_trace().ActionAt(0U, action);
}

[[nodiscard]] bool IsFreshPress(
    const std::optional<omega::runtime::InputTraceActionState>& state) noexcept
{
    return state && state->held && state->pressed && !state->released;
}

[[nodiscard]] bool IsRelease(
    const std::optional<omega::runtime::InputTraceActionState>& state) noexcept
{
    return state && !state->held && !state->pressed && state->released;
}

void CheckActiveProfileConfirmation(
    const std::filesystem::path& fixture_root,
    const omega::runtime::RuntimeSettings& settings)
{
    using Access = omega::app::detail::OmegaAppTestAccess;
    const auto profile_id = omega::profiles::ProfileId::Parse(
        "00000000000000000000000000000001");
    Check(profile_id.has_value(),
        "the generated active-profile confirmation ID parses");
    if (!profile_id)
        return;

    constexpr omega::app::FrontEndState kProfilesFirst{
        .mode = omega::app::FrontEndMode::Profiles,
        .selected_main_row = omega::app::FrontEndMainRow::Profiles,
        .selected_profile_slot = omega::app::FrontEndProfileSlot::First,
    };
    constexpr omega::app::FrontEndState kCharactersFirst{
        .mode = omega::app::FrontEndMode::Characters,
        .selected_main_row = omega::app::FrontEndMainRow::Profiles,
        .selected_profile_slot = omega::app::FrontEndProfileSlot::First,
        .selected_character_slot =
            omega::app::FrontEndCharacterSlot::First,
    };
    constexpr std::string_view kMissingFailure =
        "active profile confirmation failed: profile-not-found";
    constexpr std::string_view kCapacityFailure =
        "active profile confirmation failed: storage-limit-exceeded";
    const omega::profiles::ProfileMetadata metadata{
        .display_name = "selection",
        .created_unix_milliseconds = 1U,
        .modified_unix_milliseconds = 1U,
    };
    const std::filesystem::path database_root =
        fixture_root / "native-active-profile-confirmation";

    {
        auto persistence = omega::app::NativePersistence::Bootstrap(database_root);
        Check(persistence.has_value(),
            "the generated active-profile database bootstraps");
        if (!persistence)
            return;
        const auto created = persistence->profiles().Create(*profile_id, metadata);
        Check(created && persistence->database().generation() == 1U &&
                  persistence->database().record_count() == 1U &&
                  persistence->database().logical_value_bytes() == 41U &&
                  !persistence->persisted_confirmed_profile_id(),
            "profile creation publishes generation one with one 41-byte metadata record and no confirmation");
        if (!created)
            return;
    }

    {
        auto persistence = omega::app::NativePersistence::Bootstrap(database_root);
        auto config = omega::runtime::ParseConfigText("");
        Check(persistence && config &&
                  persistence->startup_profiles().size() == 1U &&
                  !persistence->persisted_confirmed_profile_id(),
            "the generated unconfirmed profile reopens for app startup");
        if (!persistence || !config)
            return;

        auto app = Access::CreateWithPersistence(std::move(*config), settings,
            omega::runtime::ContentStartupState{}, std::move(*persistence), false);
        Check(app && Access::FrontEnd(*app) == kProfilesFirst &&
                  !Access::ActiveProfile(*app) &&
                  !Access::PersistedConfirmedProfile(*app),
            "startup enters Profiles/First with both session activation and durable confirmation unset");
        if (!app)
            return;

        const omega::app::GpuHostSnapshot gpu_before = Access::GpuSnapshot(*app);
        const bool pushed = PushKey(SDL_SCANCODE_F1, true);
        auto selected = app->RunWithCapture(1);
        Check(pushed && selected &&
                  selected->completion() ==
                      omega::app::RunCaptureCompletion::FrameLimitReached &&
                  !selected->failure() &&
                  Access::PersistenceGeneration(*app) ==
                      std::optional<std::uint64_t>{2U} &&
                  Access::PersistenceRecordCount(*app) ==
                      std::optional<std::size_t>{2U} &&
                  Access::PersistenceLogicalValueBytes(*app) ==
                      std::optional<std::size_t>{73U} &&
                  Access::PersistedConfirmedProfile(*app) == profile_id &&
                  Access::ActiveProfile(*app) == profile_id &&
                  Access::FrontEnd(*app) == kCharactersFirst &&
                  !Access::ActiveCharacter(*app) &&
                  !Access::PersistedConfirmedCharacter(*app) &&
                  Access::CharacterCatalogCount(*app, *profile_id) ==
                      std::optional<std::size_t>{0U} &&
                  IsOneCharacterMenuSubmissionWithTextureDelta(
                      gpu_before, Access::GpuSnapshot(*app), 2U, 0U, 2U),
            "confirmation commits generation two and the 32-byte pointer before publishing Characters with the empty and first-character preview cards resident");
        Check(PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value(),
            "the generated successful confirmation edge releases");
    }

    {
        auto persistence = omega::app::NativePersistence::Bootstrap(database_root);
        auto config = omega::runtime::ParseConfigText("");
        Check(persistence && config &&
                  persistence->persisted_confirmed_profile_id() == profile_id &&
                  persistence->database().generation() == 2U &&
                  persistence->database().record_count() == 2U &&
                  persistence->database().logical_value_bytes() == 73U,
            "reopen validates the generated durable confirmation and exact database totals");
        if (!persistence || !config)
            return;

        auto app = Access::CreateWithPersistence(std::move(*config), settings,
            omega::runtime::ContentStartupState{}, std::move(*persistence), false);
        Check(app && Access::FrontEnd(*app) == kProfilesFirst &&
                  !Access::ActiveProfile(*app) &&
                  Access::PersistedConfirmedProfile(*app) == profile_id,
            "a durable confirmation never implicitly activates the reopened session");
        if (!app)
            return;

        const omega::app::GpuHostSnapshot gpu_before = Access::GpuSnapshot(*app);
        const bool pushed = PushKey(SDL_SCANCODE_F1, true);
        auto reconfirmed = app->RunWithCapture(1);
        Check(pushed && reconfirmed &&
                  reconfirmed->completion() ==
                      omega::app::RunCaptureCompletion::FrameLimitReached &&
                  Access::PersistenceGeneration(*app) ==
                      std::optional<std::uint64_t>{2U} &&
                  Access::PersistenceRecordCount(*app) ==
                      std::optional<std::size_t>{2U} &&
                  Access::PersistenceLogicalValueBytes(*app) ==
                      std::optional<std::size_t>{73U} &&
                  Access::PersistedConfirmedProfile(*app) == profile_id &&
                  Access::ActiveProfile(*app) == profile_id &&
                  Access::FrontEnd(*app) == kCharactersFirst &&
                  !Access::ActiveCharacter(*app) &&
                  !Access::PersistedConfirmedCharacter(*app) &&
                  Access::CharacterCatalogCount(*app, *profile_id) ==
                      std::optional<std::size_t>{0U} &&
                  IsOneCharacterMenuSubmissionWithTextureDelta(
                      gpu_before, Access::GpuSnapshot(*app), 2U, 0U, 2U),
            "same-ID reconfirmation remains a no-write session activation and rebuilds the two empty-catalog character cards before entering Characters");
        Check(PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value(),
            "the generated idempotent confirmation edge releases");
    }

    {
        auto persistence = omega::app::NativePersistence::Bootstrap(database_root);
        auto config = omega::runtime::ParseConfigText("");
        if (!persistence || !config)
        {
            Check(false,
                "the missing-model confirmation fixture reopens");
            return;
        }
        auto app = Access::CreateWithPersistence(std::move(*config), settings,
            omega::runtime::ContentStartupState{}, std::move(*persistence), false);
        Check(app.has_value(),
            "the missing-model confirmation app starts");
        if (!app)
            return;

        const omega::app::GpuHostSnapshot gpu_before = Access::GpuSnapshot(*app);
        const bool erased = Access::EraseStartupProfileId(
            *app, omega::app::FrontEndProfileSlot::First);
        const bool pushed = PushKey(SDL_SCANCODE_F1, true);
        auto rejected = app->RunWithCapture(1);
        std::optional<std::string_view> failure;
        if (rejected)
            failure = rejected->failure();
        const std::string root_text = database_root.string();
        const std::string id_text = profile_id->ToString();
        Check(erased && pushed && rejected &&
                  rejected->completion() ==
                      omega::app::RunCaptureCompletion::OperationalFailure &&
                  failure && *failure == kMissingFailure &&
                  failure->size() < 96U &&
                  failure->find(root_text) == std::string_view::npos &&
                  failure->find(id_text) == std::string_view::npos &&
                  Access::FrontEnd(*app) == kProfilesFirst &&
                  !Access::ActiveProfile(*app) &&
                  Access::PersistedConfirmedProfile(*app) == profile_id &&
                  Access::PersistenceGeneration(*app) ==
                      std::optional<std::uint64_t>{2U} &&
                  Access::PersistenceRecordCount(*app) ==
                      std::optional<std::size_t>{2U} &&
                  Access::PersistenceLogicalValueBytes(*app) ==
                      std::optional<std::size_t>{73U} &&
                  Access::GpuSnapshot(*app) == gpu_before,
            "a missing startup-model ID fails with one bounded private error while preserving Profiles, the durable pointer, session state, database totals, and exact GPU state");
        Check(PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value(),
            "the generated missing-model confirmation edge releases");
    }

    const std::filesystem::path constrained_root =
        fixture_root / "native-active-profile-capacity";
    {
        auto persistence = omega::app::NativePersistence::Bootstrap(constrained_root,
            omega::persistence::SaveDatabaseLimits{.max_records = 1U});
        Check(persistence.has_value(),
            "the one-record active-profile database bootstraps");
        if (!persistence)
            return;
        const auto created = persistence->profiles().Create(*profile_id, metadata);
        Check(created && persistence->database().generation() == 1U &&
                  persistence->database().record_count() == 1U &&
                  persistence->database().logical_value_bytes() == 41U,
            "the capacity fixture fills its sole record with the generated profile");
        if (!created)
            return;
    }

    auto constrained_persistence = omega::app::NativePersistence::Bootstrap(
        constrained_root,
        omega::persistence::SaveDatabaseLimits{.max_records = 1U});
    auto constrained_config = omega::runtime::ParseConfigText("");
    Check(constrained_persistence && constrained_config &&
              constrained_persistence->startup_profiles().size() == 1U,
        "the full one-record capacity fixture reopens with its startup model");
    if (!constrained_persistence || !constrained_config)
        return;

    auto constrained_app = Access::CreateWithPersistence(
        std::move(*constrained_config), settings,
        omega::runtime::ContentStartupState{},
        std::move(*constrained_persistence), false);
    Check(constrained_app && Access::FrontEnd(*constrained_app) == kProfilesFirst &&
              !Access::ActiveProfile(*constrained_app) &&
              !Access::PersistedConfirmedProfile(*constrained_app),
        "the full one-record database still starts unconfirmed in Profiles");
    if (!constrained_app)
        return;

    const omega::app::GpuHostSnapshot gpu_before =
        Access::GpuSnapshot(*constrained_app);
    const bool pushed = PushKey(SDL_SCANCODE_F1, true);
    auto rejected = constrained_app->RunWithCapture(1);
    std::optional<std::string_view> failure;
    if (rejected)
        failure = rejected->failure();
    const std::string root_text = constrained_root.string();
    const std::string id_text = profile_id->ToString();
    Check(pushed && rejected &&
              rejected->completion() ==
                  omega::app::RunCaptureCompletion::OperationalFailure &&
              failure && *failure == kCapacityFailure &&
              failure->size() < 96U &&
              failure->find(root_text) == std::string_view::npos &&
              failure->find(id_text) == std::string_view::npos &&
              Access::FrontEnd(*constrained_app) == kProfilesFirst &&
              !Access::ActiveProfile(*constrained_app) &&
              !Access::PersistedConfirmedProfile(*constrained_app) &&
              Access::PersistenceGeneration(*constrained_app) ==
                  std::optional<std::uint64_t>{1U} &&
              Access::PersistenceRecordCount(*constrained_app) ==
                  std::optional<std::size_t>{1U} &&
              Access::PersistenceLogicalValueBytes(*constrained_app) ==
                  std::optional<std::size_t>{41U} &&
              IsRejectedCharacterPreparationWithTextureDelta(gpu_before,
                  Access::GpuSnapshot(*constrained_app), 2U, 2U),
        "capacity rejection releases both staged empty-catalog character cards and preserves the prior Profiles state, unset session and durable values, generation-one database, and private fixed error");
    Check(PushKey(SDL_SCANCODE_F1, false) &&
              constrained_app->Run(1).has_value(),
        "the generated capacity-rejected confirmation edge releases");
}

void CheckDiagnosticCampaignStart(
    const std::filesystem::path& fixture_root,
    const omega::runtime::RuntimeSettings& settings)
{
    using Access = omega::app::detail::OmegaAppTestAccess;
    const auto profile_id = omega::profiles::ProfileId::Parse(
        "10101010101010101010101010101010");
    const auto character_id = omega::profiles::CharacterId::Parse(
        "00000000000000000000000000000001");
    Check(profile_id.has_value() && character_id.has_value(),
        "the generated diagnostic-campaign profile and character IDs parse");
    if (!profile_id || !character_id)
        return;

    constexpr omega::app::FrontEndState kProfilesFirst{
        .mode = omega::app::FrontEndMode::Profiles,
        .selected_main_row = omega::app::FrontEndMainRow::Profiles,
        .selected_profile_slot = omega::app::FrontEndProfileSlot::First,
    };
    constexpr omega::app::FrontEndState kMainStartDiagnostic{
        .mode = omega::app::FrontEndMode::Main,
        .selected_main_row = omega::app::FrontEndMainRow::StartDiagnostic,
        .selected_profile_slot = omega::app::FrontEndProfileSlot::First,
    };
    constexpr omega::app::FrontEndState kBriefingRoom{
        .mode = omega::app::FrontEndMode::BriefingRoom,
        .selected_main_row = omega::app::FrontEndMainRow::StartDiagnostic,
        .selected_profile_slot = omega::app::FrontEndProfileSlot::First,
        .selected_character_slot =
            omega::app::FrontEndCharacterSlot::First,
    };
    constexpr omega::app::FrontEndState kCharactersFirst{
        .mode = omega::app::FrontEndMode::Characters,
        .selected_main_row = omega::app::FrontEndMainRow::Profiles,
        .selected_profile_slot = omega::app::FrontEndProfileSlot::First,
        .selected_character_slot =
            omega::app::FrontEndCharacterSlot::First,
    };
    constexpr omega::app::FrontEndState kDiagnosticPlay{
        .mode = omega::app::FrontEndMode::DiagnosticPlay,
        .selected_main_row = omega::app::FrontEndMainRow::StartDiagnostic,
        .selected_profile_slot = omega::app::FrontEndProfileSlot::First,
    };
    const omega::profiles::ProfileMetadata metadata{
        .display_name = "diagstart",
        .created_unix_milliseconds = 1U,
        .modified_unix_milliseconds = 1U,
    };
    const std::filesystem::path database_root =
        fixture_root / "native-diagnostic-campaign-start";

    {
        auto persistence = omega::app::NativePersistence::Bootstrap(database_root);
        Check(persistence.has_value(),
            "the generated diagnostic-campaign database bootstraps");
        if (!persistence)
            return;
        const auto created = persistence->profiles().Create(*profile_id, metadata);
        Check(created.has_value(),
            "the generated diagnostic-campaign profile is created");
        if (!created)
            return;
        const auto confirmed = persistence->ConfirmActiveProfile(*profile_id);
        Check(created && confirmed && persistence->database().generation() == 2U &&
                  persistence->database().record_count() == 2U &&
                  persistence->database().logical_value_bytes() == 73U,
            "the generated diagnostic-campaign fixture stores one profile and active pointer");
        if (!created || !confirmed)
            return;
    }

    {
        auto persistence = omega::app::NativePersistence::Bootstrap(database_root);
        auto config = omega::runtime::ParseConfigText("");
        Check(persistence && config &&
                  persistence->persisted_confirmed_profile_id() == profile_id,
            "the generated diagnostic-campaign fixture reopens with a validated durable confirmation");
        if (!persistence || !config)
            return;
        auto app = Access::CreateWithPersistence(std::move(*config), settings,
            omega::runtime::ContentStartupState{}, std::move(*persistence), false);
        Check(app && Access::FrontEnd(*app) == kProfilesFirst &&
                  !Access::ActiveProfile(*app),
            "diagnostic-campaign app startup still requires explicit per-launch selection");
        if (!app)
            return;

        const auto gpu_before_profile = Access::GpuSnapshot(*app);
        Check(PushKey(SDL_SCANCODE_F1, true) &&
                  app->RunWithCapture(1).has_value() &&
                  Access::ActiveProfile(*app) == profile_id &&
                  !Access::ActiveCharacter(*app) &&
                  Access::FrontEnd(*app) == kCharactersFirst &&
                  Access::PersistenceGeneration(*app) ==
                      std::optional<std::uint64_t>{2U} &&
                  IsOneCharacterMenuSubmissionWithTextureDelta(
                      gpu_before_profile, Access::GpuSnapshot(*app), 2U, 0U, 2U),
            "same-ID profile selection enters the empty Characters surface without another write and stages both character cards");
        Check(PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value(),
            "the diagnostic-campaign selection edge releases");

        const auto gpu_before_character_create = Access::GpuSnapshot(*app);
        Check(PushKey(SDL_SCANCODE_F1, true) && app->Run(1).has_value() &&
                  Access::FrontEnd(*app) == kCharactersFirst &&
                  !Access::ActiveCharacter(*app) &&
                  Access::CharacterCatalogCount(*app, *profile_id) ==
                      std::optional<std::size_t>{1U} &&
                  Access::PersistenceGeneration(*app) ==
                      std::optional<std::uint64_t>{3U} &&
                  Access::PersistenceRecordCount(*app) ==
                      std::optional<std::size_t>{3U} &&
                  Access::PersistenceLogicalValueBytes(*app) ==
                      std::optional<std::size_t>{125U} &&
                  IsOneCharacterMenuSubmissionWithTextureDelta(
                      gpu_before_character_create, Access::GpuSnapshot(*app),
                      0U, 1U, 3U),
            "character creation publishes its 52-byte marker, stays in Characters, and releases the obsolete empty card");
        Check(PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value(),
            "the generated character-creation edge releases");

        const auto gpu_before_character_select = Access::GpuSnapshot(*app);
        Check(PushKey(SDL_SCANCODE_F1, true) && app->Run(1).has_value() &&
                  Access::FrontEnd(*app) == kBriefingRoom &&
                  Access::ActiveCharacter(*app) == character_id &&
                  Access::PersistedConfirmedCharacter(*app) == character_id &&
                  Access::PersistenceGeneration(*app) ==
                      std::optional<std::uint64_t>{4U} &&
                  Access::PersistenceRecordCount(*app) ==
                      std::optional<std::size_t>{4U} &&
                  Access::PersistenceLogicalValueBytes(*app) ==
                      std::optional<std::size_t>{173U} &&
                  IsOneVisibleMenuSubmission(
                      gpu_before_character_select, Access::GpuSnapshot(*app)),
            "character selection commits its 48-byte active pointer and enters the Briefing Room mission selector");
        Check(PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value(),
            "the generated character-selection edge releases");

        const omega::app::GpuHostSnapshot gpu_before = Access::GpuSnapshot(*app);
        const bool pushed = PushMouseButton(SDL_BUTTON_LEFT, true);
        auto started = app->RunWithCapture(1);
        Check(pushed && started &&
                  started->completion() ==
                      omega::app::RunCaptureCompletion::FrameLimitReached &&
                  !started->failure() &&
                  Access::FrontEnd(*app) == kDiagnosticPlay &&
                  Access::ActiveProfile(*app) == profile_id &&
                  Access::PersistedConfirmedProfile(*app) == profile_id &&
                  Access::ActiveCharacter(*app) == character_id &&
                  Access::PersistedConfirmedCharacter(*app) == character_id &&
                  Access::PersistenceGeneration(*app) ==
                      std::optional<std::uint64_t>{5U} &&
                  Access::PersistenceRecordCount(*app) ==
                      std::optional<std::size_t>{5U} &&
                  Access::PersistenceLogicalValueBytes(*app) ==
                      std::optional<std::size_t>{221U} &&
                  IsOneDiagnosticPlaySubmission(gpu_before,
                      Access::GpuSnapshot(*app)),
            "left-click mission selection commits the 48-byte character-owned session checkpoint before publishing DiagnosticPlay without leaking a fire cue into the deployed frame");

        Check(PushMouseButton(SDL_BUTTON_LEFT, false) && app->Run(1).has_value(),
            "the first generated mouse mission-selection edge releases");
        Check(PushKey(SDL_SCANCODE_F1, true) && app->Run(1).has_value() &&
                  Access::FrontEnd(*app) == kBriefingRoom,
            "primary returns the generated diagnostic surface to the Briefing Room");
        Check(PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value(),
            "the generated diagnostic-return edge releases");
        const std::uint64_t before_idempotent =
            Access::PersistenceGeneration(*app).value_or(0U);
        Check(PushKey(SDL_SCANCODE_F1, true) && app->Run(1).has_value() &&
                  Access::FrontEnd(*app) == kDiagnosticPlay &&
                  Access::PersistenceGeneration(*app) ==
                      std::optional<std::uint64_t>{before_idempotent},
            "a second generated diagnostic start is a no-write idempotent transition");
        Check(PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value(),
            "the idempotent diagnostic-start edge releases");
    }

    auto reopened = omega::app::NativePersistence::Bootstrap(database_root);
    Check(reopened && reopened->persisted_confirmed_profile_id() == profile_id &&
              reopened->persisted_confirmed_character_id() == character_id &&
              reopened->database().generation() == 5U &&
              reopened->database().record_count() == 5U &&
              reopened->database().logical_value_bytes() == 221U,
        "reopen validates both durable identities, the character-owned session checkpoint, and exact database totals");

    const std::filesystem::path unconfirmed_root =
        fixture_root / "native-diagnostic-campaign-unconfirmed";
    {
        auto persistence = omega::app::NativePersistence::Bootstrap(unconfirmed_root);
        Check(persistence.has_value(),
            "the unconfirmed diagnostic-campaign database bootstraps");
        if (!persistence)
            return;
        const auto created = persistence->profiles().Create(*profile_id, metadata);
        Check(created.has_value(),
            "the unconfirmed diagnostic-campaign profile is created");
        if (!created)
            return;
    }
    {
        auto persistence = omega::app::NativePersistence::Bootstrap(unconfirmed_root);
        auto config = omega::runtime::ParseConfigText("");
        if (!persistence || !config)
        {
            Check(false, "the unconfirmed diagnostic-campaign fixture reopens");
            return;
        }
        auto app = Access::CreateWithPersistence(std::move(*config), settings,
            omega::runtime::ContentStartupState{}, std::move(*persistence), false);
        Check(app.has_value(), "the unconfirmed diagnostic-campaign app starts");
        if (!app)
            return;
        Access::SetFrontEndState(*app, kMainStartDiagnostic);
        const omega::app::GpuHostSnapshot gpu_before = Access::GpuSnapshot(*app);
        auto invalid_slot = Access::ApplyFrontEndCommand(*app,
            omega::app::FrontEndCommand{
                .type = omega::app::FrontEndCommandType::StartDiagnosticCampaign,
                .profile_slot = omega::app::FrontEndProfileSlot::Second,
            });
        auto direct_unconfirmed = Access::ApplyFrontEndCommand(*app,
            omega::app::FrontEndCommand{
                .type = omega::app::FrontEndCommandType::StartDiagnosticCampaign,
                .profile_slot = omega::app::FrontEndProfileSlot::First,
            });
        Check(!invalid_slot && invalid_slot.error() ==
                  "diagnostic campaign start selected an invalid slot" &&
                  !direct_unconfirmed && direct_unconfirmed.error() ==
                      "game session start failed: active-profile-required" &&
                  Access::FrontEnd(*app) == kMainStartDiagnostic &&
                  Access::GpuSnapshot(*app) == gpu_before,
            "typed diagnostic-start application rejects malformed and unconfirmed requests without publication");
        const bool pushed = PushKey(SDL_SCANCODE_F1, true);
        auto suppressed = app->RunWithCapture(1);
        Check(pushed && suppressed &&
                  suppressed->completion() ==
                      omega::app::RunCaptureCompletion::FrameLimitReached &&
                  !suppressed->failure() &&
                  Access::FrontEnd(*app) == kMainStartDiagnostic &&
                  !Access::ActiveProfile(*app) &&
                  !Access::PersistedConfirmedProfile(*app) &&
                  Access::PersistenceGeneration(*app) ==
                      std::optional<std::uint64_t>{1U} &&
                  Access::PersistenceRecordCount(*app) ==
                      std::optional<std::size_t>{1U} &&
                  IsOneVisibleMenuSubmission(
                      gpu_before, Access::GpuSnapshot(*app)),
            "the production reducer keeps Start inert without a confirmed session profile and renders the unchanged Main surface");
        Check(PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value(),
            "the suppressed unconfirmed diagnostic-start edge releases");
    }

    const std::filesystem::path constrained_root =
        fixture_root / "native-diagnostic-campaign-capacity";
    constexpr omega::persistence::SaveDatabaseLimits kFourRecordLimit{
        .max_records = 4U,
        .max_key_bytes =
            omega::persistence::SaveDatabase::kHardMaxKeyBytes,
    };
    {
        auto persistence = omega::app::NativePersistence::Bootstrap(
            constrained_root, kFourRecordLimit);
        Check(persistence.has_value(),
            "the four-record diagnostic-campaign database bootstraps");
        if (!persistence)
            return;
        const auto created = persistence->profiles().Create(*profile_id, metadata);
        Check(created.has_value(),
            "the constrained diagnostic-campaign profile is created");
        if (!created)
            return;
        const auto confirmed = persistence->ConfirmActiveProfile(*profile_id);
        const auto character_created = persistence->characters().Create(
            *profile_id, *character_id,
            omega::profiles::CharacterMetadata{
                .display_name =
                    std::string{omega::app::kFrontEndFirstCharacterDisplayName},
                .created_unix_milliseconds = 2U,
                .modified_unix_milliseconds = 2U,
            });
        const auto character_confirmed =
            persistence->ConfirmActiveCharacter(*profile_id, *character_id);
        Check(created && confirmed && character_created && character_confirmed &&
                  persistence->database().generation() == 4U &&
                  persistence->database().record_count() == 4U &&
                  persistence->database().logical_value_bytes() == 173U,
            "the capacity fixture fills four records with profile, character, and both active pointers");
        if (!created || !confirmed || !character_created || !character_confirmed)
            return;
    }
    {
        auto persistence = omega::app::NativePersistence::Bootstrap(
            constrained_root, kFourRecordLimit);
        auto config = omega::runtime::ParseConfigText("");
        if (!persistence || !config)
        {
            Check(false, "the constrained diagnostic-campaign fixture reopens");
            return;
        }
        auto app = Access::CreateWithPersistence(std::move(*config), settings,
            omega::runtime::ContentStartupState{}, std::move(*persistence), false);
        Check(app.has_value(), "the constrained diagnostic-campaign app starts");
        if (!app)
            return;
        const auto gpu_before_profile = Access::GpuSnapshot(*app);
        Check(PushKey(SDL_SCANCODE_F1, true) && app->Run(1).has_value() &&
                  Access::ActiveProfile(*app) == profile_id &&
                  !Access::ActiveCharacter(*app) &&
                  Access::PersistedConfirmedCharacter(*app) == character_id &&
                  Access::FrontEnd(*app) == kCharactersFirst &&
                  IsOneCharacterMenuSubmissionWithTextureDelta(
                      gpu_before_profile, Access::GpuSnapshot(*app), 1U, 0U, 3U),
            "the constrained app explicitly reactivates its profile and opens the existing character without rewriting durable state");
        Check(PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value(),
            "the constrained profile-selection edge releases");
        Check(PushKey(SDL_SCANCODE_F1, true) && app->Run(1).has_value() &&
                  Access::FrontEnd(*app) == kBriefingRoom &&
                  Access::ActiveCharacter(*app) == character_id &&
                  Access::PersistedConfirmedCharacter(*app) == character_id &&
                  Access::PersistenceGeneration(*app) ==
                      std::optional<std::uint64_t>{4U},
            "the constrained app explicitly selects the already-confirmed character without another write");
        Check(PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value(),
            "the constrained character-selection edge releases");
        const omega::app::GpuHostSnapshot gpu_before = Access::GpuSnapshot(*app);
        const bool pushed = PushKey(SDL_SCANCODE_F1, true);
        auto rejected = app->RunWithCapture(1);
        const std::optional<std::string_view> failure = rejected
            ? rejected->failure()
            : std::nullopt;
        Check(pushed && rejected &&
                  rejected->completion() ==
                      omega::app::RunCaptureCompletion::OperationalFailure &&
                  failure && *failure ==
                      "game session start failed: storage-limit-exceeded" &&
                  failure->find(constrained_root.string()) ==
                      std::string_view::npos &&
                  failure->find(profile_id->ToString()) ==
                      std::string_view::npos &&
                  Access::FrontEnd(*app) == kBriefingRoom &&
                  Access::ActiveProfile(*app) == profile_id &&
                  Access::PersistedConfirmedProfile(*app) == profile_id &&
                  Access::ActiveCharacter(*app) == character_id &&
                  Access::PersistedConfirmedCharacter(*app) == character_id &&
                  Access::PersistenceGeneration(*app) ==
                      std::optional<std::uint64_t>{4U} &&
                  Access::PersistenceRecordCount(*app) ==
                      std::optional<std::size_t>{4U} &&
                  Access::PersistenceLogicalValueBytes(*app) ==
                      std::optional<std::size_t>{173U} &&
                  Access::GpuSnapshot(*app) == gpu_before,
            "character-owned checkpoint capacity failure preserves the Briefing Room, both active identities, database totals, and exact GPU state");
        Check(PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value(),
            "the rejected capacity diagnostic-start edge releases");
    }
}

void CheckDiagnosticSceneMissionActivation(
    const GeneratedLevelContentTree& tree,
    const omega::runtime::RuntimeSettings& settings)
{
    using Access = omega::app::detail::OmegaAppTestAccess;
    const auto profile_id = omega::profiles::ProfileId::Parse(
        "00000000000000000000000000000021");
    const auto character_id = omega::profiles::CharacterId::Parse(
        "00000000000000000000000000000022");
    const std::filesystem::path database_root =
        tree.root() / "native-diagnostic-scene-mission";
    Check(profile_id && character_id,
        "the synthetic diagnostic-scene profile and character IDs parse");
    if (!profile_id || !character_id)
        return;

    {
        auto persistence = omega::app::NativePersistence::Bootstrap(database_root);
        Check(persistence.has_value(),
            "the diagnostic-scene mission database bootstraps");
        if (!persistence)
            return;
        const auto profile_created = persistence->profiles().Create(
            *profile_id,
            omega::profiles::ProfileMetadata{
                .display_name = "scene",
                .created_unix_milliseconds = 1U,
                .modified_unix_milliseconds = 1U,
            });
        const auto profile_confirmed =
            persistence->ConfirmActiveProfile(*profile_id);
        const auto character_created = persistence->characters().Create(
            *profile_id, *character_id,
            omega::profiles::CharacterMetadata{
                .display_name = "mesh",
                .created_unix_milliseconds = 2U,
                .modified_unix_milliseconds = 2U,
            });
        const auto character_confirmed =
            persistence->ConfirmActiveCharacter(*profile_id, *character_id);
        Check(profile_created && profile_confirmed && character_created &&
                  character_confirmed,
            "the diagnostic-scene mission owns one confirmed profile and character");
        if (!profile_created || !profile_confirmed || !character_created ||
            !character_confirmed)
            return;
    }

    {
        auto persistence = omega::app::NativePersistence::Bootstrap(database_root);
        auto config = omega::runtime::ParseConfigText("");
        auto content = BuildLevelContentStartupState(tree);
        Check(persistence && config && content,
            "the diagnostic-scene mission startup aggregate is ready");
        if (!persistence || !config || !content)
            return;
        InstallSyntheticSpatialTriangle(*content);

        auto app = Access::CreateWithPersistence(std::move(*config), settings,
            std::move(*content), std::move(*persistence), false);
        Check(app.has_value(),
            "OmegaApp transactionally uploads the synthetic canonical spatial scene");
        if (!app)
            return;

        const omega::app::GpuHostSnapshot initial_gpu = Access::GpuSnapshot(*app);
        const auto scene_commands = Access::DiagnosticSceneMeshDrawList(*app).commands();
        const auto scene_overlay = Access::DiagnosticSceneOverlayDrawList(*app).commands();
        constexpr auto kObjectiveDestination =
            omega::app::PlanProjectDiagnosticObjectiveMarkerDestination(
                omega::gameplay::DiagnosticProximityTriggerState{});
        static_assert(kObjectiveDestination.has_value());
        constexpr auto kTargetDestination =
            omega::app::PlanProjectDiagnosticTargetMarkerDestination(
                omega::gameplay::DiagnosticProximityTriggerState{
                    .objective_complete = true},
                omega::gameplay::DiagnosticTargetFireState{});
        static_assert(kTargetDestination.has_value());
        const omega::runtime::RenderMeshHandle initial_environment_mesh_handle =
            scene_commands.empty() ? omega::runtime::RenderMeshHandle{}
                                   : scene_commands.front().mesh;
        Check(initial_gpu.successful_mesh_uploads == 2U &&
                  initial_gpu.successful_mesh_upload_logical_bytes == 96U &&
                  initial_gpu.successful_mesh_releases == 0U &&
                  initial_gpu.meshes.slot_capacity == 64U &&
                  initial_gpu.meshes.free_slots == 62U &&
                  initial_gpu.meshes.reserved_slots == 0U &&
                  initial_gpu.meshes.resident_slots == 2U &&
                  initial_gpu.meshes.resident_positions == 6U &&
                  initial_gpu.meshes.resident_triangle_indices == 6U &&
                  initial_gpu.meshes.resident_logical_bytes == 96U,
            "startup owns one environment mesh followed by one exact 48-byte actor mesh with no partial reservation");
        Check(scene_commands.size() == 2U && scene_commands[0].mesh.valid() &&
                   scene_commands[0].object_to_clip ==
                       omega::asset::kIdentityMatrix4x4IR &&
                  scene_commands[0].color ==
                      omega::runtime::RenderMeshColorRgba8{
                          .red = 112U,
                          .green = 220U,
                          .blue = 255U,
                          .alpha = 255U,
                      } &&
                   scene_commands[0].raster_mode ==
                       omega::runtime::RenderMeshRasterMode::Fill &&
                  scene_commands[1].mesh.valid() &&
                  scene_commands[1].mesh != scene_commands[0].mesh &&
                  scene_commands[1].object_to_clip ==
                      omega::asset::kIdentityMatrix4x4IR &&
                  scene_commands[1].color ==
                      omega::runtime::RenderMeshColorRgba8{
                          .red = 255U,
                          .green = 64U,
                          .blue = 224U,
                          .alpha = 255U,
                      } &&
                  scene_commands[1].raster_mode ==
                      omega::runtime::RenderMeshRasterMode::Fill &&
                   scene_overlay.size() == 1U &&
                   scene_overlay[0].texture ==
                       Access::DiagnosticActorMarkerTexture(*app) &&
                   scene_overlay[0].destination == *kObjectiveDestination &&
                   SameProximityTriggerState(
                       Access::DiagnosticProximityTriggerState(*app), {}) &&
                   Access::DiagnosticTargetFireState(*app) ==
                       omega::gameplay::DiagnosticTargetFireState{} &&
                   Access::DiagnosticMissionLifecycleState(*app) ==
                       omega::gameplay::DiagnosticMissionLifecycleState{} &&
                   Access::CurrentFrontEndMeshDrawList(*app).empty(),
            "the validated environment-plus-actor scene and armed objective remain resident but hidden in Profiles");

        const bool reached_briefing =
            PushKey(SDL_SCANCODE_F1, true) && app->Run(1).has_value() &&
            PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value() &&
            PushKey(SDL_SCANCODE_F1, true) && app->Run(1).has_value() &&
            PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value();
        const omega::app::GpuHostSnapshot briefing_gpu = Access::GpuSnapshot(*app);
        Check(reached_briefing &&
                  Access::FrontEnd(*app).mode ==
                      omega::app::FrontEndMode::BriefingRoom &&
                  briefing_gpu.mesh_submissions == 0U &&
                  briefing_gpu.successful_mesh_draws == 0U &&
                  briefing_gpu.meshes == initial_gpu.meshes &&
                  Access::CurrentFrontEndMeshDrawList(*app).empty(),
            "profile and character selection reach BriefingRoom without submitting the resident scene");
        if (!reached_briefing)
            return;

        const auto app_window = ResolveOpenOmegaWindow();
        Check(app_window.has_value(),
            "the diagnostic-scene app exposes its current logical SDL window geometry");
        if (!app_window)
            return;
        const float pointer_x =
            static_cast<float>(app_window->logical_width) / 4.0F;
        const float pointer_y =
            static_cast<float>(app_window->logical_height) * 3.0F / 4.0F;
        const bool pressed = PushMouseButton(SDL_BUTTON_LEFT, true,
            app_window->id, pointer_x, pointer_y);
        auto started = app->RunWithCapture(1);
        const auto* const started_pair = started ? started->trace_pair() : nullptr;
        const auto started_pointer = started_pair != nullptr
                                         ? started_pair->input_trace().PointerAt(0U)
                                         : std::nullopt;
        const omega::app::GpuHostSnapshot play_gpu = Access::GpuSnapshot(*app);
        Check(pressed && started && !started->failure() &&
                  started_pointer == kQuarterThreeQuarterPointer &&
                  IsFreshPress(CapturedActionState(
                      started, omega::app::kDebugFireAction)) &&
                  Access::FrontEnd(*app).mode ==
                      omega::app::FrontEndMode::DiagnosticPlay &&
                  SameTextureResidency(briefing_gpu, play_gpu) &&
                  play_gpu.meshes == briefing_gpu.meshes &&
                  play_gpu.successful_mesh_uploads ==
                      briefing_gpu.successful_mesh_uploads &&
                  play_gpu.successful_mesh_releases ==
                      briefing_gpu.successful_mesh_releases &&
                  play_gpu.frame_submissions == briefing_gpu.frame_submissions + 1U &&
                   play_gpu.blit_submissions == briefing_gpu.blit_submissions + 1U &&
                   play_gpu.successful_blit_draws ==
                       briefing_gpu.successful_blit_draws + 1U &&
                  play_gpu.mesh_submissions == briefing_gpu.mesh_submissions + 1U &&
                  play_gpu.successful_mesh_draws ==
                      briefing_gpu.successful_mesh_draws + 2U &&
                   Access::CurrentFrontEndMeshDrawList(*app).size() == 2U &&
                   Access::DiagnosticSceneOverlayDrawList(*app).commands().size() == 1U &&
                   Access::DiagnosticSceneOverlayDrawList(*app).commands()[0].destination ==
                       *kObjectiveDestination &&
                   SameProximityTriggerState(
                       Access::DiagnosticProximityTriggerState(*app), {}) &&
                   Access::DiagnosticTargetFireState(*app) ==
                       omega::gameplay::DiagnosticTargetFireState{} &&
                   Access::DiagnosticMissionLifecycleState(*app).status ==
                       omega::gameplay::DiagnosticMissionStatus::Active &&
                   Access::DebugLocomotionPosition(*app) ==
                       std::optional<omega::simulation::Position3>{
                           omega::simulation::Position3{}} &&
                   DrawListsEqual(Access::CurrentFrontEndDrawList(*app),
                       Access::DiagnosticSceneOverlayDrawList(*app)),
            "the positioned BriefingRoom mission click records the exact project pointer, publishes environment, actor, and armed objective, and emits no deploy-click activation or fire cue");

        Check(PushMouseButton(SDL_BUTTON_LEFT, false) && app->Run(1).has_value(),
            "the diagnostic-scene mission click releases in DiagnosticPlay");

        const omega::runtime::RenderMeshDrawList before_movement =
            Access::CurrentFrontEndMeshDrawList(*app);
        const omega::app::GpuHostSnapshot movement_gpu_before =
            Access::GpuSnapshot(*app);
        const bool movement_queued =
            Access::ArmNextRunElapsed(*app, settings.frame.simulation_step) &&
            PushKey(SDL_SCANCODE_W, true);
        auto movement = app->RunWithCapture(1);
        const omega::runtime::RenderMeshDrawList after_movement =
            Access::CurrentFrontEndMeshDrawList(*app);
        const auto moved_position = Access::DebugLocomotionPosition(*app);
        const omega::app::GpuHostSnapshot movement_gpu_after =
            Access::GpuSnapshot(*app);
        const auto before_movement_commands = before_movement.commands();
        const auto after_movement_commands = after_movement.commands();
        Check(movement_queued && movement && !movement->failure() &&
                  moved_position &&
                  *moved_position == omega::simulation::Position3{.z = 1} &&
                  before_movement_commands.size() == 2U &&
                  after_movement_commands.size() == 2U &&
                  after_movement_commands[0] == before_movement_commands[0] &&
                  after_movement_commands[1].mesh ==
                      before_movement_commands[1].mesh &&
                  after_movement_commands[1].color ==
                      before_movement_commands[1].color &&
                  after_movement_commands[1].raster_mode ==
                      before_movement_commands[1].raster_mode &&
                  after_movement_commands[1].object_to_clip ==
                      omega::app::PlanProjectDiagnosticActorMeshTransform(
                          *moved_position) &&
                  after_movement_commands[1].object_to_clip !=
                      before_movement_commands[1].object_to_clip &&
                  movement_gpu_after.successful_mesh_uploads ==
                      movement_gpu_before.successful_mesh_uploads &&
                   movement_gpu_after.successful_mesh_releases ==
                       movement_gpu_before.successful_mesh_releases &&
                   movement_gpu_after.meshes == movement_gpu_before.meshes &&
                   SameTextureResidency(
                       movement_gpu_before, movement_gpu_after) &&
                   movement_gpu_after.successful_mesh_draws ==
                      movement_gpu_before.successful_mesh_draws + 2U &&
                   Access::CurrentFrontEndDrawList(*app).commands().size() == 1U &&
                   Access::CurrentFrontEndDrawList(*app).commands()[0].destination ==
                       *kObjectiveDestination &&
                   SameProximityTriggerState(
                       Access::DiagnosticProximityTriggerState(*app), {}),
            "one keyboard simulation step keeps the environment prefix immutable and updates only the actor mesh transform without GPU resource churn");

        Check(PushKey(SDL_SCANCODE_W, false) &&
                  Access::ArmNextRunElapsed(
                      *app, std::chrono::nanoseconds::zero()) &&
                  app->Run(1).has_value() &&
                  MeshDrawListsEqual(after_movement,
                      Access::CurrentFrontEndMeshDrawList(*app)),
            "a zero-step movement release reproduces the same combined mesh draw list");

        const bool target_pressed = PushMouseButton(SDL_BUTTON_RIGHT, true) &&
            Access::ArmNextRunElapsed(*app, std::chrono::nanoseconds::zero());
        auto target_frame = app->RunWithCapture(1);
        const auto target_overlay =
            Access::DiagnosticSceneOverlayDrawList(*app).commands();
        Check(target_pressed && target_frame && !target_frame->failure() &&
                   target_overlay.size() == 3U && moved_position &&
                   target_overlay[0].texture ==
                       Access::DiagnosticActorMarkerTexture(*app) &&
                   target_overlay[1].texture ==
                       Access::DiagnosticActorMarkerTexture(*app) &&
                   target_overlay[2].texture ==
                       Access::DiagnosticActorMarkerTexture(*app) &&
                   target_overlay[0].destination == *kObjectiveDestination &&
                   target_overlay[1].destination !=
                       omega::app::PlanProjectDiagnosticActorMarkerDestination(
                           *moved_position) &&
                   target_overlay[2].destination !=
                       omega::app::PlanProjectDiagnosticActorMarkerDestination(
                           *moved_position) &&
                  DrawListsEqual(Access::CurrentFrontEndDrawList(*app),
                      Access::DiagnosticSceneOverlayDrawList(*app)),
            "held right mouse publishes the armed objective followed by two target cues above the actor mesh, never a duplicate actor marker");
        const bool chord_pressed = PushMouseButton(SDL_BUTTON_LEFT, true) &&
            Access::ArmNextRunElapsed(*app, std::chrono::nanoseconds::zero());
        auto chord_frame = app->RunWithCapture(1);
        const auto chord_overlay =
            Access::DiagnosticSceneOverlayDrawList(*app).commands();
        constexpr auto kExpectedTargetCues =
            omega::app::PlanProjectDiagnosticTargetCueRectangles(
                std::optional<omega::runtime::PointerPositionQ16>{
                    kQuarterThreeQuarterPointer});
        constexpr auto kExpectedFireCue =
            omega::app::PlanProjectDiagnosticFireCueRectangle(
                std::optional<omega::runtime::PointerPositionQ16>{
                    kQuarterThreeQuarterPointer});
        Check(chord_pressed && chord_frame && !chord_frame->failure() &&
                  chord_overlay.size() == 4U &&
                  chord_overlay[0].destination == *kObjectiveDestination &&
                  chord_overlay[1].destination == kExpectedTargetCues[0U] &&
                  chord_overlay[2].destination == kExpectedTargetCues[1U] &&
                  chord_overlay[3].destination == kExpectedFireCue,
            "the indexed scene overlay preserves objective, horizontal target, vertical target, then fire ordering at the fixed scratch maximum");
        Check(PushMouseButton(SDL_BUTTON_LEFT, false),
            "the indexed-scene chord fire edge releases");
        Check(PushMouseButton(SDL_BUTTON_RIGHT, false) && app->Run(1).has_value(),
            "the scene target cue releases without changing mesh ownership");

        const bool fire_pressed = PushMouseButton(SDL_BUTTON_LEFT, true) &&
            Access::ArmNextRunElapsed(*app, std::chrono::nanoseconds::zero());
        auto fire_frame = app->RunWithCapture(1);
        const auto fire_overlay =
            Access::DiagnosticSceneOverlayDrawList(*app).commands();
        Check(fire_pressed && fire_frame && !fire_frame->failure() &&
                   fire_overlay.size() == 2U && moved_position &&
                   fire_overlay[0].texture ==
                       Access::DiagnosticActorMarkerTexture(*app) &&
                   fire_overlay[1].texture ==
                       Access::DiagnosticActorMarkerTexture(*app) &&
                   fire_overlay[0].destination == *kObjectiveDestination &&
                   fire_overlay[1].destination !=
                       omega::app::PlanProjectDiagnosticActorMarkerDestination(
                           *moved_position),
            "left mouse fire publishes the armed objective before its project cue above the actor mesh");
        Check(PushMouseButton(SDL_BUTTON_LEFT, false) && app->Run(1).has_value(),
            "the scene fire cue releases without changing mesh ownership");

        const auto retained_camera = Access::DiagnosticSceneCamera(*app);
        omega::asset::SceneCameraIR dynamic_camera;
        dynamic_camera.world_to_view.row_major = {
            2.0F, 0.0F, 0.0F, 0.25F,
            0.0F, 3.0F, 0.0F, -0.5F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F,
        };
        dynamic_camera.view_to_clip.row_major = {
            1.0F, 0.5F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F,
        };
        const bool dynamic_refresh_ready = retained_camera && moved_position &&
            Access::DuplicateDiagnosticSceneEnvironmentCommand(*app) &&
            Access::SetDiagnosticSceneCamera(*app, dynamic_camera) &&
            Access::ArmNextRunElapsed(*app, std::chrono::nanoseconds::zero());
        const omega::runtime::RenderMeshDrawList before_dynamic_refresh =
            Access::CurrentFrontEndMeshDrawList(*app);
        const omega::app::GpuHostSnapshot dynamic_gpu_before =
            Access::GpuSnapshot(*app);
        auto dynamic_refresh = app->RunWithCapture(1);
        const omega::runtime::RenderMeshDrawList after_dynamic_refresh =
            Access::CurrentFrontEndMeshDrawList(*app);
        const omega::app::GpuHostSnapshot dynamic_gpu_after =
            Access::GpuSnapshot(*app);
        const auto dynamic_commands = after_dynamic_refresh.commands();
        const auto before_dynamic_commands = before_dynamic_refresh.commands();
        std::optional<omega::asset::Matrix4x4IR> expected_dynamic_actor;
        if (moved_position)
        {
            const auto composed = omega::runtime::ComposeObjectToClip(
                dynamic_camera,
                omega::app::PlanProjectDiagnosticActorMeshTransform(
                    *moved_position));
            if (composed)
                expected_dynamic_actor = *composed;
        }
        Check(dynamic_refresh_ready && dynamic_refresh &&
                  !dynamic_refresh->failure() && expected_dynamic_actor &&
                  before_dynamic_commands.size() == 3U &&
                  dynamic_commands.size() == 3U &&
                  dynamic_commands[0] == before_dynamic_commands[0] &&
                  dynamic_commands[1] == before_dynamic_commands[1] &&
                  dynamic_commands[2].mesh == before_dynamic_commands[2].mesh &&
                  dynamic_commands[2].color == before_dynamic_commands[2].color &&
                  dynamic_commands[2].raster_mode ==
                      before_dynamic_commands[2].raster_mode &&
                  dynamic_commands[2].object_to_clip == *expected_dynamic_actor &&
                  dynamic_gpu_after.successful_mesh_uploads ==
                      dynamic_gpu_before.successful_mesh_uploads &&
                  dynamic_gpu_after.successful_mesh_releases ==
                      dynamic_gpu_before.successful_mesh_releases &&
                  dynamic_gpu_after.successful_mesh_draws ==
                      dynamic_gpu_before.successful_mesh_draws + 3U,
            "refresh preserves a two-command environment prefix and composes the post-step actor through the retained camera");

        const bool restored_after_dynamic = retained_camera &&
            Access::SetDiagnosticSceneCamera(*app, *retained_camera) &&
            Access::ArmNextRunElapsed(*app, std::chrono::nanoseconds::zero()) &&
            app->Run(1).has_value();
        Check(restored_after_dynamic,
            "the dynamic camera refresh fixture restores the retained camera");

        const auto retained_actor_handle =
            Access::DiagnosticSceneActorMeshHandle(*app);
        const auto retained_environment_count =
            Access::DiagnosticSceneEnvironmentCommandCount(*app);
        const omega::runtime::RenderDrawList actor_draw_list_before_invalid =
            Access::DiagnosticActorDrawList(*app);
        const omega::runtime::RenderDrawList scene_overlay_before_invalid =
            Access::DiagnosticSceneOverlayDrawList(*app);
        const omega::runtime::RenderMeshDrawList scene_meshes_before_invalid =
            Access::DiagnosticSceneMeshDrawList(*app);
        const omega::app::GpuHostSnapshot invalid_state_gpu_before =
            Access::GpuSnapshot(*app);

        const bool invalid_actor_ready = retained_actor_handle &&
            Access::SetDiagnosticSceneActorMeshHandle(*app, {}) &&
            Access::ArmNextRunElapsed(*app, std::chrono::nanoseconds::zero());
        auto invalid_actor = app->RunWithCapture(1);
        const bool actor_handle_restored = retained_actor_handle &&
            Access::SetDiagnosticSceneActorMeshHandle(
                *app, *retained_actor_handle);
        Check(invalid_actor_ready && invalid_actor && actor_handle_restored &&
                  invalid_actor->completion() ==
                      omega::app::RunCaptureCompletion::OperationalFailure &&
                  invalid_actor->failure() == std::optional<std::string_view>{
                      "diagnostic scene draw-list creation failed: invalid-state"} &&
                  invalid_actor->result().rendered_frames == 0 &&
                  Access::GpuSnapshot(*app) == invalid_state_gpu_before &&
                  DrawListsEqual(Access::DiagnosticActorDrawList(*app),
                      actor_draw_list_before_invalid) &&
                  DrawListsEqual(Access::DiagnosticSceneOverlayDrawList(*app),
                      scene_overlay_before_invalid) &&
                  MeshDrawListsEqual(Access::DiagnosticSceneMeshDrawList(*app),
                      scene_meshes_before_invalid),
            "an invalid retained actor handle fails before rendering and preserves every published list");

        const bool invalid_count_ready = retained_environment_count &&
            Access::SetDiagnosticSceneEnvironmentCommandCount(
                *app, *retained_environment_count + 1U) &&
            Access::ArmNextRunElapsed(*app, std::chrono::nanoseconds::zero());
        auto invalid_count = app->RunWithCapture(1);
        const bool environment_count_restored = retained_environment_count &&
            Access::SetDiagnosticSceneEnvironmentCommandCount(
                *app, *retained_environment_count);
        Check(invalid_count_ready && invalid_count && environment_count_restored &&
                  invalid_count->completion() ==
                      omega::app::RunCaptureCompletion::OperationalFailure &&
                  invalid_count->failure() == std::optional<std::string_view>{
                      "diagnostic scene draw-list creation failed: invalid-state"} &&
                  invalid_count->result().rendered_frames == 0 &&
                  Access::GpuSnapshot(*app) == invalid_state_gpu_before &&
                  DrawListsEqual(Access::DiagnosticActorDrawList(*app),
                      actor_draw_list_before_invalid) &&
                  DrawListsEqual(Access::DiagnosticSceneOverlayDrawList(*app),
                      scene_overlay_before_invalid) &&
                  MeshDrawListsEqual(Access::DiagnosticSceneMeshDrawList(*app),
                      scene_meshes_before_invalid),
            "a mismatched retained environment count fails atomically before rendering");

        omega::asset::SceneCameraIR invalid_camera = retained_camera
            ? *retained_camera
            : omega::asset::SceneCameraIR{};
        invalid_camera.view_to_clip.row_major[0U] =
            std::numeric_limits<float>::infinity();
        const bool invalid_camera_ready = retained_camera &&
            Access::SetDiagnosticSceneCamera(*app, invalid_camera) &&
            Access::ArmNextRunElapsed(*app, std::chrono::nanoseconds::zero());
        auto invalid_camera_refresh = app->RunWithCapture(1);
        const bool camera_restored = retained_camera &&
            Access::SetDiagnosticSceneCamera(*app, *retained_camera);
        Check(invalid_camera_ready && invalid_camera_refresh && camera_restored &&
                  invalid_camera_refresh->completion() ==
                      omega::app::RunCaptureCompletion::OperationalFailure &&
                  invalid_camera_refresh->failure() ==
                      std::optional<std::string_view>{
                          "diagnostic scene transform is non-finite"} &&
                  invalid_camera_refresh->result().rendered_frames == 0 &&
                  Access::GpuSnapshot(*app) == invalid_state_gpu_before &&
                  DrawListsEqual(Access::DiagnosticActorDrawList(*app),
                      actor_draw_list_before_invalid) &&
                  DrawListsEqual(Access::DiagnosticSceneOverlayDrawList(*app),
                      scene_overlay_before_invalid) &&
                  MeshDrawListsEqual(Access::DiagnosticSceneMeshDrawList(*app),
                      scene_meshes_before_invalid),
            "a non-finite retained camera fails before rendering and preserves every published list");

        Check(Access::ArmNextRunElapsed(*app, std::chrono::nanoseconds::zero()) &&
                  app->Run(1).has_value(),
            "the restored retained-scene state resumes rendering after atomic failures");

        const omega::app::GpuHostSnapshot trigger_gpu_before =
            Access::GpuSnapshot(*app);
        const auto trigger_persistence_generation_before =
            Access::PersistenceGeneration(*app);
        const auto trigger_persistence_records_before =
            Access::PersistenceRecordCount(*app);
        const auto trigger_persistence_bytes_before =
            Access::PersistenceLogicalValueBytes(*app);
        constexpr omega::runtime::PointerPositionQ16 kExactTargetPointer{
            .x = 49'152U,
            .y = 32'768U,
        };
        constexpr auto kExactTargetCues =
            omega::app::PlanProjectDiagnosticTargetCueRectangles(
                std::optional<omega::runtime::PointerPositionQ16>{
                    kExactTargetPointer});
        constexpr auto kExactFireCue =
            omega::app::PlanProjectDiagnosticFireCueRectangle(
                std::optional<omega::runtime::PointerPositionQ16>{
                    kExactTargetPointer});
        const float exact_target_x =
            static_cast<float>(app_window->logical_width) * 3.0F / 4.0F;
        const float exact_target_y =
            static_cast<float>(app_window->logical_height) / 2.0F;

        constexpr std::size_t kTargetFlowFrameCount = 6U;
        std::optional<omega::runtime::RunCaptureSession>
            combined_target_flow_capture;
        std::size_t combined_target_flow_frame_count = 0U;
        const auto append_target_flow_capture =
            [&combined_target_flow_capture,
                &combined_target_flow_frame_count](
                std::expected<omega::app::RunCaptureOutcome,
                    std::string>& capture) -> bool {
            if (!capture || capture->failure())
                return false;
            const auto* const pair = capture->trace_pair();
            if (pair == nullptr || pair->input_trace().frame_count() != 1U ||
                pair->scheduler_elapsed_trace().frame_count() != 1U ||
                pair->terminal_input())
            {
                return false;
            }
            if (!combined_target_flow_capture)
            {
                auto created = omega::runtime::RunCaptureSession::Create(
                    omega::runtime::RunCaptureSessionConfig{
                        .maximum_frames = kTargetFlowFrameCount,
                        .first_frame_index =
                            pair->input_trace().first_frame_index(),
                    },
                    pair->input_trace().actions());
                if (!created)
                    return false;
                combined_target_flow_capture.emplace(std::move(*created));
            }

            auto traces = std::move(*capture).TakeTracePair();
            if (!traces)
                return false;
            auto reconstructed =
                omega::runtime::RunCaptureReplaySession::Create(
                    std::move(*traces));
            if (!reconstructed)
                return false;
            auto frame = reconstructed->Next();
            const auto elapsed = frame ? frame->elapsed() : std::nullopt;
            if (!frame || !elapsed || frame->terminal_input() ||
                !reconstructed->complete() ||
                reconstructed->remaining_frames() != 0U)
            {
                return false;
            }
            if (!combined_target_flow_capture->AppendInput(frame->input()) ||
                !combined_target_flow_capture->AppendElapsed(*elapsed))
            {
                return false;
            }
            ++combined_target_flow_frame_count;
            return true;
        };
        const bool crossing_ready =
            SameProximityTriggerState(
                Access::DiagnosticProximityTriggerState(*app), {}) &&
            Access::ArmNextRunElapsed(
                *app, settings.frame.simulation_step * 6) &&
            PushKey(SDL_SCANCODE_D, true) &&
            PushMouseButton(SDL_BUTTON_RIGHT, true, app_window->id,
                exact_target_x, exact_target_y) &&
            PushMouseButton(SDL_BUTTON_LEFT, true, app_window->id,
                exact_target_x, exact_target_y);
        auto crossing = app->RunWithCapture(1);
        const auto crossed_position = Access::DebugLocomotionPosition(*app);
        const auto crossed_state =
            Access::DiagnosticProximityTriggerState(*app);
        const auto crossed_target_action = CapturedActionState(
            crossing, omega::app::kDebugTargetAction);
        const auto crossed_fire_action = CapturedActionState(
            crossing, omega::app::kDebugFireAction);
        const auto* crossing_pair = crossing ? crossing->trace_pair() : nullptr;
        const auto crossing_pointer = crossing_pair != nullptr
            ? crossing_pair->input_trace().PointerAt(0U)
            : std::nullopt;
        const auto crossed_actor_commands =
            Access::DiagnosticActorDrawList(*app).commands();
        const auto crossed_overlay_commands =
            Access::DiagnosticSceneOverlayDrawList(*app).commands();
        const omega::app::GpuHostSnapshot trigger_gpu_after =
            Access::GpuSnapshot(*app);
        const auto crossing_elapsed = crossing_pair != nullptr
            ? crossing_pair->scheduler_elapsed_trace().FrameAt(0U)
            : std::nullopt;
        Check(crossing_ready && crossing && !crossing->failure() &&
                  crossing_pair != nullptr && crossing_elapsed &&
                  crossing_elapsed->elapsed == settings.frame.simulation_step * 6 &&
                  crossing_pointer == kExactTargetPointer &&
                  IsFreshPress(crossed_target_action) &&
                  IsFreshPress(crossed_fire_action) &&
                  crossing->result().planned_simulation_steps == 6U &&
                  crossing->result().executed_simulation_steps == 6U &&
                  crossed_position &&
                  *crossed_position ==
                      omega::simulation::Position3{.x = 6, .z = 1} &&
                  !crossed_state.inside && crossed_state.objective_complete &&
                  Access::DiagnosticTargetFireState(*app) ==
                      omega::gameplay::DiagnosticTargetFireState{} &&
                  crossed_actor_commands.size() == 6U &&
                  crossed_actor_commands[2].destination ==
                      *kTargetDestination &&
                  crossed_actor_commands[3].destination ==
                      kExactTargetCues[0U] &&
                  crossed_actor_commands[4].destination ==
                      kExactTargetCues[1U] &&
                  crossed_actor_commands[5].destination == kExactFireCue &&
                  crossed_overlay_commands.size() == 4U &&
                  crossed_overlay_commands[0].destination ==
                      *kTargetDestination &&
                  crossed_overlay_commands[1].destination ==
                      kExactTargetCues[0U] &&
                  crossed_overlay_commands[2].destination ==
                      kExactTargetCues[1U] &&
                  crossed_overlay_commands[3].destination == kExactFireCue &&
                  DrawListsEqual(Access::CurrentFrontEndDrawList(*app),
                      Access::DiagnosticSceneOverlayDrawList(*app)) &&
                  SameTextureResidency(trigger_gpu_before, trigger_gpu_after) &&
                  trigger_gpu_after.meshes == trigger_gpu_before.meshes &&
                  trigger_gpu_after.successful_mesh_uploads ==
                      trigger_gpu_before.successful_mesh_uploads &&
                  trigger_gpu_after.successful_mesh_releases ==
                      trigger_gpu_before.successful_mesh_releases &&
                  Access::PersistenceGeneration(*app) ==
                      trigger_persistence_generation_before &&
                  Access::PersistenceRecordCount(*app) ==
                      trigger_persistence_records_before &&
                  Access::PersistenceLogicalValueBytes(*app) ==
                      trigger_persistence_bytes_before,
            "one exact aimed-fire input frame enters and exits the fixed volume in six steps, remains ineligible against frame-start proximity, and publishes target, bars, then fire without resource or persistence mutation");
        Check(append_target_flow_capture(crossing),
            "the exact crossing capture starts one contiguous target-flow trace");

        const bool crossing_release_queued =
            PushKey(SDL_SCANCODE_D, false) &&
            PushMouseButton(SDL_BUTTON_LEFT, false, app_window->id,
                exact_target_x, exact_target_y) &&
            PushMouseButton(SDL_BUTTON_RIGHT, false, app_window->id,
                exact_target_x, exact_target_y) &&
            Access::ArmNextRunElapsed(
                *app, std::chrono::nanoseconds::zero());
        auto crossing_release = app->RunWithCapture(1);
        const auto crossing_release_overlay =
            Access::DiagnosticSceneOverlayDrawList(*app).commands();
        Check(crossing_release_queued && crossing_release &&
                  !crossing_release->failure() &&
                  crossing_release->result().planned_simulation_steps == 0U &&
                  crossing_release->result().executed_simulation_steps == 0U &&
                  Access::DiagnosticTargetFireState(*app) ==
                      omega::gameplay::DiagnosticTargetFireState{} &&
                  crossing_release_overlay.size() == 1U &&
                  crossing_release_overlay[0].destination ==
                      *kTargetDestination,
            "releasing the gated crossing chord leaves the ready target visible on the next zero-step input frame");
        Check(append_target_flow_capture(crossing_release),
            "the crossing release remains contiguous in the target-flow trace");

        const omega::app::GpuHostSnapshot target_flow_gpu_before =
            Access::GpuSnapshot(*app);
        const bool miss_queued =
            PushMouseButton(SDL_BUTTON_RIGHT, true, app_window->id,
                pointer_x, pointer_y) &&
            PushMouseButton(SDL_BUTTON_LEFT, true, app_window->id,
                pointer_x, pointer_y) &&
            Access::ArmNextRunElapsed(
                *app, std::chrono::nanoseconds::zero());
        auto miss = app->RunWithCapture(1);
        const auto* miss_pair = miss ? miss->trace_pair() : nullptr;
        const auto miss_pointer = miss_pair != nullptr
            ? miss_pair->input_trace().PointerAt(0U)
            : std::nullopt;
        const auto miss_overlay =
            Access::DiagnosticSceneOverlayDrawList(*app).commands();
        Check(miss_queued && miss && !miss->failure() &&
                  miss->result().planned_simulation_steps == 0U &&
                  miss->result().executed_simulation_steps == 0U &&
                  miss_pointer == kQuarterThreeQuarterPointer &&
                  Access::DiagnosticTargetFireState(*app) ==
                      omega::gameplay::DiagnosticTargetFireState{} &&
                  miss_overlay.size() == 4U &&
                  miss_overlay[0].destination == *kTargetDestination &&
                  miss_overlay[1].destination == kExpectedTargetCues[0U] &&
                  miss_overlay[2].destination == kExpectedTargetCues[1U] &&
                  miss_overlay[3].destination == kExpectedFireCue &&
                  SameTextureResidency(target_flow_gpu_before,
                      Access::GpuSnapshot(*app)) &&
                  Access::GpuSnapshot(*app).meshes ==
                      target_flow_gpu_before.meshes &&
                  Access::PersistenceGeneration(*app) ==
                      trigger_persistence_generation_before &&
                  Access::PersistenceRecordCount(*app) ==
                      trigger_persistence_records_before &&
                  Access::PersistenceLogicalValueBytes(*app) ==
                      trigger_persistence_bytes_before,
            "an off-target indexed aimed-fire attempt retains target, bars, and fire in exact overlay order without simulation, residency, or persistence mutation");
        Check(append_target_flow_capture(miss),
            "the off-target miss remains contiguous in the target-flow trace");

        const bool miss_release_queued =
            PushMouseButton(SDL_BUTTON_LEFT, false, app_window->id,
                pointer_x, pointer_y) &&
            PushMouseButton(SDL_BUTTON_RIGHT, false, app_window->id,
                pointer_x, pointer_y) &&
            Access::ArmNextRunElapsed(
                *app, std::chrono::nanoseconds::zero());
        auto miss_release = app->RunWithCapture(1);
        Check(miss_release_queued && miss_release &&
                  !miss_release->failure() &&
                  miss_release->result().planned_simulation_steps == 0U &&
                  miss_release->result().executed_simulation_steps == 0U &&
                  Access::DiagnosticTargetFireState(*app) ==
                      omega::gameplay::DiagnosticTargetFireState{} &&
                  Access::DiagnosticSceneOverlayDrawList(*app).commands().size() ==
                      1U &&
                  Access::DiagnosticSceneOverlayDrawList(*app)
                          .commands()[0]
                          .destination == *kTargetDestination,
            "releasing the indexed miss chord leaves the ready target visible on a zero-step frame");
        Check(append_target_flow_capture(miss_release),
            "the miss release remains contiguous in the target-flow trace");

        const bool hit_queued =
            PushMouseButton(SDL_BUTTON_RIGHT, true, app_window->id,
                exact_target_x, exact_target_y) &&
            PushMouseButton(SDL_BUTTON_LEFT, true, app_window->id,
                exact_target_x, exact_target_y) &&
            Access::ArmNextRunElapsed(
                *app, std::chrono::nanoseconds::zero());
        auto hit = app->RunWithCapture(1);
        const auto* hit_pair = hit ? hit->trace_pair() : nullptr;
        const auto hit_pointer = hit_pair != nullptr
            ? hit_pair->input_trace().PointerAt(0U)
            : std::nullopt;
        Check(hit_queued && hit && !hit->failure() &&
                  hit->result().planned_simulation_steps == 0U &&
                  hit->result().executed_simulation_steps == 0U &&
                  hit_pointer == kExactTargetPointer &&
                  Access::FrontEnd(*app).mode ==
                      omega::app::FrontEndMode::BriefingRoom &&
                  Access::DiagnosticTargetFireState(*app) ==
                      omega::gameplay::DiagnosticTargetFireState{
                          .target_complete = true} &&
                  Access::DiagnosticMissionLifecycleState(*app).status ==
                      omega::gameplay::DiagnosticMissionStatus::Succeeded &&
                  Access::CurrentFrontEndMeshDrawList(*app).empty() &&
                  SameTextureResidency(target_flow_gpu_before,
                      Access::GpuSnapshot(*app)) &&
                  Access::GpuSnapshot(*app).meshes ==
                      target_flow_gpu_before.meshes &&
                  Access::PersistenceGeneration(*app) ==
                      trigger_persistence_generation_before &&
                  Access::PersistenceRecordCount(*app) ==
                      trigger_persistence_records_before &&
                  Access::PersistenceLogicalValueBytes(*app) ==
                      trigger_persistence_bytes_before,
            "an exact indexed RMB plus LMB hit completes the synthetic mission and returns to BriefingRoom in the same rendered frame without resource or persistence mutation");
        Check(append_target_flow_capture(hit),
            "the exact completed hit remains contiguous in the target-flow trace");

        const bool hit_release_queued =
            PushMouseButton(SDL_BUTTON_LEFT, false, app_window->id,
                exact_target_x, exact_target_y) &&
            PushMouseButton(SDL_BUTTON_RIGHT, false, app_window->id,
                exact_target_x, exact_target_y) &&
            Access::ArmNextRunElapsed(
                *app, std::chrono::nanoseconds::zero());
        auto hit_release = app->RunWithCapture(1);
        const auto live_completed_proximity =
            Access::DiagnosticProximityTriggerState(*app);
        const auto live_completed_target =
            Access::DiagnosticTargetFireState(*app);
        Check(hit_release_queued && hit_release &&
                  !hit_release->failure() &&
                  hit_release->result().planned_simulation_steps == 0U &&
                  hit_release->result().executed_simulation_steps == 0U &&
                  SameProximityTriggerState(
                      live_completed_proximity,
                      {.inside = false, .objective_complete = true}) &&
                  live_completed_target ==
                      omega::gameplay::DiagnosticTargetFireState{
                          .target_complete = true} &&
                  Access::DiagnosticMissionLifecycleState(*app).status ==
                      omega::gameplay::DiagnosticMissionStatus::Succeeded &&
                  Access::FrontEnd(*app).mode ==
                      omega::app::FrontEndMode::BriefingRoom &&
                  Access::CurrentFrontEndMeshDrawList(*app).empty(),
            "the zero-step hit release preserves the completed diagnostic result in BriefingRoom");
        Check(append_target_flow_capture(hit_release),
            "the hit release completes the contiguous target-flow trace");

        std::optional<omega::runtime::RunCaptureTracePair>
            combined_target_flow_traces;
        if (combined_target_flow_capture &&
            combined_target_flow_frame_count == kTargetFlowFrameCount)
        {
            auto finished =
                std::move(*combined_target_flow_capture).Finish();
            if (finished)
                combined_target_flow_traces.emplace(std::move(*finished));
        }
        bool completed_target_replay_matches = false;
        if (combined_target_flow_traces)
        {
            auto replay = omega::app::RunReplaySession::Create(
                std::move(*combined_target_flow_traces),
                omega::app::RunReplaySessionConfig{
                    .scheduler = settings.frame,
                    .enable_debug_locomotion = true,
                    .enable_debug_target_fire = true,
                });
            if (replay)
            {
                bool frames_match = true;
                std::size_t replayed_frames = 0U;
                while (replay->state() ==
                       omega::app::RunReplaySessionState::Ready)
                {
                    auto frame = replay->Next();
                    const auto plan = frame ? frame->frame_plan() : std::nullopt;
                    const std::uint32_t expected_steps =
                        replayed_frames == 0U ? 6U : 0U;
                    if (!frame || !plan ||
                        plan->simulation_steps != expected_steps)
                    {
                        frames_match = false;
                        break;
                    }
                    ++replayed_frames;
                }
                const auto replayed_proximity =
                    replay->diagnostic_proximity_trigger_state();
                const auto replayed_target =
                    replay->diagnostic_target_fire_state();
                completed_target_replay_matches = frames_match &&
                    replayed_frames == kTargetFlowFrameCount &&
                    replay->state() ==
                        omega::app::RunReplaySessionState::Complete &&
                    replay->remaining_frames() == 0U &&
                    replayed_proximity && replayed_target &&
                    SameProximityTriggerState(
                        *replayed_proximity, live_completed_proximity) &&
                    *replayed_target == live_completed_target;
            }
        }
        Check(completed_target_replay_matches,
            "one fresh replay of the exact contiguous live crossing, releases, miss, and hit reproduces the completed proximity and target states");

        const omega::app::GpuHostSnapshot round_trip_gpu_before =
            Access::GpuSnapshot(*app);
        const bool reentered =
            Access::ArmNextRunElapsed(
                *app, std::chrono::nanoseconds::zero()) &&
            PushKey(SDL_SCANCODE_F1, true);
        auto round_trip = app->RunWithCapture(1);
        const omega::app::GpuHostSnapshot round_trip_gpu_after =
            Access::GpuSnapshot(*app);
        Check(reentered && round_trip && !round_trip->failure() &&
                  round_trip->result().planned_simulation_steps == 0U &&
                  round_trip->result().executed_simulation_steps == 0U &&
                  Access::FrontEnd(*app).mode ==
                      omega::app::FrontEndMode::DiagnosticPlay &&
                  SameProximityTriggerState(
                      Access::DiagnosticProximityTriggerState(*app), {}) &&
                  Access::DiagnosticTargetFireState(*app) ==
                      omega::gameplay::DiagnosticTargetFireState{} &&
                  Access::DiagnosticMissionLifecycleState(*app).status ==
                      omega::gameplay::DiagnosticMissionStatus::Active &&
                  Access::DebugLocomotionPosition(*app) ==
                      std::optional<omega::simulation::Position3>{
                          omega::simulation::Position3{}} &&
                  Access::DiagnosticSceneOverlayDrawList(*app).commands().size() ==
                      1U &&
                  Access::DiagnosticSceneOverlayDrawList(*app)
                          .commands()[0]
                          .destination == *kObjectiveDestination &&
                  DrawListsEqual(Access::CurrentFrontEndDrawList(*app),
                      Access::DiagnosticSceneOverlayDrawList(*app)) &&
                  SameTextureResidency(
                      round_trip_gpu_before, round_trip_gpu_after) &&
                  round_trip_gpu_after.meshes == round_trip_gpu_before.meshes,
            "a zero-step BriefingRoom redeploy resets mission, actor, objective, and target state without leaking the select key into gameplay or uploading resources");
        Check(PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value() &&
                  PushKey(SDL_SCANCODE_F1, true) && app->Run(1).has_value() &&
                  Access::FrontEnd(*app).mode ==
                      omega::app::FrontEndMode::BriefingRoom &&
                  SameProximityTriggerState(
                      Access::DiagnosticProximityTriggerState(*app), {}) &&
                  Access::DiagnosticTargetFireState(*app) ==
                      omega::gameplay::DiagnosticTargetFireState{} &&
                  Access::DiagnosticMissionLifecycleState(*app).status ==
                      omega::gameplay::DiagnosticMissionStatus::Failed &&
                  Access::DebugLocomotionPosition(*app) ==
                      std::optional<omega::simulation::Position3>{
                          omega::simulation::Position3{}} &&
                  PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value(),
            "an explicit DiagnosticPlay primary abort records failure and returns to BriefingRoom without changing the freshly reset gameplay state");

        const omega::app::GpuHostSnapshot mouse_redeploy_gpu_before =
            Access::GpuSnapshot(*app);
        const bool mouse_redeploy_queued =
            PushMouseButton(SDL_BUTTON_LEFT, true, app_window->id,
                exact_target_x, exact_target_y) &&
            Access::ArmNextRunElapsed(
                *app, std::chrono::nanoseconds::zero());
        auto mouse_redeploy = app->RunWithCapture(1);
        const omega::app::GpuHostSnapshot mouse_redeploy_gpu_after =
            Access::GpuSnapshot(*app);
        Check(mouse_redeploy_queued && mouse_redeploy &&
                  !mouse_redeploy->failure() &&
                  mouse_redeploy->result().planned_simulation_steps == 0U &&
                  mouse_redeploy->result().executed_simulation_steps == 0U &&
                  Access::FrontEnd(*app).mode ==
                      omega::app::FrontEndMode::DiagnosticPlay &&
                  Access::DiagnosticMissionLifecycleState(*app).status ==
                      omega::gameplay::DiagnosticMissionStatus::Active &&
                  SameProximityTriggerState(
                      Access::DiagnosticProximityTriggerState(*app), {}) &&
                  Access::DiagnosticTargetFireState(*app) ==
                      omega::gameplay::DiagnosticTargetFireState{} &&
                  Access::DebugLocomotionPosition(*app) ==
                      std::optional<omega::simulation::Position3>{
                          omega::simulation::Position3{}} &&
                  SameTextureResidency(
                      mouse_redeploy_gpu_before, mouse_redeploy_gpu_after) &&
                  mouse_redeploy_gpu_after.meshes ==
                      mouse_redeploy_gpu_before.meshes,
            "a BriefingRoom left click redeploys after failure without becoming a same-frame fire attempt or reallocating GPU resources");
        Check(PushMouseButton(SDL_BUTTON_LEFT, false, app_window->id,
                  exact_target_x, exact_target_y) &&
                  app->Run(1).has_value(),
            "the mouse-first redeploy edge releases in DiagnosticPlay");

        const omega::app::GpuHostSnapshot before_release = Access::GpuSnapshot(*app);
        Access::ReleaseDiagnosticScenePresentation(*app);
        const omega::app::GpuHostSnapshot after_release = Access::GpuSnapshot(*app);
        Check(after_release.successful_mesh_releases ==
                  before_release.successful_mesh_releases + 2U &&
                  after_release.meshes.free_slots ==
                      after_release.meshes.slot_capacity &&
                  after_release.meshes.reserved_slots == 0U &&
                  after_release.meshes.resident_slots == 0U &&
                  after_release.meshes.resident_positions == 0U &&
                  after_release.meshes.resident_triangle_indices == 0U &&
                  after_release.meshes.resident_logical_bytes == 0U &&
                  Access::DiagnosticSceneMeshDrawList(*app).empty() &&
                  Access::CurrentFrontEndMeshDrawList(*app).empty(),
            "explicit scene teardown clears commands before releasing actor then environment generations");

        auto reused_environment_slot = Access::UploadRenderMesh(
            *app, MakePresentationTriangle(0.0F));
        const bool reused_expected_slot = reused_environment_slot &&
            initial_environment_mesh_handle.valid() &&
            reused_environment_slot->pool_identity ==
                initial_environment_mesh_handle.pool_identity &&
            reused_environment_slot->slot_index ==
                initial_environment_mesh_handle.slot_index &&
            reused_environment_slot->generation ==
                initial_environment_mesh_handle.generation + 1U;
        auto released_probe = reused_environment_slot
            ? Access::ReleaseRenderMesh(*app, *reused_environment_slot)
            : std::expected<void, std::string>{
                  std::unexpected("the release-order probe did not upload")};
        const omega::app::GpuHostSnapshot after_probe = Access::GpuSnapshot(*app);
        Check(reused_expected_slot && released_probe &&
                  after_probe.meshes.free_slots ==
                      after_probe.meshes.slot_capacity &&
                  after_probe.meshes.reserved_slots == 0U &&
                  after_probe.meshes.resident_slots == 0U &&
                  after_probe.meshes.resident_positions == 0U &&
                  after_probe.meshes.resident_triangle_indices == 0U &&
                  after_probe.meshes.resident_logical_bytes == 0U,
            "production teardown releases actor first then environment so the environment slot is recycled next");
    }

    auto invalid_config = omega::runtime::ParseConfigText("");
    auto invalid_content = BuildLevelContentStartupState(tree);
    Check(invalid_config && invalid_content,
        "the non-finite diagnostic-scene rejection fixture is ready");
    if (invalid_config && invalid_content)
    {
        InstallSyntheticSpatialTriangle(*invalid_content);
        invalid_content->level_content->spatial.terrain_cells[0].vertices[0].x =
            std::numeric_limits<float>::infinity();
        const SDL_InitFlags before = SDL_WasInit(0);
        auto rejected = Access::Create(std::move(*invalid_config), settings,
            std::move(*invalid_content), false);
        constexpr std::string_view exact_error =
            "spatial diagnostic scene: spatial diagnostic scene requires finite vertex coordinates";
        Check(!rejected && rejected.error() == exact_error &&
                  rejected.error().find(tree.root().string()) == std::string::npos &&
                  SDL_WasInit(0) == before,
            "invalid spatial geometry fails closed with a fixed path-free diagnostic before SDL startup");
    }
}

void CheckExplicitFirstProfileCreation(
    const std::filesystem::path& fixture_root,
    const omega::runtime::RuntimeSettings& settings)
{
    using Access = omega::app::detail::OmegaAppTestAccess;
    constexpr std::uint64_t kCreationTimestamp = 1'725'000'000'123ULL;
    const auto first_profile_id = omega::profiles::ProfileId::Parse(
        "00000000000000000000000000000001");
    const auto first_character_id = omega::profiles::CharacterId::Parse(
        "00000000000000000000000000000001");
    Check(first_profile_id.has_value() && first_character_id.has_value(),
        "the project-owned first-profile and first-character IDs parse");
    if (!first_profile_id || !first_character_id)
        return;

    const std::filesystem::path creation_root =
        fixture_root / "native-explicit-first-profile";
    auto persistence = omega::app::NativePersistence::Bootstrap(creation_root);
    auto config = omega::runtime::ParseConfigText("");
    std::optional<std::vector<omega::profiles::ProfileSummary>> initial_profiles;
    if (persistence)
    {
        auto listed = persistence->profiles().List();
        if (listed)
            initial_profiles = std::move(*listed);
    }
    Check(persistence && config && persistence->startup_profiles().empty() &&
              initial_profiles && initial_profiles->empty(),
        "a fresh temporary native-persistence database starts with an exact empty catalog");

    if (persistence && config && initial_profiles && initial_profiles->empty())
    {
        auto app = Access::CreateWithPersistence(std::move(*config), settings,
            omega::runtime::ContentStartupState{}, std::move(*persistence), false);
        Check(app.has_value(),
            "an empty durable catalog starts with its first-profile presentation preloaded");
        if (app)
        {
            constexpr std::uint64_t kFrontEndCardLogicalBytes =
                128ULL * 72ULL * 4ULL;
            constexpr std::uint64_t kTopologyLogicalBytes = 96ULL * 32ULL * 4ULL;
            constexpr std::uint64_t kPreloadedLogicalBytes =
                kFrontEndCardLogicalBytes * 6ULL + kTopologyLogicalBytes + 4ULL;
            constexpr omega::app::FrontEndState kProfilesFirst{
                .mode = omega::app::FrontEndMode::Profiles,
                .selected_main_row = omega::app::FrontEndMainRow::Profiles,
                .selected_profile_slot = omega::app::FrontEndProfileSlot::First,
            };
            const auto initial_model = Access::FrontEndModel(*app);
            const auto initial_gpu = Access::GpuSnapshot(*app);
            const std::array initial_active_textures{
                Access::FrontEndTexture(*app),
                Access::FrontEndProfilesTexture(*app),
            };
            const auto initial_inactive_textures =
                Access::InactiveFrontEndTextures(*app);
            const bool preload_handles_are_distinct =
                initial_inactive_textures && initial_active_textures[0].valid() &&
                initial_active_textures[1].valid() &&
                (*initial_inactive_textures)[0].valid() &&
                (*initial_inactive_textures)[1].valid() &&
                initial_active_textures[0] != initial_active_textures[1] &&
                initial_active_textures[0] != (*initial_inactive_textures)[0] &&
                initial_active_textures[0] != (*initial_inactive_textures)[1] &&
                initial_active_textures[1] != (*initial_inactive_textures)[0] &&
                initial_active_textures[1] != (*initial_inactive_textures)[1] &&
                (*initial_inactive_textures)[0] != (*initial_inactive_textures)[1];
            Check(initial_model == omega::app::FrontEndStartupModel{} &&
                      Access::CanCreateFirstProfile(*app) &&
                      Access::FrontEnd(*app) == kProfilesFirst &&
                      !Access::ActiveProfile(*app) &&
                      Access::ProfileCatalogCount(*app) ==
                          std::optional<std::size_t>{0U} &&
                      preload_handles_are_distinct &&
                      initial_gpu.successful_uploads == 8U &&
                      initial_gpu.successful_upload_logical_bytes ==
                          kPreloadedLogicalBytes &&
                      initial_gpu.textures.resident_slots == 8U &&
                      initial_gpu.textures.resident_logical_bytes ==
                          kPreloadedLogicalBytes &&
                      DrawListsEqual(Access::CurrentFrontEndDrawList(*app),
                          Access::FrontEndProfilesDrawList(*app)),
                "exact-empty startup opens Profiles with distinct complete empty and one-profile GPU presentations before mutation");

            const auto run_plain_frame = [&app]() {
                auto run = app->Run(1);
                return run && run->input_frames == 1U &&
                       run->rendered_frames == 1 && !run->quit_requested;
            };

            const bool timestamp_armed = Access::ArmFirstProfileTimestamp(
                *app, kCreationTimestamp);
            const auto gpu_before_create = Access::GpuSnapshot(*app);
            const std::array active_textures_before_create{
                Access::FrontEndTexture(*app),
                Access::FrontEndProfilesTexture(*app),
            };
            const auto inactive_textures_before_create =
                Access::InactiveFrontEndTextures(*app);
            const auto scheduler_before_create =
                Access::SchedulerSnapshot(*app);
            const auto simulation_before_create =
                Access::SimulationSnapshot(*app);
            const bool creation_queued = PushKey(SDL_SCANCODE_F1, true);
            auto creation_capture = app->RunWithCapture(1);
            const auto gpu_after_create = Access::GpuSnapshot(*app);
            const auto simulation_after_create =
                Access::SimulationSnapshot(*app);
            const auto created_model = Access::FrontEndModel(*app);
            const auto created_record = Access::ReadProfile(*app, *first_profile_id);
            const std::array active_textures_after_create{
                Access::FrontEndTexture(*app),
                Access::FrontEndProfilesTexture(*app),
            };
            const auto inactive_textures_after_create =
                Access::InactiveFrontEndTextures(*app);
            const bool presentations_swapped =
                inactive_textures_before_create && inactive_textures_after_create &&
                active_textures_after_create == *inactive_textures_before_create &&
                *inactive_textures_after_create == active_textures_before_create;
            const std::string_view projected_name{
                created_model.profiles[0].label.cells.data(),
                created_model.profiles[0].label.length};
            Check(timestamp_armed && creation_queued && creation_capture &&
                      creation_capture->completion() ==
                          omega::app::RunCaptureCompletion::FrameLimitReached &&
                      creation_capture->result().input_frames == 1U &&
                      creation_capture->result().rendered_frames == 1 &&
                      creation_capture->result().planned_simulation_steps == 0U &&
                      creation_capture->result().executed_simulation_steps == 0U &&
                      creation_capture->scheduler_state_before() ==
                          scheduler_before_create &&
                      creation_capture->scheduler_state_after() ==
                          scheduler_before_create &&
                      Access::SchedulerSnapshot(*app) == scheduler_before_create &&
                      SameSimulationState(
                          simulation_before_create, simulation_after_create) &&
                      Access::FrontEnd(*app) == kProfilesFirst &&
                      !Access::CanCreateFirstProfile(*app) &&
                      !Access::ActiveProfile(*app) &&
                      Access::ProfileCatalogCount(*app) ==
                          std::optional<std::size_t>{1U} &&
                      created_model.total_profiles == 1U &&
                      created_model.visible_profiles == 1U &&
                      created_model.profiles[0].id == *first_profile_id &&
                      projected_name == omega::app::kFrontEndFirstProfileDisplayName &&
                      created_record && created_record->id == *first_profile_id &&
                      created_record->metadata.display_name ==
                          omega::app::kFrontEndFirstProfileDisplayName &&
                      created_record->metadata.created_unix_milliseconds ==
                          kCreationTimestamp &&
                      created_record->metadata.modified_unix_milliseconds ==
                          kCreationTimestamp &&
                      created_record->metadata_revision == 1U &&
                      presentations_swapped &&
                      IsOneVisibleMenuSubmission(
                          gpu_before_create, gpu_after_create),
                "captured Primary creates one PROFILE 1 durably, stays modal with frozen simulation, swaps the preloaded presentation, and performs no command-time GPU allocation");

            const bool release_frame = PushKey(SDL_SCANCODE_F1, false) &&
                                       run_plain_frame();
            Check(release_frame && Access::FrontEnd(*app) == kProfilesFirst &&
                      !Access::ActiveProfile(*app),
                "the creation key release is inert and leaves the new profile unselected");
            const auto gpu_before_selection = Access::GpuSnapshot(*app);
            const bool selection_frame = PushKey(SDL_SCANCODE_F1, true) &&
                                         run_plain_frame();
            constexpr omega::app::FrontEndState kCharactersFirst{
                .mode = omega::app::FrontEndMode::Characters,
                .selected_main_row = omega::app::FrontEndMainRow::Profiles,
                .selected_profile_slot = omega::app::FrontEndProfileSlot::First,
                .selected_character_slot =
                    omega::app::FrontEndCharacterSlot::First,
            };
            Check(selection_frame &&
                       Access::ActiveProfile(*app) == *first_profile_id &&
                       Access::PersistedConfirmedProfile(*app) == *first_profile_id &&
                       Access::PersistenceGeneration(*app) ==
                           std::optional<std::uint64_t>{2U} &&
                       Access::PersistenceRecordCount(*app) ==
                           std::optional<std::size_t>{2U} &&
                       Access::PersistenceLogicalValueBytes(*app) ==
                           std::optional<std::size_t>{73U} &&
                       Access::FrontEnd(*app) == kCharactersFirst &&
                       Access::ProfileCatalogCount(*app) ==
                           std::optional<std::size_t>{1U} &&
                       Access::CharacterCatalogCount(*app, *first_profile_id) ==
                           std::optional<std::size_t>{0U} &&
                       !Access::ActiveCharacter(*app) &&
                       !Access::PersistedConfirmedCharacter(*app) &&
                       IsOneCharacterMenuSubmissionWithTextureDelta(
                           gpu_before_selection, Access::GpuSnapshot(*app),
                           2U, 0U, 2U),
                "a second Primary confirms the durable first profile, enters empty Characters, and stages both character cards");
            Check(PushKey(SDL_SCANCODE_F1, false) && run_plain_frame(),
                "the explicit first-profile selection key releases before character creation");

            const auto gpu_before_character_create = Access::GpuSnapshot(*app);
            const bool character_creation_frame =
                PushKey(SDL_SCANCODE_F1, true) && run_plain_frame();
            const auto created_character = Access::ReadCharacter(
                *app, *first_profile_id, *first_character_id);
            Check(character_creation_frame &&
                      Access::FrontEnd(*app) == kCharactersFirst &&
                      Access::CharacterCatalogCount(*app, *first_profile_id) ==
                          std::optional<std::size_t>{1U} &&
                      created_character &&
                      created_character->id == *first_character_id &&
                      created_character->metadata.display_name ==
                          omega::app::kFrontEndFirstCharacterDisplayName &&
                      created_character->metadata.created_unix_milliseconds ==
                          created_character->metadata.modified_unix_milliseconds &&
                      created_character->metadata_revision == 3U &&
                      !Access::ActiveCharacter(*app) &&
                      !Access::PersistedConfirmedCharacter(*app) &&
                      Access::PersistenceGeneration(*app) ==
                          std::optional<std::uint64_t>{3U} &&
                      Access::PersistenceRecordCount(*app) ==
                          std::optional<std::size_t>{3U} &&
                      Access::PersistenceLogicalValueBytes(*app) ==
                          std::optional<std::size_t>{125U} &&
                      IsOneCharacterMenuSubmissionWithTextureDelta(
                          gpu_before_character_create, Access::GpuSnapshot(*app),
                          0U, 1U, 3U),
                "Primary creates the fixed diagnostic character, keeps Characters modal, and releases the obsolete empty card");
            Check(PushKey(SDL_SCANCODE_F1, false) && run_plain_frame(),
                "the explicit first-character creation key releases");

            const auto gpu_before_character_select = Access::GpuSnapshot(*app);
            const bool character_selection_frame =
                PushKey(SDL_SCANCODE_F1, true) && run_plain_frame();
            constexpr omega::app::FrontEndState kBriefingRoom{
                .mode = omega::app::FrontEndMode::BriefingRoom,
                .selected_main_row =
                    omega::app::FrontEndMainRow::StartDiagnostic,
                .selected_profile_slot =
                    omega::app::FrontEndProfileSlot::First,
                .selected_character_slot =
                    omega::app::FrontEndCharacterSlot::First,
            };
            Check(character_selection_frame &&
                      Access::FrontEnd(*app) == kBriefingRoom &&
                      Access::ActiveCharacter(*app) == *first_character_id &&
                      Access::PersistedConfirmedCharacter(*app) ==
                          *first_character_id &&
                      Access::PersistenceGeneration(*app) ==
                          std::optional<std::uint64_t>{4U} &&
                      Access::PersistenceRecordCount(*app) ==
                          std::optional<std::size_t>{4U} &&
                      Access::PersistenceLogicalValueBytes(*app) ==
                          std::optional<std::size_t>{173U} &&
                      IsOneVisibleMenuSubmission(
                          gpu_before_character_select, Access::GpuSnapshot(*app)),
                "Primary confirms the first character and enters the Briefing Room mission selector");
            Check(PushKey(SDL_SCANCODE_F1, false) && run_plain_frame(),
                "the explicit first-character selection key releases");

            const auto gpu_before_start = Access::GpuSnapshot(*app);
            const bool start_frame =
                PushKey(SDL_SCANCODE_F1, true) && run_plain_frame();
            constexpr omega::app::FrontEndState kDiagnosticPlay{
                .mode = omega::app::FrontEndMode::DiagnosticPlay,
                .selected_main_row =
                    omega::app::FrontEndMainRow::StartDiagnostic,
                .selected_profile_slot =
                    omega::app::FrontEndProfileSlot::First,
                .selected_character_slot =
                    omega::app::FrontEndCharacterSlot::First,
            };
            Check(start_frame && Access::FrontEnd(*app) == kDiagnosticPlay &&
                      Access::ActiveProfile(*app) == *first_profile_id &&
                      Access::ActiveCharacter(*app) == *first_character_id &&
                      Access::PersistenceGeneration(*app) ==
                          std::optional<std::uint64_t>{5U} &&
                      Access::PersistenceRecordCount(*app) ==
                          std::optional<std::size_t>{5U} &&
                      Access::PersistenceLogicalValueBytes(*app) ==
                          std::optional<std::size_t>{221U} &&
                      IsOneDiagnosticPlaySubmission(
                          gpu_before_start, Access::GpuSnapshot(*app)),
                "Briefing Room mission selection commits the character-owned diagnostic session and enters DiagnosticPlay");
            Check(PushKey(SDL_SCANCODE_F1, false) && run_plain_frame(),
                "the explicit diagnostic-start key releases before teardown");
        }
    }

    auto reopened = omega::app::NativePersistence::Bootstrap(creation_root);
    Check(reopened && reopened->startup_profiles().size() == 1U &&
               reopened->persisted_confirmed_profile_id() == first_profile_id &&
               reopened->persisted_confirmed_character_id() == first_character_id &&
               reopened->database().generation() == 5U &&
               reopened->database().record_count() == 5U &&
               reopened->database().logical_value_bytes() == 221U &&
               reopened->startup_profiles()[0].id == *first_profile_id &&
              reopened->startup_profiles()[0].metadata.display_name ==
                  omega::app::kFrontEndFirstProfileDisplayName &&
              reopened->startup_profiles()[0].metadata.created_unix_milliseconds ==
                  kCreationTimestamp &&
              reopened->startup_profiles()[0].metadata.modified_unix_milliseconds ==
                  kCreationTimestamp &&
              reopened->startup_profiles()[0].metadata_revision == 1U,
        "reopening native persistence observes PROFILE 1, its fixed diagnostic character, both confirmations, and the character-owned session checkpoint");

    const std::filesystem::path constrained_root =
        fixture_root / "native-first-profile-preflight";
    auto constrained_persistence =
        omega::app::NativePersistence::Bootstrap(constrained_root);
    auto constrained_config = omega::runtime::ParseConfigText("");
    bool startup_rejected_exactly = false;
    if (constrained_persistence && constrained_config &&
        constrained_persistence->startup_profiles().empty())
    {
        constexpr omega::runtime::RenderTexturePoolConfig kTightTexturePool{
            .slot_capacity = 4U,
        };
        auto rejected = Access::CreateWithPersistenceAndTextureConfig(
            std::move(*constrained_config), settings,
            omega::runtime::ContentStartupState{},
            std::move(*constrained_persistence), false, kTightTexturePool);
        startup_rejected_exactly =
            !rejected &&
            rejected.error() ==
                "SDL/GPU front-end profiles texture upload: render texture reserve: slot-capacity-exceeded";
    }
    Check(startup_rejected_exactly,
        "a four-slot texture pool rejects startup while preloading the complete first-profile presentation");
    auto constrained_reopened =
        omega::app::NativePersistence::Bootstrap(constrained_root);
    Check(constrained_reopened && constrained_reopened->startup_profiles().empty(),
        "failed first-profile presentation preflight leaves the durable catalog empty");

    // Regression for the transactional ordering used by profile reselection:
    // after first-character creation only seven slots remain resident. A
    // capacity-eight pool must admit the replacement card before releasing the
    // old one, then return to seven without leaking the obsolete empty card.
    const std::filesystem::path reselect_root =
        fixture_root / "native-character-reselect-capacity-eight";
    {
        auto reselect_persistence =
            omega::app::NativePersistence::Bootstrap(reselect_root);
        if (reselect_persistence)
        {
            const auto created = reselect_persistence->profiles().Create(
                *first_profile_id,
                omega::profiles::ProfileMetadata{
                    .display_name =
                        std::string{omega::app::kFrontEndFirstProfileDisplayName},
                    .created_unix_milliseconds = kCreationTimestamp,
                    .modified_unix_milliseconds = kCreationTimestamp,
                });
            Check(created.has_value(),
                "the capacity-eight reselect profile is created");
        }
        else
        {
            Check(false,
                "the capacity-eight reselect database bootstraps");
        }
    }
    auto reselect_persistence =
        omega::app::NativePersistence::Bootstrap(reselect_root);
    auto reselect_config = omega::runtime::ParseConfigText("");
    constexpr omega::runtime::RenderTexturePoolConfig kCapacityEight{
        .slot_capacity = 8U,
    };
    if (reselect_persistence && reselect_config)
    {
        auto reselect_app = Access::CreateWithPersistenceAndTextureConfig(
            std::move(*reselect_config), settings,
            omega::runtime::ContentStartupState{},
            std::move(*reselect_persistence), false, kCapacityEight);
        Check(reselect_app.has_value(),
            "the existing-profile fixture starts in a capacity-eight pool");
        if (reselect_app)
        {
            const auto run_reselect_frame = [&reselect_app]() {
                auto run = reselect_app->Run(1);
                return run && run->rendered_frames == 1 &&
                       !run->quit_requested;
            };
            const auto startup_gpu = Access::GpuSnapshot(*reselect_app);
            Check(startup_gpu.textures.slot_capacity == 8U &&
                      startup_gpu.textures.resident_slots == 6U,
                "the capacity-eight reselect fixture starts with six base textures");
            Check(PushKey(SDL_SCANCODE_F1, true) && run_reselect_frame() &&
                      Access::FrontEnd(*reselect_app) ==
                          omega::app::FrontEndState{
                              .mode = omega::app::FrontEndMode::Characters,
                              .selected_main_row =
                                  omega::app::FrontEndMainRow::Profiles,
                          } &&
                      Access::GpuSnapshot(*reselect_app)
                              .textures.resident_slots == 8U,
                "empty-profile selection exactly fills capacity eight with current and preview character cards");
            Check(PushKey(SDL_SCANCODE_F1, false) && run_reselect_frame(),
                "the capacity-eight profile-selection edge releases");
            Check(PushKey(SDL_SCANCODE_F1, true) && run_reselect_frame() &&
                      Access::CharacterCatalogCount(
                          *reselect_app, *first_profile_id) ==
                          std::optional<std::size_t>{1U} &&
                      Access::GpuSnapshot(*reselect_app)
                              .textures.resident_slots == 7U,
                "first-character creation releases the obsolete empty card and restores one slot of headroom");
            Check(PushKey(SDL_SCANCODE_F1, false) && run_reselect_frame(),
                "the capacity-eight character-creation edge releases");
            Check(PushKey(SDL_SCANCODE_F1, true) && run_reselect_frame() &&
                      Access::ActiveCharacter(*reselect_app) ==
                          *first_character_id,
                "the capacity-eight fixture selects its first character");
            Check(PushKey(SDL_SCANCODE_F1, false) && run_reselect_frame(),
                "the capacity-eight character-selection edge releases");
            Check(PushKey(SDL_SCANCODE_ESCAPE, true) && run_reselect_frame() &&
                      Access::FrontEnd(*reselect_app).mode ==
                          omega::app::FrontEndMode::Characters,
                "Escape returns the capacity-eight fixture from Briefing Room to Characters");
            Check(PushKey(SDL_SCANCODE_ESCAPE, false) && run_reselect_frame(),
                "the capacity-eight Briefing Room cancel edge releases");
            Check(PushKey(SDL_SCANCODE_ESCAPE, true) && run_reselect_frame() &&
                      Access::FrontEnd(*reselect_app).mode ==
                          omega::app::FrontEndMode::Main &&
                      Access::FrontEnd(*reselect_app).selected_main_row ==
                          omega::app::FrontEndMainRow::Profiles,
                "a second Escape returns the capacity-eight fixture to the Profiles row");
            Check(PushKey(SDL_SCANCODE_ESCAPE, false) && run_reselect_frame(),
                "the capacity-eight Characters cancel edge releases");
            Check(PushKey(SDL_SCANCODE_F1, true) && run_reselect_frame() &&
                      Access::FrontEnd(*reselect_app).mode ==
                          omega::app::FrontEndMode::Profiles,
                "the capacity-eight fixture reopens Profiles");
            Check(PushKey(SDL_SCANCODE_F1, false) && run_reselect_frame(),
                "the capacity-eight Profiles entry edge releases");

            const auto gpu_before_reselect = Access::GpuSnapshot(*reselect_app);
            Check(PushKey(SDL_SCANCODE_F1, true) && run_reselect_frame() &&
                      Access::FrontEnd(*reselect_app).mode ==
                          omega::app::FrontEndMode::Characters &&
                      !Access::ActiveCharacter(*reselect_app) &&
                      Access::PersistedConfirmedCharacter(*reselect_app) ==
                          *first_character_id &&
                      Access::PersistenceGeneration(*reselect_app) ==
                          std::optional<std::uint64_t>{4U} &&
                      Access::GpuSnapshot(*reselect_app)
                              .textures.resident_slots == 7U &&
                      IsOneCharacterMenuSubmissionWithTextureDelta(
                          gpu_before_reselect, Access::GpuSnapshot(*reselect_app),
                          1U, 1U, 3U),
                "capacity-eight same-profile reselection stages one replacement card at the exact peak, releases the old card, and preserves durable state");
            Check(PushKey(SDL_SCANCODE_F1, false) && run_reselect_frame(),
                "the capacity-eight reselect edge releases");
            Check(PushKey(SDL_SCANCODE_F1, true) && run_reselect_frame() &&
                      Access::ActiveCharacter(*reselect_app) ==
                          *first_character_id &&
                      Access::FrontEnd(*reselect_app).mode ==
                          omega::app::FrontEndMode::BriefingRoom &&
                      Access::FrontEnd(*reselect_app).selected_main_row ==
                          omega::app::FrontEndMainRow::StartDiagnostic,
                "the capacity-eight replacement character remains selectable");
            Check(PushKey(SDL_SCANCODE_F1, false) && run_reselect_frame(),
                "the capacity-eight replacement selection edge releases");
        }
    }
    else
    {
        Check(false,
            "the capacity-eight reselect fixture reopens with valid config");
    }
}

void CheckComposedGeneratedMenuAcceptance(
    const std::filesystem::path& fixture_root,
    const omega::runtime::RuntimeSettings& settings)
{
    using Access = omega::app::detail::OmegaAppTestAccess;
    constexpr std::uint64_t kCreationTimestamp = 1'725'000'000'456ULL;
    constexpr omega::app::FrontEndState kProfilesFirst{
        .mode = omega::app::FrontEndMode::Profiles,
        .selected_main_row = omega::app::FrontEndMainRow::Profiles,
        .selected_profile_slot = omega::app::FrontEndProfileSlot::First,
    };
    constexpr omega::app::FrontEndState kMainProfiles{
        .mode = omega::app::FrontEndMode::Main,
        .selected_main_row = omega::app::FrontEndMainRow::Profiles,
        .selected_profile_slot = omega::app::FrontEndProfileSlot::First,
    };
    constexpr omega::app::FrontEndState kCharactersFirst{
        .mode = omega::app::FrontEndMode::Characters,
        .selected_main_row = omega::app::FrontEndMainRow::Profiles,
        .selected_profile_slot = omega::app::FrontEndProfileSlot::First,
        .selected_character_slot =
            omega::app::FrontEndCharacterSlot::First,
    };
    constexpr omega::app::FrontEndState kMainStart{
        .mode = omega::app::FrontEndMode::Main,
        .selected_main_row = omega::app::FrontEndMainRow::StartDiagnostic,
        .selected_profile_slot = omega::app::FrontEndProfileSlot::First,
    };
    constexpr omega::app::FrontEndState kBriefingRoom{
        .mode = omega::app::FrontEndMode::BriefingRoom,
        .selected_main_row = omega::app::FrontEndMainRow::StartDiagnostic,
        .selected_profile_slot = omega::app::FrontEndProfileSlot::First,
        .selected_character_slot =
            omega::app::FrontEndCharacterSlot::First,
    };
    constexpr omega::app::FrontEndState kDiagnosticPlay{
        .mode = omega::app::FrontEndMode::DiagnosticPlay,
        .selected_main_row = omega::app::FrontEndMainRow::StartDiagnostic,
        .selected_profile_slot = omega::app::FrontEndProfileSlot::First,
    };
    constexpr omega::runtime::RenderSourceRectQ16 kFullSource{
        .left = 0U,
        .top = 0U,
        .right = omega::runtime::kNormalizedRenderExtent,
        .bottom = omega::runtime::kNormalizedRenderExtent,
    };
    constexpr omega::runtime::RenderTargetRectQ16 kActorOriginDestination{
        .left = 31'744U,
        .top = 31'744U,
        .right = 33'792U,
        .bottom = 33'792U,
    };
    // Fixed project-owned Profiles layout. The card fills the menu panel, the
    // selection cursor sits at the left of its row band, and the active-row cue
    // reuses the same band at the opposite edge so the two never overlap.
    constexpr omega::runtime::RenderSourceRectQ16 kProfileCueSource{
        .left = 0U,
        .top = 0U,
        .right = 512U,
        .bottom = 512U,
    };
    constexpr omega::runtime::RenderTargetRectQ16 kMenuDestination{
        .left = 2048U,
        .top = 2048U,
        .right = 26'624U,
        .bottom = 15'872U,
    };
    constexpr omega::runtime::RenderTargetRectQ16 kFirstRowSelectionTarget{
        .left = 3584U,
        .top = 7424U,
        .right = 4352U,
        .bottom = 8960U,
    };
    constexpr omega::runtime::RenderTargetRectQ16 kFirstRowActiveTarget{
        .left = 25'280U,
        .top = 7424U,
        .right = 26'048U,
        .bottom = 8960U,
    };
    constexpr std::uint64_t kFrontEndCardLogicalBytes =
        128ULL * 72ULL * 4ULL;
    constexpr std::uint64_t kTopologyLogicalBytes = 96ULL * 32ULL * 4ULL;
    constexpr std::uint64_t kPreloadedLogicalBytes =
        kFrontEndCardLogicalBytes * 6ULL + kTopologyLogicalBytes + 4ULL;

    const auto profile_id = omega::profiles::ProfileId::Parse(
        "00000000000000000000000000000001");
    const auto character_id = omega::profiles::CharacterId::Parse(
        "00000000000000000000000000000001");
    const std::filesystem::path database_root =
        fixture_root / "native-composed-menu-acceptance";
    auto persistence = omega::app::NativePersistence::Bootstrap(database_root);
    auto config = omega::runtime::ParseConfigText("");
    Check(profile_id && character_id && persistence && config &&
              persistence->startup_profiles().empty() &&
              !persistence->persisted_confirmed_profile_id() &&
              persistence->database().generation() == 0U &&
              persistence->database().record_count() == 0U &&
              persistence->database().logical_value_bytes() == 0U,
        "the composed generated menu fixture starts from an exact zero-generation, "
        "zero-record, zero-byte database with no durable confirmation");
    if (!profile_id || !character_id || !persistence || !config)
        return;

    auto app = Access::CreateWithPersistence(std::move(*config), settings,
        omega::runtime::ContentStartupState{}, std::move(*persistence), false);
    Check(app && Access::FrontEnd(*app) == kProfilesFirst &&
              Access::CanCreateFirstProfile(*app) &&
              !Access::ActiveProfile(*app),
        "the composed generated menu fixture opens Profiles/First without a session profile");
    if (!app)
        return;

    const omega::app::GpuHostSnapshot startup_gpu = Access::GpuSnapshot(*app);
    const omega::runtime::FrameSchedulerState startup_scheduler =
        Access::SchedulerSnapshot(*app);
    const omega::simulation::SimulationState startup_simulation =
        Access::SimulationSnapshot(*app);
    const auto startup_position = Access::DebugLocomotionPosition(*app);
    const omega::runtime::RenderTextureHandle marker_texture =
        Access::DiagnosticActorMarkerTexture(*app);
    Check(marker_texture.valid() && startup_gpu.successful_uploads == 8U &&
              startup_gpu.successful_upload_logical_bytes ==
                  kPreloadedLogicalBytes &&
              startup_gpu.successful_releases == 0U &&
              startup_gpu.textures.reserved_slots == 0U &&
              startup_gpu.textures.resident_slots == 8U &&
              startup_gpu.textures.resident_logical_bytes ==
                  kPreloadedLogicalBytes,
        "the composed path starts with its complete generated presentation resident once");

    const bool creation_queued =
        Access::ArmFirstProfileTimestamp(*app, kCreationTimestamp) &&
        PushKey(SDL_SCANCODE_F1, true);
    auto created = app->RunWithCapture(1);
    const omega::app::GpuHostSnapshot created_gpu = Access::GpuSnapshot(*app);
    Check(creation_queued && created &&
              created->completion() ==
                  omega::app::RunCaptureCompletion::FrameLimitReached &&
              !created->failure() &&
              created->result().planned_simulation_steps == 0U &&
              created->result().executed_simulation_steps == 0U &&
              Access::FrontEnd(*app) == kProfilesFirst &&
              !Access::CanCreateFirstProfile(*app) &&
              !Access::ActiveProfile(*app) &&
              Access::ProfileCatalogCount(*app) ==
                  std::optional<std::size_t>{1U} &&
              Access::PersistenceGeneration(*app) ==
                  std::optional<std::uint64_t>{1U} &&
              Access::PersistenceRecordCount(*app) ==
                  std::optional<std::size_t>{1U} &&
              Access::PersistenceLogicalValueBytes(*app) ==
                  std::optional<std::size_t>{41U} &&
              !Access::PersistedConfirmedProfile(*app) &&
              Access::SchedulerSnapshot(*app) == startup_scheduler &&
              SameSimulationState(
                  Access::SimulationSnapshot(*app), startup_simulation) &&
              IsOneVisibleMenuSubmission(startup_gpu, created_gpu),
        "the first Primary creates PROFILE 1 as exactly one 41-byte generation-one metadata record with no durable confirmation, while the modal scheduler stays frozen and the preloaded presentation only swaps ownership");
    if (!created)
        return;

    const bool creation_release_queued = PushKey(SDL_SCANCODE_F1, false);
    auto creation_released = app->RunWithCapture(1);
    const omega::app::GpuHostSnapshot creation_released_gpu =
        Access::GpuSnapshot(*app);
    Check(creation_release_queued && creation_released &&
              creation_released->result().planned_simulation_steps == 0U &&
              creation_released->result().executed_simulation_steps == 0U &&
              Access::FrontEnd(*app) == kProfilesFirst &&
              !Access::ActiveProfile(*app) &&
              Access::SchedulerSnapshot(*app) == startup_scheduler &&
              IsOneVisibleMenuSubmission(
                  created_gpu, creation_released_gpu),
        "the required creation-key release is inert and visibly preserves Profiles/First");
    if (!creation_released)
        return;

    // A durably created profile is still unconfirmed, so the gate must stay
    // closed. Walk out to the START DIAGNOSTIC row and prove the entry edge is
    // inert before explicitly navigating back to Profiles for confirmation.
    const bool unconfirmed_cancel_queued = PushKey(SDL_SCANCODE_BACKSPACE, true);
    auto unconfirmed_cancel = app->RunWithCapture(1);
    const omega::app::GpuHostSnapshot unconfirmed_cancel_gpu =
        Access::GpuSnapshot(*app);
    Check(unconfirmed_cancel_queued && unconfirmed_cancel &&
              unconfirmed_cancel->result().planned_simulation_steps == 0U &&
              Access::FrontEnd(*app) == kMainProfiles &&
              !Access::ActiveProfile(*app) &&
              IsOneVisibleMenuSubmission(
                  creation_released_gpu, unconfirmed_cancel_gpu),
        "Cancel leaves the unconfirmed Profiles surface for its visible Main row");

    const bool unconfirmed_cancel_release_queued =
        PushKey(SDL_SCANCODE_BACKSPACE, false);
    auto unconfirmed_cancel_released = app->RunWithCapture(1);
    const omega::app::GpuHostSnapshot unconfirmed_cancel_released_gpu =
        Access::GpuSnapshot(*app);
    Check(unconfirmed_cancel_release_queued && unconfirmed_cancel_released &&
              Access::FrontEnd(*app) == kMainProfiles &&
              IsOneVisibleMenuSubmission(
                  unconfirmed_cancel_gpu, unconfirmed_cancel_released_gpu),
        "the unconfirmed cancel release is inert");

    const bool unconfirmed_navigation_queued = PushKey(SDL_SCANCODE_UP, true);
    auto unconfirmed_navigated = app->RunWithCapture(1);
    const omega::app::GpuHostSnapshot unconfirmed_navigated_gpu =
        Access::GpuSnapshot(*app);
    Check(unconfirmed_navigation_queued && unconfirmed_navigated &&
              IsFreshPress(CapturedActionState(
                  unconfirmed_navigated, omega::app::kDebugMoveForwardAction)) &&
              unconfirmed_navigated->result().planned_simulation_steps == 0U &&
              Access::FrontEnd(*app) == kMainStart &&
              Access::SchedulerSnapshot(*app) == startup_scheduler &&
              IsOneVisibleMenuSubmission(
                  unconfirmed_cancel_released_gpu, unconfirmed_navigated_gpu),
        "a fresh action-2 press selects the START DIAGNOSTIC row while unconfirmed");

    const bool unconfirmed_navigation_release_queued =
        PushKey(SDL_SCANCODE_UP, false);
    auto unconfirmed_navigation_released = app->RunWithCapture(1);
    Check(unconfirmed_navigation_release_queued &&
              unconfirmed_navigation_released &&
              IsRelease(CapturedActionState(unconfirmed_navigation_released,
                  omega::app::kDebugMoveForwardAction)) &&
              Access::FrontEnd(*app) == kMainStart &&
              IsOneVisibleMenuSubmission(
                  unconfirmed_navigated_gpu, Access::GpuSnapshot(*app)),
        "the unconfirmed navigation emits the action-2 release edge");

    const auto persistence_generation_before_inert_start =
        Access::PersistenceGeneration(*app);
    const auto persistence_records_before_inert_start =
        Access::PersistenceRecordCount(*app);
    const auto persistence_bytes_before_inert_start =
        Access::PersistenceLogicalValueBytes(*app);
    const omega::app::GpuHostSnapshot inert_start_gpu_before =
        Access::GpuSnapshot(*app);
    const bool inert_start_queued =
        Access::ArmNextRunElapsed(*app, settings.frame.simulation_step) &&
        PushKey(SDL_SCANCODE_F1, true);
    auto inert_start = app->RunWithCapture(1);
    const omega::app::GpuHostSnapshot inert_start_gpu_after =
        Access::GpuSnapshot(*app);
    Check(inert_start_queued && inert_start &&
              inert_start->completion() ==
                  omega::app::RunCaptureCompletion::FrameLimitReached &&
              !inert_start->failure() &&
              inert_start->result().planned_simulation_steps == 0U &&
              inert_start->result().executed_simulation_steps == 0U &&
              Access::FrontEnd(*app) == kMainStart &&
              !Access::ActiveProfile(*app) &&
              !Access::PersistedConfirmedProfile(*app) &&
              Access::PersistenceGeneration(*app) ==
                  persistence_generation_before_inert_start &&
              Access::PersistenceRecordCount(*app) ==
                  persistence_records_before_inert_start &&
              Access::PersistenceLogicalValueBytes(*app) ==
                  persistence_bytes_before_inert_start &&
              Access::SchedulerSnapshot(*app) == startup_scheduler &&
              SameSimulationState(
                  Access::SimulationSnapshot(*app), startup_simulation) &&
              Access::DebugLocomotionPosition(*app) == startup_position &&
              DrawListsEqual(Access::CurrentFrontEndDrawList(*app),
                  Access::FrontEndMainDrawLists(*app)[0]) &&
              IsOneVisibleMenuSubmission(
                  inert_start_gpu_before, inert_start_gpu_after),
        "an unconfirmed Start Diagnostic edge is inert, discards a full simulation step of elapsed time, and mutates no simulation, persistence, or GPU resource");
    if (!inert_start)
        return;

    const bool inert_start_release_queued = PushKey(SDL_SCANCODE_F1, false);
    auto inert_start_released = app->RunWithCapture(1);
    const omega::app::GpuHostSnapshot inert_start_released_gpu =
        Access::GpuSnapshot(*app);
    Check(inert_start_release_queued && inert_start_released &&
              inert_start_released->result().planned_simulation_steps == 0U &&
              Access::FrontEnd(*app) == kMainStart &&
              !Access::ActiveProfile(*app) &&
              IsOneVisibleMenuSubmission(
                  inert_start_gpu_after, inert_start_released_gpu),
        "the inert entry edge releases before explicit profile navigation");

    Check(PushKey(SDL_SCANCODE_DOWN, true) && app->Run(1).has_value() &&
              Access::FrontEnd(*app) == kMainProfiles,
        "explicit downward navigation returns to the Profiles main row");
    Check(PushKey(SDL_SCANCODE_DOWN, false) && app->Run(1).has_value(),
        "the explicit Profiles navigation edge releases");
    Check(PushKey(SDL_SCANCODE_F1, true) && app->Run(1).has_value() &&
              Access::FrontEnd(*app) == kProfilesFirst,
        "Primary explicitly opens the bounded Profiles surface");
    Check(PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value(),
        "the explicit Profiles entry edge releases before confirmation");
    const omega::app::GpuHostSnapshot profile_entry_released_gpu =
        Access::GpuSnapshot(*app);

    const bool confirmation_queued = PushKey(SDL_SCANCODE_F1, true);
    auto confirmed = app->RunWithCapture(1);
    const omega::app::GpuHostSnapshot confirmed_gpu = Access::GpuSnapshot(*app);
    Check(confirmation_queued && confirmed &&
              confirmed->completion() ==
                  omega::app::RunCaptureCompletion::FrameLimitReached &&
              !confirmed->failure() &&
              confirmed->result().planned_simulation_steps == 0U &&
              confirmed->result().executed_simulation_steps == 0U &&
              Access::FrontEnd(*app) == kCharactersFirst &&
              Access::ActiveProfile(*app) == profile_id &&
              !Access::ActiveCharacter(*app) &&
              !Access::PersistedConfirmedCharacter(*app) &&
              Access::CharacterCatalogCount(*app, *profile_id) ==
                  std::optional<std::size_t>{0U} &&
              Access::PersistedConfirmedProfile(*app) == profile_id &&
              Access::PersistenceGeneration(*app) ==
                  std::optional<std::uint64_t>{2U} &&
              Access::PersistenceRecordCount(*app) ==
                  std::optional<std::size_t>{2U} &&
              Access::PersistenceLogicalValueBytes(*app) ==
                  std::optional<std::size_t>{73U} &&
              Access::SchedulerSnapshot(*app) == startup_scheduler &&
              SameSimulationState(
                  Access::SimulationSnapshot(*app), startup_simulation) &&
              IsOneCharacterMenuSubmissionWithTextureDelta(
                  profile_entry_released_gpu, confirmed_gpu, 2U, 0U, 2U),
        "the explicit Profiles confirmation durably confirms PROFILE 1 and enters empty Characters with both cards resident without advancing simulation");
    if (!confirmed)
        return;

    const bool confirmation_release_queued = PushKey(SDL_SCANCODE_F1, false);
    auto confirmation_released = app->RunWithCapture(1);
    const omega::app::GpuHostSnapshot confirmation_released_gpu =
        Access::GpuSnapshot(*app);
    Check(confirmation_release_queued && confirmation_released &&
              Access::FrontEnd(*app) == kCharactersFirst &&
              Access::ActiveProfile(*app) == profile_id &&
              IsOneCharacterMenuSubmissionWithTextureDelta(
                  confirmed_gpu, confirmation_released_gpu, 0U, 0U, 2U),
        "the confirming edge releases without republishing the durable pointer");

    const bool confirmed_characters_cancel_queued =
        PushKey(SDL_SCANCODE_BACKSPACE, true);
    auto confirmed_characters_cancel = app->RunWithCapture(1);
    const omega::app::GpuHostSnapshot confirmed_characters_cancel_gpu =
        Access::GpuSnapshot(*app);
    Check(confirmed_characters_cancel_queued && confirmed_characters_cancel &&
              Access::FrontEnd(*app) == kMainProfiles &&
              IsOneVisibleMenuSubmission(
                  confirmation_released_gpu, confirmed_characters_cancel_gpu),
        "Cancel returns from Characters to Main/Profiles while preserving the confirmed profile");
    const bool confirmed_characters_cancel_release_queued =
        PushKey(SDL_SCANCODE_BACKSPACE, false);
    auto confirmed_characters_cancel_released = app->RunWithCapture(1);
    const omega::app::GpuHostSnapshot confirmed_characters_main_gpu =
        Access::GpuSnapshot(*app);
    Check(confirmed_characters_cancel_release_queued &&
              confirmed_characters_cancel_released &&
              Access::FrontEnd(*app) == kMainProfiles &&
              IsOneVisibleMenuSubmission(confirmed_characters_cancel_gpu,
                  confirmed_characters_main_gpu),
        "the Characters cancel edge releases on Main/Profiles");

    // Re-entering Profiles now that a confirmation resolves must select the
    // preloaded selected-plus-active list rather than the unmarked one.
    const bool marked_profiles_queued = PushKey(SDL_SCANCODE_F1, true);
    auto marked_profiles = app->RunWithCapture(1);
    const omega::app::GpuHostSnapshot marked_profiles_gpu =
        Access::GpuSnapshot(*app);
    const omega::runtime::RenderTextureHandle profiles_texture =
        Access::FrontEndProfilesTexture(*app);
    const auto active_commands =
        Access::FrontEndProfileActiveDrawLists(*app)[0][0].commands();
    const auto base_commands = Access::DiagnosticHiddenDrawList(*app).commands();
    const bool active_list_is_exact =
        active_commands.size() == 4U && base_commands.size() == 1U &&
        active_commands[0] == base_commands[0] &&
        active_commands[1] == omega::runtime::RenderTextureBlitCommand{
            .texture = profiles_texture,
            .source = kFullSource,
            .destination = kMenuDestination,
            .fit_mode = omega::runtime::RenderTextureFitMode::Stretch,
            .filter_mode = omega::runtime::RenderTextureFilterMode::Nearest,
        } &&
        active_commands[2] == omega::runtime::RenderTextureBlitCommand{
            .texture = profiles_texture,
            .source = kProfileCueSource,
            .destination = kFirstRowSelectionTarget,
            .fit_mode = omega::runtime::RenderTextureFitMode::Stretch,
            .filter_mode = omega::runtime::RenderTextureFilterMode::Nearest,
        } &&
        active_commands[3] == omega::runtime::RenderTextureBlitCommand{
            .texture = profiles_texture,
            .source = kProfileCueSource,
            .destination = kFirstRowActiveTarget,
            .fit_mode = omega::runtime::RenderTextureFitMode::Stretch,
            .filter_mode = omega::runtime::RenderTextureFilterMode::Nearest,
        };
    Check(marked_profiles_queued && marked_profiles &&
              marked_profiles->completion() ==
                  omega::app::RunCaptureCompletion::FrameLimitReached &&
              !marked_profiles->failure() &&
              Access::FrontEnd(*app) == kProfilesFirst &&
              Access::ActiveProfile(*app) == profile_id &&
              Access::PersistenceGeneration(*app) ==
                  std::optional<std::uint64_t>{2U} &&
              active_list_is_exact &&
              DrawListsEqual(Access::CurrentFrontEndDrawList(*app),
                  Access::FrontEndProfileActiveDrawLists(*app)[0][0]) &&
              !DrawListsEqual(Access::CurrentFrontEndDrawList(*app),
                  Access::FrontEndProfileSelectionDrawLists(*app)[0]) &&
              IsOneActiveProfileMenuSubmission(
                  confirmed_characters_main_gpu, marked_profiles_gpu),
        "a confirmed Profiles surface submits the exact four-command selected-plus-active list, adding only the fixed active-row cue and uploading nothing");

    const bool marked_release_queued = PushKey(SDL_SCANCODE_F1, false);
    auto marked_released = app->RunWithCapture(1);
    const omega::app::GpuHostSnapshot marked_released_gpu =
        Access::GpuSnapshot(*app);
    Check(marked_release_queued && marked_released &&
              Access::FrontEnd(*app) == kProfilesFirst &&
              DrawListsEqual(Access::CurrentFrontEndDrawList(*app),
                  Access::FrontEndProfileActiveDrawLists(*app)[0][0]) &&
              IsOneActiveProfileMenuSubmission(
                  marked_profiles_gpu, marked_released_gpu),
        "the marked Profiles surface persists across the release frame");

    const bool marked_cancel_queued = PushKey(SDL_SCANCODE_BACKSPACE, true);
    auto marked_cancelled = app->RunWithCapture(1);
    const omega::app::GpuHostSnapshot marked_cancelled_gpu =
        Access::GpuSnapshot(*app);
    Check(marked_cancel_queued && marked_cancelled &&
              Access::FrontEnd(*app) == kMainProfiles &&
              Access::ActiveProfile(*app) == profile_id &&
              IsOneVisibleMenuSubmission(
                  marked_released_gpu, marked_cancelled_gpu),
        "Cancel returns the confirmed session to its visible Main/Profiles row");

    const bool marked_cancel_release_queued =
        PushKey(SDL_SCANCODE_BACKSPACE, false);
    auto marked_cancel_released = app->RunWithCapture(1);
    const omega::app::GpuHostSnapshot confirmed_main_gpu =
        Access::GpuSnapshot(*app);
    Check(marked_cancel_release_queued && marked_cancel_released &&
              Access::FrontEnd(*app) == kMainProfiles,
        "the confirmed cancel release is inert");

    const bool character_profiles_entry_queued = PushKey(SDL_SCANCODE_F1, true);
    auto character_profiles_entry = app->RunWithCapture(1);
    const auto character_profiles_entry_gpu = Access::GpuSnapshot(*app);
    Check(character_profiles_entry_queued && character_profiles_entry &&
              Access::FrontEnd(*app) == kProfilesFirst &&
              IsOneActiveProfileMenuSubmission(
                  confirmed_main_gpu, character_profiles_entry_gpu),
        "the confirmed profile surface reopens before character selection");
    Check(PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value(),
        "the character-flow Profiles entry edge releases");
    const auto character_profiles_released_gpu = Access::GpuSnapshot(*app);

    const bool character_profile_queued = PushKey(SDL_SCANCODE_F1, true);
    auto character_profile = app->RunWithCapture(1);
    const auto character_profile_gpu = Access::GpuSnapshot(*app);
    Check(character_profile_queued && character_profile &&
              character_profile->result().planned_simulation_steps == 0U &&
              Access::FrontEnd(*app) == kCharactersFirst &&
              Access::ActiveProfile(*app) == profile_id &&
              !Access::ActiveCharacter(*app) &&
              Access::PersistenceGeneration(*app) ==
                  std::optional<std::uint64_t>{2U} &&
              IsOneCharacterMenuSubmissionWithTextureDelta(
                  character_profiles_released_gpu, character_profile_gpu,
                  2U, 2U, 2U),
        "same-profile reselection transactionally replaces both empty-catalog cards and re-enters Characters without a persistence write");
    Check(PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value(),
        "the character-flow profile-selection edge releases");

    const auto gpu_before_character_create = Access::GpuSnapshot(*app);
    const bool character_create_queued = PushKey(SDL_SCANCODE_F1, true);
    auto character_created = app->RunWithCapture(1);
    const auto character_created_gpu = Access::GpuSnapshot(*app);
    Check(character_create_queued && character_created &&
              character_created->result().planned_simulation_steps == 0U &&
              Access::FrontEnd(*app) == kCharactersFirst &&
              Access::CharacterCatalogCount(*app, *profile_id) ==
                  std::optional<std::size_t>{1U} &&
              Access::PersistenceGeneration(*app) ==
                  std::optional<std::uint64_t>{3U} &&
              Access::PersistenceRecordCount(*app) ==
                  std::optional<std::size_t>{3U} &&
              Access::PersistenceLogicalValueBytes(*app) ==
                  std::optional<std::size_t>{125U} &&
              IsOneCharacterMenuSubmissionWithTextureDelta(
                  gpu_before_character_create, character_created_gpu,
                  0U, 1U, 3U),
        "character creation adds the 52-byte fixed marker and releases the obsolete empty card");
    Check(PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value(),
        "the composed character-creation edge releases");

    const auto gpu_before_character_select = Access::GpuSnapshot(*app);
    const bool character_select_queued = PushKey(SDL_SCANCODE_F1, true);
    auto character_selected = app->RunWithCapture(1);
    const auto character_selected_gpu = Access::GpuSnapshot(*app);
    Check(character_select_queued && character_selected &&
              character_selected->result().planned_simulation_steps == 0U &&
              Access::FrontEnd(*app) == kBriefingRoom &&
              Access::ActiveCharacter(*app) == character_id &&
              Access::PersistedConfirmedCharacter(*app) == character_id &&
              Access::PersistenceGeneration(*app) ==
                  std::optional<std::uint64_t>{4U} &&
              Access::PersistenceRecordCount(*app) ==
                  std::optional<std::size_t>{4U} &&
              Access::PersistenceLogicalValueBytes(*app) ==
                  std::optional<std::size_t>{173U} &&
              IsOneVisibleMenuSubmission(
                  gpu_before_character_select, character_selected_gpu),
        "character selection commits its 48-byte active pointer and enters the visible Briefing Room mission selector");
    const bool character_select_release_queued = PushKey(SDL_SCANCODE_F1, false);
    auto character_select_released = app->RunWithCapture(1);
    const omega::app::GpuHostSnapshot navigation_released_gpu =
        Access::GpuSnapshot(*app);
    Check(character_select_release_queued && character_select_released &&
              Access::FrontEnd(*app) == kBriefingRoom &&
              Access::SchedulerSnapshot(*app) == startup_scheduler &&
              IsOneVisibleMenuSubmission(
                  character_selected_gpu, navigation_released_gpu),
        "the character-selection release preserves Briefing Room with the modal scheduler frozen");
    if (!character_select_released)
        return;

    const bool play_queued =
        Access::ArmNextRunElapsed(*app, settings.frame.simulation_step) &&
        PushKey(SDL_SCANCODE_F1, true) && PushKey(SDL_SCANCODE_W, true);
    auto play = app->RunWithCapture(1);
    const omega::app::GpuHostSnapshot play_gpu = Access::GpuSnapshot(*app);
    const omega::simulation::SimulationState play_simulation =
        Access::SimulationSnapshot(*app);
    const auto play_position = Access::DebugLocomotionPosition(*app);
    const omega::runtime::RenderDrawList actor_draw_list =
        Access::DiagnosticActorDrawList(*app);
    const auto actor_commands = actor_draw_list.commands();
    const auto hidden_commands = Access::DiagnosticHiddenDrawList(*app).commands();
    constexpr auto kObjectiveDestination =
        omega::app::PlanProjectDiagnosticObjectiveMarkerDestination(
            omega::gameplay::DiagnosticProximityTriggerState{});
    static_assert(kObjectiveDestination.has_value());
    const bool marker_command_is_visible =
        actor_commands.size() == 3U && hidden_commands.size() == 1U &&
        actor_commands[0] == hidden_commands[0] &&
        actor_commands[1].texture == marker_texture &&
        actor_commands[1].source == kFullSource &&
        actor_commands[1].destination == kActorOriginDestination &&
        actor_commands[1].fit_mode ==
            omega::runtime::RenderTextureFitMode::Stretch &&
        actor_commands[1].filter_mode ==
            omega::runtime::RenderTextureFilterMode::Nearest &&
        actor_commands[2].texture == marker_texture &&
        actor_commands[2].source == kFullSource &&
        actor_commands[2].destination == *kObjectiveDestination &&
        actor_commands[2].fit_mode ==
            omega::runtime::RenderTextureFitMode::Stretch &&
        actor_commands[2].filter_mode ==
            omega::runtime::RenderTextureFilterMode::Nearest;
    Check(play_queued && play &&
              play->completion() ==
                  omega::app::RunCaptureCompletion::FrameLimitReached &&
              !play->failure() && play->result().input_frames == 1U &&
              play->result().rendered_frames == 1 &&
              IsFreshPress(CapturedActionState(
                  play, omega::app::kDebugMoveForwardAction)) &&
              play->result().planned_simulation_steps == 1U &&
              play->result().executed_simulation_steps == 1U &&
              Access::FrontEnd(*app) == kDiagnosticPlay &&
              Access::ActiveProfile(*app) == profile_id &&
              Access::ActiveCharacter(*app) == character_id &&
              Access::PersistedConfirmedCharacter(*app) == character_id &&
              Access::PersistenceGeneration(*app) ==
                  std::optional<std::uint64_t>{5U} &&
              Access::PersistenceRecordCount(*app) ==
                  std::optional<std::size_t>{5U} &&
              Access::PersistenceLogicalValueBytes(*app) ==
                  std::optional<std::size_t>{221U} &&
              play_simulation.completed_steps == 1U &&
              play_simulation.simulated_time == settings.frame.simulation_step &&
              play_simulation.alive_entities == 1U && play_position &&
              *play_position == omega::simulation::Position3{} &&
              SameProximityTriggerState(
                  Access::DiagnosticProximityTriggerState(*app), {}) &&
              marker_command_is_visible &&
              DrawListsEqual(Access::CurrentFrontEndDrawList(*app),
                  actor_draw_list) &&
              play_gpu.successful_uploads == startup_gpu.successful_uploads + 4U &&
              play_gpu.successful_upload_logical_bytes ==
                  startup_gpu.successful_upload_logical_bytes +
                      kFrontEndCardLogicalBytes * 4U &&
              play_gpu.successful_releases ==
                  startup_gpu.successful_releases + 3U &&
              play_gpu.textures.resident_slots ==
                  startup_gpu.textures.resident_slots + 1U &&
              play_gpu.textures.resident_logical_bytes ==
                  startup_gpu.textures.resident_logical_bytes +
                      kFrontEndCardLogicalBytes &&
              IsOneDiagnosticPlaySubmission(navigation_released_gpu, play_gpu),
        "a profile-and-character-confirmed Primary commits the 48-byte character-owned checkpoint, enters DiagnosticPlay, advances one generated simulation step without leaking held menu movement, and retains exactly one live character card");
    if (!play)
        return;

    const bool play_release_queued = PushKey(SDL_SCANCODE_F1, false) &&
                                     PushKey(SDL_SCANCODE_W, false) &&
                                     Access::ArmNextRunElapsed(
                                         *app, std::chrono::nanoseconds::zero());
    auto play_released = app->RunWithCapture(1);
    const omega::app::GpuHostSnapshot play_released_gpu =
        Access::GpuSnapshot(*app);
    const omega::runtime::FrameSchedulerState play_scheduler =
        Access::SchedulerSnapshot(*app);
    Check(play_release_queued && play_released &&
              IsRelease(CapturedActionState(
                  play_released, omega::app::kDebugMoveForwardAction)) &&
              play_released->result().planned_simulation_steps == 0U &&
              play_released->result().executed_simulation_steps == 0U &&
              Access::FrontEnd(*app) == kDiagnosticPlay &&
              SameSimulationState(
                  Access::SimulationSnapshot(*app), play_simulation) &&
              Access::DebugLocomotionPosition(*app) == play_position &&
              IsOneDiagnosticPlaySubmission(play_gpu, play_released_gpu),
        "the composed acceptance emits the action-2 release edge at zero elapsed without changing simulation, marker placement, or GPU residency");
    if (!play_released)
        return;

    // The confirmation is the only authorization source, so a confirmed ID the
    // bounded model no longer presents must close the gate again. Erasing the
    // model's stored identifier is the existing seam for that: nothing else in
    // this composition can make a live confirmation stale.
    const bool stale_armed =
        Access::EraseStartupProfileId(
            *app, omega::app::FrontEndProfileSlot::First) &&
        Access::ArmNextRunElapsed(*app, settings.frame.simulation_step);
    auto stale_play = app->RunWithCapture(1);
    const omega::app::GpuHostSnapshot stale_play_gpu = Access::GpuSnapshot(*app);
    Check(stale_armed && stale_play &&
              stale_play->completion() ==
                  omega::app::RunCaptureCompletion::FrameLimitReached &&
              !stale_play->failure() &&
              stale_play->result().planned_simulation_steps == 0U &&
              stale_play->result().executed_simulation_steps == 0U &&
              Access::FrontEnd(*app) == kMainStart &&
              Access::DiagnosticMissionLifecycleState(*app).status ==
                  omega::gameplay::DiagnosticMissionStatus::Failed &&
              Access::ActiveProfile(*app) == profile_id &&
              Access::PersistedConfirmedProfile(*app) == profile_id &&
              Access::ActiveCharacter(*app) == character_id &&
              Access::PersistedConfirmedCharacter(*app) == character_id &&
              Access::PersistenceGeneration(*app) ==
                  std::optional<std::uint64_t>{5U} &&
              Access::PersistenceRecordCount(*app) ==
                  std::optional<std::size_t>{5U} &&
              Access::PersistenceLogicalValueBytes(*app) ==
                  std::optional<std::size_t>{221U} &&
              Access::SchedulerSnapshot(*app) == play_scheduler &&
              SameSimulationState(
                  Access::SimulationSnapshot(*app), play_simulation) &&
              Access::DebugLocomotionPosition(*app) == play_position &&
              DrawListsEqual(Access::CurrentFrontEndDrawList(*app),
                  Access::FrontEndMainDrawLists(*app)[0]) &&
              IsOneVisibleMenuSubmission(play_released_gpu, stale_play_gpu),
        "a confirmed identifier the model no longer presents fails an entered DiagnosticPlay closed to the initial front end, discards a full simulation step of elapsed time, and leaves the durable pointer and every simulation value untouched");
    if (!stale_play)
        return;

    const bool stale_entry_queued = PushKey(SDL_SCANCODE_F1, true);
    auto stale_entry = app->RunWithCapture(1);
    const omega::app::GpuHostSnapshot stale_entry_gpu = Access::GpuSnapshot(*app);
    Check(stale_entry_queued && stale_entry && !stale_entry->failure() &&
              stale_entry->result().planned_simulation_steps == 0U &&
              Access::FrontEnd(*app) == kMainStart &&
              DrawListsEqual(Access::CurrentFrontEndDrawList(*app),
                  Access::FrontEndMainDrawLists(*app)[0]) &&
              IsOneVisibleMenuSubmission(stale_play_gpu, stale_entry_gpu),
        "the stale confirmation also leaves Start Diagnostic inert and drops the active-row cue the model can no longer resolve");

    const bool stale_release_queued = PushKey(SDL_SCANCODE_F1, false);
    auto stale_released = app->RunWithCapture(1);
    Check(stale_release_queued && stale_released &&
              Access::FrontEnd(*app) == kMainStart,
        "the stale inert-start edge releases before explicit profile navigation");

    Check(PushKey(SDL_SCANCODE_DOWN, true) && app->Run(1).has_value() &&
              Access::FrontEnd(*app) == kMainProfiles,
        "the stale fixture explicitly selects the Profiles row");
    Check(PushKey(SDL_SCANCODE_DOWN, false) && app->Run(1).has_value(),
        "the stale Profiles navigation edge releases");
    Check(PushKey(SDL_SCANCODE_F1, true) && app->Run(1).has_value() &&
              Access::FrontEnd(*app) == kProfilesFirst,
        "the stale fixture explicitly opens the Profiles surface");
    Check(PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value(),
        "the stale Profiles entry edge releases before the failing confirmation");
    const omega::app::GpuHostSnapshot stale_profiles_released_gpu =
        Access::GpuSnapshot(*app);

    // The command is applied, and therefore persisted, before its projected
    // state is published. A rejected command must therefore leave the prior
    // front-end state, the prior activation, and the durable pointer in place.
    const bool stale_confirm_queued = PushKey(SDL_SCANCODE_F1, true);
    auto stale_confirm = app->RunWithCapture(1);
    std::optional<std::string_view> stale_failure;
    if (stale_confirm)
        stale_failure = stale_confirm->failure();
    const std::string database_root_text = database_root.string();
    const std::string profile_id_text = profile_id->ToString();
    Check(stale_confirm_queued && stale_confirm &&
              stale_confirm->completion() ==
                  omega::app::RunCaptureCompletion::OperationalFailure &&
              stale_failure &&
              *stale_failure == "active profile confirmation failed: profile-not-found" &&
              stale_failure->size() < 96U &&
              stale_failure->find(database_root_text) == std::string_view::npos &&
              stale_failure->find(profile_id_text) == std::string_view::npos &&
              Access::FrontEnd(*app) == kProfilesFirst &&
              Access::ActiveProfile(*app) == profile_id &&
              Access::PersistedConfirmedProfile(*app) == profile_id &&
              Access::ActiveCharacter(*app) == character_id &&
              Access::PersistedConfirmedCharacter(*app) == character_id &&
              Access::PersistenceGeneration(*app) ==
                  std::optional<std::uint64_t>{5U} &&
              Access::PersistenceRecordCount(*app) ==
                  std::optional<std::size_t>{5U} &&
              Access::PersistenceLogicalValueBytes(*app) ==
                  std::optional<std::size_t>{221U} &&
              Access::SchedulerSnapshot(*app) == play_scheduler &&
              SameSimulationState(
                  Access::SimulationSnapshot(*app), play_simulation) &&
              Access::GpuSnapshot(*app) == stale_profiles_released_gpu,
        "a rejected confirmation publishes one bounded private error and leaves Profiles, both prior activations, both durable pointers, and the exact GPU snapshot unpublished");

    Check(PushKey(SDL_SCANCODE_F1, false) && app->Run(1).has_value(),
        "the composed acceptance releases its failing confirmation edge before teardown");
}
} // namespace

int main()
{
    using omega::app::RunCaptureCompletion;
    using omega::app::RunResult;

    auto config = omega::runtime::ParseConfigText("");
    Check(config.has_value(), "an empty project configuration parses");
    if (!config)
        return EXIT_FAILURE;

    omega::runtime::RuntimeSettings settings;
    settings.jobs.worker_count = 1U;
    settings.jobs.max_pending_jobs = 8U;
    settings.frame.simulation_step = omega::runtime::kMinimumSimulationStep;
    settings.frame.max_steps_per_frame = 8U;
    settings.frame.max_frame_delta =
        omega::runtime::kMinimumSimulationStep * 8;
    const std::chrono::nanoseconds one_step_elapsed = settings.frame.simulation_step;
    const std::chrono::nanoseconds modal_proof_elapsed =
        settings.frame.simulation_step * 2;
    settings.max_input_events_per_frame =
        omega::runtime::InputTracker::kMaxEventsPerFrameLimit;

    CheckDiagnosticScenePresentationTransactions();

    // Keep the intentionally broad acceptance executable while giving each scenario group its own
    // Debug stack frame. MSVC /Od otherwise reserves every OmegaApp/expected local in one main
    // frame, leaving too little of the default Windows stack for nested SDL/D3D12 driver calls.
    const auto check_startup_and_content = [&settings]() -> int
    {
    auto invalid_config = omega::runtime::ParseConfigText("");
    Check(invalid_config.has_value(), "the invalid-content startup fixture parses config");
    if (!invalid_config)
        return EXIT_FAILURE;
    omega::runtime::ContentStartupState invalid_content;
    invalid_content.level_manifest.emplace();
    const SDL_InitFlags sdl_before_invalid_create = SDL_WasInit(0);
    auto invalid_app = omega::app::detail::OmegaAppTestAccess::Create(
        std::move(*invalid_config), settings, std::move(invalid_content), false);
    Check(!invalid_app &&
              invalid_app.error() ==
                  "content startup state: inconsistent-ownership" &&
              SDL_WasInit(0) == sdl_before_invalid_create,
        "inconsistent content ownership fails with the exact error before touching SDL");

    GeneratedLevelContentTree generated_content;
    Check(generated_content.ready(),
        "the public synthetic LevelContent game-data tree is created");
    if (generated_content.ready())
    {
        auto level_config = omega::runtime::ParseConfigText("");
        auto level_content = BuildLevelContentStartupState(generated_content);
        const bool is_level_content = [&level_content] {
            if (!level_content)
                return false;
            const auto stage =
                omega::runtime::ClassifyContentStartupState(*level_content);
            return stage && *stage == omega::runtime::ContentStartupStage::LevelContent;
        }();
        Check(level_config && level_content && is_level_content,
            "the generated ownership aggregate classifies as LevelContent");
        if (level_config && level_content)
        {
            auto level_app = omega::app::detail::OmegaAppTestAccess::Create(
                std::move(*level_config), settings, std::move(*level_content), false);
            Check(level_app.has_value(),
                "OmegaApp starts with the generated canonical first texture");
            if (level_app)
                CheckLevelContentPresentation(*level_app);
        }

        auto constrained_config = omega::runtime::ParseConfigText("");
        auto constrained_content = BuildLevelContentStartupState(generated_content);
        Check(constrained_config && constrained_content,
            "the transfer-upload budget fallback fixture is ready");
        if (constrained_config && constrained_content)
        {
            constexpr omega::runtime::RenderTexturePoolConfig texture_config{
                .slot_capacity = 64U,
                .maximum_resident_logical_bytes =
                    2ULL * 2ULL * 4ULL + 128ULL * 72ULL * 4ULL * 3ULL +
                    32ULL * 32ULL * 4ULL + 1ULL * 1ULL * 4ULL,
            };
            auto constrained_app =
                omega::app::detail::OmegaAppTestAccess::CreateWithTextureConfig(
                    std::move(*constrained_config), settings,
                    std::move(*constrained_content), false, texture_config);
            Check(constrained_app.has_value(),
                "OmegaApp degrades to topology when the optional transfer exceeds the pool budget");
            if (constrained_app)
            {
                CheckPackedTransferUploadBudgetFallback(
                    *constrained_app, generated_content.root());
            }
        }

        auto mounted_config = omega::runtime::ParseConfigText("");
        auto mounted_content = BuildDataMountedStartupState(generated_content);
        const bool is_data_mounted = [&mounted_content] {
            if (!mounted_content)
                return false;
            const auto stage =
                omega::runtime::ClassifyContentStartupState(*mounted_content);
            return stage && *stage == omega::runtime::ContentStartupStage::DataMounted;
        }();
        Check(mounted_config && mounted_content && is_data_mounted,
            "the generated data-only ownership aggregate classifies as DataMounted");
        if (mounted_config && mounted_content)
        {
            auto mounted_app = omega::app::detail::OmegaAppTestAccess::Create(
                std::move(*mounted_config), settings, std::move(*mounted_content), false);
            Check(mounted_app.has_value(), "OmegaApp starts from the DataMounted stage");
            if (mounted_app)
            {
                const auto mounted_assets =
                    omega::app::detail::OmegaAppTestAccess::AssetSnapshot(*mounted_app);
                const omega::app::GpuHostSnapshot mounted_gpu =
                    omega::app::detail::OmegaAppTestAccess::GpuSnapshot(*mounted_app);
                constexpr std::uint64_t kSyntheticPresentationLogicalBytes =
                    128ULL * 72ULL * 4ULL * 4ULL +
                    96ULL * 32ULL * 4ULL + 1ULL * 1ULL * 4ULL;
                Check(!mounted_assets && mounted_gpu.successful_uploads == 6U &&
                          mounted_gpu.successful_upload_logical_bytes ==
                              kSyntheticPresentationLogicalBytes &&
                          mounted_gpu.textures.resident_slots == 6U &&
                          mounted_gpu.textures.resident_logical_bytes ==
                              kSyntheticPresentationLogicalBytes,
                    "DataMounted retains the synthetic 96x32 topology, actor marker, and exactly 159,748 resident bytes");
            }
        }

        CheckExplicitFirstProfileCreation(generated_content.root(), settings);
        CheckActiveProfileConfirmation(generated_content.root(), settings);
        CheckDiagnosticCampaignStart(generated_content.root(), settings);
        CheckDiagnosticSceneMissionActivation(generated_content, settings);
        CheckComposedGeneratedMenuAcceptance(generated_content.root(), settings);

        const std::filesystem::path profile_database_root =
            generated_content.root() / "native-profile-front-end";
        bool profile_setup_ready = true;
        {
            auto persistence =
                omega::app::NativePersistence::Bootstrap(profile_database_root);
            Check(persistence.has_value(),
                "the synthetic profile-front-end database bootstraps");
            if (!persistence)
            {
                profile_setup_ready = false;
            }
            else
            {
                constexpr std::array profile_ids{
                    std::string_view{"00000000000000000000000000000001"},
                    std::string_view{"00000000000000000000000000000002"},
                    std::string_view{"00000000000000000000000000000003"},
                    std::string_view{"00000000000000000000000000000004"},
                };
                constexpr std::array profile_names{
                    std::string_view{"alpha"},
                    std::string_view{"Jos\xC3\xA9 \xF0\x9F\x98\x80"},
                    std::string_view{"ABCDEFGHIJKLMNOPQRSTUVWX"},
                    std::string_view{"overflow"},
                };
                for (std::size_t index = 0U;
                     profile_setup_ready && index < profile_ids.size(); ++index)
                {
                    const auto id =
                        omega::profiles::ProfileId::Parse(profile_ids[index]);
                    if (!id)
                    {
                        profile_setup_ready = false;
                        break;
                    }
                    auto created = persistence->profiles().Create(*id,
                        omega::profiles::ProfileMetadata{
                            .display_name = std::string(profile_names[index]),
                            .created_unix_milliseconds = index + 1U,
                            .modified_unix_milliseconds = index + 1U,
                        });
                    profile_setup_ready = created.has_value();
                }
                Check(profile_setup_ready,
                    "four explicit sorted native profiles are created without front-end policy");
            }
        }

        auto profile_persistence =
            omega::app::NativePersistence::Bootstrap(profile_database_root);
        auto profile_config = omega::runtime::ParseConfigText("");
        Check(profile_setup_ready && profile_persistence && profile_config &&
                  profile_persistence->startup_profiles().size() == 4U,
            "rebootstrap exposes four sorted startup summaries to the app boundary");
        if (profile_setup_ready && profile_persistence && profile_config)
        {
            const auto expected_model = omega::app::MakeFrontEndStartupModel(
                profile_persistence->startup_profiles());
            auto profile_app =
                omega::app::detail::OmegaAppTestAccess::CreateWithPersistence(
                    std::move(*profile_config), settings,
                    omega::runtime::ContentStartupState{},
                    std::move(*profile_persistence), false);
            constexpr omega::app::FrontEndState kProfilesFirst{
                .mode = omega::app::FrontEndMode::Profiles,
                .selected_main_row = omega::app::FrontEndMainRow::Profiles,
                .selected_profile_slot = omega::app::FrontEndProfileSlot::First,
            };
            constexpr std::uint64_t kProfileStartupLogicalBytes =
                128ULL * 72ULL * 4ULL * 4ULL + 96ULL * 32ULL * 4ULL + 4ULL;
            const omega::app::GpuHostSnapshot profile_startup_gpu = profile_app
                ? omega::app::detail::OmegaAppTestAccess::GpuSnapshot(*profile_app)
                : omega::app::GpuHostSnapshot{};
            Check(expected_model && profile_app &&
                      omega::app::detail::OmegaAppTestAccess::FrontEndModel(
                          *profile_app) == *expected_model &&
                      omega::app::detail::OmegaAppTestAccess::FrontEnd(
                          *profile_app) == kProfilesFirst &&
                      !omega::app::detail::OmegaAppTestAccess::CanCreateFirstProfile(
                          *profile_app) &&
                      !omega::app::detail::OmegaAppTestAccess::ActiveProfile(
                          *profile_app) &&
                      expected_model->total_profiles == 4U &&
                      expected_model->visible_profiles == 3U &&
                      omega::app::detail::OmegaAppTestAccess::ProfileCatalogCount(
                          *profile_app) == std::optional<std::size_t>{4U} &&
                      profile_startup_gpu.successful_uploads == 6U &&
                      profile_startup_gpu.successful_upload_logical_bytes ==
                          kProfileStartupLogicalBytes &&
                      profile_startup_gpu.textures.resident_slots == 6U &&
                      profile_startup_gpu.textures.resident_logical_bytes ==
                          kProfileStartupLogicalBytes &&
                      DrawListsEqual(
                          omega::app::detail::OmegaAppTestAccess::CurrentFrontEndDrawList(
                              *profile_app),
                          omega::app::detail::OmegaAppTestAccess::FrontEndProfileSelectionDrawLists(
                              *profile_app)[0]),
                "OmegaApp opens valid existing profiles at the first slot without implicit selection, mutation, or additional GPU resources");
            if (expected_model && profile_app)
            {
                const auto selected_id = omega::profiles::ProfileId::Parse(
                    "00000000000000000000000000000002");
                const auto character_id = omega::profiles::CharacterId::Parse(
                    "00000000000000000000000000000001");
                const auto run_plain_profile_frame = [&profile_app]() {
                    auto run = profile_app->Run(1);
                    return run && run->rendered_frames == 1 &&
                           !run->quit_requested;
                };
                const bool highlighted_second_frame =
                    PushKey(SDL_SCANCODE_DOWN, true) &&
                    run_plain_profile_frame() &&
                    PushKey(SDL_SCANCODE_DOWN, false) &&
                    run_plain_profile_frame();
                const omega::app::FrontEndState highlighted_second{
                    .mode = omega::app::FrontEndMode::Profiles,
                    .selected_main_row = omega::app::FrontEndMainRow::Profiles,
                    .selected_profile_slot = omega::app::FrontEndProfileSlot::Second,
                };
                const auto profile_selection_lists =
                    omega::app::detail::OmegaAppTestAccess::FrontEndProfileSelectionDrawLists(
                        *profile_app);
                Check(highlighted_second_frame && selected_id &&
                          expected_model->profiles[1].id == selected_id &&
                          !omega::app::detail::OmegaAppTestAccess::ActiveProfile(
                              *profile_app) &&
                          omega::app::detail::OmegaAppTestAccess::FrontEnd(
                              *profile_app) == highlighted_second &&
                          DrawListsEqual(
                              omega::app::detail::OmegaAppTestAccess::CurrentFrontEndDrawList(
                                  *profile_app),
                              profile_selection_lists[1]),
                    "profile navigation advances from the startup-first slot to the second bounded slot without implicit selection");

                const auto scheduler_before_terminal =
                    omega::app::detail::OmegaAppTestAccess::SchedulerSnapshot(
                        *profile_app);
                const auto simulation_before_terminal =
                    omega::app::detail::OmegaAppTestAccess::SimulationSnapshot(
                        *profile_app);
                const auto gpu_before_terminal =
                    omega::app::detail::OmegaAppTestAccess::GpuSnapshot(*profile_app);
                const auto persistence_generation_before_terminal =
                    omega::app::detail::OmegaAppTestAccess::PersistenceGeneration(
                        *profile_app);
                const auto persistence_records_before_terminal =
                    omega::app::detail::OmegaAppTestAccess::PersistenceRecordCount(
                        *profile_app);
                const auto persistence_bytes_before_terminal =
                    omega::app::detail::OmegaAppTestAccess::PersistenceLogicalValueBytes(
                        *profile_app);
                Check(PushKey(SDL_SCANCODE_F1, true) && PushQuit(),
                    "profile selection and host-terminal events enter together");
                auto terminal_selection = profile_app->RunWithCapture(1);
                Check(terminal_selection &&
                          terminal_selection->completion() ==
                              omega::app::RunCaptureCompletion::QuitRequested &&
                          terminal_selection->terminal_input() &&
                          terminal_selection->terminal_input()->host_quit_requested &&
                          !terminal_selection->terminal_input()->logical_quit_pressed &&
                          omega::app::detail::OmegaAppTestAccess::FrontEnd(
                              *profile_app) == highlighted_second &&
                          !omega::app::detail::OmegaAppTestAccess::ActiveProfile(
                              *profile_app) &&
                          !omega::app::detail::OmegaAppTestAccess::PersistedConfirmedProfile(
                              *profile_app) &&
                          omega::app::detail::OmegaAppTestAccess::PersistenceGeneration(
                              *profile_app) == persistence_generation_before_terminal &&
                          omega::app::detail::OmegaAppTestAccess::PersistenceRecordCount(
                              *profile_app) == persistence_records_before_terminal &&
                          omega::app::detail::OmegaAppTestAccess::PersistenceLogicalValueBytes(
                              *profile_app) == persistence_bytes_before_terminal &&
                          omega::app::detail::OmegaAppTestAccess::SchedulerSnapshot(
                              *profile_app) == scheduler_before_terminal &&
                          SameSimulationState(
                              omega::app::detail::OmegaAppTestAccess::SimulationSnapshot(
                                  *profile_app),
                              simulation_before_terminal) &&
                          omega::app::detail::OmegaAppTestAccess::GpuSnapshot(
                              *profile_app) == gpu_before_terminal,
                    "terminal resolution captures but never applies or persists a simultaneous profile-selection action");

                Check(PushKey(SDL_SCANCODE_F1, false) &&
                          run_plain_profile_frame(),
                    "the terminal profile action releases before a fresh explicit selection");
                const auto gpu_before_selection =
                    omega::app::detail::OmegaAppTestAccess::GpuSnapshot(*profile_app);
                Check(PushKey(SDL_SCANCODE_F1, true),
                    "a fresh explicit profile-selection edge enters alone");
                auto selected = profile_app->RunWithCapture(1);
                const omega::app::FrontEndState characters_first{
                    .mode = omega::app::FrontEndMode::Characters,
                    .selected_main_row = omega::app::FrontEndMainRow::Profiles,
                    .selected_profile_slot = omega::app::FrontEndProfileSlot::First,
                    .selected_character_slot =
                        omega::app::FrontEndCharacterSlot::First,
                };
                const omega::app::FrontEndState legacy_returned_profiles_row{
                    .mode = omega::app::FrontEndMode::Main,
                    .selected_main_row = omega::app::FrontEndMainRow::Profiles,
                    .selected_profile_slot = omega::app::FrontEndProfileSlot::First,
                };
                Check(selected && selected_id && character_id &&
                          selected->completion() ==
                              omega::app::RunCaptureCompletion::FrameLimitReached &&
                          omega::app::detail::OmegaAppTestAccess::ActiveProfile(
                              *profile_app) == selected_id &&
                          omega::app::detail::OmegaAppTestAccess::PersistedConfirmedProfile(
                              *profile_app) == selected_id &&
                          omega::app::detail::OmegaAppTestAccess::PersistenceGeneration(
                              *profile_app) == std::optional<std::uint64_t>{5U} &&
                          omega::app::detail::OmegaAppTestAccess::PersistenceRecordCount(
                              *profile_app) == std::optional<std::size_t>{5U} &&
                          omega::app::detail::OmegaAppTestAccess::PersistenceLogicalValueBytes(
                              *profile_app) == std::optional<std::size_t>{207U} &&
                          omega::app::detail::OmegaAppTestAccess::FrontEnd(
                              *profile_app) == characters_first &&
                          !omega::app::detail::OmegaAppTestAccess::ActiveCharacter(
                              *profile_app) &&
                          !omega::app::detail::OmegaAppTestAccess::PersistedConfirmedCharacter(
                              *profile_app) &&
                          omega::app::detail::OmegaAppTestAccess::CharacterCatalogCount(
                              *profile_app, *selected_id) ==
                              std::optional<std::size_t>{0U} &&
                          omega::app::detail::OmegaAppTestAccess::ProfileCatalogCount(
                              *profile_app) == std::optional<std::size_t>{4U} &&
                          IsOneCharacterMenuSubmissionWithTextureDelta(
                              gpu_before_selection,
                              omega::app::detail::OmegaAppTestAccess::GpuSnapshot(
                                  *profile_app),
                              2U, 0U, 2U),
                    "a fresh primary edge commits the highlighted existing ID and opens empty Characters with both cards resident");

                auto replay_traces = selected
                                         ? std::move(*selected).TakeTracePair()
                                         : std::nullopt;
                omega::app::RunReplaySessionConfig replay_config{};
                replay_config.scheduler = settings.frame;
                replay_config.maximum_entities = 1U;
                replay_config.initial_front_end_state = highlighted_second;
                replay_config.front_end_visible_profile_slots = 3U;
                replay_config.front_end_total_profile_count = 4U;
                bool replay_matches = false;
                if (replay_traces)
                {
                    auto replay = omega::app::RunReplaySession::Create(
                        std::move(*replay_traces), replay_config);
                    if (replay)
                    {
                        auto replayed_frame = replay->Next();
                        replay_matches = replayed_frame &&
                                         replayed_frame->front_end_command() ==
                                             omega::app::FrontEndCommand{
                                                 .type = omega::app::FrontEndCommandType::SetActiveProfile,
                                                 .profile_slot = omega::app::FrontEndProfileSlot::Second,
                                             } &&
                                         replay->front_end_state() ==
                                             legacy_returned_profiles_row;
                    }
                }
                Check(replay_matches,
                    "the unchanged capture schema deterministically replays the same bounded selection command");
                Check(PushKey(SDL_SCANCODE_F1, false) && run_plain_profile_frame(),
                    "the explicit profile-selection key releases before character creation");

                const auto gpu_before_character_create =
                    omega::app::detail::OmegaAppTestAccess::GpuSnapshot(
                        *profile_app);
                Check(PushKey(SDL_SCANCODE_F1, true) &&
                          run_plain_profile_frame() && selected_id &&
                          omega::app::detail::OmegaAppTestAccess::FrontEnd(
                              *profile_app) == characters_first &&
                          omega::app::detail::OmegaAppTestAccess::CharacterCatalogCount(
                              *profile_app, *selected_id) ==
                              std::optional<std::size_t>{1U} &&
                          omega::app::detail::OmegaAppTestAccess::PersistenceGeneration(
                              *profile_app) == std::optional<std::uint64_t>{6U} &&
                          omega::app::detail::OmegaAppTestAccess::PersistenceRecordCount(
                              *profile_app) == std::optional<std::size_t>{6U} &&
                          omega::app::detail::OmegaAppTestAccess::PersistenceLogicalValueBytes(
                              *profile_app) == std::optional<std::size_t>{259U} &&
                          IsOneCharacterMenuSubmissionWithTextureDelta(
                              gpu_before_character_create,
                              omega::app::detail::OmegaAppTestAccess::GpuSnapshot(
                                  *profile_app),
                              0U, 1U, 3U),
                    "the selected second profile creates its fixed character and releases the obsolete empty card");
                Check(PushKey(SDL_SCANCODE_F1, false) &&
                          run_plain_profile_frame(),
                    "the four-profile character-creation edge releases");

                const auto gpu_before_character_select =
                    omega::app::detail::OmegaAppTestAccess::GpuSnapshot(
                        *profile_app);
                const omega::app::FrontEndState briefing_room{
                    .mode = omega::app::FrontEndMode::BriefingRoom,
                    .selected_main_row =
                        omega::app::FrontEndMainRow::StartDiagnostic,
                    .selected_profile_slot =
                        omega::app::FrontEndProfileSlot::First,
                    .selected_character_slot =
                        omega::app::FrontEndCharacterSlot::First,
                };
                Check(PushKey(SDL_SCANCODE_F1, true) &&
                          run_plain_profile_frame() && character_id &&
                          omega::app::detail::OmegaAppTestAccess::FrontEnd(
                              *profile_app) == briefing_room &&
                          omega::app::detail::OmegaAppTestAccess::ActiveCharacter(
                              *profile_app) == character_id &&
                          omega::app::detail::OmegaAppTestAccess::PersistedConfirmedCharacter(
                              *profile_app) == character_id &&
                          omega::app::detail::OmegaAppTestAccess::PersistenceGeneration(
                              *profile_app) == std::optional<std::uint64_t>{7U} &&
                          omega::app::detail::OmegaAppTestAccess::PersistenceRecordCount(
                              *profile_app) == std::optional<std::size_t>{7U} &&
                          omega::app::detail::OmegaAppTestAccess::PersistenceLogicalValueBytes(
                              *profile_app) == std::optional<std::size_t>{307U} &&
                          IsOneVisibleMenuSubmission(
                              gpu_before_character_select,
                              omega::app::detail::OmegaAppTestAccess::GpuSnapshot(
                                  *profile_app)),
                    "the four-profile character selection commits its pointer and enters the Briefing Room mission selector");
                Check(PushKey(SDL_SCANCODE_F1, false) &&
                          run_plain_profile_frame(),
                    "the four-profile character-selection edge releases");

                const auto gpu_before_start =
                    omega::app::detail::OmegaAppTestAccess::GpuSnapshot(
                        *profile_app);
                const omega::app::FrontEndState diagnostic_play{
                    .mode = omega::app::FrontEndMode::DiagnosticPlay,
                    .selected_main_row =
                        omega::app::FrontEndMainRow::StartDiagnostic,
                    .selected_profile_slot =
                        omega::app::FrontEndProfileSlot::First,
                    .selected_character_slot =
                        omega::app::FrontEndCharacterSlot::First,
                };
                Check(PushKey(SDL_SCANCODE_F1, true) &&
                          run_plain_profile_frame() &&
                          omega::app::detail::OmegaAppTestAccess::FrontEnd(
                              *profile_app) == diagnostic_play &&
                          omega::app::detail::OmegaAppTestAccess::PersistenceGeneration(
                              *profile_app) == std::optional<std::uint64_t>{8U} &&
                          omega::app::detail::OmegaAppTestAccess::PersistenceRecordCount(
                              *profile_app) == std::optional<std::size_t>{8U} &&
                          omega::app::detail::OmegaAppTestAccess::PersistenceLogicalValueBytes(
                              *profile_app) == std::optional<std::size_t>{355U} &&
                          IsOneDiagnosticPlaySubmission(gpu_before_start,
                              omega::app::detail::OmegaAppTestAccess::GpuSnapshot(
                                  *profile_app)),
                    "the four-profile native path commits its character-owned session and enters DiagnosticPlay");
                Check(PushKey(SDL_SCANCODE_F1, false) &&
                          run_plain_profile_frame(),
                    "the four-profile diagnostic-start edge releases before teardown");
            }
        }
    }

    GeneratedLevelContentTree generated_non_packed_content(
        GeneratedTextureKind::Packed32);
    Check(generated_non_packed_content.ready(),
        "the public synthetic Packed32 LevelContent tree is created");
    if (generated_non_packed_content.ready())
    {
        auto fallback_config = omega::runtime::ParseConfigText("");
        auto fallback_content =
            BuildLevelContentStartupState(generated_non_packed_content);
        const bool is_level_content = [&fallback_content] {
            if (!fallback_content)
                return false;
            const auto stage =
                omega::runtime::ClassifyContentStartupState(*fallback_content);
            return stage && *stage == omega::runtime::ContentStartupStage::LevelContent;
        }();
        Check(fallback_config && fallback_content && is_level_content,
            "the Packed32 ownership aggregate classifies as LevelContent");
        if (fallback_config && fallback_content)
        {
            auto fallback_app = omega::app::detail::OmegaAppTestAccess::Create(
                std::move(*fallback_config), settings,
                std::move(*fallback_content), false);
            Check(fallback_app.has_value(),
                "OmegaApp accepts non-Packed24 LevelContent through topology fallback");
            if (fallback_app)
                CheckNonPackedLevelContentFallback(
                    *fallback_app, generated_non_packed_content.root());
        }
    }

    auto marker_capacity_config = omega::runtime::ParseConfigText("");
    Check(marker_capacity_config.has_value(),
        "the diagnostic actor marker capacity fixture parses");
    if (marker_capacity_config)
    {
        constexpr omega::runtime::RenderTexturePoolConfig texture_config{
            .slot_capacity = 5U,
        };
        auto constrained =
            omega::app::detail::OmegaAppTestAccess::CreateWithTextureConfig(
                std::move(*marker_capacity_config), settings,
                omega::runtime::ContentStartupState{}, false, texture_config);
        Check(!constrained && constrained.error() ==
                  "SDL/GPU diagnostic actor marker texture upload: render texture reserve: slot-capacity-exceeded",
            "the mandatory actor marker reports its exact startup upload failure after the five earlier textures");
    }

    return EXIT_SUCCESS;
    };
    if (check_startup_and_content() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    const auto check_runtime = [&]() -> int
    {
    auto app = omega::app::detail::OmegaAppTestAccess::Create(
        std::move(*config), settings, omega::runtime::ContentStartupState{}, false);
    Check(app.has_value(), "the zero-file OmegaApp fixture starts");
    if (!app)
    {
        std::cerr << app.error() << '\n';
        return EXIT_FAILURE;
    }

    using omega::runtime::InputDevice;
    using omega::app::detail::OmegaAppTestAccess;
    Check(OmegaAppTestAccess::DebugLocomotionPosition(*app) ==
              omega::simulation::Position3{},
        "the host creates one positioned synthetic diagnostic entity at the origin");
    Check(SameProximityTriggerState(
              OmegaAppTestAccess::DiagnosticProximityTriggerState(*app), {}),
        "the app owns one default launch-local diagnostic proximity trigger state");
    Check(OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
              static_cast<std::uint16_t>(SDL_SCANCODE_W),
              omega::app::kDebugMoveForwardAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
                  static_cast<std::uint16_t>(SDL_SCANCODE_UP),
                  omega::app::kDebugMoveForwardAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
                  static_cast<std::uint16_t>(SDL_SCANCODE_S),
                  omega::app::kDebugMoveBackwardAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
                  static_cast<std::uint16_t>(SDL_SCANCODE_DOWN),
                  omega::app::kDebugMoveBackwardAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
                  static_cast<std::uint16_t>(SDL_SCANCODE_A),
                  omega::app::kDebugMoveLeftAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
                  static_cast<std::uint16_t>(SDL_SCANCODE_LEFT),
                  omega::app::kDebugMoveLeftAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
                  static_cast<std::uint16_t>(SDL_SCANCODE_D),
                  omega::app::kDebugMoveRightAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
                  static_cast<std::uint16_t>(SDL_SCANCODE_RIGHT),
                  omega::app::kDebugMoveRightAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::GamepadButton,
                  static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_DPAD_UP),
                  omega::app::kDebugMoveForwardAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::GamepadButton,
                  static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_DPAD_DOWN),
                  omega::app::kDebugMoveBackwardAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::GamepadButton,
                  static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_DPAD_LEFT),
                  omega::app::kDebugMoveLeftAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::GamepadButton,
                  static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_DPAD_RIGHT),
                  omega::app::kDebugMoveRightAction),
        "WASD and the arrow keys provide a complete keyboard movement and menu-navigation path while the gamepad dpad remains an optional alias");
    Check(OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
              static_cast<std::uint16_t>(SDL_SCANCODE_ESCAPE),
              omega::app::kFrontEndCancelAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
                  static_cast<std::uint16_t>(SDL_SCANCODE_F10),
                  OmegaAppTestAccess::QuitAction()) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::GamepadButton,
                  static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_BACK),
                  OmegaAppTestAccess::QuitAction()) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
              static_cast<std::uint16_t>(SDL_SCANCODE_F1),
              omega::app::kFrontEndPrimaryAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
                  static_cast<std::uint16_t>(SDL_SCANCODE_RETURN),
                  omega::app::kFrontEndPrimaryAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
                  static_cast<std::uint16_t>(SDL_SCANCODE_KP_ENTER),
                  omega::app::kFrontEndPrimaryAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::GamepadButton,
                  static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_START),
                  omega::app::kFrontEndPrimaryAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::GamepadButton,
                  static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_SOUTH),
                  omega::app::kFrontEndPrimaryAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
                  static_cast<std::uint16_t>(SDL_SCANCODE_BACKSPACE),
                  omega::app::kFrontEndCancelAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
                  static_cast<std::uint16_t>(SDL_SCANCODE_SPACE),
                  omega::app::kDebugFireAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::MouseButton,
                  static_cast<std::uint16_t>(SDL_BUTTON_LEFT),
                  omega::app::kDebugFireAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
                  static_cast<std::uint16_t>(SDL_SCANCODE_T),
                  omega::app::kDebugTargetAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::MouseButton,
                  static_cast<std::uint16_t>(SDL_BUTTON_RIGHT),
                  omega::app::kDebugTargetAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::GamepadButton,
                  static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_EAST),
                  omega::app::kFrontEndCancelAction) &&
              OmegaAppTestAccess::InputBindingCount(*app) == 26U &&
              OmegaAppTestAccess::InputActionCount(*app) == 9U,
        "twenty-six physical bindings expose a controller-free nine-action keyboard/mouse layout; gamepad buttons remain optional aliases");

    const omega::runtime::RenderTextureHandle diagnostic_texture =
        OmegaAppTestAccess::DiagnosticTexture(*app);
    const omega::runtime::RenderTextureHandle diagnostic_actor_marker_texture =
        OmegaAppTestAccess::DiagnosticActorMarkerTexture(*app);
    const omega::runtime::RenderTextureHandle front_end_texture =
        OmegaAppTestAccess::FrontEndTexture(*app);
    const omega::runtime::RenderTextureHandle front_end_profiles_texture =
        OmegaAppTestAccess::FrontEndProfilesTexture(*app);
    const omega::runtime::RenderTextureHandle diagnostic_controls_texture =
        OmegaAppTestAccess::DiagnosticControlsTexture(*app);
    const omega::runtime::RenderTextureHandle diagnostic_asset_topology_texture =
        OmegaAppTestAccess::DiagnosticAssetTopologyTexture(*app);
    const omega::runtime::RenderTextureHandle diagnostic_asset_transfer_texture =
        OmegaAppTestAccess::DiagnosticAssetTransferTexture(*app);
    Check(!diagnostic_asset_transfer_texture.valid(),
        "zero-file startup retains no owner-derived transfer texture");
    const omega::runtime::RenderDrawList initial_hidden_draw_list =
        OmegaAppTestAccess::DiagnosticHiddenDrawList(*app);
    const omega::runtime::RenderDrawList initial_actor_draw_list =
        OmegaAppTestAccess::DiagnosticActorDrawList(*app);
    const std::array<omega::runtime::RenderDrawList,
        omega::app::kFrontEndMainRowCount> initial_visible_draw_lists =
        OmegaAppTestAccess::FrontEndMainDrawLists(*app);
    const omega::runtime::RenderDrawList initial_controls_draw_list =
        OmegaAppTestAccess::DiagnosticControlsDrawList(*app);
    const omega::runtime::RenderDrawList initial_profiles_draw_list =
        OmegaAppTestAccess::FrontEndProfilesDrawList(*app);
    const std::array<omega::runtime::RenderDrawList,
        omega::app::kFrontEndVisibleProfiles> initial_profile_selection_draw_lists =
        OmegaAppTestAccess::FrontEndProfileSelectionDrawLists(*app);
    const omega::runtime::RenderDrawList initial_asset_topology_draw_list =
        OmegaAppTestAccess::DiagnosticAssetTopologyDrawList(*app);
    constexpr omega::runtime::RenderSourceRectQ16 kFullMenuSource{
        .left = 0U,
        .top = 0U,
        .right = omega::runtime::kNormalizedRenderExtent,
        .bottom = omega::runtime::kNormalizedRenderExtent,
    };
    constexpr omega::runtime::RenderTargetRectQ16 kFullTarget{
        .left = 0U,
        .top = 0U,
        .right = omega::runtime::kNormalizedRenderExtent,
        .bottom = omega::runtime::kNormalizedRenderExtent,
    };
    constexpr omega::runtime::RenderTargetRectQ16 kActorOriginDestination{
        .left = 31'744U,
        .top = 31'744U,
        .right = 33'792U,
        .bottom = 33'792U,
    };
    constexpr std::array kPositionedTargetCueDestinations{
        omega::runtime::RenderTargetRectQ16{
            .left = 12'288U,
            .top = 48'896U,
            .right = 20'480U,
            .bottom = 49'408U,
        },
        omega::runtime::RenderTargetRectQ16{
            .left = 16'128U,
            .top = 45'056U,
            .right = 16'640U,
            .bottom = 53'248U,
        },
    };
    constexpr omega::runtime::RenderTargetRectQ16 kFireCueDestination{
        .left = 32'000U,
        .top = 32'000U,
        .right = 33'536U,
        .bottom = 33'536U,
    };
    constexpr omega::runtime::RenderTargetRectQ16
        kPositionedFireCueDestination{
        .left = 15'616U,
        .top = 48'384U,
        .right = 17'152U,
        .bottom = 49'920U,
    };
    constexpr omega::runtime::RenderTargetRectQ16 kMenuDestination{
        .left = 2048U,
        .top = 2048U,
        .right = 26624U,
        .bottom = 15872U,
    };
    constexpr omega::runtime::RenderSourceRectQ16 kMenuSelectionSource{
        .left = 18432U,
        .top = 9103U,
        .right = 59392U,
        .bottom = 14563U,
    };
    constexpr omega::runtime::RenderSourceRectQ16 kProfileSelectionSource{
        .left = 0U,
        .top = 0U,
        .right = 512U,
        .bottom = 512U,
    };
    constexpr std::array kMenuSelectionTargets{
        omega::runtime::RenderTargetRectQ16{
            .left = 3584U,
            .top = 7424U,
            .right = 4352U,
            .bottom = 8960U,
        },
        omega::runtime::RenderTargetRectQ16{
            .left = 3584U,
            .top = 9344U,
            .right = 4352U,
            .bottom = 10880U,
        },
        omega::runtime::RenderTargetRectQ16{
            .left = 3584U,
            .top = 11264U,
            .right = 4352U,
            .bottom = 12800U,
        },
        omega::runtime::RenderTargetRectQ16{
            .left = 3584U,
            .top = 13184U,
            .right = 4352U,
            .bottom = 14720U,
        },
    };
    static_assert(kMenuSelectionTargets.size() ==
                  omega::app::kFrontEndMainRowCount);
    const auto hidden_commands = initial_hidden_draw_list.commands();
    const bool hidden_list_is_exact =
        hidden_commands.size() == 1U &&
        hidden_commands[0].texture == diagnostic_texture &&
        hidden_commands[0].source == kFullMenuSource &&
        hidden_commands[0].destination == kFullTarget &&
        hidden_commands[0].fit_mode ==
            omega::runtime::RenderTextureFitMode::Contain &&
        hidden_commands[0].filter_mode ==
            omega::runtime::RenderTextureFilterMode::Nearest;
    const auto actor_commands = initial_actor_draw_list.commands();
    constexpr auto kObjectiveDestination =
        omega::app::PlanProjectDiagnosticObjectiveMarkerDestination(
            omega::gameplay::DiagnosticProximityTriggerState{});
    static_assert(kObjectiveDestination.has_value());
    const bool actor_list_is_exact =
        actor_commands.size() == 3U && hidden_commands.size() == 1U &&
        actor_commands[0] == hidden_commands[0] &&
        actor_commands[1].texture == diagnostic_actor_marker_texture &&
        actor_commands[1].source == kFullMenuSource &&
        actor_commands[1].destination == kActorOriginDestination &&
        actor_commands[1].fit_mode ==
            omega::runtime::RenderTextureFitMode::Stretch &&
        actor_commands[1].filter_mode ==
            omega::runtime::RenderTextureFilterMode::Nearest &&
        actor_commands[2].texture == diagnostic_actor_marker_texture &&
        actor_commands[2].source == kFullMenuSource &&
        actor_commands[2].destination == *kObjectiveDestination &&
        actor_commands[2].fit_mode ==
            omega::runtime::RenderTextureFitMode::Stretch &&
        actor_commands[2].filter_mode ==
            omega::runtime::RenderTextureFilterMode::Nearest;
    bool visible_lists_are_exact = true;
    for (std::size_t row = 0U; row < initial_visible_draw_lists.size(); ++row)
    {
        const auto commands = initial_visible_draw_lists[row].commands();
        visible_lists_are_exact = visible_lists_are_exact &&
                                  commands.size() == hidden_commands.size() + 2U;
        for (std::size_t index = 0U;
             visible_lists_are_exact && index < hidden_commands.size(); ++index)
        {
            visible_lists_are_exact = commands[index] == hidden_commands[index];
        }
        if (!visible_lists_are_exact)
            break;

        const auto& card = commands[hidden_commands.size()];
        const auto& marker = commands[hidden_commands.size() + 1U];
        visible_lists_are_exact =
            card.texture == front_end_texture &&
            card.source == kFullMenuSource && card.destination == kMenuDestination &&
            card.fit_mode == omega::runtime::RenderTextureFitMode::Stretch &&
            card.filter_mode == omega::runtime::RenderTextureFilterMode::Nearest &&
            marker.texture == front_end_texture &&
            marker.source == kMenuSelectionSource &&
            marker.destination == kMenuSelectionTargets[row] &&
            marker.fit_mode == omega::runtime::RenderTextureFitMode::Stretch &&
            marker.filter_mode == omega::runtime::RenderTextureFilterMode::Nearest;
    }
    const auto controls_commands = initial_controls_draw_list.commands();
    bool controls_list_is_exact =
        controls_commands.size() == hidden_commands.size() + 1U;
    for (std::size_t index = 0U;
         controls_list_is_exact && index < hidden_commands.size(); ++index)
    {
        controls_list_is_exact = controls_commands[index] == hidden_commands[index];
    }
    if (controls_list_is_exact)
    {
        const auto& controls_card = controls_commands[hidden_commands.size()];
        controls_list_is_exact =
            controls_card.texture == diagnostic_controls_texture &&
            controls_card.source == kFullMenuSource &&
            controls_card.destination == kMenuDestination &&
            controls_card.fit_mode == omega::runtime::RenderTextureFitMode::Stretch &&
            controls_card.filter_mode == omega::runtime::RenderTextureFilterMode::Nearest;
    }
    const auto profiles_commands = initial_profiles_draw_list.commands();
    bool profiles_list_is_exact =
        profiles_commands.size() == hidden_commands.size() + 1U;
    for (std::size_t index = 0U;
         profiles_list_is_exact && index < hidden_commands.size(); ++index)
    {
        profiles_list_is_exact = profiles_commands[index] == hidden_commands[index];
    }
    if (profiles_list_is_exact)
    {
        const auto& profiles_card = profiles_commands[hidden_commands.size()];
        profiles_list_is_exact =
            profiles_card.texture == front_end_profiles_texture &&
            profiles_card.source == kFullMenuSource &&
            profiles_card.destination == kMenuDestination &&
            profiles_card.fit_mode == omega::runtime::RenderTextureFitMode::Stretch &&
            profiles_card.filter_mode == omega::runtime::RenderTextureFilterMode::Nearest;
    }
    bool profile_selection_lists_are_exact = profiles_list_is_exact;
    for (std::size_t slot = 0U;
         profile_selection_lists_are_exact && slot < initial_profile_selection_draw_lists.size(); ++slot)
    {
        const auto commands = initial_profile_selection_draw_lists[slot].commands();
        profile_selection_lists_are_exact = commands.size() == profiles_commands.size() + 1U;
        for (std::size_t index = 0U;
             profile_selection_lists_are_exact && index < profiles_commands.size(); ++index)
        {
            profile_selection_lists_are_exact = commands[index] == profiles_commands[index];
        }
        if (profile_selection_lists_are_exact)
        {
            const auto& marker = commands[profiles_commands.size()];
            profile_selection_lists_are_exact =
                marker.texture == front_end_profiles_texture &&
                marker.source == kProfileSelectionSource &&
                marker.destination == kMenuSelectionTargets[slot] &&
                marker.fit_mode == omega::runtime::RenderTextureFitMode::Stretch &&
                marker.filter_mode == omega::runtime::RenderTextureFilterMode::Nearest;
        }
    }
    const auto asset_topology_commands = initial_asset_topology_draw_list.commands();
    bool asset_topology_list_is_exact =
        asset_topology_commands.size() == hidden_commands.size() + 1U;
    for (std::size_t index = 0U;
         asset_topology_list_is_exact && index < hidden_commands.size(); ++index)
    {
        asset_topology_list_is_exact =
            asset_topology_commands[index] == hidden_commands[index];
    }
    if (asset_topology_list_is_exact)
    {
        const auto& asset_topology_card =
            asset_topology_commands[hidden_commands.size()];
        asset_topology_list_is_exact =
            asset_topology_card.texture == diagnostic_asset_topology_texture &&
            asset_topology_card.source == kFullMenuSource &&
            asset_topology_card.destination == kMenuDestination &&
            asset_topology_card.fit_mode ==
                omega::runtime::RenderTextureFitMode::Contain &&
            asset_topology_card.filter_mode ==
                omega::runtime::RenderTextureFilterMode::Nearest;
    }
    Check(diagnostic_texture.valid() &&
              diagnostic_actor_marker_texture.valid() &&
              front_end_texture.valid() &&
              front_end_profiles_texture.valid() &&
              diagnostic_controls_texture.valid() &&
              diagnostic_asset_topology_texture.valid() &&
              diagnostic_texture != front_end_texture &&
              diagnostic_texture != front_end_profiles_texture &&
              diagnostic_texture != diagnostic_controls_texture &&
              diagnostic_texture != diagnostic_asset_topology_texture &&
              diagnostic_texture != diagnostic_actor_marker_texture &&
              front_end_texture != front_end_profiles_texture &&
              front_end_texture != diagnostic_controls_texture &&
              front_end_texture != diagnostic_asset_topology_texture &&
              front_end_texture != diagnostic_actor_marker_texture &&
              front_end_profiles_texture != diagnostic_actor_marker_texture &&
              diagnostic_controls_texture != diagnostic_actor_marker_texture &&
              diagnostic_controls_texture != diagnostic_asset_topology_texture &&
              diagnostic_asset_topology_texture !=
                  diagnostic_actor_marker_texture &&
              diagnostic_texture.pool_identity ==
                  front_end_texture.pool_identity &&
              diagnostic_texture.pool_identity ==
                  front_end_profiles_texture.pool_identity &&
              diagnostic_texture.pool_identity ==
                  diagnostic_controls_texture.pool_identity &&
              diagnostic_texture.pool_identity ==
                  diagnostic_asset_topology_texture.pool_identity &&
              diagnostic_texture.pool_identity ==
                  diagnostic_actor_marker_texture.pool_identity &&
              diagnostic_texture.slot_index == 0U &&
              front_end_texture.slot_index == 1U &&
              front_end_profiles_texture.slot_index == 2U &&
              diagnostic_controls_texture.slot_index == 3U &&
              diagnostic_asset_topology_texture.slot_index == 4U &&
              diagnostic_actor_marker_texture.slot_index == 5U &&
              OmegaAppTestAccess::FrontEndModel(*app) ==
                  omega::app::FrontEndStartupModel{} &&
              !OmegaAppTestAccess::CanCreateFirstProfile(*app) &&
              !OmegaAppTestAccess::ActiveProfile(*app) &&
              OmegaAppTestAccess::FrontEnd(*app) ==
                  omega::app::InitialFrontEndState() &&
              hidden_list_is_exact && actor_list_is_exact &&
              visible_lists_are_exact &&
              profiles_list_is_exact && profile_selection_lists_are_exact &&
              controls_list_is_exact &&
              asset_topology_list_is_exact &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  initial_visible_draw_lists[0]),
        "the zero-file host without persistence fails closed to Main while uploading distinct diagnostic, main, profiles, controls, topology, and actor textures in exact order and owning every immutable list");

    OmegaAppTestAccess::SetFrontEndState(*app,
        omega::app::FrontEndState{
            .mode = static_cast<omega::app::FrontEndMode>(255U),
            .selected_main_row = omega::app::FrontEndMainRow::StartDiagnostic,
        });
    Check(DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
              initial_visible_draw_lists[0]),
        "an invalid front-end mode normalizes to the initial main-row draw list");
    OmegaAppTestAccess::SetFrontEndState(*app,
        omega::app::FrontEndState{
            .mode = omega::app::FrontEndMode::Main,
            .selected_main_row = static_cast<omega::app::FrontEndMainRow>(255U),
        });
    Check(DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
              initial_visible_draw_lists[0]),
        "an invalid main row normalizes to the initial main-row draw list");
    OmegaAppTestAccess::SetFrontEndState(*app,
        omega::app::FrontEndState{
            .mode = omega::app::FrontEndMode::Profiles,
            .selected_main_row = omega::app::FrontEndMainRow::Profiles,
        });
    Check(DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
              initial_profiles_draw_list),
        "a valid Profiles state selects the immutable profile-card draw list");
    OmegaAppTestAccess::SetFrontEndState(*app,
        omega::app::FrontEndState{
            .mode = omega::app::FrontEndMode::Controls,
            .selected_main_row = omega::app::FrontEndMainRow::Controls,
        });
    Check(DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
              initial_controls_draw_list),
        "a valid Controls state selects the exact immutable controls draw list");
    OmegaAppTestAccess::SetFrontEndState(*app,
        omega::app::FrontEndState{
            .mode = omega::app::FrontEndMode::Controls,
            .selected_main_row = static_cast<omega::app::FrontEndMainRow>(255U),
        });
    Check(DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
              initial_visible_draw_lists[0]),
        "an invalid Controls row normalizes to the initial main-row draw list");
    OmegaAppTestAccess::SetFrontEndState(*app,
        omega::app::FrontEndState{
            .mode = omega::app::FrontEndMode::AssetTopology,
            .selected_main_row = omega::app::FrontEndMainRow::AssetTopology,
        });
    Check(DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
              initial_asset_topology_draw_list),
        "a valid AssetTopology state selects the exact immutable topology draw list");
    OmegaAppTestAccess::SetFrontEndState(*app,
        omega::app::FrontEndState{
            .mode = omega::app::FrontEndMode::AssetTopology,
            .selected_main_row = static_cast<omega::app::FrontEndMainRow>(255U),
        });
    Check(DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
              initial_visible_draw_lists[0]),
        "an invalid AssetTopology row normalizes to the initial main-row draw list");
    OmegaAppTestAccess::SetFrontEndState(*app, omega::app::FrontEndState{});
    Check(DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
              initial_actor_draw_list),
        "a valid DiagnosticPlay state selects the base-actor-objective draw list");
    OmegaAppTestAccess::SetFrontEndState(
        *app, omega::app::InitialFrontEndState());

    constexpr std::uint64_t kFrontEndLogicalBytes = 128ULL * 72ULL * 4ULL;
    constexpr std::uint64_t kDiagnosticPresentationLogicalBytes =
        kFrontEndLogicalBytes * 4ULL + 96ULL * 32ULL * 4ULL +
        1ULL * 1ULL * 4ULL;
    const omega::app::GpuHostSnapshot initial_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(initial_gpu.successful_uploads == 6U &&
              initial_gpu.successful_upload_logical_bytes ==
                  kDiagnosticPresentationLogicalBytes &&
              initial_gpu.successful_releases == 0U &&
              initial_gpu.textures.reserved_slots == 0U &&
              initial_gpu.textures.resident_slots == 6U &&
              initial_gpu.textures.resident_logical_bytes ==
                  kDiagnosticPresentationLogicalBytes,
        "the four 128x72 cards, 96x32 topology image, and actor marker own exactly 159,748 no-level resident logical bytes");

    // Stable main-card probes cover the frame, project header, content/profile
    // count line, and all four row panels without depending on a platform font.
    constexpr std::array menu_probe_coordinates{
        std::array{4U, 4U}, std::array{0U, 0U}, std::array{8U, 8U},
        std::array{9U, 8U}, std::array{40U, 12U}, std::array{8U, 22U},
        std::array{9U, 22U}, std::array{40U, 22U}, std::array{104U, 22U},
        std::array{8U, 28U}, std::array{13U, 28U}, std::array{8U, 38U},
        std::array{13U, 38U}, std::array{8U, 48U}, std::array{13U, 48U},
        std::array{8U, 58U},
    };
    constexpr omega::runtime::RenderClearColorRgba8 probe_background{
        .red = 8U, .green = 12U, .blue = 24U, .alpha = 255U};
    constexpr omega::runtime::RenderClearColorRgba8 probe_cyan{
        .red = 112U, .green = 220U, .blue = 255U, .alpha = 255U};
    constexpr omega::runtime::RenderClearColorRgba8 probe_slate{
        .red = 28U, .green = 38U, .blue = 58U, .alpha = 255U};
    constexpr omega::runtime::RenderClearColorRgba8 probe_amber{
        .red = 255U, .green = 196U, .blue = 64U, .alpha = 255U};
    constexpr std::array expected_menu_probe_readback{
        probe_background, probe_cyan, probe_slate, probe_cyan,
        probe_amber, probe_background, probe_cyan, probe_cyan,
        probe_cyan, probe_cyan, probe_slate, probe_cyan,
        probe_slate, probe_cyan, probe_slate, probe_cyan,
    };
    constexpr auto source_begin = [](const std::uint32_t coordinate,
                                      const std::uint32_t dimension) noexcept {
        return static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(coordinate) *
                 omega::runtime::kNormalizedRenderExtent +
                dimension - 1U) /
            dimension);
    };
    constexpr auto source_end = [](const std::uint32_t coordinate,
                                    const std::uint32_t dimension) noexcept {
        return static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(coordinate + 1U) *
            omega::runtime::kNormalizedRenderExtent / dimension);
    };
    constexpr auto destination_edge = [](const std::uint32_t coordinate) noexcept {
        return coordinate * (omega::runtime::kNormalizedRenderExtent / 4U);
    };
    constexpr std::array no_level_probe_coordinates{
        std::array{0U, 0U}, std::array{4U, 4U}, std::array{8U, 8U},
        std::array{9U, 8U}, std::array{40U, 12U}, std::array{33U, 24U},
        std::array{34U, 24U}, std::array{7U, 34U}, std::array{8U, 34U},
        std::array{35U, 40U}, std::array{36U, 40U}, std::array{8U, 52U},
        std::array{8U, 56U}, std::array{12U, 59U}, std::array{83U, 59U},
        std::array{84U, 59U},
    };
    constexpr std::array expected_no_level_probe_readback{
        probe_cyan, probe_background, probe_slate, probe_cyan,
        probe_amber, probe_background, probe_cyan, probe_background,
        probe_slate, probe_slate, probe_cyan, probe_background,
        probe_slate, probe_cyan, probe_slate, probe_slate,
    };
    std::array<omega::runtime::RenderTextureBlitCommand, 16U>
        no_level_probe_commands{};
    for (std::size_t index = 0U; index < no_level_probe_commands.size(); ++index)
    {
        const std::uint32_t x = no_level_probe_coordinates[index][0];
        const std::uint32_t y = no_level_probe_coordinates[index][1];
        const std::uint32_t column = static_cast<std::uint32_t>(index % 4U);
        const std::uint32_t row = static_cast<std::uint32_t>(index / 4U);
        no_level_probe_commands[index] =
            omega::runtime::RenderTextureBlitCommand{
                .texture = diagnostic_texture,
                .source = omega::runtime::RenderSourceRectQ16{
                    .left = source_begin(
                        x, omega::app::kFrontEndImageWidth),
                    .top = source_begin(
                        y, omega::app::kFrontEndImageHeight),
                    .right = source_end(
                        x, omega::app::kFrontEndImageWidth),
                    .bottom = source_end(
                        y, omega::app::kFrontEndImageHeight),
                },
                .destination = omega::runtime::RenderTargetRectQ16{
                    .left = destination_edge(column),
                    .top = destination_edge(row),
                    .right = destination_edge(column + 1U),
                    .bottom = destination_edge(row + 1U),
                },
                .fit_mode = omega::runtime::RenderTextureFitMode::Stretch,
                .filter_mode = omega::runtime::RenderTextureFilterMode::Nearest,
            };
    }
    auto no_level_probe_draw_list =
        omega::runtime::RenderDrawList::Create(no_level_probe_commands);
    Check(no_level_probe_draw_list.has_value(),
        "the sixteen one-texel no-level readback commands form a valid draw list");
    if (no_level_probe_draw_list)
    {
        omega::runtime::RenderFramePacket no_level_probe_packet{
            .clear_color = omega::runtime::kDefaultRenderClearColor,
            .draw_list = *no_level_probe_draw_list,
        };
        auto no_level_probe_readback =
            omega::app::detail::SdlGpuHostTestAccess::ReadbackBlitsForTesting(
                OmegaAppTestAccess::Host(*app), no_level_probe_packet);
        Check(no_level_probe_readback &&
                  *no_level_probe_readback == expected_no_level_probe_readback,
            "the resident zero-file DiagnosticPlay placeholder preserves the exact sixteen-probe RGBA8 grid on GPU");
        Check(OmegaAppTestAccess::GpuSnapshot(*app) == initial_gpu,
            "the private no-level readback seam leaves every production GPU counter unchanged");
    }

    constexpr omega::runtime::RenderClearColorRgba8 actor_marker_color{
        .red = 255U, .green = 64U, .blue = 224U, .alpha = 255U};
    const std::array actor_marker_readback_commands{
        omega::runtime::RenderTextureBlitCommand{
            .texture = diagnostic_actor_marker_texture,
            .source = kFullMenuSource,
            .destination = kFullTarget,
            .fit_mode = omega::runtime::RenderTextureFitMode::Stretch,
            .filter_mode = omega::runtime::RenderTextureFilterMode::Nearest,
        },
    };
    auto actor_marker_readback_draw_list =
        omega::runtime::RenderDrawList::Create(
            actor_marker_readback_commands);
    Check(actor_marker_readback_draw_list.has_value(),
        "the full-target actor marker readback command forms a valid draw list");
    if (actor_marker_readback_draw_list)
    {
        const omega::runtime::RenderFramePacket actor_marker_readback_packet{
            .clear_color = omega::runtime::kDefaultRenderClearColor,
            .draw_list = *actor_marker_readback_draw_list,
        };
        auto actor_marker_readback =
            omega::app::detail::SdlGpuHostTestAccess::ReadbackBlitsForTesting(
                OmegaAppTestAccess::Host(*app), actor_marker_readback_packet);
        Check(actor_marker_readback &&
                  std::ranges::all_of(*actor_marker_readback,
                      [actor_marker_color](
                          const omega::runtime::RenderClearColorRgba8 pixel) {
                          return pixel == actor_marker_color;
                      }),
            "the immutable 1x1 actor marker uploads exact opaque RGBA8 {255,64,224,255}");
        Check(OmegaAppTestAccess::GpuSnapshot(*app) == initial_gpu,
            "the private actor marker readback leaves every production GPU counter unchanged");
    }

    std::array<omega::runtime::RenderTextureBlitCommand, 16U> menu_probe_commands{};
    for (std::size_t index = 0U; index < menu_probe_commands.size(); ++index)
    {
        const std::uint32_t x = menu_probe_coordinates[index][0];
        const std::uint32_t y = menu_probe_coordinates[index][1];
        const std::uint32_t column = static_cast<std::uint32_t>(index % 4U);
        const std::uint32_t row = static_cast<std::uint32_t>(index / 4U);
        menu_probe_commands[index] = omega::runtime::RenderTextureBlitCommand{
            .texture = front_end_texture,
            .source = omega::runtime::RenderSourceRectQ16{
                .left = source_begin(x, omega::app::kFrontEndImageWidth),
                .top = source_begin(y, omega::app::kFrontEndImageHeight),
                .right = source_end(x, omega::app::kFrontEndImageWidth),
                .bottom = source_end(y, omega::app::kFrontEndImageHeight),
            },
            .destination = omega::runtime::RenderTargetRectQ16{
                .left = destination_edge(column),
                .top = destination_edge(row),
                .right = destination_edge(column + 1U),
                .bottom = destination_edge(row + 1U),
            },
            .fit_mode = omega::runtime::RenderTextureFitMode::Stretch,
            .filter_mode = omega::runtime::RenderTextureFilterMode::Nearest,
        };
    }
    auto menu_probe_draw_list =
        omega::runtime::RenderDrawList::Create(menu_probe_commands);
    Check(menu_probe_draw_list.has_value(),
        "the sixteen one-texel menu readback commands form a valid draw list");
    if (menu_probe_draw_list)
    {
        omega::runtime::RenderFramePacket menu_probe_packet{
            .clear_color = omega::runtime::kDefaultRenderClearColor,
            .draw_list = *menu_probe_draw_list,
        };
        auto menu_probe_readback =
            omega::app::detail::SdlGpuHostTestAccess::ReadbackBlitsForTesting(
                OmegaAppTestAccess::Host(*app), menu_probe_packet);
        Check(menu_probe_readback &&
                  *menu_probe_readback == expected_menu_probe_readback,
            "the resident zero-file menu texture preserves CONTENT/NONE and the exact sixteen-probe RGBA8 grid on GPU");
        Check(OmegaAppTestAccess::GpuSnapshot(*app) == initial_gpu,
            "the private menu readback seam leaves every production GPU counter unchanged");
    }

    constexpr std::array controls_probe_coordinates{
        std::array{4U, 4U}, std::array{0U, 0U}, std::array{8U, 8U},
        std::array{9U, 8U}, std::array{40U, 12U}, std::array{42U, 11U},
        std::array{43U, 11U}, std::array{8U, 23U}, std::array{20U, 25U},
        std::array{13U, 25U}, std::array{20U, 32U}, std::array{13U, 39U},
        std::array{12U, 46U}, std::array{33U, 48U}, std::array{22U, 55U},
        std::array{12U, 62U},
    };
    constexpr std::array expected_controls_probe_readback{
        probe_background, probe_cyan, probe_slate, probe_cyan,
        probe_amber, probe_amber, probe_cyan, probe_background,
        probe_slate, probe_slate, probe_cyan, probe_cyan,
        probe_cyan, probe_slate, probe_slate, probe_cyan,
    };
    for (std::size_t index = 0U; index < menu_probe_commands.size(); ++index)
    {
        const std::uint32_t x = controls_probe_coordinates[index][0];
        const std::uint32_t y = controls_probe_coordinates[index][1];
        const std::uint32_t column = static_cast<std::uint32_t>(index % 4U);
        const std::uint32_t row = static_cast<std::uint32_t>(index / 4U);
        menu_probe_commands[index] = omega::runtime::RenderTextureBlitCommand{
            .texture = diagnostic_controls_texture,
            .source = omega::runtime::RenderSourceRectQ16{
                .left = source_begin(x, omega::app::kFrontEndImageWidth),
                .top = source_begin(y, omega::app::kFrontEndImageHeight),
                .right = source_end(x, omega::app::kFrontEndImageWidth),
                .bottom = source_end(y, omega::app::kFrontEndImageHeight),
            },
            .destination = omega::runtime::RenderTargetRectQ16{
                .left = destination_edge(column),
                .top = destination_edge(row),
                .right = destination_edge(column + 1U),
                .bottom = destination_edge(row + 1U),
            },
            .fit_mode = omega::runtime::RenderTextureFitMode::Stretch,
            .filter_mode = omega::runtime::RenderTextureFilterMode::Nearest,
        };
    }
    auto controls_probe_draw_list =
        omega::runtime::RenderDrawList::Create(menu_probe_commands);
    Check(controls_probe_draw_list.has_value(),
        "the sixteen one-texel controls readback commands form a valid draw list");
    if (controls_probe_draw_list)
    {
        omega::runtime::RenderFramePacket controls_probe_packet{
            .clear_color = omega::runtime::kDefaultRenderClearColor,
            .draw_list = *controls_probe_draw_list,
        };
        auto controls_probe_readback =
            omega::app::detail::SdlGpuHostTestAccess::ReadbackBlitsForTesting(
                OmegaAppTestAccess::Host(*app), controls_probe_packet);
        Check(controls_probe_readback &&
                  *controls_probe_readback == expected_controls_probe_readback,
            "the resident controls texture preserves the exact sixteen-probe RGBA8 grid on GPU");
        Check(OmegaAppTestAccess::GpuSnapshot(*app) == initial_gpu,
            "the private controls readback seam leaves every production GPU counter unchanged");
    }

    constexpr std::array asset_topology_probe_coordinates{
        std::array{0U, 0U}, std::array{1U, 1U}, std::array{4U, 4U},
        std::array{5U, 4U}, std::array{5U, 5U}, std::array{8U, 8U},
        std::array{9U, 8U}, std::array{11U, 9U}, std::array{12U, 9U},
        std::array{13U, 9U}, std::array{15U, 9U}, std::array{27U, 27U},
        std::array{41U, 9U}, std::array{43U, 8U}, std::array{73U, 9U},
        std::array{91U, 27U},
    };
    constexpr std::array expected_asset_topology_probe_readback{
        probe_slate, probe_background, probe_cyan, probe_background,
        probe_cyan, probe_cyan, probe_background, probe_cyan,
        probe_cyan, probe_background, probe_cyan, probe_amber,
        probe_cyan, probe_background, probe_cyan, probe_amber,
    };
    for (std::size_t index = 0U; index < menu_probe_commands.size(); ++index)
    {
        const std::uint32_t x = asset_topology_probe_coordinates[index][0];
        const std::uint32_t y = asset_topology_probe_coordinates[index][1];
        const std::uint32_t column = static_cast<std::uint32_t>(index % 4U);
        const std::uint32_t row = static_cast<std::uint32_t>(index / 4U);
        menu_probe_commands[index] = omega::runtime::RenderTextureBlitCommand{
            .texture = diagnostic_asset_topology_texture,
            .source = omega::runtime::RenderSourceRectQ16{
                .left = source_begin(x, 96U),
                .top = source_begin(y, 32U),
                .right = source_end(x, 96U),
                .bottom = source_end(y, 32U),
            },
            .destination = omega::runtime::RenderTargetRectQ16{
                .left = destination_edge(column),
                .top = destination_edge(row),
                .right = destination_edge(column + 1U),
                .bottom = destination_edge(row + 1U),
            },
            .fit_mode = omega::runtime::RenderTextureFitMode::Stretch,
            .filter_mode = omega::runtime::RenderTextureFilterMode::Nearest,
        };
    }
    auto asset_topology_probe_draw_list =
        omega::runtime::RenderDrawList::Create(menu_probe_commands);
    Check(asset_topology_probe_draw_list.has_value(),
        "the sixteen one-texel asset-topology readback commands form a valid draw list");
    if (asset_topology_probe_draw_list)
    {
        omega::runtime::RenderFramePacket asset_topology_probe_packet{
            .clear_color = omega::runtime::kDefaultRenderClearColor,
            .draw_list = *asset_topology_probe_draw_list,
        };
        auto asset_topology_probe_readback =
            omega::app::detail::SdlGpuHostTestAccess::ReadbackBlitsForTesting(
                OmegaAppTestAccess::Host(*app), asset_topology_probe_packet);
        Check(asset_topology_probe_readback &&
                  *asset_topology_probe_readback ==
                      expected_asset_topology_probe_readback,
            "the resident asset-topology texture preserves the exact sixteen-probe RGBA8 grid on GPU");
        Check(OmegaAppTestAccess::GpuSnapshot(*app) == initial_gpu,
            "the private asset-topology readback seam leaves every production GPU counter unchanged");
    }

    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    Check(PushQuit(), "a host-quit event enters the SDL queue");

    Check(OmegaAppTestAccess::ArmNextRunElapsed(*app, one_step_elapsed),
        "the negative capture-path clock override arms");
    const auto negative = app->RunWithCapture(-1);
    Check(!negative, "negative capture planning rejects before event consumption");
    if (!negative)
    {
        Check(negative.error() ==
                  omega::app::detail::FiniteRunCapturePlanErrorMessage(
                      omega::app::detail::FiniteRunCapturePlanError::NegativeFrameLimit),
            "negative planning returns its fixed pre-loop error");
    }

    Check(OmegaAppTestAccess::ArmNextRunElapsed(*app, one_step_elapsed),
        "negative capture planning discards its unobserved clock override");
    auto empty = app->RunWithCapture(0);
    Check(empty.has_value(), "zero-frame capture publishes without entering the loop");
    if (!empty)
        return EXIT_FAILURE;
    const auto* empty_pair = empty->trace_pair();
    Check(empty->requested_frame_limit() == 0U &&
              empty->completion() == RunCaptureCompletion::FrameLimitReached &&
              empty->result() == RunResult{} && !empty->failure() &&
              empty->scheduler_state_before() == empty->scheduler_state_after() &&
              empty_pair != nullptr,
        "zero-frame capture owns an empty no-work outcome");
    if (empty_pair != nullptr)
    {
        Check(empty_pair->input_trace().first_frame_index() == 0U &&
                  empty_pair->input_trace().maximum_frames() == 1U &&
                  empty_pair->input_trace().frame_count() == 0U &&
                  empty_pair->scheduler_elapsed_trace().maximum_frames() == 1U &&
                  empty_pair->scheduler_elapsed_trace().frame_count() == 0U &&
                  !empty_pair->terminal_input(),
            "zero-frame capture retains capacity one without advancing input");
    }

    Check(OmegaAppTestAccess::ArmNextRunElapsed(*app, one_step_elapsed),
        "zero-frame capture discards its unobserved clock override");
    auto host = app->RunWithCapture(1);
    Check(host.has_value(), "the queued host quit publishes a terminal capture");
    if (!host)
        return EXIT_FAILURE;
    const auto* host_pair = host->trace_pair();
    const auto host_terminal = host->terminal_input();
    const RunResult host_result = host->result();
    Check(host->completion() == RunCaptureCompletion::QuitRequested &&
              !host->failure() && host_pair != nullptr && host_terminal &&
              host_result.input_frames == 1U && host_result.rendered_frames == 0 &&
              host_result.quit_requested &&
              host->scheduler_state_before() == host->scheduler_state_after() &&
              OmegaAppTestAccess::FrontEnd(*app) ==
                  omega::app::InitialFrontEndState() &&
              OmegaAppTestAccess::GpuSnapshot(*app) == initial_gpu,
        "host quit preserves startup menu, scheduler, GPU, and render resources");
    if (host_pair != nullptr && host_terminal)
    {
        Check(host_pair->input_trace().first_frame_index() == 0U &&
                  host_pair->input_trace().frame_count() == 1U &&
                  host_pair->scheduler_elapsed_trace().frame_count() == 0U &&
                  host_terminal->frame_index == 0U &&
                  host_terminal->host_quit_requested &&
                  !host_terminal->logical_quit_pressed,
            "host quit owns the exact first terminal input and both reason flags");
    }

    Check(OmegaAppTestAccess::ArmNextRunElapsed(*app, one_step_elapsed),
        "terminal host capture discards its unobserved clock override");
    Check(PushQuitKey(true), "an F10 press enters the SDL queue");
    auto logical = app->RunWithCapture(1);
    Check(logical.has_value(), "logical quit publishes a terminal capture");
    if (!logical)
        return EXIT_FAILURE;
    const auto* logical_pair = logical->trace_pair();
    const auto logical_terminal = logical->terminal_input();
    Check(logical->completion() == RunCaptureCompletion::QuitRequested &&
              logical_pair != nullptr && logical_terminal &&
              logical->result().input_frames == 1U &&
              logical->result().rendered_frames == 0 &&
              OmegaAppTestAccess::FrontEnd(*app) ==
                  omega::app::InitialFrontEndState() &&
              OmegaAppTestAccess::GpuSnapshot(*app) == initial_gpu,
        "logical quit also preserves startup menu and ends before rendering");
    if (logical_pair != nullptr && logical_terminal)
    {
        Check(logical_pair->input_trace().first_frame_index() == 1U &&
                  logical_pair->scheduler_elapsed_trace().frame_count() == 0U &&
                  logical_terminal->frame_index == 1U &&
                  !logical_terminal->host_quit_requested &&
                  logical_terminal->logical_quit_pressed,
            "logical quit retains its distinct owned reason and continued index");
    }

    const bool movement_events_queued = PushQuitKey(false) &&
                                        PushKey(SDL_SCANCODE_RETURN, true) &&
                                        PushKey(SDL_SCANCODE_W, true);
    Check(movement_events_queued,
        "the same-frame Return and movement fixture enters the real SDL event queue");
    Check(OmegaAppTestAccess::ArmNextRunElapsed(*app, one_step_elapsed),
        "the exact one-step capture clock override arms without replacing pending state");
    auto normal = app->RunWithCapture(1);
    Check(normal.has_value(), "a released quit action permits one captured render");
    if (!normal)
        return EXIT_FAILURE;
    const auto* normal_pair = normal->trace_pair();
    const RunResult normal_result = normal->result();
    const auto normal_debug_position =
        OmegaAppTestAccess::DebugLocomotionPosition(*app);
    Check(normal->completion() == RunCaptureCompletion::FrameLimitReached &&
              !normal->failure() && normal_pair != nullptr &&
              normal_result.input_frames == 1U && normal_result.rendered_frames == 1 &&
              !normal_result.quit_requested &&
              normal_result.planned_simulation_steps == 1U &&
              normal_result.executed_simulation_steps == 1U &&
              normal_debug_position &&
              normal_debug_position->x == 0 && normal_debug_position->y == 0 &&
              normal_debug_position->z == 0 &&
              SameProximityTriggerState(
                  OmegaAppTestAccess::DiagnosticProximityTriggerState(*app), {}),
        "one Return-plus-W frame enters diagnostic play and advances simulation "
        "without leaking modal movement into the deployment step");
    const omega::app::GpuHostSnapshot normal_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    const omega::runtime::RenderDrawList normal_actor_draw_list =
        OmegaAppTestAccess::DiagnosticActorDrawList(*app);
    const auto normal_actor_commands = normal_actor_draw_list.commands();
    const std::optional<omega::runtime::RenderTargetRectQ16>
        normal_actor_destination = normal_actor_commands.size() == 3U
        ? std::optional<omega::runtime::RenderTargetRectQ16>{
              normal_actor_commands[1].destination}
        : std::nullopt;
    Check(OmegaAppTestAccess::FrontEnd(*app) ==
                  omega::app::FrontEndState{
                      .mode = omega::app::FrontEndMode::DiagnosticPlay,
                      .selected_main_row =
                          omega::app::FrontEndMainRow::StartDiagnostic,
                  } &&
              OmegaAppTestAccess::DiagnosticTexture(*app) == diagnostic_texture &&
              OmegaAppTestAccess::DiagnosticActorMarkerTexture(*app) ==
                  diagnostic_actor_marker_texture &&
              OmegaAppTestAccess::FrontEndTexture(*app) ==
                  front_end_texture &&
              OmegaAppTestAccess::FrontEndProfilesTexture(*app) ==
                  front_end_profiles_texture &&
              OmegaAppTestAccess::DiagnosticControlsTexture(*app) ==
                  diagnostic_controls_texture &&
              OmegaAppTestAccess::DiagnosticAssetTopologyTexture(*app) ==
                  diagnostic_asset_topology_texture &&
              OmegaAppTestAccess::DiagnosticAssetTransferTexture(*app) ==
                  diagnostic_asset_transfer_texture &&
              DrawListsEqual(OmegaAppTestAccess::DiagnosticHiddenDrawList(*app),
                  initial_hidden_draw_list) &&
              DrawListArraysEqual(OmegaAppTestAccess::FrontEndMainDrawLists(*app),
                  initial_visible_draw_lists) &&
              DrawListsEqual(OmegaAppTestAccess::FrontEndProfilesDrawList(*app),
                  initial_profiles_draw_list) &&
              DrawListsEqual(OmegaAppTestAccess::DiagnosticControlsDrawList(*app),
                  initial_controls_draw_list) &&
              normal_actor_commands.size() == 3U &&
              hidden_commands.size() == 1U &&
              normal_actor_commands[0] == hidden_commands[0] &&
              normal_actor_commands[1].texture ==
                  diagnostic_actor_marker_texture &&
              normal_actor_commands[1].source == kFullMenuSource &&
              normal_actor_commands[1].destination ==
                  kActorOriginDestination &&
              normal_actor_commands[1].fit_mode ==
                  omega::runtime::RenderTextureFitMode::Stretch &&
              normal_actor_commands[1].filter_mode ==
                  omega::runtime::RenderTextureFilterMode::Nearest &&
              normal_actor_commands[2].texture ==
                  diagnostic_actor_marker_texture &&
              normal_actor_commands[2].source == kFullMenuSource &&
              normal_actor_commands[2].destination == *kObjectiveDestination &&
              normal_actor_commands[2].fit_mode ==
                  omega::runtime::RenderTextureFitMode::Stretch &&
              normal_actor_commands[2].filter_mode ==
                  omega::runtime::RenderTextureFilterMode::Nearest &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  normal_actor_draw_list) &&
              IsOneDiagnosticPlaySubmission(initial_gpu, normal_gpu),
        "primary priority enters DiagnosticPlay with immutable base and an origin "
        "marker because same-frame menu movement is neutral");

    const omega::runtime::FrameSchedulerState normal_before =
        normal->scheduler_state_before();
    const omega::runtime::FrameSchedulerState normal_after =
        normal->scheduler_state_after();
    std::optional<omega::runtime::InputTraceFrameState> captured_input;
    std::optional<omega::runtime::SchedulerElapsedFrameState> captured_elapsed;
    std::optional<omega::runtime::FramePlan> captured_plan;
    std::optional<omega::runtime::InputTraceActionState> captured_forward;
    std::optional<omega::runtime::InputTraceActionState> captured_menu_toggle;
    std::array<std::uint32_t, omega::runtime::InputBindingTable::kMaxActions>
        captured_actions{};
    std::array<omega::runtime::InputTraceActionState,
        omega::runtime::InputBindingTable::kMaxActions>
        captured_action_states{};
    std::size_t captured_action_count = 0U;
    bool captured_action_schema_exact = false;
    bool captured_action_states_valid = true;
    if (normal_pair != nullptr)
    {
        captured_input = normal_pair->input_trace().FrameAt(0U);
        captured_elapsed = normal_pair->scheduler_elapsed_trace().FrameAt(0U);
        captured_forward = normal_pair->input_trace().ActionAt(
            0U, omega::app::kDebugMoveForwardAction);
        captured_menu_toggle = normal_pair->input_trace().ActionAt(
            0U, omega::app::kFrontEndPrimaryAction);
        const auto action_schema = normal_pair->input_trace().actions();
        captured_action_count = action_schema.size();
        constexpr std::array<std::uint32_t, 9U> kExpectedActions{
            1U,
            omega::app::kDebugMoveForwardAction,
            omega::app::kDebugMoveBackwardAction,
            omega::app::kDebugMoveLeftAction,
            omega::app::kDebugMoveRightAction,
            omega::app::kFrontEndPrimaryAction,
            omega::app::kFrontEndCancelAction,
            omega::app::kDebugFireAction,
            omega::app::kDebugTargetAction,
        };
        captured_action_schema_exact =
            action_schema.size() == kExpectedActions.size();
        for (std::size_t index = 0U;
             captured_action_schema_exact && index < kExpectedActions.size(); ++index)
        {
            captured_action_schema_exact = action_schema[index] == kExpectedActions[index];
        }
        for (std::size_t index = 0U; index < captured_action_count; ++index)
        {
            captured_actions[index] = action_schema[index];
            const auto action_state =
                normal_pair->input_trace().ActionAt(0U, action_schema[index]);
            if (!action_state)
            {
                captured_action_states_valid = false;
                break;
            }
            captured_action_states[index] = *action_state;
        }
        Check(normal_pair->input_trace().first_frame_index() == 2U &&
                  normal_pair->input_trace().frame_count() == 1U &&
                  normal_pair->scheduler_elapsed_trace().frame_count() == 1U &&
                  captured_input && captured_elapsed && captured_action_count == 9U &&
                  captured_action_schema_exact && captured_action_states_valid &&
                  captured_elapsed->elapsed == one_step_elapsed &&
                  captured_forward && captured_menu_toggle &&
                  captured_forward->held && captured_forward->pressed &&
                  !captured_forward->released && captured_menu_toggle->held &&
                  captured_menu_toggle->pressed && !captured_menu_toggle->released,
            "normal capture records the exact nine-action schema and simultaneous "
            "Return/W edges");
        if (captured_elapsed)
        {
            auto replay = omega::runtime::FrameScheduler::Create(normal_before.config);
            Check(replay.has_value(), "the captured scheduler configuration revalidates");
            if (replay)
            {
                captured_plan = replay->BeginFrame(captured_elapsed->elapsed);
                Check(replay->Snapshot() == normal_after &&
                          captured_plan->simulation_steps ==
                              normal_result.planned_simulation_steps,
                    "the exact captured elapsed value reproduces scheduler state");
            }
        }
    }

    auto replay_traces = std::move(*normal).TakeTracePair();
    Check(replay_traces && captured_input && captured_elapsed && captured_plan &&
              captured_menu_toggle && captured_action_schema_exact &&
              captured_action_states_valid,
        "the real capture publishes complete owned replay inputs");
    if (!replay_traces || !captured_input || !captured_elapsed || !captured_plan ||
        !captured_menu_toggle || !captured_action_schema_exact ||
        !captured_action_states_valid)
    {
        return EXIT_FAILURE;
    }

    auto replay_created = omega::app::RunReplaySession::Create(
        std::move(*replay_traces),
        omega::app::RunReplaySessionConfig{
            .scheduler = normal_before.config,
            .enable_debug_locomotion = true,
            .initial_front_end_state =
                omega::app::InitialFrontEndState(),
            .front_end_capabilities = omega::app::FrontEndCapabilities{
                .can_start_diagnostic_campaign = true,
            },
        });
    Check(replay_created.has_value(),
        "the actual real-host capture creates a fresh app replay session");
    if (!replay_created)
        return EXIT_FAILURE;
    omega::app::RunReplaySession replay_session = std::move(*replay_created);

    const auto replay_scheduler_before = replay_session.scheduler_state();
    const auto replay_simulation_before = replay_session.simulation_state();
    Check(replay_session.state() == omega::app::RunReplaySessionState::Ready &&
              replay_session.remaining_frames() == 1U && replay_scheduler_before &&
              *replay_scheduler_before == normal_before && replay_simulation_before &&
              replay_simulation_before->completed_steps == 0U &&
              replay_simulation_before->simulated_time ==
                  std::chrono::nanoseconds::zero() &&
              replay_simulation_before->alive_entities == 1U &&
              replay_session.debug_locomotion_position() ==
                  omega::simulation::Position3{} &&
              replay_session.front_end_state() ==
                  omega::app::InitialFrontEndState(),
        "real-host replay begins with a fresh positioned synthetic diagnostic entity");

    auto replay_frame = replay_session.Next();
    Check(replay_frame.has_value(), "the actual captured frame advances through app replay");
    if (!replay_frame)
        return EXIT_FAILURE;

    bool replay_actions_match =
        replay_frame->input().actions().size() == captured_action_count;
    for (std::size_t index = 0U;
         replay_actions_match && index < captured_action_count; ++index)
    {
        const std::uint32_t action = captured_actions[index];
        const auto& expected = captured_action_states[index];
        replay_actions_match = replay_frame->input().actions()[index] == action &&
                               replay_frame->input().IsHeld(action) == expected.held &&
                               replay_frame->input().WasPressed(action) == expected.pressed &&
                               replay_frame->input().WasReleased(action) == expected.released;
    }
    const auto replay_plan = replay_frame->frame_plan();
    Check(replay_frame->input().frame_index() == captured_input->frame_index &&
              replay_frame->input().accepted_event_count() ==
                  captured_input->accepted_event_count &&
              replay_frame->input().rejected_event_count() ==
                  captured_input->rejected_event_count &&
              replay_actions_match &&
              replay_frame->input().IsHeld(
                  omega::app::kFrontEndPrimaryAction) &&
              replay_frame->input().WasPressed(
                  omega::app::kFrontEndPrimaryAction) &&
              !replay_frame->input().WasReleased(
                  omega::app::kFrontEndPrimaryAction) &&
              replay_frame->elapsed() == captured_elapsed->elapsed &&
              !replay_frame->terminal_input() && replay_plan &&
              replay_plan->simulation_steps == captured_plan->simulation_steps &&
              replay_plan->interpolation_alpha == captured_plan->interpolation_alpha &&
              replay_plan->clamped_delta == captured_plan->clamped_delta &&
              replay_plan->dropped_time == captured_plan->dropped_time,
        "app replay carries action 6 with the actual input and exact scheduler plan");

    const auto replay_scheduler_after = replay_session.scheduler_state();
    const auto replay_simulation_after = replay_session.simulation_state();
    const auto expected_fresh_time = normal_before.config.simulation_step *
                                     captured_plan->simulation_steps;
    Check(replay_scheduler_after && *replay_scheduler_after == normal_after &&
              replay_simulation_after &&
              replay_simulation_after->completed_steps ==
                  captured_plan->simulation_steps &&
              replay_simulation_after->simulated_time == expected_fresh_time &&
              replay_simulation_after->alive_entities == 1U &&
              replay_session.debug_locomotion_position() == normal_debug_position &&
              replay_session.diagnostic_actor_marker_destination() ==
                  normal_actor_destination &&
              replay_session.diagnostic_actor_marker_destination() ==
                  std::optional<omega::runtime::RenderTargetRectQ16>{
                      kActorOriginDestination} &&
              replay_session.front_end_state() ==
                  omega::app::FrontEndState{} &&
              normal_result.planned_simulation_steps ==
                  captured_plan->simulation_steps &&
              normal_result.executed_simulation_steps ==
                  captured_plan->simulation_steps &&
              replay_session.state() ==
                  omega::app::RunReplaySessionState::Complete &&
              replay_session.remaining_frames() == 0U,
        "replay applies action 6 while preserving the captured neutral deployment "
        "position and actor destination");

    const auto replay_complete = replay_session.Next();
    Check(!replay_complete &&
              replay_complete.error().operation == omega::app::RunReplayOperation::Next &&
              replay_complete.error().code ==
                  omega::app::RunReplayErrorCode::ReplayComplete &&
              !replay_complete.error().replay_error,
        "the consumed real-host capture reports stable app replay completion");

    constexpr omega::app::FrontEndState kDiagnosticPlayRowZero{};
    constexpr omega::app::FrontEndState kMainRowOne{
        .mode = omega::app::FrontEndMode::Main,
        .selected_main_row = omega::app::FrontEndMainRow::Profiles,
    };
    constexpr omega::app::FrontEndState kProfilesRowOne{
        .mode = omega::app::FrontEndMode::Profiles,
        .selected_main_row = omega::app::FrontEndMainRow::Profiles,
    };
    constexpr omega::app::FrontEndState kMainRowTwo{
        .mode = omega::app::FrontEndMode::Main,
        .selected_main_row = omega::app::FrontEndMainRow::Controls,
    };
    constexpr omega::app::FrontEndState kControlsRowTwo{
        .mode = omega::app::FrontEndMode::Controls,
        .selected_main_row = omega::app::FrontEndMainRow::Controls,
    };
    constexpr omega::app::FrontEndState kMainRowThree{
        .mode = omega::app::FrontEndMode::Main,
        .selected_main_row = omega::app::FrontEndMainRow::AssetTopology,
    };
    const auto RunPlainFrame = [&app]() {
        const auto result = app->Run(1);
        Check(result && result->input_frames == 1U && result->rendered_frames == 1 &&
                  !result->quit_requested,
            "one menu navigation frame completes");
        return result.has_value();
    };

    const auto position_after_primary =
        OmegaAppTestAccess::DebugLocomotionPosition(*app);
    const std::uint64_t held_primary_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    Check(PushKey(SDL_SCANCODE_F1, true) && PushKey(SDL_SCANCODE_W, false),
        "the F1 alias and forward release enter while Return keeps action 6 held");
    auto held_primary = app->RunWithCapture(1);
    Check(held_primary.has_value(), "held primary renders DiagnosticPlay once");
    if (!held_primary)
        return EXIT_FAILURE;
    const auto* held_pair = held_primary->trace_pair();
    const auto held_action = held_pair != nullptr
                                 ? held_pair->input_trace().ActionAt(
                                       0U, omega::app::kFrontEndPrimaryAction)
                                 : std::nullopt;
    const omega::app::GpuHostSnapshot held_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(held_pair != nullptr &&
              held_pair->input_trace().first_frame_index() == held_primary_index &&
              held_action && held_action->held && !held_action->pressed &&
              !held_action->released &&
              OmegaAppTestAccess::FrontEnd(*app) == kDiagnosticPlayRowZero &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) ==
                  position_after_primary &&
              IsOneDiagnosticPlaySubmission(normal_gpu, held_gpu),
        "a second physical alias does not repeat the held action-6 press edge or reopen the menu");

    const std::uint64_t nonfinal_release_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    Check(PushKey(SDL_SCANCODE_RETURN, false),
        "Return releases while the F1 alias remains held");
    auto nonfinal_release = app->RunWithCapture(1);
    Check(nonfinal_release.has_value(),
        "the non-final action-6 alias release captures");
    if (!nonfinal_release)
        return EXIT_FAILURE;
    const auto* nonfinal_release_pair = nonfinal_release->trace_pair();
    const auto nonfinal_release_action = nonfinal_release_pair != nullptr
                                             ? nonfinal_release_pair->input_trace().ActionAt(
                                                   0U, omega::app::kFrontEndPrimaryAction)
                                             : std::nullopt;
    const omega::app::GpuHostSnapshot nonfinal_release_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(nonfinal_release_pair != nullptr &&
              nonfinal_release_pair->input_trace().first_frame_index() ==
                  nonfinal_release_index &&
              nonfinal_release_action && nonfinal_release_action->held &&
              !nonfinal_release_action->pressed &&
              !nonfinal_release_action->released &&
              nonfinal_release->result().input_frames == 1U &&
              nonfinal_release->result().rendered_frames == 1 &&
              OmegaAppTestAccess::FrontEnd(*app) == kDiagnosticPlayRowZero &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) ==
                  position_after_primary &&
              IsOneDiagnosticPlaySubmission(held_gpu, nonfinal_release_gpu),
        "releasing Return cannot release action 6 or mutate the menu while F1 remains held");

    const std::uint64_t final_release_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    Check(PushKey(SDL_SCANCODE_F1, false),
        "the last held action-6 alias releases");
    auto final_release = app->RunWithCapture(1);
    Check(final_release.has_value(), "the final action-6 alias release captures");
    if (!final_release)
        return EXIT_FAILURE;
    const auto* final_release_pair = final_release->trace_pair();
    const auto final_release_action = final_release_pair != nullptr
                                          ? final_release_pair->input_trace().ActionAt(
                                                0U, omega::app::kFrontEndPrimaryAction)
                                          : std::nullopt;
    const omega::app::GpuHostSnapshot final_release_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(final_release_pair != nullptr &&
              final_release_pair->input_trace().first_frame_index() ==
                  final_release_index &&
              final_release_action && !final_release_action->held &&
              !final_release_action->pressed && final_release_action->released &&
              final_release->result().input_frames == 1U &&
              final_release->result().rendered_frames == 1 &&
              OmegaAppTestAccess::FrontEnd(*app) == kDiagnosticPlayRowZero &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) ==
                  position_after_primary &&
              IsOneDiagnosticPlaySubmission(
                  nonfinal_release_gpu, final_release_gpu),
        "only the last physical alias release emits the logical action-6 release edge");

    Check(PushKey(SDL_SCANCODE_KP_ENTER, true),
        "a fresh keypad Enter primary edge enters the SDL queue");
    Check(RunPlainFrame(), "the keypad Enter primary frame completes");
    const omega::app::GpuHostSnapshot reopened_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(OmegaAppTestAccess::FrontEnd(*app) ==
                  omega::app::InitialFrontEndState() &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  initial_visible_draw_lists[0]) &&
              IsOneVisibleMenuSubmission(final_release_gpu, reopened_gpu),
        "keypad Enter reopens MainMenu at row zero with exactly three resident blits");
    Check(PushKey(SDL_SCANCODE_KP_ENTER, false),
        "the reopened keypad Enter primary releases");
    Check(RunPlainFrame(), "reopened release frame completes");

    const std::uint64_t next_edge_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    const omega::runtime::FrameSchedulerState modal_scheduler_before =
        OmegaAppTestAccess::SchedulerSnapshot(*app);
    const omega::simulation::SimulationState modal_simulation_before =
        OmegaAppTestAccess::SimulationSnapshot(*app);
    const auto modal_position_before =
        OmegaAppTestAccess::DebugLocomotionPosition(*app);
    const omega::app::GpuHostSnapshot modal_gpu_before =
        OmegaAppTestAccess::GpuSnapshot(*app);
    const bool modal_events_queued = PushKey(SDL_SCANCODE_DOWN, true);
    Check(modal_events_queued,
        "the Down-arrow next-row edge enters the SDL queue");
    Check(OmegaAppTestAccess::ArmNextRunElapsed(*app, modal_proof_elapsed),
        "the next-row modal clock proof arms");
    auto next_edge = app->RunWithCapture(1);
    Check(next_edge.has_value(), "next-row edge captures");
    if (!next_edge)
        return EXIT_FAILURE;
    const auto* next_pair = next_edge->trace_pair();
    const auto next_action = next_pair != nullptr
                                 ? next_pair->input_trace().ActionAt(
                                       0U, omega::app::kFrontEndNextAction)
                                 : std::nullopt;
    const auto next_elapsed = next_pair != nullptr
                                  ? next_pair->scheduler_elapsed_trace().FrameAt(0U)
                                  : std::nullopt;
    const RunResult next_result = next_edge->result();
    const omega::app::GpuHostSnapshot modal_gpu_after =
        OmegaAppTestAccess::GpuSnapshot(*app);
    const omega::simulation::SimulationState modal_simulation_after =
        OmegaAppTestAccess::SimulationSnapshot(*app);
    Check(next_pair != nullptr &&
              next_pair->input_trace().first_frame_index() == next_edge_index &&
              next_action && next_action->held && next_action->pressed &&
              next_elapsed && next_elapsed->elapsed == modal_proof_elapsed &&
              next_result.input_frames == 1U && next_result.rendered_frames == 1 &&
              next_result.planned_simulation_steps == 0U &&
              next_result.executed_simulation_steps == 0U &&
              next_edge->scheduler_state_before() == modal_scheduler_before &&
              next_edge->scheduler_state_after() == modal_scheduler_before &&
              OmegaAppTestAccess::SchedulerSnapshot(*app) == modal_scheduler_before &&
              modal_simulation_after.completed_steps ==
                  modal_simulation_before.completed_steps &&
              modal_simulation_after.simulated_time ==
                  modal_simulation_before.simulated_time &&
              modal_simulation_after.alive_entities ==
                  modal_simulation_before.alive_entities &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) ==
                  modal_position_before &&
              OmegaAppTestAccess::FrontEnd(*app) == kMainRowOne &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  initial_visible_draw_lists[1]) &&
              IsOneVisibleMenuSubmission(modal_gpu_before, modal_gpu_after),
        "a real Down-arrow sample above one fixed step navigates and renders "
        "while the modal menu freezes scheduler, world, and locomotion");

    const std::uint64_t held_next_alias_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    Check(PushKey(SDL_SCANCODE_S, true),
        "the S alias enters while Down keeps action 3 held");
    auto held_next_alias = app->RunWithCapture(1);
    Check(held_next_alias.has_value(), "the held next-action alias captures");
    if (!held_next_alias)
        return EXIT_FAILURE;
    const auto* held_next_alias_pair = held_next_alias->trace_pair();
    const auto held_next_alias_action = held_next_alias_pair != nullptr
                                            ? held_next_alias_pair->input_trace().ActionAt(
                                                  0U, omega::app::kFrontEndNextAction)
                                            : std::nullopt;
    const omega::app::GpuHostSnapshot held_next_alias_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(held_next_alias_pair != nullptr &&
              held_next_alias_pair->input_trace().first_frame_index() ==
                  held_next_alias_index &&
              held_next_alias_action && held_next_alias_action->held &&
              !held_next_alias_action->pressed &&
              !held_next_alias_action->released &&
              held_next_alias->result().planned_simulation_steps == 0U &&
              held_next_alias->result().executed_simulation_steps == 0U &&
              held_next_alias->scheduler_state_before() == modal_scheduler_before &&
              held_next_alias->scheduler_state_after() == modal_scheduler_before &&
              OmegaAppTestAccess::FrontEnd(*app) == kMainRowOne &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) ==
                  modal_position_before &&
              IsOneVisibleMenuSubmission(modal_gpu_after, held_next_alias_gpu),
        "a second physical action-3 alias cannot repeat navigation or advance modal owners");

    const std::uint64_t nonfinal_next_release_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    Check(PushKey(SDL_SCANCODE_DOWN, false),
        "Down releases while the S alias keeps action 3 held");
    auto nonfinal_next_release = app->RunWithCapture(1);
    Check(nonfinal_next_release.has_value(),
        "the non-final next-action release captures");
    if (!nonfinal_next_release)
        return EXIT_FAILURE;
    const auto* nonfinal_next_release_pair = nonfinal_next_release->trace_pair();
    const auto nonfinal_next_release_action =
        nonfinal_next_release_pair != nullptr
            ? nonfinal_next_release_pair->input_trace().ActionAt(
                  0U, omega::app::kFrontEndNextAction)
            : std::nullopt;
    const omega::app::GpuHostSnapshot nonfinal_next_release_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(nonfinal_next_release_pair != nullptr &&
              nonfinal_next_release_pair->input_trace().first_frame_index() ==
                  nonfinal_next_release_index &&
              nonfinal_next_release_action &&
              nonfinal_next_release_action->held &&
              !nonfinal_next_release_action->pressed &&
              !nonfinal_next_release_action->released &&
              nonfinal_next_release->result().planned_simulation_steps == 0U &&
              nonfinal_next_release->result().executed_simulation_steps == 0U &&
              nonfinal_next_release->scheduler_state_before() ==
                  modal_scheduler_before &&
              nonfinal_next_release->scheduler_state_after() ==
                  modal_scheduler_before &&
              OmegaAppTestAccess::FrontEnd(*app) == kMainRowOne &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) ==
                  modal_position_before &&
              IsOneVisibleMenuSubmission(
                  held_next_alias_gpu, nonfinal_next_release_gpu),
        "releasing Down cannot release action 3 or mutate the menu while S remains held");

    const omega::app::GpuHostSnapshot controls_entry_gpu_before =
        OmegaAppTestAccess::GpuSnapshot(*app);
    const bool controls_entry_events = PushKey(SDL_SCANCODE_S, false) &&
                                       PushKey(SDL_SCANCODE_F1, true) &&
                                       PushKey(SDL_SCANCODE_W, true);
    Check(controls_entry_events,
        "primary, previous, and Profiles events enter together");
    Check(OmegaAppTestAccess::ArmNextRunElapsed(*app, modal_proof_elapsed),
        "the Profiles-entry modal clock proof arms");
    auto controls_entry = app->RunWithCapture(1);
    Check(controls_entry.has_value(), "Main-to-Profiles activation captures");
    if (!controls_entry)
        return EXIT_FAILURE;
    const auto* controls_entry_pair = controls_entry->trace_pair();
    const auto controls_entry_elapsed = controls_entry_pair != nullptr
                                            ? controls_entry_pair->scheduler_elapsed_trace().FrameAt(0U)
                                            : std::nullopt;
    const auto controls_entry_primary = controls_entry_pair != nullptr
                                            ? controls_entry_pair->input_trace().ActionAt(
                                                  0U, omega::app::kFrontEndPrimaryAction)
                                            : std::nullopt;
    const auto controls_entry_next = controls_entry_pair != nullptr
                                         ? controls_entry_pair->input_trace().ActionAt(
                                               0U, omega::app::kFrontEndNextAction)
                                         : std::nullopt;
    const RunResult controls_entry_result = controls_entry->result();
    const omega::app::GpuHostSnapshot controls_entry_gpu_after =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(controls_entry_pair != nullptr && controls_entry_elapsed &&
              controls_entry_elapsed->elapsed == modal_proof_elapsed &&
              controls_entry_primary && controls_entry_primary->held &&
              controls_entry_primary->pressed &&
              controls_entry_next && !controls_entry_next->held &&
              !controls_entry_next->pressed && controls_entry_next->released &&
              controls_entry_result.input_frames == 1U &&
              controls_entry_result.rendered_frames == 1 &&
              controls_entry_result.planned_simulation_steps == 0U &&
              controls_entry_result.executed_simulation_steps == 0U &&
              controls_entry->scheduler_state_before() == modal_scheduler_before &&
              controls_entry->scheduler_state_after() == modal_scheduler_before &&
              OmegaAppTestAccess::SchedulerSnapshot(*app) == modal_scheduler_before &&
              SameSimulationState(OmegaAppTestAccess::SimulationSnapshot(*app),
                  modal_simulation_before) &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) == modal_position_before &&
              OmegaAppTestAccess::FrontEnd(*app) == kProfilesRowOne &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  initial_profiles_draw_list) &&
              IsOneModalCardSubmission(
                  controls_entry_gpu_before, controls_entry_gpu_after),
        "the last action-3 alias release emits once while primary priority enters "
        "Profiles and every simulation owner stays frozen");

    Check(PushKey(SDL_SCANCODE_F1, true), "held Profiles primary enters the queue");
    auto controls_held = app->RunWithCapture(1);
    Check(controls_held &&
              controls_held->result().planned_simulation_steps == 0U &&
              controls_held->result().executed_simulation_steps == 0U &&
              controls_held->scheduler_state_before() == modal_scheduler_before &&
              controls_held->scheduler_state_after() == modal_scheduler_before &&
              OmegaAppTestAccess::FrontEnd(*app) == kProfilesRowOne &&
              IsOneModalCardSubmission(controls_entry_gpu_after,
                  OmegaAppTestAccess::GpuSnapshot(*app)),
        "held primary does not repeat and Profiles remains an exact base-plus-card modal frame");

    Check(PushKey(SDL_SCANCODE_F1, false) && PushKey(SDL_SCANCODE_W, false),
        "Profiles primary and held movement release");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::FrontEnd(*app) == kProfilesRowOne &&
              OmegaAppTestAccess::SchedulerSnapshot(*app) == modal_scheduler_before,
        "release edges preserve Profiles and its frozen scheduler");

    const std::uint64_t controls_terminal_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    const omega::app::GpuHostSnapshot controls_terminal_gpu_before =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(PushKey(SDL_SCANCODE_F1, true) && PushQuitKey(true) && PushQuit(),
        "Profiles primary and simultaneous terminal reasons enter the queue");
    auto controls_terminal = app->RunWithCapture(1);
    Check(controls_terminal.has_value(), "Profiles terminal precedence captures");
    if (!controls_terminal)
        return EXIT_FAILURE;
    const auto* controls_terminal_pair = controls_terminal->trace_pair();
    const auto controls_terminal_reason = controls_terminal->terminal_input();
    const auto controls_terminal_primary = controls_terminal_pair != nullptr
                                               ? controls_terminal_pair->input_trace().ActionAt(
                                                     0U,
                                                     omega::app::kFrontEndPrimaryAction)
                                               : std::nullopt;
    Check(controls_terminal->completion() == RunCaptureCompletion::QuitRequested &&
              controls_terminal_reason && controls_terminal_pair != nullptr &&
              controls_terminal_reason->frame_index == controls_terminal_index &&
              controls_terminal_reason->host_quit_requested &&
              controls_terminal_reason->logical_quit_pressed &&
              controls_terminal_primary && controls_terminal_primary->held &&
              controls_terminal_primary->pressed &&
              controls_terminal->scheduler_state_before() == modal_scheduler_before &&
              controls_terminal->scheduler_state_after() == modal_scheduler_before &&
              OmegaAppTestAccess::FrontEnd(*app) == kProfilesRowOne &&
              SameSimulationState(OmegaAppTestAccess::SimulationSnapshot(*app),
                  modal_simulation_before) &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) == modal_position_before &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  initial_profiles_draw_list) &&
              OmegaAppTestAccess::GpuSnapshot(*app) == controls_terminal_gpu_before,
        "terminal resolution captures the Profiles primary edge without reducing, rendering, or mutating any owner");

    Check(PushQuitKey(false) && PushKey(SDL_SCANCODE_F1, false),
        "Profiles terminal inputs release");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::FrontEnd(*app) == kProfilesRowOne,
        "terminal release resumes the unchanged Profiles screen");

    const omega::app::GpuHostSnapshot controls_exit_gpu_before =
        OmegaAppTestAccess::GpuSnapshot(*app);
    const bool controls_exit_events = PushKey(SDL_SCANCODE_BACKSPACE, true);
    Check(controls_exit_events, "fresh Profiles cancel edge enters");
    Check(OmegaAppTestAccess::ArmNextRunElapsed(*app, modal_proof_elapsed),
        "the Profiles-exit modal clock proof arms");
    auto controls_exit = app->RunWithCapture(1);
    Check(controls_exit.has_value(), "Profiles-to-Main return captures");
    if (!controls_exit)
        return EXIT_FAILURE;
    const auto* controls_exit_pair = controls_exit->trace_pair();
    const auto controls_exit_elapsed = controls_exit_pair != nullptr
                                           ? controls_exit_pair->scheduler_elapsed_trace().FrameAt(0U)
                                           : std::nullopt;
    const auto controls_exit_cancel = controls_exit_pair != nullptr
                                          ? controls_exit_pair->input_trace().ActionAt(
                                                0U, omega::app::kFrontEndCancelAction)
                                          : std::nullopt;
    const RunResult controls_exit_result = controls_exit->result();
    const omega::app::GpuHostSnapshot controls_exit_gpu_after =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(controls_exit_pair != nullptr && controls_exit_elapsed &&
              controls_exit_elapsed->elapsed == modal_proof_elapsed &&
              controls_exit_cancel && controls_exit_cancel->held &&
              controls_exit_cancel->pressed && !controls_exit_cancel->released &&
              controls_exit_result.input_frames == 1U &&
              controls_exit_result.rendered_frames == 1 &&
              controls_exit_result.planned_simulation_steps == 0U &&
              controls_exit_result.executed_simulation_steps == 0U &&
              controls_exit->scheduler_state_before() == modal_scheduler_before &&
              controls_exit->scheduler_state_after() == modal_scheduler_before &&
              OmegaAppTestAccess::SchedulerSnapshot(*app) == modal_scheduler_before &&
              SameSimulationState(OmegaAppTestAccess::SimulationSnapshot(*app),
                  modal_simulation_before) &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) == modal_position_before &&
              OmegaAppTestAccess::FrontEnd(*app) == kMainRowOne &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  initial_visible_draw_lists[1]) &&
              IsOneVisibleMenuSubmission(
                  controls_exit_gpu_before, controls_exit_gpu_after),
        "fresh cancel returns Profiles to Main row one without a command or accumulated-menu-time advance");
    Check(PushKey(SDL_SCANCODE_BACKSPACE, false), "returned Main cancel releases");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::FrontEnd(*app) == kMainRowOne,
        "return release preserves Main row one");

    Check(PushKey(SDL_SCANCODE_S, true), "row-two edge enters the SDL queue");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::FrontEnd(*app) == kMainRowTwo &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  initial_visible_draw_lists[2]),
        "next moves row one to row two");
    Check(PushKey(SDL_SCANCODE_S, false), "row-two edge releases");
    Check(RunPlainFrame(), "row-two release completes");

    const omega::app::GpuHostSnapshot controls_screen_entry_gpu_before =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(PushKey(SDL_SCANCODE_F1, true),
        "row-two primary enters the Controls screen");
    auto controls_screen_entry = app->RunWithCapture(1);
    Check(controls_screen_entry &&
              controls_screen_entry->result().planned_simulation_steps == 0U &&
              controls_screen_entry->result().executed_simulation_steps == 0U &&
              controls_screen_entry->scheduler_state_before() == modal_scheduler_before &&
              controls_screen_entry->scheduler_state_after() == modal_scheduler_before &&
              OmegaAppTestAccess::FrontEnd(*app) == kControlsRowTwo &&
              SameSimulationState(OmegaAppTestAccess::SimulationSnapshot(*app),
                  modal_simulation_before) &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  initial_controls_draw_list) &&
              IsOneModalCardSubmission(controls_screen_entry_gpu_before,
                  OmegaAppTestAccess::GpuSnapshot(*app)),
        "Controls activation selects the immutable controls card and freezes every simulation owner");
    Check(PushKey(SDL_SCANCODE_F1, false),
        "Controls primary release enters the queue");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::FrontEnd(*app) == kControlsRowTwo &&
              OmegaAppTestAccess::SchedulerSnapshot(*app) == modal_scheduler_before,
        "Controls release preserves the modal screen and scheduler baseline");
    Check(PushKey(SDL_SCANCODE_BACKSPACE, true),
        "fresh Controls cancel edge enters the queue");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::FrontEnd(*app) == kMainRowTwo &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  initial_visible_draw_lists[2]) &&
              SameSimulationState(OmegaAppTestAccess::SimulationSnapshot(*app),
                  modal_simulation_before),
        "fresh cancel returns Controls to Main row two without advancing simulation");
    Check(PushKey(SDL_SCANCODE_BACKSPACE, false),
        "returned Controls cancel releases");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::FrontEnd(*app) == kMainRowTwo,
        "Controls return release preserves Main row two");

    Check(PushKey(SDL_SCANCODE_S, true), "row-three edge enters the SDL queue");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::FrontEnd(*app) == kMainRowThree &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  initial_visible_draw_lists[3]),
        "next moves row two to row three");
    Check(PushKey(SDL_SCANCODE_S, false), "row-three edge releases");
    Check(RunPlainFrame(), "row-three release completes");
    Check(PushKey(SDL_SCANCODE_S, true), "lower-bound edge enters the SDL queue");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::FrontEnd(*app) == kMainRowThree,
        "next clamps at row three instead of wrapping");
    Check(PushKey(SDL_SCANCODE_S, false), "lower-bound edge releases");
    Check(RunPlainFrame(), "lower-bound release completes");
    Check(PushKey(SDL_SCANCODE_W, true) && PushKey(SDL_SCANCODE_S, true),
        "simultaneous navigation edges enter the SDL queue");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::FrontEnd(*app) == kMainRowThree,
        "simultaneous previous and next edges are neutral");
    Check(PushKey(SDL_SCANCODE_W, false) && PushKey(SDL_SCANCODE_S, false),
        "simultaneous navigation controls release");
    Check(RunPlainFrame(), "simultaneous navigation release completes");

    constexpr omega::app::FrontEndState kAssetTopologyRowThree{
        .mode = omega::app::FrontEndMode::AssetTopology,
        .selected_main_row = omega::app::FrontEndMainRow::AssetTopology,
    };
    const omega::app::GpuHostSnapshot topology_entry_gpu_before =
        OmegaAppTestAccess::GpuSnapshot(*app);
    const bool topology_entry_events = PushKey(SDL_SCANCODE_F1, true) &&
                                       PushKey(SDL_SCANCODE_W, true);
    Check(topology_entry_events,
        "primary, previous, and asset-topology events enter together");
    Check(OmegaAppTestAccess::ArmNextRunElapsed(*app, modal_proof_elapsed),
        "the AssetTopology-entry modal clock proof arms");
    auto topology_entry = app->RunWithCapture(1);
    Check(topology_entry.has_value(), "MainMenu-to-AssetTopology activation captures");
    if (!topology_entry)
        return EXIT_FAILURE;
    const auto* topology_entry_pair = topology_entry->trace_pair();
    const auto topology_entry_elapsed = topology_entry_pair != nullptr
                                            ? topology_entry_pair->scheduler_elapsed_trace().FrameAt(0U)
                                            : std::nullopt;
    const auto topology_entry_primary = topology_entry_pair != nullptr
                                            ? topology_entry_pair->input_trace().ActionAt(
                                                  0U, omega::app::kFrontEndPrimaryAction)
                                            : std::nullopt;
    const RunResult topology_entry_result = topology_entry->result();
    const omega::app::GpuHostSnapshot topology_entry_gpu_after =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(topology_entry_pair != nullptr && topology_entry_elapsed &&
              topology_entry_elapsed->elapsed == modal_proof_elapsed &&
              topology_entry_primary && topology_entry_primary->held &&
              topology_entry_primary->pressed &&
              topology_entry_result.input_frames == 1U &&
              topology_entry_result.rendered_frames == 1 &&
              topology_entry_result.planned_simulation_steps == 0U &&
              topology_entry_result.executed_simulation_steps == 0U &&
              topology_entry->scheduler_state_before() == modal_scheduler_before &&
              topology_entry->scheduler_state_after() == modal_scheduler_before &&
              OmegaAppTestAccess::SchedulerSnapshot(*app) == modal_scheduler_before &&
              SameSimulationState(OmegaAppTestAccess::SimulationSnapshot(*app),
                  modal_simulation_before) &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) == modal_position_before &&
              OmegaAppTestAccess::FrontEnd(*app) == kAssetTopologyRowThree &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  initial_asset_topology_draw_list) &&
              IsOneModalCardSubmission(
                  topology_entry_gpu_before, topology_entry_gpu_after),
        "primary priority enters AssetTopology on the same frame while raw elapsed remains captured and every simulation owner stays frozen");

    Check(PushKey(SDL_SCANCODE_F1, true),
        "held AssetTopology primary enters the queue");
    auto topology_held = app->RunWithCapture(1);
    Check(topology_held.has_value(), "held AssetTopology primary captures");
    if (!topology_held)
        return EXIT_FAILURE;
    const auto* topology_held_pair = topology_held->trace_pair();
    const auto topology_held_primary = topology_held_pair != nullptr
                                           ? topology_held_pair->input_trace().ActionAt(
                                                 0U, omega::app::kFrontEndPrimaryAction)
                                           : std::nullopt;
    const omega::app::GpuHostSnapshot topology_held_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(topology_held_primary && topology_held_primary->held &&
              !topology_held_primary->pressed && !topology_held_primary->released &&
              topology_held->result().planned_simulation_steps == 0U &&
              topology_held->result().executed_simulation_steps == 0U &&
              topology_held->scheduler_state_before() == modal_scheduler_before &&
              topology_held->scheduler_state_after() == modal_scheduler_before &&
              SameSimulationState(OmegaAppTestAccess::SimulationSnapshot(*app),
                  modal_simulation_before) &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) == modal_position_before &&
              OmegaAppTestAccess::FrontEnd(*app) == kAssetTopologyRowThree &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  initial_asset_topology_draw_list) &&
              IsOneModalCardSubmission(topology_entry_gpu_after, topology_held_gpu),
        "held primary does not repeat and AssetTopology remains an exact base-plus-card modal frame");

    Check(PushKey(SDL_SCANCODE_F1, false) && PushKey(SDL_SCANCODE_W, false),
        "AssetTopology primary and held navigation release");
    auto topology_released = app->RunWithCapture(1);
    Check(topology_released.has_value(), "AssetTopology release captures");
    if (!topology_released)
        return EXIT_FAILURE;
    const auto* topology_released_pair = topology_released->trace_pair();
    const auto topology_released_primary = topology_released_pair != nullptr
                                               ? topology_released_pair->input_trace().ActionAt(
                                                     0U,
                                                     omega::app::kFrontEndPrimaryAction)
                                               : std::nullopt;
    const omega::app::GpuHostSnapshot topology_released_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(topology_released_primary && !topology_released_primary->held &&
              !topology_released_primary->pressed && topology_released_primary->released &&
              topology_released->result().planned_simulation_steps == 0U &&
              topology_released->result().executed_simulation_steps == 0U &&
              topology_released->scheduler_state_before() == modal_scheduler_before &&
              topology_released->scheduler_state_after() == modal_scheduler_before &&
              SameSimulationState(OmegaAppTestAccess::SimulationSnapshot(*app),
                  modal_simulation_before) &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) == modal_position_before &&
              OmegaAppTestAccess::FrontEnd(*app) == kAssetTopologyRowThree &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  initial_asset_topology_draw_list) &&
              IsOneModalCardSubmission(topology_held_gpu, topology_released_gpu),
        "release edges preserve AssetTopology, its base-plus-card render, and every frozen simulation owner");

    const std::uint64_t topology_terminal_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    Check(PushKey(SDL_SCANCODE_F1, true) && PushQuitKey(true) && PushQuit(),
        "AssetTopology primary and simultaneous terminal reasons enter the queue");
    auto topology_terminal = app->RunWithCapture(1);
    Check(topology_terminal.has_value(), "AssetTopology terminal precedence captures");
    if (!topology_terminal)
        return EXIT_FAILURE;
    const auto* topology_terminal_pair = topology_terminal->trace_pair();
    const auto topology_terminal_reason = topology_terminal->terminal_input();
    const auto topology_terminal_primary = topology_terminal_pair != nullptr
                                               ? topology_terminal_pair->input_trace().ActionAt(
                                                     0U,
                                                     omega::app::kFrontEndPrimaryAction)
                                               : std::nullopt;
    Check(topology_terminal->completion() == RunCaptureCompletion::QuitRequested &&
              topology_terminal_reason && topology_terminal_pair != nullptr &&
              topology_terminal_reason->frame_index == topology_terminal_index &&
              topology_terminal_reason->host_quit_requested &&
              topology_terminal_reason->logical_quit_pressed &&
              topology_terminal_primary && topology_terminal_primary->held &&
              topology_terminal_primary->pressed &&
              topology_terminal->scheduler_state_before() == modal_scheduler_before &&
              topology_terminal->scheduler_state_after() == modal_scheduler_before &&
              SameSimulationState(OmegaAppTestAccess::SimulationSnapshot(*app),
                  modal_simulation_before) &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) == modal_position_before &&
              OmegaAppTestAccess::FrontEnd(*app) == kAssetTopologyRowThree &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  initial_asset_topology_draw_list) &&
              OmegaAppTestAccess::GpuSnapshot(*app) == topology_released_gpu,
        "terminal resolution captures the AssetTopology primary edge without reducing, rendering, or mutating any owner");

    Check(PushQuitKey(false) && PushKey(SDL_SCANCODE_F1, false),
        "AssetTopology terminal inputs release");
    const omega::app::GpuHostSnapshot topology_terminal_release_gpu_before =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(RunPlainFrame(), "AssetTopology terminal release frame completes");
    const omega::app::GpuHostSnapshot topology_terminal_release_gpu_after =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(OmegaAppTestAccess::FrontEnd(*app) == kAssetTopologyRowThree &&
              OmegaAppTestAccess::SchedulerSnapshot(*app) == modal_scheduler_before &&
              SameSimulationState(OmegaAppTestAccess::SimulationSnapshot(*app),
                  modal_simulation_before) &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) == modal_position_before &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  initial_asset_topology_draw_list) &&
              IsOneModalCardSubmission(topology_terminal_release_gpu_before,
                  topology_terminal_release_gpu_after),
        "terminal release resumes the unchanged AssetTopology base-plus-card screen");

    const omega::app::GpuHostSnapshot topology_exit_gpu_before =
        OmegaAppTestAccess::GpuSnapshot(*app);
    const bool topology_exit_events = PushKey(SDL_SCANCODE_BACKSPACE, true);
    Check(topology_exit_events,
        "fresh AssetTopology cancel edge enters");
    Check(OmegaAppTestAccess::ArmNextRunElapsed(*app, modal_proof_elapsed),
        "the AssetTopology-exit modal clock proof arms");
    auto topology_exit = app->RunWithCapture(1);
    Check(topology_exit.has_value(), "AssetTopology-to-Main return captures");
    if (!topology_exit)
        return EXIT_FAILURE;
    const auto* topology_exit_pair = topology_exit->trace_pair();
    const auto topology_exit_elapsed = topology_exit_pair != nullptr
                                           ? topology_exit_pair->scheduler_elapsed_trace().FrameAt(0U)
                                           : std::nullopt;
    const RunResult topology_exit_result = topology_exit->result();
    const omega::app::GpuHostSnapshot topology_exit_gpu_after =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(topology_exit_pair != nullptr && topology_exit_elapsed &&
              topology_exit_elapsed->elapsed == modal_proof_elapsed &&
              topology_exit_result.input_frames == 1U &&
              topology_exit_result.rendered_frames == 1 &&
              topology_exit_result.planned_simulation_steps == 0U &&
              topology_exit_result.executed_simulation_steps == 0U &&
              topology_exit->scheduler_state_before() == modal_scheduler_before &&
              topology_exit->scheduler_state_after() == modal_scheduler_before &&
              OmegaAppTestAccess::SchedulerSnapshot(*app) == modal_scheduler_before &&
              SameSimulationState(OmegaAppTestAccess::SimulationSnapshot(*app),
                  modal_simulation_before) &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) == modal_position_before &&
              OmegaAppTestAccess::FrontEnd(*app) == kMainRowThree &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  initial_visible_draw_lists[3]) &&
              IsOneVisibleMenuSubmission(
                  topology_exit_gpu_before, topology_exit_gpu_after),
        "fresh cancel returns AssetTopology to Main row three without advancing accumulated modal time");
    Check(PushKey(SDL_SCANCODE_BACKSPACE, false),
        "returned AssetTopology Main cancel releases");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::FrontEnd(*app) == kMainRowThree,
        "AssetTopology return release preserves Main row three");

    for (int row = 0; row < 3; ++row)
    {
        Check(PushKey(SDL_SCANCODE_UP, true),
            "Up-arrow previous-row edge enters the SDL queue");
        Check(RunPlainFrame(), "Up-arrow previous-row frame completes");
        Check(PushKey(SDL_SCANCODE_UP, false),
            "Up-arrow previous-row edge releases");
        Check(RunPlainFrame(), "Up-arrow previous-row release completes");
    }
    Check(OmegaAppTestAccess::FrontEnd(*app) ==
              omega::app::InitialFrontEndState(),
        "three previous edges return row three to row zero");
    Check(PushKey(SDL_SCANCODE_UP, true),
        "Up-arrow upper-bound edge enters the SDL queue");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::FrontEnd(*app) ==
                  omega::app::InitialFrontEndState(),
        "Up-arrow previous clamps at row zero instead of wrapping");
    const omega::runtime::FrameSchedulerState play_resume_scheduler_before =
        OmegaAppTestAccess::SchedulerSnapshot(*app);
    Check(PushKey(SDL_SCANCODE_UP, false) && PushKey(SDL_SCANCODE_F1, true),
        "the Up-arrow alias releases as the row-zero primary edge enters");
    auto play_resume = app->RunWithCapture(1);
    Check(play_resume.has_value(), "row-zero primary activation captures");
    if (!play_resume)
        return EXIT_FAILURE;
    const auto* play_resume_pair = play_resume->trace_pair();
    const auto play_resume_elapsed = play_resume_pair != nullptr
                                         ? play_resume_pair->scheduler_elapsed_trace().FrameAt(0U)
                                         : std::nullopt;
    std::optional<ExpectedSchedulerAdvance> expected_play_resume;
    if (play_resume_elapsed)
    {
        expected_play_resume = AdvanceSchedulerSnapshot(
            play_resume_scheduler_before, play_resume_elapsed->elapsed);
    }
    const RunResult play_resume_result = play_resume->result();
    Check(play_resume_pair != nullptr && play_resume_elapsed && expected_play_resume &&
              play_resume_scheduler_before == modal_scheduler_before &&
              play_resume->scheduler_state_before() == play_resume_scheduler_before &&
              play_resume->scheduler_state_after() == expected_play_resume->state &&
              OmegaAppTestAccess::SchedulerSnapshot(*app) == expected_play_resume->state &&
              play_resume_result.planned_simulation_steps ==
                  expected_play_resume->plan.simulation_steps &&
              play_resume_result.executed_simulation_steps ==
                  expected_play_resume->plan.simulation_steps &&
              play_resume_result.clamped_frame_count ==
                  (expected_play_resume->plan.clamped_delta ? 1U : 0U) &&
              play_resume_result.dropped_time_frame_count ==
                  (expected_play_resume->plan.dropped_time ? 1U : 0U) &&
              OmegaAppTestAccess::FrontEnd(*app) == kDiagnosticPlayRowZero,
        "row-zero activation resumes from the frozen scheduler using only its own captured elapsed sample, with no modal-card catch-up");
    Check(PushKey(SDL_SCANCODE_F1, false), "row-zero primary releases");
    auto ready_for_terminal = app->RunWithCapture(1);
    Check(ready_for_terminal.has_value(),
        "DiagnosticPlay is ready for a terminal-priority frame");
    if (!ready_for_terminal)
        return EXIT_FAILURE;
    const omega::runtime::RenderDrawList keyboard_mouse_base_actor_draw_list =
        OmegaAppTestAccess::DiagnosticActorDrawList(*app);
    const auto keyboard_mouse_base_commands =
        keyboard_mouse_base_actor_draw_list.commands();
    const std::size_t keyboard_mouse_base_command_count =
        keyboard_mouse_base_commands.size();
    const auto marker_command_is_exact =
        [diagnostic_actor_marker_texture, kFullMenuSource](
            const omega::runtime::RenderTextureBlitCommand& command,
            const omega::runtime::RenderTargetRectQ16 destination) {
            return command.texture == diagnostic_actor_marker_texture &&
                   command.source == kFullMenuSource &&
                   command.destination == destination &&
                   command.fit_mode ==
                       omega::runtime::RenderTextureFitMode::Stretch &&
                   command.filter_mode ==
                       omega::runtime::RenderTextureFilterMode::Nearest;
        };
    const auto has_exact_base_prefix =
        [keyboard_mouse_base_commands](
            const std::span<const omega::runtime::RenderTextureBlitCommand>
                commands) {
            return commands.size() >= keyboard_mouse_base_commands.size() &&
                   std::equal(keyboard_mouse_base_commands.begin(),
                       keyboard_mouse_base_commands.end(), commands.begin());
        };

    const auto app_window = ResolveOpenOmegaWindow();
    Check(app_window.has_value(),
        "the live app exposes its current logical SDL window geometry");
    if (!app_window)
        return EXIT_FAILURE;
    const float pointer_x =
        static_cast<float>(app_window->logical_width) / 4.0F;
    const float pointer_y =
        static_cast<float>(app_window->logical_height) * 3.0F / 4.0F;
    const omega::app::GpuHostSnapshot pointer_gpu_before =
        OmegaAppTestAccess::GpuSnapshot(*app);

    Check(OmegaAppTestAccess::ClearPointerPosition(*app) &&
              PushMouseButton(SDL_BUTTON_LEFT, true),
        "the fallback fixture clears pointer availability before a left mouse press enters DiagnosticPlay");
    auto mouse_fire = app->RunWithCapture(1);
    const auto mouse_fire_commands =
        OmegaAppTestAccess::DiagnosticActorDrawList(*app).commands();
    const bool centered_mouse_fire_draw_is_exact =
        mouse_fire_commands.size() == keyboard_mouse_base_command_count + 1U &&
        has_exact_base_prefix(mouse_fire_commands) &&
        marker_command_is_exact(
            mouse_fire_commands[keyboard_mouse_base_command_count],
            kFireCueDestination);
    Check(mouse_fire &&
              mouse_fire->completion() ==
                  omega::app::RunCaptureCompletion::FrameLimitReached &&
              !mouse_fire->failure() &&
              mouse_fire->result().rendered_frames == 1 &&
              OmegaAppTestAccess::FrontEnd(*app) == kDiagnosticPlayRowZero &&
              OmegaAppTestAccess::DiagnosticActorDrawList(*app).commands().size() ==
                  keyboard_mouse_base_command_count + 1U &&
              centered_mouse_fire_draw_is_exact &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  OmegaAppTestAccess::DiagnosticActorDrawList(*app)),
        "left mouse fires one exact centered project cue when no pointer sample is available, without opening a menu or requiring a gamepad");

    Check(PushMouseButton(SDL_BUTTON_LEFT, false) &&
              PushMouseButton(SDL_BUTTON_RIGHT, true, app_window->id,
                  pointer_x, pointer_y),
        "the fire release and positioned right-mouse target press enter the input queue");
    auto mouse_target = app->RunWithCapture(1);
    const omega::runtime::RenderDrawList mouse_target_draw_list =
        OmegaAppTestAccess::DiagnosticActorDrawList(*app);
    const auto mouse_target_commands = mouse_target_draw_list.commands();
    const auto* const mouse_target_pair =
        mouse_target ? mouse_target->trace_pair() : nullptr;
    const auto mouse_target_pointer = mouse_target_pair != nullptr
                                          ? mouse_target_pair->input_trace().PointerAt(0U)
                                          : std::nullopt;
    const bool positioned_mouse_target_draw_is_exact =
        mouse_target_commands.size() == keyboard_mouse_base_command_count + 2U &&
        has_exact_base_prefix(mouse_target_commands) &&
        marker_command_is_exact(
            mouse_target_commands[keyboard_mouse_base_command_count],
            kPositionedTargetCueDestinations[0U]) &&
        marker_command_is_exact(
            mouse_target_commands[keyboard_mouse_base_command_count + 1U],
            kPositionedTargetCueDestinations[1U]);
    Check(mouse_target &&
              mouse_target->completion() ==
                  omega::app::RunCaptureCompletion::FrameLimitReached &&
              !mouse_target->failure() &&
              mouse_target->result().rendered_frames == 1 &&
              mouse_target_pointer == kQuarterThreeQuarterPointer &&
              OmegaAppTestAccess::FrontEnd(*app) == kDiagnosticPlayRowZero &&
              positioned_mouse_target_draw_is_exact &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  OmegaAppTestAccess::DiagnosticActorDrawList(*app)),
        "positioned right mouse records the exact normalized pointer and holds both exact project target bars without leaving DiagnosticPlay");

    auto mouse_target_held = app->RunWithCapture(1);
    const auto held_target_action = CapturedActionState(
        mouse_target_held, omega::app::kDebugTargetAction);
    const auto* const held_target_pair =
        mouse_target_held ? mouse_target_held->trace_pair() : nullptr;
    const auto held_target_pointer = held_target_pair != nullptr
                                         ? held_target_pair->input_trace().PointerAt(0U)
                                         : std::nullopt;
    Check(mouse_target_held &&
              mouse_target_held->completion() ==
                  omega::app::RunCaptureCompletion::FrameLimitReached &&
              !mouse_target_held->failure() &&
              mouse_target_held->result().rendered_frames == 1 &&
              held_target_pointer == kQuarterThreeQuarterPointer &&
              held_target_action && held_target_action->held &&
              !held_target_action->pressed && !held_target_action->released &&
              OmegaAppTestAccess::FrontEnd(*app) == kDiagnosticPlayRowZero &&
              DrawListsEqual(
                  OmegaAppTestAccess::DiagnosticActorDrawList(*app),
                  mouse_target_draw_list),
        "held right mouse persists the exact normalized pointer and targeting cue across a later frame without a repeated press edge");

    Check(PushMouseButton(SDL_BUTTON_LEFT, true, app_window->id,
              pointer_x, pointer_y),
        "a positioned left-mouse fire press enters the queue while right-mouse targeting remains held");
    auto mouse_target_fire = app->RunWithCapture(1);
    const auto mouse_target_fire_commands =
        OmegaAppTestAccess::DiagnosticActorDrawList(*app).commands();
    const auto chord_target_action = CapturedActionState(
        mouse_target_fire, omega::app::kDebugTargetAction);
    const auto* const mouse_target_fire_pair =
        mouse_target_fire ? mouse_target_fire->trace_pair() : nullptr;
    const auto mouse_target_fire_pointer = mouse_target_fire_pair != nullptr
                                               ? mouse_target_fire_pair->input_trace().PointerAt(
                                                     0U)
                                               : std::nullopt;
    const bool mouse_target_fire_draw_is_exact =
        mouse_target_fire_commands.size() ==
            keyboard_mouse_base_command_count + 3U &&
        has_exact_base_prefix(mouse_target_fire_commands) &&
        marker_command_is_exact(
            mouse_target_fire_commands[keyboard_mouse_base_command_count],
            kPositionedTargetCueDestinations[0U]) &&
        marker_command_is_exact(
            mouse_target_fire_commands[keyboard_mouse_base_command_count + 1U],
            kPositionedTargetCueDestinations[1U]) &&
        marker_command_is_exact(
            mouse_target_fire_commands[keyboard_mouse_base_command_count + 2U],
            kPositionedFireCueDestination);
    Check(mouse_target_fire &&
              mouse_target_fire->completion() ==
                  omega::app::RunCaptureCompletion::FrameLimitReached &&
              !mouse_target_fire->failure() &&
              mouse_target_fire->result().rendered_frames == 1 &&
              IsFreshPress(CapturedActionState(
                  mouse_target_fire, omega::app::kDebugFireAction)) &&
              mouse_target_fire_pointer == kQuarterThreeQuarterPointer &&
              chord_target_action && chord_target_action->held &&
              !chord_target_action->pressed && !chord_target_action->released &&
              OmegaAppTestAccess::FrontEnd(*app) == kDiagnosticPlayRowZero &&
              mouse_target_fire_draw_is_exact &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  OmegaAppTestAccess::DiagnosticActorDrawList(*app)),
        "positioned left-mouse fire composes its exact moved square after both exact targeting bars while right mouse remains held");

    Check(PushMouseButton(SDL_BUTTON_LEFT, false) &&
              PushMouseButton(SDL_BUTTON_RIGHT, false),
        "the chord's fire and target releases enter the input queue");
    auto mouse_released = app->RunWithCapture(1);
    Check(mouse_released &&
              mouse_released->completion() ==
                  omega::app::RunCaptureCompletion::FrameLimitReached &&
              !mouse_released->failure() &&
              mouse_released->result().rendered_frames == 1 &&
              IsRelease(CapturedActionState(
                  mouse_released, omega::app::kDebugFireAction)) &&
              IsRelease(CapturedActionState(
                  mouse_released, omega::app::kDebugTargetAction)) &&
              OmegaAppTestAccess::FrontEnd(*app) == kDiagnosticPlayRowZero &&
              DrawListsEqual(OmegaAppTestAccess::DiagnosticActorDrawList(*app),
                  keyboard_mouse_base_actor_draw_list),
        "releasing the mouse chord restores the ordinary actor/objective draw without menu mutation");

    constexpr auto kTargetMarkerDestination =
        omega::app::PlanProjectDiagnosticTargetMarkerDestination(
            omega::gameplay::DiagnosticProximityTriggerState{
                .objective_complete = true},
            omega::gameplay::DiagnosticTargetFireState{});
    static_assert(kTargetMarkerDestination.has_value());
    constexpr omega::runtime::PointerPositionQ16 kExactTargetPointer{
        .x = 49'152U,
        .y = 32'768U,
    };
    constexpr auto kExactTargetCues =
        omega::app::PlanProjectDiagnosticTargetCueRectangles(
            std::optional<omega::runtime::PointerPositionQ16>{
                kExactTargetPointer});
    constexpr auto kExactTargetFireCue =
        omega::app::PlanProjectDiagnosticFireCueRectangle(
            std::optional<omega::runtime::PointerPositionQ16>{
                kExactTargetPointer});
    const auto has_exact_base_actor_prefix =
        [keyboard_mouse_base_commands](
            const std::span<const omega::runtime::RenderTextureBlitCommand>
                commands) {
            return keyboard_mouse_base_commands.size() >= 2U &&
                   commands.size() >= 2U &&
                   commands[0] == keyboard_mouse_base_commands[0] &&
                   commands[1] == keyboard_mouse_base_commands[1];
        };
    const omega::app::GpuHostSnapshot target_flow_gpu_before =
        OmegaAppTestAccess::GpuSnapshot(*app);
    const std::size_t target_flow_binding_count =
        OmegaAppTestAccess::InputBindingCount(*app);
    const std::size_t target_flow_action_count =
        OmegaAppTestAccess::InputActionCount(*app);
    const auto target_flow_persistence_generation =
        OmegaAppTestAccess::PersistenceGeneration(*app);
    const auto target_flow_persistence_records =
        OmegaAppTestAccess::PersistenceRecordCount(*app);
    const auto target_flow_persistence_bytes =
        OmegaAppTestAccess::PersistenceLogicalValueBytes(*app);
    OmegaAppTestAccess::SetDiagnosticProximityTriggerState(
        *app, {.objective_complete = true});
    Check(OmegaAppTestAccess::ArmNextRunElapsed(
              *app, std::chrono::nanoseconds::zero()),
        "the fallback target-ready frame arms with zero elapsed");
    auto target_ready = app->RunWithCapture(1);
    const auto target_ready_commands =
        OmegaAppTestAccess::DiagnosticActorDrawList(*app).commands();
    Check(target_ready && !target_ready->failure() &&
              target_ready->result().planned_simulation_steps == 0U &&
              target_ready->result().executed_simulation_steps == 0U &&
              OmegaAppTestAccess::DiagnosticTargetFireState(*app) ==
                  omega::gameplay::DiagnosticTargetFireState{} &&
              target_ready_commands.size() == 3U &&
              has_exact_base_actor_prefix(target_ready_commands) &&
              marker_command_is_exact(
                  target_ready_commands[2], *kTargetMarkerDestination),
        "completed proximity replaces the fallback objective marker with the exact project target on a successful zero-step frame");

    Check(OmegaAppTestAccess::ArmNextRunElapsed(
              *app, std::chrono::nanoseconds::zero()) &&
              PushMouseButton(SDL_BUTTON_RIGHT, true, app_window->id,
                  pointer_x, pointer_y) &&
              PushMouseButton(SDL_BUTTON_LEFT, true, app_window->id,
                  pointer_x, pointer_y),
        "the fallback miss chord enters the input queue at an off-target pointer");
    auto target_miss = app->RunWithCapture(1);
    const auto target_miss_commands =
        OmegaAppTestAccess::DiagnosticActorDrawList(*app).commands();
    Check(target_miss && !target_miss->failure() &&
              target_miss->result().planned_simulation_steps == 0U &&
              OmegaAppTestAccess::DiagnosticTargetFireState(*app) ==
                  omega::gameplay::DiagnosticTargetFireState{} &&
              target_miss_commands.size() == 6U &&
              has_exact_base_actor_prefix(target_miss_commands) &&
              marker_command_is_exact(
                  target_miss_commands[2], *kTargetMarkerDestination) &&
              marker_command_is_exact(target_miss_commands[3],
                  kPositionedTargetCueDestinations[0U]) &&
              marker_command_is_exact(target_miss_commands[4],
                  kPositionedTargetCueDestinations[1U]) &&
              marker_command_is_exact(target_miss_commands[5],
                  kPositionedFireCueDestination),
        "an off-target aimed fire attempt preserves the fallback target and orders target bars then fire after it");
    Check(PushMouseButton(SDL_BUTTON_LEFT, false) &&
              PushMouseButton(SDL_BUTTON_RIGHT, false) &&
              OmegaAppTestAccess::ArmNextRunElapsed(
                  *app, std::chrono::nanoseconds::zero()) &&
              app->Run(1).has_value(),
        "the fallback miss chord releases without advancing simulation");

    const float exact_target_x =
        static_cast<float>(app_window->logical_width) * 3.0F / 4.0F;
    const float exact_target_y =
        static_cast<float>(app_window->logical_height) / 2.0F;
    Check(OmegaAppTestAccess::ArmNextRunElapsed(
              *app, std::chrono::nanoseconds::zero()) &&
              PushMouseButton(SDL_BUTTON_RIGHT, true, app_window->id,
                  exact_target_x, exact_target_y) &&
              PushMouseButton(SDL_BUTTON_LEFT, true, app_window->id,
                  exact_target_x, exact_target_y),
        "the exact fallback aimed-fire chord enters the input queue");
    auto target_hit = app->RunWithCapture(1);
    const auto* target_hit_pair = target_hit ? target_hit->trace_pair() : nullptr;
    const auto target_hit_pointer = target_hit_pair != nullptr
        ? target_hit_pair->input_trace().PointerAt(0U)
        : std::nullopt;
    const auto target_hit_commands =
        OmegaAppTestAccess::DiagnosticActorDrawList(*app).commands();
    Check(target_hit && !target_hit->failure() &&
              target_hit->result().planned_simulation_steps == 0U &&
              target_hit->result().executed_simulation_steps == 0U &&
              target_hit_pointer == kExactTargetPointer &&
              OmegaAppTestAccess::DiagnosticTargetFireState(*app) ==
                  omega::gameplay::DiagnosticTargetFireState{
                      .target_complete = true} &&
              target_hit_commands.size() == 5U &&
              has_exact_base_actor_prefix(target_hit_commands) &&
              marker_command_is_exact(
                  target_hit_commands[2], kExactTargetCues[0U]) &&
              marker_command_is_exact(
                  target_hit_commands[3], kExactTargetCues[1U]) &&
              marker_command_is_exact(
                  target_hit_commands[4], kExactTargetFireCue),
        "an exact aimed RMB plus LMB hit latches completion, removes the fallback target on the hit frame, and retains ordered transient cues");
    Check(PushMouseButton(SDL_BUTTON_LEFT, false) &&
              PushMouseButton(SDL_BUTTON_RIGHT, false) &&
              OmegaAppTestAccess::ArmNextRunElapsed(
                  *app, std::chrono::nanoseconds::zero()) &&
              app->Run(1).has_value() &&
              OmegaAppTestAccess::DiagnosticActorDrawList(*app).commands().size() ==
                  2U &&
              OmegaAppTestAccess::DiagnosticTargetFireState(*app).target_complete,
        "releasing the exact hit chord leaves only the fallback base and actor with completion latched");

    OmegaAppTestAccess::SetDiagnosticProximityTriggerState(*app, {});
    OmegaAppTestAccess::SetDiagnosticTargetFireState(*app, {});
    Check(OmegaAppTestAccess::ArmNextRunElapsed(
              *app, std::chrono::nanoseconds::zero()) &&
              app->Run(1).has_value() &&
              DrawListsEqual(OmegaAppTestAccess::DiagnosticActorDrawList(*app),
                  keyboard_mouse_base_actor_draw_list) &&
              SameTextureResidency(target_flow_gpu_before,
                  OmegaAppTestAccess::GpuSnapshot(*app)) &&
              OmegaAppTestAccess::GpuSnapshot(*app).meshes ==
                  target_flow_gpu_before.meshes &&
              OmegaAppTestAccess::InputBindingCount(*app) ==
                  target_flow_binding_count &&
              OmegaAppTestAccess::InputActionCount(*app) ==
                  target_flow_action_count &&
              OmegaAppTestAccess::PersistenceGeneration(*app) ==
                  target_flow_persistence_generation &&
              OmegaAppTestAccess::PersistenceRecordCount(*app) ==
                  target_flow_persistence_records &&
              OmegaAppTestAccess::PersistenceLogicalValueBytes(*app) ==
                  target_flow_persistence_bytes,
        "the fallback target fixture restores its launch-local state without GPU residency, input schema, gamepad, or persistence ownership changes");

    const omega::app::GpuHostSnapshot ready_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    const omega::runtime::RenderDrawList ready_actor_draw_list =
        OmegaAppTestAccess::DiagnosticActorDrawList(*app);

    Check(SameTextureResidency(pointer_gpu_before, ready_gpu) &&
              ready_gpu.meshes == pointer_gpu_before.meshes &&
              ready_gpu.successful_mesh_uploads ==
                  pointer_gpu_before.successful_mesh_uploads &&
              ready_gpu.successful_mesh_releases ==
                  pointer_gpu_before.successful_mesh_releases,
        "pointer target/fire presentation changes no texture or mesh residency");

    Check(DrawListsEqual(OmegaAppTestAccess::DiagnosticHiddenDrawList(*app),
              initial_hidden_draw_list) &&
              DrawListArraysEqual(OmegaAppTestAccess::FrontEndMainDrawLists(*app),
                   initial_visible_draw_lists) &&
              DrawListsEqual(OmegaAppTestAccess::FrontEndProfilesDrawList(*app),
                   initial_profiles_draw_list) &&
              DrawListsEqual(OmegaAppTestAccess::DiagnosticControlsDrawList(*app),
                   initial_controls_draw_list) &&
              DrawListsEqual(
                  OmegaAppTestAccess::DiagnosticAssetTopologyDrawList(*app),
                  initial_asset_topology_draw_list) &&
              OmegaAppTestAccess::DiagnosticTexture(*app) == diagnostic_texture &&
              OmegaAppTestAccess::DiagnosticActorMarkerTexture(*app) ==
                  diagnostic_actor_marker_texture &&
              OmegaAppTestAccess::FrontEndTexture(*app) ==
                  front_end_texture &&
              OmegaAppTestAccess::FrontEndProfilesTexture(*app) ==
                  front_end_profiles_texture &&
              OmegaAppTestAccess::DiagnosticControlsTexture(*app) ==
                  diagnostic_controls_texture &&
              OmegaAppTestAccess::DiagnosticAssetTopologyTexture(*app) ==
                  diagnostic_asset_topology_texture &&
              OmegaAppTestAccess::DiagnosticAssetTransferTexture(*app) ==
                  diagnostic_asset_transfer_texture &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  ready_actor_draw_list) &&
              SameTextureResidency(initial_gpu, ready_gpu),
        "navigation preserves all immutable presentation resources and their startup uploads");

    const auto debug_position_before_terminal =
        OmegaAppTestAccess::DebugLocomotionPosition(*app);
    const auto trigger_state_before_terminal =
        OmegaAppTestAccess::DiagnosticProximityTriggerState(*app);
    const omega::runtime::FrameSchedulerState scheduler_before_terminal =
        OmegaAppTestAccess::SchedulerSnapshot(*app);
    const std::uint64_t terminal_frame_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    Check(PushKey(SDL_SCANCODE_F1, true) &&
              PushKey(SDL_SCANCODE_BACKSPACE, true) && PushQuitKey(true) && PushQuit(),
        "fresh confirm/cancel edges and simultaneous quit reasons enter the SDL queue");
    auto both = app->RunWithCapture(1);
    Check(both.has_value(), "simultaneous quit reasons publish a terminal capture");
    if (!both)
        return EXIT_FAILURE;
    const auto* both_pair = both->trace_pair();
    const auto both_terminal = both->terminal_input();
    const auto terminal_menu_action = both_pair != nullptr
                                          ? both_pair->input_trace().ActionAt(
                                                0U,
                                                omega::app::kFrontEndPrimaryAction)
                                          : std::nullopt;
    const auto terminal_cancel_action = both_pair != nullptr
                                            ? both_pair->input_trace().ActionAt(
                                                  0U,
                                                  omega::app::kFrontEndCancelAction)
                                            : std::nullopt;
    const omega::app::GpuHostSnapshot terminal_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(both->completion() == RunCaptureCompletion::QuitRequested && both_terminal &&
              both_pair != nullptr &&
              both_terminal->frame_index == terminal_frame_index &&
              both_terminal->host_quit_requested &&
              both_terminal->logical_quit_pressed &&
              terminal_menu_action && terminal_menu_action->held &&
              terminal_menu_action->pressed && !terminal_menu_action->released &&
              terminal_cancel_action && terminal_cancel_action->held &&
              terminal_cancel_action->pressed && !terminal_cancel_action->released &&
              both->scheduler_state_before() == scheduler_before_terminal &&
              both->scheduler_state_after() == scheduler_before_terminal &&
              OmegaAppTestAccess::FrontEnd(*app) == kDiagnosticPlayRowZero &&
              OmegaAppTestAccess::DiagnosticTexture(*app) == diagnostic_texture &&
              OmegaAppTestAccess::DiagnosticActorMarkerTexture(*app) ==
                  diagnostic_actor_marker_texture &&
              OmegaAppTestAccess::FrontEndTexture(*app) ==
                  front_end_texture &&
              OmegaAppTestAccess::FrontEndProfilesTexture(*app) ==
                  front_end_profiles_texture &&
              OmegaAppTestAccess::DiagnosticControlsTexture(*app) ==
                   diagnostic_controls_texture &&
              OmegaAppTestAccess::DiagnosticAssetTopologyTexture(*app) ==
                  diagnostic_asset_topology_texture &&
              OmegaAppTestAccess::DiagnosticAssetTransferTexture(*app) ==
                  diagnostic_asset_transfer_texture &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) ==
                  debug_position_before_terminal &&
              SameProximityTriggerState(
                  OmegaAppTestAccess::DiagnosticProximityTriggerState(*app),
                  trigger_state_before_terminal) &&
              DrawListsEqual(OmegaAppTestAccess::DiagnosticHiddenDrawList(*app),
                  initial_hidden_draw_list) &&
              DrawListArraysEqual(OmegaAppTestAccess::FrontEndMainDrawLists(*app),
                  initial_visible_draw_lists) &&
              DrawListsEqual(OmegaAppTestAccess::FrontEndProfilesDrawList(*app),
                  initial_profiles_draw_list) &&
              DrawListsEqual(OmegaAppTestAccess::DiagnosticControlsDrawList(*app),
                   initial_controls_draw_list) &&
              DrawListsEqual(
                  OmegaAppTestAccess::DiagnosticAssetTopologyDrawList(*app),
                  initial_asset_topology_draw_list) &&
              DrawListsEqual(OmegaAppTestAccess::DiagnosticActorDrawList(*app),
                  ready_actor_draw_list) &&
              DrawListsEqual(OmegaAppTestAccess::CurrentFrontEndDrawList(*app),
                  ready_actor_draw_list) &&
              terminal_gpu == ready_gpu,
        "terminal action-6 and action-7 edges perform no trigger, render, menu, or resource mutation");

    Check(PushQuitKey(false) && PushKey(SDL_SCANCODE_F1, false) &&
              PushKey(SDL_SCANCODE_BACKSPACE, false),
        "the final F10, confirm, and cancel releases enter the SDL queue");
    Check(omega::app::detail::OmegaAppTestAccess::InstallUnownedDiagnosticDraw(*app),
        "the operational-failure fixture installs an unowned diagnostic draw");
    const omega::app::GpuHostSnapshot failure_gpu_before =
        omega::app::detail::OmegaAppTestAccess::GpuSnapshot(*app);
    const std::uint64_t failure_frame_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    auto failed = app->RunWithCapture(1);
    Check(failed.has_value(), "a render error publishes a partial capture outcome");
    if (!failed)
        return EXIT_FAILURE;
    const auto* failed_pair = failed->trace_pair();
    const RunResult failed_result = failed->result();
    Check(failed->completion() == RunCaptureCompletion::OperationalFailure &&
              failed->failure() == std::optional<std::string_view>{
                                       "render frame draw texture resolve: invalid-handle"} &&
              failed_pair != nullptr &&
              failed_result.input_frames == 1U && failed_result.rendered_frames == 0 &&
              failed_result.planned_simulation_steps ==
                  failed_result.executed_simulation_steps &&
              !failed_result.quit_requested,
        "a real render failure retains partial counters, failure text, and traces");
    omega::app::GpuHostSnapshot expected_failure_gpu = failure_gpu_before;
    ++expected_failure_gpu.rejected_nondefault_texture_handles;
    const omega::app::GpuHostSnapshot failure_gpu_after =
        omega::app::detail::OmegaAppTestAccess::GpuSnapshot(*app);
    Check(failure_gpu_after == expected_failure_gpu,
        "the rejected handle changes only its pre-acquisition diagnostic counter");
    if (failed_pair != nullptr)
    {
        Check(failed_pair->input_trace().first_frame_index() == failure_frame_index &&
                  failed_pair->input_trace().frame_count() == 1U &&
                  failed_pair->scheduler_elapsed_trace().first_frame_index() ==
                      failure_frame_index &&
                  failed_pair->scheduler_elapsed_trace().frame_count() == 1U &&
                  !failed_pair->terminal_input(),
            "the failed render remains after one exact paired input and elapsed sample");
    }
    const omega::runtime::FrameSchedulerState failed_after =
        failed->scheduler_state_after();

    omega::app::detail::OmegaAppTestAccess::ClearDiagnosticDraw(*app);
    const std::uint64_t continued_frame_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    auto continued = app->RunWithCapture(1);
    Check(continued.has_value(), "capture continues after clearing the render fixture");
    if (!continued)
        return EXIT_FAILURE;
    const auto* continued_pair = continued->trace_pair();
    const omega::app::GpuHostSnapshot continued_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    const auto continued_actor_commands =
        OmegaAppTestAccess::DiagnosticActorDrawList(*app).commands();
    Check(continued->completion() == RunCaptureCompletion::FrameLimitReached &&
              continued_pair != nullptr &&
              continued->scheduler_state_before() == failed_after &&
              continued->result().input_frames == 1U &&
              continued->result().rendered_frames == 1 &&
              OmegaAppTestAccess::FrontEnd(*app) == kDiagnosticPlayRowZero &&
              continued_actor_commands.size() == 2U &&
              continued_actor_commands[0].texture ==
                  diagnostic_actor_marker_texture &&
              continued_actor_commands[1].texture ==
                  diagnostic_actor_marker_texture &&
              continued_actor_commands[1].destination ==
                  *kObjectiveDestination &&
              IsOneDiagnosticActorAndObjectiveSubmission(
                  failure_gpu_after, continued_gpu),
        "capture resumes with actor and armed-objective submissions at the scheduler boundary");
    if (continued_pair != nullptr)
    {
        Check(continued_pair->input_trace().first_frame_index() ==
                      continued_frame_index &&
                  continued_pair->scheduler_elapsed_trace().first_frame_index() ==
                      continued_frame_index,
            "sequential capture continues the global input frame index");
    }

    const auto plain = app->Run(1);
    const omega::app::GpuHostSnapshot plain_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(plain && plain->rendered_frames == 1 && plain->input_frames == 1U &&
              !plain->quit_requested &&
              IsOneDiagnosticActorAndObjectiveSubmission(continued_gpu, plain_gpu),
        "plain Run adds actor and armed-objective submissions without reuploading any texture");

    Check(OmegaAppTestAccess::ArmNextRunElapsed(
              *app, std::chrono::nanoseconds::zero()),
        "the rejected actor marker fixture arms without advancing time");
    const omega::app::GpuHostSnapshot rejected_marker_gpu_before =
        OmegaAppTestAccess::GpuSnapshot(*app);
    const omega::runtime::RenderDrawList actor_draw_list_before_rejected_marker =
        OmegaAppTestAccess::DiagnosticActorDrawList(*app);
    OmegaAppTestAccess::SetDiagnosticActorMarkerTexture(*app, {});
    auto rejected_marker = app->RunWithCapture(1);
    OmegaAppTestAccess::SetDiagnosticActorMarkerTexture(
        *app, diagnostic_actor_marker_texture);
    Check(rejected_marker &&
              rejected_marker->completion() ==
                  RunCaptureCompletion::OperationalFailure &&
              rejected_marker->failure() ==
                  std::optional<std::string_view>{
                      "diagnostic actor draw-list creation failed"} &&
              rejected_marker->result().input_frames == 1U &&
              rejected_marker->result().rendered_frames == 0 &&
              OmegaAppTestAccess::GpuSnapshot(*app) ==
                  rejected_marker_gpu_before &&
              DrawListsEqual(OmegaAppTestAccess::DiagnosticActorDrawList(*app),
                  actor_draw_list_before_rejected_marker),
        "a rejected marker command is translated into a transactional operational failure");

    Check(OmegaAppTestAccess::InstallTwoCommandDiagnosticBase(*app) &&
              OmegaAppTestAccess::ArmNextRunElapsed(
                  *app, std::chrono::nanoseconds::zero()),
        "the runtime actor draw-list failure fixture installs without advancing time");
    const omega::app::GpuHostSnapshot draw_list_failure_gpu_before =
        OmegaAppTestAccess::GpuSnapshot(*app);
    const omega::runtime::RenderDrawList actor_draw_list_before_failure =
        OmegaAppTestAccess::DiagnosticActorDrawList(*app);
    auto draw_list_failure = app->RunWithCapture(1);
    Check(draw_list_failure &&
              draw_list_failure->completion() ==
                  RunCaptureCompletion::OperationalFailure &&
              draw_list_failure->failure() ==
                  std::optional<std::string_view>{
                      "diagnostic actor draw-list creation failed"} &&
              draw_list_failure->result().input_frames == 1U &&
              draw_list_failure->result().rendered_frames == 0 &&
              OmegaAppTestAccess::GpuSnapshot(*app) ==
                  draw_list_failure_gpu_before &&
              DrawListsEqual(OmegaAppTestAccess::DiagnosticActorDrawList(*app),
                  actor_draw_list_before_failure),
        "an invalid actor base is an operational pre-render failure with transactional draw-list state");

    OmegaAppTestAccess::ClearDiagnosticDraw(*app);
    Check(OmegaAppTestAccess::DestroyDiagnosticActor(*app) &&
              OmegaAppTestAccess::ArmNextRunElapsed(
                  *app, std::chrono::nanoseconds::zero()),
        "the missing diagnostic actor runtime fixture removes its positioned entity without advancing time");
    const omega::app::GpuHostSnapshot missing_actor_gpu_before =
        OmegaAppTestAccess::GpuSnapshot(*app);
    const omega::runtime::RenderDrawList actor_draw_list_before_missing_actor =
        OmegaAppTestAccess::DiagnosticActorDrawList(*app);
    auto missing_actor = app->RunWithCapture(1);
    Check(missing_actor &&
              missing_actor->completion() ==
                  RunCaptureCompletion::OperationalFailure &&
              missing_actor->failure() ==
                  std::optional<std::string_view>{
                      "diagnostic actor position is unavailable"} &&
              missing_actor->result().input_frames == 1U &&
              missing_actor->result().rendered_frames == 0 &&
              OmegaAppTestAccess::GpuSnapshot(*app) ==
                  missing_actor_gpu_before &&
              DrawListsEqual(OmegaAppTestAccess::DiagnosticActorDrawList(*app),
                  actor_draw_list_before_missing_actor),
        "a missing actor position is an operational pre-render failure with transactional draw-list state");

    if (failures == 0)
        std::cout << "omega_app_capture_smoke: passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    };
    return check_runtime();
}
