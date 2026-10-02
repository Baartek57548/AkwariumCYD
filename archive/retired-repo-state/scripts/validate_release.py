#!/usr/bin/env python3
"""Validate AquaCYD firmware release contracts and create checksum metadata."""

from __future__ import annotations

import argparse
import dataclasses
import datetime as dt
import hashlib
import json
import os
import pathlib
import re
import tempfile
from collections.abc import Sequence


TAG_PATTERN = re.compile(
    r"^firmware-v"
    r"(?P<version>(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*))$"
)
FIRMWARE_TARGETS = ("ili9341", "st7789")
FIRMWARE_PRODUCT_ID = "aquacyd-cyd"
FIRMWARE_PACKAGE_FORMAT = "aqfw-v1"
FIRMWARE_SECURE_BOOT_SCHEME = "esp32-secure-boot-v2-rsa3072-pss-sha256"
FIRMWARE_INFO_NAMESPACE_PATTERN = re.compile(
    r"namespace\s+FirmwareInfo\s*\{(?P<body>.*?)\}",
    re.DOTALL,
)
FIRMWARE_VERSION_PATTERN = re.compile(
    r'constexpr\s+char\s+VERSION\[\]\s*=\s*"'
    r"(?P<value>(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*))"
    r'"\s*;'
)
FIRMWARE_SECURITY_VERSION_PATTERN = re.compile(
    r"constexpr\s+uint32_t\s+SECURITY_VERSION\s*=\s*"
    r"(?P<value>(?:0|[1-9]\d*))U?\s*;"
)
FIRMWARE_BOOTLOADER_VERSION_PATTERN = re.compile(
    r"constexpr\s+uint16_t\s+BOOTLOADER_COMPATIBILITY_VERSION\s*=\s*"
    r"(?P<value>(?:0|[1-9]\d*))U?\s*;"
)


class ValidationError(RuntimeError):
    """A release would violate the firmware update or artifact contract."""


@dataclasses.dataclass(frozen=True)
class ReleaseIdentity:
    kind: str
    version: str


@dataclasses.dataclass(frozen=True)
class FirmwareContract:
    version: str
    security_version: int
    minimum_bootloader_version: int
    product_id: str = FIRMWARE_PRODUCT_ID
    package_format: str = FIRMWARE_PACKAGE_FORMAT
    secure_boot_scheme: str = FIRMWARE_SECURE_BOOT_SCHEME


@dataclasses.dataclass(frozen=True)
class Asset:
    path: pathlib.Path
    name: str
    size: int
    sha256: str


def parse_tag(tag: str) -> ReleaseIdentity:
    match = TAG_PATTERN.fullmatch(tag.strip())
    if not match:
        raise ValidationError(
            "tag must be firmware-vX.Y.Z with canonical non-negative integers"
        )
    return ReleaseIdentity("firmware", match.group("version"))


def read_firmware_contract(config_path: pathlib.Path) -> FirmwareContract:
    try:
        content = config_path.read_text(encoding="utf-8")
    except OSError as error:
        raise ValidationError(f"cannot read {config_path}: {error}") from error

    namespace = FIRMWARE_INFO_NAMESPACE_PATTERN.search(content)
    if not namespace:
        raise ValidationError("config must contain namespace FirmwareInfo")
    body = namespace.group("body")

    version_match = FIRMWARE_VERSION_PATTERN.search(body)
    security_match = FIRMWARE_SECURITY_VERSION_PATTERN.search(body)
    bootloader_match = FIRMWARE_BOOTLOADER_VERSION_PATTERN.search(body)
    if not version_match:
        raise ValidationError(
            "FirmwareInfo::VERSION must contain canonical semantic version X.Y.Z"
        )
    if not security_match:
        raise ValidationError(
            "FirmwareInfo::SECURITY_VERSION must be an unsigned integer"
        )
    if not bootloader_match:
        raise ValidationError(
            "FirmwareInfo::BOOTLOADER_COMPATIBILITY_VERSION must be an "
            "unsigned integer"
        )

    security_version = int(security_match.group("value"))
    minimum_bootloader_version = int(bootloader_match.group("value"))
    if not 1 <= security_version <= 0xFFFFFFFF:
        raise ValidationError(
            "FirmwareInfo::SECURITY_VERSION must be in range 1..4294967295"
        )
    if not 1 <= minimum_bootloader_version <= 0xFFFF:
        raise ValidationError(
            "FirmwareInfo::BOOTLOADER_COMPATIBILITY_VERSION must be in "
            "range 1..65535"
        )
    return FirmwareContract(
        version=version_match.group("value"),
        security_version=security_version,
        minimum_bootloader_version=minimum_bootloader_version,
    )


def expected_asset_names(identity: ReleaseIdentity) -> list[str]:
    names: list[str] = []
    for target in FIRMWARE_TARGETS:
        names.extend(
            [
                f"AquaCYD-Firmware-{identity.version}-{target}-sbv2.bin",
                f"AquaCYD-Firmware-{identity.version}-{target}.aqfw",
            ]
        )
    return names


def firmware_asset_identity(
    asset_name: str,
    identity: ReleaseIdentity,
) -> tuple[str, str]:
    escaped_version = re.escape(identity.version)
    match = re.fullmatch(
        rf"AquaCYD-Firmware-{escaped_version}-"
        r"(?P<target>ili9341|st7789)"
        r"(?P<suffix>-sbv2\.bin|\.aqfw)",
        asset_name,
    )
    if not match:
        raise ValidationError(f"invalid firmware asset name: {asset_name}")
    role = (
        "secureBootPayload"
        if match.group("suffix") == "-sbv2.bin"
        else "otaPackage"
    )
    return match.group("target"), role


def inspect_assets(paths: Sequence[pathlib.Path]) -> list[Asset]:
    assets: list[Asset] = []
    seen_names: set[str] = set()
    for original in paths:
        path = original.resolve()
        if path.name in seen_names:
            raise ValidationError(f"duplicate asset name: {path.name}")
        seen_names.add(path.name)
        if not path.is_file():
            raise ValidationError(f"release asset does not exist: {path}")
        size = path.stat().st_size
        if size <= 0:
            raise ValidationError(f"release asset is empty: {path}")
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        assets.append(Asset(path, path.name, size, digest.hexdigest()))
    return assets


def validate_release(
    *,
    tag: str,
    paths: Sequence[pathlib.Path],
    firmware_config_path: pathlib.Path,
) -> tuple[ReleaseIdentity, list[Asset], FirmwareContract]:
    identity = parse_tag(tag)
    assets = inspect_assets(paths)
    actual_names = sorted(asset.name for asset in assets)
    expected_names = sorted(expected_asset_names(identity))
    if actual_names != expected_names:
        raise ValidationError(
            f"asset names must be exactly {expected_names}; got {actual_names}"
        )

    contract = read_firmware_contract(firmware_config_path)
    if contract.version != identity.version:
        raise ValidationError(
            f"tag version {identity.version} does not match "
            f"FirmwareInfo::VERSION {contract.version}"
        )

    roles_by_target: dict[str, set[str]] = {
        target: set() for target in FIRMWARE_TARGETS
    }
    for asset in assets:
        target, role = firmware_asset_identity(asset.name, identity)
        roles_by_target[target].add(role)
    expected_roles = {"secureBootPayload", "otaPackage"}
    for target, roles in roles_by_target.items():
        if roles != expected_roles:
            raise ValidationError(
                f"firmware target {target} must contain roles "
                f"{sorted(expected_roles)}; got {sorted(roles)}"
            )
    return identity, assets, contract


def write_metadata(
    *,
    identity: ReleaseIdentity,
    assets: Sequence[Asset],
    contract: FirmwareContract,
    checksums_path: pathlib.Path,
    manifest_path: pathlib.Path,
) -> None:
    checksums_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    checksum_lines = [
        f"{asset.sha256}  {asset.name}"
        for asset in sorted(assets, key=lambda item: item.name)
    ]
    checksums_path.write_text(
        "\n".join(checksum_lines) + "\n",
        encoding="utf-8",
    )

    target_assets: dict[str, dict[str, str]] = {
        target: {} for target in FIRMWARE_TARGETS
    }
    asset_entries: list[dict[str, object]] = []
    for asset in sorted(assets, key=lambda item: item.name):
        target, role = firmware_asset_identity(asset.name, identity)
        target_assets[target][role] = asset.name
        asset_entries.append(
            {
                "name": asset.name,
                "bytes": asset.size,
                "sha256": asset.sha256,
                "target": target,
                "role": role,
            }
        )

    generated_at = (
        dt.datetime.now(dt.timezone.utc)
        .replace(microsecond=0)
        .isoformat()
    )
    manifest = {
        "schemaVersion": 2,
        "tag": f"firmware-v{identity.version}",
        "kind": "firmware",
        "version": identity.version,
        "buildNumber": None,
        "commit": os.getenv("GITHUB_SHA", "").strip() or None,
        "generatedAt": generated_at,
        "assets": asset_entries,
        "firmware": {
            "productId": contract.product_id,
            "securityVersion": contract.security_version,
            "minimumBootloaderVersion": contract.minimum_bootloader_version,
            "secureBootScheme": contract.secure_boot_scheme,
            "packageFormat": contract.package_format,
            "targets": target_assets,
        },
    }
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def run_self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="aquacyd-release-selftest-") as temp:
        root = pathlib.Path(temp)
        config = root / "config.h"
        config.write_text(
            "namespace FirmwareInfo {\n"
            'constexpr char VERSION[] = "5.1.0";\n'
            "constexpr uint8_t API_VERSION = 2U;\n"
            "constexpr uint32_t SECURITY_VERSION = 7U;\n"
            "constexpr uint16_t BOOTLOADER_COMPATIBILITY_VERSION = 3U;\n"
            "} // namespace FirmwareInfo\n",
            encoding="utf-8",
        )
        identity = parse_tag("firmware-v5.1.0")
        paths: list[pathlib.Path] = []
        for name in expected_asset_names(identity):
            asset = root / name
            asset.write_bytes(f"self-test:{name}".encode("ascii"))
            paths.append(asset)

        checked_identity, assets, contract = validate_release(
            tag="firmware-v5.1.0",
            paths=paths,
            firmware_config_path=config,
        )
        if checked_identity != identity or contract.security_version != 7:
            raise ValidationError("self-test firmware contract mismatch")

        checksums = root / "SHA256SUMS"
        manifest = root / "release-manifest.json"
        write_metadata(
            identity=identity,
            assets=assets,
            contract=contract,
            checksums_path=checksums,
            manifest_path=manifest,
        )
        decoded = json.loads(manifest.read_text(encoding="utf-8"))
        roles = {
            (asset["target"], asset["role"])
            for asset in decoded["assets"]
        }
        if (
            decoded["schemaVersion"] != 2
            or decoded["kind"] != "firmware"
            or decoded["firmware"]["productId"] != FIRMWARE_PRODUCT_ID
            or decoded["firmware"]["securityVersion"] != 7
            or decoded["firmware"]["minimumBootloaderVersion"] != 3
            or set(decoded["firmware"]["targets"]) != set(FIRMWARE_TARGETS)
            or roles
            != {
                (target, role)
                for target in FIRMWARE_TARGETS
                for role in ("secureBootPayload", "otaPackage")
            }
        ):
            raise ValidationError("self-test firmware manifest mismatch")
        if len(checksums.read_text(encoding="utf-8").splitlines()) != 4:
            raise ValidationError("self-test checksum count mismatch")

        try:
            parse_tag("home-v5.1.0")
        except ValidationError:
            pass
        else:
            raise ValidationError("self-test accepted a removed release product")

        try:
            validate_release(
                tag="firmware-v5.2.0",
                paths=paths,
                firmware_config_path=config,
            )
        except ValidationError:
            pass
        else:
            raise ValidationError(
                "self-test accepted firmware tag/source version mismatch"
            )

    print("Firmware release validator self-test passed")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Validate firmware tag, version, asset names and SHA-256 metadata."
        )
    )
    parser.add_argument("--tag", help="firmware-vX.Y.Z")
    parser.add_argument(
        "--asset",
        action="append",
        default=[],
        help="asset path; repeat for all four required firmware artifacts",
    )
    parser.add_argument(
        "--firmware-config",
        default="firmware/cyd_controller/include/config.h",
        help="configuration containing the FirmwareInfo contract",
    )
    parser.add_argument("--checksums", default="SHA256SUMS")
    parser.add_argument("--manifest", default="release-manifest.json")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--dry-run", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    mode.add_argument(
        "--print-firmware-contract",
        action="store_true",
        help="validate a firmware tag and print its source contract as JSON",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.self_test:
            return run_self_test()
        if not args.tag:
            raise ValidationError("--tag is required")

        identity = parse_tag(args.tag)
        config_path = pathlib.Path(args.firmware_config)
        contract = read_firmware_contract(config_path)
        if contract.version != identity.version:
            raise ValidationError(
                f"tag version {identity.version} does not match "
                f"FirmwareInfo::VERSION {contract.version}"
            )

        if args.print_firmware_contract:
            print(json.dumps(dataclasses.asdict(contract), sort_keys=True))
            return 0

        if args.dry_run:
            print(
                "Firmware contract valid: "
                f"{contract.version}, securityVersion={contract.security_version}, "
                "minimumBootloaderVersion="
                f"{contract.minimum_bootloader_version}"
            )
            print("Expected assets:")
            for name in expected_asset_names(identity):
                print(f"- {name}")
            print("Dry-run completed; no files were created")
            return 0

        checked_identity, assets, checked_contract = validate_release(
            tag=args.tag,
            paths=[pathlib.Path(item) for item in args.asset],
            firmware_config_path=config_path,
        )
        write_metadata(
            identity=checked_identity,
            assets=assets,
            contract=checked_contract,
            checksums_path=pathlib.Path(args.checksums),
            manifest_path=pathlib.Path(args.manifest),
        )
        print(
            f"Validated firmware {checked_identity.version}: "
            f"{len(assets)} assets, SHA-256 metadata written"
        )
        return 0
    except ValidationError as error:
        print(f"Release validation failed: {error}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
