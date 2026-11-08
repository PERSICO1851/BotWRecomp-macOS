#include "menu.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>

#include "../settings.h"

extern float g_render_scale;  // main.cpp: the scale this run renders at

namespace menu {
namespace {
std::mutex g_lock;
std::atomic<bool> g_open{false};
int g_cursor = 0;

enum RowId { kFps, kScale, kDisplay, kFilter, kGyro, kRumble, kLayout, kTouch, kShowFps, kVolume, kClose, kRows };
const float kScales[] = {0, 0.5f, 0.75f, 1, 1.25f, 1.5f, 2, 2.5f, 3};  // 0 = fit the display
const char* kDisplays[] = {"Fit 16:9", "Stretch", "Fill (crop)"};
const char* kFilters[] = {"Bilinear", "Bicubic (Sharp)", "Hermite", "Nearest"};

std::string scale_text(float s) {
    char b[64];
    if (s <= 0) return "Auto (Schermo intero)";
    if (fabsf(s - 1.0f) < 0.01f) return "1.0x (720p - Max FPS)";
    if (fabsf(s - 1.25f) < 0.01f) return "1.25x (900p - Bilanciato)";
    if (fabsf(s - 1.5f) < 0.01f) return "1.5x (1080p - Alta Qualità)";
    if (fabsf(s - 2.0f) < 0.01f) return "2.0x (1440p 2K Ultra)";
    if (fabsf(s - 3.0f) < 0.01f) return "3.0x (2160p 4K Retina)";
    snprintf(b, sizeof b, "%.2gx  (%dx%d)", s, (int)lroundf(1280 * s), (int)lroundf(720 * s));
    return b;
}

void step(Settings& s, int row, int dir) {
    auto wrap = [](int v, int n) { return (v % n + n) % n; };
    switch (row) {
        case kFps: s.fps = s.fps >= 60 ? 30 : 60; break;
        case kScale: {
            int i = 0;
            for (int k = 0; k < (int)std::size(kScales); k++)
                if (fabsf(kScales[k] - s.scale) < 0.01f) i = k;
            s.scale = kScales[wrap(i + dir, (int)std::size(kScales))];
            break;
        }
        case kDisplay: s.display = wrap(s.display + dir, 3); break;
        case kFilter: s.filter = wrap(s.filter + dir, 4); break;
        case kGyro: s.gyro = !s.gyro; break;
        case kRumble: s.rumble = !s.rumble; break;
        case kLayout: s.layout = wrap(s.layout + dir, 4); break;
        case kTouch: s.touch = wrap(s.touch + dir, 3), s.touch_hidden = false; break;
        case kShowFps: s.show_fps = !s.show_fps; break;
        case kVolume: s.volume = std::clamp(s.volume + dir * 10, 0, 100); break;
    }
}
}  // namespace

std::atomic<float> g_px{-1}, g_py{-1};
std::atomic<bool> g_pdown{false};
void pointer(float x, float y, bool down) {
    g_px = x;
    g_py = y;
    g_pdown = down;
}
void pointer_state(float* x, float* y, bool* down) {
    *x = g_px;
    *y = g_py;
    *down = g_pdown;
}

std::atomic<float> g_safe[4] = {0, 0, 0, 0};
void set_safe_area(float l, float t, float r, float b) {
    g_safe[0] = l, g_safe[1] = t, g_safe[2] = r, g_safe[3] = b;
}
void safe_area(float* l, float* t, float* r, float* b) {
    *l = g_safe[0], *t = g_safe[1], *r = g_safe[2], *b = g_safe[3];
}

std::atomic<float> g_panel[4] = {0, 0, 0, 0}, g_xbtn[4] = {0, 0, 0, 0};
void set_rects(const float p[4], const float x[4]) {
    for (int i = 0; i < 4; i++) g_panel[i] = p[i], g_xbtn[i] = x[i];
}
void rects(float p[4], float x[4]) {
    for (int i = 0; i < 4; i++) p[i] = g_panel[i], x[i] = g_xbtn[i];
}
bool touch_close(float x, float y) {
    const float slop = 0.04f;  // the X is small for a thumb
    bool outside = x < g_panel[0] || y < g_panel[1] || x > g_panel[2] || y > g_panel[3];
    bool on_x = x > g_xbtn[0] - slop && y > g_xbtn[1] - slop && x < g_xbtn[2] + slop && y < g_xbtn[3] + slop;
    if (!g_open || !(outside || on_x)) return false;
    close();
    return true;
}

bool is_open() { return g_open; }
void toggle() { g_open = !g_open; }
void close() { g_open = false; }

void move(int dy) {
    std::lock_guard<std::mutex> l(g_lock);
    g_cursor = (g_cursor + dy + kRows) % kRows;
}

void change(int dir) {
    int row;
    {
        std::lock_guard<std::mutex> l(g_lock);
        row = g_cursor;
    }
    if (row == kClose) return;
    Settings s = settings::get();
    step(s, row, dir);
    settings::set(s);
}

void activate() {
    int row;
    {
        std::lock_guard<std::mutex> l(g_lock);
        row = g_cursor;
    }
    if (row == kClose) close();
    else change(+1);
}

void click(int row, int dir) {
    if (row < 0 || row >= kRows) return;
    {
        std::lock_guard<std::mutex> l(g_lock);
        g_cursor = row;
    }
    if (dir == 0) activate();
    else change(dir);
}

View view() {
    View v;
    v.open = g_open;
    {
        std::lock_guard<std::mutex> l(g_lock);
        v.cursor = g_cursor;
    }
    Settings s = settings::get();
    char b[128];
    v.rows.resize(kRows);
    v.rows[kFps] = {"Obiettivo frame rate", s.fps >= 60 ? "60 FPS" : "30 FPS (originale)"};
    v.rows[kScale] = {"Risoluzione", scale_text(s.scale)};
    v.rows[kDisplay] = {"Proporzioni", kDisplays[std::clamp(s.display, 0, 2)]};
    v.rows[kFilter] = {"Filtro Upscale", kFilters[std::clamp(s.filter, 0, 3)]};
    v.rows[kGyro] = {"Mira Giroscopio", s.gyro ? "Attivo" : "Disattivo"};
    v.rows[kRumble] = {"Vibrazione", s.rumble ? "Attivo" : "Disattivo"};
    static const char* kLayoutNames[] = {
        "Nintendo (A=Destra, B=Giu)",
        "PlayStation 4/5 (Croce=Azione, Quadrato=Attacco)",
        "PlayStation Action (Croce=Salto, Quadrato=Attacco)",
        "Xbox (A=Azione, X=Attacco, Y=Salto)"
    };
    v.rows[kLayout] = {"Controller", kLayoutNames[std::clamp(s.layout, 0, 3)]};
    v.rows[kTouch] = {"Controlli Touch", s.touch == 1 ? "Attivo" : s.touch == 2 ? "Disattivo" : "Auto (no pad)"};
    v.rows[kShowFps] = {"Overlay FPS", s.show_fps ? "Mostra" : "Nascondi"};
    snprintf(b, sizeof b, "%d%%", s.volume);
    v.rows[kVolume] = {"Volume Audio", b};
    v.rows[kClose] = {"Chiudi Menu", "", true};
    snprintf(b, sizeof b, "Rendering attivo: %.2fx | Cambi di risoluzione effettivi al prossimo avvio.", g_render_scale);
    v.note = b;
    return v;
}
}  // namespace menu
