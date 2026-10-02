#ifndef AQUARIUM_UI_NAVIGATION_H
#define AQUARIUM_UI_NAVIGATION_H

namespace ui_navigation {
// A deliberate stroke must exceed ordinary touch jitter on the 4.3-inch panel.
constexpr unsigned kGestureLimit = 72;
constexpr unsigned kGestureMinVelocity = 3;
constexpr int kPageCount = 5;
constexpr int kHeaderHeight = 56;
constexpr int kBodyHeight = 326;
constexpr int kNavigationY = 420;
constexpr int kBodyY = kHeaderHeight + (kNavigationY - kHeaderHeight - kBodyHeight) / 2;
} // namespace ui_navigation

#endif
