#ifndef HAL_DISPLAY_H
#define HAL_DISPLAY_H

#include <Arduino.h>

/**
 * @brief Inicjalizuje wyświetlacz LovyanGFX oraz panel dotykowy,
 * a także rejestruje je w systemie LVGL.
 */
void hal_display_init(void);

/**
 * @brief Sprawdza, czy ekran jest dotykany, i zwraca zmapowane współrzędne.
 * @param x Wskaźnik na zmienną, do której zostanie zapisana współrzędna X.
 * @param y Wskaźnik na zmienną, do której zostanie zapisana współrzędna Y.
 * @return true jeśli ekran jest dotykany, w przeciwnym razie false.
 */
bool hal_display_get_touch(int16_t *x, int16_t *y);

/**
 * @brief Taktuje asynchroniczny kontroler DMA i zgłasza gotowość LVGL po zakończeniu transferu.
 */
void hal_display_loop_cb(void);

#endif // HAL_DISPLAY_H

