#ifndef AQUAHUB_API_H
#define AQUAHUB_API_H

#include <stddef.h>

using AquaHubPublishCallback =
    bool (*)(const char *topic,
             const char *payload,
             int qos,
             bool retained,
             void *context);

bool aquahub_api_start(AquaHubPublishCallback publish,
                       void *publish_context);
void aquahub_api_stop();
bool aquahub_api_running();

#endif
