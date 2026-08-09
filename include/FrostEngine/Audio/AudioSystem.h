#pragma once

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include "Core/Math.h"
#include <thread>
#include <mutex>
#include <atomic>

namespace Frost {

struct AudioListener {
    Vec3 position{0, 0, 0};
    Vec3 forward{0, 0, -1};
    Vec3 up{0, 1, 0};
    Vec3 velocity{0, 0, 0};
    f32 volume = 1.0f;
    f32 dopplerFactor = 1.0f;
    f32 rolloffFactor = 1.0f;
    f32 referenceDistance = 1.0f;
    f32 maxDistance = 100.0f;
};

struct AudioSource {
    u32 id = 0;
    Vec3 position{0, 0, 0};
    Vec3 velocity{0, 0, 0};
    Vec3 direction{0, 0, -1};
    f32 volume = 1.0f;
    f32 pitch = 1.0f;
    f32 pan = 0.0f;
    f32 minDistance = 1.0f;
    f32 maxDistance = 50.0f;
    f32 rolloffFactor = 1.0f;
    f32 reverbMix = 0.0f;
    f32 innerConeAngle = 45.0f;
    f32 outerConeAngle = 90.0f;
    f32 outerConeGain = 0.3f;
    bool isSpatial = false;
    bool isLooping = false;
    bool isPlaying = false;
    bool isMusic = false;
    u32 busIndex = 0;
    i32 soundIndex = -1;
    f32 currentSamplePos = 0.0f;
    f32 crossfadeVolume = 1.0f;
    f32 crossfadeTarget = 1.0f;
    f32 crossfadeSpeed = 0.0f;
    f32 pitchVariation = 0.0f;
    f32 volumeVariation = 0.0f;
    f32 pitchVariationRange = 0.0f;
    f32 volumeVariationRange = 0.0f;
};

struct AudioSound {
    enum class Format : u8 { WAV, OGG, PCM };
    Format format = Format::PCM;
    u32 channels = 0;
    u32 sampleRate = 0;
    u32 bitsPerSample = 0;
    u32 dataSize = 0;
    u8* data = nullptr;
    String name;
};

struct AudioBus {
    String name;
    f32 volume = 1.0f;
    f32 pitch = 1.0f;
    f32 pan = 0.0f;
    f32 reverbMix = 0.0f;
    bool muted = false;
    u32 sourceCount = 0;
    f32 eqLow = 1.0f;
    f32 eqMid = 1.0f;
    f32 eqHigh = 1.0f;
};

struct AudioReverbZone {
    Vec3 position{0, 0, 0};
    f32 innerRadius = 5.0f;
    f32 outerRadius = 20.0f;
    f32 wetMix = 0.4f;
    f32 dryMix = 0.6f;
    f32 roomSize = 0.7f;
    f32 damping = 0.5f;
    bool enabled = true;
};

struct AudioOcclusion {
    bool enabled = true;
    f32 highFrequencyReduction = 0.5f;
    f32 volumeReduction = 0.3f;
    f32 maxOcclusionDist = 50.0f;
};

struct AudioCrossfadeTrack {
    i32 soundIndex = -1;
    f32 volume = 0.0f;
    f32 targetVolume = 0.0f;
    f32 fadeSpeed = 1.0f;
    u32 sourceId = 0;
    bool active = false;
};

struct AudioEmitterSnapshot {
    u32 sourceId;
    Vec3 position;
    f32 volume;
    f32 distance;
    f32 dopplerShift;
};

class AudioSystem {
public:
    static constexpr u32 MAX_SOURCES = 256;
    static constexpr u32 MAX_SOUNDS = 512;
    static constexpr u32 NUM_BUSES = 4;
    static constexpr u32 MASTER_BUS = 0;
    static constexpr u32 MUSIC_BUS = 1;
    static constexpr u32 SFX_BUS = 2;
    static constexpr u32 VOICE_BUS = 3;
    static constexpr u32 MAX_REVERB_ZONES = 32;
    static constexpr u32 MAX_CROSSFADE_TRACKS = 8;
    static constexpr u32 STREAM_BUFFER_COUNT = 4;
    static constexpr u32 STREAM_BUFFER_SIZE = 4096;

    AudioSystem();
    ~AudioSystem();

    bool init(u32 sampleRate = 44100, u32 bufferSize = 4096, u32 channels = 2);
    void shutdown();
    void update(f32 dt);

    i32 loadSound(const char* path);
    i32 loadSoundWav(const char* path);
    i32 loadSoundOgg(const char* path);
    i32 loadSoundStream(const char* path);
    void freeSound(i32 soundIndex);
    void freeAllSounds();

    AudioSource& createSource();
    void destroySource(u32 sourceId);
    AudioSource* getSource(u32 sourceId);
    u32 sourceCount() const { return activeSourceCount_; }

    u32 playSound(i32 soundIndex, const Vec3& pos = Vec3(0), f32 volume = 1.0f, f32 pitch = 1.0f, bool loop = false);
    u32 playMusic(i32 soundIndex, f32 volume = 0.7f, f32 fadeInTime = 1.0f);
    void crossfadeTo(i32 newSoundIndex, f32 fadeDuration = 1.0f, f32 volume = 0.7f);
    void stopAll();
    void stopMusic();

    void setVolume(u32 sourceId, f32 vol);
    void setPitch(u32 sourceId, f32 pitch);
    void setPan(u32 sourceId, f32 pan);
    void setLooping(u32 sourceId, bool loop);
    void setPosition(u32 sourceId, const Vec3& pos);
    void setVelocity(u32 sourceId, const Vec3& vel);
    void setDirection(u32 sourceId, const Vec3& dir);
    void setSpatial(u32 sourceId, bool spatial);
    void setReverb(u32 sourceId, f32 mix);
    void setMinDistance(u32 sourceId, f32 dist);
    void setMaxDistance(u32 sourceId, f32 dist);
    void setCone(u32 sourceId, f32 inner, f32 outer, f32 outerGain);
    void setRandomVariation(u32 sourceId, f32 pitchRange, f32 volumeRange);

    void setBusVolume(u32 bus, f32 vol);
    f32 getBusVolume(u32 bus) const;
    void setBusPitch(u32 bus, f32 pitch);
    void setBusMute(u32 bus, bool mute);
    void setBusReverb(u32 bus, f32 mix);
    void setBusEQ(u32 bus, f32 low, f32 mid, f32 high);

    void addReverbZone(const AudioReverbZone& zone);
    void removeReverbZone(u32 index);
    void clearReverbZones();
    void setOcclusion(const AudioOcclusion& occ) { occlusion_ = occ; }

    AudioListener& listener() { return listener_; }
    void setListener(const Vec3& pos, const Vec3& forward, const Vec3& up);

    void setMasterVolume(f32 vol);
    void setDopplerFactor(f32 factor);

    struct Stats {
        u32 activeSources = 0;
        u32 playingSources = 0;
        u32 loadedSounds = 0;
        f32 masterVolume = 1.0f;
        f32 avgFrameMs = 0.0f;
        u32 streamedSounds = 0;
        u32 occludedSources = 0;
    };
    const Stats& stats() const { return stats_; }

    const f32* getOutputBuffer() const { return outputBuffer_; }
    u32 getBufferSize() const { return bufferSize_; }
    u32 getSampleRate() const { return sampleRate_; }

private:
    f32 calculateDistanceAttenuation(f32 distance, f32 refDist, f32 maxDist, f32 rolloff) const;
    f32 calculateDistanceAttenuationLinear(f32 distance, f32 refDist, f32 maxDist) const;
    f32 calculateDistanceAttenuationInverse(f32 distance, f32 refDist) const;
    f32 calculateDistanceAttenuationLog(f32 distance, f32 refDist, f32 maxDist) const;
    f32 calculateDopplerEffect(const Vec3& sourcePos, const Vec3& sourceVel,
                               const Vec3& listenerPos, const Vec3& listenerVel) const;
    f32 calculateConeGain(const AudioSource& src, const Vec3& toListener) const;
    f32 calculateReverbMix(const AudioSource& src) const;
    f32 calculateOcclusion(const AudioSource& src) const;
    void mixSource(AudioSource& src, const AudioSound& snd, f32& L, f32& R);
    void processSpatial(AudioSource& src, f32& L, f32& R);
    void processPanning(f32 pan, f32& L, f32& R);
    void processRandomVariation(AudioSource& src);

    void decodeWav(const u8* data, u32 size, AudioSound& out);
    void decodeOgg(const u8* data, u32 size, AudioSound& out);
    bool parseRiffHeader(const u8* data, u32 size, u16& format, u16& channels,
                         u32& sampleRate, u16& bitsPerSample, u32& dataOffset, u32& dataSize);
    bool parseWavFormatChunk(const u8* data, u32 size, u16& format, u16& channels,
                             u32& sampleRate, u16& bitsPerSample);
    i32 findWavDataChunk(const u8* data, u32 size, u32& dataOffset, u32& dataSize);
    void convertPcm16ToFloat(const u8* pcm16, u32 sampleCount, f32* out);
    void convertPcm8ToFloat(const u8* pcm8, u32 sampleCount, f32* out);
    void mixStereoToMono(const f32* stereo, f32* mono, u32 sampleCount);

    AudioListener listener_;
    AudioSource sources_[MAX_SOURCES];
    AudioSound sounds_[MAX_SOUNDS];
    AudioBus buses_[NUM_BUSES];
    AudioReverbZone reverbZones_[MAX_REVERB_ZONES];
    AudioOcclusion occlusion_;
    AudioCrossfadeTrack crossfadeTracks_[MAX_CROSSFADE_TRACKS];
    u32 activeSourceCount_ = 0;
    u32 loadedSoundCount_ = 0;
    u32 nextSourceId_ = 1;
    u32 reverbZoneCount_ = 0;
    u32 activeCrossfadeCount_ = 0;

    f32* outputBuffer_ = nullptr;
    u32 sampleRate_ = 44100;
    u32 bufferSize_ = 4096;
    u32 channels_ = 2;
    f32 masterVolume_ = 1.0f;
    f32 globalDopplerFactor_ = 1.0f;

    u32 musicSourceId_ = 0;
    f32 musicFade_ = 0.0f;
    f32 musicFadeTarget_ = 0.0f;
    f32 musicFadeSpeed_ = 0.0f;

    Stats stats_;
    f32 frameTimeAccum_ = 0.0f;
    u32 frameTimeSamples_ = 0;

    Vector<u8> fileBuffer_;

    bool streaming_ = false;
    std::thread streamThread_;
    std::mutex streamMutex_;
    std::atomic<bool> streamRunning_{false};
    i32 streamingSoundIndex_ = -1;
    u32 streamReadPos_ = 0;
    f32 streamBuffer_[STREAM_BUFFER_COUNT][STREAM_BUFFER_SIZE * 2];
    u32 streamBufferReady_[STREAM_BUFFER_COUNT] = {};
    u32 streamWriteBuffer_ = 0;
    u32 streamReadBuffer_ = 0;
};

}
