#!/usr/bin/env python3
"""Validate the production anti-rollback secure-version lifecycle."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


MAX_SECURE_VERSION = 65535


def parse_secure_version(path: Path) -> int:
    text = path.read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"[1-9][0-9]*", text):
        raise ValueError(
            f"{path.name} must contain one positive decimal integer"
        )
    value = int(text)
    if value > MAX_SECURE_VERSION:
        raise ValueError(
            f"{path.name} must not exceed {MAX_SECURE_VERSION}"
        )
    return value


def generated_secure_version(path: Path) -> int:
    text = path.read_text(encoding="utf-8", errors="replace")
    match = re.search(
        r"^CONFIG_BOOTLOADER_APP_SECURE_VERSION=([0-9]+)$",
        text,
        re.MULTILINE,
    )
    if match is None:
        raise ValueError(
            f"{path.name} must define CONFIG_BOOTLOADER_APP_SECURE_VERSION"
        )
    return int(match.group(1))


def git_show_version(root: Path, revision: str) -> int | None:
    result = subprocess.run(
        ["git", "show", f"{revision}:secure_version.txt"],
        cwd=root,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return None
    text = result.stdout.strip()
    if not re.fullmatch(r"[1-9][0-9]*", text):
        raise ValueError(
            f"{revision}:secure_version.txt is not a positive integer"
        )
    return int(text)


def git_has_file(root: Path, revision: str, path: str) -> bool:
    result = subprocess.run(
        ["git", "cat-file", "-e", f"{revision}:{path}"],
        cwd=root,
        check=False,
        capture_output=True,
        text=True,
    )
    return result.returncode == 0


def validate(
    root: Path,
    *,
    previous_revision: str | None = None,
    require_increment: bool = False,
) -> list[str]:
    errors: list[str] = []
    current: int | None = None
    try:
        current = parse_secure_version(root / "secure_version.txt")
    except (OSError, ValueError) as error:
        errors.append(str(error))

    overlay_path = root / "sdkconfig.production.defaults"
    try:
        overlay = overlay_path.read_text(
            encoding="utf-8",
            errors="replace",
        )
    except OSError as error:
        errors.append(f"cannot read {overlay_path}: {error}")
        return errors
    configured_match = re.search(
        r"^CONFIG_BOOTLOADER_APP_SECURE_VERSION=([0-9]+)$",
        overlay,
        re.MULTILINE,
    )
    if configured_match is None:
        errors.append(
            "sdkconfig.production.defaults must set "
            "CONFIG_BOOTLOADER_APP_SECURE_VERSION"
        )
    elif current is not None and int(configured_match.group(1)) != current:
        errors.append(
            "sdkconfig.production.defaults secure version "
            f"{configured_match.group(1)} does not match secure_version.txt "
            f"{current}"
        )

    if previous_revision:
        try:
            previous_has_version = git_has_file(
                root, previous_revision, "secure_version.txt"
            )
            previous = git_show_version(root, previous_revision)
        except ValueError as error:
            errors.append(str(error))
            previous = None
            previous_has_version = False
        if not previous_has_version:
            if require_increment:
                if current is not None and current != 1:
                    errors.append(
                        "first secure-version rollout must start at 1 when "
                        f"{previous_revision} has no secure_version.txt, "
                        f"found {current}"
                    )
        elif previous is None:
            if require_increment:
                errors.append(
                    f"cannot read secure_version.txt from {previous_revision}"
                )
        elif current is not None:
            expected = previous + 1
            if current != expected:
                errors.append(
                    f"secure version must increment from {previous} to "
                    f"{expected}, found {current}"
                )
    return errors


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--previous-revision", default="")
    parser.add_argument("--require-increment", action="store_true")
    parser.add_argument(
        "--generated-config",
        type=Path,
        help="Also compare the PlatformIO-generated production config.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    errors = validate(
        root,
        previous_revision=args.previous_revision or None,
        require_increment=args.require_increment,
    )
    if args.generated_config is not None:
        try:
            wanted = parse_secure_version(root / "secure_version.txt")
            actual = generated_secure_version(args.generated_config.resolve())
            if actual != wanted:
                errors.append(
                    f"generated secure version {actual} does not match "
                    f"secure_version.txt {wanted}"
                )
        except (OSError, ValueError) as error:
            errors.append(str(error))
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("production secure-version lifecycle verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
