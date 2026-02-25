#include "app/GameSession.hpp"
#include "app/SaveManager.hpp"

#include "app/menus/CreativeMenu.hpp"
#include "app/menus/CraftingMenu.hpp"
#include "app/menus/FurnaceMenu.hpp"
#include "app/menus/MiniMapMenu.hpp"
#include "app/menus/WorldMapMenu.hpp"
#include "app/menus/PauseMenu.hpp"
#include "app/menus/RecipeMenu.hpp"
#include "app/menus/TextInputMenu.hpp"
#include "core/Logger.hpp"
#include "game/AudioSystem.hpp"
#include "game/Camera.hpp"
#include "game/CraftingSystem.hpp"
#include "game/DebugMenu.hpp"
#include "game/GameRules.hpp"
#include "game/Inventory.hpp"
#include "game/ItemDropSystem.hpp"
#include "game/MapSystem.hpp"
#include "game/Player.hpp"
#include "game/SmeltingSystem.hpp"
#include "gfx/HudRenderer.hpp"
#include "gfx/ChunkBorderRenderer.hpp"
#include "gfx/Shader.hpp"
#include "gfx/SkyBodyRenderer.hpp"
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
#include <cstdint>
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

void updateFrameTiming(float now, float &lastTime, float &fpsAccumSeconds, int &fpsAccumFrames,
                       float &fpsAvgDisplay, float &fpsOut, float &dtOut) {
    dtOut = now - lastTime;
    lastTime = now;
    fpsOut = (dtOut > 0.0f) ? (1.0f / dtOut) : 0.0f;
    fpsAccumSeconds += std::max(0.0f, dtOut);
    ++fpsAccumFrames;
    if (fpsAccumSeconds >= 1.0f) {
        fpsAvgDisplay = (fpsAccumSeconds > 0.0f)
                            ? (static_cast<float>(fpsAccumFrames) / fpsAccumSeconds)
                            : 0.0f;
        fpsAccumSeconds = 0.0f;
        fpsAccumFrames = 0;
    } else if (fpsAvgDisplay <= 0.0f) {
        fpsAvgDisplay = fpsOut;
    }
}

void syncCursorAndLook(GLFWwindow *window, GLFWcursor *arrowCursor, game::Camera &camera,
                       bool blockInput, bool &recaptureMouseAfterInventoryClose) {
    const bool wantCursorNormal = blockInput;
    const int currentCursorMode = glfwGetInputMode(window, GLFW_CURSOR);
    const int desiredCursorMode = wantCursorNormal ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED;
    if (currentCursorMode != desiredCursorMode) {
        glfwSetInputMode(window, GLFW_CURSOR, desiredCursorMode);
        glfwSetCursor(window, wantCursorNormal ? arrowCursor : nullptr);
        if (desiredCursorMode == GLFW_CURSOR_DISABLED) {
            camera.resetMouseLook(window);
        }
    }
    bool justRecapturedMouse = false;
    if (recaptureMouseAfterInventoryClose && !blockInput) {
        camera.resetMouseLook(window);
        recaptureMouseAfterInventoryClose = false;
        justRecapturedMouse = true;
    }
    if (!blockInput && !justRecapturedMouse) {
        camera.handleMouse(window);
    }
}

void maybeUpdateWindowTitle(GLFWwindow *window, game::DebugMenu &debugMenu,
                            const game::DebugConfig &debugCfg, const world::WorldDebugStats &stats,
                            float fps, float dt, float &titleAccum) {
    titleAccum += dt;
    if (titleAccum >= 0.2f) {
        debugMenu.updateWindowTitle(window, debugCfg, stats, fps, dt * 1000.0f);
        titleAccum = 0.0f;
    }
}

bool handlePauseMenuInput(GLFWwindow *window, app::menus::PauseMenu &pauseMenu, bool left,
                          bool prevLeft, bool &pauseMenuOpen, bool &returnToTitle,
                          const std::function<void()> &saveCurrentPlayer) {
    if (!pauseMenuOpen) {
        return false;
    }
    int winW = 1;
    int winH = 1;
    glfwGetWindowSize(window, &winW, &winH);
    double mx = 0.0;
    double my = 0.0;
    glfwGetCursorPos(window, &mx, &my);
    if (!(left && !prevLeft)) {
        return false;
    }
    const int button = pauseMenu.buttonAtCursor(mx, my, winW, winH);
    if (button == 1) {
        pauseMenuOpen = false;
    } else if (button == 2) {
        saveCurrentPlayer();
    } else if (button == 3) {
        saveCurrentPlayer();
        returnToTitle = true;
        return true;
    } else if (button == 4) {
        saveCurrentPlayer();
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
    return false;
}

void renderOverlayMenus(GLFWwindow *window, gfx::HudRenderer &hud, int winW, int winH,
                        app::menus::PauseMenu &pauseMenu, bool pauseMenuOpen,
                        app::menus::WorldMapMenu &worldMapMenu,
                        app::menus::MiniMapMenu &miniMapMenu, bool mapOpen, bool hudVisible,
                        game::MapSystem &mapSystem, const game::Camera &camera, float mapCenterWX,
                        float mapCenterWZ, float mapZoom, int selectedWaypointIndex,
                        bool waypointNameFocused, bool waypointEditorOpen, float miniMapZoom,
                        bool miniMapNorthLocked, bool miniMapShowCompass, bool miniMapShowWaypoints,
                        voxel::BlockRegistry &hudRegistry, gfx::TextureAtlas &atlas,
                        game::DebugMenu &debugMenu) {
    if (pauseMenuOpen) {
        double pmx = 0.0;
        double pmy = 0.0;
        glfwGetCursorPos(window, &pmx, &pmy);
        pauseMenu.render(hud, winW, winH, static_cast<float>(pmx), static_cast<float>(pmy));
    }
    if (mapOpen) {
        const glm::vec3 pos = camera.position();
        const int mapWX = static_cast<int>(std::floor(pos.x));
        const int mapWZ = static_cast<int>(std::floor(pos.z));
        double mapCursorX = 0.0;
        double mapCursorY = 0.0;
        glfwGetCursorPos(window, &mapCursorX, &mapCursorY);
        std::string waypointName;
        std::uint8_t waypointR = 255;
        std::uint8_t waypointG = 96;
        std::uint8_t waypointB = 96;
        int waypointIcon = 0;
        bool waypointVisible = true;
        if (selectedWaypointIndex >= 0 &&
            selectedWaypointIndex < static_cast<int>(mapSystem.waypoints().size())) {
            const auto &wp = mapSystem.waypoints()[selectedWaypointIndex];
            waypointName = wp.name;
            waypointR = wp.r;
            waypointG = wp.g;
            waypointB = wp.b;
            waypointIcon = static_cast<int>(wp.icon);
            waypointVisible = wp.visible;
        }
        worldMapMenu.render(hud, winW, winH, mapSystem, static_cast<int>(std::round(mapCenterWX)),
                            static_cast<int>(std::round(mapCenterWZ)), mapWX, mapWZ, mapZoom,
                            static_cast<float>(mapCursorX), static_cast<float>(mapCursorY),
                            selectedWaypointIndex, waypointName, waypointR, waypointG, waypointB,
                            waypointIcon, waypointVisible,
                            std::atan2(camera.forward().x, -camera.forward().z),
                            waypointNameFocused, waypointEditorOpen, hudRegistry, atlas);
    } else if (hudVisible) {
        const glm::vec3 pos = camera.position();
        const int mapWX = static_cast<int>(std::floor(pos.x));
        const int mapWZ = static_cast<int>(std::floor(pos.z));
        const glm::vec3 fwd = camera.forward();
        const float miniMapHeadingRad = std::atan2(fwd.x, -fwd.z);
        miniMapMenu.render(hud, winW, winH, mapSystem, mapWX, mapWZ, miniMapZoom,
                           miniMapNorthLocked, miniMapShowCompass, miniMapShowWaypoints,
                           miniMapHeadingRad, hudRegistry, atlas);
    }
    debugMenu.render(winW, winH);
    glfwSwapBuffers(window);
}

} // namespace

namespace app {

GameSession::GameSession(GLFWwindow *window, GLFWcursor *arrowCursor,
                         const menus::WorldSelection &worldSelection, gfx::Shader &shader,
                         gfx::TextureAtlas &atlas, gfx::HudRenderer &hud,
                         menus::PauseMenu &pauseMenu, menus::CraftingMenu &craftingMenu,
                         menus::FurnaceMenu &furnaceMenu, menus::RecipeMenu &recipeMenu,
                         menus::CreativeMenu &creativeMenu, menus::WorldMapMenu &worldMapMenu,
                         menus::MiniMapMenu &miniMapMenu)
    : window_(window), arrowCursor_(arrowCursor), worldSelection_(worldSelection), shader_(shader),
      atlas_(atlas), hud_(hud), pauseMenu_(pauseMenu), craftingMenu_(craftingMenu),
      furnaceMenu_(furnaceMenu), recipeMenu_(recipeMenu), creativeMenu_(creativeMenu),
      worldMapMenu_(worldMapMenu), miniMapMenu_(miniMapMenu) {}

bool GameSession::run() {
    GLFWwindow *window = window_;
    GLFWcursor *arrowCursor = arrowCursor_;
    const auto &worldSelection = worldSelection_;
    auto &shader = shader_;
    auto &atlas = atlas_;
    auto &hud = hud_;
    auto &pauseMenu = pauseMenu_;
    auto &craftingMenu = craftingMenu_;
    auto &furnaceMenu = furnaceMenu_;
    auto &recipeMenu = recipeMenu_;
    auto &creativeMenu = creativeMenu_;
    auto &worldMapMenu = worldMapMenu_;
    auto &miniMapMenu = miniMapMenu_;
    world::World world(atlas, worldSelection.path, worldSelection.seed);
    game::Camera camera(glm::vec3(8.0f, 80.0f, 8.0f));
    game::DebugMenu debugMenu;
    game::ItemDropSystem itemDrops;
    game::MapSystem mapSystem;
    game::AudioSystem audio;
    voxel::BlockRegistry hudRegistry;
    audio.loadDefaultAssets();

    game::DebugConfig debugCfg;
    debugCfg.moveSpeed = camera.moveSpeed();
    debugCfg.mouseSensitivity = camera.mouseSensitivity();
    bool lastSmoothLighting = debugCfg.smoothLighting;

    bool prevLeft = false;
    bool prevRight = false;
    bool prevHudToggle = false;
    bool prevModeToggle = false;
    bool prevTextureReloadToggle = false;
    bool prevInventoryToggle = false;
    bool prevMapToggle = false;
    bool prevMapZoomInToggle = false;
    bool prevMapZoomOutToggle = false;
    bool prevPauseToggle = false;
    bool hudVisible = true;
    bool inventoryVisible = false;
    bool mapOpen = false;
    float mapZoom = 1.0f;
    float miniMapZoom = 1.0f;
    bool miniMapNorthLocked = true;
    bool miniMapShowCompass = true;
    bool miniMapShowWaypoints = true;
    int selectedWaypointIndex = -1;
    bool waypointNameFocused = false;
    bool waypointEditorOpen = false;
    bool prevWaypointBackspace = false;
    bool prevWaypointEnter = false;
    bool prevWaypointEscape = false;
    bool prevWaypointDelete = false;
    bool waypointEditHasBackup = false;
    bool waypointEditWasNew = false;
    int waypointEditBackupIndex = -1;
    game::MapSystem::Waypoint waypointEditBackup{};
    float mapCenterWX = 0.0f;
    float mapCenterWZ = 0.0f;
    bool mapDragActive = false;
    float mapDragLastMouseX = 0.0f;
    float mapDragLastMouseY = 0.0f;
    bool pauseMenuOpen = false;
    bool ghostMode = true;
    game::Player player;
    game::Inventory inventory;
    game::CraftingSystem craftingSystem;
    game::CraftingSystem::State crafting;
    game::SmeltingSystem smeltingSystem;
    game::SmeltingSystem::State smelting;
    std::optional<glm::ivec3> activeFurnaceCell;
    float recipeMenuScroll = 0.0f;
    std::string recipeSearchText;
    float creativeMenuScroll = 0.0f;
    std::string creativeSearchText;
    std::optional<voxel::BlockId> recipeIngredientFilter;
    bool recipeCraftableOnly = false;
    int craftingGridSize = game::CraftingSystem::kGridSizeInventory;
    bool usingCraftingTable = false;
    bool usingFurnace = false;
    game::Inventory::Slot carriedSlot{};
    std::array<bool, game::Inventory::kHotbarSize> prevBlockKeys{};
    int selectedBlockIndex = 0;
    glm::vec3 loadedCameraPos = camera.position();
    bool pendingSpawnResolve = false;
    if (SaveManager::loadPlayerData(worldSelection.path, loadedCameraPos, selectedBlockIndex,
                                    ghostMode, inventory, smelting)) {
        camera.setPosition(loadedCameraPos);
        if (!ghostMode) {
            pendingSpawnResolve = true;
        }
    }
    mapSystem.load(worldSelection.path);
    mapCenterWX = camera.position().x;
    mapCenterWZ = camera.position().z;
    auto persistActiveFurnaceState = [&]() {
        SaveManager::persistFurnaceState(world, activeFurnaceCell, smelting);
    };
    auto loadActiveFurnaceState = [&]() {
        SaveManager::loadFurnaceState(world, activeFurnaceCell, smelting);
    };
    auto saveCurrentPlayer = [&]() {
        persistActiveFurnaceState();
        mapSystem.save(worldSelection.path);
        SaveManager::savePlayerData(worldSelection.path, camera.position(), selectedBlockIndex,
                                    ghostMode, inventory, smelting);
    };
    camera.resetMouseLook(window);
    bool prevInventoryLeft = false;
    bool prevInventoryRight = false;
    bool prevRecipeMenuToggle = false;
    bool prevRecipeIngredientFilterKey = false;
    bool prevDropKey = false;
    bool prevRecipeSearchBackspace = false;
    float lastInventoryLeftClickTime = -1.0f;
    voxel::BlockId lastInventoryLeftClickId = voxel::AIR;
    bool recaptureMouseAfterInventoryClose = false;
    bool recipeMenuVisible = false;
    bool creativeMenuVisible = false;
    bool recipeScrollDragging = false;
    bool creativeScrollDragging = false;
    float recipeScrollGrabOffsetY = 0.0f;
    float creativeScrollGrabOffsetY = 0.0f;
    bool recipeSearchFocused = false;
    bool creativeSearchFocused = false;
    int lastRecipeFillIndex = -1;
    bool prevCreativeMenuToggle = false;
    bool prevCreativeSearchBackspace = false;
    bool inventorySpreadDragging = false;
    std::vector<int> inventorySpreadSlots;
    game::Inventory::Slot inventorySpreadStartCarried{};
    std::vector<std::pair<int, game::Inventory::Slot>> inventorySpreadOriginalSlots;
    int inventorySpreadPickupSourceSlot = -1;
    bool inventoryRightSpreadDragging = false;
    std::vector<int> inventoryRightSpreadSlots;
    game::Inventory::Slot inventoryRightSpreadStartCarried{};
    std::vector<std::pair<int, game::Inventory::Slot>> inventoryRightSpreadOriginalSlots;

    float lastTime = static_cast<float>(glfwGetTime());
    float fpsAccumSeconds = 0.0f;
    int fpsAccumFrames = 0;
    float fpsAvgDisplay = 0.0f;
    float titleAccum = 0.0f;
    float dayClockSeconds = 120.0f;
    constexpr float kDayLengthSeconds = 900.0f;
    gfx::SkyBodyRenderer skyBodyRenderer;
    gfx::ChunkBorderRenderer chunkBorderRenderer;
    world::WorldDebugStats stats{};
    bool wasMenuOpen = false;
    std::optional<glm::ivec3> miningBlock;
    float miningProgress = 0.0f;
    float mineCooldown = 0.0f;
    glm::vec3 prevStepCamPos = camera.position();
    float stepDistanceAccum = 0.0f;
    float stepCooldown = 0.0f;
    float swimCooldown = 0.0f;
    float bobCooldown = 0.0f;

    bool returnToTitle = false;
    auto handleWorldInteractionAndMovement = [&](float dt, bool blockInput,
                                                 const std::optional<voxel::RayHit> &currentHit,
                                                 bool left, bool right) {
        if (!blockInput && left && currentHit.has_value()) {
            const glm::ivec3 target = currentHit->block;
            if (!miningBlock.has_value() || miningBlock.value() != target) {
                miningBlock = target;
                miningProgress = 0.0f;
            }
            const voxel::BlockId targetId = world.getBlock(target.x, target.y, target.z);
            if (targetId == voxel::BEDROCK) {
                miningBlock.reset();
                miningProgress = 0.0f;
            } else
            if (targetId != voxel::AIR && mineCooldown <= 0.0f) {
                miningProgress += dt / game::breakSeconds(targetId);
                if (miningProgress >= 1.0f) {
                    if (world.setBlock(target.x, target.y, target.z, voxel::AIR)) {
                        if (voxel::isFurnace(targetId)) {
                            world::FurnaceState wstate{};
                            if (world.getFurnaceState(target.x, target.y, target.z, wstate)) {
                                auto dropSmeltSlot = [&](const game::Inventory::Slot &slot) {
                                    if (slot.id != voxel::AIR && slot.count > 0) {
                                        itemDrops.spawn(slot.id,
                                                        glm::vec3(target) +
                                                            glm::vec3(0.5f, 0.2f, 0.5f),
                                                        slot.count);
                                    }
                                };
                                const game::SmeltingSystem::State gstate =
                                    SaveManager::fromWorldFurnaceState(wstate);
                                dropSmeltSlot(gstate.input);
                                dropSmeltSlot(gstate.fuel);
                                dropSmeltSlot(gstate.output);
                                world.clearFurnaceState(target.x, target.y, target.z);
                            }
                            if (activeFurnaceCell.has_value() && activeFurnaceCell->x == target.x &&
                                activeFurnaceCell->y == target.y &&
                                activeFurnaceCell->z == target.z) {
                                activeFurnaceCell.reset();
                                smelting = {};
                                usingFurnace = false;
                            }
                        }
                        glm::vec3 dropPos = glm::vec3(target) + glm::vec3(0.5f, 0.2f, 0.5f);
                        if (voxel::isPlant(targetId)) {
                            dropPos.y = static_cast<float>(target.y) + 0.02f;
                        }
                        const voxel::BlockId dropId =
                            voxel::isTorch(targetId)
                                ? voxel::TORCH
                                : (voxel::isFurnace(targetId) ? voxel::FURNACE : targetId);
                        itemDrops.spawn(dropId, dropPos, 1);
                        dropUnsupportedTorchesAround(world, itemDrops, target);
                        dropUnsupportedPlantsAround(world, itemDrops, target);
                        audio.playBreak(game::soundProfileForBlock(targetId));
                    }
                    miningProgress = 0.0f;
                    miningBlock.reset();
                    mineCooldown = 0.08f;
                }
            }
        } else {
            miningBlock.reset();
            miningProgress = 0.0f;
        }

        if (!blockInput && right && !prevRight && currentHit.has_value()) {
            const glm::ivec3 place = currentHit->block + currentHit->normal;
            const glm::vec3 camPos = camera.position();
            const glm::ivec3 camCell(static_cast<int>(std::floor(camPos.x)),
                                     static_cast<int>(std::floor(camPos.y)),
                                     static_cast<int>(std::floor(camPos.z)));
            const auto slot = inventory.hotbarSlot(selectedBlockIndex);
            if (slot.id != voxel::AIR && slot.count > 0) {
                bool placementAllowed = true;
                voxel::BlockId placeId = slot.id;

                if (slot.id == voxel::STICK || slot.id == voxel::IRON_INGOT ||
                    slot.id == voxel::COPPER_INGOT || slot.id == voxel::GOLD_INGOT) {
                    placementAllowed = false;
                }

                const bool placingCollisionSolid = isCollisionSolidPlacement(slot.id);
                if (placingCollisionSolid) {
                    const bool intersectsPlayer =
                        !ghostMode && placeCellIntersectsPlayer(place, camPos);
                    if (place == camCell || intersectsPlayer) {
                        placementAllowed = false;
                    }
                }

                if (placementAllowed && slot.id == voxel::TORCH) {
                    const glm::ivec3 normal = currentHit->normal;
                    placeId = torchIdForPlacementNormal(normal);
                    if (placeId == voxel::AIR) {
                        placementAllowed = false;
                    } else {
                        const glm::ivec3 support = place - normal;
                        if (!world.isSolidBlock(support.x, support.y, support.z)) {
                            placementAllowed = false;
                        }
                    }
                }
                if (placementAllowed && isSupportPlant(slot.id)) {
                    if (!world.isSolidBlock(place.x, place.y - 1, place.z)) {
                        placementAllowed = false;
                    }
                }
                if (placementAllowed && voxel::isWaterloggedPlant(slot.id)) {
                    if (world.getBlock(place.x, place.y, place.z) != voxel::WATER) {
                        placementAllowed = false;
                    }
                }

                if (placementAllowed && placeId == voxel::FURNACE) {
                    placeId = orientedFurnaceFromForward(camera.forward(), false);
                }

                if (placementAllowed && world.setBlock(place.x, place.y, place.z, placeId)) {
                    inventory.consumeHotbar(selectedBlockIndex, 1);
                    audio.playPlace(game::soundProfileForBlock(slot.id));
                }
            }
        }
        prevLeft = left;
        prevRight = right;
        const glm::vec3 camPos = camera.position();
        const glm::vec2 stepDelta(camPos.x - prevStepCamPos.x, camPos.z - prevStepCamPos.z);
        const float horizDist = glm::length(stepDelta);
        const float horizSpeed = horizDist / std::max(dt, 1e-4f);
        if (!ghostMode && !blockInput) {
            const bool groundedNow = player.grounded();
            const bool inWaterNow = player.inWater();

            if (groundedNow && !inWaterNow) {
                stepDistanceAccum += horizDist;
                stepCooldown = std::max(0.0f, stepCooldown - dt);
                if (horizSpeed < 0.22f) {
                    stepDistanceAccum = 0.0f;
                }
                const float movingThreshold = 0.70f;
                const float stride = (horizSpeed > 8.5f) ? 0.78f : 0.96f;
                while (horizSpeed > movingThreshold && stepDistanceAccum >= stride &&
                       stepCooldown <= 0.0f) {
                    const voxel::BlockId groundId = game::blockUnderPlayer(world, camPos);
                    audio.playFootstep(game::footstepProfileForGround(groundId));
                    stepDistanceAccum -= stride;
                    stepCooldown = 0.24f;
                }
            } else {
                stepDistanceAccum = 0.0f;
                stepCooldown = 0.0f;
            }

            if (inWaterNow) {
                swimCooldown = std::max(0.0f, swimCooldown - dt);
                bobCooldown = std::max(0.0f, bobCooldown - dt);
                if (horizSpeed > 0.55f && swimCooldown <= 0.0f) {
                    audio.playSwim();
                    swimCooldown = 0.90f;
                } else if (horizSpeed <= 0.55f && bobCooldown <= 0.0f) {
                    audio.playWaterBob();
                    bobCooldown = 1.20f;
                }
            } else {
                swimCooldown = std::min(swimCooldown, 0.08f);
                bobCooldown = 0.0f;
            }
        } else {
            stepDistanceAccum = 0.0f;
            stepCooldown = 0.0f;
            swimCooldown = 0.0f;
            bobCooldown = 0.0f;
        }
        prevStepCamPos = camPos;
    };

    auto handleMapAndWaypoints = [&](bool left, bool right, int currentCursorMode) {
        if (mapOpen) {
            int winW = 1;
            int winH = 1;
            glfwGetWindowSize(window, &winW, &winH);
            double mx = 0.0;
            double my = 0.0;
            glfwGetCursorPos(window, &mx, &my);
            auto closeWaypointEditor = [&]() {
                waypointEditorOpen = false;
                waypointNameFocused = false;
                prevWaypointEnter = false;
                prevWaypointEscape = false;
                app::menus::TextInputMenu::unbind(window);
            };
            auto cancelWaypointEdit = [&]() {
                if (!waypointEditorOpen) {
                    return;
                }
                if (selectedWaypointIndex >= 0 &&
                    selectedWaypointIndex < static_cast<int>(mapSystem.waypoints().size())) {
                    if (waypointEditWasNew) {
                        auto &wps = mapSystem.waypoints();
                        wps.erase(wps.begin() + selectedWaypointIndex);
                        selectedWaypointIndex = -1;
                    } else if (waypointEditHasBackup &&
                               waypointEditBackupIndex == selectedWaypointIndex) {
                        mapSystem.waypoints()[selectedWaypointIndex] = waypointEditBackup;
                    }
                }
                waypointEditHasBackup = false;
                waypointEditWasNew = false;
                waypointEditBackupIndex = -1;
                closeWaypointEditor();
            };
            auto commitWaypointEdit = [&]() {
                if (selectedWaypointIndex >= 0 &&
                    selectedWaypointIndex < static_cast<int>(mapSystem.waypoints().size())) {
                    auto &wp = mapSystem.waypoints()[selectedWaypointIndex];
                    if (wp.name.empty()) {
                        wp.name = "Waypoint";
                    }
                    wp.icon = static_cast<std::uint8_t>(wp.icon % 5u);
                }
                waypointEditHasBackup = false;
                waypointEditWasNew = false;
                waypointEditBackupIndex = -1;
                closeWaypointEditor();
            };
            const app::menus::MapOverlayLayout mapLayout =
                worldMapMenu.computeLayout(winW, winH, mapZoom);
            const app::menus::WaypointEditorLayout wpLayout =
                worldMapMenu.computeWaypointEditorLayout(mapLayout);
            const bool inMapGrid =
                mx >= mapLayout.gridX && mx <= (mapLayout.gridX + mapLayout.gridW) &&
                my >= mapLayout.gridY && my <= (mapLayout.gridY + mapLayout.gridH);
            int hoveredWaypointIndex = -1;
            float hoveredWaypointDist = 9999.0f;
            const int drawCols =
                std::max(1, static_cast<int>(std::ceil(mapLayout.gridW / mapLayout.cell)));
            const int drawRows =
                std::max(1, static_cast<int>(std::ceil(mapLayout.gridH / mapLayout.cell)));
            const float centerIx = (static_cast<float>(drawCols) - 1.0f) * 0.5f;
            const float centerIz = (static_cast<float>(drawRows) - 1.0f) * 0.5f;
            for (int i = 0; i < static_cast<int>(mapSystem.waypoints().size()); ++i) {
                const auto &wp = mapSystem.waypoints()[i];
                const float px =
                    mapLayout.gridX +
                    (static_cast<float>(wp.x) - mapCenterWX + centerIx) * mapLayout.cell;
                const float py =
                    mapLayout.gridY +
                    (static_cast<float>(wp.z) - mapCenterWZ + centerIz) * mapLayout.cell;
                const float dx = static_cast<float>(mx) - (px + 0.5f);
                const float dy = static_cast<float>(my) - (py + 0.5f);
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d < 13.0f && d < hoveredWaypointDist) {
                    hoveredWaypointDist = d;
                    hoveredWaypointIndex = i;
                }
            }
            const bool inWaypointEditor = waypointEditorOpen && mx >= wpLayout.panelX &&
                                          mx <= (wpLayout.panelX + wpLayout.panelW) &&
                                          my >= wpLayout.panelY &&
                                          my <= (wpLayout.panelY + wpLayout.panelH);
            if (right && !prevRight && inMapGrid) {
                const int gx =
                    static_cast<int>(std::floor((mx - mapLayout.gridX) / mapLayout.cell));
                const int gz =
                    static_cast<int>(std::floor((my - mapLayout.gridY) / mapLayout.cell));
                const int mapCenterWXInt = static_cast<int>(std::round(mapCenterWX));
                const int mapCenterWZInt = static_cast<int>(std::round(mapCenterWZ));
                const int wx = mapCenterWXInt +
                               static_cast<int>(std::floor(static_cast<float>(gx) - centerIx));
                const int wz = mapCenterWZInt +
                               static_cast<int>(std::floor(static_cast<float>(gz) - centerIz));

                selectedWaypointIndex = -1;
                int bestDist2 = 999999;
                auto &wps = mapSystem.waypoints();
                for (int i = 0; i < static_cast<int>(wps.size()); ++i) {
                    const int dx = wps[i].x - wx;
                    const int dz = wps[i].z - wz;
                    const int d2 = dx * dx + dz * dz;
                    if (d2 <= 4 && d2 < bestDist2) {
                        bestDist2 = d2;
                        selectedWaypointIndex = i;
                    }
                }
                if (selectedWaypointIndex < 0) {
                    game::MapSystem::Waypoint wp{};
                    wp.x = wx;
                    wp.z = wz;
                    wp.name = "Waypoint";
                    wp.r = 255;
                    wp.g = 96;
                    wp.b = 96;
                    wp.icon = 0;
                    mapSystem.waypoints().push_back(wp);
                    selectedWaypointIndex = static_cast<int>(mapSystem.waypoints().size()) - 1;
                    waypointEditWasNew = true;
                } else {
                    waypointEditWasNew = false;
                }
                if (selectedWaypointIndex >= 0 &&
                    selectedWaypointIndex < static_cast<int>(mapSystem.waypoints().size())) {
                    waypointEditHasBackup = true;
                    waypointEditBackupIndex = selectedWaypointIndex;
                    waypointEditBackup = mapSystem.waypoints()[selectedWaypointIndex];
                }
                waypointEditorOpen = true;
                waypointNameFocused = true;
                if (selectedWaypointIndex >= 0) {
                    app::menus::TextInputMenu::bind(
                        window, &mapSystem.waypoints()[selectedWaypointIndex].name);
                }
            }
            if (left && !prevLeft) {
                if (waypointEditorOpen && inWaypointEditor && selectedWaypointIndex >= 0 &&
                    selectedWaypointIndex < static_cast<int>(mapSystem.waypoints().size())) {
                    auto &wp = mapSystem.waypoints()[selectedWaypointIndex];
                    if (mx >= wpLayout.closeX && mx <= (wpLayout.closeX + wpLayout.closeS) &&
                        my >= wpLayout.closeY && my <= (wpLayout.closeY + wpLayout.closeS)) {
                        cancelWaypointEdit();
                    } else if (mx >= wpLayout.delX && mx <= (wpLayout.delX + wpLayout.delW) &&
                               my >= wpLayout.delY && my <= (wpLayout.delY + wpLayout.delH)) {
                        auto &wps = mapSystem.waypoints();
                        wps.erase(wps.begin() + selectedWaypointIndex);
                        selectedWaypointIndex = -1;
                        waypointEditHasBackup = false;
                        waypointEditWasNew = false;
                        waypointEditBackupIndex = -1;
                        closeWaypointEditor();
                    } else if (mx >= wpLayout.nameX && mx <= (wpLayout.nameX + wpLayout.nameW) &&
                               my >= wpLayout.nameY && my <= (wpLayout.nameY + wpLayout.nameH)) {
                        waypointNameFocused = true;
                        app::menus::TextInputMenu::bind(window, &wp.name);
                    } else {
                        waypointNameFocused = false;
                        app::menus::TextInputMenu::unbind(window);
                    }
                    const glm::vec3 palette[5] = {
                        {1.00f, 0.38f, 0.38f}, {0.36f, 0.82f, 1.00f}, {0.40f, 0.92f, 0.42f},
                        {0.96f, 0.90f, 0.34f}, {0.92f, 0.52f, 0.96f},
                    };
                    for (int i = 0; i < 5; ++i) {
                        const float sx =
                            wpLayout.colorX +
                            static_cast<float>(i) * (wpLayout.colorS + wpLayout.colorGap);
                        if (mx >= sx && mx <= (sx + wpLayout.colorS) && my >= wpLayout.colorY &&
                            my <= (wpLayout.colorY + wpLayout.colorS)) {
                            wp.r = static_cast<std::uint8_t>(std::round(palette[i].r * 255.0f));
                            wp.g = static_cast<std::uint8_t>(std::round(palette[i].g * 255.0f));
                            wp.b = static_cast<std::uint8_t>(std::round(palette[i].b * 255.0f));
                        }
                    }
                    for (int i = 0; i < 5; ++i) {
                        const float sx = wpLayout.iconX + static_cast<float>(i) *
                                                              (wpLayout.iconS + wpLayout.iconGap);
                        if (mx >= sx && mx <= (sx + wpLayout.iconS) && my >= wpLayout.iconY &&
                            my <= (wpLayout.iconY + wpLayout.iconS)) {
                            wp.icon = static_cast<std::uint8_t>(i);
                        }
                    }
                    if (mx >= wpLayout.visibilityX &&
                        mx <= (wpLayout.visibilityX + wpLayout.visibilityW) &&
                        my >= wpLayout.visibilityY &&
                        my <= (wpLayout.visibilityY + wpLayout.visibilityH)) {
                        wp.visible = !wp.visible;
                    }
                    mapDragActive = false;
                } else if (inMapGrid) {
                    waypointNameFocused = false;
                    app::menus::TextInputMenu::unbind(window);
                    if (hoveredWaypointIndex >= 0) {
                        if (selectedWaypointIndex == hoveredWaypointIndex) {
                            selectedWaypointIndex = -1;
                        } else {
                            selectedWaypointIndex = hoveredWaypointIndex;
                        }
                        mapDragActive = false;
                    } else {
                        mapDragActive = true;
                        mapDragLastMouseX = static_cast<float>(mx);
                        mapDragLastMouseY = static_cast<float>(my);
                    }
                } else {
                    mapDragActive = false;
                }
            }
            if (!left) {
                mapDragActive = false;
            }
            if (mapDragActive) {
                const float dx = static_cast<float>(mx) - mapDragLastMouseX;
                const float dy = static_cast<float>(my) - mapDragLastMouseY;
                mapCenterWX -= dx / std::max(1.0f, mapLayout.cell);
                mapCenterWZ -= dy / std::max(1.0f, mapLayout.cell);
                mapDragLastMouseX = static_cast<float>(mx);
                mapDragLastMouseY = static_cast<float>(my);
            }
            if (waypointNameFocused && selectedWaypointIndex >= 0 &&
                selectedWaypointIndex < static_cast<int>(mapSystem.waypoints().size())) {
                auto &name = mapSystem.waypoints()[selectedWaypointIndex].name;
                if (name.size() > 32) {
                    name.resize(32);
                }
                app::menus::TextInputMenu::bind(window, &name);
                const bool backspaceDown = glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS;
                if (backspaceDown && !prevWaypointBackspace && !name.empty()) {
                    name.pop_back();
                }
                prevWaypointBackspace = backspaceDown;
            } else {
                prevWaypointBackspace = false;
            }
            const bool enterDown = (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS) ||
                                   (glfwGetKey(window, GLFW_KEY_KP_ENTER) == GLFW_PRESS);
            if (enterDown && !prevWaypointEnter && waypointEditorOpen) {
                commitWaypointEdit();
            }
            prevWaypointEnter = enterDown;
            const bool escDown = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
            if (escDown && !prevWaypointEscape && waypointEditorOpen) {
                cancelWaypointEdit();
            }
            prevWaypointEscape = escDown;
            const bool delDown = (glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS) ||
                                 (!waypointNameFocused && !waypointEditorOpen &&
                                  glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS);
            int deleteIndex = -1;
            if (selectedWaypointIndex >= 0 &&
                selectedWaypointIndex < static_cast<int>(mapSystem.waypoints().size())) {
                deleteIndex = selectedWaypointIndex;
            } else if (hoveredWaypointIndex >= 0) {
                deleteIndex = hoveredWaypointIndex;
            }
            if (delDown && !prevWaypointDelete && deleteIndex >= 0) {
                auto &wps = mapSystem.waypoints();
                wps.erase(wps.begin() + deleteIndex);
                selectedWaypointIndex = -1;
                waypointEditHasBackup = false;
                waypointEditWasNew = false;
                waypointEditBackupIndex = -1;
                closeWaypointEditor();
            }
            prevWaypointDelete = delDown;
        } else {
            mapDragActive = false;
            waypointEditorOpen = false;
            waypointNameFocused = false;
            prevWaypointBackspace = false;
            prevWaypointEnter = false;
            prevWaypointEscape = false;
            prevWaypointDelete = false;
            waypointEditHasBackup = false;
            waypointEditWasNew = false;
            waypointEditBackupIndex = -1;
            if (hudVisible && currentCursorMode == GLFW_CURSOR_NORMAL && left && !prevLeft) {
                int winW = 1;
                int winH = 1;
                glfwGetWindowSize(window, &winW, &winH);
                double mx = 0.0;
                double my = 0.0;
                glfwGetCursorPos(window, &mx, &my);
                const app::menus::MiniMapLayout miniMapLayout = miniMapMenu.computeLayout(winW);
                if (mx >= miniMapLayout.compassX &&
                    mx <= (miniMapLayout.compassX + miniMapLayout.compassW) &&
                    my >= miniMapLayout.compassY &&
                    my <= (miniMapLayout.compassY + miniMapLayout.compassH)) {
                    miniMapShowCompass = !miniMapShowCompass;
                } else if (mx >= miniMapLayout.followX &&
                           mx <= (miniMapLayout.followX + miniMapLayout.followW) &&
                           my >= miniMapLayout.followY &&
                           my <= (miniMapLayout.followY + miniMapLayout.followH)) {
                    miniMapNorthLocked = !miniMapNorthLocked;
                } else if (mx >= miniMapLayout.waypointX &&
                           mx <= (miniMapLayout.waypointX + miniMapLayout.waypointW) &&
                           my >= miniMapLayout.waypointY &&
                           my <= (miniMapLayout.waypointY + miniMapLayout.waypointH)) {
                    miniMapShowWaypoints = !miniMapShowWaypoints;
                } else if (mx >= miniMapLayout.minusX &&
                           mx <= (miniMapLayout.minusX + miniMapLayout.minusW) &&
                           my >= miniMapLayout.minusY &&
                           my <= (miniMapLayout.minusY + miniMapLayout.minusH)) {
                    miniMapZoom = std::clamp(miniMapZoom / 1.15f, 0.6f, 2.4f);
                } else if (mx >= miniMapLayout.plusX &&
                           mx <= (miniMapLayout.plusX + miniMapLayout.plusW) &&
                           my >= miniMapLayout.plusY &&
                           my <= (miniMapLayout.plusY + miniMapLayout.plusH)) {
                    miniMapZoom = std::clamp(miniMapZoom * 1.15f, 0.6f, 2.4f);
                }
            }
        }
    };

    auto handleInventoryAndCraftingUi = [&](bool menuOpen, bool left, bool right, float now,
                                            const std::vector<int> &filteredRecipeIndices,
                                            const std::vector<int> &filteredSmeltingIndices,
                                            const std::vector<voxel::BlockId>
                                                &filteredCreativeItems,
                                            const std::function<void()> &clearCraftInputs) {
        if (inventoryVisible && !menuOpen) {
            int winW = 1;
            int winH = 1;
            glfwGetWindowSize(window, &winW, &winH);
            double mx = 0.0;
            double my = 0.0;
            glfwGetCursorPos(window, &mx, &my);
            const int slotIndex = craftingMenu.inventorySlotAtCursor(
                mx, my, winW, winH, true, debugCfg.hudScale, craftingGridSize, usingFurnace);
            const bool shiftDown = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ||
                                   (glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
            auto clearIfEmpty = [](game::Inventory::Slot &s) {
                if (s.count <= 0) {
                    s.count = 0;
                    s.id = voxel::AIR;
                }
            };
            auto moveToRange = [&](int fromIndex, int start, int endExclusive) {
                auto &from = inventory.slot(fromIndex);
                if (from.count <= 0 || from.id == voxel::AIR) {
                    return;
                }
                for (int i = start; i < endExclusive && from.count > 0; ++i) {
                    auto &dst = inventory.slot(i);
                    if (dst.id == from.id && dst.count > 0 &&
                        dst.count < game::Inventory::kMaxStack) {
                        const int can = game::Inventory::kMaxStack - dst.count;
                        const int move = (from.count < can) ? from.count : can;
                        dst.count += move;
                        from.count -= move;
                    }
                }
                for (int i = start; i < endExclusive && from.count > 0; ++i) {
                    auto &dst = inventory.slot(i);
                    if (dst.count == 0 || dst.id == voxel::AIR) {
                        const int move = (from.count < game::Inventory::kMaxStack)
                                             ? from.count
                                             : game::Inventory::kMaxStack;
                        dst.id = from.id;
                        dst.count = move;
                        from.count -= move;
                    }
                }
                clearIfEmpty(from);
            };
            auto moveStackIntoInventory = [&](voxel::BlockId id, int &count) {
                if (id == voxel::AIR || count <= 0) {
                    return;
                }
                for (int i = 0; i < game::Inventory::kSlotCount && count > 0; ++i) {
                    auto &dst = inventory.slot(i);
                    if (dst.id != id || dst.count <= 0 || dst.count >= game::Inventory::kMaxStack) {
                        continue;
                    }
                    const int can = game::Inventory::kMaxStack - dst.count;
                    const int move = (count < can) ? count : can;
                    dst.count += move;
                    count -= move;
                }
                for (int i = 0; i < game::Inventory::kSlotCount && count > 0; ++i) {
                    auto &dst = inventory.slot(i);
                    if (dst.count != 0 && dst.id != voxel::AIR) {
                        continue;
                    }
                    const int move =
                        (count < game::Inventory::kMaxStack) ? count : game::Inventory::kMaxStack;
                    dst.id = id;
                    dst.count = move;
                    count -= move;
                }
            };
            auto inventoryCapacityFor = [&](voxel::BlockId id) {
                int cap = 0;
                for (int i = 0; i < game::Inventory::kSlotCount; ++i) {
                    const auto &dst = inventory.slot(i);
                    if (dst.count == 0 || dst.id == voxel::AIR) {
                        cap += game::Inventory::kMaxStack;
                    } else if (dst.id == id && dst.count < game::Inventory::kMaxStack) {
                        cap += (game::Inventory::kMaxStack - dst.count);
                    }
                }
                return cap;
            };

            auto slotFromUiIndex = [&](int idx) -> game::Inventory::Slot * {
                if (idx >= 0 && idx < game::Inventory::kSlotCount) {
                    return &inventory.slot(idx);
                }
                if (usingFurnace) {
                    if (idx == game::Inventory::kSlotCount) {
                        return &smelting.input;
                    }
                    if (idx == game::Inventory::kSlotCount + 1) {
                        return &smelting.fuel;
                    }
                    return nullptr;
                }
                if (idx >= game::Inventory::kSlotCount &&
                    idx <
                        game::Inventory::kSlotCount + app::menus::CraftingMenu::kCraftInputCount) {
                    return &crafting.input[idx - game::Inventory::kSlotCount];
                }
                return nullptr;
            };
            const int furnaceInputSlot = game::Inventory::kSlotCount;
            const int furnaceFuelSlot = game::Inventory::kSlotCount + 1;
            auto furnaceSlotAccepts = [&](int idx, voxel::BlockId id) {
                if (!usingFurnace || id == voxel::AIR) {
                    return true;
                }
                if (idx == furnaceInputSlot) {
                    return smeltingSystem.canSmelt(id);
                }
                if (idx == furnaceFuelSlot) {
                    return smeltingSystem.isFuel(id);
                }
                if (idx == app::menus::CraftingMenu::kCraftOutputSlot) {
                    return false;
                }
                return true;
            };
            auto canAcceptIntoSlotForId = [&](int idx, voxel::BlockId id) {
                if (idx == app::menus::CraftingMenu::kCraftOutputSlot ||
                    idx == app::menus::CraftingMenu::kTrashSlot) {
                    return false;
                }
                if (id == voxel::AIR) {
                    return false;
                }
                auto *slot = slotFromUiIndex(idx);
                if (slot == nullptr) {
                    return false;
                }
                if (!furnaceSlotAccepts(idx, id)) {
                    return false;
                }
                if (slot->count <= 0 || slot->id == voxel::AIR) {
                    return true;
                }
                return slot->id == id && slot->count < game::Inventory::kMaxStack;
            };
            auto tryPlaceOneFromCarried = [&](int idx) {
                if (idx == app::menus::CraftingMenu::kCraftOutputSlot ||
                    idx == app::menus::CraftingMenu::kTrashSlot) {
                    return false;
                }
                if (carriedSlot.id == voxel::AIR || carriedSlot.count <= 0) {
                    return false;
                }
                auto *dst = slotFromUiIndex(idx);
                if (dst == nullptr || !furnaceSlotAccepts(idx, carriedSlot.id)) {
                    return false;
                }
                if (dst->count <= 0 || dst->id == voxel::AIR) {
                    dst->id = carriedSlot.id;
                    dst->count = 1;
                    carriedSlot.count -= 1;
                    clearIfEmpty(carriedSlot);
                    return true;
                }
                if (dst->id == carriedSlot.id && dst->count < game::Inventory::kMaxStack) {
                    dst->count += 1;
                    carriedSlot.count -= 1;
                    clearIfEmpty(carriedSlot);
                    return true;
                }
                return false;
            };
            auto recomputeSpreadPreview = [&]() {
                carriedSlot = inventorySpreadStartCarried;
                for (const auto &entry : inventorySpreadOriginalSlots) {
                    if (auto *slot = slotFromUiIndex(entry.first); slot != nullptr) {
                        *slot = entry.second;
                    }
                }
                bool progress = true;
                while (carriedSlot.id != voxel::AIR && carriedSlot.count > 0 && progress) {
                    progress = false;
                    for (const int idx : inventorySpreadSlots) {
                        if (carriedSlot.id == voxel::AIR || carriedSlot.count <= 0) {
                            break;
                        }
                        auto *dst = slotFromUiIndex(idx);
                        if (dst == nullptr || !furnaceSlotAccepts(idx, carriedSlot.id)) {
                            continue;
                        }
                        if (dst->count > 0 && dst->id != carriedSlot.id) {
                            continue;
                        }
                        if (dst->count >= game::Inventory::kMaxStack) {
                            continue;
                        }
                        if (dst->count <= 0 || dst->id == voxel::AIR) {
                            dst->id = carriedSlot.id;
                            dst->count = 0;
                        }
                        dst->count += 1;
                        carriedSlot.count -= 1;
                        clearIfEmpty(carriedSlot);
                        progress = true;
                    }
                }
                if (!usingFurnace) {
                    craftingSystem.updateOutput(crafting, craftingGridSize);
                }
            };
            auto recomputeRightSpreadPreview = [&]() {
                carriedSlot = inventoryRightSpreadStartCarried;
                for (const auto &entry : inventoryRightSpreadOriginalSlots) {
                    if (auto *slot = slotFromUiIndex(entry.first); slot != nullptr) {
                        *slot = entry.second;
                    }
                }
                bool progress = true;
                while (carriedSlot.id != voxel::AIR && carriedSlot.count > 0 && progress) {
                    progress = false;
                    for (const int idx : inventoryRightSpreadSlots) {
                        if (carriedSlot.id == voxel::AIR || carriedSlot.count <= 0) {
                            break;
                        }
                        auto *dst = slotFromUiIndex(idx);
                        if (dst == nullptr ||
                            !furnaceSlotAccepts(idx, inventoryRightSpreadStartCarried.id)) {
                            continue;
                        }
                        if (dst->count > 0 && dst->id != inventoryRightSpreadStartCarried.id) {
                            continue;
                        }
                        if (dst->count >= game::Inventory::kMaxStack) {
                            continue;
                        }
                        if (dst->count <= 0 || dst->id == voxel::AIR) {
                            dst->id = inventoryRightSpreadStartCarried.id;
                            dst->count = 0;
                        }
                        dst->count += 1;
                        carriedSlot.count -= 1;
                        clearIfEmpty(carriedSlot);
                        progress = true;
                    }
                }
                if (!usingFurnace) {
                    craftingSystem.updateOutput(crafting, craftingGridSize);
                }
            };
            auto resetLeftSpread = [&]() {
                inventorySpreadDragging = false;
                inventorySpreadSlots.clear();
                inventorySpreadOriginalSlots.clear();
                inventorySpreadPickupSourceSlot = -1;
            };
            auto startLeftSpread = [&](int idx) {
                inventorySpreadDragging = true;
                inventorySpreadStartCarried = carriedSlot;
                inventorySpreadSlots.clear();
                inventorySpreadOriginalSlots.clear();
                inventorySpreadSlots.push_back(idx);
                if (auto *slot = slotFromUiIndex(idx); slot != nullptr) {
                    inventorySpreadOriginalSlots.emplace_back(idx, *slot);
                }
                recomputeSpreadPreview();
            };
            auto resetRightSpread = [&]() {
                inventoryRightSpreadDragging = false;
                inventoryRightSpreadSlots.clear();
                inventoryRightSpreadOriginalSlots.clear();
            };
            auto startRightSpread = [&](int idx) {
                inventoryRightSpreadDragging = true;
                inventoryRightSpreadStartCarried = carriedSlot;
                inventoryRightSpreadSlots.clear();
                inventoryRightSpreadOriginalSlots.clear();
                inventoryRightSpreadSlots.push_back(idx);
                if (auto *slot = slotFromUiIndex(idx); slot != nullptr) {
                    inventoryRightSpreadOriginalSlots.emplace_back(idx, *slot);
                }
            };
            if (left && !prevInventoryLeft) {
                resetLeftSpread();
            }
            if (!inventorySpreadDragging && left && prevInventoryLeft && !shiftDown &&
                carriedSlot.id != voxel::AIR && carriedSlot.count > 0 &&
                slotIndex != inventorySpreadPickupSourceSlot &&
                canAcceptIntoSlotForId(slotIndex, carriedSlot.id)) {
                startLeftSpread(slotIndex);
            }
            if (inventorySpreadDragging && left && !shiftDown &&
                canAcceptIntoSlotForId(slotIndex, carriedSlot.id)) {
                if (std::find(inventorySpreadSlots.begin(), inventorySpreadSlots.end(),
                              slotIndex) == inventorySpreadSlots.end()) {
                    inventorySpreadSlots.push_back(slotIndex);
                    if (auto *slot = slotFromUiIndex(slotIndex); slot != nullptr) {
                        inventorySpreadOriginalSlots.emplace_back(slotIndex, *slot);
                    }
                    recomputeSpreadPreview();
                }
            }
            if (inventorySpreadDragging && !left && prevInventoryLeft) {
                resetLeftSpread();
            }
            if (!left && !prevInventoryLeft) {
                resetLeftSpread();
            }
            if (right && !prevInventoryRight) {
                resetRightSpread();
                if (!shiftDown && carriedSlot.id != voxel::AIR && carriedSlot.count > 0 &&
                    canAcceptIntoSlotForId(slotIndex, carriedSlot.id)) {
                    startRightSpread(slotIndex);
                }
            }
            if (!inventoryRightSpreadDragging && right && prevInventoryRight && !shiftDown &&
                carriedSlot.id != voxel::AIR && carriedSlot.count > 0 &&
                canAcceptIntoSlotForId(slotIndex, carriedSlot.id)) {
                startRightSpread(slotIndex);
            }
            if (inventoryRightSpreadDragging && right && prevInventoryRight &&
                canAcceptIntoSlotForId(slotIndex, inventoryRightSpreadStartCarried.id)) {
                if (std::find(inventoryRightSpreadSlots.begin(), inventoryRightSpreadSlots.end(),
                              slotIndex) == inventoryRightSpreadSlots.end()) {
                    inventoryRightSpreadSlots.push_back(slotIndex);
                    if (auto *slot = slotFromUiIndex(slotIndex); slot != nullptr) {
                        inventoryRightSpreadOriginalSlots.emplace_back(slotIndex, *slot);
                    }
                    if (inventoryRightSpreadSlots.size() > 1) {
                        recomputeRightSpreadPreview();
                    }
                }
            }
            if (!right) {
                resetRightSpread();
            }

            bool handledRecipeClick = false;
            bool handledCreativeClick = false;
            bool handledTrashClick = false;
            if (creativeMenuVisible &&
                (left && !prevInventoryLeft || right && !prevInventoryRight)) {
                const app::menus::CreativeMenuLayout creativeLayout =
                    creativeMenu.computeLayout(winW, winH, debugCfg.hudScale, craftingGridSize,
                                               usingFurnace, filteredCreativeItems.size());
                const int creativeIndex = creativeMenu.itemAtCursor(
                    mx, my, creativeLayout, creativeMenuScroll, filteredCreativeItems.size());
                if (creativeIndex >= 0 &&
                    creativeIndex < static_cast<int>(filteredCreativeItems.size())) {
                    const voxel::BlockId id = filteredCreativeItems[creativeIndex];
                    const int addCount = shiftDown ? game::Inventory::kMaxStack : 1;
                    inventory.add(id, addCount);
                    handledCreativeClick = true;
                }
            }
            if (recipeMenuVisible && !usingFurnace && left && !prevInventoryLeft) {
                const app::menus::RecipeMenuLayout recipeLayout =
                    recipeMenu.computeLayout(winW, winH, debugCfg.hudScale, craftingGridSize,
                                             usingFurnace, filteredRecipeIndices.size());
                const int recipeIndex = recipeMenu.rowAtCursor(
                    mx, my, recipeLayout, recipeMenuScroll, filteredRecipeIndices.size());
                if (recipeIndex >= 0 &&
                    recipeIndex < static_cast<int>(filteredRecipeIndices.size())) {
                    const int recipeFillIndex = filteredRecipeIndices[recipeIndex];
                    const auto &recipe = craftingSystem.recipeInfos()[recipeFillIndex];
                    bool ok = (craftingGridSize >= recipe.minGridSize);
                    const int activeInputs =
                        game::CraftingSystem::activeInputCount(craftingGridSize);
                    const bool switchingRecipe = (lastRecipeFillIndex != recipeFillIndex);
                    if (ok && switchingRecipe) {
                        bool hasCraftItems = false;
                        for (int i = 0; i < activeInputs; ++i) {
                            const auto &slot = crafting.input[i];
                            if (slot.id != voxel::AIR && slot.count > 0) {
                                hasCraftItems = true;
                                break;
                            }
                        }
                        if (hasCraftItems) {
                            clearCraftInputs();
                        }
                    }
                    if (ok) {
                        if (shiftDown) {
                            const int maxSetsByOutput = std::max(
                                1, game::Inventory::kMaxStack / std::max(1, recipe.outputCount));
                            int setsAdded = 0;
                            while (setsAdded < maxSetsByOutput &&
                               craftingSystem.tryAddRecipeSet(recipe, inventory, crafting,
                                                               activeInputs)) {
                                ++setsAdded;
                            }
                            ok = (setsAdded > 0);
                        } else {
                            ok = craftingSystem.tryAddRecipeSet(recipe, inventory, crafting,
                                                                activeInputs);
                        }
                    }
                    // Track last clicked recipe so repeated clicks on the same recipe
                    // never trigger a clear/reseed cycle after a failed add.
                    lastRecipeFillIndex = recipeFillIndex;
                    craftingSystem.updateOutput(crafting, craftingGridSize);
                    handledRecipeClick = true;
                }
            }
            if (recipeMenuVisible && usingFurnace && left && !prevInventoryLeft) {
                const app::menus::RecipeMenuLayout recipeLayout =
                    recipeMenu.computeLayout(winW, winH, debugCfg.hudScale, craftingGridSize,
                                             usingFurnace, filteredSmeltingIndices.size());
                const int recipeIndex = recipeMenu.rowAtCursor(
                    mx, my, recipeLayout, recipeMenuScroll, filteredSmeltingIndices.size());
                if (recipeIndex >= 0 &&
                    recipeIndex < static_cast<int>(filteredSmeltingIndices.size())) {
                    const auto &smeltRecipes = smeltingSystem.recipes();
                    const auto &recipe = smeltRecipes[filteredSmeltingIndices[recipeIndex]];
                    int fuelCount = 0;
                    int inputCount = 0;
                    for (int i = 0; i < game::Inventory::kSlotCount; ++i) {
                        const auto &slot = inventory.slot(i);
                        if (slot.id != voxel::AIR && slot.count > 0) {
                            if (smeltingSystem.isFuel(slot.id)) {
                                fuelCount += slot.count;
                            }
                            if (slot.id == recipe.input) {
                                inputCount += slot.count;
                            }
                        }
                    }
                    const bool smeltableNow = (inputCount > 0 && fuelCount > 0);
                    if (!smeltableNow) {
                        handledRecipeClick = true;
                    } else {
                        const bool switchingRecipe =
                            (smelting.input.id != voxel::AIR && smelting.input.count > 0 &&
                             smelting.input.id != recipe.input);
                        bool abortRecipeClick = false;

                        if (switchingRecipe) {
                            const auto stackCapacityFor = [&](voxel::BlockId id) {
                                int cap = 0;
                                for (int i = 0; i < game::Inventory::kSlotCount; ++i) {
                                    const auto &dst = inventory.slot(i);
                                    if (dst.count == 0 || dst.id == voxel::AIR) {
                                        cap += game::Inventory::kMaxStack;
                                    } else if (dst.id == id &&
                                               dst.count < game::Inventory::kMaxStack) {
                                        cap += (game::Inventory::kMaxStack - dst.count);
                                    }
                                }
                                return cap;
                            };

                            int needInput =
                                (smelting.input.id != voxel::AIR && smelting.input.count > 0)
                                    ? smelting.input.count
                                    : 0;
                            int needOutput =
                                (smelting.output.id != voxel::AIR && smelting.output.count > 0)
                                    ? smelting.output.count
                                    : 0;
                            bool canStoreAll = true;
                            if (needInput > 0 || needOutput > 0) {
                                if (needInput > 0 && smelting.input.id == smelting.output.id) {
                                    canStoreAll = stackCapacityFor(smelting.input.id) >=
                                                  (needInput + needOutput);
                                } else {
                                    if (needInput > 0) {
                                        canStoreAll =
                                            canStoreAll &&
                                            (stackCapacityFor(smelting.input.id) >= needInput);
                                    }
                                    if (needOutput > 0) {
                                        canStoreAll =
                                            canStoreAll &&
                                            (stackCapacityFor(smelting.output.id) >= needOutput);
                                    }
                                }
                            }
                            if (!canStoreAll) {
                                handledRecipeClick = true;
                                abortRecipeClick = true;
                            }

                            if (!abortRecipeClick) {
                                if (needInput > 0) {
                                    int remaining = smelting.input.count;
                                    moveStackIntoInventory(smelting.input.id, remaining);
                                    smelting.input.count = remaining;
                                    clearIfEmpty(smelting.input);
                                }
                                if (needOutput > 0) {
                                    int remaining = smelting.output.count;
                                    moveStackIntoInventory(smelting.output.id, remaining);
                                    smelting.output.count = remaining;
                                    clearIfEmpty(smelting.output);
                                }
                                smelting.progressSeconds = 0.0f;
                            }
                        }

                        if (!abortRecipeClick) {
                            auto addOneInput = [&](voxel::BlockId id) {
                                if (id == voxel::AIR) {
                                    return false;
                                }
                                if (smelting.input.id != voxel::AIR && smelting.input.id != id) {
                                    return false;
                                }
                                if (smelting.input.id == voxel::AIR || smelting.input.count <= 0) {
                                    smelting.input.id = id;
                                    smelting.input.count = 0;
                                }
                                if (smelting.input.count >= game::Inventory::kMaxStack) {
                                    return false;
                                }
                                for (int i = 0; i < game::Inventory::kSlotCount; ++i) {
                                    auto &slot = inventory.slot(i);
                                    if (slot.id != id || slot.count <= 0) {
                                        continue;
                                    }
                                    slot.count -= 1;
                                    smelting.input.count += 1;
                                    if (slot.count <= 0) {
                                        slot = {};
                                    }
                                    return true;
                                }
                                return false;
                            };
                            if (shiftDown) {
                                while (addOneInput(recipe.input)) {
                                }
                            } else {
                                addOneInput(recipe.input);
                            }
                        }
                        handledRecipeClick = true;
                    }
                }
            }

            if (slotIndex == app::menus::CraftingMenu::kTrashSlot &&
                ((left && !prevInventoryLeft) || (right && !prevInventoryRight))) {
                if (carriedSlot.id != voxel::AIR && carriedSlot.count > 0) {
                    carriedSlot = {};
                }
                handledTrashClick = true;
            }

            game::Inventory::Slot *clickedSlot =
                (handledRecipeClick || handledCreativeClick || handledTrashClick)
                    ? nullptr
                    : slotFromUiIndex(slotIndex);

            if (!handledRecipeClick && slotIndex == app::menus::CraftingMenu::kCraftOutputSlot) {
                if (usingFurnace) {
                    if (left && !prevInventoryLeft && smelting.output.id != voxel::AIR &&
                        smelting.output.count > 0) {
                        if (shiftDown && carriedSlot.count == 0) {
                            int remaining = smelting.output.count;
                            const voxel::BlockId id = smelting.output.id;
                            moveStackIntoInventory(id, remaining);
                            smelting.output.count = remaining;
                            clearIfEmpty(smelting.output);
                        } else {
                            const bool canTake =
                                (carriedSlot.count == 0 || carriedSlot.id == voxel::AIR ||
                                 carriedSlot.id == smelting.output.id);
                            if (canTake) {
                                if (carriedSlot.count == 0 || carriedSlot.id == voxel::AIR) {
                                    carriedSlot.id = smelting.output.id;
                                }
                                const int canStack = game::Inventory::kMaxStack - carriedSlot.count;
                                if (canStack > 0) {
                                    const int move = std::min(canStack, smelting.output.count);
                                    carriedSlot.count += move;
                                    smelting.output.count -= move;
                                    clearIfEmpty(smelting.output);
                                }
                            }
                        }
                    }
                } else if (left && !prevInventoryLeft && crafting.output.id != voxel::AIR &&
                           crafting.output.count > 0) {
                    if (shiftDown && carriedSlot.count == 0) {
                        while (crafting.output.id != voxel::AIR && crafting.output.count > 0) {
                            const voxel::BlockId craftedId = crafting.output.id;
                            const int craftedCount = crafting.output.count;
                            if (inventoryCapacityFor(craftedId) < craftedCount) {
                                break;
                            }
                            if (!craftingSystem.consumeInputs(crafting, craftingGridSize)) {
                                break;
                            }
                            int remaining = craftedCount;
                            moveStackIntoInventory(craftedId, remaining);
                        }
                    } else {
                        const bool canTake =
                            (carriedSlot.count == 0 || carriedSlot.id == voxel::AIR ||
                             carriedSlot.id == crafting.output.id);
                        if (canTake) {
                            const voxel::BlockId craftedId = crafting.output.id;
                            const int craftedCount = crafting.output.count;
                            if (carriedSlot.count == 0 || carriedSlot.id == voxel::AIR) {
                                carriedSlot.id = craftedId;
                            }
                            const int canStack = game::Inventory::kMaxStack - carriedSlot.count;
                            if (canStack > 0 &&
                                craftingSystem.consumeInputs(crafting, craftingGridSize)) {
                                const int move = std::min(canStack, craftedCount);
                                carriedSlot.count += move;
                            }
                        }
                    }
                }
            } else if (!handledRecipeClick && !handledCreativeClick && !handledTrashClick &&
                       clickedSlot != nullptr) {
                auto *dst = clickedSlot;
                if (slotIndex >= game::Inventory::kSlotCount) {
                    lastRecipeFillIndex = -1;
                }
                if (left && !prevInventoryLeft) {
                    voxel::BlockId clickId = voxel::AIR;
                    if (carriedSlot.count > 0 && carriedSlot.id != voxel::AIR) {
                        clickId = carriedSlot.id;
                    } else if (dst->count > 0 && dst->id != voxel::AIR) {
                        clickId = dst->id;
                    }

                    const bool allowDoubleClick =
                        slotIndex >= 0 && slotIndex < game::Inventory::kSlotCount;
                    const bool doubleClickCollect =
                        allowDoubleClick && !shiftDown && clickId != voxel::AIR &&
                        lastInventoryLeftClickId == clickId && lastInventoryLeftClickTime >= 0.0f &&
                        (now - lastInventoryLeftClickTime) <= 0.30f;

                    if (doubleClickCollect) {
                        if (carriedSlot.count == 0) {
                            carriedSlot.id = clickId;
                            carriedSlot.count = 0;
                        }
                        if (carriedSlot.id == clickId &&
                            carriedSlot.count < game::Inventory::kMaxStack) {
                            for (int i = 0; i < game::Inventory::kSlotCount; ++i) {
                                auto &src = inventory.slot(i);
                                if (src.id != clickId || src.count <= 0) {
                                    continue;
                                }
                                const int canTake = game::Inventory::kMaxStack - carriedSlot.count;
                                if (canTake <= 0) {
                                    break;
                                }
                                const int move = (src.count < canTake) ? src.count : canTake;
                                carriedSlot.count += move;
                                src.count -= move;
                                clearIfEmpty(src);
                            }
                        }
                    } else if (shiftDown && carriedSlot.count == 0) {
                        if (slotIndex >= 0 && slotIndex < game::Inventory::kSlotCount) {
                            if (usingFurnace) {
                                auto &from = inventory.slot(slotIndex);
                                bool movedAny = false;
                                auto moveInto = [&](game::Inventory::Slot &dst) {
                                    if (from.count <= 0 || from.id == voxel::AIR) {
                                        return;
                                    }
                                    if (dst.count > 0 && dst.id != from.id) {
                                        return;
                                    }
                                    const int can = game::Inventory::kMaxStack - dst.count;
                                    if (can <= 0) {
                                        return;
                                    }
                                    const int move = std::min(can, from.count);
                                    if (move <= 0) {
                                        return;
                                    }
                                    if (dst.id == voxel::AIR || dst.count <= 0) {
                                        dst.id = from.id;
                                        dst.count = 0;
                                    }
                                    dst.count += move;
                                    from.count -= move;
                                    movedAny = true;
                                };
                                if (smeltingSystem.canSmelt(from.id)) {
                                    moveInto(smelting.input);
                                }
                                if (smeltingSystem.isFuel(from.id)) {
                                    moveInto(smelting.fuel);
                                }
                                if (!movedAny) {
                                    if (slotIndex < game::Inventory::kHotbarSize) {
                                        moveToRange(slotIndex, game::Inventory::kHotbarSize,
                                                    game::Inventory::kSlotCount);
                                    } else {
                                        moveToRange(slotIndex, 0, game::Inventory::kHotbarSize);
                                    }
                                }
                                clearIfEmpty(from);
                            } else {
                                if (slotIndex < game::Inventory::kHotbarSize) {
                                    moveToRange(slotIndex, game::Inventory::kHotbarSize,
                                                game::Inventory::kSlotCount);
                                } else {
                                    moveToRange(slotIndex, 0, game::Inventory::kHotbarSize);
                                }
                            }
                        } else if (slotIndex >= game::Inventory::kSlotCount &&
                                   slotIndex < game::Inventory::kSlotCount +
                                                   app::menus::CraftingMenu::kCraftInputCount) {
                            int remaining = dst->count;
                            const voxel::BlockId id = dst->id;
                            moveStackIntoInventory(id, remaining);
                            dst->count = remaining;
                            clearIfEmpty(*dst);
                        }
                    } else if (carriedSlot.count == 0) {
                        carriedSlot = *dst;
                        *dst = {};
                        inventorySpreadPickupSourceSlot = slotIndex;
                    } else if (dst->count == 0) {
                        if (furnaceSlotAccepts(slotIndex, carriedSlot.id)) {
                            *dst = carriedSlot;
                            carriedSlot = {};
                        }
                    } else if (dst->id == carriedSlot.id &&
                               dst->count < game::Inventory::kMaxStack) {
                        if (furnaceSlotAccepts(slotIndex, carriedSlot.id)) {
                            const int canTake = game::Inventory::kMaxStack - dst->count;
                            const int move =
                                (carriedSlot.count < canTake) ? carriedSlot.count : canTake;
                            dst->count += move;
                            carriedSlot.count -= move;
                            clearIfEmpty(carriedSlot);
                        }
                    } else {
                        if (furnaceSlotAccepts(slotIndex, carriedSlot.id)) {
                            std::swap(*dst, carriedSlot);
                        }
                    }
                    lastInventoryLeftClickTime = now;
                    lastInventoryLeftClickId = clickId;
                } else if (right && !prevInventoryRight) {
                    if (carriedSlot.count == 0) {
                        if (dst->count > 0 && dst->id != voxel::AIR) {
                            const int take = (dst->count + 1) / 2;
                            carriedSlot.id = dst->id;
                            carriedSlot.count = take;
                            dst->count -= take;
                            clearIfEmpty(*dst);
                        }
                    } else {
                        if (tryPlaceOneFromCarried(slotIndex)) {
                            // Single-slot right click behavior: place one.
                        }
                    }
                }
                if (!usingFurnace && slotIndex >= game::Inventory::kSlotCount) {
                    craftingSystem.updateOutput(crafting, craftingGridSize);
                }
            } else if (!handledRecipeClick && !handledCreativeClick && !handledTrashClick) {
                const bool outsideClick =
                    (left && !prevInventoryLeft) || (right && !prevInventoryRight);
                if (outsideClick && carriedSlot.id != voxel::AIR && carriedSlot.count > 0) {
                    const glm::vec3 dropPos = camera.position() + camera.forward() * 2.10f +
                                              glm::vec3(0.0f, -0.30f, 0.0f);
                    itemDrops.spawn(carriedSlot.id, dropPos, carriedSlot.count);
                    carriedSlot = {};
                }
            }
            prevInventoryLeft = left;
            prevInventoryRight = right;
        } else {
            prevInventoryLeft = false;
            prevInventoryRight = false;
        }
    };
    while (!glfwWindowShouldClose(window)) {
        const float now = static_cast<float>(glfwGetTime());
        float dt = 0.0f;
        float fps = 0.0f;
        updateFrameTiming(now, lastTime, fpsAccumSeconds, fpsAccumFrames, fpsAvgDisplay, fps, dt);

        glfwPollEvents();
        debugMenu.update(window, debugCfg, stats, fps, dt * 1000.0f);
        const bool menuOpen = debugMenu.isOpen();
        const bool pauseToggleDown = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (pauseToggleDown && !prevPauseToggle && !menuOpen && !inventoryVisible && !mapOpen) {
            pauseMenuOpen = !pauseMenuOpen;
        }
        prevPauseToggle = pauseToggleDown;
        const bool blockInput = menuOpen || inventoryVisible || pauseMenuOpen || mapOpen;
        if (wasMenuOpen && !menuOpen && !inventoryVisible) {
            camera.resetMouseLook(window);
        }
        wasMenuOpen = menuOpen;
        camera.setMoveSpeed(debugCfg.moveSpeed);
        camera.setMouseSensitivity(debugCfg.mouseSensitivity);
        world.setStreamingRadii(debugCfg.loadRadius, debugCfg.unloadRadius);
        if (debugCfg.smoothLighting != lastSmoothLighting) {
            world.setSmoothLighting(debugCfg.smoothLighting);
            lastSmoothLighting = debugCfg.smoothLighting;
        }

        syncCursorAndLook(window, arrowCursor, camera, blockInput,
                          recaptureMouseAfterInventoryClose);
        const int currentCursorMode = glfwGetInputMode(window, GLFW_CURSOR);

        if (pendingSpawnResolve) {
            const int swx = static_cast<int>(std::floor(loadedCameraPos.x));
            const int swz = static_cast<int>(std::floor(loadedCameraPos.z));
            if (world.isChunkLoadedAt(swx, swz)) {
                // Restore exact saved position on reload (no collision adjustment).
                player.setFromCamera(loadedCameraPos, world, false);
                camera.setPosition(player.cameraPosition());
                loadedCameraPos = camera.position();
                pendingSpawnResolve = false;
            }
        }

        if (ghostMode) {
            if (!blockInput) {
                camera.handleKeyboard(window, dt);
            }
        } else {
            if (!pendingSpawnResolve) {
                player.update(window, world, camera, dt, !blockInput);
                camera.setPosition(player.cameraPosition());
            }
        }

        world.updateStream(camera.position());
        world.updateFluidSimulation(dt);
        world.uploadReadyMeshes();
        stats = world.debugStats();
        mapSystem.observeLoadedChunks(world);
        if (debugCfg.overrideTime) {
            dayClockSeconds = std::clamp(debugCfg.timeOfDay01, 0.0f, 1.0f) * kDayLengthSeconds;
        } else {
            dayClockSeconds += dt;
            if (dayClockSeconds >= kDayLengthSeconds) {
                dayClockSeconds = std::fmod(dayClockSeconds, kDayLengthSeconds);
            }
            debugCfg.timeOfDay01 = dayClockSeconds / kDayLengthSeconds;
        }
        mineCooldown = std::max(0.0f, mineCooldown - dt);
        itemDrops.update(world, camera.position(), dt);
        persistActiveFurnaceState();
        const auto loadedFurnaces = world.loadedFurnacePositions();
        for (const glm::ivec3 &fpos : loadedFurnaces) {
            world::FurnaceState wstate{};
            if (!world.getFurnaceState(fpos.x, fpos.y, fpos.z, wstate)) {
                continue;
            }
            const voxel::BlockId furnaceBlockId = world.getBlock(fpos.x, fpos.y, fpos.z);
            game::SmeltingSystem::State gstate = SaveManager::fromWorldFurnaceState(wstate);
            smeltingSystem.update(gstate, dt);
            const bool furnaceActive = gstate.burnSecondsRemaining > 0.0f;
            if (voxel::isFurnace(furnaceBlockId)) {
                const voxel::BlockId desiredId = furnaceActive
                                                     ? voxel::toLitFurnace(furnaceBlockId)
                                                     : voxel::toUnlitFurnace(furnaceBlockId);
                if (desiredId != furnaceBlockId) {
                    world.setBlock(fpos.x, fpos.y, fpos.z, desiredId);
                }
            }
            const bool hasItems = (gstate.input.id != voxel::AIR && gstate.input.count > 0) ||
                                  (gstate.fuel.id != voxel::AIR && gstate.fuel.count > 0) ||
                                  (gstate.output.id != voxel::AIR && gstate.output.count > 0);
            const bool hasWork = gstate.progressSeconds > 0.0f ||
                                 gstate.burnSecondsRemaining > 0.0f ||
                                 gstate.burnSecondsCapacity > 0.0f;
            if (!hasItems && !hasWork) {
                world.clearFurnaceState(fpos.x, fpos.y, fpos.z);
            } else {
                world.setFurnaceState(fpos.x, fpos.y, fpos.z,
                                      SaveManager::toWorldFurnaceState(gstate));
            }
        }
        loadActiveFurnaceState();
        for (const auto &pickup : itemDrops.consumePickups()) {
            if (inventory.add(pickup.id, pickup.count)) {
                audio.playPickup();
            } else {
                itemDrops.spawn(pickup.id, pickup.pos, pickup.count);
            }
        }

        const bool hudToggleDown = glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS;
        if (hudToggleDown && !prevHudToggle && !menuOpen) {
            hudVisible = !hudVisible;
        }
        prevHudToggle = hudToggleDown;

        const bool mapToggleDown = glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS;
        if (mapToggleDown && !prevMapToggle && !menuOpen && !inventoryVisible &&
            !waypointNameFocused && !pauseMenuOpen) {
            mapOpen = !mapOpen;
            if (mapOpen) {
                mapCenterWX = camera.position().x;
                mapCenterWZ = camera.position().z;
                mapDragActive = false;
                waypointEditorOpen = false;
                waypointNameFocused = false;
                prevWaypointEnter = false;
                prevWaypointEscape = false;
                prevWaypointDelete = false;
                waypointEditHasBackup = false;
                waypointEditWasNew = false;
                waypointEditBackupIndex = -1;
                app::menus::TextInputMenu::unbind(window);
            } else {
                waypointEditorOpen = false;
                waypointNameFocused = false;
                prevWaypointEnter = false;
                prevWaypointEscape = false;
                prevWaypointDelete = false;
                waypointEditHasBackup = false;
                waypointEditWasNew = false;
                waypointEditBackupIndex = -1;
                app::menus::TextInputMenu::unbind(window);
            }
        }
        prevMapToggle = mapToggleDown;
        const bool mapZoomInDown = (glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS) ||
                                   (glfwGetKey(window, GLFW_KEY_KP_ADD) == GLFW_PRESS);
        if (mapZoomInDown && !prevMapZoomInToggle && mapOpen) {
            mapZoom = std::clamp(mapZoom * 1.15f, 0.5f, 3.0f);
        }
        prevMapZoomInToggle = mapZoomInDown;
        const bool mapZoomOutDown = (glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS) ||
                                    (glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS);
        if (mapZoomOutDown && !prevMapZoomOutToggle && mapOpen) {
            mapZoom = std::clamp(mapZoom / 1.15f, 0.5f, 3.0f);
        }
        prevMapZoomOutToggle = mapZoomOutDown;
        if (mapOpen && std::abs(gRecipeMenuScrollDelta) > 0.01f) {
            const float factor = 1.0f + gRecipeMenuScrollDelta * 0.08f;
            mapZoom = std::clamp(mapZoom * std::max(0.5f, factor), 0.5f, 3.0f);
            gRecipeMenuScrollDelta = 0.0f;
        }

        const bool textureReloadDown = glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS;
        if (textureReloadDown && !prevTextureReloadToggle && !menuOpen) {
            if (atlas.reload("assets/textures/atlas.png")) {
                core::Logger::instance().info("Reloaded texture atlas (F5)");
            } else {
                core::Logger::instance().warn(
                    "Failed to reload texture atlas (assets/textures/atlas.png)");
            }
        }
        prevTextureReloadToggle = textureReloadDown;

        const bool modeToggleDown = glfwGetKey(window, GLFW_KEY_F4) == GLFW_PRESS;
        if (modeToggleDown && !prevModeToggle && !menuOpen && !inventoryVisible && !pauseMenuOpen &&
            !mapOpen) {
            ghostMode = !ghostMode;
            if (!ghostMode) {
                player.setFromCamera(camera.position(), world);
            } else {
                camera.setPosition(player.cameraPosition());
            }
        }
        prevModeToggle = modeToggleDown;

        auto clearCraftInputs = [&]() {
            if (usingFurnace) {
                auto flushSmeltSlot = [&](game::Inventory::Slot &slot) {
                    if (slot.id == voxel::AIR || slot.count <= 0) {
                        slot = {};
                        return;
                    }
                    while (slot.count > 0 && inventory.add(slot.id, 1)) {
                        slot.count -= 1;
                    }
                    if (slot.count > 0) {
                        const glm::vec3 dropPos = camera.position() + camera.forward() * 2.10f +
                                                  glm::vec3(0.0f, -0.30f, 0.0f);
                        itemDrops.spawn(slot.id, dropPos, slot.count);
                    }
                    slot = {};
                };
                flushSmeltSlot(smelting.input);
                flushSmeltSlot(smelting.fuel);
                flushSmeltSlot(smelting.output);
                smelting.progressSeconds = 0.0f;
                smelting.burnSecondsRemaining = 0.0f;
                smelting.burnSecondsCapacity = 0.0f;
                return;
            }
            for (int i = 0; i < app::menus::CraftingMenu::kCraftInputCount; ++i) {
                auto &slot = crafting.input[i];
                if (slot.id == voxel::AIR || slot.count <= 0) {
                    slot = {};
                    continue;
                }
                while (slot.count > 0 && inventory.add(slot.id, 1)) {
                    slot.count -= 1;
                }
                if (slot.count > 0) {
                    const glm::vec3 dropPos = camera.position() + camera.forward() * 2.10f +
                                              glm::vec3(0.0f, -0.30f, 0.0f);
                    itemDrops.spawn(slot.id, dropPos, slot.count);
                }
                slot = {};
            }
            craftingSystem.updateOutput(crafting, craftingGridSize);
            lastRecipeFillIndex = -1;
        };

        const bool invToggleDown = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
        const bool suppressInventoryToggle = ((recipeMenuVisible && recipeSearchFocused) ||
                                              (creativeMenuVisible && creativeSearchFocused)) &&
                                             !menuOpen && !pauseMenuOpen;
        if (invToggleDown && !prevInventoryToggle && !menuOpen && !pauseMenuOpen && !mapOpen &&
            !suppressInventoryToggle) {
            const bool openingInventory = !inventoryVisible;
            inventoryVisible = openingInventory;
            if (openingInventory) {
                const auto lookedFurnace =
                    furnaceMenu.lookedAtCell(world, camera, debugCfg.raycastDistance);
                usingFurnace = lookedFurnace.has_value();
                if (usingFurnace) {
                    activeFurnaceCell = lookedFurnace;
                    loadActiveFurnaceState();
                } else {
                    activeFurnaceCell.reset();
                    smelting = {};
                }
                usingCraftingTable = !usingFurnace && craftingMenu.isLookingAtCraftingTable(
                                                          world, camera, debugCfg.raycastDistance);
                craftingGridSize = usingCraftingTable ? game::CraftingSystem::kGridSizeTable
                                                      : game::CraftingSystem::kGridSizeInventory;
                craftingSystem.updateOutput(crafting, craftingGridSize);
            } else {
                persistActiveFurnaceState();
                if (!usingFurnace) {
                    clearCraftInputs();
                }
                usingCraftingTable = false;
                usingFurnace = false;
                activeFurnaceCell.reset();
                smelting = {};
                craftingGridSize = game::CraftingSystem::kGridSizeInventory;
                recipeMenuVisible = false;
                creativeMenuVisible = false;
                recipeMenuScroll = 0.0f;
                creativeMenuScroll = 0.0f;
                recipeScrollDragging = false;
                creativeScrollDragging = false;
                recipeSearchFocused = false;
                creativeSearchFocused = false;
                recipeSearchText.clear();
                creativeSearchText.clear();
                recipeIngredientFilter.reset();
                recipeCraftableOnly = false;
                app::menus::TextInputMenu::unbind(window);
                craftingSystem.updateOutput(crafting, craftingGridSize);
                lastRecipeFillIndex = -1;
            }
            glfwSetInputMode(window, GLFW_CURSOR,
                             inventoryVisible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
            glfwSetCursor(window, inventoryVisible ? arrowCursor : nullptr);
            if (!inventoryVisible) {
                recaptureMouseAfterInventoryClose = true;
            }
        }
        prevInventoryToggle = invToggleDown;

        const bool recipeToggleDown = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
        if (recipeToggleDown && !prevRecipeMenuToggle && inventoryVisible && !menuOpen &&
            !pauseMenuOpen && !mapOpen && !(recipeMenuVisible && recipeSearchFocused) &&
            !(creativeMenuVisible && creativeSearchFocused)) {
            recipeMenuVisible = !recipeMenuVisible;
            if (recipeMenuVisible) {
                creativeMenuVisible = false;
                creativeScrollDragging = false;
                creativeSearchFocused = false;
                creativeSearchText.clear();
                recipeSearchFocused = false;
                app::menus::TextInputMenu::unbind(window);
            } else {
                recipeMenuScroll = 0.0f;
                recipeScrollDragging = false;
                recipeSearchFocused = false;
                recipeSearchText.clear();
                recipeIngredientFilter.reset();
                recipeCraftableOnly = false;
                app::menus::TextInputMenu::unbind(window);
            }
        }
        prevRecipeMenuToggle = recipeToggleDown;

        const bool creativeToggleDown = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
        if (creativeToggleDown && !prevCreativeMenuToggle && !menuOpen && !pauseMenuOpen &&
            !mapOpen && !(creativeMenuVisible && creativeSearchFocused) &&
            !(recipeMenuVisible && recipeSearchFocused)) {
            if (!inventoryVisible) {
                inventoryVisible = true;
                const auto lookedFurnace =
                    furnaceMenu.lookedAtCell(world, camera, debugCfg.raycastDistance);
                usingFurnace = lookedFurnace.has_value();
                if (usingFurnace) {
                    activeFurnaceCell = lookedFurnace;
                    loadActiveFurnaceState();
                } else {
                    activeFurnaceCell.reset();
                    smelting = {};
                }
                usingCraftingTable = !usingFurnace && craftingMenu.isLookingAtCraftingTable(
                                                          world, camera, debugCfg.raycastDistance);
                craftingGridSize = usingCraftingTable ? game::CraftingSystem::kGridSizeTable
                                                      : game::CraftingSystem::kGridSizeInventory;
                craftingSystem.updateOutput(crafting, craftingGridSize);
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                glfwSetCursor(window, arrowCursor);
            }
            creativeMenuVisible = !creativeMenuVisible;
            if (creativeMenuVisible) {
                recipeMenuVisible = false;
                recipeScrollDragging = false;
                recipeSearchFocused = false;
                recipeSearchText.clear();
                recipeIngredientFilter.reset();
                recipeCraftableOnly = false;
                creativeSearchFocused = false;
                app::menus::TextInputMenu::unbind(window);
            } else {
                creativeMenuScroll = 0.0f;
                creativeScrollDragging = false;
                creativeSearchFocused = false;
                creativeSearchText.clear();
                app::menus::TextInputMenu::unbind(window);
            }
        }
        prevCreativeMenuToggle = creativeToggleDown;

        const bool recipeBackspace = glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS;
        if (recipeMenuVisible && recipeBackspace && !prevRecipeSearchBackspace &&
            !recipeSearchText.empty()) {
            recipeSearchText.pop_back();
        }
        prevRecipeSearchBackspace = recipeBackspace;
        if (creativeMenuVisible && recipeBackspace && !prevCreativeSearchBackspace &&
            !creativeSearchText.empty()) {
            creativeSearchText.pop_back();
        }
        prevCreativeSearchBackspace = recipeBackspace;

        std::vector<int> filteredRecipeIndices;
        std::vector<int> filteredSmeltingIndices;
        std::vector<unsigned char> recipeCraftableCache;
        std::vector<unsigned char> recipeCraftableComputed;
        const int recipeFilterActiveInputs =
            game::CraftingSystem::activeInputCount(craftingGridSize);
        auto isRecipeCraftableNow = [&](int index) {
            if (index < 0 || index >= static_cast<int>(craftingSystem.recipeInfos().size())) {
                return false;
            }
            if (recipeCraftableComputed[index] != 0) {
                return recipeCraftableCache[index] != 0;
            }
            recipeCraftableComputed[index] = 1;
            const auto &recipe = craftingSystem.recipeInfos()[index];
            if (craftingGridSize < recipe.minGridSize) {
                recipeCraftableCache[index] = 0;
                return false;
            }
            game::Inventory invSim = inventory;
            game::CraftingSystem::State craftSim{};
            const bool craftable =
                craftingSystem.tryAddRecipeSet(recipe, invSim, craftSim, recipeFilterActiveInputs);
            recipeCraftableCache[index] = craftable ? 1 : 0;
            return craftable;
        };

        if (recipeMenuVisible && !usingFurnace) {
            filteredRecipeIndices.reserve(craftingSystem.recipeInfos().size());
            recipeCraftableCache.assign(craftingSystem.recipeInfos().size(), 0);
            recipeCraftableComputed.assign(craftingSystem.recipeInfos().size(), 0);
            std::vector<int> filteredPlankRecipeIndices;
            filteredPlankRecipeIndices.reserve(3);
            int plankInsertPos = -1;
            for (int i = 0; i < static_cast<int>(craftingSystem.recipeInfos().size()); ++i) {
                const auto &recipe = craftingSystem.recipeInfos()[i];
                const bool matchesSearch = recipeMenu.matchesSearch(recipe, recipeSearchText);
                const bool matchesIngredient =
                    !recipeIngredientFilter.has_value() ||
                    recipeMenu.usesIngredient(recipe, recipeIngredientFilter.value());
                const bool matchesCraftable = !recipeCraftableOnly || isRecipeCraftableNow(i);
                if (matchesSearch && matchesIngredient && matchesCraftable) {
                    const bool isPlanksFamily = (recipe.outputId == voxel::OAK_PLANKS ||
                                                 recipe.outputId == voxel::SPRUCE_PLANKS ||
                                                 recipe.outputId == voxel::BIRCH_PLANKS);
                    if (isPlanksFamily) {
                        if (plankInsertPos < 0) {
                            plankInsertPos = static_cast<int>(filteredRecipeIndices.size());
                        }
                        filteredPlankRecipeIndices.push_back(i);
                        continue;
                    }
                    filteredRecipeIndices.push_back(i);
                }
            }
            if (!filteredPlankRecipeIndices.empty()) {
                const int cycle = static_cast<int>(std::floor(std::max(0.0f, now) * 0.6f));
                const int pick =
                    filteredPlankRecipeIndices[cycle %
                                               static_cast<int>(filteredPlankRecipeIndices.size())];
                const int insertPos =
                    (plankInsertPos < 0 ||
                     plankInsertPos > static_cast<int>(filteredRecipeIndices.size()))
                        ? static_cast<int>(filteredRecipeIndices.size())
                        : plankInsertPos;
                filteredRecipeIndices.insert(filteredRecipeIndices.begin() + insertPos, pick);
            }
        }
        if (recipeMenuVisible && usingFurnace) {
            const auto &smeltRecipes = smeltingSystem.recipes();
            filteredSmeltingIndices.reserve(smeltRecipes.size());

            int fuelCount = 0;
            for (int i = 0; i < game::Inventory::kSlotCount; ++i) {
                const auto &slot = inventory.slot(i);
                if (slot.id != voxel::AIR && slot.count > 0 && smeltingSystem.isFuel(slot.id)) {
                    fuelCount += slot.count;
                }
            }

            for (int i = 0; i < static_cast<int>(smeltRecipes.size()); ++i) {
                const auto &recipe = smeltRecipes[i];
                const bool matchesSearch =
                    recipeMenu.smeltingMatchesSearch(recipe, recipeSearchText);
                const bool matchesIngredient = !recipeIngredientFilter.has_value() ||
                                               recipe.input == recipeIngredientFilter.value();
                int inputCount = 0;
                for (int s = 0; s < game::Inventory::kSlotCount; ++s) {
                    const auto &slot = inventory.slot(s);
                    if (slot.id == recipe.input && slot.count > 0) {
                        inputCount += slot.count;
                    }
                }
                const bool matchesCraftable =
                    !recipeCraftableOnly || (inputCount > 0 && fuelCount > 0);
                if (matchesSearch && matchesIngredient && matchesCraftable) {
                    filteredSmeltingIndices.push_back(i);
                }
            }
        }

        std::vector<voxel::BlockId> filteredCreativeItems;
        if (creativeMenuVisible) {
            filteredCreativeItems.reserve(creativeMenu.catalog().size());
            for (voxel::BlockId id : creativeMenu.catalog()) {
                if (creativeMenu.itemMatchesSearch(id, creativeSearchText)) {
                    filteredCreativeItems.push_back(id);
                }
            }
        }

        if (recipeMenuVisible && inventoryVisible && !menuOpen && !pauseMenuOpen) {
            int winW = 1;
            int winH = 1;
            glfwGetWindowSize(window, &winW, &winH);
            double mx = 0.0;
            double my = 0.0;
            glfwGetCursorPos(window, &mx, &my);
            const bool leftNow = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            recipeMenuScroll -= gRecipeMenuScrollDelta * (34.0f * debugCfg.hudScale);
            const std::size_t recipeCount =
                usingFurnace ? filteredSmeltingIndices.size() : filteredRecipeIndices.size();
            const app::menus::RecipeMenuLayout recipeLayout = recipeMenu.computeLayout(
                winW, winH, debugCfg.hudScale, craftingGridSize, usingFurnace, recipeCount);

            if (usingFurnace) {
                if (leftNow && !prevLeft) {
                    const bool onCraftableFilter =
                        mx >= recipeLayout.craftableFilterX &&
                        mx <= (recipeLayout.craftableFilterX + recipeLayout.craftableFilterW) &&
                        my >= recipeLayout.craftableFilterY &&
                        my <= (recipeLayout.craftableFilterY + recipeLayout.craftableFilterH);
                    const bool onIngredientFilterClear =
                        recipeIngredientFilter.has_value() &&
                        mx >= recipeLayout.ingredientTagCloseX &&
                        mx <=
                            (recipeLayout.ingredientTagCloseX + recipeLayout.ingredientTagCloseS) &&
                        my >= recipeLayout.ingredientTagCloseY &&
                        my <= (recipeLayout.ingredientTagCloseY + recipeLayout.ingredientTagCloseS);
                    if (onIngredientFilterClear) {
                        recipeIngredientFilter.reset();
                        recipeSearchFocused = false;
                        app::menus::TextInputMenu::unbind(window);
                    } else if (onCraftableFilter) {
                        recipeCraftableOnly = !recipeCraftableOnly;
                        recipeSearchFocused = false;
                        app::menus::TextInputMenu::unbind(window);
                    } else {
                        recipeSearchFocused = mx >= recipeLayout.searchX &&
                                              mx <= (recipeLayout.searchX + recipeLayout.searchW) &&
                                              my >= recipeLayout.searchY &&
                                              my <= (recipeLayout.searchY + recipeLayout.searchH);
                    }
                    if (recipeSearchFocused) {
                        app::menus::TextInputMenu::bind(window, &recipeSearchText);
                    }
                }
                if (leftNow && !prevLeft) {
                    const bool onTrack = mx >= (recipeLayout.trackX - 6.0f) &&
                                         mx <= (recipeLayout.trackX + recipeLayout.trackW + 6.0f) &&
                                         my >= recipeLayout.trackY &&
                                         my <= (recipeLayout.trackY + recipeLayout.trackH);
                    if (onTrack) {
                        const float thumbTravel =
                            std::max(0.0f, recipeLayout.trackH - recipeLayout.thumbH);
                        const float thumbY =
                            recipeLayout.trackY +
                            ((recipeLayout.maxScroll > 0.0f)
                                 ? (recipeMenuScroll / recipeLayout.maxScroll) * thumbTravel
                                 : 0.0f);
                        const bool onThumb = my >= thumbY && my <= (thumbY + recipeLayout.thumbH);
                        if (onThumb) {
                            recipeScrollDragging = true;
                            recipeScrollGrabOffsetY = static_cast<float>(my) - thumbY;
                        } else if (recipeLayout.maxScroll > 0.0f) {
                            const float t =
                                std::clamp(static_cast<float>((my - recipeLayout.trackY -
                                                               recipeLayout.thumbH * 0.5f) /
                                                              std::max(1.0f, thumbTravel)),
                                           0.0f, 1.0f);
                            recipeMenuScroll = t * recipeLayout.maxScroll;
                        }
                    }
                }
                if (!leftNow) {
                    recipeScrollDragging = false;
                }
                creativeSearchFocused = false;
                if (recipeSearchFocused) {
                    app::menus::TextInputMenu::bind(window, &recipeSearchText);
                } else {
                    app::menus::TextInputMenu::unbind(window);
                }
                if (recipeScrollDragging && recipeLayout.maxScroll > 0.0f) {
                    const float thumbTravel =
                        std::max(0.0f, recipeLayout.trackH - recipeLayout.thumbH);
                    const float thumbY =
                        std::clamp(static_cast<float>(my) - recipeScrollGrabOffsetY,
                                   recipeLayout.trackY, recipeLayout.trackY + thumbTravel);
                    const float t =
                        (thumbTravel > 0.0f) ? (thumbY - recipeLayout.trackY) / thumbTravel : 0.0f;
                    recipeMenuScroll = t * recipeLayout.maxScroll;
                }
                recipeMenuScroll = std::clamp(recipeMenuScroll, 0.0f, recipeLayout.maxScroll);
            } else {

                if (leftNow && !prevLeft) {
                    const bool onCraftableFilter =
                        mx >= recipeLayout.craftableFilterX &&
                        mx <= (recipeLayout.craftableFilterX + recipeLayout.craftableFilterW) &&
                        my >= recipeLayout.craftableFilterY &&
                        my <= (recipeLayout.craftableFilterY + recipeLayout.craftableFilterH);
                    const bool onIngredientFilterClear =
                        recipeIngredientFilter.has_value() &&
                        mx >= recipeLayout.ingredientTagCloseX &&
                        mx <=
                            (recipeLayout.ingredientTagCloseX + recipeLayout.ingredientTagCloseS) &&
                        my >= recipeLayout.ingredientTagCloseY &&
                        my <= (recipeLayout.ingredientTagCloseY + recipeLayout.ingredientTagCloseS);
                    if (onIngredientFilterClear) {
                        recipeIngredientFilter.reset();
                        recipeSearchFocused = false;
                        app::menus::TextInputMenu::unbind(window);
                    } else if (onCraftableFilter) {
                        recipeCraftableOnly = !recipeCraftableOnly;
                        recipeSearchFocused = false;
                        app::menus::TextInputMenu::unbind(window);
                    } else {
                        recipeSearchFocused = mx >= recipeLayout.searchX &&
                                              mx <= (recipeLayout.searchX + recipeLayout.searchW) &&
                                              my >= recipeLayout.searchY &&
                                              my <= (recipeLayout.searchY + recipeLayout.searchH);
                    }
                    if (recipeSearchFocused) {
                        app::menus::TextInputMenu::bind(window, &recipeSearchText);
                    }
                }

                if (leftNow && !prevLeft) {
                    const bool onTrack = mx >= (recipeLayout.trackX - 6.0f) &&
                                         mx <= (recipeLayout.trackX + recipeLayout.trackW + 6.0f) &&
                                         my >= recipeLayout.trackY &&
                                         my <= (recipeLayout.trackY + recipeLayout.trackH);
                    if (onTrack) {
                        const float thumbTravel =
                            std::max(0.0f, recipeLayout.trackH - recipeLayout.thumbH);
                        const float thumbY =
                            recipeLayout.trackY +
                            ((recipeLayout.maxScroll > 0.0f)
                                 ? (recipeMenuScroll / recipeLayout.maxScroll) * thumbTravel
                                 : 0.0f);
                        const bool onThumb = my >= thumbY && my <= (thumbY + recipeLayout.thumbH);
                        if (onThumb) {
                            recipeScrollDragging = true;
                            recipeScrollGrabOffsetY = static_cast<float>(my) - thumbY;
                        } else if (recipeLayout.maxScroll > 0.0f) {
                            const float t =
                                std::clamp(static_cast<float>((my - recipeLayout.trackY -
                                                               recipeLayout.thumbH * 0.5f) /
                                                              std::max(1.0f, thumbTravel)),
                                           0.0f, 1.0f);
                            recipeMenuScroll = t * recipeLayout.maxScroll;
                        }
                    }
                }
                if (!leftNow) {
                    recipeScrollDragging = false;
                }
                creativeSearchFocused = false;
                if (recipeSearchFocused) {
                    app::menus::TextInputMenu::bind(window, &recipeSearchText);
                } else {
                    app::menus::TextInputMenu::unbind(window);
                }
                if (recipeScrollDragging && recipeLayout.maxScroll > 0.0f) {
                    const float thumbTravel =
                        std::max(0.0f, recipeLayout.trackH - recipeLayout.thumbH);
                    const float thumbY =
                        std::clamp(static_cast<float>(my) - recipeScrollGrabOffsetY,
                                   recipeLayout.trackY, recipeLayout.trackY + thumbTravel);
                    const float t =
                        (thumbTravel > 0.0f) ? (thumbY - recipeLayout.trackY) / thumbTravel : 0.0f;
                    recipeMenuScroll = t * recipeLayout.maxScroll;
                }
                recipeMenuScroll = std::clamp(recipeMenuScroll, 0.0f, recipeLayout.maxScroll);
            }
        } else if (creativeMenuVisible && inventoryVisible && !menuOpen && !pauseMenuOpen) {
            int winW = 1;
            int winH = 1;
            glfwGetWindowSize(window, &winW, &winH);
            double mx = 0.0;
            double my = 0.0;
            glfwGetCursorPos(window, &mx, &my);
            const bool leftNow = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            creativeMenuScroll -= gRecipeMenuScrollDelta * (34.0f * debugCfg.hudScale);
            const app::menus::CreativeMenuLayout creativeLayout =
                creativeMenu.computeLayout(winW, winH, debugCfg.hudScale, craftingGridSize,
                                           usingFurnace, filteredCreativeItems.size());

            if (leftNow && !prevLeft) {
                creativeSearchFocused = mx >= creativeLayout.searchX &&
                                        mx <= (creativeLayout.searchX + creativeLayout.searchW) &&
                                        my >= creativeLayout.searchY &&
                                        my <= (creativeLayout.searchY + creativeLayout.searchH);
            }
            if (leftNow && !prevLeft) {
                const bool onTrack = mx >= (creativeLayout.trackX - 6.0f) &&
                                     mx <= (creativeLayout.trackX + creativeLayout.trackW + 6.0f) &&
                                     my >= creativeLayout.trackY &&
                                     my <= (creativeLayout.trackY + creativeLayout.trackH);
                if (onTrack) {
                    const float thumbTravel =
                        std::max(0.0f, creativeLayout.trackH - creativeLayout.thumbH);
                    const float thumbY =
                        creativeLayout.trackY +
                        ((creativeLayout.maxScroll > 0.0f)
                             ? (creativeMenuScroll / creativeLayout.maxScroll) * thumbTravel
                             : 0.0f);
                    const bool onThumb = my >= thumbY && my <= (thumbY + creativeLayout.thumbH);
                    if (onThumb) {
                        creativeScrollDragging = true;
                        creativeScrollGrabOffsetY = static_cast<float>(my) - thumbY;
                    } else if (creativeLayout.maxScroll > 0.0f) {
                        const float t =
                            std::clamp(static_cast<float>((my - creativeLayout.trackY -
                                                           creativeLayout.thumbH * 0.5f) /
                                                          std::max(1.0f, thumbTravel)),
                                       0.0f, 1.0f);
                        creativeMenuScroll = t * creativeLayout.maxScroll;
                    }
                }
            }
            if (!leftNow) {
                creativeScrollDragging = false;
            }
            recipeSearchFocused = false;
            if (creativeSearchFocused) {
                app::menus::TextInputMenu::bind(window, &creativeSearchText);
            } else {
                app::menus::TextInputMenu::unbind(window);
            }
            if (creativeScrollDragging && creativeLayout.maxScroll > 0.0f) {
                const float thumbTravel =
                    std::max(0.0f, creativeLayout.trackH - creativeLayout.thumbH);
                const float thumbY =
                    std::clamp(static_cast<float>(my) - creativeScrollGrabOffsetY,
                               creativeLayout.trackY, creativeLayout.trackY + thumbTravel);
                const float t =
                    (thumbTravel > 0.0f) ? (thumbY - creativeLayout.trackY) / thumbTravel : 0.0f;
                creativeMenuScroll = t * creativeLayout.maxScroll;
            }
            creativeMenuScroll = std::clamp(creativeMenuScroll, 0.0f, creativeLayout.maxScroll);
        } else {
            recipeScrollDragging = false;
            creativeScrollDragging = false;
            recipeSearchFocused = false;
            creativeSearchFocused = false;
            app::menus::TextInputMenu::unbind(window);
        }
        gRecipeMenuScrollDelta = 0.0f;

        int hoveredInventorySlot = -1;
        if (inventoryVisible && !menuOpen && !pauseMenuOpen) {
            int winW = 1;
            int winH = 1;
            glfwGetWindowSize(window, &winW, &winH);
            double mx = 0.0;
            double my = 0.0;
            glfwGetCursorPos(window, &mx, &my);
            hoveredInventorySlot = craftingMenu.inventorySlotAtCursor(
                mx, my, winW, winH, true, debugCfg.hudScale, craftingGridSize, usingFurnace);
        }

        const bool recipeIngredientKeyDown = glfwGetKey(window, GLFW_KEY_U) == GLFW_PRESS;
        if (recipeIngredientKeyDown && !prevRecipeIngredientFilterKey && inventoryVisible &&
            !menuOpen && !pauseMenuOpen) {
            voxel::BlockId hoveredId = voxel::AIR;
            if (hoveredInventorySlot >= 0 && hoveredInventorySlot < game::Inventory::kSlotCount) {
                const auto &s = inventory.slot(hoveredInventorySlot);
                if (s.count > 0 && s.id != voxel::AIR) {
                    hoveredId = s.id;
                }
            } else if (hoveredInventorySlot >= game::Inventory::kSlotCount &&
                       hoveredInventorySlot < game::Inventory::kSlotCount +
                                                  app::menus::CraftingMenu::kCraftInputCount) {
                const auto &s =
                    usingFurnace
                        ? ((hoveredInventorySlot == game::Inventory::kSlotCount + 1)
                               ? smelting.fuel
                               : smelting.input)
                        : crafting.input[hoveredInventorySlot - game::Inventory::kSlotCount];
                if (s.count > 0 && s.id != voxel::AIR) {
                    hoveredId = s.id;
                }
            }

            if (hoveredId != voxel::AIR) {
                recipeMenuVisible = true;
                creativeMenuVisible = false;
                creativeScrollDragging = false;
                creativeSearchFocused = false;
                creativeSearchText.clear();
                recipeMenuScroll = 0.0f;
                recipeSearchFocused = false;
                recipeIngredientFilter = hoveredId;
                recipeSearchText.clear();
                app::menus::TextInputMenu::unbind(window);
            } else {
                recipeIngredientFilter.reset();
            }
        }
        prevRecipeIngredientFilterKey = recipeIngredientKeyDown;

        // Placement block selection (keys 1..9).
        if (!pauseMenuOpen) {
            for (int i = 0; i < game::Inventory::kHotbarSize; ++i) {
                const int key = GLFW_KEY_1 + i;
                const bool down = glfwGetKey(window, key) == GLFW_PRESS;
                if (down && !prevBlockKeys[i]) {
                    if (inventoryVisible && !menuOpen && hoveredInventorySlot >= 0 &&
                        hoveredInventorySlot < game::Inventory::kSlotCount) {
                        std::swap(inventory.slot(i), inventory.slot(hoveredInventorySlot));
                    } else if (!inventoryVisible) {
                        selectedBlockIndex = i;
                        const voxel::BlockId selected = inventory.hotbarSlot(selectedBlockIndex).id;
                        core::Logger::instance().info(std::string("Selected slot block: ") +
                                                      game::blockName(selected));
                    }
                }
                prevBlockKeys[i] = down;
            }
        } else {
            prevBlockKeys.fill(false);
        }
        const bool dropDown = glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS;
        if (dropDown && !prevDropKey && !menuOpen && !inventoryVisible && !pauseMenuOpen &&
            !mapOpen) {
            auto &handSlot = inventory.slot(selectedBlockIndex);
            if (handSlot.id != voxel::AIR && handSlot.count > 0) {
                glm::vec3 dropPos =
                    camera.position() + camera.forward() * 2.10f + glm::vec3(0.0f, -0.30f, 0.0f);
                itemDrops.spawn(handSlot.id, dropPos, 1);
                handSlot.count -= 1;
                if (handSlot.count <= 0) {
                    handSlot = {};
                }
            }
        }
        prevDropKey = dropDown;

        std::optional<voxel::RayHit> currentHit;
        if (!blockInput) {
            currentHit = voxel::Raycaster::cast(world, camera.position(), camera.forward(),
                                                debugCfg.raycastDistance);
        }

        const bool left = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        const bool right = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

        handleMapAndWaypoints(left, right, currentCursorMode);

        if (handlePauseMenuInput(window, pauseMenu, left, prevLeft, pauseMenuOpen, returnToTitle,
                                 saveCurrentPlayer)) {
            break;
        }

        handleInventoryAndCraftingUi(menuOpen, left, right, now, filteredRecipeIndices,
                                     filteredSmeltingIndices, filteredCreativeItems,
                                     clearCraftInputs);

        handleWorldInteractionAndMovement(dt, blockInput, currentHit, left, right);

        int fbw = 1;
        int fbh = 1;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        const float aspect =
            (fbh > 0) ? static_cast<float>(fbw) / static_cast<float>(fbh) : 16.0f / 9.0f;
        const glm::mat4 proj = glm::perspective(glm::radians(debugCfg.fov), aspect, 0.1f, 500.0f);
        const glm::mat4 view = camera.view();

        const float dayPhase = dayClockSeconds / kDayLengthSeconds;
        const float sunAngle = dayPhase * (glm::pi<float>() * 2.0f);
        const float sunHeight = std::sin(sunAngle);
        const float daylight = glm::smoothstep(-0.10f, 0.20f, sunHeight);
        const float twilight = (1.0f - glm::clamp(std::abs(sunHeight) * 3.0f, 0.0f, 1.0f)) *
                               (0.65f + 0.35f * (1.0f - daylight));

        const glm::vec3 daySky(0.56f, 0.79f, 0.99f);
        const glm::vec3 duskSky(0.96f, 0.56f, 0.30f);
        const glm::vec3 nightSky(0.02f, 0.03f, 0.08f);
        glm::vec3 skyColor = glm::mix(nightSky, daySky, daylight);
        skyColor = glm::mix(skyColor, duskSky, twilight);

        const glm::vec3 sunDir = glm::normalize(glm::vec3(std::cos(sunAngle), sunHeight, 0.0f));
        const glm::vec3 nightTint(0.58f, 0.66f, 0.92f);
        const glm::vec3 dayTint(1.00f, 1.00f, 1.00f);
        const glm::vec3 duskTint(1.14f, 0.88f, 0.70f);
        glm::vec3 skyTint = glm::mix(nightTint, dayTint, daylight);
        skyTint = glm::mix(skyTint, duskTint, twilight * 0.65f);

        glClearColor(skyColor.r, skyColor.g, skyColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const glm::vec3 camForward = camera.forward();
        glm::vec3 camRight = glm::cross(camForward, glm::vec3(0.0f, 1.0f, 0.0f));
        if (glm::dot(camRight, camRight) < 1e-5f) {
            camRight = glm::vec3(1.0f, 0.0f, 0.0f);
        } else {
            camRight = glm::normalize(camRight);
        }
        const glm::vec3 camUp = glm::normalize(glm::cross(camRight, camForward));
        const glm::vec3 skyAnchor = camera.position() + glm::vec3(0.0f, 28.0f, 0.0f);
        const glm::vec3 sunCenter = skyAnchor + sunDir * 210.0f;
        const glm::vec3 moonCenter = skyAnchor - sunDir * 210.0f;
        const float sunVis = glm::smoothstep(-0.06f, 0.16f, sunHeight);
        const float moonVis = glm::smoothstep(0.08f, -0.16f, sunHeight);
        const float moonPhase01 = std::clamp(debugCfg.moonPhase01, 0.0f, 1.0f);
        const glm::vec3 sunColor = glm::vec3(1.00f, 0.93f, 0.64f) * (0.28f + 0.72f * sunVis);
        const glm::vec3 moonColor = glm::vec3(0.79f, 0.85f, 0.96f) * (0.28f + 0.72f * moonVis);
        const bool sunDominant = sunHeight >= 0.0f;
        const glm::vec3 celestialDir = sunDominant ? sunDir : -sunDir;
        const float celestialStrength = sunDominant ? (0.92f * sunVis) : (0.36f * moonVis);
        auto skyBillboardAxes = [&](const glm::vec3 &center) {
            const glm::vec3 toBody = glm::normalize(center - camera.position());
            const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
            glm::vec3 right = glm::cross(worldUp, toBody);
            if (glm::dot(right, right) < 1e-5f) {
                right = glm::vec3(1.0f, 0.0f, 0.0f);
            } else {
                right = glm::normalize(right);
            }
            const glm::vec3 up = glm::normalize(glm::cross(toBody, right));
            return std::pair<glm::vec3, glm::vec3>{right, up};
        };
        const float starVis = glm::clamp((1.0f - daylight) * (0.65f + 0.35f * moonVis), 0.0f, 1.0f);
        const float cloudVis = glm::clamp(0.20f + 0.60f * daylight + 0.25f * twilight, 0.0f, 0.95f);
        const float cloudLayerY = kCloudLayerY;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        if (debugCfg.showStars && starVis > 0.01f) {
            constexpr int kStarCount = 140;
            for (int i = 0; i < kStarCount; ++i) {
                const float az = hash01(i, 13) * (glm::pi<float>() * 2.0f) + dayPhase * 0.35f;
                const float el = glm::mix(0.10f, 1.12f, hash01(i, 31));
                const float ce = std::cos(el);
                glm::vec3 dir(std::cos(az) * ce, std::sin(el), std::sin(az) * ce);
                dir = glm::normalize(dir);
                if (dir.y < 0.02f) {
                    continue;
                }
                const glm::vec3 center = skyAnchor + dir * 235.0f;
                const float twinkle =
                    0.72f + 0.28f * std::sin(now * (2.0f + hash01(i, 53) * 3.0f) +
                                             hash01(i, 71) * (glm::pi<float>() * 2.0f));
                const float radius = 0.58f + 0.78f * hash01(i, 97);
                const glm::vec3 starColor =
                    glm::mix(glm::vec3(0.72f, 0.80f, 1.00f), glm::vec3(1.0f), hash01(i, 43));
                const auto axes = skyBillboardAxes(center);
                skyBodyRenderer.draw(proj, view, center, axes.first, axes.second, radius,
                                     starColor * (0.35f + 0.75f * starVis), 0.10f * twinkle,
                                     gfx::SkyBodyRenderer::BodyType::Star, 0.0f);
            }
        }

        if (sunCenter.y + 16.0f > camera.position().y) {
            const auto axes = skyBillboardAxes(sunCenter);
            skyBodyRenderer.draw(proj, view, sunCenter, axes.first, axes.second, 16.0f, sunColor,
                                0.07f + 0.15f * sunVis, gfx::SkyBodyRenderer::BodyType::Sun,
                                0.0f);
        }
        if (moonCenter.y + 14.0f > camera.position().y) {
            const auto axes = skyBillboardAxes(moonCenter);
            skyBodyRenderer.draw(proj, view, moonCenter, axes.first, axes.second, 14.0f, moonColor,
                                0.03f + 0.05f * moonVis, gfx::SkyBodyRenderer::BodyType::Moon,
                                moonPhase01);
        }

        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);

        const bool wireframe = debugCfg.renderMode == game::RenderMode::Wireframe;
        glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);

        shader.use();
        shader.setMat4("uProj", proj);
        shader.setMat4("uView", view);
        shader.setFloat("uDaylight", daylight);
        shader.setVec3("uSkyTint", skyTint);
        shader.setVec3("uCelestialDir", celestialDir);
        shader.setFloat("uCelestialStrength", celestialStrength);
        shader.setVec3("uPlayerPos", camera.position());
        const float renderEdge = static_cast<float>(debugCfg.loadRadius * voxel::Chunk::SX);
        const float fogFar = std::max(28.0f, renderEdge - 16.0f);
        const float fogNear = std::max(8.0f, fogFar - std::max(52.0f, renderEdge * 0.72f));
        const glm::vec3 fogColor = glm::mix(skyColor, glm::vec3(0.80f, 0.86f, 0.95f), 0.22f);
        shader.setVec3("uFogColor", fogColor);
        shader.setFloat("uFogNear", debugCfg.showFog ? fogNear : 1000000.0f);
        shader.setFloat("uFogFar", debugCfg.showFog ? fogFar : 1000001.0f);
        shader.setFloat("uCloudShadowEnabled", debugCfg.showClouds ? 1.0f : 0.0f);
        shader.setFloat("uCloudShadowTime", now);
        shader.setFloat("uCloudShadowStrength", 0.32f);
        shader.setFloat("uCloudShadowDay", sunVis);
        shader.setFloat("uCloudLayerY", cloudLayerY);
        shader.setFloat("uCloudShadowRange", kCloudShadowRange);
        const voxel::BlockId heldId = inventory.hotbarSlot(selectedBlockIndex).id;
        const float heldTorchStrength = voxel::isTorch(heldId) ? 0.90f : 0.0f;
        shader.setFloat("uHeldTorchStrength", heldTorchStrength);
        atlas.bind(0);
        shader.setInt("uAtlas", 0);
        shader.setInt("uRenderMode", debugCfg.renderMode == game::RenderMode::Textured ? 0 : 1);
        if (debugCfg.renderMode == game::RenderMode::Textured) {
            // Pass 1: opaque geometry writes depth.
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            shader.setInt("uAlphaPass", 0);
            world.draw();

            // Pass 2: transparent geometry blends over opaque; no depth writes.
            glEnable(GL_BLEND);
            glDepthMask(GL_TRUE);
            shader.setInt("uAlphaPass", 1);
            world.drawTransparent(camera.position(), camera.forward());
        } else {
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            shader.setInt("uAlphaPass", 2);
            world.draw();
        }

        if (debugCfg.showClouds && cloudVis > 0.01f) {
            constexpr int kRange = kCloudRenderRange;
            const float cell = kCloudCellSize;
            const float cloudY = cloudLayerY;
            const float driftX = now * kCloudDriftXSpeed;
            const float driftZ = now * kCloudDriftZSpeed;
            auto cloudHashU32 = [](std::uint32_t x) {
                x ^= x >> 16u;
                x *= 0x7feb352du;
                x ^= x >> 15u;
                x *= 0x846ca68bu;
                x ^= x >> 16u;
                return x;
            };
            auto cloudHash = [cloudHashU32](int x, int z, int salt) {
                const auto u32 = [](int v) { return static_cast<std::uint32_t>(v); };
                const std::uint32_t h = cloudHashU32(
                    u32(x) ^ (cloudHashU32(u32(z) + 0x9e3779b9u) + u32(salt) * 0x85ebca6bu));
                return static_cast<float>(h & 0x00ffffffu) * (1.0f / 16777215.0f);
            };
            const int centerGX =
                static_cast<int>(std::floor((camera.position().x - driftX) / cell));
            const int centerGZ =
                static_cast<int>(std::floor((camera.position().z - driftZ) / cell));
            const glm::vec3 cloudRight(1.0f, 0.0f, 0.0f);
            const glm::vec3 cloudUp(0.0f, 0.0f, 1.0f);
            const glm::vec3 cloudColor =
                glm::mix(glm::vec3(0.80f, 0.86f, 0.92f), glm::vec3(1.00f, 1.00f, 1.00f),
                         0.42f + 0.45f * daylight);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);
            for (int gz = -kRange; gz <= kRange; ++gz) {
                for (int gx = -kRange; gx <= kRange; ++gx) {
                    const int gxi = centerGX + gx;
                    const int gzi = centerGZ + gz;
                    const int fx = static_cast<int>(std::floor(static_cast<float>(gxi) * 0.5f));
                    const int fz = static_cast<int>(std::floor(static_cast<float>(gzi) * 0.5f));

                    const float base = cloudHash(fx, fz, 503);
                    const float nE = cloudHash(fx + 1, fz, 503);
                    const float nW = cloudHash(fx - 1, fz, 503);
                    const float nN = cloudHash(fx, fz + 1, 503);
                    const float nS = cloudHash(fx, fz - 1, 503);
                    const bool core = base > 0.77f;
                    const bool fringe =
                        (base > 0.69f) && (std::max(std::max(nE, nW), std::max(nN, nS)) > 0.78f);
                    if (!(core || fringe)) {
                        continue;
                    }

                    const glm::vec3 center((static_cast<float>(gxi) * cell) + driftX, cloudY,
                                           (static_cast<float>(gzi) * cell) + driftZ);
                    skyBodyRenderer.draw(proj, view, center, cloudRight, cloudUp,
                                         cell * kCloudQuadRadius,
                                         cloudColor * (0.48f + 0.52f * cloudVis), 0.0f,
                                        gfx::SkyBodyRenderer::BodyType::Cloud, 0.0f);
                }
            }
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            glDisable(GL_BLEND);
        }

        if (debugCfg.showChunkBorders) {
            std::vector<glm::vec3> borderVerts;
            borderVerts.reserve((voxel::Chunk::SY + 1) * 8 +
                                (voxel::Chunk::SX + voxel::Chunk::SZ + 2) * 8);
            auto pushEdge = [&](const glm::vec3 &a, const glm::vec3 &b) {
                borderVerts.push_back(a);
                borderVerts.push_back(b);
            };
            const int playerChunkX =
                static_cast<int>(std::floor(camera.position().x / voxel::Chunk::SX));
            const int playerChunkZ =
                static_cast<int>(std::floor(camera.position().z / voxel::Chunk::SZ));
            const float x0 = static_cast<float>(playerChunkX * voxel::Chunk::SX);
            const float x1 = x0 + static_cast<float>(voxel::Chunk::SX);
            const float z0 = static_cast<float>(playerChunkZ * voxel::Chunk::SZ);
            const float z1 = z0 + static_cast<float>(voxel::Chunk::SZ);
            const float yTop = static_cast<float>(voxel::Chunk::SY);

            // Horizontal perimeter lines at every block height to show chunk edge bands.
            for (int y = 0; y <= voxel::Chunk::SY; ++y) {
                const float yy = static_cast<float>(y);
                pushEdge({x0, yy, z0}, {x1, yy, z0});
                pushEdge({x1, yy, z0}, {x1, yy, z1});
                pushEdge({x1, yy, z1}, {x0, yy, z1});
                pushEdge({x0, yy, z1}, {x0, yy, z0});
            }
            // Vertical strips on every block boundary along each chunk edge.
            for (int lx = 0; lx <= voxel::Chunk::SX; ++lx) {
                const float xx = x0 + static_cast<float>(lx);
                pushEdge({xx, 0.0f, z0}, {xx, yTop, z0});
                pushEdge({xx, 0.0f, z1}, {xx, yTop, z1});
            }
            for (int lz = 0; lz <= voxel::Chunk::SZ; ++lz) {
                const float zz = z0 + static_cast<float>(lz);
                pushEdge({x0, 0.0f, zz}, {x0, yTop, zz});
                pushEdge({x1, 0.0f, zz}, {x1, yTop, zz});
            }

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glLineWidth(2.0f);
            chunkBorderRenderer.draw(proj, view, borderVerts);
            glLineWidth(1.0f);
            glDepthMask(GL_TRUE);
        }
        itemDrops.render(proj, view, atlas, hudRegistry);
        std::optional<glm::ivec3> highlightedBlock;
        if (hudVisible && currentHit.has_value()) {
            highlightedBlock = currentHit->block;
        }
        if (hudVisible) {
            float breakProgress = 0.0f;
            voxel::BlockId breakBlockId = voxel::AIR;
            if (highlightedBlock.has_value() && miningBlock.has_value() &&
                highlightedBlock.value() == miningBlock.value()) {
                breakProgress = miningProgress;
                breakBlockId =
                    world.getBlock(highlightedBlock->x, highlightedBlock->y, highlightedBlock->z);
            }
            hud.renderBreakOverlay(proj, view, highlightedBlock, breakProgress, breakBlockId,
                                   atlas);
            hud.renderBlockOutline(proj, view, highlightedBlock, breakProgress);
            hud.renderWorldWaypoints(proj, view, mapSystem, camera.position(),
                                     [&](int wx, int wz) { return world.isChunkLoadedAt(wx, wz); });
        }
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        int winW = 1;
        int winH = 1;
        glfwGetWindowSize(window, &winW, &winH);
        if (hudVisible) {
            std::vector<game::CraftingSystem::RecipeInfo> visibleRecipes;
            std::vector<bool> visibleRecipeCraftable;
            std::vector<game::SmeltingSystem::Recipe> visibleSmeltingRecipes;
            if (recipeMenuVisible && !usingFurnace) {
                visibleRecipes.reserve(filteredRecipeIndices.size());
                visibleRecipeCraftable.reserve(filteredRecipeIndices.size());
                for (int idx : filteredRecipeIndices) {
                    const auto &recipe = craftingSystem.recipeInfos()[idx];
                    visibleRecipes.push_back(recipe);
                    visibleRecipeCraftable.push_back(isRecipeCraftableNow(idx));
                }
            } else if (recipeMenuVisible && usingFurnace) {
                const auto &smeltRecipes = smeltingSystem.recipes();
                visibleSmeltingRecipes.reserve(filteredSmeltingIndices.size());
                visibleRecipeCraftable.reserve(filteredSmeltingIndices.size());
                int fuelCount = 0;
                for (int i = 0; i < game::Inventory::kSlotCount; ++i) {
                    const auto &slot = inventory.slot(i);
                    if (slot.id != voxel::AIR && slot.count > 0 && smeltingSystem.isFuel(slot.id)) {
                        fuelCount += slot.count;
                    }
                }
                for (int idx : filteredSmeltingIndices) {
                    const auto &recipe = smeltRecipes[idx];
                    visibleSmeltingRecipes.push_back(recipe);
                    int inputCount = 0;
                    for (int s = 0; s < game::Inventory::kSlotCount; ++s) {
                        const auto &slot = inventory.slot(s);
                        if (slot.id == recipe.input && slot.count > 0) {
                            inputCount += slot.count;
                        }
                    }
                    visibleRecipeCraftable.push_back(inputCount > 0 && fuelCount > 0);
                }
            }
            std::string lookedAt = "Looking: (none)";
            if (currentHit.has_value()) {
                const voxel::BlockId lookedId =
                    world.getBlock(currentHit->block.x, currentHit->block.y, currentHit->block.z);
                lookedAt = std::string("Looking: ") + game::blockName(lookedId);
            }
            const glm::vec3 pos = camera.position();
            const auto hotbarIds = inventory.hotbarIds();
            const auto hotbarCounts = inventory.hotbarCounts();
            const auto allIds = inventory.slotIds();
            const auto allCounts = inventory.slotCounts();
            double cursorX = 0.0;
            double cursorY = 0.0;
            if (inventoryVisible) {
                glfwGetCursorPos(window, &cursorX, &cursorY);
            }
            const voxel::BlockId selectedPlaceBlock = inventory.hotbarSlot(selectedBlockIndex).id;
            std::string modeText = "Walking";
            if (ghostMode) {
                modeText = "Mode: Ghost";
            } else if (player.inWater()) {
                modeText = "Swimming";
            } else if (player.sprinting()) {
                modeText = "Sprinting";
            } else if (player.crouching()) {
                modeText = "Crouching";
            }
            const std::string compassText = compassTextFromForward(camera.forward());
            const int bx = static_cast<int>(std::floor(pos.x));
            const int by = static_cast<int>(std::floor(pos.y));
            const int bz = static_cast<int>(std::floor(pos.z));
            std::ostringstream coord;
            coord << "XYZ: " << bx << ", " << by << ", " << bz;
            const std::string biomeText = std::string("Biome: ") + world.biomeLabelAt(bx, bz);
            hud.render2D(
                winW, winH, selectedBlockIndex, hotbarIds, hotbarCounts, allIds, allCounts,
                inventoryVisible, carriedSlot.id, carriedSlot.count, static_cast<float>(cursorX),
                static_cast<float>(cursorY), hoveredInventorySlot, debugCfg.hudScale,
                crafting.input, craftingGridSize, usingCraftingTable, usingFurnace, smelting.input,
                smelting.fuel, smelting.output,
                smelting.progressSeconds / game::SmeltingSystem::kSmeltSeconds,
                (smelting.burnSecondsCapacity > 0.0f)
                    ? (smelting.burnSecondsRemaining / smelting.burnSecondsCapacity)
                    : 0.0f,
                recipeMenuVisible, visibleRecipes, visibleSmeltingRecipes, visibleRecipeCraftable,
                recipeMenuScroll, static_cast<float>(glfwGetTime()), recipeSearchText,
                creativeMenuVisible, filteredCreativeItems, creativeMenuScroll, creativeSearchText,
                recipeCraftableOnly, recipeIngredientFilter, crafting.output,
                (carriedSlot.id == voxel::AIR || carriedSlot.count <= 0)
                    ? ""
                    : game::blockName(carriedSlot.id),
                (selectedPlaceBlock == voxel::AIR) ? "Empty Slot"
                                                   : game::blockName(selectedPlaceBlock),
                lookedAt, modeText, player.health01(), player.sprintStamina01(), fpsAvgDisplay,
                compassText, biomeText, coord.str(), hudRegistry, atlas);
        }
        renderOverlayMenus(window, hud, winW, winH, pauseMenu, pauseMenuOpen, worldMapMenu,
                           miniMapMenu, mapOpen, hudVisible, mapSystem, camera, mapCenterWX,
                           mapCenterWZ, mapZoom, selectedWaypointIndex, waypointNameFocused,
                           waypointEditorOpen, miniMapZoom, miniMapNorthLocked, miniMapShowCompass,
                           miniMapShowWaypoints, hudRegistry, atlas, debugMenu);
        maybeUpdateWindowTitle(window, debugMenu, debugCfg, stats, fps, dt, titleAccum);
    }

    app::menus::TextInputMenu::unbind(window);
    saveCurrentPlayer();

    return returnToTitle;
}

} // namespace app
