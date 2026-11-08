// BotWRecomp: Modern Sheikah-styled in-game overlay menu and FPS counter.
#include <imgui.h>

#include <algorithm>
#include <pthread.h>
#include <atomic>

#include "../src/host/menu.h"
#include "../src/host/touch.h"
#include "../src/host/touch_draw.h"
#include "../src/settings.h"
#include "Cafe/HW/Latte/Core/LatteOverlay.h"

void log_write(const char* fmt, ...);
namespace threads {
std::string main_wait_report();
}

namespace {
std::atomic<float> g_fps{0};
std::atomic<int> g_draws{0};
}  // namespace

// Cemu's GPU thread (LatteThread), for the host loop's slow-frame sampler (window.mm).
std::atomic<pthread_t> g_gpu_thread{nullptr};

void LatteOverlay_updateStats(double fps, sint32 drawcalls, sint32 fastDrawcalls) {
    g_gpu_thread = pthread_self();
    g_fps = (float)fps;
    g_draws = drawcalls;
    static int n = 0;
    if (fps > 0 && (n++ % 5) == 0)
        log_write("[gpu] %.1f fps, %d draws/frame (%d fast); main thread waits: %s", fps, drawcalls, fastDrawcalls,
                  threads::main_wait_report().c_str());
}

void LatteOverlay_render(bool padView) {
    if (padView) return;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    const float ui = std::max(1.0f, io.DisplaySize.y / 540.0f);

    float px, py;
    bool down;
    menu::pointer_state(&px, &py, &down);
    io.AddMousePosEvent(px, py);
    io.AddMouseButtonEvent(0, down);

    const ImGuiWindowFlags overlay = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

    // --- Modern Sleek FPS Counter Pill ---
    if (settings::get().show_fps) {
        float sl, st, sr, sb;
        menu::safe_area(&sl, &st, &sr, &sb);
        ImVec2 pos{io.DisplaySize.x - sr - 18 * ui, st + 14 * ui}, pivot{1.0f, 0.0f};
        const touch::DrawState ts = touch::draw_state();
        if (ts.visible && !ts.collapsed && ts.w > 0)
            for (const touch::DrawItem& it : ts.items)
                if (it.shape == touch::DrawItem::kMenu) {
                    const float k = io.DisplaySize.x / ts.w;
                    pos = {it.x * k, (it.y + it.r + 10 * ts.u) * k}, pivot = {0.5f, 0.0f};
                }

        ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f * ui);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * ui, 6.0f * ui));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.08f, 0.12f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.20f, 0.55f, 0.85f, 0.45f));

        if (ImGui::Begin("##fps", nullptr, overlay | ImGuiWindowFlags_NoInputs)) {
            ImGui::SetWindowFontScale(ui * 1.05f);
            float cur_fps = g_fps.load();
            ImVec4 dot_col = (cur_fps >= 57.0f) ? ImVec4(0.25f, 0.95f, 0.55f, 1.0f) :
                             (cur_fps >= 35.0f) ? ImVec4(1.00f, 0.82f, 0.20f, 1.0f) :
                                                  ImVec4(1.00f, 0.35f, 0.35f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, dot_col);
            ImGui::TextUnformatted("●");
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 6 * ui);
            ImGui::Text("%.0f FPS", (double)cur_fps);
            ImGui::SameLine(0, 8 * ui);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.65f, 0.75f, 0.85f));
            ImGui::Text("| %d draws", (int)g_draws);
            ImGui::PopStyleColor();
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    menu::View v = menu::view();
    if (!v.open) touch::draw();
    if (!v.open) return;

    // --- Modern Glassmorphic In-Game Settings Menu ---
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f * ui);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f * ui, 20.0f * ui));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f * ui, 8.0f * ui));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f * ui);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.09f, 0.13f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.60f, 0.90f, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.20f, 0.30f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.45f, 0.75f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.60f, 0.95f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.40f, 0.60f, 0.45f));

    if (ImGui::Begin("##menu", nullptr, overlay)) {
        ImGui::SetWindowFontScale(ui * 1.25f);

        // Header Title
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.35f, 1.0f));  // Gold accent
        ImGui::TextUnformatted("✦  THE LEGEND OF ZELDA: BOTW  ✦");
        ImGui::PopStyleColor();

        // Close button (X) top right
        const float xs = ImGui::GetFrameHeight();
        ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - xs);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.15f, 0.18f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.20f, 0.25f, 1.0f));
        if (ImGui::Button("✕", {xs, xs})) menu::close();
        ImGui::PopStyleColor(2);

        const ImVec2 xa = ImGui::GetItemRectMin(), xb = ImGui::GetItemRectMax();

        ImGui::SetWindowFontScale(ui * 0.95f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.75f, 0.95f, 0.80f));
        ImGui::TextUnformatted("Configurazione Sistema & Grafica (Metal Engine)");
        ImGui::PopStyleColor();
        ImGui::Separator();

        const float label_w = 170.0f * ui, value_w = 210.0f * ui;

        auto render_section_header = [&](const char* title, const char* icon) {
            ImGui::SetWindowFontScale(ui * 1.0f);
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.80f, 1.0f, 0.95f));  // Sheikah Cyan
            ImGui::Text("%s  %s", icon, title);
            ImGui::PopStyleColor();
        };

        for (int i = 0; i < (int)v.rows.size(); i++) {
            if (i == 0) render_section_header("SCHERMO & PERFORMANCE", "⚡");
            else if (i == 4) render_section_header("CONTROLLI & FEEDBACK", "🎮");
            else if (i == 7) render_section_header("AUDIO & INTERFACCIA", "🔊");
            else if (i == 10) {
                ImGui::Spacing();
                ImGui::Separator();
            }

            const menu::Row& r = v.rows[i];
            ImGui::PushID(i);
            bool sel = (i == v.cursor);

            ImGui::SetWindowFontScale(ui * 1.15f);

            // Active row indicator
            if (sel) {
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.35f, 0.60f, 0.65f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.90f, 1.00f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.10f, 0.14f, 0.20f, 0.30f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.92f, 0.96f, 1.0f));
            }

            // Row Label
            if (ImGui::Selectable(r.label.c_str(), sel, 0, {r.action ? label_w + value_w + 50 * ui : label_w, 0}))
                menu::click(i, 0);

            if (!r.action) {
                ImGui::SameLine();
                const float btn_sz = ImGui::GetFrameHeight();
                if (ImGui::Button("◀", {btn_sz, btn_sz})) menu::click(i, -1);

                ImGui::SameLine();
                // Value pill
                ImGui::PushStyleColor(ImGuiCol_Text, sel ? ImVec4(1.0f, 0.95f, 0.65f, 1.0f) : ImVec4(0.85f, 0.90f, 0.95f, 0.9f));
                ImGui::TextUnformatted(r.value.c_str());
                ImGui::PopStyleColor();

                ImGui::SameLine(label_w + value_w + 24 * ui);
                if (ImGui::Button("▶", {btn_sz, btn_sz})) menu::click(i, +1);
            }

            ImGui::PopStyleColor(2);
            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Footer Note & Shortcuts
        ImGui::SetWindowFontScale(ui * 0.95f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.70f, 0.80f, 0.9f));
        ImGui::TextUnformatted(v.note.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.60f, 0.75f, 0.85f));
        ImGui::TextUnformatted("Comandi:  D-pad / Frecce: Scegli/Modifica  |  A / Spazio: Seleziona  |  F1 / B: Chiudi");
        ImGui::PopStyleColor(2);

        // Touch bounding boxes
        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize(), d = io.DisplaySize;
        const float panel[4] = {wp.x / d.x, wp.y / d.y, (wp.x + ws.x) / d.x, (wp.y + ws.y) / d.y};
        const float xbtn[4] = {xa.x / d.x, xa.y / d.y, xb.x / d.x, xb.y / d.y};
        menu::set_rects(panel, xbtn);
    }
    ImGui::End();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(5);
}
