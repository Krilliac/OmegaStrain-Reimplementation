#!/usr/bin/env python3
"""Enforce native source dependency boundaries with fail-closed token scanning."""

from __future__ import annotations

import argparse
import os
import re
import stat
from bisect import bisect_right
from dataclasses import dataclass
from pathlib import Path, PurePosixPath


SOURCE_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".inc",
    ".inl",
    ".ipp",
    ".tpp",
    ".def",
}
MODULE_SUFFIXES = {".ccm", ".cppm", ".cxxm", ".ixx", ".mpp", ".mxx"}
MAXIMUM_SOURCE_BYTES = 5 * 1024 * 1024

_DIRECTIVE_PATTERN = re.compile(
    r"^[ \t\v\f]*(?:#|%:)[ \t\v\f]*([A-Za-z_][A-Za-z_0-9]*)(.*)$"
)
_LINE_SPLICE_PATTERN = re.compile(r"\\(?:\r\n|\n|\r)")
_MODULE_IMPORT_PATTERN = re.compile(
    r"\bimport[ \t\v\f\r\n]+(?=[:<\"A-Za-z_])"
)
_MODULE_DECLARATION_PATTERN = re.compile(
    r"(?:^|[;{}])[ \t\v\f\r\n]*(?:export[ \t\v\f\r\n]+)?module"
    r"(?:[ \t\v\f\r\n]+(?=[:A-Za-z_])|[ \t\v\f\r\n]*;)"
)
_REPARSE_POINT_ATTRIBUTE = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
_WINDOWS_FORBIDDEN_PATH_CHARACTERS = frozenset('<>"|?*:')
_WINDOWS_RESERVED_PATH_STEMS = frozenset(
    {
        "aux",
        "clock$",
        "con",
        "conin$",
        "conout$",
        "nul",
        "prn",
        *(f"com{index}" for index in range(1, 10)),
        *(f"lpt{index}" for index in range(1, 10)),
        *(f"com{index}" for index in "¹²³"),
        *(f"lpt{index}" for index in "¹²³"),
    }
)


# Platform-neutral modules accept only the standard C/C++ library headers in
# this allowlist, canonical project headers, and same/allowed-module local
# headers. Adding any external dependency therefore requires an explicit review.
STANDARD_LIBRARY_HEADERS = frozenset(
    {
        "algorithm",
        "any",
        "array",
        "assert.h",
        "atomic",
        "barrier",
        "bit",
        "bitset",
        "cassert",
        "cctype",
        "cerrno",
        "cfenv",
        "cfloat",
        "charconv",
        "chrono",
        "cinttypes",
        "ciso646",
        "climits",
        "clocale",
        "cmath",
        "codecvt",
        "compare",
        "complex",
        "concepts",
        "condition_variable",
        "coroutine",
        "csetjmp",
        "csignal",
        "cstdarg",
        "cstddef",
        "cstdint",
        "cstdio",
        "cstdlib",
        "cstring",
        "ctime",
        "cuchar",
        "cwchar",
        "cwctype",
        "deque",
        "errno.h",
        "exception",
        "execution",
        "expected",
        "fenv.h",
        "filesystem",
        "float.h",
        "format",
        "forward_list",
        "fstream",
        "functional",
        "future",
        "generator",
        "initializer_list",
        "iomanip",
        "ios",
        "iosfwd",
        "iostream",
        "istream",
        "iterator",
        "latch",
        "limits",
        "limits.h",
        "list",
        "locale",
        "locale.h",
        "map",
        "math.h",
        "mdspan",
        "memory",
        "memory_resource",
        "mutex",
        "new",
        "numbers",
        "numeric",
        "optional",
        "ostream",
        "print",
        "queue",
        "random",
        "ranges",
        "ratio",
        "regex",
        "scoped_allocator",
        "semaphore",
        "set",
        "setjmp.h",
        "shared_mutex",
        "signal.h",
        "source_location",
        "span",
        "spanstream",
        "sstream",
        "stack",
        "stacktrace",
        "stdarg.h",
        "stdexcept",
        "stddef.h",
        "stdfloat",
        "stdint.h",
        "stdio.h",
        "stdlib.h",
        "stop_token",
        "streambuf",
        "string",
        "string.h",
        "string_view",
        "syncstream",
        "system_error",
        "thread",
        "time.h",
        "tuple",
        "type_traits",
        "typeindex",
        "typeinfo",
        "uchar.h",
        "unordered_map",
        "unordered_set",
        "utility",
        "valarray",
        "variant",
        "vector",
        "version",
        "wchar.h",
        "wctype.h",
    }
)


@dataclass(frozen=True)
class ModuleRule:
    path_prefix: str
    name: str
    allowed_omega_modules: frozenset[str]
    platform_neutral: bool = False


# Keep platform-header exceptions exact and local to the translation unit that
# owns the platform boundary. Adding a Windows backend must not make the rest
# of an otherwise platform-neutral module eligible to include arbitrary SDK
# headers.
_EXACT_EXTERNAL_HEADER_ALLOWLIST = {
    "native/include/omega/debug/subsystem_entry_break.h": frozenset(
        {
            "windows.h",
            "debugapi.h",
            "intrin.h",
            "processenv.h",
        }
    ),
    "native/src/media/media_foundation_h262_decoder.cpp": frozenset(
        {
            "windows.h",
            "mfapi.h",
            "mferror.h",
            "mfidl.h",
            "mftransform.h",
            "wrl/client.h",
        }
    ),
}

_DEBUG_SUBSYSTEM_ENTRY_BREAK_HEADER = "omega/debug/subsystem_entry_break.h"
_DEBUG_SUBSYSTEM_ENTRY_BREAK_SOURCES = frozenset(
    {
        "native/apps/openomega/front_end.cpp",
        "native/apps/openomega/native_persistence.cpp",
        "native/apps/openomega/omega_app.cpp",
        "native/apps/openomega/sdl_platform_service.cpp",
        "native/apps/openomega_launcher/launcher_config.cpp",
        "native/apps/openomega_launcher/launcher_window.cpp",
        "native/src/archive/hog_archive.cpp",
        "native/src/compat/ps2_memory_card_image.cpp",
        "native/src/content/game_data_service.cpp",
        "native/src/frontend/compositor_math.cpp",
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
    }
)


_CORE_EDGES = frozenset({"omega_core", "omega_assets"})
_ASSET_EDGES = frozenset({"omega_assets"})
_FRONTEND_EDGES = frozenset({"omega_frontend", "omega_assets"})
_MEDIA_EDGES = frozenset({"omega_media", "omega_assets"})
_SIMULATION_EDGES = frozenset({"omega_simulation", "omega_assets", "omega_core"})
_GAMEPLAY_EDGES = frozenset({"omega_gameplay", "omega_simulation"})
_PERSISTENCE_EDGES = frozenset({"omega_persistence"})
_PROFILES_EDGES = frozenset({"omega_profiles", "omega_persistence"})
_PS2_COMPAT_EDGES = frozenset({"omega_ps2_compat"})
_RETAIL_EDGES = frozenset({"omega_retail_formats", "omega_assets", "omega_core"})
_FRONTEND_TEXT_EDGES = frozenset(
    {"omega_frontend_text", "omega_retail_formats"}
)
_FRONTEND_PRESENTATION_EDGES = frozenset(
    {
        "omega_frontend_presentation",
        "omega_assets",
        "omega_content",
        "omega_frontend",
        "omega_frontend_text",
    }
)
_CONTENT_EDGES = frozenset(
    {"omega_content", "omega_retail_formats", "omega_assets", "omega_core"}
)
_RUNTIME_EDGES = frozenset(
    {"omega_runtime", "omega_content", "omega_assets", "omega_core"}
)
_SDL_EDGES = frozenset({"omega_sdl_backend", "omega_runtime"})
_LAUNCHER_CORE_EDGES = frozenset({"omega_launcher_core", "omega_runtime"})
_LAUNCHER_HOST_EDGES = frozenset(
    {"omega_launcher_host", "omega_launcher_core", "omega_content"}
)
_LAUNCHER_EXE_EDGES = frozenset({"openomega_launcher", "omega_launcher_host"})
_APP_EDGES = frozenset(
    {
        "openomega",
        "omega_persistence",
        "omega_profiles",
        "omega_media",
        "omega_sdl_backend",
        "omega_runtime",
        "omega_simulation",
        "omega_gameplay",
    }
)


# Longest prefixes must come first. Tests and omega_tool receive global lexical,
# path, module-syntax, PCSX2, and filesystem safety checks, but intentionally do
# not receive shipping-library module-edge restrictions.
MODULE_RULES = (
    ModuleRule("native/apps/openomega/sdl_", "omega_sdl_backend", _SDL_EDGES),
    ModuleRule("native/apps/openomega/", "openomega", _APP_EDGES),
    ModuleRule(
        "native/include/omega/frontend_presentation/",
        "omega_frontend_presentation",
        _FRONTEND_PRESENTATION_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/src/frontend_presentation/",
        "omega_frontend_presentation",
        _FRONTEND_PRESENTATION_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/include/omega/frontend/",
        "omega_frontend",
        _FRONTEND_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/src/frontend/",
        "omega_frontend",
        _FRONTEND_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/include/omega/gameplay/",
        "omega_gameplay",
        _GAMEPLAY_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/src/gameplay/",
        "omega_gameplay",
        _GAMEPLAY_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/include/omega/persistence/",
        "omega_persistence",
        _PERSISTENCE_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/src/persistence/",
        "omega_persistence",
        _PERSISTENCE_EDGES,
    ),
    ModuleRule(
        "native/include/omega/profiles/",
        "omega_profiles",
        _PROFILES_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/src/profiles/",
        "omega_profiles",
        _PROFILES_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/include/omega/compat/",
        "omega_ps2_compat",
        _PS2_COMPAT_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/src/compat/",
        "omega_ps2_compat",
        _PS2_COMPAT_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/include/omega/simulation/",
        "omega_simulation",
        _SIMULATION_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/src/simulation/",
        "omega_simulation",
        _SIMULATION_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/include/omega/runtime/",
        "omega_runtime",
        _RUNTIME_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/src/runtime/",
        "omega_runtime",
        _RUNTIME_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/include/omega/content/",
        "omega_content",
        _CONTENT_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/src/content/",
        "omega_content",
        _CONTENT_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/include/omega/media/",
        "omega_media",
        _MEDIA_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/src/media/",
        "omega_media",
        _MEDIA_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/include/omega/retail/",
        "omega_retail_formats",
        _RETAIL_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/src/retail/",
        "omega_retail_formats",
        _RETAIL_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/include/omega/frontend_text/",
        "omega_frontend_text",
        _FRONTEND_TEXT_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/src/frontend_text/",
        "omega_frontend_text",
        _FRONTEND_TEXT_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/include/omega/asset/",
        "omega_assets",
        _ASSET_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/include/omega/archive/",
        "omega_core",
        _CORE_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/include/omega/vfs/",
        "omega_core",
        _CORE_EDGES,
        platform_neutral=True,
    ),
    ModuleRule(
        "native/src/archive/", "omega_core", _CORE_EDGES, platform_neutral=True
    ),
    ModuleRule(
        "native/src/asset/", "omega_core", _CORE_EDGES, platform_neutral=True
    ),
    ModuleRule(
        "native/src/vfs/", "omega_core", _CORE_EDGES, platform_neutral=True
    ),
)

EXACT_MODULE_RULES = {
    "native/apps/openomega_launcher/launcher_config.cpp": ModuleRule(
        "native/apps/openomega_launcher/launcher_config.cpp",
        "omega_launcher_core",
        _LAUNCHER_CORE_EDGES,
    ),
    "native/apps/openomega_launcher/launcher_config.h": ModuleRule(
        "native/apps/openomega_launcher/launcher_config.h",
        "omega_launcher_core",
        _LAUNCHER_CORE_EDGES,
    ),
    "native/apps/openomega_launcher/launcher_window.cpp": ModuleRule(
        "native/apps/openomega_launcher/launcher_window.cpp",
        "omega_launcher_host",
        _LAUNCHER_HOST_EDGES,
    ),
    "native/apps/openomega_launcher/launcher_window.h": ModuleRule(
        "native/apps/openomega_launcher/launcher_window.h",
        "omega_launcher_host",
        _LAUNCHER_HOST_EDGES,
    ),
    "native/apps/openomega_launcher/main.cpp": ModuleRule(
        "native/apps/openomega_launcher/main.cpp",
        "openomega_launcher",
        _LAUNCHER_EXE_EDGES,
    ),
    "native/include/omega/debug/subsystem_entry_break.h": ModuleRule(
        "native/include/omega/debug/subsystem_entry_break.h",
        "omega_debug",
        frozenset({"omega_debug"}),
        platform_neutral=True,
    ),
    "native/include/omega/asset/pop_terrain_index.h": ModuleRule(
        "native/include/omega/asset/pop_terrain_index.h",
        "omega_core",
        _CORE_EDGES,
        platform_neutral=True,
    ),
}

PROJECT_HEADER_MODULES = (
    ("omega/frontend_presentation/", "omega_frontend_presentation"),
    ("omega/frontend/", "omega_frontend"),
    ("omega/gameplay/", "omega_gameplay"),
    ("omega/profiles/", "omega_profiles"),
    ("omega/persistence/", "omega_persistence"),
    ("omega/compat/", "omega_ps2_compat"),
    ("omega/simulation/", "omega_simulation"),
    ("omega/runtime/", "omega_runtime"),
    ("omega/content/", "omega_content"),
    ("omega/media/", "omega_media"),
    ("omega/retail/", "omega_retail_formats"),
    ("omega/frontend_text/", "omega_frontend_text"),
    ("omega/asset/", "omega_assets"),
    ("omega/archive/", "omega_core"),
    ("omega/vfs/", "omega_core"),
)
EXACT_PROJECT_HEADER_MODULES = {
    "omega/debug/subsystem_entry_break.h": "omega_debug",
    "omega/asset/pop_terrain_index.h": "omega_core",
}

GLOBAL_ONLY_PREFIXES = ("native/tests/", "native/apps/omega_tool/")
SHIPPING_PREFIXES = (
    "native/apps/",
    "native/include/omega/",
    "native/src/",
)
_RAW_STRING_PREFIXES = ("u8R\"", "uR\"", "UR\"", "LR\"", "R\"")


@dataclass(frozen=True)
class IncludeDirective:
    line_number: int
    delimiter: str
    path: str


@dataclass(frozen=True)
class SourceScan:
    includes: tuple[IncludeDirective, ...]
    errors: tuple[tuple[int, str], ...]


class SourceParseFailure(ValueError):
    def __init__(self, line_number: int, message: str) -> None:
        super().__init__(message)
        self.line_number = line_number
        self.message = message


def module_rule(relative_path: Path) -> ModuleRule | None:
    normalized = relative_path.as_posix().casefold()
    exact = EXACT_MODULE_RULES.get(normalized)
    if exact is not None:
        return exact
    for rule in MODULE_RULES:
        if normalized.startswith(rule.path_prefix):
            return rule
    return None


def _is_global_only(relative_path: Path) -> bool:
    normalized = relative_path.as_posix().casefold()
    return any(normalized.startswith(prefix) for prefix in GLOBAL_ONLY_PREFIXES)


def _is_shipping_path(relative_path: Path) -> bool:
    normalized = relative_path.as_posix().casefold()
    return not _is_global_only(relative_path) and any(
        normalized.startswith(prefix) for prefix in SHIPPING_PREFIXES
    )


def _project_header_module(include_lower: str) -> str | None:
    exact = EXACT_PROJECT_HEADER_MODULES.get(include_lower)
    if exact is not None:
        return exact
    for prefix, module in PROJECT_HEADER_MODULES:
        if include_lower.startswith(prefix):
            return module
    return None


def _stat_is_reparse(value: os.stat_result) -> bool:
    attributes = getattr(value, "st_file_attributes", 0) or 0
    return bool(attributes & _REPARSE_POINT_ATTRIBUTE)


def _path_is_link_like(path: Path, value: os.stat_result) -> bool:
    is_junction = getattr(path, "is_junction", lambda: False)()
    return stat.S_ISLNK(value.st_mode) or is_junction or _stat_is_reparse(value)


def _same_identity(left: os.stat_result, right: os.stat_result) -> bool:
    return (left.st_dev, left.st_ino) == (right.st_dev, right.st_ino)


def _same_snapshot(left: os.stat_result, right: os.stat_result) -> bool:
    # On Windows, pathlib/os.stat and fstat can expose slightly different
    # st_ctime values for the same unchanged file handle.  Identity, kind,
    # size, and last-write time are the stable cross-handle race boundary.
    return (
        _same_identity(left, right)
        and left.st_mode == right.st_mode
        and left.st_size == right.st_size
        and left.st_mtime_ns == right.st_mtime_ns
    )


def _replace_non_newlines(buffer: list[str], start: int, end: int) -> None:
    for index in range(start, end):
        if buffer[index] not in "\r\n":
            buffer[index] = " "


def _is_digit_separator(source: str, index: int) -> bool:
    """Return whether an apostrophe belongs to a preprocessing number."""
    if (
        source[index] != "'"
        or index == 0
        or index + 1 >= len(source)
        or not source[index - 1].isalnum()
        or not source[index + 1].isalnum()
    ):
        return False
    token_start = index - 1
    while token_start > 0 and (
        source[token_start - 1].isalnum() or source[token_start - 1] in "._"
    ):
        token_start -= 1
    token = source[token_start:index]
    return bool(token) and (
        token[0].isdigit() or (token.startswith(".") and token[1:2].isdigit())
    )


def _raw_string_end(source: str, start: int, line_number: int) -> int | None:
    if start and (source[start - 1].isalnum() or source[start - 1] == "_"):
        return None
    for prefix in _RAW_STRING_PREFIXES:
        if not source.startswith(prefix, start):
            continue
        delimiter_start = start + len(prefix)
        open_parenthesis = source.find("(", delimiter_start, delimiter_start + 17)
        if open_parenthesis < 0:
            continue
        delimiter = source[delimiter_start:open_parenthesis]
        if len(delimiter) > 16 or any(
            character.isspace() or character in "()\\" for character in delimiter
        ):
            continue
        terminator = ")" + delimiter + '"'
        close = source.find(terminator, open_parenthesis + 1)
        if close < 0:
            raise SourceParseFailure(line_number, "unterminated raw string literal")
        return close + len(terminator)
    return None


def _sanitize_source(source: str) -> str:
    # Translation phase 2 removes escaped physical newlines before comments and
    # preprocessing directives are recognized.
    source = _LINE_SPLICE_PATTERN.sub("", source)
    buffer = list(source)
    index = 0
    line_number = 1
    while index < len(source):
        if source.startswith("//", index):
            end = source.find("\n", index + 2)
            if end < 0:
                end = len(source)
            _replace_non_newlines(buffer, index, end)
            index = end
            continue
        if source.startswith("/*", index):
            end = source.find("*/", index + 2)
            if end < 0:
                raise SourceParseFailure(line_number, "unterminated block comment")
            end += 2
            _replace_non_newlines(buffer, index, end)
            line_number += source.count("\n", index, end)
            index = end
            continue

        raw_end = _raw_string_end(source, index, line_number)
        if raw_end is not None:
            _replace_non_newlines(buffer, index, raw_end)
            line_number += source.count("\n", index, raw_end)
            index = raw_end
            continue

        character = source[index]
        if character == "'" and _is_digit_separator(source, index):
            index += 1
            continue
        if character in {'"', "'"}:
            quote = character
            cursor = index + 1
            while cursor < len(source):
                if source[cursor] in "\r\n":
                    raise SourceParseFailure(line_number, "unterminated quoted literal")
                if source[cursor] == "\\":
                    cursor += 2
                    continue
                if cursor < len(source) and source[cursor] == quote:
                    cursor += 1
                    break
                cursor += 1
            else:
                raise SourceParseFailure(line_number, "unterminated quoted literal")
            index = cursor
            continue
        if character == "\n":
            line_number += 1
        index += 1
    return "".join(buffer)


def _blank_ordinary_literals(line: str) -> str:
    buffer = list(line)
    index = 0
    while index < len(line):
        if line[index] == "'" and _is_digit_separator(line, index):
            index += 1
            continue
        if line[index] not in {'"', "'"}:
            index += 1
            continue
        quote = line[index]
        cursor = index + 1
        while cursor < len(line):
            if line[cursor] == "\\":
                cursor += 2
                continue
            if cursor < len(line) and line[cursor] == quote:
                cursor += 1
                break
            cursor += 1
        _replace_non_newlines(buffer, index, min(cursor, len(buffer)))
        index = cursor
    return "".join(buffer)


def _parse_literal_include(body: str) -> tuple[str, str] | str:
    operand = body.strip(" \t\v\f")
    if not operand or operand[0] not in {'"', "<"}:
        return "non-literal include operands are forbidden"
    opener = operand[0]
    closer = '"' if opener == '"' else ">"
    close = operand.find(closer, 1)
    if close < 0:
        return "unterminated literal include operand"
    include = operand[1:close]
    if not include:
        return "empty include paths are forbidden"
    if operand[close + 1 :].strip(" \t\v\f"):
        return "tokens after a literal include operand are forbidden"
    return opener, include


def scan_source(source: str) -> SourceScan:
    try:
        sanitized = _sanitize_source(source)
    except SourceParseFailure as exc:
        return SourceScan((), ((exc.line_number, exc.message),))

    includes: list[IncludeDirective] = []
    errors: list[tuple[int, str]] = []
    module_lines: list[str] = []
    for line_number, line_with_ending in enumerate(
        sanitized.splitlines(keepends=True), start=1
    ):
        line = line_with_ending.rstrip("\r\n")
        ending = "\n" if len(line) != len(line_with_ending) else ""
        directive = _DIRECTIVE_PATTERN.match(line)
        if directive is not None:
            name = directive.group(1)
            if name in {"include", "include_next"}:
                parsed = _parse_literal_include(directive.group(2))
                if isinstance(parsed, str):
                    errors.append((line_number, parsed))
                else:
                    delimiter, include = parsed
                    includes.append(IncludeDirective(line_number, delimiter, include))
            elif name == "import":
                errors.append((line_number, "preprocessor imports are unsupported"))
            module_lines.append(" " * len(line) + ending)
            continue

        module_lines.append(_blank_ordinary_literals(line) + ending)

    module_source = "".join(module_lines)
    newline_offsets = [
        index for index, character in enumerate(module_source) if character == "\n"
    ]
    for match in _MODULE_IMPORT_PATTERN.finditer(module_source):
        line_number = bisect_right(newline_offsets, match.start()) + 1
        errors.append((line_number, "C++ module imports are unsupported"))
    for match in _MODULE_DECLARATION_PATTERN.finditer(module_source):
        module_offset = match.group(0).find("module")
        line_number = bisect_right(
            newline_offsets, match.start() + max(module_offset, 0)
        ) + 1
        errors.append((line_number, "C++ module declarations are unsupported"))
    return SourceScan(tuple(includes), tuple(errors))


def _normalize_include_path(include: str) -> tuple[str, str, str | None]:
    if not include or include != include.strip():
        return include, include.casefold(), "non-canonical include path"
    had_backslash = "\\" in include
    normalized = include.replace("\\", "/")
    lower = normalized.casefold()
    if had_backslash:
        return normalized, lower, "backslashes in include paths are forbidden"
    if normalized.startswith("/") or re.match(r"^[A-Za-z]:", normalized):
        return normalized, lower, "absolute include paths are forbidden"
    parts = normalized.split("/")
    if any(part in {"", ".", ".."} for part in parts):
        return normalized, lower, "empty or dot include-path segments are forbidden"
    for part in parts:
        if any(ord(character) < 32 for character in part) or any(
            character in _WINDOWS_FORBIDDEN_PATH_CHARACTERS for character in part
        ):
            return normalized, lower, "Windows-unsafe include-path characters are forbidden"
        if part.endswith((" ", ".")):
            return normalized, lower, "Windows-aliased trailing dots or spaces are forbidden"
        stem = part.split(".", 1)[0].casefold()
        if stem in _WINDOWS_RESERVED_PATH_STEMS:
            return normalized, lower, "Windows reserved device include paths are forbidden"
    return normalized, lower, None


def _format_error(relative_path: Path, line_number: int, message: str) -> str:
    return f"{relative_path.as_posix()}:{line_number}: {message}"


def _resolve_local_include(
    root: Path, source_path: Path, normalized: str
) -> tuple[Path | None, str | None]:
    current = source_path.parent
    parts = PurePosixPath(normalized).parts
    for index, part in enumerate(parts):
        try:
            with os.scandir(current) as entries:
                names = [entry.name for entry in entries]
        except OSError:
            return None, "local include path could not be inspected safely"

        if part not in names:
            if any(name.casefold() == part.casefold() for name in names):
                return None, "local include paths must use exact on-disk spelling"
            # Win32 can resolve names that directory enumeration never returns,
            # notably 8.3 short aliases.  A path that resolves without an exact
            # directory entry is non-canonical even when its spelling is not a
            # simple case variant.
            try:
                os.stat(current / part, follow_symlinks=False)
            except FileNotFoundError:
                return None, None
            except OSError:
                return None, "local include path could not be inspected safely"
            return None, "local include paths must use exact on-disk spelling"

        current = current / part
        value, entry_error = _safe_stat(current)
        if entry_error is not None or value is None:
            return None, "local include resolves through an unsafe filesystem entry"
        final = index + 1 == len(parts)
        if final and not stat.S_ISREG(value.st_mode):
            return None, "local include target is not a regular file"
        if not final and not stat.S_ISDIR(value.st_mode):
            return None, "local include path crosses a non-directory entry"

    try:
        current.relative_to(root)
    except ValueError:
        return None, "local include escapes the repository root"
    return current, None


def check_include(
    rule: ModuleRule | None,
    relative_path: Path,
    line_number: int,
    include: str,
    *,
    delimiter: str = '"',
    root: Path | None = None,
    source_path: Path | None = None,
) -> str | None:
    normalized, include_lower, path_error = _normalize_include_path(include)
    if "pcsx2" in include_lower:
        owner = rule.name if rule is not None else "native source"
        return _format_error(
            relative_path,
            line_number,
            f"{owner} includes forbidden PCSX2 header <{include}>",
        )
    if path_error is not None:
        return _format_error(relative_path, line_number, path_error)

    if include_lower.startswith("omega/"):
        if normalized != include_lower:
            return _format_error(
                relative_path,
                line_number,
                "project include paths must use canonical lowercase spelling",
            )
        destination = _project_header_module(include_lower)
        if destination is None:
            return _format_error(
                relative_path, line_number, f"unclassified project include <{include}>"
            )
        # The exact header is a cross-cutting, Debug-only private shim, not a
        # production module edge.  The source allowlist prevents this exception
        # from entering public headers or expanding to another implementation.
        if include_lower == _DEBUG_SUBSYSTEM_ENTRY_BREAK_HEADER:
            if (
                relative_path.as_posix().casefold()
                not in _DEBUG_SUBSYSTEM_ENTRY_BREAK_SOURCES
            ):
                return _format_error(
                    relative_path,
                    line_number,
                    "debug instrumentation header is restricted to exact private callsites",
                )
            return None
        if rule is not None and destination not in rule.allowed_omega_modules:
            return _format_error(
                relative_path,
                line_number,
                f"{rule.name} includes forbidden dependency <{include}>",
            )
        return None

    if delimiter == '"' and root is not None and source_path is not None:
        local_path, local_error = _resolve_local_include(root, source_path, normalized)
        if local_error is not None:
            return _format_error(relative_path, line_number, local_error)
        if local_path is not None:
            local_relative = local_path.relative_to(root)
            destination_rule = module_rule(local_relative)
            if rule is not None:
                if destination_rule is None:
                    return _format_error(
                        relative_path,
                        line_number,
                        f"{rule.name} includes an unclassified local dependency <{include}>",
                    )
                if destination_rule.name not in rule.allowed_omega_modules:
                    return _format_error(
                        relative_path,
                        line_number,
                        f"{rule.name} includes forbidden dependency <{include}>",
                    )
            return None
        if rule is not None:
            return _format_error(
                relative_path,
                line_number,
                f"{rule.name} includes unresolved local header <{include}>",
            )
        return None

    if rule is not None and rule.platform_neutral:
        if delimiter == "<" and normalized in STANDARD_LIBRARY_HEADERS:
            return None
        exact_external_headers = _EXACT_EXTERNAL_HEADER_ALLOWLIST.get(
            relative_path.as_posix()
        )
        if (
            delimiter == "<"
            and exact_external_headers is not None
            and normalized in exact_external_headers
        ):
            return None
        return _format_error(
            relative_path,
            line_number,
            f"{rule.name} includes unapproved external header <{include}>",
        )
    return None


def _read_source_stably(path: Path) -> tuple[str | None, str | None]:
    try:
        before = os.stat(path, follow_symlinks=False)
        if not stat.S_ISREG(before.st_mode) or _path_is_link_like(path, before):
            return None, "native source is not a regular non-link file"
        if before.st_size < 0 or before.st_size > MAXIMUM_SOURCE_BYTES:
            return None, f"native source exceeds {MAXIMUM_SOURCE_BYTES} bytes"
        with path.open("rb") as stream:
            opened = os.fstat(stream.fileno())
            if (
                not stat.S_ISREG(opened.st_mode)
                or _stat_is_reparse(opened)
                or not _same_snapshot(before, opened)
            ):
                return None, "native source changed before opening"
            data = stream.read(MAXIMUM_SOURCE_BYTES + 1)
            final = os.fstat(stream.fileno())
            if not _same_snapshot(opened, final):
                return None, "native source changed while being read"
        if len(data) > MAXIMUM_SOURCE_BYTES:
            return None, f"native source exceeds {MAXIMUM_SOURCE_BYTES} bytes"
        return data.decode("utf-8-sig"), None
    except UnicodeDecodeError:
        return None, "native source is not UTF-8 text"
    except OSError:
        return None, "native source could not be read safely"


def check_file(root: Path, path: Path) -> list[str]:
    relative_path = path.relative_to(root)
    rule = module_rule(relative_path)
    source, read_error = _read_source_stably(path)
    if read_error is not None or source is None:
        return [_format_error(relative_path, 1, read_error or "source read failed")]

    scan = scan_source(source)
    errors = [
        _format_error(relative_path, line_number, message)
        for line_number, message in scan.errors
    ]
    for include in scan.includes:
        error = check_include(
            rule,
            relative_path,
            include.line_number,
            include.path,
            delimiter=include.delimiter,
            root=root,
            source_path=path,
        )
        if error is not None:
            errors.append(error)
    return errors


def _safe_stat(path: Path) -> tuple[os.stat_result | None, str | None]:
    try:
        value = os.stat(path, follow_symlinks=False)
        if _path_is_link_like(path, value):
            return None, "links, junctions, and reparse points are forbidden under native/"
    except OSError:
        return None, "filesystem entry could not be inspected safely"
    return value, None


def check_tree(root: Path) -> tuple[int, list[str]]:
    native_root = root / "native"
    native_stat, root_error = _safe_stat(native_root)
    if root_error is not None or native_stat is None:
        if not native_root.exists():
            return 0, [f"native source root is missing: {native_root}"]
        return 0, [f"native source root is unsafe: {native_root}"]
    if not stat.S_ISDIR(native_stat.st_mode):
        return 0, [f"native source root is not a directory: {native_root}"]

    checked = 0
    errors: list[str] = []

    def raise_walk_error(error: OSError) -> None:
        raise error

    try:
        for directory, subdirectories, filenames in os.walk(
            native_root, topdown=True, onerror=raise_walk_error, followlinks=False
        ):
            directory_path = Path(directory)
            directory_before, directory_error = _safe_stat(directory_path)
            if directory_error is not None or directory_before is None:
                errors.append(f"{directory_path.relative_to(root).as_posix()}: unsafe directory")
                subdirectories[:] = []
                continue

            safe_subdirectories: list[str] = []
            for name in sorted(subdirectories):
                path = directory_path / name
                value, entry_error = _safe_stat(path)
                if entry_error is not None or value is None or not stat.S_ISDIR(value.st_mode):
                    errors.append(
                        f"{path.relative_to(root).as_posix()}: unsafe native directory entry"
                    )
                    continue
                safe_subdirectories.append(name)
            subdirectories[:] = safe_subdirectories

            for name in sorted(filenames):
                path = directory_path / name
                relative_path = path.relative_to(root)
                value, entry_error = _safe_stat(path)
                if entry_error is not None or value is None or not stat.S_ISREG(value.st_mode):
                    errors.append(
                        f"{relative_path.as_posix()}: unsafe or special native file entry"
                    )
                    continue

                suffix = path.suffix.casefold()
                if suffix in MODULE_SUFFIXES:
                    checked += 1
                    errors.append(
                        f"{relative_path.as_posix()}: C++ module source suffix is unsupported"
                    )
                    continue
                if suffix in SOURCE_SUFFIXES:
                    checked += 1
                    if module_rule(relative_path) is None and not _is_global_only(relative_path):
                        errors.append(
                            f"{relative_path.as_posix()}: unclassified shipping native source path"
                        )
                    errors.extend(check_file(root, path))
                    continue
                if module_rule(relative_path) is not None:
                    errors.append(
                        f"{relative_path.as_posix()}: unsupported file type in a shipping native module"
                    )
                elif _is_shipping_path(relative_path):
                    errors.append(
                        f"{relative_path.as_posix()}: unsupported file type in an unclassified shipping native path"
                    )

            directory_after, final_error = _safe_stat(directory_path)
            if (
                final_error is not None
                or directory_after is None
                or not _same_snapshot(directory_before, directory_after)
            ):
                errors.append(
                    f"{directory_path.relative_to(root).as_posix()}: native directory changed during scan"
                )
    except OSError:
        errors.append("native source tree could not be traversed safely")
    return checked, errors


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="repository root (defaults to the parent of tools/)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    checked, errors = check_tree(root)
    if errors:
        print(f"native-dependency gate: FAILED ({len(errors)} issue(s), {checked} files checked)")
        for error in errors:
            print(f"- {error}")
        return 1
    print(f"native-dependency gate: OK ({checked} files checked)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
