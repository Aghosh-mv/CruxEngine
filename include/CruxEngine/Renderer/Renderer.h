#pragma once

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/Math.h"
#include "Renderer/Shader.h"
#include "Renderer/Mesh.h"
#include "Renderer/Material.h"
#include "Renderer/Camera.h"
#include "Renderer/Light.h"
#include "Renderer/Texture.h"
#include "Platform/Window.h"

namespace Crux {

// Terrain generators: pure functions of world coordinates so the world can be
// endless. The renderer streams a quadtree-style LOD of chunks around the
// camera and only ever draws the visible neighbourhood.
typedef f32 (*TerrainHeightFn)(f32 x, f32 z, void* user);
typedef f32 (*TerrainBiomeFn)(f32 x, f32 z, void* user);

struct RenderItem {
    const Mesh* mesh = nullptr;
    const Material* material = nullptr;
    Mat4 model;
    Vec3 boundsCenter{ 0, 0, 0 };
    f32 boundsRadius = 1.0f;
    bool castShadow = true;
    bool transparent = false;
};

struct DrawStats {
    u32 opaqueDraws = 0;
    u32 transparentDraws = 0;
    u32 triangles = 0;
    u32 shadowDraws = 0;
    u32 particleCount = 0;
    u32 terrainChunks = 0;
    u32 terrainVerts = 0;
    f32 gpuTimeMs = 0.0f;
    u32 fps = 0;
};

// High-level renderer: owns all shaders, drives the shadow, reflection,
// depth/SSAO, sky, main, particle, debug and post-process passes each frame.
class Renderer {
public:
    bool init(const Window& window, u32 msaaSamples = 4);
    void shutdown();

    void beginFrame();
    void endFrame();

    // Scene submission
    void submit(const Mesh& mesh, const Material& material, const Mat4& model,
                const Vec3& boundsCenter = Vec3(0), f32 boundsRadius = 1.0f,
                bool castShadow = true);
    void submitTransparent(const Mesh& mesh, const Material& material, const Mat4& model,
                           const Vec3& boundsCenter = Vec3(0), f32 boundsRadius = 1.0f);

    // Instanced submission: one mesh, N model matrices.
    void submitInstanced(const Mesh& mesh, const Material& material,
                         const Vector<Mat4>& models, bool castShadow = true);

    void setCamera(Camera& cam) { camera_ = &cam; }
    void setSun(const Light& sun);
    void addPointLight(const Light& light);
    void clearLights();

    // Water plane (follows the camera -> endless ocean)
    void setWaterLevel(f32 level) { waterLevel_ = level; }
    void setWaterParams(const Color& shallow, const Color& deep, f32 amplitude) {
        waterColor_ = shallow; deepWaterColor_ = deep; waveAmplitude_ = amplitude;
    }
    void setWaterReflection(bool e) { reflectionEnabled_ = e; }

    // Endless chunked terrain via world-coordinate generators.
    void setTerrainGenerator(TerrainHeightFn height, TerrainBiomeFn biome, void* user,
                             f32 heightScale, f32 heightOffset,
                             const Texture2D& grass, const Texture2D& rock,
                             const Texture2D& snow,
                             f32 tiling, f32 snowHeight, f32 rockSlope);

    // Sky
    void setSkyEnabled(bool e) { skyEnabled_ = e; }
    void setNightFactor(f32 f) { nightFactor_ = f; }
    void setFog(f32 density, const Color& color);

    // Particles: simple instanced point sprite stream.
    struct Particle {
        Vec3 position{ 0, 0, 0 };
        Vec3 color{ 1, 1, 1 };
        f32 size = 1.0f;
        f32 alpha = 1.0f;
    };
    void submitParticles(const Vector<Particle>& particles);

    // Debug lines (world-space)
    void drawLine(const Vec3& a, const Vec3& b, const Color& c);
    void drawBox(const Vec3& center, const Vec3& halfExtents, const Color& c);
    void drawSphere(const Vec3& center, f32 radius, const Color& c);
    void flushDebug(const Camera& cam);

    // Text overlay
    void drawText(const char* text, f32 x, f32 y, f32 scale, const Color& color);
    void flushText();

    // Filled screen-space rects for HUD (bars, minimap, panels).
    void drawRectFilled(f32 x, f32 y, f32 w, f32 h, const Color& color);
    void flushHud();

    void setAmbient(const Color& sky, const Color& ground, f32 intensity) {
        ambientSky_ = sky; ambientGround_ = ground; ambientIntensity_ = intensity;
    }
    void setExposure(f32 e) { exposure_ = e; }
    void setBloom(f32 strength) { bloomStrength_ = strength; }
    void setVignette(f32 v) { vignette_ = v; }
    void setSsao(bool e) { ssaoEnabled_ = e; }
    void setSsgi(bool e) { ssgiEnabled_ = e; }
    void setGodRays(bool e) { godRaysEnabled_ = e; }

    const DrawStats& stats() const { return stats_; }
    u32 width() const { return width_; }
    u32 height() const { return height_; }
    bool ready() const { return ready_; }

    Camera& debugCamera() { return debugCam_; }

    void setShadowSize(u32 size) { shadowSize_ = size; }
    void setShadowDistance(f32 d) { shadowDistance_ = d; }

private:
    void renderShadowPass();
    void renderReflectionPass();
    void renderDepthPrepass();
    void renderSsaoPass();
    void renderSkyPass(const Vec3& camPos, const Mat4& viewProj);
    void renderTerrainChunks(const Vec3& camPos, const Mat4& viewProj, bool clip);
    void renderMainPass();
    void renderWaterPass();
    void renderParticlesPass();
    void renderPostPass();
    void renderDebugPass();
    void blitMainToScreen();

    void applyMaterial(const Material& mat, const Shader& sh);
    void setupGlobalUniforms(const Shader& sh, const Vec3& camPos, const Mat4& viewProj, bool clip);

    // Chunked terrain streaming
    void updateTerrainStream(const Vec3& camPos);
    bool chunkExists(i64 key) const;
    void buildChunk(i32 level, i32 ix, i32 iz);
    void drawChunks(const Vec3& camPos, const Mat4& viewProj, bool shadows);
    void drawChunkDepth(const Vec3& camPos, const Mat4& viewProj);

    // Pipelines / resources
    Shader pbrShader_;
    Shader terrainShader_;
    Shader terrainShadowShader_;
    Shader waterShader_;
    Shader skyShader_;
    Shader particleShader_;
    Shader shadowShader_;
    Shader depthShader_;
    Shader ssaoShader_;
    Shader godrayShader_;
    Shader postShader_;
    Shader blurShader_;
    Shader compositeShader_;
    Shader textShader_;
    Shader hudRectShader_;
    Shader debugShader_;

    Mesh fullscreenTri_;
    Mesh skyCube_;
    Mesh waterPlane_;
    Mesh debugMesh_;
    Mesh particleMesh_;
    Mesh textMesh_;
    Vector<u32> textIndices_;
    Mesh hudRectMesh_;
    Vector<u32> hudIndices_;

    FrameBuffer mainFbo_;      // MSAA main target
    FrameBuffer resolveFbo_;   // resolved color
    FrameBuffer blurFbo_[2];   // bloom ping-pong
    FrameBuffer shadowFbo_[2]; // two shadow cascades
    FrameBuffer depthFbo_;     // raw depth (SSAO), half res
    FrameBuffer ssaoFbo_;      // ambient occlusion
    FrameBuffer ssaoBlurFbo_;
    FrameBuffer godrayFbo_;
    FrameBuffer reflectionFbo_;

    Texture2D whiteTex_;
    Texture2D fontTex_;
    const Texture2D* terrainGrass_ = nullptr;
    const Texture2D* terrainRock_ = nullptr;
    const Texture2D* terrainSnow_ = nullptr;
    Cubemap skyCubemap_;

    Camera shadowCam_;
    Mat4 shadowVP_[2];
    Mat4 reflectionVP_;
    Camera debugCam_;
    Camera* camera_ = nullptr;

    Light sun_;
    Light sunForShadows_;
    Light pointLights_[16];
    u32 pointLightCount_ = 0;

    struct InstancedSubmit {
        const Mesh* mesh = nullptr;
        const Material* material = nullptr;
        Vector<Mat4> models;
        bool castShadow = true;
    };
    Vector<InstancedSubmit> scratchInstances_;

    Vector<RenderItem> opaqueItems_;
    Vector<RenderItem> transparentItems_;

    // Terrain streaming state
    struct TerrainChunk {
        i64 key = 0;
        i32 level = 0;
        i32 ix = 0;
        i32 iz = 0;
        Mesh mesh;
        bool active = false;
    };
    Vector<TerrainChunk> chunks_;
    i32 chunkGrid_[4] = { 64, 64, 64, 64 };       // quads per side
    f32 chunkCell_[4] = { 3.0f, 12.0f, 48.0f, 160.0f };
    f32 chunkExtent_[4] = { 192.0f, 768.0f, 3072.0f, 10240.0f };
    f32 loadRadius_[4] = { 700.0f, 1800.0f, 5500.0f, 26000.0f };

    TerrainHeightFn heightFn_ = nullptr;
    TerrainBiomeFn biomeFn_ = nullptr;
    void* terrainUser_ = nullptr;
    f32 terrainHeightScale_ = 1.0f;
    f32 terrainHeightOffset_ = 0.0f;
    f32 terrainTiling_ = 8.0f;
    f32 terrainSnowHeight_ = 90.0f;
    f32 terrainRockSlope_ = 0.35f;
    bool terrainEnabled_ = false;
    Vec2 sunScreen_ = Vec2(-10, -10);

    // Debug line geometry
    Vector<Vertex> debugVerts_;
    bool debugLinesDirty_ = false;

    // Text rendering
    Vector<Vertex> textVerts_;

    // HUD rect rendering
    Vector<Vertex> hudVerts_;

    // Particle geometry
    Vector<Vertex> particleVerts_;
    bool particlesDirty_ = false;
    u32 lastParticleCount_ = 0;

    Window* window_ = nullptr;
    u32 width_ = 0, height_ = 0;
    u32 msaaSamples_ = 4;
    bool ready_ = false;

    f32 time_ = 0.0f;
    u32 frame_ = 0;

    // Scene settings
    f32 waterLevel_ = -10.0f;
    Color waterColor_{ 0.1f, 0.35f, 0.48f, 1.0f };
    Color deepWaterColor_{ 0.05f, 0.18f, 0.32f, 1.0f };
    f32 waveAmplitude_ = 1.0f;

    bool skyEnabled_ = true;
    f32 nightFactor_ = 0.0f;
    f32 fogDensity_ = 0.0006f;
    Color fogColor_{ 0.6f, 0.7f, 0.8f, 1.0f };

    Color ambientSky_{ 0.55f, 0.65f, 0.85f, 1.0f };
    Color ambientGround_{ 0.35f, 0.3f, 0.25f, 1.0f };
    f32 ambientIntensity_ = 0.45f;

    f32 exposure_ = 1.1f;
    f32 bloomStrength_ = 0.25f;
    f32 vignette_ = 0.25f;

    bool ssaoEnabled_ = true;
    bool ssgiEnabled_ = true;
    bool godRaysEnabled_ = true;
    bool reflectionEnabled_ = true;

    u32 shadowSize_ = 2048;
    f32 shadowDistance_ = 420.0f;
    f32 cascadeDist_ = 130.0f;

    DrawStats stats_;
};

}
