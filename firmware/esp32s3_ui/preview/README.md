# Headless LVGL preview

Compiles production UI, model, status, runtime, I/O, plan helpers, history renderer and fonts.
Actual LVGL RGB565 rendering writes 800×480 BMP. Touches use press/release samples through lv_indev.
The host does not emulate physical ADCs, relays, Wi-Fi, NVS flash timing or RGB DMA.

```powershell
python -m platformio run
./.pio/build/lvgl_preview/program.exe --script system-refinement-v9.txt --out output/final
```

Run tools/verify_s3_ui.ps1 from the repository root to execute the full suite and export PNGs.

| Command | Effect |
|---|---|
| click Caption / contains Text | Touch nearest clickable ancestor of matching label |
| tap 120 200 | Press/release coordinate |
| tap_start 120 200 | Short 20 ms release, permits sampling notification entrance |
| swipe 650 250 180 250 320 | Held pointer stroke, optional duration ms |
| assert / assert_contains / assert_not_contains | Visible text checks |
| assert_page Panel / assert_disabled System | Navigation state checks |
| assert_bounds Caption\|x1 y1 x2 y2 | Entire label must fit the specified rectangle |
| assert_toast Text\|yMin yMax opaMin opaMax | Notification position and opacity, including offscreen entrance |
| assert_no_toast Text | Notification object must be absent, not merely offscreen |
| input Text | Set production keyboard textarea and accept through LVGL READY |
| restart_ui | Recreate application and reload host-persisted configuration |
| fail_save | Inject failure of next storage write |
| assert_saves 1 | Count committed storage writes, not submission attempts |
| delay_completion 1600 | Delay next host completion to exercise pending-save protection |
| assert_stored_range 24.1 26 | Check committed temperature range independently of draft |
| date 2026 10 25 1 30 | Set fixture through the production date/time validation path |
| assert_centered / assert_chrome dim | Check equal body gaps and chrome scrims, keeping content opaque |
| assert_plan_marker | Check actual pixels of the vertical current-time line |
| assert_plan_feeding_rail | Check scheduled feeding marks on the shared 00:00–24:00 axis |
| scenario 0 / event 2\|Text | Set a domain fixture / add a prioritized log record |
| assert_y_before A\|B | Check grouping and ordering even outside the scroll viewport |
| assert_label_count Text\|1 / assert_nav_order | Check duplicate labels / bottom navigation order |
| assert_palette light/dark | Match production screen/chrome colors to independent palettes |
| assert_unified_events | Reject literal event-category headings and row labels |
| assert_no_save_popup | Reject success/busy save notifications in the top layer |
| assert_history 1 7 259200 | Check full inspection, enabled pH/EC/heater mask and 72-hour range, including actual pane existence |
| assert_history_focus 1 / 255 | Check focused pH / comparison mode |
| remember_history_point / assert_history_point | Require the same selected sample after focus/return |
| advance 600000 | Jump simulated time with activity; never replay missed schedules |
| scroll_to Caption | Scroll a label into view without executing its action |
| set_slider 123 | Set the production numeric slider and deliver VALUE_CHANGED; values are clamped |
| seed_history 78 | Host-only bounded history fixture, 1–96 hours; never enabled on the device |
| ato_report 60000 | Inject a completed 60-second dose through model validation; no pump command |
| graph_soak 100 | Full sensor/ATO inspection and range changes with retained allocation check |
| controller_source / position 37.25 0 | Exercise feedback-required mode / report position for current job |
| assert_feed 1 2 | Check dose ID and motor phase independently of animation |
| assert_motion x1 y1 x2 y2 ms minFrames maxFrames | Compare actual rendered pixels and refresh count over time |
| assert_static x1 y1 x2 y2 ms minFrames maxFrames | Require identical pixels, also bound refresh count |
| assert_restarts 1 | Check deferred host restart request count |
| soak 100 / swipe_soak 100 | Navigation cycles with retained allocation check |
| toast_soak 100 | Replace during entry/exit, dismiss, expire; validate retained allocation |
| wait 200 | Simulated LVGL and service time, up to 60,000 ms per command |
| network off/lost/unknown | Host radio observations |
| network home -65 / ap 2 / dual -54 1 | STA RSSI / AP clients / both |
| clock 23 59 / clock interface | Host clock observation or local fallback |
| snapshot home / dump | Save rendered frame / dump visible labels |

Scripts stop on failure, return nonzero and save failure.bmp. Comments start with #.
Each run starts with 134 model, 24 status, 36 runtime, 19 plan, 32 theme and 19 history-time checks (264 distinct checks).
ux-v11: feeding phases, palette/range/network auto-save, storage error/retry, reload, clock,
touch test, firmware, water faults and service outputs.
edge-v9: merged rapid changes, pending-save protection, persisted state after storage failure,
invalid spring time, explicit autumn DST occurrence, new PIN, five-attempt cooldown and reset.
animation-v11: pixel motion/refresh budget, static reduced-motion state, bounds/alarms,
notification entrance/exit, replacement, dismissal and 100 notification cycles.
navigation-v9/header-v9: gestures and boundaries, blocked editor/dialog/keyboard navigation,
SSID/AP/off/RSSI, clock rollover and dynamically enabled NTP action.
settings-v9: all 11 module pages, range/AP/OFF persistence, interaction/sleep/wake,
diagnostics, firmware and restart cancellation/confirmation.
lifecycle-v11: natural feeding/service expiry with an open dialog; stale controls disappear.
system-v9/system-refinement-v9: ordered categories, Hardware map, truthful ESP8266 diagnostics,
slider limits, return-to-origin navigation, no writes during dragging, persistence after reload,
service modal slider, quiet saves and dimmed chrome.
plan-v9: shared time marker, midnight and present output states.
history-v11: temperature-only overview, hidden sample statistics, range picker, independent pH/EC/heater
panes and common cursor, exact captured local times, midnight and immutable clock corrections,
unknown/offline/empty samples, ATO details, visual-only event importance, day/night,
and 100 graph/range/layer/statistics open/close cycles.
feeding-v11: feedback-required motor position, stale feedback, watchdog, fault acknowledgement.
palette-v9: independent day/night palettes, focused subpages, keyboard and feeding.
soak/swipe-soak: 100 repeated cycles each. Allocation figures are host LVGL measurements, not ESP32 RAM/FPS.
Older scripts preserve previous iterations and are not the current suite.
The host does not emulate CPU affinity, cross-core flash handshakes or LCD timing.
Three --explore variants preserve the initial design exploration, not the current production UI.


refinement-v11 covers Panel labels, last/next feeding and duplicate protection, focused panes and cursor,
session ranges/layers, event dates/coalescing/resolution, complete system rows and breadcrumbs,
interval slider limits/persistence, and clearing only from Advanced with confirmation.
The active suite has 17 scenarios, including premium-main screenshots of all five top-level
screens in light and dark palettes; older scripts are retained for historical reference.
