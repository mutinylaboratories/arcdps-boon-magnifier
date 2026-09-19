// Overlay + options UI, written against the ImGui API subset that is identical in
// 1.80 (Nexus) and 1.92.7 (arcdps). Not a standalone file: overlay_nexus.cpp and
// overlay_arcdps.cpp each #include it inside their own namespace after including
// the matching ImGui, giving one independent copy (and one set of statics) per host.

static bool g_preview = false;
static bool g_reset_pos = false;

static ImU32 rgba(int r, int g, int b, float a) {
    return IM_COL32(r, g, b, (int)(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f));
}

// Boon icon silhouette: a square with a peaked top. Coordinates are in icon units (0..1).
static void shield_path(ImVec2 out[5], ImVec2 p, float s, float inset) {
    const float lo = inset, hi = 1.0f - inset;
    out[0] = ImVec2(p.x + s * 0.5f, p.y + s * lo);
    out[1] = ImVec2(p.x + s * hi,   p.y + s * (0.28f + inset * 0.4f));
    out[2] = ImVec2(p.x + s * hi,   p.y + s * hi);
    out[3] = ImVec2(p.x + s * lo,   p.y + s * hi);
    out[4] = ImVec2(p.x + s * lo,   p.y + s * (0.28f + inset * 0.4f));
}

// Resolution-independent rendition of the Stability icon (white column on orange).
static void draw_stability_vector(ImDrawList* dl, ImVec2 p, float s, float alpha, bool active) {
    ImVec2 pts[5];
    shield_path(pts, p, s, 0.02f);
    dl->AddConvexPolyFilled(pts, 5, rgba(40, 22, 6, alpha));
    shield_path(pts, p, s, 0.07f);
    dl->AddConvexPolyFilled(pts, 5, active ? rgba(232, 146, 38, alpha) : rgba(120, 112, 100, alpha));

    const ImU32 stone = rgba(255, 246, 224, alpha);
    const ImU32 shade = active ? rgba(190, 120, 40, alpha) : rgba(110, 104, 96, alpha);
    auto rect = [&](float x0, float y0, float x1, float y1, ImU32 col, float round = 0.0f) {
        dl->AddRectFilled(ImVec2(p.x + s * x0, p.y + s * y0), ImVec2(p.x + s * x1, p.y + s * y1),
                          col, s * round);
    };
    // capital with volutes
    rect(0.24f, 0.29f, 0.76f, 0.37f, stone, 0.02f);
    dl->AddCircleFilled(ImVec2(p.x + s * 0.25f, p.y + s * 0.37f), s * 0.055f, stone, 16);
    dl->AddCircleFilled(ImVec2(p.x + s * 0.75f, p.y + s * 0.37f), s * 0.055f, stone, 16);
    rect(0.31f, 0.37f, 0.69f, 0.42f, stone);
    // shaft with flutes
    rect(0.34f, 0.42f, 0.66f, 0.76f, stone);
    rect(0.425f, 0.45f, 0.455f, 0.73f, shade);
    rect(0.545f, 0.45f, 0.575f, 0.73f, shade);
    // base
    rect(0.29f, 0.76f, 0.71f, 0.82f, stone);
    rect(0.22f, 0.82f, 0.78f, 0.90f, stone, 0.015f);
}

// Drawn stand-in for boons whose official icon is not available (no Nexus texture yet):
// the boon shield in its signature colour with its abbreviation.
static void draw_boon_badge(ImDrawList* dl, ImFont* font, ImVec2 p, float s, float alpha, bool active,
                            const core::BoonDef* def) {
    ImVec2 pts[5];
    shield_path(pts, p, s, 0.02f);
    dl->AddConvexPolyFilled(pts, 5, rgba(30, 30, 30, alpha));
    shield_path(pts, p, s, 0.07f);
    unsigned c = def ? def->color : 0x888888;
    int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    if (!active) { r = (r + 100) / 2; g = (g + 100) / 2; b = (b + 100) / 2; }
    dl->AddConvexPolyFilled(pts, 5, rgba(r, g, b, alpha));
    const char* text = def ? def->abbrev : "?";
    const float px = s * 0.3f;
    ImVec2 ts = font->CalcTextSizeA(px, FLT_MAX, 0.0f, text);
    dl->AddText(font, px, ImVec2(p.x + (s - ts.x) * 0.5f, p.y + s * 0.62f - ts.y * 0.5f), rgba(255, 255, 255, alpha), text);
}

static void draw_text_outlined(ImDrawList* dl, ImFont* font, float px, ImVec2 pos, ImU32 col,
                               float alpha, const char* text) {
    const float o = std::max(1.0f, px * 0.06f);
    const ImU32 black = rgba(0, 0, 0, alpha);
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            if (dx || dy) dl->AddText(font, px, ImVec2(pos.x + dx * o, pos.y + dy * o), black, text);
    dl->AddText(font, px, pos, col, text);
}

// ---- Seven-segment digits: stay sharp at any size, unlike a scaled-up bitmap font ----
//  segments: a top, b top-right, c bottom-right, d bottom, e bottom-left, f top-left, g middle
static const unsigned char kSegments[10] = {
    0b0111111, 0b0000110, 0b1011011, 0b1001111, 0b1100110,
    0b1101101, 0b1111101, 0b0000111, 0b1111111, 0b1101111};

static float seg_digit_w(float h) { return h * 0.52f; }
static float seg_thick(float h)   { return h * 0.15f; }
static float seg_advance(float h, char c) {
    return (c == '.' ? seg_thick(h) * 1.2f : seg_digit_w(h)) + seg_thick(h) * 1.5f;
}

static ImVec2 seg_measure(float h, const char* text) {
    float w = 0.0f;
    for (const char* c = text; *c; ++c) w += seg_advance(h, *c);
    return ImVec2(w > 0.0f ? w - seg_thick(h) * 0.5f : 0.0f, h + seg_thick(h));
}

static void seg_pass(ImDrawList* dl, ImVec2 pos, float h, const char* text, ImU32 col, float grow) {
    const float t = seg_thick(h), w = seg_digit_w(h);
    float x = pos.x + t * 0.5f;
    const float y = pos.y + t * 0.5f;
    for (const char* c = text; *c; ++c) {
        if (*c == '.') {
            dl->AddCircleFilled(ImVec2(x + t * 0.6f, y + h - t * 0.1f), t * 0.75f + grow, col, 16);
        } else if (*c >= '0' && *c <= '9') {
            const ImVec2 tl(x, y), tr(x + w, y), ml(x, y + h * 0.5f), mr(x + w, y + h * 0.5f),
                         bl(x, y + h), br(x + w, y + h);
            const ImVec2 seg[7][2] = {{tl, tr}, {tr, mr}, {mr, br}, {bl, br}, {ml, bl}, {tl, ml}, {ml, mr}};
            for (int i = 0; i < 7; ++i) {
                if (!(kSegments[*c - '0'] & (1 << i))) continue;
                dl->AddLine(seg[i][0], seg[i][1], col, t + grow * 2.0f);
                dl->AddCircleFilled(seg[i][0], t * 0.5f + grow, col, 12);   // round caps
                dl->AddCircleFilled(seg[i][1], t * 0.5f + grow, col, 12);
            }
        }
        x += seg_advance(h, *c);
    }
}

// px is the nominal font size for both styles, so switching styles keeps the layout.
static ImVec2 measure_number(ImFont* font, float px, const char* text) {
    if (g_cfg.digit_style == core::DIGITS_FONT) return font->CalcTextSizeA(px, FLT_MAX, 0.0f, text);
    return seg_measure(px * 0.72f, text);
}

static void draw_number(ImDrawList* dl, ImFont* font, float px, ImVec2 pos, ImU32 col, float alpha,
                        const char* text) {
    if (g_cfg.digit_style == core::DIGITS_FONT) {
        draw_text_outlined(dl, font, px, pos, col, alpha, text);
        return;
    }
    const float h = px * 0.72f;
    seg_pass(dl, pos, h, text, rgba(0, 0, 0, alpha), std::max(1.5f, h * 0.09f));
    seg_pass(dl, pos, h, text, col, 0.0f);
}

static core::BoonSnapshot current_snapshot(uint32_t boon_id) {
    if (!g_preview) return boon_snapshot(boon_id);
    // Fake an 8s, 3-stack application on loop so the overlay can be styled anywhere.
    core::BoonSnapshot s;
    double t = std::fmod(ImGui::GetTime(), 9.0);
    s.peak_ms = 8000;
    s.remaining_ms = (int64_t)((8.0 - t) * 1000.0);
    s.active = s.remaining_ms > 0;
    s.stacks = s.active ? 3 : 0;
    if (!s.active) s.remaining_ms = 0;
    return s;
}

static void draw_one_overlay(uint32_t boon_id, size_t index, ImFont* font) {
    const core::BoonDef* def = core::find_boon(boon_id);
    const core::BoonSnapshot snap = current_snapshot(boon_id);
    if (!snap.active && !g_cfg.show_inactive && g_cfg.locked) return;

    char window_name[64];
    std::snprintf(window_name, sizeof(window_name), "Boon Magnifier %u##boon_magnifier_overlay_%u", boon_id, boon_id);

    const float S = (float)g_cfg.icon_size;
    const float text_px = std::max(10.0f, S * 0.42f * g_cfg.text_scale);
    const float gap = std::max(2.0f, S * 0.04f);
    // Reserve room for the widest countdown so the window doesn't resize every tick.
    const ImVec2 text_box = measure_number(font, text_px, "00.0");

    ImVec2 total(S, S);
    if (g_cfg.text_pos == core::TEXT_BELOW) total = ImVec2(std::max(S, text_box.x), S + gap + text_box.y);
    if (g_cfg.text_pos == core::TEXT_RIGHT) total = ImVec2(S + gap + text_box.x, std::max(S, text_box.y));

    ImGuiIO& io = ImGui::GetIO();
    // Default placement: a row across the lower middle of the screen, one slot per boon.
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f + (float)index * (S + 24.0f), io.DisplaySize.y * 0.62f),
                            g_reset_pos ? ImGuiCond_Always : ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (g_cfg.locked) flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(8.0f, 8.0f));
    if (ImGui::Begin(window_name, nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::Dummy(total);

        ImVec2 icon = origin;
        if (g_cfg.text_pos == core::TEXT_BELOW) icon.x += (total.x - S) * 0.5f;
        if (g_cfg.text_pos == core::TEXT_RIGHT) icon.y += (total.y - S) * 0.5f;

        const bool show_icon = snap.active || g_cfg.show_inactive || !g_cfg.locked;
        const float alpha = g_cfg.opacity * (snap.active ? 1.0f : 0.35f);

        if (show_icon) {
            // Stability has a crisp vector rendition; other boons use the official icon
            // when the host can provide it and a labelled badge otherwise.
            const bool want_texture = g_cfg.icon_style == core::ICON_TEXTURE || boon_id != core::kBuffStability;
            void* tex = want_texture ? boon_icon_texture(boon_id) : nullptr;
            if (tex) {
                int v = snap.active ? 255 : 150;
                dl->AddImage((ImTextureID)(uintptr_t)tex, icon, ImVec2(icon.x + S, icon.y + S),
                             ImVec2(0, 0), ImVec2(1, 1), rgba(v, v, v, alpha));
            } else if (boon_id == core::kBuffStability) {
                draw_stability_vector(dl, icon, S, alpha, snap.active);
            } else {
                draw_boon_badge(dl, font, icon, S, alpha, snap.active, def);
            }
        }

        if (snap.active) {
            ImVec2 shape[5];
            if (g_cfg.show_sweep && snap.remaining_known && snap.peak_ms > 0) {
                float frac = std::clamp((float)snap.remaining_ms / (float)snap.peak_ms, 0.0f, 1.0f);
                shield_path(shape, icon, S, 0.02f);
                dl->PushClipRect(icon, ImVec2(icon.x + S, icon.y + S * (1.0f - frac)), true);
                dl->AddConvexPolyFilled(shape, 5, rgba(0, 0, 0, 0.55f * g_cfg.opacity));
                dl->PopClipRect();
            }

            const bool warn = snap.remaining_known && g_cfg.warn_seconds > 0.0f &&
                              snap.remaining_ms < (int64_t)(g_cfg.warn_seconds * 1000.0f);
            if (warn) {
                float pulse = 0.55f + 0.45f * std::sin((float)ImGui::GetTime() * 12.0f);
                shield_path(shape, icon, S, 0.02f);
                dl->AddPolyline(shape, 5, rgba(255, 60, 40, pulse * g_cfg.opacity), true,
                                std::max(2.0f, S * 0.045f));
            }

            char buf[16];
            if (snap.remaining_known) {
            float secs = (float)snap.remaining_ms / 1000.0f;
            if (g_cfg.show_decimals && secs < 10.0f) std::snprintf(buf, sizeof(buf), "%.1f", secs);
            else std::snprintf(buf, sizeof(buf), "%d", (int)std::ceil(secs));
            ImVec2 ts = measure_number(font, text_px, buf);
            ImVec2 tp;
            if (g_cfg.text_pos == core::TEXT_BELOW)
                tp = ImVec2(origin.x + (total.x - ts.x) * 0.5f, icon.y + S + gap);
            else if (g_cfg.text_pos == core::TEXT_RIGHT)
                tp = ImVec2(icon.x + S + gap, origin.y + (total.y - ts.y) * 0.5f);
            else
                tp = ImVec2(icon.x + (S - ts.x) * 0.5f, icon.y + S * 0.56f - ts.y * 0.5f);
            ImU32 col = warn ? rgba(255, 90, 70, g_cfg.opacity) : rgba(255, 255, 255, g_cfg.opacity);
            draw_number(dl, font, text_px, tp, col, g_cfg.opacity, buf);
            }   // remaining_known: no digits until arcdps delivers the duration

            if (g_cfg.show_uptime && snap.uptime_ms > 0) {
                const float up_px = std::max(9.0f, S * 0.2f);
                std::snprintf(buf, sizeof(buf), "%d", (int)(snap.uptime_ms / 1000));
                ImVec2 us = measure_number(font, up_px, buf);
                draw_number(dl, font, up_px, ImVec2(icon.x + S * 0.05f, icon.y + S * 0.97f - us.y),
                            rgba(180, 230, 255, g_cfg.opacity), g_cfg.opacity, buf);
            }

            if (g_cfg.show_stacks && snap.stacks > 1) {
                const float stack_px = std::max(9.0f, S * 0.24f);
                std::snprintf(buf, sizeof(buf), "%d", snap.stacks);
                ImVec2 ss = measure_number(font, stack_px, buf);
                draw_number(dl, font, stack_px,
                            ImVec2(icon.x + S * 0.95f - ss.x, icon.y + S * 0.97f - ss.y),
                            rgba(255, 255, 255, g_cfg.opacity), g_cfg.opacity, buf);
            }
        }

        if (!g_cfg.locked) {
            // Show the drag handle area while the overlay is movable.
            ImVec2 a = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
            dl->PushClipRectFullScreen();
            dl->AddRect(a, ImVec2(a.x + sz.x, a.y + sz.y), rgba(255, 255, 255, 0.35f), 3.0f);
            dl->PopClipRect();
            if (ImGui::IsWindowHovered())
                ImGui::SetTooltip("Drag to move. Lock it in the Boon Magnifier options.");
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

// Squad view: every squad member's state for the first tracked boon, with who supplied it.
static void draw_squad_window() {
    const std::vector<SquadBoon> squad = squad_state();
    const uint32_t boon = g_cfg.boons.empty() ? core::kBuffStability : g_cfg.boons.front();
    const core::BoonDef* def = core::find_boon(boon);

    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (g_cfg.locked) flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoTitleBar;
    ImGui::SetNextWindowBgAlpha(0.55f * g_cfg.opacity);
    char title[64];
    std::snprintf(title, sizeof(title), "Squad %s##boon_magnifier_squad", def ? def->name : "boon");
    if (ImGui::Begin(title, nullptr, flags)) {
        if (squad.empty()) {
            ImGui::TextDisabled("No squad members known yet (arcdps announces them as it sees them).");
        } else {
            const float bar_w = 120.0f, bar_h = ImGui::GetTextLineHeight();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const uint64_t now = GetTickCount64();
            for (const SquadBoon& m : squad) {
                const int64_t left = m.readable && m.stacks ? std::max<int64_t>(0, m.remaining_ms - (int64_t)(now - m.updated_ms)) : 0;
                ImGui::Text("%2u  %-20s", (unsigned)m.subgroup, m.name.c_str());
                ImGui::SameLine(210.0f);
                ImVec2 p = ImGui::GetCursorScreenPos();
                dl->AddRectFilled(p, ImVec2(p.x + bar_w, p.y + bar_h), rgba(60, 60, 60, g_cfg.opacity), 3.0f);
                if (left > 0) {
                    const float frac = std::min(1.0f, (float)left / 15000.0f);
                    unsigned c = def ? def->color : 0xE89226;
                    dl->AddRectFilled(p, ImVec2(p.x + bar_w * frac, p.y + bar_h),
                                      rgba((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, g_cfg.opacity), 3.0f);
                }
                ImGui::Dummy(ImVec2(bar_w, bar_h));
                ImGui::SameLine();
                if (!m.readable) ImGui::TextDisabled("  (out of range)");
                else if (!m.stacks) ImGui::TextDisabled("  none");
                else ImGui::Text("  %4.1fs x%d  %s", (float)left / 1000.0f, m.stacks, m.source.empty() ? "" : ("from " + m.source).c_str());
            }
        }
    }
    ImGui::End();
}

void draw_overlay(bool gameplay, void* big_font) {
    if (!ImGui::IsAnyMouseDown()) config_flush();   // not on every slider tick
    if (!g_cfg.enabled || !gameplay) return;
    ImFont* font = big_font ? (ImFont*)big_font : ImGui::GetFont();
    const std::vector<uint32_t> boons = g_cfg.boons;   // copy: options may edit the list mid-frame
    for (size_t i = 0; i < boons.size(); ++i) draw_one_overlay(boons[i], i, font);
    if (g_cfg.show_squad) draw_squad_window();
    g_reset_pos = false;
}

void draw_options() {
    bool changed = false;
    changed |= ImGui::Checkbox("Enabled", &g_cfg.enabled);
    changed |= ImGui::Checkbox("Lock position (click-through)", &g_cfg.locked);
    ImGui::SameLine();
    if (ImGui::Button("Reset position")) g_reset_pos = true;
    ImGui::Checkbox("Preview (fake boon)", &g_preview);

    ImGui::Separator();
    ImGui::TextUnformatted("Boons to show (one overlay each)");
    {
        int col = 0;
        for (const core::BoonDef& def : core::boon_catalogue()) {
            bool on = std::find(g_cfg.boons.begin(), g_cfg.boons.end(), def.id) != g_cfg.boons.end();
            if (col++ % 3) ImGui::SameLine(0.0f, 12.0f);
            if (ImGui::Checkbox(def.name, &on)) {
                if (on) g_cfg.boons.push_back(def.id);
                else g_cfg.boons.erase(std::remove(g_cfg.boons.begin(), g_cfg.boons.end(), def.id), g_cfg.boons.end());
                if (g_cfg.boons.empty()) g_cfg.boons.push_back(def.id);   // keep at least one
                set_tracked_boons(g_cfg.boons);
                changed = true;
            }
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Icon size");
    for (int preset : core::kIconSizePresets) {
        char label[16];
        std::snprintf(label, sizeof(label), "%d", preset);
        const bool selected = g_cfg.icon_size == preset;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label)) { g_cfg.icon_size = preset; changed = true; }
        if (selected) ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    ImGui::NewLine();
    changed |= ImGui::SliderInt("Custom size (px)", &g_cfg.icon_size, core::kIconSizeMin, core::kIconSizeMax);

    const char* styles[] = {"Crisp (vector)", "Game icon (32px, pixelated)"};
    changed |= ImGui::Combo("Stability icon", &g_cfg.icon_style, styles, IM_ARRAYSIZE(styles));
    if (g_cfg.icon_style == core::ICON_TEXTURE && !icon_texture())
        ImGui::TextDisabled("Game icon texture unavailable; using the vector icon.");
    ImGui::TextDisabled("Other boons use the official icon (fetched by Nexus) or a labelled badge.");

    ImGui::Separator();
    const char* positions[] = {"Over the icon", "Below the icon", "Right of the icon"};
    changed |= ImGui::Combo("Countdown position", &g_cfg.text_pos, positions, IM_ARRAYSIZE(positions));
    const char* digits[] = {"Crisp (segment digits)", "Host font (scaled)"};
    changed |= ImGui::Combo("Countdown digits", &g_cfg.digit_style, digits, IM_ARRAYSIZE(digits));
    changed |= ImGui::SliderFloat("Countdown scale", &g_cfg.text_scale, 0.25f, 3.0f, "%.2f");
    changed |= ImGui::Checkbox("Tenths of a second below 10s", &g_cfg.show_decimals);
    changed |= ImGui::Checkbox("Show stack count", &g_cfg.show_stacks);
    changed |= ImGui::Checkbox("Show uptime (seconds continuously active, bottom-left)", &g_cfg.show_uptime);
    changed |= ImGui::Checkbox("Squad window: every member's boon and who supplied it", &g_cfg.show_squad);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reads squad members' buff bars from the client (first tracked boon). Needs the realtime\n"
                          "source; membership comes from arcdps, so members appear as arcdps announces them.");
    changed |= ImGui::Checkbox("Project pulsing fields (e.g. Hallowed Ground) to their end", &g_cfg.project_pulses);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Once a second identical stack arrives on the field's rhythm, the countdown runs to the\n"
                          "field's projected end instead of the current stack. Drops back the moment a pulse is missed.\n"
                          "Sources: arcdps_boon_magnifier_pulses.ini beside the DLL (built-in: Hallowed Ground).");
    changed |= ImGui::Checkbox("Darken icon as time runs out", &g_cfg.show_sweep);
    changed |= ImGui::Checkbox("Show dimmed icon while inactive", &g_cfg.show_inactive);
    changed |= ImGui::SliderFloat("Warn below (s)", &g_cfg.warn_seconds, 0.0f, 10.0f, "%.1f");
    changed |= ImGui::SliderFloat("Opacity", &g_cfg.opacity, 0.1f, 1.0f, "%.2f");

    ImGui::Separator();
    for (uint32_t id : g_cfg.boons) {
        const core::BoonDef* def = core::find_boon(id);
        const char* name = def ? def->name : "?";
        const core::BoonSnapshot snap = boon_snapshot(id);
        if (snap.active)
            ImGui::Text("%s: %d stack%s, %.1fs left", name, snap.stacks, snap.stacks == 1 ? "" : "s",
                        (float)snap.remaining_ms / 1000.0f);
        else
            ImGui::TextDisabled("%s: not active", name);
    }

    switch (last_feed()) {
    case Feed::ArcdpsDirect: ImGui::TextDisabled("Combat data: arcdps (direct)"); break;
    case Feed::NexusEvents:  ImGui::TextDisabled("Combat data: ArcDPS Integration events"); break;
    default:
        ImGui::TextDisabled("Combat data: nothing received yet");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Boon data comes from arcdps. Under Nexus, either let arcdps load this DLL too\n"
                              "(same addons folder) or install the ArcDPS Integration addon.");
        break;
    }

    if (ImGui::TreeNode("Diagnostics")) {
        const char* names[2] = {"Area ", "Local"};
        const uint32_t first = g_cfg.boons.empty() ? core::kBuffStability : g_cfg.boons.front();
        for (int i = 0; i < 2; ++i) {
            const LatencyStats lat = latency_stats((Channel)i);
            const core::BoonSnapshot cs = boon_snapshot(first, (Channel)i);
            ImGui::Text("%s: %d stacks, %.1fs | %u boon events, delay last %u / avg %u / max %u ms", names[i],
                        cs.stacks, (float)cs.remaining_ms / 1000.0f, lat.count, lat.last, lat.avg, lat.max);
        }
        ImGui::TextDisabled("Delay = how long after it happened arcdps delivered the event.\n"
                            "arcdps holds its boon feed back ~2.65s on purpose; the overlay appears that much late.");
        const core::RealtimeState rt = realtime_state(first);
        if (rt.valid)
            ImGui::Text("Realtime source: %s, %d stacks, %.1fs left", rt.active ? "UP" : "down", rt.stacks,
                        rt.remaining_ms >= 0 ? (float)rt.remaining_ms / 1000.0f : -1.0f);
        else
            ImGui::TextDisabled("Realtime source: nothing reported (arcdps only)");
        ImGui::TextWrapped("Signatures: %s", realtime_source_status().c_str());
        ImGui::TextWrapped("Poll: %s", realtime_poll_status().c_str());
        if (!realtime_debug_status().empty()) ImGui::TextWrapped("Skill bar: %s", realtime_debug_status().c_str());
        if (ImGui::Button("Reset stats")) latency_reset();
        bool logging = event_log_enabled();
        if (ImGui::Checkbox("Log tracked boon events to file", &logging)) event_log_enable(logging);
        if (logging) ImGui::TextDisabled("arcdps_boon_magnifier_events.log, next to the DLL");
        ImGui::TreePop();
    }

    if (changed) {
        core::clamp(g_cfg);
        config_mark_dirty();
    }
}
