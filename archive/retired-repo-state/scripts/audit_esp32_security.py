#!/usr/bin/env python3
"""Read and sanitize ESP32 security state without writing any eFuse."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import sys
import tempfile
from typing import Any


class AuditError(RuntimeError):
    """The read-only security audit could not be completed."""


def find_platformio_tool(name: str) -> pathlib.Path:
    home = pathlib.Path.home()
    candidates = sorted(
        (home / ".platformio" / "packages").glob(f"**/{name}")
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise AuditError(
        f"{name} was not found below {home / '.platformio' / 'packages'}"
    )


def run_read_only(arguments: list[str]) -> str:
    result = subprocess.run(
        arguments,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise AuditError(
            f"read-only command failed ({result.returncode}): {detail}"
        )
    return result.stdout


def field_value(fields: dict[str, Any], name: str, default: Any = None) -> Any:
    field = fields.get(name)
    return field.get("value", default) if isinstance(field, dict) else default


def field_flag(fields: dict[str, Any], name: str) -> bool:
    return field_value(fields, name, False) is True


def field_writeable(fields: dict[str, Any], name: str) -> bool:
    field = fields.get(name)
    return field.get("writeable", True) is True if isinstance(field, dict) else True


def parse_revision(chip_information: str) -> tuple[int | None, int | None]:
    match = re.search(
        r"(?:revision|Revision)\s+v?(?P<major>\d+)"
        r"(?:\.(?P<minor>\d+))?",
        chip_information,
    )
    if not match:
        return None, None
    return int(match.group("major")), int(match.group("minor") or 0)


def analyze(fields: dict[str, Any], chip_information: str) -> dict[str, Any]:
    flash_crypt_count = field_value(fields, "FLASH_CRYPT_CNT", 0)
    if not isinstance(flash_crypt_count, int) or flash_crypt_count < 0:
        raise AuditError("FLASH_CRYPT_CNT is not a non-negative integer")
    flash_encryption_enabled = flash_crypt_count.bit_count() % 2 == 1
    secure_boot_v2_enabled = field_flag(fields, "ABS_DONE_1")
    download_encrypt_disabled = field_flag(fields, "DISABLE_DL_ENCRYPT")
    download_decrypt_disabled = field_flag(fields, "DISABLE_DL_DECRYPT")
    download_cache_disabled = field_flag(fields, "DISABLE_DL_CACHE")
    uart_download_disabled = field_flag(fields, "UART_DOWNLOAD_DIS")
    jtag_disabled = field_flag(fields, "JTAG_DISABLE")
    coding_scheme = field_value(fields, "CODING_SCHEME")
    coding_scheme_compatible = (
        isinstance(coding_scheme, str)
        and coding_scheme.upper().startswith("NONE")
    )
    revision_major, revision_minor = parse_revision(chip_information)
    secure_boot_v2_capable = (
        revision_major is not None
        and revision_major >= 3
        and coding_scheme_compatible
    )

    warnings: list[str] = []
    if revision_major is None:
        warnings.append("chip revision could not be parsed")
    elif revision_major < 3:
        warnings.append("ESP32 revision is below ECO3; Secure Boot v2 is unsupported")
    if not secure_boot_v2_enabled:
        warnings.append("Secure Boot v2 is not enabled")
    if not flash_encryption_enabled:
        warnings.append("Flash Encryption is not enabled")
    if secure_boot_v2_enabled and not flash_encryption_enabled:
        warnings.append("Secure Boot v2 is enabled without Flash Encryption")
    if flash_encryption_enabled and not (
        download_encrypt_disabled
        and download_decrypt_disabled
        and download_cache_disabled
    ):
        warnings.append("Flash Encryption is not locked in production release mode")
    if secure_boot_v2_enabled and field_writeable(fields, "BLOCK2"):
        warnings.append("Secure Boot key digest block is still writeable")
    if not coding_scheme_compatible:
        warnings.append("eFuse coding scheme is incompatible with Secure Boot v2")
    if not jtag_disabled:
        warnings.append("JTAG remains enabled")
    if not uart_download_disabled:
        warnings.append("UART ROM download mode remains enabled")

    return {
        "schemaVersion": 1,
        "readOnly": True,
        "chipRevision": (
            f"{revision_major}.{revision_minor}"
            if revision_major is not None
            else None
        ),
        "secureBootV2Capable": secure_boot_v2_capable,
        "secureBootV2Enabled": secure_boot_v2_enabled,
        "flashEncryptionEnabled": flash_encryption_enabled,
        "flashEncryptionReleaseMode": (
            flash_encryption_enabled
            and download_encrypt_disabled
            and download_decrypt_disabled
            and download_cache_disabled
        ),
        "downloadCacheDisabled": download_cache_disabled,
        "uartDownloadDisabled": uart_download_disabled,
        "jtagDisabled": jtag_disabled,
        "codingScheme": coding_scheme,
        "codingSchemeCompatible": coding_scheme_compatible,
        "warnings": warnings,
    }


def audit_device(port: str) -> dict[str, Any]:
    if not port.strip():
        raise AuditError("serial port must not be empty")
    esptool = find_platformio_tool("esptool.py")
    espefuse = find_platformio_tool("espefuse.py")
    chip_information = run_read_only(
        [
            sys.executable,
            str(esptool),
            "--chip",
            "esp32",
            "--port",
            port,
            "chip_id",
        ]
    )
    with tempfile.TemporaryDirectory(prefix="aquacyd-efuse-audit-") as temp:
        report_path = pathlib.Path(temp) / "efuse-summary.json"
        run_read_only(
            [
                sys.executable,
                str(espefuse),
                "--chip",
                "esp32",
                "--port",
                port,
                "summary",
                "--format",
                "json",
                "--file",
                str(report_path),
            ]
        )
        fields = json.loads(report_path.read_text(encoding="utf-8"))
    if not isinstance(fields, dict):
        raise AuditError("espefuse summary is not a JSON object")
    return analyze(fields, chip_information)


def audit_saved_report(path: pathlib.Path, chip_information: str) -> dict[str, Any]:
    try:
        fields = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise AuditError(f"cannot read eFuse JSON report: {error}") from error
    if not isinstance(fields, dict):
        raise AuditError("saved eFuse report is not a JSON object")
    return analyze(fields, chip_information)


def self_test() -> None:
    fields = {
        "ABS_DONE_1": {"value": True},
        "FLASH_CRYPT_CNT": {"value": 1},
        "DISABLE_DL_ENCRYPT": {"value": True},
        "DISABLE_DL_DECRYPT": {"value": True},
        "DISABLE_DL_CACHE": {"value": True},
        "UART_DOWNLOAD_DIS": {"value": True},
        "JTAG_DISABLE": {"value": True},
        "CODING_SCHEME": {"value": "NONE (BLK1-3 len=256 bits)"},
        "BLOCK2": {"value": "redacted", "writeable": False},
    }
    result = analyze(fields, "Chip is ESP32-D0WD-V3 (revision v3.1)")
    if (
        result["chipRevision"] != "3.1"
        or result["secureBootV2Enabled"] is not True
        or result["flashEncryptionReleaseMode"] is not True
        or result["warnings"]
    ):
        raise AuditError("self-test rejected a secure production fixture")

    insecure = analyze(
        {
            "ABS_DONE_1": {"value": False},
            "FLASH_CRYPT_CNT": {"value": 0},
            "DISABLE_DL_ENCRYPT": {"value": False},
            "DISABLE_DL_DECRYPT": {"value": False},
            "DISABLE_DL_CACHE": {"value": False},
            "UART_DOWNLOAD_DIS": {"value": False},
            "JTAG_DISABLE": {"value": False},
            "BLOCK2": {"value": "", "writeable": True},
        },
        "Chip is ESP32-D0WDQ6 (revision 1)",
    )
    if insecure["secureBootV2Capable"] is not False or not insecure["warnings"]:
        raise AuditError("self-test accepted an unsupported ESP32 revision")
    print("Read-only ESP32 security audit self-test passed")


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Read ESP32 chip/eFuse security state. This tool contains no "
            "provisioning or eFuse-write operation."
        )
    )
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--port", help="serial port read by esptool/espefuse")
    source.add_argument("--efuse-json", help="previous espefuse JSON summary")
    source.add_argument("--self-test", action="store_true")
    parser.add_argument(
        "--chip-information",
        default="",
        help="saved esptool chip_id output used with --efuse-json",
    )
    parser.add_argument("--output", help="optional sanitized JSON output path")
    parser.add_argument(
        "--require-secure",
        action="store_true",
        help="fail unless Secure Boot v2 and Flash Encryption release mode are active",
    )
    args = parser.parse_args()

    try:
        if args.self_test:
            self_test()
            return 0
        if args.port:
            report = audit_device(args.port)
        else:
            report = audit_saved_report(
                pathlib.Path(args.efuse_json).resolve(),
                args.chip_information,
            )
        encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.output:
            output = pathlib.Path(args.output).resolve()
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(encoded, encoding="utf-8")
        print(encoded, end="")
        if args.require_secure and (
            report["secureBootV2Capable"] is not True
            or report["secureBootV2Enabled"] is not True
            or report["flashEncryptionReleaseMode"] is not True
            or report["warnings"]
        ):
            raise AuditError("device does not satisfy the production security gate")
        return 0
    except (AuditError, OSError, subprocess.SubprocessError) as error:
        print(f"ESP32 security audit failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
