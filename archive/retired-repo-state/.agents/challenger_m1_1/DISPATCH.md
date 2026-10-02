# DISPATCH — challenger_m1_1

## Role
Adversarial Stress Tester & Fuzzer for Milestone 1 (UART NDJSON Protocol).

## Task
1. Read `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\ORIGINAL_REQUEST.md` (header `## Follow-up — 2026-09-18T17:21:02Z`).
2. Read `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\orchestrator_3\SCOPE.md`.
3. Inspect `firmware/shared/uart_protocol/include/uart_protocol.h` and `src/uart_protocol.cpp`.
4. Write and execute an adversarial test harness (or fuzzing script) that bombards `parseMessage` and serializers with:
   - Malformed NDJSON, truncated JSON, unbalanced braces, non-JSON garbage.
   - Buffer overflow payloads (SSID > 32 chars, password > 64 chars, IP > 15 chars, 10KB strings).
   - Extreme boundary cases: null pointers, empty strings, unusual unicode, negative numbers, missing fields.
   - Verify that NO crashes, buffer overruns, memory leaks, or corrupted memory occur.
5. Document empirical findings, test code, and verdict (PASS/FAIL) in `c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\challenger_m1_1\handoff.md`.
6. Send completion message to parent.

## 2026-09-18T17:55:54Z
You are challenger_m1_1, working in c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\challenger_m1_1.
Your task is in c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\challenger_m1_1\DISPATCH.md.
Read c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\ORIGINAL_REQUEST.md and
c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\orchestrator_3\SCOPE.md.

Adversarially stress-test and fuzz the NDJSON protocol parser and serializers in firmware/shared/uart_protocol/.
Write and run a test harness to fuzz with malformed JSON, buffer overflows (huge SSID/pass/IP), unicode, missing fields, and boundary cases.
Verify memory safety and robust parsing.

Write your report and verdict (PASS/FAIL) to:
c:\Users\Bartek\Documents\PlatformIO\Projects\cydAquarium\.agents\challenger_m1_1\handoff.md
Send a completion message to parent (conversation ID: 1e431c65-65c5-4543-abb7-e6a8950aeb32).

