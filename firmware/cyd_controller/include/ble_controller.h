#ifndef AQUARIUM_BLE_CONTROLLER_H
#define AQUARIUM_BLE_CONTROLLER_H

#include <stdint.h>

/**
 * Schedules deletion of all BLE bonds in the BLE task. The request is
 * asynchronous so the command response can be delivered before the active
 * secure link is disconnected.
 */
bool ble_controller_request_forget_bonds();

/** Returns the number of peer identities currently persisted by NimBLE. */
int ble_controller_bond_count();

#endif
