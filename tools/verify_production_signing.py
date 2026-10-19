"""Fail closed when a production image lacks build-time trust material.

PlatformIO treats a missing Secure Boot signing key as a warning: the build
still reports success and silently skips the signing step, which would let a
release job publish an unsigned image while its sdkconfig claims Secure Boot is
on. Production OTA also needs a trusted Ed25519 public keyring, otherwise every
downloaded manifest is rejected. Production environments must stop before
compiling when either input is missing or malformed.

The key path comes from the ``board_build.secure_boot_signing_key`` project
option, then the ``SPROUT_SECURE_BOOT_SIGNING_KEY`` environment variable, and
finally the ESP-IDF default filename. Only environments whose name ends in
``-production`` are checked, so development and factory builds keep working
without keys.
"""

from __future__ import annotations

import base64
import binascii
import json
import os
import re
import sys

Import("env")  # noqa: F821 - provided by SCons


def _production_environment():
    return env.subst("$PIOENV").endswith("-production")  # noqa: F821


def _has_trusted_keyring(sdkconfig_content):
    match = re.search(
        r'^CONFIG_OTA_VALIDATE_TRUSTED_KEYS_JSON="(.*)"$',
        sdkconfig_content,
        re.MULTILINE,
    )
    return match is not None and bool(match.group(1).strip())


def _verify_generated_keyring(target, source, env):  # noqa: ARG001
    sdkconfig_path = os.path.join(
        env.subst("$PROJECT_DIR"),
        "sdkconfig." + env.subst("$PIOENV"),
    )
    if not os.path.isfile(sdkconfig_path):
        sys.stderr.write(
            f"\nError: generated OTA config {sdkconfig_path} does not exist.\n"
        )
        env.Exit(1)  # noqa: F821
    with open(sdkconfig_path, encoding="utf-8", errors="replace") as config:
        content = config.read()
    if (
        "CONFIG_FEATURE_OTA_MANAGER=y" in content
        and not _has_trusted_keyring(content)
    ):
        sys.stderr.write(
            f"\nError: generated OTA config {sdkconfig_path} has no "
            "trusted keyring.\n"
        )
        env.Exit(1)  # noqa: F821
    required = (
        "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y",
        "CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=y",
        "CONFIG_BOOTLOADER_APP_SECURE_VERSION",
        "CONFIG_SECURE_BOOT=y",
        "CONFIG_SECURE_FLASH_ENC_ENABLED=y",
    )
    missing = [item for item in required if item not in content]
    if missing:
        sys.stderr.write(
            f"\nError: generated production config {sdkconfig_path} lacks "
            f"required OTA hardening: {', '.join(missing)}.\n"
        )
        env.Exit(1)  # noqa: F821
    return None


def _resolve_signing_key():
    try:
        configured = env.GetProjectOption(  # noqa: F821
            "board_build.secure_boot_signing_key", ""
        )
    except Exception:  # pylint: disable=broad-except - option may be absent
        configured = ""
    candidate = (
        configured
        or os.environ.get("SPROUT_SECURE_BOOT_SIGNING_KEY", "")
        or "secure_boot_signing_key.pem"
    )

    if not os.path.isabs(candidate):
        candidate = os.path.join(env.subst("$PROJECT_DIR"), candidate)  # noqa: F821
    return candidate


def _resolve_ota_keyring():
    encoded = os.environ.get("SPROUT_OTA_TRUSTED_KEYS_JSON", "").strip()
    if not encoded:
        return None
    try:
        keyring = json.loads(encoded)
    except json.JSONDecodeError as error:
        sys.stderr.write(
            "\nError: SPROUT_OTA_TRUSTED_KEYS_JSON is not valid JSON: "
            f"{error}.\n"
        )
        env.Exit(1)  # noqa: F821
    if not isinstance(keyring, dict) or not keyring:
        sys.stderr.write(
            "\nError: SPROUT_OTA_TRUSTED_KEYS_JSON must be a non-empty "
            "JSON object mapping key ids to Ed25519 public keys.\n"
        )
        env.Exit(1)  # noqa: F821
    key_pattern = re.compile(r"^[A-Za-z0-9._:-]{1,64}$")
    for key_id, public_key in keyring.items():
        if (
            not isinstance(key_id, str)
            or not key_pattern.fullmatch(key_id)
            or not isinstance(public_key, str)
        ):
            sys.stderr.write(
                "\nError: every OTA trust key id must be 1-64 characters "
                "from [A-Za-z0-9._:-] with a Base64 string value.\n"
            )
            env.Exit(1)  # noqa: F821
        try:
            raw = base64.b64decode(public_key, validate=True)
        except (binascii.Error, ValueError):
            sys.stderr.write(
                f"\nError: OTA public key '{key_id}' is not canonical "
                "standard Base64.\n"
            )
            env.Exit(1)  # noqa: F821
        if len(raw) != 32 or base64.b64encode(raw).decode("ascii") != public_key:
            sys.stderr.write(
                f"\nError: OTA public key '{key_id}' must encode exactly "
                "one 32-byte Ed25519 key.\n"
            )
            env.Exit(1)  # noqa: F821
    return json.dumps(keyring, separators=(",", ":"), sort_keys=True)


def _write_overlay(project_dir, keyring):
    overlay_path = os.path.join(project_dir, "sdkconfig.ota-production.generated")
    def sdkconfig_string(value):
        return (
            value.replace("\\", "\\\\")
            .replace('"', '\\"')
            .replace("$", "$$")
        )

    with open(overlay_path, "w", encoding="utf-8", newline="\n") as overlay:
        overlay.write(
            "# Generated by verify_production_signing.py. Do not commit.\n"
            "CONFIG_OTA_VALIDATE_TRUSTED_KEYS_JSON="
            f'"{sdkconfig_string(keyring)}"\n'
        )
    return overlay_path


if _production_environment():
    project_dir = env.subst("$PROJECT_DIR")  # noqa: F821
    signing_key = _resolve_signing_key()
    if not os.path.isfile(signing_key):
        sys.stderr.write(
            "\nError: production build requires a Secure Boot signing key, "
            f"but '{signing_key}' does not exist.\n"
            "Provide one with board_build.secure_boot_signing_key in "
            "platformio.ini or the SPROUT_SECURE_BOOT_SIGNING_KEY environment "
            "variable. Refusing to build an unsigned production image.\n"
        )
        env.Exit(1)  # noqa: F821
    env["PIO_ESP32_SECURE_BOOT_SIGNING_KEY"] = signing_key  # noqa: F821
    ota_keyring = _resolve_ota_keyring()
    if ota_keyring is None:
        sys.stderr.write(
            "\nError: production build requires "
            "SPROUT_OTA_TRUSTED_KEYS_JSON with the trusted firmware-signing "
            "Ed25519 public keys. Refusing to build an image that cannot "
            "authenticate OTA manifests.\n"
        )
        env.Exit(1)  # noqa: F821
    overlay_path = _write_overlay(project_dir, ota_keyring)
    defaults = env.BoardConfig().get(  # noqa: F821
        "build.cmake_extra_args", ""
    )
    match = re.search(r'-DSDKCONFIG_DEFAULTS="([^"]+)"', defaults)
    if match is None:
        sys.stderr.write(
            "\nError: production build must define "
            'board_build.cmake_extra_args=-DSDKCONFIG_DEFAULTS="..."\n'
        )
        env.Exit(1)  # noqa: F821
    existing = [item.strip() for item in match.group(1).split(";") if item.strip()]
    if overlay_path not in existing:
        existing.append(overlay_path)
    generated = ';'.join(existing)
    generated_for_cmake = (
        generated.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("$", "$$")
    )
    env.BoardConfig().update(  # noqa: F821
        "build.cmake_extra_args",
        f'-DSDKCONFIG_DEFAULTS="{generated_for_cmake}"',
    )

    env.AddPostAction("buildprog", _verify_generated_keyring)  # noqa: F821
