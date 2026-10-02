#include "ui_app.h"
#include "ui_design.h"
#include "panel_hal.h"
#include "ui_fonts.h"
#include "ui_history_time.h"
#include "ui_history_view.h"
#include "ui_io.h"
#include "ui_model.h"
#include "ui_navigation.h"
#include "ui_plan.h"
#include "ui_runtime.h"
#include "ui_status.h"
#include "ui_theme.h"
#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <lvgl.h>

namespace {
using namespace ui_model;
enum class Tab : uint8_t { Home, Service, Plan, History, System };
enum class Page : uint8_t {
    Main,
    Module,
    Schedule,
    Heater,
    Feeding,
    Alarms,
    Calibration,
    Settings,
    TouchTest,
    Range,
    Numeric
};
enum class Setting : uint8_t {
    Network,
    Display,
    Clock,
    Sound,
    Security,
    Hardware,
    Diagnostics,
    Update,
    Power,
    Scenarios,
    Feeder,
    Controller,
    Journal,
    Colors,
    FeederManual
};
enum class Action : uint8_t {
    Nav,
    Back,
    Module,
    Category,
    Schedule,
    EditHeater,
    EditFeeding,
    Alarm,
    Setting,
    Scenario,
    Manual,
    Light,
    Feed,
    AdditionalDose,
    Service,
    Stop,
    Confirm,
    Dismiss,
    Save,
    Cancel,
    Day,
    ScheduleMode,
    LightProfileToggle,
    Time,
    Adjust,
    Chart,
    Calibrate,
    CalNext,
    CalBuffer,
    Toggle,
    PinDigit,
    PinBack,
    PinEnter,
    Wireless,
    ServiceStart,
    ServiceLight,
    ServiceOutput,
    NetworkMode,
    Reconnect,
    ClockField,
    ClockSave,
    ClockSync,
    ScreenSleep,
    Wake,
    TestTouch,
    TouchTarget,
    Logs,
    ClearHistory,
    ClearLog,
    VerifyFirmware,
    Restart,
    EditRange,
    RetrySave,
    RevertChanges,
    ClockEdit,
    ClockOccurrence,
    SystemCategory,
    ResetFeederFault,
    HardwareCategory,
    EditValue,
    ValueStep,
    ValueAccept,
    ColorPreset,
    ColorMode,
    FeederManualStep,
    FeederManualJog
};
enum class Dialog : uint8_t {
    None,
    Feed,
    Service,
    Manual,
    Light,
    Stop,
    Time,
    Pin,
    Reset,
    Wireless,
    Restart,
    ClearHistory,
    ClearLog
};
enum class PinPurpose : uint8_t { Unlock, AdditionalDose };
using Preferences = ui_runtime::Preferences;
struct Ui {
    Tab tab = Tab::Home;
    Page page = Page::Main;
    Setting setting = Setting::Scenarios;
    ModuleId selected = ModuleId::Heater;
    int category = -1;
    uint8_t chart = 0, calStep = 0;
    bool calEc = false, buffer686 = true, feedDose = true;
    Schedule schedule{};
    LightProgram lightProgram{};
    HeaterConfig heater{};
    FeedingConfig feeding{};
    FeederConfig feeder{};
    Preferences prefs, prefDraft;
    Mode manual = Mode::Auto;
    LightMode light = LightMode::Day;
    uint16_t durationMinutes = 5, serviceMinutes = 15, manualMinutes = 15;
    uint8_t feederManualStep = 15;
    uint8_t timeField = 0;
    uint16_t timeDraft = 0;
    uint32_t unlockUntil = 0;
    char pin[5] = "";
    uint8_t pinLength = 0;
    Page gatedPage = Page::Main;
    Dialog dialog = Dialog::None;
    PinPurpose pinPurpose = PinPurpose::Unlock;
    bool rebuild = false, closeOverlay = false;
    bool feedCardOpen = false;
    int8_t entryDirection = 0;
    ui_runtime::DateTime clockDraft;
    uint8_t clockField = 3, touchTarget = 0, failedPins = 0;
    uint32_t pinBlockedUntil = 0;
    uint32_t adminDoseJob = 0;
    bool pending = false;
    Action pendingAction = Action::Nav;
    int pendingIndex = 0;
    SensorRange rangeDraft{};
    uint8_t rangeKind = 0;
    bool apVisible = false;
    bool clockEditing = false;
    uint8_t systemCategory = 0;
    uint8_t hardwareCategory = 0, valueIndex = 0;
    int valueDraft = 0, valueOriginal = 0;
    Page valueOrigin = Page::Main;
    Dialog valueDialog = Dialog::None;
    bool moduleFromMap = false;
    bool colorNight = false;
};
Ui u;
lv_obj_t *body = nullptr, *overlay = nullptr, *toast = nullptr, *ribbon = nullptr;
lv_obj_t *pinLabel = nullptr, *countdownLabel = nullptr;
lv_obj_t *statusStrip = nullptr;
lv_obj_t *headerClock = nullptr, *headerDate = nullptr, *headerNetworkIcon = nullptr,
         *headerNetworkCaption = nullptr;
lv_obj_t *wirelessDetailsLabel = nullptr, *clockDetailsLabel = nullptr, *clockSourceLabel = nullptr;
ui_status::Status panelStatus;
lv_obj_t *networkSsid = nullptr, *networkPassword = nullptr, *keyboard = nullptr;
lv_obj_t *solarCityField = nullptr;
lv_obj_t *networkEditor = nullptr, *networkEditField = nullptr, *networkEditTarget = nullptr;
uint32_t toastUntil = 0, lastRefresh = 0, lastRevision = 0;
bool toastClosing = false;
constexpr int kToastTop = 64;
struct Binding {
    lv_obj_t *label;
    uint8_t kind;
    ModuleId id;
};
Binding bindings[32];
uint8_t bindingCount = 0;
lv_obj_t *healthCard = nullptr, *healthTitle = nullptr, *healthDetail = nullptr;
lv_obj_t *waterGraphic = nullptr, *waterStatus = nullptr;
lv_obj_t *feedButton = nullptr, *feedLast = nullptr, *feedNext = nullptr;
lv_obj_t *flowStatus = nullptr;
lv_obj_t *diagnosticsLabel = nullptr, *networkDetailsLabel = nullptr, *firmwareLabel = nullptr;
lv_obj_t *securityPin = nullptr, *sleepOverlay = nullptr, *serviceRemaining = nullptr;
uint32_t lastWave = 0, lastActivity = 0, lastTouchCount = 0;
uint32_t lastMotorCaption = 0;
uint32_t lastLogRevision = 0;
bool editDirty = false, savePending = false, saveFailed = false, leaveAfterSave = false;
bool clockAfterSave = false;
bool saveConfirmed = false;
Dialog lastDialogShown = Dialog::None;
uint32_t saveAt = 0;
ui_runtime::Configuration savingConfig;
lv_obj_t *saveLabel = nullptr, *feedVisual = nullptr, *feedCaption = nullptr;
lv_obj_t *feedCardGraphic = nullptr, *feedCardPercent = nullptr, *feedCardPhase = nullptr;
lv_obj_t *feedCardPosition = nullptr, *feedCardSource = nullptr, *feedCardCountdown = nullptr;
lv_obj_t *feedCardFill = nullptr;
lv_obj_t *additionalDoseButton = nullptr;
FeederPhase feedCardGraphicPhase = FeederPhase::Idle;
float feedCardGraphicDegrees = -1.0f;
bool feedCardGraphicValid = false;
lv_obj_t *clockLive = nullptr, *clockDate = nullptr;
lv_obj_t *clockSyncButton = nullptr, *clockNetworkLabel = nullptr, *clockOrigin = nullptr;
lv_obj_t *numericSlider = nullptr, *numericLabel = nullptr;
char firmwareSummary[160] = "Kontrola obrazu nie została uruchomiona";
constexpr int kPlanX = 180, kPlanWidth = 550, kPlanFirstRow = 64, kPlanRowHeight = 44;
constexpr ModuleId kPlanIds[] = {ModuleId::FrontLight, ModuleId::RearLight, ModuleId::Filter, ModuleId::Air,
                                 ModuleId::Co2};
constexpr unsigned kPlanRows = sizeof(kPlanIds) / sizeof(kPlanIds[0]);
lv_obj_t *planDayLabel = nullptr, *planNowBadge = nullptr, *planNowLabel = nullptr;
lv_obj_t *planMarker = nullptr;
lv_obj_t *planFeedingRail = nullptr;
FeedingConfig planFeedingCache{};
bool planFeedingReady = false;
lv_obj_t *planRails[kPlanRows]{};
uint16_t planMinute = UINT16_MAX;
uint8_t planDay = UINT8_MAX;
uint8_t planMode = UINT8_MAX;
struct PlanRowCache {
    Schedule schedule{};
    LightProgram program{};
    State state = State::Unknown;
    LightMode lightMode = LightMode::Off;
    bool known = false, transitioning = false, ready = false;
};
PlanRowCache planCache[kPlanRows];
void markEdited();
void save();
void servicePersistence();
bool editing();
void refreshHistory();
void dispatch(Action action, int index);
void stepper(const char *caption, const char *value, int x, int y, int w, int index);

bool sameColors(const ui_theme::Settings &a, const ui_theme::Settings &b) {
    return a.preset[0] == b.preset[0] && a.preset[1] == b.preset[1];
}
const ui_theme::Palette &theme() {
    static ui_theme::Palette resolved = ui_theme::light;
    static ui_theme::Settings previous{};
    static bool ready = false, previousNight = false;
    const bool night = ui_runtime::nightTheme(u.prefs);
    if (!ready || previousNight != night || !sameColors(previous, u.prefs.colors)) {
        resolved = ui_theme::resolve(u.prefs.colors, night);
        previous = u.prefs.colors;
        previousNight = night;
        ready = true;
    }
    return resolved;
}
uint32_t bg() { return theme().background; }
uint32_t ink() { return theme().text; }
uint32_t muted() { return theme().muted; }
uint32_t lineColor() { return theme().border; }
uint32_t wash() { return theme().surface; }
uint32_t chromeBg() { return theme().chrome; }
uint32_t chromeInk() { return theme().chromeText; }
uint32_t chromeMuted() { return theme().chromeMuted; }
uint32_t chromeActive() { return theme().chromeActive; }
uint32_t chromeLine() { return theme().chromeBorder; }
uint32_t navigationBg() { return theme().navigation; }
uint32_t navigationInk() { return theme().navigationText; }
uint32_t navigationMuted() { return theme().navigationMuted; }
uint32_t navigationActive() { return theme().navigationActive; }
uint32_t navigationLine() { return theme().navigationBorder; }
uint32_t selectedFill() { return theme().selected; }
uint32_t contentGood() { return theme().good; }
uint32_t contentBad() { return theme().bad; }
uint32_t chartCardColor() { return theme().card; }
bool chromeDimmed() { return u.page != Page::Main; }
uint32_t teal = ui_theme::light.accent, danger = ui_theme::light.bad, amber = ui_theme::light.warning;
const char *tabNames[] = {"Panel", "Serwis", "Plan dnia", "Historia", "System"};
constexpr Tab tabOrder[] = {Tab::Home, Tab::Plan, Tab::History, Tab::Service, Tab::System};
int tabPosition(Tab tab) {
    for (int index = 0; index < ui_navigation::kPageCount; ++index)
        if (tabOrder[index] == tab)
            return index;
    return 0;
}
const char *categories[] = {"Światło", "Obieg wody", "Karmienie", "Chemia"};
const char *dayNames[] = {"Pn", "Wt", "Śr", "Cz", "Pt", "So", "Nd"};
const char *settingNames[] = {"Sieć Wi-Fi",          "Ekran",          "Zegar i czas", "Interakcja",
                              "PIN i dostęp",        "Mapa sprzętu",   "Stan panelu",  "Firmware",
                              "Zasilanie i restart", "Test czujników", "Karmnik",      "Sterownik ESP8266",
                              "Dziennik zdarzeń", "Motywy interfejsu", "Ruch ręczny"};

lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius = 0) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    if (parent != lv_scr_act() && theme().cardOutline && color == theme().card) {
        lv_obj_set_style_border_color(o, lv_color_hex(ui_theme::readableText(color)), 0);
        lv_obj_set_style_border_opa(o, LV_OPA_40, 0);
        lv_obj_set_style_border_width(o, 2, 0);
    }
    return o;
}
uint32_t legibleColor(lv_obj_t *parent, uint32_t desired) {
    for (lv_obj_t *node = parent; node; node = lv_obj_get_parent(node)) {
        if (lv_obj_get_style_bg_opa(node, LV_PART_MAIN) == LV_OPA_COVER) {
            const uint32_t background = lv_color_to32(lv_obj_get_style_bg_color(node, LV_PART_MAIN)) & 0xFFFFFFu;
            return ui_theme::readableForeground(desired, background);
        }
    }
    return ui_theme::readableForeground(desired, bg());
}
void setLegibleText(lv_obj_t *object, uint32_t desired) {
    lv_obj_set_style_text_color(object, lv_color_hex(legibleColor(lv_obj_get_parent(object), desired)), 0);
}
lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int w,
                const lv_font_t *font = &ui_font_16, uint32_t color = 0) {
    lv_obj_t *o = lv_label_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_width(o, w);
    lv_label_set_text(o, text);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(legibleColor(parent, color ? color : ink())), 0);
    lv_obj_set_style_text_line_space(o, 4, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}
void rule(lv_obj_t *p, int x, int y, int w, uint32_t color = 0, int h = 1) {
    box(p, x, y, w, h, color ? color : lineColor());
}
bool isFourDigitPin(const char *value) {
    if (!value || std::strlen(value) != 4)
        return false;
    for (uint8_t index = 0; index < 4; ++index)
        if (value[index] < '0' || value[index] > '9')
            return false;
    return true;
}
void wirelessDescription(char *text, size_t capacity) {
    using ui_status::WirelessMode;
    const ui_status::Wireless &wifi = panelStatus.wireless;
    char signal[64];
    if (wifi.signalValid) {
        const char *quality = wifi.signalLevel == 4   ? "mocny"
                              : wifi.signalLevel == 3 ? "dobry"
                              : wifi.signalLevel == 2 ? "słaby"
                                                      : "bardzo słaby";
        std::snprintf(signal, sizeof(signal), "%d dBm / %s", wifi.rssi, quality);
    } else
        std::snprintf(signal, sizeof(signal), "niedostępny");
    const char *station = wifi.stationName[0] ? wifi.stationName : "–";
    const char *accessPoint = wifi.apName[0] ? wifi.apName : "–";
    switch (wifi.mode) {
    case WirelessMode::Home:
        std::snprintf(text, capacity,
                      "Tryb: sieć domowa (STA)\nSieć: %s\nSygnał: %s\n\nPanel korzysta z "
                      "routera. Własny AP jest wyłączony.",
                      station, signal);
        break;
    case WirelessMode::AccessPoint:
        std::snprintf(text, capacity,
                      "Tryb: access point\nSieć panelu: %s\nPołączone urządzenia: %u\n\n"
                      "Panel nadaje własną sieć.\nRSSI do routera nie dotyczy trybu AP.",
                      accessPoint, wifi.clients);
        break;
    case WirelessMode::HomeAndAp:
        std::snprintf(text, capacity,
                      "Tryb: sieć domowa + access point\nDom: %s\nSygnał do routera: %s\n"
                      "AP: %s\nPołączone urządzenia AP: %u",
                      station, signal, accessPoint, wifi.clients);
        break;
    case WirelessMode::Disconnected:
        std::snprintf(text, capacity,
                      "Wi-Fi jest włączone, ale panel nie ma połączenia.\nSieć domowa: "
                      "rozłączona\nAccess point: nieaktywny\n\nSprawdź ustawienia sieci.");
        break;
    case WirelessMode::Unknown:
        std::snprintf(text, capacity,
                      "Nie można odczytać stanu Wi-Fi panelu.\nSiła sygnału i rodzaj "
                      "połączenia są nieznane.\n\nSprawdź diagnostykę sieci.");
        break;
    case WirelessMode::Off:
        std::snprintf(text, capacity,
                      "Wi-Fi panelu jest wyłączone.\nSieć domowa i access point są "
                      "nieaktywne.\n\nStan sterownika akwarium jest niezależny\nod "
                      "połączenia panelu z Wi-Fi.");
        break;
    }
}
void drawWireless(lv_event_t *event) {
    const ui_status::Wireless &wifi = panelStatus.wireless;
    lv_area_t bounds;
    lv_obj_get_coords(lv_event_get_target(event), &bounds);
    const lv_point_t center = {static_cast<lv_coord_t>(bounds.x1 + 17),
                               static_cast<lv_coord_t>(bounds.y1 + 29)};
    lv_draw_ctx_t *context = lv_event_get_draw_ctx(event);
    if (wifi.mode == ui_status::WirelessMode::AccessPoint) {
        lv_draw_rect_dsc_t node;
        lv_draw_rect_dsc_init(&node);
        node.bg_opa = LV_OPA_TRANSP;
        node.border_color = lv_color_hex(chromeMuted());
        node.border_width = 2;
        node.radius = 3;
        lv_area_t router = {static_cast<lv_coord_t>(bounds.x1 + 6), static_cast<lv_coord_t>(bounds.y1 + 21),
                            static_cast<lv_coord_t>(bounds.x1 + 28),
                            static_cast<lv_coord_t>(bounds.y1 + 31)};
        lv_draw_rect(context, &node, &router);
        lv_draw_line_dsc_t wire;
        lv_draw_line_dsc_init(&wire);
        wire.color = lv_color_hex(chromeMuted());
        wire.width = 2;
        const int antennas[] = {9, 25};
        for (int side : antennas) {
            lv_point_t a = {static_cast<lv_coord_t>(bounds.x1 + side),
                            static_cast<lv_coord_t>(bounds.y1 + 21)};
            lv_point_t b = {a.x, static_cast<lv_coord_t>(bounds.y1 + 12)};
            lv_draw_line(context, &wire, &a, &b);
        }
        return;
    }
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.width = 2;
    arc.rounded = 1;
    // AP waves denote broadcasting, not a fabricated measurement of received signal.
    const bool broadcasting = wifi.mode == ui_status::WirelessMode::AccessPoint;
    for (unsigned wave = 0; wave < 3; ++wave) {
        const bool received = wifi.signalValid && wifi.signalLevel >= wave + 2;
        arc.color = lv_color_hex(wifi.mode == ui_status::WirelessMode::Off ? 0x788680
                                 : received                                ? chromeMuted()
                                                                           : chromeLine());
        arc.opa = received || broadcasting ? LV_OPA_COVER : LV_OPA_70;
        if (broadcasting)
            arc.color = lv_color_hex(chromeMuted());
        lv_draw_arc(context, &arc, &center, 8 + wave * 5, 225, 315);
    }
    lv_draw_rect_dsc_t point;
    lv_draw_rect_dsc_init(&point);
    point.bg_color = lv_color_hex(wifi.mode == ui_status::WirelessMode::Off ? 0x788680
                                  : wifi.signalValid || broadcasting        ? chromeMuted()
                                                                            : chromeLine());
    point.bg_opa = LV_OPA_COVER;
    point.radius = LV_RADIUS_CIRCLE;
    const lv_area_t area = {static_cast<lv_coord_t>(center.x - 2), static_cast<lv_coord_t>(center.y - 2),
                            static_cast<lv_coord_t>(center.x + 2), static_cast<lv_coord_t>(center.y + 2)};
    lv_draw_rect(context, &point, &area);
}
void drawBrandMark(lv_event_t *event) {
    lv_area_t bounds;
    lv_obj_get_coords(lv_event_get_target(event), &bounds);
    lv_draw_ctx_t *context = lv_event_get_draw_ctx(event);
    lv_draw_line_dsc_t wave;
    lv_draw_line_dsc_init(&wave);
    wave.color = lv_color_hex(theme().accentText);
    wave.width = 2;
    wave.round_start = wave.round_end = 1;
    for (int row = 0; row < 2; ++row) {
        const int y = bounds.y1 + 18 + row * 10;
        const lv_point_t points[] = {
            {static_cast<lv_coord_t>(bounds.x1 + 8), static_cast<lv_coord_t>(y + 2)},
            {static_cast<lv_coord_t>(bounds.x1 + 14), static_cast<lv_coord_t>(y - 1)},
            {static_cast<lv_coord_t>(bounds.x1 + 20), static_cast<lv_coord_t>(y + 2)},
            {static_cast<lv_coord_t>(bounds.x1 + 26), static_cast<lv_coord_t>(y - 1)},
            {static_cast<lv_coord_t>(bounds.x1 + 33), static_cast<lv_coord_t>(y + 2)}};
        for (unsigned segment = 0; segment < 4; ++segment)
            lv_draw_line(context, &wave, &points[segment], &points[segment + 1]);
    }
    lv_draw_rect_dsc_t bubble;
    lv_draw_rect_dsc_init(&bubble);
    bubble.bg_color = wave.color;
    bubble.bg_opa = LV_OPA_COVER;
    bubble.radius = LV_RADIUS_CIRCLE;
    const lv_area_t dot = {static_cast<lv_coord_t>(bounds.x1 + 27),
                           static_cast<lv_coord_t>(bounds.y1 + 8),
                           static_cast<lv_coord_t>(bounds.x1 + 31),
                           static_cast<lv_coord_t>(bounds.y1 + 12)};
    lv_draw_rect(context, &bubble, &dot);
}
void updateHeader() {
    const ui_status::Status previous = panelStatus;
    panelStatus = ui_status::read(demoMinute());
    char clock[9];
    std::snprintf(clock, sizeof(clock), "%02u:%02u:%02u", panelStatus.clock.hour,
                  panelStatus.clock.minute, panelStatus.clock.second);
    if (headerClock && std::strcmp(lv_label_get_text(headerClock), clock) != 0)
        lv_label_set_text(headerClock, clock);
    if (headerDate) {
        static const char *months[] = {"stycznia", "lutego", "marca", "kwietnia", "maja", "czerwca",
                                       "lipca", "sierpnia", "września", "października", "listopada",
                                       "grudnia"};
        const ui_runtime::DateTime date = ui_runtime::dateTime();
        char caption[48];
        if (panelStatus.clock.source == ui_status::ClockSource::System && date.month >= 1 &&
            date.month <= 12 && date.day >= 1 && date.day <= 31)
            std::snprintf(caption, sizeof(caption), "%d %s", date.day, months[date.month - 1]);
        else
            std::snprintf(caption, sizeof(caption), "Data nieustawiona");
        if (std::strcmp(lv_label_get_text(headerDate), caption) != 0)
            lv_label_set_text(headerDate, caption);
    }
    if (clockDetailsLabel && std::strcmp(lv_label_get_text(clockDetailsLabel), clock) != 0)
        lv_label_set_text(clockDetailsLabel, clock);
    if (clockSourceLabel)
        lv_label_set_text_static(
            clockSourceLabel, panelStatus.clock.source == ui_status::ClockSource::System
                                  ? "Źródło: zegar systemowy ESP32 / Europe/Warsaw"
                                  : "Źródło: lokalny zegar interfejsu\nPoczątek: 14:32 po uruchomieniu.");
    if (headerNetworkCaption) {
        const auto mode = panelStatus.wireless.mode;
        const char *name =
            mode == ui_status::WirelessMode::Off           ? ""
            : mode == ui_status::WirelessMode::AccessPoint ? "AquaBox AP"
            : mode == ui_status::WirelessMode::Home || mode == ui_status::WirelessMode::HomeAndAp
                ? (panelStatus.wireless.stationName[0] ? panelStatus.wireless.stationName : "Sieć domowa")
            : mode == ui_status::WirelessMode::Disconnected ? "Brak sieci"
                                                            : "Brak danych";
        if (std::strcmp(lv_label_get_text(headerNetworkCaption), name) != 0)
            lv_label_set_text(headerNetworkCaption, name);
    }
    if (headerNetworkIcon && (previous.wireless.mode != panelStatus.wireless.mode ||
                              previous.wireless.signalLevel != panelStatus.wireless.signalLevel ||
                              previous.wireless.signalValid != panelStatus.wireless.signalValid))
        lv_obj_invalidate(headerNetworkIcon);
    if (wirelessDetailsLabel) {
        char details[384];
        wirelessDescription(details, sizeof(details));
        if (std::strcmp(lv_label_get_text(wirelessDetailsLabel), details) != 0)
            lv_label_set_text(wirelessDetailsLabel, details);
    }
}
uintptr_t key(Action action, int index = 0) {
    return (static_cast<uintptr_t>(action) << 16) | static_cast<uint16_t>(index);
}
void onEvent(lv_event_t *e);
void onGesture(lv_event_t *e);
void clickable(lv_obj_t *o, Action action, int index = 0) {
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    // A focus outline is normally invisible on touch, but keeps encoder/test
    // navigation discoverable without changing the object's layout.
    lv_obj_set_style_outline_color(o, lv_color_hex(theme().accent), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(o, 2, LV_STATE_FOCUS_KEY);
    lv_obj_add_event_cb(o, onEvent, LV_EVENT_CLICKED, reinterpret_cast<void *>(key(action, index)));
}
lv_obj_t *button(lv_obj_t *p, const char *title, int x, int y, int w, int h, Action action, int index = 0,
                 bool active = false, bool disabled = false) {
    lv_obj_t *o = lv_btn_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, ui_design::radius::control, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(active ? selectedFill() : wash()), 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(selectedFill()), LV_STATE_PRESSED);
    lv_obj_set_style_transform_zoom(o, ui_design::pressedZoom, LV_STATE_PRESSED);
    // Keep unavailable actions legible on the physical LCD; the muted fill and
    // disabled input state distinguish them without washing out the label.
    lv_obj_set_style_opa(o, LV_OPA_80, LV_STATE_DISABLED);
    lv_obj_t *t = label(o, title, 0, 0, w - 12, ui_design::typography::body(), ink());
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(t);
    clickable(o, action, index);
    if (disabled)
        lv_obj_add_state(o, LV_STATE_DISABLED);
    return o;
}
void bind(lv_obj_t *o, uint8_t kind, ModuleId id = ModuleId::Count) {
    if (bindingCount < 32)
        bindings[bindingCount++] = {o, kind, id};
}
void formatTime(char *out, size_t n, uint16_t minutes) {
    snprintf(out, n, "%02u:%02u", minutes / 60, minutes % 60);
}
void decimal(char *out, size_t n, float value, int places) {
    snprintf(out, n, places == 0 ? "%.0f" : places == 2 ? "%.2f" : "%.1f", static_cast<double>(value));
    for (char *c = out; *c; ++c)
        if (*c == '.')
            *c = ',';
}
const char *measurementStatus(const Measurement &m) {
    return !m.valid ? "Brak pomiaru" : m.stale ? "Dane nieaktualne" : "";
}
const char *moduleState(ModuleId id) {
    const Module &m = module(id);
    const Snapshot &s = snapshot();
    if (!m.known)
        return "Stan nieznany";
    if (m.transitioning)
        return "Zmiana profilu…";
    if (id == ModuleId::Ato)
        return "Pompa nieprzypisana";
    if (m.state == State::Blocked)
        return s.serviceActive   ? "Pauza: serwis"
               : s.feedingActive ? "Pauza: karmienie"
                                 : "Blokada ochronna";
    if (m.state == State::On)
        return metadata(id).controllable ? "Pracuje" : "Pomiar aktywny";
    if (m.state == State::Warning)
        return "Wymaga uwagi";
    return "Wyłączone";
}
uint32_t stateColor(ModuleId id) {
    const Module &m = module(id);
    return !m.known                    ? muted()
           : m.state == State::Warning ? danger
           : m.state == State::Blocked ? amber
           : m.state == State::On      ? teal
                                       : muted();
}
void dot(lv_obj_t *p, int x, int y, uint32_t color, bool filled = true) {
    lv_obj_t *o = box(p, x, y, 10, 10, filled ? color : bg(), 5);
    if (!filled) {
        lv_obj_set_style_border_color(o, lv_color_hex(color), 0);
        lv_obj_set_style_border_width(o, 2, 0);
    }
}
void moveToast(void *object, int32_t y) {
    lv_obj_set_y(static_cast<lv_obj_t *>(object), static_cast<lv_coord_t>(y));
}
void fadeToast(void *object, int32_t opacity) {
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(object), static_cast<lv_opa_t>(opacity), 0);
}
void closeToast() {
    if (!toast || toastClosing)
        return;
    if (u.prefs.reducedMotion) {
        lv_obj_del(toast);
        toast = nullptr;
        return;
    }
    toastClosing = true;
    lv_anim_del(toast, nullptr);
    lv_anim_t motion;
    lv_anim_init(&motion);
    lv_anim_set_var(&motion, toast);
    lv_anim_set_exec_cb(&motion, moveToast);
    lv_anim_set_values(&motion, lv_obj_get_y(toast), -lv_obj_get_height(toast) - 8);
    lv_anim_set_time(&motion, ui_design::motion::normal);
    lv_anim_set_path_cb(&motion, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&motion, [](lv_anim_t *animation) {
        if (toast == animation->var) {
            lv_obj_t *finished = toast;
            toast = nullptr;
            toastClosing = false;
            // Remove after the animation callback has returned, never inside its active traversal.
            lv_obj_del_async(finished);
        }
    });
    lv_anim_start(&motion);
    lv_anim_set_exec_cb(&motion, fadeToast);
    lv_anim_set_values(&motion, lv_obj_get_style_opa(toast, 0), LV_OPA_TRANSP);
    lv_anim_set_path_cb(&motion, lv_anim_path_linear);
    lv_anim_set_ready_cb(&motion, nullptr);
    lv_anim_start(&motion);
}
void notify(const char *text, bool error = false) {
    if (!error && !u.prefs.feedback)
        return;
    if (toast) {
        lv_anim_del(toast, nullptr);
        lv_obj_del(toast);
        toast = nullptr;
    }
    toastClosing = false;
    toast = box(lv_layer_top(), 80, kToastTop, 640, 52, error ? theme().badTint : wash(), 8);
    lv_obj_set_style_border_width(toast, 1, 0);
    lv_obj_set_style_border_color(toast, lv_color_hex(error ? contentBad() : lineColor()), 0);
    lv_obj_add_flag(toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(toast, [](lv_event_t *) { closeToast(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *message = label(toast, text, 20, 14, 600, &ui_font_16, error ? contentBad() : ink());
    lv_obj_update_layout(message);
    lv_obj_set_height(toast, std::fmax(52, lv_obj_get_height(message) + 28));
    toastUntil = millis() + 3500;
    if (!u.prefs.reducedMotion) {
        lv_anim_t motion;
        lv_anim_init(&motion);
        lv_anim_set_var(&motion, toast);
        lv_anim_set_exec_cb(&motion, moveToast);
        lv_anim_set_values(&motion, -lv_obj_get_height(toast) - 8, kToastTop);
        lv_anim_set_time(&motion, ui_design::motion::slow);
        // A small, damped overshoot makes arrival readable without bouncing the whole screen.
        lv_anim_set_path_cb(&motion, lv_anim_path_overshoot);
        lv_anim_start(&motion);
        lv_anim_set_exec_cb(&motion, fadeToast);
        lv_anim_set_values(&motion, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_time(&motion, ui_design::motion::normal);
        lv_anim_set_path_cb(&motion, lv_anim_path_ease_out);
        lv_anim_start(&motion);
    }
}
bool accepted(const Validation &v) {
    if (!v.ok())
        notify(v.message, true);
    return v.ok();
}
void openPage(Page p) {
    const bool protectedEditor =
        p == Page::Schedule || p == Page::Heater || p == Page::Feeding || p == Page::Range ||
        p == Page::Numeric ||
        (p == Page::Settings &&
         (u.setting == Setting::Network || u.setting == Setting::Display || u.setting == Setting::Colors ||
          u.setting == Setting::Clock ||
          u.setting == Setting::Sound || u.setting == Setting::Security || u.setting == Setting::Feeder ||
          u.setting == Setting::FeederManual));
    if (u.prefs.lock && protectedEditor && static_cast<int32_t>(u.unlockUntil - millis()) <= 0) {
        u.gatedPage = p;
        u.pinPurpose = PinPurpose::Unlock;
        u.pinLength = 0;
        u.pin[0] = 0;
        u.dialog = Dialog::Pin;
        u.rebuild = true;
        return;
    }
    u.page = p;
    u.rebuild = true;
}
void heading(const char *title, const char *subtitle) {
    button(body, "←", 0, 0, 48, 44, Action::Back);
    label(body, title, 64, 0, 600, ui_design::typography::title());
    if (u.tab == Tab::System && (u.page == Page::Settings || u.page == Page::Heater ||
                                 (u.page == Page::Module && u.moduleFromMap))) {
        const char *group =
            u.page != Page::Settings                                       ? "Sterowanie"
            : u.setting == Setting::Network || u.setting == Setting::Clock ? "Sieć i czas"
            : u.setting == Setting::Display || u.setting == Setting::Colors ||
                      u.setting == Setting::Sound || u.setting == Setting::Security
                ? "Obsługa"
            : u.page == Page::Heater || u.page == Page::Module || u.setting == Setting::Feeder ||
                    u.setting == Setting::FeederManual || u.setting == Setting::Hardware
                ? "Sterowanie"
            : u.setting == Setting::Diagnostics || u.setting == Setting::Controller ||
                    u.setting == Setting::Journal || u.setting == Setting::Scenarios
                ? "Diagnostyka"
                : "Urządzenie";
        char path[128];
        std::snprintf(path, sizeof(path), "%s / %s%s", group,
                      u.page == Page::Module ? "Mapa sprzętu / " : "", title);
        label(body, path, 64, 29, 680, ui_design::typography::caption(), muted());
    } else if (subtitle)
        label(body, subtitle, 64, 29, 680, ui_design::typography::caption(), muted());
}
void sensor(lv_obj_t *p, const char *title, const Measurement &m, const char *unit, int x, int y, int width,
            uint8_t kind, bool big) {
    label(p, title, x, y, width, &ui_font_14, muted());
    char value[32];
    if (m.valid && !m.stale && std::isfinite(m.value)) {
        decimal(value, sizeof(value), m.value, kind == 1 ? 2 : 1);
        if (kind == 2)
            snprintf(value, sizeof(value), "%.0f", static_cast<double>(m.value));
    } else
        snprintf(value, sizeof(value), "–");
    lv_obj_t *v = label(p, value, x, y + 25, width, big ? &ui_font_44 : &ui_font_24);
    bind(v, kind);
    label(p, unit, x, y + (big ? 78 : 58), width, &ui_font_14, muted());
    rule(p, x, y + (big ? 112 : 87), width, teal, 3);
    for (int i = 0; i < 17; ++i)
        box(p, x + i * (width - 2) / 16, y + (big ? 104 : 79), 1, i % 4 ? 4 : 8, muted());
    if (!m.valid || m.stale)
        label(p, measurementStatus(m), x, y + (big ? 122 : 97), width, &ui_font_14, muted());
}
uint32_t severityColor(Severity severity) {
    return severity == Severity::Critical ? danger : severity == Severity::Warning ? amber : teal;
}
void drawSensorScale(lv_event_t *event) {
    const unsigned kind = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    if (kind > 2)
        return;
    const Snapshot &s = snapshot();
    const Measurement &reading = kind == 0 ? s.temperature : kind == 1 ? s.ph : s.ec;
    const SensorRange &range = s.ranges[kind];
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(event), &area);
    const float span = range.maximum - range.minimum;
    const float low = std::fmax(0.0f, range.minimum - span);
    const float high = range.maximum + span;
    const int width = area.x2 - area.x1;
    auto position = [&](float value) {
        float normalized = (value - low) / (high - low);
        normalized = std::fmax(0.0f, std::fmin(1.0f, normalized));
        return static_cast<lv_coord_t>(area.x1 + normalized * width);
    };
    lv_draw_rect_dsc_t bar;
    lv_draw_rect_dsc_init(&bar);
    bar.radius = 2;
    bar.bg_color = lv_color_hex(lineColor());
    lv_area_t track = {area.x1, static_cast<lv_coord_t>(area.y1 + 6), area.x2,
                       static_cast<lv_coord_t>(area.y1 + 10)};
    lv_draw_rect(lv_event_get_draw_ctx(event), &bar, &track);
    if (!reading.valid || reading.stale || !std::isfinite(reading.value))
        return;
    bar.bg_color = lv_color_hex(teal);
    track.x1 = position(range.minimum);
    track.x2 = position(range.maximum);
    lv_draw_rect(lv_event_get_draw_ctx(event), &bar, &track);
    const bool inRange = reading.value >= range.minimum && reading.value <= range.maximum;
    bar.bg_color = lv_color_hex(inRange ? ink() : (kind == 0 && reading.value >= 28.5f ? danger : amber));
    const int marker = position(reading.value);
    track = {static_cast<lv_coord_t>(marker - 2), area.y1, static_cast<lv_coord_t>(marker + 2),
             static_cast<lv_coord_t>(area.y1 + 16)};
    lv_draw_rect(lv_event_get_draw_ctx(event), &bar, &track);
}
void dashboardSensor(const char *title, uint8_t kind, int x, int y) {
    const Snapshot &s = snapshot();
    const Measurement &reading = kind == 0 ? s.temperature : kind == 1 ? s.ph : s.ec;
    const SensorRange &range = s.ranges[kind];
    lv_obj_t *tile = box(body, x, y, 207, 142, bg());
    clickable(tile, Action::Module, kind == 0 ? 4 : kind == 1 ? 7 : 8);
    lv_obj_set_style_bg_color(tile, lv_color_hex(selectedFill()), LV_STATE_PRESSED);
    label(tile, title, 0, 0, 207, ui_design::typography::body(), muted());
    bind(label(tile, "", 0, 28, 140, ui_design::typography::measurement()), kind);
    label(tile, kind == 0 ? "°C" : kind == 1 ? "pH" : "µS/cm", 151, 50, 56, &ui_font_14, muted());
    lv_obj_t *scale = box(tile, 0, 100, 205, 18, bg());
    lv_obj_add_event_cb(scale, drawSensorScale, LV_EVENT_DRAW_MAIN,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(kind)));
    bind(scale, 20 + kind);
    char a[16], b[16], text[80];
    decimal(a, sizeof(a), range.minimum, kind == 2 ? 0 : 1);
    decimal(b, sizeof(b), range.maximum, kind == 2 ? 0 : 1);
    const char *state = !reading.valid || reading.stale ? "Brak danych"
                        : reading.value < range.minimum ? "Za nisko"
                        : reading.value > range.maximum ? "Za wysoko"
                                                        : "W zakresie";
    std::snprintf(text, sizeof(text), "%s / %s–%s", state, a, b);
    bind(label(tile, text, 0, 124, 207, &ui_font_14, muted()), 23 + kind);
}
const char *waterCaption() {
    switch (waterState()) {
    case WaterState::Normal:
        return "Poziom w normie";
    case WaterState::Between:
        return "Między MIN i MAX";
    case WaterState::Low:
        return "Niski poziom";
    case WaterState::Unknown:
        return "Brak danych poziomu";
    case WaterState::Conflict:
        return "Błąd pływaków";
    }
    return "Brak danych poziomu";
}
void drawWater(lv_event_t *event) {
    lv_area_t bounds;
    lv_obj_get_coords(lv_event_get_target(event), &bounds);
    lv_draw_rect_dsc_t paint;
    lv_draw_rect_dsc_init(&paint);
    paint.radius = 3;
    paint.bg_color = lv_color_hex(theme().waterBase);
    paint.border_width = 1;
    paint.border_color = lv_color_hex(theme().waterBorder);
    lv_draw_rect(lv_event_get_draw_ctx(event), &paint, &bounds);
    const WaterState state = waterState();
    if (state == WaterState::Unknown || state == WaterState::Conflict) {
        lv_draw_line_dsc_t hatch;
        lv_draw_line_dsc_init(&hatch);
        hatch.width = 2;
        hatch.color = lv_color_hex(state == WaterState::Conflict ? contentBad() : theme().waterBorder);
        for (int x = bounds.x1 + 3; x < bounds.x2 - 20; x += 20) {
            const lv_point_t a{static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(bounds.y2 - 4)};
            const lv_point_t b{static_cast<lv_coord_t>(x + 18), static_cast<lv_coord_t>(bounds.y1 + 4)};
            lv_draw_line(lv_event_get_draw_ctx(event), &hatch, &a, &b);
        }
        return;
    }
    const int height = bounds.y2 - bounds.y1 + 1;
    const int level = state == WaterState::Low       ? height * 3 / 4
                      : state == WaterState::Between ? height / 2
                                                     : height / 4;
    const int width = bounds.x2 - bounds.x1 + 1;
    paint.radius = 0;
    paint.border_width = 0;
    paint.bg_color = lv_color_hex(state == WaterState::Low ? theme().warning : theme().waterFill);
    const unsigned phase = u.prefs.reducedMotion ? 0 : (millis() % 2400) * 360 / 2400;
    // These are discrete float states, not a fabricated percentage or continuous level measurement.
    for (int column = 0; column < 40; ++column) {
        const int offset =
            u.prefs.reducedMotion ? 0 : (lv_trigo_sin((column * 18 + phase) % 360) * 3) / 32767;
        lv_area_t water = {static_cast<lv_coord_t>(bounds.x1 + column * width / 40),
                           static_cast<lv_coord_t>(bounds.y1 + level + offset),
                           static_cast<lv_coord_t>(bounds.x1 + (column + 1) * width / 40 - 1), bounds.y2};
        lv_draw_rect(lv_event_get_draw_ctx(event), &paint, &water);
    }
}
void drawFeeding(lv_event_t *event) {
    const auto &state = snapshot();
    if (!state.feedingActive)
        return;
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(event), &area);
    lv_draw_ctx_t *context = lv_event_get_draw_ctx(event);
    lv_draw_rect_dsc_t ring;
    lv_draw_rect_dsc_init(&ring);
    ring.bg_opa = LV_OPA_TRANSP;
    ring.border_color = lv_color_hex(theme().accent);
    ring.border_width = 2;
    ring.radius = LV_RADIUS_CIRCLE;
    const lv_area_t bounds{static_cast<lv_coord_t>(area.x1 + 12), static_cast<lv_coord_t>(area.y1 + 6),
                           static_cast<lv_coord_t>(area.x1 + 40), static_cast<lv_coord_t>(area.y1 + 34)};
    lv_draw_rect(context, &ring, &bounds);
    const int angle = state.motor.positionValid ? static_cast<int>(state.motor.positionDegrees) : 0;
    lv_draw_line_dsc_t pointer;
    lv_draw_line_dsc_init(&pointer);
    pointer.color = lv_color_hex(theme().accent);
    pointer.width = 3;
    pointer.round_end = 1;
    const lv_point_t center{static_cast<lv_coord_t>(area.x1 + 26), static_cast<lv_coord_t>(area.y1 + 20)};
    const lv_point_t end{static_cast<lv_coord_t>(center.x + lv_trigo_sin(angle % 360) * 10 / 32767),
                         static_cast<lv_coord_t>(center.y - lv_trigo_cos(angle % 360) * 10 / 32767)};
    lv_draw_line(context, &pointer, &center, &end);
    ring.bg_opa = LV_OPA_COVER;
    ring.bg_color = lv_color_hex(theme().warning);
    ring.border_width = 0;
    if (state.motor.phase == FeederPhase::Moving && state.motor.positionValid) {
        for (unsigned grain = 0; grain < 3; ++grain) {
            const int descent =
                u.prefs.reducedMotion ? grain * 5 : (millis() + grain * 240) % 900 * 18 / 900;
            const lv_area_t pellet{
                static_cast<lv_coord_t>(area.x1 + 45), static_cast<lv_coord_t>(area.y1 + 4 + descent),
                static_cast<lv_coord_t>(area.x1 + 47), static_cast<lv_coord_t>(area.y1 + 6 + descent)};
            lv_draw_rect(context, &ring, &pellet);
        }
    }
    const float fraction = state.motor.positionValid && state.motor.targetDegrees
                               ? std::fmax(0.0f, std::fmin(1.0f, state.motor.positionDegrees /
                                                                   state.motor.targetDegrees))
                               : 0.0f;
    if (fraction > 0) {
        ring.bg_color = lv_color_hex(theme().accent);
        const lv_area_t bar{static_cast<lv_coord_t>(area.x1 + 8), static_cast<lv_coord_t>(area.y2 - 3),
                            static_cast<lv_coord_t>(area.x1 + 8 + fraction * (area.x2 - area.x1 - 16)),
                            static_cast<lv_coord_t>(area.y2 - 2)};
        lv_draw_rect(context, &ring, &bar);
    }
}
int feederPercent(const Snapshot &state) {
    if (!state.motor.positionValid || !state.motor.targetDegrees ||
        (state.motor.phase != FeederPhase::Moving && state.motor.phase != FeederPhase::Complete))
        return -1;
    if (state.motor.phase == FeederPhase::Complete)
        return 100;
    const float fraction = std::fmax(0.0f, std::fmin(1.0f, state.motor.positionDegrees /
                                                        state.motor.targetDegrees));
    // A final 100% requires the motor's completion acknowledgement, not rounding.
    const int progress = static_cast<int>(fraction * 100.0f);
    return progress < 100 ? progress : 99;
}
bool canRequestAdditionalDose(const Snapshot &state) {
    return state.connected && !state.serviceActive && state.feedingCooldownSeconds != 0 &&
           state.lastFeedingKnown && state.motor.phase == FeederPhase::Complete;
}
void drawFeedingCard(lv_event_t *event) {
    const Snapshot &state = snapshot();
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(event), &area);
    lv_draw_ctx_t *context = lv_event_get_draw_ctx(event);
    const int cx = area.x1 + 105, cy = area.y1 + 77;
    lv_draw_rect_dsc_t paint;
    lv_draw_rect_dsc_init(&paint);
    paint.radius = LV_RADIUS_CIRCLE;
    paint.bg_color = lv_color_hex(selectedFill());
    paint.border_color = lv_color_hex(theme().accent);
    paint.border_width = 4;
    const lv_area_t shell{static_cast<lv_coord_t>(cx - 50), static_cast<lv_coord_t>(cy - 50),
                          static_cast<lv_coord_t>(cx + 50), static_cast<lv_coord_t>(cy + 50)};
    lv_draw_rect(context, &paint, &shell);

    // The four paddle positions follow reported motor degrees; time only animates falling food.
    lv_draw_line_dsc_t spoke;
    lv_draw_line_dsc_init(&spoke);
    spoke.color = lv_color_hex(theme().accent);
    spoke.width = 6;
    spoke.round_end = 1;
    const int angle = state.motor.positionValid ? static_cast<int>(state.motor.positionDegrees) % 360 : 0;
    for (int blade = 0; blade < 4; ++blade) {
        const int direction = (angle + blade * 90) % 360;
        const lv_point_t hub{static_cast<lv_coord_t>(cx), static_cast<lv_coord_t>(cy)};
        const lv_point_t tip{static_cast<lv_coord_t>(cx + lv_trigo_sin(direction) * 32 / 32767),
                             static_cast<lv_coord_t>(cy - lv_trigo_cos(direction) * 32 / 32767)};
        lv_draw_line(context, &spoke, &hub, &tip);
    }
    paint.bg_color = lv_color_hex(theme().accent);
    paint.border_width = 0;
    const lv_area_t hub{static_cast<lv_coord_t>(cx - 8), static_cast<lv_coord_t>(cy - 8),
                        static_cast<lv_coord_t>(cx + 8), static_cast<lv_coord_t>(cy + 8)};
    lv_draw_rect(context, &paint, &hub);

    paint.radius = 5;
    paint.bg_color = lv_color_hex(lineColor());
    const lv_area_t chute{static_cast<lv_coord_t>(cx + 45), static_cast<lv_coord_t>(cy + 23),
                          static_cast<lv_coord_t>(cx + 89), static_cast<lv_coord_t>(cy + 31)};
    lv_draw_rect(context, &paint, &chute);
    const lv_area_t spout{static_cast<lv_coord_t>(cx + 82), static_cast<lv_coord_t>(cy + 23),
                          static_cast<lv_coord_t>(cx + 91), static_cast<lv_coord_t>(cy + 47)};
    lv_draw_rect(context, &paint, &spout);
    paint.bg_color = lv_color_hex(theme().accent);
    paint.radius = 6;
    const lv_area_t water{static_cast<lv_coord_t>(area.x1 + 20), static_cast<lv_coord_t>(area.y2 - 31),
                          static_cast<lv_coord_t>(area.x2 - 20), static_cast<lv_coord_t>(area.y2 - 10)};
    lv_draw_rect(context, &paint, &water);
    paint.bg_color = lv_color_hex(bg());
    paint.radius = LV_RADIUS_CIRCLE;
    const lv_area_t fish{static_cast<lv_coord_t>(area.x1 + 71), static_cast<lv_coord_t>(area.y2 - 23),
                         static_cast<lv_coord_t>(area.x1 + 100), static_cast<lv_coord_t>(area.y2 - 16)};
    lv_draw_rect(context, &paint, &fish);
    lv_draw_line_dsc_t tail;
    lv_draw_line_dsc_init(&tail);
    tail.color = lv_color_hex(bg());
    tail.width = 3;
    const lv_point_t tailTop{static_cast<lv_coord_t>(area.x1 + 105),
                             static_cast<lv_coord_t>(area.y2 - 25)};
    const lv_point_t tailMiddle{static_cast<lv_coord_t>(area.x1 + 99),
                                static_cast<lv_coord_t>(area.y2 - 20)};
    const lv_point_t tailBottom{static_cast<lv_coord_t>(area.x1 + 105),
                                static_cast<lv_coord_t>(area.y2 - 15)};
    lv_draw_line(context, &tail, &tailTop, &tailMiddle);
    lv_draw_line(context, &tail, &tailMiddle, &tailBottom);
    if (state.motor.phase == FeederPhase::Moving && state.motor.positionValid) {
        paint.bg_color = lv_color_hex(theme().warning);
        paint.radius = LV_RADIUS_CIRCLE;
        for (int grain = 0; grain < 5; ++grain) {
            const int fall = u.prefs.reducedMotion ? grain * 10 : (millis() + grain * 163) % 850 * 52 / 850;
            const int x = cx + 86 + (grain % 3 - 1) * 8;
            const lv_area_t pellet{static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(cy + 48 + fall),
                                    static_cast<lv_coord_t>(x + 4),
                                    static_cast<lv_coord_t>(cy + 52 + fall)};
            lv_draw_rect(context, &paint, &pellet);
        }
    }
}
void setFeedCardText(lv_obj_t *target, const char *value) {
    if (target && std::strcmp(lv_label_get_text(target), value) != 0)
        lv_label_set_text(target, value);
}
void refreshFeedingCard() {
    if (!feedCardPercent)
        return;
    const Snapshot &state = snapshot();
    if (feedCardGraphic &&
        (feedCardGraphicPhase != state.motor.phase || feedCardGraphicValid != state.motor.positionValid ||
         (state.motor.positionValid && feedCardGraphicDegrees != state.motor.positionDegrees))) {
        feedCardGraphicPhase = state.motor.phase;
        feedCardGraphicValid = state.motor.positionValid;
        feedCardGraphicDegrees = state.motor.positionDegrees;
        lv_obj_invalidate(feedCardGraphic);
    }
    const int percent = feederPercent(state);
    char text[96];
    if (percent < 0)
        setFeedCardText(feedCardPercent, "?");
    else {
        std::snprintf(text, sizeof(text), "%d%%", percent);
        setFeedCardText(feedCardPercent, text);
    }
    const char *phase = state.motor.phase == FeederPhase::Waiting ? "Czekam na pozycję karmnika"
                        : state.motor.phase == FeederPhase::Moving ? "Podawanie dawki"
                        : state.motor.phase == FeederPhase::Complete ? "Obrót zakończony"
                        : state.motor.phase == FeederPhase::Fault ? "Brak potwierdzenia pozycji"
                        : "Pauza obiegu bez dawki";
    setFeedCardText(feedCardPhase, phase);
    if (state.motor.positionValid) {
        std::snprintf(text, sizeof(text), "%.0f° / %u°", static_cast<double>(state.motor.positionDegrees),
                      state.motor.targetDegrees);
        setFeedCardText(feedCardPosition, text);
    } else
        setFeedCardText(feedCardPosition, "Pozycja: brak odczytu");
    setFeedCardText(feedCardSource, state.motor.source == FeederSource::Controller
                                         ? "STEROWNIK / informacja zwrotna"
                                         : "MODEL LOKALNY / symulacja");
    std::snprintf(text, sizeof(text), "Pauza / %02lu:%02lu / potem AUTO",
                  static_cast<unsigned long>(state.feedingRemainingSeconds / 60),
                  static_cast<unsigned long>(state.feedingRemainingSeconds % 60));
    setFeedCardText(feedCardCountdown, text);
    if (feedCardFill)
        lv_obj_set_width(feedCardFill, percent > 0 ? 322 * percent / 100 : 0);
    if (additionalDoseButton) {
        if (canRequestAdditionalDose(state))
            lv_obj_clear_state(additionalDoseButton, LV_STATE_DISABLED);
        else
            lv_obj_add_state(additionalDoseButton, LV_STATE_DISABLED);
    }
}
void feedingCard() {
    if (!u.feedCardOpen || !snapshot().feedingActive)
        return;
    lv_obj_t *card = box(body, 0, 0, 752, 326, bg(), ui_design::radius::card);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_color(card, lv_color_hex(theme().accent), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    label(card, "Karmienie w toku", 20, 13, 450, &ui_font_24);
    label(card, "POSTĘP SILNIKA", 355, 53, 340, &ui_font_14, muted());
    feedCardGraphic = box(card, 20, 51, 310, 194, chartCardColor(), ui_design::radius::card);
    feedCardGraphicPhase = FeederPhase::Idle;
    feedCardGraphicDegrees = -1.0f;
    feedCardGraphicValid = false;
    lv_obj_add_event_cb(feedCardGraphic, drawFeedingCard, LV_EVENT_DRAW_MAIN, nullptr);
    feedCardPercent = label(card, "?", 350, 78, 170, &ui_font_44, theme().accent);
    feedCardPhase = label(card, "", 355, 133, 363, &ui_font_20);
    feedCardPosition = label(card, "", 355, 169, 363, &ui_font_16, muted());
    box(card, 355, 202, 322, 10, lineColor(), 5);
    feedCardFill = box(card, 355, 202, 1, 10, theme().accent, 5);
    feedCardSource = label(card, "", 355, 225, 363, &ui_font_14, muted());
    feedCardCountdown = label(card, "", 355, 245, 363, &ui_font_16);
    clickable(feedCardCountdown, Action::Stop);
    button(card, "Zamknij podgląd", 20, 271, 180, 48, Action::Dismiss);
    additionalDoseButton = button(card, "Kolejna dawka / PIN", 210, 271, 250, 48,
                                  Action::AdditionalDose, 0, false, true);
    button(card, "Przerwij karmienie", 470, 271, 262, 48, Action::Stop, 0, false);
    refreshFeedingCard();
}
void tank(lv_obj_t *p, int x, int y, int w, int h) {
    lv_obj_t *vessel = box(p, x, y, w, h, wash(), 6);
    label(vessel, "POZIOM WODY", 14, 10, w - 28, &ui_font_14, muted());
    waterStatus = label(vessel, waterCaption(), 14, 31, w - 28, &ui_font_20);
    waterGraphic = box(vessel, 14, 65, w - 28, h - 72, wash(), 3);
    lv_obj_add_event_cb(waterGraphic, drawWater, LV_EVENT_DRAW_MAIN, nullptr);
    clickable(vessel, Action::Module, static_cast<int>(ModuleId::Ato));
    lv_obj_set_style_bg_color(vessel, lv_color_hex(selectedFill()), LV_STATE_PRESSED);
}
void home() {
    const Snapshot &s = snapshot();
    const Health state = health();
    lv_obj_t *overview = box(body, 454, 0, 298, 326, wash(), ui_design::radius::card);
    healthCard = box(overview, 14, 12, 270, 104, wash(), 6);
    clickable(healthCard, Action::Alarm);
    lv_obj_set_style_bg_color(healthCard, lv_color_hex(selectedFill()), LV_STATE_PRESSED);
    rule(healthCard, 0, 0, 3, severityColor(state.severity), 104);
    healthTitle = label(healthCard, state.title, 14, 5, 246, ui_design::typography::section(),
                        severityColor(state.severity));
    healthDetail = label(healthCard, state.detail, 14, 37, 246, &ui_font_14, muted());
    dashboardSensor("TEMPERATURA WODY", 0, 0, 14);
    dashboardSensor("pH WODY", 1, 225, 14);
    dashboardSensor("PRZEWODNOŚĆ EC", 2, 0, 184);
    lv_obj_t *flow = box(body, 225, 184, 207, 142, bg());
    clickable(flow, Action::Module, static_cast<int>(ModuleId::Flow));
    lv_obj_set_style_bg_color(flow, lv_color_hex(selectedFill()), LV_STATE_PRESSED);
    label(flow, "PRZEPŁYW WODY", 0, 0, 207, ui_design::typography::body(), muted());
    flowStatus =
        label(flow, s.flowValid ? (s.flowDetected ? "Obecny" : "Brak") : "–", 0, 42, 207, &ui_font_24);
    label(flow, "Impulsy / szczegóły →", 0, 124, 207, &ui_font_14, muted());
    rule(flow, 0, 107, 205, s.flowValid && s.flowDetected ? teal : lineColor(), 4);
    rule(body, 0, 169, 432);
    tank(overview, 0, 120, 298, 108);
    feedLast = label(overview, "", 14, 232, 128, &ui_font_14, muted());
    feedNext = label(overview, "", 152, 232, 132, &ui_font_14, muted());
    lv_obj_set_style_text_line_space(feedLast, 0, 0);
    lv_obj_set_style_text_line_space(feedNext, 0, 0);
    lv_obj_t *feed = button(overview, "Karmienie", 14, 270, 270, 44, Action::Feed, 0, true);
    feedButton = feed;
    if (s.feedingActive) {
        feedVisual = feed;
        feedCaption = lv_obj_get_child(feed, 0);
        lv_obj_set_align(feedCaption, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(feedCaption, 58, 3);
        lv_obj_set_width(feedCaption, 202);
        lv_obj_set_style_text_font(feedCaption, &ui_font_14, 0);
        lv_obj_set_style_text_line_space(feedCaption, 1, 0);
        lv_obj_set_style_text_align(feedCaption, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_add_event_cb(feed, drawFeeding, LV_EVENT_DRAW_MAIN, nullptr);
    }
    lv_obj_set_style_bg_color(feed, lv_color_hex(chromeBg()), 0);
    setLegibleText(lv_obj_get_child(feed, 0), chromeInk());
}
void servicePage() {
    const Snapshot &s = snapshot();
    label(body, s.serviceActive ? "Serwis aktywny" : "Serwis akwarium", 0, 0, 500, &ui_font_24);
    label(body, "Sterowanie lokalne / WeMos nie jest podłączony", 0, 29, 700, &ui_font_14, muted());
    box(body, 0, 64, 240, 262, chartCardColor(), ui_design::radius::card);
    box(body, 256, 64, 224, 262, chartCardColor(), ui_design::radius::card);
    box(body, 496, 64, 256, 262, chartCardColor(), ui_design::radius::card);

    label(body, "SESJA SERWISOWA", 16, 78, 208, &ui_font_14, muted());
    label(body, s.serviceActive ? "W toku" : "Gotowa", 16, 103, 208, &ui_font_24,
          s.serviceActive ? contentGood() : ink());
    rule(body, 16, 150, 208);
    if (s.serviceActive) {
        char remaining[16];
        std::snprintf(remaining, sizeof(remaining), "%02lu:%02lu",
                      static_cast<unsigned long>(s.serviceRemainingSeconds / 60),
                      static_cast<unsigned long>(s.serviceRemainingSeconds % 60));
        label(body, "Powrót AUTO za", 16, 165, 208, &ui_font_14, muted());
        serviceRemaining = label(body, remaining, 16, 188, 208, &ui_font_44, contentGood());
    } else {
        char duration[24];
        std::snprintf(duration, sizeof(duration), "%u min", u.serviceMinutes);
        stepper("CZAS SERWISU", duration, 16, 163, 208, 6);
    }
    button(body, s.serviceActive ? "Zakończ serwis" : "Rozpocznij serwis", 16, 268, 208, 44,
           s.serviceActive ? Action::Stop : Action::ServiceStart, 0, true, !s.connected);

    label(body, "OŚWIETLENIE", 272, 78, 192, &ui_font_14, muted());
    label(body, "Jedna lampa pozostaje włączona", 272, 104, 192, &ui_font_14, muted());
    button(body, "Przednia", 272, 145, 192, 44, Action::ServiceLight, 0,
           s.service.primaryLight == ModuleId::FrontLight);
    button(body, "Tylna", 272, 197, 192, 44, Action::ServiceLight, 1,
           s.service.primaryLight == ModuleId::RearLight);
    button(body, "Mapa urządzeń →", 272, 268, 192, 44, Action::Setting,
           static_cast<int>(Setting::Hardware));

    label(body, "WYJŚCIA / PO URUCHOMIENIU", 512, 78, 224, &ui_font_14, muted());
    const ModuleId secondary =
        s.service.primaryLight == ModuleId::FrontLight ? ModuleId::RearLight : ModuleId::FrontLight;
    const ModuleId ids[] = {secondary, ModuleId::Filter, ModuleId::Air, ModuleId::Heater, ModuleId::Co2};
    const char *names[] = {"Druga lampa", "Filtr", "Powietrze", "Grzałka", "CO2"};
    for (int i = 0; i < 5; ++i) {
        const bool on = s.serviceActive && module(ids[i]).state == State::On;
        char caption[56];
        std::snprintf(caption, sizeof(caption), "%s / %s", names[i], on ? "WŁ." : "WYŁ.");
        lv_obj_t *output = button(body, caption, 512, 102 + i * 46, 224, 44,
                                  Action::ServiceOutput, static_cast<int>(ids[i]), false,
                                  !s.serviceActive || !s.connected);
        if (on) {
            lv_obj_set_style_bg_color(output, lv_color_hex(theme().goodTint), 0);
            setLegibleText(lv_obj_get_child(output, 0), contentGood());
        }
    }
}
uint32_t planMarkerColor() { return ui_theme::timeMarker(ui_runtime::nightTheme(u.prefs)); }
uint32_t lightProfileColor(LightMode mode) {
    return mode == LightMode::Daybreak ? theme().warning
           : mode == LightMode::Night ? (ui_runtime::nightTheme(u.prefs) ? 0xA99AF0 : 0x7464BD)
                                      : theme().accent;
}
struct PlanState {
    uint32_t color;
    bool on;
};
PlanState planState(ModuleId id) {
    const Module &item = module(id);
    if (!item.known || item.state == State::Unknown)
        return {muted(), false};
    if (item.transitioning)
        return {amber, false};
    if (item.state == State::On) {
        if (id == ModuleId::FrontLight || id == ModuleId::RearLight)
            return {lightProfileColor(item.lightMode), true};
        return {teal, true};
    }
    if (item.state == State::Blocked) {
        if (snapshot().serviceActive)
            return {muted(), false};
        return {amber, false};
    }
    return {item.state == State::Warning ? danger : muted(), false};
}
bool feedingPlannedToday() {
    const FeedingConfig &feeding = snapshot().feeding;
    const uint8_t day = weekday();
    if (day >= 7 || !(feeding.dayMask & (1u << day)) || feeding.count < 1 || feeding.count > 2)
        return false;
    for (unsigned index = 0; index < feeding.count; ++index)
        if (feeding.times[index] >= 1440)
            return false;
    return true;
}
lv_area_t planTrackArea(const lv_area_t &area, bool lamp) {
    // Lamp phases need enough height for their colors to carry the meaning;
    // other modules retain the slimmer timeline used for on/off periods.
    return {area.x1, static_cast<lv_coord_t>(area.y1 + (lamp ? 2 : 6)), area.x2,
            static_cast<lv_coord_t>(area.y1 + (lamp ? 13 : 10))};
}
void drawPlanTicks(lv_draw_ctx_t *context, const lv_area_t &area) {
    lv_draw_rect_dsc_t stroke;
    lv_draw_rect_dsc_init(&stroke);
    for (int hour = 0; hour <= 24; hour += 2) {
        const bool major = hour % 6 == 0;
        stroke.bg_color = lv_color_hex(major ? muted() : lineColor());
        const int x = area.x1 + ui_plan::minutePosition(hour * 60, kPlanWidth);
        // Keep the time ruler legible where the inactive track is transparent.
        const lv_area_t tick{static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(area.y1 + 2),
                             static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(area.y1 + 14)};
        lv_draw_rect(context, &stroke, &tick);
    }
}
void drawPlanRail(lv_event_t *event) {
    const unsigned index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    if (index >= kPlanRows)
        return;
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(event), &area);
    lv_draw_ctx_t *context = lv_event_get_draw_ctx(event);
    const bool lamp = index < 2;
    lv_draw_rect_dsc_t stroke;
    lv_draw_rect_dsc_init(&stroke);
    lv_area_t track = planTrackArea(area, lamp);
    const Schedule &schedule = snapshot().schedules[static_cast<unsigned>(kPlanIds[index])];
    const bool programmedLamp = index < 2 && snapshot().lightPrograms[index].enabled &&
                                schedule.mode == ScheduleMode::Timed;
    if (programmedLamp) {
        const auto segments = ui_plan::lightSegmentsForDay(schedule, snapshot().lightPrograms[index], weekday());
        for (unsigned segment = 0; segment < segments.count; ++segment) {
            const auto &part = segments.items[segment];
            const int start = ui_plan::minutePosition(part.start, kPlanWidth);
            const int end = ui_plan::minutePosition(part.end, kPlanWidth);
            stroke.bg_color = lv_color_hex(lightProfileColor(part.mode));
            track.x1 = area.x1 + start;
            track.x2 = part.end == 1440 ? area.x2 : area.x1 + (end > start ? end - 1 : start);
            lv_draw_rect(context, &stroke, &track);
        }
    } else {
        const auto spans = ui_plan::intervalsForDay(schedule, weekday());
        stroke.bg_color = lv_color_hex(teal);
        for (unsigned span = 0; span < spans.count; ++span) {
            const int start = ui_plan::minutePosition(spans.items[span].start, kPlanWidth);
            const int end = ui_plan::minutePosition(spans.items[span].end, kPlanWidth);
            track.x1 = area.x1 + start;
            track.x2 = spans.items[span].end == 1440 ? area.x2 : area.x1 + (end > start ? end - 1 : start);
            lv_draw_rect(context, &stroke, &track);
        }
    }
    drawPlanTicks(context, area);
}
void drawPlanFeedingRail(lv_event_t *event) {
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(event), &area);
    lv_draw_ctx_t *context = lv_event_get_draw_ctx(event);
    drawPlanTicks(context, area);
    if (!feedingPlannedToday())
        return;
    const FeedingConfig &feeding = snapshot().feeding;
    lv_draw_rect_dsc_t dose;
    lv_draw_rect_dsc_init(&dose);
    dose.bg_color = lv_color_hex(theme().warning);
    dose.radius = 4;
    for (unsigned index = 0; index < feeding.count; ++index) {
        const int position = ui_plan::minutePosition(feeding.times[index], kPlanWidth);
        const int x = area.x1 + (position < 4 ? 4 : position > kPlanWidth - 5 ? kPlanWidth - 5
                                                                              : position);
        const lv_area_t marker{static_cast<lv_coord_t>(x - 4), static_cast<lv_coord_t>(area.y1 + 1),
                               static_cast<lv_coord_t>(x + 4), static_cast<lv_coord_t>(area.y1 + 15)};
        lv_draw_rect(context, &dose, &marker);
    }
}
void drawPlanMarker(lv_event_t *event) {
    if (planMinute >= 1440)
        return;
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(event), &area);
    lv_draw_ctx_t *context = lv_event_get_draw_ctx(event);
    const lv_coord_t x = area.x1 + 4 + ui_plan::minutePosition(planMinute, kPlanWidth);
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    const lv_point_t start{x, area.y1}, end{x, area.y2};
    // The high-contrast outline keeps the time cursor visible over both pale
    // tracks and saturated lamp phases without recoloring their schedule data.
    line.color = lv_color_hex(ui_theme::readableText(bg()));
    line.width = 6;
    lv_draw_line(context, &line, &start, &end);
    line.color = lv_color_hex(planMarkerColor());
    line.width = 3;
    lv_draw_line(context, &line, &start, &end);
    for (unsigned index = 0; index <= kPlanRows; ++index) {
        const bool feeding = index == kPlanRows;
        const PlanState state = feeding
                                    ? PlanState{snapshot().feedingActive ? amber : muted(),
                                                snapshot().feedingActive}
                                    : planState(kPlanIds[index]);
        const int y = area.y1 + 34 + index * kPlanRowHeight;
        lv_draw_rect_dsc_t circle;
        lv_draw_rect_dsc_init(&circle);
        circle.radius = LV_RADIUS_CIRCLE;
        circle.bg_color = lv_color_hex(state.on ? state.color : bg());
        circle.border_color = lv_color_hex(planMarkerColor());
        circle.border_width = 2;
        lv_area_t bounds{static_cast<lv_coord_t>(x - 4), static_cast<lv_coord_t>(y - 4),
                         static_cast<lv_coord_t>(x + 4), static_cast<lv_coord_t>(y + 4)};
        lv_draw_rect(context, &circle, &bounds);
    }
}
void refreshPlan() {
    if (!planMarker)
        return;
    const uint16_t minute = panelStatus.clock.hour * 60u + panelStatus.clock.minute;
    const uint8_t day = weekday();
    const uint8_t mode = snapshot().serviceActive ? 2 : snapshot().feedingActive ? 1 : 0;
    const bool timeChanged = minute != planMinute;
    const bool dayChanged = day != planDay;
    const bool modeChanged = mode != planMode;
    bool markerChanged = timeChanged;
    if (timeChanged) {
        planMinute = minute;
        char caption[24];
        std::snprintf(caption, sizeof(caption), "Teraz %02u:%02u", minute / 60u, minute % 60u);
        lv_label_set_text(planNowLabel, caption);
        int x = kPlanX + ui_plan::minutePosition(minute, kPlanWidth) - 55;
        x = x < kPlanX ? kPlanX : x > kPlanX + kPlanWidth - 110 ? kPlanX + kPlanWidth - 110 : x;
        lv_obj_set_x(planNowBadge, x);
    }
    if (dayChanged || modeChanged) {
        const char *weekdays[] = {"Poniedziałek", "Wtorek", "Środa",    "Czwartek",
                                  "Piątek",       "Sobota", "Niedziela"};
        char dayText[48];
        std::snprintf(dayText, sizeof(dayText), "%s%s", weekdays[day],
                      mode == 2   ? " / Serwis"
                      : mode == 1 ? " / Karmienie"
                                  : "");
        lv_label_set_text(planDayLabel, dayText);
    }
    for (unsigned index = 0; index < kPlanRows; ++index) {
        auto &previous = planCache[index];
        const auto &item = module(kPlanIds[index]);
        const LightProgram program = index < 2 ? snapshot().lightPrograms[index] : LightProgram{};
        if (!previous.ready || modeChanged || previous.state != item.state ||
            previous.known != item.known || previous.transitioning != item.transitioning ||
            previous.lightMode != item.lightMode)
            markerChanged = true;
        const auto &schedule = snapshot().schedules[static_cast<unsigned>(kPlanIds[index])];
        const bool scheduleChanged =
            !previous.ready || dayChanged || previous.schedule.startMinute != schedule.startMinute ||
            previous.schedule.endMinute != schedule.endMinute || previous.schedule.mode != schedule.mode ||
            previous.schedule.dayMask != schedule.dayMask || previous.program.enabled != program.enabled ||
            previous.program.dayStartMinute != program.dayStartMinute ||
            previous.program.duskStartMinute != program.duskStartMinute ||
            previous.program.nightStartMinute != program.nightStartMinute;
        previous.schedule = schedule;
        previous.program = program;
        previous.state = item.state;
        previous.lightMode = item.lightMode;
        previous.known = item.known;
        previous.transitioning = item.transitioning;
        previous.ready = true;
        if (scheduleChanged)
            lv_obj_invalidate(planRails[index]);
    }
    const FeedingConfig &feeding = snapshot().feeding;
    const bool feedingChanged = !planFeedingReady || dayChanged ||
                                planFeedingCache.dayMask != feeding.dayMask ||
                                planFeedingCache.count != feeding.count ||
                                planFeedingCache.times[0] != feeding.times[0] ||
                                planFeedingCache.times[1] != feeding.times[1];
    if (feedingChanged) {
        lv_obj_invalidate(planFeedingRail);
        planFeedingCache = feeding;
        planFeedingReady = true;
    }
    markerChanged = markerChanged || modeChanged;
    planDay = day;
    planMode = mode;
    if (markerChanged)
        lv_obj_invalidate(planMarker);
}
void plan() {
    label(body, "Rytm dobowy", 0, 0, 250, &ui_font_24);
    planDayLabel = label(body, "", 0, 29, 150, &ui_font_14, muted());
    lv_label_set_long_mode(planDayLabel, LV_LABEL_LONG_DOT);
    planNowBadge = box(body, kPlanX, 0, 110, 25, planMarkerColor(), 4);
    planNowLabel = label(planNowBadge, "", 5, 4, 100, &ui_font_14,
                         ui_theme::readableText(planMarkerColor()));
    lv_obj_set_style_text_align(planNowLabel, LV_TEXT_ALIGN_CENTER, 0);
    for (int hour = 0; hour <= 24; hour += 2) {
        char caption[8];
        snprintf(caption, sizeof(caption), "%d:00", hour);
        const int tick = kPlanX + ui_plan::minutePosition(hour * 60, kPlanWidth);
        lv_obj_t *hourLabel = label(body, caption, tick - 22, 34, 44, &ui_font_12, muted());
        lv_obj_set_style_text_align(hourLabel, LV_TEXT_ALIGN_CENTER, 0);
    }
    for (unsigned i = 0; i < kPlanRows; ++i) {
        ModuleId id = kPlanIds[i];
        int y = kPlanFirstRow + i * kPlanRowHeight;
        lv_obj_t *row = box(body, 0, y, 752, kPlanRowHeight, bg());
        clickable(row, Action::Schedule, static_cast<int>(id));
        label(row, metadata(id).title, 0, 12, 170, &ui_font_16);
        planRails[i] = box(row, kPlanX, 14, kPlanWidth, 16, bg());
        lv_obj_set_style_bg_opa(planRails[i], LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(planRails[i], drawPlanRail, LV_EVENT_DRAW_MAIN,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    }
    lv_obj_t *feedingRow = box(body, 0, kPlanFirstRow + kPlanRows * kPlanRowHeight, 752,
                               kPlanRowHeight, bg());
    clickable(feedingRow, Action::EditFeeding);
    label(feedingRow, "Karmienie", 0, 12, 170, &ui_font_16);
    planFeedingRail = box(feedingRow, kPlanX, 14, kPlanWidth, 16, bg());
    lv_obj_set_style_bg_opa(planFeedingRail, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(planFeedingRail, drawPlanFeedingRail, LV_EVENT_DRAW_MAIN, nullptr);
    // A non-interactive foreground layer preserves row hit targets and swipe bubbling.
    planMarker = box(body, kPlanX - 4, kPlanFirstRow - 12, kPlanWidth + 8,
                     (kPlanRows + 1) * kPlanRowHeight + 2, bg());
    lv_obj_set_style_bg_opa(planMarker, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(planMarker, drawPlanMarker, LV_EVENT_DRAW_MAIN, nullptr);
}
void modeSelector() {
    const Module &m = module(u.selected);
    label(body, "Tryb zadany", 374, 60, 350, &ui_font_14, muted());
    label(body, snapshot().serviceActive ? "SERWIS" : modeName(m.mode), 374, 88, 100, &ui_font_20, teal);
    button(body, "Sterowanie w serwisie →", 489, 80, 255, 48, Action::Service);
}
void modulePage() {
    const Metadata &m = metadata(u.selected);
    const Snapshot &s = snapshot();
    heading(m.title, "Tryb zadany i stan wykonania są pokazywane osobno");
    rule(body, 344, 64, 1, lineColor(), 242);
    label(body, "STAN WYKONANIA", 0, 69, 316, &ui_font_14, muted());
    dot(body, 0, 110, stateColor(u.selected), module(u.selected).state == State::On);
    lv_obj_t *v = label(body, moduleState(u.selected), 22, 99, 310, &ui_font_24, stateColor(u.selected));
    bind(v, 3, u.selected);
    if (m.controllable)
        modeSelector();
    switch (u.selected) {
    case ModuleId::FrontLight:
    case ModuleId::RearLight: {
        label(body, "Profil Aquael", 0, 151, 310, &ui_font_14, muted());
        label(body, lightModeName(module(u.selected).lightMode), 0, 180, 320, &ui_font_24);
        label(body, "Zmiana: reset → profil → stabilizacja", 0, 230, 316, &ui_font_14, muted());
        label(body,
              "W serwisie jedna lampa jest obowiązkowa.\nDrugą możesz włączać niezależnie.\nPlan lamp "
              "pozostaje zapisany.",
              374, 180, 370, &ui_font_16, muted());
        button(body, "Plan tej lampy →", 0, 281, 320, 44, Action::Schedule, static_cast<int>(u.selected));
        break;
    }
    case ModuleId::Heater: {
        char value[36], temp[16];
        decimal(temp, sizeof(temp), s.temperature.value, 1);
        snprintf(value, sizeof(value), s.temperature.valid && !s.temperature.stale ? "%s°C" : "–", temp);
        label(body, value, 0, 151, 320, &ui_font_44);
        label(body, "DS18B20 / ochrona 28,5°C", 0, 219, 320, &ui_font_16, muted());
        button(body, "Zakres normy →", 0, 281, 320, 44, Action::EditRange, 0);
        button(body, "Nastawy termostatu →", 374, 184, 370, 48, Action::EditHeater);
        label(body, "Brak pomiaru, przegrzanie lub limit czasu\nwymuszają odcięcie grzania.", 374, 249, 370,
              &ui_font_16, muted());
        break;
    }
    case ModuleId::Feeder:
        label(body, "Jedna dawka = ustawiony kąt", 0, 161, 320, &ui_font_20);
        label(body, "Pauza dotyczy wybranych urządzeń.\nPo jej zakończeniu wraca AUTO.", 0, 205, 320,
              &ui_font_16, muted());
        button(body, "Karmienie teraz", 374, 184, 370, 48, Action::Feed, 0, true);
        button(body, "Ustawienia karmnika →", 374, 247, 370, 48, Action::Setting,
               static_cast<int>(Setting::Feeder));
        break;
    case ModuleId::Ph:
    case ModuleId::Ec: {
        bool ec = u.selected == ModuleId::Ec;
        sensor(body, ec ? "EC / PRZEWODNOŚĆ" : "pH / KWASOWOŚĆ", ec ? s.ec : s.ph,
               ec ? "µS/cm, odniesienie 25°C" : "jednostka pH", 0, 153, 320, ec ? 2 : 1, true);
        label(body, ec ? "Kompensacja temperatury" : "Kontrola jakości pomiaru", 374, 72, 370, &ui_font_20);
        label(body,
              ec ? "Współczynnik: 0,019 / °C\nBufor wzorcowy: 1413 µS/cm"
                 : "Bufory: 4,01 i 6,86 / 7,00\nKalibracja dwupunktowa",
              374, 113, 370, &ui_font_16, muted());
        button(body, "Zakres normy →", 374, 184, 370, 44, Action::EditRange, ec ? 2 : 1);
        label(body, "Kalibracja sondy wymaga połączenia\nz przetwornikiem ADS1115 na WeMos.", 374, 245, 370,
              &ui_font_16, muted());
        break;
    }
    case ModuleId::Ato:
        tank(body, 0, 150, 320, 154);
        label(body, "Dwa niezależne pływaki", 374, 75, 370, &ui_font_20);
        label(body, "MIN: żądanie uzupełnienia\nMAX: normalny koniec dolewania", 374, 119, 370, &ui_font_16,
              muted());
        label(body, "ATO / konfiguracja niepełna", 374, 196, 370, &ui_font_20, amber);
        label(body,
              "Nie przypisano wyjścia pompy.\nBrak danych MAX blokuje dolewanie.\nMAX nie jest alarmem "
              "przepełnienia.",
              374, 234, 370, &ui_font_16, muted());
        break;
    case ModuleId::Flow:
        label(body, s.flowValid ? (s.flowDetected ? "Impulsy obecne" : "Brak impulsów") : "Brak danych", 0,
              162, 320, &ui_font_24);
        label(body, "Informacja o ruchu wody", 374, 77, 370, &ui_font_20);
        label(body,
              "Czujnik sprawdza obecność impulsów.\nDo wartości l/h potrzebna jest kalibracja\ni "
              "przelicznik konkretnego czujnika.",
              374, 128, 370, &ui_font_16, muted());
        button(body, "Zobacz filtr →", 374, 242, 370, 48, Action::Module, 2);
        break;
    case ModuleId::Filter:
    case ModuleId::Air:
    case ModuleId::Co2:
        label(body,
              u.selected == ModuleId::Filter ? "Przepływ kontroluje pracę filtra"
              : u.selected == ModuleId::Co2  ? "Dozowanie w rytmie światła"
                                             : "Natlenianie w rytmie dobowym",
              0, 165, 320, &ui_font_20);
        label(body,
              u.selected == ModuleId::Co2
                  ? "Pauza podczas karmienia i serwisu.\nStan pH jest dostępny w pomiarach."
                  : "AUTO korzysta z harmonogramu.\nBlokady mają priorytet nad planem.",
              0, 218, 320, &ui_font_16, muted());
        button(body, "Edytuj plan →", 374, 192, 370, 48, Action::Schedule, static_cast<int>(u.selected));
        button(body, u.selected == ModuleId::Co2 ? "Pomiar pH →" : "Przepływ →", 374, 254, 370, 48,
               Action::Module, u.selected == ModuleId::Co2 ? 7 : 10);
        break;
    case ModuleId::Count:
        break;
    }
}
void days(uint8_t mask, int y) {
    for (int i = 0; i < 7; ++i)
        button(body, dayNames[i], i * 72, y, 64, 44, Action::Day, i, (mask & (1 << i)) != 0);
}
void editFooter() {
    saveLabel = label(body,
                      savePending     ? "Zapisywanie"
                      : editDirty     ? "Oczekiwanie na zapis"
                      : saveConfirmed ? "Zapisano automatycznie"
                                      : "Zapis automatyczny",
                      0, 294, saveFailed ? 418 : 752, &ui_font_14, saveFailed ? danger : muted());
    if (saveFailed) {
        lv_label_set_text(saveLabel, "Nie zapisano / popraw dane");
        button(body, "Cofnij", 436, 281, 142, 44, Action::RevertChanges);
        button(body, "Ponów", 594, 281, 158, 44, Action::RetrySave, 0, true);
    }
}
void schedulePage() {
    heading(metadata(u.selected).title, "Zmiany zapisują się automatycznie / harmonogram lokalny");
    const bool lamp = u.selected == ModuleId::FrontLight || u.selected == ModuleId::RearLight;
    const char *modes[] = {"Wyłączony", "Godziny", "Całą dobę"};
    for (int i = 0; i < 3; ++i)
        button(body, modes[i], i * (lamp ? 172 : 180), 62, lamp ? 160 : 168, 44, Action::ScheduleMode, i,
               u.schedule.mode == static_cast<ScheduleMode>(i));
    if (lamp) {
        button(body, u.lightProgram.enabled ? "3 tryby: WŁ." : "3 tryby: WYŁ.", 530, 62, 222, 44,
               Action::LightProfileToggle, 0, u.lightProgram.enabled,
               u.schedule.mode != ScheduleMode::Timed);
        const uint16_t minutes[] = {u.schedule.startMinute, u.lightProgram.dayStartMinute,
                                    u.lightProgram.duskStartMinute, u.lightProgram.nightStartMinute,
                                    u.schedule.endMinute};
        const char *names[] = {"Świt", "Dzień", "Zmierzch", "Noc", "Wyłączenie"};
        for (int i = 0; i < 5; ++i) {
            char time[8], caption[40];
            formatTime(time, sizeof(time), minutes[i]);
            std::snprintf(caption, sizeof(caption), "%s\n%s", names[i], time);
            const int field = i == 0 ? 0 : i == 4 ? 1 : i + 3;
            button(body, caption, i * 153, 125, 140, 62, Action::Time, field, false,
                   u.schedule.mode != ScheduleMode::Timed ||
                       ((i > 0 && i < 4) && !u.lightProgram.enabled));
        }
        label(body, "Świt i zmierzch używają tego samego trybu świetlówki.", 0, 194, 752,
              &ui_font_14, muted());
        days(u.schedule.dayMask, 230);
        editFooter();
        return;
    }
    char a[8], b[8], text[40];
    formatTime(a, sizeof(a), u.schedule.startMinute);
    formatTime(b, sizeof(b), u.schedule.endMinute);
    snprintf(text, sizeof(text), "Od %s", a);
    button(body, text, 0, 122, 246, 56, Action::Time, 0, false, u.schedule.mode != ScheduleMode::Timed);
    snprintf(text, sizeof(text), "Do %s", b);
    button(body, text, 262, 122, 246, 56, Action::Time, 1, false, u.schedule.mode != ScheduleMode::Timed);
    label(body,
          u.schedule.startMinute > u.schedule.endMinute ? "Przedział przechodzi przez północ"
                                                        : "Przedział w obrębie jednej doby",
          532, 130, 212, &ui_font_14, muted());
    days(u.schedule.dayMask, 208);
    editFooter();
}
struct NumericSpec {
    const char *title, *unit;
    int minimum, maximum, scale;
};
NumericSpec numericSpec(int index) {
    switch (index) {
    case 0:
        return {"Temperatura zadana", "°C", 180, 280, 10};
    case 1:
        return {"Histereza grzałki", "°C", 1, 20, 10};
    case 2:
        return {"Pauza karmienia", "s", 1, 3600, 1};
    case 4:
        return {"Wygaszanie treści", "min", 0, 60, 1};
    case 6:
        return {"Czas serwisu", "min", 1, 120, 1};
    case 7:
        return {"Obrót jednej dawki", "°", 1, 360, 1};
    case 8:
        return {"Czas ręcznego sterowania", "min", 1, 1440, 1};
    case 9:
        return {"Odstęp między dawkami", "min", 1, 60, 1};
    default:
        return {nullptr, nullptr, 0, 0, 1};
    }
}
void numericText(char *text, size_t capacity, int value, const NumericSpec &spec) {
    if (u.valueIndex == 4 && value == 0) {
        std::snprintf(text, capacity, "Wyłączone");
        return;
    }
    if (spec.scale == 10)
        std::snprintf(text, capacity, "%d,%d %s", value / 10, value % 10, spec.unit);
    else
        std::snprintf(text, capacity, "%d %s", value, spec.unit);
}
void refreshNumeric() {
    const NumericSpec spec = numericSpec(u.valueIndex);
    if (!spec.title)
        return;
    if (u.valueDraft < spec.minimum)
        u.valueDraft = spec.minimum;
    if (u.valueDraft > spec.maximum)
        u.valueDraft = spec.maximum;
    if (numericSlider)
        lv_slider_set_value(numericSlider, u.valueDraft, LV_ANIM_OFF);
    if (numericLabel) {
        char text[32];
        numericText(text, sizeof(text), u.valueDraft, spec);
        lv_label_set_text(numericLabel, text);
    }
}
void openNumeric(int index) {
    if (!numericSpec(index).title)
        return;
    u.valueIndex = static_cast<uint8_t>(index);
    u.valueOrigin = u.page;
    u.valueDialog = u.dialog;
    switch (index) {
    case 0:
        u.valueDraft = static_cast<int>(std::round(u.heater.target * 10));
        break;
    case 1:
        u.valueDraft = static_cast<int>(std::round(u.heater.hysteresis * 10));
        break;
    case 2:
        u.valueDraft = u.feeding.pauseSeconds;
        break;
    case 4:
        u.valueDraft = u.prefDraft.sleepMinutes;
        break;
    case 6:
        u.valueDraft = u.serviceMinutes;
        break;
    case 7:
        u.valueDraft = u.feeder.rotationDegrees;
        break;
    case 8:
        u.valueDraft = u.manualMinutes;
        break;
    case 9:
        u.valueDraft = u.feeder.minimumIntervalMinutes;
        break;
    default:
        return;
    }
    u.valueOriginal = u.valueDraft;
    u.dialog = Dialog::None;
    openPage(Page::Numeric);
}
void numericPage() {
    const NumericSpec spec = numericSpec(u.valueIndex);
    if (!spec.title) {
        u.page = u.valueOrigin;
        u.rebuild = true;
        return;
    }
    button(body, "←", 0, 0, 48, 44, Action::ValueAccept);
    label(body, spec.title, 64, 0, 600, &ui_font_24);
    label(body, "Powrót zatwierdza wartość / suwak i dokładna korekta", 64, 29, 680, &ui_font_14, muted());
    box(body, 0, 62, 752, 218, chartCardColor(), 12);
    button(body, "-", 24, 87, 64, 56, Action::ValueStep, 0);
    numericLabel = label(body, "", 112, 90, 528, &ui_font_44);
    lv_obj_set_style_text_align(numericLabel, LV_TEXT_ALIGN_CENTER, 0);
    button(body, "+", 664, 87, 64, 56, Action::ValueStep, 1);
    numericSlider = lv_slider_create(body);
    lv_obj_remove_style_all(numericSlider);
    lv_obj_set_pos(numericSlider, 50, 183);
    lv_obj_set_size(numericSlider, 652, 18);
    lv_obj_set_ext_click_area(numericSlider, 24);
    lv_obj_set_style_bg_color(numericSlider, lv_color_hex(lineColor()), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(numericSlider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(numericSlider, 9, LV_PART_MAIN);
    lv_obj_set_style_bg_color(numericSlider, lv_color_hex(selectedFill()), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(numericSlider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(numericSlider, 9, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(numericSlider, lv_color_hex(theme().accent), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(numericSlider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(numericSlider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(numericSlider, 8, LV_PART_KNOB);
    lv_obj_clear_flag(numericSlider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_slider_set_range(numericSlider, spec.minimum, spec.maximum);
    lv_obj_add_event_cb(
        numericSlider,
        [](lv_event_t *event) {
            u.valueDraft = lv_slider_get_value(lv_event_get_target(event));
            refreshNumeric();
        },
        LV_EVENT_VALUE_CHANGED, nullptr);
    char text[32];
    numericText(text, sizeof(text), spec.minimum, spec);
    label(body, text, 30, 223, 320, &ui_font_16, muted());
    numericText(text, sizeof(text), spec.maximum, spec);
    lv_obj_t *maximum = label(body, text, 402, 223, 320, &ui_font_16, muted());
    lv_obj_set_style_text_align(maximum, LV_TEXT_ALIGN_RIGHT, 0);
    char precision[64];
    if (spec.scale == 10)
        std::snprintf(precision, sizeof(precision), "Dokładność korekty: 0,1°C");
    else
        std::snprintf(precision, sizeof(precision), "Dokładność korekty: 1 %s", spec.unit);
    label(body, precision, 24, 294, 704, &ui_font_14, muted());
    refreshNumeric();
}
void commitNumeric() {
    refreshNumeric();
    const bool changed = u.valueDraft != u.valueOriginal;
    switch (u.valueIndex) {
    case 0:
        u.heater.target = u.valueDraft / 10.0f;
        break;
    case 1:
        u.heater.hysteresis = u.valueDraft / 10.0f;
        break;
    case 2:
        u.feeding.pauseSeconds = static_cast<uint16_t>(u.valueDraft);
        break;
    case 4:
        u.prefDraft.sleepMinutes = static_cast<uint8_t>(u.valueDraft);
        break;
    case 6:
        u.serviceMinutes = static_cast<uint16_t>(u.valueDraft);
        break;
    case 7:
        u.feeder.rotationDegrees = static_cast<uint16_t>(u.valueDraft);
        break;
    case 8:
        u.manualMinutes = static_cast<uint16_t>(u.valueDraft);
        break;
    case 9:
        u.feeder.minimumIntervalMinutes = static_cast<uint16_t>(u.valueDraft);
        break;
    default:
        return;
    }
    u.page = u.valueOrigin;
    u.dialog = u.valueDialog;
    u.valueDialog = Dialog::None;
    if (changed && u.valueIndex != 6 && u.valueIndex != 8)
        markEdited();
    u.rebuild = true;
}
void stepper(const char *caption, const char *value, int x, int y, int w, int index) {
    label(body, caption, x, y, w, &ui_font_14, muted());
    if (index == 0 || index == 1 || index == 2 || index == 4 || index == 6 || index == 7) {
        lv_obj_t *control = button(body, value, x, y + 27, w, 50, Action::EditValue, index);
        lv_obj_t *text = lv_obj_get_child(control, 0);
        lv_obj_set_style_text_font(text, &ui_font_24, 0);
        lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_width(text, w - 64);
        lv_obj_align(text, LV_ALIGN_LEFT_MID, 16, 0);
        label(control, "→", w - 38, 13, 24, &ui_font_20, theme().accent);
    } else {
        button(body, "-", x, y + 27, 54, 50, Action::Adjust, index * 2);
        label(body, value, x + 66, y + 37, w - 136, &ui_font_24);
        button(body, "+", x + w - 54, y + 27, 54, 50, Action::Adjust, index * 2 + 1);
    }
}
void heaterPage() {
    heading("Termostat", "Nastawy są robocze do momentu zapisu");
    char target[16], hyst[16], value[32];
    decimal(target, sizeof(target), u.heater.target, 1);
    decimal(hyst, sizeof(hyst), u.heater.hysteresis, 1);
    snprintf(value, sizeof(value), "%s°C", target);
    stepper("TEMPERATURA ZADANA", value, 0, 77, 332, 0);
    snprintf(value, sizeof(value), "%s°C", hyst);
    stepper("HISTEREZA", value, 396, 77, 332, 1);
    rule(body, 0, 181, 752);
    label(body, "OCHRONA / 28,5°C", 0, 205, 250, &ui_font_20, amber);
    label(body,
          "Zakres celu: 18,0–28,0°C. Brak wiarygodnego pomiaru blokuje grzanie.\nFizyczne zabezpieczenia "
          "wykonuje WeMos niezależnie od panelu.",
          280, 198, 460, &ui_font_16, muted());
    editFooter();
}
void feedingPage() {
    heading("Plan karmienia", "Godziny i dni / parametry dawki znajdziesz w System → Karmnik");
    char a[8], b[8], text[32];
    formatTime(a, sizeof(a), u.feeding.times[0]);
    formatTime(b, sizeof(b), u.feeding.times[1]);
    snprintf(text, sizeof(text), "Dawka 1 / %s", a);
    button(body, text, 0, 65, 236, 50, Action::Time, 2);
    snprintf(text, sizeof(text), "Dawka 2 / %s", b);
    button(body, text, 250, 65, 236, 50, Action::Time, 3, false, u.feeding.count != 2);
    button(body, u.feeding.count == 2 ? "2 dawki / dzień" : "1 dawka / dzień", 500, 65, 252, 50,
           Action::Toggle, 10, u.feeding.count == 2);
    label(body, "Automatyczne karmienie", 0, 143, 380, &ui_font_20);
    label(body,
          "Zmiana zegara nie odtwarza pominiętych dawek.\nJedno karmienie naraz / brak automatycznego "
          "ponawiania.",
          0, 180, 752, &ui_font_16, muted());
    days(u.feeding.dayMask, 226);
    editFooter();
}
void alarmRow(lv_obj_t *parent, int y, const char *title, const char *impact, const char *action,
              ModuleId id, bool critical = false) {
    const uint32_t color = critical ? danger : amber;
    rule(parent, 0, y, 4, color, 66);
    label(parent, title, 18, y, 520, &ui_font_20, color);
    label(parent, impact, 18, y + 30, 524, &ui_font_16, muted());
    button(parent, action, 560, y + 8, 184, 48, Action::Module, static_cast<int>(id));
}
void alarms() {
    const Snapshot &s = snapshot();
    heading("Centrum uwagi", s.alarmAcknowledged ? "Potwierdzono / przyczyny nadal wymagają sprawdzenia"
                                                 : "Przyczyna → wpływ na akwarium → następne działanie");
    lv_obj_t *list = box(body, 0, 63, 752, 204, bg());
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    int y = 0;
    if (s.alarmFlags & AlarmFeeder) {
        label(list, "Niepewne zakończenie karmienia", 0, y, 492, &ui_font_20, contentBad());
        label(list, "Sprawdź silnik i łącze. Dawka nie zostanie ponowiona.", 0, y + 30, 492, &ui_font_14,
              muted());
        button(list, "Sprawdziłem karmnik", 510, y + 8, 234, 48, Action::ResetFeederFault);
        y += 80;
    }
    if (s.alarmFlags & AlarmTemperature) {
        alarmRow(list, y, "Za wysoka temperatura / przegrzanie",
                 "Grzałka odcięta. Sprawdź wodę i termometr.", "Grzałka →", ModuleId::Heater, true);
        y += 76;
    }
    if (s.alarmFlags & AlarmPh) {
        alarmRow(list, y, "pH poza ustawionym zakresem", "Sprawdź sondę oraz dawkowanie CO2.",
                 "Pomiar pH →", ModuleId::Ph);
        y += 76;
    }
    if (s.alarmFlags & AlarmLevelUnknown) {
        alarmRow(list, y, "MIN / MAX / brak wiarygodnego stanu", "Dolewanie zablokowane. Sprawdź pływaki.",
                 "Poziom →", ModuleId::Ato);
        y += 76;
    }
    if (s.alarmFlags & AlarmOffline) {
        alarmRow(list, y, "Sterownik wykonawczy offline", "Stany wyjść nieznane. Sprawdź łącze UART.",
                 "Filtr →", ModuleId::Filter);
        y += 76;
    }
    if (s.alarmFlags & AlarmLowWater) {
        alarmRow(list, y, "Niski poziom wody", "MIN zgłasza niski poziom; pompa ATO nieprzypisana.",
                 "Poziom →", ModuleId::Ato);
        y += 76;
    }
    if (s.alarmFlags & AlarmLevelFault) {
        alarmRow(list, y, "Sprzeczne wskazania MIN i MAX", "Nie można potwierdzić poziomu wody.",
                 "Poziom →", ModuleId::Ato, true);
        y += 76;
    }
    if ((s.alarmFlags & AlarmTemperatureRange) && !(s.alarmFlags & AlarmTemperature)) {
        alarmRow(list, y, "Temperatura poza zakresem", "Porównaj ustawione granice z odczytem.",
                 "Temperatura →", ModuleId::Heater);
        y += 76;
    }
    if (s.alarmFlags & AlarmEcRange) {
        alarmRow(list, y, "EC poza zakresem", "Sprawdź przewodność i własne granice akwarium.",
                 "Pomiar EC →", ModuleId::Ec);
        y += 76;
    }
    if (s.alarmFlags & AlarmSensorUnknown)
        alarmRow(list, y, "Brak wiarygodnych odczytów", "Nie używaj ostatniej wartości jako aktualnej.",
                 "Czujnik →", ModuleId::Heater);
    if (!s.alarmFlags) {
        label(list, "Brak aktywnych alarmów", 0, 27, 752, &ui_font_24, teal);
        label(list, "Status akwarium jest zawsze dostępny na stronie Panel.", 0, 82, 752, &ui_font_16,
              muted());
    }
    button(body, s.alarmAcknowledged ? "Alarmy potwierdzone" : "Potwierdź odczytanie", 0, 281, 290, 44,
           Action::Alarm, 1, false, !s.alarmFlags || s.alarmAcknowledged);
    label(body, "Potwierdzenie nie usuwa przyczyny.", 318, 293, 420, &ui_font_14, muted());
}
void history() {
    const ui_history_view::Colors colors{
        bg(), chartCardColor(), ink(), muted(), lineColor(), contentGood(), theme().warning,
        contentBad(), theme().goodTint, theme().warningTint, theme().badTint, theme().badTint,
        selectedFill(), theme()};
    ui_history_view::create(body, u.chart, colors, [](ui_history_view::Command command, unsigned index) {
        dispatch(command == ui_history_view::Command::Sensor         ? Action::Chart
                 : command == ui_history_view::Command::ClearHistory ? Action::ClearHistory
                                                                     : Action::ClearLog,
                 static_cast<int>(index));
    });
}
void refreshHistory() { ui_history_view::refresh(); }
void calibration() {
    heading(u.calEc ? "Kalibracja EC" : "Kalibracja pH", "Przygotowanie → wzorce → stabilizacja");
    const char *steps[] = {"Przygotowanie", "Pierwszy wzorzec", "Stabilizacja", "Drugi wzorzec",
                           "Podsumowanie"};
    const char *ecSteps[] = {"Przygotowanie", "Wzorzec 1413", "Stabilizacja", "Podsumowanie"};
    int last = u.calEc ? 3 : 4;
    char text[64];
    snprintf(text, sizeof(text), "KROK %u / %d", u.calStep + 1, last + 1);
    label(body, text, 0, 70, 285, &ui_font_14, muted());
    label(body, (u.calEc ? ecSteps : steps)[u.calStep], 0, 104, 330, &ui_font_24);
    rule(body, 0, 148, 330, teal, 3);
    for (int i = 0; i <= last; ++i)
        dot(body, i * 40, 166, i <= u.calStep ? teal : lineColor(), i <= u.calStep);
    const char *instruction;
    if (u.calStep == 0)
        instruction = "Przygotuj świeże bufory.\nWypłucz sondę wodą destylowaną.\nDelikatnie osusz, nie "
                      "pocieraj elektrody.";
    else if (u.calStep == 1)
        instruction = u.calEc ? "Zanurz sondę we wzorcu 1413 µS/cm.\nUwzględnij temperaturę "
                                "roztworu.\nUnikaj pęcherzyków na elektrodach."
                              : "Zanurz sondę w buforze 4,01 pH.\nOdczekaj do ustabilizowania "
                                "wskazania.\nNie mieszaj roztworów wzorcowych.";
    else if (u.calStep == 2)
        instruction = "Poczekaj na stabilny odczyt.\nNie poruszaj sondą podczas pomiaru.\nUnikaj "
                      "pęcherzyków na elektrodzie.";
    else if (!u.calEc && u.calStep == 3)
        instruction = u.buffer686 ? "Wypłucz sondę. Zanurz w buforze 6,86.\nOdczekaj do ustabilizowania "
                                    "odczytu.\nNie mieszaj roztworów wzorcowych."
                                  : "Wypłucz sondę. Zanurz w buforze 7,00.\nOdczekaj do ustabilizowania "
                                    "odczytu.\nNie mieszaj roztworów wzorcowych.";
    else
        instruction = "Procedura zakończona.\nIntegracja zapisu kalibracji jest nieaktywna.\nParametry "
                      "fizycznej sondy nie zostały zmienione.";
    label(body, instruction, 380, 80, 366, &ui_font_16, muted());
    if (!u.calEc && u.calStep == 0)
        button(body, u.buffer686 ? "Drugi bufor: 6,86" : "Drugi bufor: 7,00", 380, 210, 366, 48,
               Action::CalBuffer);
    button(body, "Anuluj", 0, 281, 170, 44, Action::Cancel);
    button(body, u.calStep == last ? "Zakończ" : "Dalej →", 512, 281, 240, 44, Action::CalNext, 0, true);
}
void system() {
    const char *groups[] = {"Obsługa", "Sterowanie", "Sieć i czas", "Urządzenie", "Diagnostyka"};
    const char *titles[] = {"Wygląd i obsługa", "Sterowanie akwarium", "Połączenie i czas",
                             "Utrzymanie urządzenia", "Stan techniczny"};
    const char *descriptions[] = {"Wygląd, dotyk i dostęp", "Dawka, termostat, plan i moduły",
                                   "Połączenie i synchronizacja", "Firmware, zasilanie i restart",
                                   "Stan panelu, modułu, testy i dziennik"};
    const int entries[5][4] = {
        {1, 13, 3, 4}, {10, 100, 101, 5}, {0, 2, -1, -1}, {7, 8, -1, -1}, {6, 11, 12, 9}};
    const int counts[] = {4, 4, 2, 2, 4};
    char networkNote[64], displayNote[64], feederNote[64], thermostatNote[64];
    std::snprintf(networkNote, sizeof(networkNote), "%s",
                  u.prefs.network == ui_runtime::NetworkMode::Off           ? "Radio wyłączone"
                  : u.prefs.network == ui_runtime::NetworkMode::AccessPoint ? "Punkt AquaBox AP"
                                                                            : u.prefs.ssid);
    std::snprintf(displayNote, sizeof(displayNote), "%s / wygaszanie %u min",
                  ui_runtime::nightTheme(u.prefs) ? "Paleta nocna" : "Paleta jasna", u.prefs.sleepMinutes);
    std::snprintf(feederNote, sizeof(feederNote), "%u° na dawkę / pauza %u s",
                  snapshot().feeder.rotationDegrees, snapshot().feeding.pauseSeconds);
    std::snprintf(thermostatNote, sizeof(thermostatNote), "Cel %.1f°C / histereza %.1f°C",
                  static_cast<double>(snapshot().heater.target),
                  static_cast<double>(snapshot().heater.hysteresis));
    const char *notes[] = {networkNote,
                           displayNote,
                           u.prefs.ntp ? "NTP / Europe/Warsaw" : "Ręcznie / Europe/Warsaw",
                           u.prefs.reducedMotion ? "Ograniczone animacje"
                                                 : "Pełne animacje i potwierdzenia",
                           u.prefs.lock ? "Edycja chroniona PIN-em" : "Edycja bez PIN-u",
                           "11 modułów / dwa pływaki",
                           "Pamięć panelu, dotyk i dziennik",
                           "Aktualizacja USB / kontrola obrazu",
                           "Restart lub przywrócenie ustawień",
                           "Kontrolowane scenariusze lokalne",
                           feederNote,
                           "UART niepodłączony / dane niedostępne",
                           "Podgląd i czyszczenie lokalnych zdarzeń",
                           "Motywy i własne kolory obszarów"};
    // The root remains a calm index of settings. Detail is intentionally moved into
    // individual pages so the operator can scan categories without a second tab bar.
    box(body, 0, 0, 208, 326, wash(), ui_design::radius::card);
    box(body, 224, 0, 528, 326, chartCardColor(), ui_design::radius::card);
    label(body, "Ustawienia", 16, 18, 176, &ui_font_24);
    for (int group = 0; group < 5; ++group) {
        lv_obj_t *tab = button(body, groups[group], 8, 66 + group * 50, 192, 49,
                                Action::SystemCategory, group, u.systemCategory == group);
        if (u.systemCategory == group) {
            lv_obj_set_style_bg_color(tab, lv_color_hex(selectedFill()), 0);
            setLegibleText(lv_obj_get_child(tab, 0), ink());
        }
    }
    label(body, titles[u.systemCategory], 244, 18, 480, &ui_font_24);
    label(body, descriptions[u.systemCategory], 244, 52, 480, &ui_font_14, muted());
    const int count = counts[u.systemCategory];
    constexpr int rowHeight = 60;
    for (int row = 0; row < count; ++row) {
        const int index = entries[u.systemCategory][row];
        const Action action = index == 100   ? Action::EditHeater
                              : index == 101 ? Action::Nav
                                             : Action::Setting;
        const int y = 84 + row * rowHeight;
        lv_obj_t *entry = box(body, 244, y, 492, rowHeight, chartCardColor());
        clickable(entry, action, index == 101 ? static_cast<int>(Tab::Plan) : index == 100 ? 0 : index);
        lv_obj_set_style_bg_opa(entry, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(entry, lv_color_hex(wash()), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(entry, LV_OPA_COVER, LV_STATE_PRESSED);
        const char *title = index == 100 ? "Termostat" : index == 101 ? "Plan dnia" : settingNames[index];
        const char *summary = index == 100   ? thermostatNote
                              : index == 101 ? "Godziny, dni i bieżące stany wyjść"
                                             : notes[index];
        // Identical row geometry keeps text and touch targets stable across categories.
        label(entry, title, 0, 6, 438, ui_design::typography::section());
        lv_obj_t *note = label(entry, summary, 0, 34, 438,
                               ui_design::typography::caption(), muted());
        lv_label_set_long_mode(note, LV_LABEL_LONG_DOT);
        label(entry, "→", 456, 18, 24, ui_design::typography::section(), theme().accent);
        if (row + 1 < count)
            rule(body, 260, y + rowHeight - 1, 460);
    }
}
void onTextarea(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_FOCUSED)
        return;
    if (networkEditor)
        return;
    if (savePending) {
        return;
    }
    networkEditTarget = lv_event_get_target(e);
    const bool pin = networkEditTarget == securityPin;
    bool password = networkEditTarget == networkPassword || pin;
    networkEditor = box(lv_layer_top(), 0, 0, 800, 480, bg());
    lv_obj_add_flag(networkEditor, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(networkEditor, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(networkEditor, onGesture, LV_EVENT_GESTURE, nullptr);
    label(networkEditor,
          pin        ? "PIN / 4 cyfry"
          : password ? "Sieć / hasło"
                     : "Sieć / nazwa SSID",
          24, 24, 752, &ui_font_24);
    label(networkEditor, "Zatwierdź haczykiem. Klawisz z ikoną klawiatury anuluje edycję.", 24, 68, 752,
          &ui_font_16, muted());
    networkEditField = lv_textarea_create(networkEditor);
    lv_obj_set_pos(networkEditField, 24, 124);
    lv_obj_set_size(networkEditField, 752, 56);
    lv_textarea_set_one_line(networkEditField, true);
    lv_textarea_set_max_length(networkEditField,
                               pin ? 4
                               : password
                                   ? (u.prefDraft.network == ui_runtime::NetworkMode::AccessPoint ? 16 : 64)
                                   : 32);
    if (pin)
        lv_textarea_set_accepted_chars(networkEditField, "0123456789");
    lv_textarea_set_password_mode(networkEditField, password);
    lv_textarea_set_text(networkEditField, lv_textarea_get_text(networkEditTarget));
    lv_obj_set_style_text_font(networkEditField, &ui_font_20, 0);
    lv_obj_set_style_text_color(networkEditField,
                                lv_color_hex(ui_theme::readableForeground(ink(), wash())), 0);
    lv_obj_set_style_bg_color(networkEditField, lv_color_hex(wash()), 0);
    lv_obj_set_style_border_width(networkEditField, 0, 0);
    keyboard = lv_keyboard_create(networkEditor);
    lv_obj_set_size(keyboard, 800, 218);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(keyboard, networkEditField);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(wash()), 0);
    lv_obj_set_style_pad_all(keyboard, 8, 0);
    lv_obj_set_style_pad_row(keyboard, 4, 0);
    lv_obj_set_style_pad_column(keyboard, 4, 0);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(bg()), LV_PART_ITEMS);
    lv_obj_set_style_bg_grad_dir(keyboard, LV_GRAD_DIR_NONE, LV_PART_ITEMS);
    lv_obj_set_style_border_color(keyboard, lv_color_hex(lineColor()), LV_PART_ITEMS);
    lv_obj_set_style_border_width(keyboard, 1, LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard,
                                lv_color_hex(ui_theme::readableForeground(ink(), wash())), LV_PART_ITEMS);
    constexpr lv_style_selector_t checkedKey =
        static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_CHECKED;
    constexpr lv_style_selector_t pressedKey =
        static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_PRESSED;
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(selectedFill()), checkedKey);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(selectedFill()), pressedKey);
    lv_obj_set_style_text_color(keyboard,
                                lv_color_hex(ui_theme::readableForeground(ink(), selectedFill())), checkedKey);
    lv_obj_set_style_text_color(keyboard,
                                lv_color_hex(ui_theme::readableForeground(ink(), selectedFill())), pressedKey);
    lv_obj_add_event_cb(
        keyboard,
        [](lv_event_t *event) {
            if (lv_event_get_code(event) == LV_EVENT_READY || lv_event_get_code(event) == LV_EVENT_CANCEL) {
                const bool acceptedText = lv_event_get_code(event) == LV_EVENT_READY && networkEditTarget;
                if (acceptedText) {
                    const char *text = lv_textarea_get_text(networkEditField);
                    char *destination = networkEditTarget == solarCityField ? u.prefDraft.solarCity
                                        : networkEditTarget == securityPin   ? u.prefDraft.pin
                                        : networkEditTarget == networkSsid ? u.prefDraft.ssid
                                        : u.prefDraft.network == ui_runtime::NetworkMode::AccessPoint
                                            ? u.prefDraft.apPassword
                                            : u.prefDraft.password;
                    const size_t capacity = networkEditTarget == solarCityField ? sizeof(u.prefDraft.solarCity)
                                            : networkEditTarget == securityPin   ? sizeof(u.prefDraft.pin)
                                            : networkEditTarget == networkSsid ? sizeof(u.prefDraft.ssid)
                                            : u.prefDraft.network == ui_runtime::NetworkMode::AccessPoint
                                                ? sizeof(u.prefDraft.apPassword)
                                                : sizeof(u.prefDraft.password);
                    if (std::strlen(text) >= capacity) {
                        notify("Dane są zbyt długie w bajtach / skróć tekst", true);
                        return;
                    }
                    std::snprintf(destination, capacity, "%s", text);
                    lv_textarea_set_text(networkEditTarget, text);
                    if (networkEditTarget == securityPin && isFourDigitPin(text))
                        // A complete replacement PIN is an explicit security action. Enabling the
                        // guard here prevents silently storing an unused credential after first setup.
                        u.prefDraft.lock = true;
                }
                if (networkEditTarget)
                    lv_obj_clear_state(networkEditTarget, LV_STATE_FOCUSED);
                lv_keyboard_set_textarea(keyboard, nullptr);
                lv_obj_del_async(networkEditor);
                keyboard = nullptr;
                networkEditor = networkEditField = networkEditTarget = nullptr;
                if (acceptedText)
                    markEdited();
            }
        },
        LV_EVENT_ALL, nullptr);
}
lv_obj_t *textarea(const char *text, int y, bool password, int x = 16, int width = 452) {
    lv_obj_t *o = lv_textarea_create(body);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, width, 48);
    lv_textarea_set_one_line(o, true);
    lv_textarea_set_max_length(o, password ? 64 : 32);
    lv_textarea_set_password_mode(o, password);
    lv_textarea_set_text(o, text);
    lv_obj_set_style_text_font(o, &ui_font_16, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(wash()), 0);
    lv_obj_set_style_text_color(o, lv_color_hex(ui_theme::readableForeground(ink(), wash())), 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_add_event_cb(o, onTextarea, LV_EVENT_FOCUSED, nullptr);
    return o;
}
void clockSettings() {
    if (!u.clockEditing) {
        lv_obj_t *current = box(body, 0, 56, 368, 214, chartCardColor(), ui_design::radius::card);
        label(current, "AKTUALNY CZAS", 16, 14, 336, &ui_font_14, muted());
        clockLive = label(current, "", 16, 38, 336, &ui_font_44);
        clockDate = label(current, "", 16, 102, 336, &ui_font_24);
        clockOrigin = label(current,
                            panelStatus.clock.source == ui_status::ClockSource::System
                                ? "Źródło: zegar systemowy"
                                : "Czas niepotwierdzony / ustaw zegar",
                            16, 146, 336, &ui_font_14,
                            panelStatus.clock.source == ui_status::ClockSource::System ? muted() : amber);
        label(current, "Europe/Warsaw / DST / bez RTC", 16, 176, 336, &ui_font_14, muted());
        lv_obj_t *automatic = box(body, 384, 56, 368, 214, chartCardColor(),
                                  ui_design::radius::card);
        label(automatic, "ŹRÓDŁO CZASU", 16, 14, 336, &ui_font_14, muted());
        label(automatic, "Synchronizacja", 16, 40, 336, &ui_font_20);
        button(automatic, "Automatyczna", 16, 78, 160, 44, Action::Toggle, 14, u.prefDraft.ntp);
        button(automatic, "Ręczna", 192, 78, 160, 44, Action::Toggle, 15, !u.prefDraft.ntp);
        const bool online = panelStatus.wireless.mode == ui_status::WirelessMode::Home ||
                            panelStatus.wireless.mode == ui_status::WirelessMode::HomeAndAp;
        clockNetworkLabel =
            label(automatic,
                  online ? "Router dostępny / NTP może działać" : "Brak internetu / NTP wymaga routera", 16,
                  136, 336, &ui_font_14, online ? teal : amber);
        clockSyncButton =
            button(automatic, "Synchronizuj teraz", 16, 164, 336, 44, Action::ClockSync, 0, false,
                   !online);
        editFooter();
        if (!saveFailed) {
            button(body, "Zmień datę i czas", 384, 281, 368, 44, Action::ClockEdit, 0, true);
            lv_obj_set_width(saveLabel, 192);
        }
        return;
    }
    const int values[] = {u.clockDraft.year, u.clockDraft.month, u.clockDraft.day, u.clockDraft.hour,
                          u.clockDraft.minute};
    const char *fields[] = {"Rok", "Miesiąc", "Dzień", "Godzina", "Minuta"};
    const int order[] = {2, 1, 0, 3, 4};
    const int positions[] = {0, 128, 256, 428, 592};
    const int widths[] = {112, 112, 140, 148, 148};
    for (int column = 0; column < 5; ++column) {
        const int field = order[column];
        label(body, fields[field], positions[column], 64, widths[column], &ui_font_14, muted());
        char value[12];
        std::snprintf(value, sizeof(value), field == 0 ? "%04d" : "%02d", values[field]);
        lv_obj_t *control = button(body, value, positions[column], 89, widths[column], 60,
                                   Action::ClockField, field, field == u.clockField);
        lv_obj_set_style_text_font(lv_obj_get_child(control, 0), &ui_font_24, 0);
    }
    char selected[56];
    std::snprintf(selected, sizeof(selected), "Zmieniasz: %s", fields[u.clockField]);
    label(body, selected, 0, 174, 396, &ui_font_20);
    button(body, "-", 428, 163, 148, 52, Action::Adjust, 80);
    button(body, "+", 592, 163, 148, 52, Action::Adjust, 81);
    if (ui_runtime::ambiguousTime(u.clockDraft)) {
        button(body, "Czas letni", 0, 225, 180, 44, Action::ClockOccurrence, 1, u.clockDraft.dst == 1);
        button(body, "Czas zimowy", 196, 225, 180, 44, Action::ClockOccurrence, 0, u.clockDraft.dst == 0);
        label(body, "Godzina występuje dwukrotnie.\nWybierz jej pierwsze lub drugie wystąpienie.", 408, 225,
              340, &ui_font_14, amber);
    } else
        label(body,
              "Ustawienie ręczne wyłącza NTP. Nie odtwarza pominiętych karmień.\nZegar bez RTC nie zachowa "
              "czasu po utracie zasilania.",
              0, 231, 752, &ui_font_14, muted());
    button(body, "Ustaw zegar", 374, 281, 378, 44, Action::ClockSave, 0, true);
}
void colorSettings() {
    label(body, "WYBIERZ MOTYW DLA TRYBU", 0, 66, 456, &ui_font_14, muted());
    lv_obj_t *lightMode = button(body, "Jasny", 480, 56, 128, 40, Action::ColorMode, 0, !u.colorNight);
    lv_obj_t *darkMode = button(body, "Ciemny", 624, 56, 128, 40, Action::ColorMode, 1, u.colorNight);
    lv_obj_add_state(u.colorNight ? darkMode : lightMode, LV_STATE_CHECKED);
    const unsigned mode = u.colorNight ? 1u : 0u;
    for (unsigned preset = 0; preset < ui_theme::kPresetCount; ++preset) {
        const bool selected = u.prefDraft.colors.preset[mode] == preset;
        lv_obj_t *choice = box(body, (preset % 3) * 256, 104 + (preset / 3) * 90,
                              240, 84, chartCardColor(), ui_design::radius::card);
        clickable(choice, Action::ColorPreset, preset);
        lv_obj_set_style_bg_color(choice, lv_color_hex(selectedFill()), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(choice, selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(choice, lv_color_hex(selected ? theme().accent : lineColor()), 0);
        if (selected)
            lv_obj_add_state(choice, LV_STATE_CHECKED);
        ui_theme::Settings sample{};
        sample.preset[mode] = static_cast<uint8_t>(preset);
        const auto palette = ui_theme::resolve(sample, u.colorNight);
        lv_obj_t *preview = box(choice, 12, 13, 72, 58, palette.background, 5);
        box(preview, 0, 0, 72, 10, palette.chrome);
        box(preview, 0, 49, 72, 9, palette.navigation);
        box(preview, 6, 16, 27, 26, palette.card, 3);
        box(preview, 39, 16, 27, 26, palette.card, 3);
        box(preview, 43, 32, 19, 5, palette.accent, 2);
        label(choice, ui_theme::presetName(preset), 98, 12, 128, &ui_font_20);
        label(choice, preset == 0 ? "Oryginalny panel" : ui_theme::presetDescription(preset),
              98, 40, 128, &ui_font_14, muted());
    }
    editFooter();
}
void settings() {
    const char *subtitles[] = {"Wybierz sposób połączenia / konfiguracja zapisuje się automatycznie",
                               "Paleta i wygaszanie treści / podświetlenie ma stałe zasilanie",
                               "Aktualny czas, źródło i synchronizacja / Europe/Warsaw",
                               "Potwierdzenia działań i ruch na ekranie",
                               "Ochrona lokalnej edycji / przeglądanie pozostaje dostępne",
                               "Wybierz moduł, aby zobaczyć jego stan i dostępne ustawienia",
                               "Kondycja panelu i rzeczywisty stan integracji",
                               "Bieżąca aplikacja / kontrola integralności obrazu",
                               "Bezpieczne działania panelu / ustawienia sterownika są niezależne",
                               "Lokalne scenariusze czujników / nie sterują sprzętem",
                               "Dawka, odstęp i pauza / zapis automatyczny",
                               "Stan transportu i potwierdzeń głównego sterownika",
                               "Podgląd i czyszczenie lokalnych zdarzeń",
                               "Gotowe motywy / niezależny wybór dla trybu jasnego i ciemnego",
                               "Krótkie kroki serwisowe / napęd jeszcze niepodłączony"};
    heading(settingNames[static_cast<int>(u.setting)],
            u.setting == Setting::Clock && u.clockEditing
                ? "Zmiana zegara wymaga potwierdzenia / nie zmienia czasu podczas edycji"
                : subtitles[static_cast<unsigned>(u.setting)]);
    switch (u.setting) {
    case Setting::Colors:
        colorSettings();
        break;
    case Setting::Feeder: {
        button(body, "Ruch ręczny →", 528, 0, 224, 44, Action::Setting,
               static_cast<int>(Setting::FeederManual), false, editDirty || savePending || saveFailed);
        char degrees[24], pause[24];
        std::snprintf(degrees, sizeof(degrees), "%u°", u.feeder.rotationDegrees);
        std::snprintf(pause, sizeof(pause), "%u s", u.feeding.pauseSeconds);
        for (int column = 0; column < 3; ++column)
            box(body, column * 256, 56, 240, 112, chartCardColor(), ui_design::radius::card);
        stepper("OBRÓT JEDNEJ DAWKI", degrees, 16, 66, 208, 7);
        stepper("PAUZA PO DAWCE", pause, 272, 66, 208, 2);
        char interval[32];
        std::snprintf(interval, sizeof(interval), "Odstęp: %u min", u.feeder.minimumIntervalMinutes);
        label(body, "MINIMALNY ODSTĘP", 528, 66, 208, &ui_font_14, muted());
        button(body, interval, 528, 93, 208, 50, Action::EditValue, 9);
        label(body, "WYJŚCIA PODCZAS KARMIENIA", 0, 175, 344, &ui_font_14, muted());
        lv_obj_t *ruleNote = label(body, "Ręczna dawka zastępuje bliską automatyczną", 344, 175,
                                   408, &ui_font_14, muted());
        lv_obj_set_style_text_align(ruleNote, LV_TEXT_ALIGN_RIGHT, 0);
        const ModuleId outputs[] = {ModuleId::Filter, ModuleId::Air,        ModuleId::Heater,
                                    ModuleId::Co2,    ModuleId::FrontLight, ModuleId::RearLight};
        const char *names[] = {"Filtr", "Powietrze", "Grzałka", "CO2", "Lampa przednia", "Lampa tylna"};
        for (unsigned index = 0; index < 6; ++index) {
            const bool paused = u.feeder.pauseMask & outputBit(outputs[index]);
            char text[56];
            std::snprintf(text, sizeof(text), "%s / %s", names[index], paused ? "pauza" : "bez zmian");
            button(body, text, 0 + (index % 3) * 256, 196 + (index / 3) * 48, 240,
                   ui_design::minimumTouchHeight, Action::Toggle,
                   20 + static_cast<int>(outputs[index]), paused);
        }
        editFooter();
        break;
    }
    case Setting::FeederManual: {
        box(body, 0, 56, 368, 270, chartCardColor(), ui_design::radius::card);
        box(body, 384, 56, 368, 270, chartCardColor(), ui_design::radius::card);
        label(body, "KROK SERWISOWY", 16, 72, 336, &ui_font_14, muted());
        const uint8_t steps[] = {5, 15, 45};
        for (unsigned index = 0; index < 3; ++index) {
            char caption[12];
            std::snprintf(caption, sizeof(caption), "%u°", steps[index]);
            button(body, caption, 16 + static_cast<int>(index) * 112, 108, 104, 48,
                   Action::FeederManualStep, steps[index], u.feederManualStep == steps[index]);
        }
        label(body, "KIERUNEK", 16, 169, 336, &ui_font_14, muted());
        char left[32], right[32];
        std::snprintf(left, sizeof(left), "← %u°", u.feederManualStep);
        std::snprintf(right, sizeof(right), "%u° →", u.feederManualStep);
        button(body, left, 16, 197, 160, 52, Action::FeederManualJog, -1, false, true);
        button(body, right, 192, 197, 160, 52, Action::FeederManualJog, 1, false, true);
        label(body, "Ruch wirnika może podać pokarm.", 16, 271, 336, &ui_font_14, muted());
        label(body, "STATUS NAPĘDU", 400, 72, 336, &ui_font_14, muted());
        label(body, "Niepodłączony", 400, 103, 336, &ui_font_20, amber);
        rule(body, 400, 145, 336);
        label(body,
              "ULN2003 nie ma jeszcze połączeń\nIN1–IN4 z kontrolerem.\nWeMos ma tylko przekaźnik karmnika.\nRuch fizyczny jest zablokowany.",
              400, 161, 336, &ui_font_16, muted());
        label(body, "Po podłączeniu skonfigurujemy piny i limit ruchu.", 400, 272, 336,
              &ui_font_14, muted());
        break;
    }
    case Setting::Scenarios: {
        const char *names[] = {"Normalna praca", "Brak łącza",        "Aktywne alarmy",   "Karmienie",
                               "Serwis testowy", "Niski poziom wody", "Sprzeczne pływaki"};
        lv_obj_t *status = box(body, 0, 56, 240, 270, chartCardColor(), ui_design::radius::card);
        label(status, "AKTYWNY SCENARIUSZ", 16, 18, 208, &ui_font_14, muted());
        label(status, names[static_cast<int>(snapshot().scenario)], 16, 52, 208,
              ui_design::typography::section());
        rule(status, 16, 116, 208);
        label(status, "Tylko wizualizacja\nczujników i alarmów.\nBez sterowania wyjściami.", 16, 137,
              208, &ui_font_16, muted());
        label(body, "WYBIERZ STAN DO PODGLĄDU", 256, 60, 496, &ui_font_14, muted());
        for (int i = 0; i < 7; ++i)
            button(body, names[i], 256 + (i % 2) * 248, 90 + (i / 2) * 58, 232, 48, Action::Scenario, i,
                   snapshot().scenario == static_cast<Scenario>(i));
        break;
    }
    case Setting::Display: {
        box(body, 0, 56, 368, 214, chartCardColor(), ui_design::radius::card);
        box(body, 384, 56, 368, 214, chartCardColor(), ui_design::radius::card);
        label(body, "WYGLĄD", 16, 72, 336, &ui_font_14, muted());
        label(body, "Tryb kolorów", 16, 98, 336, ui_design::typography::section());
        button(body, "Jasny", 16, 129, 103, 42, Action::Toggle, 5,
               !u.prefDraft.autoTheme && !u.prefDraft.night);
        button(body, "Ciemny", 129, 129, 103, 42, Action::Toggle, 6,
               !u.prefDraft.autoTheme && u.prefDraft.night);
        button(body, "Auto", 242, 129, 110, 42, Action::Toggle, 7, u.prefDraft.autoTheme);
        char solarText[112];
        ui_runtime::solarStatus(solarText, sizeof(solarText));
        label(body, solarText, 16, 177, 336, &ui_font_14, muted());
        label(body, "Miejscowość w Polsce", 16, 202, 336, &ui_font_14, muted());
        solarCityField = textarea(u.prefDraft.solarCity, 222, false, 16, 336);
        label(body, "WYŚWIETLACZ", 400, 72, 336, &ui_font_14, muted());
        char text[24];
        snprintf(text, sizeof(text), "%u min", u.prefDraft.sleepMinutes);
        stepper("WYGASZANIE TREŚCI / 0 = WYŁ.", text, 400, 96, 336, 4);
        button(body, "Test wygaszania", 400, 184, 336, 44, Action::ScreenSleep);
        label(body, "Pierwszy dotyk wybudza / bez PWM", 400, 236, 336, &ui_font_14, muted());
        editFooter();
        break;
    }
    case Setting::Sound:
        box(body, 0, 56, 368, 214, chartCardColor(), ui_design::radius::card);
        box(body, 384, 56, 368, 214, chartCardColor(), ui_design::radius::card);
        label(body, "ODPOWIEDŹ SYSTEMU", 16, 72, 336, &ui_font_14, muted());
        label(body, "Potwierdzenia działań", 16, 104, 336, ui_design::typography::section());
        label(body, "Błędy pozostają widoczne zawsze", 16, 148, 336, &ui_font_14, muted());
        button(body, u.prefDraft.feedback ? "Włączone" : "Wyłączone", 16, 205, 336, 48,
               Action::Toggle, 1, u.prefDraft.feedback);
        label(body, "PŁYNNOŚĆ", 400, 72, 336, &ui_font_14, muted());
        label(body, "Ograniczenie animacji", 400, 104, 336, ui_design::typography::section());
        label(body, "Mniej ruchu i odświeżeń ekranu", 400, 148, 336, &ui_font_14, muted());
        button(body, u.prefDraft.reducedMotion ? "Włączone" : "Wyłączone", 400, 205, 336, 48,
               Action::Toggle, 2, u.prefDraft.reducedMotion);
        editFooter();
        break;
    case Setting::Security:
        box(body, 0, 56, 368, 214, chartCardColor(), ui_design::radius::card);
        box(body, 384, 56, 368, 214, chartCardColor(), ui_design::radius::card);
        label(body, "DOSTĘP", 16, 72, 336, &ui_font_14, muted());
        label(body, "Ochrona edycji PIN-em", 16, 104, 336, ui_design::typography::section());
        label(body, "Sesja 5 min / 5 prób i blokada 30 s", 16, 148, 336, &ui_font_14, muted());
        button(body, u.prefDraft.lock ? "Włączone" : "Wyłączone", 16, 205, 336, 48,
               Action::Toggle, 3, u.prefDraft.lock);
        label(body, "KOD DOSTĘPU", 400, 72, 336, &ui_font_14, muted());
        label(body, "PIN / 4 cyfry", 400, 104, 336, ui_design::typography::section());
        securityPin = textarea(u.prefDraft.pin, 146, true, 400, 336);
        lv_textarea_set_max_length(securityPin, 4);
        lv_textarea_set_accepted_chars(securityPin, "0123456789");
        label(body, "Ustawienie PIN-u włącza blokadę / nie szyfruje pamięci", 400, 218, 336, &ui_font_14,
              muted());
        editFooter();
        break;
    case Setting::Network: {
        box(body, 0, 56, 480, 214, chartCardColor(), ui_design::radius::card);
        box(body, 496, 56, 256, 214, chartCardColor(), ui_design::radius::card);
        const char *modes[] = {"Wyłączone", "Dom / router", "AquaBox AP"};
        for (int i = 0; i < 3; ++i)
            button(body, modes[i], 16 + i * 152, 64, 136, 44, Action::NetworkMode, i,
                   static_cast<int>(u.prefDraft.network) == i);
        const bool ap = u.prefDraft.network == ui_runtime::NetworkMode::AccessPoint;
        label(body, "NAZWA SIECI / SSID", 16, 115, 452, &ui_font_14, muted());
        networkSsid = textarea(ap ? "AquaBox-Setup" : u.prefDraft.ssid, 137, false, 16, 448);
        if (ap)
            lv_obj_add_state(networkSsid, LV_STATE_DISABLED);
        label(body, ap ? "HASŁO PUNKTU DOSTĘPOWEGO" : "HASŁO SIECI DOMOWEJ", 16, 193, 452, &ui_font_14,
              muted());
        networkPassword = textarea(ap ? u.prefDraft.apPassword : u.prefDraft.password, 215, true, 16,
                                   448);
        char details[160];
        ui_runtime::networkDetails(details, sizeof(details));
        label(body, "STAN POŁĄCZENIA", 512, 72, 224, &ui_font_14, muted());
        networkDetailsLabel = label(body, details, 512, 104, 224, &ui_font_14, muted());
        if (ap) {
            lv_textarea_set_max_length(networkPassword, 16);
            lv_textarea_set_password_mode(networkPassword, !u.apVisible);
            button(body, u.apVisible ? "Ukryj hasło AP" : "Pokaż hasło AP", 512, 162, 224, 44,
                   Action::Toggle, 13);
        }
        button(body, "Ponów połączenie", 512, 216, 224, 44, Action::Reconnect, 0, false,
               u.prefs.network == ui_runtime::NetworkMode::Off);
        editFooter();
        break;
    }
    case Setting::Clock: {
        clockSettings();
        break;
    }
    case Setting::Hardware: {
        box(body, 0, 59, 752, 51, wash(), ui_design::radius::control);
        for (int category = 0; category < 4; ++category) {
            lv_obj_t *tab = button(body, categories[category], 2 + category * 189, 62, 181, 44,
                                   Action::HardwareCategory, category);
            if (u.hardwareCategory == category) {
                lv_obj_set_style_bg_color(tab, lv_color_hex(chartCardColor()), 0);
                setLegibleText(lv_obj_get_child(tab, 0), theme().accent);
                box(tab, 18, 41, 145, 3, theme().accent, 1);
            }
        }
        int row = 0;
        for (int id = 0; id < static_cast<int>(ModuleId::Count); ++id) {
            const ModuleId moduleId = static_cast<ModuleId>(id);
            const Metadata &meta = metadata(moduleId);
            if (static_cast<int>(meta.category) != u.hardwareCategory)
                continue;
            lv_obj_t *entry = box(body, (row % 2) * 384, 122 + (row / 2) * 68, 368, 60,
                                  chartCardColor(), ui_design::radius::card);
            clickable(entry, Action::Module, id);
            lv_obj_set_style_bg_color(entry, lv_color_hex(wash()), LV_STATE_PRESSED);
            lv_obj_set_style_transform_zoom(entry, ui_design::pressedZoom, LV_STATE_PRESSED);
            label(entry, meta.title, 16, 4, 306, ui_design::typography::section());
            lv_obj_t *state = label(entry, moduleState(moduleId), 16, 32, 306, &ui_font_14,
                                    stateColor(moduleId));
            bind(state, 12, moduleId);
            label(entry, "→", 336, 18, 24, &ui_font_20, theme().accent);
            ++row;
        }
        break;
    }
    case Setting::Diagnostics: {
        char text[160];
        snprintf(text, sizeof(text),
                 "Wolna sterta: %lu KB\nWolna PSRAM: %lu KB\nDotknięcia: %lu\nGT911: %s",
                 static_cast<unsigned long>(ESP.getFreeHeap() / 1024),
                 static_cast<unsigned long>(ESP.getFreePsram() / 1024),
                 static_cast<unsigned long>(panel_hal_touch_count()),
                 panel_hal_touch_ready() ? "aktywny" : "niedostępny");
        box(body, 0, 56, 368, 214, chartCardColor(), ui_design::radius::card);
        box(body, 384, 56, 368, 214, chartCardColor(), ui_design::radius::card);
        label(body, "PANEL / ESP32-S3", 16, 70, 340, &ui_font_14, muted());
        label(body, "Pamięć i dotyk", 16, 94, 340, ui_design::typography::section());
        diagnosticsLabel = label(body, text, 16, 132, 350, &ui_font_16, muted());
        label(body, "MODUŁ WYKONAWCZY", 400, 70, 330, &ui_font_14, muted());
        label(body, "Brak łącza UART", 400, 94, 330, ui_design::typography::section(), amber);
        label(body,
              "Źródło danych: model DEMO\nUART: integracja oczekująca\nMCP / ADC / RTC: bez odczytu "
              "sprzętu\nLDR: brak czujnika na nowym panelu\nOstatni ACK: brak rzeczywistej komendy",
              400, 132, 330, &ui_font_14, muted());
        button(body, "Test dotyku", 0, 281, 240, 44, Action::TestTouch);
        button(body, "Dziennik zdarzeń", 256, 281, 240, 44, Action::Logs);
        button(body, "Test czujników", 512, 281, 240, 44, Action::Setting,
               static_cast<int>(Setting::Scenarios));
        break;
    }
    case Setting::Controller: {
        box(body, 0, 56, 368, 214, chartCardColor(), ui_design::radius::card);
        box(body, 384, 56, 368, 214, chartCardColor(), ui_design::radius::card);
        label(body, "MODUŁ / ESP8266", 16, 70, 348, &ui_font_14, muted());
        label(body, "UART niepodłączony", 16, 94, 348, ui_design::typography::section(), amber);
        label(body,
              "Brak skonfigurowanego transportu.\nNie wysłano testu łączności.\nWi-Fi panelu nie oznacza "
              "łącza z WeMos.",
              16, 132, 348, &ui_font_16, muted());
        label(body, "TELEMETRIA TRANSPORTU", 400, 70, 332, &ui_font_14, muted());
        label(body, "Komunikacja", 400, 94, 332, ui_design::typography::section());
        label(body,
              "TX / RX: niedostępne\nOstatni ACK: brak danych\nOpóźnienie: brak danych\nBłędy / timeouty: "
              "brak danych\nWersja ESP8266: brak danych\nGPIO TX/RX: nieustalone",
              400, 132, 332, &ui_font_14, muted());
        button(body, "Dziennik zdarzeń", 0, 281, 360, 44, Action::Logs);
        button(body, "Mapa sprzętu", 392, 281, 360, 44, Action::Setting,
               static_cast<int>(Setting::Hardware));
        break;
    }
    case Setting::Journal: {
        box(body, 0, 56, 368, 270, chartCardColor(), ui_design::radius::card);
        box(body, 384, 56, 368, 270, chartCardColor(), ui_design::radius::card);
        label(body, "PRZEGLĄD", 16, 74, 336, &ui_font_14, muted());
        label(body, "Dziennik zdarzeń", 16, 109, 336, ui_design::typography::section());
        label(body, "Czas zdarzenia, liczba powtórzeń\ni moment ustąpienia przyczyny.", 16, 160,
              336, &ui_font_16, muted());
        button(body, "Otwórz zdarzenia", 16, 264, 336, 48, Action::Logs);
        label(body, "PAMIĘĆ PANELU", 400, 74, 336, &ui_font_14, muted());
        label(body, "Dane tymczasowe", 400, 109, 336, ui_design::typography::section());
        label(body, "Zdarzenia pozostają w RAM.\nCzyszczenie nie usuwa pomiarów\nani konfiguracji.", 400,
              160, 336, &ui_font_16, muted());
        button(body, "Wyczyść dziennik", 400, 264, 336, 48, Action::ClearLog);
        break;
    }
    case Setting::Update:
        box(body, 0, 56, 368, 270, chartCardColor(), ui_design::radius::card);
        box(body, 384, 56, 368, 270, chartCardColor(), ui_design::radius::card);
        label(body, "OBRAZ PANELU", 16, 74, 336, &ui_font_14, muted());
        label(body, "ESP32-S3 N16R8", 16, 109, 336, ui_design::typography::section());
        label(body, "Aktualizacja przez USB.\nFirmware WeMos to osobny pakiet.\nTa kontrola dotyczy tylko panelu.", 16,
              160, 336, &ui_font_16, muted());
        label(body, "INTEGRALNOŚĆ", 400, 74, 336, &ui_font_14, muted());
        label(body, "Bieżąca aplikacja", 400, 109, 336, ui_design::typography::section());
        firmwareLabel = label(body, firmwareSummary, 400, 160, 336, &ui_font_16, teal);
        button(body, "Sprawdź firmware", 400, 264, 336, 48, Action::VerifyFirmware);
        break;
    case Setting::Power:
        box(body, 0, 56, 368, 270, chartCardColor(), ui_design::radius::card);
        box(body, 384, 56, 368, 270, chartCardColor(), ui_design::radius::card);
        label(body, "PONOWNE URUCHOMIENIE", 16, 74, 336, &ui_font_14, muted());
        label(body, "Restart panelu", 16, 109, 336, ui_design::typography::section());
        label(body, "Zachowuje zapisaną konfigurację.\nNie uruchamia ponownie WeMos.", 16, 160, 336,
              &ui_font_16, muted());
        button(body, "Restart panelu", 16, 264, 336, 48, Action::Restart);
        label(body, "PRZYWRACANIE", 400, 74, 336, &ui_font_14, contentBad());
        label(body, "Ustawienia fabryczne", 400, 109, 336, ui_design::typography::section(),
              contentBad());
        label(body, "Usuwa konfigurację AquaBox w NVS.\nNie resetuje sterownika wykonawczego.\nWymaga potwierdzenia.",
              400, 160, 336, &ui_font_16, muted());
        button(body, "Przywróć panel", 400, 264, 336, 48, Action::Stop, 2);
        break;
    }
}
void refreshFeedingCaption() {
    const auto &s = snapshot();
    if (feedCaption) {
        char remaining[112];
        if (s.motor.phase == FeederPhase::Waiting)
            std::snprintf(remaining, sizeof(remaining), "Czekam na pozycję\n%s",
                          s.motor.source == FeederSource::LocalModel ? "Model lokalny"
                                                                     : "Sterownik karmnika");
        else if (s.motor.phase == FeederPhase::Moving && s.motor.positionValid)
            std::snprintf(remaining, sizeof(remaining), "%s\n%.0f° / %u°",
                          s.motor.source == FeederSource::LocalModel ? "Model podawania dawki"
                                                                     : "Podawanie dawki",
                          static_cast<double>(s.motor.positionDegrees), s.motor.targetDegrees);
        else
            std::snprintf(remaining, sizeof(remaining), "%s\nPauza / %02lu:%02lu",
                          s.motor.source == FeederSource::LocalModel ? "Model: obrót zakończony"
                                                                     : "Obrót potwierdzony",
                          static_cast<unsigned long>(s.feedingRemainingSeconds / 60),
                          static_cast<unsigned long>(s.feedingRemainingSeconds % 60));
        lv_label_set_text(feedCaption, remaining);
    }
}
void refreshBindings() {
    updateHeader();
    refreshPlan();
    const Snapshot &s = snapshot();
    if (feedLast && feedNext && feedButton) {
        char time[32], text[96];
        ui_history_time::format(s.lastFeedingWallSeconds, time, sizeof(time));
        std::snprintf(text, sizeof(text), "Ostatnie\n%s",
                      !s.lastFeedingKnown        ? "-"
                      : s.lastFeedingWallSeconds ? time
                                                 : "czas nieznany");
        lv_label_set_text(feedLast, text);
        const NextFeeding next = nextFeeding();
        if (s.motor.phase == FeederPhase::Fault)
            std::snprintf(text, sizeof(text), "Następne\nwstrzymane");
        else if (next.available) {
            static const char *days[] = {"Pn ", "Wt ", "Śr ", "Cz ", "Pt ", "So ", "Nd "};
            const char *when = next.daysAway == 0   ? ""
                               : next.daysAway == 1 ? "jutro "
                                                    : days[(weekday() + next.daysAway) % 7];
            std::snprintf(text, sizeof(text), "Następne\n%s%02u:%02u", when, next.minute / 60,
                          next.minute % 60);
        } else
            std::snprintf(text, sizeof(text), "Następne\nbrak planu");
        lv_label_set_text(feedNext, text);
        if (!s.feedingActive) {
            if (canRequestAdditionalDose(s))
                std::snprintf(text, sizeof(text), "Kolejna dawka / PIN");
            else if (s.feedingCooldownSeconds && s.motor.phase != FeederPhase::Fault)
                std::snprintf(text, sizeof(text), "Karmienie / %02lu:%02lu",
                              static_cast<unsigned long>(s.feedingCooldownSeconds / 60),
                              static_cast<unsigned long>(s.feedingCooldownSeconds % 60));
            else
                std::snprintf(text, sizeof(text), "Karmienie");
            lv_label_set_text(lv_obj_get_child(feedButton, 0), text);
        }
        if (!s.feedingActive &&
            (s.serviceActive || (s.feedingCooldownSeconds && !canRequestAdditionalDose(s)) ||
             s.motor.phase == FeederPhase::Fault))
            lv_obj_add_state(feedButton, LV_STATE_DISABLED);
        else
            lv_obj_clear_state(feedButton, LV_STATE_DISABLED);
    }
    if (clockLive && clockDate) {
        const auto date = ui_runtime::dateTime();
        char time[16], text[24];
        std::snprintf(time, sizeof(time), "%02d:%02d", date.hour, date.minute);
        std::snprintf(text, sizeof(text), "%02d.%02d.%04d", date.day, date.month, date.year);
        lv_label_set_text(clockLive, time);
        lv_label_set_text(clockDate, text);
        const bool online = panelStatus.wireless.mode == ui_status::WirelessMode::Home ||
                            panelStatus.wireless.mode == ui_status::WirelessMode::HomeAndAp;
        lv_label_set_text_static(clockNetworkLabel, online ? "Router dostępny / NTP może działać"
                                                           : "Brak internetu / NTP wymaga routera");
        setLegibleText(clockNetworkLabel, online ? teal : amber);
        if (online)
            lv_obj_clear_state(clockSyncButton, LV_STATE_DISABLED);
        else
            lv_obj_add_state(clockSyncButton, LV_STATE_DISABLED);
        const bool validTime = panelStatus.clock.source == ui_status::ClockSource::System;
        lv_label_set_text_static(clockOrigin, validTime ? "Źródło: zegar systemowy"
                                                        : "Czas niepotwierdzony / ustaw zegar");
        setLegibleText(clockOrigin, validTime ? muted() : amber);
    }
    refreshFeedingCaption();
    for (int i = 0; i < bindingCount; ++i) {
        const Binding &b = bindings[i];
        char value[64];
        if (b.kind >= 20 && b.kind <= 22) {
            lv_obj_invalidate(b.label);
            continue;
        }
        if (b.kind >= 23 && b.kind <= 25) {
            const unsigned kind = b.kind - 23;
            const Measurement &reading = kind == 0 ? s.temperature : kind == 1 ? s.ph : s.ec;
            const SensorRange &range = s.ranges[kind];
            char minimum[16], maximum[16];
            decimal(minimum, sizeof(minimum), range.minimum, kind == 2 ? 0 : 1);
            decimal(maximum, sizeof(maximum), range.maximum, kind == 2 ? 0 : 1);
            const bool valid = reading.valid && !reading.stale && std::isfinite(reading.value);
            const bool normal = valid && reading.value >= range.minimum && reading.value <= range.maximum;
            std::snprintf(value, sizeof(value), "%s / %s–%s",
                          !valid                          ? "Brak danych"
                          : reading.value < range.minimum ? "Za nisko"
                          : reading.value > range.maximum ? "Za wysoko"
                                                          : "W zakresie",
                          minimum, maximum);
            lv_label_set_text(b.label, value);
            setLegibleText(b.label, !valid ? muted() : normal ? teal : amber);
            continue;
        }
        if (b.kind == 3) {
            lv_label_set_text(b.label, moduleState(b.id));
            setLegibleText(b.label, stateColor(b.id));
            continue;
        }
        if (b.kind == 4) {
            lv_label_set_text(b.label, modeName(module(b.id).mode));
            continue;
        }
        if (b.kind == 12) {
            lv_label_set_text(b.label, moduleState(b.id));
            continue;
        }
        if (b.kind == 5) {
            snprintf(value, sizeof(value), "Filtr / %s", moduleState(b.id));
            lv_label_set_text(b.label, value);
            continue;
        }
        if (b.kind >= 6 && b.kind <= 8) {
            bool chosen = module(b.id).mode == static_cast<Mode>(b.kind - 6);
            lv_obj_set_style_bg_color(b.label, lv_color_hex(chosen ? selectedFill() : wash()), 0);
            setLegibleText(lv_obj_get_child(b.label, 0), ink());
            continue;
        }
        if (b.kind == 9) {
            uint32_t seconds = s.manualRemainingSeconds[static_cast<int>(b.id)];
            if (seconds)
                snprintf(value, sizeof(value), "Powrót AUTO za %02lu:%02lu",
                         static_cast<unsigned long>(seconds / 60),
                         static_cast<unsigned long>(seconds % 60));
            else
                snprintf(value, sizeof(value), "Ręcznie: czasowe wymuszenie, potem AUTO");
            lv_label_set_text(b.label, value);
            continue;
        }
        const Measurement &m = b.kind == 0 ? s.temperature : b.kind == 1 ? s.ph : s.ec;
        if (m.valid && !m.stale && std::isfinite(m.value)) {
            decimal(value, sizeof(value), m.value, b.kind == 1 ? 2 : 1);
            if (b.kind == 2)
                snprintf(value, sizeof(value), "%.0f", static_cast<double>(m.value));
        } else
            snprintf(value, sizeof(value), "–");
        lv_label_set_text(b.label, value);
        if (b.kind < 3) {
            const SensorRange &range = s.ranges[b.kind];
            const bool out = m.valid && !m.stale && (m.value < range.minimum || m.value > range.maximum);
            setLegibleText(b.label, out ? (b.kind == 0 && m.value >= 28.5f ? danger : amber) : ink());
        }
    }
    char text[140];
    if (healthCard) {
        const Health state = health();
        const uint32_t color = state.severity == Severity::Critical  ? theme().badTint
                               : state.severity == Severity::Warning ? theme().warningTint
                                                                     : wash();
        lv_obj_set_style_bg_color(healthCard, lv_color_hex(color), 0);
        lv_label_set_text_static(healthTitle, state.title);
        lv_label_set_text_static(healthDetail, state.detail);
        lv_obj_update_layout(healthTitle);
        lv_obj_set_y(healthDetail, lv_obj_get_y(healthTitle) + lv_obj_get_height(healthTitle) + 6);
        setLegibleText(healthTitle, severityColor(state.severity));
        setLegibleText(healthDetail, muted());
        lv_obj_set_style_bg_color(lv_obj_get_child(healthCard, 0),
                                  lv_color_hex(severityColor(state.severity)), 0);
    }
    if (waterStatus) {
        lv_label_set_text_static(waterStatus, waterCaption());
        const WaterState state = waterState();
        setLegibleText(waterStatus, state == WaterState::Conflict ? danger
                                   : state == WaterState::Low    ? amber : ink());
    }
    if (flowStatus)
        lv_label_set_text_static(flowStatus,
                                 s.flowValid ? (s.flowDetected ? "Obecny" : "Brak") : "Brak danych");
    if (serviceRemaining) {
        std::snprintf(text, sizeof(text), "%02lu:%02lu",
                      static_cast<unsigned long>(s.serviceRemainingSeconds / 60),
                      static_cast<unsigned long>(s.serviceRemainingSeconds % 60));
        lv_label_set_text(serviceRemaining, text);
    }
    if (diagnosticsLabel) {
        char diagnostics[220];
        std::snprintf(
            diagnostics, sizeof(diagnostics),
            "Sterta: %lu KB / PSRAM: %lu KB\nDotknięcia: %lu / GT911: %s\nNVS: %s\nCzas pracy: %lu s",
            static_cast<unsigned long>(ESP.getFreeHeap() / 1024),
            static_cast<unsigned long>(ESP.getFreePsram() / 1024),
            static_cast<unsigned long>(panel_hal_touch_count()),
            panel_hal_touch_ready() ? "aktywny" : "brak", ui_runtime::storageHealthy() ? "OK" : "błąd",
            static_cast<unsigned long>(millis() / 1000));
        lv_label_set_text(diagnosticsLabel, diagnostics);
    }
    if (networkDetailsLabel) {
        char details[160];
        ui_runtime::networkDetails(details, sizeof(details));
        lv_label_set_text(networkDetailsLabel, details);
    }
    refreshHistory();
    if (s.serviceActive || s.feedingActive) {
        uint32_t secs = s.serviceActive ? s.serviceRemainingSeconds : s.feedingRemainingSeconds;
        snprintf(text, sizeof(text), "%s  /  %02lu:%02lu do powrotu AUTO  /  dotknij, aby zakończyć",
                 s.serviceActive ? "SERWIS" : "KARMIENIE", static_cast<unsigned long>(secs / 60),
                 static_cast<unsigned long>(secs % 60));
        if (countdownLabel)
            lv_label_set_text(countdownLabel, text);
    } else if (!s.connected)
        snprintf(text, sizeof(text), "BRAK ŁĄCZA  /  pomiary i stany wyjść są nieznane");
    else
        snprintf(text, sizeof(text),
                 s.alarmFlags ? "UWAGA  /  sprawdź aktywne przyczyny w Centrum uwagi" : "");
    if (ribbon) {
        lv_label_set_text(ribbon, text);
        if (text[0] && !(u.tab == Tab::Home && u.page == Page::Main))
            lv_obj_clear_flag(statusStrip, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(statusStrip, LV_OBJ_FLAG_HIDDEN);
    }
    const int top = statusStrip && !lv_obj_has_flag(statusStrip, LV_OBJ_FLAG_HIDDEN) ? 82 : 56;
    lv_obj_set_y(body, top + (ui_navigation::kNavigationY - top - ui_navigation::kBodyHeight) / 2);
}
void closeDialog() {
    lastDialogShown = Dialog::None;
    if (overlay) {
        lv_obj_del(overlay);
        overlay = nullptr;
    }
    pinLabel = countdownLabel = wirelessDetailsLabel = nullptr;
    u.dialog = Dialog::None;
}
void renderDialog() {
    if (u.dialog == Dialog::None) {
        lastDialogShown = Dialog::None;
        return;
    }
    const bool entering = lastDialogShown != u.dialog;
    lastDialogShown = u.dialog;
    overlay = box(lv_layer_top(), 0, 0, 800, 480, theme().veil);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_80, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(overlay, onGesture, LV_EVENT_GESTURE, nullptr);
    int height = u.dialog == Dialog::Pin ? 394 : u.dialog == Dialog::Wireless ? 350 : 310;
    lv_obj_t *p = box(overlay, 116, (480 - height) / 2, 568, height, bg(), ui_design::radius::dialog);
    if (entering && !u.prefs.reducedMotion) {
        lv_anim_t motion;
        lv_anim_init(&motion);
        lv_anim_set_var(&motion, p);
        lv_anim_set_exec_cb(&motion, moveToast);
        lv_anim_set_values(&motion, (480 - height) / 2 + 16, (480 - height) / 2);
        lv_anim_set_time(&motion, ui_design::motion::normal);
        lv_anim_set_path_cb(&motion, lv_anim_path_ease_out);
        lv_anim_start(&motion);
        lv_anim_set_exec_cb(&motion, fadeToast);
        lv_anim_set_values(&motion, 0, LV_OPA_COVER);
        lv_anim_start(&motion);
    }
    if (u.dialog == Dialog::Wireless) {
        label(p, "Połączenie Wi-Fi", 24, 20, 520, &ui_font_24);
        char details[384];
        wirelessDescription(details, sizeof(details));
        wirelessDetailsLabel = label(p, details, 24, 70, 520, &ui_font_16, muted());
        button(p, "Zamknij", 24, 280, 186, 48, Action::Dismiss);
        const bool blocked = editing() || savePending || editDirty || saveFailed;
        if (blocked)
            label(p, "Wróć z bieżącej edycji, aby przejść do ustawień sieci.", 24, 245, 520, &ui_font_14,
                  muted());
        button(p, "Ustawienia sieci →", 294, 280, 250, 48, Action::Wireless, 1, false, blocked);
        return;
    }
    if (u.dialog == Dialog::Time) {
        label(p, "Godzina", 24, 20, 480, &ui_font_24);
        char t[8];
        formatTime(t, sizeof(t), u.timeDraft);
        label(p, t, 202, 112, 170, &ui_font_44);
        button(p, "+ godz.", 38, 76, 136, 48, Action::Adjust, 40);
        button(p, "+ min.", 394, 76, 136, 48, Action::Adjust, 42);
        button(p, "- godz.", 38, 149, 136, 48, Action::Adjust, 41);
        button(p, "- min.", 394, 149, 136, 48, Action::Adjust, 43);
        button(p, "Anuluj", 24, 241, 182, 48, Action::Dismiss);
        button(p, "Ustaw godzinę", 318, 241, 226, 48, Action::Confirm, 0, true);
        return;
    }
    if (u.dialog == Dialog::Pin) {
        const bool blocked = static_cast<int32_t>(u.pinBlockedUntil - millis()) > 0;
        const bool additional = u.pinPurpose == PinPurpose::AdditionalDose;
        label(p, additional ? "Dodatkowa dawka" : "Odblokuj edycję", 24, 16, 490, &ui_font_24);
        label(p, blocked ? "Blokada po 5 próbach / odczekaj 30 sekund"
                         : additional ? "PIN administratora / potwierdź jedną dawkę"
                                      : "Wpisz 4 cyfry / sesja 5 min",
              24, 49, 500, &ui_font_14, muted());
        pinLabel = label(p, "– – – –", 368, 116, 170, &ui_font_24);
        for (int i = 0; i < 9; ++i) {
            char t[2] = {static_cast<char>('1' + i), 0};
            button(p, t, 24 + (i % 3) * 104, 87 + (i / 3) * 62, 92, 52, Action::PinDigit, i + 1, false,
                   blocked);
        }
        button(p, "Usuń", 24, 273, 92, 52, Action::PinBack);
        button(p, "0", 128, 273, 92, 52, Action::PinDigit, 0, false, blocked);
        button(p, "OK", 232, 273, 92, 52, Action::PinEnter, 0, true, blocked);
        button(p, "Anuluj", 364, 273, 180, 52, Action::Dismiss);
        return;
    }
    const Snapshot &s = snapshot();
    bool active =
        (u.dialog == Dialog::Feed && s.feedingActive) || (u.dialog == Dialog::Service && s.serviceActive);
    const char *title = u.dialog == Dialog::Feed           ? "Karmienie"
                        : u.dialog == Dialog::Service      ? "Serwis akwarium"
                        : u.dialog == Dialog::Manual       ? "Ręczne sterowanie"
                        : u.dialog == Dialog::Light        ? "Zmiana profilu lampy"
                        : u.dialog == Dialog::Reset        ? "Przywrócić ustawienia?"
                        : u.dialog == Dialog::Restart      ? "Uruchomić panel ponownie?"
                        : u.dialog == Dialog::ClearHistory ? "Wyczyścić historię pomiarów?"
                         : u.dialog == Dialog::ClearLog     ? "Wyczyścić dziennik zdarzeń?"
                         : u.dialog == Dialog::Stop && s.feedingActive ? "Przerwać karmienie?"
                                                           : "Zakończyć pauzę?";
    label(p, title, 24, 20, 520, &ui_font_24);
    const char *info =
        u.dialog == Dialog::Feed           ? "Pauza filtra, CO2 i ATO. Po czasie powrót AUTO."
        : u.dialog == Dialog::Service      ? "Jedna lampa włączona / pozostałe wyjścia OFF."
        : u.dialog == Dialog::Manual       ? "Czasowe wymuszenie / ochrona nadal ma priorytet."
        : u.dialog == Dialog::Light        ? "Reset i stabilizacja zajmują około 9 sekund."
        : u.dialog == Dialog::Reset        ? "Usunięta zostanie tylko konfiguracja AquaBox."
        : u.dialog == Dialog::Restart      ? "Zapisane ustawienia zostaną zachowane. WeMos niezależny."
        : u.dialog == Dialog::ClearHistory ? "Usuwa próbki z RAM. Nastawy pozostają bez zmian."
        : u.dialog == Dialog::ClearLog     ? "Usuwa wpisy z RAM. Nie kasuje konfiguracji ani pomiarów."
                                           : "Przywrócenie automatyki po zakończeniu trybu.";
    label(p, info, 24, 65, 520, &ui_font_16, muted());
    if (active) {
        countdownLabel = label(p, "", 24, 130, 520, &ui_font_20, teal);
        refreshBindings();
    } else if (u.dialog == Dialog::Feed || u.dialog == Dialog::Service || u.dialog == Dialog::Manual) {
        int minutes = u.dialog == Dialog::Feed      ? u.durationMinutes
                      : u.dialog == Dialog::Service ? u.serviceMinutes
                                                    : u.manualMinutes;
        char text[30];
        snprintf(text, sizeof(text), "%d min", minutes);
        if (u.dialog == Dialog::Service || u.dialog == Dialog::Manual) {
            button(p, text, 24, 128, 252, 52, Action::EditValue, u.dialog == Dialog::Service ? 6 : 8);
            label(p, "Dotknij, aby ustawić czas", 24, 191, 520, &ui_font_14, muted());
        } else {
            button(p, "-", 24, 128, 56, 52, Action::Adjust, 60);
            label(p, text, 96, 141, 120, &ui_font_24);
            button(p, "+", 220, 128, 56, 52, Action::Adjust, 61);
        }
        if (u.dialog == Dialog::Feed)
            button(p, u.feedDose ? "Jedna dawka" : "Tylko pauza", 304, 128, 240, 52, Action::Toggle, 11,
                   u.feedDose);
        else
            label(p, u.dialog == Dialog::Manual ? modeName(u.manual) : "Czas do powrotu AUTO", 306, 141,
                  238, &ui_font_20, muted());
    } else if (u.dialog == Dialog::Light)
        label(p, lightModeName(u.light), 24, 139, 520, &ui_font_24);
    button(p, active ? "Zamknij" : "Anuluj", 24, 240, 186, 48, Action::Dismiss);
    lv_obj_t *confirm = button(p,
           active || (u.dialog == Dialog::Stop && s.feedingActive)            ? "Zakończ teraz"
           : u.dialog == Dialog::Reset                                        ? "Przywróć"
           : u.dialog == Dialog::Restart                                      ? "Uruchom ponownie"
           : u.dialog == Dialog::ClearHistory || u.dialog == Dialog::ClearLog ? "Wyczyść"
           : u.dialog == Dialog::Feed                                         ? "Rozpocznij karmienie"
           : u.dialog == Dialog::Service                                      ? "Rozpocznij serwis"
           : u.dialog == Dialog::Light                                        ? "Zmień profil"
           : u.dialog == Dialog::Manual                                       ? "Ustaw tryb"
                                                                              : "Przywróć AUTO",
           294, 240, 250, 48, Action::Confirm, 0, true,
           !s.connected && (u.dialog == Dialog::Feed || u.dialog == Dialog::Service ||
                            u.dialog == Dialog::Manual || u.dialog == Dialog::Light));
    if (u.dialog == Dialog::Reset || u.dialog == Dialog::ClearHistory || u.dialog == Dialog::ClearLog) {
        // Destructive confirmation is visible without turning the whole dialog red.
        lv_obj_set_style_bg_color(confirm, lv_color_hex(theme().badTint), 0);
        lv_obj_set_style_border_color(confirm, lv_color_hex(contentBad()), 0);
        lv_obj_set_style_border_width(confirm, 1, 0);
        setLegibleText(lv_obj_get_child(confirm, 0), contentBad());
    }
}
bool editing() {
    return u.page == Page::Numeric || u.page == Page::Schedule || u.page == Page::Heater ||
           u.page == Page::Feeding || u.page == Page::Calibration || u.page == Page::Range ||
           (u.page == Page::Settings && (u.setting == Setting::Network || u.setting == Setting::Display ||
                                         u.setting == Setting::Colors ||
                                         u.setting == Setting::Sound || u.setting == Setting::Security ||
                                         u.setting == Setting::Clock || u.setting == Setting::Feeder ||
                                         u.setting == Setting::FeederManual));
}
void navigate(int index, bool animate) {
    if (index < 0 || index >= ui_navigation::kPageCount || editing() || u.dialog != Dialog::None ||
        networkEditor)
        return;
    if (u.tab == static_cast<Tab>(index) && u.page == Page::Main)
        return;
    u.feedCardOpen = false;
    u.entryDirection = animate ? (tabPosition(static_cast<Tab>(index)) > tabPosition(u.tab) ? 1 : -1) : 0;
    u.tab = static_cast<Tab>(index);
    u.page = Page::Main;
    u.rebuild = true;
}
void onGesture(lv_event_t *) {
    lv_indev_t *input = lv_indev_get_act();
    if (!input || lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER)
        return;
    const lv_dir_t direction = lv_indev_get_gesture_dir(input);
    if (direction != LV_DIR_LEFT && direction != LV_DIR_RIGHT)
        return;
    // A stroke is never also a command on the control beneath the finger.
    // This applies to blocked edits, dialogs and the first/last page too.
    lv_indev_wait_release(input);
    if (u.page == Page::Main) {
        const int target = tabPosition(u.tab) + (direction == LV_DIR_LEFT ? 1 : -1);
        if (target >= 0 && target < ui_navigation::kPageCount)
            navigate(static_cast<int>(tabOrder[target]), true);
    }
}
void drawNavigationIcon(lv_event_t *event) {
    const unsigned index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(event), &area);
    lv_draw_ctx_t *context = lv_event_get_draw_ctx(event);
    const uint32_t color = static_cast<unsigned>(u.tab) == index ? navigationInk() : navigationMuted();
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.width = 2;
    line.color = lv_color_hex(color);
    auto segment = [&](int x1, int y1, int x2, int y2) {
        lv_point_t a = {static_cast<lv_coord_t>(area.x1 + x1), static_cast<lv_coord_t>(area.y1 + y1)};
        lv_point_t b = {static_cast<lv_coord_t>(area.x1 + x2), static_cast<lv_coord_t>(area.y1 + y2)};
        lv_draw_line(context, &line, &a, &b);
    };
    auto rectangle = [&](int x1, int y1, int x2, int y2, int radius) {
        lv_draw_rect_dsc_t outline;
        lv_draw_rect_dsc_init(&outline);
        outline.bg_opa = LV_OPA_TRANSP;
        outline.border_color = lv_color_hex(color);
        outline.border_width = 2;
        outline.radius = radius;
        lv_area_t bounds = {static_cast<lv_coord_t>(area.x1 + x1), static_cast<lv_coord_t>(area.y1 + y1),
                            static_cast<lv_coord_t>(area.x1 + x2), static_cast<lv_coord_t>(area.y1 + y2)};
        lv_draw_rect(context, &outline, &bounds);
    };
    if (index == 0) {
        rectangle(3, 3, 10, 10, 2);
        rectangle(15, 3, 22, 10, 2);
        rectangle(3, 15, 10, 22, 2);
        rectangle(15, 15, 22, 22, 2);
    } else if (index == 1) {
        // One open-ended wrench remains recognizable at 26 px; crossed strokes did not.
        line.round_start = line.round_end = 1;
        static const int8_t wrench[][2] = {{22, 3}, {18, 7}, {20, 9}, {24, 5}, {24, 10}, {20, 14}, {16, 13},
                                           {7, 22}, {3, 22}, {3, 18}, {12, 9}, {11, 5},  {15, 2},  {22, 3}};
        for (unsigned point = 1; point < sizeof(wrench) / sizeof(wrench[0]); ++point)
            segment(wrench[point - 1][0], wrench[point - 1][1], wrench[point][0], wrench[point][1]);
    } else if (index == 2) {
        rectangle(3, 5, 23, 23, 3);
        segment(3, 10, 23, 10);
        segment(8, 2, 8, 7);
        segment(18, 2, 18, 7);
        segment(7, 15, 10, 15);
        segment(15, 15, 18, 15);
        segment(7, 19, 10, 19);
    } else if (index == 3) {
        segment(3, 3, 3, 23);
        segment(3, 23, 24, 23);
        segment(6, 17, 11, 12);
        segment(11, 12, 16, 15);
        segment(16, 15, 23, 5);
    } else {
        rectangle(6, 6, 20, 20, 7);
        rectangle(10, 10, 16, 16, 3);
        segment(13, 2, 13, 5);
        segment(13, 21, 13, 25);
        segment(2, 13, 5, 13);
        segment(21, 13, 25, 13);
        segment(4, 4, 7, 7);
        segment(19, 19, 22, 22);
        segment(4, 22, 7, 19);
        segment(19, 7, 22, 4);
    }
}
void navigation(lv_obj_t *screen) {
    lv_obj_t *footer = box(screen, 0, ui_navigation::kNavigationY, 800, 60, navigationBg());
    rule(footer, 24, 0, 752, navigationLine());
    for (int i = 0; i < ui_navigation::kPageCount; ++i) {
        const int tab = static_cast<int>(tabOrder[i]);
        const bool current = u.tab == tabOrder[i];
        lv_obj_t *nav =
            button(footer, tabNames[tab], 24 + i * 151, 3, 146, 54, Action::Nav, tab, false, editing());
        lv_obj_set_style_bg_color(nav, lv_color_hex(navigationBg()), 0);
        lv_obj_set_style_bg_color(nav, lv_color_hex(navigationActive()), LV_STATE_PRESSED);
        if (chromeDimmed())
            lv_obj_set_style_opa(nav, LV_OPA_COVER, LV_STATE_DISABLED);
        lv_obj_t *caption = lv_obj_get_child(nav, 0);
        lv_obj_align(caption, LV_ALIGN_TOP_LEFT, 6, 29);
        lv_obj_set_width(caption, 134);
        setLegibleText(caption, current ? navigationInk() : navigationMuted());
        lv_obj_t *indicator = box(nav, 60, 3, 26, 26, navigationBg());
        lv_obj_set_style_bg_opa(indicator, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(indicator, drawNavigationIcon, LV_EVENT_DRAW_MAIN,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(tab)));
        if (current)
            lv_obj_add_state(nav, LV_STATE_CHECKED);
    }
    if (chromeDimmed()) {
        lv_obj_t *scrim = box(footer, 0, 0, 800, 60, theme().veil);
        lv_obj_set_style_bg_opa(scrim, LV_OPA_40, 0);
    }
}
void render() {
    ui_history_view::captureScroll();
    ui_history_view::reset();
    teal = theme().accent;
    danger = theme().bad;
    amber = theme().warning;
    if (networkEditor) {
        lv_obj_del(networkEditor);
        networkEditor = networkEditField = networkEditTarget = keyboard = nullptr;
    }
    if (overlay) {
        lv_obj_del(overlay);
        overlay = nullptr;
    }
    bindingCount = 0;
    planDayLabel = planNowBadge = planNowLabel = planMarker = nullptr;
    planFeedingRail = nullptr;
    planFeedingReady = false;
    planMinute = UINT16_MAX;
    planDay = UINT8_MAX;
    planMode = UINT8_MAX;
    for (unsigned index = 0; index < kPlanRows; ++index) {
        planRails[index] = nullptr;
        planCache[index].ready = false;
    }
    healthCard = healthTitle = healthDetail = waterGraphic = waterStatus = nullptr;
    feedButton = feedLast = feedNext = nullptr;
    flowStatus = diagnosticsLabel = networkDetailsLabel = firmwareLabel = nullptr;
    securityPin = serviceRemaining = saveLabel = feedVisual = feedCaption = clockLive = clockDate = nullptr;
    feedCardGraphic = feedCardPercent = feedCardPhase = feedCardPosition = feedCardSource = nullptr;
    feedCardCountdown = feedCardFill = nullptr;
    additionalDoseButton = nullptr;
    clockSyncButton = clockNetworkLabel = clockOrigin = nullptr;
    numericSlider = numericLabel = nullptr;
    networkSsid = networkPassword = solarCityField = nullptr;
    pinLabel = countdownLabel = wirelessDetailsLabel = clockDetailsLabel = clockSourceLabel = nullptr;
    headerClock = headerDate = headerNetworkIcon = headerNetworkCaption = nullptr;
    lv_obj_t *screen = lv_scr_act();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(bg()), 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *header = box(screen, 0, 0, 800, 56, chromeBg());
    lv_obj_t *brand = box(header, 24, 7, 42, 42, theme().accent, 11);
    lv_obj_add_event_cb(brand, drawBrandMark, LV_EVENT_DRAW_MAIN, nullptr);
    label(header, "AquaBox", 78, 5, 184, &ui_font_24, chromeInk());
    label(header, "akwarystyka pod kontrolą", 79, 32, 207, &ui_font_14, chromeMuted());
    headerClock = label(header, "", 310, 3, 184, &ui_font_24, chromeInk());
    lv_obj_set_style_text_align(headerClock, LV_TEXT_ALIGN_CENTER, 0);
    headerDate = label(header, "", 300, 33, 204, &ui_font_14, chromeMuted());
    lv_obj_set_style_text_align(headerDate, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *wifi = box(header, 560, 4, 216, 44, chromeBg(), ui_design::radius::control);
    lv_obj_set_style_bg_opa(wifi, LV_OPA_TRANSP, 0);
    clickable(wifi, Action::Wireless);
    headerNetworkIcon = box(wifi, 182, 0, 34, 40, chromeBg());
    lv_obj_set_style_bg_opa(headerNetworkIcon, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(headerNetworkIcon, drawWireless, LV_EVENT_DRAW_MAIN, nullptr);
    headerNetworkCaption = label(wifi, "", 0, 15, 174, &ui_font_14, chromeMuted());
    lv_obj_set_style_text_align(headerNetworkCaption, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(headerNetworkCaption, LV_LABEL_LONG_DOT);
    rule(header, 24, 54, 752, chromeLine());
    if (chromeDimmed()) {
        lv_obj_t *scrim = box(header, 0, 0, 800, 56, theme().veil);
        lv_obj_set_style_bg_opa(scrim, LV_OPA_40, 0);
    }
    body = box(screen, ui_design::bodyInset, ui_navigation::kBodyY,
               800 - 2 * ui_design::bodyInset, ui_navigation::kBodyHeight, bg());
    if (u.page == Page::Main) {
        switch (u.tab) {
        case Tab::Home:
            home();
            break;
        case Tab::Service:
            servicePage();
            break;
        case Tab::Plan:
            plan();
            break;
        case Tab::History:
            history();
            break;
        case Tab::System:
            system();
            break;
        }
    } else
        switch (u.page) {
        case Page::Numeric:
            numericPage();
            break;
        case Page::Module:
            modulePage();
            break;
        case Page::Schedule:
            schedulePage();
            break;
        case Page::Heater:
            heaterPage();
            break;
        case Page::Feeding:
            feedingPage();
            break;
        case Page::Alarms:
            alarms();
            break;
        case Page::Calibration:
            calibration();
            break;
        case Page::Settings:
            settings();
            break;
        case Page::TouchTest: {
            heading("Test dotyku", "Dotknij kolejno pięciu oznaczonych pól / możesz przerwać strzałką");
            const int points[5][2] = {{0, 75}, {638, 75}, {320, 155}, {0, 260}, {638, 260}};
            char target[40];
            std::snprintf(target, sizeof(target), "Cel %u / 5", u.touchTarget + 1);
            button(body, target, points[u.touchTarget][0], points[u.touchTarget][1], 110, 60,
                   Action::TouchTarget, 0, true);
            break;
        }
        case Page::Range: {
            const char *titles[] = {"Zakres temperatury", "Zakres pH", "Zakres EC"};
            heading(titles[u.rangeKind],
                    "Własne granice akwarium / zakres nie zmienia ochrony grzałki 28,5°C");
            char low[24], high[24];
            decimal(low, sizeof(low), u.rangeDraft.minimum, u.rangeKind == 2 ? 0 : 1);
            decimal(high, sizeof(high), u.rangeDraft.maximum, u.rangeKind == 2 ? 0 : 1);
            stepper("DOLNA GRANICA", low, 0, 80, 340, 10);
            stepper("GÓRNA GRANICA", high, 396, 80, 340, 11);
            label(body,
                  "Zaznaczony odcinek skali pokazuje te granice.\nPoza nimi pojawia się ostrzeżenie, a "
                  "odczyt "
                  "zmienia kolor.\nDopasuj zakres do swojego akwarium, nie do uniwersalnej normy.",
                  0, 189, 752, &ui_font_16, muted());
            editFooter();
            break;
        }
        case Page::Main:
            break;
        }
    feedingCard();
    statusStrip = box(screen, 24, 58, 752, 24, chromeBg(), 3);
    lv_obj_set_style_opa(statusStrip, chromeDimmed() ? LV_OPA_20 : LV_OPA_COVER, 0);
    clickable(statusStrip, Action::Stop, 0);
    ribbon = label(statusStrip, "", 8, 3, 736, &ui_font_14, chromeInk());
    navigation(screen);
    if (u.entryDirection && !u.prefs.reducedMotion && u.page == Page::Main && u.dialog == Dialog::None) {
        // Only the current body is animated: no second full widget tree/framebuffer.
        lv_anim_t motion;
        lv_anim_init(&motion);
        lv_anim_set_var(&motion, body);
        lv_anim_set_exec_cb(&motion, [](void *object, int32_t x) {
            lv_obj_set_x(static_cast<lv_obj_t *>(object), static_cast<lv_coord_t>(x));
        });
        lv_anim_set_values(&motion, ui_design::bodyInset + u.entryDirection * 72,
                           ui_design::bodyInset);
        lv_anim_set_time(&motion, ui_design::motion::normal);
        lv_anim_set_path_cb(&motion, lv_anim_path_ease_out);
        lv_anim_start(&motion);
    }
    u.entryDirection = 0;
    refreshBindings();
    renderDialog();
    if (toast)
        lv_obj_move_foreground(toast);
}
void editSchedule(ModuleId id) {
    u.selected = id;
    u.schedule = snapshot().schedules[static_cast<int>(id)];
    if (id == ModuleId::FrontLight || id == ModuleId::RearLight)
        u.lightProgram = snapshot().lightPrograms[static_cast<int>(id)];
    openPage(Page::Schedule);
}
bool captureNetworkDraft() {
    if (u.setting != Setting::Network || !networkPassword)
        return true;
    const char *password = lv_textarea_get_text(networkPassword);
    const size_t capacity = u.prefDraft.network == ui_runtime::NetworkMode::AccessPoint
                                ? sizeof(u.prefDraft.apPassword)
                                : sizeof(u.prefDraft.password);
    if (std::strlen(password) >= capacity ||
        (networkSsid && u.prefDraft.network != ui_runtime::NetworkMode::AccessPoint &&
         std::strlen(lv_textarea_get_text(networkSsid)) >= sizeof(u.prefDraft.ssid))) {
        notify("Zbyt długie dane sieci / SSID maks. 32 bajty", true);
        return false;
    }
    if (u.prefDraft.network == ui_runtime::NetworkMode::AccessPoint)
        std::snprintf(u.prefDraft.apPassword, sizeof(u.prefDraft.apPassword), "%s",
                      lv_textarea_get_text(networkPassword));
    else {
        if (networkSsid)
            std::snprintf(u.prefDraft.ssid, sizeof(u.prefDraft.ssid), "%s",
                          lv_textarea_get_text(networkSsid));
        std::snprintf(u.prefDraft.password, sizeof(u.prefDraft.password), "%s",
                      lv_textarea_get_text(networkPassword));
    }
    return true;
}
void save() {
    if (u.prefs.lock && static_cast<int32_t>(u.unlockUntil - millis()) <= 0) {
        saveFailed = true;
        editDirty = false;
        leaveAfterSave = false;
        notify("Sesja PIN wygasła / odblokuj edycję przed zapisem", true);
        u.rebuild = true;
        return;
    }
    ui_runtime::Configuration config;
    config.prefs = u.prefs;
    config.domain = configuration();
    if (u.page == Page::Schedule) {
        config.domain.schedules[static_cast<unsigned>(u.selected)] = u.schedule;
        if (u.selected == ModuleId::FrontLight || u.selected == ModuleId::RearLight)
            config.domain.lightPrograms[static_cast<unsigned>(u.selected)] = u.lightProgram;
    }
    else if (u.page == Page::Heater)
        config.domain.heater = u.heater;
    else if (u.page == Page::Feeding)
        config.domain.feeding = u.feeding;
    else if (u.page == Page::Range)
        config.domain.ranges[u.rangeKind] = u.rangeDraft;
    else if (u.page == Page::Settings) {
        if (u.setting == Setting::Feeder) {
            config.domain.feeding = u.feeding;
            config.domain.feeder = u.feeder;
        }
        if (u.setting == Setting::Network && !captureNetworkDraft()) {
            editDirty = false;
            saveFailed = true;
            leaveAfterSave = false;
            u.rebuild = true;
            return;
        }
        if (u.setting == Setting::Security && securityPin)
            std::snprintf(u.prefDraft.pin, sizeof(u.prefDraft.pin), "%s",
                          lv_textarea_get_text(securityPin));
        config.prefs = u.prefDraft;
    } else
        return;
    const ui_runtime::Result validation = ui_runtime::validate(config);
    if (!validation.ok) {
        saveFailed = true;
        editDirty = false;
        leaveAfterSave = false;
        notify(validation.message, true);
        u.rebuild = true;
        return;
    }
    ui_runtime::Configuration current;
    current.prefs = u.prefs;
    current.domain = configuration();
    if (ui_runtime::equivalent(current, config)) {
        editDirty = saveFailed = false;
        if (clockAfterSave) {
            clockAfterSave = false;
            const auto result = ui_runtime::setDateTime(u.clockDraft);
            notify(result.message, !result.ok);
            if (result.ok)
                u.clockEditing = false;
        }
        if (leaveAfterSave)
            u.page = Page::Main;
        leaveAfterSave = false;
        u.rebuild = true;
        return;
    }
    const ui_runtime::Result result = ui_io::submit(ui_io::Operation::Save, &config);
    if (!result.ok) {
        saveFailed = true;
        editDirty = false;
        leaveAfterSave = false;
        notify(result.message, true);
        u.rebuild = true;
        return;
    }
    savingConfig = config;
    savePending = true;
    saveFailed = false;
    editDirty = false;
    u.rebuild = true;
}
void markEdited() {
    editDirty = true;
    saveFailed = false;
    saveAt = millis() + 1200;
    if (saveLabel)
        lv_label_set_text(saveLabel, "Oczekiwanie na zapis");
}
void servicePersistence() {
    ui_io::Completion completion;
    while (ui_io::take(completion)) {
        if (completion.operation == ui_io::Operation::Save) {
            savePending = false;
            if (completion.ok) {
                saveConfirmed = true;
                const Preferences previous = u.prefs;
                u.prefs = savingConfig.prefs;
                u.prefDraft = u.prefs;
                restoreConfiguration(savingConfig.domain);
                if (std::strcmp(previous.pin, u.prefs.pin) != 0 || (!previous.lock && u.prefs.lock))
                    u.unlockUntil = millis() + 300000;
                const bool networkChanged = previous.network != u.prefs.network ||
                                            previous.ntp != u.prefs.ntp ||
                                            std::strcmp(previous.ssid, u.prefs.ssid) ||
                                            std::strcmp(previous.password, u.prefs.password) ||
                                            std::strcmp(previous.apPassword, u.prefs.apPassword);
                if (networkChanged) {
                    const auto applied = ui_runtime::applyNetwork(u.prefs);
                    if (!applied.ok)
                        notify(applied.message, true);
                }
                ui_runtime::configureSolar(u.prefs);
                recordPanelEvent("Automatycznie zapisano ustawienia");
                if (leaveAfterSave)
                    u.page = Page::Main;
                if (clockAfterSave) {
                    clockAfterSave = false;
                    const auto clockResult = ui_runtime::setDateTime(u.clockDraft);
                    if (!clockResult.ok)
                        notify(clockResult.message, true);
                    if (clockResult.ok)
                        u.clockEditing = false;
                }
            } else {
                clockAfterSave = false;
                saveFailed = true;
                notify(completion.message, true);
            }
            leaveAfterSave = false;
            u.rebuild = true;
        } else if (completion.operation == ui_io::Operation::Reset) {
            if (completion.ok) {
                ui_model::init(millis());
                u = Ui{};
                ui_runtime::Configuration defaults;
                ui_runtime::load(defaults);
                u.prefs = u.prefDraft = defaults.prefs;
                ui_runtime::applyNetwork(u.prefs);
                editDirty = savePending = saveFailed = false;
                u.rebuild = true;
            }
            notify(completion.message, !completion.ok);
        } else {
            if (completion.operation == ui_io::Operation::Clock && !completion.ok) {
                u.clockEditing = true;
                u.rebuild = true;
            }
            if (completion.operation == ui_io::Operation::Verify) {
                std::snprintf(firmwareSummary, sizeof(firmwareSummary), "%s", completion.message);
                if (firmwareLabel)
                    lv_label_set_text(firmwareLabel, firmwareSummary);
            }
            recordPanelEvent(completion.message, completion.ok ? LogLevel::Info : LogLevel::Warning);
            if (!completion.ok)
                notify(completion.message, !completion.ok);
        }
    }
    if (editDirty && u.page != Page::Numeric && !savePending && !overlay && !networkEditor &&
        static_cast<int32_t>(millis() - saveAt) >= 0)
        save();
}
void adjust(int index) {
    int delta = index % 2 ? 1 : -1;
    if (index >= 80) {
        int *fields[] = {&u.clockDraft.year, &u.clockDraft.month, &u.clockDraft.day, &u.clockDraft.hour,
                         &u.clockDraft.minute};
        const int minimum[] = {2024, 1, 1, 0, 0}, maximum[] = {2099, 12, 31, 23, 59};
        const int field = u.clockField;
        *fields[field] += delta;
        if (*fields[field] < minimum[field])
            *fields[field] = maximum[field];
        if (*fields[field] > maximum[field])
            *fields[field] = minimum[field];
        static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        const int dayLimit =
            days[u.clockDraft.month - 1] + (u.clockDraft.month == 2 && u.clockDraft.year % 4 == 0 ? 1 : 0);
        if (u.clockDraft.day > dayLimit)
            u.clockDraft.day = dayLimit;
    } else if (index >= 60) {
        int value = u.dialog == Dialog::Feed      ? u.durationMinutes
                    : u.dialog == Dialog::Service ? u.serviceMinutes
                                                  : u.manualMinutes;
        int max = u.dialog == Dialog::Feed ? 60 : u.dialog == Dialog::Service ? 120 : 1440;
        value += delta * (u.dialog == Dialog::Manual ? 15 : 1);
        if (value < 1)
            value = 1;
        if (value > max)
            value = max;
        if (u.dialog == Dialog::Feed)
            u.durationMinutes = value;
        else if (u.dialog == Dialog::Service)
            u.serviceMinutes = value;
        else
            u.manualMinutes = value;
    } else if (index >= 40) {
        int hour = u.timeDraft / 60, minute = u.timeDraft % 60;
        if (index == 40)
            hour = (hour + 1) % 24;
        if (index == 41)
            hour = (hour + 23) % 24;
        if (index == 42)
            minute = (minute + 1) % 60;
        if (index == 43)
            minute = (minute + 59) % 60;
        u.timeDraft = hour * 60 + minute;
    } else
        switch (index / 2) {
        case 0:
            u.heater.target = std::round((u.heater.target + delta * 0.1f) * 10) / 10;
            if (u.heater.target < 18)
                u.heater.target = 18;
            if (u.heater.target > 28)
                u.heater.target = 28;
            break;
        case 1:
            u.heater.hysteresis = std::round((u.heater.hysteresis + delta * 0.1f) * 10) / 10;
            if (u.heater.hysteresis < 0.1f)
                u.heater.hysteresis = 0.1f;
            if (u.heater.hysteresis > 2)
                u.heater.hysteresis = 2;
            break;
        case 2: {
            int v = u.feeding.pauseSeconds + delta * 60;
            if (v < 60)
                v = 60;
            if (v > 3600)
                v = 3600;
            u.feeding.pauseSeconds = v;
            break;
        }
        case 7: {
            const int value = u.feeder.rotationDegrees + delta;
            u.feeder.rotationDegrees = static_cast<uint16_t>(value < 1 ? 1 : value > 360 ? 360 : value);
            break;
        }
        case 4: {
            int v = u.prefDraft.sleepMinutes + delta;
            if (v < 0)
                v = 0;
            if (v > 60)
                v = 60;
            u.prefDraft.sleepMinutes = v;
            break;
        }
        case 6:
            u.serviceMinutes =
                static_cast<uint16_t>(std::fmax(1, std::fmin(120, u.serviceMinutes + delta)));
            break;
        case 10:
        case 11: {
            float &value = index / 2 == 10 ? u.rangeDraft.minimum : u.rangeDraft.maximum;
            value += delta * (u.rangeKind == 2 ? 10.0f : 0.1f);
            value = std::round(value * 10.0f) / 10.0f;
            break;
        }
        }
    u.rebuild = true;
}
void confirm() {
    bool good = true;
    const Snapshot &s = snapshot();
    switch (u.dialog) {
    case Dialog::Time:
        if (u.timeField < 2) {
            if (u.timeField == 0)
                u.schedule.startMinute = u.timeDraft;
            else
                u.schedule.endMinute = u.timeDraft;
        } else if (u.timeField < 4)
            u.feeding.times[u.timeField - 2] = u.timeDraft;
        else if (u.timeField == 4)
            u.lightProgram.dayStartMinute = u.timeDraft;
        else if (u.timeField == 5)
            u.lightProgram.duskStartMinute = u.timeDraft;
        else if (u.timeField == 6)
            u.lightProgram.nightStartMinute = u.timeDraft;
        markEdited();
        break;
    case Dialog::Feed:
        if (s.feedingActive)
            cancelFeeding();
        else {
            const auto result = startFeeding(u.durationMinutes * 60, u.feedDose);
            good = result.ok();
            notify(good ? (u.feedDose ? "Karmienie rozpoczęte / jedna dawka"
                                      : "Pauza obiegu rozpoczęta / bez dawki")
                        : result.message,
                   !good);
        }
        break;
    case Dialog::Service:
        if (s.serviceActive)
            cancelService();
        else
            good = accepted(startService(u.serviceMinutes * 60));
        break;
    case Dialog::Manual:
        good = accepted(setTimedMode(u.selected, u.manual, u.manualMinutes * 60));
        break;
    case Dialog::Light:
        good = accepted(setLightMode(u.selected, u.light));
        break;
    case Dialog::Stop:
        if (s.feedingActive)
            cancelFeeding();
        if (s.serviceActive)
            cancelService();
        break;
    case Dialog::Reset:
        if (!ui_io::submit(ui_io::Operation::Reset).ok) {
            notify("Nie udało się przywrócić ustawień", true);
            return;
        }
        break;
    case Dialog::Restart:
        ui_runtime::requestRestart();
        notify("Panel uruchomi się ponownie");
        break;
    case Dialog::ClearHistory:
        clearHistory();
        break;
    case Dialog::ClearLog:
        clearLog();
        break;
    default:
        break;
    }
    if (good) {
        u.dialog = Dialog::None;
        u.rebuild = true;
    }
}
void onEvent(lv_event_t *e) {
    uintptr_t data = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    Action action = static_cast<Action>(data >> 16);
    int i = static_cast<uint16_t>(data);
    lastActivity = millis();
    dispatch(action, i);
}
void sleepScreen() {
    if (sleepOverlay || overlay || networkEditor)
        return;
    sleepOverlay = box(lv_layer_top(), 0, 0, 800, 480, 0x000000);
    lv_obj_add_flag(sleepOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(sleepOverlay, LV_OBJ_FLAG_GESTURE_BUBBLE);
    // Consume the first touch on this overlay; never pass it through to an output control.
    lv_obj_add_event_cb(
        sleepOverlay, [](lv_event_t *) { dispatch(Action::Wake, 0); }, LV_EVENT_PRESSED, nullptr);
}
void dispatch(Action action, int i) {
    if (savePending && action == Action::EditValue)
        return;
    const bool configurationChange =
        action == Action::Day || action == Action::ScheduleMode ||
        action == Action::LightProfileToggle || action == Action::NetworkMode ||
        action == Action::ColorPreset ||
        (action == Action::Adjust && i < 40 &&
         (u.page == Page::Heater || u.page == Page::Feeding || u.page == Page::Range ||
          (u.page == Page::Settings && (u.setting == Setting::Display || u.setting == Setting::Feeder)))) ||
        (action == Action::Toggle && (i < 4 || (i >= 5 && i <= 7) || i == 10 ||
                                     i == 14 || i == 15 || (i >= 20 && i < 31)));
    if (action == Action::Toggle && ((i == 14 && u.prefDraft.ntp) || (i == 15 && !u.prefDraft.ntp)))
        return;
    if (savePending &&
        (configurationChange || action == Action::Back || action == Action::Cancel ||
         action == Action::Restart || action == Action::Stop || action == Action::RetrySave ||
         action == Action::RevertChanges || action == Action::ClockSave || action == Action::ClockSync ||
         action == Action::ClockEdit || action == Action::ClockOccurrence || action == Action::ClockField ||
         (action == Action::Adjust && i >= 80))) {
        return;
    }
    const bool mutation =
        (action == Action::ValueAccept && u.valueDraft != u.valueOriginal && u.valueIndex != 6 &&
         u.valueIndex != 8) ||
        (action == Action::Feed && !snapshot().feedingActive &&
         !canRequestAdditionalDose(snapshot())) || action == Action::ServiceStart ||
        action == Action::ServiceOutput ||
        action == Action::Restart || action == Action::ClearLog || action == Action::ClearHistory ||
        action == Action::Scenario || action == Action::Stop || action == Action::Reconnect ||
        action == Action::RetrySave || action == Action::ClockSave || action == Action::ClockSync ||
        action == Action::ResetFeederFault || configurationChange ||
        (action == Action::ServiceLight && snapshot().serviceActive);
    if (mutation && u.prefs.lock && static_cast<int32_t>(u.unlockUntil - millis()) <= 0) {
        u.pending = true;
        u.pendingAction = action;
        u.pendingIndex = i;
        u.gatedPage = u.page;
        u.pinPurpose = PinPurpose::Unlock;
        u.pinLength = 0;
        u.pin[0] = 0;
        u.dialog = Dialog::Pin;
        u.rebuild = true;
        return;
    }
    switch (action) {
    case Action::EditValue:
        openNumeric(i);
        break;
    case Action::ValueStep:
        u.valueDraft += i == 1 ? 1 : -1;
        refreshNumeric();
        break;
    case Action::ValueAccept:
        commitNumeric();
        break;
    case Action::Wireless:
        if (i) {
            if (editing() || savePending || editDirty || saveFailed) {
                notify("Najpierw wróć z bieżącej edycji", true);
                break;
            }
            u.dialog = Dialog::None;
            u.setting = Setting::Network;
            u.prefDraft = u.prefs;
            openPage(Page::Settings);
        } else {
            u.dialog = Dialog::Wireless;
            u.rebuild = true;
        }
        break;
    case Action::Nav:
        navigate(i, true);
        break;
    case Action::Back:
    case Action::Cancel:
        if (editDirty || saveFailed) {
            leaveAfterSave = true;
            save();
            return;
        }
        if (u.page == Page::Settings && u.setting == Setting::FeederManual) {
            u.setting = Setting::Feeder;
            u.rebuild = true;
            break;
        }
        if (u.page == Page::Module && u.moduleFromMap) {
            u.page = Page::Settings;
            u.setting = Setting::Hardware;
            u.moduleFromMap = false;
        } else
            u.page = Page::Main;
        u.dialog = Dialog::None;
        u.rebuild = true;
        break;
    case Action::Module:
        if (u.page != Page::Module)
            u.moduleFromMap = u.page == Page::Settings && u.setting == Setting::Hardware;
        u.selected = static_cast<ModuleId>(i);
        u.page = Page::Module;
        u.rebuild = true;
        break;
    case Action::Category:
        u.category = u.category == i ? -1 : i;
        u.rebuild = true;
        break;
    case Action::SystemCategory:
        if (i >= 0 && i < 5) {
            u.systemCategory = static_cast<uint8_t>(i);
            u.rebuild = true;
        }
        break;
    case Action::HardwareCategory:
        if (i >= 0 && i < 4)
            u.hardwareCategory = static_cast<uint8_t>(i);
        u.rebuild = true;
        break;
    case Action::Schedule:
        editSchedule(static_cast<ModuleId>(i));
        break;
    case Action::EditHeater:
        u.heater = snapshot().heater;
        openPage(Page::Heater);
        break;
    case Action::EditFeeding:
        u.feeding = snapshot().feeding;
        openPage(Page::Feeding);
        break;
    case Action::Alarm:
        if (i) {
            acknowledgeAlarms();
            u.rebuild = true;
        } else {
            u.page = Page::Alarms;
            u.rebuild = true;
        }
        break;
    case Action::ResetFeederFault:
        accepted(acknowledgeFeederFault());
        u.rebuild = true;
        break;
    case Action::Setting:
        if (i < 0 || i > static_cast<int>(Setting::FeederManual))
            break;
        if (i == static_cast<int>(Setting::FeederManual) &&
            (editDirty || savePending || saveFailed)) {
            notify("Najpierw zapisz ustawienia karmnika", true);
            break;
        }
        u.setting = static_cast<Setting>(i);
        u.prefDraft = u.prefs;
        if (u.setting == Setting::Colors) {
            u.colorNight = ui_runtime::nightTheme(u.prefs);
        }
        u.feeding = snapshot().feeding;
        u.feeder = snapshot().feeder;
        if (u.setting == Setting::Clock)
            u.clockDraft = ui_runtime::dateTime();
        u.clockEditing = false;
        openPage(Page::Settings);
        break;
    case Action::ColorMode:
        if (i == 0 || i == 1) {
            u.colorNight = i == 1;
            u.rebuild = true;
        }
        break;
    case Action::ColorPreset:
        if (u.page == Page::Settings && u.setting == Setting::Colors &&
            i >= 0 && i < ui_theme::kPresetCount) {
            ui_theme::Settings selected = u.prefDraft.colors;
            selected.preset[u.colorNight ? 1u : 0u] = static_cast<uint8_t>(i);
            if (!ui_theme::valid(selected)) {
                notify("Nieprawidłowe dane motywu", true);
                break;
            }
            u.prefDraft.colors = selected;
            markEdited();
            u.rebuild = true;
        }
        break;
    case Action::FeederManualStep:
        if (u.page == Page::Settings && u.setting == Setting::FeederManual &&
            (i == 5 || i == 15 || i == 45)) {
            u.feederManualStep = static_cast<uint8_t>(i);
            u.rebuild = true;
        }
        break;
    case Action::FeederManualJog:
        // The UI must never report a motor command as accepted before the ULN2003 is wired.
        notify("Napęd ULN2003 niepodłączony / ruch zablokowany", true);
        break;
    case Action::Scenario:
        setScenario(static_cast<Scenario>(i));
        u.rebuild = true;
        break;
    case Action::Manual:
        u.manual = static_cast<Mode>(i);
        if (u.manual == Mode::Auto) {
            accepted(setTimedMode(u.selected, u.manual, 0));
            u.rebuild = true;
        } else {
            u.dialog = Dialog::Manual;
            u.rebuild = true;
        }
        break;
    case Action::Light:
        u.light = static_cast<LightMode>(i);
        u.dialog = Dialog::Light;
        u.rebuild = true;
        break;
    case Action::Feed:
        if (snapshot().feedingActive) {
            u.feedCardOpen = true;
            u.rebuild = true;
            break;
        }
        if (canRequestAdditionalDose(snapshot())) {
            dispatch(Action::AdditionalDose, 0);
            break;
        }
        if (accepted(startFeeding(snapshot().feeding.pauseSeconds, true))) {
            u.feedCardOpen = true;
            u.rebuild = true;
        }
        break;
    case Action::AdditionalDose:
        if (!canRequestAdditionalDose(snapshot())) {
            notify("Kolejna dawka wymaga potwierdzenia poprzedniej i aktywnego odstępu", true);
            break;
        }
        u.pending = false;
        u.pinPurpose = PinPurpose::AdditionalDose;
        u.adminDoseJob = snapshot().motor.job;
        u.pinLength = 0;
        u.pin[0] = 0;
        u.dialog = Dialog::Pin;
        u.rebuild = true;
        break;
    case Action::Service:
        u.tab = Tab::Service;
        u.page = Page::Main;
        u.rebuild = true;
        break;
    case Action::ServiceStart:
        u.dialog = Dialog::Service;
        u.rebuild = true;
        break;
    case Action::Stop:
        if (i == 2) {
            u.dialog = Dialog::Reset;
            u.rebuild = true;
        } else if (snapshot().feedingActive || snapshot().serviceActive) {
            u.dialog = Dialog::Stop;
            u.rebuild = true;
        } else if (snapshot().alarmFlags) {
            u.page = Page::Alarms;
            u.rebuild = true;
        }
        break;
    case Action::Confirm:
        confirm();
        break;
    case Action::Dismiss:
        if (u.feedCardOpen && u.dialog == Dialog::None) {
            u.feedCardOpen = false;
            u.rebuild = true;
            break;
        }
        u.pending = false;
        u.pinPurpose = PinPurpose::Unlock;
        u.adminDoseJob = 0;
        u.pinLength = 0;
        u.pin[0] = 0;
        u.dialog = Dialog::None;
        u.closeOverlay = true;
        break;
    case Action::Save:
        save();
        break;
    case Action::Day:
        if (u.page == Page::Schedule)
            u.schedule.dayMask ^= 1 << i;
        else
            u.feeding.dayMask ^= 1 << i;
        u.rebuild = true;
        break;
    case Action::ScheduleMode:
        u.schedule.mode = static_cast<ScheduleMode>(i);
        u.rebuild = true;
        break;
    case Action::LightProfileToggle:
        u.lightProgram.enabled = !u.lightProgram.enabled;
        if (u.lightProgram.enabled && !validateLightProgram(u.schedule, u.lightProgram).ok()) {
            const uint16_t duration = static_cast<uint16_t>((u.schedule.endMinute + 1440u -
                                                             u.schedule.startMinute) % 1440u);
            if (duration >= 4) {
                const uint16_t day = static_cast<uint16_t>(duration / 4);
                const uint16_t dusk = static_cast<uint16_t>(duration / 2);
                const uint16_t night = static_cast<uint16_t>(duration * 3 / 4);
                u.lightProgram.dayStartMinute = static_cast<uint16_t>((u.schedule.startMinute + day) % 1440u);
                u.lightProgram.duskStartMinute = static_cast<uint16_t>((u.schedule.startMinute + dusk) % 1440u);
                u.lightProgram.nightStartMinute = static_cast<uint16_t>((u.schedule.startMinute + night) % 1440u);
            }
        }
        u.rebuild = true;
        break;
    case Action::Time:
        u.timeField = i;
        u.timeDraft = i < 2 ? (i == 0 ? u.schedule.startMinute : u.schedule.endMinute)
                      : i < 4 ? u.feeding.times[i - 2]
                      : i == 4 ? u.lightProgram.dayStartMinute
                      : i == 5 ? u.lightProgram.duskStartMinute
                               : u.lightProgram.nightStartMinute;
        u.dialog = Dialog::Time;
        u.rebuild = true;
        break;
    case Action::Adjust:
        adjust(i);
        break;
    case Action::Chart:
        u.chart = i;
        u.rebuild = true;
        break;
    case Action::Calibrate:
        u.calStep = 0;
        u.calEc = i == 1;
        openPage(Page::Calibration);
        break;
    case Action::CalNext:
        if (u.calStep == (u.calEc ? 3 : 4))
            u.page = Page::Main;
        else
            ++u.calStep;
        u.rebuild = true;
        break;
    case Action::CalBuffer:
        u.buffer686 = !u.buffer686;
        u.rebuild = true;
        break;
    case Action::Toggle:
        if (i == 0)
            u.prefDraft.night = !u.prefDraft.night;
        if (i == 1)
            u.prefDraft.feedback = !u.prefDraft.feedback;
        if (i == 5 || i == 6) {
            u.prefDraft.autoTheme = false;
            u.prefDraft.night = i == 6;
        }
        if (i == 7)
            u.prefDraft.autoTheme = true;
        if (i == 2)
            u.prefDraft.reducedMotion = !u.prefDraft.reducedMotion;
        if (i == 3) {
            if (securityPin)
                std::snprintf(u.prefDraft.pin, sizeof(u.prefDraft.pin), "%s",
                              lv_textarea_get_text(securityPin));
            u.prefDraft.lock = !u.prefDraft.lock;
        }
        if (i == 10)
            u.feeding.count = u.feeding.count == 2 ? 1 : 2;
        if (i == 11)
            u.feedDose = !u.feedDose;
        if (i == 13) {
            if (!captureNetworkDraft())
                break;
            u.apVisible = !u.apVisible;
        }
        if (i == 14 || i == 15)
            u.prefDraft.ntp = i == 14;
        if (i >= 20 && i < 31 && u.setting == Setting::Feeder)
            u.feeder.pauseMask ^= outputBit(static_cast<ModuleId>(i - 20));
        u.rebuild = true;
        break;
    case Action::PinDigit:
        if (static_cast<int32_t>(u.pinBlockedUntil - millis()) > 0)
            break;
        if (u.pinLength < 4) {
            u.pin[u.pinLength++] = static_cast<char>('0' + i);
            u.pin[u.pinLength] = 0;
        }
        u.rebuild = true;
        break;
    case Action::PinBack:
        if (u.pinLength)
            u.pin[--u.pinLength] = 0;
        u.rebuild = true;
        break;
    case Action::PinEnter: {
        if (static_cast<int32_t>(u.pinBlockedUntil - millis()) > 0) {
            notify("Za dużo prób / odczekaj 30 sekund", true);
            break;
        }
        const bool additional = u.pinPurpose == PinPurpose::AdditionalDose;
        const Validation result = additional ? startAdditionalDose(snapshot().feeding.pauseSeconds,
                                                                  u.adminDoseJob, u.pin)
                                             : Validation{Error::None, ""};
        if (additional ? result.error != Error::InvalidPin : std::strcmp(u.pin, u.prefs.pin) == 0) {
            u.failedPins = 0;
            u.pinLength = 0;
            std::memset(u.pin, 0, sizeof(u.pin));
            u.pinPurpose = PinPurpose::Unlock;
            u.adminDoseJob = 0;
            u.dialog = Dialog::None;
            u.rebuild = true;
            if (additional) {
                u.pending = false;
                u.feedCardOpen = result.ok();
                notify(result.ok() ? "Dodatkowa dawka rozpoczęta" : result.message, !result.ok());
            } else {
                u.unlockUntil = millis() + 300000;
                u.page = u.gatedPage;
                if (u.pending) {
                    u.pending = false;
                    dispatch(u.pendingAction, u.pendingIndex);
                }
            }
        } else {
            u.pinLength = 0;
            std::memset(u.pin, 0, sizeof(u.pin));
            if (++u.failedPins >= 5) {
                u.failedPins = 0;
                u.pinBlockedUntil = millis() + 30000;
                u.rebuild = true;
                notify("5 błędów / blokada na 30 sekund", true);
            } else
                notify(additional ? "Błędny PIN administratora." : "Błędny PIN.", true);
        }
        break;
    }
    case Action::ServiceLight:
        accepted(chooseServiceLight(i == 0 ? ModuleId::FrontLight : ModuleId::RearLight));
        u.rebuild = true;
        break;
    case Action::ServiceOutput: {
        const ModuleId id = static_cast<ModuleId>(i);
        accepted(setServiceOutput(id, module(id).state != State::On));
        u.rebuild = true;
        break;
    }
    case Action::NetworkMode:
        if (i >= 0 && i <= 2 && captureNetworkDraft()) {
            u.prefDraft.network = static_cast<ui_runtime::NetworkMode>(i);
            u.rebuild = true;
        }
        break;
    case Action::Reconnect: {
        const auto result = ui_runtime::applyNetwork(u.prefs);
        if (!result.ok)
            notify(result.message, true);
        break;
    }
    case Action::ClockField:
        if (i >= 0 && i < 5)
            u.clockField = i;
        u.rebuild = true;
        break;
    case Action::ClockSave: {
        const auto valid = ui_runtime::validateDateTime(u.clockDraft);
        if (!valid.ok) {
            notify(valid.message, true);
            break;
        }
        if (u.prefs.ntp) {
            u.prefDraft.ntp = false;
            clockAfterSave = true;
            markEdited();
            save();
            break;
        }
        const auto result = ui_runtime::setDateTime(u.clockDraft);
        if (!result.ok)
            notify(result.message, true);
        if (result.ok) {
            u.clockEditing = false;
            u.rebuild = true;
        }
        break;
    }
    case Action::ClockSync: {
        const auto result = ui_runtime::syncTime();
        if (!result.ok)
            notify(result.message, true);
        break;
    }
    case Action::ScreenSleep:
        sleepScreen();
        break;
    case Action::Wake:
        lastActivity = millis();
        if (sleepOverlay) {
            lv_obj_del_async(sleepOverlay);
            sleepOverlay = nullptr;
        }
        break;
    case Action::TestTouch:
        u.touchTarget = 0;
        u.page = Page::TouchTest;
        u.rebuild = true;
        break;
    case Action::TouchTarget:
        if (++u.touchTarget == 5) {
            recordPanelEvent("Test dotyku: zaliczono 5 pól");
            notify("Dotyk poprawny / zaliczono wszystkie 5 pól");
            u.page = Page::Settings;
            u.setting = Setting::Diagnostics;
        }
        u.rebuild = true;
        break;
    case Action::Logs:
        u.tab = Tab::History;
        u.chart = 3;
        u.page = Page::Main;
        u.rebuild = true;
        break;
    case Action::ClearHistory:
        u.dialog = Dialog::ClearHistory;
        u.rebuild = true;
        break;
    case Action::ClearLog:
        u.dialog = Dialog::ClearLog;
        u.rebuild = true;
        break;
    case Action::VerifyFirmware: {
        const auto result = ui_runtime::verifyFirmware();
        notify(result.message, !result.ok);
        if (firmwareLabel)
            lv_label_set_text(firmwareLabel, result.message);
        break;
    }
    case Action::ClockEdit:
        u.clockDraft = ui_runtime::dateTime();
        u.clockEditing = true;
        u.rebuild = true;
        break;
    case Action::ClockOccurrence:
        u.clockDraft.dst = i == 1 ? 1 : 0;
        u.rebuild = true;
        break;
    case Action::RetrySave:
        markEdited();
        saveAt = millis();
        break;
    case Action::RevertChanges:
        editDirty = saveFailed = leaveAfterSave = clockAfterSave = false;
        u.prefDraft = u.prefs;
        u.schedule = snapshot().schedules[static_cast<unsigned>(u.selected)];
        if (u.selected == ModuleId::FrontLight || u.selected == ModuleId::RearLight)
            u.lightProgram = snapshot().lightPrograms[static_cast<unsigned>(u.selected)];
        u.heater = snapshot().heater;
        u.feeding = snapshot().feeding;
        u.rangeDraft = snapshot().ranges[u.rangeKind];
        u.feeder = snapshot().feeder;
        u.rebuild = true;
        break;
    case Action::Restart:
        u.dialog = Dialog::Restart;
        u.rebuild = true;
        break;
    case Action::EditRange:
        if (i >= 0 && i < 3) {
            u.rangeKind = i;
            u.rangeDraft = snapshot().ranges[i];
            openPage(Page::Range);
        }
        break;
    }
    if (configurationChange)
        markEdited();
}
} // namespace
bool ui_app_init() {
    if (!lv_disp_get_default() || ESP.getFreePsram() < 256 * 1024)
        return false;
#ifdef AQUARIUM_UI_HOST_PREVIEW
    ui_io::resetPreview();
#endif
    editDirty = savePending = saveFailed = leaveAfterSave = clockAfterSave = false;
    lastDialogShown = Dialog::None;
    ui_status::init();
    u = Ui{};
    ui_runtime::init();
    ui_history_view::resetSession();
    ui_model::setHistoryClock(ui_runtime::historyTime);
    ui_model::init(millis());
    ui_runtime::Configuration config;
    saveConfirmed = ui_runtime::load(config);
    u.prefs = config.prefs;
    restoreConfiguration(config.domain);
    const auto network = ui_runtime::applyNetwork(u.prefs);
    ui_runtime::configureSolar(u.prefs);
    if (!network.ok)
        recordPanelEvent(network.message, LogLevel::Warning);
    if (toast) {
        lv_anim_del(toast, nullptr);
        lv_obj_del(toast);
        toast = nullptr;
    }
    toastClosing = false;
    if (sleepOverlay) {
        lv_obj_del(sleepOverlay);
        sleepOverlay = nullptr;
    }
    lastActivity = millis();
    lastTouchCount = panel_hal_touch_count();
    lv_obj_remove_event_cb(lv_scr_act(), onGesture);
    lv_obj_add_event_cb(lv_scr_act(), onGesture, LV_EVENT_GESTURE, nullptr);
    render();
    lastRevision = snapshot().revision;
    lastLogRevision = logRevision();
    return body != nullptr;
}
void ui_app_service() {
    if (!body)
        return;
    uint32_t now = millis();
    static bool lastThemeNight = false;
    const bool themeNight = ui_runtime::nightTheme(u.prefs);
    if (themeNight != lastThemeNight || ui_runtime::takeSolarThemeChange()) {
        lastThemeNight = themeNight;
        u.rebuild = true;
    }
    const bool wasService = snapshot().serviceActive;
    const bool wasFeeding = snapshot().feedingActive;
    ui_runtime::service();
    servicePersistence();
    ui_model::tick(now);
    if (wasFeeding != snapshot().feedingActive) {
        if (u.tab == Tab::Home && u.page == Page::Main)
            u.rebuild = true;
        if (wasFeeding) {
            u.feedCardOpen = false;
            if (u.dialog == Dialog::Feed || u.dialog == Dialog::Stop) {
                u.dialog = Dialog::None;
                u.closeOverlay = true;
            }
            notify(snapshot().motor.phase == FeederPhase::Fault
                       ? "Brak pozycji karmnika / sprawdź sterownik"
                   : snapshot().motor.source == FeederSource::LocalModel
                       ? "Model karmienia zakończony / AUTO"
                       : "Karmienie zakończone / przywrócono AUTO",
                   snapshot().motor.phase == FeederPhase::Fault);
        }
    }
    if (wasService && !snapshot().serviceActive) {
        if (u.dialog == Dialog::Service || u.dialog == Dialog::Stop) {
            u.dialog = Dialog::None;
            u.closeOverlay = true;
        }
        notify("Serwis zakończony / przywrócono AUTO");
    }
    if (wasService != snapshot().serviceActive && (u.tab == Tab::Service || u.tab == Tab::Home) &&
        u.page == Page::Main)
        u.rebuild = true;
    if (lastLogRevision != logRevision()) {
        lastLogRevision = logRevision();
        if (u.tab == Tab::History && u.page == Page::Main && u.chart == 3 && u.dialog == Dialog::None)
            u.rebuild = true;
    }
    if (ui_runtime::takeVideoRefresh())
        lv_obj_invalidate(lv_scr_act());
    if (u.pinBlockedUntil && static_cast<int32_t>(now - u.pinBlockedUntil) >= 0) {
        u.pinBlockedUntil = 0;
        if (u.dialog == Dialog::Pin)
            u.rebuild = true;
    }
    const uint32_t count = panel_hal_touch_count();
    if (count != lastTouchCount) {
        lastTouchCount = count;
        lastActivity = now;
    }
    if (!u.prefs.reducedMotion && now - lastWave >= 30) {
        lastWave = now;
        if (waterGraphic && waterState() != WaterState::Unknown && waterState() != WaterState::Conflict)
            lv_obj_invalidate(waterGraphic);
        if (feedVisual && snapshot().feedingActive)
            lv_obj_invalidate(feedVisual);
        if (feedCardGraphic && snapshot().motor.phase == FeederPhase::Moving &&
            snapshot().motor.positionValid)
            lv_obj_invalidate(feedCardGraphic);
    }
    if (u.prefs.sleepMinutes && now - lastActivity >= u.prefs.sleepMinutes * 60000u)
        sleepScreen();
    if ((feedCaption || feedCardPercent) && now - lastMotorCaption >= 100) {
        lastMotorCaption = now;
        refreshFeedingCaption();
        refreshFeedingCard();
    }
    // Screen replacement is deferred until LVGL has finished the input callback.
    if (u.closeOverlay) {
        u.closeOverlay = false;
        closeDialog();
    }
    if (u.rebuild) {
        u.rebuild = false;
        render();
    }
    // History owns deferred fullscreen transitions, on this same sole LVGL/UI owner.
    ui_history_view::refresh();
    if (snapshot().revision != lastRevision || now - lastRefresh >= 1000) {
        lastRevision = snapshot().revision;
        lastRefresh = now;
        refreshBindings();
    }
    if (pinLabel) {
        char text[24];
        std::snprintf(text, sizeof(text), "%s%s%s%s", u.pinLength > 0 ? "* " : "– ",
                      u.pinLength > 1 ? "* " : "– ", u.pinLength > 2 ? "* " : "– ",
                      u.pinLength > 3 ? "*" : "–");
        lv_label_set_text(pinLabel, text);
    }
    if (toast && !toastClosing && static_cast<int32_t>(now - toastUntil) >= 0)
        closeToast();
}
