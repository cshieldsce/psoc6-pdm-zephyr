/*
 * Copyright (c) 2026 Charlie Shields
 * SPDX-License-Identifier: Apache-2.0
 *
 * psoc6_pdm.h : PSoC6 PDM-PCM driver interface
 *
 * Hardware: CY8CKIT-062S2-AI (CY8C624ABZI-S2D44)
 *   PDM_CLK  = P10.4  (HSIOM = P10_4_AUDIOSS0_PDM_CLK = 21)
 *   PDM_DATA = P10.5  (HSIOM = P10_5_AUDIOSS0_PDM_DATA = 21)
 *   Mic 1 SELECT = LOW  -> left channel  (CLK falling edge)
 *   Mic 2 SELECT = HIGH -> right channel (CLK rising edge)
 *
 * Sample rate (mono LEFT, confirmed):
 *   HFCLK1 (100 MHz) -> clkDiv /4 -> mclkDiv /4 -> ckoDiv /4
 *   -> bit-clock 1.5625 MHz -> sincDecRate 98 -> ~15,954 Hz PCM
 *
 * Each 32-bit FIFO word = one 16-bit left-channel sample in bits[15:0].
 * See HARDWARE.md for full details.
 */

#ifndef PSOC6_PDM_H
#define PSOC6_PDM_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Initialise PDM0 peripheral and configure GPIO pins.
 *
 * Prints a raw FIFO diagnostic after init. Interpret the log:
 *   L= all -1  -> DATA line floating HIGH (check P10.5 trace)
 *   L= all  0  -> DATA line floating LOW  (check HSIOM mux)
 *   L= random  -> mic is producing a valid bitstream  [OK]
 *
 * @return 0 on success, -1 on failure.
 */
int psoc6_pdm_init(void);

/**
 * @brief Read PCM samples from the PDM FIFO (polling, blocking).
 *
 * Mono LEFT mode. Reads @p n 16-bit signed samples from Mic 1.
 * Blocks until all @p n samples are collected.
 *
 * @param buf   Destination buffer (must hold @p n int16_t values).
 * @param n     Number of samples to read.
 * @return 0 on success, -1 on FIFO overflow.
 */
int psoc6_pdm_read(int16_t *buf, size_t n);

#endif /* PSOC6_PDM_H */