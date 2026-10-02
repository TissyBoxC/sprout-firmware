"""Keep PlatformIO's ESP-IDF build working in this Windows workspace.

PlatformIO builds the root ESP-IDF linker script by passing every linker
fragment as one ``cmd.exe`` command. A full ESP32-S3 component graph exceeds
the Windows 8191-character shell limit. The command itself is valid, so only
this one command is launched without ``cmd.exe``; all other SCons actions keep
the platform's normal shell behavior.

The ESP-IDF CMake integration can create the certificate bundle binary before
PlatformIO asks SCons for its generated assembly source. PlatformIO then skips
the embed step because its early-return check only looks at the binary. Removing
that stale binary lets PlatformIO regenerate both files consistently.
"""

from __future__ import annotations

import os
import subprocess
import sys

Import("env")  # noqa: F821 - provided by SCons


_original_spawn = env["SPAWN"]  # noqa: F821 - provided by SCons


def _remove_stale_certificate_bundle():
    build_dir = env.subst("$BUILD_DIR")  # noqa: F821 - provided by SCons
    bundle_path = os.path.join(build_dir, "x509_crt_bundle")
    assembly_path = f"{bundle_path}.S"
    if os.path.isfile(bundle_path) and not os.path.isfile(assembly_path):
        os.remove(bundle_path)


def _spawn_without_shell_for_ldgen(sh, escape, cmd, args, spawn_env):
    if any("ldgen.py" in str(argument) for argument in args):
        command_arguments = [
            str(argument).strip('"') for argument in args
        ]
        try:
            completed = subprocess.run(
                command_arguments,
                cwd=os.getcwd(),
                env=spawn_env,
                check=False,
            )
            return completed.returncode
        except OSError as error:
            sys.stderr.write(
                "scons: unable to launch ldgen without cmd.exe: "
                f"{error}\n"
            )
            return 1
    return _original_spawn(sh, escape, cmd, args, spawn_env)


env["SPAWN"] = _spawn_without_shell_for_ldgen  # noqa: F821 - provided by SCons
_remove_stale_certificate_bundle()
