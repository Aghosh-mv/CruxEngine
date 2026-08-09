// ============================================================================
// FrostEngine MegaLights System - Implementation
// ============================================================================

#include "FrostEngine/Renderer/MegaLights.h"
#include "FrostEngine/Renderer/Camera.h"
#include "FrostEngine/Core/Math.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace Frost {

// ============================================================================
// Constructor / Destructor
// ============================================================================
MegaLights::MegaLights() = default;

MegaLights::~MegaLights() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================
bool MegaLights::init(u32 screenWidth, u32 screenHeight) {
    if (initialized_) return true;

    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;

    // Initialize tile grid
    tileCountX_ = (screenWidth + MEGALIGHTS_TILE_SIZE - 1) / MEGALIGHTS_TILE_SIZE;
    tileCountY_ = (screenHeight + MEGALIGHTS_TILE_SIZE - 1) / MEGALIGHTS_TILE_SIZE;
    initializeTileGrid();

    // Initialize cluster grid
    initializeClusterGrid();

    // Initialize shadow atlas
    shadowAtlas_.resolution = MEGALIGHTS_SHADOW_ATLAS_SIZE;
    shadowAtlas_.pageCount = 0;
    shadowAtlas_.dirtyPageCount = 0;
    shadowAtlas_.usedMemoryBytes = 0;

    // Initialize cookies
    cookies_.resize(MEGALIGHTS_MAX_COOKIES);
    cookieCount_ = 0;

    // Initialize temporal buffers
    u32 pixelCount = screenWidth * screenHeight;
    historyShadow_.resize(pixelCount);
    currentShadow_.resize(pixelCount);
    motionVectors_.resize(pixelCount);

    // Initialize directional light
    directionalLight_.type = MegaLightType::Directional;
    directionalLight_.direction = Vec3(0.5f, -0.8f, -0.3f).normalized();
    directionalLight_.color = Vec3(1.0f, 0.96f, 0.9f);
    directionalLight_.intensity = 3.0f;
    directionalLight_.castShadow = true;
    directionalLight_.shadowType = ShadowType::Virtual;
    hasDirectionalLight_ = true;

    initialized_ = true;
    return true;
}

void MegaLights::shutdown() {
    if (!initialized_) return;

    allLights_.clear();
    activeLights_.clear();
    tileGrid_.clear();
    clusterGrid_.clear();
    cookies_.clear();
    historyShadow_.clear();
    currentShadow_.clear();
    motionVectors_.clear();

    initialized_ = false;
}

void MegaLights::resize(u32 screenWidth, u32 screenHeight) {
    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;
    tileCountX_ = (screenWidth + MEGALIGHTS_TILE_SIZE - 1) / MEGALIGHTS_TILE_SIZE;
    tileCountY_ = (screenHeight + MEGALIGHTS_TILE_SIZE - 1) / MEGALIGHTS_TILE_SIZE;
    initializeTileGrid();
    initializeClusterGrid();

    u32 pixelCount = screenWidth * screenHeight;
    historyShadow_.resize(pixelCount);
    currentShadow_.resize(pixelCount);
    motionVectors_.resize(pixelCount);
}

// ============================================================================
// Grid Initialization
// ============================================================================
void MegaLights::initializeTileGrid() {
    u32 tileCount = tileCountX_ * tileCountY_;
    tileGrid_.resize(tileCount);
    clearTileLightLists();
}

void MegaLights::initializeClusterGrid() {
    u32 clusterCount = MEGALIGHTS_CLUSTER_X * MEGALIGHTS_CLUSTER_Y * MEGALIGHTS_CLUSTER_Z;
    clusterGrid_.resize(clusterCount);
    clearClusterLightLists();
}

void MegaLights::clearTileLightLists() {
    for (auto& tile : tileGrid_) {
        tile.lightCount = 0;
    }
}

void MegaLights::clearClusterLightLists() {
    for (auto& cluster : clusterGrid_) {
        cluster.lightCount = 0;
    }
}

// ============================================================================
// Frame Lifecycle
// ============================================================================
void MegaLights::beginFrame(const Camera& camera, f32 deltaTime) {
    deltaTime_ = deltaTime;
    frameIndex_++;

    cameraPosition_ = camera.position();
    cameraDirection_ = camera.forward();
    viewMatrix_ = camera.view();
    projMatrix_ = camera.proj();
    viewProjMatrix_ = camera.viewProj();
    nearPlane_ = camera.nearPlane();
    farPlane_ = camera.farPlane();

    stats_.totalLights = static_cast<u32>(allLights_.size());
    stats_.activeLights = static_cast<u32>(activeLights_.size());
    stats_.shadowCastingLights = 0;
    stats_.tilesUpdated = 0;
    stats_.clustersUpdated = 0;
    stats_.shadowPagesRendered = 0;
    stats_.cookieBindings = 0;
}

void MegaLights::classifyLights() {
    activeLights_.clear();

    for (auto& light : allLights_) {
        if (!light.enabled) continue;

        classifyLightType(light);
        computeImportanceScore(light);

        activeLights_.push_back(&light);

        if (light.castShadow) stats_.shadowCastingLights++;
    }

    sortLightsByImportance();
}

void MegaLights::classifyLightType(MegaLight& light) {
    // Classification is already done via the type field
    // Here we compute additional metadata
    switch (light.type) {
    case MegaLightType::Point:
        light.tileMask = 0;
        break;
    case MegaLightType::Spot:
        light.tileMask = 0;
        break;
    case MegaLightType::AreaRect:
    case MegaLightType::AreaDisk:
    case MegaLightType::AreaTube:
        light.tileMask = 0;
        break;
    default:
        break;
    }
}

void MegaLights::computeImportanceScore(MegaLight& light) {
    // Compute importance based on intensity, distance to camera, and screen coverage
    f32 dist = (light.position - cameraPosition_).length();
    f32 attenuation = light.attenuation(dist);
    f32 screenCoverage = computeTileCoverage(light,
        (u32)(screenWidth_ * 0.5f / MEGALIGHTS_TILE_SIZE),
        (u32)(screenHeight_ * 0.5f / MEGALIGHTS_TILE_SIZE));

    light.importanceScore = light.intensity * attenuation * (1.0f + screenCoverage);
}

void MegaLights::sortLightsByImportance() {
    // Sort active lights by importance (descending)
    for (u32 i = 1; i < activeLights_.size(); i++) {
        MegaLight* key = activeLights_[i];
        u32 j = i - 1;
        while (j < activeLights_.size() && activeLights_[j]->importanceScore < key->importanceScore) {
            activeLights_[j + 1] = activeLights_[j];
            if (j == 0) break;
            j--;
        }
        activeLights_[j + (j < activeLights_.size() ? 1 : 0)] = key;
    }
}

void MegaLights::cullLights() {
    u32 culledCount = 0;

    for (auto* light : activeLights_) {
        if (cullingCfg_.frustumCull) {
            // Frustum cull light sphere
            Vec4 clip = viewProjMatrix_ * Vec4(light->position, 1.0f);
            if (clip.w <= 0.0f) {
                light->enabled = false;
                culledCount++;
                continue;
            }
        }

        if (cullingCfg_.distanceCull) {
            f32 dist = (light->position - cameraPosition_).length();
            if (dist > light->range && light->type != MegaLightType::Directional) {
                light->enabled = false;
                culledCount++;
                continue;
            }
        }
    }
}

void MegaLights::assignLightsToTiles() {
    clearTileLightLists();

    for (auto* light : activeLights_) {
        if (!light->enabled) continue;

        for (u32 ty = 0; ty < tileCountY_; ty++) {
            for (u32 tx = 0; tx < tileCountX_; tx++) {
                if (lightIntersectsTile(*light, tx, ty)) {
                    u32 tileIdx = ty * tileCountX_ + tx;
                    TileLightList& tile = tileGrid_[tileIdx];
                    if (tile.lightCount < MEGALIGHTS_MAX_LIGHTS_PER_TILE) {
                        // Find index in allLights_
                        u32 lightIdx = 0;
                        for (u32 l = 0; l < allLights_.size(); l++) {
                            if (&allLights_[l] == light) {
                                lightIdx = l;
                                break;
                            }
                        }
                        tile.lightIndices[tile.lightCount++] = lightIdx;
                    }
                }
            }
        }
    }

    // Count updated tiles
    for (auto& tile : tileGrid_) {
        if (tile.lightCount > 0) stats_.tilesUpdated++;
    }
}

void MegaLights::assignLightsToClusters() {
    clearClusterLightLists();

    for (auto* light : activeLights_) {
        if (!light->enabled) continue;

        for (u32 cz = 0; cz < MEGALIGHTS_CLUSTER_Z; cz++) {
            for (u32 cy = 0; cy < MEGALIGHTS_CLUSTER_Y; cy++) {
                for (u32 cx = 0; cx < MEGALIGHTS_CLUSTER_X; cx++) {
                    if (lightIntersectsCluster(*light, cx, cy, cz)) {
                        u32 clusterIdx = cz * MEGALIGHTS_CLUSTER_Y * MEGALIGHTS_CLUSTER_X +
                                        cy * MEGALIGHTS_CLUSTER_X + cx;
                        ClusterLightList& cluster = clusterGrid_[clusterIdx];
                        if (cluster.lightCount < MEGALIGHTS_MAX_LIGHTS_PER_TILE) {
                            u32 lightIdx = 0;
                            for (u32 l = 0; l < allLights_.size(); l++) {
                                if (&allLights_[l] == light) {
                                    lightIdx = l;
                                    break;
                                }
                            }
                            cluster.lightIndices[cluster.lightCount++] = lightIdx;
                        }
                    }
                }
            }
        }
    }

    stats_.clustersUpdated = static_cast<u32>(clusterGrid_.size());
}

void MegaLights::renderShadows() {
    for (auto* light : activeLights_) {
        if (!light->enabled || !light->castShadow) continue;

        switch (light->type) {
        case MegaLightType::Directional:
            renderDirectionalShadow(*light);
            break;
        case MegaLightType::Point:
            for (u32 face = 0; face < 6; face++) {
                u32 pageIdx = allocateShadowPage(
                    static_cast<u32>(&*light - allLights_.data()),
                    light->shadowResolution
                );
                if (pageIdx != UINT32_MAX) {
                    renderPointShadow(*light, pageIdx);
                }
            }
            break;
        case MegaLightType::Spot: {
            u32 pageIdx = allocateShadowPage(
                static_cast<u32>(&*light - allLights_.data()),
                light->shadowResolution
            );
            if (pageIdx != UINT32_MAX) {
                renderSpotShadow(*light, pageIdx);
            }
            break;
        }
        case MegaLightType::AreaRect:
        case MegaLightType::AreaDisk:
        case MegaLightType::AreaTube: {
            u32 pageIdx = allocateShadowPage(
                static_cast<u32>(&*light - allLights_.data()),
                light->shadowResolution
            );
            if (pageIdx != UINT32_MAX) {
                renderAreaShadow(*light, pageIdx);
            }
            break;
        }
        default:
            break;
        }
    }

    updateShadowAtlas();
}

void MegaLights::applyLighting() {
    // In a real implementation, this would dispatch compute shaders
    // to evaluate lighting for each pixel using the tile/cluster data
}

void MegaLights::denoiseShadows() {
    if (stochasticCfg_.enabled && stochasticCfg_.temporalAccumulation) {
        temporalAccumulate();
    }
    if (stochasticCfg_.denoiseRadius > 0) {
        spatialDenoise();
    }
}

void MegaLights::endFrame() {
    prevViewProjMatrix_ = viewProjMatrix_;
}

// ============================================================================
// Light Management
// ============================================================================
u64 MegaLights::addLight(const MegaLight& light) {
    u64 id = nextLightId_++;
    MegaLight newLight = light;
    newLight.id = id;
    allLights_.push_back(newLight);
    return id;
}

void MegaLights::removeLight(u64 lightId) {
    for (u32 i = 0; i < allLights_.size(); i++) {
        if (allLights_[i].id == lightId) {
            allLights_.eraseSwap(i);
            return;
        }
    }
}

void MegaLights::updateLight(u64 lightId, const MegaLight& light) {
    for (auto& l : allLights_) {
        if (l.id == lightId) {
            l = light;
            return;
        }
    }
}

MegaLight* MegaLights::getLight(u64 lightId) {
    for (auto& l : allLights_) {
        if (l.id == lightId) return &l;
    }
    return nullptr;
}

const MegaLight* MegaLights::getLight(u64 lightId) const {
    for (auto& l : allLights_) {
        if (l.id == lightId) return &l;
    }
    return nullptr;
}

void MegaLights::setDirectionalLight(const Vec3& direction, const Vec3& color, f32 intensity) {
    directionalLight_.direction = direction.normalized();
    directionalLight_.color = color;
    directionalLight_.intensity = intensity;
    hasDirectionalLight_ = true;
}

// ============================================================================
// Shadow Atlas
// ============================================================================
u32 MegaLights::allocateShadowPage(u32 lightIndex, u32 resolution) {
    u32 slot = findFreeAtlasSlot(resolution);
    if (slot == UINT32_MAX) return UINT32_MAX;

    ShadowAtlasPage page;
    page.pageIndex = slot;
    page.lightIndex = lightIndex;
    page.resolution = resolution;
    page.dirty = true;
    page.frameLastRendered = 0;

    // Compute atlas position
    f32 sizeNorm = (f32)resolution / (f32)shadowAtlas_.resolution;
    u32 pagesPerRow = shadowAtlas_.resolution / resolution;
    page.x = (f32)(slot % pagesPerRow) * sizeNorm;
    page.y = (f32)(slot / pagesPerRow) * sizeNorm;
    page.size = sizeNorm;

    shadowAtlas_.pages.push_back(page);
    shadowAtlas_.pageCount++;
    shadowAtlas_.usedMemoryBytes += resolution * resolution * 4; // 4 bytes per texel

    return slot;
}

void MegaLights::deallocateShadowPage(u32 pageIndex) {
    for (u32 i = 0; i < shadowAtlas_.pages.size(); i++) {
        if (shadowAtlas_.pages[i].pageIndex == pageIndex) {
            shadowAtlas_.pages.eraseSwap(i);
            shadowAtlas_.pageCount--;
            return;
        }
    }
}

void MegaLights::renderShadowPage(u32 pageIndex) {
    for (auto& page : shadowAtlas_.pages) {
        if (page.pageIndex == pageIndex) {
            page.dirty = false;
            page.frameLastRendered = frameIndex_;
            stats_.shadowPagesRendered++;
            return;
        }
    }
}

void MegaLights::updateShadowAtlas() {
    compactAtlas();
}

void MegaLights::renderDirectionalShadow(const MegaLight& light) {
    // Directional light uses cascaded shadow maps
    u32 pageIdx = allocateShadowPage(0, light.shadowResolution);
    if (pageIdx != UINT32_MAX) {
        renderShadowPage(pageIdx);
    }
}

void MegaLights::renderPointShadow(const MegaLight& light, u32 pageIndex) {
    renderShadowPage(pageIndex);
}

void MegaLights::renderSpotShadow(const MegaLight& light, u32 pageIndex) {
    renderShadowPage(pageIndex);
}

void MegaLights::renderAreaShadow(const MegaLight& light, u32 pageIndex) {
    renderShadowPage(pageIndex);
}

void MegaLights::compactAtlas() {
    // Remove pages that haven't been rendered recently
    for (u32 i = shadowAtlas_.pages.size(); i > 0; i--) {
        u32 idx = i - 1;
        u32 framesSinceRender = frameIndex_ - shadowAtlas_.pages[idx].frameLastRendered;
        if (framesSinceRender > 600) {
            shadowAtlas_.pages.eraseSwap(idx);
            shadowAtlas_.pageCount--;
        }
    }
}

u32 MegaLights::findFreeAtlasSlot(u32 resolution) const {
    u32 pagesPerRow = shadowAtlas_.resolution / resolution;
    u32 maxPages = pagesPerRow * pagesPerRow;

    for (u32 i = 0; i < maxPages; i++) {
        bool used = false;
        for (auto& page : shadowAtlas_.pages) {
            if (page.pageIndex == i && page.resolution == resolution) {
                used = true;
                break;
            }
        }
        if (!used) return i;
    }

    return UINT32_MAX;
}

void MegaLights::computeAtlasUV(const ShadowAtlasPage& page, f32& u0, f32& v0,
                                  f32& u1, f32& v1) const {
    u0 = page.x;
    v0 = page.y;
    u1 = page.x + page.size;
    v1 = page.y + page.size;
}

// ============================================================================
// Cookies
// ============================================================================
u32 MegaLights::addCookie(u32 textureHandle, const Vec2& scale, const Vec2& offset) {
    if (cookieCount_ >= MEGALIGHTS_MAX_COOKIES) return UINT32_MAX;

    u32 idx = cookieCount_++;
    LightCookie& cookie = cookies_[idx];
    cookie.textureHandle = textureHandle;
    cookie.scale = scale;
    cookie.offset = offset;
    cookie.rotation = 0.0f;
    cookie.intensity = 1.0f;
    cookie.isProjected = true;

    return idx;
}

void MegaLights::removeCookie(u32 cookieIndex) {
    if (cookieIndex < cookieCount_) {
        cookies_[cookieIndex].textureHandle = 0;
    }
}

void MegaLights::bindCookie(u32 lightIndex, u32 cookieIndex) {
    if (lightIndex < allLights_.size() && cookieIndex < cookieCount_) {
        allLights_[lightIndex].cookieIndex = cookieIndex;
        allLights_[lightIndex].hasCookie = true;
        allLights_[lightIndex].cookieType = CookieType::Texture;
        stats_.cookieBindings++;
    }
}

// ============================================================================
// Tile & Cluster Operations
// ============================================================================
void MegaLights::computeTileBounds(u32 tileX, u32 tileY, Vec3& boundsMin, Vec3& boundsMax) const {
    f32 tileMinX = (f32)(tileX * MEGALIGHTS_TILE_SIZE) / (f32)screenWidth_;
    f32 tileMinY = (f32)(tileY * MEGALIGHTS_TILE_SIZE) / (f32)screenHeight_;
    f32 tileMaxX = (f32)((tileX + 1) * MEGALIGHTS_TILE_SIZE) / (f32)screenWidth_;
    f32 tileMaxY = (f32)((tileY + 1) * MEGALIGHTS_TILE_SIZE) / (f32)screenHeight_;

    // Convert to world space at near plane
    f32 nearHeight = std::tan(nearPlane_ * 0.5f) * 2.0f;
    f32 nearWidth = nearHeight * (f32)screenWidth_ / (f32)screenHeight_;

    Vec3 nearMin(-nearWidth * 0.5f, -nearHeight * 0.5f, nearPlane_);
    Vec3 nearMax(nearWidth * 0.5f, nearHeight * 0.5f, nearPlane_);

    boundsMin = nearMin + Vec3(nearWidth * tileMinX, nearHeight * tileMinY, 0);
    boundsMax = nearMin + Vec3(nearWidth * tileMaxX, nearHeight * tileMaxY, 0);
}

bool MegaLights::lightIntersectsTile(const MegaLight& light, u32 tileX, u32 tileY) const {
    if (light.type == MegaLightType::Directional) return true;

    Vec3 tileMin, tileMax;
    computeTileBounds(tileX, tileY, tileMin, tileMax);

    // Project light position to screen space
    Vec4 clip = viewProjMatrix_ * Vec4(light.position, 1.0f);
    if (clip.w <= 0.0f) return false;

    f32 lightScreenX = (clip.x / clip.w * 0.5f + 0.5f) * (f32)screenWidth_;
    f32 lightScreenY = (1.0f - clip.y / clip.w * 0.5f - 0.5f) * (f32)screenHeight_;

    f32 tileMinX = (f32)(tileX * MEGALIGHTS_TILE_SIZE);
    f32 tileMinY = (f32)(tileY * MEGALIGHTS_TILE_SIZE);
    f32 tileMaxX = (f32)((tileX + 1) * MEGALIGHTS_TILE_SIZE);
    f32 tileMaxY = (f32)((tileY + 1) * MEGALIGHTS_TILE_SIZE);

    // Simple AABB test in screen space
    f32 lightRadius = light.range * 100.0f / (clip.w + 1.0f); // Approximate screen radius
    return lightScreenX + lightRadius > tileMinX &&
           lightScreenX - lightRadius < tileMaxX &&
           lightScreenY + lightRadius > tileMinY &&
           lightScreenY - lightRadius < tileMaxY;
}

void MegaLights::computeClusterBounds(u32 x, u32 y, u32 z, Vec3& boundsMin, Vec3& boundsMax) const {
    f32 clusterSizeX = 2.0f / MEGALIGHTS_CLUSTER_X;
    f32 clusterSizeY = 2.0f / MEGALIGHTS_CLUSTER_Y;

    f32 ndcMinX = -1.0f + (f32)x * clusterSizeX;
    f32 ndcMaxX = ndcMinX + clusterSizeX;
    f32 ndcMinY = -1.0f + (f32)y * clusterSizeY;
    f32 ndcMaxY = ndcMinY + clusterSizeY;

    f32 linearDepthMin = nearPlane_ * std::pow(farPlane_ / nearPlane_, (f32)z / MEGALIGHTS_CLUSTER_Z);
    f32 linearDepthMax = nearPlane_ * std::pow(farPlane_ / nearPlane_, (f32)(z + 1) / MEGALIGHTS_CLUSTER_Z);

    boundsMin = Vec3(ndcMinX, ndcMinY, linearDepthMin);
    boundsMax = Vec3(ndcMaxX, ndcMaxY, linearDepthMax);
}

bool MegaLights::lightIntersectsCluster(const MegaLight& light, u32 x, u32 y, u32 z) const {
    if (light.type == MegaLightType::Directional) return true;

    Vec3 clusterMin, clusterMax;
    computeClusterBounds(x, y, z, clusterMin, clusterMax);

    // Test light sphere against cluster frustum
    f32 dist = (light.position - cameraPosition_).length();
    return dist < light.range * 1.5f;
}

// ============================================================================
// Stochastic Sampling
// ============================================================================
u32 MegaLights::selectLightForSample(const Vec2& screenUV, u32 sampleIndex) const {
    if (activeLights_.empty()) return UINT32_MAX;

    // Weighted random selection based on importance
    f32 totalWeight = 0.0f;
    for (auto* light : activeLights_) {
        if (!light->enabled) continue;
        totalWeight += computeLightProbability(*light, screenUV);
    }

    if (totalWeight <= 0.0f) return 0;

    f32 random = (f32)((sampleIndex * 7919 + frameIndex_ * 104729) % 10000) / 10000.0f;
    f32 cumulative = 0.0f;

    for (u32 i = 0; i < activeLights_.size(); i++) {
        if (!activeLights_[i]->enabled) continue;
        cumulative += computeLightProbability(*activeLights_[i], screenUV) / totalWeight;
        if (random <= cumulative) return i;
    }

    return 0;
}

f32 MegaLights::computeLightProbability(const MegaLight& light, const Vec2& screenUV) const {
    f32 dist = (light.position - cameraPosition_).length();
    f32 atten = light.attenuation(dist);
    return light.intensity * atten * light.importanceScore;
}

// ============================================================================
// Query
// ============================================================================
const TileLightList& MegaLights::getTileLights(u32 tileX, u32 tileY) const {
    u32 idx = tileY * tileCountX_ + tileX;
    return tileGrid_[idx];
}

const ClusterLightList& MegaLights::getClusterLights(u32 x, u32 y, u32 z) const {
    u32 idx = z * MEGALIGHTS_CLUSTER_Y * MEGALIGHTS_CLUSTER_X +
              y * MEGALIGHTS_CLUSTER_X + x;
    return clusterGrid_[idx];
}

f32 MegaLights::getLightAtPosition(const Vec3& worldPos, u32& lightCount) const {
    Vec3 totalLight = Vec3(0);
    lightCount = 0;

    // Sun
    if (hasDirectionalLight_) {
        Vec3 L = -directionalLight_.direction;
        totalLight = totalLight + directionalLight_.color * directionalLight_.intensity;
        lightCount++;
    }

    // All other lights
    for (auto& light : allLights_) {
        if (!light.enabled) continue;

        f32 dist = (light.position - worldPos).length();
        if (dist > light.range) continue;

        f32 atten = light.attenuation(dist);
        totalLight = totalLight + light.color * light.intensity * atten;
        lightCount++;
    }

    return (totalLight.x + totalLight.y + totalLight.z) / 3.0f;
}

f32 MegaLights::computeLightInfluence(const MegaLight& light, const Vec2& screenUV,
                                       f32 sceneDepth) const {
    Vec3 worldPos;
    f32 depth;
    screenToWorld(screenUV, sceneDepth, worldPos);

    f32 dist = (light.position - worldPos).length();
    if (dist > light.range) return 0.0f;

    return light.attenuation(dist) * light.intensity;
}

f32 MegaLights::computeTileCoverage(const MegaLight& light, u32 tileX, u32 tileY) const {
    Vec3 tileMin, tileMax;
    computeTileBounds(tileX, tileY, tileMin, tileMax);

    if (light.type == MegaLightType::Directional) return 1.0f;

    // Project light and compute coverage
    Vec4 clip = viewProjMatrix_ * Vec4(light.position, 1.0f);
    if (clip.w <= 0.0f) return 0.0f;

    f32 lightScreenX = (clip.x / clip.w * 0.5f + 0.5f) * (f32)screenWidth_;
    f32 lightScreenY = (1.0f - clip.y / clip.w * 0.5f - 0.5f) * (f32)screenHeight_;
    f32 screenRadius = light.range * 100.0f / (clip.w + 1.0f);

    f32 tileCenterX = ((f32)tileX + 0.5f) * MEGALIGHTS_TILE_SIZE;
    f32 tileCenterY = ((f32)tileY + 0.5f) * MEGALIGHTS_TILE_SIZE;

    f32 dx = lightScreenX - tileCenterX;
    f32 dy = lightScreenY - tileCenterY;
    f32 dist = std::sqrt(dx * dx + dy * dy);

    if (dist > screenRadius + MEGALIGHTS_TILE_SIZE) return 0.0f;
    if (dist < screenRadius) return 1.0f;

    return 1.0f - (dist - screenRadius) / MEGALIGHTS_TILE_SIZE;
}

// ============================================================================
// Screen-space Helpers
// ============================================================================
void MegaLights::screenToWorld(const Vec2& screenUV, f32 depth, Vec3& worldPos) const {
    f32 ndcX = screenUV.x * 2.0f - 1.0f;
    f32 ndcY = 1.0f - screenUV.y * 2.0f;

    Vec4 clip = Vec4(ndcX, ndcY, depth, 1.0f);
    Vec4 world = viewProjMatrix_ * clip;
    worldPos = Vec3(world.x, world.y, world.z) / world.w;
}

void MegaLights::worldToScreen(const Vec3& worldPos, Vec2& screenUV, f32& depth) const {
    Vec4 clip = viewProjMatrix_ * Vec4(worldPos, 1.0f);
    if (clip.w <= 0.0f) {
        screenUV = Vec2(-1, -1);
        depth = 0;
        return;
    }
    screenUV.x = (clip.x / clip.w * 0.5f + 0.5f);
    screenUV.y = (1.0f - clip.y / clip.w * 0.5f - 0.5f);
    depth = clip.z / clip.w;
}

// ============================================================================
// Denoising
// ============================================================================
void MegaLights::spatialDenoise() {
    u32 width = screenWidth_;
    u32 height = screenHeight_;
    u32 pixelCount = width * height;

    if (currentShadow_.size() != pixelCount) return;

    // Simple box blur
    f32 radius = stochasticCfg_.denoiseRadius;
    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            f32 sum = 0.0f;
            f32 weightSum = 0.0f;

            i32 r = (i32)std::ceil(radius);
            for (i32 ky = -r; ky <= r; ky++) {
                for (i32 kx = -r; kx <= r; kx++) {
                    i32 sx = Mathf::max(0, Mathf::min((i32)x + kx, (i32)width - 1));
                    i32 sy = Mathf::max(0, Mathf::min((i32)y + ky, (i32)height - 1));
                    u32 idx = sy * width + sx;

                    f32 w = 1.0f;
                    sum += currentShadow_[idx] * w;
                    weightSum += w;
                }
            }

            currentShadow_[y * width + x] = sum / (weightSum + 0.001f);
        }
    }
}

void MegaLights::temporalAccumulate() {
    u32 pixelCount = screenWidth_ * screenHeight_;
    if (currentShadow_.size() != pixelCount || historyShadow_.size() != pixelCount) return;

    for (u32 i = 0; i < pixelCount; i++) {
        currentShadow_[i] = historyShadow_[i] * stochasticCfg_.temporalBlendWeight +
                           currentShadow_[i] * (1.0f - stochasticCfg_.temporalBlendWeight);
        historyShadow_[i] = currentShadow_[i];
    }
}

void MegaLights::computeMotionVectors() {
    u32 pixelCount = screenWidth_ * screenHeight_;
    if (motionVectors_.size() != pixelCount) return;

    // Simplified motion vector computation
    for (u32 y = 0; y < screenHeight_; y++) {
        for (u32 x = 0; x < screenWidth_; x++) {
            Vec2 screenUV((f32)x / screenWidth_, (f32)y / screenHeight_);
            Vec3 worldPos;
            f32 depth;
            screenToWorld(screenUV, 0.5f, worldPos);

            // Project to previous frame
            Vec4 prevClip = prevViewProjMatrix_ * Vec4(worldPos, 1.0f);
            if (prevClip.w > 0) {
                f32 prevU = prevClip.x / prevClip.w * 0.5f + 0.5f;
                f32 prevV = 1.0f - prevClip.y / prevClip.w * 0.5f - 0.5f;
                motionVectors_[y * screenWidth_ + x] = Vec3(
                    prevU - screenUV.x,
                    prevV - screenUV.y,
                    0.0f
                );
            }
        }
    }
}

} // namespace Frost
