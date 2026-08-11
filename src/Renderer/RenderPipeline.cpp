#include "Renderer/RenderPipeline.h"
#include "Core/Math.h"

namespace Frost {

// ============================================================================
// Render pass graph helpers
// ============================================================================

namespace {

bool passDependsOn(const Vector<RenderPassNode>& graph, u32 dependent, u32 producer) {
    if (dependent == producer) return false;
    for (usize k = 0; k < graph[dependent].inputTextures.size(); k++) {
        u32 input = graph[dependent].inputTextures[k];
        for (usize m = 0; m < graph[producer].outputTextures.size(); m++) {
            if (input == graph[producer].outputTextures[m]) return true;
        }
    }
    return false;
}

bool topoSortPasses(const Vector<RenderPassNode>& graph, Vector<u32>& order) {
    order.clear();
    const usize n = graph.size();
    if (n == 0) return true;

    Vector<u32> indegree(n, 0u);
    for (usize i = 0; i < n; i++) {
        for (usize j = 0; j < n; j++) {
            if (passDependsOn(graph, (u32)i, (u32)j)) indegree[i]++;
        }
    }

    Vector<u32> queue;
    for (usize i = 0; i < n; i++) {
        if (indegree[i] == 0) queue.push_back((u32)i);
    }

    while (!queue.empty()) {
        u32 node = queue[0];
        queue.erase(0);
        order.push_back(node);
        for (usize j = 0; j < n; j++) {
            if (passDependsOn(graph, (u32)j, node)) {
                if (indegree[j] > 0) indegree[j]--;
                if (indegree[j] == 0) queue.push_back((u32)j);
            }
        }
    }
    return order.size() == n;
}

u32 graphDepthForNode(const Vector<RenderPassNode>& graph, Vector<u8>& state,
                      Vector<u32>& depth, u32 node) {
    if (state[node] == 2) return depth[node];
    if (state[node] == 1) return 0;
    state[node] = 1;
    u32 best = 0;
    for (usize j = 0; j < graph.size(); j++) {
        if (passDependsOn(graph, node, (u32)j)) {
            u32 d = graphDepthForNode(graph, state, depth, (u32)j);
            if (d > best) best = d;
        }
    }
    state[node] = 2;
    depth[node] = best + 1;
    return depth[node];
}

} // namespace

// ============================================================================
// RenderPipeline
// ============================================================================

RenderPipeline& RenderPipeline::instance() {
    static RenderPipeline pipeline;
    return pipeline;
}

void RenderPipeline::init(Renderer::GraphicsDevice* device) {
    device_ = device;
    passes_.clear();
    sortedPasses_.clear();
    passGraph_.clear();
    transitions_.clear();
    frameTimes_.clear();
    frameIndex_ = 0;
    totalPasses_ = 0;
    passExecutions_ = 0;
    frameTimeMs_ = 0.0f;
    smoothedFrameTimeMs_ = 0.0f;
    targetFrameTimeMs_ = 16.67f;
    width_ = 0;
    height_ = 0;
}

void RenderPipeline::shutdown() {
    for (usize i = 0; i < passes_.size(); i++) {
        delete passes_[i];
    }
    passes_.clear();
    sortedPasses_.clear();
    passGraph_.clear();
    transitions_.clear();
    frameTimes_.clear();
    device_ = nullptr;
    frameIndex_ = 0;
    totalPasses_ = 0;
    passExecutions_ = 0;
    frameTimeMs_ = 0.0f;
    smoothedFrameTimeMs_ = 0.0f;
}

void RenderPipeline::addPass(RenderPass* pass) {
    if (pass) passes_.push_back(pass);
}

void RenderPipeline::removePass(PassType type) {
    for (usize i = 0; i < passes_.size(); i++) {
        if (passes_[i]->type == type) {
            delete passes_[i];
            passes_.erase(i);
            break;
        }
    }
}

void RenderPipeline::setPassEnabled(PassType type, bool enabled) {
    for (usize i = 0; i < passes_.size(); i++) {
        if (passes_[i]->type == type) {
            passes_[i]->enabled = enabled;
            break;
        }
    }
}

void RenderPipeline::prepare(const ViewData& view) {
    sortedPasses_.clear();
    for (usize i = 0; i < passes_.size(); i++) {
        if (passes_[i]->enabled) sortedPasses_.push_back(passes_[i]);
    }
    for (usize i = 0; i < sortedPasses_.size(); i++) {
        for (usize j = i + 1; j < sortedPasses_.size(); j++) {
            if (sortedPasses_[j]->priority < sortedPasses_[i]->priority) {
                RenderPass* tmp = sortedPasses_[i];
                sortedPasses_[i] = sortedPasses_[j];
                sortedPasses_[j] = tmp;
            }
        }
    }
    for (usize i = 0; i < sortedPasses_.size(); i++) {
        sortedPasses_[i]->prepare(view);
    }
}

void RenderPipeline::execute(Renderer::CommandBuffer* cmd, const ViewData& view) {
    for (usize i = 0; i < sortedPasses_.size(); i++) {
        sortedPasses_[i]->execute(cmd, view);
    }
}

void RenderPipeline::resolve(Renderer::CommandBuffer* cmd) {
    for (usize i = 0; i < sortedPasses_.size(); i++) {
        sortedPasses_[i]->resolve(cmd);
    }
}

void RenderPipeline::executeAll(Renderer::CommandBuffer* cmd, const ViewData& view) {
    prepare(view);
    execute(cmd, view);
    resolve(cmd);
}

void RenderPipeline::createGBuffer(u32 width, u32 height) {
    width_ = width;
    height_ = height;
}

void RenderPipeline::destroyGBuffer() {
    width_ = 0;
    height_ = 0;
}

Renderer::Texture* RenderPipeline::getGBufferTarget(u32 index) const {
    if (index < 4) return gbufferTargets_[index];
    return nullptr;
}

Renderer::Texture* RenderPipeline::getDepthTarget() const {
    return depthTarget_;
}

Renderer::Texture* RenderPipeline::getLightingTarget() const {
    return lightingTarget_;
}

Renderer::Texture* RenderPipeline::getNormalTarget() const {
    return normalTarget_;
}

// ============================================================================
// Render pass graph
// ============================================================================

u32 RenderPipeline::addRenderPass(u32 nameHash, const Vector<u32>& inputs,
                                  const Vector<u32>& outputs, bool isCompute) {
    RenderPassNode node;
    node.passId = (u32)passGraph_.size();
    node.nameHash = nameHash;
    node.inputTextures = inputs;
    node.outputTextures = outputs;
    node.dependencies = 0;
    node.isCompute = isCompute;
    passGraph_.push_back(node);
    totalPasses_ = (u32)passGraph_.size();
    return node.passId;
}

void RenderPipeline::addResourceTransition(u32 resourceId, u32 fromState,
                                          u32 toState, u32 passIndex) {
    ResourceTransition transition;
    transition.resourceId = resourceId;
    transition.fromState = fromState;
    transition.toState = toState;
    transition.passIndex = passIndex;
    transitions_.push_back(transition);
}

void RenderPipeline::buildGraph() {
    const usize n = passGraph_.size();
    if (n == 0) return;

    Vector<u32> order;
    if (!topoSortPasses(passGraph_, order)) return;

    Vector<RenderPassNode> sorted;
    sorted.reserve(n);
    for (usize i = 0; i < n; i++) {
        sorted.push_back(passGraph_[order[i]]);
    }
    for (usize i = 0; i < n; i++) {
        u32 deps = 0;
        for (usize j = 0; j < n; j++) {
            if (passDependsOn(passGraph_, (u32)i, (u32)j)) deps++;
        }
        sorted[i].dependencies = deps;
    }
    passGraph_.swap(sorted);
}

u32 RenderPipeline::executePassGraph() {
    buildGraph();
    for (usize i = 0; i < passGraph_.size(); i++) {
        passExecutions_++;
    }
    return (u32)passGraph_.size();
}

bool RenderPipeline::validateGraph() const {
    const usize n = passGraph_.size();
    Vector<u32> order;
    if (!topoSortPasses(passGraph_, order)) return false;

    for (usize i = 0; i < n; i++) {
        const RenderPassNode& node = passGraph_[i];
        for (usize k = 0; k < node.inputTextures.size(); k++) {
            u32 input = node.inputTextures[k];
            bool produced = false;
            for (usize j = 0; j < n; j++) {
                if (j == i) continue;
                const RenderPassNode& producer = passGraph_[j];
                for (usize m = 0; m < producer.outputTextures.size(); m++) {
                    if (producer.outputTextures[m] == input) {
                        produced = true;
                        break;
                    }
                }
                if (produced) break;
            }
            if (!produced) return false;
        }
    }
    return true;
}

u32 RenderPipeline::getGraphDepth() const {
    const usize n = passGraph_.size();
    if (n == 0) return 0;
    Vector<u8> state(n, (u8)0);
    Vector<u32> depth(n, 0u);
    u32 maxDepth = 0;
    for (usize i = 0; i < n; i++) {
        u32 d = graphDepthForNode(passGraph_, state, depth, (u32)i);
        if (d > maxDepth) maxDepth = d;
    }
    return maxDepth;
}

// ============================================================================
// Frame pacing
// ============================================================================

void RenderPipeline::beginFrame(f32 dt) {
    if (dt < 0.0f) dt = 0.0f;
    frameTimeMs_ = dt;
    frameTimes_.push_back(dt);
    const usize kMaxHistory = 256;
    while (frameTimes_.size() > kMaxHistory) {
        frameTimes_.erase(0);
    }
    if (smoothedFrameTimeMs_ <= 0.0f) {
        smoothedFrameTimeMs_ = dt;
    } else {
        smoothedFrameTimeMs_ = Mathf::lerp(smoothedFrameTimeMs_, dt, 0.1f);
    }
}

void RenderPipeline::endFrame() {
    frameIndex_++;
    if (frameTimes_.empty()) return;
    f32 sum = 0.0f;
    for (usize i = 0; i < frameTimes_.size(); i++) {
        sum += frameTimes_[i];
    }
    smoothedFrameTimeMs_ = sum / (f32)frameTimes_.size();
}

void RenderPipeline::setTargetFrameTime(f32 ms) {
    targetFrameTimeMs_ = ms > 0.0f ? ms : 16.67f;
}

f32 RenderPipeline::getTargetFrameTime() const {
    return targetFrameTimeMs_;
}

f32 RenderPipeline::getFrameTimeMs() const {
    return frameTimeMs_;
}

f32 RenderPipeline::getSmoothedFrameTimeMs() const {
    return smoothedFrameTimeMs_;
}

f32 RenderPipeline::getFPS() const {
    return smoothedFrameTimeMs_ > 0.0f ? 1000.0f / smoothedFrameTimeMs_ : 0.0f;
}

u32 RenderPipeline::getTotalPasses() const {
    return totalPasses_;
}

u32 RenderPipeline::getPassExecutions() const {
    return passExecutions_;
}

void RenderPipeline::resetStats() {
    frameIndex_ = 0;
    passExecutions_ = 0;
    frameTimeMs_ = 0.0f;
    smoothedFrameTimeMs_ = 0.0f;
    frameTimes_.clear();
}

// ============================================================================
// Pass implementations
// ============================================================================

ShadowPass::ShadowPass() = default;
ShadowPass::~ShadowPass() = default;

void ShadowPass::prepare(const ViewData& view) {
    (void)view;
}

void ShadowPass::execute(Renderer::CommandBuffer* cmd, const ViewData& view) {
    (void)cmd;
    (void)view;
}

void ShadowPass::resolve(Renderer::CommandBuffer* cmd) {
    (void)cmd;
}

void ShadowPass::setLight(LightComponent* light) {
    light_ = light;
}

void ShadowPass::setQuality(ShadowQuality quality) {
    quality_ = quality;
}

GBufferPass::GBufferPass() = default;
GBufferPass::~GBufferPass() = default;

void GBufferPass::prepare(const ViewData& view) {
    (void)view;
}

void GBufferPass::execute(Renderer::CommandBuffer* cmd, const ViewData& view) {
    (void)cmd;
    (void)view;
}

void GBufferPass::resolve(Renderer::CommandBuffer* cmd) {
    (void)cmd;
}

LightPass::LightPass() = default;
LightPass::~LightPass() = default;

void LightPass::prepare(const ViewData& view) {
    (void)view;
}

void LightPass::execute(Renderer::CommandBuffer* cmd, const ViewData& view) {
    (void)cmd;
    (void)view;
}

void LightPass::resolve(Renderer::CommandBuffer* cmd) {
    (void)cmd;
}

GlobalIlluminationPass::GlobalIlluminationPass() = default;
GlobalIlluminationPass::~GlobalIlluminationPass() = default;

void GlobalIlluminationPass::prepare(const ViewData& view) {
    (void)view;
}

void GlobalIlluminationPass::execute(Renderer::CommandBuffer* cmd, const ViewData& view) {
    (void)cmd;
    (void)view;
}

void GlobalIlluminationPass::resolve(Renderer::CommandBuffer* cmd) {
    (void)cmd;
}

PostProcessPass::PostProcessPass() = default;
PostProcessPass::~PostProcessPass() = default;

void PostProcessPass::prepare(const ViewData& view) {
    (void)view;
}

void PostProcessPass::execute(Renderer::CommandBuffer* cmd, const ViewData& view) {
    (void)cmd;
    (void)view;
}

void PostProcessPass::resolve(Renderer::CommandBuffer* cmd) {
    (void)cmd;
}

void PostProcessPass::enableDLSS(bool enable) {
    enableDLSS_ = enable;
}

void PostProcessPass::enableFSR(bool enable) {
    enableFSR_ = enable;
}

} // namespace Frost
