#ifndef AQUAHUB_OTA_H
#define AQUAHUB_OTA_H

#include <stddef.h>

/** Initializes the bounded OTA state machine. Safe to call more than once. */
bool aquahub_ota_initialize();

/**
 * Confirms a newly booted image only after the registry, HTTPS API and local
 * broker have started. This keeps rollback meaningful instead of acknowledging
 * an image before its critical services are healthy.
 */
bool aquahub_ota_confirm_running_image();

/** Starts an asynchronous manifest refresh. Returns false while another job runs. */
bool aquahub_ota_request_check();

/** Starts an asynchronous A/B installation of the validated available release. */
bool aquahub_ota_request_install();

/** Serializes the current OTA capability and progress for the HTTPS API. */
bool aquahub_ota_write_json(char *output, size_t output_capacity);

#endif
