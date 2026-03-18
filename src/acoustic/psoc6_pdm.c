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

#include "cy_pdm_pcm.h"
#include "cy_sysclk.h"
#include "cy_gpio.h"
#include "cy_device_headers.h"

LOG_MODULE_REGISTER(psoc6_pdm, LOG_LEVEL_DBG);

/*
 * HSIOM mux value for PDM pins on Port 10.
 * P10_4_AUDIOSS0_PDM_CLK = P10_5_AUDIOSS0_PDM_DATA = 21
 * Verified across all PSoC6 CAT1A device headers in mtb-pdl-cat1.
 */
#define PDM_HSIOM_SEL   21U

/*
 * Clock chain: HFCLK1 (100 MHz) -> clkDiv /4 -> mclkDiv /4 -> ckoDiv /4
 *   -> bit-clock 1.5625 MHz -> sincDecRate 98 -> ~15,954 Hz PCM
 *
 * ckoDiv register = divisor - 1, so /4 = write 3.
 * sincDecRate 98 targets ~16 kHz; adjust to change sample rate.
 */
static const cy_stc_pdm_pcm_config_t pdm_config = {
    .clkDiv             = CY_PDM_PCM_CLK_DIV_1_4,
    .mclkDiv            = CY_PDM_PCM_CLK_DIV_1_4,
    .ckoDiv             = 3U,    /* /4: 6.25 MHz -> 1.5625 MHz bit-clock */
    .sincDecRate        = 98U,   /* decimation rate -> ~15,954 Hz PCM */
    .chanSelect         = CY_PDM_PCM_OUT_CHAN_LEFT,  /* Mic 1 only */
    .chanSwapEnable     = false,
    .highPassDisable    = false,
    .highPassFilterGain = 8U,    /* HPF pole at Fs/2^8 = ~62 Hz, removes DC */
    .gainLeft           = CY_PDM_PCM_GAIN_4_5_DB,
    .gainRight          = CY_PDM_PCM_GAIN_4_5_DB,   /* unused in mono */
    .softMuteEnable     = false,
    .softMuteFineGain   = 1U,
    .softMuteCycles     = CY_PDM_PCM_SOFT_MUTE_CYCLES_64,
    .ckoDelay           = 3U,    /* sampler phase delay, 3 = default/no delay */
    .wordLen            = CY_PDM_PCM_WLEN_16_BIT,
    .signExtension      = true,
    .rxFifoTriggerLevel = 32U,   /* polling mode, well below 254-word limit */
    .dmaTriggerEnable   = false,
    .interruptMask      = 0U
};

int psoc6_pdm_init(void)
{
    LOG_INF("PDM: init...");

    /* HFCLK1 -> PDM peripheral clock source (100 MHz, FLL Path 0).
     * The board overlay also sets this; these calls ensure correct state
     * at driver init regardless of boot order. */
    Cy_SysClk_ClkHfSetSource(1U, CY_SYSCLK_CLKHF_IN_CLKPATH0);
    Cy_SysClk_ClkHfSetDivider(1U, CY_SYSCLK_CLKHF_NO_DIVIDE);
    Cy_SysClk_ClkHfEnable(1U);

    uint32_t root1 = SRSS->CLK_ROOT_SELECT[1];
    LOG_INF("PDM: HF1 ROOT1=0x%08x (Path %u, Div %u)",
            root1,
            (unsigned int)(root1 & 0xFU),
            (unsigned int)((root1 >> 16) & 0xFFU) + 1);

    /* Enable AUDIOSS peripheral group clock gate.
     * PERI->GR[9] = AUDIOSS group (PDM, I2S) on PSoC6 CAT1A.
     * Without this, PDM registers are inaccessible. */
    PERI->GR[9].SL_CTL |= 1U;

    /* P10.4 = PDM_CLK output, P10.5 = PDM_DATA input.
     * DATA uses PULLDOWN (not HIGHZ) -- see HARDWARE.md pitfall #1. */
    Cy_GPIO_Pin_Init(GPIO_PRT10, 4U, &(cy_stc_gpio_pin_config_t){
        .outVal    = 0,
        .driveMode = CY_GPIO_DM_STRONG_IN_OFF,
        .hsiom     = PDM_HSIOM_SEL,
        .vtrip     = CY_GPIO_VTRIP_CMOS,
        .slewRate  = CY_GPIO_SLEW_FAST,
    });
    Cy_GPIO_Pin_Init(GPIO_PRT10, 5U, &(cy_stc_gpio_pin_config_t){
        .outVal    = 0,
        .driveMode = CY_GPIO_DM_PULLDOWN,
        .hsiom     = PDM_HSIOM_SEL,
        .vtrip     = CY_GPIO_VTRIP_CMOS,
        .slewRate  = CY_GPIO_SLEW_FAST,
    });

    /* Confirm pinmux took effect. HSIOM is a separate peripheral on CAT1A;
     * use Cy_GPIO_GetHSIOM(), not GPIO_PRT10->ATT[] (that member does not exist). */
    en_hsiom_sel_t h4 = Cy_GPIO_GetHSIOM(GPIO_PRT10, 4U);
    en_hsiom_sel_t h5 = Cy_GPIO_GetHSIOM(GPIO_PRT10, 5U);
    LOG_INF("PDM: HSIOM readback - P10.4=%u (expect %u)  P10.5=%u (expect %u)",
            (unsigned)h4, PDM_HSIOM_SEL, (unsigned)h5, PDM_HSIOM_SEL);
    if ((unsigned)h4 != PDM_HSIOM_SEL || (unsigned)h5 != PDM_HSIOM_SEL) {
        LOG_WRN("PDM: HSIOM mismatch -- pinmux may have been overridden.");
    }

    Cy_PDM_PCM_DeInit(PDM0);
    cy_en_pdm_pcm_status_t status = Cy_PDM_PCM_Init(PDM0, &pdm_config);
    if (status != CY_PDM_PCM_SUCCESS) {
        LOG_ERR("PDM: init failed (status=0x%02x)", (int)status);
        return -1;
    }

    Cy_PDM_PCM_Enable(PDM0);
    LOG_INF("PDM: streaming, warming up...");

    /* Poll up to 100 ms for FIFO data, then let HPF settle for 200 ms. */
    for (int i = 0; i < 100; i++) {
        k_msleep(1);
        if (Cy_PDM_PCM_GetNumInFifo(PDM0) > 0U) {
            LOG_INF("PDM: streaming! (FIFO has %u samples)",
                    (unsigned int)Cy_PDM_PCM_GetNumInFifo(PDM0));
            k_msleep(200);
            break;
        }
    }

    if (Cy_PDM_PCM_GetNumInFifo(PDM0) == 0U) {
        LOG_ERR("PDM: no data after 100 ms -- check HFCLK1 and pin connections");
        return -1;
    }

    /* Raw FIFO diagnostic: log 8 words to check DATA line health.
     *   L= all -1  -> DATA floating high (check P10.5 and mic connections)
     *   L= all  0  -> DATA floating low  (check HSIOM mux)
     *   L= random  -> mic is producing a valid bitstream  [OK]           */
    LOG_INF("PDM: === RAW FIFO DIAGNOSTIC (8 words) ===");
    for (int d = 0; d < 8; d++) {
        uint32_t raw = Cy_PDM_PCM_ReadFifoSilent(PDM0);
        int16_t  lch = (int16_t)(raw & 0xFFFFU); /* left channel = bits[15:0] */
        LOG_INF("PDM DIAG[%d]: 0x%08X  L=%6d", d, raw, (int)lch);
        if (d < 7) {
            (void)Cy_PDM_PCM_ReadFifo(PDM0);
        }
    }
    LOG_INF("PDM: === END DIAGNOSTIC ===");
    LOG_INF("PDM: HINT: L=-1 all = DATA floating HIGH  |  L=0 all = floating LOW  |  random = OK");

    Cy_PDM_PCM_ClearFifo(PDM0);
    return 0;
}

int psoc6_pdm_read(int16_t *buf, size_t n)
{
    size_t collected = 0;

    while (collected < n) {
        /* FIFO overflow = samples lost; caller should discard this frame */
        if (Cy_PDM_PCM_GetInterruptStatus(PDM0) & CY_PDM_PCM_INTR_RX_OVERFLOW) {
            Cy_PDM_PCM_ClearInterrupt(PDM0, CY_PDM_PCM_INTR_RX_OVERFLOW);
            Cy_PDM_PCM_ClearFifo(PDM0);
            return -1;
        }

        uint32_t used = Cy_PDM_PCM_GetNumInFifo(PDM0);
        if (used == 0U) {
            k_yield();
            continue;
        }

        /* Mono LEFT: each 32-bit FIFO word = one signed 16-bit sample in bits[15:0] */
        while (used-- > 0U && collected < n) {
            buf[collected++] = (int16_t)(Cy_PDM_PCM_ReadFifo(PDM0) & 0xFFFFU);
        }
    }

    return 0;
}
