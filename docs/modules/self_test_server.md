# Self Test Server Module

## Purpose

The self-test server module adds HTTP and WebSocket connectivity to the
hardware self-test flow.  When the device enters self-test mode it brings
up WiFi (SoftAP always, STA optional) and starts a lightweight web server
that serves an embedded HTML dashboard and pushes real-time test status
via WebSocket.

Key goals:

- Allow production technicians to monitor test progress from a browser
- Enable triggering re-runs without physically resetting the device
- Report hashrate, temperature, voltage, and per-test pass/fail results

## Module Interface

### Public Functions

- `self_test_server_start(GlobalState *)` - Start HTTP server on port 80
- `self_test_server_notify_status(test, state, detail)` - Push test status via WS
- `self_test_server_notify_metrics(hashrate, temp, voltage)` - Push live metrics via WS
- `self_test_server_notify_done(passed)` - Push test-complete message via WS
- `run_test_sequence(GlobalState *)` - Execute the two-phase hardware test sequence

### HTTP Endpoints

| Method | Path                | Description                          |
|--------|---------------------|--------------------------------------|
| GET    | `/`                 | Serve embedded HTML dashboard        |
| GET    | `/api/test/status`  | JSON snapshot of current test state  |
| POST   | `/api/test/start`   | Trigger a new test run (409 if busy) |
| POST   | `/api/system/restart` | Restart the ESP32                  |
| GET    | `/ws`               | WebSocket endpoint for live updates  |

### Data Structures

- `SelfTestResult` - Per-test result with name, pass/fail, and detail
- `SelfTestModule` - Extended module state including result array, hashrate,
  temperature, and core voltage

### Dependencies

- `esp_http_server` - HTTP/WebSocket server framework
- `cJSON` - JSON serialisation for API responses
- `connect` component - WiFi init/connect and SSID generation
- `nvs_config` - NVS credential retrieval for STA mode
- All hardware test dependencies from the existing self_test module

## Implementation Details

### Source Files

- `main/self_test/self_test_server.c` - HTTP/WS server
- `main/self_test/self_test_server.h` - Server public API
- `main/self_test/self_test_page.h` - Embedded HTML page
- `main/self_test/self_test.c` - Test sequence and WiFi orchestration
- `main/self_test/self_test.h` - Test sequence public API
- `components/connect/connect.c` - WiFi init/connect (shared with normal boot)

### Variables and Data Sources

- `st_server` (static) - httpd handle for the self-test server
- `ws_fd` (static) - file descriptor of the connected WebSocket client
- `st_global` (static) - pointer to GlobalState passed during init
- `BootSemaphore` (static) - binary semaphore for reset button handling

### WebSocket Message Format

Messages are JSON objects with a `type` field:

```json
{"type":"status","test":"PSRAM","state":"running","detail":"Testing..."}
{"type":"result","test":"PSRAM","state":"pass","detail":"OK"}
{"type":"metrics","hashrate":850.0,"temperature":42.5,"core_voltage":1150}
{"type":"done","passed":true}
```

### Two-Phase Test Architecture

`run_test_sequence()` splits hardware validation into two phases:

**Phase 1 -- Init tests** (run once per power cycle):
- PSRAM, peripherals (EMC2101/INA260), voltage regulator (full
  `VCORE_init`), core voltage ADC, temperature continuity
- On success, `init_complete` is set and `init_result_count` records
  how many results belong to the init phase

**Phase 2 -- ASIC tests** (run on every call):
- `power_on_asic()` re-enables ASIC power via GPIO and
  `VCORE_set_voltage()` without calling `VCORE_init()`/`TPS546_init()`
- ASIC chip detection and hashrate measurement

On re-run (`POST /api/test/start`), only ASIC-phase results are
cleared; init-phase results are preserved in the results array.

### Power Safety

1. `tests_done()` always calls `power_off_asic()` which:
   - Zeroes TPS546 output voltage
   - Sets `GPIO_ASIC_ENABLE` high to disable ASIC power (MAX/ULTRA/SUPRA)
2. Job buffers are freed with NULL guards in `cleanup_job_buffers()`
3. `POST /api/test/start` returns HTTP 409 if `SELF_TEST_MODULE.active`
   is true, preventing overlapping runs
4. Re-runs call `power_on_asic()` which only sets voltage (no VR
   reinitialisation), so the TPS546 configuration from init is reused

## Integration

### Boot Flow

```
app_main()
  -> should_test() / button press
  -> self_test()
     -> wifi_init()              // bring up AP + STA (connect component)
     -> self_test_server_start() // start HTTP/WS on port 80
     -> wifi_connect()           // block until STA connects
     -> run_test_sequence()      // first automatic run
     -> while(1) sleep           // keep server alive for re-runs
```

### WiFi Modes

WiFi is handled by the shared `connect` component (`components/connect/connect.c`),
the same code used during normal boot.

- **SoftAP** (always): Open network named `Bitaxe_XXXX` where XXXX is
  derived from the device MAC address, accessible at `192.168.4.1`
- **STA** (required): `wifi_connect()` blocks until the STA connects to
  the configured SSID. If no credentials are set, the AP remains
  available for the user to configure WiFi via the self-test page and
  restart.

### Browser Interaction

The embedded HTML page connects via WebSocket and accumulates test
results in a browser-session-local JavaScript object.  The table updates
in real time as WebSocket messages arrive.  The "Start Test" button
triggers `POST /api/test/start` and disables itself until a `done`
message is received.
