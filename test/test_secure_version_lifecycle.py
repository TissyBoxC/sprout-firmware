"""Unit tests for the production secure-version release gate."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools/verify_secure_version_lifecycle.py"
SPEC = importlib.util.spec_from_file_location(
    "verify_secure_version_lifecycle", MODULE_PATH
)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class SecureVersionLifecycleTest(unittest.TestCase):
    def _root(self, current: int) -> tuple[tempfile.TemporaryDirectory, Path]:
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        (root / "secure_version.txt").write_text(
            f"{current}\n", encoding="utf-8"
        )
        (root / "sdkconfig.production.defaults").write_text(
            f"CONFIG_BOOTLOADER_APP_SECURE_VERSION={current}\n",
            encoding="utf-8",
        )
        return temporary, root

    def test_first_rollout_from_tag_without_version_file_passes(
        self,
    ) -> None:
        temporary, root = self._root(1)
        self.addCleanup(temporary.cleanup)
        with (
            mock.patch.object(MODULE, "git_has_file", return_value=False),
            mock.patch.object(MODULE, "git_show_version", return_value=None),
        ):
            errors = MODULE.validate(
                root,
                previous_revision="v0.12.0",
                require_increment=True,
            )
        self.assertEqual(errors, [])

    def test_first_rollout_must_start_at_one(self) -> None:
        temporary, root = self._root(2)
        self.addCleanup(temporary.cleanup)
        with (
            mock.patch.object(MODULE, "git_has_file", return_value=False),
            mock.patch.object(MODULE, "git_show_version", return_value=None),
        ):
            errors = MODULE.validate(
                root,
                previous_revision="v0.12.0",
                require_increment=True,
            )
        self.assertTrue(
            any("must start at 1" in error for error in errors), errors
        )

    def test_existing_version_file_must_increment(self) -> None:
        temporary, root = self._root(2)
        self.addCleanup(temporary.cleanup)
        with (
            mock.patch.object(MODULE, "git_has_file", return_value=True),
            mock.patch.object(MODULE, "git_show_version", return_value=1),
        ):
            errors = MODULE.validate(
                root,
                previous_revision="v0.13.0",
                require_increment=True,
            )
        self.assertEqual(errors, [])

    def test_existing_version_file_rejects_skipped_increment(self) -> None:
        temporary, root = self._root(3)
        self.addCleanup(temporary.cleanup)
        with (
            mock.patch.object(MODULE, "git_has_file", return_value=True),
            mock.patch.object(MODULE, "git_show_version", return_value=1),
        ):
            errors = MODULE.validate(
                root,
                previous_revision="v0.13.0",
                require_increment=True,
            )
        self.assertTrue(
            any("must increment from 1 to 2" in error for error in errors),
            errors,
        )

    def test_existing_version_file_rejects_downgrade(self) -> None:
        temporary, root = self._root(1)
        self.addCleanup(temporary.cleanup)
        with (
            mock.patch.object(MODULE, "git_has_file", return_value=True),
            mock.patch.object(MODULE, "git_show_version", return_value=2),
        ):
            errors = MODULE.validate(
                root,
                previous_revision="v0.13.0",
                require_increment=True,
            )
        self.assertTrue(
            any("must increment from 2 to 3" in error for error in errors),
            errors,
        )


if __name__ == "__main__":
    unittest.main()
