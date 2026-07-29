#!/usr/bin/env python3
"""Validate AquaCYD release contracts and create checksum metadata."""

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
    r"^(?P<kind>mobile|firmware)-v"
    r"(?P<version>(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*))$"
)
PUBSPEC_VERSION_PATTERN = re.compile(
    r"^version:\s*"
    r"(?P<version>(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*))"
    r"\+(?P<build>(?:0|[1-9]\d*))\s*$",
    re.MULTILINE,
)
EXPECTED_PACKAGE = "pl.cydakwarium.cyd_aquarium_mobile"
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
    """A release would violate the public update or artifact contract."""


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
            "tag must be mobile-vX.Y.Z or firmware-vX.Y.Z with canonical integers"
        )
    return ReleaseIdentity(match.group("kind"), match.group("version"))


def read_mobile_version(pubspec_path: pathlib.Path) -> tuple[str, int]:
    try:
        content = pubspec_path.read_text(encoding="utf-8")
    except OSError as error:
        raise ValidationError(f"cannot read {pubspec_path}: {error}") from error
    match = PUBSPEC_VERSION_PATTERN.search(content)
    if not match:
        raise ValidationError("pubspec.yaml must contain canonical version X.Y.Z+N")
    return match.group("version"), int(match.group("build"))


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
    if identity.kind == "mobile":
        return [f"AquaCYD-Control-{identity.version}-current.apk"]
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
    pubspec_path: pathlib.Path,
    package_name: str,
    apk_version_name: str,
    apk_version_code: str,
    firmware_config_path: pathlib.Path,
) -> tuple[
    ReleaseIdentity,
    list[Asset],
    int | None,
    FirmwareContract | None,
]:
    identity = parse_tag(tag)
    assets = inspect_assets(paths)
    actual_names = sorted(asset.name for asset in assets)
    expected_names = sorted(expected_asset_names(identity))
    if actual_names != expected_names:
        raise ValidationError(
            f"asset names must be exactly {expected_names}; got {actual_names}"
        )
    build_number: int | None = None
    firmware_contract: FirmwareContract | None = None
    if identity.kind == "mobile":
        pubspec_version, build_number = read_mobile_version(pubspec_path)
        if pubspec_version != identity.version:
            raise ValidationError(
                f"tag version {identity.version} does not match pubspec {pubspec_version}"
            )
        if package_name != EXPECTED_PACKAGE:
            raise ValidationError(
                f"APK package must be {EXPECTED_PACKAGE}; got {package_name or '<empty>'}"
            )
        if apk_version_name != identity.version:
            raise ValidationError(
                f"APK versionName must be {identity.version}; "
                f"got {apk_version_name or '<empty>'}"
            )
        if not apk_version_code.isdecimal() or int(apk_version_code) != build_number:
            raise ValidationError(
                f"APK versionCode must be {build_number}; "
                f"got {apk_version_code or '<empty>'}"
            )
    else:
        firmware_contract = read_firmware_contract(firmware_config_path)
        if firmware_contract.version != identity.version:
            raise ValidationError(
                f"tag version {identity.version} does not match "
                f"FirmwareInfo::VERSION {firmware_contract.version}"
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
    return identity, assets, build_number, firmware_contract


def write_metadata(
    *,
    identity: ReleaseIdentity,
    assets: Sequence[Asset],
    build_number: int | None,
    firmware_contract: FirmwareContract | None,
    checksums_path: pathlib.Path,
    manifest_path: pathlib.Path,
) -> None:
    checksums_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    checksum_lines = [
        f"{asset.sha256}  {asset.name}" for asset in sorted(assets, key=lambda a: a.name)
    ]
    checksums_path.write_text("\n".join(checksum_lines) + "\n", encoding="utf-8")
    generated_at = dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()
    manifest: dict[str, object] = {
        "schemaVersion": 2 if firmware_contract is not None else 1,
        "tag": f"{identity.kind}-v{identity.version}",
        "kind": identity.kind,
        "version": identity.version,
        "buildNumber": build_number,
        "commit": os.getenv("GITHUB_SHA", "").strip() or None,
        "generatedAt": generated_at,
    }
    asset_entries: list[dict[str, object]] = []
    target_assets: dict[str, dict[str, str]] = {
        target: {} for target in FIRMWARE_TARGETS
    }
    for asset in sorted(assets, key=lambda item: item.name):
        entry: dict[str, object] = {
            "name": asset.name,
            "bytes": asset.size,
            "sha256": asset.sha256,
        }
        if firmware_contract is not None:
            target, role = firmware_asset_identity(asset.name, identity)
            entry["target"] = target
            entry["role"] = role
            target_assets[target][role] = asset.name
        asset_entries.append(entry)
    manifest["assets"] = asset_entries
    if firmware_contract is not None:
        manifest["firmware"] = {
            "productId": firmware_contract.product_id,
            "securityVersion": firmware_contract.security_version,
            "minimumBootloaderVersion": (
                firmware_contract.minimum_bootloader_version
            ),
            "secureBootScheme": firmware_contract.secure_boot_scheme,
            "packageFormat": firmware_contract.package_format,
            "targets": target_assets,
        }
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def run_self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="aquacyd-release-selftest-") as temp:
        root = pathlib.Path(temp)
        pubspec = root / "pubspec.yaml"
        pubspec.write_text("name: sample\nversion: 4.2.1+17\n", encoding="utf-8")
        apk = root / "AquaCYD-Control-4.2.1-current.apk"
        apk.write_bytes(b"self-test-apk")
        identity, assets, build_number, firmware_contract = validate_release(
            tag="mobile-v4.2.1",
            paths=[apk],
            pubspec_path=pubspec,
            package_name=EXPECTED_PACKAGE,
            apk_version_name="4.2.1",
            apk_version_code="17",
            firmware_config_path=root / "config.h",
        )
        checksums = root / "SHA256SUMS"
        manifest = root / "release-manifest.json"
        write_metadata(
            identity=identity,
            assets=assets,
            build_number=build_number,
            firmware_contract=firmware_contract,
            checksums_path=checksums,
            manifest_path=manifest,
        )
        checksum_line = checksums.read_text(encoding="utf-8").strip()
        expected_digest = hashlib.sha256(b"self-test-apk").hexdigest()
        if checksum_line != f"{expected_digest}  {apk.name}":
            raise ValidationError("self-test checksum mismatch")
        decoded = json.loads(manifest.read_text(encoding="utf-8"))
        if decoded["version"] != "4.2.1" or decoded["buildNumber"] != 17:
            raise ValidationError("self-test manifest mismatch")
        try:
            validate_release(
                tag="mobile-v4.2.0",
                paths=[apk],
                pubspec_path=pubspec,
                package_name=EXPECTED_PACKAGE,
                apk_version_name="4.2.1",
                apk_version_code="17",
                firmware_config_path=root / "config.h",
            )
        except ValidationError:
            pass
        else:
            raise ValidationError("self-test accepted a mismatched release")

        firmware_config = root / "config.h"
        firmware_config.write_text(
            "namespace FirmwareInfo {\n"
            'constexpr char VERSION[] = "5.1.0";\n'
            "constexpr uint8_t API_VERSION = 2U;\n"
            "constexpr uint32_t SECURITY_VERSION = 7U;\n"
            "constexpr uint16_t BOOTLOADER_COMPATIBILITY_VERSION = 3U;\n"
            "} // namespace FirmwareInfo\n",
            encoding="utf-8",
        )
        firmware_identity = parse_tag("firmware-v5.1.0")
        firmware_paths: list[pathlib.Path] = []
        for name in expected_asset_names(firmware_identity):
            asset = root / name
            asset.write_bytes(f"self-test:{name}".encode("ascii"))
            firmware_paths.append(asset)
        (
            checked_identity,
            firmware_assets,
            firmware_build,
            checked_contract,
        ) = validate_release(
            tag="firmware-v5.1.0",
            paths=firmware_paths,
            pubspec_path=pubspec,
            package_name="",
            apk_version_name="",
            apk_version_code="",
            firmware_config_path=firmware_config,
        )
        if (
            checked_identity != firmware_identity
            or firmware_build is not None
            or checked_contract is None
            or checked_contract.security_version != 7
            or checked_contract.minimum_bootloader_version != 3
        ):
            raise ValidationError("self-test firmware contract mismatch")
        firmware_checksums = root / "FIRMWARE_SHA256SUMS"
        firmware_manifest = root / "firmware-release-manifest.json"
        write_metadata(
            identity=checked_identity,
            assets=firmware_assets,
            build_number=None,
            firmware_contract=checked_contract,
            checksums_path=firmware_checksums,
            manifest_path=firmware_manifest,
        )
        decoded_firmware = json.loads(
            firmware_manifest.read_text(encoding="utf-8")
        )
        firmware_section = decoded_firmware["firmware"]
        manifest_roles = {
            (asset["target"], asset["role"])
            for asset in decoded_firmware["assets"]
        }
        if (
            decoded_firmware["schemaVersion"] != 2
            or firmware_section["productId"] != FIRMWARE_PRODUCT_ID
            or firmware_section["packageFormat"] != FIRMWARE_PACKAGE_FORMAT
            or firmware_section["secureBootScheme"]
            != FIRMWARE_SECURE_BOOT_SCHEME
            or firmware_section["securityVersion"] != 7
            or firmware_section["minimumBootloaderVersion"] != 3
            or set(firmware_section["targets"]) != set(FIRMWARE_TARGETS)
            or manifest_roles
            != {
                (target, role)
                for target in FIRMWARE_TARGETS
                for role in ("secureBootPayload", "otaPackage")
            }
        ):
            raise ValidationError("self-test firmware manifest mismatch")
        mismatched_identity = parse_tag("firmware-v5.2.0")
        mismatched_paths: list[pathlib.Path] = []
        for name in expected_asset_names(mismatched_identity):
            asset = root / name
            asset.write_bytes(f"self-test:{name}".encode("ascii"))
            mismatched_paths.append(asset)
        try:
            validate_release(
                tag="firmware-v5.2.0",
                paths=mismatched_paths,
                pubspec_path=pubspec,
                package_name="",
                apk_version_name="",
                apk_version_code="",
                firmware_config_path=firmware_config,
            )
        except ValidationError:
            pass
        else:
            raise ValidationError(
                "self-test accepted firmware tag/source version mismatch"
            )
    print("Release validator self-test passed")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate release tag, version, asset names and SHA-256 metadata."
    )
    parser.add_argument("--tag", help="mobile-vX.Y.Z or firmware-vX.Y.Z")
    parser.add_argument(
        "--asset",
        action="append",
        default=[],
        help="asset path; repeat for every required release asset",
    )
    parser.add_argument(
        "--pubspec",
        default="mobile_app/pubspec.yaml",
        help="mobile pubspec used as the version source",
    )
    parser.add_argument(
        "--firmware-config",
        default="include/config.h",
        help="firmware configuration containing the FirmwareInfo contract",
    )
    parser.add_argument("--package-name", default="")
    parser.add_argument("--apk-version-name", default="")
    parser.add_argument("--apk-version-code", default="")
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
        firmware_config_path = pathlib.Path(args.firmware_config)
        if args.print_firmware_contract:
            if identity.kind != "firmware":
                raise ValidationError(
                    "--print-firmware-contract requires a firmware-vX.Y.Z tag"
                )
            contract = read_firmware_contract(firmware_config_path)
            if contract.version != identity.version:
                raise ValidationError(
                    f"tag version {identity.version} does not match "
                    f"FirmwareInfo::VERSION {contract.version}"
                )
            print(json.dumps(dataclasses.asdict(contract), sort_keys=True))
            return 0
        if args.dry_run:
            if identity.kind == "mobile":
                version, build = read_mobile_version(pathlib.Path(args.pubspec))
                if version != identity.version:
                    raise ValidationError(
                        f"tag version {identity.version} does not match pubspec {version}"
                    )
                print(f"Mobile version contract valid: {version}+{build}")
            else:
                contract = read_firmware_contract(firmware_config_path)
                if contract.version != identity.version:
                    raise ValidationError(
                        f"tag version {identity.version} does not match "
                        f"FirmwareInfo::VERSION {contract.version}"
                    )
                print(
                    "Firmware contract valid: "
                    f"{contract.version}, securityVersion="
                    f"{contract.security_version}, minimumBootloaderVersion="
                    f"{contract.minimum_bootloader_version}"
                )
            print("Expected assets:")
            for name in expected_asset_names(identity):
                print(f"- {name}")
            print("Dry-run completed; no files were created")
            return 0
        identity, assets, build_number, firmware_contract = validate_release(
            tag=args.tag,
            paths=[pathlib.Path(item) for item in args.asset],
            pubspec_path=pathlib.Path(args.pubspec),
            package_name=args.package_name,
            apk_version_name=args.apk_version_name,
            apk_version_code=args.apk_version_code,
            firmware_config_path=firmware_config_path,
        )
        write_metadata(
            identity=identity,
            assets=assets,
            build_number=build_number,
            firmware_contract=firmware_contract,
            checksums_path=pathlib.Path(args.checksums),
            manifest_path=pathlib.Path(args.manifest),
        )
        print(
            f"Validated {identity.kind} {identity.version}: "
            f"{len(assets)} asset(s), SHA-256 metadata written"
        )
        return 0
    except ValidationError as error:
        print(f"Release validation failed: {error}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
