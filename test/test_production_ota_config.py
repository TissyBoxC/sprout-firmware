"""Release gate for the generated production OTA bootloader configuration."""

import importlib.util
from pathlib import Path
import re
import subprocess
import sys
import unittest


REQUIRED_GENERATED = (
    "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y",
    "CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=y",
    "CONFIG_FEATURE_OTA_VALIDATE=y",
    "CONFIG_FEATURE_OTA_DOWNLOAD=y",
    "CONFIG_FEATURE_OTA_ROLLBACK=y",
    "CONFIG_FEATURE_OTA_MANAGER=y",
    "CONFIG_SECURE_BOOT=y",
    "CONFIG_SECURE_FLASH_ENC_ENABLED=y",
)


def parse_config(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"(CONFIG_[A-Z0-9_]+)=(.*)", line.strip())
        if match:
            values[match.group(1)] = match.group(2)
            continue
        match = re.fullmatch(r"# (CONFIG_[A-Z0-9_]+) is not set", line.strip())
        if match:
            values[match.group(1)] = "n"
    return values


def parse_partition_size(value: str) -> int:
    normalized = value.strip()
    match = re.fullmatch(r"(0x[0-9A-Fa-f]+|[0-9]+)([KkMm]?)", normalized)
    if match is None:
        raise ValueError(f"unsupported partition size: {value}")
    size = int(match.group(1), 0)
    suffix = match.group(2).upper()
    if suffix == "K":
        size *= 1024
    elif suffix == "M":
        size *= 1024 * 1024
    return size


def validate_production_ota_config(root: Path) -> list[str]:
    errors: list[str] = []
    generated_path = root / "sdkconfig.esp32-s3-n16r8-production"
    if not generated_path.is_file():
        return [
            "sdkconfig.esp32-s3-n16r8-production is missing; "
            "build the production environment before running this gate"
        ]
    production = parse_config(generated_path)
    for key in REQUIRED_GENERATED:
        feature, _, expected = key.partition("=")
        actual = production.get(feature, "n")
        if actual != expected:
            errors.append(
                f"generated config: {feature}={actual}, want {expected}"
            )

    secure_version_text = production.get(
        "CONFIG_BOOTLOADER_APP_SECURE_VERSION"
    )
    try:
        overlay_version = int(
            re.search(
                r"^CONFIG_BOOTLOADER_APP_SECURE_VERSION=([0-9]+)$",
                (root / "sdkconfig.production.defaults").read_text(
                    encoding="utf-8"
                ),
                re.MULTILINE,
            ).group(1)
        )
    except (AttributeError, OSError, ValueError):
        errors.append(
            "sdkconfig.production.defaults must set an integer "
            "CONFIG_BOOTLOADER_APP_SECURE_VERSION"
        )
        overlay_version = None
    try:
        file_version = int(
            (root / "secure_version.txt").read_text(
                encoding="utf-8"
            ).strip()
        )
    except (OSError, ValueError):
        errors.append("secure_version.txt must contain a positive integer")
        file_version = None
    if (
        secure_version_text is None
        or not re.fullmatch(r"[1-9][0-9]*", secure_version_text)
    ):
        errors.append(
            "generated config must have a positive "
            "CONFIG_BOOTLOADER_APP_SECURE_VERSION"
        )
    elif (
        overlay_version is not None
        and file_version is not None
        and int(secure_version_text) != file_version
    ):
        errors.append(
            "generated secure version "
            f"{secure_version_text} does not match secure_version.txt "
            f"{file_version}"
        )
    elif (
        overlay_version is not None
        and file_version is not None
        and overlay_version != file_version
    ):
        errors.append(
            "sdkconfig.production.defaults secure version "
            f"{overlay_version} does not match secure_version.txt "
            f"{file_version}"
        )
    elif secure_version_text is not None:
        result = subprocess.run(
            [
                sys.executable,
                str(root / "tools/verify_secure_version_lifecycle.py"),
                "--root",
                str(root),
                "--generated-config",
                str(generated_path),
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            errors.append(
                result.stderr.strip()
                or "secure-version lifecycle verification failed"
            )

    partitions = (root / "partitions.csv").read_text(encoding="utf-8")
    ota_size_values = re.findall(
        r"^\s*ota_[01]\s*,\s*app\s*,\s*ota_[01]\s*,\s*[^,]*"
        r"\s*,\s*([^,\s]+)",
        partitions,
        re.MULTILINE,
    )
    if len(ota_size_values) != 2:
        errors.append("production partition table must contain two OTA app slots")
    else:
        try:
            ota_sizes = [
                parse_partition_size(value) for value in ota_size_values
            ]
        except ValueError as error:
            errors.append(str(error))
        else:
            if any(size > 4 * 1024 * 1024 for size in ota_sizes):
                errors.append("OTA application partitions must not exceed 4 MiB")
    return errors


def main() -> int:
    errors = validate_production_ota_config(Path(__file__).resolve().parents[1])
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("production OTA rollback and anti-downgrade configuration verified")
    return 0


class ProductionOTAConfigTest(unittest.TestCase):
    def test_production_build_keeps_rollback_and_anti_downgrade(self) -> None:
        errors = validate_production_ota_config(Path(__file__).resolve().parents[1])
        self.assertEqual(errors, [])

    def test_missing_production_overlay_setting_fails_closed(self) -> None:
        module = importlib.import_module("test_production_ota_config")
        root = Path(__file__).resolve().parents[1]
        overlay = root / "sdkconfig.production.defaults"
        original_text = overlay.read_text(encoding="utf-8")
        try:
            overlay.write_text(
                original_text.replace(
                    "CONFIG_BOOTLOADER_APP_SECURE_VERSION=1",
                    "# missing secure version",
                ),
                encoding="utf-8",
            )
            errors = module.validate_production_ota_config(root)
        finally:
            overlay.write_text(original_text, encoding="utf-8")
        self.assertTrue(
            any("CONFIG_BOOTLOADER_APP_SECURE_VERSION" in error for error in errors),
            errors,
        )

    def test_partition_size_parser_accepts_platformio_units(self) -> None:
        self.assertEqual(parse_partition_size("4M"), 4 * 1024 * 1024)
        self.assertEqual(parse_partition_size("4096K"), 4 * 1024 * 1024)
        self.assertEqual(parse_partition_size("0x400000"), 4 * 1024 * 1024)
        with self.assertRaises(ValueError):
            parse_partition_size("four-megabytes")


if __name__ == "__main__":
    raise SystemExit(main())
