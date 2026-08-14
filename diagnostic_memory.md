# Diagnostic Memory - PMW3610 Trackball

## 1. Hardware State & Voltages
- **Mainboard 3.3V Output:** Confirmed 3.3V *(Confidence: 100% - User verified via multimeter)*
- **Sensor Breakout VCC Pad:** Confirmed 3.3V *(Confidence: 100% - User verified)*
- **Sensor Internal LDO (C15849):** 2.47V *(Confidence: 95% - Expected ~1.8V-2.2V. 2.47V suggests the sensor's VDD logic is alive but slightly back-powered by 3.3V SPI pull-ups via internal ESD diodes, which is normal for direct 3.3V-to-1.8V connections without level shifters)*.
- **SDIO (SPI Data):** 2.2V *(Confidence: 100% - Matches the PMW3610 internal logic high voltage, proving the sensor is alive and actively driving the bus)*.
- **SCLK, NCS, MOTION:** 3.3V *(Confidence: 100% - Pulled high by the MCU, normal).*
- **Ribbon Cable Continuity:** Verified *(Confidence: 100% - User confirmed no crosses/shorts).*

## 2. The Pin Mapping Dilemma
The Charybdis trackball adapter shield has 6 pads labeled: `GND`, `MISO`, `SCLK`, `MOSI`, `CS`, `VCC`.
However, the PMW3610 sensor breakout also has 6 pads, but they are: `GND`, `MOTION`, `SCLK`, `SDIO`, `NCS`, `VCC`.

**User's Traced MCU Pins (Upside-down view verified):**
- **Pad `VCC`** -> `Pin 21` (VCC)
- **Pad `GND`** -> `Pin 23` (GND)
- **Pad `CS`** -> `P0.20`
- **Pad `SCLK`** -> `P0.08`
- **Pad `MOSI`** -> `P0.17`
- **Pad `MISO`** -> `P0.06`

**The Core Uncertainty Resolved (Confidence: 100%):**
We tested "Possibility B" (4-wire hack where MISO was set to `P0.06`). The sensor still reported `0xFF`. 
Furthermore, an exhaustive review of the `280Zo` build guide confirms that the `victorlucachi` breakout board natively uses a 3-wire `SDIO` bus. The `Elite-C-holder` adapter shield explicitly routes the sensor's `SDIO` to the shield's `MOSI` pad, and the sensor's `MOTION` interrupt to the shield's `MISO` pad.
*Conclusion:* "Possibility A" is unequivocally the correct physical wiring. The firmware **MUST** use `P0.17` for both `SPIM_MOSI` and `SPIM_MISO`, and `P0.06` for `irq-gpios`.

## 3. Software Configuration State
- **ZMK Pointing Enabled:** Yes, `CONFIG_ZMK_POINTING=y` *(Confidence: 100% - Present in configs)*
- **USB Logging Enabled:** Yes, `CONFIG_ZMK_USB_LOGGING=y` *(Confidence: 100%)*
- **SPI Clock Frequency:** 2MHz *(Confidence: 100% - Restored in DTSI)*
- **Power-Up Delay:** 2000ms *(Confidence: 100% - Present in configs)*

## 4. The `0xFF` Error & The Zephyr 4.1 Fix
**Fact:** The log captured earlier showed `Incorrect product id 0xff (expecting 0x3e)!`.
**Assumption Corrected:** Hardware lockup ruled out via repeated hard power-cycles.

**Root Cause (Software Driver Conflict):** 
With physical wiring and voltages verified 100% correct, the issue lies in how legacy third-party drivers (like `badjeff` and `inorichi` on ZMK `v0.3`) handle 3-wire SPI on the Nordic nRF52840. Because the PMW3610 sensor physically uses a single 3-wire SPI interface (SDIO pin), MOSI and MISO are shorted to `P0.17` on the nice_nano. When the nRF52840 executes a full-duplex read on a 3-wire shared pin without specifically instructing the hardware to disable the MOSI pin, the hardware SPIM peripheral actively drives the MOSI pin with dummy clock bytes (e.g. `0xFF`) during the read phase. This completely drowns out the sensor's response and results in the nice!nano reading its own dummy byte: `0xFF`.

The standard Zephyr hardware fix is to use the `spi-half-duplex;` devicetree property, which instructs the nRF52 to temporarily disable the MOSI pin during reads. However, the YAML compiler definitions for the third-party modules explicitly forbade this property, creating an impossible loop on ZMK `v0.3`.

## 5. The Failed Attempts & Hardware Limitations
To resolve the `0xFF` collision, we attempted to enforce `SPI_HALF_DUPLEX` on the nice!nano's hardware SPI driver. This resulted in a series of failures that exposed a fundamental hardware limitation of the nRF52:
1. **Third-Party Drivers (`badjeff`/`inorichi` on ZMK `v0.3`):** We attempted to use the legacy `spi-half-duplex;` devicetree property. This failed because community modules explicitly rejected the property in their YAML definitions.
2. **Native Zephyr 4.1 Driver (Attempt 1):** We upgraded to ZMK `main` (Zephyr 4.1) and used the native `pixart,pmw3610` driver. We tried injecting `spi-half-duplex;`, but the Zephyr compiler rejected it because the property is deprecated in 4.1.
3. **Native Zephyr 4.1 Driver (Attempt 2):** We used the modern integer property `duplex = <2048>;` (`SPI_HALF_DUPLEX`). The code compiled, but the boot log threw: `spi_nrfx_spim: Half-duplex not supported` followed by another `0xFF` collision.

**The Hardware Truth:** The Nordic nRF52's SPIM (SPI Master with EasyDMA) hardware block **does not physically support half-duplex SPI**. It is incapable of disabling the MOSI pin during a read operation. Therefore, any attempt to use the `nordic,nrf-spim` hardware driver on a 3-wire shared bus will inevitably result in a collision unless external hardware (a resistor) is added.

## 6. The Software Solution: Bit-Banged SPI (`spi-gpio`) - FAILED
Since the hardware SPIM peripheral cannot handle 3-wire SPI, we attempted a definitive software workaround: bypassing the SPIM hardware entirely using **software bit-banging** (`spi-gpio`). 
By converting the SPI node to a `spi-gpio` device and setting `duplex = <2048>;`, Zephyr should theoretically manually toggle the `P0.17` pin from output to input during reads.

**The Fatal Result:** The `spi-gpio` bitbang driver crashed instantly with `spi_bitbang: Both RX and TX specified in half duplex mode (Device configuration failed: -22)`.
This error reveals the final nail in the coffin: The native Zephyr 4.1 `input_pmw3610.c` driver is hardcoded to execute full-duplex `spi_transceive()` API calls. It attempts to send and receive data simultaneously. When forced onto a half-duplex bus, the bitbang driver panics because you cannot do a full-duplex transceive on a single wire.

**Conclusion:** A pure software 3-wire SPI implementation for the PMW3610 on the nRF52 running modern ZMK/Zephyr is physically impossible without patching the core OS kernel drivers. 

## 7. The Only Solution: The 4.7k Resistor "Pseudo 4-Wire" Hack
Because the Zephyr drivers demand a 4-wire interface, the only remaining solution is to physically convert the 3-wire sensor into a 4-wire interface.

**How it works:**
1. Desolder the bridge between `MOSI` and `MISO`. Assign `MOSI` and `MISO` to **different** GPIO pins on the nice!nano. 
2. Wire the sensor's single `SDIO` pad directly to the nice!nano's `MISO` pin.
3. Place a **4.7kΩ resistor** between the nice!nano's `MOSI` pin and the sensor's `SDIO` pad.

During a write phase, the nice!nano drives the signal through the resistor to the sensor. During a read phase, the sensor drives the `SDIO` line directly into the MCU's `MISO` pin. Because they are on separate pins, the MCU's dummy clock bytes outputted on `MOSI` are blocked by the 4.7k resistor, allowing the sensor's much weaker signal to safely overpower the node and be read correctly by `MISO`.

## 8. The ENOTSUP / Zephyr 4.1 Native Driver Conflict
*   **The Error:** `input_pmw3610: Device configuration failed: -134`.
*   **The Cause:** Error `-134` in Zephyr is `-ENOTSUP` (Not Supported). The native Zephyr 4.1 `input_pmw3610.c` driver is hardcoded to request a `SPI_HALF_DUPLEX` transfer in its C source code. However, the Nordic `nrf-spim` hardware SPI driver explicitly rejects `SPI_HALF_DUPLEX` requests and immediately aborts the transfer, leaving the receive buffer empty (resulting in `Invalid product id: 00`).
*   **The Implication:** The native Zephyr 4.1 PMW3610 driver is fundamentally and permanently incompatible with the Nordic nRF52 hardware SPI controller. It cannot be fixed via Devicetree overlays.
*   **The Fix:** We must use the community `badjeff/zmk-pmw3610-driver` (specifically the `zmk-0.4` branch, which is compatible with Zephyr 4.1's input subsystem but does not hardcode `SPI_HALF_DUPLEX`).
    *   `west.yml` updated to pull `badjeff` revision `zmk-0.4`.
    *   Devicetree `compatible` updated to `pixart,pmw3610-alt`.
    *   Kconfig updated to `CONFIG_PMW3610_ALT=y` and `CONFIG_PMW3610_ALT_INIT_POWER_UP_EXTRA_DELAY_MS=1000`.
    *   Orientation configuration (e.g., `swap-xy;`) moved from Kconfig to Devicetree node properties per `zmk-0.4` requirements.

## 9. Configuration Validations: Native vs Alt Driver
The Devicetree schema requirements strictly differ between the native Zephyr 4.1 driver and the `badjeff` alt driver. Mixing them will result in a Devicetree compiler failure (`devicetree error: '...' is marked as required`).

**Native Zephyr 4.1 (`pixart,pmw3610`):**
*   Requires `zephyr,axis-x = <INPUT_REL_X>;`
*   Requires `zephyr,axis-y = <INPUT_REL_Y>;`
*   Requires `spi-cpol;` and `spi-cpha;` to be explicitly declared.
*   Requires `motion-gpios`.

**Badjeff Alt Driver (`pixart,pmw3610-alt` branch `zmk-0.4`):**
*   Requires `evt-type = <INPUT_EV_REL>;`
*   Requires `x-input-code = <INPUT_REL_X>;`
*   Requires `y-input-code = <INPUT_REL_Y>;`
*   Requires `irq-gpios` (replaces `motion-gpios`).
*   Requires `spi-cpol;` and `spi-cpha;` to be explicitly declared (same as the native driver, as the `zmk-0.4` branch migrated away from hardcoded C-level SPI modes).
*   Requires `#include <zephyr/dt-bindings/input/input-event-codes.h>` in the `.dtsi` to resolve the `INPUT_EV_REL` macros.
## 10. The Voltage Divider / `overrun-character` Hypothesis
Even with the `badjeff` alt driver successfully executing SPI transfers (avoiding `-ENOTSUP`), the sensor failed its internal self-test (`Failed self-test (0x0)`).
*   **The Theory:** During a read operation, standard hardware SPI controllers still drive the `MOSI` pin to generate clock cycles. Zephyr's SPI driver defaults to driving `0x00` (0V). 
*   Because the `MOSI` pin is driving 0V through a 10k resistor into the sensor's `SDIO` pin, it creates a voltage divider. If the PMW3610 uses an open-drain output (or has a weak 3.3V drive/pull-up, typically ~10k) for `SDIO`, the `MOSI` pin's 0V pull-down cuts the 3.3V signal to ~1.65V. 
*   The nRF52's Input High Voltage (`VIH`) threshold is ~2.31V. Thus, the 1.65V signal is read as a logic `0`, causing the receive buffer to fill with `0x00` and failing the self-test.
*   **The Fix:** Even with `overrun-character = <0xff>;`, the hardware might still be enforcing `0x00` during reads, or the sensor's logic level is just too weak. To overcome this voltage divider, the series resistor must be lowered. Using a **2.2kΩ resistor** instead of 10kΩ shifts the voltage divider math (3.3V * 10k / 12.2k = ~2.7V). 2.7V is well above the nRF52's 2.31V logic high threshold, guaranteeing a successful read, while still safely limiting the current (1.5mA) during contention.
*   **The 0xFF Read & Over-Read Character (ORC) Revelation:** After configuring both MISO and MOSI to the same physical pin (`P0.10`), the sensor self-test failed with `0xFF` instead of `0x00`. This proved that the nRF52's `nrfx_spim` hardware uses an Over-Read Character (ORC) defaulting to `0xFF`, which actively drives 3.3V on the MOSI pin during dummy reads. Because the nRF52's push-pull drive (~200Ω) is stronger than the PMW3610's open-drain pull-down, the nRF52 overpowers the sensor, causing it to read `0xFF` continuously. This proved that a true "same pin" 3-wire hack without a resistor is physically impossible on this nRF52 configuration.
*   **The Upside-Down Topology Discovery:** When investigating the physical traces against the `FLASHING_GUIDE.md`, the user mapped their continuity from the bottom of the board (USB facing up). This flipped the left and right sides. Once corrected to a top-down view, the user's traces precisely matched their *original* `.dtsi` configuration (`P0.17` for SDIO, `P0.08` for SCLK, `P0.06` for IRQ, `P0.20` for CS). The `FLASHING_GUIDE.md` table was either outdated or for a different revision.
*   **Final Resistor Hack Architecture:** Because the nRF52 hardware requires physical separation to prevent the ORC from overpowering the sensor, the 2.2kΩ resistor hack is mandatory. The final configuration sets the unused `P0.28` (5th pin down, right side) as the `MOSI` driver, which pushes through the resistor to the true `SDIO/MISO` line on `P0.17` (5th pin down, left side).
*   **The Hardware Disconnect Discovery (and Alt Driver Failure):** We attempted to map both MOSI and MISO to `P0.17` without a resistor earlier in the process. This resulted in an `0xFF` read (with the internal pull-up enabled), rather than the expected overpower by the ORC. We initially hypothesized this proved the `nrfx_spim` hardware completely disconnects the `MOSI` pin during dummy reads. However, we later realized this `0xFF` read was a failure of the `badjeff` alt driver. The legacy alt driver does not natively understand or implement the Zephyr 4.1 `duplex = <2048>;` (`SPI_HALF_DUPLEX`) devicetree property correctly to tell the Nordic SPI API to release the TX buffer.
*   **The SuperMini Pinout Discovery:** The user provided an image proving they are using a **SuperMini NRF52840**, not a standard nice!nano. The SuperMini has a fundamentally different physical pinout for its analog pins. `P0.28` (which the devicetree was using for the MOSI driver) does not exist on the edge headers. When the user was instructed to solder the resistor to the "5th pin down, right side" (assuming it was `P0.28`), they actually soldered it to `VCC`. This perfectly explains why the sensor was deaf during the resistor hack: it was receiving constant 3.3V power on its data line instead of the clock commands, while the microcontroller was screaming into a physically disconnected pad inside the chip. 
*   **Transition to Built-in Zephyr Driver for True SPI_HALF_DUPLEX:** Realizing the `badjeff` driver was failing to execute half-duplex, and knowing our physical pins were finally correct (`P0.17`, `P0.08`, `P0.06`, `P0.20`), we completely discarded the `badjeff` module and the root Kconfig overrides (`config/charybdis_right.conf`) in favor of the built-in Zephyr `pixart,pmw3610` driver. This modern driver passes the `SPI_HALF_DUPLEX` flag directly to the Zephyr SPI API, which theoretically eliminates the need for the resistor/diode hack entirely, allowing us to map both `MOSI` and `MISO` cleanly to `P0.17`.
*   **Devicetree Binding Gotchas:** When migrating from the `badjeff/zmk-pmw3610-driver` to the upstream Zephyr driver, several critical property names changed and will cause `CMake` build failures if missed:
    *   `irq-gpios` must be changed to `motion-gpios`.
    *   `x-input-code` and `y-input-code` must be changed to `zephyr,axis-x` and `zephyr,axis-y`.
    *   `cpi` must be changed to `res-cpi`.
    *   `swap-xy`, `invert-x`, and `invert-y` must be removed from the sensor node entirely. They are now handled by the ZMK input subsystem using `input-processors = <&zip_xy_transform (INPUT_TRANSFORM_XY_SWAP | INPUT_TRANSFORM_X_INVERT)>;` inside the `zmk,input-listener` node, which requires `#include <dt-bindings/zmk/input_transform.h>` AND `#include <input/processors.dtsi>`.
    *   `evt-type` is no longer supported/required by the sensor node itself.
    *   The `CONFIG_PMW3610_ALT_INIT_POWER_UP_EXTRA_DELAY_MS` Kconfig option is invalid for the built-in driver.

## The Software Boot Delay (Native vs Alt Driver)
Even with the physical resistor hack working perfectly, you cannot use the official Zephyr upstream driver out-of-the-box on a wireless board using `ext-power`.
*   **The `ext-power` Problem:** ZMK routes sensor power through a MOSFET to save battery. This rail takes hundreds of milliseconds to fully turn on.
*   **The Native Driver Failure:** The official upstream Zephyr `pixart,pmw3610` driver fires its initialization sequence instantly on boot. It hits the sensor before power is stable, reads an empty bus (`0xFF`), throws `Invalid product id: ff`, and crashes with `-134`.
*   **The `badjeff` Solution:** The `badjeff/zmk-pmw3610-driver` module was specifically built by the community to inject a 1-second software delay *before* initialization, allowing the power rail to stabilize.

### Critical Driver Configurations (Do Not Cross Streams)
If you switch between the native driver and the `badjeff` driver, you **must** change ALL of these properties, or the build will fail:

| Property / Kconfig | Native Zephyr Upstream | `badjeff` Alt Driver |
| :--- | :--- | :--- |
| **Kconfigs** | `(None)` | `CONFIG_PMW3610_ALT=y` <br> `CONFIG_PMW3610_ALT_INIT_POWER_UP_EXTRA_DELAY_MS=1000` |
| **Compatible** | `compatible = "pixart,pmw3610";` | `compatible = "pixart,pmw3610-alt";` |
| **IRQ Pin** | `motion-gpios` | `irq-gpios` |
| **Resolution** | `res-cpi` | `cpi` |
| **Axes** | `zephyr,axis-x = <INPUT_REL_X>;`<br>`zephyr,axis-y = <INPUT_REL_Y>;` | `x-input-code = <INPUT_REL_X>;`<br>`y-input-code = <INPUT_REL_Y>;`<br>`evt-type = <INPUT_EV_REL>;` |
| **Axis Transforms** | Handled via `<&zip_xy_transform>` on the `zmk,input-listener` node. | Handled via `swap-xy;` and `invert-x;` directly on the sensor node. |

If you ever see a Kconfig error like `undefined symbol INPUT_PMW3610_INIT_PRIORITY`, it is because the specific Kconfig does not exist in the driver currently active in `west.yml`.
