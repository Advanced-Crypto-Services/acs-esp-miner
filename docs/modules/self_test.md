# Self Test Module

## Purpose

The self-test module validates hardware during production by running a
sequence of diagnostic tests and reporting results via the on-device
display and an HTTP/WebSocket server (see `self_test_server.md`).

Tests are split into two phases so that module-initialisation tests only
run once, while ASIC power-on and hashrate tests can be re-run without
reinitialising the voltage regulator or peripherals.

## Module Interface

### Public Functions

- `self_test(void *pvParameters)` - Main entry point; initialises WiFi,
  HTTP server, and runs the first test sequence.  Does not return.
- `should_test(GlobalState *)` - Check NVS config to decide whether
  self-test mode should be entered.
- `run_test_sequence(GlobalState *)` - Execute the two-phase test
  sequence.  Safe to call multiple times for re-runs.

### Data Structures

- `SelfTestResult` - Per-test result: name, pass/fail, detail string
- `SelfTestModule` - Module state including result array, hashrate,
  temperature, core voltage, `init_complete` flag, and
  `init_result_count`

### Dependencies

- `esp_psram` - PSRAM detection
- `EMC2101` / `INA260` - Peripheral init and monitoring
- `vcore` / `TPS546` / `DS4432U` - Voltage regulator control
- `serial` - UART to ASIC chain
- `ASIC_functions` - Function pointers for chip init, work, and results
- `connect` - WiFi init/connect
- `self_test_server` - HTTP/WS server for remote monitoring

## Two-Phase Test Architecture

### Phase 1 -- Init Tests (run once)

Validates hardware modules that only need initialisation once per power
cycle.  Results are preserved across subsequent ASIC re-runs.

| Test    | What it does                                      |
|---------|---------------------------------------------------|
| PSRAM   | Verify PSRAM is initialised                       |
| PERIPH  | Init EMC2101 fan controller, INA260 power monitor |
| VR      | Full VCORE_init (TPS546_init), set voltage, test  |
| VCORE   | ADC measurement of core voltage in range           |
| TEMP    | Temperature sensor continuity check               |

On success, `init_complete = true` and `init_result_count` is recorded
so `reset_test_state()` knows where init results end.

### Phase 2 -- ASIC Tests (every run)

Powers on the ASIC and validates chip detection and hashrate.

| Test     | What it does                                       |
|----------|----------------------------------------------------|
| POWER    | Enable ASIC GPIO + VCORE_set_voltage (no VR init)  |
| ASIC     | SERIAL_init, detect expected number of chips        |
| HASHRATE | Set baud, send work, measure hashrate for 5 seconds|

`power_on_asic()` is the reverse of `power_off_asic()` -- it enables the
ASIC power GPIO and restores voltage via `VCORE_set_voltage()` without
calling `VCORE_init()` or `TPS546_init()`.

### Re-run Behaviour

When `run_test_sequence()` is called after init has already passed:

1. `reset_test_state()` preserves init results (indices 0 through
   `init_result_count - 1`) and clears only the ASIC-phase entries
2. Phase 1 is skipped entirely
3. Phase 2 runs: power on, ASIC detect, hashrate
4. `tests_done()` powers off the ASIC regardless of outcome

## Implementation Details

### Source Files

- `main/self_test/self_test.c` - Test sequence, power control
- `main/self_test/self_test.h` - Public API

### Key Static Functions

- `run_init_tests()` - Phase 1 orchestration
- `run_asic_tests()` - Phase 2 orchestration
- `power_on_asic()` - GPIO enable + voltage restore (no VR init)
- `power_off_asic()` - Zero voltage + GPIO disable
- `reset_test_state()` - Init-aware state reset
- `record_result()` - Append result and notify via WebSocket
- `tests_done()` - Power off, update module state, notify completion

### State Variables

- `BootSemaphore` (static) - Binary semaphore for long-press reset
- `SelfTestModule.init_complete` - Tracks whether init phase has passed
- `SelfTestModule.init_result_count` - Number of results from init phase

## Integration

### Boot Flow

```
app_main()
  -> should_test()
  -> self_test()
     -> display_init() / input_init() / screen_start()
     -> wifi_init() / self_test_server_start() / wifi_connect()
     -> run_test_sequence()
        -> run_init_tests()   [first run only]
        -> run_asic_tests()   [every run]
     -> while(1) sleep        [keep server alive]
```

### Re-run via Web UI

```
POST /api/test/start
  -> run_test_task()
     -> run_test_sequence()
        -> [skip init if init_complete]
        -> run_asic_tests()
```
