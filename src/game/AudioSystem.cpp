#include "game/AudioSystem.hpp"

#include "core/Logger.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#if defined(VOXEL_HAS_OPENAL)
#if defined(__APPLE__)
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif
#endif

namespace game {

#if defined(VOXEL_HAS_OPENAL)

namespace {

constexpr std::size_t kProfileCount = static_cast<std::size_t>(AudioSystem::SoundProfile::Count);

bool readU16(std::istream &in, std::uint16_t &out) {
    char b[2];
    in.read(b, sizeof(b));
    if (!in) {
        return false;
    }
    out = static_cast<std::uint16_t>(static_cast<unsigned char>(b[0])) |
          (static_cast<std::uint16_t>(static_cast<unsigned char>(b[1])) << 8);
    return true;
}

bool readU32(std::istream &in, std::uint32_t &out) {
    char b[4];
    in.read(b, sizeof(b));
    if (!in) {
        return false;
    }
    out = static_cast<std::uint32_t>(static_cast<unsigned char>(b[0])) |
          (static_cast<std::uint32_t>(static_cast<unsigned char>(b[1])) << 8) |
          (static_cast<std::uint32_t>(static_cast<unsigned char>(b[2])) << 16) |
          (static_cast<std::uint32_t>(static_cast<unsigned char>(b[3])) << 24);
    return true;
}

bool loadWav(const std::string &path, ALuint buffer) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        core::Logger::instance().warn("Audio file missing: " + path);
        return false;
    }

    char riff[4];
    in.read(riff, 4);
    if (std::string(riff, 4) != "RIFF") {
        core::Logger::instance().warn("Invalid WAV header: " + path);
        return false;
    }

    std::uint32_t riffSize = 0;
    (void)readU32(in, riffSize);

    char wave[4];
    in.read(wave, 4);
    if (std::string(wave, 4) != "WAVE") {
        core::Logger::instance().warn("Invalid WAV format: " + path);
        return false;
    }

    bool foundFmt = false;
    bool foundData = false;
    std::uint16_t audioFormat = 0;
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
    std::vector<char> pcmData;

    while (in && (!foundFmt || !foundData)) {
        char chunkId[4];
        in.read(chunkId, 4);
        if (!in) {
            break;
        }
        std::uint32_t chunkSize = 0;
        if (!readU32(in, chunkSize)) {
            break;
        }
        const std::string id(chunkId, 4);
        if (id == "fmt ") {
            std::uint16_t blockAlign = 0;
            std::uint32_t byteRate = 0;
            if (!readU16(in, audioFormat) || !readU16(in, channels) || !readU32(in, sampleRate) ||
                !readU32(in, byteRate) || !readU16(in, blockAlign) || !readU16(in, bitsPerSample)) {
                return false;
            }
            if (chunkSize > 16) {
                in.seekg(static_cast<std::streamoff>(chunkSize - 16), std::ios::cur);
            }
            foundFmt = true;
        } else if (id == "data") {
            pcmData.resize(chunkSize);
            in.read(pcmData.data(), static_cast<std::streamsize>(chunkSize));
            if (!in) {
                return false;
            }
            foundData = true;
        } else {
            in.seekg(static_cast<std::streamoff>(chunkSize), std::ios::cur);
        }

        if ((chunkSize % 2) != 0) {
            in.seekg(1, std::ios::cur);
        }
    }

    if (!foundFmt || !foundData) {
        core::Logger::instance().warn("Incomplete WAV file: " + path);
        return false;
    }
    if (audioFormat != 1) {
        core::Logger::instance().warn("Only PCM WAV is supported: " + path);
        return false;
    }

    ALenum format = 0;
    if (channels == 1 && bitsPerSample == 8) {
        format = AL_FORMAT_MONO8;
    } else if (channels == 1 && bitsPerSample == 16) {
        format = AL_FORMAT_MONO16;
    } else if (channels == 2 && bitsPerSample == 8) {
        format = AL_FORMAT_STEREO8;
    } else if (channels == 2 && bitsPerSample == 16) {
        format = AL_FORMAT_STEREO16;
    } else {
        core::Logger::instance().warn("Unsupported WAV channel/bit depth: " + path);
        return false;
    }

    alBufferData(buffer, format, pcmData.data(), static_cast<ALsizei>(pcmData.size()),
                 static_cast<ALsizei>(sampleRate));
    if (alGetError() != AL_NO_ERROR) {
        core::Logger::instance().warn("Failed to upload audio buffer: " + path);
        return false;
    }
    return true;
}

void releaseBuffers(std::vector<ALuint> &buffers) {
    if (!buffers.empty()) {
        alDeleteBuffers(static_cast<ALsizei>(buffers.size()), buffers.data());
        buffers.clear();
    }
}

void cleanStoppedSources(std::vector<ALuint> &activeSources) {
    activeSources.erase(std::remove_if(activeSources.begin(), activeSources.end(),
                                       [](ALuint src) {
                                           ALint state = AL_STOPPED;
                                           alGetSourcei(src, AL_SOURCE_STATE, &state);
                                           if (state == AL_STOPPED) {
                                               alDeleteSources(1, &src);
                                               return true;
                                           }
                                           return false;
                                       }),
                        activeSources.end());
}

void playFromPool(const std::vector<ALuint> &buffers, std::vector<ALuint> &activeSources,
                  float gain, float pitchMin, float pitchMax) {
    if (buffers.empty()) {
        return;
    }
    cleanStoppedSources(activeSources);

    const std::size_t idx = static_cast<std::size_t>(std::rand()) % buffers.size();
    ALuint src = 0;
    alGenSources(1, &src);
    if (alGetError() != AL_NO_ERROR || src == 0) {
        return;
    }

    alSourcei(src, AL_BUFFER, static_cast<ALint>(buffers[idx]));
    alSourcef(src, AL_GAIN, gain);
    const float t = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    alSourcef(src, AL_PITCH, pitchMin + t * (pitchMax - pitchMin));
    alSource3f(src, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSourcePlay(src);

    if (alGetError() == AL_NO_ERROR) {
        activeSources.push_back(src);
    } else {
        alDeleteSources(1, &src);
    }
}

std::size_t profileIndex(AudioSystem::SoundProfile profile) {
    return static_cast<std::size_t>(profile);
}

} // namespace

struct AudioSystem::Impl {
    ALCdevice *device = nullptr;
    ALCcontext *context = nullptr;
    std::vector<ALuint> pickupBuffers;
    std::vector<ALuint> swimBuffers;
    std::vector<ALuint> bobBuffers;
    std::array<std::vector<ALuint>, kProfileCount> breakPools;
    std::array<std::vector<ALuint>, kProfileCount> footstepPools;
    std::array<std::vector<ALuint>, kProfileCount> placePools;
    std::vector<ALuint> activeSources;
};

AudioSystem::~AudioSystem() {
    if (impl_ == nullptr) {
        return;
    }

    for (ALuint src : impl_->activeSources) {
        alSourceStop(src);
        alDeleteSources(1, &src);
    }
    impl_->activeSources.clear();

    releaseBuffers(impl_->pickupBuffers);
    releaseBuffers(impl_->swimBuffers);
    releaseBuffers(impl_->bobBuffers);
    for (auto &pool : impl_->breakPools) {
        releaseBuffers(pool);
    }
    for (auto &pool : impl_->footstepPools) {
        releaseBuffers(pool);
    }
    for (auto &pool : impl_->placePools) {
        releaseBuffers(pool);
    }

    if (impl_->context != nullptr) {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(impl_->context);
    }
    if (impl_->device != nullptr) {
        alcCloseDevice(impl_->device);
    }
    delete impl_;
    impl_ = nullptr;
}

bool AudioSystem::init() {
    if (ready_) {
        return true;
    }
    if (impl_ == nullptr) {
        impl_ = new Impl();
    }

    impl_->device = alcOpenDevice(nullptr);
    if (impl_->device == nullptr) {
        core::Logger::instance().warn("OpenAL unavailable: audio disabled");
        return false;
    }

    impl_->context = alcCreateContext(impl_->device, nullptr);
    if (impl_->context == nullptr) {
        core::Logger::instance().warn("Failed to create OpenAL context");
        alcCloseDevice(impl_->device);
        impl_->device = nullptr;
        return false;
    }

    if (alcMakeContextCurrent(impl_->context) != ALC_TRUE) {
        core::Logger::instance().warn("Failed to activate OpenAL context");
        alcDestroyContext(impl_->context);
        alcCloseDevice(impl_->device);
        impl_->context = nullptr;
        impl_->device = nullptr;
        return false;
    }

    alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
    alListenerf(AL_GAIN, 1.0f);

    ready_ = true;
    return true;
}

bool AudioSystem::loadPickupSounds(const std::vector<std::string> &paths) {
    if (!ready_ && !init()) {
        return false;
    }
    if (impl_ == nullptr) {
        return false;
    }

    releaseBuffers(impl_->pickupBuffers);

    impl_->pickupBuffers.reserve(paths.size());
    for (const std::string &path : paths) {
        ALuint buffer = 0;
        alGenBuffers(1, &buffer);
        if (alGetError() != AL_NO_ERROR || buffer == 0) {
            core::Logger::instance().warn("Failed to allocate audio buffer: " + path);
            continue;
        }
        if (!loadWav(path, buffer)) {
            alDeleteBuffers(1, &buffer);
            continue;
        }
        impl_->pickupBuffers.push_back(buffer);
    }

    if (impl_->pickupBuffers.empty()) {
        core::Logger::instance().warn("No pickup sounds loaded");
        return false;
    }
    return true;
}

bool AudioSystem::loadSwimSounds(const std::vector<std::string> &paths) {
    if (!ready_ && !init()) {
        return false;
    }
    if (impl_ == nullptr) {
        return false;
    }

    releaseBuffers(impl_->swimBuffers);
    impl_->swimBuffers.reserve(paths.size());
    for (const std::string &path : paths) {
        ALuint buffer = 0;
        alGenBuffers(1, &buffer);
        if (alGetError() != AL_NO_ERROR || buffer == 0) {
            core::Logger::instance().warn("Failed to allocate audio buffer: " + path);
            continue;
        }
        if (!loadWav(path, buffer)) {
            alDeleteBuffers(1, &buffer);
            continue;
        }
        impl_->swimBuffers.push_back(buffer);
    }

    if (impl_->swimBuffers.empty()) {
        core::Logger::instance().warn("No swim sounds loaded");
        return false;
    }
    return true;
}

bool AudioSystem::loadWaterBobSounds(const std::vector<std::string> &paths) {
    if (!ready_ && !init()) {
        return false;
    }
    if (impl_ == nullptr) {
        return false;
    }

    releaseBuffers(impl_->bobBuffers);
    impl_->bobBuffers.reserve(paths.size());
    for (const std::string &path : paths) {
        ALuint buffer = 0;
        alGenBuffers(1, &buffer);
        if (alGetError() != AL_NO_ERROR || buffer == 0) {
            core::Logger::instance().warn("Failed to allocate audio buffer: " + path);
            continue;
        }
        if (!loadWav(path, buffer)) {
            alDeleteBuffers(1, &buffer);
            continue;
        }
        impl_->bobBuffers.push_back(buffer);
    }

    if (impl_->bobBuffers.empty()) {
        core::Logger::instance().warn("No water bob sounds loaded");
        return false;
    }
    return true;
}

bool AudioSystem::loadBreakSounds(SoundProfile profile, const std::vector<std::string> &paths) {
    if (!ready_ && !init()) {
        return false;
    }
    if (impl_ == nullptr) {
        return false;
    }

    auto &breakPool = impl_->breakPools[profileIndex(profile)];
    releaseBuffers(breakPool);

    breakPool.reserve(paths.size());
    for (const std::string &path : paths) {
        ALuint buffer = 0;
        alGenBuffers(1, &buffer);
        if (alGetError() != AL_NO_ERROR || buffer == 0) {
            core::Logger::instance().warn("Failed to allocate audio buffer: " + path);
            continue;
        }
        if (!loadWav(path, buffer)) {
            alDeleteBuffers(1, &buffer);
            continue;
        }
        breakPool.push_back(buffer);
    }

    if (breakPool.empty()) {
        core::Logger::instance().warn("No break sounds loaded");
        return false;
    }
    return true;
}

bool AudioSystem::loadFootstepSounds(SoundProfile profile, const std::vector<std::string> &paths) {
    if (!ready_ && !init()) {
        return false;
    }
    if (impl_ == nullptr) {
        return false;
    }

    auto &footPool = impl_->footstepPools[profileIndex(profile)];
    releaseBuffers(footPool);

    footPool.reserve(paths.size());
    for (const std::string &path : paths) {
        ALuint buffer = 0;
        alGenBuffers(1, &buffer);
        if (alGetError() != AL_NO_ERROR || buffer == 0) {
            core::Logger::instance().warn("Failed to allocate audio buffer: " + path);
            continue;
        }
        if (!loadWav(path, buffer)) {
            alDeleteBuffers(1, &buffer);
            continue;
        }
        footPool.push_back(buffer);
    }

    if (footPool.empty()) {
        core::Logger::instance().warn("No footstep sounds loaded");
        return false;
    }
    return true;
}

bool AudioSystem::loadPlaceSounds(SoundProfile profile, const std::vector<std::string> &paths) {
    if (!ready_ && !init()) {
        return false;
    }
    if (impl_ == nullptr) {
        return false;
    }

    auto &placePool = impl_->placePools[profileIndex(profile)];
    releaseBuffers(placePool);

    placePool.reserve(paths.size());
    for (const std::string &path : paths) {
        ALuint buffer = 0;
        alGenBuffers(1, &buffer);
        if (alGetError() != AL_NO_ERROR || buffer == 0) {
            core::Logger::instance().warn("Failed to allocate audio buffer: " + path);
            continue;
        }
        if (!loadWav(path, buffer)) {
            alDeleteBuffers(1, &buffer);
            continue;
        }
        placePool.push_back(buffer);
    }

    if (placePool.empty()) {
        core::Logger::instance().warn("No place sounds loaded");
        return false;
    }
    return true;
}

void AudioSystem::playPickup() {
    if (!ready_ || impl_ == nullptr) {
        return;
    }
    playFromPool(impl_->pickupBuffers, impl_->activeSources, 0.56f, 0.97f, 1.04f);
}

void AudioSystem::playBreak(SoundProfile profile) {
    if (!ready_ || impl_ == nullptr) {
        return;
    }
    const auto &pool = impl_->breakPools[profileIndex(profile)];
    if (pool.empty()) {
        playFromPool(impl_->breakPools[profileIndex(SoundProfile::Default)], impl_->activeSources,
                     0.72f, 0.93f, 1.02f);
        return;
    }
    playFromPool(pool, impl_->activeSources, 0.72f, 0.93f, 1.02f);
}

void AudioSystem::playFootstep(SoundProfile profile) {
    if (!ready_ || impl_ == nullptr) {
        return;
    }
    const auto &pool = impl_->footstepPools[profileIndex(profile)];
    const bool isStone = profile == SoundProfile::Stone;
    const float gain = isStone ? 0.26f : 0.33f;
    const float pitchMin = isStone ? 0.95f : 0.93f;
    const float pitchMax = isStone ? 1.01f : 1.03f;
    if (pool.empty()) {
        playFromPool(impl_->footstepPools[profileIndex(SoundProfile::Default)],
                     impl_->activeSources, gain, pitchMin, pitchMax);
        return;
    }
    playFromPool(pool, impl_->activeSources, gain, pitchMin, pitchMax);
}

void AudioSystem::playPlace(SoundProfile profile) {
    if (!ready_ || impl_ == nullptr) {
        return;
    }
    const auto &pool = impl_->placePools[profileIndex(profile)];
    if (pool.empty()) {
        playFromPool(impl_->placePools[profileIndex(SoundProfile::Default)], impl_->activeSources,
                     0.70f, 0.97f, 1.03f);
        return;
    }
    playFromPool(pool, impl_->activeSources, 0.70f, 0.97f, 1.03f);
}

void AudioSystem::playSwim() {
    if (!ready_ || impl_ == nullptr) {
        return;
    }
    playFromPool(impl_->swimBuffers, impl_->activeSources, 0.54f, 0.97f, 1.03f);
}

void AudioSystem::playWaterBob() {
    if (!ready_ || impl_ == nullptr) {
        return;
    }
    playFromPool(impl_->bobBuffers, impl_->activeSources, 0.35f, 0.98f, 1.02f);
}

#else

struct AudioSystem::Impl {};

AudioSystem::~AudioSystem() {
    delete impl_;
    impl_ = nullptr;
}

bool AudioSystem::init() {
    core::Logger::instance().warn("Audio unavailable: build without OpenAL");
    ready_ = false;
    return false;
}

bool AudioSystem::loadPickupSounds(const std::vector<std::string> & /*paths*/) {
    return false;
}

void AudioSystem::playPickup() {}

bool AudioSystem::loadBreakSounds(SoundProfile /*profile*/,
                                  const std::vector<std::string> & /*paths*/) {
    return false;
}

bool AudioSystem::loadFootstepSounds(SoundProfile /*profile*/,
                                     const std::vector<std::string> & /*paths*/) {
    return false;
}

bool AudioSystem::loadPlaceSounds(SoundProfile /*profile*/,
                                  const std::vector<std::string> & /*paths*/) {
    return false;
}

bool AudioSystem::loadSwimSounds(const std::vector<std::string> & /*paths*/) {
    return false;
}

bool AudioSystem::loadWaterBobSounds(const std::vector<std::string> & /*paths*/) {
    return false;
}

void AudioSystem::playBreak(SoundProfile /*profile*/) {}

void AudioSystem::playFootstep(SoundProfile /*profile*/) {}

void AudioSystem::playPlace(SoundProfile /*profile*/) {}

void AudioSystem::playSwim() {}

void AudioSystem::playWaterBob() {}

#endif

} // namespace game
