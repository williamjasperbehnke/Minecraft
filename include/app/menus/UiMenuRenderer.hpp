#pragma once

#include <string>
#include <vector>

namespace app::menus {

class UiMenuRenderer {
  public:
    void begin(int width, int height);
    void drawRect(float x, float y, float w, float h, float r, float g, float b, float a);
    void drawText(float x, float y, const std::string &text, unsigned char r, unsigned char g,
                  unsigned char b, unsigned char a);
    void end();

    static float textWidthPx(const std::string &text);

  private:
    struct UiVertex {
        float x;
        float y;
        float r;
        float g;
        float b;
        float a;
    };

    void init();

    bool ready_ = false;
    unsigned int shader_ = 0;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    int width_ = 1;
    int height_ = 1;
    std::vector<UiVertex> verts_;
};

} // namespace app::menus
