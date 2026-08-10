#include "Core/Noema.h"

#include <algorithm>
#include <cmath>

namespace Frost {

char Noema::toLowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

bool Noema::labelsMatchIgnoreCase(const String& a, const String& b) {
    if (a.length() != b.length()) return false;
    for (usize i = 0; i < a.length(); ++i) {
        if (toLowerAscii(a[i]) != toLowerAscii(b[i])) return false;
    }
    return true;
}

u64 Noema::perceptKey(SenseModality modality, u64 sourceId) {
    return ((u64)modality << 32) | sourceId;
}

bool Noema::initialize() {
    config_ = Config{};
    return initialize(config_);
}

bool Noema::initialize(const Config& config) {
    config_ = config;
    percepts_.clear();
    beliefs_.clear();
    allPercepts_.clear();
    stats_ = {};
    nextPerceptId_ = 1;
    nextBeliefId_ = 1;
    return true;
}

void Noema::shutdown() {
    percepts_.clear();
    beliefs_.clear();
    allPercepts_.clear();
    stats_ = {};
    nextPerceptId_ = 1;
    nextBeliefId_ = 1;
}

void Noema::clearPercepts() {
    percepts_.clear();
    allPercepts_.clear();
    stats_.perceptsActive = 0;
}

void Noema::clearBeliefs() {
    beliefs_.clear();
    stats_.beliefsTracked = 0;
}

void Noema::ingestStimulus(const Stimulus& stimulus) {
    stats_.stimuliTotal++;

    if (!percepts_.empty()) {
        f32 decay = 1.0f - config_.perceptDecayRate;
        for (auto it = percepts_.begin(); it != percepts_.end(); ++it) {
            Percept& p = it.value();
            p.confidence *= decay;
            if (p.confidence < 0.0f) p.confidence = 0.0f;
        }
    }

    u64 key = perceptKey(stimulus.modality, stimulus.sourceId);
    auto it = percepts_.find(key);
    if (it == percepts_.end()) {
        if (percepts_.size() >= config_.maxPercepts) {
            evictLowestConfidence();
        }
        Percept p;
        p.id = nextPerceptId_++;
        p.modality = stimulus.modality;
        p.sourceId = stimulus.sourceId;
        p.position = stimulus.position;
        p.confidence = stimulus.confidence;
        p.lastUpdatedFrame = stimulus.frame;
        p.creationFrame = stimulus.frame;
        p.hits = 1;
        p.misses = 0;
        p.attention = stimulus.intensity * stimulus.confidence;
        p.label = stimulus.label;
        percepts_[key] = p;
    } else {
        Percept& p = it.value();
        p.hits++;
        p.lastUpdatedFrame = stimulus.frame;
        p.label = stimulus.label;
        p.attention += stimulus.intensity * stimulus.confidence;
        if (config_.enableFusion) {
            f32 a = p.confidence;
            f32 b = stimulus.confidence;
            p.confidence = 1.0f - (1.0f - a) * (1.0f - b);
            f32 weight = a + b;
            if (weight > 0.0f) {
                p.position = (p.position * a + stimulus.position * b) / weight;
            }
            stats_.fusionCount++;
        } else {
            p.confidence = stimulus.confidence;
            p.position = stimulus.position;
        }
    }

    feedBelief(stimulus);

    stats_.perceptsActive = (u32)percepts_.size();
    stats_.beliefsTracked = (u32)beliefs_.size();
}

const Percept* Noema::getPercept(SenseModality modality, u64 sourceId) const {
    u64 key = perceptKey(modality, sourceId);
    auto it = percepts_.find(key);
    return (it == percepts_.end()) ? nullptr : &it.value();
}

Percept* Noema::getPercept(SenseModality modality, u64 sourceId) {
    u64 key = perceptKey(modality, sourceId);
    auto it = percepts_.find(key);
    return (it == percepts_.end()) ? nullptr : &it.value();
}

Vector<Percept>& Noema::getAllPercepts() {
    allPercepts_.clear();
    allPercepts_.reserve(percepts_.size());
    for (auto it = percepts_.begin(); it != percepts_.end(); ++it) {
        allPercepts_.push_back(it.value());
    }
    return allPercepts_;
}

Belief* Noema::getBelief(const String& name) {
    for (auto it = beliefs_.begin(); it != beliefs_.end(); ++it) {
        if (labelsMatchIgnoreCase(it.key(), name)) {
            return &it.value();
        }
    }
    return nullptr;
}

const Belief* Noema::getBelief(const String& name) const {
    for (auto it = beliefs_.begin(); it != beliefs_.end(); ++it) {
        if (labelsMatchIgnoreCase(it.key(), name)) {
            return &it.value();
        }
    }
    return nullptr;
}

void Noema::setBeliefPrior(const String& name, f32 prior) {
    Belief* belief = getBelief(name);
    if (!belief) {
        Belief b;
        b.id = nextBeliefId_++;
        b.name = name;
        b.prior = prior;
        b.posterior = prior;
        beliefs_[name] = b;
        belief = getBelief(name);
    }
    if (belief) {
        belief->prior = prior;
    }
}

void Noema::update(f32 dt) {
    if (dt < 0.0f) dt = 0.0f;
    f32 perceptDecay = std::exp(-config_.perceptDecayRate * dt);
    f32 beliefDecay = std::exp(-config_.beliefDecayRate * dt);

    Vector<u64> toRemove;
    for (auto it = percepts_.begin(); it != percepts_.end(); ++it) {
        Percept& p = it.value();
        p.confidence *= perceptDecay;
        p.attention *= perceptDecay;
        p.misses++;
        if (p.confidence < config_.perceptLifetimeThreshold) {
            toRemove.push_back(it.key());
        }
    }
    for (usize i = 0; i < toRemove.size(); ++i) {
        percepts_.erase(toRemove[i]);
    }

    for (auto it = beliefs_.begin(); it != beliefs_.end(); ++it) {
        Belief& b = it.value();
        b.posterior = b.prior + (b.posterior - b.prior) * beliefDecay;
        b.attentionWeight *= beliefDecay;
    }

    stats_.perceptsActive = (u32)percepts_.size();
    stats_.beliefsTracked = (u32)beliefs_.size();
}

Vector<Percept> Noema::attention(u64 maxSalient) const {
    Vector<Percept> result;
    if (percepts_.empty() || maxSalient == 0) return result;
    result.reserve(percepts_.size());
    for (auto it = percepts_.begin(); it != percepts_.end(); ++it) {
        result.push_back(it.value());
    }
    std::sort(result.begin(), result.end(), [](const Percept& a, const Percept& b) {
        f32 sa = a.confidence * a.attention;
        f32 sb = b.confidence * b.attention;
        return sa > sb;
    });
    if (result.size() > (usize)maxSalient) {
        result.resize((usize)maxSalient);
    }
    return result;
}

void Noema::feedBelief(const Stimulus& stimulus) {
    if (stimulus.label.empty()) return;
    Belief* belief = getBelief(stimulus.label);
    if (!belief) {
        Belief b;
        b.id = nextBeliefId_++;
        b.name = stimulus.label;
        b.prior = 0.5f;
        b.posterior = 0.5f;
        beliefs_[stimulus.label] = b;
        belief = getBelief(stimulus.label);
    }
    if (!belief) return;

    f32 likelihood = stimulus.confidence;
    if (likelihood < 0.05f) likelihood = 0.05f;
    if (likelihood > 0.95f) likelihood = 0.95f;
    f32 p = belief->prior;
    f32 denom = (likelihood * p) + (1.0f - likelihood) * (1.0f - p);
    if (denom > 0.0f) {
        belief->posterior = (likelihood * p) / denom;
    }
    belief->evidenceCount++;
    belief->lastEvidenceFrame = stimulus.frame;
    belief->attentionWeight += stimulus.intensity * stimulus.confidence;
}

void Noema::evictLowestConfidence() {
    u64 evictKey = 0;
    f32 lowest = 1e30f;
    bool found = false;
    for (auto it = percepts_.begin(); it != percepts_.end(); ++it) {
        if (it.value().confidence < lowest) {
            lowest = it.value().confidence;
            evictKey = it.key();
            found = true;
        }
    }
    if (found) percepts_.erase(evictKey);
}

} // namespace Frost
