/*
 * Copyright (c) 2026 Charlie Shields
 * SPDX-License-Identifier: Apache-2.0
 *
 * acoustic.c : Audio capture layer wrapping the PSoC6 PDM driver.
 * Provides a clean frame-based API for the application layer.
 */

#include "acoustic.h"
#include "psoc6_pdm.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(acoustic, LOG_LEVEL_DBG);

int acoustic_init(void)
{
    LOG_INF("Acoustic: initialising mic array");
    LOG_INF("  Mics  : 2x IM72D128V01XTMA1 (PDM)");
    LOG_INF("  Rate  : ~16 kHz  Frame: %d ms = %d samples",
            ACOUSTIC_FRAME_MS, ACOUSTIC_FRAME_SAMPLES);
    return psoc6_pdm_init();
}

int acoustic_capture(int16_t *buf)
{
    return psoc6_pdm_read(buf, ACOUSTIC_FRAME_SAMPLES);
}

uint32_t acoustic_energy(const int16_t *buf, uint32_t n)
{
    int64_t sum = 0;
    int16_t min_val = 32767;
    int16_t max_val = -32768;

    for (uint32_t i = 0; i < n; i++) {
        int16_t s = buf[i];
        sum += s;
        if (s < min_val) min_val = s;
        if (s > max_val) max_val = s;
    }

    int32_t avg = (int32_t)(sum / n);
    LOG_INF("  -> min: %6d, max: %6d, avg: %6d", min_val, max_val, avg);

    /* AC energy: subtract mean before squaring to remove DC bias */
    uint64_t energy_sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        int32_t s = (int32_t)buf[i] - avg;
        energy_sum += (uint64_t)(s * s);
    }

    return (uint32_t)(energy_sum / n);
}