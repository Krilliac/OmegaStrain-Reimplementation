#!/usr/bin/env python3
"""Require every non-merge commit in a revision range to carry a matching DCO sign-off."""

from __future__ import annotations

import argparse
import re
import subprocess


SIGN_OFF = re.compile(r"^Signed-off-by:\s*(.+?)\s*<([^<>\s]+@[^<>\s]+)>\s*$", re.MULTILINE)


def git(*arguments: str) -> str:
    return subprocess.run(
        ["git", *arguments],
        check=True,
        stdout=subprocess.PIPE,
        text=True,
        encoding="utf-8",
    ).stdout


def commits(revision_range: str) -> list[str]:
    output = git("rev-list", "--reverse", "--no-merges", revision_range, "--")
    return [line for line in output.splitlines() if line]


def check_commit(commit: str) -> str | None:
    record = git("show", "-s", "--format=%an%x00%ae%x00%B", commit)
    author_name, author_email, body = record.split("\0", 2)
    sign_offs = SIGN_OFF.findall(body)
    if any(
        name.strip() == author_name.strip() and email.casefold() == author_email.casefold()
        for name, email in sign_offs
    ):
        return None
    short = git("rev-parse", "--short=12", commit).strip()
    return f"{short}: expected 'Signed-off-by: {author_name} <{author_email}>'"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("revision_range", help="Git revision range, for example BASE..HEAD")
    arguments = parser.parse_args()

    commit_ids = commits(arguments.revision_range)
    errors = [error for commit in commit_ids if (error := check_commit(commit)) is not None]
    if errors:
        print(f"DCO gate: FAILED ({len(errors)} unsigned or mismatched commit(s))")
        for error in errors:
            print(f"- {error}")
        return 1
    print(f"DCO gate: OK ({len(commit_ids)} non-merge commit(s) checked)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
