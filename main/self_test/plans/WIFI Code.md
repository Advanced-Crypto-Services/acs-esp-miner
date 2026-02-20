
### STATE MANAGEMENT
```C
/**
 * struct wifi_status - WiFi connection status structure
 * @state: Current WiFi state from WIFI_STATE enum
 * @initialized: true if WiFi subsystem initialized
 * @current_mode: Current WiFi mode (APSTA, STA, or NULL)
 * @sta_netif: Network interface handle for STA mode
 * @ap_netif: Network interface handle for AP mode
 * @is_station_connected: true if STA mode connected to external AP
 * @station_ssid: SSID of network we're connected to (STA mode)
 * @sta_reconnect_attempts: Current number of reconnect attempts
 * @sta_max_reconnect_attempts: Maximum reconnect attempts before fallback
 * @sta_reconnect_enabled: Whether automatic STA reconnection is enabled
 * @is_ws_connected: true if WebSocket client connected
 * @ws_connections: Number of active WebSocket connections
 * @is_ap_client_connected: true if client connected to our SoftAP
 * @ap_client_connections: Number of clients connected to our AP
 * @last_error_message: Last WiFi error message for debugging
 */
struct wifi_status {
	enum WIFI_STATE state;
	bool initialized;
	wifi_mode_t current_mode;
	esp_netif_t *sta_netif;
	esp_netif_t *ap_netif;
	bool is_station_connected;
	char station_ssid[64];
	uint32_t sta_reconnect_attempts;
	uint32_t sta_max_reconnect_attempts;
	bool sta_reconnect_enabled;
	bool is_ws_connected;
	uint32_t ws_connections;
	bool is_ap_client_connected;
	uint32_t ap_client_connections;
	char last_error_message[256];
};
```

## INIT
```C 
esp_err_t wifi_init(void)
{
	if (wifi_status.initialized) {
		ESP_LOGW(TAG, "WIFI already initialized");
		return ESP_ERR_INVALID_STATE;
	}
	ESP_LOGI(TAG, "Initializing WIFI);
	
	memset(&wifi_status, 0, sizeof(wifi_status));
	wifi_status.state = WIFI_OFF;
	wifi_status.current_mode = WIFI_MODE_NULL;

	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	/* Create BOTH netifs upfront - this prevents netif conflicts */
	wifi_status.sta_netif = esp_netif_create_default_wifi_sta();
	wifi_status.ap_netif = esp_netif_create_default_wifi_ap();
	if (wifi_status.sta_netif == NULL || wifi_status.ap_netif == NULL) {
		ESP_LOGE(TAG, "Failed to create network interfaces");
		if (wifi_status.sta_netif) esp_netif_destroy(wifi_status.sta_netif);
		if (wifi_status.ap_netif) esp_netif_destroy(wifi_status.ap_netif);
		strncpy(wifi_status.last_error_message,
			"Failed to create network interfaces",
			sizeof(wifi_status.last_error_message) - 1);
		return ESP_FAIL;
	}

	/* Initialize WiFi with default config */
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	/* Set mode to APSTA from the start */
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
	/* Initialize reconnect parameters */
	wifi_status.sta_reconnect_enabled = true;
	wifi_status.sta_max_reconnect_attempts = MAX_STA_RECONNECT_ATTEMPTS;
	wifi_status.sta_reconnect_attempts = 0;
	/* Create event group for STA connection synchronization */
	wifi_event_group = xEventGroupCreate();
	if (wifi_event_group == NULL) {
		ESP_LOGE(TAG, "Failed to create WiFi event group");
		strncpy(wifi_status.last_error_message,
			"Failed to create event group",
			sizeof(wifi_status.last_error_message) - 1);
		return ESP_FAIL;
	}

	/* Register event handlers */
	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
						   &wifi_event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
						   &wifi_event_handler, NULL));

	/* Create reconnection task */
	BaseType_t task_ret = xTaskCreate(
		wifi_reconnect_task,
		"wifi_reconnect",
		WIFI_RECONNECT_TASK_STACK_SIZE,
		NULL,
		WIFI_RECONNECT_TASK_PRIORITY,
		&reconnect_task_handle
	);
	
	if (task_ret != pdPASS) {
		ESP_LOGE(TAG, "Failed to create reconnect task");
		strncpy(wifi_status.last_error_message,
			"Failed to create reconnect task",
			sizeof(wifi_status.last_error_message) - 1);
		return ESP_FAIL;
	}

	/* Turn Power Saving Off */
	esp_wifi_set_ps(WIFI_PS_NONE);
	wifi_ps_type_t ps;
	esp_wifi_get_ps(&ps);
	ESP_LOGI(TAG, "WiFi power save mode: %d", ps);


	wifi_status.initialized = true;

	/* Check NVS for stored credentials */
	struct network_config stored_config;
	esp_err_t nvs_ret = nvs_manager_load_network_config(&stored_config);
	
	if (nvs_ret == ESP_OK && stored_config.configured) {
		ESP_LOGI(TAG, "Found stored credentials for: %s", stored_config.ssid);
		
		/* Attempt STA connection */
		esp_err_t sta_ret = wifi_start_sta_mode(stored_config.ssid, 
							stored_config.password);
		
		if (sta_ret == ESP_OK) {
			ESP_LOGI(TAG, "Successfully connected to stored network");
			return ESP_OK;
		} else {
			ESP_LOGW(TAG, "Failed to connect to stored network, falling back to AP mode");
			/* Fall through to start AP */
		}
	} else {
		ESP_LOGI(TAG, "No stored credentials found");
	}
	
	/* Start AP mode (no credentials or STA failed) */
	return wifi_start_ap_mode();
}
```

### AP+STA

### STAT