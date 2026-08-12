#!/usr/bin/env python3
"""Generate deterministic CycloneDX SBOMs for AquaCYD release artifacts."""

from __future__ import annotations

import argparse
import configparser
import hashlib
import json
import pathlib
import re
import tempfile
import urllib.parse
import uuid
from collections.abc import Iterable


SEMVER_PATTERN = re.compile(
    r"^(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)$"
)
SUPPORTED_KINDS = {"mobile", "home", "firmware", "web"}


class SbomError(RuntimeError):
    """The dependency or artifact metadata cannot form a valid SBOM."""


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(128 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def component(
    *,
    name: str,
    version: str,
    ecosystem: str,
    scope: str = "required",
) -> dict[str, object]:
    safe_name = urllib.parse.quote(name, safe="/")
    safe_version = urllib.parse.quote(version, safe=".-_~")
    purl_type = {
        "npm": "npm",
        "pub": "pub",
        "platformio": "generic",
    }[ecosystem]
    purl = f"pkg:{purl_type}/{safe_name}@{safe_version}"
    return {
        "type": "library",
        "bom-ref": purl,
        "name": name,
        "version": version,
        "scope": scope,
        "purl": purl,
        "properties": [
            {"name": "aquacyd:dependency-ecosystem", "value": ecosystem}
        ],
    }


def npm_components(lock_path: pathlib.Path) -> list[dict[str, object]]:
    try:
        payload = json.loads(lock_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SbomError(f"cannot parse npm lockfile: {error}") from error
    packages = payload.get("packages")
    if not isinstance(packages, dict):
        raise SbomError("npm lockfile does not contain a packages map")
    result: list[dict[str, object]] = []
    for package_path, metadata in packages.items():
        if package_path == "" or not isinstance(metadata, dict):
            continue
        version = metadata.get("version")
        if not isinstance(version, str) or not version:
            continue
        marker = "node_modules/"
        index = package_path.rfind(marker)
        name = package_path[index + len(marker):] if index >= 0 else package_path
        if not name:
            continue
        result.append(
            component(
                name=name,
                version=version,
                ecosystem="npm",
                scope="optional" if metadata.get("optional") is True else "required",
            )
        )
    return result


def pub_components(lock_path: pathlib.Path) -> list[dict[str, object]]:
    try:
        lines = lock_path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise SbomError(f"cannot read pub lockfile: {error}") from error
    result: list[dict[str, object]] = []
    current_name = ""
    current_version = ""
    current_source = ""

    def flush() -> None:
        nonlocal current_name, current_version, current_source
        if current_name and current_version and current_source != "sdk":
            result.append(
                component(
                    name=current_name,
                    version=current_version,
                    ecosystem="pub",
                )
            )
        current_name = ""
        current_version = ""
        current_source = ""

    for line in lines:
        package_match = re.fullmatch(r"  ([A-Za-z0-9_.+-]+):", line)
        if package_match:
            flush()
            current_name = package_match.group(1)
            continue
        if not current_name:
            continue
        version_match = re.fullmatch(r'    version: "?([^"]+)"?', line)
        source_match = re.fullmatch(r"    source: ([A-Za-z0-9_+-]+)", line)
        if version_match:
            current_version = version_match.group(1)
        elif source_match:
            current_source = source_match.group(1)
    flush()
    if not result:
        raise SbomError("pub lockfile did not yield any third-party packages")
    return result


def parse_platformio_reference(value: str) -> tuple[str, str]:
    normalized = value.strip()
    if not normalized:
        raise SbomError("empty PlatformIO dependency reference")
    if "@" in normalized:
        name, version = normalized.rsplit("@", 1)
    else:
        name, version = normalized, "unversioned"
    if not name or not version:
        raise SbomError(f"invalid PlatformIO dependency reference: {value}")
    return name, version


def platformio_components(config_path: pathlib.Path) -> list[dict[str, object]]:
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    try:
        with config_path.open("r", encoding="utf-8") as stream:
            parser.read_file(stream)
    except (OSError, configparser.Error) as error:
        raise SbomError(f"cannot parse PlatformIO configuration: {error}") from error
    if "cyd_firmware" not in parser:
        raise SbomError("PlatformIO configuration has no cyd_firmware section")
    section = parser["cyd_firmware"]
    references: list[str] = []
    platform = section.get("platform", "").strip()
    if platform:
        references.append(platform)
    for key in ("lib_deps", "platform_packages"):
        raw = section.get(key, "")
        references.extend(
            line.strip()
            for line in raw.splitlines()
            if line.strip() and not line.lstrip().startswith(";")
        )
    result: list[dict[str, object]] = []
    for reference in references:
        name, version = parse_platformio_reference(reference)
        result.append(
            component(
                name=name,
                version=version,
                ecosystem="platformio",
            )
        )
    if not result:
        raise SbomError("PlatformIO configuration has no pinned dependencies")
    return result


def deduplicate_components(
    components: Iterable[dict[str, object]],
) -> list[dict[str, object]]:
    by_reference: dict[str, dict[str, object]] = {}
    for item in components:
        reference = str(item["bom-ref"])
        by_reference[reference] = item
    return [by_reference[key] for key in sorted(by_reference)]


def build_sbom(
    *,
    kind: str,
    version: str,
    artifacts: list[pathlib.Path],
    project_root: pathlib.Path,
) -> dict[str, object]:
    if kind not in SUPPORTED_KINDS:
        raise SbomError(f"unsupported SBOM kind: {kind}")
    if not SEMVER_PATTERN.fullmatch(version):
        raise SbomError("SBOM version must use canonical X.Y.Z format")
    if not artifacts:
        raise SbomError("at least one release artifact is required")
    artifact_entries: list[tuple[pathlib.Path, str]] = []
    for artifact in artifacts:
        if not artifact.is_file() or artifact.stat().st_size <= 0:
            raise SbomError(f"release artifact is absent or empty: {artifact}")
        artifact_entries.append((artifact, sha256_file(artifact)))

    if kind == "web":
        dependencies = npm_components(project_root / "package-lock.json")
        application_name = "AquaCYD Web"
    elif kind == "mobile":
        dependencies = pub_components(
            project_root / "apps" / "aquacyd_service" / "pubspec.lock"
        )
        application_name = "AquaCYD Control"
    elif kind == "home":
        dependencies = pub_components(
            project_root / "apps" / "home_control" / "pubspec.lock"
        )
        application_name = "AquaCYD Home"
    else:
        dependencies = platformio_components(
            project_root / "firmware" / "cyd_controller" / "platformio.ini"
        )
        application_name = "AquaCYD Firmware"

    digest_seed = "|".join(
        f"{path.name}:{digest}" for path, digest in sorted(
            artifact_entries, key=lambda entry: entry[0].name
        )
    )
    serial = uuid.uuid5(
        uuid.NAMESPACE_URL,
        f"https://github.com/Baartek57548/AkwariumCYD/{kind}/{version}/{digest_seed}",
    )
    application_ref = f"pkg:generic/aquacyd-{kind}@{version}"
    properties = [
        {
            "name": f"aquacyd:artifact:{path.name}:sha256",
            "value": digest,
        }
        for path, digest in sorted(artifact_entries, key=lambda entry: entry[0].name)
    ]
    properties.extend(
        {
            "name": f"aquacyd:artifact:{path.name}:bytes",
            "value": str(path.stat().st_size),
        }
        for path, _ in sorted(artifact_entries, key=lambda entry: entry[0].name)
    )
    return {
        "bomFormat": "CycloneDX",
        "specVersion": "1.6",
        "serialNumber": f"urn:uuid:{serial}",
        "version": 1,
        "metadata": {
            "component": {
                "type": "application",
                "bom-ref": application_ref,
                "name": application_name,
                "version": version,
                "properties": properties,
            }
        },
        "components": deduplicate_components(dependencies),
        "dependencies": [
            {
                "ref": application_ref,
                "dependsOn": sorted(
                    str(item["bom-ref"])
                    for item in deduplicate_components(dependencies)
                ),
            }
        ],
    }


def write_sbom(payload: dict[str, object], output: pathlib.Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def run_self_test() -> int:
    project_root = pathlib.Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="aquacyd-sbom-") as temp:
        artifact = pathlib.Path(temp) / "artifact.bin"
        artifact.write_bytes(b"aquacyd-sbom-self-test")
        serials: set[str] = set()
        for kind in sorted(SUPPORTED_KINDS):
            payload = build_sbom(
                kind=kind,
                version="5.1.0",
                artifacts=[artifact],
                project_root=project_root,
            )
            if (
                payload.get("bomFormat") != "CycloneDX"
                or payload.get("specVersion") != "1.6"
                or not payload.get("components")
            ):
                raise SbomError(f"{kind} SBOM self-test failed")
            serial = str(payload["serialNumber"])
            if serial in serials:
                raise SbomError("SBOM serial numbers must be kind-specific")
            serials.add(serial)
    print("SBOM generator self-test passed")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a CycloneDX SBOM for AquaCYD artifacts."
    )
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--kind", choices=sorted(SUPPORTED_KINDS))
    parser.add_argument("--version", default="")
    parser.add_argument("--artifact", action="append", default=[])
    parser.add_argument("--output", default="")
    parser.add_argument(
        "--project-root",
        default=str(pathlib.Path(__file__).resolve().parent.parent),
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        return run_self_test()
    if not args.kind or not args.output:
        raise SbomError("--kind and --output are required")
    payload = build_sbom(
        kind=args.kind,
        version=args.version,
        artifacts=[pathlib.Path(path) for path in args.artifact],
        project_root=pathlib.Path(args.project_root),
    )
    output = pathlib.Path(args.output)
    write_sbom(payload, output)
    print(f"SBOM written: {output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SbomError as error:
        raise SystemExit(f"SBOM error: {error}") from error
