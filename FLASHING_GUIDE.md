# Charybdis Wireless Mini - Flashing & Pairing Guide

This guide walks you through downloading your built firmware, flashing the nice!nano v2 microcontrollers, and pairing your split keyboard halves.

---

## 1. How the Firmware is Built

When you push changes to your GitHub repository (or click **Run workflow** manually under the Actions tab):
1. GitHub Actions automatically compiles the firmware for both halves plus a reset file.
2. In the **Actions** tab on your GitHub repository, click the latest workflow run.
3. Scroll down to the **Artifacts** section and download the ZIP file (named `firmware` or individual `.uf2` files):
   - `charybdis_left.uf2` (Left Half - Peripheral)
   - `charybdis_right.uf2` (Right Half - Central & Trackball)
   - `settings_reset.uf2` (Settings Reset / Pairing Clear)

---

## 2. Flashing Procedure (Step-by-Step)

> [!IMPORTANT]
> **First-Time Flash / Major Upgrade Recommendation**:
> Because the Bluetooth and HID pointing descriptors were updated, flash `settings_reset.uf2` onto **both** halves first before flashing the new firmware. This ensures any stale Bluetooth bonding data is cleared cleanly.

### Step 2.1: Clear Bluetooth Settings (Optional but Recommended)
1. Remove/Forget "Charybdis" in your computer / phone Bluetooth settings.
2. Connect the **Right half** to your computer via USB-C.
3. **Double-press** the physical reset button on the nice!nano (or the case reset button).
4. A drive called `NICENANO` will appear in Windows File Explorer.
5. Drag and drop `settings_reset.uf2` into the `NICENANO` drive.
6. The drive will automatically disconnect.
7. Repeat the exact same steps for the **Left half** with `settings_reset.uf2`.

---

### Step 2.2: Flash the Left Half
1. Connect the **Left half** to your computer via USB-C.
2. **Double-press** the reset button.
3. When the `NICENANO` drive appears, drag and drop `charybdis_left.uf2`.
4. The nice!nano will flash and reboot automatically.
5. Unplug the Left half.

---

### Step 2.3: Flash the Right Half (Central + Trackball)
1. Connect the **Right half** to your computer via USB-C.
2. **Double-press** the reset button.
3. When the `NICENANO` drive appears, drag and drop `charybdis_right.uf2`.
4. The nice!nano will flash and reboot automatically.

---

## 3. Pairing & Connecting

1. **Power Up Both Halves**:
   - Turn the power switch **ON** on the Right half.
   - Turn the power switch **ON** on the Left half.
2. **Sync the Split Halves**:
   - Keep the halves near each other. They will automatically pair via Bluetooth.
   - If they do not connect immediately, single-press the reset button on both halves at the same time.
3. **Pair with your Computer**:
   - On your computer, open **Bluetooth & devices** -> **Add device**.
   - Select **"Charybdis"**.
   - Your keyboard and PMW3610 trackball cursor will now be active!

---

## 4. Hardware Wiring & Driver Reference

This firmware is configured for standard Charybdis 3x6 with a 1:1 straight-through 6-pin ribbon cable to the PMW3610 sensor:

| Sensor Pin | Ribbon Cable Wire | Mainboard / Shield Pin | Controller GPIO | Role in Software |
| :--- | :--- | :--- | :--- | :--- |
| **VDD** | Wire 1 | 3.3V / VCC | 3.3V Rail | Sensor Power |
| **GND** | Wire 2 | GND | GND Rail | Sensor Ground |
| **SDIO** | Wire 3 | MOSI | `P0.17` (Pro Micro 2) | Half-Duplex SPI (MOSI & MISO) |
| **MOTION** | Wire 4 | MISO | `P0.06` (Pro Micro 0) | Hardware Interrupt (`irq-gpios`) |
| **SCLK** | Wire 5 | SCK | `P0.08` (Pro Micro 1) | SPI Clock |
| **NCS** | Wire 6 | CS | `P0.20` (Pro Micro 3) | Chip Select (`cs-gpios`) |

### Key Driver Configurations:
- **Compatible Driver**: `pixart,pmw3610-alt`
- **Power Stabilization Delay**: `CONFIG_PMW3610_ALT_INIT_POWER_UP_EXTRA_DELAY_MS=1000` (fixes `0xFF` product ID detection issue)
- **Pointing Subsystem**: Modern ZMK `CONFIG_ZMK_POINTING=y`

---

## 5. Layer Reference

- **Layer 0 (BASE)**: QWERTY layout with Home Row Mods (`A`, `S` [Alt], `D` [Ctrl], `F` [Shift] / `J` [Shift], `K` [Ctrl], `L` [Alt], `;`). Mouse cursor moves via trackball.
- **Layer 1 (Mouse)**: Mouse clicks (`MB1`, `MB2`, `MB3`), scroll buttons (`SCROLL_UP`, `SCROLL_DOWN`, `SCROLL_LEFT`, `SCROLL_RIGHT`), navigation shortcuts.
- **Layer 2 (Arrow)**: Navigation arrows (`UP`, `DOWN`, `LEFT`, `RIGHT`), `HOME`, `END`.
- **Layer 3 (Symbol)**: Number row (`0-9`), math symbols (`-`, `=`), backtick (`` ` ``).
