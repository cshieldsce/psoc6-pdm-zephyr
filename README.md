# psoc6-pdm-zephyr

PDM microphone driver for the **Infineon CY8CKIT-062S2-AI** under **Zephyr RTOS**. No ModusToolbox, no proprietary IDE, no cyhal dependency.

Zephyr 4.x ships no upstream AUDIOSS/PDM driver for PSoC6. This fills that gap using the Infineon PDL directly from the standard `hal_infineon` west module.

---

## Prerequisites

- [Zephyr SDK 0.17.0+](https://github.com/zephyrproject-rtos/sdk-ng/releases)
- `west` + Python 3.12+

```bash
# One-time workspace setup
west init -l .
west update
```

---

## Build & Flash

```bash
west build -p always -b cy8ckit_062s2_ai/cy8c624abzi_s2d44 app
west flash
```

---

## Verifying it works

Open a serial terminal on the KitProg3 COM port. At boot you'll see a short diagnostic:

```
PDM: HSIOM readback - P10.4=21 (expect 21)  P10.5=21 (expect 21)
PDM: === RAW FIFO DIAGNOSTIC (8 words) ===
PDM DIAG[0]: 0xXXXXXXXX  L=XXXX
...
```

| `L=` values | Meaning |
|---|---|
| All `-1` | DATA line floating high — see [HARDWARE.md](HARDWARE.md) |
| All `0` | DATA line floating low — see [HARDWARE.md](HARDWARE.md) |
| **Mix of +/− values** | **Working correctly** |

After that, each 100 ms frame logs min/max/avg and an energy value. Clap or blow on the board and the energy should spike by several orders of magnitude and drop back to the noise floor immediately after.

---

## Hardware

- **Board:** CY8CKIT-062S2-AI (PSoC 62 — CY8C624ABZI-S2D44)
- **Microphones:** 2× Infineon IM72D128V01XTMA1 (PDM MEMS)
- **Output:** Mono left channel, ~16 kHz, 16-bit signed PCM

See [HARDWARE.md](HARDWARE.md) for pin map, clock chain, and a full list of pitfalls encountered during development.

---

## License

Apache 2.0
