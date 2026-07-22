from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "native/include/omega/debug/subsystem_entry_break.h"
CMAKE = ROOT / "CMakeLists.txt"

MAPPINGS = (
    (
        "native/src/archive/hog_archive.cpp",
        "omega_core",
        "HogIndex::Open(const HogReadSource& source)",
        "const HogFileRange range",
    ),
    (
        "native/src/persistence/save_database.cpp",
        "omega_persistence",
        "SaveDatabaseErrorCodeName(const SaveDatabaseErrorCode code) noexcept",
        "switch (code)",
    ),
    (
        "native/src/profiles/profile_catalog.cpp",
        "omega_profiles",
        "ProfileId::Parse(const std::string_view text) noexcept",
        "if (text.size()",
    ),
    (
        "native/src/compat/ps2_memory_card_image.cpp",
        "omega_ps2_compat",
        "InspectPs2MemoryCardImage(const std::span<const std::byte> image)",
        "const auto layout",
    ),
    (
        "native/src/media/mpeg_program_stream_descriptor.cpp",
        "omega_media",
        "InspectMpegProgramStream(",
        "if (bytes.size()",
    ),
    (
        "native/src/simulation/simulation_world.cpp",
        "omega_simulation",
        "SimulationWorld::Create(",
        "if (config.fixed_step",
    ),
    (
        "native/src/gameplay/debug_locomotion.cpp",
        "omega_gameplay",
        "PlanDebugLocomotionStep(const DigitalMoveCommand command) noexcept",
        "constexpr std::int8_t kMinimumAxis",
    ),
    (
        "native/src/retail/vag_adpcm_decoder.cpp",
        "omega_retail_formats",
        "DecodeVagAdpcm(const std::span<const std::byte> bytes",
        "constexpr std::uint64_t hard_maximum_input_bytes",
    ),
    (
        "native/src/content/game_data_service.cpp",
        "omega_content",
        "GameDataService::Open(",
        "if (config.root.empty()",
    ),
    (
        "native/src/runtime/input_trace.cpp",
        "omega_runtime",
        "InputTraceRecorder::Create(",
        "if (!IsValidConfig(config))",
    ),
    (
        "native/apps/openomega_launcher/launcher_config.cpp",
        "omega_launcher_core",
        "LoadLauncherPreferences(\n    const std::filesystem::path& config_path)",
        "auto existing = LoadExistingStore(config_path)",
    ),
    (
        "native/apps/openomega_launcher/launcher_window.cpp",
        "omega_launcher_host",
        "int RunLauncher(HINSTANCE instance, const int show_command)",
        "LauncherWindow launcher(instance, DefaultConfigurationPath())",
    ),
    (
        "native/apps/openomega/front_end.cpp",
        "omega_app_core",
        "MakeFrontEndStartupModel(",
        "if (summaries.size()",
    ),
    (
        "native/apps/openomega/native_persistence.cpp",
        "omega_native_persistence",
        "NativePersistence::Bootstrap(\n    std::filesystem::path directory)",
        "return Bootstrap(std::move(directory)",
    ),
    (
        "native/apps/openomega/omega_app.cpp",
        "omega_app_host",
        "OmegaApp::Create(runtime::ConfigStore config",
        "return CreateWithTextureConfig(",
    ),
    (
        "native/apps/openomega/sdl_platform_service.cpp",
        "omega_sdl_backend",
        "SdlPlatformService::Create()",
        "auto impl = std::make_unique<Impl>()",
    ),
)


def read(relative_path: str | Path) -> str:
    path = relative_path if isinstance(relative_path, Path) else ROOT / relative_path
    return path.read_text(encoding="utf-8")


class SubsystemEntryBreakPolicyTests(unittest.TestCase):
    def test_helper_is_bounded_exact_single_read_and_single_shot(self) -> None:
        source = read(HEADER)
        self.assertIn("defined(_WIN32)", source)
        self.assertIn("defined(_MSC_VER)", source)
        self.assertIn("defined(_DEBUG)", source)
        self.assertEqual(source.count("::GetEnvironmentVariableW("), 1)
        self.assertEqual(
            source.count('L"OPENOMEGA_DEBUG_BREAK_SUBSYSTEM"'), 1
        )
        self.assertIn("kMaximumSubsystemNameBytes = 63U", source)
        self.assertIn("size == 0U || size >= capacity", source)
        self.assertIn("request.size != subsystem.size()", source)
        self.assertIn("character < L'!' || character > L'~'", source)
        self.assertRegex(
            source,
            r"inline std::atomic_flag g_subsystem_entry_break_consumed\s*=\s*"
            r"ATOMIC_FLAG_INIT;",
        )
        self.assertEqual(
            len(re.findall(r"inline std::atomic_flag\s+", source)), 1
        )

        debugger_guard = source.index("::IsDebuggerPresent()")
        one_shot = source.index("g_subsystem_entry_break_consumed.test_and_set")
        breakpoint = source.index("__debugbreak()")
        self.assertLess(debugger_guard, one_shot)
        self.assertLess(one_shot, breakpoint)

        disabled_branch = source.rsplit("#else", maxsplit=1)[1]
        self.assertIn(
            "#define OMEGA_DEBUG_BREAK_SUBSYSTEM_ENTRY(subsystem_literal) ((void)0)",
            disabled_branch,
        )
        self.assertNotIn("GetEnvironmentVariable", disabled_branch)
        self.assertNotIn("IsDebuggerPresent", disabled_branch)
        self.assertNotIn("atomic", disabled_branch)

    def test_every_concrete_library_has_one_genuine_first_entry_hook(self) -> None:
        expected_pairs = {(path, target) for path, target, _, _ in MAPPINGS}
        observed_pairs: set[tuple[str, str]] = set()
        call_pattern = re.compile(
            r'OMEGA_DEBUG_BREAK_SUBSYSTEM_ENTRY\("([a-z0-9_]+)"\);'
        )

        shipping_roots = (
            ROOT / "native/src",
            ROOT / "native/apps/openomega",
            ROOT / "native/apps/openomega_launcher",
        )
        for shipping_root in shipping_roots:
            for path in shipping_root.rglob("*.cpp"):
                source = read(path)
                for target in call_pattern.findall(source):
                    observed_pairs.add((path.relative_to(ROOT).as_posix(), target))

        self.assertEqual(observed_pairs, expected_pairs)
        for relative_path, target, signature, first_statement in MAPPINGS:
            with self.subTest(target=target):
                source = read(relative_path)
                self.assertEqual(
                    source.count('#include "omega/debug/subsystem_entry_break.h"'),
                    1,
                )
                self.assertEqual(
                    source.count(
                        f'OMEGA_DEBUG_BREAK_SUBSYSTEM_ENTRY("{target}");'
                    ),
                    1,
                )
                signature_offset = source.index(signature)
                hook_offset = source.index(
                    f'OMEGA_DEBUG_BREAK_SUBSYSTEM_ENTRY("{target}");'
                )
                statement_offset = source.index(first_statement, signature_offset)
                self.assertLess(signature_offset, hook_offset)
                self.assertLess(hook_offset, statement_offset)

    def test_cmake_enables_only_the_exact_debug_msvc_windows_target_set(self) -> None:
        source = read(CMAKE)
        target_block = re.search(
            r"set\(OMEGA_SUBSYSTEM_ENTRY_BREAK_TARGETS\s+(.*?)\n\)",
            source,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(target_block)
        targets = tuple(target_block.group(1).split())
        self.assertEqual(targets, tuple(mapping[1] for mapping in MAPPINGS))

        definition_block = source[
            source.index(
                'if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")',
                target_block.end(),
            ) : source.index("include(CTest)")
        ]
        self.assertIn(
            "$<$<CONFIG:Debug>:OPENOMEGA_ENABLE_SUBSYSTEM_ENTRY_BREAK=1>",
            definition_block,
        )
        self.assertNotIn("CONFIG:Release", definition_block)
        self.assertNotIn("PUBLIC", definition_block)
        self.assertNotIn("INTERFACE", definition_block)

    def test_simulation_host_is_immediate_and_registered(self) -> None:
        host = read(
            "native/tests/debug_subsystem_entry_break_simulation_host.cpp"
        )
        self.assertEqual(host.count("SimulationWorld::Create("), 1)
        main_offset = host.index("int main()")
        create_offset = host.index("SimulationWorld::Create(", main_offset)
        self.assertNotIn(";", host[main_offset:create_offset])

        cmake = read(CMAKE)
        self.assertIn(
            "add_executable(omega_simulation_entry_break_host\n"
            "        native/tests/debug_subsystem_entry_break_simulation_host.cpp",
            cmake,
        )
        self.assertIn(
            "target_link_libraries(omega_simulation_entry_break_host PRIVATE\n"
            "        omega_simulation",
            cmake,
        )
        self.assertIn(
            "add_test(NAME omega_simulation_entry_break_host",
            cmake,
        )

    def test_windows_contract_scenarios_cover_exactness_and_bounds(self) -> None:
        source = read(CMAKE)
        scenario_block = re.search(
            r"set\(OMEGA_SUBSYSTEM_ENTRY_BREAK_CONTRACT_SCENARIOS\s+"
            r"(.*?)\n\s*\)",
            source,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(scenario_block)
        self.assertEqual(
            tuple(scenario_block.group(1).split()),
            (
                "exact",
                "case",
                "suffix",
                "whitespace",
                "non_ascii",
                "maximum",
                "too_long",
                "empty",
                "absent",
            ),
        )
        contract = read("native/tests/subsystem_entry_break_contract_tests.cpp")
        self.assertIn("RequestedSubsystemFromEnvironment()", contract)
        self.assertIn("MatchesRequestedSubsystem(\"omega_simulation\")", contract)
        self.assertIn("SetEnvironmentVariableW", contract)


if __name__ == "__main__":
    unittest.main()
