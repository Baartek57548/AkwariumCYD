#!/usr/bin/env python3
"""Verify that the committed RSA key and C++ trust anchor are identical."""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile


class TrustError(RuntimeError):
    """The committed firmware trust material is inconsistent."""


def find_openssl() -> str:
    candidates = [
        shutil.which("openssl"),
        r"C:\Program Files\Git\usr\bin\openssl.exe",
        r"C:\Program Files\OpenSSL-Win64\bin\openssl.exe",
    ]
    for candidate in candidates:
        if candidate and pathlib.Path(candidate).is_file():
            return str(pathlib.Path(candidate))
    raise TrustError("OpenSSL executable was not found")


def run_openssl(arguments: list[str]) -> bytes:
    result = subprocess.run(
        [find_openssl(), *arguments],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise TrustError(f"OpenSSL rejected the public key: {detail}")
    return result.stdout


def parse_cpp_string(content: str, symbol: str) -> str:
    declaration = re.search(
        rf"constexpr\s+char\s+{re.escape(symbol)}\[\]\s*=\s*"
        r"(?P<value>(?:\"(?:\\.|[^\"\\])*\"\s*)+);",
        content,
        re.DOTALL,
    )
    if not declaration:
        raise TrustError(f"trust anchor does not define {symbol}")
    literals = re.findall(r'"(?:\\.|[^"\\])*"', declaration.group("value"))
    if not literals:
        raise TrustError(f"trust anchor {symbol} is empty")
    try:
        return "".join(ast.literal_eval(item) for item in literals)
    except (SyntaxError, ValueError) as error:
        raise TrustError(f"trust anchor {symbol} is not a valid C string") from error


def public_der(key_path: pathlib.Path) -> bytes:
    return run_openssl(
        [
            "pkey",
            "-pubin",
            "-in",
            str(key_path),
            "-pubout",
            "-outform",
            "DER",
        ]
    )


def verify(public_key: pathlib.Path, trust_anchor: pathlib.Path) -> dict[str, object]:
    if not public_key.is_file():
        raise TrustError(f"public key does not exist: {public_key}")
    if not trust_anchor.is_file():
        raise TrustError(f"trust anchor does not exist: {trust_anchor}")

    key_description = run_openssl(
        ["pkey", "-pubin", "-in", str(public_key), "-text", "-noout"]
    ).decode("utf-8", errors="replace")
    if "Public-Key: (3072 bit)" not in key_description:
        raise TrustError("firmware public key must be RSA-3072")

    content = trust_anchor.read_text(encoding="utf-8")
    embedded_key = parse_cpp_string(content, "RSA3072_PUBLIC_KEY_PEM")
    embedded_digest = parse_cpp_string(content, "PUBLIC_KEY_SHA256")
    embedded_key_id = parse_cpp_string(content, "KEY_ID")

    committed_der = public_der(public_key)
    digest = hashlib.sha256(committed_der).hexdigest()
    key_id = digest[:16]
    if embedded_digest != digest:
        raise TrustError(
            f"embedded public-key digest {embedded_digest} differs from {digest}"
        )
    if embedded_key_id != key_id:
        raise TrustError(f"embedded key id {embedded_key_id} differs from {key_id}")

    with tempfile.TemporaryDirectory(prefix="aquacyd-trust-") as temp:
        temporary_path = pathlib.Path(temp) / "embedded-public.pem"
        temporary_path.write_text(embedded_key, encoding="ascii")
        embedded_der = public_der(temporary_path)
    if embedded_der != committed_der:
        raise TrustError("embedded RSA public key differs from the committed PEM")

    return {
        "algorithm": "RSA-3072",
        "keyId": key_id,
        "publicKeySha256": digest,
        "publicKey": str(public_key),
        "trustAnchor": str(trust_anchor),
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify the public firmware key and embedded C++ trust anchor."
    )
    parser.add_argument(
        "--public-key",
        default="security/firmware-signing-public.pem",
    )
    parser.add_argument(
        "--trust-anchor",
        default="include/firmware_trust_anchor.h",
    )
    args = parser.parse_args()
    try:
        result = verify(
            pathlib.Path(args.public_key).resolve(),
            pathlib.Path(args.trust_anchor).resolve(),
        )
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    except (OSError, TrustError) as error:
        print(f"Firmware trust verification failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
