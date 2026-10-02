#include <unity.h>
#include "gui_lifecycle.h"

static lv_disp_draw_buf_t draw_buffer;
static lv_color_t pixels[320 * 10];
static lv_indev_data_t pointer_data;
static unsigned clicks = 0;
static unsigned delayed_calls = 0;

static void advance(uint32_t duration_ms) {
    for (uint32_t elapsed = 0; elapsed < duration_ms; elapsed += 10U) {
        lv_tick_inc(10U);
        lv_timer_handler();
    }
}

void setUp() {
    pointer_data.state = LV_INDEV_STATE_REL;
    advance(50U);
    lv_obj_clean(lv_scr_act());
    clicks = 0;
    delayed_calls = 0;
}

void tearDown() {
    lv_obj_clean(lv_scr_act());
    advance(50U);
}

static void count_delayed(lv_anim_t *) { ++delayed_calls; }

static void test_callback_is_cancelled_with_its_object() {
    lv_obj_t *old_object = lv_obj_create(lv_scr_act());
    TEST_ASSERT_TRUE(gui_lifecycle::after(old_object, 2000U, count_delayed));
    lv_obj_del(old_object);
    lv_obj_t *replacement = lv_obj_create(lv_scr_act());
    advance(3000U);
    TEST_ASSERT_EQUAL_UINT(0U, delayed_calls);
    TEST_ASSERT_TRUE(lv_obj_is_valid(replacement));
}

static void test_callback_runs_once_for_a_live_object() {
    lv_obj_t *object = lv_obj_create(lv_scr_act());
    TEST_ASSERT_TRUE(gui_lifecycle::after(object, 100U, count_delayed));
    advance(500U);
    TEST_ASSERT_EQUAL_UINT(1U, delayed_calls);
}

static void test_notification_deletion_clears_the_saved_reference() {
    lv_obj_t *notice = lv_obj_create(lv_scr_act());
    lv_obj_add_event_cb(notice, gui_lifecycle::clear_object_reference, LV_EVENT_DELETE, &notice);
    gui_lifecycle::show_transient(notice, -60, 15, 50U, 100U, 50U);
    advance(500U);
    TEST_ASSERT_NULL(notice);
}

static void test_rebuild_during_notification_keeps_navigation_clickable() {
    for (unsigned cycle = 0; cycle < 30; ++cycle) {
        lv_obj_t *notice = lv_obj_create(lv_scr_act());
        lv_obj_add_event_cb(notice, gui_lifecycle::clear_object_reference, LV_EVENT_DELETE, &notice);
        gui_lifecycle::show_transient(notice, -60, 15, 50U, 100U, 50U);
        // Rebuild both during the entry animation and its delayed playback.
        advance(cycle % 2U == 0U ? 20U : 100U);
        lv_obj_clean(lv_scr_act());
        TEST_ASSERT_NULL(notice);
        lv_theme_t *theme = lv_theme_default_init(lv_disp_get_default(),
            lv_palette_main(LV_PALETTE_CYAN), lv_palette_main(LV_PALETTE_GREY),
            cycle % 2U != 0U, &lv_font_montserrat_14);
        lv_disp_set_theme(lv_disp_get_default(), theme);
        lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *navigation = lv_obj_create(lv_scr_act());
        lv_obj_set_size(navigation, 320, 35);
        lv_obj_set_pos(navigation, 0, 205);
        lv_obj_set_style_pad_all(navigation, 0, 0);
        lv_obj_set_style_border_width(navigation, 0, 0);
        lv_obj_clear_flag(navigation, LV_OBJ_FLAG_SCROLLABLE);
        for (int button_index = 0; button_index < 5; ++button_index) {
            lv_obj_t *button = lv_btn_create(navigation);
            lv_obj_set_size(button, 62, 33);
            lv_obj_set_pos(button, button_index * 64 + 1, 1);
            lv_obj_add_event_cb(button, [](lv_event_t *) { ++clicks; }, LV_EVENT_CLICKED, nullptr);
        }
        advance(500U);
        TEST_ASSERT_TRUE(lv_obj_is_valid(navigation));
        TEST_ASSERT_EQUAL_INT(205, lv_obj_get_y(navigation));
        TEST_ASSERT_EQUAL_UINT(5U, lv_obj_get_child_cnt(navigation));
        for (int button_index = 0; button_index < 5; ++button_index) {
            pointer_data.point.x = button_index * 64 + 31;
            pointer_data.point.y = 222;
            pointer_data.state = LV_INDEV_STATE_PR;
            advance(80U);
            pointer_data.state = LV_INDEV_STATE_REL;
            advance(80U);
        }
        TEST_ASSERT_EQUAL_UINT((cycle + 1U) * 5U, clicks);
        lv_obj_clean(lv_scr_act());
    }
}

static void test_deferred_deletion_does_not_delete_replacement() {
    lv_obj_t *old_object = lv_obj_create(lv_scr_act());
    lv_obj_del_delayed(old_object, 1U);
    lv_obj_clean(lv_scr_act());
    lv_obj_t *replacement = lv_obj_create(lv_scr_act());
    advance(100U);
    TEST_ASSERT_TRUE(lv_obj_is_valid(replacement));
}

int main() {
    lv_init();
    lv_disp_draw_buf_init(&draw_buffer, pixels, nullptr, 320 * 10);
    lv_disp_drv_t display;
    lv_disp_drv_init(&display);
    display.hor_res = 320;
    display.ver_res = 240;
    display.draw_buf = &draw_buffer;
    display.flush_cb = [](lv_disp_drv_t *driver, const lv_area_t *, lv_color_t *) {
        lv_disp_flush_ready(driver);
    };
    lv_disp_drv_register(&display);
    lv_indev_drv_t input;
    lv_indev_drv_init(&input);
    input.type = LV_INDEV_TYPE_POINTER;
    input.read_cb = [](lv_indev_drv_t *, lv_indev_data_t *data) { *data = pointer_data; };
    lv_indev_drv_register(&input);
    UNITY_BEGIN();
    RUN_TEST(test_callback_is_cancelled_with_its_object);
    RUN_TEST(test_callback_runs_once_for_a_live_object);
    RUN_TEST(test_notification_deletion_clears_the_saved_reference);
    RUN_TEST(test_rebuild_during_notification_keeps_navigation_clickable);
    RUN_TEST(test_deferred_deletion_does_not_delete_replacement);
    return UNITY_END();
}
