#pragma once

#include "game/AudioSystem.hpp"
#include "voxel/Block.hpp"

#include <glm/vec3.hpp>

#include <string>

namespace world {
class World;
}

namespace game {

const char *blockName(voxel::BlockId id);
std::string biomeHintFromSurface(const world::World &world, const glm::vec3 &pos);
float breakSeconds(voxel::BlockId id);
AudioSystem::SoundProfile soundProfileForBlock(voxel::BlockId id);
AudioSystem::SoundProfile footstepProfileForGround(voxel::BlockId id);
voxel::BlockId blockUnderPlayer(const world::World &world, const glm::vec3 &cameraPos);

} // namespace game
