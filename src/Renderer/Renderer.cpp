#include "Renderer/Renderer.h"
#include "Renderer/Gl.h"
#include "Renderer/ShaderSource.h"
#include "Renderer/Font.h"
#include "Assets/MeshFactory.h"
#include "Core/Log.h"
#include <cmath>

namespace Frost {

static i64 makeChunkKey(i32 level, i32 ix, i32 iz) {
    return ((i64)level << 52) |
           ((i64)((u32)(ix + 4096) & 0x1FFFFF) << 26) |
           ((i64)((u32)(iz + 4096) & 0x1FFFFF));
}

static f32 chunkWorldSize(i32 level, const i32 grid[4], const f32 cell[4]) {
    return (f32)grid[level] * cell[level];
}

bool Renderer::init(const Window& window, u32 msaaSamples) {
    window_ = (Window*)&window;
    width_ = window.width();
    height_ = window.height();
    msaaSamples_ = msaaSamples;

    FROST_LOG_INFO("[Renderer] initializing...");

    if (!pbrShader_.create(ShaderSource::pbrVert, ShaderSource::pbrFrag, "pbr")) return false;
    if (!terrainShader_.create(ShaderSource::terrainVert, ShaderSource::terrainFrag, "terrain")) return false;
    if (!terrainShadowShader_.create(ShaderSource::terrainShadowVert, ShaderSource::shadowFrag, "terrainShadow")) return false;
    if (!waterShader_.create(ShaderSource::waterVert, ShaderSource::waterFrag, "water")) return false;
    if (!skyShader_.create(ShaderSource::skyVert, ShaderSource::skyFrag, "sky")) return false;
    if (!particleShader_.create(ShaderSource::particleVert, ShaderSource::particleFrag, "particle")) return false;
    if (!shadowShader_.create(ShaderSource::shadowVert, ShaderSource::shadowFrag, "shadow")) return false;
    if (!depthShader_.create(ShaderSource::depthVert, ShaderSource::depthFrag, "depth")) return false;
    if (!ssaoShader_.create(ShaderSource::postVert, ShaderSource::ssaoFrag, "ssao")) return false;
    if (!godrayShader_.create(ShaderSource::postVert, ShaderSource::godrayFrag, "godray")) return false;
    if (!postShader_.create(ShaderSource::postVert, ShaderSource::postFrag, "post")) return false;
    if (!blurShader_.create(ShaderSource::blurVert, ShaderSource::blurFrag, "blur")) return false;
    if (!compositeShader_.create(ShaderSource::postVert, ShaderSource::compositeFrag, "composite")) return false;
    if (!textShader_.create(ShaderSource::textVert, ShaderSource::textFrag, "text")) return false;
    if (!hudRectShader_.create(ShaderSource::textVert, ShaderSource::hudRectFrag, "hudRect")) return false;
    if (!debugShader_.create(ShaderSource::debugVert, ShaderSource::debugFrag, "debug")) return false;

    fullscreenTri_ = MeshFactory::makeFullscreenTriangle();
    skyCube_ = MeshFactory::makeCube(2.0f);
    waterPlane_ = MeshFactory::makePlane(1.0f, 1.0f, 127, 127);

    // White 1x1 texture for untextured materials
    u8 white[4] = { 255, 255, 255, 255 };
    whiteTex_.create(1, 1, white, false, TextureFilter::Nearest, TextureWrap::Repeat);

    // Font atlas
    Font font;
    fontTex_.create(font.atlas().width, font.atlas().height, font.atlas().data, false,
                    TextureFilter::Nearest, TextureWrap::Clamp);

    // Framebuffers
    if (!mainFbo_.create(width_, height_, true, msaaSamples_, true)) return false;
    if (!resolveFbo_.create(width_, height_, false, 1, true)) return false;
    if (!blurFbo_[0].create(width_, height_, false, 1, true)) return false;
    if (!blurFbo_[1].create(width_, height_, false, 1, true)) return false;
    if (!shadowFbo_[0].create(shadowSize_, shadowSize_, true, 1, false)) return false;
    if (!shadowFbo_[1].create(shadowSize_, shadowSize_, true, 1, false)) return false;
    if (!depthFbo_.createDepth(width_ / 2, height_ / 2)) return false;
    if (!ssaoFbo_.create(width_ / 2, height_ / 2, false, 1, true)) return false;
    if (!ssaoBlurFbo_.create(width_ / 2, height_ / 2, false, 1, true)) return false;
    if (!godrayFbo_.create(width_, height_, false, 1, true)) return false;
    if (!reflectionFbo_.create(width_, height_, true, 1, false)) return false;

    // Default lighting state
    sun_ = Light::directional(Vec3(0.5f, -0.8f, -0.3f), Color(1.0f, 0.96f, 0.9f), 3.0f);
    sun_.castShadow = true;

    ready_ = true;
    FROST_LOG_INFO("[Renderer] ready (%ux%u, MSAA x%u)", width_, height_, msaaSamples_);
    return true;
}

void Renderer::shutdown() {
    if (!ready_) return;
    whiteTex_.destroy();
    fontTex_.destroy();
    mainFbo_.destroy();
    resolveFbo_.destroy();
    blurFbo_[0].destroy();
    blurFbo_[1].destroy();
    shadowFbo_[0].destroy();
    shadowFbo_[1].destroy();
    depthFbo_.destroy();
    ssaoFbo_.destroy();
    ssaoBlurFbo_.destroy();
    godrayFbo_.destroy();
    reflectionFbo_.destroy();
    for (auto& c : chunks_) c.mesh.destroy();
    chunks_.clear();
    ready_ = false;
}

void Renderer::setSun(const Light& sun) { sun_ = sun; }

void Renderer::addPointLight(const Light& light) {
    if (pointLightCount_ < 16) pointLights_[pointLightCount_++] = light;
}

void Renderer::clearLights() { pointLightCount_ = 0; }

void Renderer::setFog(f32 density, const Color& color) {
    fogDensity_ = density;
    fogColor_ = color;
}

void Renderer::setTerrainGenerator(TerrainHeightFn height, TerrainBiomeFn biome, void* user,
                                   f32 heightScale, f32 heightOffset,
                                   const Texture2D& grass, const Texture2D& rock,
                                   const Texture2D& snow,
                                   f32 tiling, f32 snowHeight, f32 rockSlope) {
    heightFn_ = height;
    biomeFn_ = biome;
    terrainUser_ = user;
    terrainHeightScale_ = heightScale;
    terrainHeightOffset_ = heightOffset;
    terrainGrass_ = &grass;
    terrainRock_ = &rock;
    terrainSnow_ = &snow;
    terrainTiling_ = tiling;
    terrainSnowHeight_ = snowHeight;
    terrainRockSlope_ = rockSlope;
    terrainEnabled_ = true;
    for (auto& c : chunks_) c.mesh.destroy();
    chunks_.clear();
}

void Renderer::submit(const Mesh& mesh, const Material& mat, const Mat4& model,
                      const Vec3& boundsCenter, f32 boundsRadius, bool castShadow) {
    RenderItem item;
    item.mesh = &mesh;
    item.material = &mat;
    item.model = model;
    item.boundsCenter = boundsCenter;
    item.boundsRadius = boundsRadius;
    item.castShadow = castShadow && mat.castShadow;
    if (mat.alphaBlend) {
        item.transparent = true;
        transparentItems_.pushBack(item);
    } else {
        opaqueItems_.pushBack(item);
    }
}

void Renderer::submitTransparent(const Mesh& mesh, const Material& mat, const Mat4& model,
                                 const Vec3& boundsCenter, f32 boundsRadius) {
    RenderItem item;
    item.mesh = &mesh;
    item.material = &mat;
    item.model = model;
    item.boundsCenter = boundsCenter;
    item.boundsRadius = boundsRadius;
    item.castShadow = false;
    item.transparent = true;
    transparentItems_.pushBack(item);
}

void Renderer::submitInstanced(const Mesh& mesh, const Material& mat,
                               const Vector<Mat4>& models, bool castShadow) {
    if (models.empty()) return;
    InstancedSubmit inst;
    inst.mesh = &mesh;
    inst.material = &mat;
    inst.models = models;
    inst.castShadow = castShadow && mat.castShadow;
    scratchInstances_.pushBack(inst);
}

void Renderer::beginFrame() {
    opaqueItems_.clear();
    transparentItems_.clear();
    scratchInstances_.clear();
    pointLightCount_ = 0;
    debugVerts_.clear();
    debugLinesDirty_ = false;
    textVerts_.clear();
    hudVerts_.clear();
    particleVerts_.clear();
    particlesDirty_ = false;
    stats_ = DrawStats();
    frame_++;
}

void Renderer::endFrame() {
    if (!ready_ || !camera_) return;
    time_ += 0.016f;

    // Resize on window change
    if (window_ && (window_->width() != width_ || window_->height() != height_)) {
        width_ = window_->width();
        height_ = window_->height();
        mainFbo_.resize(width_, height_);
        resolveFbo_.resize(width_, height_);
        blurFbo_[0].resize(width_, height_);
        blurFbo_[1].resize(width_, height_);
        godrayFbo_.resize(width_, height_);
        reflectionFbo_.resize(width_, height_);
        depthFbo_.destroy();
        depthFbo_.createDepth(width_ / 2, height_ / 2);
        ssaoFbo_.resize(width_ / 2, height_ / 2);
        ssaoBlurFbo_.resize(width_ / 2, height_ / 2);
        camera_->setAspect((f32)width_ / (f32)height_);
    }

    // Stream terrain chunks around the camera
    if (terrainEnabled_) updateTerrainStream(camera_->position());

    // Sun screen position for god rays
    {
        Vec3 camPos = camera_->position();
        Vec3 sunPos = camPos + sun_.direction.normalized() * 1000.0f;
        Vec4 clip = camera_->viewProj() * Vec4(sunPos, 1.0f);
        if (clip.w > 0.0f) {
            Vec3 ndc(clip.x / clip.w, clip.y / clip.w, clip.z / clip.w);
            if (std::abs(ndc.x) <= 1.0f && std::abs(ndc.y) <= 1.0f && ndc.z < 1.0f)
                sunScreen_ = Vec2(ndc.x * 0.5f + 0.5f, ndc.y * 0.5f + 0.5f);
            else
                sunScreen_ = Vec2(-10, -10);
        } else {
            sunScreen_ = Vec2(-10, -10);
        }
    }

    renderShadowPass();
    renderReflectionPass();
    renderDepthPrepass();
    renderSsaoPass();
    renderMainPass();
    renderPostPass();
    flushHud();
    flushText();

    stats_.fps = frame_;
}

// ---------------------------------------------------------------------------
// Shadow pass: two cascaded shadow maps fitted around the camera
// ---------------------------------------------------------------------------
void Renderer::renderShadowPass() {
    if (!sun_.castShadow) return;

    Vec3 camPos = camera_->position();
    Vec3 lightDir = sun_.direction.normalized();
    Vec3 up = (std::abs(lightDir.dot(Vec3::up())) > 0.99f) ? Vec3(1, 0, 0) : Vec3::up();

    const f32 cascadeHalf[2] = { 85.0f, 320.0f };

    for (int c = 0; c < 2; c++) {
        Vec3 lightPos = camPos + lightDir * shadowDistance_;
        Mat4 lightView = Mat4::lookAt(lightPos, camPos, up);
        f32 halfX = cascadeHalf[c];
        f32 halfY = halfX * 0.7f;
        f32 zExt = shadowDistance_ * 4.0f;
        Mat4 ortho;
        ortho.m[0] = 1.0f / halfX;
        ortho.m[5] = 1.0f / halfY;
        ortho.m[10] = 1.0f / (zExt * 2.0f);
        ortho.m[15] = 1.0f;
        shadowVP_[c] = ortho * lightView;

        shadowFbo_[c].bind();
        Gl::Viewport(0, 0, (GLsizei)shadowSize_, (GLsizei)shadowSize_);
        Gl::Enable(GL_DEPTH_TEST);
        Gl::DepthMask(GL_TRUE);
        Gl::Clear(GL_DEPTH_BUFFER_BIT);
        Gl::Disable(GL_CULL_FACE);
        Gl::Enable(GL_POLYGON_OFFSET_FILL);
        Gl::PolygonOffset(1.5f, 4.0f);

        shadowShader_.use();
        shadowShader_.setMat4("u_lightVP", shadowVP_[c]);
        shadowShader_.setFloat("u_depthBias", 0.0005f);

        for (const auto& item : opaqueItems_) {
            if (!item.castShadow || item.boundsRadius == 0.0f) continue;
            shadowShader_.setFloat("u_useInstancing", 0.0f);
            shadowShader_.setMat4("u_model", item.model);
            item.mesh->draw();
            stats_.shadowDraws++;
        }

        for (const auto& inst : scratchInstances_) {
            if (!inst.castShadow) continue;
            shadowShader_.setFloat("u_useInstancing", 1.0f);
            inst.mesh->uploadInstances(inst.models);
            inst.mesh->drawInstanced((u32)inst.models.size());
            stats_.shadowDraws++;
        }

        // Terrain chunks
        shadowShader_.setFloat("u_useInstancing", 0.0f);
        shadowShader_.setMat4("u_model", Mat4::identity());
        for (const auto& chunk : chunks_) {
            if (!chunk.active) continue;
            chunk.mesh.draw();
            stats_.shadowDraws++;
        }

        Gl::Disable(GL_POLYGON_OFFSET_FILL);
        Gl::Enable(GL_CULL_FACE);
        shadowFbo_[c].unbind();
    }
}

// ---------------------------------------------------------------------------
// Reflection pass: mirrored view of the world clipped at the water plane
// ---------------------------------------------------------------------------
void Renderer::renderReflectionPass() {
    if (!reflectionEnabled_) return;

    Vec3 camPos = camera_->position();
    Vec3 mirPos = Vec3(camPos.x, -camPos.y, camPos.z);
    Mat4 mirror = Mat4::scaling(Vec3(1, -1, 1));
    reflectionVP_ = camera_->proj() * (mirror * camera_->view());

    reflectionFbo_.bind();
    Gl::Viewport(0, 0, (GLsizei)width_, (GLsizei)height_);
    float clearCol[4] = { 0.3f, 0.45f, 0.6f, 1.0f };
    Gl::ClearColor(clearCol[0], clearCol[1], clearCol[2], clearCol[3]);
    Gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Gl::Enable(GL_DEPTH_TEST);
    Gl::DepthFunc(GL_LESS);
    Gl::DepthMask(GL_TRUE);
    Gl::CullFace(GL_BACK);
    Gl::Enable(GL_CULL_FACE);

    renderSkyPass(mirPos, reflectionVP_);

    if (terrainEnabled_ && heightFn_) renderTerrainChunks(mirPos, reflectionVP_, true);

    pbrShader_.use();
    setupGlobalUniforms(pbrShader_, mirPos, reflectionVP_, true);
    for (const auto& item : opaqueItems_) {
        if (item.boundsRadius == 0.0f) continue;
        applyMaterial(*item.material, pbrShader_);
        pbrShader_.setFloat("u_useInstancing", 0.0f);
        pbrShader_.setMat4("u_model", item.model);
        item.mesh->draw();
    }
    for (const auto& inst : scratchInstances_) {
        applyMaterial(*inst.material, pbrShader_);
        pbrShader_.setFloat("u_useInstancing", 1.0f);
        inst.mesh->uploadInstances(inst.models);
        inst.mesh->drawInstanced((u32)inst.models.size());
    }

    Gl::Disable(GL_BLEND);
    reflectionFbo_.unbind();
}

// ---------------------------------------------------------------------------
// Depth prepass (SSAO input) - half resolution, depth only
// ---------------------------------------------------------------------------
void Renderer::renderDepthPrepass() {
    if (!ssaoEnabled_ && !ssgiEnabled_) return;

    const Camera& cam = *camera_;
    depthFbo_.bind();
    Gl::Viewport(0, 0, (GLsizei)(width_ / 2), (GLsizei)(height_ / 2));
    Gl::Enable(GL_DEPTH_TEST);
    Gl::DepthFunc(GL_LESS);
    Gl::DepthMask(GL_TRUE);
    Gl::Clear(GL_DEPTH_BUFFER_BIT);
    Gl::CullFace(GL_BACK);
    Gl::Enable(GL_CULL_FACE);

    depthShader_.use();
    depthShader_.setMat4("u_viewProj", cam.viewProj());

    for (const auto& item : opaqueItems_) {
        if (item.boundsRadius == 0.0f) continue;
        depthShader_.setFloat("u_useInstancing", 0.0f);
        depthShader_.setMat4("u_model", item.model);
        item.mesh->draw();
    }
    for (const auto& inst : scratchInstances_) {
        depthShader_.setFloat("u_useInstancing", 1.0f);
        inst.mesh->uploadInstances(inst.models);
        inst.mesh->drawInstanced((u32)inst.models.size());
    }

    if (terrainEnabled_ && heightFn_) drawChunkDepth(cam.position(), cam.viewProj());

    depthFbo_.unbind();
}

void Renderer::renderSsaoPass() {
    if (!ssaoEnabled_) return;
    ssaoFbo_.bind();
    Gl::Viewport(0, 0, (GLsizei)(width_ / 2), (GLsizei)(height_ / 2));
    Gl::Disable(GL_DEPTH_TEST);

    ssaoShader_.use();
    depthFbo_.depthBind(0);
    ssaoShader_.setInt("u_depth", 0);
    ssaoShader_.setVec2("u_resolution", Vec2((f32)(width_ / 2), (f32)(height_ / 2)));
    ssaoShader_.setFloat("u_radius", 9.0f);
    ssaoShader_.setFloat("u_power", 1.4f);
    fullscreenTri_.draw();

    // Blur AO: horizontal then vertical
    ssaoBlurFbo_.bind();
    blurShader_.use();
    Gl::ActiveTexture(GL_TEXTURE0);
    Gl::BindTexture(GL_TEXTURE_2D, ssaoFbo_.color());
    blurShader_.setInt("u_texture", 0);
    blurShader_.setVec2("u_direction", Vec2(1, 0));
    blurShader_.setFloat("u_radius", 2.0f);
    fullscreenTri_.draw();

    ssaoFbo_.bind();
    Gl::ActiveTexture(GL_TEXTURE0);
    Gl::BindTexture(GL_TEXTURE_2D, ssaoBlurFbo_.color());
    blurShader_.setInt("u_texture", 0);
    blurShader_.setVec2("u_direction", Vec2(0, 1));
    blurShader_.setFloat("u_radius", 2.0f);
    fullscreenTri_.draw();

    ssaoFbo_.unbind();
}

// ---------------------------------------------------------------------------
// Sky
// ---------------------------------------------------------------------------
void Renderer::renderSkyPass(const Vec3& camPos, const Mat4& viewProj) {
    if (!skyEnabled_) return;
    Gl::DepthMask(GL_FALSE);
    Gl::DepthFunc(GL_LEQUAL);
    skyShader_.use();
    Mat4 skyModel = Mat4::translation(camPos);
    skyShader_.setMat4("u_viewProj", viewProj);
    skyShader_.setMat4("u_model", skyModel);
    skyShader_.setVec3("u_sunDir", sun_.direction.normalized());
    skyShader_.setVec3("u_sunColor", sun_.color.rgb());
    skyShader_.setFloat("u_sunIntensity", sun_.intensity);
    skyShader_.setFloat("u_time", time_);
    skyShader_.setFloat("u_nightFactor", nightFactor_);
    skyCube_.draw();
    Gl::DepthFunc(GL_LESS);
    Gl::DepthMask(GL_TRUE);
}

// ---------------------------------------------------------------------------
// Terrain chunks
// ---------------------------------------------------------------------------
void Renderer::renderTerrainChunks(const Vec3& camPos, const Mat4& viewProj, bool clip) {
    if (!terrainEnabled_ || !heightFn_) return;

    terrainShader_.use();
    terrainShader_.setMat4("u_model", Mat4::identity());
    terrainShader_.setMat4("u_viewProj", viewProj);
    terrainShader_.setVec3("u_camPos", camPos);
    terrainShader_.setVec3("u_sunDir", sun_.direction.normalized());
    terrainShader_.setVec3("u_sunColor", sun_.color.rgb());
    terrainShader_.setFloat("u_sunIntensity", sun_.intensity);
    terrainShader_.setMat4("u_lightVP0", shadowVP_[0]);
    terrainShader_.setMat4("u_lightVP1", shadowVP_[1]);
    shadowFbo_[0].depthBind(0);
    terrainShader_.setInt("u_shadowMap0", 0);
    shadowFbo_[1].depthBind(1);
    terrainShader_.setInt("u_shadowMap1", 1);
    terrainShader_.setFloat("u_shadowEnabled", sun_.castShadow ? 1.0f : 0.0f);
    terrainShader_.setFloat("u_shadowTexel", 1.0f / (f32)shadowSize_);
    terrainShader_.setFloat("u_cascadeDist", cascadeDist_);
    terrainShader_.setVec3("u_ambientSky", ambientSky_.rgb());
    terrainShader_.setVec3("u_ambientGround", ambientGround_.rgb());
    terrainShader_.setFloat("u_ambientIntensity", ambientIntensity_);
    terrainShader_.setFloat("u_fogDensity", fogDensity_);
    terrainShader_.setVec3("u_fogColor", fogColor_.rgb());
    terrainShader_.setFloat("u_snowHeight", terrainSnowHeight_);
    terrainShader_.setFloat("u_rockSlope", terrainRockSlope_);
    terrainShader_.setFloat("u_tiling", terrainTiling_);
    terrainShader_.setFloat("u_time", time_);
    terrainGrass_->bind(2);
    terrainShader_.setInt("u_texGrass", 2);
    terrainRock_->bind(3);
    terrainShader_.setInt("u_texRock", 3);
    terrainSnow_->bind(4);
    terrainShader_.setInt("u_texSnow", 4);
    terrainShader_.setVec4("u_clipPlane", Vec4(0, 1, 0, waterLevel_));
    terrainShader_.setFloat("u_clipEnabled", clip ? 1.0f : 0.0f);

    drawChunks(camPos, viewProj, false);
}

void Renderer::drawChunks(const Vec3& camPos, const Mat4& viewProj, bool shadows) {
    (void)camPos; (void)viewProj; (void)shadows;
    for (const auto& chunk : chunks_) {
        if (!chunk.active) continue;
        chunk.mesh.draw();
        stats_.terrainChunks++;
        stats_.terrainVerts += chunk.mesh.vertexCount();
    }
}

void Renderer::drawChunkDepth(const Vec3& camPos, const Mat4& viewProj) {
    (void)camPos;
    depthShader_.setFloat("u_useInstancing", 0.0f);
    depthShader_.setMat4("u_model", Mat4::identity());
    depthShader_.setMat4("u_viewProj", viewProj);
    for (const auto& chunk : chunks_) {
        if (!chunk.active) continue;
        chunk.mesh.draw();
        stats_.terrainChunks++;
    }
}

void Renderer::updateTerrainStream(const Vec3& camPos) {
    for (auto& c : chunks_) c.active = false;

    for (i32 level = 0; level < 4; level++) {
        f32 inner = (level == 0) ? 0.0f : chunkExtent_[level - 1];
        f32 outer = chunkExtent_[level];
        f32 size = chunkWorldSize(level, chunkGrid_, chunkCell_);
        if (size <= 0.0f) continue;

        i64 i0 = (i64)std::floor((camPos.x - outer) / size);
        i64 i1 = (i64)std::floor((camPos.x + outer) / size);
        i64 j0 = (i64)std::floor((camPos.z - outer) / size);
        i64 j1 = (i64)std::floor((camPos.z + outer) / size);

        for (i64 ix = i0; ix <= i1; ix++) {
            for (i64 iz = j0; iz <= j1; iz++) {
                f32 cx = (f32)((f64)ix + 0.5) * size;
                f32 cz = (f32)((f64)iz + 0.5) * size;
                f32 dx = cx - camPos.x;
                f32 dz = cz - camPos.z;
                f32 d = std::sqrt(dx * dx + dz * dz);
                if (d < inner || d > outer) continue;

                i64 key = makeChunkKey(level, (i32)ix, (i32)iz);
                if (chunkExists(key)) {
                    for (auto& c : chunks_) {
                        if (c.key == key) { c.active = true; break; }
                    }
                } else {
                    buildChunk(level, (i32)ix, (i32)iz);
                }
            }
        }
    }

    // Prune inactive chunks (keep a small cache to avoid rebuild churn)
    if (chunks_.size() > 64) {
        usize w = 0;
        for (usize i = 0; i < chunks_.size(); i++) {
            if (chunks_[i].active) {
                if (w != i) chunks_[w] = std::move(chunks_[i]);
                w++;
            }
        }
        chunks_.erase(w, chunks_.size());
    }
}

bool Renderer::chunkExists(i64 key) const {
    for (const auto& c : chunks_) {
        if (c.key == key && c.active) return true;
    }
    return false;
}

void Renderer::buildChunk(i32 level, i32 ix, i32 iz) {
    i32 grid = chunkGrid_[level];
    f32 cell = chunkCell_[level];
    f32 size = (f32)grid * cell;
    f32 x0 = (f32)ix * size;
    f32 z0 = (f32)iz * size;

    Vector<Vertex> verts;
    Vector<u32> idx;
    verts.reserve((usize)(grid + 1) * (grid + 1));
    idx.reserve((usize)grid * grid * 6);

    auto sampleH = [&](f32 wx, f32 wz) -> f32 {
        return heightFn_(wx, wz, terrainUser_) * terrainHeightScale_ + terrainHeightOffset_;
    };
    auto sampleBiome = [&](f32 wx, f32 wz) -> f32 {
        return biomeFn_ ? biomeFn_(wx, wz, terrainUser_) : 0.0f;
    };

    for (i32 gz = 0; gz <= grid; gz++) {
        for (i32 gx = 0; gx <= grid; gx++) {
            f32 wx = x0 + (f32)gx * cell;
            f32 wz = z0 + (f32)gz * cell;
            f32 y = sampleH(wx, wz);

            // Central differences for the normal
            f32 hN = sampleH(wx - cell, wz);
            f32 hS = sampleH(wx + cell, wz);
            f32 hW = sampleH(wx, wz - cell);
            f32 hE = sampleH(wx, wz + cell);
            Vec3 n = Vec3(hN - hS, 2.0f * cell, hW - hE).normalized();

            f32 biome = sampleBiome(wx, wz);
            // Cheap slope-based ambient occlusion
            f32 slope = 1.0f - n.y;
            f32 ao = Mathf::clamp(1.0f - slope * 0.35f, 0.55f, 1.0f);

            Vertex v;
            v.position = Vec3(wx, y, wz);
            v.normal = n;
            v.tangent = Vec4(biome, ao, 0.0f, 1.0f);
            v.uv = Vec2(wx, wz);   // world-space UVs keep texturing seamless
            verts.pushBack(v);
        }
    }

    for (i32 gz = 0; gz < grid; gz++) {
        for (i32 gx = 0; gx < grid; gx++) {
            u32 a = (u32)(gz * (grid + 1) + gx);
            u32 b = a + grid + 1;
            idx.pushBack(a); idx.pushBack(b); idx.pushBack(a + 1);
            idx.pushBack(a + 1); idx.pushBack(b); idx.pushBack(b + 1);
        }
    }

    TerrainChunk chunk;
    chunk.key = makeChunkKey(level, ix, iz);
    chunk.level = level;
    chunk.ix = ix;
    chunk.iz = iz;
    chunk.mesh.upload(verts, idx);
    chunk.active = true;
    chunks_.pushBack(std::move(chunk));
}

// ---------------------------------------------------------------------------
// Main pass
// ---------------------------------------------------------------------------
void Renderer::renderMainPass() {
    const Camera& cam = *camera_;
    const Vec3 camPos = cam.position();
    const Mat4 viewProj = cam.viewProj();

    float clearCol[4] = { 0.55f, 0.7f, 0.85f, 1.0f };
    mainFbo_.clear(clearCol, true);
    Gl::Viewport(0, 0, (GLsizei)width_, (GLsizei)height_);
    Gl::Enable(GL_DEPTH_TEST);
    Gl::DepthFunc(GL_LESS);
    Gl::DepthMask(GL_TRUE);
    Gl::CullFace(GL_BACK);
    Gl::Enable(GL_CULL_FACE);

    // ---- Sky
    renderSkyPass(camPos, viewProj);

    // ---- Terrain
    if (terrainEnabled_ && heightFn_) renderTerrainChunks(camPos, viewProj, false);

    // ---- Opaque PBR
    pbrShader_.use();
    setupGlobalUniforms(pbrShader_, camPos, viewProj, false);
    for (const auto& item : opaqueItems_) {
        if (item.boundsRadius == 0.0f) continue;
        if (!cam.containsSphere(item.boundsCenter, item.boundsRadius)) continue;
        applyMaterial(*item.material, pbrShader_);
        pbrShader_.setFloat("u_useInstancing", 0.0f);
        pbrShader_.setMat4("u_model", item.model);
        item.mesh->draw();
        stats_.opaqueDraws++;
        stats_.triangles += item.mesh->indexCount() / 3;
    }

    // ---- Instanced opaque
    for (const auto& inst : scratchInstances_) {
        applyMaterial(*inst.material, pbrShader_);
        pbrShader_.setFloat("u_useInstancing", 1.0f);
        inst.mesh->uploadInstances(inst.models);
        inst.mesh->drawInstanced((u32)inst.models.size());
        stats_.opaqueDraws++;
        stats_.triangles += inst.mesh->indexCount() / 3 * (u32)inst.models.size();
    }

    // ---- Water
    renderWaterPass();

    // ---- Transparent PBR
    Gl::DepthMask(GL_FALSE);
    Gl::Enable(GL_BLEND);
    Gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    pbrShader_.use();
    setupGlobalUniforms(pbrShader_, camPos, viewProj, false);
    for (const auto& item : transparentItems_) {
        if (item.boundsRadius == 0.0f) continue;
        applyMaterial(*item.material, pbrShader_);
        pbrShader_.setFloat("u_useInstancing", 0.0f);
        pbrShader_.setMat4("u_model", item.model);
        item.mesh->draw();
        stats_.transparentDraws++;
    }
    Gl::DepthMask(GL_TRUE);

    // ---- Particles
    renderParticlesPass();

    // ---- Debug
    renderDebugPass();

    Gl::Disable(GL_BLEND);
}

void Renderer::setupGlobalUniforms(const Shader& sh, const Vec3& camPos,
                                   const Mat4& viewProj, bool clip) {
    sh.setVec3("u_camPos", camPos);
    sh.setFloat("u_time", time_);
    sh.setVec3("u_sunDir", sun_.direction.normalized());
    sh.setVec3("u_sunColor", sun_.color.rgb());
    sh.setFloat("u_sunIntensity", sun_.intensity);
    sh.setMat4("u_viewProj", viewProj);
    sh.setMat4("u_lightVP0", shadowVP_[0]);
    sh.setMat4("u_lightVP1", shadowVP_[1]);
    shadowFbo_[0].depthBind(0);
    sh.setInt("u_shadowMap0", 0);
    shadowFbo_[1].depthBind(1);
    sh.setInt("u_shadowMap1", 1);
    sh.setFloat("u_shadowEnabled", sun_.castShadow ? 1.0f : 0.0f);
    sh.setFloat("u_shadowTexel", 1.0f / (f32)shadowSize_);
    sh.setFloat("u_cascadeDist", cascadeDist_);
    sh.setFloat("u_aoEnabled", ssaoEnabled_ ? 1.0f : 0.0f);
    Gl::ActiveTexture(GL_TEXTURE2);
    Gl::BindTexture(GL_TEXTURE_2D, ssaoFbo_.color());
    sh.setInt("u_aoTex", 2);
    sh.setVec3("u_ambientSky", ambientSky_.rgb());
    sh.setVec3("u_ambientGround", ambientGround_.rgb());
    sh.setFloat("u_ambientIntensity", ambientIntensity_);
    sh.setFloat("u_fogDensity", fogDensity_);
    sh.setVec3("u_fogColor", fogColor_.rgb());
    sh.setInt("u_pointLightCount", (i32)pointLightCount_);
    for (u32 i = 0; i < pointLightCount_; i++) {
        char name[64];
        std::snprintf(name, sizeof(name), "u_pointLights[%u].position", i);
        sh.setVec3(name, pointLights_[i].position);
        std::snprintf(name, sizeof(name), "u_pointLights[%u].color", i);
        sh.setVec3(name, pointLights_[i].color.rgb());
        std::snprintf(name, sizeof(name), "u_pointLights[%u].intensity", i);
        sh.setFloat(name, pointLights_[i].intensity);
        std::snprintf(name, sizeof(name), "u_pointLights[%u].range", i);
        sh.setFloat(name, pointLights_[i].range);
    }
    sh.setVec4("u_clipPlane", Vec4(0, 1, 0, waterLevel_));
    sh.setFloat("u_clipEnabled", clip ? 1.0f : 0.0f);
}

void Renderer::applyMaterial(const Material& m, const Shader& sh) {
    sh.setVec3("u_baseColor", m.baseColor.rgb());
    sh.setFloat("u_metallic", m.metallic);
    sh.setFloat("u_roughness", m.roughness);
    sh.setFloat("u_ao", m.ambientOcclusion);
    sh.setVec3("u_emission", m.emission.rgb());
    sh.setFloat("u_emissionStrength", m.emissionStrength);
    sh.setFloat("u_normalStrength", m.normalStrength);
    sh.setFloat("u_unlit", m.unlit ? 1.0f : 0.0f);
    sh.setFloat("u_opacity", m.opacity);
    sh.setFloat("u_toon", 0.0f);

    sh.setInt("u_hasAlbedo", m.albedoMap ? 1 : 0);
    sh.setInt("u_hasNormal", m.normalMap ? 1 : 0);
    sh.setInt("u_hasMetal", m.metallicMap ? 1 : 0);
    sh.setInt("u_hasRough", m.roughnessMap ? 1 : 0);
    sh.setInt("u_hasAO", m.aoMap ? 1 : 0);

    // Material maps live in units 3..7 (0/1 = shadow cascades, 2 = SSAO)
    if (m.albedoMap) {
        Gl::ActiveTexture(GL_TEXTURE3);
        Gl::BindTexture(GL_TEXTURE_2D, m.albedoMap);
        sh.setInt("u_albedoMap", 3);
    } else {
        Gl::ActiveTexture(GL_TEXTURE3);
        Gl::BindTexture(GL_TEXTURE_2D, whiteTex_.handle());
        sh.setInt("u_albedoMap", 3);
    }
    if (m.normalMap) {
        Gl::ActiveTexture(GL_TEXTURE4);
        Gl::BindTexture(GL_TEXTURE_2D, m.normalMap);
        sh.setInt("u_normalMap", 4);
    }
    if (m.metallicMap) {
        Gl::ActiveTexture(GL_TEXTURE5);
        Gl::BindTexture(GL_TEXTURE_2D, m.metallicMap);
        sh.setInt("u_metalMap", 5);
    }
    if (m.roughnessMap) {
        Gl::ActiveTexture(GL_TEXTURE6);
        Gl::BindTexture(GL_TEXTURE_2D, m.roughnessMap);
        sh.setInt("u_roughMap", 6);
    }
    if (m.aoMap) {
        Gl::ActiveTexture(GL_TEXTURE7);
        Gl::BindTexture(GL_TEXTURE_2D, m.aoMap);
        sh.setInt("u_aoMap", 7);
    }

    if (m.doubleSided) {
        Gl::Disable(GL_CULL_FACE);
    } else {
        Gl::Enable(GL_CULL_FACE);
    }
}

void Renderer::renderWaterPass() {
    Gl::Enable(GL_DEPTH_TEST);
    Gl::DepthMask(GL_TRUE);
    Gl::Disable(GL_BLEND);
    Gl::Disable(GL_CULL_FACE);
    Gl::ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

    const f32 kWaterScale = 1500.0f;
    Mat4 waterModel = Mat4::translation(Vec3(camera_->position().x, waterLevel_,
                                             camera_->position().z)) *
                      Mat4::scaling(Vec3(kWaterScale, 1, kWaterScale));

    waterShader_.use();
    waterShader_.setMat4("u_model", waterModel);
    waterShader_.setMat4("u_viewProj", camera_->viewProj());
    waterShader_.setVec3("u_camPos", camera_->position());
    waterShader_.setVec3("u_sunDir", sun_.direction.normalized());
    waterShader_.setVec3("u_sunColor", sun_.color.rgb());
    waterShader_.setFloat("u_sunIntensity", sun_.intensity);
    waterShader_.setVec3("u_waterColor", waterColor_.rgb());
    waterShader_.setVec3("u_deepColor", deepWaterColor_.rgb());
    waterShader_.setFloat("u_time", time_);
    waterShader_.setFloat("u_waveAmplitude", waveAmplitude_);
    waterShader_.setFloat("u_fogDensity", fogDensity_);
    waterShader_.setVec3("u_fogColor", fogColor_.rgb());
    waterShader_.setFloat("u_waterLevel", waterLevel_);
    Gl::ActiveTexture(GL_TEXTURE0);
    Gl::BindTexture(GL_TEXTURE_2D, reflectionFbo_.color());
    waterShader_.setInt("u_reflection", 0);
    Gl::ActiveTexture(GL_TEXTURE1);
    Gl::BindTexture(GL_TEXTURE_2D, resolveFbo_.color());
    waterShader_.setInt("u_refraction", 1);
    waterShader_.setFloat("u_reflectionEnabled", reflectionEnabled_ ? 1.0f : 0.0f);
    waterPlane_.draw();

    // Blend pass
    Gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    Gl::DepthMask(GL_FALSE);
    Gl::Enable(GL_BLEND);
    Gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    waterShader_.use();
    waterShader_.setMat4("u_model", waterModel);
    waterShader_.setMat4("u_viewProj", camera_->viewProj());
    waterPlane_.draw();
    Gl::DepthMask(GL_TRUE);
    Gl::Enable(GL_CULL_FACE);
}

void Renderer::submitParticles(const Vector<Particle>& particles) {
    for (const auto& p : particles) {
        Vertex v;
        v.position = p.position;
        v.normal = p.color;
        v.tangent = Vec4(p.size, 0, 0, p.alpha);
        particleVerts_.pushBack(v);
    }
    particlesDirty_ = true;
    stats_.particleCount = (u32)particles.size();
}

void Renderer::renderParticlesPass() {
    if (particleVerts_.empty()) return;
    particleMesh_.upload(particleVerts_, Vector<u32>());
    Gl::DepthMask(GL_FALSE);
    Gl::Enable(GL_BLEND);
    Gl::BlendFunc(GL_SRC_ALPHA, GL_ONE);
    Gl::Disable(GL_CULL_FACE);

    particleShader_.use();
    particleShader_.setMat4("u_model", Mat4::identity());
    particleShader_.setMat4("u_viewProj", camera_->viewProj());
    particleShader_.setFloat("u_pointSize", 12.0f);
    particleShader_.setFloat("u_time", time_);
    particleMesh_.draw();
    Gl::Enable(GL_CULL_FACE);
    Gl::DepthMask(GL_TRUE);
}

void Renderer::drawLine(const Vec3& a, const Vec3& b, const Color& c) {
    Vertex va, vb;
    va.position = a;
    va.normal = c.rgb();
    vb.position = b;
    vb.normal = c.rgb();
    debugVerts_.pushBack(va);
    debugVerts_.pushBack(vb);
    debugLinesDirty_ = true;
}

void Renderer::drawBox(const Vec3& center, const Vec3& half, const Color& c) {
    Vec3 corners[8] = {
        center + Vec3(-half.x, -half.y, -half.z),
        center + Vec3(half.x, -half.y, -half.z),
        center + Vec3(half.x, -half.y, half.z),
        center + Vec3(-half.x, -half.y, half.z),
        center + Vec3(-half.x, half.y, -half.z),
        center + Vec3(half.x, half.y, -half.z),
        center + Vec3(half.x, half.y, half.z),
        center + Vec3(-half.x, half.y, half.z),
    };
    const i32 edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
    };
    for (i32 i = 0; i < 12; i++)
        drawLine(corners[edges[i][0]], corners[edges[i][1]], c);
}

void Renderer::drawSphere(const Vec3& center, f32 radius, const Color& c) {
    const u32 rings = 12, segs = 16;
    for (u32 r = 0; r < rings; r++) {
        f32 phi0 = Mathf::PI * (f32)r / (f32)rings;
        f32 phi1 = Mathf::PI * (f32)(r + 1) / (f32)rings;
        for (u32 s = 0; s < segs; s++) {
            f32 th0 = Mathf::TWO_PI * (f32)s / (f32)segs;
            f32 th1 = Mathf::TWO_PI * (f32)(s + 1) / (f32)segs;
            Vec3 a(std::sin(phi0) * std::cos(th0), std::cos(phi0), std::sin(phi0) * std::sin(th0));
            Vec3 b(std::sin(phi0) * std::cos(th1), std::cos(phi0), std::sin(phi0) * std::sin(th1));
            Vec3 c1(std::sin(phi1) * std::cos(th0), std::cos(phi1), std::sin(phi1) * std::sin(th0));
            Vec3 d(std::sin(phi1) * std::cos(th1), std::cos(phi1), std::sin(phi1) * std::sin(th1));
            drawLine(center + a * radius, center + b * radius, c);
            drawLine(center + a * radius, center + c1 * radius, c);
            drawLine(center + b * radius, center + d * radius, c);
        }
    }
}

void Renderer::flushDebug(const Camera& cam) {
    if (debugVerts_.empty()) return;
    debugMesh_.upload(debugVerts_, Vector<u32>());
    debugMesh_.setLineTopology(true);
    Gl::Disable(GL_DEPTH_TEST);
    debugShader_.use();
    debugShader_.setMat4("u_viewProj", cam.viewProj());
    debugShader_.setVec4("u_color", Vec4(1, 1, 1, 1));
    debugMesh_.drawLines();
    Gl::Enable(GL_DEPTH_TEST);
}

void Renderer::renderDebugPass() {
    if (debugVerts_.empty()) return;
    Gl::Disable(GL_DEPTH_TEST);
    debugMesh_.upload(debugVerts_, Vector<u32>());
    debugMesh_.setLineTopology(true);
    debugShader_.use();
    debugShader_.setMat4("u_viewProj", camera_->viewProj());
    debugShader_.setVec4("u_color", Vec4(1, 1, 1, 1));
    debugMesh_.drawLines();
    Gl::Enable(GL_DEPTH_TEST);
}

void Renderer::drawText(const char* text, f32 x, f32 y, f32 scale, const Color& color) {
    f32 cx = x;
    f32 cy = y;
    const f32 gw = 8.0f, gh = 8.0f;
    const f32 cw = gw * scale, ch = gh * scale;
    u8 colorBytes[4];
    colorBytes[0] = (u8)(color.r * 255);
    colorBytes[1] = (u8)(color.g * 255);
    colorBytes[2] = (u8)(color.b * 255);
    colorBytes[3] = (u8)(color.a * 255);
    for (const char* p = text; *p; p++) {
        char c = *p;
        if (c == '\n') { cx = x; cy += ch * 1.4f; continue; }
        i32 gi = Font::glyphIndex(c);
        i32 col = gi % 16;
        i32 row = gi / 16;
        f32 u0 = (f32)col * gw / 128.0f;
        f32 v0 = (f32)row * gh / 128.0f;
        f32 u1 = u0 + gw / 128.0f;
        f32 v1 = v0 + gh / 128.0f;
        Vertex tl, tr, br, bl;
        tl.position = Vec3(cx, cy, 0); tl.uv = Vec2(u0, v0);
        tr.position = Vec3(cx + cw, cy, 0); tr.uv = Vec2(u1, v0);
        br.position = Vec3(cx + cw, cy + ch, 0); br.uv = Vec2(u1, v1);
        bl.position = Vec3(cx, cy + ch, 0); bl.uv = Vec2(u0, v1);
        tl.normal = tr.normal = br.normal = bl.normal =
            Vec3(colorBytes[0] / 255.0f, colorBytes[1] / 255.0f, colorBytes[2] / 255.0f);
        tl.tangent = tr.tangent = br.tangent = bl.tangent = Vec4(0, 0, 0, color.a);
        u32 base = (u32)textVerts_.size();
        textVerts_.pushBack(tl); textVerts_.pushBack(tr);
        textVerts_.pushBack(br); textVerts_.pushBack(bl);
        textIndices_.pushBack(base); textIndices_.pushBack(base + 1); textIndices_.pushBack(base + 2);
        textIndices_.pushBack(base); textIndices_.pushBack(base + 2); textIndices_.pushBack(base + 3);
        cx += cw;
    }
}

void Renderer::drawRectFilled(f32 x, f32 y, f32 w, f32 h, const Color& color) {
    if (w <= 0.0f || h <= 0.0f) return;
    u8 cb[4];
    cb[0] = (u8)(color.r * 255);
    cb[1] = (u8)(color.g * 255);
    cb[2] = (u8)(color.b * 255);
    cb[3] = (u8)(color.a * 255);
    Vec3 col(cb[0] / 255.0f, cb[1] / 255.0f, cb[2] / 255.0f);
    Vertex tl, tr, br, bl;
    tl.position = Vec3(x, y, 0); tl.normal = col;
    tr.position = Vec3(x + w, y, 0); tr.normal = col;
    br.position = Vec3(x + w, y + h, 0); br.normal = col;
    bl.position = Vec3(x, y + h, 0); bl.normal = col;
    Vec4 alpha(0, 0, 0, color.a);
    tl.tangent = tr.tangent = br.tangent = bl.tangent = alpha;
    u32 base = (u32)hudVerts_.size();
    hudVerts_.pushBack(tl); hudVerts_.pushBack(tr);
    hudVerts_.pushBack(br); hudVerts_.pushBack(bl);
    hudIndices_.pushBack(base); hudIndices_.pushBack(base + 1); hudIndices_.pushBack(base + 2);
    hudIndices_.pushBack(base); hudIndices_.pushBack(base + 2); hudIndices_.pushBack(base + 3);
}

void Renderer::flushHud() {
    if (hudVerts_.empty()) return;
    hudRectMesh_.upload(hudVerts_, hudIndices_);
    hudIndices_.clear();
    Gl::Disable(GL_DEPTH_TEST);
    Gl::Enable(GL_BLEND);
    Gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    Mat4 proj;
    proj.m[0] = 2.0f / (f32)width_;
    proj.m[5] = -2.0f / (f32)height_;
    proj.m[10] = -1.0f;
    proj.m[15] = 1.0f;
    hudRectShader_.use();
    hudRectShader_.setMat4("u_proj", proj);
    hudRectShader_.setVec4("u_color", Vec4(1, 1, 1, 1));
    hudRectMesh_.draw();
    Gl::Disable(GL_BLEND);
    Gl::Enable(GL_DEPTH_TEST);
}

void Renderer::flushText() {
    if (textVerts_.empty()) return;
    textMesh_.upload(textVerts_, textIndices_);
    textIndices_.clear();
    Gl::Disable(GL_DEPTH_TEST);
    Gl::Enable(GL_BLEND);
    Gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    Mat4 proj;
    proj.m[0] = 2.0f / (f32)width_;
    proj.m[5] = -2.0f / (f32)height_;
    proj.m[10] = -1.0f;
    proj.m[15] = 1.0f;
    textShader_.use();
    textShader_.setMat4("u_proj", proj);
    fontTex_.bind(0);
    textShader_.setInt("u_font", 0);
    textShader_.setVec4("u_color", Vec4(1, 1, 1, 1));
    textMesh_.draw();
    Gl::Disable(GL_BLEND);
    Gl::Enable(GL_DEPTH_TEST);
}

void Renderer::renderPostPass() {
    // 1) Resolve MSAA main target -> resolveFbo_
    Gl::BindFramebuffer(GL_READ_FRAMEBUFFER, mainFbo_.handle());
    Gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo_.handle());
    Gl::BlitFramebuffer(0, 0, (GLint)width_, (GLint)height_, 0, 0,
                        (GLint)width_, (GLint)height_, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    Gl::BindFramebuffer(GL_FRAMEBUFFER, 0);

    // 2) Bloom: horizontal then vertical blur of the resolved image
    blurFbo_[0].bind();
    Gl::Viewport(0, 0, (GLsizei)width_, (GLsizei)height_);
    blurShader_.use();
    Gl::ActiveTexture(GL_TEXTURE0);
    Gl::BindTexture(GL_TEXTURE_2D, resolveFbo_.color());
    blurShader_.setInt("u_texture", 0);
    blurShader_.setVec2("u_direction", Vec2(1, 0));
    blurShader_.setFloat("u_radius", 4.0f);
    fullscreenTri_.draw();

    blurFbo_[1].bind();
    Gl::ActiveTexture(GL_TEXTURE0);
    Gl::BindTexture(GL_TEXTURE_2D, blurFbo_[0].color());
    blurShader_.setInt("u_texture", 0);
    blurShader_.setVec2("u_direction", Vec2(0, 1));
    blurShader_.setFloat("u_radius", 4.0f);
    fullscreenTri_.draw();

    // 3) God rays
    godrayFbo_.bind();
    godrayShader_.use();
    Gl::ActiveTexture(GL_TEXTURE0);
    Gl::BindTexture(GL_TEXTURE_2D, resolveFbo_.color());
    godrayShader_.setInt("u_scene", 0);
    godrayShader_.setVec2("u_sunScreen", sunScreen_);
    f32 godStrength = 0.0f;
    if (godRaysEnabled_ && sunScreen_.x > -1.0f && nightFactor_ < 0.6f)
        godStrength = 0.55f * (1.0f - nightFactor_);
    godrayShader_.setFloat("u_strength", godStrength);
    fullscreenTri_.draw();

    // 4) Composite to screen (window's default framebuffer)
    Gl::Viewport(0, 0, (GLsizei)width_, (GLsizei)height_);
    compositeShader_.use();
    Gl::ActiveTexture(GL_TEXTURE0);
    Gl::BindTexture(GL_TEXTURE_2D, resolveFbo_.color());
    compositeShader_.setInt("u_scene", 0);
    Gl::ActiveTexture(GL_TEXTURE1);
    Gl::BindTexture(GL_TEXTURE_2D, blurFbo_[1].color());
    compositeShader_.setInt("u_bloom", 1);
    Gl::ActiveTexture(GL_TEXTURE2);
    Gl::BindTexture(GL_TEXTURE_2D, godrayFbo_.color());
    compositeShader_.setInt("u_godRays", 2);
    compositeShader_.setFloat("u_bloomStrength", bloomStrength_);
    compositeShader_.setFloat("u_godRayStrength", godStrength);
    compositeShader_.setFloat("u_exposure", exposure_);
    compositeShader_.setFloat("u_gamma", 2.2f);
    compositeShader_.setFloat("u_vignette", vignette_);
    compositeShader_.setFloat("u_time", time_);
    compositeShader_.setVec2("u_resolution", Vec2((f32)width_, (f32)height_));
    fullscreenTri_.draw();
    Gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
}

}
