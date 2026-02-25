#pragma once

#include "app/menus/WorldSelectionMenu.hpp"

struct GLFWwindow;
struct GLFWcursor;

namespace gfx {
class Shader;
class TextureAtlas;
class HudRenderer;
} // namespace gfx

namespace game {
class MapSystem;
class Camera;
class DebugMenu;
class DebugConfig;
} // namespace game

namespace voxel {
class BlockRegistry;
} // namespace voxel

namespace world {
struct WorldDebugStats;
} // namespace world

namespace app::menus {
class PauseMenu;
class CraftingMenu;
class FurnaceMenu;
class RecipeMenu;
class CreativeMenu;
class WorldMapMenu;
class MiniMapMenu;
} // namespace app::menus

namespace app {

class GameSession {
  public:
    GameSession(GLFWwindow *window, GLFWcursor *arrowCursor,
                const menus::WorldSelection &worldSelection, gfx::Shader &shader,
                gfx::TextureAtlas &atlas, gfx::HudRenderer &hud, menus::PauseMenu &pauseMenu,
                menus::CraftingMenu &craftingMenu, menus::FurnaceMenu &furnaceMenu,
                menus::RecipeMenu &recipeMenu, menus::CreativeMenu &creativeMenu,
                menus::WorldMapMenu &worldMapMenu, menus::MiniMapMenu &miniMapMenu);

    bool run();

  private:
    GLFWwindow *window_;
    GLFWcursor *arrowCursor_;
    const menus::WorldSelection &worldSelection_;
    gfx::Shader &shader_;
    gfx::TextureAtlas &atlas_;
    gfx::HudRenderer &hud_;
    menus::PauseMenu &pauseMenu_;
    menus::CraftingMenu &craftingMenu_;
    menus::FurnaceMenu &furnaceMenu_;
    menus::RecipeMenu &recipeMenu_;
    menus::CreativeMenu &creativeMenu_;
    menus::WorldMapMenu &worldMapMenu_;
    menus::MiniMapMenu &miniMapMenu_;
};

} // namespace app
