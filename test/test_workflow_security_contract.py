"""Regression tests for firmware workflow trust-key boundaries."""

from __future__ import annotations

import json
import os
from pathlib import Path
import textwrap
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
CI_WORKFLOW = ROOT / ".github/workflows/firmware-ci.yml"
RELEASE_WORKFLOW = ROOT / ".github/workflows/release.yml"
VERIFIER = ROOT / "tools/verify_production_signing.py"
DUMMY_KEY = "11qYAYKxCrfVS/7TyWQHOg7hcvPapiMlrwIaaPcHURo="
DUMMY_KEYRING = '{"ci-test-only":"' + DUMMY_KEY + '"}'


def python_scripts(workflow: str) -> list[str]:
    scripts: list[str] = []
    marker = "python -c '"
    start = 0
    while True:
        marker_index = workflow.find(marker, start)
        if marker_index < 0:
            return scripts
        script_start = marker_index + len(marker)
        script_end = workflow.find("'", script_start)
        if script_end < 0:
            raise AssertionError("unterminated python -c script")
        scripts.append(
            textwrap.dedent(workflow[script_start:script_end]).lstrip()
        )
        start = script_end + 1


class WorkflowSecurityContractTest(unittest.TestCase):
    def test_dummy_keyring_matches_production_validator(self) -> None:
        environment = mock.patch.dict(
            os.environ,
            {"SPROUT_OTA_TRUSTED_KEYS_JSON": DUMMY_KEYRING},
        )
        namespace: dict[str, object] = {
            "Import": lambda _name: None,
            "env": mock.Mock(),
        }
        namespace["env"].subst.side_effect = lambda value: value
        namespace["env"].Exit.side_effect = RuntimeError("validator rejected")
        with environment:
            exec(
                compile(
                    VERIFIER.read_text(encoding="utf-8"),
                    str(VERIFIER),
                    "exec",
                ),
                namespace,
            )
            normalized = namespace["_resolve_ota_keyring"]()
        self.assertEqual(json.loads(normalized), {"ci-test-only": DUMMY_KEY})

    def test_ci_uses_checked_in_dummy_keyring_not_secret(self) -> None:
        workflow = CI_WORKFLOW.read_text(encoding="utf-8")
        self.assertIn('"ci-test-only":"', workflow)
        self.assertIn(DUMMY_KEYRING, workflow)
        self.assertNotIn(
            "secrets.SPROUT_OTA_TRUSTED_KEYS_JSON",
            workflow,
        )

    def test_release_requires_real_keyring_secret(self) -> None:
        workflow = RELEASE_WORKFLOW.read_text(encoding="utf-8")
        self.assertIn(
            "SPROUT_OTA_TRUSTED_KEYS_JSON: "
            "${{ secrets.SPROUT_OTA_TRUSTED_KEYS_JSON }}",
            workflow,
        )
        self.assertIn(
            'if [[ -z "${SPROUT_OTA_TRUSTED_KEYS_JSON:-}" ]]',
            workflow,
        )
        self.assertIn("Missing required secret", workflow)
        self.assertIn("is not canonical Base64", workflow)
        self.assertNotIn(DUMMY_KEY, workflow)

    def test_release_keyring_validator_rejects_missing_and_malformed(self) -> None:
        workflow = RELEASE_WORKFLOW.read_text(encoding="utf-8")
        scripts = python_scripts(workflow)
        self.assertGreaterEqual(len(scripts), 1)
        validator = scripts[0]
        valid = compile(validator, "release-keyring-validator", "exec")
        cases = (
            ("", "Invalid SPROUT_OTA_TRUSTED_KEYS_JSON"),
            ("[]", "non-empty JSON object"),
            ("{", "Invalid SPROUT_OTA_TRUSTED_KEYS_JSON"),
            (
                '{"bad key":"11qYAYKxCrfVS/7TyWQHOg7hcvPapiMlrwIaaPcHURo="}',
                "invalid key entry",
            ),
            (
                '{"ci-test-only":"not-base64"}',
                "not canonical Base64",
            ),
            (
                '{"ci-test-only":"YWJj"}',
                "exactly 32 bytes",
            ),
        )
        for keyring, expected in cases:
            with self.subTest(keyring=keyring):
                with (
                    mock.patch.dict(
                        os.environ,
                        {"SPROUT_OTA_TRUSTED_KEYS_JSON": keyring},
                    ),
                    mock.patch(
                        "sys.exit",
                        side_effect=SystemExit,
                    ) as exit_mock,
                ):
                    with self.assertRaises(SystemExit):
                        exec(valid, {})
                exit_mock.assert_called_once()
                self.assertIn(expected, str(exit_mock.call_args.args[0]))
        valid_keyring = dict(
            os.environ, SPROUT_OTA_TRUSTED_KEYS_JSON=DUMMY_KEYRING
        )
        with mock.patch.dict(os.environ, valid_keyring, clear=True):
            exec(valid, {})

    def test_existing_release_must_be_complete_before_skip(self) -> None:
        workflow = RELEASE_WORKFLOW.read_text(encoding="utf-8")
        detect_start = workflow.index("Detect version and existing release")
        setup_python = workflow.index(
            "- name: Set up Python",
            detect_start,
        )
        detect_block = workflow[detect_start:setup_python]
        for required in (
            "gh release view",
            "gh release download",
            "sha256sum -c SHA256SUMS",
            "sprout-firmware-$tag.bin",
        ):
            self.assertIn(required, detect_block)
        self.assertNotIn("Release $tag already exists; skipping.", detect_block)

    def test_local_host_verification_fails_without_python(self) -> None:
        script = (ROOT / "tools/run_host_tests.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "OTA signing-to-firmware verification requires Python.",
            script,
        )
        self.assertNotIn(
            "Skipping OTA signing-to-firmware verification",
            script,
        )


if __name__ == "__main__":
    unittest.main()
