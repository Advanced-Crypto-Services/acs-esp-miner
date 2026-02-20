/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Self-Test HTTP/WebSocket Server
 *
 * Lightweight HTTP server with embedded HTML page and WebSocket
 * endpoint for real-time self-test status updates.  Runs
 * entirely within the self_test module -- no dependency on
 * the main http_server component.
 */

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "global_state.h"
#include "nvs_config.h"
#include "self_test_server.h"
#include "self_test_page.h"
#include "self_test.h"

static const char *TAG = "SELF_TEST_SRV";

static httpd_handle_t st_server;
static GlobalState *st_global;
static int ws_fd = -1;

#define MAX_WS_FRAME 512

/**
 * ws_send_text - send a text frame to the connected WS client
 * @msg: null-terminated JSON string to send
 *
 * Silently returns if no client is connected.
 */
static void ws_send_text(const char *msg)
{
	if (ws_fd < 0 || st_server == NULL)
		return;

	httpd_ws_frame_t frame = {
		.type = HTTPD_WS_TYPE_TEXT,
		.payload = (uint8_t *)msg,
		.len = strlen(msg),
	};

	esp_err_t ret = httpd_ws_send_frame_async(st_server, ws_fd,
						  &frame);
	if (ret != ESP_OK) {
		ESP_LOGW(TAG, "WS send failed: %s", esp_err_to_name(ret));
		ws_fd = -1;
	}
}

/**
 * handle_ws - WebSocket handshake and frame handler
 *
 * On GET (handshake), records the socket fd.  Incoming
 * data frames are ignored.
 */
static esp_err_t handle_ws(httpd_req_t *req)
{
	if (req->method == HTTP_GET) {
		ws_fd = httpd_req_to_sockfd(req);
		ESP_LOGI(TAG, "WS client connected (fd=%d)", ws_fd);
		return ESP_OK;
	}
	return ESP_OK;
}

/**
 * handle_get_root - serve the embedded HTML page
 */
static esp_err_t handle_get_root(httpd_req_t *req)
{
	httpd_resp_set_type(req, "text/html");
	return httpd_resp_send(req, self_test_page,
			       sizeof(self_test_page) - 1);
}

/**
 * handle_get_status - return JSON snapshot of current test state
 */
static esp_err_t handle_get_status(httpd_req_t *req)
{
	SelfTestModule *m = &st_global->SELF_TEST_MODULE;
	cJSON *root = cJSON_CreateObject();

	cJSON_AddBoolToObject(root, "active", m->active);
	cJSON_AddBoolToObject(root, "finished", m->finished);
	cJSON_AddBoolToObject(root, "result", m->result);
	cJSON_AddNumberToObject(root, "hashrate", m->hashrate);
	cJSON_AddNumberToObject(root, "temperature", m->temperature);
	cJSON_AddNumberToObject(root, "core_voltage", m->core_voltage);

	cJSON *arr = cJSON_AddArrayToObject(root, "results");
	for (uint8_t i = 0; i < m->result_count; i++) {
		cJSON *item = cJSON_CreateObject();
		cJSON_AddStringToObject(item, "name",
					m->results[i].name);
		cJSON_AddBoolToObject(item, "passed",
				      m->results[i].passed);
		cJSON_AddBoolToObject(item, "completed",
				      m->results[i].completed);
		cJSON_AddStringToObject(item, "detail",
					m->results[i].detail);
		cJSON_AddItemToArray(arr, item);
	}

	const char *json = cJSON_PrintUnformatted(root);
	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, json);
	free((void *)json);
	cJSON_Delete(root);

	return ESP_OK;
}

#define MAX_CONFIG_BODY 1024

/**
 * handle_get_config - return current NVS connection settings as JSON
 *
 * Passwords are not returned for security; the fields are omitted
 * so the browser shows empty password inputs.
 */
static esp_err_t handle_get_config(httpd_req_t *req)
{
	cJSON *root = cJSON_CreateObject();

	char *ssid = nvs_config_get_string(NVS_CONFIG_WIFI_SSID, "");
	char *host = nvs_config_get_string(NVS_CONFIG_HOSTNAME, "");
	char *surl = nvs_config_get_string(NVS_CONFIG_STRATUM_URL, "");
	uint16_t sport = nvs_config_get_u16(NVS_CONFIG_STRATUM_PORT, 3333);
	char *suser = nvs_config_get_string(NVS_CONFIG_STRATUM_USER, "");

	cJSON_AddStringToObject(root, "wifi_ssid", ssid);
	cJSON_AddStringToObject(root, "hostname", host);
	cJSON_AddStringToObject(root, "stratum_url", surl);
	cJSON_AddNumberToObject(root, "stratum_port", sport);
	cJSON_AddStringToObject(root, "stratum_user", suser);

	free(ssid);
	free(host);
	free(surl);
	free(suser);

	const char *json = cJSON_PrintUnformatted(root);
	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, json);
	free((void *)json);
	cJSON_Delete(root);

	return ESP_OK;
}

/**
 * handle_post_config - save connection settings to NVS
 *
 * Accepts a JSON body with any subset of: wifi_ssid, wifi_pass,
 * hostname, stratum_url, stratum_port, stratum_user, stratum_pass.
 * Only provided fields are written; omitted fields are unchanged.
 */
static esp_err_t handle_post_config(httpd_req_t *req)
{
	char buf[MAX_CONFIG_BODY];
	int received = httpd_req_recv(req, buf, sizeof(buf) - 1);

	if (received <= 0) {
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
				    "Empty body");
		return ESP_OK;
	}
	buf[received] = '\0';

	cJSON *root = cJSON_Parse(buf);
	if (root == NULL) {
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
				    "Invalid JSON");
		return ESP_OK;
	}

	cJSON *item;

	item = cJSON_GetObjectItem(root, "wifi_ssid");
	if (cJSON_IsString(item) && item->valuestring[0] != '\0')
		nvs_config_set_string(NVS_CONFIG_WIFI_SSID,
				      item->valuestring);

	item = cJSON_GetObjectItem(root, "wifi_pass");
	if (cJSON_IsString(item) && item->valuestring[0] != '\0')
		nvs_config_set_string(NVS_CONFIG_WIFI_PASS,
				      item->valuestring);

	item = cJSON_GetObjectItem(root, "hostname");
	if (cJSON_IsString(item) && item->valuestring[0] != '\0')
		nvs_config_set_string(NVS_CONFIG_HOSTNAME,
				      item->valuestring);

	item = cJSON_GetObjectItem(root, "stratum_url");
	if (cJSON_IsString(item) && item->valuestring[0] != '\0')
		nvs_config_set_string(NVS_CONFIG_STRATUM_URL,
				      item->valuestring);

	item = cJSON_GetObjectItem(root, "stratum_port");
	if (cJSON_IsNumber(item))
		nvs_config_set_u16(NVS_CONFIG_STRATUM_PORT,
				   (uint16_t)item->valuedouble);

	item = cJSON_GetObjectItem(root, "stratum_user");
	if (cJSON_IsString(item) && item->valuestring[0] != '\0')
		nvs_config_set_string(NVS_CONFIG_STRATUM_USER,
				      item->valuestring);

	item = cJSON_GetObjectItem(root, "stratum_pass");
	if (cJSON_IsString(item) && item->valuestring[0] != '\0')
		nvs_config_set_string(NVS_CONFIG_STRATUM_PASS,
				      item->valuestring);

	cJSON_Delete(root);

	ESP_LOGI(TAG, "Config saved to NVS");
	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, "{\"status\":\"saved\"}");
	return ESP_OK;
}

static void run_test_task(void *arg);

/**
 * handle_post_start - trigger a new self-test sequence
 *
 * Returns 409 if a test is already in progress.
 */
static esp_err_t handle_post_start(httpd_req_t *req)
{
	if (st_global->SELF_TEST_MODULE.active) {
		httpd_resp_set_status(req, "409 Conflict");
		httpd_resp_sendstr(req, "{\"error\":\"test running\"}");
		return ESP_OK;
	}

	BaseType_t ret = xTaskCreate(run_test_task, "self_test_run",
				     8192, st_global, 5, NULL);
	if (ret != pdPASS) {
		httpd_resp_send_500(req);
		return ESP_OK;
	}

	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, "{\"status\":\"started\"}");
	return ESP_OK;
}

/**
 * handle_post_restart - restart the ESP32
 */
static esp_err_t handle_post_restart(httpd_req_t *req)
{
	httpd_resp_set_type(req, "application/json");
	httpd_resp_sendstr(req, "{\"status\":\"restarting\"}");
	vTaskDelay(pdMS_TO_TICKS(500));
	esp_restart();
	return ESP_OK;
}

/**
 * run_test_task - FreeRTOS task wrapper for re-running the test sequence
 * @arg: pointer to GlobalState
 */
static void run_test_task(void *arg)
{
	GlobalState *state = (GlobalState *)arg;
	run_test_sequence(state);
	vTaskDelete(NULL);
}

esp_err_t self_test_server_start(GlobalState *global_state)
{
	st_global = global_state;

	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.uri_match_fn = httpd_uri_match_wildcard;
	config.max_open_sockets = 4;
	config.max_uri_handlers = 10;

	ESP_LOGI(TAG, "Starting self-test HTTP server");
	esp_err_t ret = httpd_start(&st_server, &config);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to start server: %s",
			 esp_err_to_name(ret));
		return ret;
	}

	httpd_uri_t uri_ws = {
		.uri = "/ws",
		.method = HTTP_GET,
		.handler = handle_ws,
		.user_ctx = NULL,
		.is_websocket = true,
	};
	httpd_register_uri_handler(st_server, &uri_ws);

	httpd_uri_t uri_status = {
		.uri = "/api/test/status",
		.method = HTTP_GET,
		.handler = handle_get_status,
		.user_ctx = NULL,
	};
	httpd_register_uri_handler(st_server, &uri_status);

	httpd_uri_t uri_start = {
		.uri = "/api/test/start",
		.method = HTTP_POST,
		.handler = handle_post_start,
		.user_ctx = NULL,
	};
	httpd_register_uri_handler(st_server, &uri_start);

	httpd_uri_t uri_restart = {
		.uri = "/api/system/restart",
		.method = HTTP_POST,
		.handler = handle_post_restart,
		.user_ctx = NULL,
	};
	httpd_register_uri_handler(st_server, &uri_restart);

	httpd_uri_t uri_get_config = {
		.uri = "/api/config",
		.method = HTTP_GET,
		.handler = handle_get_config,
		.user_ctx = NULL,
	};
	httpd_register_uri_handler(st_server, &uri_get_config);

	httpd_uri_t uri_post_config = {
		.uri = "/api/config",
		.method = HTTP_POST,
		.handler = handle_post_config,
		.user_ctx = NULL,
	};
	httpd_register_uri_handler(st_server, &uri_post_config);

	httpd_uri_t uri_root = {
		.uri = "/*",
		.method = HTTP_GET,
		.handler = handle_get_root,
		.user_ctx = NULL,
	};
	httpd_register_uri_handler(st_server, &uri_root);

	ESP_LOGI(TAG, "Self-test server started on port 80");
	return ESP_OK;
}

void self_test_server_notify_status(const char *test_name,
				    const char *state,
				    const char *detail)
{
	char buf[MAX_WS_FRAME];
	int len = snprintf(buf, sizeof(buf),
		"{\"type\":\"%s\",\"test\":\"%s\","
		"\"state\":\"%s\",\"detail\":\"%s\"}",
		(strcmp(state, "running") == 0) ? "status" : "result",
		test_name, state, detail);

	if (len > 0 && (size_t)len < sizeof(buf))
		ws_send_text(buf);
}

void self_test_server_notify_metrics(float hashrate,
				     float temperature,
				     uint16_t core_voltage)
{
	char buf[MAX_WS_FRAME];
	int len = snprintf(buf, sizeof(buf),
		"{\"type\":\"metrics\",\"hashrate\":%.1f,"
		"\"temperature\":%.1f,\"core_voltage\":%u}",
		hashrate, temperature, core_voltage);

	if (len > 0 && (size_t)len < sizeof(buf))
		ws_send_text(buf);
}

void self_test_server_notify_done(bool passed)
{
	char buf[64];
	snprintf(buf, sizeof(buf),
		 "{\"type\":\"done\",\"passed\":%s}",
		 passed ? "true" : "false");
	ws_send_text(buf);
}
