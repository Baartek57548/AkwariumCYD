#pragma once

#include <lvgl.h>

namespace gui_lifecycle {

inline void clear_object_reference(lv_event_t *event) {
    auto **reference = static_cast<lv_obj_t **>(lv_event_get_user_data(event));
    if (reference != nullptr && *reference == lv_event_get_target(event)) {
        *reference = nullptr;
    }
}

inline void slide_y(void *object, int32_t y) {
    lv_obj_set_y(static_cast<lv_obj_t *>(object), y);
}

inline void delete_after_slide(lv_anim_t *animation) {
    lv_obj_del(static_cast<lv_obj_t *>(animation->var));
}

inline bool after(lv_obj_t *object, uint32_t delay_ms, lv_anim_ready_cb_t callback) {
    if (object == nullptr || callback == nullptr) return false;
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_time(&animation, delay_ms);
    lv_anim_set_ready_cb(&animation, callback);
    return lv_anim_start(&animation) != nullptr;
}

inline void show_transient(lv_obj_t *object, int32_t hidden_y, int32_t visible_y,
                           uint32_t enter_ms, uint32_t hold_ms, uint32_t leave_ms) {
    if (object == nullptr) return;
    // LVGL cancels an object's animations on deletion, but not its timers.
    // Playback keeps the entire notification lifetime tied to that object, so
    // a theme rebuild cannot later animate/delete a new object at its address.
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_values(&animation, hidden_y, visible_y);
    lv_anim_set_time(&animation, enter_ms);
    lv_anim_set_playback_delay(&animation, hold_ms);
    lv_anim_set_playback_time(&animation, leave_ms);
    lv_anim_set_exec_cb(&animation, slide_y);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&animation, delete_after_slide);
    if (lv_anim_start(&animation) == nullptr) {
        lv_obj_del(object);
    }
}

} // namespace gui_lifecycle
