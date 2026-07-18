from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import check_native_dependencies as gate  # noqa: E402


class NativeDependencyGateTests(unittest.TestCase):
    def check_source(self, relative_path: str, source: str) -> tuple[int, list[str]]:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / relative_path
            path.parent.mkdir(parents=True)
            path.write_text(source, encoding="utf-8", newline="")
            return gate.check_tree(root)

    def test_platform_neutral_runtime_accepts_canonical_dependencies(self) -> None:
        checked, errors = self.check_source(
            "native/src/runtime/example.cpp",
            '#include "omega/runtime/example.h"\n#include "omega/content/game_data_service.h"\n',
        )
        self.assertEqual(checked, 1)
        self.assertEqual(errors, [])

    def test_runtime_cannot_include_retail_formats(self) -> None:
        _, errors = self.check_source(
            "native/include/omega/runtime/example.h",
            '#include "omega/retail/tdx_texture_storage_decoder.h"\n',
        )
        self.assertEqual(len(errors), 1)
        self.assertIn("omega_runtime includes forbidden dependency", errors[0])

    def test_simulation_cannot_reach_runtime_content_or_retail(self) -> None:
        for include in (
            "omega/runtime/frame_scheduler.h",
            "omega/content/game_data_service.h",
            "omega/retail/pop_level_manifest_decoder.h",
        ):
            with self.subTest(include=include):
                _, errors = self.check_source(
                    "native/src/simulation/example.cpp", f'#include "{include}"\n'
                )
                self.assertEqual(len(errors), 1)
                self.assertIn("omega_simulation includes forbidden dependency", errors[0])

    def test_platform_headers_are_rejected_from_neutral_modules(self) -> None:
        for include in (
            "SDL3/SDL.h",
            "windows.h",
            "d3d12.h",
            "DirectXMath.h",
            "vulkan/vulkan.h",
            "Metal/Metal.h",
        ):
            with self.subTest(include=include):
                _, errors = self.check_source(
                    "native/include/omega/simulation/example.h", f"#include <{include}>\n"
                )
                self.assertEqual(len(errors), 1)
                self.assertIn("includes platform header", errors[0])

    def test_sdl_leaf_accepts_sdl_but_not_retail(self) -> None:
        checked, errors = self.check_source(
            "native/apps/openomega/sdl_example.cpp",
            "#include <SDL3/SDL.h>\n#include \"omega/runtime/render_frame_packet.h\"\n",
        )
        self.assertEqual(checked, 1)
        self.assertEqual(errors, [])

        _, errors = self.check_source(
            "native/apps/openomega/sdl_example.cpp",
            '#include "omega/retail/vum_render_payload_descriptor.h"\n',
        )
        self.assertEqual(len(errors), 1)
        self.assertIn("omega_sdl_backend includes forbidden dependency", errors[0])

    def test_pcsx2_headers_are_rejected_from_shipping_modules(self) -> None:
        for relative_path in (
            "native/src/archive/example.cpp",
            "native/src/retail/example.cpp",
            "native/src/content/example.cpp",
            "native/src/runtime/example.cpp",
            "native/apps/openomega/sdl_example.cpp",
        ):
            with self.subTest(relative_path=relative_path):
                _, errors = self.check_source(relative_path, '#include "pcsx2/VMManager.h"\n')
                self.assertEqual(len(errors), 1)
                self.assertIn("includes forbidden PCSX2 header", errors[0])

    def test_content_can_use_retail_adapter_but_not_reverse_dependency(self) -> None:
        checked, errors = self.check_source(
            "native/src/content/example.cpp",
            '#include "omega/retail/pop_level_manifest_decoder.h"\n',
        )
        self.assertEqual(checked, 1)
        self.assertEqual(errors, [])

        _, errors = self.check_source(
            "native/src/retail/example.cpp",
            '#include "omega/content/game_data_service.h"\n',
        )
        self.assertEqual(len(errors), 1)
        self.assertIn("omega_retail_formats includes forbidden dependency", errors[0])

    def test_comments_and_tools_are_outside_the_shipping_gate(self) -> None:
        checked, errors = self.check_source(
            "native/apps/omega_tool/example.cpp",
            '// #include "omega/retail/example.h"\n#include "omega/retail/example.h"\n',
        )
        self.assertEqual(checked, 0)
        self.assertEqual(errors, [])

    def test_missing_native_root_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            checked, errors = gate.check_tree(Path(directory))
        self.assertEqual(checked, 0)
        self.assertEqual(len(errors), 1)
        self.assertIn("native source root is missing", errors[0])


if __name__ == "__main__":
    unittest.main()
