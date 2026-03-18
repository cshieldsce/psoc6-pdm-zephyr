/*
 * Copyright (c) 2026 Charlie Shields
 * SPDX-License-Identifier: Apache-2.0
 *
 * acoustic.h : Frame-based PDM microphone capture API.
 *
 * Hardware: 2x Infineon IM72D128V01XTMA1 on shared PDM bus
 *   PDM_CLK  -> P10.4  (HSIOM = 21)
 *   PDM_DATA -> P10.5  (HSIOM = 21)
 *   Mic 1 SELECT = LOW  -> left channel  (CLK falling edge)
 *   Mic 2 SELECT = HIGH -> right channel (CLK rising edge)
 *
 * Driver runs in mono LEFT mode (Mic 1 only).
 * See HARDWARE.md for full pin map and clock chain.
 */

#ifndef ACOUSTIC_H
#define ACOUSTIC_H

#include <stdint.h>
#include <stdbool.h>

/* Audio config - must match model input if using TinyML inference */
#define ACOUSTIC_SAMPLE_RATE_HZ     16000
#define ACOUSTIC_FRAME_MS           100         /* 100 ms frame = 1600 samples */
#define ACOUSTIC_FRAME_SAMPLES      (ACOUSTIC_SAMPLE_RATE_HZ / (1000 / ACOUSTIC_FRAME_MS))

/* Broadband energy gate threshold - tune for your environment */
#define ACOUSTIC_ENERGY_THRESHOLD   1000000UL

/**
 * @brief Initialise the PDM peripheral and mic array.
 * @return 0 on success, negative errno on failure.
 */
int acoustic_init(void);

/**
 * @brief Capture one audio frame (blocking, ~100 ms).
 * @param buf Output buffer - must hold ACOUSTIC_FRAME_SAMPLES int16_t values.
 * @return 0 on success, negative errno on failure.
 */
int acoustic_capture(int16_t *buf);

/**
 * @brief Compute broadband RMS energy of a frame.
 *
 * Mean-subtracts the frame before squaring to remove DC bias from the
 * on-chip high-pass filter's settling transient.
 *
 * @param buf   Audio samples.
 * @param n     Number of samples.
 * @return      Energy value (mean of squared AC samples).
 */
uint32_t acoustic_energy(const int16_t *buf, uint32_t n);

#endif /* ACOUSTIC_H */