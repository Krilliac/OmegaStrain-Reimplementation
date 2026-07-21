from __future__ import annotations

import json
import re
import tempfile
import unittest
from collections import Counter
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]

CLASS_LABELS = {
    "canonical decoder": "canonical_decoder",
    "structural envelope only": "structural_envelope_only",
    "passive descriptor only": "passive_descriptor_only",
    "aggregate scanner only": "aggregate_scanner_only",
    "unknown": "unknown",
}
NATIVE_CLASSES = {
    "canonical_decoder",
    "structural_envelope_only",
    "passive_descriptor_only",
}

MATRIX_HEADING = "## 1. Suffix classification matrix"
MATRIX_COLUMNS = ("Suffix", "Class", "Native/tool evidence", "Test evidence")
CPP_PATH_PATTERN = re.compile(
    r"native/(?:include|src|tests)/[A-Za-z0-9_./-]+\.(?:h|cpp)"
)
CPP_PATH_FRAGMENT_PATTERN = re.compile(
    r"native/(?:include|src|tests)/[A-Za-z0-9_./-]+\.(?:h|cpp)"
)
INLINE_CODE_PATTERN = re.compile(r"`(?P<value>[^`\r\n]+)`")
TARGET_FILE_EXPRESSION_PATTERN = re.compile(
    r"\$<TARGET_FILE:(?P<target>[A-Za-z0-9_.+:-]+)>"
)
BOUNDARY_FUNCTION_PATTERN = re.compile(
    r"\b(?P<name>(?:Decode|Inspect)[A-Z0-9][A-Za-z0-9_]*)\s*\("
)
CXX_TOKEN_PATTERN = re.compile(
    r'(?P<raw>(?:u8|u|U|L)?R"(?P<delimiter>[^\s()\\]{0,16})\(.*?\)'
    r'(?P=delimiter)")'
    r'|(?P<string>"(?:\\.|[^"\\])*")'
    r"|(?P<char>'(?:\\.|[^'\\])*')"
    r"|(?P<line_comment>//[^\n]*)"
    r"|(?P<block_comment>/\*.*?\*/)",
    re.DOTALL,
)
CMAKE_BRACKET_COMMENT_PATTERN = re.compile(
    r"#\[(?P<equals>=*)\[.*?\](?P=equals)\]", re.DOTALL
)


@dataclass(frozen=True)
class MatrixRow:
    suffix: str
    decoder_class: str
    native_evidence: str
    test_evidence: str
    line_number: int


@dataclass(frozen=True)
class CMakeInvocation:
    name: str
    body: str


def split_markdown_row(line: str) -> list[str] | None:
    stripped = line.strip()
    if not stripped.startswith("|") or not stripped.endswith("|"):
        return None

    cells: list[str] = []
    cell: list[str] = []
    content = stripped[1:-1]
    cursor = 0
    while cursor < len(content):
        character = content[cursor]
        if character == "\\" and cursor + 1 < len(content) and content[cursor + 1] in {
            "\\",
            "|",
        }:
            cursor += 1
            cell.append(content[cursor])
        elif character == "|":
            cells.append("".join(cell).strip())
            cell = []
        else:
            cell.append(character)
        cursor += 1
    cells.append("".join(cell).strip())
    return cells


def blank_token(value: str) -> str:
    return "".join("\n" if character == "\n" else " " for character in value)


def strip_cpp_comments(text: str) -> str:
    text = re.sub(r"\\\r?\n", "", text)

    def replace(match: re.Match[str]) -> str:
        if match.group("line_comment") is not None or match.group("block_comment") is not None:
            return blank_token(match.group(0))
        return match.group(0)

    return CXX_TOKEN_PATTERN.sub(replace, text)


def cpp_lexical_code_view(text: str) -> str:
    without_comments = strip_cpp_comments(text)

    def replace(match: re.Match[str]) -> str:
        if any(match.group(group) is not None for group in ("raw", "string", "char")):
            return blank_token(match.group(0))
        return match.group(0)

    return CXX_TOKEN_PATTERN.sub(replace, without_comments)


def strip_literal_inactive_cpp(text: str) -> str:
    text = re.sub(r"\\\r?\n", "", text)
    directive_view = cpp_lexical_code_view(text)
    output: list[str] = []
    active = True
    stack: list[dict[str, bool | None]] = []

    def tri_or(left: bool | None, right: bool | None) -> bool | None:
        if left is True or right is True:
            return True
        if left is False and right is False:
            return False
        return None

    def condition(expression: str) -> bool | None:
        expression = expression.replace("&&", " AND ").replace("||", " OR ")
        expression = re.sub(r"!(?!=)", " NOT ", expression)
        return literal_cmake_condition(expression.split())

    text_lines = text.splitlines(keepends=True)
    view_lines = directive_view.splitlines(keepends=True)
    for line, view_line in zip(text_lines, view_lines, strict=True):
        directive = re.match(
            r"^\s*#\s*(if|elif|else|endif)\b(.*)$", view_line, re.IGNORECASE
        )
        if directive is None:
            output.append(line if active else blank_token(line))
            continue

        name = directive.group(1).lower()
        expression = directive.group(2).strip()
        output.append(blank_token(line))
        if name == "if":
            value = condition(expression)
            stack.append({"parent": active, "prior": value})
            active = active and value is not False
        elif name == "elif" and stack:
            frame = stack[-1]
            value = condition(expression)
            prior = frame["prior"]
            active = bool(frame["parent"]) and prior is not True and value is not False
            frame["prior"] = tri_or(prior, value)
        elif name == "else" and stack:
            frame = stack[-1]
            active = bool(frame["parent"]) and frame["prior"] is not True
            frame["prior"] = True
        elif name == "endif" and stack:
            frame = stack.pop()
            active = bool(frame["parent"])
    return "".join(output)


def cpp_code_view(text: str) -> str:
    return cpp_lexical_code_view(strip_literal_inactive_cpp(text))


def cpp_includes(text: str) -> set[str]:
    active_text = strip_literal_inactive_cpp(text)
    without_comments = strip_cpp_comments(active_text)
    code_view = cpp_lexical_code_view(active_text)
    includes: set[str] = set()
    for directive in re.finditer(r"(?m)^\s*#\s*include\b", code_view):
        line_end = without_comments.find("\n", directive.start())
        if line_end < 0:
            line_end = len(without_comments)
        line = without_comments[directive.start():line_end]
        match = re.match(r'^\s*#\s*include\s*[<"]([^>"\r\n]+)[>"]', line)
        if match is not None:
            includes.add(match.group(1))
    return includes


def strip_cmake_comments(text: str) -> str:
    text = CMAKE_BRACKET_COMMENT_PATTERN.sub(
        lambda match: blank_token(match.group(0)), text
    )
    output: list[str] = []
    for line in text.splitlines(keepends=True):
        in_quote = False
        escaped = False
        for index, character in enumerate(line):
            if escaped:
                escaped = False
                continue
            if character == "\\" and in_quote:
                escaped = True
                continue
            if character == '"':
                in_quote = not in_quote
            elif character == "#" and not in_quote:
                ending = "\n" if line.endswith("\n") else ""
                output.append(line[:index] + ending)
                break
        else:
            output.append(line)
    return "".join(output)


def cmake_invocations(text: str) -> tuple[list[CMakeInvocation], list[str]]:
    text = strip_cmake_comments(text)
    invocations: list[CMakeInvocation] = []
    errors: list[str] = []
    cursor = 0
    while cursor < len(text):
        if not (text[cursor].isalpha() or text[cursor] == "_"):
            cursor += 1
            continue
        name_start = cursor
        cursor += 1
        while cursor < len(text) and (text[cursor].isalnum() or text[cursor] == "_"):
            cursor += 1
        name = text[name_start:cursor]
        opening = cursor
        while opening < len(text) and text[opening].isspace():
            opening += 1
        if opening >= len(text) or text[opening] != "(":
            continue

        depth = 1
        body_start = opening + 1
        position = body_start
        in_quote = False
        escaped = False
        while position < len(text):
            character = text[position]
            if escaped:
                escaped = False
            elif character == "\\" and in_quote:
                escaped = True
            elif character == '"':
                in_quote = not in_quote
            elif not in_quote and character == "(":
                depth += 1
            elif not in_quote and character == ")":
                depth -= 1
                if depth == 0:
                    break
            position += 1
        if depth != 0:
            errors.append(f"CMake invocation {name} has unbalanced parentheses")
            break
        invocations.append(
            CMakeInvocation(name=name.lower(), body=text[body_start:position])
        )
        cursor = position + 1
    return invocations, errors


def cmake_words(body: str) -> list[str]:
    return [
        quoted if quoted else bare
        for quoted, bare in re.findall(r'"((?:\\.|[^"\\])*)"|([^\s]+)', body)
    ]


def literal_cmake_condition(words: list[str]) -> bool | None:
    tokens = re.findall(r"\(|\)|[^\s()]+", " ".join(words))
    cursor = 0

    def constant(token: str) -> bool | None:
        normalized = token.upper()
        if normalized in {"0", "OFF", "NO", "FALSE", "N", "IGNORE", "NOTFOUND", ""}:
            return False
        if normalized.endswith("-NOTFOUND"):
            return False
        if normalized in {"1", "ON", "YES", "TRUE", "Y"}:
            return True
        try:
            return int(token, 0) != 0
        except ValueError:
            try:
                return float(token) != 0
            except ValueError:
                return None

    def parse_primary() -> bool | None:
        nonlocal cursor
        if cursor >= len(tokens):
            return None
        if tokens[cursor] == "(":
            cursor += 1
            value = parse_or()
            if cursor >= len(tokens) or tokens[cursor] != ")":
                return None
            cursor += 1
            return value
        value = constant(tokens[cursor])
        cursor += 1
        return value

    def parse_not() -> bool | None:
        nonlocal cursor
        if cursor < len(tokens) and tokens[cursor].upper() == "NOT":
            cursor += 1
            value = parse_not()
            return None if value is None else not value
        return parse_primary()

    def tri_and(left: bool | None, right: bool | None) -> bool | None:
        if left is False or right is False:
            return False
        if left is True and right is True:
            return True
        return None

    def tri_or(left: bool | None, right: bool | None) -> bool | None:
        if left is True or right is True:
            return True
        if left is False and right is False:
            return False
        return None

    def parse_and() -> bool | None:
        nonlocal cursor
        value = parse_not()
        while cursor < len(tokens) and tokens[cursor].upper() == "AND":
            cursor += 1
            value = tri_and(value, parse_not())
        return value

    def parse_or() -> bool | None:
        nonlocal cursor
        value = parse_and()
        while cursor < len(tokens) and tokens[cursor].upper() == "OR":
            cursor += 1
            value = tri_or(value, parse_and())
        return value

    result = parse_or()
    return result if cursor == len(tokens) else None


def active_cmake_invocations(
    invocations: list[CMakeInvocation],
) -> tuple[list[CMakeInvocation], list[str]]:
    active_invocations: list[CMakeInvocation] = []
    errors: list[str] = []
    active = True
    if_stack: list[dict[str, bool | None]] = []
    while_stack: list[bool] = []
    definition_stack: list[tuple[bool, int, int, str]] = []

    def tri_or(left: bool | None, right: bool | None) -> bool | None:
        if left is True or right is True:
            return True
        if left is False and right is False:
            return False
        return None

    for invocation in invocations:
        words = cmake_words(invocation.body)
        if invocation.name in {"function", "macro"}:
            definition_stack.append(
                (active, len(if_stack), len(while_stack), invocation.name)
            )
            active = False
            continue
        if invocation.name in {"endfunction", "endmacro"}:
            expected = invocation.name.removeprefix("end")
            if not definition_stack or definition_stack[-1][3] != expected:
                errors.append(f"CMake has unmatched {invocation.name} invocation")
                continue
            parent_active, if_depth, while_depth, _ = definition_stack.pop()
            if len(if_stack) != if_depth:
                errors.append(
                    f"CMake {expected} body has unbalanced conditional blocks"
                )
                del if_stack[if_depth:]
            if len(while_stack) != while_depth:
                errors.append(f"CMake {expected} body has unbalanced while blocks")
                del while_stack[while_depth:]
            active = parent_active
            continue
        if invocation.name == "if":
            condition = literal_cmake_condition(words)
            if_stack.append({"parent": active, "prior": condition})
            active = active and condition is not False
            continue
        if invocation.name == "elseif":
            if not if_stack:
                errors.append("CMake has unmatched elseif invocation")
                continue
            frame = if_stack[-1]
            condition = literal_cmake_condition(words)
            prior = frame["prior"]
            active = bool(frame["parent"]) and prior is not True and condition is not False
            frame["prior"] = tri_or(prior, condition)
            continue
        if invocation.name == "else":
            if not if_stack:
                errors.append("CMake has unmatched else invocation")
                continue
            frame = if_stack[-1]
            active = bool(frame["parent"]) and frame["prior"] is not True
            frame["prior"] = True
            continue
        if invocation.name == "endif":
            if not if_stack:
                errors.append("CMake has unmatched endif invocation")
                continue
            frame = if_stack.pop()
            active = bool(frame["parent"])
            continue
        if invocation.name == "while":
            while_stack.append(active)
            active = active and literal_cmake_condition(words) is not False
            continue
        if invocation.name == "endwhile":
            if not while_stack:
                errors.append("CMake has unmatched endwhile invocation")
                continue
            active = while_stack.pop()
            continue
        if active:
            active_invocations.append(invocation)

    if if_stack:
        errors.append("CMake has unterminated conditional block")
    if definition_stack:
        errors.append("CMake has unterminated function or macro definition")
    if while_stack:
        errors.append("CMake has unterminated while block")
    return active_invocations, errors


def invocation_paths(invocation: CMakeInvocation, prefix: str) -> set[str]:
    pattern = re.compile(
        rf"{re.escape(prefix)}[A-Za-z0-9_./-]+\.cpp"
    )
    return {word for word in cmake_words(invocation.body) if pattern.fullmatch(word)}


def compiled_target_source_paths(
    invocation: CMakeInvocation, prefix: str
) -> set[str]:
    pattern = re.compile(rf"{re.escape(prefix)}[A-Za-z0-9_./-]+\.cpp")
    visibility: str | None = None
    paths: set[str] = set()
    for word in cmake_words(invocation.body)[1:]:
        upper = word.upper()
        if upper in {"PRIVATE", "PUBLIC", "INTERFACE"}:
            visibility = upper
        elif visibility in {"PRIVATE", "PUBLIC"} and pattern.fullmatch(word):
            paths.add(word)
    return paths


def is_canonical_repository_path(value: str) -> bool:
    path = PurePosixPath(value)
    return (
        not path.is_absolute()
        and value == path.as_posix()
        and all(part not in {"", ".", ".."} for part in path.parts)
    )


def repository_path_status(
    root: Path, relative: str, expected_kind: str = "file"
) -> str:
    if not is_canonical_repository_path(relative):
        return "unsafe"
    candidate = root.joinpath(*PurePosixPath(relative).parts)
    probe = root
    for part in PurePosixPath(relative).parts:
        probe /= part
        try:
            if probe.is_symlink() or (
                hasattr(probe, "is_junction") and probe.is_junction()
            ):
                return "unsafe"
        except OSError:
            return "missing"
    try:
        resolved_root = root.resolve(strict=True)
        resolved_candidate = candidate.resolve(strict=True)
    except OSError:
        return "missing"
    if not resolved_candidate.is_relative_to(resolved_root):
        return "unsafe"
    if expected_kind == "directory":
        return "ok" if resolved_candidate.is_dir() else "missing"
    return "ok" if resolved_candidate.is_file() else "missing"


def normalize_decoder_class(value: str) -> str | None:
    normalized = " ".join(value.strip().lower().split())
    for label, decoder_class in CLASS_LABELS.items():
        if normalized == label:
            return decoder_class
        if normalized.startswith(f"{label} (") and normalized.endswith(")"):
            return decoder_class
    return None


def parse_suffix_cell(value: str) -> str | None:
    token_pattern = re.compile(r"`(\.[A-Za-z0-9]+)`")
    aliases = token_pattern.findall(value)
    normalized = {alias.lower() for alias in aliases}
    residue = token_pattern.sub("", value)
    if (
        not aliases
        or len(normalized) != 1
        or re.fullmatch(r"[\s()]*", residue) is None
    ):
        return None
    return normalized.pop()


def parse_matrix(text: str) -> tuple[dict[str, MatrixRow], list[str]]:
    lines = text.splitlines()
    errors: list[str] = []
    try:
        heading_index = lines.index(MATRIX_HEADING)
    except ValueError:
        return {}, [f"decoder coverage matrix is missing heading {MATRIX_HEADING!r}"]

    cursor = heading_index + 1
    while cursor < len(lines) and not lines[cursor].strip():
        cursor += 1
    if cursor >= len(lines):
        return {}, ["decoder coverage matrix is missing its Markdown table"]

    header = split_markdown_row(lines[cursor])
    if header != list(MATRIX_COLUMNS):
        errors.append(
            "decoder coverage matrix has unexpected columns: "
            f"expected {list(MATRIX_COLUMNS)!r}, found {header!r}"
        )
    cursor += 1

    if cursor >= len(lines):
        errors.append("decoder coverage matrix is missing its separator row")
        return {}, errors
    separator = split_markdown_row(lines[cursor])
    if separator is None or len(separator) != len(MATRIX_COLUMNS) or any(
        re.fullmatch(r":?-{3,}:?", cell) is None for cell in separator
    ):
        errors.append("decoder coverage matrix has a malformed separator row")
    cursor += 1

    rows: dict[str, MatrixRow] = {}
    while cursor < len(lines) and lines[cursor].strip():
        line_number = cursor + 1
        cells = split_markdown_row(lines[cursor])
        if cells is None:
            errors.append(
                f"decoder coverage matrix line {line_number} is not a Markdown table row"
            )
            cursor += 1
            continue
        if len(cells) != len(MATRIX_COLUMNS):
            errors.append(
                f"decoder coverage matrix line {line_number} must contain exactly "
                f"{len(MATRIX_COLUMNS)} cells; found {len(cells)}"
            )
            cursor += 1
            continue

        suffix = parse_suffix_cell(cells[0])
        if suffix is None:
            errors.append(
                f"decoder coverage matrix line {line_number} has a malformed suffix cell: "
                f"{cells[0]!r}"
            )
            cursor += 1
            continue
        decoder_class = normalize_decoder_class(cells[1])
        if decoder_class is None:
            errors.append(
                f"decoder coverage matrix line {line_number} has an unsupported class: "
                f"{cells[1]!r}"
            )
            cursor += 1
            continue
        if not cells[2] or not cells[3]:
            errors.append(
                f"decoder coverage matrix line {line_number} has an empty evidence cell"
            )
            cursor += 1
            continue
        if suffix in rows:
            errors.append(
                f"decoder coverage matrix has duplicate suffix {suffix} at lines "
                f"{rows[suffix].line_number} and {line_number}"
            )
            cursor += 1
            continue
        rows[suffix] = MatrixRow(
            suffix=suffix,
            decoder_class=decoder_class,
            native_evidence=cells[2],
            test_evidence=cells[3],
            line_number=line_number,
        )
        cursor += 1

    if not rows:
        errors.append("decoder coverage matrix contains no data rows")
    return rows, errors


def parse_catalog(path: Path) -> tuple[dict[str, str], list[str]]:
    errors: list[str] = []
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return {}, ["decoder dossier catalog cannot be read as canonical UTF-8 JSON"]

    families = document.get("families") if isinstance(document, dict) else None
    if not isinstance(families, list):
        return {}, ["decoder dossier catalog field 'families' must be an array"]

    matrix: dict[str, str] = {}
    for index, family in enumerate(families):
        if not isinstance(family, dict) or family.get("scope") != "decoder_matrix":
            continue
        suffix = family.get("suffix")
        decoder_class = family.get("decoder_class")
        if not isinstance(suffix, str) or re.fullmatch(r"\.[a-z0-9]+", suffix) is None:
            errors.append(
                f"decoder dossier catalog family {index} has an invalid matrix suffix"
            )
            continue
        if decoder_class not in CLASS_LABELS.values():
            errors.append(
                f"decoder dossier catalog family {suffix} has an unsupported class: "
                f"{decoder_class!r}"
            )
            continue
        if suffix in matrix:
            errors.append(f"decoder dossier catalog has duplicate suffix {suffix}")
            continue
        matrix[suffix] = decoder_class
    return matrix, errors


def target_sources(
    invocations: list[CMakeInvocation], target: str
) -> tuple[set[str], list[str]]:
    definitions = []
    sources: set[str] = set()
    for invocation in invocations:
        words = cmake_words(invocation.body)
        if not words or words[0] != target:
            continue
        if invocation.name == "add_library":
            definitions.append(invocation)
            if "STATIC" not in words[1:]:
                continue
            sources.update(invocation_paths(invocation, "native/src/"))
        elif invocation.name == "target_sources":
            sources.update(compiled_target_source_paths(invocation, "native/src/"))

    errors: list[str] = []
    static_definitions = [
        invocation
        for invocation in definitions
        if "STATIC" in cmake_words(invocation.body)[1:]
    ]
    if len(static_definitions) != 1:
        errors.append(
            f"CMake must define exactly one static {target} target; "
            f"found {len(static_definitions)}"
        )
    return sources, errors


def registered_cmake_surface(
    invocations: list[CMakeInvocation],
) -> tuple[set[str], set[str]]:
    source_paths: set[str] = set()
    executable_sources: dict[str, set[str]] = {}
    supplemental_sources: dict[str, set[str]] = {}
    tested_targets: set[str] = set()

    for invocation in invocations:
        words = cmake_words(invocation.body)
        if not words:
            continue
        if invocation.name in {"add_library", "add_executable"}:
            source_paths.update(invocation_paths(invocation, "native/src/"))
        elif invocation.name == "target_sources":
            source_paths.update(
                compiled_target_source_paths(invocation, "native/src/")
            )
        if invocation.name == "add_executable":
            executable_sources.setdefault(words[0], set()).update(
                invocation_paths(invocation, "native/tests/")
            )
        elif invocation.name == "target_sources":
            supplemental_sources.setdefault(words[0], set()).update(
                compiled_target_source_paths(invocation, "native/tests/")
            )
        elif invocation.name == "add_test":
            upper_words = [word.upper() for word in words]
            if "COMMAND" in upper_words:
                command_index = upper_words.index("COMMAND") + 1
                if command_index < len(words):
                    command = words[command_index]
                    target_file = TARGET_FILE_EXPRESSION_PATTERN.fullmatch(command)
                    tested_targets.add(
                        target_file["target"] if target_file is not None else command
                    )
            elif len(words) >= 2:
                tested_targets.add(words[1])

    registered_tests: set[str] = set()
    for target in tested_targets:
        if target not in executable_sources:
            continue
        registered_tests.update(executable_sources.get(target, set()))
        registered_tests.update(supplemental_sources.get(target, set()))
    return source_paths, registered_tests


def boundary_suffix(function_name: str, known_suffixes: set[str]) -> str | None:
    remainder = re.sub(r"^(?:Decode|Inspect)", "", function_name)
    for suffix in sorted(known_suffixes, key=lambda value: (-len(value), value)):
        token = suffix.removeprefix(".")
        if not remainder.casefold().startswith(token.casefold()):
            continue
        if len(remainder) == len(token) or not remainder[len(token)].islower():
            return suffix
    return None


def public_retail_boundaries(
    root: Path, known_suffixes: set[str]
) -> tuple[dict[Path, set[str]], list[str]]:
    header_root = root / "native" / "include" / "omega" / "retail"
    boundaries: dict[Path, set[str]] = {}
    errors: list[str] = []
    seen_functions: dict[str, Path] = {}
    header_root_relative = header_root.relative_to(root).as_posix()
    if repository_path_status(root, header_root_relative, "directory") != "ok":
        return {}, ["public retail header directory is missing or link-like"]

    for header in sorted(header_root.glob("*.h")):
        relative = header.relative_to(root)
        relative_text = relative.as_posix()
        if repository_path_status(root, relative_text) != "ok":
            errors.append(
                f"public retail header {relative_text} is missing or link-like"
            )
            continue
        try:
            text = cpp_code_view(header.read_text(encoding="utf-8"))
        except (OSError, UnicodeError):
            errors.append(
                f"public retail header {relative_text} cannot be read as UTF-8 text"
            )
            continue
        suffixes: set[str] = set()
        for match in BOUNDARY_FUNCTION_PATTERN.finditer(text):
            function_name = match["name"]
            previous = seen_functions.get(function_name)
            if previous is not None and previous != relative:
                errors.append(
                    f"public retail boundary {function_name} is declared in both "
                    f"{previous.as_posix()} and {relative.as_posix()}"
                )
            elif previous is None:
                seen_functions[function_name] = relative
            suffix = boundary_suffix(function_name, known_suffixes)
            if suffix is None:
                errors.append(
                    f"public retail boundary {function_name} in {relative.as_posix()} "
                    "does not expose a mechanical suffix token"
                )
                continue
            suffixes.add(suffix)
        if suffixes:
            boundaries[relative] = suffixes
    return boundaries, errors


def format_suffixes(values: set[str]) -> str:
    return ", ".join(sorted(values))


def cited_cpp_paths(evidence: str) -> tuple[set[str], bool]:
    paths: set[str] = set()
    valid_ranges: list[tuple[int, int]] = []
    for match in INLINE_CODE_PATTERN.finditer(evidence):
        value = match["value"]
        if CPP_PATH_PATTERN.fullmatch(value) is None:
            continue
        paths.add(value)
        valid_ranges.append((match.start("value"), match.end("value")))

    malformed = any(
        not any(start <= match.start() < end for start, end in valid_ranges)
        for match in CPP_PATH_FRAGMENT_PATTERN.finditer(evidence)
    )
    return paths, malformed


def validate_summary(text: str, rows: dict[str, MatrixRow]) -> list[str]:
    match = re.search(r"(?ms)^Totals observed in this pass:(.*?)(?:\n\s*\n|\Z)", text)
    if match is None:
        return ["decoder coverage matrix is missing its totals summary"]

    reported: dict[str, int] = {}
    errors: list[str] = []
    for count, label in re.findall(r"\*\*(\d+)\s+([^*]+?)\*\*", match.group(1)):
        decoder_class = normalize_decoder_class(" ".join(label.split()))
        if decoder_class is not None:
            if decoder_class in reported:
                errors.append(
                    f"decoder coverage totals repeat class {decoder_class}"
                )
            else:
                reported[decoder_class] = int(count)

    expected = Counter(row.decoder_class for row in rows.values())
    for decoder_class in CLASS_LABELS.values():
        if decoder_class not in reported:
            errors.append(
                f"decoder coverage totals omit class {decoder_class}"
            )
        elif reported[decoder_class] != expected[decoder_class]:
            errors.append(
                f"decoder coverage total for {decoder_class} is "
                f"{reported[decoder_class]}; matrix has {expected[decoder_class]}"
            )
    return errors


def validate_decoder_coverage(root: Path) -> list[str]:
    coverage_path = root / "analysis" / "formats" / "DECODER-COVERAGE.md"
    catalog_path = root / "analysis" / "formats" / "dossiers" / "catalog.json"
    cmake_path = root / "CMakeLists.txt"
    fixed_inputs = (
        ("analysis/formats/DECODER-COVERAGE.md", "decoder coverage document"),
        ("analysis/formats/dossiers/catalog.json", "decoder dossier catalog"),
        ("CMakeLists.txt", "CMake source manifest"),
    )
    for relative, label in fixed_inputs:
        if repository_path_status(root, relative) != "ok":
            return [f"{label} is missing or link-like"]
    try:
        coverage_text = coverage_path.read_text(encoding="utf-8")
    except (OSError, UnicodeError):
        return ["decoder coverage document cannot be read as UTF-8 text"]
    try:
        cmake_text = cmake_path.read_text(encoding="utf-8")
    except (OSError, UnicodeError):
        return ["CMake source manifest cannot be read as UTF-8 text"]

    rows, errors = parse_matrix(coverage_text)
    catalog, catalog_errors = parse_catalog(catalog_path)
    errors.extend(catalog_errors)
    errors.extend(validate_summary(coverage_text, rows))

    missing_from_matrix = set(catalog) - set(rows)
    extra_in_matrix = set(rows) - set(catalog)
    if missing_from_matrix:
        errors.append(
            "decoder coverage matrix is missing catalog suffixes: "
            f"{format_suffixes(missing_from_matrix)}"
        )
    if extra_in_matrix:
        errors.append(
            "decoder coverage matrix has suffixes absent from catalog: "
            f"{format_suffixes(extra_in_matrix)}"
        )
    for suffix in sorted(set(rows) & set(catalog)):
        if rows[suffix].decoder_class != catalog[suffix]:
            errors.append(
                f"decoder coverage class mismatch for {suffix}: "
                f"matrix={rows[suffix].decoder_class}, catalog={catalog[suffix]}"
            )

    boundaries, boundary_errors = public_retail_boundaries(root, set(catalog))
    errors.extend(boundary_errors)
    suffix_headers: dict[str, set[Path]] = {}
    for header, suffixes in boundaries.items():
        for suffix in suffixes:
            suffix_headers.setdefault(suffix, set()).add(header)

    invocations, cmake_errors = cmake_invocations(cmake_text)
    errors.extend(cmake_errors)
    invocations, activity_errors = active_cmake_invocations(invocations)
    errors.extend(activity_errors)
    retail_target_sources, target_errors = target_sources(
        invocations, "omega_retail_formats"
    )
    errors.extend(target_errors)
    all_cmake_sources, registered_tests = registered_cmake_surface(invocations)

    retail_source_root = root / "native" / "src" / "retail"
    source_includes: dict[str, set[str]] = {}
    source_root_status = repository_path_status(
        root, retail_source_root.relative_to(root).as_posix(), "directory"
    )
    if source_root_status != "ok":
        errors.append("retail source directory is missing or link-like")
    for path in sorted(retail_source_root.glob("*.cpp")) if source_root_status == "ok" else []:
        relative = path.relative_to(root).as_posix()
        if repository_path_status(root, relative) != "ok":
            errors.append(f"retail source {relative} is missing or link-like")
            continue
        try:
            source_includes[relative] = cpp_includes(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError):
            errors.append(f"retail source {relative} cannot be read as UTF-8 text")
    actual_retail_sources = set(source_includes)
    unregistered_sources = actual_retail_sources - retail_target_sources
    if unregistered_sources:
        errors.append(
            "omega_retail_formats omits retail sources: "
            f"{', '.join(sorted(unregistered_sources))}"
        )
    missing_sources = retail_target_sources - actual_retail_sources
    if missing_sources:
        errors.append(
            "omega_retail_formats references missing retail sources: "
            f"{', '.join(sorted(missing_sources))}"
        )

    native_test_root = root / "native" / "tests"
    test_includes: dict[str, set[str]] = {}
    test_root_status = repository_path_status(
        root, native_test_root.relative_to(root).as_posix(), "directory"
    )
    if test_root_status != "ok":
        errors.append("native test directory is missing or link-like")
    for path in sorted(native_test_root.glob("*.cpp")) if test_root_status == "ok" else []:
        relative = path.relative_to(root).as_posix()
        if repository_path_status(root, relative) != "ok":
            errors.append(f"native test {relative} is missing or link-like")
            continue
        try:
            test_includes[relative] = cpp_includes(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError):
            errors.append(f"native test {relative} cannot be read as UTF-8 text")

    public_include_root = root / "native" / "include"
    for header in sorted(boundaries, key=lambda item: item.as_posix()):
        public_include = (root / header).relative_to(public_include_root).as_posix()
        implementing_sources = {
            path for path, includes in source_includes.items() if public_include in includes
        }
        if not implementing_sources:
            errors.append(
                f"public retail header {header.as_posix()} has no implementing retail source"
            )
        for source in sorted(implementing_sources):
            if source not in retail_target_sources:
                errors.append(
                    f"public retail implementation {source} is not registered in "
                    "omega_retail_formats"
                )

        covering_tests = {
            path for path, includes in test_includes.items() if public_include in includes
        }
        if not covering_tests:
            errors.append(
                f"public retail header {header.as_posix()} has no focused native test include"
            )
        elif not (covering_tests & registered_tests):
            errors.append(
                f"public retail header {header.as_posix()} has no CMake-registered native test"
            )

    for suffix in sorted(suffix_headers):
        row = rows.get(suffix)
        if row is None:
            errors.append(
                f"public retail suffix {suffix} has no decoder coverage matrix row"
            )
        elif row.decoder_class not in NATIVE_CLASSES:
            errors.append(
                f"public retail suffix {suffix} is classified as non-native "
                f"{row.decoder_class}"
            )
        else:
            expected_headers = {
                header.as_posix() for header in suffix_headers[suffix]
            }
            cited_headers, _ = cited_cpp_paths(row.native_evidence)
            missing_headers = expected_headers - cited_headers
            if missing_headers:
                errors.append(
                    f"matrix row {suffix} omits public boundary headers: "
                    f"{', '.join(sorted(missing_headers))}"
                )

    for suffix, row in sorted(rows.items()):
        native_paths, malformed_native_path = cited_cpp_paths(row.native_evidence)
        test_paths, malformed_test_path = cited_cpp_paths(row.test_evidence)
        if malformed_native_path or malformed_test_path:
            errors.append(
                f"matrix row {suffix} cites malformed or non-repository C++ "
                "evidence path"
            )
        paths = native_paths | test_paths
        safe_paths: set[str] = set()
        for path in sorted(paths):
            status = repository_path_status(root, path)
            if status == "unsafe":
                errors.append(
                    f"matrix row {suffix} cites unsafe non-repository path {path}"
                )
                continue
            safe_paths.add(path)
            if status == "missing":
                errors.append(f"matrix row {suffix} cites missing path {path}")

        if row.decoder_class in NATIVE_CLASSES:
            headers = {
                path
                for path in native_paths & safe_paths
                if path.startswith("native/include/")
            }
            tests = {
                path
                for path in test_paths & safe_paths
                if path.startswith("native/tests/")
            }
            if not headers:
                errors.append(f"native matrix row {suffix} cites no public header")
            if not tests:
                errors.append(f"native matrix row {suffix} cites no focused test")
            if suffix not in suffix_headers and not any(
                not path.startswith("native/include/omega/retail/") for path in headers
            ):
                errors.append(
                    f"native matrix row {suffix} has no matching public boundary declaration"
                )

            header_includes = {
                path.removeprefix("native/include/") for path in headers
            }
            cited_covering_tests = {
                test
                for test in tests
                if header_includes & test_includes.get(test, set())
            }
            if headers and tests and not cited_covering_tests:
                errors.append(
                    f"native matrix row {suffix} cites no test that includes one of its "
                    "cited public headers"
                )

            for header in sorted(headers):
                if Path(header) not in boundaries:
                    continue
                public_include = header.removeprefix("native/include/")
                implementing_sources = {
                    source
                    for source, includes in source_includes.items()
                    if public_include in includes
                }
                if not (native_paths & implementing_sources):
                    errors.append(
                        f"matrix row {suffix} cites no implementing source for public "
                        f"boundary header {header}"
                    )
                covering_tests = {
                    test
                    for test, includes in test_includes.items()
                    if public_include in includes
                }
                if not (tests & covering_tests):
                    errors.append(
                        f"matrix row {suffix} cites no focused test for public boundary "
                        f"header {header}"
                    )

        for path in sorted(safe_paths):
            if path.startswith("native/src/") and path not in all_cmake_sources:
                errors.append(
                    f"matrix row {suffix} cites unregistered source {path}"
                )
            if path.startswith("native/tests/") and path not in registered_tests:
                errors.append(f"matrix row {suffix} cites unregistered test {path}")
            if path.startswith("native/include/omega/retail/"):
                header_suffixes = boundaries.get(Path(path))
                if header_suffixes is not None and suffix not in header_suffixes:
                    errors.append(
                        f"matrix row {suffix} cites retail header {path} whose public "
                        f"boundaries map to {format_suffixes(header_suffixes)}"
                    )

    return errors


def synthetic_document(rows: list[str], counts: dict[str, int] | None = None) -> str:
    counts = counts or {
        "canonical_decoder": 1,
        "structural_envelope_only": 0,
        "passive_descriptor_only": 0,
        "aggregate_scanner_only": 0,
        "unknown": 0,
    }
    summary = ", ".join(
        f"**{counts[decoder_class]} {label}**"
        for label, decoder_class in CLASS_LABELS.items()
    )
    return "\n".join(
        (
            "# Synthetic decoder coverage",
            "",
            MATRIX_HEADING,
            "",
            "| Suffix | Class | Native/tool evidence | Test evidence |",
            "|---|---|---|---|",
            *rows,
            "",
            f"Totals observed in this pass: {summary}.",
            "",
        )
    )


def write_synthetic_repository(root: Path, rows: list[str] | None = None) -> None:
    coverage = root / "analysis" / "formats" / "DECODER-COVERAGE.md"
    catalog = root / "analysis" / "formats" / "dossiers" / "catalog.json"
    header = root / "native" / "include" / "omega" / "retail" / "foo_decoder.h"
    source = root / "native" / "src" / "retail" / "foo_decoder.cpp"
    test = root / "native" / "tests" / "foo_decoder_tests.cpp"
    for parent in (coverage.parent, catalog.parent, header.parent, source.parent, test.parent):
        parent.mkdir(parents=True, exist_ok=True)

    row = (
        "| `.foo` | canonical decoder | `DecodeFoo` "
        "(`native/include/omega/retail/foo_decoder.h`, "
        "`native/src/retail/foo_decoder.cpp`) | "
        "`native/tests/foo_decoder_tests.cpp` |"
    )
    coverage.write_text(synthetic_document(rows if rows is not None else [row]), encoding="utf-8")
    catalog.write_text(
        json.dumps(
            {
                "families": [
                    {
                        "suffix": ".foo",
                        "scope": "decoder_matrix",
                        "decoder_class": "canonical_decoder",
                    }
                ]
            }
        ),
        encoding="utf-8",
    )
    header.write_text("int DecodeFoo(const void* bytes);\n", encoding="utf-8")
    source.write_text('#include "omega/retail/foo_decoder.h"\n', encoding="utf-8")
    test.write_text('#include "omega/retail/foo_decoder.h"\n', encoding="utf-8")
    (root / "CMakeLists.txt").write_text(
        "\n".join(
            (
                "add_library(omega_retail_formats STATIC",
                "    native/src/retail/foo_decoder.cpp",
                ")",
                "add_executable(foo_tests",
                "    native/tests/foo_decoder_tests.cpp",
                ")",
                "add_test(NAME foo_tests COMMAND foo_tests)",
                "",
            )
        ),
        encoding="utf-8",
    )


class DecoderCoverageContractTests(unittest.TestCase):
    def test_repository_decoder_coverage_matches_public_native_surface(self) -> None:
        self.assertEqual(validate_decoder_coverage(REPOSITORY_ROOT), [])

    def test_generated_minimal_repository_satisfies_contract(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_synthetic_repository(root)
            self.assertEqual(validate_decoder_coverage(root), [])

    def test_every_public_boundary_requires_row_source_and_test_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_synthetic_repository(root)
            secondary_header = (
                root
                / "native/include/omega/retail/foo_secondary_descriptor.h"
            )
            secondary_source = (
                root / "native/src/retail/foo_secondary_descriptor.cpp"
            )
            secondary_test = (
                root / "native/tests/foo_secondary_descriptor_tests.cpp"
            )
            secondary_header.write_text(
                "int InspectFooSecondary(const void* bytes);\n",
                encoding="utf-8",
            )
            secondary_source.write_text(
                '#include "omega/retail/foo_secondary_descriptor.h"\n',
                encoding="utf-8",
            )
            secondary_test.write_text(
                '#include "omega/retail/foo_secondary_descriptor.h"\n',
                encoding="utf-8",
            )
            (root / "CMakeLists.txt").write_text(
                "\n".join(
                    (
                        "add_library(omega_retail_formats STATIC",
                        "    native/src/retail/foo_decoder.cpp",
                        "    native/src/retail/foo_secondary_descriptor.cpp",
                        ")",
                        "add_executable(foo_tests ",
                        "    native/tests/foo_decoder_tests.cpp)",
                        "add_test(NAME foo_tests COMMAND foo_tests)",
                        "add_executable(foo_secondary_tests",
                        "    native/tests/foo_secondary_descriptor_tests.cpp)",
                        "add_test(NAME foo_secondary_tests COMMAND foo_secondary_tests)",
                        "",
                    )
                ),
                encoding="utf-8",
            )

            missing_boundary = (
                "matrix row .foo omits public boundary headers: "
                "native/include/omega/retail/foo_secondary_descriptor.h"
            )
            self.assertEqual(validate_decoder_coverage(root), [missing_boundary])

            missing_source_evidence = (
                "`DecodeFoo` and `InspectFooSecondary` "
                "(`native/include/omega/retail/foo_decoder.h`, "
                "`native/src/retail/foo_decoder.cpp`, "
                "`native/include/omega/retail/foo_secondary_descriptor.h`)"
            )
            coverage = root / "analysis/formats/DECODER-COVERAGE.md"
            missing_source_row = (
                f"| `.foo` | canonical decoder | {missing_source_evidence} | "
                "`native/tests/foo_decoder_tests.cpp`, "
                "`native/tests/foo_secondary_descriptor_tests.cpp` |"
            )
            coverage.write_text(
                synthetic_document([missing_source_row]), encoding="utf-8"
            )
            self.assertEqual(
                validate_decoder_coverage(root),
                [
                    "matrix row .foo cites no implementing source for public "
                    "boundary header "
                    "native/include/omega/retail/foo_secondary_descriptor.h"
                ],
            )

            native_evidence = (
                "`DecodeFoo` and `InspectFooSecondary` "
                "(`native/include/omega/retail/foo_decoder.h`, "
                "`native/src/retail/foo_decoder.cpp`, "
                "`native/include/omega/retail/foo_secondary_descriptor.h`, "
                "`native/src/retail/foo_secondary_descriptor.cpp`)"
            )
            missing_test_row = (
                f"| `.foo` | canonical decoder | {native_evidence} | "
                "`native/tests/foo_decoder_tests.cpp` |"
            )
            coverage.write_text(
                synthetic_document([missing_test_row]), encoding="utf-8"
            )
            self.assertEqual(
                validate_decoder_coverage(root),
                [
                    "matrix row .foo cites no focused test for public boundary "
                    "header "
                    "native/include/omega/retail/foo_secondary_descriptor.h"
                ],
            )

            complete_row = (
                f"| `.foo` | canonical decoder | {native_evidence} | "
                "`native/tests/foo_decoder_tests.cpp`, "
                "`native/tests/foo_secondary_descriptor_tests.cpp` |"
            )
            coverage.write_text(
                synthetic_document([complete_row]), encoding="utf-8"
            )
            self.assertEqual(validate_decoder_coverage(root), [])

    def test_malformed_row_has_stable_failure_message(self) -> None:
        text = synthetic_document(
            ["| `.foo` | canonical decoder | evidence only |"]
        )
        _, errors = parse_matrix(text)
        self.assertEqual(
            errors,
            [
                "decoder coverage matrix line 7 must contain exactly 4 cells; found 3",
                "decoder coverage matrix contains no data rows",
            ],
        )

    def test_duplicate_row_has_stable_failure_message(self) -> None:
        row = "| `.foo` | canonical decoder | evidence | test |"
        _, errors = parse_matrix(synthetic_document([row, row]))
        self.assertEqual(
            errors,
            ["decoder coverage matrix has duplicate suffix .foo at lines 7 and 8"],
        )

    def test_missing_row_is_detected_against_catalog_and_public_header(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_synthetic_repository(root, rows=[])
            errors = validate_decoder_coverage(root)
            self.assertEqual(
                errors,
                [
                    "decoder coverage matrix contains no data rows",
                    "decoder coverage total for canonical_decoder is 1; matrix has 0",
                    "decoder coverage matrix is missing catalog suffixes: .foo",
                    "public retail suffix .foo has no decoder coverage matrix row",
                ],
            )

    def test_source_and_test_registration_drift_is_detected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_synthetic_repository(root)
            (root / "CMakeLists.txt").write_text(
                "add_library(omega_retail_formats STATIC\n)\n",
                encoding="utf-8",
            )
            errors = validate_decoder_coverage(root)
            self.assertIn(
                "omega_retail_formats omits retail sources: "
                "native/src/retail/foo_decoder.cpp",
                errors,
            )
            self.assertIn(
                "public retail implementation native/src/retail/foo_decoder.cpp is "
                "not registered in omega_retail_formats",
                errors,
            )
            self.assertIn(
                "public retail header native/include/omega/retail/foo_decoder.h "
                "has no CMake-registered native test",
                errors,
            )

    def test_commented_cmake_paths_do_not_count_and_one_line_forms_work(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_synthetic_repository(root)
            (root / "CMakeLists.txt").write_text(
                "\n".join(
                    (
                        "add_library(omega_retail_formats STATIC",
                        "    # native/src/retail/foo_decoder.cpp",
                        ")",
                        "# add_executable(foo_tests native/tests/foo_decoder_tests.cpp)",
                        "# add_test(NAME foo_tests COMMAND foo_tests)",
                        "",
                    )
                ),
                encoding="utf-8",
            )
            errors = validate_decoder_coverage(root)
            self.assertIn(
                "omega_retail_formats omits retail sources: "
                "native/src/retail/foo_decoder.cpp",
                errors,
            )
            self.assertIn(
                "matrix row .foo cites unregistered test "
                "native/tests/foo_decoder_tests.cpp",
                errors,
            )

            (root / "CMakeLists.txt").write_text(
                "\n".join(
                    (
                        "add_library(omega_retail_formats STATIC "
                        "native/src/retail/foo_decoder.cpp)",
                        "add_executable(foo_tests native/tests/foo_decoder_tests.cpp)",
                        "add_test(NAME foo_tests COMMAND foo_tests)",
                        "",
                    )
                ),
                encoding="utf-8",
            )
            self.assertEqual(validate_decoder_coverage(root), [])

    def test_inactive_or_nonexistent_cmake_targets_do_not_register_tests(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_synthetic_repository(root)
            cmake = root / "CMakeLists.txt"
            cmake.write_text(
                "\n".join(
                    (
                        "add_library(omega_retail_formats STATIC "
                        "native/src/retail/foo_decoder.cpp)",
                        "target_sources(ghost_tests PRIVATE "
                        "native/tests/foo_decoder_tests.cpp)",
                        "add_test(NAME ghost_tests COMMAND ghost_tests)",
                        "",
                    )
                ),
                encoding="utf-8",
            )
            self.assertIn(
                "matrix row .foo cites unregistered test "
                "native/tests/foo_decoder_tests.cpp",
                validate_decoder_coverage(root),
            )

            cmake.write_text(
                "\n".join(
                    (
                        "if(FALSE)",
                        "  add_library(omega_retail_formats STATIC "
                        "native/src/retail/foo_decoder.cpp)",
                        "  add_executable(foo_tests "
                        "native/tests/foo_decoder_tests.cpp)",
                        "  add_test(NAME foo_tests COMMAND foo_tests)",
                        "endif()",
                        "",
                    )
                ),
                encoding="utf-8",
            )
            self.assertIn(
                "CMake must define exactly one static omega_retail_formats target; found 0",
                validate_decoder_coverage(root),
            )

            cmake.write_text(
                "\n".join(
                    (
                        "while(FALSE)",
                        "  add_library(omega_retail_formats STATIC "
                        "native/src/retail/foo_decoder.cpp)",
                        "  add_executable(foo_tests "
                        "native/tests/foo_decoder_tests.cpp)",
                        "  add_test(NAME foo_tests COMMAND foo_tests)",
                        "endwhile()",
                        "",
                    )
                ),
                encoding="utf-8",
            )
            self.assertIn(
                "CMake must define exactly one static omega_retail_formats target; found 0",
                validate_decoder_coverage(root),
            )

    def test_generator_expression_does_not_count_as_direct_test_registration(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_synthetic_repository(root)
            (root / "native/tests/other.cpp").write_text(
                "int main() { return 0; }\n", encoding="utf-8"
            )
            (root / "CMakeLists.txt").write_text(
                "\n".join(
                    (
                        "add_library(omega_retail_formats STATIC "
                        "native/src/retail/foo_decoder.cpp)",
                        "add_executable(foo_tests native/tests/other.cpp "
                        "$<$<BOOL:0>:native/tests/foo_decoder_tests.cpp>)",
                        "add_test(NAME foo_tests COMMAND foo_tests)",
                        "",
                    )
                ),
                encoding="utf-8",
            )
            self.assertIn(
                "matrix row .foo cites unregistered test "
                "native/tests/foo_decoder_tests.cpp",
                validate_decoder_coverage(root),
            )

    def test_target_file_generator_expression_registers_test_target(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_synthetic_repository(root)
            (root / "CMakeLists.txt").write_text(
                "\n".join(
                    (
                        "add_library(omega_retail_formats STATIC "
                        "native/src/retail/foo_decoder.cpp)",
                        "add_executable(foo_tests "
                        "native/tests/foo_decoder_tests.cpp)",
                        "add_test(NAME foo_tests COMMAND "
                        '"$<TARGET_FILE:foo_tests>")',
                        "",
                    )
                ),
                encoding="utf-8",
            )
            self.assertEqual(validate_decoder_coverage(root), [])

    def test_interface_only_target_sources_do_not_count_as_compiled(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_synthetic_repository(root)
            (root / "native/src/retail/other.cpp").write_text(
                "int OtherRetailSource() { return 0; }\n", encoding="utf-8"
            )
            (root / "native/tests/other.cpp").write_text(
                "int main() { return 0; }\n", encoding="utf-8"
            )
            (root / "CMakeLists.txt").write_text(
                "\n".join(
                    (
                        "add_library(omega_retail_formats STATIC "
                        "native/src/retail/other.cpp)",
                        "target_sources(omega_retail_formats INTERFACE "
                        "native/src/retail/foo_decoder.cpp)",
                        "add_executable(foo_tests native/tests/other.cpp)",
                        "target_sources(foo_tests INTERFACE "
                        "native/tests/foo_decoder_tests.cpp)",
                        "add_test(NAME foo_tests COMMAND foo_tests)",
                        "",
                    )
                ),
                encoding="utf-8",
            )
            errors = validate_decoder_coverage(root)
            self.assertIn(
                "matrix row .foo cites unregistered source "
                "native/src/retail/foo_decoder.cpp",
                errors,
            )
            self.assertIn(
                "matrix row .foo cites unregistered test "
                "native/tests/foo_decoder_tests.cpp",
                errors,
            )

    def test_comments_literals_overloads_and_acronyms_are_handled_mechanically(
        self,
    ) -> None:
        self.assertEqual(boundary_suffix("DecodeTM2", {".tm2", ".t"}), ".tm2")
        self.assertEqual(boundary_suffix("DecodeVPKWrapper", {".vpk", ".v"}), ".vpk")
        self.assertEqual(boundary_suffix("Inspect64Envelope", {".64"}), ".64")

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_synthetic_repository(root)
            header = root / "native/include/omega/retail/foo_decoder.h"
            header.write_text(
                "// int DecodeFoo(const void* bytes);\n"
                'constexpr auto kText = R"tag(DecodeFoo())tag";\n',
                encoding="utf-8",
            )
            boundaries, errors = public_retail_boundaries(root, {".foo"})
            self.assertEqual(boundaries, {})
            self.assertEqual(errors, [])
            self.assertIn(
                "native matrix row .foo has no matching public boundary declaration",
                validate_decoder_coverage(root),
            )

            header.write_text(
                "int DecodeFoo(const void* bytes);\n"
                "int DecodeFoo(const void* bytes, int limit);\n",
                encoding="utf-8",
            )
            self.assertEqual(validate_decoder_coverage(root), [])

    def test_raw_literals_and_literal_false_cpp_regions_cannot_fake_surface(self) -> None:
        fake_include = (
            'constexpr auto kText = R"tag(\n'
            '#include "omega/retail/foo_decoder.h"\n'
            ')tag";\n'
        )
        self.assertEqual(cpp_includes(fake_include), set())
        self.assertEqual(
            cpp_includes(
                '#if 0\n#include "omega/retail/foo_decoder.h"\n#endif\n'
            ),
            set(),
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_synthetic_repository(root)
            header = root / "native/include/omega/retail/foo_decoder.h"
            source = root / "native/src/retail/foo_decoder.cpp"
            test = root / "native/tests/foo_decoder_tests.cpp"
            header.write_text(
                "#if 0\nint DecodeFoo(const void* bytes);\n#endif\n",
                encoding="utf-8",
            )
            source.write_text(fake_include, encoding="utf-8")
            test.write_text(fake_include, encoding="utf-8")
            errors = validate_decoder_coverage(root)
            self.assertIn(
                "native matrix row .foo has no matching public boundary declaration",
                errors,
            )

    def test_row_cited_test_must_include_a_row_cited_header(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_synthetic_repository(root)
            unrelated = root / "native/tests/unrelated_tests.cpp"
            unrelated.write_text("int main() { return 0; }\n", encoding="utf-8")
            row = (
                "| `.foo` | canonical decoder | `DecodeFoo` "
                "(`native/include/omega/retail/foo_decoder.h`, "
                "`native/src/retail/foo_decoder.cpp`) | "
                "`native/tests/unrelated_tests.cpp` |"
            )
            coverage = root / "analysis/formats/DECODER-COVERAGE.md"
            coverage.write_text(synthetic_document([row]), encoding="utf-8")
            (root / "CMakeLists.txt").write_text(
                "\n".join(
                    (
                        "add_library(omega_retail_formats STATIC "
                        "native/src/retail/foo_decoder.cpp)",
                        "add_executable(foo_tests native/tests/foo_decoder_tests.cpp)",
                        "add_test(NAME foo_tests COMMAND foo_tests)",
                        "add_executable(unrelated_tests native/tests/unrelated_tests.cpp)",
                        "add_test(NAME unrelated_tests COMMAND unrelated_tests)",
                        "",
                    )
                ),
                encoding="utf-8",
            )
            self.assertIn(
                "native matrix row .foo cites no test that includes one of its cited "
                "public headers",
                validate_decoder_coverage(root),
            )

    def test_duplicate_summary_class_is_rejected_deterministically(self) -> None:
        row = "| `.foo` | canonical decoder | evidence | test |"
        text = synthetic_document([row]).replace(
            "**1 canonical decoder**",
            "**999 canonical decoder**, **1 canonical decoder**",
        )
        rows, parse_errors = parse_matrix(text)
        self.assertEqual(parse_errors, [])
        self.assertEqual(
            validate_summary(text, rows),
            [
                "decoder coverage totals repeat class canonical_decoder",
                "decoder coverage total for canonical_decoder is 999; matrix has 1",
            ],
        )

    def test_evidence_traversal_is_rejected_before_repository_file_checks(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            outer = Path(directory)
            root = outer / "repo"
            write_synthetic_repository(root)
            (outer / "external_tests.cpp").write_text(
                "owner-independent synthetic fixture\n", encoding="utf-8"
            )
            unsafe = "native/tests/../../../external_tests.cpp"
            row = (
                "| `.foo` | canonical decoder | `DecodeFoo` "
                "(`native/include/omega/retail/foo_decoder.h`, "
                "`native/src/retail/foo_decoder.cpp`) | "
                f"`{unsafe}` |"
            )
            coverage = root / "analysis/formats/DECODER-COVERAGE.md"
            coverage.write_text(synthetic_document([row]), encoding="utf-8")
            self.assertIn(
                f"matrix row .foo cites unsafe non-repository path {unsafe}",
                validate_decoder_coverage(root),
            )

    def test_prefixed_repository_path_substrings_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_synthetic_repository(root)
            coverage = root / "analysis/formats/DECODER-COVERAGE.md"
            expected = (
                "matrix row .foo cites malformed or non-repository C++ evidence "
                "path"
            )
            for prefix in ("C:/secret/", "../../", "x"):
                with self.subTest(prefix=prefix):
                    row = (
                        "| `.foo` | canonical decoder | `DecodeFoo` "
                        "(`native/include/omega/retail/foo_decoder.h`, "
                        "`native/src/retail/foo_decoder.cpp`, "
                        f"`{prefix}native/include/omega/retail/foo_decoder.h`) | "
                        "`native/tests/foo_decoder_tests.cpp` |"
                    )
                    coverage.write_text(
                        synthetic_document([row]), encoding="utf-8"
                    )
                    self.assertEqual(validate_decoder_coverage(root), [expected])

    def test_fixed_input_link_is_rejected_before_any_text_read(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_synthetic_repository(root)
            linked = root / "analysis/formats"
            original_is_symlink = Path.is_symlink

            def emulate_symlink(path: Path) -> bool:
                return path == linked or original_is_symlink(path)

            with mock.patch.object(Path, "is_symlink", emulate_symlink):
                with mock.patch.object(
                    Path,
                    "read_text",
                    side_effect=AssertionError("link target must not be read"),
                ):
                    self.assertEqual(
                        validate_decoder_coverage(root),
                        ["decoder coverage document is missing or link-like"],
                    )

    def test_malformed_utf8_header_reports_a_stable_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_synthetic_repository(root)
            header = root / "native/include/omega/retail/foo_decoder.h"
            header.write_bytes(b"\xff")
            self.assertIn(
                "public retail header native/include/omega/retail/foo_decoder.h "
                "cannot be read as UTF-8 text",
                validate_decoder_coverage(root),
            )


if __name__ == "__main__":
    unittest.main()
