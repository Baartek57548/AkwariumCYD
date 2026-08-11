#ifndef AQUAHUB_BROKER_H
#define AQUAHUB_BROKER_H

#include <stdint.h>

bool aquahub_broker_start(const char *username,
                          const char *password,
                          uint16_t port);
void aquahub_broker_stop();
bool aquahub_broker_running();

#endif
