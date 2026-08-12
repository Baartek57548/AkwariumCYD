#include "hmi_ui.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "aquacyd_link_protocol.h"
#include "lvgl.h"

namespace {

constexpr lv_coord_t kScreenWidth = 1024;
constexpr lv_coord_t kScreenHeight = 600;
constexpr lv_coord_t kSidebarWidth = 184;
constexpr lv_coord_t kHeaderHeight = 76;
constexpr lv_coord_t kContentWidth = kScreenWidth - kSidebarWidth;
constexpr lv_coord_t kContentHeight = kScreenHeight - kHeaderHeight;
constexpr uint32_t kSnapshotStaleMs = 7000U;
constexpr uint32_t kToastVisibleMs = 3600U;
constexpr uint32_t kBootMaximumMs = 4200U;
constexpr uint32_t kAnimationFastMs = 120U;
constexpr uint32_t kAnimationNormalMs = 220U;
constexpr uint32_t kAnimationSlowMs = 360U;
constexpr size_t kOutputCount = 5U;
constexpr size_t kAlarmRowCount = 5U;

constexpr uint32_t kColorCanvas = 0x07101FU;
constexpr uint32_t kColorSurface = 0x0D192CU;
constexpr uint32_t kColorCard = 0x142238U;
constexpr uint32_t kColorElevated = 0x1B2D48U;
constexpr uint32_t kColorBorder = 0x29405FU;
constexpr uint32_t kColorText = 0xF4F8FFU;
constexpr uint32_t kColorTextSecondary = 0x91A8C7U;
constexpr uint32_t kColorTextDisabled = 0x60748FU;
constexpr uint32_t kColorAccent = 0x4D8DFFU;
constexpr uint32_t kColorAccentPressed = 0x316FD6U;
constexpr uint32_t kColorSuccess = 0x5CDBA2U;
constexpr uint32_t kColorSuccessDark = 0x143F35U;
constexpr uint32_t kColorWarning = 0xFFB45CU;
constexpr uint32_t kColorWarningDark = 0x49341FU;
constexpr uint32_t kColorDanger = 0xFF6878U;
constexpr uint32_t kColorDangerDark = 0x4B2230U;
constexpr uint32_t kColorInfo = 0x68C5FFU;

constexpr uint32_t kCriticalAlarmMask =
    (1UL << 0U) | (1UL << 4U) | (1UL << 9U);

enum class Page : uint8_t {
    Dashboard = 0U,
    Controls,
    Sensors,
    Alarms,
    Automation,
    System,
    Count
};

enum class AutomationEditor : uint8_t {
    LightPrimary = 0U,
    LightSecondary,
    Filter,
    Aerator,
    Temperature,
    Count
};

struct MetricWidgets {
    lv_obj_t *card;
    lv_obj_t *value;
    lv_obj_t *caption;
    lv_obj_t *bar;
};

struct OutputWidgets {
    lv_obj_t *state;
    lv_obj_t *indicator;
};

struct AlarmDescriptor {
    uint32_t flag;
    const char *title;
    const char *action;
    bool critical;
};

struct CommandDefinition {
    HmiCommandRequest request;
    const char *confirmation_title;
    const char *confirmation_body;
    bool requires_confirmation;
};

struct UiState {
    bool created;
    bool wifi_connected;
    bool mqtt_connected;
    bool controller_online;
    bool command_pending;
    bool boot_visible;
    bool alarm_pulsing;
    Page active_page;
    HmiUiCallbacks callbacks;
    HmiSnapshot snapshot;
    HmiHubSummary hub_summary;
    uint32_t snapshot_received_ms;
    uint32_t last_age_refresh_ms;
    uint32_t toast_hide_at_ms;
    uint32_t boot_started_ms;
    uint8_t brightness;

    lv_obj_t *root;
    lv_obj_t *pages[static_cast<size_t>(Page::Count)];
    lv_obj_t *nav_buttons[static_cast<size_t>(Page::Count)];
    lv_obj_t *page_title;
    lv_obj_t *connection_chip;
    lv_obj_t *connection_chip_label;
    lv_obj_t *alarm_chip;
    lv_obj_t *alarm_chip_label;
    lv_obj_t *sync_label;
    lv_obj_t *stale_banner;
    lv_obj_t *stale_label;

    MetricWidgets dashboard_temperature;
    MetricWidgets dashboard_ph;
    MetricWidgets dashboard_ec;
    lv_obj_t *health_card;
    lv_obj_t *health_indicator;
    lv_obj_t *health_title;
    lv_obj_t *health_detail;
    lv_obj_t *dashboard_outputs[kOutputCount];
    lv_obj_t *dashboard_safety;
    lv_obj_t *dashboard_link;

    OutputWidgets control_outputs[4U];
    lv_obj_t *control_buttons[10U];
    lv_obj_t *command_status;
    lv_obj_t *pending_bar;

    MetricWidgets sensor_temperature;
    MetricWidgets sensor_ph;
    MetricWidgets sensor_ec;
    MetricWidgets sensor_ldr;
    lv_obj_t *sensor_water;
    lv_obj_t *sensor_leak;

    lv_obj_t *alarm_summary;
    lv_obj_t *alarm_summary_detail;
    lv_obj_t *alarm_rows[kAlarmRowCount];
    lv_obj_t *alarm_row_titles[kAlarmRowCount];
    lv_obj_t *alarm_row_actions[kAlarmRowCount];

    lv_obj_t *automation_revision;
    lv_obj_t *automation_status;
    lv_obj_t *automation_summaries[
        static_cast<size_t>(AutomationEditor::Count)];
    lv_obj_t *automation_buttons[
        static_cast<size_t>(AutomationEditor::Count)];

    lv_obj_t *editor_overlay;
    lv_obj_t *editor_title;
    lv_obj_t *editor_help;
    lv_obj_t *schedule_controls;
    lv_obj_t *schedule_mode_value;
    lv_obj_t *schedule_start_value;
    lv_obj_t *schedule_end_value;
    lv_obj_t *schedule_profile_row;
    lv_obj_t *schedule_profile_value;
    lv_obj_t *temperature_controls;
    lv_obj_t *temperature_target_value;
    lv_obj_t *temperature_hysteresis_value;
    lv_obj_t *temperature_mode_value;
    AutomationEditor active_editor;
    HmiSchedule editor_schedule;
    int32_t editor_target_milli_c;
    uint16_t editor_hysteresis_milli_c;
    uint8_t editor_heater_mode;

    lv_obj_t *system_cyd;
    lv_obj_t *system_hmi;
    lv_obj_t *system_network;
    lv_obj_t *system_fingerprint;
    lv_obj_t *brightness_label;

    lv_obj_t *toast;
    lv_obj_t *toast_indicator;
    lv_obj_t *toast_label;

    lv_obj_t *confirm_overlay;
    lv_obj_t *confirm_title;
    lv_obj_t *confirm_body;
    const CommandDefinition *pending_confirmation;

    lv_obj_t *boot_overlay;
    lv_obj_t *boot_mark;
    lv_obj_t *boot_status;
    lv_obj_t *boot_progress;
};

UiState ui = {};

constexpr AlarmDescriptor kAlarmDescriptors[] = {
    {1UL << 4U, "Wykryto wyciek",
     "Odłącz zasilanie urządzeń wykonawczych i sprawdź instalację.", true},
    {1UL << 9U, "Błąd sterowania wyjściem",
     "Sprawdź magistralę MCP23017 oraz zasilanie przekaźników.", true},
    {1UL << 0U, "Temperatura za wysoka",
     "Sprawdź chłodzenie, grzałkę i przepływ wody.", true},
    {1UL << 1U, "Temperatura za niska",
     "Sprawdź grzałkę, czujnik i zasilanie.", false},
    {1UL << 2U, "pH poza zakresem",
     "Zweryfikuj pomiar i kalibrację sondy pH.", false},
    {1UL << 3U, "Niski poziom wody",
     "Uzupełnij wodę i sprawdź układ automatycznej dolewki.", false},
    {1UL << 5U, "Niskie napięcie zasilania",
     "Sprawdź zasilacz oraz połączenia przewodów.", false},
    {1UL << 6U, "Brak wymaganego czujnika",
     "Sprawdź połączenie i konfigurację czujników.", false},
    {1UL << 7U, "Nieaktualne dane czujnika",
     "Sprawdź magistralę i przewody czujnika.", false},
    {1UL << 8U, "Błąd magistrali czujników",
     "Sprawdź terminację, przewody i adresy urządzeń.", false}
};

constexpr CommandDefinition kLightPrimaryOn = {
    {"set_output", "light_primary", 1, 900000U}, "", "", false
};
constexpr CommandDefinition kLightPrimaryOff = {
    {"set_output", "light_primary", 0, 900000U}, "", "", false
};
constexpr CommandDefinition kLightSecondaryOn = {
    {"set_output", "light_secondary", 1, 900000U}, "", "", false
};
constexpr CommandDefinition kLightSecondaryOff = {
    {"set_output", "light_secondary", 0, 900000U}, "", "", false
};
constexpr CommandDefinition kFilterOn = {
    {"set_output", "filter", 1, 900000U}, "", "", false
};
constexpr CommandDefinition kFilterOff = {
    {"set_output", "filter", 0, 900000U},
    "Wyłączyć filtr?",
    "Filtr zostanie wyłączony na 15 minut. Zabezpieczenia CYD pozostaną aktywne.",
    true
};
constexpr CommandDefinition kAeratorOn = {
    {"set_output", "aerator", 1, 900000U}, "", "", false
};
constexpr CommandDefinition kAeratorOff = {
    {"set_output", "aerator", 0, 900000U}, "", "", false
};
constexpr CommandDefinition kFeed = {
    {"trigger_feed", "feeder", 1, 600000U},
    "Uruchomić karmienie?",
    "CYD poda jedną dawkę i włączy bezpieczny tryb karmienia na 10 minut.",
    true
};
constexpr CommandDefinition kRefresh = {
    {"request_snapshot", "controller", 0, 0U}, "", "", false
};

constexpr const CommandDefinition *kCommandDefinitions[] = {
    &kLightPrimaryOn,
    &kLightPrimaryOff,
    &kLightSecondaryOn,
    &kLightSecondaryOff,
    &kFilterOn,
    &kFilterOff,
    &kAeratorOn,
    &kAeratorOff,
    &kFeed,
    &kRefresh
};

constexpr const char *kPageTitles[] = {
    "Centrum akwarium",
    "Sterowanie czasowe",
    "Czujniki i jakość wody",
    "Alarmy i bezpieczeństwo",
    "Automatyka CYD",
    "System i diagnostyka"
};

constexpr const char *kNavigationLabels[] = {
    LV_SYMBOL_HOME "  Podgląd",
    LV_SYMBOL_POWER "  Sterowanie",
    LV_SYMBOL_EYE_OPEN "  Czujniki",
    LV_SYMBOL_WARNING "  Alarmy",
    LV_SYMBOL_LOOP "  Automatyka",
    LV_SYMBOL_SETTINGS "  System"
};

constexpr const char *kAutomationTitles[] = {
    "Światło główne",
    "Światło roślinne",
    "Filtr",
    "Napowietrzanie",
    "Temperatura"
};

constexpr const char *kScheduleTargets[] = {
    "light_primary",
    "light_secondary",
    "filter",
    "aerator"
};

const char *schedule_mode_name(uint8_t mode) {
    switch (mode) {
    case 1U:
        return "ZAWSZE WŁ.";
    case 2U:
        return "ZAWSZE WYŁ.";
    default:
        return "HARMONOGRAM";
    }
}

const char *schedule_profile_name(uint8_t profile) {
    switch (profile) {
    case 1U:
        return "DAY";
    case 2U:
        return "DAYBREAK";
    case 3U:
        return "NIGHT";
    default:
        return "CYKL AUTO";
    }
}

void open_automation_editor(AutomationEditor editor);
void update_editor_values();

lv_color_t color(uint32_t value) {
    return lv_color_hex(value);
}

void set_no_scroll(lv_obj_t *object) {
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
}

void style_transparent(lv_obj_t *object) {
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    set_no_scroll(object);
}

void style_card(lv_obj_t *object, uint32_t background = kColorCard) {
    lv_obj_set_style_bg_color(object, color(background), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(object, color(kColorBorder), 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_radius(object, 18, 0);
    lv_obj_set_style_pad_all(object, 16, 0);
    set_no_scroll(object);
}

lv_obj_t *create_label(lv_obj_t *parent,
                       const char *text,
                       const lv_font_t *font,
                       uint32_t text_color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color(text_color), 0);
    return label;
}

lv_obj_t *create_chip(lv_obj_t *parent,
                      lv_coord_t width,
                      const char *text,
                      uint32_t background,
                      uint32_t foreground) {
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_set_size(chip, width, 38);
    lv_obj_set_style_radius(chip, 19, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip, color(foreground), 0);
    lv_obj_set_style_bg_color(chip, color(background), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(chip, 0, 0);
    set_no_scroll(chip);
    lv_obj_t *label =
        create_label(chip, text, &lv_font_montserrat_16, foreground);
    lv_obj_center(label);
    return chip;
}

lv_obj_t *create_button(lv_obj_t *parent,
                        const char *text,
                        lv_coord_t width,
                        lv_coord_t height,
                        uint32_t background,
                        const CommandDefinition *definition) {
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_bg_color(button, color(background), 0);
    lv_obj_set_style_bg_color(button, color(kColorAccentPressed),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, color(kColorElevated),
                              LV_STATE_DISABLED);
    lv_obj_set_style_text_color(button, color(kColorTextDisabled),
                                LV_STATE_DISABLED);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_t *label =
        create_label(button, text, &lv_font_montserrat_16, kColorText);
    lv_obj_center(label);
    if (definition != nullptr) {
        lv_obj_add_event_cb(
            button,
            [](lv_event_t *event) {
                if (event == nullptr ||
                    lv_event_get_code(event) != LV_EVENT_CLICKED) {
                    return;
                }
                const CommandDefinition *command =
                    static_cast<const CommandDefinition *>(
                        lv_event_get_user_data(event));
                if (command == nullptr) {
                    return;
                }
                if (!ui.mqtt_connected || !ui.controller_online) {
                    hmi_ui_show_toast(
                        HmiFeedbackKind::Warning,
                        "Sterowanie jest zablokowane, ponieważ CYD jest offline.");
                    return;
                }
                if (ui.command_pending) {
                    hmi_ui_show_toast(
                        HmiFeedbackKind::Information,
                        "Poczekaj na potwierdzenie poprzedniego polecenia.");
                    return;
                }
                if (command->requires_confirmation) {
                    ui.pending_confirmation = command;
                    lv_label_set_text(
                        ui.confirm_title, command->confirmation_title);
                    lv_label_set_text(
                        ui.confirm_body, command->confirmation_body);
                    lv_obj_remove_flag(
                        ui.confirm_overlay, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_set_style_opa(
                        ui.confirm_overlay, LV_OPA_TRANSP, 0);
                    lv_anim_t animation;
                    lv_anim_init(&animation);
                    lv_anim_set_var(&animation, ui.confirm_overlay);
                    lv_anim_set_values(
                        &animation, LV_OPA_TRANSP, LV_OPA_COVER);
                    lv_anim_set_duration(&animation, kAnimationFastMs);
                    lv_anim_set_exec_cb(
                        &animation,
                        [](void *object, int32_t value) {
                            lv_obj_set_style_opa(
                                static_cast<lv_obj_t *>(object),
                                static_cast<lv_opa_t>(value),
                                0);
                        });
                    lv_anim_start(&animation);
                    return;
                }
                if (ui.callbacks.command != nullptr) {
                    ui.callbacks.command(
                        command->request, ui.callbacks.context);
                }
            },
            LV_EVENT_CLICKED,
            const_cast<CommandDefinition *>(definition));
    }
    return button;
}

void set_chip_state(lv_obj_t *chip,
                    lv_obj_t *label,
                    const char *text,
                    uint32_t background,
                    uint32_t foreground) {
    lv_label_set_text(label, text);
    lv_obj_set_style_bg_color(chip, color(background), 0);
    lv_obj_set_style_border_color(chip, color(foreground), 0);
    lv_obj_set_style_text_color(label, color(foreground), 0);
}

void opacity_animation(void *object, int32_t value) {
    lv_obj_set_style_opa(
        static_cast<lv_obj_t *>(object), static_cast<lv_opa_t>(value), 0);
}

void x_animation(void *object, int32_t value) {
    lv_obj_set_x(static_cast<lv_obj_t *>(object),
                 static_cast<lv_coord_t>(value));
}

void y_animation(void *object, int32_t value) {
    lv_obj_set_y(static_cast<lv_obj_t *>(object),
                 static_cast<lv_coord_t>(value));
}

void start_enter_animation(lv_obj_t *page) {
    lv_obj_set_x(page, 20);
    lv_obj_set_style_opa(page, LV_OPA_TRANSP, 0);

    lv_anim_t slide;
    lv_anim_init(&slide);
    lv_anim_set_var(&slide, page);
    lv_anim_set_values(&slide, 20, 0);
    lv_anim_set_duration(&slide, kAnimationNormalMs);
    lv_anim_set_path_cb(&slide, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&slide, x_animation);
    lv_anim_start(&slide);

    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, page);
    lv_anim_set_values(&fade, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&fade, kAnimationNormalMs);
    lv_anim_set_exec_cb(&fade, opacity_animation);
    lv_anim_start(&fade);
}

void select_page(Page page) {
    if (!ui.created || page >= Page::Count) {
        return;
    }
    const size_t selected = static_cast<size_t>(page);
    for (size_t index = 0U;
         index < static_cast<size_t>(Page::Count);
         ++index) {
        if (index == selected) {
            lv_obj_remove_flag(ui.pages[index], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_state(ui.nav_buttons[index], LV_STATE_CHECKED);
        } else {
            lv_obj_add_flag(ui.pages[index], LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_state(ui.nav_buttons[index], LV_STATE_CHECKED);
        }
    }
    ui.active_page = page;
    lv_label_set_text(ui.page_title, kPageTitles[selected]);
    start_enter_animation(ui.pages[selected]);
}

void navigation_event(lv_event_t *event) {
    if (event == nullptr || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    const uintptr_t raw =
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    if (raw < static_cast<uintptr_t>(Page::Count)) {
        select_page(static_cast<Page>(raw));
    }
}

MetricWidgets create_metric_card(lv_obj_t *parent,
                                 lv_coord_t x,
                                 lv_coord_t y,
                                 lv_coord_t width,
                                 lv_coord_t height,
                                 const char *title,
                                 const char *caption,
                                 bool with_bar) {
    MetricWidgets widgets = {};
    widgets.card = lv_obj_create(parent);
    lv_obj_set_pos(widgets.card, x, y);
    lv_obj_set_size(widgets.card, width, height);
    style_card(widgets.card);

    lv_obj_t *title_label = create_label(
        widgets.card, title, &lv_font_montserrat_16, kColorTextSecondary);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    widgets.value = create_label(
        widgets.card, "—", &lv_font_montserrat_28, kColorText);
    lv_obj_align(widgets.value, LV_ALIGN_TOP_LEFT, 0, 30);

    widgets.caption = create_label(
        widgets.card, caption, &lv_font_montserrat_16, kColorTextSecondary);
    lv_obj_set_width(widgets.caption, width - 34);
    lv_label_set_long_mode(widgets.caption, LV_LABEL_LONG_DOT);
    lv_obj_align(widgets.caption, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    if (with_bar) {
        widgets.bar = lv_bar_create(widgets.card);
        lv_obj_set_size(widgets.bar, width - 34, 8);
        lv_obj_align(widgets.bar, LV_ALIGN_BOTTOM_LEFT, 0, -28);
        lv_obj_set_style_radius(widgets.bar, 4, LV_PART_MAIN);
        lv_obj_set_style_radius(widgets.bar, 4, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(
            widgets.bar, color(kColorElevated), LV_PART_MAIN);
        lv_obj_set_style_bg_color(
            widgets.bar, color(kColorAccent), LV_PART_INDICATOR);
    }
    return widgets;
}

void set_metric(MetricWidgets &widgets,
                const char *value,
                const char *caption,
                bool valid,
                int32_t bar_value,
                int32_t bar_minimum,
                int32_t bar_maximum) {
    lv_label_set_text(widgets.value, valid ? value : "Brak danych");
    lv_label_set_text(widgets.caption, caption);
    lv_obj_set_style_text_color(
        widgets.value,
        color(valid ? kColorText : kColorWarning),
        0);
    if (widgets.bar != nullptr) {
        lv_bar_set_range(widgets.bar, bar_minimum, bar_maximum);
        lv_bar_set_value(
            widgets.bar,
            valid ? bar_value : bar_minimum,
            LV_ANIM_ON);
        lv_obj_set_style_bg_color(
            widgets.bar,
            color(valid ? kColorAccent : kColorWarning),
            LV_PART_INDICATOR);
    }
}

lv_obj_t *create_output_pill(lv_obj_t *parent,
                             lv_coord_t x,
                             lv_coord_t y,
                             lv_coord_t width,
                             const char *name) {
    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_set_pos(pill, x, y);
    lv_obj_set_size(pill, width, 48);
    lv_obj_set_style_radius(pill, 14, 0);
    lv_obj_set_style_bg_color(pill, color(kColorElevated), 0);
    lv_obj_set_style_border_width(pill, 0, 0);
    lv_obj_set_style_pad_all(pill, 12, 0);
    set_no_scroll(pill);
    lv_obj_t *label =
        create_label(pill, name, &lv_font_montserrat_16, kColorText);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    return pill;
}

void set_output_pill(lv_obj_t *pill, bool active) {
    lv_obj_set_style_bg_color(
        pill, color(active ? kColorSuccessDark : kColorElevated), 0);
    lv_obj_set_style_border_width(pill, active ? 1 : 0, 0);
    lv_obj_set_style_border_color(pill, color(kColorSuccess), 0);
}

void create_sidebar() {
    lv_obj_t *sidebar = lv_obj_create(ui.root);
    lv_obj_set_pos(sidebar, 0, 0);
    lv_obj_set_size(sidebar, kSidebarWidth, kScreenHeight);
    lv_obj_set_style_radius(sidebar, 0, 0);
    lv_obj_set_style_border_width(sidebar, 0, 0);
    lv_obj_set_style_bg_color(sidebar, color(kColorSurface), 0);
    lv_obj_set_style_pad_all(sidebar, 0, 0);
    set_no_scroll(sidebar);

    lv_obj_t *logo =
        create_label(sidebar, "AquaCYD", &lv_font_montserrat_28, kColorText);
    lv_obj_set_pos(logo, 20, 18);
    lv_obj_t *subtitle = create_label(
        sidebar, "SMART AQUARIUM", &lv_font_montserrat_16, kColorInfo);
    lv_obj_set_pos(subtitle, 20, 52);

    for (size_t index = 0U;
         index < static_cast<size_t>(Page::Count);
         ++index) {
        lv_obj_t *button = lv_button_create(sidebar);
        ui.nav_buttons[index] = button;
        lv_obj_set_pos(button, 12, 96 + static_cast<lv_coord_t>(index) * 58);
        lv_obj_set_size(button, 160, 50);
        lv_obj_set_style_radius(button, 12, 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(
            button, color(kColorAccent), LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(
            button, LV_OPA_COVER, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(
            button, color(kColorElevated), LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_pad_left(button, 14, 0);
        lv_obj_add_event_cb(
            button,
            navigation_event,
            LV_EVENT_CLICKED,
            reinterpret_cast<void *>(index));

        lv_obj_t *label = create_label(
            button,
            kNavigationLabels[index],
            &lv_font_montserrat_16,
            kColorText);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    }

    lv_obj_t *footer = lv_obj_create(sidebar);
    lv_obj_set_pos(footer, 12, 464);
    lv_obj_set_size(footer, 160, 120);
    style_card(footer, kColorCanvas);
    lv_obj_set_style_radius(footer, 14, 0);
    create_label(
        footer, "OSTATNIA SYNCHRONIZACJA",
        &lv_font_montserrat_16, kColorTextSecondary);
    ui.sync_label = create_label(
        footer, "Oczekiwanie…", &lv_font_montserrat_16, kColorWarning);
    lv_obj_align(ui.sync_label, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_t *local = create_label(
        footer, "Automatyka lokalna: CYD",
        &lv_font_montserrat_16, kColorSuccess);
    lv_obj_align(local, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

void create_header() {
    lv_obj_t *header = lv_obj_create(ui.root);
    lv_obj_set_pos(header, kSidebarWidth, 0);
    lv_obj_set_size(header, kContentWidth, kHeaderHeight);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_bg_color(header, color(kColorCanvas), 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    set_no_scroll(header);

    ui.page_title = create_label(
        header, kPageTitles[0], &lv_font_montserrat_24, kColorText);
    lv_obj_set_pos(ui.page_title, 24, 14);
    lv_obj_t *subtitle = create_label(
        header,
        "Bezpieczne sterowanie • dane z CYD przez ESP-NOW",
        &lv_font_montserrat_16,
        kColorTextSecondary);
    lv_obj_set_pos(subtitle, 24, 44);

    ui.connection_chip = create_chip(
        header, 170, "ŁĄCZENIE", kColorWarningDark, kColorWarning);
    lv_obj_align(ui.connection_chip, LV_ALIGN_RIGHT_MID, -178, 0);
    ui.connection_chip_label = lv_obj_get_child(ui.connection_chip, 0);
    lv_obj_add_flag(ui.connection_chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        ui.connection_chip,
        [](lv_event_t *event) {
            if (event != nullptr &&
                lv_event_get_code(event) == LV_EVENT_CLICKED) {
                select_page(Page::System);
            }
        },
        LV_EVENT_CLICKED,
        nullptr);

    ui.alarm_chip = create_chip(
        header, 154, "BEZ ALARMÓW", kColorSuccessDark, kColorSuccess);
    lv_obj_align(ui.alarm_chip, LV_ALIGN_RIGHT_MID, -16, 0);
    ui.alarm_chip_label = lv_obj_get_child(ui.alarm_chip, 0);
    lv_obj_add_flag(ui.alarm_chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        ui.alarm_chip,
        [](lv_event_t *event) {
            if (event != nullptr &&
                lv_event_get_code(event) == LV_EVENT_CLICKED) {
                select_page(Page::Alarms);
            }
        },
        LV_EVENT_CLICKED,
        nullptr);

    ui.stale_banner = lv_obj_create(ui.root);
    lv_obj_set_pos(ui.stale_banner, kSidebarWidth + 16, kHeaderHeight + 8);
    lv_obj_set_size(ui.stale_banner, kContentWidth - 32, 42);
    lv_obj_set_style_radius(ui.stale_banner, 12, 0);
    lv_obj_set_style_bg_color(
        ui.stale_banner, color(kColorWarningDark), 0);
    lv_obj_set_style_border_color(
        ui.stale_banner, color(kColorWarning), 0);
    lv_obj_set_style_border_width(ui.stale_banner, 1, 0);
    lv_obj_set_style_pad_all(ui.stale_banner, 0, 0);
    set_no_scroll(ui.stale_banner);
    ui.stale_label = create_label(
        ui.stale_banner,
        LV_SYMBOL_WARNING " Dane sterownika są nieaktualne",
        &lv_font_montserrat_16,
        kColorWarning);
    lv_obj_center(ui.stale_label);
    lv_obj_add_flag(ui.stale_banner, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *create_page(Page page) {
    lv_obj_t *object = lv_obj_create(ui.root);
    ui.pages[static_cast<size_t>(page)] = object;
    lv_obj_set_pos(object, kSidebarWidth, kHeaderHeight);
    lv_obj_set_size(object, kContentWidth, kContentHeight);
    style_transparent(object);
    if (page != Page::Dashboard) {
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
    return object;
}

void create_dashboard_page() {
    lv_obj_t *page = create_page(Page::Dashboard);

    ui.health_card = lv_obj_create(page);
    lv_obj_set_pos(ui.health_card, 20, 12);
    lv_obj_set_size(ui.health_card, 800, 82);
    style_card(ui.health_card, kColorSuccessDark);
    lv_obj_set_style_border_color(
        ui.health_card, color(kColorSuccess), 0);

    ui.health_indicator = lv_obj_create(ui.health_card);
    lv_obj_set_size(ui.health_indicator, 14, 14);
    lv_obj_set_style_radius(ui.health_indicator, 7, 0);
    lv_obj_set_style_bg_color(
        ui.health_indicator, color(kColorSuccess), 0);
    lv_obj_set_style_border_width(ui.health_indicator, 0, 0);
    lv_obj_align(ui.health_indicator, LV_ALIGN_LEFT_MID, 0, 0);

    ui.health_title = create_label(
        ui.health_card,
        "System bezpieczny",
        &lv_font_montserrat_20,
        kColorText);
    lv_obj_align(ui.health_title, LV_ALIGN_TOP_LEFT, 28, 0);
    ui.health_detail = create_label(
        ui.health_card,
        "CYD wykonuje automatykę lokalnie",
        &lv_font_montserrat_16,
        kColorSuccess);
    lv_obj_align(ui.health_detail, LV_ALIGN_BOTTOM_LEFT, 28, 0);
    lv_obj_t *shield = create_label(
        ui.health_card,
        LV_SYMBOL_OK,
        &lv_font_montserrat_28,
        kColorSuccess);
    lv_obj_align(shield, LV_ALIGN_RIGHT_MID, -4, 0);

    ui.dashboard_temperature = create_metric_card(
        page, 20, 106, 256, 136, "TEMPERATURA", "Pomiar z CYD", false);
    ui.dashboard_ph = create_metric_card(
        page, 292, 106, 256, 136, "pH", "Pomiar z CYD", false);
    ui.dashboard_ec = create_metric_card(
        page, 564, 106, 256, 136, "PRZEWODNOŚĆ", "Pomiar z CYD", false);

    lv_obj_t *outputs_card = lv_obj_create(page);
    lv_obj_set_pos(outputs_card, 20, 256);
    lv_obj_set_size(outputs_card, 522, 248);
    style_card(outputs_card);
    create_label(
        outputs_card, "Urządzenia", &lv_font_montserrat_20, kColorText);
    lv_obj_t *outputs_caption = create_label(
        outputs_card,
        "Bieżący stan wyjść sterownika",
        &lv_font_montserrat_16,
        kColorTextSecondary);
    lv_obj_align(outputs_caption, LV_ALIGN_TOP_RIGHT, 0, 2);

    ui.dashboard_outputs[0] =
        create_output_pill(outputs_card, 0, 42, 236, "Światło główne");
    ui.dashboard_outputs[1] =
        create_output_pill(outputs_card, 252, 42, 236, "Światło roślinne");
    ui.dashboard_outputs[2] =
        create_output_pill(outputs_card, 0, 102, 236, "Filtr");
    ui.dashboard_outputs[3] =
        create_output_pill(outputs_card, 252, 102, 236, "Napowietrzanie");
    ui.dashboard_outputs[4] =
        create_output_pill(outputs_card, 0, 162, 236, "Grzałka");

    lv_obj_t *status_card = lv_obj_create(page);
    lv_obj_set_pos(status_card, 558, 256);
    lv_obj_set_size(status_card, 262, 248);
    style_card(status_card);
    create_label(
        status_card, "Stan instalacji", &lv_font_montserrat_20, kColorText);
    ui.dashboard_safety = create_label(
        status_card,
        "Poziom wody  —\nWyciek        —\nFail-safe     —",
        &lv_font_montserrat_16,
        kColorTextSecondary);
    lv_obj_set_style_text_line_space(ui.dashboard_safety, 15, 0);
    lv_obj_align(ui.dashboard_safety, LV_ALIGN_TOP_LEFT, 0, 48);
    ui.dashboard_link = create_label(
        status_card,
        "ESP-NOW  — dBm",
        &lv_font_montserrat_16,
        kColorInfo);
    lv_obj_align(ui.dashboard_link, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

void create_output_control_card(lv_obj_t *page,
                                lv_coord_t x,
                                lv_coord_t y,
                                const char *title,
                                size_t output_index,
                                size_t on_command,
                                size_t off_command) {
    lv_obj_t *card = lv_obj_create(page);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, 386, 174);
    style_card(card);

    create_label(card, title, &lv_font_montserrat_20, kColorText);
    ui.control_outputs[output_index].indicator =
        lv_obj_create(card);
    lv_obj_set_size(ui.control_outputs[output_index].indicator, 12, 12);
    lv_obj_set_style_radius(
        ui.control_outputs[output_index].indicator, 6, 0);
    lv_obj_set_style_border_width(
        ui.control_outputs[output_index].indicator, 0, 0);
    lv_obj_align(
        ui.control_outputs[output_index].indicator,
        LV_ALIGN_TOP_RIGHT,
        0,
        6);

    ui.control_outputs[output_index].state = create_label(
        card, "Stan: —", &lv_font_montserrat_16, kColorTextSecondary);
    lv_obj_align(
        ui.control_outputs[output_index].state,
        LV_ALIGN_TOP_LEFT,
        0,
        38);

    lv_obj_t *on_button = create_button(
        card, "Włącz", 164, 52, kColorAccent,
        kCommandDefinitions[on_command]);
    lv_obj_align(on_button, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    ui.control_buttons[on_command] = on_button;

    lv_obj_t *off_button = create_button(
        card, "Wyłącz", 164, 52, kColorElevated,
        kCommandDefinitions[off_command]);
    lv_obj_align(off_button, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    ui.control_buttons[off_command] = off_button;
}

void create_controls_page() {
    lv_obj_t *page = create_page(Page::Controls);

    lv_obj_t *notice = lv_obj_create(page);
    lv_obj_set_pos(notice, 20, 12);
    lv_obj_set_size(notice, 800, 64);
    style_card(notice, 0x102B43U);
    lv_obj_set_style_border_color(notice, color(kColorInfo), 0);
    lv_obj_t *notice_text = create_label(
        notice,
        LV_SYMBOL_BULLET "  Każde ręczne polecenie wygasa po 15 minutach",
        &lv_font_montserrat_16,
        kColorInfo);
    lv_obj_align(notice_text, LV_ALIGN_LEFT_MID, 0, 0);

    create_output_control_card(
        page, 20, 88, "Światło główne", 0U, 0U, 1U);
    create_output_control_card(
        page, 434, 88, "Światło roślinne", 1U, 2U, 3U);
    create_output_control_card(
        page, 20, 274, "Filtr", 2U, 4U, 5U);
    create_output_control_card(
        page, 434, 274, "Napowietrzanie", 3U, 6U, 7U);

    lv_obj_t *actions = lv_obj_create(page);
    lv_obj_set_pos(actions, 20, 460);
    lv_obj_set_size(actions, 800, 56);
    style_transparent(actions);

    ui.control_buttons[8] = create_button(
        actions, LV_SYMBOL_PLAY "  Karmienie 10 min",
        244, 52, kColorAccent, kCommandDefinitions[8]);
    lv_obj_align(ui.control_buttons[8], LV_ALIGN_LEFT_MID, 0, 0);
    ui.control_buttons[9] = create_button(
        actions, LV_SYMBOL_REFRESH "  Odśwież stan",
        220, 52, kColorElevated, kCommandDefinitions[9]);
    lv_obj_align(ui.control_buttons[9], LV_ALIGN_LEFT_MID, 260, 0);

    ui.command_status = create_label(
        actions,
        "Sterowanie gotowe",
        &lv_font_montserrat_16,
        kColorSuccess);
    lv_obj_align(ui.command_status, LV_ALIGN_RIGHT_MID, 0, -8);

    ui.pending_bar = lv_bar_create(actions);
    lv_obj_set_size(ui.pending_bar, 268, 6);
    lv_obj_align(ui.pending_bar, LV_ALIGN_RIGHT_MID, 0, 18);
    lv_bar_set_range(ui.pending_bar, 0, 100);
    lv_bar_set_value(ui.pending_bar, 100, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(
        ui.pending_bar, color(kColorElevated), LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        ui.pending_bar, color(kColorSuccess), LV_PART_INDICATOR);
}

void create_sensors_page() {
    lv_obj_t *page = create_page(Page::Sensors);
    ui.sensor_temperature = create_metric_card(
        page, 20, 16, 386, 202,
        "TEMPERATURA WODY", "Zakres prezentacji 18–30 °C", true);
    ui.sensor_ph = create_metric_card(
        page, 434, 16, 386, 202,
        "ODCZYN pH", "Zakres prezentacji 4–10 pH", true);
    ui.sensor_ec = create_metric_card(
        page, 20, 234, 386, 202,
        "PRZEWODNOŚĆ EC", "Zakres prezentacji 0–2000 µS/cm", true);
    ui.sensor_ldr = create_metric_card(
        page, 434, 234, 386, 202,
        "ŚWIATŁO LDR", "Surowa wartość wejścia analogowego", true);

    lv_obj_t *safety = lv_obj_create(page);
    lv_obj_set_pos(safety, 20, 452);
    lv_obj_set_size(safety, 800, 64);
    style_card(safety);
    create_label(
        safety, "CZUJNIKI BEZPIECZEŃSTWA",
        &lv_font_montserrat_16, kColorTextSecondary);
    ui.sensor_water = create_label(
        safety, "Poziom wody: —", &lv_font_montserrat_16, kColorText);
    lv_obj_align(ui.sensor_water, LV_ALIGN_RIGHT_MID, -310, 0);
    ui.sensor_leak = create_label(
        safety, "Wyciek: —", &lv_font_montserrat_16, kColorText);
    lv_obj_align(ui.sensor_leak, LV_ALIGN_RIGHT_MID, 0, 0);
}

void create_alarms_page() {
    lv_obj_t *page = create_page(Page::Alarms);
    lv_obj_t *summary = lv_obj_create(page);
    lv_obj_set_pos(summary, 20, 16);
    lv_obj_set_size(summary, 800, 102);
    style_card(summary, kColorSuccessDark);
    lv_obj_set_style_border_color(summary, color(kColorSuccess), 0);
    ui.alarm_summary = create_label(
        summary, "Brak aktywnych alarmów",
        &lv_font_montserrat_24, kColorSuccess);
    lv_obj_align(ui.alarm_summary, LV_ALIGN_TOP_LEFT, 0, 0);
    ui.alarm_summary_detail = create_label(
        summary,
        "CYD pracuje w normalnym trybie automatycznym.",
        &lv_font_montserrat_16,
        kColorTextSecondary);
    lv_obj_align(ui.alarm_summary_detail, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    for (size_t index = 0U; index < kAlarmRowCount; ++index) {
        lv_obj_t *row = lv_obj_create(page);
        ui.alarm_rows[index] = row;
        lv_obj_set_pos(row, 20, 130 + static_cast<lv_coord_t>(index) * 74);
        lv_obj_set_size(row, 800, 64);
        style_card(row);
        lv_obj_set_style_radius(row, 14, 0);
        ui.alarm_row_titles[index] = create_label(
            row, "—", &lv_font_montserrat_16, kColorText);
        lv_obj_align(
            ui.alarm_row_titles[index], LV_ALIGN_TOP_LEFT, 0, 0);
        ui.alarm_row_actions[index] = create_label(
            row, "", &lv_font_montserrat_16, kColorTextSecondary);
        lv_obj_set_width(ui.alarm_row_actions[index], 740);
        lv_label_set_long_mode(
            ui.alarm_row_actions[index], LV_LABEL_LONG_DOT);
        lv_obj_align(
            ui.alarm_row_actions[index], LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }
}

void create_automation_card(lv_obj_t *parent,
                            lv_coord_t x,
                            lv_coord_t y,
                            AutomationEditor editor) {
    const size_t index = static_cast<size_t>(editor);
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, 386, 130);
    style_card(card);
    create_label(
        card, kAutomationTitles[index], &lv_font_montserrat_20, kColorText);
    ui.automation_summaries[index] = create_label(
        card,
        "Oczekiwanie na konfigurację CYD…",
        &lv_font_montserrat_16,
        kColorTextSecondary);
    lv_obj_set_width(ui.automation_summaries[index], 230);
    lv_label_set_long_mode(
        ui.automation_summaries[index], LV_LABEL_LONG_WRAP);
    lv_obj_align(
        ui.automation_summaries[index], LV_ALIGN_BOTTOM_LEFT, 0, 0);

    ui.automation_buttons[index] = create_button(
        card, "Edytuj", 104, 44, kColorAccent, nullptr);
    lv_obj_align(
        ui.automation_buttons[index], LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(
        ui.automation_buttons[index],
        [](lv_event_t *event) {
            if (event == nullptr ||
                lv_event_get_code(event) != LV_EVENT_CLICKED) {
                return;
            }
            const uintptr_t raw = reinterpret_cast<uintptr_t>(
                lv_event_get_user_data(event));
            if (raw <
                static_cast<uintptr_t>(AutomationEditor::Count)) {
                open_automation_editor(
                    static_cast<AutomationEditor>(raw));
            }
        },
        LV_EVENT_CLICKED,
        reinterpret_cast<void *>(index));
}

void create_automation_page() {
    lv_obj_t *page = create_page(Page::Automation);
    lv_obj_t *intro = lv_obj_create(page);
    lv_obj_set_pos(intro, 20, 16);
    lv_obj_set_size(intro, 800, 74);
    style_card(intro, 0x102B43U);
    lv_obj_set_style_border_color(intro, color(kColorInfo), 0);
    create_label(
        intro, "Automatyka pozostaje w sterowniku CYD",
        &lv_font_montserrat_20, kColorInfo);
    ui.automation_status = create_label(
        intro,
        "Panel i Raspberry Pi mogą zostać wyłączone bez zatrzymania akwarium.",
        &lv_font_montserrat_16,
        kColorTextSecondary);
    lv_obj_align(ui.automation_status, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    create_automation_card(
        page, 20, 104, AutomationEditor::LightPrimary);
    create_automation_card(
        page, 434, 104, AutomationEditor::LightSecondary);
    create_automation_card(
        page, 20, 250, AutomationEditor::Filter);
    create_automation_card(
        page, 434, 250, AutomationEditor::Aerator);
    create_automation_card(
        page, 20, 392, AutomationEditor::Temperature);

    lv_obj_t *revision = lv_obj_create(page);
    lv_obj_set_pos(revision, 434, 392);
    lv_obj_set_size(revision, 386, 130);
    style_card(revision);
    create_label(
        revision, "REWIZJA KONFIGURACJI",
        &lv_font_montserrat_16, kColorTextSecondary);
    ui.automation_revision = create_label(
        revision, "—", &lv_font_montserrat_20, kColorText);
    lv_obj_align(ui.automation_revision, LV_ALIGN_LEFT_MID, 0, 8);
    lv_obj_t *revision_note = create_label(
        revision,
        "Każdy zapis używa kontroli konfliktu i pełnego ACK.",
        &lv_font_montserrat_16,
        kColorSuccess);
    lv_obj_set_width(revision_note, 350);
    lv_label_set_long_mode(revision_note, LV_LABEL_LONG_WRAP);
    lv_obj_align(revision_note, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

lv_obj_t *create_system_card(lv_obj_t *parent,
                             lv_coord_t x,
                             const char *title) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, 16);
    lv_obj_set_size(card, 386, 242);
    style_card(card);
    create_label(card, title, &lv_font_montserrat_20, kColorText);
    return card;
}

void create_system_page(uint8_t initial_brightness) {
    lv_obj_t *page = create_page(Page::System);
    lv_obj_t *cyd_card = create_system_card(page, 20, "Sterownik CYD");
    ui.system_cyd = create_label(
        cyd_card,
        "Status                 —\n"
        "Uptime                 —\n"
        "Wolna pamięć           —\n"
        "Rewizja                —\n"
        "ESP-NOW                —",
        &lv_font_montserrat_16,
        kColorTextSecondary);
    lv_obj_set_style_text_line_space(ui.system_cyd, 14, 0);
    lv_obj_align(ui.system_cyd, LV_ALIGN_TOP_LEFT, 0, 42);

    lv_obj_t *hmi_card = create_system_card(page, 434, "AquaHub ESP32-P4");
    ui.system_hmi = create_label(
        hmi_card,
        "Status                 start\n"
        "Wolna pamięć           —\n"
        "Urządzenia             0 / 0 online\n"
        "Encje                  0 / 0 ster.\n"
        "HTTPS / MQTTS          start\n"
        "Parowanie              ------",
        &lv_font_montserrat_16,
        kColorTextSecondary);
    lv_obj_set_style_text_line_space(ui.system_hmi, 9, 0);
    lv_obj_align(ui.system_hmi, LV_ALIGN_TOP_LEFT, 0, 42);

    lv_obj_t *display_card = lv_obj_create(page);
    lv_obj_set_pos(display_card, 20, 274);
    lv_obj_set_size(display_card, 800, 132);
    style_card(display_card);
    create_label(
        display_card, "Jasność panelu",
        &lv_font_montserrat_20, kColorText);
    ui.brightness_label = create_label(
        display_card, "80%", &lv_font_montserrat_20, kColorText);
    lv_obj_align(ui.brightness_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *slider = lv_slider_create(display_card);
    lv_obj_set_size(slider, 766, 24);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, initial_brightness, LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(
        slider, color(kColorElevated), LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        slider, color(kColorAccent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(
        slider, color(kColorText), LV_PART_KNOB);
    lv_obj_add_event_cb(
        slider,
        [](lv_event_t *event) {
            if (event == nullptr) {
                return;
            }
            lv_obj_t *target =
                static_cast<lv_obj_t *>(lv_event_get_target(event));
            if (target == nullptr) {
                return;
            }
            const int32_t raw = lv_slider_get_value(target);
            if (raw < 10 || raw > 100) {
                return;
            }
            ui.brightness = static_cast<uint8_t>(raw);
            lv_label_set_text_fmt(
                ui.brightness_label, "%" PRId32 "%%", raw);
            const lv_event_code_t code = lv_event_get_code(event);
            if ((code == LV_EVENT_VALUE_CHANGED ||
                 code == LV_EVENT_RELEASED) &&
                ui.callbacks.brightness != nullptr) {
                ui.callbacks.brightness(
                    static_cast<uint8_t>(raw),
                    code == LV_EVENT_RELEASED,
                    ui.callbacks.context);
            }
        },
        LV_EVENT_ALL,
        nullptr);

    lv_obj_t *network = lv_obj_create(page);
    lv_obj_set_pos(network, 20, 422);
    lv_obj_set_size(network, 800, 94);
    style_card(network);
    create_label(
        network, "ŁĄCZNOŚĆ", &lv_font_montserrat_16, kColorTextSecondary);
    ui.system_network = create_label(
        network,
        "Wi-Fi: łączenie     MQTT: oczekiwanie     CYD: offline",
        &lv_font_montserrat_16,
        kColorWarning);
    lv_obj_align(ui.system_network, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    ui.system_fingerprint = create_label(
        network,
        "TLS SHA-256: —",
        &lv_font_montserrat_14,
        kColorTextSecondary);
    lv_obj_set_width(ui.system_fingerprint, 766);
    lv_label_set_long_mode(ui.system_fingerprint, LV_LABEL_LONG_CLIP);
    lv_obj_align(ui.system_fingerprint, LV_ALIGN_TOP_LEFT, 0, 28);
}

void hide_automation_editor() {
    lv_obj_add_flag(ui.editor_overlay, LV_OBJ_FLAG_HIDDEN);
}

void update_editor_values() {
    if (ui.active_editor == AutomationEditor::Temperature) {
        lv_label_set_text_fmt(
            ui.temperature_target_value,
            "%.1f °C",
            static_cast<double>(ui.editor_target_milli_c) / 1000.0);
        lv_label_set_text_fmt(
            ui.temperature_hysteresis_value,
            "%.1f °C",
            static_cast<double>(ui.editor_hysteresis_milli_c) / 1000.0);
        lv_label_set_text(
            ui.temperature_mode_value,
            ui.editor_heater_mode == 0U ? "REGULACJA" : "WYŁĄCZONA");
        return;
    }
    lv_label_set_text(
        ui.schedule_mode_value,
        schedule_mode_name(ui.editor_schedule.mode));
    lv_label_set_text_fmt(
        ui.schedule_start_value,
        "%02u:%02u",
        static_cast<unsigned int>(
            ui.editor_schedule.start_minute / 60U),
        static_cast<unsigned int>(
            ui.editor_schedule.start_minute % 60U));
    lv_label_set_text_fmt(
        ui.schedule_end_value,
        "%02u:%02u",
        static_cast<unsigned int>(
            ui.editor_schedule.end_minute / 60U),
        static_cast<unsigned int>(
            ui.editor_schedule.end_minute % 60U));
    lv_label_set_text(
        ui.schedule_profile_value,
        schedule_profile_name(ui.editor_schedule.profile));
}

void editor_adjust_event(lv_event_t *event) {
    if (event == nullptr ||
        lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    const uintptr_t operation = reinterpret_cast<uintptr_t>(
        lv_event_get_user_data(event));
    switch (operation) {
    case 0U:
        ui.editor_schedule.mode =
            static_cast<uint8_t>((ui.editor_schedule.mode + 1U) % 3U);
        break;
    case 1U:
        ui.editor_schedule.start_minute =
            static_cast<uint16_t>(
                (ui.editor_schedule.start_minute + 1440U - 15U) %
                1440U);
        break;
    case 2U:
        ui.editor_schedule.start_minute =
            static_cast<uint16_t>(
                (ui.editor_schedule.start_minute + 15U) % 1440U);
        break;
    case 3U:
        ui.editor_schedule.end_minute =
            static_cast<uint16_t>(
                (ui.editor_schedule.end_minute + 1440U - 15U) %
                1440U);
        break;
    case 4U:
        ui.editor_schedule.end_minute =
            static_cast<uint16_t>(
                (ui.editor_schedule.end_minute + 15U) % 1440U);
        break;
    case 5U:
        ui.editor_schedule.profile =
            static_cast<uint8_t>((ui.editor_schedule.profile + 1U) % 4U);
        break;
    case 6U:
        if (ui.editor_target_milli_c > 18000) {
            ui.editor_target_milli_c -= 500;
        }
        break;
    case 7U:
        if (ui.editor_target_milli_c < 30000) {
            ui.editor_target_milli_c += 500;
        }
        break;
    case 8U:
        if (ui.editor_hysteresis_milli_c > 100U) {
            ui.editor_hysteresis_milli_c =
                static_cast<uint16_t>(
                    ui.editor_hysteresis_milli_c - 100U);
        }
        break;
    case 9U:
        if (ui.editor_hysteresis_milli_c < 5000U) {
            ui.editor_hysteresis_milli_c =
                static_cast<uint16_t>(
                    ui.editor_hysteresis_milli_c + 100U);
        }
        break;
    case 10U:
        ui.editor_heater_mode =
            ui.editor_heater_mode == 0U ? 1U : 0U;
        break;
    default:
        return;
    }
    update_editor_values();
}

lv_obj_t *create_editor_adjust_button(lv_obj_t *parent,
                                      const char *text,
                                      lv_coord_t x,
                                      lv_coord_t y,
                                      uintptr_t operation) {
    lv_obj_t *button =
        create_button(parent, text, 58, 44, kColorElevated, nullptr);
    lv_obj_set_pos(button, x, y);
    lv_obj_add_event_cb(
        button,
        editor_adjust_event,
        LV_EVENT_CLICKED,
        reinterpret_cast<void *>(operation));
    return button;
}

void editor_save_event(lv_event_t *event) {
    if (event == nullptr ||
        lv_event_get_code(event) != LV_EVENT_CLICKED ||
        ui.callbacks.command == nullptr) {
        return;
    }
    if (!ui.snapshot.configuration_valid ||
        !ui.mqtt_connected ||
        !ui.controller_online ||
        ui.command_pending) {
        hmi_ui_show_toast(
            HmiFeedbackKind::Warning,
            "Zapis zablokowany: konfiguracja CYD nie jest aktualna.");
        return;
    }

    HmiCommandRequest request = {};
    if (ui.active_editor == AutomationEditor::Temperature) {
        request.action = "set_setpoint";
        request.target = "heater";
        if (!aquacyd::link::pack_temperature_command(
                ui.editor_heater_mode,
                ui.editor_target_milli_c,
                ui.editor_hysteresis_milli_c,
                &request.value,
                &request.duration_ms)) {
            hmi_ui_show_toast(
                HmiFeedbackKind::Error,
                "Nie można zakodować ustawień temperatury.");
            return;
        }
    } else {
        const size_t index =
            static_cast<size_t>(ui.active_editor);
        if (index >= 4U ||
            !aquacyd::link::pack_schedule_command(
                ui.editor_schedule.mode,
                ui.editor_schedule.profile,
                ui.editor_schedule.start_minute,
                ui.editor_schedule.end_minute,
                &request.value,
                &request.duration_ms)) {
            hmi_ui_show_toast(
                HmiFeedbackKind::Error,
                "Nie można zakodować ustawień harmonogramu.");
            return;
        }
        request.action = "set_schedule";
        request.target = kScheduleTargets[index];
    }
    hide_automation_editor();
    ui.callbacks.command(request, ui.callbacks.context);
}

void open_automation_editor(AutomationEditor editor) {
    const size_t index = static_cast<size_t>(editor);
    if (index >= static_cast<size_t>(AutomationEditor::Count)) {
        return;
    }
    if (!ui.snapshot.configuration_valid) {
        hmi_ui_show_toast(
            HmiFeedbackKind::Warning,
            "Poczekaj na pełną konfigurację z CYD.");
        return;
    }
    if (!ui.mqtt_connected || !ui.controller_online ||
        ui.command_pending) {
        hmi_ui_show_toast(
            HmiFeedbackKind::Warning,
            "Edycja wymaga aktywnego połączenia z CYD.");
        return;
    }
    ui.active_editor = editor;
    lv_label_set_text(ui.editor_title, kAutomationTitles[index]);
    if (editor == AutomationEditor::Temperature) {
        ui.editor_target_milli_c = static_cast<int32_t>(
            ui.snapshot.target_temperature_c * 1000.0 + 0.5);
        ui.editor_hysteresis_milli_c = static_cast<uint16_t>(
            ui.snapshot.temperature_hysteresis_c * 1000.0 + 0.5);
        ui.editor_heater_mode = ui.snapshot.heater_mode;
        lv_obj_add_flag(
            ui.schedule_controls, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(
            ui.temperature_controls, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(
            ui.editor_help,
            "CYD sprawdzi tryb, zakres temperatury i histerezę, a następnie zapisze całość atomowo.");
    } else {
        const HmiSchedule schedules[] = {
            ui.snapshot.light_primary_schedule,
            ui.snapshot.light_secondary_schedule,
            ui.snapshot.filter_schedule,
            ui.snapshot.aerator_schedule
        };
        ui.editor_schedule = schedules[index];
        lv_obj_remove_flag(
            ui.schedule_controls, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(
            ui.temperature_controls, LV_OBJ_FLAG_HIDDEN);
        if (editor == AutomationEditor::LightPrimary ||
            editor == AutomationEditor::LightSecondary) {
            lv_obj_remove_flag(
                ui.schedule_profile_row, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(
                ui.schedule_profile_row, LV_OBJ_FLAG_HIDDEN);
        }
        lv_label_set_text(
            ui.editor_help,
            "Zmiana zostanie zapisana w CYD z kontrolą rewizji; start i koniec można ustawiać co 15 minut.");
    }
    update_editor_values();
    lv_obj_remove_flag(ui.editor_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui.editor_overlay);
}

void create_automation_editor() {
    ui.editor_overlay = lv_obj_create(ui.root);
    lv_obj_set_pos(ui.editor_overlay, 0, 0);
    lv_obj_set_size(ui.editor_overlay, kScreenWidth, kScreenHeight);
    lv_obj_set_style_radius(ui.editor_overlay, 0, 0);
    lv_obj_set_style_bg_color(ui.editor_overlay, color(0x02060CU), 0);
    lv_obj_set_style_bg_opa(ui.editor_overlay, LV_OPA_80, 0);
    lv_obj_set_style_border_width(ui.editor_overlay, 0, 0);
    lv_obj_set_style_pad_all(ui.editor_overlay, 0, 0);
    set_no_scroll(ui.editor_overlay);

    lv_obj_t *dialog = lv_obj_create(ui.editor_overlay);
    lv_obj_set_size(dialog, 660, 480);
    style_card(dialog, kColorSurface);
    lv_obj_set_style_radius(dialog, 24, 0);
    lv_obj_center(dialog);

    ui.editor_title = create_label(
        dialog, "Harmonogram", &lv_font_montserrat_24, kColorText);
    ui.editor_help = create_label(
        dialog, "", &lv_font_montserrat_16, kColorTextSecondary);
    lv_obj_set_width(ui.editor_help, 620);
    lv_label_set_long_mode(ui.editor_help, LV_LABEL_LONG_WRAP);
    lv_obj_align(ui.editor_help, LV_ALIGN_TOP_LEFT, 0, 44);

    ui.schedule_controls = lv_obj_create(dialog);
    lv_obj_set_pos(ui.schedule_controls, 0, 94);
    lv_obj_set_size(ui.schedule_controls, 620, 290);
    style_transparent(ui.schedule_controls);

    create_label(
        ui.schedule_controls, "TRYB", &lv_font_montserrat_16,
        kColorTextSecondary);
    ui.schedule_mode_value = create_label(
        ui.schedule_controls, "HARMONOGRAM",
        &lv_font_montserrat_20, kColorInfo);
    lv_obj_set_pos(ui.schedule_mode_value, 180, 0);
    lv_obj_t *mode_button = create_button(
        ui.schedule_controls, "Zmień", 104, 44, kColorElevated, nullptr);
    lv_obj_set_pos(mode_button, 500, -10);
    lv_obj_add_event_cb(
        mode_button,
        editor_adjust_event,
        LV_EVENT_CLICKED,
        reinterpret_cast<void *>(0U));

    lv_obj_t *start_title = create_label(
        ui.schedule_controls, "START", &lv_font_montserrat_16,
        kColorTextSecondary);
    lv_obj_set_pos(start_title, 0, 72);
    ui.schedule_start_value = create_label(
        ui.schedule_controls, "00:00", &lv_font_montserrat_24, kColorText);
    lv_obj_set_pos(ui.schedule_start_value, 260, 66);
    create_editor_adjust_button(
        ui.schedule_controls, "−", 430, 58, 1U);
    create_editor_adjust_button(
        ui.schedule_controls, "+", 500, 58, 2U);

    lv_obj_t *end_title = create_label(
        ui.schedule_controls, "KONIEC", &lv_font_montserrat_16,
        kColorTextSecondary);
    lv_obj_set_pos(end_title, 0, 134);
    ui.schedule_end_value = create_label(
        ui.schedule_controls, "00:00", &lv_font_montserrat_24, kColorText);
    lv_obj_set_pos(ui.schedule_end_value, 260, 128);
    create_editor_adjust_button(
        ui.schedule_controls, "−", 430, 120, 3U);
    create_editor_adjust_button(
        ui.schedule_controls, "+", 500, 120, 4U);

    ui.schedule_profile_row = lv_obj_create(ui.schedule_controls);
    lv_obj_set_pos(ui.schedule_profile_row, 0, 196);
    lv_obj_set_size(ui.schedule_profile_row, 620, 52);
    style_transparent(ui.schedule_profile_row);
    create_label(
        ui.schedule_profile_row, "PROFIL ŚWIATŁA",
        &lv_font_montserrat_16, kColorTextSecondary);
    ui.schedule_profile_value = create_label(
        ui.schedule_profile_row, "CYKL AUTO",
        &lv_font_montserrat_20, kColorInfo);
    lv_obj_set_pos(ui.schedule_profile_value, 260, 0);
    lv_obj_t *profile_button = create_button(
        ui.schedule_profile_row, "Zmień", 104, 44,
        kColorElevated, nullptr);
    lv_obj_set_pos(profile_button, 500, -10);
    lv_obj_add_event_cb(
        profile_button,
        editor_adjust_event,
        LV_EVENT_CLICKED,
        reinterpret_cast<void *>(5U));

    ui.temperature_controls = lv_obj_create(dialog);
    lv_obj_set_pos(ui.temperature_controls, 0, 110);
    lv_obj_set_size(ui.temperature_controls, 620, 210);
    style_transparent(ui.temperature_controls);
    lv_obj_t *target_title = create_label(
        ui.temperature_controls, "TEMPERATURA DOCELOWA",
        &lv_font_montserrat_16, kColorTextSecondary);
    lv_obj_set_pos(target_title, 0, 12);
    ui.temperature_target_value = create_label(
        ui.temperature_controls, "25.0 °C",
        &lv_font_montserrat_24, kColorText);
    lv_obj_set_pos(ui.temperature_target_value, 260, 4);
    create_editor_adjust_button(
        ui.temperature_controls, "−", 430, 0, 6U);
    create_editor_adjust_button(
        ui.temperature_controls, "+", 500, 0, 7U);

    lv_obj_t *hysteresis_title = create_label(
        ui.temperature_controls, "HISTEREZA",
        &lv_font_montserrat_16, kColorTextSecondary);
    lv_obj_set_pos(hysteresis_title, 0, 92);
    ui.temperature_hysteresis_value = create_label(
        ui.temperature_controls, "0.5 °C",
        &lv_font_montserrat_24, kColorText);
    lv_obj_set_pos(ui.temperature_hysteresis_value, 260, 84);
    create_editor_adjust_button(
        ui.temperature_controls, "−", 430, 80, 8U);
    create_editor_adjust_button(
        ui.temperature_controls, "+", 500, 80, 9U);

    lv_obj_t *mode_title_temperature = create_label(
        ui.temperature_controls, "TRYB GRZAŁKI",
        &lv_font_montserrat_16, kColorTextSecondary);
    lv_obj_set_pos(mode_title_temperature, 0, 172);
    ui.temperature_mode_value = create_label(
        ui.temperature_controls, "REGULACJA",
        &lv_font_montserrat_20, kColorInfo);
    lv_obj_set_pos(ui.temperature_mode_value, 260, 164);
    lv_obj_t *temperature_mode_button = create_button(
        ui.temperature_controls, "Zmień", 104, 44,
        kColorElevated, nullptr);
    lv_obj_set_pos(temperature_mode_button, 500, 154);
    lv_obj_add_event_cb(
        temperature_mode_button,
        editor_adjust_event,
        LV_EVENT_CLICKED,
        reinterpret_cast<void *>(10U));

    lv_obj_t *cancel = create_button(
        dialog, "Anuluj", 292, 54, kColorElevated, nullptr);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(
        cancel,
        [](lv_event_t *event) {
            if (event != nullptr &&
                lv_event_get_code(event) == LV_EVENT_CLICKED) {
                hide_automation_editor();
            }
        },
        LV_EVENT_CLICKED,
        nullptr);
    lv_obj_t *save = create_button(
        dialog, "Zapisz w CYD", 292, 54, kColorAccent, nullptr);
    lv_obj_align(save, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(
        save, editor_save_event, LV_EVENT_CLICKED, nullptr);

    lv_obj_add_flag(ui.editor_overlay, LV_OBJ_FLAG_HIDDEN);
}

void hide_confirmation() {
    ui.pending_confirmation = nullptr;
    lv_obj_add_flag(ui.confirm_overlay, LV_OBJ_FLAG_HIDDEN);
}

void create_confirmation_dialog() {
    ui.confirm_overlay = lv_obj_create(ui.root);
    lv_obj_set_pos(ui.confirm_overlay, 0, 0);
    lv_obj_set_size(ui.confirm_overlay, kScreenWidth, kScreenHeight);
    lv_obj_set_style_radius(ui.confirm_overlay, 0, 0);
    lv_obj_set_style_bg_color(ui.confirm_overlay, color(0x02060CU), 0);
    lv_obj_set_style_bg_opa(ui.confirm_overlay, LV_OPA_80, 0);
    lv_obj_set_style_border_width(ui.confirm_overlay, 0, 0);
    lv_obj_set_style_pad_all(ui.confirm_overlay, 0, 0);
    set_no_scroll(ui.confirm_overlay);

    lv_obj_t *dialog = lv_obj_create(ui.confirm_overlay);
    lv_obj_set_size(dialog, 560, 286);
    style_card(dialog, kColorSurface);
    lv_obj_set_style_radius(dialog, 24, 0);
    lv_obj_center(dialog);

    lv_obj_t *mark = lv_obj_create(dialog);
    lv_obj_set_size(mark, 54, 54);
    lv_obj_set_style_radius(mark, 27, 0);
    lv_obj_set_style_bg_color(mark, color(kColorWarningDark), 0);
    lv_obj_set_style_border_color(mark, color(kColorWarning), 0);
    lv_obj_set_style_border_width(mark, 1, 0);
    lv_obj_align(mark, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *mark_label = create_label(
        mark, LV_SYMBOL_WARNING, &lv_font_montserrat_24, kColorWarning);
    lv_obj_center(mark_label);

    ui.confirm_title = create_label(
        dialog, "Potwierdź polecenie",
        &lv_font_montserrat_24, kColorText);
    lv_obj_align(ui.confirm_title, LV_ALIGN_TOP_LEFT, 72, 2);
    ui.confirm_body = create_label(
        dialog, "", &lv_font_montserrat_16, kColorTextSecondary);
    lv_obj_set_width(ui.confirm_body, 520);
    lv_label_set_long_mode(ui.confirm_body, LV_LABEL_LONG_WRAP);
    lv_obj_align(ui.confirm_body, LV_ALIGN_TOP_LEFT, 0, 76);

    lv_obj_t *cancel = create_button(
        dialog, "Anuluj", 238, 54, kColorElevated, nullptr);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(
        cancel,
        [](lv_event_t *event) {
            if (event != nullptr &&
                lv_event_get_code(event) == LV_EVENT_CLICKED) {
                hide_confirmation();
            }
        },
        LV_EVENT_CLICKED,
        nullptr);

    lv_obj_t *confirm = create_button(
        dialog, "Potwierdź", 238, 54, kColorAccent, nullptr);
    lv_obj_align(confirm, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(
        confirm,
        [](lv_event_t *event) {
            if (event == nullptr ||
                lv_event_get_code(event) != LV_EVENT_CLICKED ||
                ui.pending_confirmation == nullptr) {
                return;
            }
            const CommandDefinition *definition =
                ui.pending_confirmation;
            hide_confirmation();
            if (ui.callbacks.command != nullptr) {
                ui.callbacks.command(
                    definition->request, ui.callbacks.context);
            }
        },
        LV_EVENT_CLICKED,
        nullptr);
    lv_obj_add_flag(ui.confirm_overlay, LV_OBJ_FLAG_HIDDEN);
}

void create_toast() {
    ui.toast = lv_obj_create(ui.root);
    lv_obj_set_pos(ui.toast, 392, 616);
    lv_obj_set_size(ui.toast, 600, 64);
    style_card(ui.toast, kColorElevated);
    lv_obj_set_style_radius(ui.toast, 18, 0);
    lv_obj_set_style_shadow_width(ui.toast, 24, 0);
    lv_obj_set_style_shadow_color(ui.toast, color(0x000000U), 0);
    lv_obj_set_style_shadow_opa(ui.toast, LV_OPA_50, 0);

    ui.toast_indicator = lv_obj_create(ui.toast);
    lv_obj_set_size(ui.toast_indicator, 10, 32);
    lv_obj_set_style_radius(ui.toast_indicator, 5, 0);
    lv_obj_set_style_border_width(ui.toast_indicator, 0, 0);
    lv_obj_align(ui.toast_indicator, LV_ALIGN_LEFT_MID, 0, 0);

    ui.toast_label = create_label(
        ui.toast, "", &lv_font_montserrat_16, kColorText);
    lv_obj_set_width(ui.toast_label, 540);
    lv_label_set_long_mode(ui.toast_label, LV_LABEL_LONG_DOT);
    lv_obj_align(ui.toast_label, LV_ALIGN_LEFT_MID, 24, 0);
    lv_obj_add_flag(ui.toast, LV_OBJ_FLAG_HIDDEN);
}

void create_boot_overlay() {
    ui.boot_overlay = lv_obj_create(ui.root);
    lv_obj_set_pos(ui.boot_overlay, 0, 0);
    lv_obj_set_size(ui.boot_overlay, kScreenWidth, kScreenHeight);
    lv_obj_set_style_radius(ui.boot_overlay, 0, 0);
    lv_obj_set_style_border_width(ui.boot_overlay, 0, 0);
    lv_obj_set_style_bg_color(ui.boot_overlay, color(kColorCanvas), 0);
    lv_obj_set_style_pad_all(ui.boot_overlay, 0, 0);
    set_no_scroll(ui.boot_overlay);

    ui.boot_mark = lv_obj_create(ui.boot_overlay);
    lv_obj_set_size(ui.boot_mark, 104, 104);
    lv_obj_set_style_radius(ui.boot_mark, 34, 0);
    lv_obj_set_style_bg_color(ui.boot_mark, color(kColorAccent), 0);
    lv_obj_set_style_border_width(ui.boot_mark, 0, 0);
    lv_obj_align(ui.boot_mark, LV_ALIGN_CENTER, 0, -88);
    lv_obj_t *letter = create_label(
        ui.boot_mark, "A", &lv_font_montserrat_36, kColorText);
    lv_obj_center(letter);

    lv_obj_t *title = create_label(
        ui.boot_overlay, "AquaCYD", &lv_font_montserrat_36, kColorText);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 4);
    ui.boot_status = create_label(
        ui.boot_overlay,
        "Uruchamianie panelu ESP32-P4…",
        &lv_font_montserrat_20,
        kColorTextSecondary);
    lv_obj_align(ui.boot_status, LV_ALIGN_CENTER, 0, 50);

    ui.boot_progress = lv_bar_create(ui.boot_overlay);
    lv_obj_set_size(ui.boot_progress, 420, 8);
    lv_obj_align(ui.boot_progress, LV_ALIGN_CENTER, 0, 98);
    lv_bar_set_range(ui.boot_progress, 0, 100);
    lv_bar_set_value(ui.boot_progress, 12, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(
        ui.boot_progress, color(kColorElevated), LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        ui.boot_progress, color(kColorAccent), LV_PART_INDICATOR);
    ui.boot_visible = true;
    ui.boot_started_ms = lv_tick_get();

    lv_anim_t pulse;
    lv_anim_init(&pulse);
    lv_anim_set_var(&pulse, ui.boot_mark);
    lv_anim_set_values(&pulse, LV_OPA_COVER, LV_OPA_60);
    lv_anim_set_duration(&pulse, 900U);
    lv_anim_set_playback_duration(&pulse, 900U);
    lv_anim_set_repeat_count(&pulse, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&pulse, opacity_animation);
    lv_anim_start(&pulse);
}

void update_boot_progress() {
    if (!ui.boot_visible) {
        return;
    }
    int32_t progress = 18;
    const char *message = "Uruchamianie interfejsu i pamięci…";
    if (ui.wifi_connected) {
        progress = 48;
        message = "Wi-Fi połączone, uruchamianie MQTT…";
    }
    if (ui.mqtt_connected) {
        progress = 76;
        message = "MQTT online, oczekiwanie na telemetrię CYD…";
    }
    if (ui.controller_online) {
        progress = 100;
        message = "CYD online — system gotowy";
    }
    lv_bar_set_value(ui.boot_progress, progress, LV_ANIM_ON);
    lv_label_set_text(ui.boot_status, message);
}

void hide_boot_overlay() {
    if (!ui.boot_visible) {
        return;
    }
    ui.boot_visible = false;
    lv_anim_delete(ui.boot_mark, opacity_animation);
    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, ui.boot_overlay);
    lv_anim_set_values(&fade, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&fade, kAnimationSlowMs);
    lv_anim_set_exec_cb(&fade, opacity_animation);
    lv_anim_set_completed_cb(
        &fade,
        [](lv_anim_t *animation) {
            lv_obj_t *object =
                static_cast<lv_obj_t *>(lv_anim_get_user_data(animation));
            if (object != nullptr) {
                lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
            }
        });
    lv_anim_set_user_data(&fade, ui.boot_overlay);
    lv_anim_start(&fade);
}

void set_output_state(OutputWidgets &widgets, bool active) {
    lv_label_set_text(widgets.state, active ? "Stan: WŁĄCZONE" : "Stan: wyłączone");
    lv_obj_set_style_text_color(
        widgets.state, color(active ? kColorSuccess : kColorTextSecondary), 0);
    lv_obj_set_style_bg_color(
        widgets.indicator, color(active ? kColorSuccess : kColorTextDisabled), 0);
}

void update_automation_summary(size_t index,
                               const HmiSchedule &schedule,
                               bool light) {
    if (index >= 4U || ui.automation_summaries[index] == nullptr) {
        return;
    }
    char summary[112] = {};
    if (schedule.mode == 0U) {
        if (light) {
            snprintf(
                summary,
                sizeof(summary),
                "%02u:%02u–%02u:%02u\n%s",
                static_cast<unsigned int>(schedule.start_minute / 60U),
                static_cast<unsigned int>(schedule.start_minute % 60U),
                static_cast<unsigned int>(schedule.end_minute / 60U),
                static_cast<unsigned int>(schedule.end_minute % 60U),
                schedule_profile_name(schedule.profile));
        } else {
            snprintf(
                summary,
                sizeof(summary),
                "%02u:%02u–%02u:%02u\nHarmonogram CYD",
                static_cast<unsigned int>(schedule.start_minute / 60U),
                static_cast<unsigned int>(schedule.start_minute % 60U),
                static_cast<unsigned int>(schedule.end_minute / 60U),
                static_cast<unsigned int>(schedule.end_minute % 60U));
        }
    } else {
        snprintf(
            summary,
            sizeof(summary),
            "%s\nCzasy pozostają zapisane",
            schedule_mode_name(schedule.mode));
    }
    lv_label_set_text(ui.automation_summaries[index], summary);
}

void update_alarm_rows(uint32_t flags) {
    size_t row_index = 0U;
    size_t active_count = 0U;
    bool critical = false;
    for (const AlarmDescriptor &descriptor : kAlarmDescriptors) {
        if ((flags & descriptor.flag) == 0U) {
            continue;
        }
        ++active_count;
        critical = critical || descriptor.critical;
        if (row_index >= kAlarmRowCount) {
            continue;
        }
        lv_obj_t *row = ui.alarm_rows[row_index];
        lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui.alarm_row_titles[row_index], descriptor.title);
        lv_label_set_text(ui.alarm_row_actions[row_index], descriptor.action);
        lv_obj_set_style_border_color(
            row,
            color(descriptor.critical ? kColorDanger : kColorWarning),
            0);
        lv_obj_set_style_bg_color(
            row,
            color(descriptor.critical ? kColorDangerDark : kColorWarningDark),
            0);
        lv_obj_set_style_text_color(
            ui.alarm_row_titles[row_index],
            color(descriptor.critical ? kColorDanger : kColorWarning),
            0);
        ++row_index;
    }
    while (row_index < kAlarmRowCount) {
        lv_obj_add_flag(ui.alarm_rows[row_index], LV_OBJ_FLAG_HIDDEN);
        ++row_index;
    }

    if (active_count == 0U) {
        lv_label_set_text(ui.alarm_summary, "Brak aktywnych alarmów");
        lv_label_set_text(
            ui.alarm_summary_detail,
            "CYD pracuje w normalnym trybie automatycznym.");
        lv_obj_set_style_text_color(
            ui.alarm_summary, color(kColorSuccess), 0);
        lv_obj_set_style_bg_color(
            lv_obj_get_parent(ui.alarm_summary),
            color(kColorSuccessDark),
            0);
        lv_obj_set_style_border_color(
            lv_obj_get_parent(ui.alarm_summary),
            color(kColorSuccess),
            0);
    } else {
        lv_label_set_text_fmt(
            ui.alarm_summary,
            "%u aktywn%s alarm%s",
            static_cast<unsigned int>(active_count),
            active_count == 1U ? "y" : "ych",
            active_count == 1U ? "" : "ów");
        lv_label_set_text(
            ui.alarm_summary_detail,
            critical
                ? "CYD uruchomił lokalną procedurę bezpieczeństwa."
                : "Sprawdź zalecenia i usuń przyczynę ostrzeżenia.");
        lv_obj_set_style_text_color(
            ui.alarm_summary,
            color(critical ? kColorDanger : kColorWarning),
            0);
        lv_obj_set_style_bg_color(
            lv_obj_get_parent(ui.alarm_summary),
            color(critical ? kColorDangerDark : kColorWarningDark),
            0);
        lv_obj_set_style_border_color(
            lv_obj_get_parent(ui.alarm_summary),
            color(critical ? kColorDanger : kColorWarning),
            0);
    }
}

void update_alarm_pulse(bool alarms_active) {
    if (alarms_active && !ui.alarm_pulsing) {
        ui.alarm_pulsing = true;
        lv_anim_t pulse;
        lv_anim_init(&pulse);
        lv_anim_set_var(&pulse, ui.alarm_chip);
        lv_anim_set_values(&pulse, LV_OPA_COVER, LV_OPA_60);
        lv_anim_set_duration(&pulse, 650U);
        lv_anim_set_playback_duration(&pulse, 650U);
        lv_anim_set_repeat_count(&pulse, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_exec_cb(&pulse, opacity_animation);
        lv_anim_start(&pulse);
    } else if (!alarms_active && ui.alarm_pulsing) {
        ui.alarm_pulsing = false;
        lv_anim_delete(ui.alarm_chip, opacity_animation);
        lv_obj_set_style_opa(ui.alarm_chip, LV_OPA_COVER, 0);
    }
}

void set_command_buttons_enabled(bool enabled) {
    for (lv_obj_t *button : ui.control_buttons) {
        if (button == nullptr) {
            continue;
        }
        if (enabled) {
            lv_obj_remove_state(button, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(button, LV_STATE_DISABLED);
        }
    }
    const bool configuration_enabled =
        enabled && ui.snapshot.configuration_valid;
    for (lv_obj_t *button : ui.automation_buttons) {
        if (button == nullptr) {
            continue;
        }
        if (configuration_enabled) {
            lv_obj_remove_state(button, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(button, LV_STATE_DISABLED);
        }
    }
    if (!configuration_enabled &&
        ui.editor_overlay != nullptr &&
        !lv_obj_has_flag(
            ui.editor_overlay, LV_OBJ_FLAG_HIDDEN)) {
        hide_automation_editor();
    }
}

void start_pending_animation() {
    lv_anim_delete(ui.pending_bar, nullptr);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, ui.pending_bar);
    lv_anim_set_values(&animation, 8, 100);
    lv_anim_set_duration(&animation, 950U);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(
        &animation,
        [](void *object, int32_t value) {
            lv_bar_set_value(
                static_cast<lv_obj_t *>(object), value, LV_ANIM_OFF);
        });
    lv_anim_start(&animation);
}

void stop_pending_animation(HmiFeedbackKind kind) {
    lv_anim_delete(ui.pending_bar, nullptr);
    lv_bar_set_value(ui.pending_bar, 100, LV_ANIM_ON);
    uint32_t status_color = kColorInfo;
    if (kind == HmiFeedbackKind::Success) {
        status_color = kColorSuccess;
    } else if (kind == HmiFeedbackKind::Warning) {
        status_color = kColorWarning;
    } else if (kind == HmiFeedbackKind::Error) {
        status_color = kColorDanger;
    }
    lv_obj_set_style_bg_color(
        ui.pending_bar, color(status_color), LV_PART_INDICATOR);
}

uint32_t feedback_color(HmiFeedbackKind kind) {
    switch (kind) {
    case HmiFeedbackKind::Success:
        return kColorSuccess;
    case HmiFeedbackKind::Warning:
        return kColorWarning;
    case HmiFeedbackKind::Error:
        return kColorDanger;
    case HmiFeedbackKind::Information:
    default:
        return kColorInfo;
    }
}

void update_age(uint32_t now_ms) {
    if (ui.snapshot_received_ms == 0U) {
        lv_label_set_text(ui.sync_label, "Brak danych");
        lv_obj_set_style_text_color(
            ui.sync_label, color(kColorWarning), 0);
        return;
    }
    const uint32_t age_ms = now_ms - ui.snapshot_received_ms;
    const uint32_t age_seconds = age_ms / 1000U;
    if (age_seconds < 2U) {
        lv_label_set_text(ui.sync_label, "Teraz");
    } else if (age_seconds < 60U) {
        lv_label_set_text_fmt(
            ui.sync_label, "%" PRIu32 " s temu", age_seconds);
    } else {
        lv_label_set_text_fmt(
            ui.sync_label, "%" PRIu32 " min temu", age_seconds / 60U);
    }
    const bool stale = age_ms > kSnapshotStaleMs || !ui.controller_online;
    lv_obj_set_style_text_color(
        ui.sync_label, color(stale ? kColorWarning : kColorSuccess), 0);
    if (stale) {
        lv_obj_remove_flag(ui.stale_banner, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(
            ui.stale_label,
            ui.controller_online
                ? LV_SYMBOL_WARNING " Dane sterownika są nieaktualne"
                : LV_SYMBOL_WARNING " CYD offline — wyświetlany jest ostatni stan");
    } else {
        lv_obj_add_flag(ui.stale_banner, LV_OBJ_FLAG_HIDDEN);
    }
}

} // namespace

bool hmi_ui_create(const HmiUiCallbacks &callbacks,
                   uint8_t initial_brightness) {
    if (ui.created || callbacks.command == nullptr ||
        callbacks.brightness == nullptr ||
        initial_brightness < 10U || initial_brightness > 100U) {
        return false;
    }
    ui.callbacks = callbacks;
    ui.brightness = initial_brightness;
    ui.active_page = Page::Dashboard;
    ui.root = lv_screen_active();
    lv_obj_set_style_bg_color(ui.root, color(kColorCanvas), 0);
    lv_obj_set_style_text_color(ui.root, color(kColorText), 0);
    lv_obj_set_style_pad_all(ui.root, 0, 0);
    set_no_scroll(ui.root);

    create_sidebar();
    create_header();
    create_dashboard_page();
    create_controls_page();
    create_sensors_page();
    create_alarms_page();
    create_automation_page();
    create_system_page(initial_brightness);
    lv_obj_move_foreground(ui.stale_banner);
    create_automation_editor();
    create_toast();
    create_confirmation_dialog();
    create_boot_overlay();

    ui.created = true;
    select_page(Page::Dashboard);
    hmi_ui_set_connectivity(false, false, false);
    return true;
}

void hmi_ui_apply_snapshot(const HmiSnapshot &snapshot,
                           uint32_t received_at_ms) {
    if (!ui.created) {
        return;
    }
    ui.snapshot = snapshot;
    ui.snapshot_received_ms = received_at_ms;
    ui.controller_online = snapshot.controller_online;

    char value[48] = {};
    if (snapshot.temperature_valid) {
        snprintf(value, sizeof(value), "%.2f °C", snapshot.temperature_c);
    }
    set_metric(
        ui.dashboard_temperature,
        value,
        snapshot.temperature_valid ? "Pomiar prawidłowy" : "Czujnik niedostępny",
        snapshot.temperature_valid,
        0,
        0,
        1);
    set_metric(
        ui.sensor_temperature,
        value,
        snapshot.temperature_valid
            ? "Aktualny pomiar z czujnika temperatury"
            : "Brak ważnego pomiaru temperatury",
        snapshot.temperature_valid,
        static_cast<int32_t>(snapshot.temperature_c * 100.0),
        1800,
        3000);

    memset(value, 0, sizeof(value));
    if (snapshot.ph_valid) {
        snprintf(value, sizeof(value), "%.3f", snapshot.ph);
    }
    set_metric(
        ui.dashboard_ph,
        value,
        snapshot.ph_valid ? "Pomiar prawidłowy" : "Sonda niedostępna",
        snapshot.ph_valid,
        0,
        0,
        1);
    set_metric(
        ui.sensor_ph,
        value,
        snapshot.ph_valid
            ? "Aktualny pomiar sondy pH"
            : "Brak ważnego pomiaru pH",
        snapshot.ph_valid,
        static_cast<int32_t>(snapshot.ph * 1000.0),
        4000,
        10000);

    memset(value, 0, sizeof(value));
    if (snapshot.ec_valid) {
        snprintf(value, sizeof(value), "%.0f µS/cm", snapshot.ec_us_cm);
    }
    set_metric(
        ui.dashboard_ec,
        value,
        snapshot.ec_valid ? "Pomiar prawidłowy" : "Sonda niedostępna",
        snapshot.ec_valid,
        0,
        0,
        1);
    set_metric(
        ui.sensor_ec,
        value,
        snapshot.ec_valid
            ? "Aktualny pomiar przewodności"
            : "Brak ważnego pomiaru EC",
        snapshot.ec_valid,
        static_cast<int32_t>(snapshot.ec_us_cm),
        0,
        2000);

    snprintf(value, sizeof(value), "%d", snapshot.ldr_raw);
    set_metric(
        ui.sensor_ldr,
        value,
        "Surowy poziom światła otoczenia",
        true,
        snapshot.ldr_raw,
        0,
        65535);

    const bool healthy =
        snapshot.controller_safe && snapshot.alarm_flags == 0U;
    lv_label_set_text(
        ui.health_title,
        healthy ? "System bezpieczny" : "CYD zgłasza alarm");
    lv_label_set_text(
        ui.health_detail,
        healthy
            ? "Automatyka i zabezpieczenia działają lokalnie"
            : "Otwórz ekran Alarmy i usuń przyczynę");
    lv_obj_set_style_bg_color(
        ui.health_card,
        color(healthy ? kColorSuccessDark : kColorDangerDark),
        0);
    lv_obj_set_style_border_color(
        ui.health_card,
        color(healthy ? kColorSuccess : kColorDanger),
        0);
    lv_obj_set_style_bg_color(
        ui.health_indicator,
        color(healthy ? kColorSuccess : kColorDanger),
        0);
    lv_obj_set_style_text_color(
        ui.health_detail,
        color(healthy ? kColorSuccess : kColorDanger),
        0);

    const bool output_states[kOutputCount] = {
        snapshot.light_primary_on,
        snapshot.light_secondary_on,
        snapshot.filter_on,
        snapshot.aerator_on,
        snapshot.heater_on
    };
    for (size_t index = 0U; index < kOutputCount; ++index) {
        set_output_pill(ui.dashboard_outputs[index], output_states[index]);
    }
    for (size_t index = 0U; index < 4U; ++index) {
        set_output_state(ui.control_outputs[index], output_states[index]);
    }

    lv_label_set_text_fmt(
        ui.dashboard_safety,
        "Poziom wody  %s\nWyciek        %s\nFail-safe     %s",
        snapshot.water_level_low ? "NISKI" : "OK",
        snapshot.leak_detected ? "WYKRYTY" : "BRAK",
        snapshot.controller_safe ? "AKTYWNY" : "ALARM");
    lv_obj_set_style_text_color(
        ui.dashboard_safety,
        color(
            snapshot.water_level_low || snapshot.leak_detected
                ? kColorDanger
                : kColorTextSecondary),
        0);
    lv_label_set_text_fmt(
        ui.dashboard_link,
        "ESP-NOW  %d dBm",
        snapshot.espnow_rssi_dbm);
    lv_label_set_text(
        ui.sensor_water,
        snapshot.water_level_low
            ? "Poziom wody: NISKI"
            : "Poziom wody: OK");
    lv_obj_set_style_text_color(
        ui.sensor_water,
        color(snapshot.water_level_low ? kColorDanger : kColorSuccess),
        0);
    lv_label_set_text(
        ui.sensor_leak,
        snapshot.leak_detected ? "Wyciek: WYKRYTY" : "Wyciek: BRAK");
    lv_obj_set_style_text_color(
        ui.sensor_leak,
        color(snapshot.leak_detected ? kColorDanger : kColorSuccess),
        0);

    update_alarm_rows(snapshot.alarm_flags);
    const bool alarms_active = snapshot.alarm_flags != 0U;
    if (alarms_active) {
        const bool critical =
            (snapshot.alarm_flags & kCriticalAlarmMask) != 0U;
        set_chip_state(
            ui.alarm_chip,
            ui.alarm_chip_label,
            critical ? "ALARM KRYTYCZNY" : "OSTRZEŻENIE",
            critical ? kColorDangerDark : kColorWarningDark,
            critical ? kColorDanger : kColorWarning);
    } else {
        set_chip_state(
            ui.alarm_chip,
            ui.alarm_chip_label,
            "BEZ ALARMÓW",
            kColorSuccessDark,
            kColorSuccess);
    }
    update_alarm_pulse(alarms_active);

    lv_label_set_text_fmt(
        ui.automation_revision,
        "%08" PRIX32,
        snapshot.configuration_revision);
    if (snapshot.configuration_valid) {
        update_automation_summary(
            0U, snapshot.light_primary_schedule, true);
        update_automation_summary(
            1U, snapshot.light_secondary_schedule, true);
        update_automation_summary(
            2U, snapshot.filter_schedule, false);
        update_automation_summary(
            3U, snapshot.aerator_schedule, false);
        lv_label_set_text_fmt(
            ui.automation_summaries[
                static_cast<size_t>(AutomationEditor::Temperature)],
            "%s • cel %.1f °C\nHistereza %.1f °C",
            snapshot.heater_mode == 0U ? "REGULACJA" : "WYŁĄCZONA",
            snapshot.target_temperature_c,
            snapshot.temperature_hysteresis_c);
        lv_label_set_text(
            ui.automation_status,
            "Edycja zapisuje ustawienia atomowo w CYD; panel może zostać wyłączony.");
        lv_obj_set_style_text_color(
            ui.automation_status, color(kColorSuccess), 0);
    } else {
        for (lv_obj_t *summary : ui.automation_summaries) {
            if (summary != nullptr) {
                lv_label_set_text(
                    summary, "Oczekiwanie na konfigurację CYD…");
            }
        }
        lv_label_set_text(
            ui.automation_status,
            "Bramka wysyła telemetrię starszego schematu — edycja jest zablokowana.");
        lv_obj_set_style_text_color(
            ui.automation_status, color(kColorWarning), 0);
    }
    lv_label_set_text_fmt(
        ui.system_cyd,
        "Status                 %s\n"
        "Uptime                 %" PRIu32 " s\n"
        "Wolna pamięć           %" PRIu32 " B\n"
        "Rewizja                %08" PRIX32 "\n"
        "ESP-NOW                %d dBm",
        snapshot.controller_online ? "online" : "offline",
        snapshot.controller_uptime_seconds,
        snapshot.controller_free_heap_bytes,
        snapshot.configuration_revision,
        snapshot.espnow_rssi_dbm);

    hmi_ui_set_connectivity(
        ui.wifi_connected, ui.mqtt_connected, snapshot.controller_online);
    update_age(received_at_ms);
}

void hmi_ui_apply_hub_summary(const HmiHubSummary &summary) {
    if (!ui.created) {
        return;
    }
    ui.hub_summary = summary;
    const bool service_healthy =
        summary.api_running &&
        (summary.broker_port == 0U || summary.broker_running);
    char pairing[40] = {};
    if (summary.pairing_code >= 100000U &&
        summary.pairing_code <= 999999U &&
        summary.pairing_seconds_remaining > 0U) {
        snprintf(pairing,
                 sizeof(pairing),
                 "%06" PRIu32 " (%" PRIu32 " s)",
                 summary.pairing_code,
                 summary.pairing_seconds_remaining);
    } else {
        strlcpy(pairing, "wygasło", sizeof(pairing));
    }
    lv_label_set_text_fmt(
        ui.system_hmi,
        "Status                 %s\n"
        "Wolna pamięć           %" PRIu32 " B\n"
        "Urządzenia             %u / %u online\n"
        "Encje                  %u / %u ster.\n"
        "HTTPS / MQTTS          %u / %u\n"
        "Parowanie              %s",
        service_healthy ? "online" : "ograniczony",
        summary.free_heap_bytes,
        static_cast<unsigned int>(summary.online_device_count),
        static_cast<unsigned int>(summary.device_count),
        static_cast<unsigned int>(summary.entity_count),
        static_cast<unsigned int>(summary.writable_entity_count),
        static_cast<unsigned int>(summary.api_port),
        static_cast<unsigned int>(summary.broker_port),
        pairing);
    lv_obj_set_style_text_color(
        ui.system_hmi,
        color(service_healthy ? kColorTextSecondary : kColorWarning),
        0);
    lv_label_set_text_fmt(
        ui.system_fingerprint,
        "TLS SHA-256: %s",
        summary.tls_fingerprint[0] != '\0'
            ? summary.tls_fingerprint
            : "—");
}

void hmi_ui_set_connectivity(bool wifi_connected,
                             bool mqtt_connected,
                             bool controller_online_value) {
    if (!ui.created) {
        return;
    }
    ui.wifi_connected = wifi_connected;
    ui.mqtt_connected = mqtt_connected;
    ui.controller_online = controller_online_value;

    if (wifi_connected && mqtt_connected && controller_online_value) {
        set_chip_state(
            ui.connection_chip,
            ui.connection_chip_label,
            "WSZYSTKO ONLINE",
            kColorSuccessDark,
            kColorSuccess);
    } else if (!wifi_connected) {
        set_chip_state(
            ui.connection_chip,
            ui.connection_chip_label,
            "WI-FI OFFLINE",
            kColorDangerDark,
            kColorDanger);
    } else if (!mqtt_connected) {
        set_chip_state(
            ui.connection_chip,
            ui.connection_chip_label,
            "MQTT OFFLINE",
            kColorWarningDark,
            kColorWarning);
    } else {
        set_chip_state(
            ui.connection_chip,
            ui.connection_chip_label,
            "CYD OFFLINE",
            kColorWarningDark,
            kColorWarning);
    }

    lv_label_set_text_fmt(
        ui.system_network,
        "Wi-Fi: %s     MQTT: %s     CYD: %s",
        wifi_connected ? "online" : "offline",
        mqtt_connected ? "online" : "offline",
        controller_online_value ? "online" : "offline");
    lv_obj_set_style_text_color(
        ui.system_network,
        color(
            wifi_connected && mqtt_connected && controller_online_value
                ? kColorSuccess
                : kColorWarning),
        0);
    set_command_buttons_enabled(
        mqtt_connected && controller_online_value && !ui.command_pending);
    update_boot_progress();
}

void hmi_ui_set_command_pending(const char *message) {
    if (!ui.created) {
        return;
    }
    ui.command_pending = true;
    lv_label_set_text(
        ui.command_status,
        message != nullptr ? message : "Oczekiwanie na ACK CYD…");
    lv_obj_set_style_text_color(
        ui.command_status, color(kColorInfo), 0);
    lv_obj_set_style_bg_color(
        ui.pending_bar, color(kColorInfo), LV_PART_INDICATOR);
    set_command_buttons_enabled(false);
    start_pending_animation();
    hmi_ui_show_toast(
        HmiFeedbackKind::Information,
        message != nullptr ? message : "Polecenie wysłane do sterownika.");
}

void hmi_ui_set_command_result(HmiFeedbackKind kind, const char *message) {
    if (!ui.created) {
        return;
    }
    ui.command_pending = false;
    lv_label_set_text(
        ui.command_status,
        message != nullptr ? message : "Sterowanie gotowe");
    lv_obj_set_style_text_color(
        ui.command_status, color(feedback_color(kind)), 0);
    stop_pending_animation(kind);
    set_command_buttons_enabled(
        ui.mqtt_connected && ui.controller_online);
    hmi_ui_show_toast(kind, message);
}

void hmi_ui_show_toast(HmiFeedbackKind kind, const char *message) {
    if (!ui.created || message == nullptr || message[0] == '\0') {
        return;
    }
    lv_anim_delete(ui.toast, y_animation);
    lv_label_set_text(ui.toast_label, message);
    const uint32_t indicator = feedback_color(kind);
    lv_obj_set_style_bg_color(
        ui.toast_indicator, color(indicator), 0);
    lv_obj_set_style_border_color(
        ui.toast, color(indicator), 0);
    lv_obj_remove_flag(ui.toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(ui.toast, 616);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, ui.toast);
    lv_anim_set_values(&animation, 616, 520);
    lv_anim_set_duration(&animation, kAnimationNormalMs);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&animation, y_animation);
    lv_anim_start(&animation);
    ui.toast_hide_at_ms = lv_tick_get() + kToastVisibleMs;
}

void hmi_ui_tick(uint32_t now_ms, uint32_t hmi_free_heap_bytes) {
    if (!ui.created) {
        return;
    }
    if (static_cast<uint32_t>(now_ms - ui.last_age_refresh_ms) >= 1000U) {
        ui.last_age_refresh_ms = now_ms;
        update_age(now_ms);
        ui.hub_summary.free_heap_bytes = hmi_free_heap_bytes;
        hmi_ui_apply_hub_summary(ui.hub_summary);
    }
    if (ui.toast_hide_at_ms != 0U &&
        static_cast<int32_t>(now_ms - ui.toast_hide_at_ms) >= 0) {
        ui.toast_hide_at_ms = 0U;
        lv_anim_delete(ui.toast, y_animation);
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, ui.toast);
        lv_anim_set_values(&animation, lv_obj_get_y(ui.toast), 616);
        lv_anim_set_duration(&animation, kAnimationNormalMs);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_in);
        lv_anim_set_exec_cb(&animation, y_animation);
        lv_anim_set_completed_cb(
            &animation,
            [](lv_anim_t *finished) {
                lv_obj_t *object =
                    static_cast<lv_obj_t *>(
                        lv_anim_get_user_data(finished));
                if (object != nullptr) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
                }
            });
        lv_anim_set_user_data(&animation, ui.toast);
        lv_anim_start(&animation);
    }
    if (ui.boot_visible) {
        const uint32_t elapsed = now_ms - ui.boot_started_ms;
        if ((ui.controller_online && elapsed > 900U) ||
            elapsed > kBootMaximumMs) {
            hide_boot_overlay();
        }
    }
}
