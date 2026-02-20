#include "game/Inventory.hpp"

namespace game {

Inventory::Inventory() {
    add(voxel::DIRT, 32);
    add(voxel::STONE, 32);
    add(voxel::SAND, 24);
    add(voxel::WOOD, 24);
    add(voxel::LEAVES, 24);
    add(voxel::WATER, 16);
}

bool Inventory::add(voxel::BlockId id, int count) {
    if (id == voxel::AIR || count <= 0) {
        return false;
    }

    int remaining = count;

    for (Slot &slot : slots_) {
        if (slot.id != id || slot.count >= kMaxStack) {
            continue;
        }
        const int can = kMaxStack - slot.count;
        const int take = (remaining < can) ? remaining : can;
        slot.count += take;
        remaining -= take;
        if (remaining == 0) {
            return true;
        }
    }

    for (Slot &slot : slots_) {
        if (slot.count != 0) {
            continue;
        }
        const int take = (remaining < kMaxStack) ? remaining : kMaxStack;
        slot.id = id;
        slot.count = take;
        remaining -= take;
        if (remaining == 0) {
            return true;
        }
    }

    return remaining != count;
}

bool Inventory::consumeHotbar(int hotbarIndex, int count) {
    if (hotbarIndex < 0 || hotbarIndex >= kHotbarSize || count <= 0) {
        return false;
    }
    Slot &slot = slots_[hotbarIndex];
    if (slot.count < count || slot.id == voxel::AIR) {
        return false;
    }
    slot.count -= count;
    if (slot.count == 0) {
        slot.id = voxel::AIR;
    }
    return true;
}

std::array<voxel::BlockId, Inventory::kHotbarSize> Inventory::hotbarIds() const {
    std::array<voxel::BlockId, kHotbarSize> out{};
    for (int i = 0; i < kHotbarSize; ++i) {
        out[i] = slots_[i].id;
    }
    return out;
}

std::array<int, Inventory::kHotbarSize> Inventory::hotbarCounts() const {
    std::array<int, kHotbarSize> out{};
    for (int i = 0; i < kHotbarSize; ++i) {
        out[i] = slots_[i].count;
    }
    return out;
}

std::array<voxel::BlockId, Inventory::kSlotCount> Inventory::slotIds() const {
    std::array<voxel::BlockId, kSlotCount> out{};
    for (int i = 0; i < kSlotCount; ++i) {
        out[i] = slots_[i].id;
    }
    return out;
}

std::array<int, Inventory::kSlotCount> Inventory::slotCounts() const {
    std::array<int, kSlotCount> out{};
    for (int i = 0; i < kSlotCount; ++i) {
        out[i] = slots_[i].count;
    }
    return out;
}

} // namespace game
