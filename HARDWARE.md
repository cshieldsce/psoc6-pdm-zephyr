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

HSIOM = 21 verified across all PSoC6 CAT1A device headers in `mtb-pdl-cat1`.

---

## Clock Chain

```
FLL (Path 0) = 100 MHz
  -> HFCLK1 (no divide) = 100 MHz     (enabled in board overlay)
       -> clkDiv  /4 = 25 MHz
            -> mclkDiv /4 = 6.25 MHz
                 -> ckoDiv  /4 = 1.5625 MHz   (bit-clock to mics)
                      -> sincDecRate 98 = ~15,954 Hz PCM
```

IM72D128 requires a PDM clock between **1.0 - 3.072 MHz**. The 1.5625 MHz bit-clock is within spec.

The Zephyr board DTS does not enable HFCLK1 by default. `boards/cy8ckit_062s2_ai.overlay` adds it:

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

With `CY_GPIO_DM_HIGHZ`, a floating DATA line reads as all-ones (0xFFFF = -1). The on-chip HPF then produces a decaying response that looks like a settling signal but is dead silence. `CY_GPIO_DM_PULLDOWN` keeps the line defined and makes the failure immediately obvious (`L=0` in the diagnostic instead of `L=-1`).

**2. The HPF decay is a diagnostic, not a bug**

If your log shows energy decaying from ~100 million to a constant floor with `max` always `-1`, the DATA line is the issue -- not the HPF gain or clock dividers.

**3. HSIOM is not in `GPIO_PRT_V2_Type`**

On CAT1A silicon, HSIOM is a separate peripheral block. `GPIO_PRT10->ATT[]` does not exist and will fail to compile. Use `Cy_GPIO_GetHSIOM(GPIO_PRT10, pin)`.

**4. Stereo FIFO layout in mono mode**

When `chanSelect = CY_PDM_PCM_OUT_CHAN_LEFT`, each 32-bit FIFO word holds one 16-bit sample in `bits[15:0]`. Do not split it as a stereo pair.

---

## PDL Integration in Zephyr

`mtb-pdl-cat1` is part of the standard Zephyr `hal_infineon` west module -- no extra setup needed. `CMakeLists.txt` pulls in the one required source file:

```cmake
target_sources(app PRIVATE ${PDL_SRC}/cy_pdm_pcm.c)
```

---

## Zephyr Upstream Status

As of Zephyr 4.3 there is no upstream `CONFIG_AUDIO_DMIC` driver for PSoC6. This repo fills that gap.
