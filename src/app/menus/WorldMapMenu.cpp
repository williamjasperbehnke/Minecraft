#include "app/menus/WorldMapMenu.hpp"

#include "game/GameRules.hpp"
#include "gfx/HudRenderer.hpp"
#include "gfx/TextureAtlas.hpp"
#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"

#include <algorithm>
#include <cmath>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace app::menus {
namespace {

glm::vec3 mapColorForBlock(voxel::BlockId id, const voxel::BlockRegistry &registry,
                           const gfx::TextureAtlas &atlas) {
    if (voxel::isWaterLike(id)) {
        return {0.18f, 0.44f, 0.86f};
    }
    if (id == voxel::SEAGRASS) {
        return {0.22f, 0.64f, 0.34f};
    }
    if (id == voxel::KELP) {
        return {0.16f, 0.56f, 0.26f};
    }
    if (id == voxel::CORAL) {
        return {0.94f, 0.58f, 0.40f};
    }
    if (voxel::isLavaLike(id)) {
        return {0.94f, 0.39f, 0.14f};
    }
    if (voxel::isPlant(id) || id == voxel::LEAVES || id == voxel::SPRUCE_LEAVES ||
        id == voxel::BIRCH_LEAVES || id == voxel::MOSS) {
        return {0.24f, 0.64f, 0.30f};
    }
    const voxel::BlockDef &def = registry.get(id);
    glm::vec3 c = atlas.tileAverageColor(def.topTile);
    if (id == voxel::SAND || id == voxel::RED_SAND || id == voxel::SANDSTONE) {
        c = glm::mix(c, glm::vec3(0.90f, 0.82f, 0.52f), 0.45f);
    } else if (id == voxel::SNOW_BLOCK || id == voxel::ICE) {
        c = glm::mix(c, glm::vec3(0.86f, 0.94f, 1.00f), 0.42f);
    } else if (id == voxel::STONE || id == voxel::BASALT || id == voxel::BEDROCK) {
        c = glm::mix(c, glm::vec3(0.46f, 0.50f, 0.54f), 0.30f);
    }
    return c;
}

} // namespace

MapOverlayLayout WorldMapMenu::computeLayout(int width, int height, float zoom) const {
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float panelW = std::max(320.0f, std::min(w - 80.0f, w * 0.86f));
    const float panelH = std::max(260.0f, std::min(h - 60.0f, h * 0.82f));
    const float panelX = (w - panelW) * 0.5f;
    const float panelY = (h - panelH) * 0.5f;
    const float innerPad = 14.0f;
    const float gridX = panelX + innerPad;
    const float gridY = panelY + innerPad + 18.0f;
    const float gridW = panelW - innerPad * 2.0f;
    const float gridH = panelH - innerPad * 2.0f - 24.0f;
    const float cell = std::clamp(4.0f * zoom, 2.0f, 14.0f);
    const float chunkBtnW = 94.0f;
    const float chunkBtnH = 18.0f;
    const float chunkBtnX = panelX + panelW - 298.0f;
    const float chunkBtnY = panelY + 6.0f;
    return {panelX, panelY, panelW, panelH, gridX, gridY, gridW, gridH, cell,
            chunkBtnX, chunkBtnY, chunkBtnW, chunkBtnH};
}

WaypointEditorLayout WorldMapMenu::computeWaypointEditorLayout(const MapOverlayLayout &mapLayout) const {
    const float panelW = 236.0f;
    const float panelH = 96.0f;
    const float panelX = mapLayout.panelX + (mapLayout.panelW - panelW) * 0.5f;
    const float panelY = mapLayout.panelY + (mapLayout.panelH - panelH) * 0.5f;
    const float nameX = panelX + 8.0f;
    const float nameY = panelY + 32.0f;
    const float nameW = panelW - 16.0f;
    const float nameH = 20.0f;
    const float colorX = panelX + 8.0f;
    const float colorY = panelY + 58.0f;
    const float colorS = 16.0f;
    const float colorGap = 6.0f;
    const float iconX = panelX + 118.0f;
    const float iconY = colorY;
    const float iconS = 18.0f;
    const float iconGap = 5.0f;
    const float closeS = 16.0f;
    const float closeX = panelX + panelW - closeS - 6.0f;
    const float closeY = panelY + 6.0f;
    const float visibilityW = 16.0f;
    const float visibilityH = 16.0f;
    const float delW = 54.0f;
    const float delH = 18.0f;
    const float visibilityX = closeX - 4.0f - visibilityW;
    const float delY = panelY + 6.0f;
    const float delX = visibilityX - 4.0f - delW;
    const float visibilityY = panelY + 7.0f;
    return {panelX, panelY, panelW, panelH, nameX, nameY, nameW, nameH, colorX, colorY,
            colorS, colorGap, iconX, iconY, iconS, iconGap, closeX, closeY, closeS, delX, delY,
            delW, delH, visibilityX, visibilityY, visibilityW, visibilityH};
}

void WorldMapMenu::render(gfx::HudRenderer &hud, int width, int height, const game::MapSystem &map,
                          int mapCenterWX, int mapCenterWZ, int playerWX, int playerWZ,
                          float zoom, float cursorX, float cursorY, int selectedWaypoint,
                          const std::string &waypointName, std::uint8_t waypointR,
                          std::uint8_t waypointG, std::uint8_t waypointB, int waypointIcon,
                          bool waypointVisible, float playerHeadingRad, bool waypointNameFocused,
                          bool waypointEditorOpen, bool showChunkBorders,
                          const voxel::BlockRegistry &registry,
                          const gfx::TextureAtlas &atlas) const {
    (void)hud;
    ui_.begin(width, height);

    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float panelW = std::max(320.0f, std::min(w - 80.0f, w * 0.86f));
    const float panelH = std::max(260.0f, std::min(h - 60.0f, h * 0.82f));
    const float panelX = (w - panelW) * 0.5f;
    const float panelY = (h - panelH) * 0.5f;
    const float innerPad = 14.0f;
    const float gridX = panelX + innerPad;
    const float gridY = panelY + innerPad + 18.0f;
    const float gridW = panelW - innerPad * 2.0f;
    const float gridH = panelH - innerPad * 2.0f - 24.0f;
    const float cell = std::clamp(4.0f * zoom, 2.0f, 14.0f);
    const float chunkBtnW = 94.0f;
    const float chunkBtnH = 18.0f;
    const float chunkBtnX = panelX + panelW - 298.0f;
    const float chunkBtnY = panelY + 6.0f;
    const int drawCols = std::max(1, static_cast<int>(std::ceil(gridW / cell)));
    const int drawRows = std::max(1, static_cast<int>(std::ceil(gridH / cell)));
    const float centerIx = (static_cast<float>(drawCols) - 1.0f) * 0.5f;
    const float centerIz = (static_cast<float>(drawRows) - 1.0f) * 0.5f;

    ui_.drawRect(0.0f, 0.0f, w, h, 0.03f, 0.04f, 0.05f, 0.62f);
    ui_.drawRect(panelX, panelY, panelW, panelH, 0.05f, 0.07f, 0.10f, 0.92f);
    ui_.drawRect(panelX + 2.0f, panelY + 2.0f, panelW - 4.0f, panelH - 4.0f, 0.10f, 0.12f, 0.16f,
                 0.92f);
    ui_.drawRect(gridX, gridY, gridW, gridH, 0.08f, 0.10f, 0.12f, 0.94f);
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
                ui_.drawRect(cx - wrow * 0.5f, topY + static_cast<float>(row), wrow, 1.0f, r, g, b,
                             a);
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
    auto drawWaypointIcon = [&](float cx, float cy, float size, int icon, float r, float g,
                                float b, float a, bool selected) {
        const float drawSize = selected ? (size + 4.0f) : size;
        drawWaypointShape(cx + 0.9f, cy + 1.1f, drawSize + 1.0f, icon, 0.0f, 0.0f, 0.0f, 0.22f);
        drawWaypointShape(cx + 0.6f, cy + 0.8f, drawSize + 0.4f, icon, 0.0f, 0.0f, 0.0f, 0.34f);
        drawWaypointShape(cx, cy, drawSize + 0.9f, icon, r, g, b, 0.40f);
        drawWaypointShape(cx, cy, drawSize, icon, r, g, b, a);
    };

    for (int iz = 0; iz < drawRows; ++iz) {
        for (int ix = 0; ix < drawCols; ++ix) {
            const int dx = static_cast<int>(std::floor(static_cast<float>(ix) - centerIx));
            const int dz = static_cast<int>(std::floor(static_cast<float>(iz) - centerIz));
            voxel::BlockId id = voxel::AIR;
            if (!map.sample(mapCenterWX + dx, mapCenterWZ + dz, id)) {
                continue;
            }
            glm::vec3 c = mapColorForBlock(id, registry, atlas);
            int y = voxel::Chunk::SY / 2;
            if (map.sampleHeight(mapCenterWX + dx, mapCenterWZ + dz, y)) {
                int yX1 = y;
                int yX0 = y;
                int yZ1 = y;
                int yZ0 = y;
                (void)map.sampleHeight(mapCenterWX + dx + 1, mapCenterWZ + dz, yX1);
                (void)map.sampleHeight(mapCenterWX + dx - 1, mapCenterWZ + dz, yX0);
                (void)map.sampleHeight(mapCenterWX + dx, mapCenterWZ + dz + 1, yZ1);
                (void)map.sampleHeight(mapCenterWX + dx, mapCenterWZ + dz - 1, yZ0);
                const float slope = static_cast<float>((yX1 - yX0) - (yZ1 - yZ0));
                const float shade = std::clamp(0.84f + slope * 0.025f, 0.68f, 1.16f);
                const float altitude = std::clamp(0.90f + (static_cast<float>(y) / 127.0f) * 0.22f,
                                                  0.88f, 1.16f);
                c *= shade * altitude;
            }
            bool waterCovered = false;
            if (map.sampleWaterCover(mapCenterWX + dx, mapCenterWZ + dz, waterCovered) &&
                waterCovered) {
                const glm::vec3 waterTint(0.22f, 0.46f, 0.86f);
                const float tintMix = voxel::isWaterloggedPlant(id) ? 0.10f
                                     : (voxel::isPlant(id) ? 0.22f : 0.44f);
                c = glm::mix(c, waterTint, tintMix);
            }
            c = glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
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
    // Chunk grid overlay and hovered chunk highlight.
    auto floorMod = [](int a, int b) {
        const int m = a % b;
        return (m < 0) ? (m + b) : m;
    };
    auto floorDiv = [](int a, int b) {
        int q = a / b;
        const int r = a % b;
        if (r != 0 && ((r > 0) != (b > 0))) {
            --q;
        }
        return q;
    };
    if (showChunkBorders && cell >= 2.0f) {
        for (int ix = 0; ix < drawCols; ++ix) {
            const int dx = static_cast<int>(std::floor(static_cast<float>(ix) - centerIx));
            const int wx = mapCenterWX + dx;
            if (floorMod(wx, voxel::Chunk::SX) != 0) {
                continue;
            }
            const float px = gridX + static_cast<float>(ix) * cell;
            ui_.drawRect(px, gridY, 1.0f, gridH, 0.18f, 0.22f, 0.30f, 0.28f);
        }
        for (int iz = 0; iz < drawRows; ++iz) {
            const int dz = static_cast<int>(std::floor(static_cast<float>(iz) - centerIz));
            const int wz = mapCenterWZ + dz;
            if (floorMod(wz, voxel::Chunk::SZ) != 0) {
                continue;
            }
            const float py = gridY + static_cast<float>(iz) * cell;
            ui_.drawRect(gridX, py, gridW, 1.0f, 0.18f, 0.22f, 0.30f, 0.28f);
        }
        if (cursorX >= gridX && cursorX <= (gridX + gridW) && cursorY >= gridY &&
            cursorY <= (gridY + gridH)) {
            const int gx = static_cast<int>(std::floor((cursorX - gridX) / cell));
            const int gz = static_cast<int>(std::floor((cursorY - gridY) / cell));
            const int dx = static_cast<int>(std::floor(static_cast<float>(gx) - centerIx));
            const int dz = static_cast<int>(std::floor(static_cast<float>(gz) - centerIz));
            const int wx = mapCenterWX + dx;
            const int wz = mapCenterWZ + dz;
            const int cX = floorDiv(wx, voxel::Chunk::SX);
            const int cZ = floorDiv(wz, voxel::Chunk::SZ);
            int minIx = drawCols;
            int maxIx = -1;
            for (int ix = 0; ix < drawCols; ++ix) {
                const int cdx = static_cast<int>(std::floor(static_cast<float>(ix) - centerIx));
                const int cwx = mapCenterWX + cdx;
                if (floorDiv(cwx, voxel::Chunk::SX) != cX) {
                    continue;
                }
                minIx = std::min(minIx, ix);
                maxIx = std::max(maxIx, ix);
            }
            int minIz = drawRows;
            int maxIz = -1;
            for (int iz = 0; iz < drawRows; ++iz) {
                const int cdz = static_cast<int>(std::floor(static_cast<float>(iz) - centerIz));
                const int cwz = mapCenterWZ + cdz;
                if (floorDiv(cwz, voxel::Chunk::SZ) != cZ) {
                    continue;
                }
                minIz = std::min(minIz, iz);
                maxIz = std::max(maxIz, iz);
            }
            if (minIx <= maxIx && minIz <= maxIz) {
                const float minX = gridX + static_cast<float>(minIx) * cell;
                const float maxX = gridX + static_cast<float>(maxIx + 1) * cell;
                const float minY = gridY + static_cast<float>(minIz) * cell;
                const float maxY = gridY + static_cast<float>(maxIz + 1) * cell;
                const float hx = std::floor(std::clamp(minX, gridX, gridX + gridW));
                const float hy = std::floor(std::clamp(minY, gridY, gridY + gridH));
                const float hw = std::ceil(std::clamp(maxX, gridX, gridX + gridW)) - hx;
                const float hh = std::ceil(std::clamp(maxY, gridY, gridY + gridH)) - hy;
                if (hw > 1.0f && hh > 1.0f) {
                    ui_.drawRect(hx, hy, hw, 1.0f, 0.98f, 0.92f, 0.30f, 0.95f);
                    ui_.drawRect(hx, hy + hh - 1.0f, hw, 1.0f, 0.98f, 0.92f, 0.30f, 0.95f);
                    ui_.drawRect(hx, hy, 1.0f, hh, 0.98f, 0.92f, 0.30f, 0.95f);
                    ui_.drawRect(hx + hw - 1.0f, hy, 1.0f, hh, 0.98f, 0.92f, 0.30f, 0.95f);
                }
            }
        }
    }

    int hoveredWaypoint = -1;
    float hoveredWaypointPx = 9999.0f;
    for (std::size_t i = 0; i < map.waypoints().size(); ++i) {
        const auto &wp = map.waypoints()[i];
        const float px = gridX + (static_cast<float>(wp.x - mapCenterWX) + centerIx) * cell;
        const float py = gridY + (static_cast<float>(wp.z - mapCenterWZ) + centerIz) * cell;
        if (px < gridX - 10.0f || py < gridY - 10.0f || px > gridX + gridW + 10.0f ||
            py > gridY + gridH + 10.0f) {
            continue;
        }
        const float rr = static_cast<float>(wp.r) / 255.0f;
        const float rg = static_cast<float>(wp.g) / 255.0f;
        const float rb = static_cast<float>(wp.b) / 255.0f;
        const bool selected = (selectedWaypoint >= 0 && static_cast<std::size_t>(selectedWaypoint) == i);
        drawWaypointIcon(px + 0.5f, py + 0.5f, 9.0f, static_cast<int>(wp.icon), rr, rg, rb, 0.98f,
                         selected);
        const float dx = cursorX - (px + 0.5f);
        const float dy = cursorY - (py + 0.5f);
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d < 9.0f && d < hoveredWaypointPx) {
            hoveredWaypointPx = d;
            hoveredWaypoint = static_cast<int>(i);
        }
    }

    const float pCenterX = gridX + centerIx * cell + static_cast<float>(playerWX - mapCenterWX) * cell;
    const float pCenterY = gridY + centerIz * cell + static_cast<float>(playerWZ - mapCenterWZ) * cell;
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
    const float pDrawX = std::clamp(pCenterX, gridX + 7.0f, gridX + gridW - 7.0f);
    const float pDrawY = std::clamp(pCenterY, gridY + 7.0f, gridY + gridH - 7.0f);
    const float iconAngle = playerHeadingRad;
    const glm::vec2 fwd(std::sin(iconAngle), -std::cos(iconAngle));
    const glm::vec2 right(-fwd.y, fwd.x);
    const glm::vec2 center(pDrawX + 0.5f, pDrawY + 0.5f);
    const glm::vec2 tip = center + fwd * 7.6f;
    const glm::vec2 baseL = center - fwd * 3.8f - right * 4.8f;
    const glm::vec2 baseR = center - fwd * 3.8f + right * 4.8f;
    drawFilledTri(tip + glm::vec2(1.0f, 1.2f), baseL + glm::vec2(1.0f, 1.2f),
                  baseR + glm::vec2(1.0f, 1.2f), 0.0f, 0.0f, 0.0f, 0.34f);
    drawFilledTri(tip, baseL, baseR, 0.98f, 0.30f, 0.22f, 0.98f);
    ui_.drawRect(center.x - 1.3f, center.y - 1.3f, 2.6f, 2.6f, 1.0f, 0.92f, 0.84f, 0.96f);
    const float playerHoverDx = cursorX - pDrawX;
    const float playerHoverDy = cursorY - pDrawY;
    const bool hoverPlayer = std::sqrt(playerHoverDx * playerHoverDx + playerHoverDy * playerHoverDy) < 9.0f;

    ui_.drawText(panelX + 10.0f, panelY + 10.0f, "World Map", 240, 244, 252, 255);
    const std::string zoomText = "Zoom: " + std::to_string(static_cast<int>(std::round(zoom * 100.0f))) + "%";
    ui_.drawText(panelX + 96.0f, panelY + 10.0f, zoomText, 208, 216, 232, 255);
    ui_.drawRect(chunkBtnX, chunkBtnY, chunkBtnW, chunkBtnH, 0.09f, 0.11f, 0.14f, 0.95f);
    ui_.drawRect(chunkBtnX + 1.0f, chunkBtnY + 1.0f, chunkBtnW - 2.0f, chunkBtnH - 2.0f,
                 showChunkBorders ? 0.22f : 0.14f, showChunkBorders ? 0.34f : 0.20f,
                 showChunkBorders ? 0.56f : 0.28f, 0.98f);
    ui_.drawRect(chunkBtnX + 6.0f, chunkBtnY + 5.0f, 9.0f, 1.0f, 0.92f, 0.95f, 0.99f, 0.95f);
    ui_.drawRect(chunkBtnX + 6.0f, chunkBtnY + 8.0f, 9.0f, 1.0f, 0.92f, 0.95f, 0.99f, 0.95f);
    ui_.drawRect(chunkBtnX + 6.0f, chunkBtnY + 11.0f, 9.0f, 1.0f, 0.92f, 0.95f, 0.99f, 0.95f);
    ui_.drawRect(chunkBtnX + 6.0f, chunkBtnY + 5.0f, 1.0f, 7.0f, 0.92f, 0.95f, 0.99f, 0.95f);
    ui_.drawRect(chunkBtnX + 10.0f, chunkBtnY + 5.0f, 1.0f, 7.0f, 0.92f, 0.95f, 0.99f, 0.95f);
    ui_.drawRect(chunkBtnX + 14.0f, chunkBtnY + 5.0f, 1.0f, 7.0f, 0.92f, 0.95f, 0.99f, 0.95f);
    ui_.drawText(chunkBtnX + 20.0f, chunkBtnY + 5.0f, showChunkBorders ? "Chunks On" : "Chunks Off",
                 showChunkBorders ? 236 : 210, showChunkBorders ? 242 : 218,
                 showChunkBorders ? 252 : 226, 255);
    ui_.drawText(panelX + panelW - 196.0f, panelY + 10.0f, "M: Close  +/-: Zoom", 192, 200, 214, 255);

    if (waypointEditorOpen) {
        const float editorW = 236.0f;
        const float editorH = 96.0f;
        const float editorX = panelX + (panelW - editorW) * 0.5f;
        const float editorY = panelY + (panelH - editorH) * 0.5f;
        ui_.drawRect(editorX, editorY, editorW, editorH, 0.04f, 0.06f, 0.08f, 0.95f);
        ui_.drawRect(editorX + 1.0f, editorY + 1.0f, editorW - 2.0f, editorH - 2.0f, 0.10f, 0.13f,
                     0.18f, 0.98f);
        ui_.drawText(editorX + 8.0f, editorY + 8.0f, "Waypoint", 226, 232, 246, 255);
        ui_.drawRect(editorX + editorW - 22.0f, editorY + 6.0f, 16.0f, 16.0f, 0.16f, 0.18f, 0.24f, 0.95f);
        ui_.drawText(editorX + editorW - 17.0f, editorY + 10.0f, "X", 240, 128, 128, 255);
        const float nameX = editorX + 8.0f;
        const float nameY = editorY + 32.0f;
        const float nameW = editorW - 16.0f;
        const float nameH = 20.0f;
        ui_.drawRect(nameX, nameY, nameW, nameH, waypointNameFocused ? 0.24f : 0.12f,
                     waypointNameFocused ? 0.38f : 0.18f, waypointNameFocused ? 0.62f : 0.27f, 0.98f);
        ui_.drawRect(nameX + 1.0f, nameY + 1.0f, nameW - 2.0f, nameH - 2.0f, 0.08f, 0.10f, 0.14f, 0.98f);
        const std::string shownName = waypointName.empty() ? std::string("(name)") : waypointName;
        ui_.drawText(nameX + 6.0f, nameY + 6.0f, shownName, waypointName.empty() ? 154 : 232,
                     waypointName.empty() ? 164 : 236, waypointName.empty() ? 182 : 245, 255);
        const float swY = editorY + 58.0f;
        const float swS = 16.0f;
        const float swGap = 6.0f;
        const glm::vec3 palette[5] = {{1.00f, 0.38f, 0.38f}, {0.36f, 0.82f, 1.00f}, {0.40f, 0.92f, 0.42f},
                                      {0.96f, 0.90f, 0.34f}, {0.92f, 0.52f, 0.96f}};
        for (int i = 0; i < 5; ++i) {
            const float sx = editorX + 8.0f + static_cast<float>(i) * (swS + swGap);
            const bool selectedColor =
                (std::abs(static_cast<int>(waypointR) - static_cast<int>(palette[i].r * 255.0f)) <= 8 &&
                 std::abs(static_cast<int>(waypointG) - static_cast<int>(palette[i].g * 255.0f)) <= 8 &&
                 std::abs(static_cast<int>(waypointB) - static_cast<int>(palette[i].b * 255.0f)) <= 8);
            if (selectedColor) {
                ui_.drawRect(sx - 1.0f, swY - 1.0f, swS + 2.0f, swS + 2.0f, 0.36f, 0.56f, 0.88f, 0.95f);
                ui_.drawRect(sx, swY, swS, swS, 0.12f, 0.20f, 0.34f, 0.88f);
            }
            ui_.drawRect(sx, swY, swS, swS, palette[i].r, palette[i].g, palette[i].b, 0.98f);
        }
        const float iconX0 = editorX + 118.0f;
        const float iconY = swY;
        const float iconW = 18.0f;
        const float iconGap = 5.0f;
        const float closeS = 16.0f;
        const float closeX = editorX + editorW - closeS - 6.0f;
        const float eyeW = 18.0f;
        const float eyeH = 18.0f;
        const float eyeX = closeX - 4.0f - eyeW;
        const float eyeY = editorY + 5.0f;
        const float delW = 54.0f;
        const float delH = 18.0f;
        const float delX = eyeX - 4.0f - delW;
        const float delY = editorY + 5.0f;
        ui_.drawRect(delX, delY, delW, delH, 0.36f, 0.10f, 0.10f, 0.95f);
        ui_.drawRect(delX + 1.0f, delY + 1.0f, delW - 2.0f, delH - 2.0f, 0.58f, 0.14f, 0.14f, 0.95f);
        const std::string delText = "Delete";
        ui_.drawText(delX + (delW - UiMenuRenderer::textWidthPx(delText)) * 0.5f,
                     delY + (delH - 8.0f) * 0.5f + 1.0f, delText, 246, 230, 230, 255);
        for (int i = 0; i < 5; ++i) {
            const float bx = iconX0 + static_cast<float>(i) * (iconW + iconGap);
            const bool active = (std::clamp(waypointIcon, 0, 4) == i);
            if (active) {
                ui_.drawRect(bx - 1.0f, iconY - 1.0f, iconW + 2.0f, iconW + 2.0f, 0.36f, 0.56f, 0.88f, 0.95f);
                ui_.drawRect(bx, iconY, iconW, iconW, 0.12f, 0.20f, 0.34f, 0.88f);
            }
            drawWaypointShape(bx + iconW * 0.5f, iconY + iconW * 0.5f, iconW - 5.0f, i, 0.92f, 0.95f, 0.99f, 1.0f);
        }
        ui_.drawRect(eyeX, eyeY, eyeW, eyeH, 0.10f, 0.12f, 0.16f, 0.94f);
        ui_.drawRect(eyeX + 1.0f, eyeY + 1.0f, eyeW - 2.0f, eyeH - 2.0f, waypointVisible ? 0.22f : 0.14f,
                     waypointVisible ? 0.34f : 0.20f, waypointVisible ? 0.56f : 0.26f, 0.95f);
        const float cxx = eyeX + eyeW * 0.5f;
        const float cyy = eyeY + eyeH * 0.5f;
        ui_.drawRect(cxx - 4.5f, cyy - 1.0f, 9.0f, 2.0f, 0.90f, 0.94f, 0.99f, 0.96f);
        ui_.drawRect(cxx - 3.5f, cyy - 2.0f, 7.0f, 1.0f, 0.90f, 0.94f, 0.99f, 0.96f);
        ui_.drawRect(cxx - 3.5f, cyy + 1.0f, 7.0f, 1.0f, 0.90f, 0.94f, 0.99f, 0.96f);
        if (waypointVisible) {
            ui_.drawRect(cxx - 1.5f, cyy - 1.5f, 3.0f, 3.0f, 0.24f, 0.56f, 0.92f, 0.98f);
        } else {
            for (int i = -4; i <= 4; ++i) {
                ui_.drawRect(cxx + static_cast<float>(i) * 0.85f - 0.55f,
                             cyy + static_cast<float>(i) * 0.85f - 0.55f, 1.1f, 1.1f, 0.92f, 0.34f,
                             0.32f, 0.98f);
            }
        }
    }

    if (!waypointEditorOpen) {
        if (hoveredWaypoint >= 0 && hoveredWaypoint < static_cast<int>(map.waypoints().size())) {
            const auto &wp = map.waypoints()[hoveredWaypoint];
            const std::string tipt = wp.name.empty() ? std::string("Waypoint") : wp.name;
            const float rr = static_cast<float>(wp.r) / 255.0f;
            const float rg = static_cast<float>(wp.g) / 255.0f;
            const float rb = static_cast<float>(wp.b) / 255.0f;
            const float padX = 7.0f;
            const float tipH = 16.0f;
            const float tipW = UiMenuRenderer::textWidthPx(tipt) + padX * 2.0f;
            const float tipX = std::clamp(cursorX + 12.0f, 4.0f, w - tipW - 4.0f);
            const float tipY = std::clamp(cursorY - 8.0f, 4.0f, h - tipH - 4.0f);
            ui_.drawRect(tipX, tipY, tipW, tipH, rr * 0.45f, rg * 0.45f, rb * 0.45f, 0.94f);
            ui_.drawRect(tipX + 1.0f, tipY + 1.0f, tipW - 2.0f, tipH - 2.0f, rr * 0.78f, rg * 0.78f,
                         rb * 0.78f, 0.96f);
            ui_.drawText(tipX + (tipW - UiMenuRenderer::textWidthPx(tipt)) * 0.5f, tipY + 4.0f, tipt,
                         245, 248, 252, 255);
        } else if (hoverPlayer) {
            const std::string tipt = "Player";
            const float padX = 7.0f;
            const float tipH = 16.0f;
            const float tipW = UiMenuRenderer::textWidthPx(tipt) + padX * 2.0f;
            const float tipX = std::clamp(cursorX + 12.0f, 4.0f, w - tipW - 4.0f);
            const float tipY = std::clamp(cursorY - 8.0f, 4.0f, h - tipH - 4.0f);
            ui_.drawRect(tipX, tipY, tipW, tipH, 0.10f, 0.18f, 0.34f, 0.94f);
            ui_.drawRect(tipX + 1.0f, tipY + 1.0f, tipW - 2.0f, tipH - 2.0f, 0.16f, 0.28f, 0.50f, 0.96f);
            ui_.drawText(tipX + (tipW - UiMenuRenderer::textWidthPx(tipt)) * 0.5f, tipY + 4.0f, tipt,
                         245, 248, 252, 255);
        } else if (cursorX >= gridX && cursorX <= (gridX + gridW) && cursorY >= gridY && cursorY <= (gridY + gridH)) {
            const int gx = static_cast<int>(std::floor((cursorX - gridX) / cell));
            const int gz = static_cast<int>(std::floor((cursorY - gridY) / cell));
            const int dx = static_cast<int>(std::floor(static_cast<float>(gx) - centerIx));
            const int dz = static_cast<int>(std::floor(static_cast<float>(gz) - centerIz));
            voxel::BlockId hoverId = voxel::AIR;
            if (map.sample(mapCenterWX + dx, mapCenterWZ + dz, hoverId)) {
                const std::string tipt = game::blockName(hoverId);
                const float padX = 7.0f;
                const float tipH = 16.0f;
                const float tipW = UiMenuRenderer::textWidthPx(tipt) + padX * 2.0f;
                const float tipX = std::clamp(cursorX + 12.0f, 4.0f, w - tipW - 4.0f);
                const float tipY = std::clamp(cursorY - 8.0f, 4.0f, h - tipH - 4.0f);
                ui_.drawRect(tipX, tipY, tipW, tipH, 0.04f, 0.06f, 0.08f, 0.92f);
                ui_.drawRect(tipX + 1.0f, tipY + 1.0f, tipW - 2.0f, tipH - 2.0f, 0.11f, 0.14f, 0.19f, 0.94f);
                ui_.drawText(tipX + (tipW - UiMenuRenderer::textWidthPx(tipt)) * 0.5f, tipY + 4.0f, tipt,
                             228, 234, 246, 255);
            }
        }
    }

    ui_.end();
}

} // namespace app::menus
