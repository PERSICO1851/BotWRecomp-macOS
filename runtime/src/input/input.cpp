#include "input.h"

#include <deque>
#include <mutex>

namespace input {
namespace {
std::mutex g_lock;
PadState g_pad;

uint32_t stick_buttons(float x, float y, uint32_t base) {
    uint32_t b = 0;
    if (x < -0.5f) b |= base << 3;
    if (x > 0.5f) b |= base << 2;
    if (y > 0.5f) b |= base << 1;
    if (y < -0.5f) b |= base;
    return b;
}

uint32_t virtual_buttons(const PadState& s) {
    return s.buttons | stick_buttons(s.lx, s.ly, 0x08000000) | stick_buttons(s.rx, s.ry, 0x00800000);
}
}
PadState gamepad() {
    std::lock_guard<std::mutex> l(g_lock);
    return g_pad;
}
void set_gamepad(const PadState& s) {
    std::lock_guard<std::mutex> l(g_lock);
    PadState sample = s;
    const uint32_t previous = virtual_buttons(g_pad);
    const uint32_t current = virtual_buttons(sample);
    sample.pressed = current & ~previous;
    sample.released = previous & ~current;
    g_pad = sample;
}

namespace {
std::mutex g_rumble_lock;
std::deque<bool> g_rumble;  // 60 Hz steps
}  // namespace
bool push_rumble(const uint8_t* pattern, int bits) {
    std::lock_guard<std::mutex> l(g_rumble_lock);
    if (g_rumble.size() >= 5 * 60) return false;  // Cemu queues at most 5 one-second patterns
    for (int i = 0; i < bits; i += 2)  // two 120 Hz bits per step
        g_rumble.push_back((pattern[i / 8] >> (i % 8)) & 3);
    return true;
}
void clear_rumble() {
    std::lock_guard<std::mutex> l(g_rumble_lock);
    g_rumble.clear();
}
bool rumble_step() {
    std::lock_guard<std::mutex> l(g_rumble_lock);
    if (g_rumble.empty()) return false;
    bool on = g_rumble.front();
    g_rumble.pop_front();
    return on;
}
}  // namespace input
