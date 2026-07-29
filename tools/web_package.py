#!/usr/bin/env python3
"""Create and verify deterministic, signed AquaCYD web bundles."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import pathlib
import re
import shutil
import subprocess
import tarfile
import tempfile
from collections.abc import Sequence


SEMVER_PATTERN = re.compile(
    r"^(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)$"
)
COMMIT_PATTERN = re.compile(r"^[0-9a-f]{7,40}$")
PRODUCT_ID = "aquacyd-cyd"
PACKAGE_SCHEMA_VERSION = 1
SIGNATURE_ALGORITHM = "rsa-pss-sha256"
ARCHIVE_PREFIX = "aq/ota/"
MAX_PACKAGE_BYTES = 16 * 1024 * 1024


class WebPackageError(RuntimeError):
    """A web package violates the release or installation contract."""


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(128 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_asset_contract(path: pathlib.Path) -> tuple[list[str], set[str]]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise WebPackageError(f"cannot read asset contract {path}: {error}") from error
    if payload.get("schemaVersion") != 1:
        raise WebPackageError("unsupported web asset contract schema")
    source_files = payload.get("sourceFiles")
    gzip_extensions = payload.get("gzipExtensions")
    if (
        not isinstance(source_files, list)
        or not source_files
        or not isinstance(gzip_extensions, list)
    ):
        raise WebPackageError("asset contract must define files and gzip extensions")
    normalized: list[str] = []
    for item in source_files:
        candidate = str(item)
        pure = pathlib.PurePosixPath(candidate)
        if (
            pure.is_absolute()
            or len(pure.parts) != 1
            or candidate in {"", ".", ".."}
        ):
            raise WebPackageError(f"unsafe web asset path: {candidate!r}")
        normalized.append(candidate)
    if len(set(normalized)) != len(normalized):
        raise WebPackageError("asset contract contains duplicate paths")
    extensions = {str(item).lower() for item in gzip_extensions}
    if any(not item.startswith(".") for item in extensions):
        raise WebPackageError("gzip extensions must start with a dot")
    return normalized, extensions


def collect_assets(
    source: pathlib.Path,
    contract_path: pathlib.Path,
) -> list[tuple[str, pathlib.Path]]:
    files, gzip_extensions = load_asset_contract(contract_path)
    collected: list[tuple[str, pathlib.Path]] = []
    for relative in files:
        source_path = source / relative
        if not source_path.is_file():
            raise WebPackageError(f"missing generated web asset: {source_path}")
        collected.append((f"{ARCHIVE_PREFIX}{relative}", source_path))
        if source_path.suffix.lower() in gzip_extensions:
            compressed_path = source / f"{relative}.gz"
            if not compressed_path.is_file():
                raise WebPackageError(
                    f"missing generated compressed web asset: {compressed_path}"
                )
            with compressed_path.open("rb") as stream:
                if stream.read(2) != b"\x1f\x8b":
                    raise WebPackageError(f"invalid gzip asset: {compressed_path}")
            collected.append(
                (f"{ARCHIVE_PREFIX}{relative}.gz", compressed_path)
            )
    return sorted(collected, key=lambda entry: entry[0])


def create_deterministic_archive(
    output: pathlib.Path,
    assets: Sequence[tuple[str, pathlib.Path]],
) -> list[dict[str, object]]:
    output.parent.mkdir(parents=True, exist_ok=True)
    entries: list[dict[str, object]] = []
    with output.open("wb") as raw_stream:
        with gzip.GzipFile(
            filename="",
            mode="wb",
            fileobj=raw_stream,
            compresslevel=9,
            mtime=0,
        ) as compressed_stream:
            with tarfile.open(
                mode="w",
                fileobj=compressed_stream,
                format=tarfile.PAX_FORMAT,
            ) as archive:
                for archive_name, source_path in assets:
                    content = source_path.read_bytes()
                    info = tarfile.TarInfo(archive_name)
                    info.size = len(content)
                    info.mode = 0o644
                    info.mtime = 0
                    info.uid = 0
                    info.gid = 0
                    info.uname = ""
                    info.gname = ""
                    archive.addfile(info, io.BytesIO(content))
                    entries.append(
                        {
                            "path": archive_name,
                            "bytes": len(content),
                            "sha256": hashlib.sha256(content).hexdigest(),
                        }
                    )
    if not output.is_file() or output.stat().st_size <= 0:
        raise WebPackageError("web archive was not created")
    if output.stat().st_size > MAX_PACKAGE_BYTES:
        raise WebPackageError(
            f"web archive exceeds {MAX_PACKAGE_BYTES} bytes"
        )
    return entries


def run_openssl(arguments: Sequence[str]) -> None:
    executable = shutil.which("openssl")
    if executable is None:
        for candidate in (
            pathlib.Path(r"C:\Program Files\Git\usr\bin\openssl.exe"),
            pathlib.Path(r"C:\Program Files\Git\mingw64\bin\openssl.exe"),
        ):
            if candidate.is_file():
                executable = str(candidate)
                break
    if executable is None:
        raise WebPackageError("openssl is required for package signing")
    try:
        subprocess.run(
            [executable, *arguments],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except FileNotFoundError as error:
        raise WebPackageError("openssl executable could not be started") from error
    except subprocess.CalledProcessError as error:
        detail = error.stderr.strip() or error.stdout.strip() or str(error)
        raise WebPackageError(f"openssl failed: {detail}") from error


def sign_manifest(
    manifest: pathlib.Path,
    signature: pathlib.Path,
    private_key: pathlib.Path,
    public_key: pathlib.Path,
) -> None:
    for key_path in (private_key, public_key):
        if not key_path.is_file() or key_path.stat().st_size <= 0:
            raise WebPackageError(f"signing key does not exist: {key_path}")
    run_openssl(
        [
            "dgst",
            "-sha256",
            "-sign",
            str(private_key),
            "-sigopt",
            "rsa_padding_mode:pss",
            "-sigopt",
            "rsa_pss_saltlen:32",
            "-out",
            str(signature),
            str(manifest),
        ]
    )
    verify_manifest_signature(manifest, signature, public_key)


def verify_manifest_signature(
    manifest: pathlib.Path,
    signature: pathlib.Path,
    public_key: pathlib.Path,
) -> None:
    for path in (manifest, signature, public_key):
        if not path.is_file() or path.stat().st_size <= 0:
            raise WebPackageError(f"verification input does not exist: {path}")
    run_openssl(
        [
            "dgst",
            "-sha256",
            "-verify",
            str(public_key),
            "-signature",
            str(signature),
            "-sigopt",
            "rsa_padding_mode:pss",
            "-sigopt",
            "rsa_pss_saltlen:32",
            str(manifest),
        ]
    )


def build_package(
    *,
    source: pathlib.Path,
    output_dir: pathlib.Path,
    version: str,
    commit: str,
    contract_path: pathlib.Path,
    private_key: pathlib.Path | None,
    public_key: pathlib.Path | None,
    allow_unsigned: bool,
) -> dict[str, pathlib.Path]:
    if not SEMVER_PATTERN.fullmatch(version):
        raise WebPackageError("version must use canonical X.Y.Z format")
    normalized_commit = commit.strip().lower()
    if not COMMIT_PATTERN.fullmatch(normalized_commit):
        raise WebPackageError("commit must contain 7-40 lowercase hex characters")
    if not source.is_dir():
        raise WebPackageError(f"web source directory does not exist: {source}")
    if not allow_unsigned and (private_key is None or public_key is None):
        raise WebPackageError("production package requires private and public keys")
    if (private_key is None) != (public_key is None):
        raise WebPackageError("private and public keys must be supplied together")

    base_name = f"AquaCYD-Web-{version}"
    archive = output_dir / f"{base_name}.tar.gz"
    manifest = output_dir / f"{base_name}.manifest.json"
    signature = output_dir / f"{base_name}.manifest.json.sig"
    checksum = output_dir / f"{base_name}.tar.gz.sha256"

    assets = collect_assets(source, contract_path)
    file_entries = create_deterministic_archive(archive, assets)
    archive_digest = sha256_file(archive)
    manifest_payload = {
        "schemaVersion": PACKAGE_SCHEMA_VERSION,
        "kind": "webBundle",
        "productId": PRODUCT_ID,
        "version": version,
        "commit": normalized_commit,
        "installRoot": "/aq/ota",
        "offlinePolicy": "read-only-no-command-queue",
        "signatureAlgorithm": SIGNATURE_ALGORITHM,
        "archive": {
            "name": archive.name,
            "bytes": archive.stat().st_size,
            "sha256": archive_digest,
        },
        "files": file_entries,
    }
    manifest.write_text(
        json.dumps(manifest_payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    checksum.write_text(
        f"{archive_digest}  {archive.name}\n",
        encoding="ascii",
        newline="\n",
    )

    outputs = {
        "archive": archive,
        "manifest": manifest,
        "checksum": checksum,
    }
    if private_key is not None and public_key is not None:
        sign_manifest(manifest, signature, private_key, public_key)
        outputs["signature"] = signature
    return outputs


def run_self_test() -> int:
    project_root = pathlib.Path(__file__).resolve().parent.parent
    contract_path = project_root / "tools" / "web-assets.json"
    files, gzip_extensions = load_asset_contract(contract_path)
    with tempfile.TemporaryDirectory(prefix="aquacyd-web-package-") as temp:
        root = pathlib.Path(temp)
        source = root / "source"
        output_one = root / "output-one"
        output_two = root / "output-two"
        source.mkdir()
        for index, relative in enumerate(files):
            content = f"self-test:{index}:{relative}\n".encode("utf-8")
            (source / relative).write_bytes(content)
            if pathlib.Path(relative).suffix.lower() in gzip_extensions:
                with (source / f"{relative}.gz").open("wb") as raw_stream:
                    with gzip.GzipFile(
                        filename="",
                        mode="wb",
                        fileobj=raw_stream,
                        compresslevel=9,
                        mtime=0,
                    ) as compressed:
                        compressed.write(content)

        private_key = root / "private.pem"
        public_key = root / "public.pem"
        run_openssl(
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
        run_openssl(
            [
                "pkey",
                "-in",
                str(private_key),
                "-pubout",
                "-out",
                str(public_key),
            ]
        )

        first = build_package(
            source=source,
            output_dir=output_one,
            version="5.1.0",
            commit="0123456789abcdef",
            contract_path=contract_path,
            private_key=private_key,
            public_key=public_key,
            allow_unsigned=False,
        )
        second = build_package(
            source=source,
            output_dir=output_two,
            version="5.1.0",
            commit="0123456789abcdef",
            contract_path=contract_path,
            private_key=private_key,
            public_key=public_key,
            allow_unsigned=False,
        )
        if first["archive"].read_bytes() != second["archive"].read_bytes():
            raise WebPackageError("deterministic archive self-test failed")
        if first["manifest"].read_bytes() != second["manifest"].read_bytes():
            raise WebPackageError("deterministic manifest self-test failed")
        verify_manifest_signature(
            first["manifest"],
            first["signature"],
            public_key,
        )
        with tarfile.open(first["archive"], mode="r:gz") as archive_handle:
            names = sorted(archive_handle.getnames())
        expected_names = sorted(entry["path"] for entry in json.loads(
            first["manifest"].read_text(encoding="utf-8")
        )["files"])
        if names != expected_names:
            raise WebPackageError("archive inventory self-test failed")
    print("Web package self-test passed")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create a deterministic, signed AquaCYD web bundle."
    )
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--source", default="sdcard/aq/ota")
    parser.add_argument("--output-dir", default="artifacts/web")
    parser.add_argument("--version", default="")
    parser.add_argument("--commit", default="")
    parser.add_argument("--contract", default="tools/web-assets.json")
    parser.add_argument("--private-key")
    parser.add_argument("--public-key")
    parser.add_argument(
        "--unsigned-for-testing",
        action="store_true",
        help="permit an unsigned diagnostic package; never use for releases",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        return run_self_test()
    outputs = build_package(
        source=pathlib.Path(args.source),
        output_dir=pathlib.Path(args.output_dir),
        version=args.version,
        commit=args.commit,
        contract_path=pathlib.Path(args.contract),
        private_key=pathlib.Path(args.private_key) if args.private_key else None,
        public_key=pathlib.Path(args.public_key) if args.public_key else None,
        allow_unsigned=args.unsigned_for_testing,
    )
    print(
        json.dumps(
            {name: str(path) for name, path in outputs.items()},
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except WebPackageError as error:
        raise SystemExit(f"web package error: {error}") from error
