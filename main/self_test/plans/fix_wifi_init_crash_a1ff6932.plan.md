---
name: Fix WiFi Init Crash
overview: "Rewrite `self_test_wifi_init()` to follow the reference WiFi code pattern: create both netifs upfront before `esp_wifi_init()`, always set APSTA mode, and stop calling `esp_wifi_get_mac()` before `esp_wifi_start()`."
todos:
  - id: fix-wifi-init
    content: "Rewrite self_test_wifi.c: create both netifs upfront, always APSTA, move MAC read after esp_wifi_start()"
    status: pending
isProject: false
---

# Fix Self-Test WiFi Init Crash

## Root Causes

The current [self_test_wifi.c](main/self_test/self_test_wifi.c) has three ordering bugs compared to the reference pattern in [WIFI Code.md](main/self_test/plans/WIFI%20Code.md):

1. **Netifs created lazily and conditionally** -- `st_init_softap()` calls `esp_netif_create_default_wifi_ap()` and `st_init_sta()` conditionally calls `esp_netif_create_default_wifi_sta()`. The reference code creates **both** netifs upfront before `esp_wifi_init()` to prevent netif conflicts.

2. **`esp_wifi_get_mac()` called before `esp_wifi_start()`** -- In `st_init_softap()` (line 66), `esp_wifi_get_mac(ESP_IF_WIFI_AP, mac)` is called before the WiFi driver is started. This can crash because the MAC is not available until after `esp_wifi_start()`.

3. **WiFi mode set conditionally** -- The current code sets `WIFI_MODE_AP` or `WIFI_MODE_APSTA` depending on STA config. The reference always sets `WIFI_MODE_APSTA` from the start to avoid mode-switching issues.

## Fix (single file change)

Rewrite [self_test_wifi.c](main/self_test/self_test_wifi.c) `self_test_wifi_init()` to follow the reference init ordering:

```
1. esp_netif_init()
2. esp_event_loop_create_default()
3. Create BOTH netifs upfront (STA + AP)
4. esp_wifi_init(&cfg)
5. esp_wifi_set_mode(WIFI_MODE_APSTA)  -- always APSTA
6. Register event handlers
7. Configure AP (ssid, auth, etc.) -- no netif creation
8. Configure STA if credentials exist -- no netif creation
9. esp_wifi_start()
10. esp_wifi_set_ps(WIFI_PS_NONE)
11. Generate SSID using esp_wifi_get_mac() -- now safe, WiFi is started
12. Wait for STA connection (non-blocking timeout)
```

Key changes in the helper functions:

- `st_init_softap()` -- remove `esp_netif_create_default_wifi_ap()` call and `esp_wifi_get_mac()` call. Just configure the AP wifi_config_t. SSID generation moves to after `esp_wifi_start()`.
- `st_init_sta()` -- remove `esp_netif_create_default_wifi_sta()` call. Just configure the STA wifi_config_t.
- Store both netif handles in static variables (like the reference `wifi_status.sta_netif` / `wifi_status.ap_netif`).