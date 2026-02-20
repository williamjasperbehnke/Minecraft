#pragma once

#include <string>
#include <vector>

struct GLFWwindow;

namespace world {
struct WorldDebugStats;
}

namespace game {

enum class RenderMode {
    Textured = 0,
    Flat = 1,
    Wireframe = 2,
};

struct DebugConfig {
    float fov = 75.0f;
    float moveSpeed = 15.0f;
    float mouseSensitivity = 0.1f;
    float raycastDistance = 8.0f;
    int loadRadius = 8;
    int unloadRadius = 10;
    RenderMode renderMode = RenderMode::Textured;
    bool showChunkBorders = false;
    bool overrideTime = false;
    bool smoothLighting = true;
    bool showClouds = true;
    bool showStars = true;
    bool showFog = false;
    float timeOfDay01 = 0.25f;
    float moonPhase01 = 0.0f;
};

class DebugMenu {
  public:
    void update(GLFWwindow *window, DebugConfig &cfg, const world::WorldDebugStats &stats,
                float fps, float frameMs);
    void render(int width, int height);
    void updateWindowTitle(GLFWwindow *window, const DebugConfig &cfg,
                           const world::WorldDebugStats &stats, float fps, float frameMs) const;

    bool isOpen() const {
        return open_;
    }

  private:
    struct Rect {
        float x;
        float y;
        float w;
        float h;
        bool contains(float px, float py) const {
            return px >= x && px <= (x + w) && py >= y && py <= (y + h);
        }
    };

    bool keyPressed(GLFWwindow *window, int key);
    void printMenuHint() const;
    void drawRect(float x, float y, float w, float h, float r, float g, float b, float a);
    void drawText(float x, float y, const std::string &text, unsigned char r, unsigned char g,
                  unsigned char b, unsigned char a);
    void initRenderer();
    void applyClick(float mx, float my, DebugConfig &cfg);
    bool hitStepButton(float mx, float my, int &idx, int &dir) const;
    void adjustControl(DebugConfig &cfg, int idx, int dir) const;
    static const char *renderModeName(RenderMode mode);

    bool open_ = false;
    std::vector<int> keysDown_;
    bool prevMouseLeft_ = false;
    bool draggingTimeSlider_ = false;
    bool draggingMoonSlider_ = false;
    bool draggingScrollThumb_ = false;
    int heldButtonRow_ = -1;
    int heldButtonDir_ = 0;
    double nextButtonRepeatTime_ = 0.0;
    float scrollGrabOffsetY_ = 0.0f;
    float scrollOffset_ = 0.0f;
    float contentHeight_ = 0.0f;
    float viewHeight_ = 0.0f;
    float mouseX_ = 0.0f;
    float mouseY_ = 0.0f;
    float panelX_ = 20.0f;
    float panelY_ = 20.0f;
    float panelW_ = 620.0f;
    float panelH_ = 560.0f;

    std::vector<float> rowY_;
    std::vector<int> visibleRows_;
    std::vector<std::string> infoLines_;
    DebugConfig lastCfg_{};
    int selectedTab_ = 0;

    bool rendererReady_ = false;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    unsigned int shader_ = 0;

    struct UiVertex {
        float x;
        float y;
        float r;
        float g;
        float b;
        float a;
    };
    std::vector<UiVertex> verts_;
};

} // namespace game
