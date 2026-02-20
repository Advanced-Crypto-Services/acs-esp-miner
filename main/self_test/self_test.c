/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Self Test - Production hardware validation
 *
 * Runs a sequence of hardware diagnostic tests (PSRAM, display,
 * peripherals, voltage regulator, temperature, ASIC detection,
 * hashrate) and reports results via the on-device display and
 * an HTTP/WebSocket server.  Power is always safely disabled
 * at the end of each test run.
 */

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_psram.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "i2c_bitaxe.h"
#include "DS4432U.h"
#include "EMC2101.h"
#include "INA260.h"
#include "adc.h"
#include "global_state.h"
#include "nvs_config.h"
#include "nvs_flash.h"
#include "display.h"
#include "screen.h"
#include "input.h"
#include "vcore.h"
#include "utils.h"
#include "TPS546.h"
#include "serial.h"

#include "connect.h"
#include "self_test.h"
#include "self_test_server.h"

#define GPIO_ASIC_ENABLE CONFIG_GPIO_ASIC_ENABLE

#define TESTS_FAILED 0
#define TESTS_PASSED 1

/* Test Fan Speed */
#define FAN_SPEED_TARGET_MIN 1000

/* Test Core Voltage (mV) */
#define CORE_VOLTAGE_TARGET_MIN 1000
#define CORE_VOLTAGE_TARGET_MAX 1300

/* Test Power Consumption (watts) */
#define POWER_CONSUMPTION_TARGET_SUB_402 12
#define POWER_CONSUMPTION_TARGET_402 5
#define POWER_CONSUMPTION_TARGET_GAMMA 11
#define POWER_CONSUMPTION_MARGIN 3

/* Hashrate targets (GH/s) */
#define HASHRATE_TARGET_GAMMA 900
#define HASHRATE_TARGET_SUPRA 500

static const char *TAG = "SELF_TEST";

static SemaphoreHandle_t BootSemaphore;

static void tests_done(GlobalState *GLOBAL_STATE, bool test_result);

/**
 * record_result - store a test result in the SELF_TEST_MODULE array
 * @state:     pointer to GlobalState
 * @name:      short test name
 * @passed:    whether the test passed
 * @detail:    detail string (voltage, temp, error message, etc.)
 */
static void record_result(GlobalState *state, const char *name,
			  bool passed, const char *detail)
{
	SelfTestModule *m = &state->SELF_TEST_MODULE;

	if (m->result_count >= SELF_TEST_MAX_RESULTS)
		return;

	SelfTestResult *r = &m->results[m->result_count];
	strncpy(r->name, name, sizeof(r->name) - 1);
	r->name[sizeof(r->name) - 1] = '\0';
	r->passed = passed;
	r->completed = true;
	strncpy(r->detail, detail, sizeof(r->detail) - 1);
	r->detail[sizeof(r->detail) - 1] = '\0';
	m->result_count++;

	self_test_server_notify_status(name,
				       passed ? "pass" : "fail",
				       detail);
}

static float Thermal_get_chip_temp(GlobalState *GLOBAL_STATE)
{
	switch (GLOBAL_STATE->device_model) {
	case DEVICE_MAX:
	case DEVICE_ULTRA:
	case DEVICE_SUPRA:
		if (GLOBAL_STATE->board_version >= 402 &&
		    GLOBAL_STATE->board_version <= 499) {
			return EMC2101_get_external_temp();
		} else {
			return EMC2101_get_internal_temp() + 5;
		}
	case DEVICE_GAMMA:
		return EMC2101_get_external_temp();
	default:
		return -1;
	}
}

bool should_test(GlobalState *GLOBAL_STATE)
{
	bool is_max = GLOBAL_STATE->asic_model == ASIC_BM1397;
	uint64_t best_diff = nvs_config_get_u64(NVS_CONFIG_BEST_DIFF, 0);
	uint16_t should_self_test =
		nvs_config_get_u16(NVS_CONFIG_SELF_TEST, 0);

	if (should_self_test == 1 && !is_max && best_diff < 1)
		return true;

	return false;
}

static void reset_self_test(void)
{
	ESP_LOGI(TAG, "Long press detected...");
	xSemaphoreGive(BootSemaphore);
}

static void display_msg(char *msg, GlobalState *GLOBAL_STATE)
{
	GLOBAL_STATE->SELF_TEST_MODULE.message = msg;
}

static esp_err_t test_fan_sense(GlobalState *GLOBAL_STATE)
{
	uint16_t fan_speed = 0;

	switch (GLOBAL_STATE->device_model) {
	case DEVICE_MAX:
	case DEVICE_ULTRA:
	case DEVICE_SUPRA:
	case DEVICE_GAMMA:
		fan_speed = EMC2101_get_fan_speed();
		break;
	default:
		break;
	}

	ESP_LOGI(TAG, "fanSpeed: %d", fan_speed);
	if (fan_speed > FAN_SPEED_TARGET_MIN)
		return ESP_OK;

	ESP_LOGE(TAG, "FAN test failed!");
	display_msg("FAN:WARN", GLOBAL_STATE);
	return ESP_FAIL;
}

static esp_err_t test_INA260_power_consumption(int target_power,
					       int margin)
{
	float power = INA260_read_power() / 1000;

	ESP_LOGI(TAG, "Power: %f", power);
	if (power > target_power - margin &&
	    power < target_power + margin)
		return ESP_OK;

	return ESP_FAIL;
}

static esp_err_t test_TPS546_power_consumption(int target_power,
					       int margin)
{
	float voltage = TPS546_get_vout();
	float current = TPS546_get_iout();
	float power = voltage * current;

	ESP_LOGI(TAG, "Power: %f, Voltage: %f, Current %f",
		 power, voltage, current);
	if (power > target_power - margin &&
	    power < target_power + margin)
		return ESP_OK;

	return ESP_FAIL;
}

static esp_err_t test_core_voltage(GlobalState *GLOBAL_STATE)
{
	uint16_t core_voltage = VCORE_get_voltage_mv(GLOBAL_STATE);

	ESP_LOGI(TAG, "Voltage: %u", core_voltage);
	char display_vcore[16] = {0};
	snprintf(display_vcore, sizeof(display_vcore),
		 "VCORE: %u", core_voltage);
	display_msg(display_vcore, GLOBAL_STATE);

	GLOBAL_STATE->SELF_TEST_MODULE.core_voltage = core_voltage;

	if (core_voltage > CORE_VOLTAGE_TARGET_MIN &&
	    core_voltage < CORE_VOLTAGE_TARGET_MAX)
		return ESP_OK;

	ESP_LOGE(TAG, "Core Voltage TEST FAIL, INCORRECT CORE VOLTAGE");
	display_msg("VCORE:FAIL", GLOBAL_STATE);
	return ESP_FAIL;
}

static esp_err_t test_display(GlobalState *GLOBAL_STATE)
{
	switch (GLOBAL_STATE->device_model) {
	case DEVICE_MAX:
	case DEVICE_ULTRA:
	case DEVICE_SUPRA:
	case DEVICE_GAMMA:
		if (display_init(GLOBAL_STATE) != ESP_OK) {
			display_msg("DISPLAY:FAIL", GLOBAL_STATE);
			return ESP_FAIL;
		}
		if (GLOBAL_STATE->SYSTEM_MODULE.is_screen_active)
			ESP_LOGI(TAG, "DISPLAY init success!");
		else
			ESP_LOGW(TAG, "DISPLAY not found!");
		break;
	default:
		break;
	}

	return ESP_OK;
}

static esp_err_t test_input(GlobalState *GLOBAL_STATE)
{
	switch (GLOBAL_STATE->device_model) {
	case DEVICE_MAX:
	case DEVICE_ULTRA:
	case DEVICE_SUPRA:
	case DEVICE_GAMMA:
		if (input_init(NULL, reset_self_test) != ESP_OK) {
			display_msg("INPUT:FAIL", GLOBAL_STATE);
			return ESP_FAIL;
		}
		ESP_LOGI(TAG, "INPUT init success!");
		break;
	default:
		break;
	}

	return ESP_OK;
}

static esp_err_t test_screen(GlobalState *GLOBAL_STATE)
{
	switch (GLOBAL_STATE->device_model) {
	case DEVICE_MAX:
	case DEVICE_ULTRA:
	case DEVICE_SUPRA:
	case DEVICE_GAMMA:
		if (screen_start(GLOBAL_STATE) != ESP_OK) {
			display_msg("SCREEN:FAIL", GLOBAL_STATE);
			return ESP_FAIL;
		}
		ESP_LOGI(TAG, "SCREEN start success!");
		break;
	default:
		break;
	}

	return ESP_OK;
}

static esp_err_t init_voltage_regulator(GlobalState *GLOBAL_STATE)
{
	ESP_RETURN_ON_ERROR(VCORE_init(GLOBAL_STATE), TAG,
			    "VCORE init failed!");
	ESP_RETURN_ON_ERROR(
		VCORE_set_voltage(
			nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE,
					   CONFIG_ASIC_VOLTAGE) / 1000.0,
			GLOBAL_STATE),
		TAG, "VCORE set voltage failed!");

	return ESP_OK;
}

static esp_err_t test_voltage_regulator(GlobalState *GLOBAL_STATE)
{
	switch (GLOBAL_STATE->device_model) {
	case DEVICE_MAX:
	case DEVICE_ULTRA:
	case DEVICE_SUPRA:
		gpio_set_direction(GPIO_ASIC_ENABLE, GPIO_MODE_OUTPUT);
		gpio_set_level(GPIO_ASIC_ENABLE, 0);
		break;
	case DEVICE_GAMMA:
	default:
		break;
	}

	if (init_voltage_regulator(GLOBAL_STATE) != ESP_OK) {
		ESP_LOGE(TAG, "VCORE init failed!");
		display_msg("VCORE:FAIL", GLOBAL_STATE);
		return ESP_FAIL;
	}

	switch (GLOBAL_STATE->device_model) {
	case DEVICE_MAX:
	case DEVICE_ULTRA:
	case DEVICE_SUPRA:
		if (GLOBAL_STATE->board_version < 402) {
			if (DS4432U_test() != ESP_OK) {
				ESP_LOGE(TAG, "DS4432 test failed!");
				display_msg("DS4432U:FAIL",
					    GLOBAL_STATE);
				return ESP_FAIL;
			}
		}
		break;
	case DEVICE_GAMMA:
		break;
	default:
		break;
	}

	ESP_LOGI(TAG, "Voltage Regulator test success!");
	return ESP_OK;
}

static esp_err_t test_init_peripherals(GlobalState *GLOBAL_STATE)
{
	switch (GLOBAL_STATE->device_model) {
	case DEVICE_MAX:
	case DEVICE_ULTRA:
	case DEVICE_SUPRA:
		ESP_RETURN_ON_ERROR(
			EMC2101_init(nvs_config_get_u16(
				NVS_CONFIG_INVERT_FAN_POLARITY, 1)),
			TAG, "EMC2101 init failed!");
		EMC2101_set_fan_speed(1);
		break;
	case DEVICE_GAMMA:
		ESP_RETURN_ON_ERROR(
			EMC2101_init(nvs_config_get_u16(
				NVS_CONFIG_INVERT_FAN_POLARITY, 1)),
			TAG, "EMC2101 init failed!");
		EMC2101_set_fan_speed(1);
		EMC2101_set_ideality_factor(EMC2101_IDEALITY_1_0319);
		EMC2101_set_beta_compensation(EMC2101_BETA_11);
		break;
	default:
		break;
	}

	switch (GLOBAL_STATE->device_model) {
	case DEVICE_MAX:
	case DEVICE_ULTRA:
	case DEVICE_SUPRA:
		if (GLOBAL_STATE->board_version < 402)
			ESP_RETURN_ON_ERROR(INA260_init(), TAG,
					    "INA260 init failed!");
		break;
	case DEVICE_GAMMA:
	default:
		break;
	}

	ESP_LOGI(TAG, "Peripherals init success!");
	return ESP_OK;
}

static esp_err_t test_psram(GlobalState *GLOBAL_STATE)
{
	if (!esp_psram_is_initialized()) {
		ESP_LOGE(TAG, "No PSRAM available on ESP32!");
		display_msg("PSRAM:FAIL", GLOBAL_STATE);
		return ESP_FAIL;
	}
	return ESP_OK;
}

/**
 * power_off_asic - safely disable ASIC power
 * @GLOBAL_STATE: pointer to GlobalState
 *
 * Zeroes TPS546 output voltage and disables the ASIC
 * enable GPIO for devices that support it.
 */
static void power_off_asic(GlobalState *GLOBAL_STATE)
{
	TPS546_set_vout(0);

	switch (GLOBAL_STATE->device_model) {
	case DEVICE_MAX:
	case DEVICE_ULTRA:
	case DEVICE_SUPRA:
		gpio_set_direction(GPIO_ASIC_ENABLE, GPIO_MODE_OUTPUT);
		gpio_set_level(GPIO_ASIC_ENABLE, 1);
		break;
	default:
		break;
	}

	uint16_t v = VCORE_get_voltage_mv(GLOBAL_STATE);
	ESP_LOGI(TAG, "Power off complete, voltage: %u mV", v);
}

/**
 * power_on_asic - re-enable ASIC power without reinitialising the VR
 * @GLOBAL_STATE: pointer to GlobalState
 *
 * Enables the ASIC power GPIO and restores the configured core
 * voltage.  Unlike test_voltage_regulator(), this does NOT call
 * VCORE_init() / TPS546_init() -- the regulator is already
 * configured from the init phase.
 */
static esp_err_t power_on_asic(GlobalState *GLOBAL_STATE)
{
	switch (GLOBAL_STATE->device_model) {
	case DEVICE_MAX:
	case DEVICE_ULTRA:
	case DEVICE_SUPRA:
		gpio_set_direction(GPIO_ASIC_ENABLE, GPIO_MODE_OUTPUT);
		gpio_set_level(GPIO_ASIC_ENABLE, 0);
		break;
	case DEVICE_GAMMA:
	default:
		break;
	}

	ESP_RETURN_ON_ERROR(
		VCORE_set_voltage(
			nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE,
					   CONFIG_ASIC_VOLTAGE) / 1000.0,
			GLOBAL_STATE),
		TAG, "VCORE set voltage failed on power-on!");

	vTaskDelay(pdMS_TO_TICKS(100));

	uint16_t v = VCORE_get_voltage_mv(GLOBAL_STATE);
	ESP_LOGI(TAG, "Power on complete, voltage: %u mV", v);

	return ESP_OK;
}

/**
 * cleanup_job_buffers - free ASIC job buffers with NULL guards
 * @GLOBAL_STATE: pointer to GlobalState
 */
static void cleanup_job_buffers(GlobalState *GLOBAL_STATE)
{
	if (GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs != NULL) {
		free(GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs);
		GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs = NULL;
	}
	if (GLOBAL_STATE->valid_jobs != NULL) {
		free(GLOBAL_STATE->valid_jobs);
		GLOBAL_STATE->valid_jobs = NULL;
	}
}

/**
 * reset_test_state - prepare SELF_TEST_MODULE for a new run
 * @GLOBAL_STATE: pointer to GlobalState
 *
 * When init_complete is set, only the ASIC-phase results are
 * cleared; init-phase results and their count are preserved.
 * Otherwise a full reset is performed.
 */
static void reset_test_state(GlobalState *GLOBAL_STATE)
{
	SelfTestModule *m = &GLOBAL_STATE->SELF_TEST_MODULE;

	m->active = true;
	m->finished = false;
	m->result = false;
	m->hashrate = 0;
	m->message = "Starting...";

	if (m->init_complete) {
		for (uint8_t i = m->init_result_count;
		     i < SELF_TEST_MAX_RESULTS; i++)
			memset(&m->results[i], 0,
			       sizeof(SelfTestResult));
		m->result_count = m->init_result_count;
	} else {
		m->result_count = 0;
		m->temperature = 0;
		m->core_voltage = 0;
		memset(m->results, 0, sizeof(m->results));
	}
}

void self_test(void *pvParameters)
{
	GlobalState *GLOBAL_STATE = (GlobalState *)pvParameters;

	ESP_LOGI(TAG, "Entering self-test mode");

	GLOBAL_STATE->SELF_TEST_MODULE.active = true;

	BootSemaphore = xSemaphoreCreateBinary();
	if (BootSemaphore == NULL) {
		ESP_LOGE(TAG, "Failed to create semaphore");
		return;
	}

	if (display_init(GLOBAL_STATE) != ESP_OK) {
		ESP_LOGW(TAG, "Display init failed");
	} else if (GLOBAL_STATE->SYSTEM_MODULE.is_screen_active) {
		ESP_LOGI(TAG, "Display init success!");
		input_init(NULL, reset_self_test);
		screen_start(GLOBAL_STATE);
	}

	char *wifi_ssid = nvs_config_get_string(NVS_CONFIG_WIFI_SSID,
					       WIFI_SSID);
	char *wifi_pass = nvs_config_get_string(NVS_CONFIG_WIFI_PASS,
					       WIFI_PASS);
	char *hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME,
					       HOSTNAME);

	wifi_init(wifi_ssid, wifi_pass, hostname,
		  GLOBAL_STATE->SYSTEM_MODULE.ip_addr_str);
	generate_ssid(GLOBAL_STATE->SYSTEM_MODULE.ap_ssid);

	free(wifi_ssid);
	free(wifi_pass);
	free(hostname);

	if (self_test_server_start(GLOBAL_STATE) != ESP_OK)
		ESP_LOGE(TAG, "Server start failed");

	wifi_connect();

	run_test_sequence(GLOBAL_STATE);

	/* Keep the task alive so the HTTP server remains reachable */
	while (1) {
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}

/**
 * run_init_tests - one-time module initialisation tests
 * @GLOBAL_STATE: pointer to GlobalState
 *
 * Runs PSRAM, peripheral, voltage-regulator, core-voltage and
 * temperature tests.  On success sets init_complete and records
 * init_result_count so subsequent ASIC re-runs can skip this phase.
 *
 * Return: ESP_OK if all init tests passed, ESP_FAIL otherwise.
 */
static esp_err_t run_init_tests(GlobalState *GLOBAL_STATE)
{
	SelfTestModule *m = &GLOBAL_STATE->SELF_TEST_MODULE;

	/* --- PSRAM --- */
	display_msg("TEST PSRAM", GLOBAL_STATE);
	self_test_server_notify_status("PSRAM", "running",
				       "Testing...");
	if (test_psram(GLOBAL_STATE) != ESP_OK) {
		ESP_LOGE(TAG, "NO PSRAM on device!");
		display_msg("NO PSRAM", GLOBAL_STATE);
		record_result(GLOBAL_STATE, "PSRAM", false, "Not found");
		return ESP_FAIL;
	}
	record_result(GLOBAL_STATE, "PSRAM", true, "OK");

	/* --- Peripherals (EMC2101 / INA260) --- */
	display_msg("TEST PERIPH", GLOBAL_STATE);
	self_test_server_notify_status("PERIPH", "running",
				       "Testing...");
	if (test_init_peripherals(GLOBAL_STATE) != ESP_OK) {
		ESP_LOGE(TAG, "Peripherals init failed!");
		display_msg("PERIPH:FAIL", GLOBAL_STATE);
		record_result(GLOBAL_STATE, "PERIPH", false, "Init fail");
		return ESP_FAIL;
	}
	record_result(GLOBAL_STATE, "PERIPH", true, "OK");

	/* --- Voltage Regulator --- */
	display_msg("TEST VR", GLOBAL_STATE);
	self_test_server_notify_status("VR", "running", "Testing...");
	if (test_voltage_regulator(GLOBAL_STATE) != ESP_OK) {
		ESP_LOGE(TAG, "Voltage Regulator test failed!");
		display_msg("VR:FAIL", GLOBAL_STATE);
		record_result(GLOBAL_STATE, "VR", false, "Init fail");
		return ESP_FAIL;
	}
	record_result(GLOBAL_STATE, "VR", true, "OK");

	/* --- Core Voltage ADC --- */
	self_test_server_notify_status("VCORE", "running",
				       "Measuring...");
	if (test_core_voltage(GLOBAL_STATE) != ESP_OK) {
		char vbuf[32];
		snprintf(vbuf, sizeof(vbuf), "%u mV",
			 m->core_voltage);
		record_result(GLOBAL_STATE, "VCORE", false, vbuf);
		return ESP_FAIL;
	}
	{
		char vbuf[32];
		snprintf(vbuf, sizeof(vbuf), "%u mV",
			 m->core_voltage);
		record_result(GLOBAL_STATE, "VCORE", true, vbuf);
	}

	/* --- Temperature Continuity --- */
	self_test_server_notify_status("TEMP", "running",
				       "Measuring...");
	float asic_temp = Thermal_get_chip_temp(GLOBAL_STATE);
	ESP_LOGI(TAG, "ASIC Temp: %.2f C", asic_temp);
	m->temperature = asic_temp;

	{
		char tbuf[32];
		snprintf(tbuf, sizeof(tbuf), "%.1f C", asic_temp);
		display_msg(tbuf, GLOBAL_STATE);

		if (asic_temp == -1.0f || asic_temp == 127.0f) {
			display_msg("TEMP:OPEN", GLOBAL_STATE);
			record_result(GLOBAL_STATE, "TEMP", false,
				      "Open circuit");
			return ESP_FAIL;
		}
		record_result(GLOBAL_STATE, "TEMP", true, tbuf);
	}

	self_test_server_notify_metrics(0, asic_temp, m->core_voltage);

	m->init_complete = true;
	m->init_result_count = m->result_count;
	ESP_LOGI(TAG, "Init tests passed (%u results recorded)",
		 m->init_result_count);

	return ESP_OK;
}

/**
 * run_asic_tests - ASIC power-on, detection and hashrate tests
 * @GLOBAL_STATE: pointer to GlobalState
 *
 * Powers on the ASIC (without reinitialising the VR), detects
 * chips, and measures hashrate.  Calls tests_done() on both
 * success and failure paths.  Safe to call repeatedly after
 * init tests have passed.
 */
static void run_asic_tests(GlobalState *GLOBAL_STATE)
{
	/* --- Power on ASIC --- */
	display_msg("POWER ON", GLOBAL_STATE);
	self_test_server_notify_status("POWER", "running",
				       "Powering on...");
	if (power_on_asic(GLOBAL_STATE) != ESP_OK) {
		ESP_LOGE(TAG, "ASIC power-on failed!");
		display_msg("POWER:FAIL", GLOBAL_STATE);
		record_result(GLOBAL_STATE, "POWER", false,
			      "Voltage set fail");
		tests_done(GLOBAL_STATE, TESTS_FAILED);
		return;
	}
	record_result(GLOBAL_STATE, "POWER", true, "OK");

	/* --- ASIC Detection --- */
	self_test_server_notify_status("ASIC", "running",
				       "Detecting chips...");
	if (SERIAL_init() != ESP_OK) {
		ESP_LOGE(TAG, "SERIAL init failed!");
		record_result(GLOBAL_STATE, "ASIC", false,
			      "Serial init fail");
		tests_done(GLOBAL_STATE, TESTS_FAILED);
		return;
	}

	uint8_t chips_detected =
		(GLOBAL_STATE->ASIC_functions.init_fn)(
			GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value,
			GLOBAL_STATE->asic_count);
	ESP_LOGI(TAG, "%u chips detected, %u expected",
		 chips_detected, GLOBAL_STATE->asic_count);

	if (chips_detected != GLOBAL_STATE->asic_count) {
		ESP_LOGE(TAG, "SELF TEST FAIL, %d of %d CHIPS DETECTED",
			 chips_detected, GLOBAL_STATE->asic_count);
		char ebuf[48];
		snprintf(ebuf, sizeof(ebuf), "%d/%d chips",
			 chips_detected, GLOBAL_STATE->asic_count);
		display_msg("ASIC:FAIL", GLOBAL_STATE);
		record_result(GLOBAL_STATE, "ASIC", false, ebuf);
		tests_done(GLOBAL_STATE, TESTS_FAILED);
		return;
	}
	{
		char cbuf[32];
		snprintf(cbuf, sizeof(cbuf), "%d chips OK",
			 chips_detected);
		record_result(GLOBAL_STATE, "ASIC", true, cbuf);
	}

	/* --- Hashrate --- */
	self_test_server_notify_status("HASHRATE", "running",
				       "Setting baud...");
	int baud = (*GLOBAL_STATE->ASIC_functions.set_max_baud_fn)();
	vTaskDelay(pdMS_TO_TICKS(10));

	if (SERIAL_set_baud(baud) != ESP_OK) {
		ESP_LOGE(TAG, "SERIAL set baud failed!");
		record_result(GLOBAL_STATE, "HASHRATE", false,
			      "Baud set fail");
		tests_done(GLOBAL_STATE, TESTS_FAILED);
		return;
	}
GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs = malloc(sizeof(bm_job *) * 128);
GLOBAL_STATE->valid_jobs = malloc(sizeof(uint8_t) * 128);

for (int i = 0; i < 128; i++) {
    GLOBAL_STATE->ASIC_TASK_MODULE.active_jobs[i] = NULL;
     GLOBAL_STATE->valid_jobs[i] = 0;
 }

	vTaskDelay(1000 / portTICK_PERIOD_MS);

  mining_notify notify_message;
 notify_message.job_id = 0;
  notify_message.prev_block_hash = "0c859545a3498373a57452fac22eb7113df2a465000543520000000000000000";
   notify_message.version = 0x20000004;
    notify_message.version_mask = 0x1fffe000;
  notify_message.target = 0x1705ae3a;
   notify_message.ntime = 0x647025b5;
    notify_message.difficulty = 1000000;

    const char * coinbase_tx = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b0389130cfab"
                               "e6d6d5cbab26a2599e92916edec"
                               "5657a94a0708ddb970f5c45b5d12905085617eff8e010000000000000031650707758de07b010000000000001cfd703"
                               "8212f736c7573682f0000000003"
                               "79ad0c2a000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c29525"
                               "34b424c4f434b3ae725d3994b81"
                               "1572c1f345deb98b56b465ef8e153ecbbd27fa37bf1b005161380000000000000000266a24aa21a9ed63b06a7946b19"
                               "0a3fda1d76165b25c9b883bcc66"
                               "21b040773050ee2a1bb18f1800000000";
    uint8_t merkles[13][32];
    int num_merkles = 13;

    hex2bin("2b77d9e413e8121cd7a17ff46029591051d0922bd90b2b2a38811af1cb57a2b2", merkles[0], 32);
    hex2bin("5c8874cef00f3a233939516950e160949ef327891c9090467cead995441d22c5", merkles[1], 32);
    hex2bin("2d91ff8e19ac5fa69a40081f26c5852d366d608b04d2efe0d5b65d111d0d8074", merkles[2], 32);
    hex2bin("0ae96f609ad2264112a0b2dfb65624bedbcea3b036a59c0173394bba3a74e887", merkles[3], 32);
    hex2bin("e62172e63973d69574a82828aeb5711fc5ff97946db10fc7ec32830b24df7bde", merkles[4], 32);
    hex2bin("adb49456453aab49549a9eb46bb26787fb538e0a5f656992275194c04651ec97", merkles[5], 32);
    hex2bin("a7bc56d04d2672a8683892d6c8d376c73d250a4871fdf6f57019bcc737d6d2c2", merkles[6], 32);
    hex2bin("d94eceb8182b4f418cd071e93ec2a8993a0898d4c93bc33d9302f60dbbd0ed10", merkles[7], 32);
    hex2bin("5ad7788b8c66f8f50d332b88a80077ce10e54281ca472b4ed9bbbbcb6cf99083", merkles[8], 32);
    hex2bin("9f9d784b33df1b3ed3edb4211afc0dc1909af9758c6f8267e469f5148ed04809", merkles[9], 32);
    hex2bin("48fd17affa76b23e6fb2257df30374da839d6cb264656a82e34b350722b05123", merkles[10], 32);
    hex2bin("c4f5ab01913fc186d550c1a28f3f3e9ffaca2016b961a6a751f8cca0089df924", merkles[11], 32);
    hex2bin("cff737e1d00176dd6bbfa73071adbb370f227cfb5fba186562e4060fcec877e1", merkles[12], 32);

    char * merkle_root = calculate_merkle_root_hash(coinbase_tx, merkles, num_merkles);

    bm_job job = construct_bm_job(&notify_message, merkle_root, 0x1fffe000);

    uint8_t difficulty_mask = 8;

    (*GLOBAL_STATE->ASIC_functions.set_difficulty_mask_fn)(difficulty_mask);

    ESP_LOGI(TAG, "Sending work");

    (*GLOBAL_STATE->ASIC_functions.send_work_fn)(GLOBAL_STATE, &job);

	double start = esp_timer_get_time();
	double sum = 0;
	double duration = 0;
	double hash_rate = 0;
	uint16_t core_voltage = 0;
	char hashrate_message[24] = {0};

	while (duration < 5) {
		task_result *asic_result =
			(*GLOBAL_STATE->ASIC_functions.receive_result_fn)(
				GLOBAL_STATE);

		if (asic_result != NULL) {
			double nonce_diff = test_nonce_value(
				&job, asic_result->nonce,
				asic_result->rolled_version);
			sum += difficulty_mask;
			duration = (double)(esp_timer_get_time() - start)
				   / 1000000;
			hash_rate = (sum * 4294967296)
				    / (duration * 1000000000);
			ESP_LOGI(TAG,
				 "Nonce %lu Nonce difficulty %.32f.",
				 asic_result->nonce, nonce_diff);
			ESP_LOGI(TAG, "%f Gh/s, duration %f",
				 hash_rate, duration);
		}

		duration = (double)(esp_timer_get_time() - start)
			   / 1000000;
		core_voltage = VCORE_get_voltage_mv(GLOBAL_STATE);
		ESP_LOGI(TAG, "Voltage: %u", core_voltage);
		snprintf(hashrate_message, sizeof(hashrate_message),
			 "HR: %.0f|V: %u", hash_rate, core_voltage);
		display_msg(hashrate_message, GLOBAL_STATE);

		GLOBAL_STATE->SELF_TEST_MODULE.hashrate = hash_rate;
		GLOBAL_STATE->SELF_TEST_MODULE.core_voltage = core_voltage;
		self_test_server_notify_metrics(hash_rate,
						GLOBAL_STATE->SELF_TEST_MODULE.temperature,
						core_voltage);
	}

	ESP_LOGI(TAG, "Hashrate: %f", hash_rate);
	vTaskDelay(pdMS_TO_TICKS(10));

	float expected_hashrate_mhs =
		GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value
		* GLOBAL_STATE->small_core_count
		* 0.8f
		* GLOBAL_STATE->asic_count
		/ 1000.0f;

	ESP_LOGI(TAG, "Hashrate: %.2f Gh/s, Expected: %.2f Gh/s",
		 hash_rate, expected_hashrate_mhs);

	if (hash_rate < expected_hashrate_mhs && hash_rate > 0) {
		char hbuf[48];
		snprintf(hbuf, sizeof(hbuf), "%.0f GH/s (exp %.0f)",
			 hash_rate, expected_hashrate_mhs);
		snprintf(hashrate_message, sizeof(hashrate_message),
			 "HR FAIL LOW: %.0f", hash_rate);
		display_msg(hashrate_message, GLOBAL_STATE);
		record_result(GLOBAL_STATE, "HASHRATE", false, hbuf);
		tests_done(GLOBAL_STATE, TESTS_FAILED);
		return;
	} else if (expected_hashrate_mhs == 0) {
		display_msg("HASHRATE: 0 GH", GLOBAL_STATE);
		record_result(GLOBAL_STATE, "HASHRATE", false, "0 GH/s");
		tests_done(GLOBAL_STATE, TESTS_FAILED);
		return;
	}

	{
		char hbuf[32];
		snprintf(hbuf, sizeof(hbuf), "%.0f GH/s", hash_rate);
		record_result(GLOBAL_STATE, "HASHRATE", true, hbuf);
	}

	tests_done(GLOBAL_STATE, TESTS_PASSED);
}

void run_test_sequence(GlobalState *GLOBAL_STATE)
{
	reset_test_state(GLOBAL_STATE);
	ESP_LOGI(TAG, "Running Self Tests");

	if (!GLOBAL_STATE->SELF_TEST_MODULE.init_complete) {
		ESP_LOGI(TAG, "Running init-phase tests");
		if (run_init_tests(GLOBAL_STATE) != ESP_OK) {
			tests_done(GLOBAL_STATE, TESTS_FAILED);
			return;
		}
	} else {
		ESP_LOGI(TAG,
			 "Init already complete, skipping to ASIC tests");
	}

	run_asic_tests(GLOBAL_STATE);
}

static void tests_done(GlobalState *GLOBAL_STATE, bool test_result)
{
	power_off_asic(GLOBAL_STATE);
	cleanup_job_buffers(GLOBAL_STATE);

	GLOBAL_STATE->SELF_TEST_MODULE.result = test_result;
	GLOBAL_STATE->SELF_TEST_MODULE.finished = true;
	GLOBAL_STATE->SELF_TEST_MODULE.active = false;

	self_test_server_notify_done(test_result == TESTS_PASSED);

	if (test_result == TESTS_FAILED) {
		ESP_LOGI(TAG,
			 "SELF TESTS FAIL -- use web UI or RESET to continue");
	} else {
		ESP_LOGI(TAG, "Self Tests Passed!!!");
	}
}
