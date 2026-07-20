from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class WindowsCiTimeoutContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.cmake_lists = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.package_contract = (
            ROOT / "cmake" / "openomega_windows_package_contract.cmake"
        ).read_text(encoding="utf-8")
        cls.native_ci = (
            ROOT / ".github" / "workflows" / "native-ci.yml"
        ).read_text(encoding="utf-8")

    def test_windows_timeout_ceilings_remain_bounded_and_hierarchical(self) -> None:
        sdl_input_timeout = self._ctest_timeout("omega_sdl_input_tests")
        package_contract_timeout = self._ctest_timeout(
            "openomega_windows_portable_package_contract"
        )
        self.assertEqual(sdl_input_timeout, 15)

        timeout_constants = self._package_timeout_constants()
        self.assertEqual(
            timeout_constants,
            {
                "openomega_cpack_negative_timeout_seconds": 60,
                "openomega_install_negative_timeout_seconds": 30,
                "openomega_release_install_timeout_seconds": 30,
                "openomega_dependency_dump_timeout_seconds": 20,
                "openomega_reparse_inspection_timeout_seconds": 10,
                "openomega_positive_package_timeout_seconds": 60,
                "openomega_archive_list_timeout_seconds": 20,
                "openomega_launcher_timeout_seconds": 10,
            },
        )
        timeout_uses = re.findall(
            r"\bTIMEOUT\s+\$\{(openomega_[a-z_]+_timeout_seconds)\}",
            self.package_contract,
        )
        self.assertCountEqual(timeout_uses, timeout_constants)

        multiplicities = {
            "openomega_cpack_negative_timeout_seconds": self._call_count(
                "run_cpack_failure"
            ),
            "openomega_install_negative_timeout_seconds": self._call_count(
                "run_install_failure"
            ),
            "openomega_release_install_timeout_seconds": 1,
            "openomega_dependency_dump_timeout_seconds": self._call_count(
                "validate_dependencies"
            ),
            "openomega_reparse_inspection_timeout_seconds": self._call_count(
                "validate_payload_tree"
            ),
            "openomega_positive_package_timeout_seconds": 1,
            "openomega_archive_list_timeout_seconds": 1,
            "openomega_launcher_timeout_seconds": self._call_count(
                "run_launcher_case"
            ),
        }
        self.assertEqual(tuple(multiplicities.values()), (4, 2, 1, 2, 2, 1, 1, 3))
        sequential_subprocess_maximum = sum(
            timeout_constants[name] * count
            for name, count in multiplicities.items()
        )
        self.assertEqual(sequential_subprocess_maximum, 500)
        self.assertEqual(package_contract_timeout, 600)

        workflow_timeout_seconds = (
            self._workflow_job_timeout_minutes("windows-portable-package") * 60
        )
        self.assertEqual(workflow_timeout_seconds, 1800)
        self.assertLess(sequential_subprocess_maximum, package_contract_timeout)
        self.assertLess(
            package_contract_timeout,
            workflow_timeout_seconds,
        )

    def test_cpack_timeout_precedes_contract_gate_diagnostic(self) -> None:
        self._assert_timeout_precedes_gate_diagnostic(
            self._function_body("run_cpack_failure"),
            "openomega_cpack_negative_timeout_seconds",
            "CPack subprocess timed out after",
        )

    def test_install_timeout_precedes_release_gate_diagnostic(self) -> None:
        self._assert_timeout_precedes_gate_diagnostic(
            self._function_body("run_install_failure"),
            "openomega_install_negative_timeout_seconds",
            "CMake install subprocess timed out after",
        )

    def _assert_timeout_precedes_gate_diagnostic(
        self, body: str, timeout_constant: str, timeout_message: str
    ) -> None:
        execute_position = body.index("execute_process(")
        timeout_position = body.index('if(result MATCHES "[Tt]imeout")')
        gate_position = body.index("if(NOT combined_output MATCHES")
        self.assertLess(execute_position, timeout_position)
        self.assertLess(timeout_position, gate_position)
        self.assertIn(f"TIMEOUT ${{{timeout_constant}}}", body)
        self.assertIn(timeout_message, body)
        self.assertIn("result=[${result}]", body[timeout_position:gate_position])
        self.assertIn("result=[${result}]", body[gate_position:])

    def _function_body(self, function_name: str) -> str:
        function_match = re.search(
            rf"function\({re.escape(function_name)}\b(?P<body>.*?)\nendfunction\(\)",
            self.package_contract,
            re.DOTALL,
        )
        self.assertIsNotNone(function_match, function_name)
        return function_match.group("body")

    def _package_timeout_constants(self) -> dict[str, int]:
        return {
            name: int(value)
            for name, value in re.findall(
                r"set\((openomega_[a-z_]+_timeout_seconds)\s+(\d+)\)",
                self.package_contract,
            )
        }

    def _call_count(self, function_name: str) -> int:
        return len(
            re.findall(
                rf"(?m)^{re.escape(function_name)}\(", self.package_contract
            )
        )

    def _ctest_timeout(self, test_name: str) -> int:
        properties_match = re.search(
            rf"set_tests_properties\(\s*{re.escape(test_name)}\s+PROPERTIES"
            r"(?P<properties>.*?)\)",
            self.cmake_lists,
            re.DOTALL,
        )
        self.assertIsNotNone(properties_match, test_name)
        timeout_match = re.search(
            r"\bTIMEOUT\s+(\d+)\b", properties_match.group("properties")
        )
        self.assertIsNotNone(timeout_match, test_name)
        return int(timeout_match.group(1))

    def _workflow_job_timeout_minutes(self, job_name: str) -> int:
        job_match = re.search(
            rf"(?ms)^  {re.escape(job_name)}:\n(?P<body>.*?)(?=^  [a-z0-9-]+:\n|\Z)",
            self.native_ci,
        )
        self.assertIsNotNone(job_match, job_name)
        timeout_match = re.search(
            r"(?m)^    timeout-minutes:\s*(\d+)\s*$",
            job_match.group("body"),
        )
        self.assertIsNotNone(timeout_match, job_name)
        return int(timeout_match.group(1))


if __name__ == "__main__":
    unittest.main()
