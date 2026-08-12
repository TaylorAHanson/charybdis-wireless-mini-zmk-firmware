# Charybdis Wireless Mini ZMK Firmware

ZMK firmware configuration for the **BastardKB Charybdis 3x6 Split Wireless Keyboard** powered by **nice!nano v2** microcontrollers with an integrated **PMW3610 optical trackball sensor**.

---

## Quick Links

- 📖 **[Flashing & Pairing Guide](FLASHING_GUIDE.md)** — Step-by-step instructions on putting nice!nano into bootloader mode, flashing UF2 files, resetting pairings, and pairing to Bluetooth.
- ⚙️ **[Keymap Configuration](config/charybdis.keymap)** — Custom QWERTY layout, home-row modifiers, mouse keys, arrows, and symbol layers.
- 🔌 **[Shield Hardware Overlays](boards/shields/charybdis/)** — Devicetree definitions for left, right, and PMW3610 trackball 3-wire SPI communication.

---

## Features

- **Latest ZMK Base**: Built against modern ZMK using official GitHub Actions (`zmkfirmware/zmk/.github/workflows/build-user-config.yml@main`).
- **PMW3610 Optical Trackball Support**:
  - Configured for 1:1 ribbon cable with half-duplex 3-wire SPI (`SDIO` on MOSI `P0.17`, `MOTION` interrupt on MISO `P0.06`).
  - Power stabilization delay (`CONFIG_PMW3610_ALT_INIT_POWER_UP_EXTRA_DELAY_MS=1000`) to ensure reliable sensor initialization.
  - Native pointing subsystem (`CONFIG_ZMK_POINTING=y`).
- **Split Bluetooth Architecture**:
  - Right half acts as Central (receives Left half keystrokes and transmits keyboard + mouse events to host computer).
  - Left half acts as Peripheral.
- **Home Row Modifiers & Custom Layers**:
  - Layer 0: Base QWERTY with home-row modifiers.
  - Layer 1: Mouse buttons and wheel navigation.
  - Layer 2: Arrow navigation keys.
  - Layer 3: Numbers and symbols.

---

## How to Flash

1. Go to the **Actions** tab on your GitHub repository.
2. Click the latest completed build run and download the **Artifacts** (`charybdis_left`, `charybdis_right`, `settings_reset`).
3. Connect the controller to your computer via USB, double-click the **Reset** button to mount the `NICENANO` USB drive, and drag the `.uf2` file into the drive.
4. For detailed steps, see **[FLASHING_GUIDE.md](FLASHING_GUIDE.md)**.