#include "Audio/AudioSystem.h"
#include "Core/Log.h"
#include <cstring>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace Frost {

AudioSystem::AudioSystem() {
    memset(sources_, 0, sizeof(sources_));
    memset(sounds_, 0, sizeof(sounds_));
    memset(reverbZones_, 0, sizeof(reverbZones_));
    memset(crossfadeTracks_, 0, sizeof(crossfadeTracks_));
    memset(streamBuffer_, 0, sizeof(streamBuffer_));
    memset(streamBufferReady_, 0, sizeof(streamBufferReady_));
    buses_[MASTER_BUS] = {"Master", 1.0f, 1.0f, 0.0f, 0.0f, false, 0, 1.0f, 1.0f, 1.0f};
    buses_[MUSIC_BUS] = {"Music", 0.7f, 1.0f, 0.0f, 0.0f, false, 0, 1.0f, 1.0f, 1.0f};
    buses_[SFX_BUS] = {"SFX", 1.0f, 1.0f, 0.0f, 0.0f, false, 0, 1.0f, 1.0f, 1.0f};
    buses_[VOICE_BUS] = {"Voice", 1.0f, 1.0f, 0.0f, 0.0f, false, 0, 1.0f, 1.0f, 1.0f};
}

AudioSystem::~AudioSystem() {
    shutdown();
}

bool AudioSystem::init(u32 sampleRate, u32 bufferSize, u32 ch) {
    sampleRate_ = sampleRate;
    bufferSize_ = bufferSize;
    channels_ = ch;
    outputBuffer_ = (f32*)calloc(bufferSize_ * channels_, sizeof(f32));
    if (!outputBuffer_) {
        FROST_LOG_ERROR("[AudioSystem] Failed to allocate output buffer");
        return false;
    }
    FROST_LOG_INFO("[AudioSystem] Initialized: %uHz, %u buffer, %u channels", sampleRate_, bufferSize_, channels_);
    return true;
}

void AudioSystem::shutdown() {
    streamRunning_ = false;
    if (streamThread_.joinable()) streamThread_.join();
    freeAllSounds();
    if (outputBuffer_) {
        free(outputBuffer_);
        outputBuffer_ = nullptr;
    }
    activeSourceCount_ = 0;
    FROST_LOG_INFO("[AudioSystem] Shutdown");
}

void AudioSystem::update(f32 dt) {
    f32 t0 = (f32)clock() / (f32)CLOCKS_PER_SEC;

    stats_.playingSources = 0;
    stats_.occludedSources = 0;

    for (u32 i = 0; i < activeSourceCount_; i++) {
        AudioSource& src = sources_[i];
        if (!src.isPlaying) continue;

        AudioSound& snd = sounds_[src.soundIndex];
        if (snd.data == nullptr) {
            src.isPlaying = false;
            continue;
        }

        src.position += src.velocity * dt;

        f32 effectiveVol = src.volume * buses_[src.busIndex].volume * masterVolume_;
        f32 effectivePitch = src.pitch * buses_[src.busIndex].pitch;
        f32 doppler = 1.0f;
        f32 attenuation = 1.0f;
        f32 coneGain = 1.0f;
        f32 reverbMix = calculateReverbMix(src);

        if (src.isSpatial) {
            Vec3 toListener = listener_.position - src.position;
            f32 distance = toListener.length();
            attenuation = calculateDistanceAttenuation(distance, src.minDistance, src.maxDistance, src.rolloffFactor);
            doppler = calculateDopplerEffect(src.position, src.velocity, listener_.position, listener_.velocity);
            effectivePitch *= doppler * globalDopplerFactor_;
            if (distance > 0.01f) {
                coneGain = calculateConeGain(src, toListener / distance);
            }
            f32 occFactor = calculateOcclusion(src);
            effectiveVol *= attenuation * coneGain * (1.0f - reverbMix * 0.5f) * occFactor;
            if (occFactor < 0.99f) stats_.occludedSources++;
        } else {
            effectiveVol *= (1.0f - reverbMix * 0.5f);
        }

        processRandomVariation(src);

        effectivePitch = Mathf::clamp(effectivePitch, 0.1f, 4.0f);
        effectiveVol = Mathf::clamp(effectiveVol, 0.0f, 2.0f);

        src.currentSamplePos += effectivePitch * (f32)snd.sampleRate * dt;
        if (src.isLooping) {
            f32 totalSamples = (f32)(snd.dataSize / (snd.bitsPerSample / 8));
            if (src.currentSamplePos >= totalSamples) {
                src.currentSamplePos = std::fmod(src.currentSamplePos, totalSamples);
            }
        } else if (src.currentSamplePos >= (f32)(snd.dataSize / (snd.bitsPerSample / 8))) {
            src.isPlaying = false;
        }

        if (src.crossfadeSpeed != 0.0f) {
            src.crossfadeVolume = Mathf::approach(src.crossfadeVolume, src.crossfadeTarget, src.crossfadeSpeed * dt);
            if (Mathf::approx(src.crossfadeVolume, src.crossfadeTarget)) {
                src.crossfadeSpeed = 0.0f;
            }
        }
        effectiveVol *= src.crossfadeVolume;

        f32 L = effectiveVol, R = effectiveVol;
        if (src.isSpatial) {
            processSpatial(src, L, R);
        }
        processPanning(src.pan, L, R);

        L *= buses_[src.busIndex].eqLow * buses_[src.busIndex].eqMid;
        R *= buses_[src.busIndex].eqLow * buses_[src.busIndex].eqMid;

        stats_.playingSources++;
    }

    for (u32 i = 0; i < activeCrossfadeCount_; i++) {
        AudioCrossfadeTrack& ct = crossfadeTracks_[i];
        if (!ct.active) continue;
        ct.volume = Mathf::approach(ct.volume, ct.targetVolume, ct.fadeSpeed * dt);
        if (Mathf::approx(ct.volume, ct.targetVolume) && ct.targetVolume <= 0.001f) {
            ct.active = false;
            if (ct.sourceId > 0) {
                AudioSource* src = getSource(ct.sourceId);
                if (src) src->isPlaying = false;
            }
        }
    }

    if (musicSourceId_ > 0) {
        AudioSource* ms = getSource(musicSourceId_);
        if (ms && musicFadeSpeed_ != 0.0f) {
            musicFade_ = Mathf::approach(musicFade_, musicFadeTarget_, musicFadeSpeed_ * dt);
            ms->volume = musicFade_;
            if (Mathf::approx(musicFade_, musicFadeTarget_) && musicFadeTarget_ == 0.0f) {
                ms->isPlaying = false;
                musicFadeSpeed_ = 0.0f;
            }
        }
    }

    f32 t1 = (f32)clock() / (f32)CLOCKS_PER_SEC;
    frameTimeAccum_ += (t1 - t0) * 1000.0f;
    frameTimeSamples_++;
    if (frameTimeSamples_ >= 60) {
        stats_.avgFrameMs = frameTimeAccum_ / (f32)frameTimeSamples_;
        frameTimeAccum_ = 0.0f;
        frameTimeSamples_ = 0;
    }
    stats_.activeSources = activeSourceCount_;
}

i32 AudioSystem::loadSound(const char* path) {
    if (loadedSoundCount_ >= MAX_SOUNDS) {
        FROST_LOG_ERROR("[AudioSystem] Sound pool exhausted");
        return -1;
    }
    String s(path);
    if (s.endsWith(".wav") || s.endsWith(".WAV")) {
        return loadSoundWav(path);
    } else if (s.endsWith(".ogg") || s.endsWith(".OGG")) {
        return loadSoundOgg(path);
    }
    return loadSoundWav(path);
}

i32 AudioSystem::loadSoundWav(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        FROST_LOG_ERROR("[AudioSystem] Failed to open WAV: %s", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fileSize < 44) {
        fclose(f);
        return -1;
    }
    fileBuffer_.resize((usize)fileSize);
    fread(fileBuffer_.data(), 1, (usize)fileSize, f);
    fclose(f);

    u8* d = fileBuffer_.data();
    if (memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) {
        FROST_LOG_ERROR("[AudioSystem] Invalid WAV header: %s", path);
        return -1;
    }

    u16 audioFormat = 0;
    u16 numChannels = 0;
    u32 sampleRate = 0;
    u16 bitsPerSample = 0;
    u32 dataOffset = 0;
    u32 dataSize = 0;

    if (!parseRiffHeader(d, (u32)fileSize, audioFormat, numChannels, sampleRate, bitsPerSample, dataOffset, dataSize)) {
        FROST_LOG_ERROR("[AudioSystem] Failed to parse WAV header: %s", path);
        return -1;
    }

    if (audioFormat != 1 && audioFormat != 3) {
        FROST_LOG_ERROR("[AudioSystem] Unsupported WAV format %u: %s", audioFormat, path);
        return -1;
    }

    if (dataSize == 0 || dataOffset + dataSize > (u32)fileSize) {
        FROST_LOG_ERROR("[AudioSystem] WAV data chunk not found: %s", path);
        return -1;
    }

    AudioSound& snd = sounds_[loadedSoundCount_];
    snd.format = (audioFormat == 3) ? AudioSound::Format::PCM : AudioSound::Format::WAV;
    snd.channels = numChannels;
    snd.sampleRate = sampleRate;
    snd.bitsPerSample = bitsPerSample;
    snd.dataSize = dataSize;
    snd.data = (u8*)malloc(dataSize);
    memcpy(snd.data, d + dataOffset, dataSize);
    snd.name = path;

    i32 idx = loadedSoundCount_;
    loadedSoundCount_++;
    stats_.loadedSounds = loadedSoundCount_;
    FROST_LOG_INFO("[AudioSystem] Loaded WAV: %s (%uHz, %uch, %ubit, %ukB)",
                   path, sampleRate, numChannels, bitsPerSample, dataSize / 1024);
    return idx;
}

i32 AudioSystem::loadSoundOgg(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        FROST_LOG_ERROR("[AudioSystem] Failed to open OGG: %s", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fileSize < 4) {
        fclose(f);
        return -1;
    }

    u8 header[4];
    fread(header, 1, 4, f);
    fseek(f, 0, SEEK_SET);

    AudioSound& snd = sounds_[loadedSoundCount_];
    snd.format = AudioSound::Format::OGG;

    if (header[0] == 'O' && header[1] == 'g' && header[2] == 'g' && header[3] == 'S') {
        snd.channels = 2;
        snd.sampleRate = 44100;
        snd.bitsPerSample = 16;
        snd.dataSize = (u32)fileSize;
        snd.data = (u8*)malloc((usize)fileSize);
        fread(snd.data, 1, (usize)fileSize, f);
    } else {
        snd.channels = 2;
        snd.sampleRate = 44100;
        snd.bitsPerSample = 16;
        snd.dataSize = (u32)fileSize;
        snd.data = (u8*)malloc((usize)fileSize);
        fread(snd.data, 1, (usize)fileSize, f);
    }
    fclose(f);
    snd.name = path;

    i32 idx = loadedSoundCount_;
    loadedSoundCount_++;
    stats_.loadedSounds = loadedSoundCount_;
    FROST_LOG_INFO("[AudioSystem] Loaded OGG: %s (%ukB)", path, fileSize / 1024);
    return idx;
}

i32 AudioSystem::loadSoundStream(const char* path) {
    i32 idx = loadSound(path);
    if (idx >= 0) {
        stats_.streamedSounds++;
    }
    return idx;
}

void AudioSystem::freeSound(i32 soundIndex) {
    if (soundIndex < 0 || soundIndex >= (i32)loadedSoundCount_) return;
    AudioSound& snd = sounds_[soundIndex];
    if (snd.data) {
        free(snd.data);
        snd.data = nullptr;
    }
    snd.dataSize = 0;
    snd.name.clear();
}

void AudioSystem::freeAllSounds() {
    for (u32 i = 0; i < loadedSoundCount_; i++) {
        if (sounds_[i].data) {
            free(sounds_[i].data);
            sounds_[i].data = nullptr;
        }
        sounds_[i].dataSize = 0;
        sounds_[i].name.clear();
    }
    loadedSoundCount_ = 0;
}

AudioSource& AudioSystem::createSource() {
    for (u32 i = 0; i < MAX_SOURCES; i++) {
        if (!sources_[i].isPlaying && sources_[i].id == 0) {
            sources_[i] = AudioSource{};
            sources_[i].id = nextSourceId_++;
            sources_[i].busIndex = SFX_BUS;
            activeSourceCount_++;
            return sources_[i];
        }
    }
    FROST_LOG_WARN("[AudioSystem] Source pool exhausted");
    return sources_[0];
}

void AudioSystem::destroySource(u32 sourceId) {
    for (u32 i = 0; i < activeSourceCount_; i++) {
        if (sources_[i].id == sourceId) {
            sources_[i].isPlaying = false;
            sources_[i].id = 0;
            if (i != activeSourceCount_ - 1) {
                sources_[i] = sources_[activeSourceCount_ - 1];
            }
            activeSourceCount_--;
            return;
        }
    }
}

AudioSource* AudioSystem::getSource(u32 sourceId) {
    for (u32 i = 0; i < activeSourceCount_; i++) {
        if (sources_[i].id == sourceId) return &sources_[i];
    }
    return nullptr;
}

u32 AudioSystem::playSound(i32 soundIndex, const Vec3& pos, f32 volume, f32 pitch, bool loop) {
    if (soundIndex < 0 || soundIndex >= (i32)loadedSoundCount_) return 0;
    AudioSource& src = createSource();
    src.soundIndex = soundIndex;
    src.position = pos;
    src.volume = volume;
    src.pitch = pitch;
    src.isLooping = loop;
    src.isPlaying = true;
    src.busIndex = SFX_BUS;
    src.isSpatial = (pos.lengthSquared() > 0.01f);
    src.currentSamplePos = 0.0f;
    src.crossfadeVolume = 1.0f;
    src.crossfadeSpeed = 0.0f;
    return src.id;
}

u32 AudioSystem::playMusic(i32 soundIndex, f32 volume, f32 fadeInTime) {
    if (soundIndex < 0 || soundIndex >= (i32)loadedSoundCount_) return 0;
    if (musicSourceId_ > 0) {
        stopMusic();
    }
    AudioSource& src = createSource();
    src.soundIndex = soundIndex;
    src.volume = 0.0f;
    src.isLooping = true;
    src.isPlaying = true;
    src.isMusic = true;
    src.busIndex = MUSIC_BUS;
    src.currentSamplePos = 0.0f;
    musicSourceId_ = src.id;
    musicFade_ = 0.0f;
    musicFadeTarget_ = volume;
    musicFadeSpeed_ = (fadeInTime > 0.01f) ? volume / fadeInTime : 100.0f;
    return src.id;
}

void AudioSystem::crossfadeTo(i32 newSoundIndex, f32 fadeDuration, f32 volume) {
    if (musicSourceId_ > 0) {
        AudioSource* old = getSource(musicSourceId_);
        if (old) {
            old->crossfadeTarget = 0.0f;
            old->crossfadeSpeed = (fadeDuration > 0.01f) ? 1.0f / fadeDuration : 100.0f;
        }
    }
    if (newSoundIndex >= 0 && newSoundIndex < (i32)loadedSoundCount_) {
        u32 newId = playMusic(newSoundIndex, volume, fadeDuration);
        AudioSource* newSrc = getSource(newId);
        if (newSrc) {
            newSrc->crossfadeVolume = 0.0f;
            newSrc->crossfadeTarget = 1.0f;
            newSrc->crossfadeSpeed = (fadeDuration > 0.01f) ? 1.0f / fadeDuration : 100.0f;
        }
    }
}

void AudioSystem::stopAll() {
    for (u32 i = 0; i < activeSourceCount_; i++) {
        sources_[i].isPlaying = false;
    }
    activeSourceCount_ = 0;
    musicSourceId_ = 0;
}

void AudioSystem::stopMusic() {
    if (musicSourceId_ > 0) {
        AudioSource* ms = getSource(musicSourceId_);
        if (ms) {
            musicFadeTarget_ = 0.0f;
            musicFadeSpeed_ = 2.0f;
        }
    }
}

void AudioSystem::setVolume(u32 sourceId, f32 vol) {
    AudioSource* s = getSource(sourceId);
    if (s) s->volume = Mathf::clamp(vol, 0.0f, 2.0f);
}

void AudioSystem::setPitch(u32 sourceId, f32 pitch) {
    AudioSource* s = getSource(sourceId);
    if (s) s->pitch = Mathf::clamp(pitch, 0.1f, 4.0f);
}

void AudioSystem::setPan(u32 sourceId, f32 pan) {
    AudioSource* s = getSource(sourceId);
    if (s) s->pan = Mathf::clamp(pan, -1.0f, 1.0f);
}

void AudioSystem::setLooping(u32 sourceId, bool loop) {
    AudioSource* s = getSource(sourceId);
    if (s) s->isLooping = loop;
}

void AudioSystem::setPosition(u32 sourceId, const Vec3& pos) {
    AudioSource* s = getSource(sourceId);
    if (s) s->position = pos;
}

void AudioSystem::setVelocity(u32 sourceId, const Vec3& vel) {
    AudioSource* s = getSource(sourceId);
    if (s) s->velocity = vel;
}

void AudioSystem::setDirection(u32 sourceId, const Vec3& dir) {
    AudioSource* s = getSource(sourceId);
    if (s) s->direction = dir.normalized();
}

void AudioSystem::setSpatial(u32 sourceId, bool spatial) {
    AudioSource* s = getSource(sourceId);
    if (s) s->isSpatial = spatial;
}

void AudioSystem::setReverb(u32 sourceId, f32 mix) {
    AudioSource* s = getSource(sourceId);
    if (s) s->reverbMix = Mathf::clamp(mix, 0.0f, 1.0f);
}

void AudioSystem::setMinDistance(u32 sourceId, f32 dist) {
    AudioSource* s = getSource(sourceId);
    if (s) s->minDistance = Mathf::max(dist, 0.01f);
}

void AudioSystem::setMaxDistance(u32 sourceId, f32 dist) {
    AudioSource* s = getSource(sourceId);
    if (s) s->maxDistance = Mathf::max(dist, 0.1f);
}

void AudioSystem::setCone(u32 sourceId, f32 inner, f32 outer, f32 outerGain) {
    AudioSource* s = getSource(sourceId);
    if (s) {
        s->innerConeAngle = inner;
        s->outerConeAngle = outer;
        s->outerConeGain = Mathf::clamp(outerGain, 0.0f, 1.0f);
    }
}

void AudioSystem::setRandomVariation(u32 sourceId, f32 pitchRange, f32 volumeRange) {
    AudioSource* s = getSource(sourceId);
    if (s) {
        s->pitchVariationRange = Mathf::clamp(pitchRange, 0.0f, 1.0f);
        s->volumeVariationRange = Mathf::clamp(volumeRange, 0.0f, 1.0f);
    }
}

void AudioSystem::setBusVolume(u32 bus, f32 vol) {
    if (bus < NUM_BUSES) buses_[bus].volume = Mathf::clamp(vol, 0.0f, 2.0f);
}

f32 AudioSystem::getBusVolume(u32 bus) const {
    return (bus < NUM_BUSES) ? buses_[bus].volume : 0.0f;
}

void AudioSystem::setBusPitch(u32 bus, f32 pitch) {
    if (bus < NUM_BUSES) buses_[bus].pitch = Mathf::clamp(pitch, 0.1f, 4.0f);
}

void AudioSystem::setBusMute(u32 bus, bool mute) {
    if (bus < NUM_BUSES) buses_[bus].muted = mute;
}

void AudioSystem::setBusReverb(u32 bus, f32 mix) {
    if (bus < NUM_BUSES) buses_[bus].reverbMix = Mathf::clamp(mix, 0.0f, 1.0f);
}

void AudioSystem::setBusEQ(u32 bus, f32 low, f32 mid, f32 high) {
    if (bus < NUM_BUSES) {
        buses_[bus].eqLow = Mathf::clamp(low, 0.0f, 2.0f);
        buses_[bus].eqMid = Mathf::clamp(mid, 0.0f, 2.0f);
        buses_[bus].eqHigh = Mathf::clamp(high, 0.0f, 2.0f);
    }
}

void AudioSystem::addReverbZone(const AudioReverbZone& zone) {
    if (reverbZoneCount_ < MAX_REVERB_ZONES) {
        reverbZones_[reverbZoneCount_++] = zone;
    }
}

void AudioSystem::removeReverbZone(u32 index) {
    if (index < reverbZoneCount_) {
        reverbZones_[index] = reverbZones_[--reverbZoneCount_];
    }
}

void AudioSystem::clearReverbZones() {
    reverbZoneCount_ = 0;
}

void AudioSystem::setListener(const Vec3& pos, const Vec3& forward, const Vec3& up) {
    listener_.velocity = (pos - listener_.position);
    listener_.position = pos;
    listener_.forward = forward.normalized();
    listener_.up = up.normalized();
}

void AudioSystem::setMasterVolume(f32 vol) {
    masterVolume_ = Mathf::clamp(vol, 0.0f, 2.0f);
}

void AudioSystem::setDopplerFactor(f32 factor) {
    globalDopplerFactor_ = Mathf::clamp(factor, 0.0f, 4.0f);
}

f32 AudioSystem::calculateDistanceAttenuation(f32 distance, f32 refDist, f32 maxDist, f32 rolloff) const {
    if (distance <= refDist) return 1.0f;
    if (distance >= maxDist) return 0.0f;
    f32 t = (distance - refDist) / (maxDist - refDist);
    f32 att = 1.0f - Mathf::lerp(0.0f, 1.0f, std::pow(t, rolloff));
    return Mathf::clamp(att, 0.0f, 1.0f);
}

f32 AudioSystem::calculateDistanceAttenuationLinear(f32 distance, f32 refDist, f32 maxDist) const {
    if (distance <= refDist) return 1.0f;
    if (distance >= maxDist) return 0.0f;
    return 1.0f - (distance - refDist) / (maxDist - refDist);
}

f32 AudioSystem::calculateDistanceAttenuationInverse(f32 distance, f32 refDist) const {
    if (distance <= refDist) return 1.0f;
    return refDist / (refDist + (distance - refDist));
}

f32 AudioSystem::calculateDistanceAttenuationLog(f32 distance, f32 refDist, f32 maxDist) const {
    if (distance <= refDist) return 1.0f;
    if (distance >= maxDist) return 0.0f;
    f32 ratio = std::log(refDist / distance) / std::log(refDist / maxDist);
    return Mathf::clamp(ratio, 0.0f, 1.0f);
}

f32 AudioSystem::calculateDopplerEffect(const Vec3& sourcePos, const Vec3& sourceVel,
                                         const Vec3& listenerPos, const Vec3& listenerVel) const {
    Vec3 toListener = listenerPos - sourcePos;
    f32 distance = toListener.length();
    if (distance < 0.01f) return 1.0f;

    Vec3 dirToSource = toListener / distance;
    f32 sourceProj = sourceVel.dot(dirToSource);
    f32 listenerProj = listenerVel.dot(dirToSource);
    f32 effectiveSpeed = 343.3f;
    sourceProj = Mathf::min(sourceProj, effectiveSpeed * 0.9f);
    listenerProj = Mathf::min(listenerProj, effectiveSpeed * 0.9f);
    f32 shift = (effectiveSpeed - listenerProj) / (effectiveSpeed + sourceProj);
    shift = Mathf::clamp(shift, 0.5f, 2.0f);
    return shift;
}

f32 AudioSystem::calculateConeGain(const AudioSource& src, const Vec3& toListener) const {
    Vec3 srcForward = src.direction;
    f32 angle = std::acos(Mathf::clamp(srcForward.dot(toListener), -1.0f, 1.0f));
    f32 innerRad = Mathf::radians(src.innerConeAngle) * 0.5f;
    f32 outerRad = Mathf::radians(src.outerConeAngle) * 0.5f;
    if (angle <= innerRad) return 1.0f;
    if (angle >= outerRad) return src.outerConeGain;
    f32 t = (angle - innerRad) / (outerRad - innerRad);
    return Mathf::lerp(1.0f, src.outerConeGain, t);
}

f32 AudioSystem::calculateReverbMix(const AudioSource& src) const {
    f32 totalMix = src.reverbMix + buses_[src.busIndex].reverbMix;
    for (u32 i = 0; i < reverbZoneCount_; i++) {
        const AudioReverbZone& zone = reverbZones_[i];
        if (!zone.enabled) continue;
        f32 dist = (src.position - zone.position).length();
        if (dist < zone.outerRadius) {
            f32 zoneMix = 0.0f;
            if (dist < zone.innerRadius) {
                zoneMix = zone.wetMix;
            } else {
                f32 t = (dist - zone.innerRadius) / (zone.outerRadius - zone.innerRadius);
                zoneMix = Mathf::lerp(zone.wetMix, 0.0f, t);
            }
            totalMix += zoneMix;
        }
    }
    return Mathf::clamp(totalMix, 0.0f, 1.0f);
}

f32 AudioSystem::calculateOcclusion(const AudioSource& src) const {
    if (!occlusion_.enabled) return 1.0f;
    Vec3 toListener = listener_.position - src.position;
    f32 distance = toListener.length();
    if (distance > occlusion_.maxOcclusionDist) return 1.0f - occlusion_.volumeReduction;
    f32 t = distance / occlusion_.maxOcclusionDist;
    return Mathf::lerp(1.0f, 1.0f - occlusion_.volumeReduction, t);
}

void AudioSystem::mixSource(AudioSource& src, const AudioSound& snd, f32& L, f32& R) {
    f32 vol = src.volume * buses_[src.busIndex].volume * masterVolume_;
    if (buses_[src.busIndex].muted) { L = 0; R = 0; return; }
    L = vol;
    R = vol;
    if (src.isSpatial) {
        processSpatial(src, L, R);
    }
    processPanning(src.pan, L, R);
}

void AudioSystem::processSpatial(AudioSource& src, f32& L, f32& R) {
    Vec3 toListener = listener_.position - src.position;
    f32 distance = toListener.length();
    f32 atten = calculateDistanceAttenuation(distance, src.minDistance, src.maxDistance, src.rolloffFactor);
    L *= atten;
    R *= atten;
    if (distance > 0.01f) {
        Vec3 right = listener_.forward.cross(listener_.up).normalized();
        f32 panVal = toListener.dot(right);
        f32 panNorm = Mathf::clamp(panVal / Mathf::max(distance, 1.0f), -1.0f, 1.0f);
        L *= 1.0f - panNorm * 0.5f;
        R *= 1.0f + panNorm * 0.5f;
    }
}

void AudioSystem::processPanning(f32 pan, f32& L, f32& R) {
    f32 panL = 1.0f - pan * 0.5f;
    f32 panR = 1.0f + pan * 0.5f;
    L *= Mathf::clamp(panL, 0.0f, 1.0f);
    R *= Mathf::clamp(panR, 0.0f, 1.0f);
}

void AudioSystem::processRandomVariation(AudioSource& src) {
    if (src.pitchVariationRange > 0.01f && src.pitchVariation == 0.0f) {
        src.pitchVariation = ((f32)std::rand() / (f32)RAND_MAX - 0.5f) * 2.0f * src.pitchVariationRange;
        src.pitch += src.pitchVariation;
    }
    if (src.volumeVariationRange > 0.01f && src.volumeVariation == 0.0f) {
        src.volumeVariation = ((f32)std::rand() / (f32)RAND_MAX - 0.5f) * 2.0f * src.volumeVariationRange;
        src.volume = Mathf::clamp(src.volume + src.volumeVariation, 0.0f, 2.0f);
    }
}

bool AudioSystem::parseRiffHeader(const u8* data, u32 size, u16& format, u16& channels,
                                   u32& sampleRate, u16& bitsPerSample, u32& dataOffset, u32& dataSize) {
    if (size < 44) return false;
    if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) return false;

    format = data[20] | (data[21] << 8);
    channels = data[22] | (data[23] << 8);
    sampleRate = data[24] | (data[25] << 8) | (data[26] << 16) | (data[27] << 24);
    bitsPerSample = data[34] | (data[35] << 8);

    dataOffset = 36;
    dataSize = 0;
    while (dataOffset < size - 8) {
        u32 chunkSize = data[dataOffset + 4] | (data[dataOffset + 5] << 8) |
                        (data[dataOffset + 6] << 16) | (data[dataOffset + 7] << 24);
        if (memcmp(data + dataOffset, "data", 4) == 0) {
            dataSize = chunkSize;
            dataOffset += 8;
            return true;
        }
        dataOffset += 8 + chunkSize;
    }
    return false;
}

bool AudioSystem::parseWavFormatChunk(const u8* data, u32 size, u16& format, u16& channels,
                                       u32& sampleRate, u16& bitsPerSample) {
    if (size < 44) return false;
    format = data[20] | (data[21] << 8);
    channels = data[22] | (data[23] << 8);
    sampleRate = data[24] | (data[25] << 8) | (data[26] << 16) | (data[27] << 24);
    bitsPerSample = data[34] | (data[35] << 8);
    return true;
}

i32 AudioSystem::findWavDataChunk(const u8* data, u32 size, u32& dataOffset, u32& dataSize) {
    dataOffset = 36;
    dataSize = 0;
    while (dataOffset < size - 8) {
        u32 chunkSize = data[dataOffset + 4] | (data[dataOffset + 5] << 8) |
                        (data[dataOffset + 6] << 16) | (data[dataOffset + 7] << 24);
        if (memcmp(data + dataOffset, "data", 4) == 0) {
            dataSize = chunkSize;
            dataOffset += 8;
            return 0;
        }
        dataOffset += 8 + chunkSize;
    }
    return -1;
}

void AudioSystem::convertPcm16ToFloat(const u8* pcm16, u32 sampleCount, f32* out) {
    const i16* src = (const i16*)pcm16;
    for (u32 i = 0; i < sampleCount; i++) {
        out[i] = (f32)src[i] / 32768.0f;
    }
}

void AudioSystem::convertPcm8ToFloat(const u8* pcm8, u32 sampleCount, f32* out) {
    for (u32 i = 0; i < sampleCount; i++) {
        out[i] = ((f32)pcm8[i] - 128.0f) / 128.0f;
    }
}

void AudioSystem::mixStereoToMono(const f32* stereo, f32* mono, u32 sampleCount) {
    for (u32 i = 0; i < sampleCount; i++) {
        mono[i] = (stereo[i * 2] + stereo[i * 2 + 1]) * 0.5f;
    }
}

void AudioSystem::decodeWav(const u8* data, u32 size, AudioSound& out) {
    u16 format = 0, channels = 0, bitsPerSample = 0;
    u32 sampleRate = 0, dataOffset = 0, dataSize = 0;
    if (parseRiffHeader(data, size, format, channels, sampleRate, bitsPerSample, dataOffset, dataSize)) {
        out.format = (format == 3) ? AudioSound::Format::PCM : AudioSound::Format::WAV;
        out.channels = channels;
        out.sampleRate = sampleRate;
        out.bitsPerSample = bitsPerSample;
        out.dataSize = dataSize;
        out.data = (u8*)malloc(dataSize);
        memcpy(out.data, data + dataOffset, dataSize);
    }
}

void AudioSystem::decodeOgg(const u8* data, u32 size, AudioSound& out) {
    out.format = AudioSound::Format::OGG;
    out.channels = 2;
    out.sampleRate = 44100;
    out.bitsPerSample = 16;
    out.dataSize = size;
    out.data = (u8*)malloc(size);
    memcpy(out.data, data, size);
}

}
