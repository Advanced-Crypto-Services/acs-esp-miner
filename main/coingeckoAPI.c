#include "coingeckoAPI.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "cJSON.h"

static char *response_buffer = NULL;  // Buffer para acumular la respuesta completa
static int response_length = 0; 

const char *COINGECKO_API_URL = "https://api.coingecko.com/api/v3/";
const char * API_AUTH = "x_cg_demo_api_key=CG-iUyVPGbu2nECCwVo8yXXUf57";
const char * Ping = "ping?";
esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGE("HTTP API", "HTTP API request failed");
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGI("HTTP API", "HTTP API data received");
        if (!esp_http_client_is_chunked_response(evt->client))
        {
            // Allocate memory for the response buffer
            char *new_buffer = realloc(response_buffer, response_length + evt->data_len + 1);
            if (new_buffer == NULL)
            {
                ESP_LOGE("HTTP API", "Failed to allocate memory for response buffer");
                return ESP_ERR_NO_MEM;
            }
                response_buffer = new_buffer;
                memcpy(response_buffer + response_length, (char*)evt->data, evt->data_len);
            response_length += evt->data_len;
            response_buffer[response_length] = '\0';  // Ensure the buffer is a valid string
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        // Try to parse the complete JSON at the end of the transmission
        //ESP_LOGI(tag, "Final JSON received: %s", response_buffer);
        
        cJSON *json = cJSON_Parse(response_buffer);
        ESP_LOGI("HTTP API", "Final JSON received: %s", response_buffer);
        if (json != NULL)
        {
            cJSON *Ping = cJSON_GetObjectItem(json, "gecko_says");
            if (Ping != NULL) 
            {
                if (cJSON_IsString(Ping))
                {
                    ESP_LOGI("HTTP API", "Ping response: %s", Ping->valuestring);
                }
            }
            cJSON_Delete(json);
        } else {
                ESP_LOGE("HTTP API", "Failed to parse JSON");
            }
            // Liberar el buffer después de procesar
            free(response_buffer);
            response_buffer = NULL;
            response_length = 0;
            break;
    default:
        ESP_LOGI("HTTP API", "HTTP API event id: %d", evt->event_id);
        break;
    }
    return ESP_OK;
}

esp_err_t coingecko_api_ping(void)
{
    static int64_t last_update = 0;  // Make static to persist between calls
    int64_t current_time = esp_timer_get_time();
    
    if (((current_time - last_update)/1000000) > 30 || last_update == 0)
    {
        last_update = current_time;
        esp_http_client_config_t config = {
            .url = "https://api.coingecko.com/api/v3/ping?x_cg_demo_api_key=CG-iUyVPGbu2nECCwVo8yXXUf57",
            .event_handler = http_event_handler,
        };

        // Initialize and perform the request
        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_err_t err = esp_http_client_perform(client);
        
        if (err == ESP_OK) {
            ESP_LOGI("HTTP API", "HTTP GET Status = %d", esp_http_client_get_status_code(client));
        } else {
            ESP_LOGE("HTTP API", "HTTP GET request failed: %s", esp_err_to_name(err));
        }
        
        esp_http_client_cleanup(client);
        return err;
    }
    
    return ESP_OK;  // Return OK if we're skipping due to time
}

