/*
 * Copyright (c) 2026 Charlie Shields
 * SPDX-License-Identifier: Apache-2.0
 *
 * psoc6-pdm-zephyr
 * main.c : Boot, LED heartbeat, acoustic energy gate
 *
 * Demonstrates PDM microphone capture on the CY8CKIT-062S2-AI board.
 * Logs per-frame energy and triggers when sound exceeds threshold.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/version.h>
#include <zephyr/drivers/gpio.h>

#include "acoustic/acoustic.h"

LOG_MODULE_REGISTER(pdm_example, LOG_LEVEL_DBG);

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* Audio frame buffer - 1600 samples @ 16 kHz = 100 ms */
static int16_t audio_buf[ACOUSTIC_FRAME_SAMPLES];

int main(void)
{
    LOG_INF("========================================");
    LOG_INF("  psoc6-pdm-zephyr");
    LOG_INF("  Zephyr %s", KERNEL_VERSION_STRING);
    LOG_INF("  Board : CY8CKIT-062S2-AI");
    LOG_INF("  Mic   : 2x IM72D128V01XTMA1 (PDM)");
    LOG_INF("========================================");

    /* LED init */
    if (!gpio_is_ready_dt(&led)) {
        LOG_ERR("LED GPIO not ready");
        return -1;
    }
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);

    /* Acoustic init */
    if (acoustic_init() != 0) {
        LOG_ERR("Acoustic init failed - check PDM clock/pinmux + board target");
        while (1) {
            gpio_pin_toggle_dt(&led);
            k_msleep(100);  /* fast blink = error */
        }
    }

    LOG_INF("Entering audio capture loop...");

    uint32_t frame = 0;

    while (1) {
        if (acoustic_capture(audio_buf) == 0) {
            uint32_t energy = acoustic_energy(audio_buf,
                                              ACOUSTIC_FRAME_SAMPLES);

            LOG_INF("Frame %5u | Energy: %8u | %s",
                    frame,
                    energy,
                    energy > ACOUSTIC_ENERGY_THRESHOLD ? "*** TRIGGERED ***"
                                                       : "quiet");

            gpio_pin_toggle_dt(&led);
        }
        frame++;
    }

    return 0;
}
