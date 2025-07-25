#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

// #include "protocol_examples_common.h"
#include "main.h"

#include "asic_result_task.h"
#include "asic_task.h"
#include "create_jobs_task.h"
#include "esp_netif.h"
#include "system.h"
#include "http_server.h"
#include "nvs_config.h"
#include "serial.h"
#include "stratum_task.h"
#include "i2c_bitaxe.h"
#include "adc.h"
#include "nvs_device.h"
#include "self_test.h"
#include "lvglDisplayBAP.h"
#include "serial.h"
#include "theme_api.h"
#include "dataBase.h"
#include "power_management_task.h"

static GlobalState GLOBAL_STATE = {
    .extranonce_str = NULL, 
    .extranonce_2_len = 0, 
    .abandon_work = 0, 
    .version_mask = 0,
    .ASIC_initalized = false
};

static const char * TAG = "bitaxe";

void app_main(void)
{
    ESP_LOGI(TAG, "Welcome to the bitaxe - hack the planet!");

    // Init I2C
    ESP_ERROR_CHECK(i2c_bitaxe_init());
    ESP_LOGI(TAG, "I2C initialized successfully");

    //wait for I2C to init
    vTaskDelay(100 / portTICK_PERIOD_MS);

    //Init ADC
    ADC_init();

    //initialize the ESP32 NVS
    if (NVSDevice_init() != ESP_OK){
        ESP_LOGE(TAG, "Failed to init NVS");
        return;
    }

    //parse the NVS config into GLOBAL_STATE
    if (NVSDevice_parse_config(&GLOBAL_STATE) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse NVS config");
        return;
    }

    // Optionally hold the boot button
    bool pressed = gpio_get_level(CONFIG_GPIO_BUTTON_BOOT) == 0; // LOW when pressed
    //should we run the self test?
    if (should_test(&GLOBAL_STATE) || pressed) {
        self_test((void *) &GLOBAL_STATE);
        return;
    }

    SYSTEM_init_system(&GLOBAL_STATE);

    initializeTheme(THEME_ACS_DEFAULT);
    ESP_LOGI(TAG, "Theme initialized");
    ESP_LOGI(TAG, "Current theme preset: %d", getCurrentThemePreset());

    // pull the wifi credentials and hostname out of NVS
    char * wifi_ssid = nvs_config_get_string(NVS_CONFIG_WIFI_SSID, WIFI_SSID);
    char * wifi_pass = nvs_config_get_string(NVS_CONFIG_WIFI_PASS, WIFI_PASS);
    char * hostname  = nvs_config_get_string(NVS_CONFIG_HOSTNAME, HOSTNAME);

    // copy the wifi ssid to the global state
    strncpy(GLOBAL_STATE.SYSTEM_MODULE.ssid, wifi_ssid, sizeof(GLOBAL_STATE.SYSTEM_MODULE.ssid));
    GLOBAL_STATE.SYSTEM_MODULE.ssid[sizeof(GLOBAL_STATE.SYSTEM_MODULE.ssid)-1] = 0;

    // init and connect to wifi
    wifi_init(wifi_ssid, wifi_pass, hostname, GLOBAL_STATE.SYSTEM_MODULE.ip_addr_str);

    generate_ssid(GLOBAL_STATE.SYSTEM_MODULE.ap_ssid);

    SYSTEM_init_peripherals(&GLOBAL_STATE);

   
    
    xTaskCreate(SYSTEM_task, "SYSTEM_task", 4096, (void *) &GLOBAL_STATE, 3, NULL);
    xTaskCreate(POWER_MANAGEMENT_task, "power management", 8192, (void *) &GLOBAL_STATE, 10, NULL);

    //start the API for AxeOS
    start_rest_server((void *) &GLOBAL_STATE);
    
    EventBits_t result_bits = wifi_connect();


    if (result_bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to SSID: %s", wifi_ssid);
        strncpy(GLOBAL_STATE.SYSTEM_MODULE.wifi_status, "Connected!", 20);
        
        // Log WiFi connection event
        char wifi_data[128];
        snprintf(wifi_data, sizeof(wifi_data), 
                 "{\"ipAddress\":\"%s\",\"ssid\":\"%s\"}", 
                 GLOBAL_STATE.SYSTEM_MODULE.ip_addr_str, wifi_ssid);
        dataBase_log_event("network", "info", "WiFi connection established", wifi_data);
    } else if (result_bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID: %s", wifi_ssid);

        strncpy(GLOBAL_STATE.SYSTEM_MODULE.wifi_status, "Failed to connect", 20);
        // User might be trying to configure with AP, just chill here
        ESP_LOGI(TAG, "Finished, waiting for user input.");
        while (1) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            
        }
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
        strncpy(GLOBAL_STATE.SYSTEM_MODULE.wifi_status, "unexpected error", 20);
        // User might be trying to configure with AP, just chill here
        ESP_LOGI(TAG, "Finished, waiting for user input.");
        while (1) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);

        }
    }

    free(wifi_ssid);
    free(wifi_pass);
    free(hostname);
    GLOBAL_STATE.SYSTEM_MODULE.startup_done = true;
    GLOBAL_STATE.new_stratum_version_rolling_msg = false;


    if (GLOBAL_STATE.SYSTEM_MODULE.overheat_mode) {
        ESP_LOGI(TAG, "Device is in overheat mode. Resetting to balanced preset and clearing overheat mode flag.");
        
        // Reset to balanced preset for safe operation
        if (apply_preset(GLOBAL_STATE.device_model, "balanced")) {
            ESP_LOGI(TAG, "Successfully applied balanced preset for overheat recovery");
        } else {
            ESP_LOGE(TAG, "Failed to apply balanced preset, using safe defaults");
            nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, 1100);
            nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, 400);
            nvs_config_set_u16(NVS_CONFIG_FAN_SPEED, 75);
            nvs_config_set_u16(NVS_CONFIG_AUTO_FAN_SPEED, 1);
            nvs_config_set_u16(NVS_CONFIG_AUTOTUNE_FLAG, 1);
            nvs_config_set_string(NVS_CONFIG_AUTOTUNE_PRESET, "balanced");
        }
        
        // Clear overheat mode flag for normal operation
        nvs_config_set_u16(NVS_CONFIG_OVERHEAT_MODE, 0);
        GLOBAL_STATE.SYSTEM_MODULE.overheat_mode = 0;
        
        // Log the recovery event
        char recovery_data[128];
        snprintf(recovery_data, sizeof(recovery_data), 
                 "{\"resetToPreset\":\"balanced\",\"deviceModel\":%d}", 
                 GLOBAL_STATE.device_model);
        dataBase_log_event("power", "info", "Startup overheat mode reset - applied balanced preset", recovery_data);
    }

    if (GLOBAL_STATE.ASIC_functions.init_fn != NULL && !GLOBAL_STATE.SYSTEM_MODULE.overheat_mode) {
        wifi_softap_off();

        queue_init(&GLOBAL_STATE.stratum_queue);
        queue_init(&GLOBAL_STATE.ASIC_jobs_queue);

        SERIAL_init();
        (*GLOBAL_STATE.ASIC_functions.init_fn)(GLOBAL_STATE.POWER_MANAGEMENT_MODULE.frequency_value, GLOBAL_STATE.asic_count);
        SERIAL_set_baud((*GLOBAL_STATE.ASIC_functions.set_max_baud_fn)());
        SERIAL_clear_buffer();

        GLOBAL_STATE.ASIC_initalized = true;

        xTaskCreate(stratum_task, "stratum admin", 8192, (void *) &GLOBAL_STATE, 5, NULL);
        xTaskCreate(create_jobs_task, "stratum miner", 8192, (void *) &GLOBAL_STATE, 10, NULL);
        xTaskCreate(ASIC_task, "asic", 8192, (void *) &GLOBAL_STATE, 10, NULL);
        xTaskCreate(ASIC_result_task, "asic result", 8192, (void *) &GLOBAL_STATE, 15, NULL);
    }
}

void MINER_set_wifi_status(wifi_status_t status, int retry_count, int reason)
{
    switch(status) {
        case WIFI_CONNECTING:
            snprintf(GLOBAL_STATE.SYSTEM_MODULE.wifi_status, 20, "Connecting...");
            return;
        case WIFI_CONNECTED:
            snprintf(GLOBAL_STATE.SYSTEM_MODULE.wifi_status, 20, "Connected!");
            return;
        case WIFI_RETRYING:
            // See https://github.com/espressif/esp-idf/blob/master/components/esp_wifi/include/esp_wifi_types_generic.h for codes
            switch(reason) {
                case 201:
                    snprintf(GLOBAL_STATE.SYSTEM_MODULE.wifi_status, 20, "No AP found (%d)", retry_count);
                    return;
                case 15:
                case 205:
                    snprintf(GLOBAL_STATE.SYSTEM_MODULE.wifi_status, 20, "Password error (%d)", retry_count);
                    return;
                default:
                    snprintf(GLOBAL_STATE.SYSTEM_MODULE.wifi_status, 20, "Error %d (%d)", reason, retry_count);
                    return;
            }
    }
    ESP_LOGW(TAG, "Unknown status: %d", status);
}

void MINER_set_ap_status(bool enabled) {
    GLOBAL_STATE.SYSTEM_MODULE.ap_enabled = enabled;
}
