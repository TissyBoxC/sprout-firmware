"""Fail closed when a production image is built without its signing key.

PlatformIO treats a missing Secure Boot signing key as a warning: the build
still reports success and silently skips the signing step, which would let a
release job publish an unsigned image while its sdkconfig claims Secure Boot is
on. Production environments must instead stop before compiling.

The key path comes from the ``board_build.secure_boot_signing_key`` project
option, then the ``SPROUT_SECURE_BOOT_SIGNING_KEY`` environment variable, and
finally the ESP-IDF default filename. Only environments whose name ends in
``-production`` are checked, so development and factory builds keep working
without keys.
"""

from __future__ import annotations

import os
import sys

Import("env")  # noqa: F821 - provided by SCons


def _production_environment():
    return env.subst("$PIOENV").endswith("-production")  # noqa: F821


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


if _production_environment():
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
