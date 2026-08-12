#ifndef AQUAHUB_IDENTITY_H
#define AQUAHUB_IDENTITY_H

#include <stddef.h>
#include <stdint.h>

constexpr size_t kAquaHubAccessTokenBytes = 65U;
constexpr size_t kAquaHubFingerprintBytes = 65U;

bool aquahub_identity_initialize();
const uint8_t *aquahub_identity_certificate(size_t *length);
const uint8_t *aquahub_identity_private_key(size_t *length);
const char *aquahub_identity_fingerprint();

uint32_t aquahub_identity_pairing_code();
uint32_t aquahub_identity_pairing_seconds_remaining();
void aquahub_identity_rotate_pairing_code();

/** Exchanges the physical six-digit code for a random bearer token. */
bool aquahub_identity_pair(uint32_t code,
                           char *token,
                           size_t token_capacity);

/** Validates an exact `Bearer <64 lowercase hex>` authorization header. */
bool aquahub_identity_authorize(const char *authorization_header);

#endif
