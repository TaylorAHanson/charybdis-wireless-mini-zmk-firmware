# Known Good ZMK Trackball Hardware (Zephyr 4.1 Upstream)

If you want to use the **official, upstream Zephyr 4.1 driver** without relying on third-party modules (`badjeff`, `inorichi`), without Kconfig init-delay hacks, and without physical resistor/diode soldering hacks, you must respect the fundamental hardware limitations of the components. 

Here are the cold, hard physical facts of the current ZMK/Zephyr ecosystem:

## The Root of the Problem
1. **The PMW3610 is a 3-Wire Sensor:** It physically only possesses an `SDIO` pin, meaning it requires a half-duplex SPI bus where the microcontroller releases the TX line during read cycles.
2. **The nRF52840 Hardware Limit:** The Nordic nRF52840 (the chip powering the nice!nano, SuperMini, and nice!view) uses the `nrfx_spim` hardware peripheral. The Zephyr 4.1 implementation of this peripheral **flatly refuses** to initialize if passed the `SPI_HALF_DUPLEX` flag (throwing `-ENOTSUP`). 

Therefore: **There is no combination of an nRF52840 and a PMW3610 that can natively run upstream hardware SPI without a physical resistor/diode hack or a third-party software module.**

If you are willing to spend money to replace components for a perfectly clean, natively supported upstream build, choose one of the following two paths:

---

## Path A: Keep the nRF52840, Change the Sensor (Highly Recommended)
If you want to keep your SuperMini / nice!nano for its fantastic battery life and Bluetooth performance, you must buy a sensor that natively speaks **4-Wire SPI**.

**The Known Good Sensor:** `PMW3360` (or `PMW3389`)
*   **Why it works natively:** The PMW3360 has physically separate `MOSI` and `MISO` pins. 
*   **The Result:** You plug `MOSI` to `MOSI` and `MISO` to `MISO`. The Zephyr driver requires absolutely zero half-duplex flags, zero physical resistors, and zero `badjeff` modules. It initializes flawlessly on upstream Zephyr because it operates as a standard, full-duplex SPI device.
*   **Where to buy:** Breakouts for the PMW3360 are widely available on Tindie, AliExpress, or through BastardKB and Tractyl projects. 
*   **Trade-off:** The 3360 draws slightly more power than the 3610, but the stability and native support are well worth it.

---

## Path B: Keep the PMW3610, Change the Microcontroller
If you want to keep your PMW3610 sensor for its ultra-low power consumption, you must replace your microcontroller with one whose Zephyr SPI implementation natively supports dynamic `SPI_HALF_DUPLEX` data line switching.

**The Known Good MCU:** `RP2040` based boards (e.g., SeaMicro, Splinky, or Elite-Pi)
*   **Why it works natively:** The RP2040's hardware SPI (and its PIO system) is fully capable of bidirectional 3-wire SPI. Zephyr's RP2040 SPI driver accepts the `duplex = <2048>;` flag natively and dynamically disables the TX buffer during reads.
*   **The Result:** You simply map both `MOSI` and `MISO` in the Devicetree to the exact same physical pin, connect that one pin to the PMW3610's `SDIO`, and it works flawlessly on the upstream native Zephyr driver without a single resistor.
*   **Where to buy:** SeaMicro (by Beekeeb), Splinky (by splin), or Elite-Pi. Ensure it matches the Pro Micro footprint of your Charybdis shield.
*   **Trade-off:** RP2040 boards generally do not have built-in Bluetooth (they are wired-first). There are some wireless RP2040 implementations emerging, but the nRF52840 is still the absolute king of ZMK wireless. 

---

## Summary Verdict
If you are building a **wireless** board, you want to stay on the nRF52840 (SuperMini or nice!nano). 
The absolute best "money-fixes-the-problem" solution is to buy a **PMW3360 breakout board**. 

It eliminates the 3-wire half-duplex nightmare, allows you to use the strict, official ZMK/Zephyr upstream driver, and requires zero resistor hacks. It is the gold standard for clean ZMK trackball builds.
