# Moto ESP32-P4 open integration reference

[中文](README.md) · [Features](docs/FEATURES.en.md) · [Architecture](docs/ARCHITECTURE.md)

A board application for JC1060P470C_I_W_Y, JD9165 1024×600, ESP32-P4 + ESP32-C6. Wi-Fi pairing, navigation/mirroring reception, HUD, notifications and instrument switching have board adapters. Vehicle telemetry remains visibly DEMO data.

**The application and adapters are open source; Moto SDK implementation remains in proprietary static libraries.** Obtain the SDK 0.1.0 / FreeRTOS ABI 6 / ESP-IDF 5.5.4 binary package and your device's license from the integration team. No SDK implementation, SDK distribution, device license or private key is published here.

## Build the full board application

Install ESP-IDF 5.5.4 with its toolchain, Python 3.10+ and Git. Substitute your SDK package and IDF installation paths:

```sh
git clone https://github.com/oounet/moto-esp32p4-example.git
cd moto-esp32p4-example
python3 tools/import_sdk.py /path/to/moto-sdk-0.1.0-freertos-esp32p4-binary-20261010-abi6.zip
python3 tools/fetch_lvgl.py
. /path/to/esp-idf-v5.5.4/export.sh
idf.py build
```

The importer checks package hashes and installs only headers, archives, component metadata and notices into ignored `vendor/moto-sdk`. LVGL is pinned to its official 9.3.0 commit. ESP-IDF resolves Espressif components from `dependencies.lock`. No adjacent private repositories are needed. Output: `build/moto_p4_example.bin`.

Without provisioning, the build can exercise the display but shows LICENSE REQUIRED and does not fake authentication. Add the platform's trusted public verification header:

```sh
idf.py -DMOTO_LICENSE_TRUST_HEADER=/absolute/path/to/moto-license-trust.h build
```

The header declares `static const uint8_t moto_license_trust_key[65]`, containing the trusted P-256 public key. Do not derive trust from the license being checked. See [Provisioning](docs/PROVISIONING.md).

After checking your board, wiring, partition layout and backup, use your serial port with `idf.py -p PORT flash monitor`; an existing compatible partition layout can use `app-flash`. These commands write hardware; builds/tests never flash automatically.

## Learn and port each feature

- `main/` is the complete board application. It uses the SDK's **public low-level C APIs**, including independent Wi-Fi business and BLE notification sessions plus video tasks. It is not SDK implementation source.
- `examples/managed-terminal/` is a separate, simpler ABI 6 FreeRTOS project with SDK-owned tasks/events, compilable feature examples and replaceable BSP. Do not start both architectures in one firmware.
- [Feature matrix](docs/FEATURES.en.md) identifies implemented paths, examples, missing hardware and acceptance steps. Advertising, OTA and call examples do not imply the board has a media player, OTA partition layout or HFP hardware.

```sh
cd examples/managed-terminal
idf.py build
```

Implement license/Wi-Fi transport/display in `main/board_driver.c`, then add features from `snippets/`. Unimplemented drivers fail explicitly and optional features stay off.

## Validation and licensing

```sh
python3 tools/test_host.py
python3 tools/audit_public.py
```

See [validation](docs/VALIDATION.md). The public UI uses native LVGL widgets and redistributable fonts; original dashboard artwork and system-font derivatives are excluded. P4/C6 does not provide Classic Bluetooth HFP; call audio requires external hardware. Validate radio, rendering, storage and power-loss behavior on your board.

Application code is MIT; third-party files retain their licenses in [THIRD_PARTY.md](THIRD_PARTY.md). The license does not grant redistribution rights to Moto SDK headers or binaries.

## Offline maps

The root project includes compressed map reception and route/Wi-Fi position rendering. See [Offline maps](docs/OFFLINE_MAPS.en.md). Use matching revision 11 or newer SDK headers and archives. Move the old vendor/moto-sdk to a local backup before importing an upgrade; do not replace headers alone.
