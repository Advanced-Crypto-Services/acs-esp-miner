//updatetest
#include "http_server.h"
#include "recovery_page.h"
#include "theme_api.h"  // Add theme API include
#include "dataBase.h"  // Add database API include
#include "cJSON.h"
#include "esp_chip_info.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "global_state.h"
#include "nvs_config.h"
#include "vcore.h"
#include "power_management_task.h"  // Add this for preset support
#include <fcntl.h>
#include <string.h>
#include <sys/param.h>

#include "dns_server.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "lwip/err.h"
#include "lwip/inet.h"
#include "lwip/lwip_napt.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <pthread.h>
#include "lvglDisplayBAP.h"
#include "connect.h"
#include "esp_wifi_types.h"

static const char * TAG = "http_server";
static const char * CORS_TAG = "CORS";

#include "tasks/power_management_task.h"

static GlobalState * GLOBAL_STATE;
static httpd_handle_t server = NULL;
QueueHandle_t log_queue = NULL;

static int fd = -1;

#define REST_CHECK(a, str, goto_tag, ...)                                                                                          \
    do {                                                                                                                           \
        if (!(a)) {                                                                                                                \
            ESP_LOGE(TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__);                                                  \
            goto goto_tag;                                                                                                         \
        }                                                                                                                          \
    } while (0)

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE (10240)
#define MESSAGE_QUEUE_SIZE (128)

#define MAX_HTTP_REQUEST_SIZE 4096  // Maximum HTTP request body size
#define MAX_JSON_RESPONSE_SIZE 4096  // Maximum JSON response size
#define MAX_NVS_STRING_SIZE 128      // Maximum NVS string length
#define MAX_OTA_BUFFER_SIZE 1000     // Maximum OTA buffer size

// Pre-allocated buffers for HTTP request handling
static char http_request_buffer[MAX_HTTP_REQUEST_SIZE];
static char json_response_buffer[MAX_JSON_RESPONSE_SIZE];
static char nvs_string_buffer[MAX_NVS_STRING_SIZE];
static char ota_buffer[MAX_OTA_BUFFER_SIZE];  // Dedicated buffer for OTA operations

static esp_err_t GET_wifi_scan(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    
    // Give some time for the connected flag to take effect
    vTaskDelay(100 / portTICK_PERIOD_MS);
    
    wifi_ap_record_simple_t ap_records[20];
    uint16_t ap_count = 0;

    esp_err_t err = wifi_scan(ap_records, &ap_count);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "WiFi scan failed");
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *networks = cJSON_CreateArray();

    for (int i = 0; i < ap_count; i++) {
        cJSON *network = cJSON_CreateObject();
        cJSON_AddStringToObject(network, "ssid", (char *)ap_records[i].ssid);
        cJSON_AddNumberToObject(network, "rssi", ap_records[i].rssi);
        cJSON_AddNumberToObject(network, "authmode", ap_records[i].authmode);
        cJSON_AddItemToArray(networks, network);
    }

    cJSON_AddItemToObject(root, "networks", networks);

    const char *response = cJSON_Print(root);
    httpd_resp_sendstr(req, response);

    free((void *)response);
    cJSON_Delete(root);
    return ESP_OK;
}

typedef struct rest_server_context
{
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

#define CHECK_FILE_EXTENSION(filename, ext) (strcasecmp(&filename[strlen(filename) - strlen(ext)], ext) == 0)

static esp_err_t ip_in_private_range(uint32_t address) {
    uint32_t ip_address = ntohl(address);

    // 10.0.0.0 - 10.255.255.255 (Class A)
    if ((ip_address >= 0x0A000000) && (ip_address <= 0x0AFFFFFF)) {
        return ESP_OK;
    }

    // 172.16.0.0 - 172.31.255.255 (Class B)
    if ((ip_address >= 0xAC100000) && (ip_address <= 0xAC1FFFFF)) {
        return ESP_OK;
    }

    // 192.168.0.0 - 192.168.255.255 (Class C)
    if ((ip_address >= 0xC0A80000) && (ip_address <= 0xC0A8FFFF)) {
        return ESP_OK;
    }

    return ESP_FAIL;
}

static uint32_t extract_origin_ip_addr(httpd_req_t *req)
{
    char origin[128];
    char ip_str[16];
    uint32_t origin_ip_addr = 0;

    // Attempt to get the Origin header.
    if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK) {
        ESP_LOGD(CORS_TAG, "No origin header found.");
        return 0;
    }
    ESP_LOGD(CORS_TAG, "Origin header: %s", origin);

    // Find the start of the IP address in the Origin header
    const char *prefix = "http://";
    char *ip_start = strstr(origin, prefix);
    if (ip_start) {
        ip_start += strlen(prefix); // Move past "http://"

        // Extract the IP address portion (up to the next '/')
        char *ip_end = strchr(ip_start, '/');
        size_t ip_len = ip_end ? (size_t)(ip_end - ip_start) : strlen(ip_start);
        if (ip_len < sizeof(ip_str)) {
            strncpy(ip_str, ip_start, ip_len);
            ip_str[ip_len] = '\0'; // Null-terminate the string

            // Convert the IP address string to uint32_t
            origin_ip_addr = inet_addr(ip_str);
            if (origin_ip_addr == INADDR_NONE) {
                ESP_LOGW(CORS_TAG, "Invalid IP address: %s", ip_str);
            } else {
                ESP_LOGD(CORS_TAG, "Extracted IP address %lu", origin_ip_addr);
            }
        } else {
            ESP_LOGW(CORS_TAG, "IP address string is too long: %s", ip_start);
        }
    }

    return origin_ip_addr;
}

static esp_err_t is_network_allowed(httpd_req_t * req)
{
    if (GLOBAL_STATE->SYSTEM_MODULE.ap_enabled == true) {
        ESP_LOGI(CORS_TAG, "Device in AP mode. Allowing CORS.");
        return ESP_OK;
    }

    int sockfd = httpd_req_to_sockfd(req);
    char ipstr[INET6_ADDRSTRLEN];
    struct sockaddr_in6 addr;   // esp_http_server uses IPv6 addressing
    socklen_t addr_size = sizeof(addr);

    if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_size) < 0) {
        ESP_LOGE(CORS_TAG, "Error getting client IP");
        return ESP_FAIL;
    }

    uint32_t request_ip_addr = addr.sin6_addr.un.u32_addr[3];

    // // Convert to IPv6 string
    // inet_ntop(AF_INET, &addr.sin6_addr, ipstr, sizeof(ipstr));

    // Convert to IPv4 string
    inet_ntop(AF_INET, &request_ip_addr, ipstr, sizeof(ipstr));

    uint32_t origin_ip_addr = extract_origin_ip_addr(req);
    if (origin_ip_addr == 0) {
        origin_ip_addr = request_ip_addr;
    }

    if (ip_in_private_range(origin_ip_addr) == ESP_OK && ip_in_private_range(request_ip_addr) == ESP_OK) {
        return ESP_OK;
    }

    ESP_LOGI(CORS_TAG, "Client is NOT in the private ip ranges or same range as server.");
    return ESP_FAIL;
}

esp_err_t init_fs(void)
{
    esp_vfs_spiffs_conf_t conf = {.base_path = "", .partition_label = "www", .max_files = 5, .format_if_mount_failed = false};
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ESP_FAIL;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    return ESP_OK;
}

/* Function for stopping the webserver */
void stop_webserver(httpd_handle_t server)
{
    if (server) {
        /* Stop the httpd server */
        httpd_stop(server);
    }
}

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t * req, const char * filepath)
{
    const char * type = "text/plain";
    if (CHECK_FILE_EXTENSION(filepath, ".html")) {
        type = "text/html";
    } else if (CHECK_FILE_EXTENSION(filepath, ".js")) {
        type = "application/javascript";
    } else if (CHECK_FILE_EXTENSION(filepath, ".css")) {
        type = "text/css";
    } else if (CHECK_FILE_EXTENSION(filepath, ".png")) {
        type = "image/png";
    } else if (CHECK_FILE_EXTENSION(filepath, ".ico")) {
        type = "image/x-icon";
    } else if (CHECK_FILE_EXTENSION(filepath, ".svg")) {
        type = "text/xml";
    }
    return httpd_resp_set_type(req, type);
}

static esp_err_t set_cors_headers(httpd_req_t * req)
{

    esp_err_t err;

    err = httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    err = httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    err = httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

/* Recovery handler */
static esp_err_t rest_recovery_handler(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_send(req, recovery_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* Send HTTP response with the contents of the requested file */
static esp_err_t rest_common_get_handler(httpd_req_t * req)
{
    char filepath[FILE_PATH_MAX];
    uint8_t filePathLength = sizeof(filepath);

    rest_server_context_t * rest_context = (rest_server_context_t *) req->user_ctx;
    strlcpy(filepath, rest_context->base_path, filePathLength);
    if (req->uri[strlen(req->uri) - 1] == '/') {
        strlcat(filepath, "/index.html", filePathLength);
    } else {
        strlcat(filepath, req->uri, filePathLength);
    }
    set_content_type_from_file(req, filepath);
    strcat(filepath, ".gz");
    int fd = open(filepath, O_RDONLY, 0);
    if (fd == -1) {
        // Set status
        httpd_resp_set_status(req, "302 Temporary Redirect");
        // Redirect to the "/" root directory
        httpd_resp_set_hdr(req, "Location", "/");
        // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
        httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

        ESP_LOGI(TAG, "Redirecting to root");
        return ESP_OK;
    }

    if (req->uri[strlen(req->uri) - 1] != '/') {
        httpd_resp_set_hdr(req, "Cache-Control", "max-age=2592000");
    }

    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");

    char * chunk = rest_context->scratch;
    ssize_t read_bytes;
    do {
        /* Read file in chunks into the scratch buffer */
        read_bytes = read(fd, chunk, SCRATCH_BUFSIZE);
        if (read_bytes == -1) {
            ESP_LOGE(TAG, "Failed to read file : %s", filepath);
        } else if (read_bytes > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                close(fd);
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_OK;
            }
        }
    } while (read_bytes > 0);
    /* Close file after sending complete */
    close(fd);
    ESP_LOGI(TAG, "File sending complete");
    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t handle_options_request(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    // Set CORS headers for OPTIONS request
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    // Send a blank response for OPTIONS request
    httpd_resp_send(req, NULL, 0);

    return ESP_OK;
}

static esp_err_t PATCH_update_settings(httpd_req_t * req)
{

    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    int total_len = req->content_len;
    int cur_len = 0;
    int received = 0;
    
    // Use pre-allocated buffer instead of scratch buffer
    if (total_len >= MAX_HTTP_REQUEST_SIZE) {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_OK;
    }
    
    // Clear buffer before use
    memset(http_request_buffer, 0, MAX_HTTP_REQUEST_SIZE);
    
    while (cur_len < total_len) {
        received = httpd_req_recv(req, http_request_buffer + cur_len, total_len - cur_len);
        if (received <= 0) {
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
            return ESP_OK;
        }
        cur_len += received;
    }
    http_request_buffer[total_len] = '\0';

    cJSON * root = cJSON_Parse(http_request_buffer);
    cJSON * item;
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    if ((item = cJSON_GetObjectItem(root, "stratumURL")) != NULL) {
        nvs_config_set_string(NVS_CONFIG_STRATUM_URL, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "fallbackStratumURL")) != NULL) {
        nvs_config_set_string(NVS_CONFIG_FALLBACK_STRATUM_URL, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "stratumUser")) != NULL) {
        nvs_config_set_string(NVS_CONFIG_STRATUM_USER, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "stratumPassword")) != NULL) {
        nvs_config_set_string(NVS_CONFIG_STRATUM_PASS, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "fallbackStratumUser")) != NULL) {
        nvs_config_set_string(NVS_CONFIG_FALLBACK_STRATUM_USER, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "fallbackStratumPassword")) != NULL) {
        nvs_config_set_string(NVS_CONFIG_FALLBACK_STRATUM_PASS, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "stratumPort")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_STRATUM_PORT, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "fallbackStratumPort")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_FALLBACK_STRATUM_PORT, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "ssid")) != NULL) {
        nvs_config_set_string(NVS_CONFIG_WIFI_SSID, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "wifiPass")) != NULL) {
        nvs_config_set_string(NVS_CONFIG_WIFI_PASS, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "hostname")) != NULL) {
        nvs_config_set_string(NVS_CONFIG_HOSTNAME, item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "coreVoltage")) != NULL && item->valueint > 0) {
        nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "frequency")) != NULL && item->valueint > 0) {
        nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "flipscreen")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_FLIP_SCREEN, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "overheat_mode")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_OVERHEAT_MODE, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "invertscreen")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_INVERT_SCREEN, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "invertfanpolarity")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_INVERT_FAN_POLARITY, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "autofanspeed")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "fanspeed")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "autotune")) != NULL) {
        nvs_config_set_u16(NVS_CONFIG_AUTOTUNE_FLAG, item->valueint);
    }
    // Apply preset immediately if "presetName" field is present
    if ((item = cJSON_GetObjectItem(root, "presetName")) != NULL && item->valuestring != NULL) {
        if (apply_preset(GLOBAL_STATE->device_model, item->valuestring)) {
            ESP_LOGI(TAG, "Preset '%s' applied successfully", item->valuestring);
            lvglSendPresetBAP();
        } else {
            ESP_LOGE(TAG, "Failed to apply preset '%s'", item->valuestring);
        }
    }

    // Create response JSON with updated settings
    httpd_resp_set_type(req, "application/json");
    
    cJSON *response = cJSON_CreateObject();
    cJSON *updated_settings = cJSON_CreateObject();
    
    // Add each setting that was updated to the response
    if ((item = cJSON_GetObjectItem(root, "stratumURL")) != NULL) {
        cJSON_AddStringToObject(updated_settings, "stratumURL", item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "fallbackStratumURL")) != NULL) {
        cJSON_AddStringToObject(updated_settings, "fallbackStratumURL", item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "stratumUser")) != NULL) {
        cJSON_AddStringToObject(updated_settings, "stratumUser", item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "stratumPassword")) != NULL) {
        cJSON_AddStringToObject(updated_settings, "stratumPassword", "***"); // Don't expose password
    }
    if ((item = cJSON_GetObjectItem(root, "fallbackStratumUser")) != NULL) {
        cJSON_AddStringToObject(updated_settings, "fallbackStratumUser", item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "fallbackStratumPassword")) != NULL) {
        cJSON_AddStringToObject(updated_settings, "fallbackStratumPassword", "***"); // Don't expose password
    }
    if ((item = cJSON_GetObjectItem(root, "stratumPort")) != NULL) {
        cJSON_AddNumberToObject(updated_settings, "stratumPort", item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "fallbackStratumPort")) != NULL) {
        cJSON_AddNumberToObject(updated_settings, "fallbackStratumPort", item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "ssid")) != NULL) {
        cJSON_AddStringToObject(updated_settings, "ssid", item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "wifiPass")) != NULL) {
        cJSON_AddStringToObject(updated_settings, "wifiPass", "***"); // Don't expose password
    }
    if ((item = cJSON_GetObjectItem(root, "hostname")) != NULL) {
        cJSON_AddStringToObject(updated_settings, "hostname", item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "coreVoltage")) != NULL && item->valueint > 0) {
        cJSON_AddNumberToObject(updated_settings, "coreVoltage", item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "frequency")) != NULL && item->valueint > 0) {
        cJSON_AddNumberToObject(updated_settings, "frequency", item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "flipscreen")) != NULL) {
        cJSON_AddNumberToObject(updated_settings, "flipscreen", item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "overheat_mode")) != NULL) {
        cJSON_AddNumberToObject(updated_settings, "overheat_mode", item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "invertscreen")) != NULL) {
        cJSON_AddNumberToObject(updated_settings, "invertscreen", item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "invertfanpolarity")) != NULL) {
        cJSON_AddNumberToObject(updated_settings, "invertfanpolarity", item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "autofanspeed")) != NULL) {
        cJSON_AddNumberToObject(updated_settings, "autofanspeed", item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "fanspeed")) != NULL) {
        cJSON_AddNumberToObject(updated_settings, "fanspeed", item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "autotune")) != NULL) {
        cJSON_AddNumberToObject(updated_settings, "autotune", item->valueint);
    }
    if ((item = cJSON_GetObjectItem(root, "autotunePreset")) != NULL) {
        cJSON_AddStringToObject(updated_settings, "autotunePreset", item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "presetName")) != NULL && item->valuestring != NULL) {
        cJSON_AddStringToObject(updated_settings, "presetName", item->valuestring);
        cJSON_AddBoolToObject(updated_settings, "presetApplied", apply_preset(GLOBAL_STATE->device_model, item->valuestring));
    }
    
    // Add metadata to response
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Settings updated successfully");
    cJSON_AddItemToObject(response, "updatedSettings", updated_settings);
    time_t now;
    time(&now);
    cJSON_AddNumberToObject(response, "timestamp", now);
    
    // Send response
    const char *response_str = cJSON_Print(response);
    httpd_resp_sendstr(req, response_str);

    // Log the event
    dataBase_log_event("settings", "info", "Settings updated via WebUI", response_str);
    
    // Cleanup
    free((char *)response_str);
    cJSON_Delete(response);
    cJSON_Delete(root);
    
    return ESP_OK;
}

static esp_err_t POST_restart(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        ESP_LOGW(TAG, "Unauthorized system restart attempt from client");
        dataBase_log_event("system", "warning", "Unauthorized system restart attempt", NULL);
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Restarting System because of API Request");

    // Set content type to JSON
    httpd_resp_set_type(req, "application/json");

    // Create JSON response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "System will restart shortly");
    time_t now;
    time(&now);
    cJSON_AddNumberToObject(response, "timestamp", now);
    
    // Send JSON response before restarting
    const char *response_str = cJSON_Print(response);
    httpd_resp_sendstr(req, response_str);
    
    // Log the restart event
    dataBase_log_event("system", "info", "System restart initiated via API", response_str);
    
    // Cleanup
    free((char *)response_str);
    cJSON_Delete(response);

    // Delay to ensure the response is sent
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // Restart the system
    esp_restart();

    // This return statement will never be reached, but it's good practice to include it
    return ESP_OK;
}

/* Simple handler for getting system handler */
// Pre-allocated buffers for NVS strings to avoid dynamic allocation
static char ssid_buffer[MAX_NVS_STRING_SIZE];
static char hostname_buffer[MAX_NVS_STRING_SIZE];
static char stratum_url_buffer[MAX_NVS_STRING_SIZE];
static char fallback_stratum_url_buffer[MAX_NVS_STRING_SIZE];
static char stratum_user_buffer[MAX_NVS_STRING_SIZE];
static char fallback_stratum_user_buffer[MAX_NVS_STRING_SIZE];
static char board_version_buffer[MAX_NVS_STRING_SIZE];

static esp_err_t GET_system_info(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "application/json");

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    // Use safe string copying to pre-allocated buffers
    const char* ssid_temp = nvs_config_get_string(NVS_CONFIG_WIFI_SSID, CONFIG_ESP_WIFI_SSID);
    strncpy(ssid_buffer, ssid_temp, MAX_NVS_STRING_SIZE - 1);
    ssid_buffer[MAX_NVS_STRING_SIZE - 1] = '\0';
    free((char*)ssid_temp);

    const char* hostname_temp = nvs_config_get_string(NVS_CONFIG_HOSTNAME, CONFIG_LWIP_LOCAL_HOSTNAME);
    strncpy(hostname_buffer, hostname_temp, MAX_NVS_STRING_SIZE - 1);
    hostname_buffer[MAX_NVS_STRING_SIZE - 1] = '\0';
    free((char*)hostname_temp);

    const char* stratum_url_temp = nvs_config_get_string(NVS_CONFIG_STRATUM_URL, CONFIG_STRATUM_URL);
    strncpy(stratum_url_buffer, stratum_url_temp, MAX_NVS_STRING_SIZE - 1);
    stratum_url_buffer[MAX_NVS_STRING_SIZE - 1] = '\0';
    free((char*)stratum_url_temp);

    const char* fallback_stratum_url_temp = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_URL, CONFIG_FALLBACK_STRATUM_URL);
    strncpy(fallback_stratum_url_buffer, fallback_stratum_url_temp, MAX_NVS_STRING_SIZE - 1);
    fallback_stratum_url_buffer[MAX_NVS_STRING_SIZE - 1] = '\0';
    free((char*)fallback_stratum_url_temp);

    const char* stratum_user_temp = nvs_config_get_string(NVS_CONFIG_STRATUM_USER, CONFIG_STRATUM_USER);
    strncpy(stratum_user_buffer, stratum_user_temp, MAX_NVS_STRING_SIZE - 1);
    stratum_user_buffer[MAX_NVS_STRING_SIZE - 1] = '\0';
    free((char*)stratum_user_temp);

    const char* fallback_stratum_user_temp = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_USER, CONFIG_FALLBACK_STRATUM_USER);
    strncpy(fallback_stratum_user_buffer, fallback_stratum_user_temp, MAX_NVS_STRING_SIZE - 1);
    fallback_stratum_user_buffer[MAX_NVS_STRING_SIZE - 1] = '\0';
    free((char*)fallback_stratum_user_temp);

    const char* board_version_temp = nvs_config_get_string(NVS_CONFIG_BOARD_VERSION, "unknown");
    strncpy(board_version_buffer, board_version_temp, MAX_NVS_STRING_SIZE - 1);
    board_version_buffer[MAX_NVS_STRING_SIZE - 1] = '\0';
    free((char*)board_version_temp);

    uint8_t mac[6];
    char formattedMac[18];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(formattedMac, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        cJSON * root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "power", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.power);
    cJSON_AddNumberToObject(root, "voltage", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.voltage);
    cJSON_AddNumberToObject(root, "current", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.current);
    cJSON_AddNumberToObject(root, "temp", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.chip_temp_avg);
    cJSON_AddNumberToObject(root, "vrTemp", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.vr_temp);
    cJSON_AddNumberToObject(root, "hashRate", GLOBAL_STATE->SYSTEM_MODULE.current_hashrate);
    
    // Calculate expected hashrate based on current frequency, small core count, and ASIC count
    float expectedHashrate = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, CONFIG_ASIC_FREQUENCY) * ((GLOBAL_STATE->small_core_count * GLOBAL_STATE->asic_count) / 1000.0);
    cJSON_AddNumberToObject(root, "expectedHashrate", expectedHashrate);
    
    cJSON_AddStringToObject(root, "bestDiff", GLOBAL_STATE->SYSTEM_MODULE.best_diff_string);
    cJSON_AddStringToObject(root, "bestSessionDiff", GLOBAL_STATE->SYSTEM_MODULE.best_session_diff_string);
    cJSON_AddNumberToObject(root, "stratumDiff", GLOBAL_STATE->stratum_difficulty);

    cJSON_AddNumberToObject(root, "isUsingFallbackStratum", GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback);

    cJSON_AddNumberToObject(root, "freeHeap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "coreVoltage", nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, CONFIG_ASIC_VOLTAGE));
    cJSON_AddNumberToObject(root, "coreVoltageActual", VCORE_get_voltage_mv(GLOBAL_STATE));
    cJSON_AddNumberToObject(root, "frequency", nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, CONFIG_ASIC_FREQUENCY));
    cJSON_AddStringToObject(root, "ssid", ssid_buffer);
    cJSON_AddStringToObject(root, "macAddr", formattedMac);
    cJSON_AddStringToObject(root, "hostname", hostname_buffer);
    cJSON_AddStringToObject(root, "wifiStatus", GLOBAL_STATE->SYSTEM_MODULE.wifi_status);
    cJSON_AddNumberToObject(root, "sharesAccepted", GLOBAL_STATE->SYSTEM_MODULE.shares_accepted);
    cJSON_AddNumberToObject(root, "sharesRejected", GLOBAL_STATE->SYSTEM_MODULE.shares_rejected);
    cJSON_AddNumberToObject(root, "uptimeSeconds", (esp_timer_get_time() - GLOBAL_STATE->SYSTEM_MODULE.start_time) / 1000000);
    cJSON_AddNumberToObject(root, "asicCount", GLOBAL_STATE->asic_count);
    uint16_t small_core_count = 0;
    switch (GLOBAL_STATE->asic_model){
        case ASIC_BM1397:
            small_core_count = BM1397_SMALL_CORE_COUNT;
            break;
        case ASIC_BM1366:
            small_core_count = BM1366_SMALL_CORE_COUNT;
            break;
        case ASIC_BM1368:
            small_core_count = BM1368_SMALL_CORE_COUNT;
            break;
        case ASIC_BM1370:
            small_core_count = BM1370_SMALL_CORE_COUNT;
            break;
        case ASIC_UNKNOWN:
        default:
            small_core_count = -1;
            break;
    }
    cJSON_AddNumberToObject(root, "smallCoreCount", small_core_count);
    cJSON_AddStringToObject(root, "ASICModel", GLOBAL_STATE->asic_model_str);
    cJSON_AddStringToObject(root, "stratumURL", stratum_url_buffer);
    cJSON_AddStringToObject(root, "fallbackStratumURL", fallback_stratum_url_buffer);
    cJSON_AddNumberToObject(root, "stratumPort", nvs_config_get_u16(NVS_CONFIG_STRATUM_PORT, CONFIG_STRATUM_PORT));
    cJSON_AddNumberToObject(root, "fallbackStratumPort", nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_PORT, CONFIG_FALLBACK_STRATUM_PORT));
    cJSON_AddStringToObject(root, "stratumUser", stratum_user_buffer);
    cJSON_AddStringToObject(root, "fallbackStratumUser", fallback_stratum_user_buffer);

    cJSON_AddStringToObject(root, "version", esp_app_get_description()->version);
    cJSON_AddStringToObject(root, "idfVersion", esp_get_idf_version());
    cJSON_AddStringToObject(root, "boardVersion", board_version_buffer);
    cJSON_AddStringToObject(root, "runningPartition", esp_ota_get_running_partition()->label);

    cJSON_AddNumberToObject(root, "flipscreen", nvs_config_get_u16(NVS_CONFIG_FLIP_SCREEN, 1));
    cJSON_AddNumberToObject(root, "overheat_mode", nvs_config_get_u16(NVS_CONFIG_OVERHEAT_MODE, 0));
    cJSON_AddNumberToObject(root, "invertscreen", nvs_config_get_u16(NVS_CONFIG_INVERT_SCREEN, 0));

    cJSON_AddNumberToObject(root, "invertfanpolarity", nvs_config_get_u16(NVS_CONFIG_INVERT_FAN_POLARITY, 1));
    cJSON_AddNumberToObject(root, "autofanspeed", nvs_config_get_u16(NVS_CONFIG_AUTO_FAN_SPEED, 1));

    cJSON_AddNumberToObject(root, "fanspeed", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan_perc);
    cJSON_AddNumberToObject(root, "fanrpm", GLOBAL_STATE->POWER_MANAGEMENT_MODULE.fan_rpm);
    cJSON_AddNumberToObject(root, "autotune", nvs_config_get_u16(NVS_CONFIG_AUTOTUNE_FLAG, 1));
    const char* autotune_preset_temp = nvs_config_get_string(NVS_CONFIG_AUTOTUNE_PRESET, "");
    strncpy(nvs_string_buffer, autotune_preset_temp, MAX_NVS_STRING_SIZE - 1);
    nvs_string_buffer[MAX_NVS_STRING_SIZE - 1] = '\0';
    free((char*)autotune_preset_temp);
    cJSON_AddStringToObject(root, "autotunePreset", nvs_string_buffer);
    
    const char* serial_temp = nvs_config_get_string(NVS_CONFIG_SERIAL_NUMBER, "");
    strncpy(nvs_string_buffer, serial_temp, MAX_NVS_STRING_SIZE - 1);
    nvs_string_buffer[MAX_NVS_STRING_SIZE - 1] = '\0';
    free((char*)serial_temp);
    cJSON_AddStringToObject(root, "serialnumber", nvs_string_buffer);

    // Use pre-allocated buffer for JSON response
    char* sys_info = cJSON_PrintUnformatted(root);
    if (sys_info != NULL) {
        size_t sys_info_len = strlen(sys_info);
        if (sys_info_len < MAX_JSON_RESPONSE_SIZE) {
            strncpy(json_response_buffer, sys_info, MAX_JSON_RESPONSE_SIZE - 1);
            json_response_buffer[MAX_JSON_RESPONSE_SIZE - 1] = '\0';
            httpd_resp_sendstr(req, json_response_buffer);
        } else {
            ESP_LOGE(TAG, "JSON response too large: %d bytes", sys_info_len);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Response too large");
        }
        free(sys_info);  // Free the cJSON allocated string
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON creation failed");
    }
    
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t POST_WWW_update(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        ESP_LOGW(TAG, "Unauthorized WWW update attempt from client");
        dataBase_log_event("system", "warning", "Unauthorized WWW update attempt", NULL);
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)
    {
        ESP_LOGE(TAG, "WWW update attempted while in AP mode");
        dataBase_log_event("system", "error", "WWW update failed: Not allowed in AP mode", NULL);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Not allowed in AP mode");
        return ESP_OK;
    }

    int remaining = req->content_len;
    char* buf = ota_buffer;  // Use dedicated OTA buffer
    const int buf_size = MAX_OTA_BUFFER_SIZE;

    const esp_partition_t * www_partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "www");
    if (www_partition == NULL) {
        ESP_LOGE(TAG, "WWW partition not found during update");
        dataBase_log_event("system", "error", "WWW update failed: WWW partition not found", NULL);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "WWW partition not found");
        return ESP_OK;
    }

    // Don't attempt to write more than what can be stored in the partition
    if (remaining > www_partition->size) {
        ESP_LOGE(TAG, "WWW update file too large: %d bytes, partition size: %lu bytes", remaining, www_partition->size);
        dataBase_log_event("system", "error", "WWW update failed: File too large for partition", NULL);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File provided is too large for device");
        return ESP_OK;
    }

    // Erase the entire www partition before writing
    ESP_ERROR_CHECK(esp_partition_erase_range(www_partition, 0, www_partition->size));

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, MIN(remaining, buf_size));

        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        } else if (recv_len <= 0) {
            ESP_LOGE(TAG, "WWW update protocol error: recv_len=%d", recv_len);
            dataBase_log_event("system", "error", "WWW update failed: Protocol error during file receive", NULL);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Protocol Error");
            return ESP_OK;
        }

        if (esp_partition_write(www_partition, www_partition->size - remaining, (const void *) buf, recv_len) != ESP_OK) {
            ESP_LOGE(TAG, "WWW update write error: offset=%lu, length=%d", www_partition->size - remaining, recv_len);
            dataBase_log_event("system", "error", "WWW update failed: Write error to partition", NULL);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write Error");
            return ESP_OK;
        }

        remaining -= recv_len;
    }

    // Set content type to JSON
    httpd_resp_set_type(req, "application/json");

    // Create JSON response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "WWW update completed successfully");
    time_t now;
    time(&now);
    cJSON_AddNumberToObject(response, "timestamp", now);
    
    // Send JSON response
    const char *response_str = cJSON_Print(response);
    httpd_resp_sendstr(req, response_str);
    
    // Log the update event
    dataBase_log_event("system", "info", "WWW partition updated via API", response_str);
    
    // Cleanup
    free((char *)response_str);
    cJSON_Delete(response);
    
    return ESP_OK;
}

/*
 * Handle OTA file upload
 */
esp_err_t POST_OTA_update(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        ESP_LOGW(TAG, "Unauthorized OTA firmware update attempt from client");
        dataBase_log_event("system", "warning", "Unauthorized OTA firmware update attempt", NULL);
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)
    {
        ESP_LOGE(TAG, "OTA firmware update attempted while in AP mode");
        dataBase_log_event("system", "error", "OTA firmware update failed: Not allowed in AP mode", NULL);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Not allowed in AP mode");
        return ESP_OK;
    }
    
    esp_ota_handle_t ota_handle;
    int remaining = req->content_len;
    char* buf = ota_buffer;  // Use dedicated OTA buffer
    const int buf_size = MAX_OTA_BUFFER_SIZE;

    const esp_partition_t * ota_partition = esp_ota_get_next_update_partition(NULL);
    ESP_ERROR_CHECK(esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle));

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, MIN(remaining, buf_size));

        // Timeout Error: Just retry
        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;

            // Serious Error: Abort OTA
        } else if (recv_len <= 0) {
            ESP_LOGE(TAG, "OTA firmware update protocol error: recv_len=%d", recv_len);
            dataBase_log_event("system", "error", "OTA firmware update failed: Protocol error during file receive", NULL);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Protocol Error");
            return ESP_OK;
        }

        // Successful Upload: Flash firmware chunk
        if (esp_ota_write(ota_handle, (const void *) buf, recv_len) != ESP_OK) {
            ESP_LOGE(TAG, "OTA firmware update flash error: length=%d", recv_len);
            dataBase_log_event("system", "error", "OTA firmware update failed: Flash write error", NULL);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash Error");
            return ESP_OK;
        }

        remaining -= recv_len;
    }

    // Validate and switch to new OTA image and reboot
    if (esp_ota_end(ota_handle) != ESP_OK || esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
        ESP_LOGE(TAG, "OTA firmware update validation/activation error");
        dataBase_log_event("system", "error", "OTA firmware update failed: Validation or activation error", NULL);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Validation / Activation Error");
        return ESP_OK;
    }

    // Set content type to JSON
    httpd_resp_set_type(req, "application/json");

    // Create JSON response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Firmware update completed successfully, rebooting now");
    time_t now;
    time(&now);
    cJSON_AddNumberToObject(response, "timestamp", now);
    
    // Send JSON response
    const char *response_str = cJSON_Print(response);
    httpd_resp_sendstr(req, response_str);
    
    // Log the update event
    dataBase_log_event("system", "info", "Firmware OTA update completed via API", response_str);
    
    // Cleanup
    free((char *)response_str);
    cJSON_Delete(response);
    
    ESP_LOGI(TAG, "Restarting System because of Firmware update complete");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    esp_restart();

    return ESP_OK;
}

#define MAX_LOG_BUFFER_SIZE 1024  // Define maximum log message size
static char log_buffer[MAX_LOG_BUFFER_SIZE];  // Pre-allocated buffer for log messages

int log_to_queue(const char * format, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);

    // Format the string into the pre-allocated buffer
    int written = vsnprintf(log_buffer, MAX_LOG_BUFFER_SIZE - 1, format, args_copy);
    va_end(args_copy);

    if (written < 0) {
        return 0;  // Formatting error
    }

    // Ensure the log message ends with a newline
    size_t len = strlen(log_buffer);
    if (len > 0 && log_buffer[len - 1] != '\n') {
        if (len < MAX_LOG_BUFFER_SIZE - 2) {  // Check if we have space for \n\0
            log_buffer[len] = '\n';
            log_buffer[len + 1] = '\0';
            len++;
        }
    }

    // Print to standard output
    printf("%s", log_buffer);

    // Create a copy of the message for the queue
    char *queue_message = pvPortMalloc(len + 1);
    if (queue_message == NULL) {
        return 0;  // Memory allocation failed
    }
    strncpy(queue_message, log_buffer, len);
    queue_message[len] = '\0';

    if (xQueueSendToBack(log_queue, (void*)&queue_message, (TickType_t) 0) != pdPASS) {
        vPortFree(queue_message);
    }

    return 0;
}

#define MAX_WS_FRAME_SIZE 1024  // Define maximum WebSocket frame size
static uint8_t ws_frame_buffer[MAX_WS_FRAME_SIZE];  // Pre-allocated buffer for WebSocket frames

void send_log_to_websocket(char *message)
{
    if (message == NULL) {
        return;
    }

    // Prepare the WebSocket frame
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    
    // Copy message to pre-allocated buffer
    size_t msg_len = strlen(message);
    if (msg_len >= MAX_WS_FRAME_SIZE) {
        msg_len = MAX_WS_FRAME_SIZE - 1;  // Leave room for null terminator
    }
    memcpy(ws_frame_buffer, message, msg_len);
    ws_frame_buffer[msg_len] = '\0';
    
    ws_pkt.payload = ws_frame_buffer;
    ws_pkt.len = msg_len;
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    // Ensure server and fd are valid
    if (server != NULL && fd >= 0) {
        // Send the WebSocket frame asynchronously
        if (httpd_ws_send_frame_async(server, fd, &ws_pkt) != ESP_OK) {
            esp_log_set_vprintf(vprintf);
        }
    }

    // Free the message buffer
    vPortFree(message);
}

/*
 * This handler echos back the received ws data
 * and triggers an async send if certain message received
 */
esp_err_t echo_handler(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        fd = httpd_req_to_sockfd(req);
        esp_log_set_vprintf(log_to_queue);
        return ESP_OK;
    }
    return ESP_OK;
}

// HTTP Error (404) Handler - Redirects all requests to the root page
esp_err_t http_404_error_handler(httpd_req_t * req, httpd_err_code_t err)
{
    // Set status
    httpd_resp_set_status(req, "302 Temporary Redirect");
    // Redirect to the "/" root directory
    httpd_resp_set_hdr(req, "Location", "/");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Redirecting to root");
    return ESP_OK;
}

void websocket_log_handler()
{
    char *message;
    const TickType_t queue_timeout = pdMS_TO_TICKS(100);  // 100ms timeout

    while (true)
    {
        if (xQueueReceive(log_queue, &message, queue_timeout) != pdPASS) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }

        if (message == NULL) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }

        if (fd == -1) {
            vPortFree(message);
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        send_log_to_websocket(message);
    }
}

/* Handler for getting recent logs from database */
static esp_err_t GET_recent_logs(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "application/json");

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    // Parse query parameters for limit
    int limit = 50; // Default limit
    char query_buf[128];
    if (httpd_req_get_url_query_str(req, query_buf, sizeof(query_buf)) == ESP_OK) {
        char limit_str[16];
        if (httpd_query_key_value(query_buf, "limit", limit_str, sizeof(limit_str)) == ESP_OK) {
            int parsed_limit = atoi(limit_str);
            if (parsed_limit > 0 && parsed_limit <= 100) {
                limit = parsed_limit;
            }
        }
    }

    // Get logs from database
    cJSON* logs_json = NULL;
    esp_err_t ret = dataBase_get_recent_logs(limit, &logs_json);
    
    if (ret != ESP_OK || logs_json == NULL) {
        ESP_LOGW(TAG, "Failed to get logs from database, returning empty response");
        // Return empty logs response
        cJSON* empty_response = cJSON_CreateObject();
        cJSON_AddArrayToObject(empty_response, "events");
        cJSON_AddNumberToObject(empty_response, "count", 0);
        
        const char* response_str = cJSON_Print(empty_response);
        httpd_resp_sendstr(req, response_str);
        free((char*)response_str);
        cJSON_Delete(empty_response);
        return ESP_OK;
    }

    // Send the logs response
    const char* logs_str = cJSON_Print(logs_json);
    httpd_resp_sendstr(req, logs_str);
    free((char*)logs_str);
    cJSON_Delete(logs_json);
    
    return ESP_OK;
}

/* Handler for getting error logs from database */
static esp_err_t GET_error_logs(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "application/json");

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    // Parse query parameters for limit
    int limit = 0; // Default: return all errors
    char query_buf[128];
    if (httpd_req_get_url_query_str(req, query_buf, sizeof(query_buf)) == ESP_OK) {
        char limit_str[16];
        if (httpd_query_key_value(query_buf, "limit", limit_str, sizeof(limit_str)) == ESP_OK) {
            int parsed_limit = atoi(limit_str);
            if (parsed_limit > 0) {
                limit = parsed_limit;
            }
        }
    }

    // Get error logs from database
    cJSON* logs_json = NULL;
    esp_err_t ret = dataBase_get_error_logs(limit, &logs_json);
    
    if (ret != ESP_OK || logs_json == NULL) {
        ESP_LOGW(TAG, "Failed to get error logs from database, returning empty response");
        // Return empty error logs response
        cJSON* empty_response = cJSON_CreateObject();
        cJSON_AddArrayToObject(empty_response, "errors");
        cJSON_AddNumberToObject(empty_response, "count", 0);
        cJSON_AddNumberToObject(empty_response, "totalErrors", 0);
        cJSON_AddNumberToObject(empty_response, "lastError", 0);
        
        const char* response_str = cJSON_Print(empty_response);
        httpd_resp_sendstr(req, response_str);
        free((char*)response_str);
        cJSON_Delete(empty_response);
        return ESP_OK;
    }

    // Send the error logs response
    const char* logs_str = cJSON_Print(logs_json);
    httpd_resp_sendstr(req, logs_str);
    free((char*)logs_str);
    cJSON_Delete(logs_json);
    
    return ESP_OK;
}

/* Handler for getting critical logs from database */
static esp_err_t GET_critical_logs(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "application/json");

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    // Parse query parameters for limit
    int limit = 0; // Default: return all critical events
    char query_buf[128];
    if (httpd_req_get_url_query_str(req, query_buf, sizeof(query_buf)) == ESP_OK) {
        char limit_str[16];
        if (httpd_query_key_value(query_buf, "limit", limit_str, sizeof(limit_str)) == ESP_OK) {
            int parsed_limit = atoi(limit_str);
            if (parsed_limit > 0) {
                limit = parsed_limit;
            }
        }
    }

    // Get critical logs from database
    cJSON* logs_json = NULL;
    esp_err_t ret = dataBase_get_critical_logs(limit, &logs_json);
    
    if (ret != ESP_OK || logs_json == NULL) {
        ESP_LOGW(TAG, "Failed to get critical logs from database, returning empty response");
        // Return empty critical logs response
        cJSON* empty_response = cJSON_CreateObject();
        cJSON_AddArrayToObject(empty_response, "critical");
        cJSON_AddNumberToObject(empty_response, "count", 0);
        cJSON_AddNumberToObject(empty_response, "totalCritical", 0);
        cJSON_AddNumberToObject(empty_response, "lastCritical", 0);
        
        const char* response_str = cJSON_Print(empty_response);
        httpd_resp_sendstr(req, response_str);
        free((char*)response_str);
        cJSON_Delete(empty_response);
        return ESP_OK;
    }

    // Send the critical logs response
    const char* logs_str = cJSON_Print(logs_json);
    httpd_resp_sendstr(req, logs_str);
    free((char*)logs_str);
    cJSON_Delete(logs_json);
    
    return ESP_OK;
}

esp_err_t start_rest_server(void * pvParameters)
{
    GLOBAL_STATE = (GlobalState *) pvParameters;
    const char * base_path = "";

    bool enter_recovery = false;
    if (init_fs() != ESP_OK) {
        // Unable to initialize the web app filesystem.
        // Enter recovery mode
        enter_recovery = true;
    }
    
    // Initialize data partition database (independent of www partition)
    esp_err_t db_ret = dataBase_init();
    if (db_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize database system, continuing without database features");
    }

    REST_CHECK(base_path, "wrong base path", err);
    rest_server_context_t * rest_context = calloc(1, sizeof(rest_server_context_t));
    REST_CHECK(rest_context, "No memory for rest context", err);
    strlcpy(rest_context->base_path, base_path, sizeof(rest_context->base_path));

    log_queue = xQueueCreate(MESSAGE_QUEUE_SIZE, sizeof(char*));

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_open_sockets = 10;
    config.max_uri_handlers = 20;

    ESP_LOGI(TAG, "Starting HTTP Server");
    REST_CHECK(httpd_start(&server, &config) == ESP_OK, "Start server failed", err_start);

    httpd_uri_t recovery_explicit_get_uri = {
        .uri = "/recovery", 
        .method = HTTP_GET, 
        .handler = rest_recovery_handler, 
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &recovery_explicit_get_uri);
    
    // Register theme API endpoints
    ESP_ERROR_CHECK(register_theme_api_endpoints(server, rest_context));

    /* URI handler for fetching system info */
    httpd_uri_t system_info_get_uri = {
        .uri = "/api/system/info", 
        .method = HTTP_GET, 
        .handler = GET_system_info, 
        .user_ctx = rest_context
    };
    
    httpd_register_uri_handler(server, &system_info_get_uri);
    httpd_uri_t system_restart_uri = {
        .uri = "/api/system/restart", 
        .method = HTTP_POST, 
        .handler = POST_restart, 
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &system_restart_uri);
    httpd_uri_t system_restart_options_uri = {
        .uri = "/api/system/restart", 
        .method = HTTP_OPTIONS, 
        .handler = handle_options_request, 
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &system_restart_options_uri);

    httpd_uri_t update_system_settings_uri = {
        .uri = "/api/system", 
        .method = HTTP_PATCH, 
        .handler = PATCH_update_settings, 
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &update_system_settings_uri);

    httpd_uri_t system_options_uri = {
        .uri = "/api/system",
        .method = HTTP_OPTIONS,
        .handler = handle_options_request,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &system_options_uri);

     /* URI handler for WiFi scan */
    httpd_uri_t wifi_scan_get_uri = {
        .uri = "/api/system/wifi/scan",
        .method = HTTP_GET,
        .handler = GET_wifi_scan,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &wifi_scan_get_uri);

    httpd_uri_t update_post_ota_firmware = {
        .uri = "/api/system/OTA", 
        .method = HTTP_POST, 
        .handler = POST_OTA_update, 
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &update_post_ota_firmware);

    httpd_uri_t update_post_ota_www = {
        .uri = "/api/system/OTAWWW", 
        .method = HTTP_POST, 
        .handler = POST_WWW_update, 
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &update_post_ota_www);

    // This is for ACSOSv1.0.4 with wrong endpoint
    httpd_uri_t update_post_ota_www_alt = {
        .uri = "/api/system/upload-webapp", 
        .method = HTTP_POST, 
        .handler = POST_WWW_update, 
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &update_post_ota_www_alt);

    /* URI handler for fetching recent logs */
    httpd_uri_t logs_recent_get_uri = {
        .uri = "/api/logs/recent", 
        .method = HTTP_GET, 
        .handler = GET_recent_logs, 
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &logs_recent_get_uri);

    /* URI handler for fetching error logs */
    httpd_uri_t logs_errors_get_uri = {
        .uri = "/api/logs/error", 
        .method = HTTP_GET, 
        .handler = GET_error_logs, 
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &logs_errors_get_uri);

    /* URI handler for fetching critical logs */
    httpd_uri_t logs_critical_get_uri = {
        .uri = "/api/logs/critical", 
        .method = HTTP_GET, 
        .handler = GET_critical_logs, 
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &logs_critical_get_uri);

    httpd_uri_t ws = {
        .uri = "/api/ws", 
        .method = HTTP_GET, 
        .handler = echo_handler, 
        .user_ctx = NULL, 
        .is_websocket = true
    };
    httpd_register_uri_handler(server, &ws);


    if (enter_recovery) {
        /* Make default route serve Recovery */
        httpd_uri_t recovery_implicit_get_uri = {
            .uri = "/*", .method = HTTP_GET, 
            .handler = rest_recovery_handler, 
            .user_ctx = rest_context
        };
        httpd_register_uri_handler(server, &recovery_implicit_get_uri);

    } else {
        /* URI handler for getting web server files */
        httpd_uri_t common_get_uri = {
            .uri = "/*", 
            .method = HTTP_GET, 
            .handler = rest_common_get_handler, 
            .user_ctx = rest_context
        };
        httpd_register_uri_handler(server, &common_get_uri);
    }

    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);

    // Start websocket log handler thread
    xTaskCreate(&websocket_log_handler, "websocket_log_handler", 4096, NULL, 2, NULL);

    // Start the DNS server that will redirect all queries to the softAP IP
    dns_server_config_t dns_config = DNS_SERVER_CONFIG_SINGLE("*" /* all A queries */, "WIFI_AP_DEF" /* softAP netif ID */);
    start_dns_server(&dns_config);

    return ESP_OK;
err_start:
    free(rest_context);
err:
    return ESP_FAIL;
}
