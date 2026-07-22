from __future__ import annotations

import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import check_native_dependencies as gate  # noqa: E402


class NativeDependencyGateTests(unittest.TestCase):
    def check_sources(
        self, sources: dict[str, str | bytes]
    ) -> tuple[int, list[str]]:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for relative_path, source in sources.items():
                path = root / relative_path
                path.parent.mkdir(parents=True, exist_ok=True)
                if isinstance(source, bytes):
                    path.write_bytes(source)
                else:
                    path.write_text(source, encoding="utf-8", newline="")
            return gate.check_tree(root)

    def check_source(
        self, relative_path: str, source: str | bytes
    ) -> tuple[int, list[str]]:
        return self.check_sources({relative_path: source})

    def assert_rejected(
        self, relative_path: str, source: str | bytes, message: str
    ) -> None:
        checked, errors = self.check_source(relative_path, source)
        self.assertEqual(checked, 1)
        self.assertEqual(len(errors), 1, errors)
        self.assertIn(message, errors[0])

    def test_explicit_allowed_module_edges(self) -> None:
        cases = (
            ("native/src/archive/example.cpp", "omega/asset/decode.h"),
            ("native/src/profiles/example.cpp", "omega/persistence/save_database.h"),
            ("native/src/simulation/example.cpp", "omega/asset/decode.h"),
            ("native/src/media/example.cpp", "omega/asset/decode.h"),
            ("native/src/retail/example.cpp", "omega/archive/archive_reader.h"),
            ("native/src/retail/example.cpp", "omega/asset/audio_ir.h"),
            ("native/src/content/example.cpp", "omega/retail/pop_level_manifest_decoder.h"),
            ("native/src/runtime/example.cpp", "omega/content/game_data_service.h"),
            ("native/apps/openomega/sdl_example.cpp", "omega/runtime/render_frame_packet.h"),
            (
                "native/apps/openomega_launcher/launcher_config.cpp",
                "omega/runtime/config_service.h",
            ),
            (
                "native/apps/openomega_launcher/launcher_window.cpp",
                "omega/content/game_data_service.h",
            ),
            ("native/apps/openomega/example.cpp", "omega/simulation/simulation_world.h"),
            ("native/apps/openomega/example.cpp", "omega/profiles/profile_catalog.h"),
            ("native/apps/openomega/example.cpp", "omega/media/nv12_to_rgba8.h"),
        )
        for relative_path, include in cases:
            with self.subTest(relative_path=relative_path, include=include):
                checked, errors = self.check_source(
                    relative_path, f'#include "{include}"\n'
                )
                self.assertEqual(checked, 1)
                self.assertEqual(errors, [])

    def test_exact_debug_instrumentation_header_is_cross_cutting_and_fail_closed(
        self,
    ) -> None:
        shipping_sources = (
            "native/apps/openomega/front_end.cpp",
            "native/apps/openomega/native_persistence.cpp",
            "native/apps/openomega/omega_app.cpp",
            "native/apps/openomega/sdl_platform_service.cpp",
            "native/apps/openomega_launcher/launcher_config.cpp",
            "native/apps/openomega_launcher/launcher_window.cpp",
            "native/src/archive/hog_archive.cpp",
            "native/src/compat/ps2_memory_card_image.cpp",
            "native/src/content/game_data_service.cpp",
            "native/src/gameplay/debug_locomotion.cpp",
            "native/src/media/mpeg_program_stream_descriptor.cpp",
            "native/src/persistence/save_database.cpp",
            "native/src/profiles/profile_catalog.cpp",
            "native/src/retail/fnt_v3_decoder.cpp",
            "native/src/retail/retail_string_table_decoder.cpp",
            "native/src/retail/vag_adpcm_decoder.cpp",
            "native/src/runtime/input_trace.cpp",
            "native/src/simulation/simulation_world.cpp",
            "native/tests/subsystem_entry_break_contract_tests.cpp",
        )
        self.assertEqual(
            gate._DEBUG_SUBSYSTEM_ENTRY_BREAK_SOURCES,
            frozenset(shipping_sources),
        )
        for relative_path in shipping_sources:
            with self.subTest(relative_path=relative_path):
                checked, errors = self.check_source(
                    relative_path,
                    '#include "omega/debug/subsystem_entry_break.h"\n',
                )
                self.assertEqual(checked, 1)
                self.assertEqual(errors, [])

        for relative_path in (
            "native/src/archive/example.cpp",
            "native/include/omega/archive/hog_archive.h",
            "native/apps/openomega/example.cpp",
        ):
            with self.subTest(rejected_path=relative_path):
                self.assert_rejected(
                    relative_path,
                    '#include "omega/debug/subsystem_entry_break.h"\n',
                    "debug instrumentation header is restricted to exact private callsites",
                )

        self.assert_rejected(
            "native/src/archive/example.cpp",
            '#include "omega/debug/another_helper.h"\n',
            "unclassified project include",
        )

        checked, errors = self.check_source(
            "native/include/omega/debug/subsystem_entry_break.h",
            (
                "#pragma once\n"
                "#include <windows.h>\n"
                "#include <debugapi.h>\n"
                "#include <intrin.h>\n"
                "#include <processenv.h>\n"
            ),
        )
        self.assertEqual(checked, 1)
        self.assertEqual(errors, [])
        rule = gate.module_rule(
            Path("native/include/omega/debug/subsystem_entry_break.h")
        )
        self.assertIsNotNone(rule)
        self.assertEqual(rule.name, "omega_debug")
        self.assertEqual(rule.allowed_omega_modules, frozenset({"omega_debug"}))
        self.assertTrue(rule.platform_neutral)
        self.assert_rejected(
            "native/include/omega/debug/subsystem_entry_break.h",
            '#include "omega/runtime/frame_scheduler.h"\n',
            "omega_debug includes forbidden dependency",
        )
        self.assert_rejected(
            "native/include/omega/debug/subsystem_entry_break.h",
            "#include <shellapi.h>\n",
            "omega_debug includes unapproved external header",
        )

    def test_explicit_forbidden_module_edges(self) -> None:
        cases = (
            ("native/src/archive/example.cpp", "omega/runtime/frame_scheduler.h"),
            ("native/src/persistence/example.cpp", "omega/profiles/profile_catalog.h"),
            ("native/src/profiles/example.cpp", "omega/runtime/frame_scheduler.h"),
            ("native/src/simulation/example.cpp", "omega/runtime/frame_scheduler.h"),
            ("native/src/media/example.cpp", "omega/retail/vag_adpcm_decoder.h"),
            ("native/src/retail/example.cpp", "omega/content/game_data_service.h"),
            ("native/src/content/example.cpp", "omega/simulation/simulation_world.h"),
            ("native/src/runtime/example.cpp", "omega/simulation/simulation_world.h"),
            ("native/src/runtime/example.cpp", "omega/retail/pop_level_manifest_decoder.h"),
            ("native/src/runtime/example.cpp", "omega/retail/vag_adpcm_decoder.h"),
            ("native/src/runtime/example.cpp", "omega/retail/vpk_wrapper_envelope_decoder.h"),
            ("native/apps/openomega/sdl_example.cpp", "omega/simulation/simulation_world.h"),
            ("native/apps/openomega/example.cpp", "omega/retail/pop_level_manifest_decoder.h"),
        )
        for relative_path, include in cases:
            with self.subTest(relative_path=relative_path, include=include):
                self.assert_rejected(
                    relative_path,
                    f'#include "{include}"\n',
                    "includes forbidden dependency",
                )

    def test_runtime_cannot_reach_simulation(self) -> None:
        self.assert_rejected(
            "native/src/runtime/example.cpp",
            '#include "omega/simulation/simulation_world.h"\n',
            "omega_runtime includes forbidden dependency",
        )

    def test_media_header_and_source_allow_only_self_assets_and_stdlib(self) -> None:
        checked, errors = self.check_sources(
            {
                "native/include/omega/media/descriptor.h": (
                    "#pragma once\n"
                    "#include <vector>\n"
                    '#include "omega/asset/decode.h"\n'
                ),
                "native/src/media/descriptor.cpp": (
                    '#include "omega/media/descriptor.h"\n'
                    "#include <algorithm>\n"
                ),
                "native/include/omega/media/mpeg_video_elementary_stream.h": (
                    "#pragma once\n"
                    '#include "omega/media/mpeg_program_stream_descriptor.h"\n'
                    "#include <span>\n"
                ),
                "native/src/media/mpeg_video_elementary_stream.cpp": (
                    '#include "omega/media/mpeg_video_elementary_stream.h"\n'
                    "#include <array>\n"
                ),
            }
        )
        self.assertEqual(checked, 4)
        self.assertEqual(errors, [])

    def test_media_foundation_backend_has_an_exact_platform_header_allowlist(
        self,
    ) -> None:
        allowed_headers = (
            "windows.h",
            "mfapi.h",
            "mferror.h",
            "mfidl.h",
            "mftransform.h",
            "wrl/client.h",
        )
        source = "".join(f"#include <{header}>\n" for header in allowed_headers)
        checked, errors = self.check_source(
            "native/src/media/media_foundation_h262_decoder.cpp", source
        )
        self.assertEqual(checked, 1)
        self.assertEqual(errors, [])

        self.assert_rejected(
            "native/src/media/example.cpp",
            "#include <windows.h>\n",
            "omega_media includes unapproved external header",
        )
        self.assert_rejected(
            "native/src/media/media_foundation_h262_decoder.cpp",
            "#include <shlwapi.h>\n",
            "omega_media includes unapproved external header",
        )
        self.assert_rejected(
            "native/src/media/media_foundation_h262_decoder.cpp",
            '#include "windows.h"\n',
            "omega_media includes unresolved local header",
        )

    def test_media_classification_remains_fail_closed(self) -> None:
        self.assert_rejected(
            "native/src/media_extra/example.cpp",
            "constexpr int example = 0;\n",
            "unclassified shipping native source path",
        )
        self.assert_rejected(
            "native/src/media/example.cpp",
            '#include "omega/media_extra/example.h"\n',
            "unclassified project include",
        )

    def test_gameplay_header_and_source_allow_only_self_simulation_and_stdlib(
        self,
    ) -> None:
        checked, errors = self.check_sources(
            {
                "native/include/omega/gameplay/debug_locomotion.h": (
                    "#pragma once\n"
                    "#include <cstdint>\n"
                    '#include "omega/simulation/simulation_world.h"\n'
                ),
                "native/src/gameplay/debug_locomotion.cpp": (
                    '#include "omega/gameplay/debug_locomotion.h"\n'
                    "#include <vector>\n"
                ),
            }
        )
        self.assertEqual(checked, 2)
        self.assertEqual(errors, [])
        for relative_path in (
            "native/include/omega/gameplay/debug_locomotion.h",
            "native/src/gameplay/debug_locomotion.cpp",
        ):
            with self.subTest(relative_path=relative_path):
                rule = gate.module_rule(Path(relative_path))
                self.assertIsNotNone(rule)
                self.assertEqual(rule.name, "omega_gameplay")

    def test_gameplay_rejects_runtime_sdl_and_app_dependencies(self) -> None:
        cases = (
            (
                '#include "omega/runtime/frame_scheduler.h"\n',
                "omega_gameplay includes forbidden dependency",
            ),
            ("#include <SDL3/SDL.h>\n", "includes unapproved external header"),
            (
                '#include "sdl_platform_service.h"\n',
                "includes unresolved local header",
            ),
            ('#include "omega_app.h"\n', "includes unresolved local header"),
        )
        for source, message in cases:
            with self.subTest(source=source):
                self.assert_rejected(
                    "native/src/gameplay/example.cpp", source, message
                )

    def test_lower_layers_cannot_depend_upward_on_gameplay(self) -> None:
        cases = (
            "native/src/simulation/example.cpp",
            "native/src/runtime/example.cpp",
            "native/include/omega/asset/example.h",
            "native/src/retail/example.cpp",
            "native/src/archive/example.cpp",
            "native/src/content/example.cpp",
            "native/apps/openomega/sdl_example.cpp",
        )
        for relative_path in cases:
            with self.subTest(relative_path=relative_path):
                self.assert_rejected(
                    relative_path,
                    '#include "omega/gameplay/debug_locomotion.h"\n',
                    "includes forbidden dependency",
                )

    def test_openomega_app_core_and_host_may_depend_on_gameplay(self) -> None:
        for relative_path in (
            "native/apps/openomega/run_replay_session.cpp",
            "native/apps/openomega/omega_app.cpp",
        ):
            with self.subTest(relative_path=relative_path):
                checked, errors = self.check_source(
                    relative_path,
                    '#include "omega/gameplay/debug_locomotion.h"\n',
                )
                self.assertEqual(checked, 1)
                self.assertEqual(errors, [])

    def test_openomega_movie_presentation_may_depend_on_media(self) -> None:
        for relative_path in (
            "native/apps/openomega/opening_movie_player.h",
            "native/apps/openomega/opening_movie_player.cpp",
        ):
            with self.subTest(relative_path=relative_path):
                checked, errors = self.check_source(
                    relative_path,
                    '#include "omega/media/nv12_to_rgba8.h"\n',
                )
                self.assertEqual(checked, 1)
                self.assertEqual(errors, [])

    def test_gameplay_classification_remains_fail_closed(self) -> None:
        self.assert_rejected(
            "native/src/gameplay_extra/example.cpp",
            "constexpr int example = 0;\n",
            "unclassified shipping native source path",
        )
        self.assert_rejected(
            "native/src/gameplay/example.cpp",
            '#include "omega/gameplay_extra/example.h"\n',
            "unclassified project include",
        )

    def test_persistence_header_and_source_form_standalone_layer(self) -> None:
        checked, errors = self.check_sources(
            {
                "native/include/omega/persistence/types.h": (
                    "#pragma once\n"
                    "#include <cstdint>\n"
                ),
                "native/include/omega/persistence/store.h": (
                    "#pragma once\n"
                    '#include "omega/persistence/types.h"\n'
                    "#include <filesystem>\n"
                ),
                "native/src/persistence/detail.h": "#pragma once\n",
                "native/src/persistence/store.cpp": (
                    '#include "omega/persistence/store.h"\n'
                    '#include "detail.h"\n'
                    "#include <Windows.h>\n"
                ),
            }
        )
        self.assertEqual(checked, 4)
        self.assertEqual(errors, [])

        header_rule = gate.module_rule(
            Path("native/include/omega/persistence/store.h")
        )
        source_rule = gate.module_rule(Path("native/src/persistence/store.cpp"))
        self.assertIsNotNone(header_rule)
        self.assertIsNotNone(source_rule)
        self.assertEqual(header_rule.name, "omega_persistence")
        self.assertEqual(source_rule.name, "omega_persistence")
        self.assertEqual(
            header_rule.allowed_omega_modules, frozenset({"omega_persistence"})
        )
        self.assertEqual(
            source_rule.allowed_omega_modules, frozenset({"omega_persistence"})
        )
        self.assertTrue(header_rule.platform_neutral)
        self.assertFalse(source_rule.platform_neutral)

    def test_persistence_rejects_cross_layer_and_pcsx2_dependencies(self) -> None:
        cases = (
            (
                "native/src/persistence/example.cpp",
                '#include "omega/runtime/frame_scheduler.h"\n',
                "omega_persistence includes forbidden dependency",
            ),
            (
                "native/src/runtime/example.cpp",
                '#include "omega/persistence/store.h"\n',
                "omega_runtime includes forbidden dependency",
            ),
            (
                "native/include/omega/persistence/example.h",
                '#include "PCSX2/VMManager.h"\n',
                "omega_persistence includes forbidden PCSX2 header",
            ),
            (
                "native/src/persistence/example.cpp",
                '#include "pcsx2/GS/GS.h"\n',
                "omega_persistence includes forbidden PCSX2 header",
            ),
        )
        for relative_path, source, message in cases:
            with self.subTest(relative_path=relative_path, source=source):
                self.assert_rejected(relative_path, source, message)

    def test_persistence_classification_remains_fail_closed(self) -> None:
        self.assert_rejected(
            "native/src/persistence_extra/example.cpp",
            "constexpr int example = 0;\n",
            "unclassified shipping native source path",
        )
        self.assert_rejected(
            "native/src/persistence/example.cpp",
            '#include "omega/persistence_extra/example.h"\n',
            "unclassified project include",
        )

    def test_profiles_allow_only_self_and_persistence(self) -> None:
        checked, errors = self.check_sources(
            {
                "native/include/omega/profiles/types.h": (
                    "#pragma once\n"
                    "#include <cstdint>\n"
                ),
                "native/include/omega/profiles/catalog.h": (
                    "#pragma once\n"
                    '#include "omega/profiles/types.h"\n'
                    '#include "omega/persistence/save_database.h"\n'
                ),
                "native/src/profiles/catalog.cpp": (
                    '#include "omega/profiles/catalog.h"\n'
                    '#include "omega/persistence/save_database.h"\n'
                ),
            }
        )
        self.assertEqual(checked, 3)
        self.assertEqual(errors, [])

        header_rule = gate.module_rule(
            Path("native/include/omega/profiles/catalog.h")
        )
        source_rule = gate.module_rule(Path("native/src/profiles/catalog.cpp"))
        self.assertIsNotNone(header_rule)
        self.assertIsNotNone(source_rule)
        self.assertEqual(header_rule.name, "omega_profiles")
        self.assertEqual(source_rule.name, "omega_profiles")
        self.assertEqual(
            header_rule.allowed_omega_modules,
            frozenset({"omega_profiles", "omega_persistence"}),
        )
        self.assertEqual(
            source_rule.allowed_omega_modules,
            frozenset({"omega_profiles", "omega_persistence"}),
        )
        self.assertTrue(header_rule.platform_neutral)
        self.assertTrue(source_rule.platform_neutral)

    def test_ps2_compat_is_dependency_free_and_platform_neutral(self) -> None:
        checked, errors = self.check_sources(
            {
                "native/include/omega/compat/card.h": (
                    "#pragma once\n"
                    "#include <expected>\n"
                    "#include <span>\n"
                    "#include <vector>\n"
                ),
                "native/src/compat/card.cpp": (
                    '#include "omega/compat/card.h"\n'
                    "#include <algorithm>\n"
                ),
            }
        )
        self.assertEqual(checked, 2)
        self.assertEqual(errors, [])

        header_rule = gate.module_rule(Path("native/include/omega/compat/card.h"))
        source_rule = gate.module_rule(Path("native/src/compat/card.cpp"))
        self.assertIsNotNone(header_rule)
        self.assertIsNotNone(source_rule)
        self.assertEqual(header_rule.name, "omega_ps2_compat")
        self.assertEqual(source_rule.name, "omega_ps2_compat")
        self.assertEqual(
            header_rule.allowed_omega_modules, frozenset({"omega_ps2_compat"})
        )
        self.assertEqual(
            source_rule.allowed_omega_modules, frozenset({"omega_ps2_compat"})
        )
        self.assertTrue(header_rule.platform_neutral)
        self.assertTrue(source_rule.platform_neutral)

    def test_profiles_reject_cross_layer_and_pcsx2_dependencies(self) -> None:
        cases = (
            (
                "native/src/profiles/example.cpp",
                '#include "omega/runtime/frame_scheduler.h"\n',
                "omega_profiles includes forbidden dependency",
            ),
            (
                "native/include/omega/profiles/example.h",
                '#include "pcsx2/MemoryCardFile.h"\n',
                "omega_profiles includes forbidden PCSX2 header",
            ),
        )
        for relative_path, source, message in cases:
            with self.subTest(relative_path=relative_path, source=source):
                self.assert_rejected(relative_path, source, message)

    def test_ps2_compat_rejects_cross_layer_platform_and_pcsx2_dependencies(
        self,
    ) -> None:
        cases = (
            (
                "native/src/compat/example.cpp",
                '#include "omega/persistence/save_database.h"\n',
                "omega_ps2_compat includes forbidden dependency",
            ),
            (
                "native/src/compat/example.cpp",
                "#include <Windows.h>\n",
                "omega_ps2_compat includes unapproved external header",
            ),
            (
                "native/include/omega/compat/example.h",
                '#include "pcsx2/SIO/Memcard/MemoryCardFile.h"\n',
                "omega_ps2_compat includes forbidden PCSX2 header",
            ),
        )
        for relative_path, source, message in cases:
            with self.subTest(relative_path=relative_path, source=source):
                self.assert_rejected(relative_path, source, message)

    def test_profiles_classification_remains_fail_closed(self) -> None:
        self.assert_rejected(
            "native/src/profiles_extra/example.cpp",
            "constexpr int example = 0;\n",
            "unclassified shipping native source path",
        )
        self.assert_rejected(
            "native/src/profiles/example.cpp",
            '#include "omega/profiles_extra/example.h"\n',
            "unclassified project include",
        )

    def test_ps2_compat_classification_remains_fail_closed(self) -> None:
        self.assert_rejected(
            "native/src/compatibility/example.cpp",
            "constexpr int example = 0;\n",
            "unclassified shipping native source path",
        )
        self.assert_rejected(
            "native/src/compat/example.cpp",
            '#include "omega/compatibility/example.h"\n',
            "unclassified project include",
        )

    def test_platform_neutral_modules_accept_standard_headers(self) -> None:
        source = "".join(
            f"#include <{header}>\n"
            for header in ("algorithm", "cstdint", "expected", "filesystem", "vector")
        )
        checked, errors = self.check_source(
            "native/include/omega/simulation/example.h", source
        )
        self.assertEqual(checked, 1)
        self.assertEqual(errors, [])

    def test_platform_neutral_modules_reject_unapproved_external_headers(self) -> None:
        headers = (
            "SDL3/SDL.h",
            "windows.h",
            "winsock2.h",
            "d3d12.h",
            "DirectXMath.h",
            "vulkan/vulkan.h",
            "Metal/Metal.h",
            "X11/Xlib.h",
            "GL/gl.h",
            "unistd.h",
            "pthread.h",
        )
        source = "".join(f"#include <{header}>\n" for header in headers)
        checked, errors = self.check_source(
            "native/include/omega/simulation/example.h", source
        )
        self.assertEqual(checked, 1)
        self.assertEqual(len(errors), len(headers), errors)
        self.assertTrue(
            all("includes unapproved external header" in error for error in errors)
        )

    def test_bom_splices_comments_and_digraphs_cannot_hide_pcsx2(self) -> None:
        cases: tuple[str | bytes, ...] = (
            b'\xef\xbb\xbf#include "pcsx2/VMManager.h"\n',
            '#inc\\\nlude "pcsx2/VMManager.h"\n',
            '#include "pcsx2/\\\nVMManager.h"\n',
            '#/**/include "pcsx2/VMManager.h"\n',
            '%: include "pcsx2/VMManager.h"\n',
        )
        for source in cases:
            with self.subTest(source=source):
                self.assert_rejected(
                    "native/src/runtime/example.cpp",
                    source,
                    "includes forbidden PCSX2 header",
                )

    def test_comments_strings_and_raw_strings_do_not_create_false_includes(self) -> None:
        source = r'''
// #include "pcsx2/VMManager.h"
/*
#include "pcsx2/VMManager.h"
import forbidden.module;
*/
constexpr auto ordinary = "#include \"pcsx2/VMManager.h\"";
constexpr auto raw = R"tag(
#include "pcsx2/VMManager.h"
export module forbidden.module;
)tag";
constexpr int separated = 1'048'576;
'''
        checked, errors = self.check_source(
            "native/src/runtime/example.cpp", source
        )
        self.assertEqual(checked, 1)
        self.assertEqual(errors, [])

    def test_nonliteral_and_malformed_include_operands_fail_closed(self) -> None:
        cases = (
            ('#define HEADER "vector"\n#include HEADER\n', "non-literal include"),
            ('#include MAKE_HEADER("vector")\n', "non-literal include"),
            ('#include_next HEADER\n', "non-literal include"),
            ('#include <vector> trailing\n', "tokens after a literal include"),
            ('#include "unterminated\n', "unterminated quoted literal"),
        )
        for source, message in cases:
            with self.subTest(source=source):
                self.assert_rejected(
                    "native/src/runtime/example.cpp", source, message
                )

    def test_include_paths_must_be_canonical_and_relative(self) -> None:
        cases = (
            ("omega/runtime/../retail/example.h", "dot include-path segments"),
            ("../../include/omega/retail/example.h", "dot include-path segments"),
            ("Omega/Retail/example.h", "canonical lowercase"),
            (r"omega\retail\example.h", "backslashes"),
            ("/omega/runtime/example.h", "absolute include paths"),
            ("C:/omega/runtime/example.h", "absolute include paths"),
            ("omega//runtime/example.h", "empty or dot include-path segments"),
            ("omega/runtime/example.h ", "non-canonical include path"),
            ("omega/unknown/example.h", "unclassified project include"),
            ("carrier.h:payload", "Windows-unsafe include-path characters"),
            ("carrier.h.", "Windows-aliased trailing dots or spaces"),
            ("directory./carrier.h", "Windows-aliased trailing dots or spaces"),
            ("CON", "Windows reserved device include paths"),
            ("nul.txt", "Windows reserved device include paths"),
            ("COM1.hpp", "Windows reserved device include paths"),
            ("LPT².log", "Windows reserved device include paths"),
            ("question?.h", "Windows-unsafe include-path characters"),
            ("pipe|name.h", "Windows-unsafe include-path characters"),
        )
        for include, message in cases:
            with self.subTest(include=include):
                self.assert_rejected(
                    "native/src/runtime/example.cpp",
                    f'#include "{include}"\n',
                    message,
                )

    def test_pcsx2_ban_is_global_and_casefolded(self) -> None:
        cases = (
            ("native/src/archive/example.cpp", "PCSX2/VMManager.h"),
            ("native/tests/example.cpp", "PcSx2/VMManager.h"),
            ("native/apps/omega_tool/example.cpp", r"pcsx2\VMManager.h"),
        )
        for relative_path, include in cases:
            with self.subTest(relative_path=relative_path, include=include):
                self.assert_rejected(
                    relative_path,
                    f'#include "{include}"\n',
                    "includes forbidden PCSX2 header",
                )

    def test_tests_and_tools_are_scanned_but_do_not_receive_shipping_edges(self) -> None:
        checked, errors = self.check_sources(
            {
                "native/tests/example.cpp": '#include "omega/retail/example.h"\n',
                "native/apps/omega_tool/example.cpp": '#include "omega/simulation/example.h"\n',
            }
        )
        self.assertEqual(checked, 2)
        self.assertEqual(errors, [])

    def test_source_fragments_are_scanned(self) -> None:
        for suffix in (".inc", ".inl", ".ipp", ".tpp", ".def"):
            with self.subTest(suffix=suffix):
                self.assert_rejected(
                    f"native/src/runtime/example{suffix}",
                    '#include "pcsx2/VMManager.h"\n',
                    "includes forbidden PCSX2 header",
                )

    def test_cpp_module_source_suffixes_are_rejected(self) -> None:
        for suffix in (".ccm", ".cppm", ".cxxm", ".ixx", ".mpp", ".mxx"):
            with self.subTest(suffix=suffix):
                self.assert_rejected(
                    f"native/src/runtime/example{suffix}",
                    "export module example;\n",
                    "C++ module source suffix is unsupported",
                )

    def test_cpp_module_and_import_syntax_is_rejected(self) -> None:
        cases = (
            ("import example;\n", "C++ module imports"),
            ("export import example;\n", "C++ module imports"),
            ("import <vector>;\n", "C++ module imports"),
            ("module;\n", "C++ module declarations"),
            ("export module example;\n", "C++ module declarations"),
            ('#import "example.h"\n', "preprocessor imports"),
        )
        for source, message in cases:
            with self.subTest(source=source):
                self.assert_rejected(
                    "native/src/runtime/example.cpp", source, message
                )

    def test_newlines_and_multiline_comments_cannot_split_module_tokens(self) -> None:
        cases = (
            ("import\nexample;\n", "C++ module imports"),
            ("import/* split\n */example;\n", "C++ module imports"),
            ("import\n:partition;\n", "C++ module imports"),
            ("module\nexample;\n", "C++ module declarations"),
            ("module\n;\n", "C++ module declarations"),
            ("export\nmodule\nexample;\n", "C++ module declarations"),
        )
        for source, message in cases:
            with self.subTest(source=source):
                self.assert_rejected(
                    "native/src/runtime/example.cpp", source, message
                )

    def test_unmapped_shipping_source_paths_fail_closed(self) -> None:
        for relative_path in (
            "native/src/new_module/example.cpp",
            "native/include/omega/new_module/example.h",
            "native/apps/new_app/example.cpp",
        ):
            with self.subTest(relative_path=relative_path):
                self.assert_rejected(
                    relative_path,
                    "constexpr int example = 0;\n",
                    "unclassified shipping native source path",
                )

    def test_unknown_file_types_in_shipping_modules_fail_closed(self) -> None:
        checked, errors = self.check_source(
            "native/src/runtime/example.generated", "opaque\n"
        )
        self.assertEqual(checked, 0)
        self.assertEqual(len(errors), 1, errors)
        self.assertIn("unsupported file type", errors[0])

    def test_unknown_file_types_in_unclassified_shipping_paths_fail_closed(self) -> None:
        checked, errors = self.check_sources(
            {
                "native/src/new_module/example.mm": "opaque\n",
                "native/include/omega/new_module/example.generated": "opaque\n",
                "native/apps/new_app/example.shader": "opaque\n",
            }
        )
        self.assertEqual(checked, 0)
        self.assertEqual(len(errors), 3, errors)
        self.assertTrue(
            all("unclassified shipping native path" in error for error in errors)
        )

    def test_unknown_nonshipping_test_and_tool_files_remain_out_of_scope(self) -> None:
        checked, errors = self.check_sources(
            {
                "native/tests/fixture.data": "opaque\n",
                "native/apps/omega_tool/fixture.data": "opaque\n",
            }
        )
        self.assertEqual(checked, 0)
        self.assertEqual(errors, [])

    def test_pop_terrain_index_uses_its_cmake_core_ownership(self) -> None:
        relative = Path("native/include/omega/asset/pop_terrain_index.h")
        rule = gate.module_rule(relative)
        self.assertIsNotNone(rule)
        self.assertEqual(rule.name, "omega_core")
        self.assertEqual(
            gate._project_header_module("omega/asset/pop_terrain_index.h"),
            "omega_core",
        )
        checked, errors = self.check_source(
            relative.as_posix(), '#include "omega/archive/archive_reader.h"\n'
        )
        self.assertEqual(checked, 1)
        self.assertEqual(errors, [])
        self.assert_rejected(
            "native/include/omega/asset/example.h",
            '#include "omega/archive/archive_reader.h"\n',
            "omega_assets includes forbidden dependency",
        )

    def test_unresolved_local_shipping_headers_fail_closed(self) -> None:
        self.assert_rejected(
            "native/src/runtime/example.cpp",
            '#include "missing_local_header.h"\n',
            "includes unresolved local header",
        )

    def test_resolved_local_headers_follow_module_edges(self) -> None:
        checked, errors = self.check_sources(
            {
                "native/src/runtime/example.cpp": '#include "example_internal.h"\n',
                "native/src/runtime/example_internal.h": "#pragma once\n",
            }
        )
        self.assertEqual(checked, 2)
        self.assertEqual(errors, [])

    def test_local_headers_require_exact_on_disk_spelling(self) -> None:
        checked, errors = self.check_sources(
            {
                "native/src/runtime/example.cpp": '#include "Example_Internal.h"\n',
                "native/src/runtime/example_internal.h": "#pragma once\n",
            }
        )
        self.assertEqual(checked, 2)
        self.assertEqual(len(errors), 1, errors)
        self.assertIn("exact on-disk spelling", errors[0])

    def test_local_windows_filesystem_aliases_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "native/src/runtime/example.cpp"
            canonical = source.with_name("example_internal_long_header.h")
            alias = source.with_name("EXAMPL~1.H")
            source.parent.mkdir(parents=True)
            source.write_text("constexpr int example = 0;\n", encoding="utf-8")
            canonical.write_text("#pragma once\n", encoding="utf-8")
            original_stat = os.stat

            def resolve_short_alias(path: os.PathLike[str] | str, *args, **kwargs):
                target = canonical if Path(path) == alias else path
                return original_stat(target, *args, **kwargs)

            with mock.patch.object(
                gate.os, "stat", side_effect=resolve_short_alias
            ):
                resolved, error = gate._resolve_local_include(
                    root, source, alias.name
                )

        self.assertIsNone(resolved)
        self.assertIn("exact on-disk spelling", error)

    @unittest.skipUnless(os.name == "nt", "NTFS alternate streams are Windows-only")
    def test_ntfs_alternate_stream_cannot_bypass_the_pcsx2_ban(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "native/src/runtime/example.cpp"
            carrier = source.with_name("carrier.h")
            source.parent.mkdir(parents=True)
            source.write_text(
                '#include "carrier.h:payload"\n', encoding="utf-8", newline=""
            )
            carrier.write_text("#pragma once\n", encoding="utf-8", newline="")
            Path(f"{carrier}:payload").write_text(
                '#include "PCSX2/VMManager.h"\n', encoding="utf-8", newline=""
            )
            checked, errors = gate.check_tree(root)

        self.assertEqual(checked, 2)
        self.assertEqual(len(errors), 1, errors)
        self.assertIn("Windows-unsafe include-path characters", errors[0])

    def test_link_like_source_entries_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "native/src/runtime/example.cpp"
            path.parent.mkdir(parents=True)
            path.write_text("constexpr int example = 0;\n", encoding="utf-8")
            original = gate._path_is_link_like

            def mark_source_as_link(candidate: Path, value: os.stat_result) -> bool:
                return candidate == path or original(candidate, value)

            with mock.patch.object(
                gate, "_path_is_link_like", side_effect=mark_source_as_link
            ):
                checked, errors = gate.check_tree(root)
        self.assertEqual(checked, 0)
        self.assertEqual(len(errors), 1, errors)
        self.assertIn("unsafe or special native file entry", errors[0])

    def test_reparse_attribute_is_treated_as_link_like(self) -> None:
        value = mock.Mock(
            st_mode=stat.S_IFREG | 0o644,
            st_file_attributes=gate._REPARSE_POINT_ATTRIBUTE,
        )
        self.assertTrue(gate._stat_is_reparse(value))
        self.assertTrue(gate._path_is_link_like(Path("not-a-real-source.cpp"), value))

    def test_special_source_entries_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "native/src/runtime/example.cpp"
            path.parent.mkdir(parents=True)
            path.write_text("constexpr int example = 0;\n", encoding="utf-8")
            original = gate._safe_stat

            def mark_source_as_fifo(candidate: Path) -> tuple[os.stat_result | None, str | None]:
                value, error = original(candidate)
                if candidate == path and value is not None:
                    fields = list(value)
                    fields[0] = stat.S_IFIFO | 0o644
                    return os.stat_result(fields), error
                return value, error

            with mock.patch.object(gate, "_safe_stat", side_effect=mark_source_as_fifo):
                checked, errors = gate.check_tree(root)
        self.assertEqual(checked, 0)
        self.assertEqual(len(errors), 1, errors)
        self.assertIn("unsafe or special native file entry", errors[0])

    def test_changed_source_snapshot_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "example.cpp"
            path.write_text("constexpr int example = 0;\n", encoding="utf-8")
            original_fstat = os.fstat

            def changed_size(file_descriptor: int) -> os.stat_result:
                value = original_fstat(file_descriptor)
                fields = list(value)
                fields[6] = value.st_size + 1
                return os.stat_result(fields)

            with mock.patch.object(gate.os, "fstat", side_effect=changed_size):
                source, error = gate._read_source_stably(path)
        self.assertIsNone(source)
        self.assertEqual(error, "native source changed before opening")

    def test_missing_native_root_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            checked, errors = gate.check_tree(Path(directory))
        self.assertEqual(checked, 0)
        self.assertEqual(len(errors), 1)
        self.assertIn("native source root is missing", errors[0])


if __name__ == "__main__":
    unittest.main()
