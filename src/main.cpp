#include "app/GameSession.hpp"
#include "app/menus/CreativeMenu.hpp"
#include "app/menus/CraftingMenu.hpp"
#include "app/menus/FurnaceMenu.hpp"
#include "app/menus/MiniMapMenu.hpp"
#include "app/menus/WorldMapMenu.hpp"
#include "app/menus/PauseMenu.hpp"
#include "app/menus/RecipeMenu.hpp"
#include "app/menus/WorldSelectionMenu.hpp"
#include "app/menus/TextInputMenu.hpp"
#include "core/Logger.hpp"
#include "game/Camera.hpp"
#include "game/CraftingSystem.hpp"
#include "game/DebugMenu.hpp"
#include "game/GameRules.hpp"
#include "game/Inventory.hpp"
#include "game/ItemDropSystem.hpp"
#include "game/MapSystem.hpp"
#include "game/Player.hpp"
#include "game/SmeltingSystem.hpp"
#include "game/AudioSystem.hpp"
#include "gfx/HudRenderer.hpp"
#include "gfx/Shader.hpp"
#include "gfx/TextureAtlas.hpp"
#include "voxel/Block.hpp"
#include "voxel/Raycaster.hpp"
#include "world/World.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "app/Util.hpp"

namespace {

struct AppWindowContext {
    GLFWwindow *window = nullptr;
    GLFWcursor *arrowCursor = nullptr;
};

void onFramebufferResize(GLFWwindow * /*window*/, int width, int height) {
    glViewport(0, 0, width, height);
}

void onMouseScroll(GLFWwindow * /*window*/, double /*xoffset*/, double yoffset) {
    gRecipeMenuScrollDelta += static_cast<float>(yoffset);
}

bool initializeWindowContext(AppWindowContext &ctx) {
    if (!glfwInit()) {
        core::Logger::instance().error("Failed to initialize GLFW");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    ctx.window = glfwCreateWindow(1280, 720, "Voxel Clone", nullptr, nullptr);
    if (ctx.window == nullptr) {
        core::Logger::instance().error("Failed to create window");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(ctx.window);
    ctx.arrowCursor = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    glfwSetFramebufferSizeCallback(ctx.window, onFramebufferResize);
    glfwSetScrollCallback(ctx.window, onMouseScroll);
    glfwSetInputMode(ctx.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    glfwSetCursor(ctx.window, ctx.arrowCursor);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        core::Logger::instance().error("Failed to initialize glad");
        glfwDestroyWindow(ctx.window);
        ctx.window = nullptr;
        glfwTerminate();
        return false;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return true;
}

void shutdownWindowContext(AppWindowContext &ctx) {
    if (ctx.window != nullptr) {
        glfwDestroyWindow(ctx.window);
        ctx.window = nullptr;
    }
    if (ctx.arrowCursor != nullptr) {
        glfwDestroyCursor(ctx.arrowCursor);
        ctx.arrowCursor = nullptr;
    }
    glfwTerminate();
}

} // namespace

int main() {
    AppWindowContext appWindow;
    try {
        std::srand(static_cast<unsigned int>(std::time(nullptr)));
        if (!initializeWindowContext(appWindow)) {
            return 1;
        }
        GLFWwindow *window = appWindow.window;
        GLFWcursor *arrowCursor = appWindow.arrowCursor;

        gfx::Shader shader("shaders/chunk.vert", "shaders/chunk.frag");
        gfx::TextureAtlas atlas("assets/textures/atlas.png", 16, 16);
        gfx::HudRenderer hud;
        app::menus::WorldSelectionMenu worldSelectionMenu;
        app::menus::PauseMenu pauseMenu;
        app::menus::CraftingMenu craftingMenu;
        app::menus::FurnaceMenu furnaceMenu;
        app::menus::RecipeMenu recipeMenu;
        app::menus::CreativeMenu creativeMenu;
        app::menus::WorldMapMenu worldMapMenu;
        app::menus::MiniMapMenu miniMapMenu;
        bool appRunning = true;
        while (appRunning && !glfwWindowShouldClose(window)) {
            const app::menus::WorldSelection worldSelection = worldSelectionMenu.run(window, hud);
            if (!worldSelection.start) {
                appRunning = false;
                break;
            }
            glfwSetWindowTitle(window, ("Voxel Clone - " + worldSelection.name).c_str());
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            glfwSetCursor(window, nullptr);
            app::GameSession session(window, arrowCursor, worldSelection, shader, atlas, hud,
                                     pauseMenu, craftingMenu, furnaceMenu, recipeMenu,
                                     creativeMenu, worldMapMenu, miniMapMenu);
            const bool returnToTitle = session.run();
            if (returnToTitle && !glfwWindowShouldClose(window)) {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                glfwSetCursor(window, arrowCursor);
                continue;
            }
            appRunning = false;
        }
        shutdownWindowContext(appWindow);
        return 0;
    } catch (const std::exception &e) {
        shutdownWindowContext(appWindow);
        core::Logger::instance().error(e.what());
        return 1;
    }
}
