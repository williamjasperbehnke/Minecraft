#include "app/menus/PauseMenu.hpp"

#include "gfx/HudRenderer.hpp"

#include <string>

namespace app::menus {

int PauseMenu::buttonAtCursor(double mx, double my, int width, int height) const {
    const float cx = static_cast<float>(width) * 0.5f;
    const float cy = static_cast<float>(height) * 0.5f;
    const float x = cx - 140.0f;
    const float w = 280.0f;
    const float h = 36.0f;
    const float resumeY = cy - 70.0f;
    const float saveY = cy - 24.0f;
    const float titleY = cy + 22.0f;
    const float quitY = cy + 68.0f;
    if (mx >= x && mx <= (x + w) && my >= resumeY && my <= (resumeY + h)) {
        return 1;
    }
    if (mx >= x && mx <= (x + w) && my >= saveY && my <= (saveY + h)) {
        return 2;
    }
    if (mx >= x && mx <= (x + w) && my >= titleY && my <= (titleY + h)) {
        return 3;
    }
    if (mx >= x && mx <= (x + w) && my >= quitY && my <= (quitY + h)) {
        return 4;
    }
    return 0;
}

void PauseMenu::render(gfx::HudRenderer &hud, int width, int height, float cursorX,
                       float cursorY) const {
    (void)hud;
    ui_.begin(width, height);

    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;

    ui_.drawRect(0.0f, 0.0f, w, h, 0.02f, 0.03f, 0.04f, 0.55f);
    ui_.drawRect(cx - 202.0f, cy - 142.0f, 404.0f, 284.0f, 0.08f, 0.08f, 0.11f, 0.96f);
    ui_.drawRect(cx - 198.0f, cy - 138.0f, 396.0f, 276.0f, 0.16f, 0.17f, 0.21f, 0.96f);
    ui_.drawRect(cx - 198.0f, cy - 138.0f, 396.0f, 38.0f, 0.23f, 0.26f, 0.33f, 0.96f);
    const std::string pausedTitle = "PAUSED";
    const std::string pausedSubtitle = "Game is paused. Choose what to do next.";
    ui_.drawText(cx - UiMenuRenderer::textWidthPx(pausedTitle) * 0.5f, cy - 122.0f, pausedTitle,
                 246, 248, 252, 255);
    ui_.drawText(cx - UiMenuRenderer::textWidthPx(pausedSubtitle) * 0.5f, cy - 96.0f,
                 pausedSubtitle, 208, 216, 232, 255);

    struct Btn {
        float x;
        float y;
        float w;
        float h;
        const char *label;
    };
    const Btn buttons[4] = {
        {cx - 140.0f, cy - 70.0f, 280.0f, 36.0f, "Resume"},
        {cx - 140.0f, cy - 24.0f, 280.0f, 36.0f, "Save World"},
        {cx - 140.0f, cy + 22.0f, 280.0f, 36.0f, "Save & Title"},
        {cx - 140.0f, cy + 68.0f, 280.0f, 36.0f, "Save & Quit"},
    };
    for (int i = 0; i < 4; ++i) {
        const Btn &btn = buttons[i];
        const bool hover = cursorX >= btn.x && cursorX <= (btn.x + btn.w) && cursorY >= btn.y &&
                           cursorY <= (btn.y + btn.h);
        if (i == 3) {
            ui_.drawRect(btn.x, btn.y, btn.w, btn.h, hover ? 0.45f : 0.30f,
                         hover ? 0.16f : 0.12f, hover ? 0.12f : 0.10f, 0.98f);
            ui_.drawRect(btn.x + 2.0f, btn.y + 2.0f, btn.w - 4.0f, btn.h - 4.0f,
                         hover ? 0.68f : 0.48f, hover ? 0.24f : 0.18f,
                         hover ? 0.18f : 0.14f, 0.95f);
        } else if (i == 2) {
            ui_.drawRect(btn.x, btn.y, btn.w, btn.h, hover ? 0.21f : 0.15f,
                         hover ? 0.33f : 0.24f, hover ? 0.52f : 0.40f, 0.98f);
            ui_.drawRect(btn.x + 2.0f, btn.y + 2.0f, btn.w - 4.0f, btn.h - 4.0f,
                         hover ? 0.28f : 0.22f, hover ? 0.46f : 0.36f,
                         hover ? 0.72f : 0.58f, 0.95f);
        } else if (hover) {
            ui_.drawRect(btn.x, btn.y, btn.w, btn.h, 0.52f, 0.39f, 0.16f, 0.98f);
            ui_.drawRect(btn.x + 2.0f, btn.y + 2.0f, btn.w - 4.0f, btn.h - 4.0f, 0.80f, 0.62f,
                         0.30f, 0.95f);
        } else {
            ui_.drawRect(btn.x, btn.y, btn.w, btn.h, 0.11f, 0.11f, 0.13f, 0.95f);
            ui_.drawRect(btn.x + 1.0f, btn.y + 1.0f, btn.w - 2.0f, btn.h - 2.0f, 0.20f, 0.21f,
                         0.24f, 0.95f);
        }
        const std::string label(btn.label);
        const float lx = btn.x + btn.w * 0.5f - UiMenuRenderer::textWidthPx(label) * 0.5f;
        ui_.drawText(lx, btn.y + 13.0f, label, 245, 246, 250, 255);
    }

    const std::string pauseHint = "Esc closes this menu";
    ui_.drawText(cx - UiMenuRenderer::textWidthPx(pauseHint) * 0.5f, cy + 120.0f, pauseHint, 210,
                 216, 230, 255);
    ui_.end();
}

} // namespace app::menus
