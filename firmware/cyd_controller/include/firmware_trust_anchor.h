#ifndef AQUARIUM_FIRMWARE_TRUST_ANCHOR_H
#define AQUARIUM_FIRMWARE_TRUST_ANCHOR_H

#include <stddef.h>

namespace FirmwareTrust {

constexpr char KEY_ID[] = "9470c281de5f898f";
constexpr char PUBLIC_KEY_SHA256[] =
    "9470c281de5f898fd387acd8b89c3f21fb435ed71bf7d638159b4b9339fc2bc5";

// This public key is intentionally part of the firmware. The corresponding
// private RSA-3072 key is kept only in the protected GitHub environment and a
// local DPAPI-encrypted recovery copy.
constexpr char RSA3072_PUBLIC_KEY_PEM[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIIBojANBgkqhkiG9w0BAQEFAAOCAY8AMIIBigKCAYEAomJ5HVOx1KjkFHZD5rmL\n"
    "c1wWvdJn+IxKES40/C3vssGHdygeXNS3dxC9LH4PD5cqkDRAlPNSAoTnbB+zkjl4\n"
    "Fx41/St/zzmIhJZ5KYyNabgdwsdbmEuH3qZxbyD9mN5bXjOayvP3wt1ByoLS+zvR\n"
    "FqpYif66n36e5chjJ4SIO0zzFbDyxLXyWC5XypHnxHG8yq6rKU0txEG+FBazfyKJ\n"
    "JJCcmO4Q1Pt654S7apboN/b18uVvAjpw39zxmSDHQNH0G5OwtQqrJqSlC3x0kK92\n"
    "HJRd4Qne5ztOssQ9rax1eOllG+V860mEfDhAzGn89O3TJ0JpbQh9xpW2Em1Y7uem\n"
    "upLI8AhSLrW+ZTFNXVrxYR40P560IAxA4pGJzMMAK0aMv0v4nG6pZINlgiZXJXpw\n"
    "GhI0NrRpCGGw4T023gndvALL5D14RXbE0KJSYWbBiLLt/42qLVNtiQtEAAR7YVKw\n"
    "+YjUkS2g4DGrma6cjo8nkZu5ApPx1Ca2nKI5zQPVbTFRAgMBAAE=\n"
    "-----END PUBLIC KEY-----\n";

constexpr size_t RSA3072_PUBLIC_KEY_PEM_BYTES =
    sizeof(RSA3072_PUBLIC_KEY_PEM);

} // namespace FirmwareTrust

#endif
