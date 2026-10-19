"""Regression tests for firmware workflow trust-key boundaries."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
import textwrap
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
CI_WORKFLOW = ROOT / ".github/workflows/firmware-ci.yml"
RELEASE_WORKFLOW = ROOT / ".github/workflows/release.yml"
RELEASE_NOTES_SCRIPT = ROOT / "tools/release_notes.py"
VERIFIER = ROOT / "tools/verify_production_signing.py"
DUMMY_KEY = "11qYAYKxCrfVS/7TyWQHOg7hcvPapiMlrwIaaPcHURo="
DUMMY_KEYRING = '{"ci-test-only":"' + DUMMY_KEY + '"}'
RELEASE_COMMIT = "a" * 40
OTHER_COMMIT = "b" * 40


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


def heredoc_scripts(workflow: str) -> dict[str, str]:
    pattern = re.compile(
        r"<<'(?P<marker>PY_[A-Z0-9_]+)'\n"
        r"(?P<script>.*?)\n\s*(?P=marker)",
        re.DOTALL,
    )
    scripts: dict[str, str] = {}
    for match in pattern.finditer(workflow):
        marker = match.group("marker")
        scripts[marker] = (
            textwrap.dedent(match.group("script")).strip() + "\n"
        )
    return scripts


def load_heredoc_script(workflow: str, marker: str) -> dict[str, object]:
    scripts = heredoc_scripts(workflow)
    if marker not in scripts:
        raise AssertionError(f"missing workflow script {marker}")
    namespace: dict[str, object] = {"__name__": marker.lower()}
    exec(
        compile(scripts[marker], marker, "exec"),
        namespace,
    )
    return namespace


def release_assets(tag: str) -> tuple[str, ...]:
    return (
        f"sprout-firmware-{tag}.bin",
        "bootloader.bin",
        "partitions.bin",
        "firmware.elf",
        "firmware.map",
        "SHA256SUMS",
    )


def release_payload(
    tag: str = "v0.13.0",
    *,
    target_commitish: str = RELEASE_COMMIT,
    draft: bool = False,
    immutable: bool = False,
    extra_asset: str | None = None,
) -> dict[str, object]:
    names = list(release_assets(tag))
    if extra_asset is not None:
        names.append(extra_asset)
    return {
        "tag_name": tag,
        "target_commitish": target_commitish,
        "draft": draft,
        "immutable": immutable,
        "assets": [{"name": name} for name in names],
    }


class WorkflowSecurityContractTest(unittest.TestCase):
    def run_state_machine(
        self,
        *,
        tag_sha: str | None,
        direct_tag_sha: str | None | object = ...,
        release: dict[str, object] | None,
        assets_complete: bool = True,
        assets_reason: str = "",
    ) -> tuple[dict[str, str], dict[str, object]]:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "VERSION").write_text("0.13.0\n", encoding="utf-8")
            output_path = root / "github-output.txt"
            environment = {
                "GITHUB_WORKSPACE": str(root),
                "GITHUB_OUTPUT": str(output_path),
                "GITHUB_REPOSITORY": "example/sprout-firmware",
                "GITHUB_SHA": RELEASE_COMMIT,
                "RUNNER_TEMP": str(root),
            }
            workflow = RELEASE_WORKFLOW.read_text(encoding="utf-8")
            namespace = load_heredoc_script(
                workflow,
                "PY_RELEASE_STATE",
            )
            direct = tag_sha if direct_tag_sha is ... else direct_tag_sha
            namespace["resolve_tag"] = mock.Mock(
                return_value=(direct, tag_sha)
            )
            namespace["run"] = mock.Mock(return_value=RELEASE_COMMIT + "\n")
            namespace["read_release"] = mock.Mock(return_value=release)
            namespace["verify_release_assets"] = mock.Mock(
                return_value=(assets_complete, assets_reason)
            )
            with mock.patch.dict(os.environ, environment, clear=True):
                namespace["main"]()
            outputs = {}
            if output_path.exists():
                outputs = dict(
                    line.split("=", 1)
                    for line in output_path.read_text(
                        encoding="utf-8"
                    ).splitlines()
                )
            return outputs, namespace

    def run_publisher(
        self,
        mode: str,
        *,
        tag_sha: str | None = RELEASE_COMMIT,
        direct_tag_sha: str | None = None,
    ) -> tuple[list[list[str]], dict[str, object]]:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            dist = root / "dist"
            dist.mkdir()
            tag = "v0.13.0"
            expected = release_assets(tag)
            checksum_lines = []
            for name in expected:
                if name == "SHA256SUMS":
                    continue
                data = f"asset:{name}\n".encode()
                (dist / name).write_bytes(data)
                checksum_lines.append(
                    f"{hashlib.sha256(data).hexdigest()}  {name}"
                )
            (dist / "SHA256SUMS").write_text(
                "\n".join(checksum_lines) + "\n",
                encoding="utf-8",
            )
            (root / "release-notes.md").write_text(
                "中文发布说明\n",
                encoding="utf-8",
            )
            environment = {
                "GITHUB_WORKSPACE": str(root),
                "GITHUB_REPOSITORY": "example/sprout-firmware",
                "GITHUB_SHA": RELEASE_COMMIT,
                "RELEASE_TAG": tag,
                "RELEASE_VERSION": "0.13.0",
                "RELEASE_PUBLISH_MODE": mode,
            }
            namespace = load_heredoc_script(
                RELEASE_WORKFLOW.read_text(encoding="utf-8"),
                "PY_RELEASE_PUBLISH",
            )
            namespace["resolve_tag"] = mock.Mock(
                return_value=(
                    direct_tag_sha
                    if direct_tag_sha is not None
                    else tag_sha,
                    tag_sha,
                )
            )
            commands: list[list[str]] = []
            namespace["run"] = mock.Mock(
                side_effect=lambda command: commands.append(command) or ""
            )
            with mock.patch.dict(os.environ, environment, clear=True):
                namespace["main"]()
            return commands, namespace

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
            "read_release",
            "releases/tags/{tag}",
            '"download",',
            "hashlib.sha256",
            "sprout-firmware-{tag}.bin",
        ):
            self.assertIn(required, detect_block)
        self.assertNotIn("Release $tag already exists; skipping.", detect_block)

    def test_checkout_does_not_persist_write_credentials(self) -> None:
        workflow = RELEASE_WORKFLOW.read_text(encoding="utf-8")
        checkout_end = workflow.index(
            "- name: Detect version and existing release"
        )
        self.assertIn(
            "persist-credentials: false",
            workflow[:checkout_end],
        )

    def test_complete_release_is_verified_then_skipped(self) -> None:
        outputs, namespace = self.run_state_machine(
            tag_sha=RELEASE_COMMIT,
            release=release_payload(),
        )
        self.assertEqual(outputs, {"should_release": "false"})
        namespace["verify_release_assets"].assert_called_once()

    def test_complete_immutable_release_is_verified_then_skipped(self) -> None:
        outputs, namespace = self.run_state_machine(
            tag_sha=RELEASE_COMMIT,
            release=release_payload(immutable=True),
        )
        self.assertEqual(outputs, {"should_release": "false"})
        namespace["verify_release_assets"].assert_called_once()

    def test_draft_release_with_complete_assets_is_published_without_rebuild(
        self,
    ) -> None:
        outputs, _ = self.run_state_machine(
            tag_sha=RELEASE_COMMIT,
            release=release_payload(draft=True),
        )
        self.assertEqual(outputs, {"should_release": "false"})

    def test_incomplete_draft_is_rebuilt(self) -> None:
        outputs, _ = self.run_state_machine(
            tag_sha=RELEASE_COMMIT,
            release=release_payload(
                draft=True,
                extra_asset="unexpected.txt",
            ),
            assets_complete=False,
            assets_reason="asset set mismatch",
        )
        self.assertEqual(outputs["publish_mode"], "rebuild_draft")
        commands, _ = self.run_publisher("rebuild_draft")
        self.assertEqual(
            commands[0][:4],
            ["gh", "release", "delete", "v0.13.0"],
        )
        self.assertIn("--verify-tag", commands[1])

    def test_missing_tag_and_release_starts_new_publication(self) -> None:
        outputs, _ = self.run_state_machine(
            tag_sha=None,
            release=None,
        )
        self.assertEqual(
            outputs,
            {
                "should_release": "true",
                "version": "0.13.0",
                "tag": "v0.13.0",
                "publish_mode": "create",
            },
        )

    def test_existing_tag_with_missing_release_is_recoverable(self) -> None:
        outputs, _ = self.run_state_machine(
            tag_sha=RELEASE_COMMIT,
            release=None,
        )
        self.assertEqual(outputs["publish_mode"], "attach")
        self.assertEqual(outputs["should_release"], "true")

    def test_incomplete_release_is_rebuilt(self) -> None:
        outputs, _ = self.run_state_machine(
            tag_sha=RELEASE_COMMIT,
            release=release_payload(extra_asset="unexpected.txt"),
            assets_complete=False,
            assets_reason="asset set mismatch",
        )
        self.assertEqual(outputs["publish_mode"], "rebuild")
        self.assertEqual(outputs["should_release"], "true")

    def test_release_targeting_other_commit_is_rejected(self) -> None:
        with self.assertRaisesRegex(
            RuntimeError,
            "targets",
        ):
            self.run_state_machine(
                tag_sha=RELEASE_COMMIT,
                release=release_payload(
                    target_commitish=OTHER_COMMIT,
                ),
            )

    def test_tag_targeting_other_commit_is_never_overwritten(self) -> None:
        with self.assertRaisesRegex(
            RuntimeError,
            "Refusing to replace tag",
        ):
            self.run_state_machine(
                tag_sha=OTHER_COMMIT,
                release=None,
            )

    def test_release_without_tag_is_rejected(self) -> None:
        with self.assertRaisesRegex(
            RuntimeError,
            "exists without its tag",
        ):
            self.run_state_machine(
                tag_sha=None,
                release=release_payload(),
            )

    def test_immutable_incomplete_release_is_rejected(self) -> None:
        with self.assertRaisesRegex(
            RuntimeError,
            "immutable but incomplete",
        ):
            self.run_state_machine(
                tag_sha=RELEASE_COMMIT,
                release=release_payload(
                    immutable=True,
                    extra_asset="unexpected.txt",
                ),
                assets_complete=False,
                assets_reason="asset set mismatch",
            )

    def test_release_asset_checksums_are_verified(self) -> None:
        namespace = load_heredoc_script(
            RELEASE_WORKFLOW.read_text(encoding="utf-8"),
            "PY_RELEASE_STATE",
        )
        expected = release_assets("v0.13.0")
        payload = {
            name: hashlib.sha256(name.encode()).hexdigest()
            for name in expected
            if name != "SHA256SUMS"
        }

        def download(command: list[str]) -> str:
            name = command[command.index("--pattern") + 1]
            destination = Path(command[command.index("--dir") + 1])
            if name == "SHA256SUMS":
                content = "\n".join(
                    f"{digest}  {asset}"
                    for asset, digest in payload.items()
                )
            else:
                content = name
            (destination / name).write_text(content, encoding="utf-8")
            return ""

        with tempfile.TemporaryDirectory() as temp:
            namespace["run"] = mock.Mock(side_effect=download)
            with mock.patch.dict(
                os.environ,
                {"RUNNER_TEMP": temp},
                clear=False,
            ):
                complete, reason = namespace["verify_release_assets"](
                    "example/sprout-firmware",
                    "v0.13.0",
                    release_payload(),
                    expected,
                )
        self.assertEqual((complete, reason), (True, ""))

    def test_create_upload_is_draft_until_assets_exist(self) -> None:
        commands, _ = self.run_publisher("create", tag_sha=None)
        create = commands[0]
        publish = commands[1]
        self.assertEqual(create[:4], ["gh", "release", "create", "v0.13.0"])
        self.assertIn("--draft", create)
        self.assertIn("--target", create)
        self.assertEqual(
            publish,
            [
                "gh",
                "release",
                "edit",
                "v0.13.0",
                "--repo",
                "example/sprout-firmware",
                "--draft=false",
            ],
        )

    def test_attach_uses_verified_existing_tag(self) -> None:
        commands, _ = self.run_publisher("attach")
        create = commands[0]
        self.assertIn("--verify-tag", create)
        self.assertNotIn("--target", create)
        self.assertFalse(
            any(
                command[:3] == ["gh", "release", "delete"]
                for command in commands
            )
        )

    def test_rebuild_deletes_release_but_never_tag(self) -> None:
        commands, _ = self.run_publisher("rebuild")
        self.assertEqual(
            commands[0][:4],
            ["gh", "release", "delete", "v0.13.0"],
        )
        self.assertNotIn("--cleanup-tag", commands[0])
        self.assertIn("--verify-tag", commands[1])
        self.assertIn("--draft", commands[1])

    def test_publisher_rejects_tag_from_other_commit(self) -> None:
        with self.assertRaisesRegex(
            RuntimeError,
            "Refusing to replace tag",
        ):
            self.run_publisher(
                "rebuild",
                tag_sha=OTHER_COMMIT,
            )

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

    def test_release_notes_classify_ota_commits(self) -> None:
        namespace: dict[str, object] = {"__name__": "release_notes_test"}
        exec(
            compile(
                RELEASE_NOTES_SCRIPT.read_text(encoding="utf-8"),
                str(RELEASE_NOTES_SCRIPT),
                "exec",
            ),
            namespace,
        )
        commits = [
            {
                "sha": RELEASE_COMMIT,
                "author": "tester",
                "scope": "ota",
                "type": "fix",
                "description": "统一跨端签名契约并加固发布",
                "is_breaking": "false",
            }
        ]
        notes = namespace["render_release_notes"](
            "0.13.0",
            "example/sprout-firmware",
            "v0.12.0",
            commits,
        )
        self.assertIn("**OTA**：统一跨端签名契约并加固发布", notes)


if __name__ == "__main__":
    unittest.main()
