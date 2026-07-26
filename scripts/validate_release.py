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


class ValidationError(RuntimeError):
    """A release would violate the public update or artifact contract."""


@dataclasses.dataclass(frozen=True)
class ReleaseIdentity:
    kind: str
    version: str


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


def expected_asset_names(identity: ReleaseIdentity) -> list[str]:
    if identity.kind == "mobile":
        return [f"AquaCYD-Control-{identity.version}-current.apk"]
    return [
        f"AquaCYD-Firmware-{identity.version}-ili9341.bin",
        f"AquaCYD-Firmware-{identity.version}-st7789.bin",
    ]


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
) -> tuple[ReleaseIdentity, list[Asset], int | None]:
    identity = parse_tag(tag)
    assets = inspect_assets(paths)
    actual_names = sorted(asset.name for asset in assets)
    expected_names = sorted(expected_asset_names(identity))
    if actual_names != expected_names:
        raise ValidationError(
            f"asset names must be exactly {expected_names}; got {actual_names}"
        )
    build_number: int | None = None
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
    return identity, assets, build_number


def write_metadata(
    *,
    identity: ReleaseIdentity,
    assets: Sequence[Asset],
    build_number: int | None,
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
    manifest = {
        "schemaVersion": 1,
        "tag": f"{identity.kind}-v{identity.version}",
        "kind": identity.kind,
        "version": identity.version,
        "buildNumber": build_number,
        "commit": os.getenv("GITHUB_SHA", "").strip() or None,
        "generatedAt": generated_at,
        "assets": [
            {
                "name": asset.name,
                "bytes": asset.size,
                "sha256": asset.sha256,
            }
            for asset in sorted(assets, key=lambda a: a.name)
        ],
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
        identity, assets, build_number = validate_release(
            tag="mobile-v4.2.1",
            paths=[apk],
            pubspec_path=pubspec,
            package_name=EXPECTED_PACKAGE,
            apk_version_name="4.2.1",
            apk_version_code="17",
        )
        checksums = root / "SHA256SUMS"
        manifest = root / "release-manifest.json"
        write_metadata(
            identity=identity,
            assets=assets,
            build_number=build_number,
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
            )
        except ValidationError:
            pass
        else:
            raise ValidationError("self-test accepted a mismatched release")
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
    parser.add_argument("--package-name", default="")
    parser.add_argument("--apk-version-name", default="")
    parser.add_argument("--apk-version-code", default="")
    parser.add_argument("--checksums", default="SHA256SUMS")
    parser.add_argument("--manifest", default="release-manifest.json")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--dry-run", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.self_test:
            return run_self_test()
        if not args.tag:
            raise ValidationError("--tag is required")
        identity = parse_tag(args.tag)
        if args.dry_run:
            if identity.kind == "mobile":
                version, build = read_mobile_version(pathlib.Path(args.pubspec))
                if version != identity.version:
                    raise ValidationError(
                        f"tag version {identity.version} does not match pubspec {version}"
                    )
                print(f"Mobile version contract valid: {version}+{build}")
            print("Expected assets:")
            for name in expected_asset_names(identity):
                print(f"- {name}")
            print("Dry-run completed; no files were created")
            return 0
        identity, assets, build_number = validate_release(
            tag=args.tag,
            paths=[pathlib.Path(item) for item in args.asset],
            pubspec_path=pathlib.Path(args.pubspec),
            package_name=args.package_name,
            apk_version_name=args.apk_version_name,
            apk_version_code=args.apk_version_code,
        )
        write_metadata(
            identity=identity,
            assets=assets,
            build_number=build_number,
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
