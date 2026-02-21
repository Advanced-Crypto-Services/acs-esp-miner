# Self Test Module

Production hardware validation for Bitaxe devices. Runs diagnostic tests
and exposes an HTTP/WebSocket server for remote monitoring and control.

## Connecting

The device starts a WiFi SoftAP named `Bitaxe_XXXX` (derived from the
MAC address). Connect to that network and access the dashboard at
`http://192.168.4.1`. If STA WiFi credentials are configured, the
device also joins that network and is reachable via its IP or hostname.

## HTTP API

All endpoints are on port 80.

### Start a test run

```
POST /api/test/start
```

No body required. Returns `{"status":"started"}` on success, or
HTTP 409 with `{"error":"test running"}` if a test is already in
progress.

On the first run, both init tests (PSRAM, peripherals, voltage
regulator, core voltage, temperature) and ASIC tests (power on, chip
detection, hashrate) are executed. On subsequent runs, only the ASIC
tests are re-run -- init results are preserved.

### Get current test status

```
GET /api/test/status
```

Returns a JSON snapshot:

```json
{
  "active": false,
  "finished": true,
  "result": true,
  "hashrate": 850.0,
  "temperature": 42.5,
  "core_voltage": 1195,
  "results": [
    {"name": "PSRAM",    "passed": true, "completed": true, "detail": "OK"},
    {"name": "PERIPH",   "passed": true, "completed": true, "detail": "OK"},
    {"name": "VR",       "passed": true, "completed": true, "detail": "OK"},
    {"name": "VCORE",    "passed": true, "completed": true, "detail": "1195 mV"},
    {"name": "TEMP",     "passed": true, "completed": true, "detail": "42.5 C"},
    {"name": "POWER",    "passed": true, "completed": true, "detail": "OK"},
    {"name": "ASIC",     "passed": true, "completed": true, "detail": "1 chips OK"},
    {"name": "HASHRATE", "passed": true, "completed": true, "detail": "850 GH/s"}
  ]
}
```

### Get/set device configuration

```
GET  /api/config
POST /api/config
```

GET returns current NVS settings (passwords omitted). POST accepts a
JSON body with any subset of: `wifi_ssid`, `wifi_pass`, `hostname`,
`stratum_url`, `stratum_port`, `stratum_user`, `stratum_pass`. Only
provided fields are written. Restart required to apply.

### Restart the device

```
POST /api/system/restart
```

Returns `{"status":"restarting"}` then reboots.

## WebSocket

Connect to `ws://<device-ip>/ws` to receive real-time JSON messages
pushed by the server. The WebSocket is output-only -- the server does
not process incoming frames.

### Message types

**Test status** -- sent when a test begins:

```json
{"type": "status", "test": "PSRAM", "state": "running", "detail": "Testing..."}
```

**Test result** -- sent when a test completes:

```json
{"type": "result", "test": "PSRAM", "state": "pass", "detail": "OK"}
{"type": "result", "test": "ASIC",  "state": "fail", "detail": "0/1 chips"}
```

**Live metrics** -- sent periodically during the hashrate test:

```json
{"type": "metrics", "hashrate": 850.0, "temperature": 42.5, "core_voltage": 1195}
```

**Test complete** -- sent when the full sequence finishes:

```json
{"type": "done", "passed": true}
{"type": "done", "passed": false}
```

## Quick start with curl and websocat

Start a test run:

```bash
curl -X POST http://192.168.4.1/api/test/start
```

Monitor results via WebSocket (using [websocat](https://github.com/vi/websocat)):

```bash
websocat ws://192.168.4.1/ws
```

Poll status:

```bash
curl http://192.168.4.1/api/test/status | python3 -m json.tool
```

Save WiFi credentials:

```bash
curl -X POST http://192.168.4.1/api/config \
  -H 'Content-Type: application/json' \
  -d '{"wifi_ssid":"MyNetwork","wifi_pass":"secret"}'
```

## Two-phase test architecture

Tests are split into two phases:

**Phase 1 -- Init** (runs once): PSRAM, PERIPH, VR, VCORE, TEMP

These validate hardware modules that only need initialisation once.
Results are preserved across re-runs.

**Phase 2 -- ASIC** (every run): POWER, ASIC, HASHRATE

Powers on the ASIC (without reinitialising the voltage regulator),
detects chips, and measures hashrate. The ASIC is always powered off
at the end regardless of pass/fail.

When you POST `/api/test/start` after the first run, only Phase 2
executes. The Phase 1 results remain visible in the status response
and on the web dashboard.
