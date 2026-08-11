#include "Renderer/BindlessMaterial.h"

namespace Frost {

// ============================================================================
// Bindless material pool — descriptor indexing + texture array management
// ============================================================================

// ---------------------------------------------------------------------------
// Register a material in the pool. Reserves a descriptor range (textureCount
// descriptors in the bindless heap) plus the matching range in the global
// texture array. Returns a pool handle, or INVALID_HANDLE on failure.
// ---------------------------------------------------------------------------
u32 BindlessMaterialSystem::registerMaterial(u32 materialId, u32 textureCount) {
    if (!enableBindless_) return INVALID_HANDLE;
    if (materialCount_ >= maxMaterials_) return INVALID_HANDLE;
    if (textureCount_ + textureCount > maxTextures_) return INVALID_HANDLE;

    MaterialSlot slot;
    slot.materialId = materialId;
    slot.textureStart = textureCount_;
    slot.textureCount = textureCount;
    slot.descriptorIndex = descriptorAllocations_;
    slot.valid = true;

    materialSlots_.push_back(slot);
    descriptorAllocations_ += textureCount;
    textureCount_ += textureCount;
    materialCount_++;

    return (u32)materialSlots_.size() - 1;
}

// ---------------------------------------------------------------------------
// Remove a material from the pool. Frees its descriptor range and releases
// the texture array slots bound to it. Invalidates the given handle.
// ---------------------------------------------------------------------------
void BindlessMaterialSystem::unregisterMaterial(u32 handle) {
    if (handle >= materialSlots_.size()) return;
    MaterialSlot& slot = materialSlots_[handle];
    if (!slot.valid) return;

    u32 slotCount = slot.textureCount;
    slot.valid = false;

    // Remove texture array entries owned by this material's reserved range.
    for (usize i = textureHandles_.size(); i > 0; --i) {
        usize idx = i - 1;
        u32 arrayIndex = textureHandles_[idx].arrayIndex;
        if (arrayIndex >= slot.textureStart && arrayIndex < slot.textureStart + slotCount) {
            textureHandles_.erase(idx);
        }
    }

    if (descriptorAllocations_ >= slotCount) descriptorAllocations_ -= slotCount;
    if (textureCount_ >= slotCount) textureCount_ -= slotCount;
    if (materialCount_ > 0) materialCount_--;

    materialSlots_.erase(handle);
}

// ---------------------------------------------------------------------------
// Bind a texture to a material slot. The texture array entry is allocated
// within the material's reserved range. Returns a texture handle.
// ---------------------------------------------------------------------------
u32 BindlessMaterialSystem::bindTexture(u32 materialHandle, u32 slot, u32 textureId) {
    if (materialHandle >= materialSlots_.size()) return INVALID_HANDLE;
    MaterialSlot& m = materialSlots_[materialHandle];
    if (!m.valid) return INVALID_HANDLE;
    if (slot >= m.textureCount) return INVALID_HANDLE;

    TextureHandle h;
    h.textureId = textureId;
    h.arrayIndex = m.textureStart + slot;
    h.frameCreated = frameCounter_++;
    textureHandles_.push_back(h);

    return (u32)textureHandles_.size() - 1;
}

// ---------------------------------------------------------------------------
// Look up the most recent texture handle bound to (materialHandle, slot).
// ---------------------------------------------------------------------------
u32 BindlessMaterialSystem::getTextureHandle(u32 materialHandle, u32 slot) const {
    if (materialHandle >= materialSlots_.size()) return INVALID_HANDLE;
    const MaterialSlot& m = materialSlots_[materialHandle];
    if (!m.valid) return INVALID_HANDLE;
    if (slot >= m.textureCount) return INVALID_HANDLE;

    u32 target = m.textureStart + slot;
    for (usize i = textureHandles_.size(); i > 0; --i) {
        usize idx = i - 1;
        if (textureHandles_[idx].arrayIndex == target) return (u32)idx;
    }
    return INVALID_HANDLE;
}

// ---------------------------------------------------------------------------
// Return the material slot for a handle, or a static invalid slot.
// ---------------------------------------------------------------------------
const MaterialSlot& BindlessMaterialSystem::getMaterialSlot(u32 handle) const {
    if (handle >= materialSlots_.size()) return invalidSlot();
    const MaterialSlot& s = materialSlots_[handle];
    return s.valid ? s : invalidSlot();
}

u32 BindlessMaterialSystem::getDescriptorIndex(u32 materialHandle) const {
    if (materialHandle >= materialSlots_.size()) return 0;
    const MaterialSlot& s = materialSlots_[materialHandle];
    return s.valid ? s.descriptorIndex : 0;
}

u32 BindlessMaterialSystem::getTextureArrayIndex(u32 textureHandle) const {
    if (textureHandle >= textureHandles_.size()) return INVALID_HANDLE;
    return textureHandles_[textureHandle].arrayIndex;
}

// ---------------------------------------------------------------------------
// Reset the bindless material pool back to an empty state.
// ---------------------------------------------------------------------------
void BindlessMaterialSystem::resetPool() {
    materialSlots_.clear();
    textureHandles_.clear();
    materialCount_ = 0;
    textureCount_ = 0;
    descriptorAllocations_ = 0;
    frameCounter_ = 0;
}

const MaterialSlot& BindlessMaterialSystem::invalidSlot() {
    static const MaterialSlot s{ 0, 0, 0, 0, false };
    return s;
}

} // namespace Frost
