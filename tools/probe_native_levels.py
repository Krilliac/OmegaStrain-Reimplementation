#!/usr/bin/env python3
"""Run OpenOmega's headless native startup against every discovered level."""

from __future__ import annotations

import argparse
import ctypes
import json
import math
import os
import re
import signal
import subprocess
import sys
import threading
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


MAX_LEVEL_DIRECTORIES = 4096
MAX_LEVEL_SUMMARY_ITEMS = 1 << 20
MAX_PROBE_CAPTURE_BYTES = 64 * 1024
PIPE_READ_BYTES = 4096
LEVEL_SUMMARY = re.compile(
    r"OpenOmega level: code=([A-Z0-9]+) terrain_cells=(\d+) spatial_meshes=(\d+)\b"
)
ERROR_CATEGORIES = (
    "timeout",
    "process_exit",
    "missing_summary",
    "invalid_summary",
    "summary_identity_mismatch",
    "cardinality_mismatch",
    "discovery",
    "config",
)


@dataclass
class ProbeResults:
    levels: int
    valid: int = 0
    terrain_cells: int = 0
    spatial_meshes: int = 0
    errors: list[dict[str, object]] = field(default_factory=list)
    error_categories: dict[str, int] = field(
        default_factory=lambda: {category: 0 for category in ERROR_CATEGORIES}
    )

    @property
    def error_count(self) -> int:
        return sum(self.error_categories.values())

    def record_error(
        self, category: str, detail: dict[str, object] | None = None
    ) -> None:
        self.error_categories[category] += 1
        if detail is not None:
            self.errors.append(detail)


Runner = Callable[..., Any]
Discoverer = Callable[[Path], list[str]]
PathResolver = Callable[[Path], Path]


class AggregateArgumentError(Exception):
    pass


class ProbeOutputLimitExceeded(Exception):
    pass


if os.name == "nt":
    from ctypes import wintypes

    class _JobObjectBasicLimitInformation(ctypes.Structure):
        _fields_ = [
            ("per_process_user_time_limit", ctypes.c_longlong),
            ("per_job_user_time_limit", ctypes.c_longlong),
            ("limit_flags", wintypes.DWORD),
            ("minimum_working_set_size", ctypes.c_size_t),
            ("maximum_working_set_size", ctypes.c_size_t),
            ("active_process_limit", wintypes.DWORD),
            ("affinity", ctypes.c_size_t),
            ("priority_class", wintypes.DWORD),
            ("scheduling_class", wintypes.DWORD),
        ]

    class _IoCounters(ctypes.Structure):
        _fields_ = [
            ("read_operation_count", ctypes.c_ulonglong),
            ("write_operation_count", ctypes.c_ulonglong),
            ("other_operation_count", ctypes.c_ulonglong),
            ("read_transfer_count", ctypes.c_ulonglong),
            ("write_transfer_count", ctypes.c_ulonglong),
            ("other_transfer_count", ctypes.c_ulonglong),
        ]

    class _JobObjectExtendedLimitInformation(ctypes.Structure):
        _fields_ = [
            ("basic_limit_information", _JobObjectBasicLimitInformation),
            ("io_info", _IoCounters),
            ("process_memory_limit", ctypes.c_size_t),
            ("job_memory_limit", ctypes.c_size_t),
            ("peak_process_memory_used", ctypes.c_size_t),
            ("peak_job_memory_used", ctypes.c_size_t),
        ]

    _KERNEL32 = ctypes.WinDLL("kernel32", use_last_error=True)
    _NTDLL = ctypes.WinDLL("ntdll")
    _KERNEL32.CreateJobObjectW.argtypes = (ctypes.c_void_p, wintypes.LPCWSTR)
    _KERNEL32.CreateJobObjectW.restype = wintypes.HANDLE
    _KERNEL32.SetInformationJobObject.argtypes = (
        wintypes.HANDLE,
        ctypes.c_int,
        ctypes.c_void_p,
        wintypes.DWORD,
    )
    _KERNEL32.SetInformationJobObject.restype = wintypes.BOOL
    _KERNEL32.AssignProcessToJobObject.argtypes = (wintypes.HANDLE, wintypes.HANDLE)
    _KERNEL32.AssignProcessToJobObject.restype = wintypes.BOOL
    _KERNEL32.TerminateJobObject.argtypes = (wintypes.HANDLE, wintypes.UINT)
    _KERNEL32.TerminateJobObject.restype = wintypes.BOOL
    _KERNEL32.CloseHandle.argtypes = (wintypes.HANDLE,)
    _KERNEL32.CloseHandle.restype = wintypes.BOOL
    _NTDLL.NtResumeProcess.argtypes = (wintypes.HANDLE,)
    _NTDLL.NtResumeProcess.restype = wintypes.LONG

    _JOB_OBJECT_EXTENDED_LIMIT_INFORMATION = 9
    _JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000
    _CREATE_SUSPENDED = getattr(subprocess, "CREATE_SUSPENDED", 0x00000004)


class _OwnedProcessTree:
    """Own one probe process tree until all captured handles reach EOF."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._terminated = False
        self._termination_error: OSError | None = None
        self._job: Any | None = None
        if os.name == "nt":
            job = _KERNEL32.CreateJobObjectW(None, None)
            if not job:
                raise ctypes.WinError(ctypes.get_last_error())
            information = _JobObjectExtendedLimitInformation()
            information.basic_limit_information.limit_flags = (
                _JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
            )
            if not _KERNEL32.SetInformationJobObject(
                job,
                _JOB_OBJECT_EXTENDED_LIMIT_INFORMATION,
                ctypes.byref(information),
                ctypes.sizeof(information),
            ):
                error = ctypes.WinError(ctypes.get_last_error())
                _KERNEL32.CloseHandle(job)
                raise error
            self._job = job

    def popen_options(self) -> dict[str, object]:
        if os.name == "nt":
            return {"creationflags": _CREATE_SUSPENDED}
        return {"start_new_session": True}

    def attach(self, process: subprocess.Popen[bytes]) -> None:
        if os.name != "nt":
            return
        process_handle = wintypes.HANDLE(int(process._handle))  # type: ignore[attr-defined]
        if not _KERNEL32.AssignProcessToJobObject(self._job, process_handle):
            error = ctypes.WinError(ctypes.get_last_error())
            process.kill()
            process.wait()
            self.close()
            raise error

    def start(self, process: subprocess.Popen[bytes]) -> None:
        if os.name != "nt":
            return
        process_handle = wintypes.HANDLE(int(process._handle))  # type: ignore[attr-defined]
        status = int(_NTDLL.NtResumeProcess(process_handle))
        if status < 0:
            self.terminate(process)
            process.wait()
            self.close()
            raise OSError(f"NtResumeProcess failed with status {status}")

    def terminate(self, process: subprocess.Popen[bytes]) -> None:
        with self._lock:
            if self._terminated:
                return
            self._terminated = True
            if os.name == "nt":
                if not _KERNEL32.TerminateJobObject(self._job, 1):
                    self._termination_error = ctypes.WinError(ctypes.get_last_error())
            else:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                except OSError as error:
                    self._termination_error = error
            if process.poll() is None:
                try:
                    process.kill()
                except OSError:
                    pass

    def raise_if_termination_failed(self) -> None:
        if self._termination_error is not None:
            raise RuntimeError("probe process-tree termination failed") from self._termination_error

    def close(self) -> None:
        if os.name == "nt" and self._job is not None:
            _KERNEL32.CloseHandle(self._job)
            self._job = None


class ProbeArgumentParser(argparse.ArgumentParser):
    def __init__(self, *, aggregate_only: bool) -> None:
        super().__init__()
        self.aggregate_only = aggregate_only

    def error(self, message: str) -> None:
        if self.aggregate_only:
            raise AggregateArgumentError from None
        super().error(message)


def bounded_run(
    command: Sequence[str],
    *,
    check: bool,
    capture_output: bool,
    text: bool,
    timeout: float,
) -> subprocess.CompletedProcess[str]:
    """Run one probe while bounding combined captured stdout and stderr."""
    if not capture_output or not text:
        raise ValueError("bounded probe runner requires captured text output")

    tree = _OwnedProcessTree()
    try:
        process = subprocess.Popen(
            list(command),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=False,
            close_fds=True,
            **tree.popen_options(),
        )
    except Exception:
        tree.close()
        raise
    if process.stdout is None or process.stderr is None:
        tree.terminate(process)
        process.wait()
        tree.close()
        raise RuntimeError("bounded probe runner could not create output pipes")
    try:
        tree.attach(process)
    except Exception:
        process.stdout.close()
        process.stderr.close()
        tree.close()
        raise

    buffers = (bytearray(), bytearray())
    capture_lock = threading.Lock()
    output_exceeded = threading.Event()
    captured_bytes = 0

    def drain(pipe: Any, output: bytearray) -> None:
        nonlocal captured_bytes
        while True:
            try:
                chunk = pipe.read1(PIPE_READ_BYTES)
            except (OSError, ValueError):
                return
            if not chunk:
                return
            exceeded = False
            with capture_lock:
                remaining = max(0, MAX_PROBE_CAPTURE_BYTES - captured_bytes)
                retained = min(len(chunk), remaining)
                if retained:
                    output.extend(chunk[:retained])
                    captured_bytes += retained
                exceeded = retained != len(chunk)
            if exceeded:
                output_exceeded.set()
                tree.terminate(process)

    readers = (
        threading.Thread(target=drain, args=(process.stdout, buffers[0]), daemon=True),
        threading.Thread(target=drain, args=(process.stderr, buffers[1]), daemon=True),
    )
    try:
        for reader in readers:
            reader.start()
        tree.start(process)
    except Exception:
        tree.terminate(process)
        process.wait()
        for reader in readers:
            if reader.ident is not None:
                reader.join(timeout=1.0)
        process.stdout.close()
        process.stderr.close()
        tree.close()
        raise

    timeout_error: subprocess.TimeoutExpired | None = None
    try:
        try:
            returncode = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired as error:
            timeout_error = error
            returncode = -1
        finally:
            # A direct child may exit while a descendant still owns an inherited pipe. End the
            # complete owned tree before waiting for reader EOF or closing buffered handles.
            tree.terminate(process)

        if process.poll() is None:
            try:
                returncode = process.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                try:
                    process.kill()
                except OSError:
                    pass
                returncode = process.wait(timeout=5.0)

        for reader in readers:
            reader.join(timeout=1.0)
        if any(reader.is_alive() for reader in readers):
            # Avoid BufferedReader.close() here: it can block on a reader thread's internal lock.
            for pipe in (process.stdout, process.stderr):
                try:
                    os.close(pipe.fileno())
                except OSError:
                    pass
            for reader in readers:
                reader.join(timeout=1.0)
        readers_stopped = not any(reader.is_alive() for reader in readers)

        for pipe in (process.stdout, process.stderr):
            if readers_stopped:
                try:
                    pipe.close()
                except OSError:
                    pass

        tree.raise_if_termination_failed()
        if not readers_stopped:
            raise RuntimeError("probe output readers did not stop after process-tree termination")
        if timeout_error is not None:
            raise subprocess.TimeoutExpired(
                command,
                timeout,
                output=bytes(buffers[0]),
                stderr=bytes(buffers[1]),
            ) from timeout_error
        if output_exceeded.is_set():
            raise ProbeOutputLimitExceeded

        stdout = bytes(buffers[0]).decode("utf-8", errors="replace")
        stderr = bytes(buffers[1]).decode("utf-8", errors="replace")
        completed = subprocess.CompletedProcess(list(command), returncode, stdout, stderr)
        if check:
            completed.check_returncode()
        return completed
    finally:
        tree.terminate(process)
        tree.close()


def discover_levels(
    root: Path, *, maximum_level_directories: int = MAX_LEVEL_DIRECTORIES
) -> list[str]:
    gamedata = root / "GAMEDATA"
    if not gamedata.is_dir() or gamedata.is_symlink():
        raise ValueError("data root has no safe GAMEDATA directory")

    levels: list[str] = []
    visited = 0
    for candidate in gamedata.iterdir():
        visited += 1
        if visited > maximum_level_directories:
            raise ValueError("GAMEDATA directory count exceeds safety limit")
        if candidate.is_symlink() or not candidate.is_dir():
            continue
        pop = candidate / "DATA.POP"
        if pop.is_file() and not pop.is_symlink():
            levels.append(candidate.name)
    return sorted(levels, key=str.upper)


def resolve_existing(path: Path) -> Path:
    return path.resolve(strict=True)


def _detailed_error(level: str, completed: Any) -> dict[str, object]:
    return {
        "level": level,
        "exit": completed.returncode,
        "error": completed.stderr.strip() or "missing native level summary",
    }


def probe_levels(
    executable: Path,
    root: Path,
    levels: Sequence[str],
    timeout: float,
    *,
    runner: Runner = bounded_run,
    aggregate_only: bool = False,
) -> ProbeResults:
    results = ProbeResults(levels=len(levels))
    for level in levels:
        command = [
            str(executable),
            f"--data-root={root}",
            f"--level={level}",
            "--probe-only",
        ]
        try:
            completed = runner(
                command,
                check=False,
                capture_output=True,
                text=True,
                timeout=timeout,
            )
        except subprocess.TimeoutExpired:
            results.record_error(
                "timeout",
                None
                if aggregate_only
                else {"level": level, "error": "probe timed out"},
            )
            continue
        except ProbeOutputLimitExceeded:
            results.record_error(
                "process_exit",
                None
                if aggregate_only
                else {"level": level, "error": "probe output exceeded safety limit"},
            )
            continue
        except Exception:
            if not aggregate_only:
                raise
            results.record_error("process_exit")
            continue

        try:
            match = LEVEL_SUMMARY.search(completed.stdout)
            if completed.returncode != 0:
                category = "process_exit"
            elif match is None:
                category = "missing_summary"
            elif match.group(1) != level.upper():
                category = "summary_identity_mismatch"
            elif (
                len(match.group(2)) > len(str(MAX_LEVEL_SUMMARY_ITEMS))
                or len(match.group(3)) > len(str(MAX_LEVEL_SUMMARY_ITEMS))
            ):
                category = "invalid_summary"
            elif match.group(2) != match.group(3):
                category = "cardinality_mismatch"
            else:
                terrain_count = int(match.group(2))
                spatial_count = int(match.group(3))
                category = (
                    "invalid_summary"
                    if terrain_count > MAX_LEVEL_SUMMARY_ITEMS
                    or spatial_count > MAX_LEVEL_SUMMARY_ITEMS
                    else None
                )
        except Exception:
            if not aggregate_only:
                raise
            results.record_error("process_exit")
            continue

        if category is not None:
            results.record_error(
                category,
                None if aggregate_only else _detailed_error(level, completed),
            )
            continue
        results.valid += 1
        results.terrain_cells += terrain_count
        results.spatial_meshes += spatial_count
    return results


def detailed_document(results: ProbeResults) -> dict[str, object]:
    return {
        "levels": results.levels,
        "valid": results.valid,
        "terrain_cells": results.terrain_cells,
        "spatial_meshes": results.spatial_meshes,
        "errors": results.errors,
    }


def aggregate_document(results: ProbeResults) -> dict[str, object]:
    return {
        "levels": results.levels,
        "valid": results.valid,
        "terrain_cells": results.terrain_cells,
        "spatial_meshes": results.spatial_meshes,
        "errors": results.error_count,
        "error_categories": {
            category: results.error_categories[category]
            for category in ERROR_CATEGORIES
        },
    }


def aggregate_failure_document(category: str) -> dict[str, object]:
    results = ProbeResults(levels=0)
    results.error_categories[category] = 1
    return aggregate_document(results)


def _print_compact(document: dict[str, object]) -> None:
    print(json.dumps(document, separators=(",", ":"), sort_keys=True))


def main(
    argv: Sequence[str] | None = None,
    *,
    runner: Runner = bounded_run,
    discoverer: Discoverer = discover_levels,
    path_resolver: PathResolver = resolve_existing,
) -> int:
    raw_argv = list(sys.argv[1:] if argv is None else argv)
    aggregate_requested = any(
        argument == "--aggregate-only" or argument.startswith("--aggregate-only=")
        for argument in raw_argv
    )
    parser = ProbeArgumentParser(aggregate_only=aggregate_requested)
    parser.add_argument("openomega", type=Path)
    parser.add_argument("root", type=Path)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument(
        "--aggregate-only",
        action="store_true",
        help="emit fixed aggregate counts without level identities or raw diagnostics",
    )
    try:
        args = parser.parse_args(raw_argv)
    except AggregateArgumentError:
        _print_compact(aggregate_failure_document("config"))
        return 1

    try:
        executable = path_resolver(args.openomega)
        root = path_resolver(args.root)
    except Exception:
        if not args.aggregate_only:
            raise
        _print_compact(aggregate_failure_document("config"))
        return 1

    if not math.isfinite(args.timeout) or args.timeout <= 0:
        if not args.aggregate_only:
            parser.error("--timeout must be finite and positive")
        _print_compact(aggregate_failure_document("config"))
        return 1

    try:
        levels = discoverer(root)
    except (OSError, ValueError) as error:
        if args.aggregate_only:
            _print_compact(aggregate_failure_document("discovery"))
        else:
            print(
                json.dumps(
                    {
                        "levels": 0,
                        "valid": 0,
                        "errors": [str(error)],
                    },
                    sort_keys=True,
                )
            )
        return 1
    except Exception:
        if not args.aggregate_only:
            raise
        _print_compact(aggregate_failure_document("discovery"))
        return 1
    if not levels:
        if args.aggregate_only:
            _print_compact(aggregate_failure_document("discovery"))
        else:
            print(
                json.dumps(
                    {"levels": 0, "valid": 0, "errors": ["no level POP files found"]},
                    sort_keys=True,
                )
            )
        return 1

    results = probe_levels(
        executable,
        root,
        levels,
        args.timeout,
        runner=runner,
        aggregate_only=args.aggregate_only,
    )
    if args.aggregate_only:
        _print_compact(aggregate_document(results))
    else:
        _print_compact(detailed_document(results))
    return 1 if results.error_count else 0


if __name__ == "__main__":
    raise SystemExit(main())
