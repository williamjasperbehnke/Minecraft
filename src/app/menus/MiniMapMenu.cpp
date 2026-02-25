#include "app/menus/MiniMapMenu.hpp"

#include "gfx/HudRenderer.hpp"
#include "gfx/TextureAtlas.hpp"
#include "voxel/Block.hpp"

#include <algorithm>
#include <cmath>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace app::menus {
namespace {

glm::vec3 mapColorForBlock(voxel::BlockId id, const voxel::BlockRegistry &registry,
                           const gfx::TextureAtlas &atlas) {
    const voxel::BlockDef &def = registry.get(id);
    return atlas.tileAverageColor(def.topTile);
}

} // namespace

MiniMapLayout MiniMapMenu::computeLayout(int width) const {
    const float w = static_cast<float>(width);
    const float panelW = 190.0f;
    const float panelH = 190.0f;
    const float margin = 14.0f;
    const float panelX = w - panelW - margin;
    const float panelY = margin;
    const float btnH = 16.0f;
    const float btnY = panelY + panelH - btnH - 7.0f;
    const float compassW = 22.0f;
    const float followW = 22.0f;
    const float waypointW = 22.0f;
    const float minusW = 24.0f;
    const float plusW = 24.0f;
    const float gap = 4.0f;
    const float totalW = compassW + followW + waypointW + minusW + plusW + gap * 4.0f;
    const float btnX0 = panelX + panelW - totalW - 8.0f;
    const float followX = btnX0 + compassW + gap;
    const float waypointX = followX + followW + gap;
    const float minusX = waypointX + waypointW + gap;
    const float plusX = minusX + minusW + gap;
    return {panelX, panelY, panelW, panelH, btnX0, btnY, compassW, btnH, followX, btnY, followW,
            btnH, waypointX, btnY, waypointW, btnH, minusX, btnY, minusW, btnH, plusX, btnY,
            plusW, btnH};
}

void MiniMapMenu::render(gfx::HudRenderer &hud, int width, int height, const game::MapSystem &map,
                         int playerWX, int playerWZ, float zoom, bool northLocked,
                         bool showCompass, bool showWaypoints, float headingRad,
                         const voxel::BlockRegistry &registry,
                         const gfx::TextureAtlas &atlas) const {
    (void)hud;
    ui_.begin(width, height);

    const float w = static_cast<float>(width);
    const float panelW = 190.0f;
    const float panelH = 190.0f;
    const float margin = 14.0f;
    const float panelX = w - panelW - margin;
    const float panelY = margin;
    const float innerPad = 10.0f;
    const float gridX = panelX + innerPad;
    const float gridY = panelY + innerPad + 14.0f;
    const float gridW = panelW - innerPad * 2.0f;
    const float gridH = panelH - innerPad * 2.0f - 36.0f;
    const float cell = std::clamp(2.2f * zoom, 1.2f, 6.0f);
    const int drawCols = std::max(1, static_cast<int>(std::ceil(gridW / cell)));
    const int drawRows = std::max(1, static_cast<int>(std::ceil(gridH / cell)));
    const float centerIx = (static_cast<float>(drawCols) - 1.0f) * 0.5f;
    const float centerIz = (static_cast<float>(drawRows) - 1.0f) * 0.5f;
    auto drawWaypointShape = [&](float cx, float cy, float size, int icon, float r, float g,
                                 float b, float a) {
        const float half = size * 0.5f;
        switch (icon % 5) {
        case 0: {
            const int iy0 = static_cast<int>(std::floor(-half));
            const int iy1 = static_cast<int>(std::ceil(half));
            for (int iy = iy0; iy <= iy1; ++iy) {
                const float y = static_cast<float>(iy);
                const float xr = std::sqrt(std::max(0.0f, half * half - y * y));
                ui_.drawRect(cx - xr, cy + y, xr * 2.0f, 1.0f, r, g, b, a);
            }
        } break;
        case 1:
            ui_.drawRect(cx - half, cy - half, size, size, r, g, b, a);
            break;
        case 2: {
            const float hh = size;
            const float topY = cy - hh * 0.5f;
            for (int row = 0; row < static_cast<int>(std::ceil(hh)); ++row) {
                const float t = static_cast<float>(row) / std::max(1.0f, hh - 1.0f);
                const float wrow = (0.18f + t * 0.82f) * size;
                ui_.drawRect(cx - wrow * 0.5f, topY + static_cast<float>(row), wrow, 1.0f, r, g, b, a);
            }
        } break;
        case 3: {
            const int iy0 = static_cast<int>(std::floor(-half));
            const int iy1 = static_cast<int>(std::ceil(half));
            for (int iy = iy0; iy <= iy1; ++iy) {
                const float y = std::abs(static_cast<float>(iy));
                const float xr = std::max(0.0f, half - y);
                ui_.drawRect(cx - xr, cy + static_cast<float>(iy), xr * 2.0f, 1.0f, r, g, b, a);
            }
        } break;
        default: {
            const float t = std::max(1.0f, size * 0.28f);
            ui_.drawRect(cx - t * 0.5f, cy - half, t, size, r, g, b, a);
            ui_.drawRect(cx - half, cy - t * 0.5f, size, t, r, g, b, a);
        } break;
        }
    };

    ui_.drawRect(panelX, panelY, panelW, panelH, 0.05f, 0.07f, 0.10f, 0.86f);
    ui_.drawRect(panelX + 2.0f, panelY + 2.0f, panelW - 4.0f, panelH - 4.0f, 0.10f, 0.12f, 0.16f,
                 0.86f);
    ui_.drawRect(gridX, gridY, gridW, gridH, 0.08f, 0.10f, 0.12f, 0.92f);
    ui_.drawText(panelX + 10.0f, panelY + 8.0f, "Mini Map", 228, 234, 246, 255);

    for (int iz = 0; iz < drawRows; ++iz) {
        for (int ix = 0; ix < drawCols; ++ix) {
            const int dx = static_cast<int>(std::floor(static_cast<float>(ix) - centerIx));
            const int dz = static_cast<int>(std::floor(static_cast<float>(iz) - centerIz));
            int sdx = dx;
            int sdz = dz;
            if (!northLocked) {
                const float c = std::cos(headingRad);
                const float s = std::sin(headingRad);
                const float rx = static_cast<float>(dx) * c - static_cast<float>(dz) * s;
                const float rz = static_cast<float>(dx) * s + static_cast<float>(dz) * c;
                sdx = static_cast<int>(std::round(rx));
                sdz = static_cast<int>(std::round(rz));
            }
            voxel::BlockId id = voxel::AIR;
            if (!map.sample(playerWX + sdx, playerWZ + sdz, id)) {
                continue;
            }
            const glm::vec3 c = mapColorForBlock(id, registry, atlas);
            const float px = gridX + static_cast<float>(ix) * cell;
            const float py = gridY + static_cast<float>(iz) * cell;
            const float pw = std::min(cell, (gridX + gridW) - px);
            const float ph = std::min(cell, (gridY + gridH) - py);
            if (pw <= 0.0f || ph <= 0.0f) {
                continue;
            }
            ui_.drawRect(px, py, pw, ph, c.r, c.g, c.b, 0.96f);
        }
    }

    const float pCenterX = gridX + centerIx * cell;
    const float pCenterY = gridY + centerIz * cell;
    auto drawFilledTri = [&](glm::vec2 a, glm::vec2 b, glm::vec2 c, float rr, float rg, float rb,
                             float ra) {
        const float minX = std::floor(std::min({a.x, b.x, c.x}));
        const float maxX = std::ceil(std::max({a.x, b.x, c.x}));
        const float minY = std::floor(std::min({a.y, b.y, c.y}));
        const float maxY = std::ceil(std::max({a.y, b.y, c.y}));
        const auto edge = [](glm::vec2 p0, glm::vec2 p1, glm::vec2 p) {
            return (p.x - p0.x) * (p1.y - p0.y) - (p.y - p0.y) * (p1.x - p0.x);
        };
        for (int y = static_cast<int>(minY); y <= static_cast<int>(maxY); ++y) {
            for (int x = static_cast<int>(minX); x <= static_cast<int>(maxX); ++x) {
                const glm::vec2 p(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
                const float e0 = edge(a, b, p);
                const float e1 = edge(b, c, p);
                const float e2 = edge(c, a, p);
                if ((e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f) ||
                    (e0 <= 0.0f && e1 <= 0.0f && e2 <= 0.0f)) {
                    ui_.drawRect(static_cast<float>(x), static_cast<float>(y), 1.0f, 1.0f, rr, rg, rb, ra);
                }
            }
        }
    };
    const float iconAngle = northLocked ? headingRad : 0.0f;
    const glm::vec2 fwd(std::sin(iconAngle), -std::cos(iconAngle));
    const glm::vec2 right(-fwd.y, fwd.x);
    const glm::vec2 center(pCenterX + 0.5f, pCenterY + 0.5f);
    const glm::vec2 tip = center + fwd * 6.8f;
    const glm::vec2 baseL = center - fwd * 3.6f - right * 4.4f;
    const glm::vec2 baseR = center - fwd * 3.6f + right * 4.4f;
    drawFilledTri(tip + glm::vec2(0.9f, 1.1f), baseL + glm::vec2(0.9f, 1.1f),
                  baseR + glm::vec2(0.9f, 1.1f), 0.0f, 0.0f, 0.0f, 0.36f);
    drawFilledTri(tip, baseL, baseR, 0.98f, 0.30f, 0.22f, 0.98f);
    ui_.drawRect(center.x - 1.2f, center.y - 1.2f, 2.4f, 2.4f, 1.0f, 0.92f, 0.84f, 0.96f);

    if (showWaypoints) {
        for (const auto &wp : map.waypoints()) {
            if (!wp.visible) {
                continue;
            }
            float dx = static_cast<float>(wp.x - playerWX);
            float dz = static_cast<float>(wp.z - playerWZ);
            if (!northLocked) {
                const float c = std::cos(headingRad);
                const float s = std::sin(headingRad);
                const float sx = dx * c + dz * s;
                const float sz = -dx * s + dz * c;
                dx = sx;
                dz = sz;
            }
            float px = pCenterX + dx * cell;
            float py = pCenterY + dz * cell;
            const float m = 5.0f;
            const float minX = gridX + m;
            const float maxX = gridX + gridW - m;
            const float minY = gridY + m;
            const float maxY = gridY + gridH - m;
            const bool clipped = (px < minX || px > maxX || py < minY || py > maxY);
            px = std::clamp(px, minX, maxX);
            py = std::clamp(py, minY, maxY);
            const float rr = static_cast<float>(wp.r) / 255.0f;
            const float rg = static_cast<float>(wp.g) / 255.0f;
            const float rb = static_cast<float>(wp.b) / 255.0f;
            drawWaypointShape(px + 0.6f, py + 0.8f, 7.0f, static_cast<int>(wp.icon), 0.0f, 0.0f,
                              0.0f, clipped ? 0.30f : 0.40f);
            drawWaypointShape(px, py, clipped ? 5.5f : 6.5f, static_cast<int>(wp.icon), rr, rg, rb,
                              clipped ? 0.82f : 0.96f);
        }
    }

    if (showCompass) {
        const float compassR = std::min(gridW, gridH) * 0.44f;
        const struct Dir { const char *label; float angle; } dirs[4] = {
            {"N", 0.0f}, {"E", 1.5707964f}, {"S", 3.1415927f}, {"W", 4.7123890f}};
        for (const auto &d : dirs) {
            const float a = northLocked ? d.angle : (d.angle - headingRad);
            const float cx = pCenterX + std::sin(a) * compassR;
            const float cy = pCenterY - std::cos(a) * compassR;
            const float lw = UiMenuRenderer::textWidthPx(d.label) + 3.0f;
            const float lh = 10.0f;
            const float lx = cx - lw * 0.5f;
            const float ly = cy - lh * 0.5f;
            ui_.drawRect(lx, ly, lw, lh, 0.02f, 0.03f, 0.04f, 0.84f);
            if (d.label[0] == 'N') {
                ui_.drawText(lx + (lw - UiMenuRenderer::textWidthPx(d.label)) * 0.5f + 0.5f, ly + 1.5f,
                             d.label, 246, 118, 98, 252);
            } else {
                ui_.drawText(lx + (lw - UiMenuRenderer::textWidthPx(d.label)) * 0.5f + 0.5f, ly + 1.5f,
                             d.label, 228, 236, 248, 250);
            }
        }
    }

    const float btnH = 16.0f;
    const float btnY = panelY + panelH - btnH - 7.0f;
    const float compassW = 22.0f;
    const float followW = 22.0f;
    const float waypointW = 22.0f;
    const float minusW = 24.0f;
    const float plusW = 24.0f;
    const float gap = 4.0f;
    const float totalW = compassW + followW + waypointW + minusW + plusW + gap * 4.0f;
    const float btnX0 = panelX + panelW - totalW - 8.0f;
    const float disabledMul = 0.45f;
    ui_.drawRect(btnX0, btnY, compassW, btnH, 0.09f, 0.11f, 0.14f, 0.90f);
    ui_.drawRect(btnX0 + 1.0f, btnY + 1.0f, compassW - 2.0f, btnH - 2.0f, 0.16f, 0.18f, 0.23f, 0.90f);
    const float ccx = btnX0 + compassW * 0.5f;
    const float ccy = btnY + btnH * 0.5f;
    const float cm = showCompass ? 1.0f : disabledMul;
    ui_.drawRect(ccx - 5.0f, ccy - 5.0f, 10.0f, 10.0f, 0.88f * cm, 0.92f * cm, 0.98f * cm, 0.94f);
    ui_.drawRect(ccx - 4.0f, ccy - 4.0f, 8.0f, 8.0f, 0.07f * cm, 0.10f * cm, 0.14f * cm, 0.95f);
    ui_.drawRect(ccx - 0.5f, ccy - 4.0f, 1.0f, 1.6f, 0.92f * cm, 0.95f * cm, 0.99f * cm, 0.95f);
    ui_.drawRect(ccx - 0.5f, ccy + 2.4f, 1.0f, 1.6f, 0.92f * cm, 0.95f * cm, 0.99f * cm, 0.95f);
    ui_.drawRect(ccx - 4.0f, ccy - 0.5f, 1.6f, 1.0f, 0.92f * cm, 0.95f * cm, 0.99f * cm, 0.95f);
    ui_.drawRect(ccx + 2.4f, ccy - 0.5f, 1.6f, 1.0f, 0.92f * cm, 0.95f * cm, 0.99f * cm, 0.95f);
    ui_.drawRect(ccx - 0.8f, ccy - 3.4f, 1.6f, 2.4f, 0.95f * cm, 0.36f * cm, 0.34f * cm, 0.98f);
    ui_.drawRect(ccx - 0.4f, ccy - 2.0f, 0.8f, 4.0f, 0.88f * cm, 0.90f * cm, 0.95f * cm, 0.95f);
    ui_.drawRect(ccx - 0.7f, ccy - 0.7f, 1.4f, 1.4f, 0.95f * cm, 0.95f * cm, 0.99f * cm, 0.98f);

    const float followX = btnX0 + compassW + gap;
    ui_.drawRect(followX, btnY, followW, btnH, 0.09f, 0.11f, 0.14f, 0.90f);
    ui_.drawRect(followX + 1.0f, btnY + 1.0f, followW - 2.0f, btnH - 2.0f, 0.16f, 0.18f, 0.23f, 0.90f);
    const float lcx = followX + followW * 0.5f;
    const float lcy = btnY + btnH * 0.5f;
    if (northLocked) {
        ui_.drawRect(lcx - 3.0f, lcy - 4.0f, 6.0f, 1.8f, 0.88f, 0.92f, 0.99f, 0.98f);
        ui_.drawRect(lcx - 2.0f, lcy - 2.5f, 1.2f, 1.4f, 0.88f, 0.92f, 0.99f, 0.98f);
        ui_.drawRect(lcx + 0.8f, lcy - 2.5f, 1.2f, 1.4f, 0.88f, 0.92f, 0.99f, 0.98f);
    } else {
        ui_.drawRect(lcx - 3.0f, lcy - 4.0f, 5.0f, 1.8f, 0.88f, 0.92f, 0.99f, 0.98f);
        ui_.drawRect(lcx - 2.0f, lcy - 2.5f, 1.2f, 1.4f, 0.88f, 0.92f, 0.99f, 0.98f);
    }
    ui_.drawRect(lcx - 4.0f, lcy - 1.0f, 8.0f, 6.0f, northLocked ? 0.38f : 0.28f,
                 northLocked ? 0.68f : 0.44f, northLocked ? 0.94f : 0.62f, 0.96f);
    ui_.drawRect(lcx - 0.7f, lcy + 1.0f, 1.4f, 2.5f, 0.92f, 0.95f, 0.99f, 0.96f);

    const float waypointX = followX + followW + gap;
    ui_.drawRect(waypointX, btnY, waypointW, btnH, 0.09f, 0.11f, 0.14f, 0.90f);
    ui_.drawRect(waypointX + 1.0f, btnY + 1.0f, waypointW - 2.0f, btnH - 2.0f, 0.16f, 0.18f, 0.23f, 0.90f);
    const float wpx = waypointX + waypointW * 0.5f;
    const float wpy = btnY + btnH * 0.5f;
    const float wm = showWaypoints ? 1.0f : disabledMul;
    ui_.drawRect(wpx - 3.6f, wpy - 4.4f, 1.2f, 8.8f, 0.92f * wm, 0.95f * wm, 0.99f * wm, 0.96f);
    ui_.drawRect(wpx - 2.4f, wpy - 4.0f, 5.4f, 3.4f, 0.94f * wm, 0.36f * wm, 0.34f * wm, 0.96f);
    ui_.drawRect(wpx - 2.4f, wpy - 0.8f, 3.9f, 1.0f, 0.94f * wm, 0.36f * wm, 0.34f * wm, 0.96f);

    const float minusX = waypointX + waypointW + gap;
    ui_.drawRect(minusX, btnY, minusW, btnH, 0.12f, 0.15f, 0.20f, 0.94f);
    ui_.drawRect(minusX + 1.0f, btnY + 1.0f, minusW - 2.0f, btnH - 2.0f, 0.20f, 0.24f, 0.32f, 0.92f);
    ui_.drawText(minusX + (minusW - UiMenuRenderer::textWidthPx("-")) * 0.5f, btnY + 4.0f, "-", 240, 244, 252, 255);
    const float plusX = minusX + minusW + gap;
    ui_.drawRect(plusX, btnY, plusW, btnH, 0.12f, 0.15f, 0.20f, 0.94f);
    ui_.drawRect(plusX + 1.0f, btnY + 1.0f, plusW - 2.0f, btnH - 2.0f, 0.20f, 0.24f, 0.32f, 0.92f);
    ui_.drawText(plusX + (plusW - UiMenuRenderer::textWidthPx("+")) * 0.5f, btnY + 4.0f, "+", 240, 244, 252, 255);
    const std::string zoomText = std::to_string(static_cast<int>(std::round(zoom * 100.0f))) + "%";
    ui_.drawText(panelX + 10.0f, btnY + 5.0f, zoomText, 208, 216, 232, 255);

    ui_.end();
}

} // namespace app::menus
