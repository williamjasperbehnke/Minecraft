#pragma once

#include "voxel/Block.hpp"

#include <functional>
#include <string>

namespace app::menus {

struct SlotLabel {
    float x = 0.0f;
    float y = 0.0f;
    std::string text;
};

struct RecipeNameLabel {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    std::string text;
};

struct TooltipState {
    std::string text;
    float x = 0.0f;
    float y = 0.0f;
};

struct DragIconState {
    bool active = false;
    voxel::BlockId id = voxel::AIR;
    int count = 0;
    float x = 0.0f;
    float y = 0.0f;
    float size = 0.0f;
    std::string name;
};

struct HudDrawContext {
    std::function<void(float, float, float, float, float, float, float, float)> drawRect;
    std::function<void(float, float, const std::string &, unsigned char, unsigned char,
                       unsigned char, unsigned char)>
        drawText;
    std::function<void(float, float, float)> drawHoverOutline;
    std::function<void(float, float, float)> drawValidHintOutline;
    std::function<void(float, float, float)> drawSlotFrame;
    std::function<void(float, float, float, float, float, float, float, float, float, float, float,
                       float)>
        drawRectClipped;
    std::function<void(float, float, const std::string &, float, float, float, float, unsigned char,
                       unsigned char, unsigned char, unsigned char)>
        drawTextClipped;
    std::function<void(float, float, float, float, voxel::BlockId, float)> appendItemIcon;
    std::function<void(float, float, float, float, const voxel::BlockDef &)> appendCubeIcon;
    std::function<std::size_t(float, float, float, float)> beginIconClipBatch;
    std::function<void(std::size_t)> endIconClipBatch;
    std::function<float(const std::string &)> textWidthPx;
    std::function<bool(voxel::BlockId)> isFlatItemId;
};

} // namespace app::menus
