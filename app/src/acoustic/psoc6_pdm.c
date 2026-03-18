/*
 * Copyright (c) 2026 Charlie Shields
 * SPDX-License-Identifier: Apache-2.0
 *
 * psoc6_pdm.c : PSoC6 PDM-PCM driver for Zephyr
 *
 * Drives the AUDIOSS0 PDM peripheral via the Infineon PDL (cy_pdm_pcm).
 * No ModusToolbox or cyhal dependency required.
 *
 * Board: CY8CKIT-062S2-AI (CY8C624ABZI-S2D44)
 * Mics:  2x IM72D128V01XTMA1, mono LEFT channel, ~16 kHz PCM output
 */

#include "psoc6_pdm.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* Infineon PDL headers */
#include "cy_pdm_pcm.h"
#include "cy_sysclk.h"
#include "cy_gpio.h"
#include "cy_device_headers.h"

LOG_MODULE_REGISTER(psoc6_pdm, LOG_LEVEL_DBG);

/*
 * HSIOM peripheral mux value for both PDM pins on Port 10.
 *
 * P10_4_AUDIOSS0_PDM_CLK  = 21  (PDM bit-clock output)
 * P10_5_AUDIOSS0_PDM_DATA = 21  (PDM data input)
 *
 * Verified against gpio_psoc6_02_128_tqfp.h and all other CAT1A device
 * headers in mtb-pdl-cat1. Some docs mention 27 (0x1B) for other PSoC6
 * variants -- ignore those, 21 is correct for CY8C624ABZI-S2D44.
 */
#define PDM_HSIOM_SEL   21U

/*
 * PDM clock divider chain and resulting sample rate:
 *
 *   Source:   HFCLK1 = 100 MHz  (FLL, Path 0, set in board overlay)
 *   clkDiv:   /4  ->  25 MHz    (first stage, sets PDM core clock)
 *   mclkDiv:  /4  ->   6.25 MHz (second stage, MCLKQ)
 *   ckoDiv:   /4  ->   1.5625 MHz (bit-clock to mics; IM72D128 range: 1.0-3.072 MHz)
 *
 *   Mono LEFT sample rate formula:
 *     Fs = HFCLK1 / (clkDiv * mclkDiv * (ckoDiv+1) * 2 * sincDecRate)
 *        = 100,000,000 / (4 * 4 * 4 * 2 * 98)
 *        = 15,954 Hz  (~16 kHz)
 *
 *   ckoDiv register value is (desired_divisor - 1), so /4 = write 3.
 *   sincDecRate tuning: lower value = higher Fs, higher value = lower Fs.
 *   98 was chosen to hit ~16 kHz to match typical TinyML model input.
 */
static const cy_stc_pdm_pcm_config_t pdm_config = {
    .clkDiv             = CY_PDM_PCM_CLK_DIV_1_4,   /* /4: 100 MHz -> 25 MHz */
    .mclkDiv            = CY_PDM_PCM_CLK_DIV_1_4,   /* /4: 25 MHz -> 6.25 MHz */
    .ckoDiv             = 3U,    /* register value = divisor - 1, so /4: 6.25 MHz -> 1.5625 MHz */
    .sincDecRate        = 98U,   /* decimation rate: yields ~15,954 Hz PCM output */

    /* Mono left: only Mic 1 (SELECT pin grounded) is captured.            */
    /* Mic 2 (SELECT pin high) is physically on the right channel but      */
    /* ignored here. Switch to CY_PDM_PCM_OUT_STEREO to use both mics.    */
    .chanSelect         = CY_PDM_PCM_OUT_CHAN_LEFT,

    .chanSwapEnable     = false, /* left/right channels not swapped */

    /* High-pass filter removes DC offset introduced by the PDM decimation */
    /* filter startup transient. Gain=8 sets the HPF pole at Fs / 2^8,    */
    /* which is ~62 Hz at 16 kHz -- attenuates DC within a few frames.    */
    .highPassDisable    = false,
    .highPassFilterGain = 8U,    /* HPF pole = Fs / 2^8 = ~62 Hz */

    /* Gain: +4.5 dB applied to the left (and right, though unused) channel. */
    /* Boost if the mic signal is too quiet; reduce if clipping is observed. */
    .gainLeft           = CY_PDM_PCM_GAIN_4_5_DB,
    .gainRight          = CY_PDM_PCM_GAIN_4_5_DB,   /* unused in mono, kept symmetric */

    /* Soft mute is disabled. These fields are required by the PDL struct   */
    /* but have no effect when softMuteEnable is false.                     */
    .softMuteEnable     = false,
    .softMuteFineGain   = 1U,    /* 0 = 0.13 dB step, 1 = 0.26 dB step (unused) */
    .softMuteCycles     = CY_PDM_PCM_SOFT_MUTE_CYCLES_64, /* unused */

    /* ckoDelay adjusts the phase of the internal sampler relative to the  */
    /* PDM clock. 3 = no delay (datasheet default for normal operation).   */
    .ckoDelay           = 3U,

    .wordLen            = CY_PDM_PCM_WLEN_16_BIT,   /* 16-bit signed PCM samples */
    .signExtension      = true,  /* sign-extend 16-bit value into 32-bit FIFO word */

    /* FIFO trigger level: interrupt/DMA fires when FIFO depth >= this value. */
    /* We use polling so this only affects overflow detection timing.         */
    /* 32 is well below the 254-word mono limit, giving enough headroom.     */
    .rxFifoTriggerLevel = 32U,

    .dmaTriggerEnable   = false, /* polling mode, no DMA */
    .interruptMask      = 0U     /* all interrupts disabled; overflow checked by polling */
};

int psoc6_pdm_init(void)
{
    LOG_INF("PDM: init starting...");

    /* ------------------------------------------------------------------
     * 1. Configure HFCLK1 as the PDM clock source.
     *
     *    The PDM peripheral (AUDIOSS0) is clocked from HFCLK1, not HFCLK0.
     *    HFCLK0 drives the CPU. We set HFCLK1 to Path 0 (FLL, 100 MHz)
     *    with no additional divider.
     *
     *    The board overlay (&clk_hf1) also enables this -- the calls below
     *    are belt-and-suspenders to guarantee the state at driver init time.
     * ------------------------------------------------------------------ */
    Cy_SysClk_ClkHfSetSource(1U, CY_SYSCLK_CLKHF_IN_CLKPATH0); /* HFCLK1 from FLL */
    Cy_SysClk_ClkHfSetDivider(1U, CY_SYSCLK_CLKHF_NO_DIVIDE);  /* no extra division */
    Cy_SysClk_ClkHfEnable(1U);

    /* Log the SRSS clock root register so the clock path can be verified. */
    uint32_t root1 = SRSS->CLK_ROOT_SELECT[1];
    LOG_INF("PDM: HF1 ROOT1=0x%08x (Path %u, Div %u)",
            root1,
            (unsigned int)(root1 & 0xFU),           /* bits[3:0] = clock path */
            (unsigned int)((root1 >> 16) & 0xFFU) + 1); /* bits[23:16]+1 = divider */

    /* ------------------------------------------------------------------
     * 2. Enable the AUDIOSS peripheral group clock gate.
     *
     *    PERI->GR[9] is the peripheral group that contains AUDIOSS (PDM, I2S)
     *    on PSoC6 CAT1A. SL_CTL bit 0 = slave clock enable for that group.
     *    Without this the PDM registers are inaccessible.
     * ------------------------------------------------------------------ */
    PERI->GR[9].SL_CTL |= 1U; /* bit 0: enable AUDIOSS group clock */

    /* ------------------------------------------------------------------
     * 3. Configure GPIO pins for PDM via PDL.
     *
     *    P10.4 = PDM_CLK  (output from PSoC to mics)
     *    P10.5 = PDM_DATA (input  to  PSoC from mics)
     *
     *    DATA pin uses PULLDOWN, not HIGHZ. With HIGHZ, a floating DATA
     *    line reads as all-ones (0xFFFF = -1), which makes the HPF log
     *    look like a settling transient but is actually dead silence.
     *    PULLDOWN keeps the line defined and makes this failure mode
     *    immediately visible as L=0 in the diagnostic, not L=-1.
     * ------------------------------------------------------------------ */
    Cy_GPIO_Pin_Init(GPIO_PRT10, 4U, &(cy_stc_gpio_pin_config_t){
        .outVal    = 0,
        .driveMode = CY_GPIO_DM_STRONG_IN_OFF, /* push-pull output, input buffer off */
        .hsiom     = PDM_HSIOM_SEL,            /* connect pin to AUDIOSS0 PDM clock */
        .vtrip     = CY_GPIO_VTRIP_CMOS,
        .slewRate  = CY_GPIO_SLEW_FAST,        /* fast edge for 1.5 MHz clock signal */
    });

    Cy_GPIO_Pin_Init(GPIO_PRT10, 5U, &(cy_stc_gpio_pin_config_t){
        .outVal    = 0,
        .driveMode = CY_GPIO_DM_PULLDOWN,      /* input with weak pull-down (see note above) */
        .hsiom     = PDM_HSIOM_SEL,            /* connect pin to AUDIOSS0 PDM data */
        .vtrip     = CY_GPIO_VTRIP_CMOS,
        .slewRate  = CY_GPIO_SLEW_FAST,
    });

    /* ------------------------------------------------------------------
     * 4. Read back HSIOM to confirm the peripheral mux took effect.
     *
     *    On CAT1A silicon, HSIOM is a separate peripheral block -- it is
     *    NOT a member of GPIO_PRT_V2_Type. Use Cy_GPIO_GetHSIOM() which
     *    reads through the correct register path.
     * ------------------------------------------------------------------ */
    en_hsiom_sel_t hsiom_p4 = Cy_GPIO_GetHSIOM(GPIO_PRT10, 4U);
    en_hsiom_sel_t hsiom_p5 = Cy_GPIO_GetHSIOM(GPIO_PRT10, 5U);
    LOG_INF("PDM: HSIOM readback - P10.4=%u (expect %u)  P10.5=%u (expect %u)",
            (unsigned)hsiom_p4, PDM_HSIOM_SEL,
            (unsigned)hsiom_p5, PDM_HSIOM_SEL);
    if ((unsigned)hsiom_p4 != PDM_HSIOM_SEL || (unsigned)hsiom_p5 != PDM_HSIOM_SEL) {
        LOG_WRN("PDM: HSIOM mismatch -- pinmux may have been overridden by another driver.");
    }

    /* ------------------------------------------------------------------
     * 5. Initialise the PDM peripheral via PDL.
     *
     *    DeInit first to reset any leftover state from a previous run
     *    (e.g. after a soft reset without a full power cycle).
     * ------------------------------------------------------------------ */
    Cy_PDM_PCM_DeInit(PDM0);
    cy_en_pdm_pcm_status_t status = Cy_PDM_PCM_Init(PDM0, &pdm_config);
    if (status != CY_PDM_PCM_SUCCESS) {
        LOG_ERR("PDM: PDL init failed (status=0x%02x)", (int)status);
        return -1;
    }

    /* ------------------------------------------------------------------
     * 6. Start streaming and wait for the FIFO to fill.
     *
     *    After Enable(), the PDM hardware immediately starts clocking the
     *    microphones and pushing decimated samples into the RX FIFO.
     *    We poll for up to 100 ms (100 x 1 ms sleep) to confirm data is
     *    arriving, then wait a further 200 ms for the HPF to settle before
     *    sampling the FIFO for the diagnostic below.
     * ------------------------------------------------------------------ */
    Cy_PDM_PCM_Enable(PDM0);
    LOG_INF("PDM: streaming enabled, warming up...");

    for (int i = 0; i < 100; i++) {
        k_msleep(1); /* poll every 1 ms, timeout after 100 ms */
        if (Cy_PDM_PCM_GetNumInFifo(PDM0) > 0U) {
            LOG_INF("PDM: streaming! (FIFO has %u samples)",
                    (unsigned int)Cy_PDM_PCM_GetNumInFifo(PDM0));
            k_msleep(200); /* wait for HPF to drain its startup transient */
            break;
        }
    }

    if (Cy_PDM_PCM_GetNumInFifo(PDM0) == 0U) {
        LOG_ERR("PDM: no data after 100 ms -- check HFCLK1 and pin connections");
        return -1;
    }

    /* ------------------------------------------------------------------
     * 7. Raw FIFO diagnostic.
     *
     *    Read 8 words and log the raw hex and the left-channel int16 value.
     *    This makes it immediately obvious whether real audio is arriving:
     *
     *      L= all -1  (0xFFFF) -> DATA line floating high (check P10.5)
     *      L= all  0  (0x0000) -> DATA line floating low  (check HSIOM mux)
     *      L= random           -> mic is producing a valid bitstream  [OK]
     *
     *    ReadFifoSilent() peeks without advancing the read pointer.
     *    We interleave ReadFifo() calls to step through different words.
     * ------------------------------------------------------------------ */
    LOG_INF("PDM: === RAW FIFO DIAGNOSTIC (8 words) ===");
    for (int d = 0; d < 8; d++) {
        uint32_t raw = Cy_PDM_PCM_ReadFifoSilent(PDM0); /* peek at next word */
        int16_t  lch = (int16_t)(raw & 0xFFFFU);        /* left channel = bits[15:0] */
        LOG_INF("PDM DIAG[%d]: 0x%08X  L=%6d", d, raw, (int)lch);
        if (d < 7) {
            (void)Cy_PDM_PCM_ReadFifo(PDM0); /* advance pointer for next peek */
        }
    }
    LOG_INF("PDM: === END DIAGNOSTIC ===");
    LOG_INF("PDM: HINT: if all L= -1, DATA line is floating HIGH (check P10.5 trace)");
    LOG_INF("PDM: HINT: if all L=  0, DATA line is floating LOW  (check HSIOM mux)");
    LOG_INF("PDM: HINT: random L= values = mic producing valid bitstream  [OK]");

    /* Flush the FIFO so the capture loop starts from a clean state */
    Cy_PDM_PCM_ClearFifo(PDM0);

    return 0;
}

int psoc6_pdm_read(int16_t *buf, size_t n)
{
    size_t collected = 0;

    while (collected < n) {
        /* Check for FIFO overflow before reading.
         * Overflow means the application is not draining the FIFO fast
         * enough and samples have been lost. Signal the caller to discard
         * this frame and try again. */
        if (Cy_PDM_PCM_GetInterruptStatus(PDM0) & CY_PDM_PCM_INTR_RX_OVERFLOW) {
            Cy_PDM_PCM_ClearInterrupt(PDM0, CY_PDM_PCM_INTR_RX_OVERFLOW);
            Cy_PDM_PCM_ClearFifo(PDM0);
            return -1; /* caller should retry the frame */
        }

        uint32_t used = Cy_PDM_PCM_GetNumInFifo(PDM0);
        if (used == 0U) {
            /* FIFO not yet full enough -- yield to other threads and retry */
            k_yield();
            continue;
        }

        /* Mono LEFT mode: each 32-bit FIFO word contains one 16-bit
         * left-channel sample in bits[15:0]. Bits[31:16] are zero (or
         * residual noise if the peripheral briefly ran in stereo). We
         * mask to 16 bits and cast to signed before storing.            */
        while (used-- > 0U && collected < n) {
            uint32_t raw = Cy_PDM_PCM_ReadFifo(PDM0);
            buf[collected++] = (int16_t)(raw & 0xFFFFU); /* extract left channel */
        }
    }

    return 0;
}
