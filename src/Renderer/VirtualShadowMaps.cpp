// ============================================================================
// FrostEngine Virtual Shadow Maps - Implementation
// ============================================================================

#include "FrostEngine/Renderer/VirtualShadowMaps.h"
#include "FrostEngine/Renderer/Camera.h"
#include "FrostEngine/Core/Math.h"
#include <cmath>
#include <cstring>

namespace Frost {

// ============================================================================
// Constructor / Destructor
// ============================================================================
VirtualShadowMaps::VirtualShadowMaps() = default;

VirtualShadowMaps::~VirtualShadowMaps() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================
bool VirtualShadowMaps::init(u32 screenWidth, u32 screenHeight) {
    if (initialized_) return true;

    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;

    // Initialize page table
    initializePageTable();

    // Initialize clipmaps
    for (u32 level = 0; level < VSM_CLIPMAP_LEVELS; level++) {
        clipmaps_[level].level = level;
        clipmaps_[level].activePageCount = 0;
        clipmaps_[level].needsUpdate = true;
        clipmaps_[level].innerRadius = config_.clipmapBaseResolution *
                                        std::pow(config_.clipmapSpacingFactor, (f32)level) * 0.5f;
        clipmaps_[level].outerRadius = clipmaps_[level].innerRadius * config_.clipmapSpacingFactor;
        clipmaps_[level].pageResolution = (f32)config_.pageSize;
        clipmaps_[level].texelSize = config_.clipmapBaseResolution *
                                      std::pow(config_.clipmapSpacingFactor, (f32)level);
    }

    // Initialize lights
    for (u32 i = 0; i < VSM_MAX_LIGHTS; i++) {
        lights_[i].active = false;
        lights_[i].faceCount = 1;
    }

    // Initialize temporal
    previousViewProj_ = Mat4::identity();
    firstFrame_ = true;

    // Initialize contact shadow config
    contactCfg_ = config_.contactShadow;

    initialized_ = true;
    return true;
}

void VirtualShadowMaps::shutdown() {
    if (!initialized_) return;

    pages_.clear();
    historyPages_.clear();

    for (u32 level = 0; level < VSM_CLIPMAP_LEVELS; level++) {
        clipmaps_[level].pages.clear();
    }

    initialized_ = false;
}

void VirtualShadowMaps::resize(u32 screenWidth, u32 screenHeight) {
    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;
}

// ============================================================================
// Page Table Management
// ============================================================================
void VirtualShadowMaps::initializePageTable() {
    pages_.resize(VSM_MAX_PAGES);
    pageCapacity_ = VSM_MAX_PAGES;
    pageCount_ = 0;

    for (u32 i = 0; i < VSM_MAX_PAGES; i++) {
        pages_[i].pageIndex = i;
        pages_[i].state = PageState::Unallocated;
        pages_[i].dirty = false;
        pages_[i].frameLastRendered = 0;
        pages_[i].frameLastAccessed = 0;
    }
}

u32 VirtualShadowMaps::allocatePage(u32 clipmapLevel, u32 tileX, u32 tileY,
                                     u32 lightIndex, PageType type) {
    // Find a free page
    for (u32 i = 0; i < pageCapacity_; i++) {
        if (pages_[i].state == PageState::Unallocated ||
            pages_[i].state == PageState::Evicted) {
            ShadowPage& page = pages_[i];
            page.pageIndex = i;
            page.clipmapLevel = clipmapLevel;
            page.tileX = tileX;
            page.tileY = tileY;
            page.lightIndex = lightIndex;
            page.type = type;
            page.state = PageState::Allocated;
            page.dirty = true;
            page.frameLastRendered = 0;
            page.frameLastAccessed = frameIndex_;
            page.resolutionScale = 1.0f / std::pow(2.0f, (f32)clipmapLevel);
            page.dirty = true;

            pageCount_++;
            allocStats_.allocatedThisFrame++;

            return i;
        }
    }

    return UINT32_MAX; // No free pages
}

void VirtualShadowMaps::deallocatePage(u32 pageIndex) {
    if (pageIndex >= pageCapacity_) return;

    ShadowPage& page = pages_[pageIndex];
    if (page.state == PageState::Unallocated) return;

    page.state = PageState::Evicted;
    page.dirty = false;
    pageCount_--;
    allocStats_.evictedThisFrame++;
}

void VirtualShadowMaps::markPageDirty(u32 pageIndex) {
    if (pageIndex >= pageCapacity_) return;
    pages_[pageIndex].dirty = true;
    pages_[pageIndex].state = PageState::Dirty;
}

u32 VirtualShadowMaps::findPage(u32 clipmapLevel, u32 tileX, u32 tileY) const {
    for (u32 i = 0; i < pageCapacity_; i++) {
        const ShadowPage& page = pages_[i];
        if (page.state == PageState::Unallocated || page.state == PageState::Evicted) continue;
        if (page.clipmapLevel == clipmapLevel && page.tileX == tileX && page.tileY == tileY) {
            return i;
        }
    }
    return UINT32_MAX;
}

// ============================================================================
// Frame Lifecycle
// ============================================================================
void VirtualShadowMaps::beginFrame(const Camera& camera, f32 deltaTime) {
    deltaTime_ = deltaTime;
    frameIndex_++;

    // Cache camera data
    cameraPosition_ = camera.position();
    cameraDirection_ = camera.forward();
    viewMatrix_ = camera.view();
    projMatrix_ = camera.proj();
    viewProjMatrix_ = camera.viewProj();

    // Reset stats
    allocStats_.allocatedThisFrame = 0;
    allocStats_.evictedThisFrame = 0;
    allocStats_.renderedThisFrame = 0;

    stats_.allocatedPages = 0;
    stats_.cachedPages = 0;
    stats_.dirtyPages = 0;
    stats_.renderedPages = 0;
    stats_.evictedPages = 0;
    stats_.clipmapUpdates = 0;
    stats_.contactShadows = 0;
}

void VirtualShadowMaps::allocatePages() {
    // Update clipmaps based on camera position
    updateClipmaps();

    // Garbage collect old pages
    garbageCollectPages();

    // Prioritize which pages to update
    prioritizePageUpdates();

    // Update allocation stats
    for (u32 i = 0; i < pageCapacity_; i++) {
        switch (pages_[i].state) {
        case PageState::Allocated:
            stats_.allocatedPages++;
            break;
        case PageState::Cached:
            stats_.cachedPages++;
            break;
        case PageState::Dirty:
            stats_.dirtyPages++;
            break;
        default:
            break;
        }
    }
}

void VirtualShadowMaps::renderShadowMaps() {
    // Render dirty pages
    for (u32 i = 0; i < pageCapacity_; i++) {
        ShadowPage& page = pages_[i];
        if (page.state != PageState::Dirty && !page.dirty) continue;
        if (page.state == PageState::Unallocated || page.state == PageState::Evicted) continue;

        // Check if this page needs rendering
        u32 framesSinceRender = frameIndex_ - page.frameLastRendered;
        if (framesSinceRender < 2 && !page.dirty) continue;

        // Render the page
        renderPageToTexture(page);
        page.frameLastRendered = frameIndex_;
        page.state = PageState::Rendered;
        page.dirty = false;

        allocStats_.renderedThisFrame++;
        stats_.renderedPages++;
    }

    // Compute moment shadow maps if enabled
    if (config_.useMomentShadowMaps) {
        for (u32 i = 0; i < pageCapacity_; i++) {
            if (pages_[i].state == PageState::Rendered) {
                computeMomentShadowMap(pages_[i]);
            }
        }
    }
}

void VirtualShadowMaps::applyShadowMaps() {
    // Temporal reprojection for stability
    temporalReprojection();

    // Contact shadows if enabled
    if (config_.useContactShadows) {
        renderContactShadows();
    }
}

void VirtualShadowMaps::endFrame() {
    // Store current VP for next frame's reprojection
    previousViewProj_ = viewProjMatrix_;
    firstFrame_ = false;

    updateStats();
}

// ============================================================================
// Clipmap Management
// ============================================================================
void VirtualShadowMaps::updateClipmaps() {
    for (u32 level = 0; level < config_.clipmapLevels; level++) {
        ClipmapRing& ring = clipmaps_[level];

        // Update ring center based on camera position
        ring.centerOffset = cameraPosition_;

        // Compute view-projection for this clipmap level
        buildClipmapViewProjection(level, cameraPosition_, ring.viewProjection);

        // Update pages for this level
        updateClipmapPages(level, cameraPosition_);

        ring.needsUpdate = (ring.activePageCount > 0);
        if (ring.needsUpdate) stats_.clipmapUpdates++;
    }
}

void VirtualShadowMaps::updateClipmapPages(u32 level, const Vec3& cameraPos) {
    ClipmapRing& ring = clipmaps_[level];
    ring.pages.clear();
    ring.activePageCount = 0;

    f32 texelSize = ring.texelSize;
    f32 pageWorldSize = texelSize * config_.pageSize;
    f32 halfExtent = ring.outerRadius;

    // Determine which pages are visible from camera
    i32 pagesPerSide = (i32)(halfExtent * 2.0f / pageWorldSize);
    pagesPerSide = Mathf::min(pagesPerSide, (i32)(VSM_PAGES_PER_ROW / 2));

    for (i32 py = -pagesPerSide; py < pagesPerSide; py++) {
        for (i32 px = -pagesPerSide; px < pagesPerSide; px++) {
            u32 tileX = (u32)(px + pagesPerSide);
            u32 tileY = (u32)(py + pagesPerSide);

            if (tileX >= VSM_PAGES_PER_ROW || tileY >= VSM_PAGES_PER_ROW) continue;

            // Check if page is already allocated
            u32 existingPage = findPage(level, tileX, tileY);
            if (existingPage != UINT32_MAX) {
                ring.pages.push_back(existingPage);
                ring.activePageCount++;
                continue;
            }

            // Compute page world bounds
            Vec3 pageCenter = cameraPos + Vec3(
                (f32)px * pageWorldSize + pageWorldSize * 0.5f,
                0.0f,
                (f32)py * pageWorldSize + pageWorldSize * 0.5f
            );

            // Frustum cull page
            Vec3 pageMin = pageCenter - Vec3(pageWorldSize * 0.5f, 1000.0f, pageWorldSize * 0.5f);
            Vec3 pageMax = pageCenter + Vec3(pageWorldSize * 0.5f, 1000.0f, pageWorldSize * 0.5f);

            Vec3 toPage = pageCenter - cameraPos;
            f32 dist = toPage.length();

            // Only allocate pages within clipmap range
            if (dist > ring.outerRadius) continue;

            // Allocate page
            u32 pageIndex = allocatePage(level, tileX, tileY, 0, PageType::DirectLight);
            if (pageIndex != UINT32_MAX) {
                ShadowPage& page = pages_[pageIndex];
                page.boundsMin = pageMin;
                page.boundsMax = pageMax;
                page.lightDirection = Vec3(0, -1, 0);

                ring.pages.push_back(pageIndex);
                ring.activePageCount++;
            }
        }
    }
}

void VirtualShadowMaps::buildClipmapViewProjection(u32 level, const Vec3& cameraPos, Mat4& viewProj) {
    // Build orthographic projection for clipmap level
    f32 halfExtent = clipmaps_[level].outerRadius;
    f32 nearPlane = 0.1f;
    f32 farPlane = halfExtent * 2.0f;

    // View matrix: look in light direction from camera
    Vec3 lightDir = Vec3(0, -1, 0); // Default sun direction
    Vec3 lightPos = cameraPos - lightDir * farPlane * 0.5f;

    Mat4 view = Mat4::lookAt(lightPos, cameraPos, Vec3(0, 0, 1));

    // Orthographic projection
    Mat4 proj;
    proj.m[0] = 1.0f / halfExtent;
    proj.m[5] = 1.0f / halfExtent;
    proj.m[10] = 1.0f / (nearPlane - farPlane);
    proj.m[11] = 0.0f;
    proj.m[14] = nearPlane / (nearPlane - farPlane);
    proj.m[15] = 1.0f;

    viewProj = proj * view;
}

Vec3 VirtualShadowMaps::computeClipmapOrigin(u32 level, const Vec3& cameraPos) const {
    f32 texelSize = clipmaps_[level].texelSize;
    f32 pageWorldSize = texelSize * config_.pageSize;

    // Snap to page grid
    Vec3 origin = cameraPos;
    origin.x = std::floor(origin.x / pageWorldSize) * pageWorldSize;
    origin.z = std::floor(origin.z / pageWorldSize) * pageWorldSize;
    return origin;
}

// ============================================================================
// Shadow Page Rendering
// ============================================================================
void VirtualShadowMaps::renderPageToTexture(const ShadowPage& page) {
    // Set up render target for this page
    // In a real implementation, this would:
    // 1. Bind the page's texture region in the shadow atlas
    // 2. Set up orthographic projection
    // 3. Render scene geometry
    // 4. Apply shadow bias

    rasterizePageGeometry(page);
}

void VirtualShadowMaps::rasterizePageGeometry(const ShadowPage& page) {
    // Compute page view-projection
    Mat4 pageVP;
    Vec3 pageCenter = (page.boundsMin + page.boundsMax) * 0.5f;
    Vec3 lightDir = page.lightDirection;

    Vec3 lightPos = pageCenter - lightDir * 1000.0f;
    pageVP = Mat4::lookAt(lightPos, pageCenter, Vec3(0, 0, 1));

    // In a real implementation, this would rasterize all geometry
    // visible from the light into this page's texture
}

void VirtualShadowMaps::applyShadowBias(f32& depth, const Vec3& normal, const Vec3& lightDir) {
    // Slope-scaled bias
    f32 cosTheta = Mathf::max(normal.dot(-lightDir), 0.0f);
    f32 bias = config_.shadowBias * (1.0f - cosTheta) + config_.normalBias * (1.0f - cosTheta);

    depth += bias;
}

// ============================================================================
// Moment Shadow Maps
// ============================================================================
void VirtualShadowMaps::computeMomentShadowMap(const ShadowPage& page) {
    // Compute moments for soft PCF filtering
    // M1 = E[z], M2 = E[z^2], M3 = E[z^3], M4 = E[z^4]

    // In a real implementation, this would process the shadow depth texture
    // and compute moment values for each texel
}

void VirtualShadowMaps::computeMoments2(f32 depth, f32& m1, f32& m2) {
    m1 = depth;
    m2 = depth * depth;
}

void VirtualShadowMaps::computeMoments4(const f32* depthBuffer, f32* momentBuffer,
                                         u32 width, u32 height) {
    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            u32 idx = y * width + x;
            f32 depth = depthBuffer[idx];

            // Compute moments
            f32 m1 = depth;
            f32 m2 = depth * depth;
            f32 m3 = depth * depth * depth;
            f32 m4 = depth * depth * depth * depth;

            momentBuffer[idx * 4 + 0] = m1;
            momentBuffer[idx * 4 + 1] = m2;
            momentBuffer[idx * 4 + 2] = m3;
            momentBuffer[idx * 4 + 3] = m4;
        }
    }
}

f32 VirtualShadowMaps::sampleShadowMOM(const Vec2& uv, f32 comparisonDepth, u32 lightIndex) {
    // Sample moment shadow map and compute visibility
    // Using Chebyshev's inequality for soft shadows

    // In a real implementation, this would sample the moment texture
    // For now, return a simple comparison
    return (comparisonDepth > 0.5f) ? 1.0f : 0.0f;
}

f32 VirtualShadowMaps::chebyshevInequality(f32 m1, f32 m2, f32 distance) {
    // P(x <= d) <= sigma^2 / (sigma^2 + (d - E[x])^2)
    f32 variance = m2 - m1 * m1;
    variance = Mathf::max(variance, 0.0001f);

    f32 d = distance - m1;
    f32 pMax = variance / (variance + d * d);

    // One-tailed Chebyshev
    if (distance <= m1) return 1.0f;
    return pMax;
}

// ============================================================================
// Contact Shadows
// ============================================================================
void VirtualShadowMaps::renderContactShadows() {
    if (!contactCfg_.enabled) return;

    // Contact shadows are short-range ray-traced shadows
    // applied on top of regular shadow maps for fine detail

    stats_.contactShadows++;
}

f32 VirtualShadowMaps::traceContactShadow(const Vec3& worldPos, const Vec3& lightDir, f32 maxDist) {
    f32 visibility = 1.0f;
    Vec3 rayOrigin = worldPos + lightDir * contactCfg_.stepSize;

    for (u32 step = 0; step < contactCfg_.maxSteps; step++) {
        Vec3 samplePos = rayOrigin + lightDir * (f32)step * contactCfg_.stepSize;

        // Sample scene depth at this position
        f32 sceneDepth = sampleSceneDepth(Vec2(0.5f, 0.5f)); // Simplified

        // Compare with expected depth
        f32 expectedDepth = samplePos.z;
        if (sceneDepth < expectedDepth - contactCfg_.thickness) {
            visibility *= 0.5f; // Occluded
        }

        f32 dist = (samplePos - worldPos).length();
        if (dist > maxDist) break;
    }

    return visibility;
}

f32 VirtualShadowMaps::traceContactShadowHiZ(const Vec3& worldPos, const Vec3& lightDir, f32 maxDist) {
    // HiZ-accelerated contact shadow tracing
    f32 t = 0.0f;
    Vec3 currentPos = worldPos + lightDir * contactCfg_.stepSize;

    for (u32 step = 0; step < contactCfg_.maxSteps; step++) {
        f32 sceneDepth = sampleSceneDepth(Vec2(0.5f, 0.5f)); // Simplified

        if (sceneDepth < currentPos.z - contactCfg_.thickness) {
            return 0.0f; // Fully occluded
        }

        t += contactCfg_.stepSize;
        currentPos = worldPos + lightDir * t;

        if (t > maxDist) break;
    }

    return 1.0f;
}

f32 VirtualShadowMaps::sampleSceneDepth(const Vec2& screenUV) const {
    // In a real implementation, this would sample the scene depth buffer
    return 0.5f; // Placeholder
}

bool VirtualShadowMaps::hiZTraceSegment(const Vec3& start, const Vec3& end, f32& hitT) const {
    // HiZ-accelerated line segment intersection
    Vec3 dir = end - start;
    f32 length = dir.length();
    if (length < 0.001f) return false;

    dir = dir / length;

    // Simplified: just check a few points
    for (u32 i = 0; i < 8; i++) {
        f32 t = (f32)i / 8.0f * length;
        Vec3 pos = start + dir * t;
        f32 depth = sampleSceneDepth(Vec2(0.5f, 0.5f)); // Simplified

        if (depth < pos.z) {
            hitT = t;
            return true;
        }
    }

    return false;
}

// ============================================================================
// Temporal Reprojection
// ============================================================================
void VirtualShadowMaps::temporalReprojection() {
    if (firstFrame_) return;

    Mat4 reproject;
    computeReprojectionMatrix(viewProjMatrix_, previousViewProj_, reproject);

    for (u32 i = 0; i < pageCapacity_; i++) {
        ShadowPage& page = pages_[i];
        if (page.state == PageState::Unallocated || page.state == PageState::Evicted) continue;

        // Try to reproject from history
        ShadowPage historyPage;
        if (reprojectPage(page, historyPage)) {
            // Blend with history for temporal stability
            f32 temporalWeight = computeTemporalWeight(frameIndex_, historyPage.frameLastRendered);
            page.minDepth = Mathf::lerp(historyPage.minDepth, page.minDepth, temporalWeight);
            page.maxDepth = Mathf::lerp(historyPage.maxDepth, page.maxDepth, temporalWeight);
        }
    }
}

bool VirtualShadowMaps::reprojectPage(const ShadowPage& currentPage, ShadowPage& historyPage) {
    // Reproject current page to find corresponding history page
    Vec3 pageCenter = (currentPage.boundsMin + currentPage.boundsMax) * 0.5f;

    // Transform to previous frame's screen space
    Vec4 prevClip = previousViewProj_ * Vec4(pageCenter, 1.0f);
    if (prevClip.w <= 0.0f) return false;

    f32 prevU = prevClip.x / prevClip.w * 0.5f + 0.5f;
    f32 prevV = prevClip.y / prevClip.w * 0.5f + 0.5f;

    if (prevU < 0 || prevU > 1 || prevV < 0 || prevV > 1) return false;

    // Find history page at this position
    u32 historyTileX = (u32)(prevU * VSM_PAGES_PER_ROW);
    u32 historyTileY = (u32)(prevV * VSM_PAGES_PER_ROW);

    u32 historyPageIdx = findPage(currentPage.clipmapLevel, historyTileX, historyTileY);
    if (historyPageIdx == UINT32_MAX) return false;

    historyPage = pages_[historyPageIdx];
    return true;
}

f32 VirtualShadowMaps::computeTemporalWeight(u32 currentFrame, u32 lastFrame) const {
    u32 frameDiff = currentFrame - lastFrame;
    if (frameDiff > config_.maxReprojectionFrames) return 0.0f;

    f32 weight = std::pow(config_.temporalStability, (f32)frameDiff);
    return weight;
}

// ============================================================================
// Nanite Integration
// ============================================================================
void VirtualShadowMaps::renderNaniteShadow(const Cluster& cluster, const Mat4& viewProj) {
    // Render Nanite cluster to shadow map
    for (u32 t = 0; t < cluster.triangleCount; t++) {
        const ClusterTriangle& tri = cluster.triangles[t];
        Vec3 v0 = cluster.positions[tri.indices[0]];
        Vec3 v1 = cluster.positions[tri.indices[1]];
        Vec3 v2 = cluster.positions[tri.indices[2]];

        // Find which page this triangle falls on
        Vec3 center = (v0 + v1 + v2) / 3.0f;

        // Project to virtual shadow map space
        Vec4 clip = viewProj * Vec4(center, 1.0f);
        if (clip.w <= 0.0f) continue;

        f32 u = clip.x / clip.w * 0.5f + 0.5f;
        f32 v = clip.y / clip.w * 0.5f + 0.5f;

        u32 tileX = (u32)(u * VSM_PAGES_PER_ROW);
        u32 tileY = (u32)(v * VSM_PAGES_PER_ROW);

        if (tileX >= VSM_PAGES_PER_ROW || tileY >= VSM_PAGES_PER_ROW) continue;

        // Find or allocate page
        u32 pageIdx = findPage(0, tileX, tileY);
        if (pageIdx == UINT32_MAX) {
            pageIdx = allocatePage(0, tileX, tileY, 0, PageType::DirectLight);
        }

        if (pageIdx != UINT32_MAX) {
            // Rasterize triangle to page
            // In a real implementation, this would write to the page's depth texture
        }
    }
}

void VirtualShadowMaps::rasterizeClusterShadow(const Cluster& cluster, const Mat4& viewProj,
                                                 u32 pageIndex) {
    // Rasterize cluster triangles to specific shadow page
    for (u32 t = 0; t < cluster.triangleCount; t++) {
        const ClusterTriangle& tri = cluster.triangles[t];
        Vec3 v0 = cluster.positions[tri.indices[0]];
        Vec3 v1 = cluster.positions[tri.indices[1]];
        Vec3 v2 = cluster.positions[tri.indices[2]];

        // Project and rasterize to page texture
        // Simplified: just mark page as needing update
        if (pageIndex < pageCapacity_) {
            pages_[pageIndex].dirty = true;
        }
    }
}

// ============================================================================
// Shadow Query
// ============================================================================
f32 VirtualShadowMaps::getShadowAtPosition(const Vec3& worldPos, u32 lightIndex) const {
    // Find the appropriate page for this world position
    for (u32 i = 0; i < pageCapacity_; i++) {
        const ShadowPage& page = pages_[i];
        if (page.state == PageState::Unallocated || page.state == PageState::Evicted) continue;
        if (page.lightIndex != lightIndex) continue;

        if (worldPos.x >= page.boundsMin.x && worldPos.x <= page.boundsMax.x &&
            worldPos.z >= page.boundsMin.z && worldPos.z <= page.boundsMax.z) {
            // Found page - sample shadow
            return computePCFShadow(Vec2(0.5f, 0.5f), worldPos.y, lightIndex);
        }
    }

    return 1.0f; // No shadow
}

f32 VirtualShadowMaps::computePCFShadow(const Vec2& uv, f32 depth, u32 lightIndex) const {
    // Percentage Closer Filtering for soft shadows
    f32 shadow = 0.0f;
    f32 filterRadius = 1.0f / 2048.0f;

    // 5x5 PCF kernel
    for (i32 y = -2; y <= 2; y++) {
        for (i32 x = -2; x <= 2; x++) {
            Vec2 sampleUV = uv + Vec2((f32)x, (f32)y) * filterRadius;

            // Sample depth from shadow map
            f32 shadowDepth = 0.5f; // Would sample from texture

            // Compare
            if (depth > shadowDepth) {
                shadow += 1.0f;
            }
        }
    }

    return shadow / 25.0f;
}

f32 VirtualShadowMaps::computePCSSShadow(const Vec3& worldPos, u32 lightIndex) const {
    // Percentage-Closer Soft Shadows
    // 1. Find blocker
    f32 avgBlockerDepth = 0.0f;
    u32 blockerCount = 0;

    f32 searchRadius = 5.0f / 2048.0f;

    for (i32 y = -4; y <= 4; y++) {
        for (i32 x = -4; x <= 4; x++) {
            Vec2 sampleOffset = Vec2((f32)x, (f32)y) * searchRadius;
            f32 sampleDepth = 0.5f; // Would sample from texture

            if (sampleDepth < worldPos.y) {
                avgBlockerDepth += sampleDepth;
                blockerCount++;
            }
        }
    }

    if (blockerCount == 0) return 1.0f; // No blockers

    avgBlockerDepth /= (f32)blockerCount;

    // 2. Compute filter radius
    f32 penumbraWidth = (worldPos.y - avgBlockerDepth) / avgBlockerDepth;
    f32 filterRadius = penumbraWidth * 5.0f / 2048.0f;

    // 3. PCF with computed filter radius
    f32 shadow = 0.0f;
    u32 sampleCount = 0;

    for (i32 y = -4; y <= 4; y++) {
        for (i32 x = -4; x <= 4; x++) {
            Vec2 sampleUV = Vec2(0.5f) + Vec2((f32)x, (f32)y) * filterRadius;
            f32 shadowDepth = 0.5f; // Would sample from texture

            if (worldPos.y > shadowDepth) {
                shadow += 1.0f;
            }
            sampleCount++;
        }
    }

    return shadow / (f32)sampleCount;
}

// ============================================================================
// Light Management
// ============================================================================
u32 VirtualShadowMaps::addShadowCastingLight(const Vec3& position, const Vec3& direction,
                                               f32 intensity, f32 range, LightType type) {
    if (lightCount_ >= VSM_MAX_LIGHTS) return UINT32_MAX;

    u32 idx = lightCount_++;
    ShadowLight& light = lights_[idx];
    light.position = position;
    light.direction = direction.normalized();
    light.intensity = intensity;
    light.range = range;
    light.type = type;
    light.active = true;

    switch (type) {
    case LightType::Point:
        light.faceCount = 6;
        break;
    case LightType::Spot:
    case LightType::Directional:
    default:
        light.faceCount = 1;
        break;
    }

    // Compute shadow VP for this light
    Vec3 lightPos = position - direction * range;
    Mat4 view = Mat4::lookAt(lightPos, position, Vec3(0, 1, 0));
    Mat4 proj = Mat4::perspective(Mathf::HALF_PI, 1.0f, 0.1f, range * 2.0f);
    shadowVPs_[idx] = proj * view;

    return idx;
}

void VirtualShadowMaps::removeShadowCastingLight(u32 lightIndex) {
    if (lightIndex >= lightCount_) return;
    lights_[lightIndex].active = false;

    // Deallocate pages for this light
    for (u32 i = 0; i < pageCapacity_; i++) {
        if (pages_[i].lightIndex == lightIndex) {
            deallocatePage(i);
        }
    }
}

void VirtualShadowMaps::updateLightTransform(u32 lightIndex, const Vec3& position,
                                               const Vec3& direction) {
    if (lightIndex >= lightCount_) return;

    ShadowLight& light = lights_[lightIndex];
    light.position = position;
    light.direction = direction.normalized();

    // Recompute shadow VP
    Vec3 lightPos = position - direction * light.range;
    Mat4 view = Mat4::lookAt(lightPos, position, Vec3(0, 1, 0));
    Mat4 proj = Mat4::perspective(Mathf::HALF_PI, 1.0f, 0.1f, light.range * 2.0f);
    shadowVPs_[lightIndex] = proj * view;

    // Mark all pages for this light as dirty
    for (u32 i = 0; i < pageCapacity_; i++) {
        if (pages_[i].lightIndex == lightIndex) {
            pages_[i].dirty = true;
        }
    }
}

// ============================================================================
// Page Management Helpers
// ============================================================================
void VirtualShadowMaps::garbageCollectPages() {
    for (u32 i = 0; i < pageCapacity_; i++) {
        ShadowPage& page = pages_[i];
        if (page.state == PageState::Unallocated) continue;

        u32 framesSinceAccess = frameIndex_ - page.frameLastAccessed;
        if (framesSinceAccess > (u32)config_.pageLifeTime) {
            deallocatePage(i);
        }
    }
}

void VirtualShadowMaps::prioritizePageUpdates() {
    // Sort dirty pages by priority (distance to camera, importance)
    // For simplicity, we just iterate and update oldest first

    u32 updatesRemaining = config_.maxPagesPerFrame;

    for (u32 i = 0; i < pageCapacity_ && updatesRemaining > 0; i++) {
        ShadowPage& page = pages_[i];
        if (page.state != PageState::Dirty && !page.dirty) continue;

        // Check distance to camera
        Vec3 pageCenter = (page.boundsMin + page.boundsMax) * 0.5f;
        f32 dist = (pageCenter - cameraPosition_).length();

        // Prioritize closer pages
        if (dist < 100.0f) {
            page.state = PageState::Dirty;
            updatesRemaining--;
        }
    }

    // Update remaining dirty pages
    for (u32 i = 0; i < pageCapacity_ && updatesRemaining > 0; i++) {
        ShadowPage& page = pages_[i];
        if (page.state != PageState::Dirty && !page.dirty) continue;

        page.state = PageState::Dirty;
        updatesRemaining--;
    }
}

void VirtualShadowMaps::updateStats() {
    allocStats_.totalAllocated = stats_.allocatedPages;
    allocStats_.totalCached = stats_.cachedPages;
    allocStats_.totalDirty = stats_.dirtyPages;
}

const ShadowPage* VirtualShadowMaps::getPage(u32 pageIndex) const {
    if (pageIndex >= pageCapacity_) return nullptr;
    if (pages_[pageIndex].state == PageState::Unallocated) return nullptr;
    return &pages_[pageIndex];
}

u32 VirtualShadowMaps::getActivePageCount() const {
    u32 count = 0;
    for (u32 i = 0; i < pageCapacity_; i++) {
        if (pages_[i].state != PageState::Unallocated && pages_[i].state != PageState::Evicted) {
            count++;
        }
    }
    return count;
}

void VirtualShadowMaps::computeReprojectionMatrix(const Mat4& currentVP, const Mat4& historyVP,
                                                    Mat4& reprojection) {
    // Reprojection = historyVP * inverse(currentVP)
    // Simplified: just use the difference
    reprojection = historyVP;
}

} // namespace Frost
