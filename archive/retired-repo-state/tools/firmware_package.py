#!/usr/bin/env python3
"""Create and verify immutable AquaCYD signed OTA packages."""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from typing import Final


MAGIC: Final = b"AQCYDOTA"
FORMAT_VERSION: Final = 1
HEADER_BYTES: Final = 512
SIGNED_METADATA_BYTES: Final = 128
SIGNATURE_BYTES: Final = 384
ALGORITHM_RSA3072_PSS_SHA256: Final = 1
PRODUCT_ID: Final = "aquacyd-cyd"
TARGETS: Final = {"ili9341": 1, "st7789": 2}
VERSION_PATTERN: Final = re.compile(
    r"(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)"
)
COMMIT_PATTERN: Final = re.compile(r"[0-9a-f]{7,19}")
KEY_ID_PATTERN: Final = re.compile(r"[0-9a-f]{16}")


class PackageError(RuntimeError):
    """The OTA package violates the signed release contract."""


@dataclasses.dataclass(frozen=True)
class PackageMetadata:
    target: str
    image_bytes: int
    security_version: int
    image_sha256: str
    firmware_version: str
    product_id: str
    key_id: str
    commit: str
    minimum_bootloader_version: int
    signature_sha256: str


def find_openssl() -> str:
    explicit = os.getenv("OPENSSL")
    candidates = [
        explicit,
        shutil.which("openssl"),
        r"C:\Program Files\Git\usr\bin\openssl.exe",
        r"C:\Program Files\OpenSSL-Win64\bin\openssl.exe",
    ]
    for candidate in candidates:
        if candidate and pathlib.Path(candidate).is_file():
            return str(pathlib.Path(candidate))
    raise PackageError("OpenSSL executable was not found")


def run_checked(arguments: list[str], *, capture: bool = False) -> bytes:
    result = subprocess.run(
        arguments,
        check=False,
        stdout=subprocess.PIPE if capture else subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise PackageError(
            f"command failed ({result.returncode}): {arguments[0]}: {detail}"
        )
    return result.stdout if capture else b""


def encode_fixed(value: str, size: int, label: str, pattern: re.Pattern[str]) -> bytes:
    if not pattern.fullmatch(value):
        raise PackageError(f"invalid {label}: {value!r}")
    encoded = value.encode("ascii")
    if len(encoded) >= size:
        raise PackageError(f"{label} must use fewer than {size} ASCII bytes")
    return encoded + bytes(size - len(encoded))


def public_key_der(openssl: str, key_path: pathlib.Path, *, private: bool) -> bytes:
    arguments = [openssl, "pkey"]
    if not private:
        arguments.append("-pubin")
    arguments.extend(["-in", str(key_path), "-pubout", "-outform", "DER"])
    return run_checked(arguments, capture=True)


def key_id_from_der(der: bytes) -> tuple[str, str]:
    digest = hashlib.sha256(der).hexdigest()
    return digest[:16], digest


def build_metadata(
    *,
    image: bytes,
    target: str,
    version: str,
    security_version: int,
    commit: str,
    minimum_bootloader_version: int,
    key_id: str,
) -> tuple[bytearray, str]:
    if target not in TARGETS:
        raise PackageError(f"unsupported target: {target}")
    if not VERSION_PATTERN.fullmatch(version):
        raise PackageError(f"invalid semantic version: {version}")
    if not COMMIT_PATTERN.fullmatch(commit):
        raise PackageError("commit must contain 7-19 lowercase hexadecimal characters")
    if not KEY_ID_PATTERN.fullmatch(key_id):
        raise PackageError("key id must contain exactly 16 lowercase hex characters")
    if not 1 <= security_version <= 0xFFFFFFFF:
        raise PackageError("security version must be in range 1..4294967295")
    if not 1 <= minimum_bootloader_version <= 0xFFFF:
        raise PackageError("minimum bootloader version must be in range 1..65535")
    if not image:
        raise PackageError("firmware image is empty")
    if len(image) > 0xFFFFFFFF:
        raise PackageError("firmware image is too large")

    digest = hashlib.sha256(image).digest()
    metadata = bytearray(SIGNED_METADATA_BYTES)
    metadata[0:8] = MAGIC
    struct.pack_into("<H", metadata, 8, FORMAT_VERSION)
    struct.pack_into("<H", metadata, 10, HEADER_BYTES)
    metadata[12] = ALGORITHM_RSA3072_PSS_SHA256
    metadata[13] = TARGETS[target]
    struct.pack_into("<H", metadata, 14, 0)
    struct.pack_into("<I", metadata, 16, len(image))
    struct.pack_into("<I", metadata, 20, security_version)
    metadata[24:56] = digest
    metadata[56:72] = encode_fixed(version, 16, "version", VERSION_PATTERN)
    metadata[72:88] = encode_fixed(
        PRODUCT_ID,
        16,
        "product id",
        re.compile(r"[a-z0-9-]+"),
    )
    metadata[88:104] = key_id.encode("ascii")
    metadata[104:124] = encode_fixed(
        commit,
        20,
        "commit",
        COMMIT_PATTERN,
    )
    struct.pack_into("<H", metadata, 124, minimum_bootloader_version)
    struct.pack_into("<H", metadata, 126, 0)
    return metadata, digest.hex()


def sign_metadata(
    metadata: bytes,
    private_key: pathlib.Path,
    openssl: str,
) -> bytes:
    with tempfile.TemporaryDirectory(prefix="aquacyd-sign-") as temp:
        root = pathlib.Path(temp)
        metadata_path = root / "metadata.bin"
        signature_path = root / "metadata.sig"
        metadata_path.write_bytes(metadata)
        run_checked(
            [
                openssl,
                "dgst",
                "-sha256",
                "-sign",
                str(private_key),
                "-sigopt",
                "rsa_padding_mode:pss",
                "-sigopt",
                "rsa_pss_saltlen:32",
                "-out",
                str(signature_path),
                str(metadata_path),
            ]
        )
        signature = signature_path.read_bytes()
    if len(signature) != SIGNATURE_BYTES:
        raise PackageError(
            f"RSA-3072 signature must contain {SIGNATURE_BYTES} bytes; "
            f"got {len(signature)}"
        )
    return signature


def verify_signature(
    metadata: bytes,
    signature: bytes,
    public_key: pathlib.Path,
    openssl: str,
) -> None:
    with tempfile.TemporaryDirectory(prefix="aquacyd-verify-") as temp:
        root = pathlib.Path(temp)
        metadata_path = root / "metadata.bin"
        signature_path = root / "metadata.sig"
        metadata_path.write_bytes(metadata)
        signature_path.write_bytes(signature)
        run_checked(
            [
                openssl,
                "dgst",
                "-sha256",
                "-verify",
                str(public_key),
                "-signature",
                str(signature_path),
                "-sigopt",
                "rsa_padding_mode:pss",
                "-sigopt",
                "rsa_pss_saltlen:32",
                str(metadata_path),
            ]
        )


def parse_fixed(raw: bytes, label: str, pattern: re.Pattern[str]) -> str:
    terminator = raw.find(b"\x00")
    if terminator <= 0:
        raise PackageError(f"{label} is missing a canonical NUL terminator")
    if any(raw[terminator:]):
        raise PackageError(f"{label} contains non-zero padding")
    try:
        value = raw[:terminator].decode("ascii")
    except UnicodeDecodeError as error:
        raise PackageError(f"{label} is not ASCII") from error
    if not pattern.fullmatch(value):
        raise PackageError(f"invalid {label}: {value!r}")
    return value


def inspect_package(
    package: pathlib.Path,
    *,
    public_key: pathlib.Path | None = None,
    expected_target: str | None = None,
    expected_version: str | None = None,
    expected_security_version: int | None = None,
) -> PackageMetadata:
    size = package.stat().st_size
    if size < HEADER_BYTES:
        raise PackageError("package is shorter than its fixed header")
    with package.open("rb") as stream:
        header = stream.read(HEADER_BYTES)
        image = stream.read()
    metadata = header[:SIGNED_METADATA_BYTES]
    signature = header[SIGNED_METADATA_BYTES:HEADER_BYTES]

    if metadata[:8] != MAGIC:
        raise PackageError("invalid package magic")
    format_version, header_bytes = struct.unpack_from("<HH", metadata, 8)
    if format_version != FORMAT_VERSION or header_bytes != HEADER_BYTES:
        raise PackageError("unsupported package format")
    if metadata[12] != ALGORITHM_RSA3072_PSS_SHA256:
        raise PackageError("unsupported signature algorithm")
    target_id = metadata[13]
    target = next((name for name, value in TARGETS.items() if value == target_id), "")
    if not target:
        raise PackageError("unknown hardware target")
    flags = struct.unpack_from("<H", metadata, 14)[0]
    image_bytes = struct.unpack_from("<I", metadata, 16)[0]
    security_version = struct.unpack_from("<I", metadata, 20)[0]
    declared_digest = metadata[24:56].hex()
    version = parse_fixed(metadata[56:72], "version", VERSION_PATTERN)
    product_id = parse_fixed(
        metadata[72:88],
        "product id",
        re.compile(r"[a-z0-9-]+"),
    )
    try:
        key_id = metadata[88:104].decode("ascii")
    except UnicodeDecodeError as error:
        raise PackageError("key id is not ASCII") from error
    commit = parse_fixed(metadata[104:124], "commit", COMMIT_PATTERN)
    minimum_bootloader_version, reserved = struct.unpack_from("<HH", metadata, 124)

    if flags != 0 or reserved != 0:
        raise PackageError("reserved package flags must be zero")
    if product_id != PRODUCT_ID:
        raise PackageError(f"unexpected product id: {product_id}")
    if not KEY_ID_PATTERN.fullmatch(key_id):
        raise PackageError("invalid key id")
    if image_bytes != len(image):
        raise PackageError(
            f"declared image size {image_bytes} differs from payload {len(image)}"
        )
    actual_digest = hashlib.sha256(image).hexdigest()
    if actual_digest != declared_digest:
        raise PackageError("firmware SHA-256 does not match signed metadata")
    if len(signature) != SIGNATURE_BYTES or not any(signature):
        raise PackageError("signature is missing or malformed")

    if public_key is not None:
        openssl = find_openssl()
        public_der = public_key_der(openssl, public_key, private=False)
        expected_key_id, _ = key_id_from_der(public_der)
        if key_id != expected_key_id:
            raise PackageError(
                f"package key id {key_id} does not match trusted key {expected_key_id}"
            )
        verify_signature(metadata, signature, public_key, openssl)

    if expected_target is not None and target != expected_target:
        raise PackageError(
            f"package target {target} does not match expected {expected_target}"
        )
    if expected_version is not None and version != expected_version:
        raise PackageError(
            f"package version {version} does not match expected {expected_version}"
        )
    if (
        expected_security_version is not None
        and security_version != expected_security_version
    ):
        raise PackageError(
            "package security version "
            f"{security_version} does not match expected {expected_security_version}"
        )

    return PackageMetadata(
        target=target,
        image_bytes=image_bytes,
        security_version=security_version,
        image_sha256=actual_digest,
        firmware_version=version,
        product_id=product_id,
        key_id=key_id,
        commit=commit,
        minimum_bootloader_version=minimum_bootloader_version,
        signature_sha256=hashlib.sha256(signature).hexdigest(),
    )


def create_package(args: argparse.Namespace) -> PackageMetadata:
    openssl = find_openssl()
    image_path = pathlib.Path(args.input).resolve()
    private_key = pathlib.Path(args.private_key).resolve()
    trusted_public_key = pathlib.Path(args.public_key).resolve()
    output_path = pathlib.Path(args.output).resolve()
    if not image_path.is_file():
        raise PackageError(f"firmware image does not exist: {image_path}")
    if not private_key.is_file():
        raise PackageError(f"private signing key does not exist: {private_key}")
    if not trusted_public_key.is_file():
        raise PackageError(f"trusted public key does not exist: {trusted_public_key}")
    if output_path.exists() and not args.force:
        raise PackageError(f"output already exists: {output_path}")

    private_public_der = public_key_der(openssl, private_key, private=True)
    trusted_public_der = public_key_der(
        openssl,
        trusted_public_key,
        private=False,
    )
    if private_public_der != trusted_public_der:
        raise PackageError("private key does not match the committed trust anchor")
    key_id, public_key_sha256 = key_id_from_der(trusted_public_der)
    image = image_path.read_bytes()
    metadata, image_sha256 = build_metadata(
        image=image,
        target=args.target,
        version=args.version,
        security_version=args.security_version,
        commit=args.commit.lower()[:19],
        minimum_bootloader_version=args.minimum_bootloader_version,
        key_id=key_id,
    )
    signature = sign_metadata(metadata, private_key, openssl)
    verify_signature(metadata, signature, trusted_public_key, openssl)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary_output = output_path.with_suffix(output_path.suffix + ".tmp")
    if temporary_output.exists():
        temporary_output.unlink()
    try:
        with temporary_output.open("xb") as stream:
            stream.write(metadata)
            stream.write(signature)
            stream.write(image)
            stream.flush()
            os.fsync(stream.fileno())
        temporary_output.replace(output_path)
    finally:
        if temporary_output.exists():
            temporary_output.unlink()

    result = inspect_package(
        output_path,
        public_key=trusted_public_key,
        expected_target=args.target,
        expected_version=args.version,
        expected_security_version=args.security_version,
    )
    if result.image_sha256 != image_sha256:
        raise PackageError("post-write firmware digest changed")
    print(
        json.dumps(
            {
                **dataclasses.asdict(result),
                "package": str(output_path),
                "packageBytes": output_path.stat().st_size,
                "packageSha256": hashlib.sha256(output_path.read_bytes()).hexdigest(),
                "publicKeySha256": public_key_sha256,
            },
            indent=2,
            sort_keys=True,
        )
    )
    return result


def inspect_command(args: argparse.Namespace) -> PackageMetadata:
    package = pathlib.Path(args.package).resolve()
    if not package.is_file():
        raise PackageError(f"package does not exist: {package}")
    public_key = (
        pathlib.Path(args.public_key).resolve() if args.public_key else None
    )
    metadata = inspect_package(
        package,
        public_key=public_key,
        expected_target=args.target,
        expected_version=args.version,
        expected_security_version=args.security_version,
    )
    print(
        json.dumps(
            {
                **dataclasses.asdict(metadata),
                "package": str(package),
                "packageBytes": package.stat().st_size,
                "packageSha256": hashlib.sha256(package.read_bytes()).hexdigest(),
            },
            indent=2,
            sort_keys=True,
        )
    )
    return metadata


def self_test() -> None:
    openssl = find_openssl()
    with tempfile.TemporaryDirectory(prefix="aquacyd-package-test-") as temp:
        root = pathlib.Path(temp)
        private_key = root / "test-private.pem"
        public_key = root / "test-public.pem"
        image = root / "firmware.bin"
        package = root / "firmware.aqfw"
        run_checked(
            [
                openssl,
                "genpkey",
                "-algorithm",
                "RSA",
                "-pkeyopt",
                "rsa_keygen_bits:3072",
                "-out",
                str(private_key),
            ]
        )
        run_checked(
            [
                openssl,
                "pkey",
                "-in",
                str(private_key),
                "-pubout",
                "-out",
                str(public_key),
            ]
        )
        image.write_bytes(bytes((index * 17 + 3) & 0xFF for index in range(65536)))
        arguments = argparse.Namespace(
            input=str(image),
            output=str(package),
            private_key=str(private_key),
            public_key=str(public_key),
            target="ili9341",
            version="5.1.0",
            security_version=1,
            commit="0123456789abcdef012",
            minimum_bootloader_version=1,
            force=False,
        )
        create_package(arguments)
        verified = inspect_package(
            package,
            public_key=public_key,
            expected_target="ili9341",
            expected_version="5.1.0",
            expected_security_version=1,
        )
        if verified.image_bytes != 65536:
            raise PackageError("self-test image length mismatch")

        tampered = bytearray(package.read_bytes())
        tampered[-1] ^= 0x01
        package.write_bytes(tampered)
        try:
            inspect_package(package, public_key=public_key)
        except PackageError:
            pass
        else:
            raise PackageError("self-test accepted a modified firmware payload")
    print("Firmware package self-test passed")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Create or verify AquaCYD RSA-PSS signed OTA packages."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    create = subparsers.add_parser("create", help="create a signed .aqfw package")
    create.add_argument("--input", required=True)
    create.add_argument("--output", required=True)
    create.add_argument("--private-key", required=True)
    create.add_argument(
        "--public-key",
        default="security/firmware-signing-public.pem",
    )
    create.add_argument("--target", choices=sorted(TARGETS), required=True)
    create.add_argument("--version", required=True)
    create.add_argument("--security-version", type=int, required=True)
    create.add_argument("--commit", required=True)
    create.add_argument("--minimum-bootloader-version", type=int, default=1)
    create.add_argument("--force", action="store_true")

    inspect = subparsers.add_parser("inspect", help="verify a .aqfw package")
    inspect.add_argument("--package", required=True)
    inspect.add_argument("--public-key")
    inspect.add_argument("--target", choices=sorted(TARGETS))
    inspect.add_argument("--version")
    inspect.add_argument("--security-version", type=int)

    subparsers.add_parser("self-test", help="run deterministic contract tests")
    return parser


def main() -> int:
    try:
        args = build_parser().parse_args()
        if args.command == "create":
            create_package(args)
        elif args.command == "inspect":
            inspect_command(args)
        else:
            self_test()
        return 0
    except (OSError, PackageError) as error:
        print(f"Firmware package error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
