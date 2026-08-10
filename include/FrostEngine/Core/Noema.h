#pragma once

#include "Core/Types.h"
#include "Core/Vec3.h"
#include "Core/String.h"
#include "Core/Vector.h"
#include "Core/HashMap.h"

namespace Frost {

enum class SenseModality : u8 {
    Visual = 0,
    Auditory,
    Tactile,
    Olfactory,
    Proprioceptive,
    Semantic,
    Count
};

struct Stimulus {
    u64 id = 0;
    SenseModality modality = SenseModality::Visual;
    u64 sourceId = 0;
    Vec3 position;
    f32 intensity = 1.0f;
    f32 confidence = 1.0f;
    u64 frame = 0;
    String label;
};

struct Percept {
    u64 id = 0;
    SenseModality modality = SenseModality::Visual;
    u64 sourceId = 0;
    Vec3 position;
    f32 confidence = 0.0f;
    u64 lastUpdatedFrame = 0;
    u64 creationFrame = 0;
    u32 hits = 0;
    u32 misses = 0;
    f32 attention = 0.0f;
    String label;
};

struct Belief {
    u64 id = 0;
    String name;
    f32 prior = 0.5f;
    f32 posterior = 0.5f;
    u64 lastEvidenceFrame = 0;
    u32 evidenceCount = 0;
    f32 attentionWeight = 0.0f;
};

class Noema {
public:
    struct Config {
        f32 perceptDecayRate = 0.02f;
        f32 beliefDecayRate = 0.01f;
        f32 perceptLifetimeThreshold = 0.1f;
        u32 maxPercepts = 4096;
        bool enableFusion = true;
    };

    struct Stats {
        u32 perceptsActive = 0;
        u32 beliefsTracked = 0;
        u64 stimuliTotal = 0;
        u64 fusionCount = 0;
    };

    bool initialize();
    bool initialize(const Config& config);
    void shutdown();

    void ingestStimulus(const Stimulus& stimulus);

    const Percept* getPercept(SenseModality modality, u64 sourceId) const;
    Percept* getPercept(SenseModality modality, u64 sourceId);

    Vector<Percept>& getAllPercepts();

    Belief* getBelief(const String& name);
    const Belief* getBelief(const String& name) const;
    void setBeliefPrior(const String& name, f32 prior);

    void update(f32 dt);

    Vector<Percept> attention(u64 maxSalient = 5) const;

    void setConfig(const Config& config) { config_ = config; }
    const Config& getConfig() const { return config_; }

    void clearPercepts();
    void clearBeliefs();

    const Stats& getStats() const { return stats_; }

    static u64 perceptKey(SenseModality modality, u64 sourceId);
    static bool labelsMatchIgnoreCase(const String& a, const String& b);

private:
    static char toLowerAscii(char c);

    void feedBelief(const Stimulus& stimulus);
    void evictLowestConfidence();

    Config config_;
    Stats stats_;
    HashMap<u64, Percept> percepts_;
    HashMap<String, Belief> beliefs_;
    Vector<Percept> allPercepts_;
    u64 nextPerceptId_ = 1;
    u64 nextBeliefId_ = 1;
};

} // namespace Frost
