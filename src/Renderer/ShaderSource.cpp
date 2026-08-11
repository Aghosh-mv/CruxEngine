#include "Renderer/ShaderSource.h"

#include <cstring>
#include <utility>

namespace Frost {
namespace ShaderSource {

namespace {

u64 hashSourceFlags(const char* source, u32 flags) {
    u64 h = 1469598103934665603ull;
    if (source) {
        for (const char* p = source; *p; ++p) {
            h ^= (u8)*p;
            h *= 1099511628211ull;
        }
    }
    h ^= (u64)flags;
    h *= 1099511628211ull;
    return h;
}

bool wordMatch(const String& s, const char* w) {
    usize wl = strlen(w);
    for (usize i = 0; i + wl <= s.size(); i++) {
        if (s[i] != w[0]) continue;
        if (i > 0 && s[i - 1] != ' ' && s[i - 1] != '(' && s[i - 1] != '\t') continue;
        usize k = 0;
        while (k < wl && s[i + k] == w[k]) k++;
        if (k != wl) continue;
        usize after = i + wl;
        if (after == s.size() || s[after] == ' ' || s[after] == ';' ||
            s[after] == ')' || s[after] == '\t') {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

u32 ShaderSource_cache::registerVariant(const char* name, u32 featureMask, const char* entryPoint) {
    if (!name) return 0xFFFFFFFF;
    for (usize i = 0; i < variants_.size(); i++) {
        if (variants_[i].name == name) return (u32)i;
    }
    if (variants_.size() >= maxVariants_) return 0xFFFFFFFF;
    ShaderVariant variant;
    variant.name = name;
    variant.featureMask = featureMask;
    variant.entryPoint = entryPoint ? entryPoint : "main";
    variants_.push_back(std::move(variant));
    return (u32)(variants_.size() - 1);
}

const ShaderVariant& ShaderSource_cache::getVariant(u32 id) const {
    if (id >= variants_.size()) return kEmptyVariant();
    return variants_[id];
}

u32 ShaderSource_cache::compileVariant(u32 id, const char* source, u32 featureFlags) {
    if (id >= variants_.size() || !source) return 0xFFFFFFFF;
    u64 key = hashSourceFlags(source, featureFlags);
    auto it = variantCache_.find(key);
    if (it != variantCache_.end()) return it.value();
    if (variantCache_.size() >= maxVariants_) variantCache_.clear();
    u32 shaderId = nextShaderId_++;
    variantCache_[key] = shaderId;
    return shaderId;
}

void ShaderSource_cache::recordCompile(u32 shaderId, u64 sourceHash, u32 variantCount,
                                       u32 compileTimeMs, bool success) {
    usize idx = compileRecords_.size();
    for (usize i = 0; i < compileRecords_.size(); i++) {
        if (compileRecords_[i].shaderId == shaderId) {
            idx = i;
            break;
        }
    }
    if (idx == compileRecords_.size()) {
        ShaderCompileRecord record;
        record.shaderId = shaderId;
        record.sourceHash = sourceHash;
        record.variantCount = variantCount;
        record.compileTimeMs = compileTimeMs;
        record.success = success;
        compileRecords_.push_back(record);
    } else {
        compileRecords_[idx].sourceHash = sourceHash;
        compileRecords_[idx].variantCount = variantCount;
        compileRecords_[idx].compileTimeMs = compileTimeMs;
        compileRecords_[idx].success = success;
    }
    totalVariants_ += variantCount;
    totalCompileTimeMs_ += (f32)compileTimeMs;
    if (success) successfulCompiles_++;
    else failedCompiles_++;
}

const ShaderCompileRecord& ShaderSource_cache::getCompileRecord(u32 shaderId) const {
    for (usize i = 0; i < compileRecords_.size(); i++) {
        if (compileRecords_[i].shaderId == shaderId) return compileRecords_[i];
    }
    return kEmptyRecord();
}

f32 ShaderSource_cache::getCompileSuccessRate() const {
    u32 total = successfulCompiles_ + failedCompiles_;
    if (total == 0) return 0.0f;
    return (f32)successfulCompiles_ / (f32)total;
}

void ShaderSource_cache::clearCache() {
    variantCache_.clear();
    compileRecords_.clear();
    totalVariants_ = 0;
    successfulCompiles_ = 0;
    failedCompiles_ = 0;
    totalCompileTimeMs_ = 0.0f;
}

u32 ShaderSource_cache::reflectShader(u32 shaderId, const char* source) {
    if (!source) return 0;
    ShaderReflection reflection;
    reflection.shaderId = shaderId;
    reflection.entryPoint = "main";
    String src(source);
    usize pos = 0;
    while (pos < src.size()) {
        usize nl = src.find('\n', pos);
        String line = (nl == String::npos) ? src.substr(pos) : src.substr(pos, nl - pos);
        pos = (nl == String::npos) ? src.size() : nl + 1;
        usize s = 0;
        while (s < line.size() && (line[s] == ' ' || line[s] == '\t')) s++;
        String trimmed = line.substr(s);
        if (trimmed.empty()) continue;
        if (trimmed[0] == '#' || trimmed[0] == '/' || trimmed[0] == '*') continue;
        if (trimmed.startsWith("struct ") || trimmed.startsWith("void ") ||
            trimmed.startsWith("const ")) {
            continue;
        }
        if (trimmed.startsWith("uniform ") && trimmed.find(';') != String::npos) {
            reflection.uniforms.push_back(trimmed);
            reflection.uniformCount++;
        } else if ((wordMatch(trimmed, "in") || wordMatch(trimmed, "out")) &&
                   trimmed.find(';') != String::npos) {
            reflection.attributes.push_back(trimmed);
            reflection.attributeCount++;
        }
    }
    reflections_[shaderId] = reflection;
    return reflection.uniformCount + reflection.attributeCount;
}

const ShaderReflection& ShaderSource_cache::getReflection(u32 shaderId) const {
    auto it = reflections_.find(shaderId);
    if (it != reflections_.end()) return it.value();
    return kEmptyReflection();
}

const ShaderVariant& ShaderSource_cache::kEmptyVariant() {
    static const ShaderVariant kEmpty;
    return kEmpty;
}

const ShaderCompileRecord& ShaderSource_cache::kEmptyRecord() {
    static const ShaderCompileRecord kEmpty;
    return kEmpty;
}

const ShaderReflection& ShaderSource_cache::kEmptyReflection() {
    static const ShaderReflection kEmpty;
    return kEmpty;
}

}
}
