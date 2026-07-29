# Config

Configuration files in this directory are intended to be copied, edited, and
renamed by the user before the firmware starts consuming them.

Example workflow:

```text
ui.cfg.example          -> ui.cfg
calibration.cfg.example -> calibration.cfg
schedules.cfg.example   -> schedules.cfg
wifi_profiles.cfg.example -> wifi_profiles.cfg
device.cfg.example -> device.cfg
safe_mode.flag.example -> safe_mode.flag
```

The active runtime configuration can still live in ESP32 NVS/Preferences. SD
config files are intended for import, export, diagnostics, and recovery.

Wi-Fi credentials confirmed by a successful ESP32 connection are stored in the
ESP32 NVS namespace `aq_wifi_sec`. Production devices must protect NVS with
Flash Encryption. The removable card contains only non-secret connection
metadata:

```text
/aq/config/wifi/profile_<ssid_hash>.cfg
```

The files contain the SSID, last IP, RSSI and update time, but never the
password. During the first boot after upgrading, legacy v1 profiles are copied
to NVS and overwritten with the password-free v2 format. The old file bytes are
zeroed on a best-effort basis before removal; securely erase archival card
images separately.

## Remote alarm gateway trust anchor

The optional outbound alarm relay accepts only HTTPS. Place the issuing CA
certificate chain for the configured gateway at:

```text
/aq/config/gateway-ca.pem
```

The PEM file must contain one or more `BEGIN CERTIFICATE` blocks, be smaller
than 6144 bytes and contain no private key. Use the stable issuing CA/root chain,
not the gateway's short-lived private key. After replacing this file, restart
the controller or wait for the relay retry. Gateway URL, device ID and HMAC
secret are provisioned separately through authenticated, bonded BLE; the HMAC
secret must never be written to this card.

`safe_mode.flag.example` is a reserved recovery-file contract and is not
consumed by firmware 6.0.0. Renaming it does not change the boot path.
