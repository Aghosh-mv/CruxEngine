#pragma once

#include "Core/Types.h"
#include "Core/HashMap.h"

// All engine shaders are embedded as C++ raw strings so the engine has no
// runtime dependency on shader file paths. Editing these regenerates the
// pipeline at the next build.

namespace Frost {
namespace ShaderSource {

// ---------------------------------------------------------------- PBR main
extern const char* pbrVert;
extern const char* pbrFrag;

// --------------------------------------------------------------- terrain
extern const char* terrainVert;
extern const char* terrainFrag;

// ----------------------------------------------------------------- water
extern const char* waterVert;
extern const char* waterFrag;

// ------------------------------------------------------------------ sky
extern const char* skyVert;
extern const char* skyFrag;

// ------------------------------------------------------------- particles
extern const char* particleVert;
extern const char* particleFrag;

// ------------------------------------------------------------- shadows
extern const char* shadowVert;
extern const char* shadowFrag;
extern const char* terrainShadowVert;

// ---------------------------------------------------- depth / ssao / rays
extern const char* depthVert;
extern const char* depthFrag;
extern const char* ssaoFrag;
extern const char* godrayFrag;

// ------------------------------------------------------- postprocessing
extern const char* postVert;
extern const char* postFrag;
extern const char* blurVert;
extern const char* blurFrag;
extern const char* compositeFrag;

// ------------------------------------------------------------------ text
extern const char* textVert;
extern const char* textFrag;

// ------------------------------------------------------------------- hud
extern const char* hudRectFrag;

// ----------------------------------------------------------------- debug
extern const char* debugVert;
extern const char* debugFrag;

// ============================================================================
// Shader compilation cache, variant tracking and shader reflection
// ============================================================================

struct ShaderVariant {
    String name;
    u32 featureMask = 0;
    Vector<String> defines;
    String entryPoint = "main";
};

struct ShaderCompileRecord {
    u32 shaderId = 0;
    u64 sourceHash = 0;
    u32 variantCount = 0;
    u32 compileTimeMs = 0;
    bool success = false;
};

struct ShaderReflection {
    u32 shaderId = 0;
    String entryPoint;
    Vector<String> attributes;
    Vector<String> uniforms;
    u32 attributeCount = 0;
    u32 uniformCount = 0;
};

class ShaderSource_cache {
public:
    u32 registerVariant(const char* name, u32 featureMask, const char* entryPoint);
    const ShaderVariant& getVariant(u32 id) const;
    u32 compileVariant(u32 id, const char* source, u32 featureFlags);
    void recordCompile(u32 shaderId, u64 sourceHash, u32 variantCount,
                       u32 compileTimeMs, bool success);
    const ShaderCompileRecord& getCompileRecord(u32 shaderId) const;
    u32 getTotalVariants() const { return totalVariants_; }
    u32 getSuccessfulCompiles() const { return successfulCompiles_; }
    u32 getFailedCompiles() const { return failedCompiles_; }
    f32 getTotalCompileTimeMs() const { return totalCompileTimeMs_; }
    f32 getCompileSuccessRate() const;
    void clearCache();
    u32 getMaxVariants() const { return maxVariants_; }
    void setMaxVariants(u32 count) { maxVariants_ = count ? count : 1u; }

    u32 reflectShader(u32 shaderId, const char* source);
    const ShaderReflection& getReflection(u32 shaderId) const;

private:
    static const ShaderVariant& kEmptyVariant();
    static const ShaderCompileRecord& kEmptyRecord();
    static const ShaderReflection& kEmptyReflection();

    Vector<ShaderVariant> variants_;
    HashMap<u64, u32> variantCache_;
    Vector<ShaderCompileRecord> compileRecords_;
    HashMap<u32, ShaderReflection> reflections_;
    u32 totalVariants_ = 0;
    u32 successfulCompiles_ = 0;
    u32 failedCompiles_ = 0;
    f32 totalCompileTimeMs_ = 0.0f;
    u32 maxVariants_ = 512;
    u32 nextShaderId_ = 1;
};

}
}
