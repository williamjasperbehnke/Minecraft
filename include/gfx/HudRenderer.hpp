#pragma once

#include "game/CraftingSystem.hpp"
#include "game/Inventory.hpp"
#include "game/SmeltingSystem.hpp"
#include "gfx/TextureAtlas.hpp"
#include "voxel/Block.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace gfx {

class HudRenderer {
  public:
    void renderTitleScreen(int width, int height, const std::vector<std::string> &worlds,
                           int selectedWorld, bool createMode, bool editSeed,
                           const std::string &createName, const std::string &createSeed,
                           float cursorX, float cursorY);
    void renderPauseMenu(int width, int height, float cursorX, float cursorY);

    void render2D(int width, int height, int selectedIndex,
                  const std::array<voxel::BlockId, game::Inventory::kHotbarSize> &hotbar,
                  const std::array<int, game::Inventory::kHotbarSize> &hotbarCounts,
                  const std::array<voxel::BlockId, game::Inventory::kSlotCount> &allIds,
                  const std::array<int, game::Inventory::kSlotCount> &allCounts, bool showInventory,
                  voxel::BlockId carryingId, int carryingCount, float cursorX, float cursorY,
                  int hoveredSlotIndex, float hudScale,
                  const std::array<game::Inventory::Slot, game::CraftingSystem::kInputCount>
                      &craftInput,
                  int craftingGridSize, bool usingCraftingTable, bool usingFurnace,
                  const game::Inventory::Slot &smeltInput,
                  const game::Inventory::Slot &smeltFuel,
                  const game::Inventory::Slot &smeltOutput, float smeltProgress01,
                  float smeltFuel01,
                  bool showRecipeMenu, const std::vector<game::CraftingSystem::RecipeInfo> &recipes,
                  const std::vector<game::SmeltingSystem::Recipe> &smeltingRecipes,
                  const std::vector<bool> &recipeCraftable,
                  float recipeScroll, float uiTimeSeconds, const std::string &recipeSearch,
                  bool showCreativeMenu, const std::vector<voxel::BlockId> &creativeItems,
                  float creativeScroll, const std::string &creativeSearch,
                  bool recipeCraftableOnly,
                  const std::optional<voxel::BlockId> &recipeIngredientFilter,
                  const game::Inventory::Slot &craftOutput,
                  const std::string &carryingName, const std::string &selectedName,
                  const std::string &lookedAtText, const std::string &modeText,
                  const std::string &compassText, const std::string &coordText,
                  const voxel::BlockRegistry &registry, const TextureAtlas &atlas);

    void renderBreakOverlay(const glm::mat4 &proj, const glm::mat4 &view,
                            const std::optional<glm::ivec3> &block, float breakProgress,
                            voxel::BlockId blockId, const TextureAtlas &atlas);

    void renderBlockOutline(const glm::mat4 &proj, const glm::mat4 &view,
                            const std::optional<glm::ivec3> &block, float breakProgress = 0.0f);

  private:
    struct UiVertex {
        float x;
        float y;
        float r;
        float g;
        float b;
        float a;
    };

    void init2D();
    void initIcons();
    void initLine();
    void initCrack();
    void drawRect(float x, float y, float w, float h, float r, float g, float b, float a);
    void drawText(float x, float y, const std::string &text, unsigned char r, unsigned char g,
                  unsigned char b, unsigned char a);
    unsigned int uiShader_ = 0;
    unsigned int uiVao_ = 0;
    unsigned int uiVbo_ = 0;
    bool uiReady_ = false;

    unsigned int lineShader_ = 0;
    unsigned int lineVao_ = 0;
    unsigned int lineVbo_ = 0;
    bool lineReady_ = false;

    unsigned int crackShader_ = 0;
    unsigned int crackVao_ = 0;
    unsigned int crackVbo_ = 0;
    bool crackReady_ = false;

    unsigned int iconShader_ = 0;
    unsigned int iconVao_ = 0;
    unsigned int iconVbo_ = 0;
    bool iconReady_ = false;

    struct IconVertex {
        float x;
        float y;
        float u;
        float v;
        float light;
    };
    std::vector<IconVertex> iconVerts_;

    std::vector<UiVertex> verts_;
};

} // namespace gfx
