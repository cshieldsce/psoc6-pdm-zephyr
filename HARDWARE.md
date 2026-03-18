# Hardware Reference

Technical details for the CY8CKIT-062S2-AI PDM driver.

---

## Pin Map

| Signal | Pin | HSIOM | Direction |
|---|---|---|---|
| PDM_CLK | P10.4 | `P10_4_AUDIOSS0_PDM_CLK` = **21** | Output |
| PDM_DATA | P10.5 | `P10_5_AUDIOSS0_PDM_DATA` = **21** | Input |
| Mic 1 SELECT | GND | - | Left channel (CLK falling edge) |
| Mic 2 SELECT | VCC | - | Right channel (CLK rising edge) |

HSIOM = 21 verified across all PSoC6 CAT1A device headers in `mtb-pdl-cat1`. Some older documentation references 27 (0x1B) for other PSoC6 variants - ignore it for this device.

---

## Clock Chain

```
FLL (Path 0) = 100 MHz
  └── HFCLK1 (no divide) = 100 MHz    <- enabled in board overlay
        └── clkDiv  /4 = 25 MHz
              └── mclkDiv /4 = 6.25 MHz
                    └── ckoDiv  /4 = 1.5625 MHz  (bit-clock to mics)
                          └── sincDecRate 98 = ~15,954 Hz PCM output
```

IM72D128 requires a PDM clock between **1.0 – 3.072 MHz**. The 1.5625 MHz bit-clock is within spec.

The Zephyr board DTS does not enable HFCLK1 by default. The `boards/cy8ckit_062s2_ai.overlay` in this repo adds it:

```dts
&clk_hf1 {
    status = "okay";
    clock-div = <1>;
    clocks = <&fll0>;
};
```

---

## Pitfalls

**1. DATA pin must use `PULLDOWN`, not `HIGHZ`**

`CY_GPIO_DM_HIGHZ` leaves P10.5 undefined when no PDM bitstream is arriving. The PSoC6 PDM block reads the floating line as all-ones (0xFFFF = -1 in int16). The on-chip high-pass filter then produces an exponentially-decaying response that looks exactly like a mic settling after a loud event - but is actually dead silence. `CY_GPIO_DM_PULLDOWN` keeps the line at a well-defined 0V and makes this failure mode immediately obvious (`L=0` in the diagnostic).

**2. Interpreting the HPF decay pattern**

If your log shows energy decaying from ~100 million to a constant floor of 2 with `max` always equal to `-1`, the DATA line is the problem - not the HPF gain, not the clock dividers, not the sample rate. The floor value is the IIR filter's DC pole response to a constant input.

**3. HSIOM is a separate peripheral - not a member of `GPIO_PRT_V2_Type`**

On CAT1A silicon, HSIOM register access goes through its own peripheral block. `GPIO_PRT10->ATT[]` does not exist - that will fail to compile. Use `Cy_GPIO_GetHSIOM(GPIO_PRT10, pin)` from the PDL.

**4. Stereo FIFO layout in mono mode**

When `chanSelect = CY_PDM_PCM_OUT_CHAN_LEFT`, each 32-bit FIFO word contains only the left-channel sample in `bits[15:0]`. Do not split it as two 16-bit stereo samples - `bits[31:16]` are not valid.

**5. `REG_EN` on P5.1 conflicts with UART TX**

P5.1 is used by SCB5 as the UART TX line (console output). It also appears in some board documentation as a sensor-rail enable (`REG_EN`). Do not attempt to toggle it - it will corrupt serial output or cause a fault. The PDM mic VDD on this board is always-on and requires no GPIO enable.

**6. Mic SELECT pins must be hard-wired**

Mic 1 SELECT must be tied to GND before power-on. Mic 2 SELECT must be tied to VCC. If SELECT is floating or wrong, the mic outputs on the wrong clock edge for the configured channel and the FIFO appears empty even though the hardware is running.

---

## PDL Integration in Zephyr

The Infineon PDL (`mtb-pdl-cat1`) ships as part of the standard Zephyr `hal_infineon` west module - no extra configuration needed. The `CMakeLists.txt` pulls in the one required source file directly:

```cmake
set(PDL_SRC $ENV{ZEPHYR_BASE}/../modules/hal/infineon/mtb-pdl-cat1/drivers/source)
target_sources(app PRIVATE ${PDL_SRC}/cy_pdm_pcm.c)
```

`cyhal_pdmpcm` (also in `hal_infineon`) is a higher-level alternative that handles clock and pin configuration automatically. It is an architectural upgrade path if you want to remove the direct PDL dependency, but the current PDL-based driver is fully functional and stable.

---

## Zephyr Upstream Status

As of Zephyr 4.3, there is no upstream `CONFIG_AUDIO_DMIC` driver for PSoC6. Infineon has not contributed one. This repo exists to fill that gap until an upstream driver lands.
