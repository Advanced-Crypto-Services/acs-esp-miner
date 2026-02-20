/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

#include "esp_err.h"
#include "global_state.h"

/**
 * self_test_server_start - start the self-test HTTP/WS server
 * @global_state: pointer to the system-wide GlobalState
 *
 * Registers HTTP routes and WebSocket endpoint on port 80.
 * Must be called after wifi_init() from the connect component.
 *
 * Context: task context only.
 * Return: ESP_OK on success, error code on failure.
 */
esp_err_t self_test_server_start(GlobalState *global_state);

/**
 * self_test_server_notify_status - push a test status update via WS
 * @test_name: short name of the test (e.g. "PSRAM", "VCORE")
 * @state:     one of "running", "pass", "fail"
 * @detail:    human-readable detail string
 *
 * Sends a JSON message to the connected WebSocket client.
 * Safe to call when no client is connected (silently drops).
 *
 * Context: any task context.
 */
void self_test_server_notify_status(const char *test_name,
				    const char *state,
				    const char *detail);

/**
 * self_test_server_notify_metrics - push live metric values via WS
 * @hashrate:     current hashrate in GH/s
 * @temperature:  ASIC temperature in celsius
 * @core_voltage: core voltage in mV
 *
 * Context: any task context.
 */
void self_test_server_notify_metrics(float hashrate,
				     float temperature,
				     uint16_t core_voltage);

/**
 * self_test_server_notify_done - push test-complete message via WS
 * @passed: true if all tests passed
 *
 * Context: any task context.
 */
void self_test_server_notify_done(bool passed);
