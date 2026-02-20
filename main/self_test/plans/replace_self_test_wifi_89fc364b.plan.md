---
name: Replace self_test WiFi
overview: Strip out the custom WiFi implementation in self_test and replace it with the existing connect.c component. Keep the self_test HTTP server and embedded HTML page unchanged.
todos:
  - id: delete-wifi-files
    content: Delete self_test_wifi.c and self_test_wifi.h
    status: pending
  - id: update-cmake
    content: Remove self_test_wifi.c from main/CMakeLists.txt SRCS
    status: pending
  - id: update-self-test
    content: "Update self_test.c: replace self_test_wifi_init() with wifi_init() from connect.c"
    status: pending
  - id: update-docs
    content: Update documentation to reflect WiFi is now handled by connect component
    status: pending
isProject: false
---

# Replace self_test WiFi with connect.c

## Problem

`self_test_wifi.c` (199 lines) duplicates all the WiFi init logic that `components/connect/connect.c` already provides -- event handlers, AP/STA setup, event groups, netif creation, etc. It's a separate, worse copy of the same thing.

## Approach

Use `wifi_init()` / `wifi_connect()` / `generate_ssid()` from `connect.c` instead. The self_test HTTP server (`self_test_server.c`) and embedded HTML page (`self_test_page.h`) stay completely unchanged.

The flow in `self_test()` mirrors normal boot (`main.c` lines 87-101):

1. `wifi_init()` -- brings up AP + STA, starts WiFi
2. `self_test_server_start()` -- start HTTP server so config page is accessible via AP at 192.168.4.1
3. `wifi_connect()` -- blocks until STA connects (connect.c retries forever)
4. Run test sequence

If STA has no credentials or can't connect, `wifi_connect()` blocks forever while the AP stays up -- the user can access the self_test config page at 192.168.4.1 to set WiFi credentials and restart. This is identical to normal boot behavior.

## Changes

### 1. Delete `self_test_wifi.c` and `self_test_wifi.h`

- Delete [main/self_test/self_test_wifi.c](main/self_test/self_test_wifi.c)
- Delete [main/self_test/self_test_wifi.h](main/self_test/self_test_wifi.h)

### 2. Remove from build: [main/CMakeLists.txt](main/CMakeLists.txt)

Remove the `./self_test/self_test_wifi.c` entry from the SRCS list (line 28).

### 3. Update [main/self_test/self_test.c](main/self_test/self_test.c)

In the includes:

- Remove `#include "self_test_wifi.h"` (line 39)
- Add `#include "connect.h"` and `#include "nvs_config.h"` (nvs_config.h is already included)

Rewrite the WiFi + server + test orchestration in `self_test()` (lines 464-479). The new flow:

```c
/* WiFi init -- AP + STA, same as normal boot */
char *wifi_ssid = nvs_config_get_string(NVS_CONFIG_WIFI_SSID, WIFI_SSID);
char *wifi_pass = nvs_config_get_string(NVS_CONFIG_WIFI_PASS, WIFI_PASS);
char *hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME, HOSTNAME);

wifi_init(wifi_ssid, wifi_pass, hostname,
          GLOBAL_STATE->SYSTEM_MODULE.ip_addr_str);
generate_ssid(GLOBAL_STATE->SYSTEM_MODULE.ap_ssid);

free(wifi_ssid);
free(wifi_pass);
free(hostname);

/* Start HTTP server before blocking on STA -- config page
 * is accessible via AP at 192.168.4.1 while waiting */
if (self_test_server_start(GLOBAL_STATE) != ESP_OK)
        ESP_LOGE(TAG, "Server start failed");

/* Block until STA connects (retries forever, AP stays up) */
wifi_connect();

/* STA connected -- run tests */
run_test_sequence(GLOBAL_STATE);
```

### 4. Documentation

- Update or remove [docs/modules/](docs/) entries referencing self_test_wifi if they exist
- Update self_test module docs to note WiFi is now handled by the connect component

## What stays the same

- `self_test_server.c` -- no changes, still serves the embedded HTML page
- `self_test_page.h` -- no changes
- `self_test.h` -- no changes needed (it doesn't reference WiFi)
- `self_test_server.h` -- no changes
- All test logic in `self_test.c` -- unchanged