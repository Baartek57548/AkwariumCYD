#!/usr/bin/env python3
"""Verify, atomically install, or roll back an AquaCYD SD web bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import sys
import tarfile
import tempfile
import uuid
from collections.abc import Sequence

import web_package


MAX_MEMBER_BYTES = 2 * 1024 * 1024
MAX_EXPANDED_BYTES = 12 * 1024 * 1024


class InstallError(RuntimeError):
    """The requested SD installation cannot be completed safely."""


def path_is_within(candidate: pathlib.Path, parent: pathlib.Path) -> bool:
    try:
        candidate.relative_to(parent)
        return True
    except ValueError:
        return False


def validate_sd_root(sd_root: pathlib.Path) -> pathlib.Path:
    resolved = sd_root.resolve()
    if resolved == pathlib.Path(resolved.anchor):
        raise InstallError("refusing to use a filesystem root as the SD target")
    aq_root = resolved / "aq"
    if not aq_root.is_dir():
        raise InstallError("SD target must already contain an aq directory")
    if not path_is_within(aq_root.resolve(), resolved):
        raise InstallError("resolved aq directory escapes the SD target")
    return resolved


def load_and_verify_manifest(
    archive: pathlib.Path,
    manifest: pathlib.Path,
    signature: pathlib.Path,
    public_key: pathlib.Path,
) -> dict[str, object]:
    if not archive.is_file():
        raise InstallError(f"web archive does not exist: {archive}")
    web_package.verify_manifest_signature(manifest, signature, public_key)
    try:
        payload = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise InstallError(f"cannot parse web manifest: {error}") from error
    if (
        payload.get("schemaVersion") != web_package.PACKAGE_SCHEMA_VERSION
        or payload.get("kind") != "webBundle"
        or payload.get("productId") != web_package.PRODUCT_ID
        or payload.get("installRoot") != "/aq/ota"
        or payload.get("signatureAlgorithm") != web_package.SIGNATURE_ALGORITHM
    ):
        raise InstallError("web manifest identity or schema is invalid")
    version = str(payload.get("version", ""))
    if not web_package.SEMVER_PATTERN.fullmatch(version):
        raise InstallError("web manifest version is invalid")
    commit = str(payload.get("commit", "")).lower()
    if not web_package.COMMIT_PATTERN.fullmatch(commit):
        raise InstallError("web manifest commit is invalid")
    payload["commit"] = commit
    archive_info = payload.get("archive")
    if not isinstance(archive_info, dict):
        raise InstallError("web manifest does not describe its archive")
    if archive.name != archive_info.get("name"):
        raise InstallError("archive filename does not match the signed manifest")
    expected_bytes = archive_info.get("bytes")
    expected_sha256 = archive_info.get("sha256")
    valid_sha256 = (
        isinstance(expected_sha256, str)
        and len(expected_sha256) == 64
        and all(character in "0123456789abcdef" for character in expected_sha256)
    )
    try:
        archive_bytes = archive.stat().st_size
        archive_sha256 = web_package.sha256_file(archive)
    except OSError as error:
        raise InstallError(f"cannot read web archive: {error}") from error
    if (
        not isinstance(expected_bytes, int)
        or expected_bytes <= 0
        or expected_bytes > web_package.MAX_PACKAGE_BYTES
        or not valid_sha256
        or archive_bytes != expected_bytes
        or archive_sha256 != expected_sha256
    ):
        raise InstallError("archive size or SHA-256 does not match the manifest")
    return payload


def validated_inventory(
    archive: pathlib.Path,
    manifest: dict[str, object],
) -> list[tuple[tarfile.TarInfo, dict[str, object]]]:
    file_entries = manifest.get("files")
    if not isinstance(file_entries, list) or not file_entries:
        raise InstallError("signed manifest has no file inventory")
    expected: dict[str, dict[str, object]] = {}
    for entry in file_entries:
        if not isinstance(entry, dict):
            raise InstallError("manifest contains an invalid file entry")
        name = str(entry.get("path", ""))
        pure = pathlib.PurePosixPath(name)
        if (
            not name.startswith(web_package.ARCHIVE_PREFIX)
            or pure.is_absolute()
            or ".." in pure.parts
            or name in expected
        ):
            raise InstallError(f"unsafe or duplicate manifest path: {name!r}")
        size = entry.get("bytes")
        digest = entry.get("sha256")
        if (
            not isinstance(size, int)
            or not 0 <= size <= MAX_MEMBER_BYTES
            or not isinstance(digest, str)
            or len(digest) != 64
            or any(character not in "0123456789abcdef" for character in digest)
        ):
            raise InstallError(f"invalid manifest metadata for {name}")
        expected[name] = entry

    try:
        handle = tarfile.open(archive, mode="r:gz")
    except (OSError, tarfile.TarError) as error:
        raise InstallError(f"cannot open web archive: {error}") from error
    with handle:
        members = handle.getmembers()
        if len(members) != len(expected):
            raise InstallError("archive and signed inventory have different sizes")
        total = 0
        validated: list[tuple[tarfile.TarInfo, dict[str, object]]] = []
        seen: set[str] = set()
        for member in members:
            if (
                not member.isfile()
                or member.issym()
                or member.islnk()
                or member.name in seen
                or member.name not in expected
            ):
                raise InstallError(f"unsafe or unexpected archive member: {member.name}")
            entry = expected[member.name]
            if member.size != entry["bytes"] or member.size > MAX_MEMBER_BYTES:
                raise InstallError(f"size mismatch for {member.name}")
            total += member.size
            if total > MAX_EXPANDED_BYTES:
                raise InstallError("expanded web bundle exceeds the safety limit")
            seen.add(member.name)
            validated.append((member, entry))
        return validated


def extract_verified(
    archive: pathlib.Path,
    manifest: dict[str, object],
    staging: pathlib.Path,
) -> None:
    inventory = validated_inventory(archive, manifest)
    staging.mkdir(parents=True, exist_ok=False)
    with tarfile.open(archive, mode="r:gz") as handle:
        for member, entry in inventory:
            relative = member.name[len(web_package.ARCHIVE_PREFIX):]
            destination = staging / pathlib.PurePosixPath(relative)
            resolved_destination = destination.resolve()
            if not path_is_within(resolved_destination, staging.resolve()):
                raise InstallError(f"archive member escapes staging: {member.name}")
            destination.parent.mkdir(parents=True, exist_ok=True)
            source = handle.extractfile(member)
            if source is None:
                raise InstallError(f"cannot read archive member: {member.name}")
            digest = hashlib.sha256()
            written = 0
            with source, destination.open("wb") as output:
                for block in iter(lambda: source.read(64 * 1024), b""):
                    digest.update(block)
                    written += len(block)
                    output.write(block)
            if written != entry["bytes"] or digest.hexdigest() != entry["sha256"]:
                raise InstallError(f"content verification failed for {member.name}")


def write_receipt(
    aq_root: pathlib.Path,
    manifest: dict[str, object],
    rollback_available: bool,
) -> None:
    deploy_root = aq_root / "deploy"
    deploy_root.mkdir(parents=True, exist_ok=True)
    receipt = deploy_root / "web-install.json"
    temporary = deploy_root / "web-install.json.tmp"
    archive_info = manifest["archive"]
    payload = {
        "schemaVersion": 1,
        "version": manifest["version"],
        "commit": manifest["commit"],
        "archive": archive_info["name"],
        "sha256": archive_info["sha256"],
        "rollbackAvailable": rollback_available,
    }
    temporary.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    os.replace(temporary, receipt)


def remove_tree(path: pathlib.Path, parent: pathlib.Path) -> None:
    resolved = path.resolve()
    if not path_is_within(resolved, parent.resolve()) or resolved == parent.resolve():
        raise InstallError(f"refusing to remove unsafe path: {path}")
    if path.exists():
        if not path.is_dir() or path.is_symlink():
            raise InstallError(f"expected a real directory: {path}")
        shutil.rmtree(path)


def install_bundle(
    *,
    archive: pathlib.Path,
    manifest_path: pathlib.Path,
    signature: pathlib.Path,
    public_key: pathlib.Path,
    sd_root: pathlib.Path,
) -> None:
    root = validate_sd_root(sd_root)
    aq_root = root / "aq"
    target = aq_root / "ota"
    rollback = aq_root / "ota.rollback"
    staging = aq_root / f".ota.stage-{uuid.uuid4().hex}"
    retired_rollback = aq_root / f".ota.rollback-retired-{uuid.uuid4().hex}"
    failed_new = aq_root / f".ota.failed-{uuid.uuid4().hex}"
    manifest = load_and_verify_manifest(
        archive,
        manifest_path,
        signature,
        public_key,
    )
    if target.is_symlink() or (target.exists() and not target.is_dir()):
        raise InstallError("existing /aq/ota is not a real directory")
    if rollback.is_symlink() or (rollback.exists() and not rollback.is_dir()):
        raise InstallError("existing /aq/ota.rollback is not a real directory")
    target_exists = target.is_dir()
    rollback_exists = rollback.is_dir()
    previous_rollback_moved = False
    target_moved = False
    new_installed = False
    try:
        extract_verified(archive, manifest, staging)
        if rollback_exists:
            os.replace(rollback, retired_rollback)
            previous_rollback_moved = True
        if target_exists:
            os.replace(target, rollback)
            target_moved = True
        os.replace(staging, target)
        new_installed = True
        write_receipt(aq_root, manifest, rollback_available=target_moved)
    except (InstallError, OSError, web_package.WebPackageError) as error:
        try:
            if new_installed and target.is_dir() and not target.is_symlink():
                os.replace(target, failed_new)
            if target_moved and rollback.is_dir() and not target.exists():
                os.replace(rollback, target)
            if (
                previous_rollback_moved
                and retired_rollback.is_dir()
                and not rollback.exists()
            ):
                os.replace(retired_rollback, rollback)
            if failed_new.exists():
                remove_tree(failed_new, aq_root)
        except (InstallError, OSError) as restore_error:
            raise InstallError(
                f"web installation failed ({error}); restoring the previous "
                f"layout also failed ({restore_error})"
            ) from restore_error
        raise InstallError(str(error)) from error
    finally:
        if staging.exists():
            remove_tree(staging, aq_root)
    if retired_rollback.exists():
        try:
            remove_tree(retired_rollback, aq_root)
        except (InstallError, OSError) as cleanup_error:
            print(
                "warning: new web bundle is active, but the retired rollback "
                f"directory could not be removed: {cleanup_error}",
                file=sys.stderr,
            )


def rollback_bundle(sd_root: pathlib.Path) -> None:
    root = validate_sd_root(sd_root)
    aq_root = root / "aq"
    target = aq_root / "ota"
    rollback = aq_root / "ota.rollback"
    if not target.is_dir() or target.is_symlink():
        raise InstallError("current /aq/ota directory is missing")
    if not rollback.is_dir() or rollback.is_symlink():
        raise InstallError("no verified rollback directory is available")
    failed = aq_root / f".ota.failed-{uuid.uuid4().hex}"
    try:
        os.replace(target, failed)
        try:
            os.replace(rollback, target)
        except OSError:
            os.replace(failed, target)
            raise
        os.replace(failed, rollback)
    except OSError as error:
        raise InstallError(f"atomic rollback failed: {error}") from error


def run_self_test() -> int:
    project_root = pathlib.Path(__file__).resolve().parent.parent
    contract_path = project_root / "tools" / "web-assets.json"
    files, gzip_extensions = web_package.load_asset_contract(contract_path)
    with tempfile.TemporaryDirectory(prefix="aquacyd-web-install-") as temp:
        root = pathlib.Path(temp)
        source = root / "source"
        output = root / "release"
        sd_root = root / "sd"
        (sd_root / "aq" / "ota").mkdir(parents=True)
        (sd_root / "aq" / "ota" / "old.txt").write_text(
            "old\n", encoding="utf-8"
        )
        source.mkdir()
        for relative in files:
            content = f"installed:{relative}\n".encode("utf-8")
            (source / relative).write_bytes(content)
            if pathlib.Path(relative).suffix.lower() in gzip_extensions:
                import gzip

                with (source / f"{relative}.gz").open("wb") as raw_stream:
                    with gzip.GzipFile(
                        filename="",
                        mode="wb",
                        fileobj=raw_stream,
                        mtime=0,
                    ) as compressed:
                        compressed.write(content)

        private_key = root / "private.pem"
        public_key = root / "public.pem"
        web_package.run_openssl(
            [
                "genpkey",
                "-algorithm",
                "RSA",
                "-pkeyopt",
                "rsa_keygen_bits:3072",
                "-out",
                str(private_key),
            ]
        )
        web_package.run_openssl(
            [
                "pkey",
                "-in",
                str(private_key),
                "-pubout",
                "-out",
                str(public_key),
            ]
        )
        outputs = web_package.build_package(
            source=source,
            output_dir=output,
            version="5.1.0",
            commit="0123456789abcdef",
            contract_path=contract_path,
            private_key=private_key,
            public_key=public_key,
            allow_unsigned=False,
        )
        install_bundle(
            archive=outputs["archive"],
            manifest_path=outputs["manifest"],
            signature=outputs["signature"],
            public_key=public_key,
            sd_root=sd_root,
        )
        if not (sd_root / "aq" / "ota" / "index.html").is_file():
            raise InstallError("self-test did not install the new web bundle")
        if not (sd_root / "aq" / "ota.rollback" / "old.txt").is_file():
            raise InstallError("self-test did not preserve the rollback bundle")
        rollback_bundle(sd_root)
        if not (sd_root / "aq" / "ota" / "old.txt").is_file():
            raise InstallError("self-test did not restore the previous bundle")
        if not (sd_root / "aq" / "ota.rollback" / "index.html").is_file():
            raise InstallError("self-test rollback did not retain the newer bundle")

        original_write_receipt = write_receipt

        def fail_receipt(
            aq_root: pathlib.Path,
            manifest: dict[str, object],
            rollback_available: bool,
        ) -> None:
            del aq_root, manifest, rollback_available
            raise OSError("injected receipt failure")

        globals()["write_receipt"] = fail_receipt
        try:
            try:
                install_bundle(
                    archive=outputs["archive"],
                    manifest_path=outputs["manifest"],
                    signature=outputs["signature"],
                    public_key=public_key,
                    sd_root=sd_root,
                )
            except InstallError:
                pass
            else:
                raise InstallError("injected installation failure was not reported")
        finally:
            globals()["write_receipt"] = original_write_receipt
        if not (sd_root / "aq" / "ota" / "old.txt").is_file():
            raise InstallError("failed install did not restore the current bundle")
        if not (sd_root / "aq" / "ota.rollback" / "index.html").is_file():
            raise InstallError("failed install did not preserve the prior rollback")
    print("Atomic web installer self-test passed")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verify and atomically install an AquaCYD web bundle on SD."
    )
    parser.add_argument("--self-test", action="store_true")
    subparsers = parser.add_subparsers(dest="command")

    install = subparsers.add_parser("install")
    install.add_argument("--archive", required=True)
    install.add_argument("--manifest", required=True)
    install.add_argument("--signature", required=True)
    install.add_argument("--public-key", required=True)
    install.add_argument("--sd-root", required=True)

    rollback = subparsers.add_parser("rollback")
    rollback.add_argument("--sd-root", required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        return run_self_test()
    if args.command == "install":
        install_bundle(
            archive=pathlib.Path(args.archive),
            manifest_path=pathlib.Path(args.manifest),
            signature=pathlib.Path(args.signature),
            public_key=pathlib.Path(args.public_key),
            sd_root=pathlib.Path(args.sd_root),
        )
        print("Web bundle installed atomically; previous bundle kept as rollback.")
        return 0
    if args.command == "rollback":
        rollback_bundle(pathlib.Path(args.sd_root))
        print("Web bundle rollback completed atomically.")
        return 0
    raise InstallError("choose install or rollback")


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (InstallError, web_package.WebPackageError) as error:
        raise SystemExit(f"web install error: {error}") from error
