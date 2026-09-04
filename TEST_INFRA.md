# E2E Test Infra: ESP32-CYD Aquarium Controller

## Test Philosophy
- Multi-tier testing covering unit domain logic, build integrity across PlatformIO targets, REST API contract validation, and firmware stability stress testing.
- Methodology: Category-Partition + Boundary Value Analysis + Concurrency/Stress Testing.

## Test Environments & Commands
1. **Target Compilation**:
   - \pio run -e esp32dev\ (Primary target)
   - \pio run -e esp32dev-espnow\ (ESP-NOW target)
   - \pio run -e esp32dev-dev\ (Development target)
2. **Native Unit Domain Tests**:
   - \pio test -e native\ (Unity test framework in \irmware/cyd_controller/test/test_native_domain\)
3. **Web & REST API Tests**:
   - \
pm run test:api\ (Node.js API verification test suite)
   - \
pm run build:web-assets\ (Web asset budget and integrity verification)

## Coverage Thresholds
- Native unit tests: >=40 domain tests passing (100% pass rate) with expanded tests for safety limits and schedules.
- PlatformIO builds: Clean compilation with 0 errors across target environments.
- API tests: 100% passing.
- RAM & Heap: Operational free heap preserved, zero TLS buffer OOM crashes, task stack safe margins.
