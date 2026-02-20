#pragma once

#include <string>
#include <vector>

namespace game {

class AudioSystem {
  public:
    enum class SoundProfile : int {
        Default = 0,
        Dirt,
        Grass,
        Stone,
        Wood,
        Foliage,
        Sand,
        Snow,
        Ice,
        Count
    };

    AudioSystem() = default;
    ~AudioSystem();

    AudioSystem(const AudioSystem &) = delete;
    AudioSystem &operator=(const AudioSystem &) = delete;

    bool init();
    bool loadPickupSounds(const std::vector<std::string> &paths);
    bool loadBreakSounds(SoundProfile profile, const std::vector<std::string> &paths);
    bool loadFootstepSounds(SoundProfile profile, const std::vector<std::string> &paths);
    bool loadPlaceSounds(SoundProfile profile, const std::vector<std::string> &paths);
    bool loadSwimSounds(const std::vector<std::string> &paths);
    bool loadWaterBobSounds(const std::vector<std::string> &paths);
    void playPickup();
    void playBreak(SoundProfile profile);
    void playFootstep(SoundProfile profile);
    void playPlace(SoundProfile profile);
    void playSwim();
    void playWaterBob();
    bool isReady() const {
        return ready_;
    }

  private:
    bool ready_ = false;

    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace game
