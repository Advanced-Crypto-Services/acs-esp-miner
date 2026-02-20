/* SPDX-License-Identifier: GPL-2.0 */
#ifndef SELF_TEST_H_
#define SELF_TEST_H_

#include "global_state.h"

/**
 * self_test - main entry point for self-test mode
 * @pvParameters: pointer to GlobalState
 *
 * Initialises WiFi and HTTP server, then runs the full
 * hardware test sequence.  Called from app_main() when
 * self-test conditions are met.  Does not return while
 * the server is active.
 *
 * Context: called from app_main task context.
 */
void self_test(void *pvParameters);

/**
 * should_test - determine whether self-test mode should run
 * @GLOBAL_STATE: pointer to GlobalState
 *
 * Checks NVS configuration and device type.
 *
 * Return: true if self-test should be entered.
 */
bool should_test(GlobalState *GLOBAL_STATE);

/**
 * run_test_sequence - execute the two-phase hardware test sequence
 * @GLOBAL_STATE: pointer to GlobalState
 *
 * Phase 1 (init -- runs once): PSRAM, peripherals, voltage
 * regulator, core voltage, temperature.  Results are preserved
 * across re-runs once the init phase passes.
 *
 * Phase 2 (ASIC -- every call): powers on the ASIC (without
 * reinitialising the VR), detects chips, and measures hashrate.
 *
 * On subsequent calls after the first successful init phase,
 * only phase 2 is executed.  Always powers off the ASIC on
 * exit regardless of pass/fail.
 *
 * Context: must be called from a task context.
 */
void run_test_sequence(GlobalState *GLOBAL_STATE);

#endif
