#pragma once

#include "voxel/Block.hpp"

#include <array>

namespace game {

class Inventory {
  public:
    static constexpr int kHotbarSize = 9;
    static constexpr int kColumns = 9;
    static constexpr int kRows = 5;
    static constexpr int kSlotCount = 45;
    static constexpr int kMaxStack = 64;

    struct Slot {
        voxel::BlockId id = voxel::AIR;
        int count = 0;
    };

    Inventory();

    bool add(voxel::BlockId id, int count);
    bool consumeHotbar(int hotbarIndex, int count);

    const Slot &hotbarSlot(int index) const {
        return slots_[index];
    }
    const Slot &slot(int index) const {
        return slots_[index];
    }
    Slot &slot(int index) {
        return slots_[index];
    }
    std::array<voxel::BlockId, kHotbarSize> hotbarIds() const;
    std::array<int, kHotbarSize> hotbarCounts() const;
    std::array<voxel::BlockId, kSlotCount> slotIds() const;
    std::array<int, kSlotCount> slotCounts() const;

  private:
    std::array<Slot, kSlotCount> slots_{};
};

} // namespace game
