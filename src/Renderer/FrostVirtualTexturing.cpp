// ============================================================================
// FrostEngine FrostVirtualTexturing — Virtual Texturing System
// ============================================================================
// Proprietary feedback-driven virtual texturing with streaming compression.
// Uses giant virtual textures (16K+) divided into pages, with feedback-driven
// streaming, BC7 compression, and shelf packing for physical atlas management.
// ============================================================================

#include "FrostEngine/Renderer/FrostVirtualTexturing.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <random>

namespace Frost {

// ============================================================================
// Construction / Destruction
// ============================================================================

FrostVirtualTexturing::FrostVirtualTexturing()
    : atlasSize_(VT_ATLAS_SIZE), pagesPerAtlas_(0), maxPagesPerAtlas_(0),
      pageTableSize_(0), activeAtlases_(0), feedbackCount_(0), textureCount_(0),
      totalVirtualPages_(0), legacyResidentPages_(0), streamingPages_(0),
      evictionCount_(0), lastProcessTimeMs_(0), frameCount_(0), initialized_(false),
      feedbackQueueCapacity_(4096), pageTtlFrames_(120), nextFreeSlot_(0) {
    memset(shelfCount_, 0, sizeof(shelfCount_));
    atlas_.pageSize = 64;
    atlas_.width = 2048;
    atlas_.height = 2048;
    ensurePageTableSize();
}

FrostVirtualTexturing::~FrostVirtualTexturing() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

bool FrostVirtualTexturing::init(u32 atlasSize) {
    atlasSize_ = atlasSize;
    pagesPerAtlas_ = atlasSize / VT_PAGE_SIZE;
    maxPagesPerAtlas_ = pagesPerAtlas_ * pagesPerAtlas_;

    // Initialize page table: enough for 16K x 16K virtual texture
    pageTableSize_ = VT_MAX_VIRTUAL_PAGES;
    legacyPageTable_.resize(pageTableSize_);

    // Initialize feedback buffer
    feedbackBuffer_.resize(VT_FEEDBACK_BUFFER_SIZE);
    feedbackCount_ = 0;

    // Initialize atlases
    activeAtlases_ = 0;

    totalVirtualPages_ = 0;
    legacyResidentPages_ = 0;
    streamingPages_ = 0;
    evictionCount_ = 0;

    initialized_ = true;
    return true;
}

void FrostVirtualTexturing::shutdown() {
    legacyPageTable_.clear();
    feedbackBuffer_.clear();
    textures_.clear();

    for (u32 i = 0; i < VT_MAX_ATLASES; i++) {
        atlases_[i].clear();
    }

    initialized_ = false;
}

void FrostVirtualTexturing::reset() {
    for (auto& entry : legacyPageTable_) {
        entry = VTPageTableEntry();
    }
    legacyResidentPages_ = 0;
    streamingPages_ = 0;
    evictionCount_ = 0;
    feedbackCount_ = 0;
}

// ============================================================================
// Texture Registration
// ============================================================================

u32 FrostVirtualTexturing::registerTexture(const VTTextureDesc& desc) {
    if (textureCount_ >= 256) return 0xFFFFFFFF;

    u32 id = textureCount_++;
    VTTextureDesc texDesc = desc;
    texDesc.textureID = id;
    textures_.push_back(texDesc);

    // Compute virtual pages for this texture
    u32 pagesX = (texDesc.virtualWidth + VT_PAGE_SIZE - 1) / VT_PAGE_SIZE;
    u32 pagesY = (texDesc.virtualHeight + VT_PAGE_SIZE - 1) / VT_PAGE_SIZE;
    totalVirtualPages_ += pagesX * pagesY * texDesc.mipLevels;

    return id;
}

// ============================================================================
// Feedback Pass — Record Which Pages Are Needed
// ============================================================================

void FrostVirtualTexturing::feedbackPass(const Vector<u32>& visiblePageAddresses,
                                          const Vector<f32>& coverages) {
    feedbackCount_ = 0;

    for (u32 i = 0; i < visiblePageAddresses.size() && i < feedbackBuffer_.size(); i++) {
        VTFeedbackEntry& entry = feedbackBuffer_[feedbackCount_];
        entry.pageAddress = visiblePageAddresses[i];
        entry.coverage = (i < coverages.size()) ? coverages[i] : 1.0f;
        entry.screenX = 0;
        entry.screenY = 0;
        feedbackCount_++;
    }
}

// ============================================================================
// Feedback Processing — Determine Which Pages to Stream
// ============================================================================

void FrostVirtualTexturing::processFeedback() {
    if (feedbackCount_ == 0) return;

    // Sort by priority (coverage = screen importance)
    sortFeedbackByPriority();

    // Merge duplicate entries
    mergeDuplicateFeedback();

    // Compute page priorities
    computePagePriorities();

    // Determine pages that need streaming
    u32 pagesNeeded = 0;
    for (u32 i = 0; i < feedbackCount_; i++) {
        u32 addr = feedbackBuffer_[i].pageAddress;
        VTPageTableEntry* entry = getPageEntryMut(addr);

        if (entry && !(entry->status & 1)) {  // Not loaded
            pagesNeeded++;
        }
    }

    // If we need more pages than available space, evict
    if (legacyResidentPages_ + pagesNeeded > maxPagesPerAtlas_ * VT_MAX_ATLASES) {
        evictPages(pagesNeeded);
    }

    // Stream needed pages
    streamPages();

    // Pack into atlas
    packPages();

    // Update page table
    updatePageTable();
}

void FrostVirtualTexturing::sortFeedbackByPriority() {
    // Sort feedback entries by coverage (descending)
    for (u32 i = 0; i < feedbackCount_ - 1; i++) {
        for (u32 j = i + 1; j < feedbackCount_; j++) {
            if (feedbackBuffer_[j].coverage > feedbackBuffer_[i].coverage) {
                VTFeedbackEntry temp = feedbackBuffer_[i];
                feedbackBuffer_[i] = feedbackBuffer_[j];
                feedbackBuffer_[j] = temp;
            }
        }
    }
}

void FrostVirtualTexturing::mergeDuplicateFeedback() {
    // Remove duplicate page addresses, keeping highest coverage
    Vector<u32> uniqueAddrs;
    Vector<f32> uniqueCoverages;

    for (u32 i = 0; i < feedbackCount_; i++) {
        bool found = false;
        for (u32 j = 0; j < uniqueAddrs.size(); j++) {
            if (uniqueAddrs[j] == feedbackBuffer_[i].pageAddress) {
                uniqueCoverages[j] = std::max(uniqueCoverages[j], feedbackBuffer_[i].coverage);
                found = true;
                break;
            }
        }

        if (!found) {
            uniqueAddrs.push_back(feedbackBuffer_[i].pageAddress);
            uniqueCoverages.push_back(feedbackBuffer_[i].coverage);
        }
    }

    feedbackCount_ = (u32)uniqueAddrs.size();
    for (u32 i = 0; i < feedbackCount_; i++) {
        feedbackBuffer_[i].pageAddress = uniqueAddrs[i];
        feedbackBuffer_[i].coverage = uniqueCoverages[i];
    }
}

void FrostVirtualTexturing::computePagePriorities() {
    for (u32 i = 0; i < feedbackCount_; i++) {
        u32 addr = feedbackBuffer_[i].pageAddress;
        VTPageTableEntry* entry = getPageEntryMut(addr);

        if (entry) {
            entry->priority = (u8)(feedbackBuffer_[i].coverage * 255.0f);
            entry->screenCoverage = feedbackBuffer_[i].coverage;
        }
    }
}

// ============================================================================
// Page Streaming
// ============================================================================

void FrostVirtualTexturing::streamPages() {
    for (u32 i = 0; i < feedbackCount_; i++) {
        u32 addr = feedbackBuffer_[i].pageAddress;
        VTPageTableEntry* entry = getPageEntryMut(addr);

        if (entry && !(entry->status & 1)) {  // Not loaded
            VTPageAddress pageAddr = computePageAddress(addr);
            markPageStreaming(addr);

            // Simulate loading from disk
            VTAtlasPage page;
            loadPageFromDisk(pageAddr, page);

            streamingPages_++;
        }
    }
}

void FrostVirtualTexturing::loadPageFromDisk(VTPageAddress addr, VTAtlasPage& page) {
    // Simulate loading a page from disk
    // In production, this would read from a virtual texture file

    // Generate procedural data for demo
    std::mt19937 rng(addr.tileX * 1000 + addr.tileY * 100 + addr.mipLevel);
    std::uniform_int_distribution<u32> dist(0, 255);

    for (u32 y = 0; y < VT_PAGE_SIZE; y++) {
        for (u32 x = 0; x < VT_PAGE_SIZE; x++) {
            u32 pixelIdx = (y * VT_PAGE_SIZE + x) * 4;

            // Procedural checkerboard pattern
            bool checker = ((x / 16) + (y / 16)) % 2 == 0;
            u32 baseColor = checker ? 128 : 64;

            page.data[pixelIdx + 0] = (u8)(baseColor + dist(rng) % 32);
            page.data[pixelIdx + 1] = (u8)(baseColor + dist(rng) % 32);
            page.data[pixelIdx + 2] = (u8)(baseColor + dist(rng) % 32);
            page.data[pixelIdx + 3] = 255;
        }
    }

    page.dirty = true;
}

void FrostVirtualTexturing::decompressPage(const u8* compressed, u8* output, u32 size) {
    // Simplified BC7 decompression
    // In production, would use actual BC7 block decompression

    u32 blockCount = size / 16;  // BC7: 16 bytes per 4x4 block

    for (u32 b = 0; b < blockCount; b++) {
        const u8* block = compressed + b * 16;
        u8* outBlock = output + b * 16;

        // Simple: just copy bytes (would decode BC7 properly in production)
        for (u32 i = 0; i < 16; i++) {
            outBlock[i] = block[i];
        }
    }
}

void FrostVirtualTexturing::generateMipData(const u8* src, u8* dst, u32 srcW, u32 srcH) {
    // Generate next mip level by 2x2 box filter
    u32 dstW = srcW / 2;
    u32 dstH = srcH / 2;

    for (u32 y = 0; y < dstH; y++) {
        for (u32 x = 0; x < dstW; x++) {
            u32 dstIdx = (y * dstW + x) * 4;

            f32 r = 0, g = 0, b = 0, a = 0;
            for (u32 dy = 0; dy < 2; dy++) {
                for (u32 dx = 0; dx < 2; dx++) {
                    u32 srcIdx = ((y * 2 + dy) * srcW + (x * 2 + dx)) * 4;
                    r += src[srcIdx + 0];
                    g += src[srcIdx + 1];
                    b += src[srcIdx + 2];
                    a += src[srcIdx + 3];
                }
            }

            dst[dstIdx + 0] = (u8)(r / 4.0f);
            dst[dstIdx + 1] = (u8)(g / 4.0f);
            dst[dstIdx + 2] = (u8)(b / 4.0f);
            dst[dstIdx + 3] = (u8)(a / 4.0f);
        }
    }
}

// ============================================================================
// Shelf Packing — Physical Atlas Management
// ============================================================================

void FrostVirtualTexturing::packPages() {
    for (u32 i = 0; i < feedbackCount_; i++) {
        u32 addr = feedbackBuffer_[i].pageAddress;
        VTPageTableEntry* entry = getPageEntryMut(addr);

        if (entry && !(entry->status & 1)) {  // Not yet resident
            VTAtlasLocation location;
            if (shelfPackPage(addr, location)) {
                markPageResident(addr, location);
                legacyResidentPages_++;
            }
        }
    }
}

bool FrostVirtualTexturing::shelfPackPage(u32 virtualAddress, VTAtlasLocation& result) {
    VTPageAddress pageAddr = computePageAddress(virtualAddress);

    // Try each atlas
    for (u32 a = 0; a < activeAtlases_ && a < VT_MAX_ATLASES; a++) {
        u32 shelfIdx = findBestShelf(VT_PAGE_SIZE, a);

        if (shelfIdx < shelfCount_[a]) {
            VTShelf& shelf = shelves_[a][shelfIdx];

            if (shelf.canFit(VT_PAGE_SIZE)) {
                result.atlasIndex = (u16)a;
                result.pageX = shelf.x;
                result.pageY = shelf.y;

                shelf.x += VT_PAGE_SIZE;
                shelf.width += VT_PAGE_SIZE;

                return true;
            }
        }
    }

    // Need new atlas
    if (activeAtlases_ < VT_MAX_ATLASES) {
        u32 newAtlas = activeAtlases_++;
        initAtlas(newAtlas);

        VTShelf& shelf = shelves_[newAtlas][0];
        result.atlasIndex = (u16)newAtlas;
        result.pageX = 0;
        result.pageY = 0;

        shelf.x = VT_PAGE_SIZE;
        shelf.width = VT_PAGE_SIZE;

        return true;
    }

    return false;
}

void FrostVirtualTexturing::initShelfAllocator(u32 atlasIndex) {
    shelfCount_[atlasIndex] = 0;

    // Initialize with one shelf covering the full atlas width
    VTShelf& shelf = shelves_[atlasIndex][0];
    shelf.x = 0;
    shelf.y = 0;
    shelf.width = 0;
    shelf.height = VT_PAGE_SIZE;
    shelf.maxHeight = atlasSize_;

    shelfCount_[atlasIndex] = 1;
}

u16 FrostVirtualTexturing::findBestShelf(u16 pageWidth, u32 atlasIndex) const {
    u16 bestShelf = 0;
    u16 bestFit = 0xFFFF;

    for (u16 i = 0; i < shelfCount_[atlasIndex]; i++) {
        const VTShelf& shelf = shelves_[atlasIndex][i];

        if (shelf.canFit(pageWidth)) {
            u16 remaining = (atlasSize_ / VT_PAGE_SIZE) * VT_PAGE_SIZE - shelf.x;
            if (remaining < bestFit) {
                bestFit = remaining;
                bestShelf = i;
            }
        }
    }

    return bestShelf;
}

// ============================================================================
// LRU Eviction
// ============================================================================

void FrostVirtualTexturing::evictPages(u32 pagesNeeded) {
    u32 pagesEvicted = 0;

    while (pagesEvicted < pagesNeeded && legacyResidentPages_ > 0) {
        u32 lruPage = findLRUPage();
        if (lruPage == 0xFFFFFFFF) break;

        evictSinglePage(lruPage);
        pagesEvicted++;
        evictionCount_++;
    }
}

u32 FrostVirtualTexturing::findLRUPage() const {
    f32 oldestTime = 1e30f;
    u32 oldestAddr = 0xFFFFFFFF;

    for (u32 i = 0; i < pageTableSize_; i++) {
        const VTPageTableEntry& entry = legacyPageTable_[i];
        if (entry.status & 1) {  // Resident
            if (entry.lastAccessTime < oldestTime) {
                oldestTime = entry.lastAccessTime;
                oldestAddr = i;
            }
        }
    }

    return oldestAddr;
}

void FrostVirtualTexturing::evictSinglePage(u32 virtualAddress) {
    VTPageTableEntry* entry = getPageEntryMut(virtualAddress);
    if (!entry) return;

    // Mark page as not resident
    entry->status &= ~1;
    entry->status &= ~4;
    legacyResidentPages_--;

    // In production, would free the atlas space
}

// ============================================================================
// Page Table Management
// ============================================================================

void FrostVirtualTexturing::updatePageTable() {
    // Update timestamps for accessed pages
    f32 currentTime = (f32)frameCount_;  // simplified

    for (u32 i = 0; i < feedbackCount_; i++) {
        u32 addr = feedbackBuffer_[i].pageAddress;
        VTPageTableEntry* entry = getPageEntryMut(addr);

        if (entry) {
            entry->lastAccessTime = currentTime;
            entry->refCount++;
        }
    }
}

const VTPageTableEntry* FrostVirtualTexturing::getPageEntry(u32 virtualAddress) const {
    if (virtualAddress >= pageTableSize_) return nullptr;
    return &legacyPageTable_[virtualAddress];
}

VTPageTableEntry* FrostVirtualTexturing::getPageEntryMut(u32 virtualAddress) {
    if (virtualAddress >= pageTableSize_) return nullptr;
    return &legacyPageTable_[virtualAddress];
}

VTAtlasLocation FrostVirtualTexturing::getPhysicalLocation(u32 virtualAddress) const {
    const VTPageTableEntry* entry = getPageEntry(virtualAddress);
    if (entry && (entry->status & 1)) {
        return entry->location;
    }
    return VTAtlasLocation();
}

void FrostVirtualTexturing::markPageResident(u32 virtualAddress, VTAtlasLocation location) {
    VTPageTableEntry* entry = getPageEntryMut(virtualAddress);
    if (entry) {
        entry->location = location;
        entry->status |= 1;   // loaded
        entry->status |= 4;   // resident
        entry->status &= ~2;  // not streaming
    }
}

void FrostVirtualTexturing::markPageStreaming(u32 virtualAddress) {
    VTPageTableEntry* entry = getPageEntryMut(virtualAddress);
    if (entry) {
        entry->status |= 2;  // streaming
    }
}

void FrostVirtualTexturing::markPageEvicted(u32 virtualAddress) {
    VTPageTableEntry* entry = getPageEntryMut(virtualAddress);
    if (entry) {
        entry->status &= ~1;
        entry->status &= ~4;
        entry->status &= ~2;
    }
}

// ============================================================================
// UV Transform
// ============================================================================

void FrostVirtualTexturing::computeUVTransform(u32 virtualAddress, f32& uOffset,
                                                f32& vOffset, f32& uScale,
                                                f32& vScale) const {
    VTPageTableEntry* entry = const_cast<FrostVirtualTexturing*>(this)->getPageEntryMut(virtualAddress);
    if (!entry || !(entry->status & 1)) {
        uOffset = 0; vOffset = 0; uScale = 0; vScale = 0;
        return;
    }

    VTAtlasLocation loc = entry->location;

    // Compute UV from atlas position
    uOffset = (f32)loc.pageX / (f32)atlasSize_;
    vOffset = (f32)loc.pageY / (f32)atlasSize_;
    uScale = (f32)VT_PAGE_SIZE / (f32)atlasSize_;
    vScale = (f32)VT_PAGE_SIZE / (f32)atlasSize_;
}

// ============================================================================
// Virtual Address Computation
// ============================================================================

u32 FrostVirtualTexturing::computeVirtualAddress(VTPageAddress addr) const {
    return addr.pack() % pageTableSize_;
}

VTPageAddress FrostVirtualTexturing::computePageAddress(u32 virtualAddress) const {
    return VTPageAddress::unpack(virtualAddress);
}

u32 FrostVirtualTexturing::computeMipAddress(u32 baseAddress, u32 mipLevel, u32 tileSize) const {
    VTPageAddress base = computePageAddress(baseAddress);
    base.mipLevel = (u8)mipLevel;

    // Adjust tile coordinates for mip level
    base.tileX >>= mipLevel;
    base.tileY >>= mipLevel;

    return computeVirtualAddress(base);
}

// ============================================================================
// Mip Streaming
// ============================================================================

void FrostVirtualTexturing::streamMipLevels(u32 virtualAddress, u32 targetMip) {
    // Load lower mips first, then upgrade
    for (u32 mip = targetMip; mip >= 0; mip--) {
        u32 mipAddr = computeMipAddress(virtualAddress, mip, VT_PAGE_SIZE);

        VTPageTableEntry* entry = getPageEntryMut(mipAddr);
        if (entry && !(entry->status & 1)) {
            // Stream this mip level
            VTPageAddress pageAddr = computePageAddress(mipAddr);
            VTAtlasPage page;
            loadPageFromDisk(pageAddr, page);

            // Pack into atlas
            VTAtlasLocation location;
            if (shelfPackPage(mipAddr, location)) {
                markPageResident(mipAddr, location);
            }
        }

        if (mip == 0) break;  // prevent underflow
    }
}

// ============================================================================
// Atlas Management
// ============================================================================

void FrostVirtualTexturing::initAtlas(u32 index) {
    if (index >= VT_MAX_ATLASES) return;

    atlases_[index].clear();

    // Initialize atlas pages
    u32 pageCount = pagesPerAtlas_ * pagesPerAtlas_;
    atlases_[index].resize(pageCount);

    initShelfAllocator(index);
}

bool FrostVirtualTexturing::allocateAtlasPage(VTAtlasLocation& result) {
    for (u32 a = 0; a < activeAtlases_ && a < VT_MAX_ATLASES; a++) {
        u32 shelfIdx = findBestShelf(VT_PAGE_SIZE, a);

        if (shelfIdx < shelfCount_[a]) {
            VTShelf& shelf = shelves_[a][shelfIdx];

            if (shelf.canFit(VT_PAGE_SIZE)) {
                result.atlasIndex = (u16)a;
                result.pageX = shelf.x;
                result.pageY = shelf.y;

                shelf.x += VT_PAGE_SIZE;
                shelf.width += VT_PAGE_SIZE;

                return true;
            }
        }
    }

    return false;
}

// ============================================================================
// Atlas Page Data
// ============================================================================

const VTAtlasPage& FrostVirtualTexturing::getAtlasPage(u32 atlasIndex, u32 pageX,
                                                         u32 pageY) const {
    static VTAtlasPage empty;
    if (atlasIndex >= VT_MAX_ATLASES) return empty;

    u32 pageIdx = pageY * pagesPerAtlas_ + pageX;
    if (pageIdx < atlases_[atlasIndex].size()) {
        return atlases_[atlasIndex][pageIdx];
    }

    return empty;
}

// ============================================================================
// Statistics
// ============================================================================

f32 FrostVirtualTexturing::atlasUtilization() const {
    u32 totalPages = activeAtlases_ * maxPagesPerAtlas_;
    if (totalPages == 0) return 0;
    return (f32)legacyResidentPages_ / (f32)totalPages;
}

// ============================================================================
// Advanced Virtual Texturing Operations
// ============================================================================

void FrostVirtualTexturing::compactAtlas() {
    // Defragment atlas by moving pages to fill gaps
    u32 writePage = 0;

    for (u32 a = 0; a < activeAtlases_; a++) {
        for (u32 p = 0; p < maxPagesPerAtlas_; p++) {
            u32 pageIdx = p;
            if (pageIdx < atlases_[a].size() && atlases_[a][pageIdx].dirty) {
                if (writePage != pageIdx) {
                    // Move page
                    atlases_[a][writePage] = atlases_[a][pageIdx];

                    // Update page table
                    for (u32 t = 0; t < pageTableSize_; t++) {
                        VTPageTableEntry& entry = legacyPageTable_[t];
                        if ((entry.status & 1) && entry.location.atlasIndex == a &&
                            entry.location.pageX == (pageIdx % pagesPerAtlas_) * VT_PAGE_SIZE &&
                            entry.location.pageY == (pageIdx / pagesPerAtlas_) * VT_PAGE_SIZE) {
                            entry.location.pageX = (writePage % pagesPerAtlas_) * VT_PAGE_SIZE;
                            entry.location.pageY = (writePage / pagesPerAtlas_) * VT_PAGE_SIZE;
                            break;
                        }
                    }
                }
                writePage++;
            }
        }
    }
}

void FrostVirtualTexturing::optimizeShelfPacking() {
    // Sort shelves by width utilization for better packing
    for (u32 a = 0; a < activeAtlases_; a++) {
        for (u32 i = 0; i < shelfCount_[a] - 1; i++) {
            for (u32 j = i + 1; j < shelfCount_[a]; j++) {
                if (shelves_[a][j].width > shelves_[a][i].width) {
                    VTShelf temp = shelves_[a][i];
                    shelves_[a][i] = shelves_[a][j];
                    shelves_[a][j] = temp;
                }
            }
        }
    }
}

f32 FrostVirtualTexturing::computeShelfUtilization(u32 atlasIndex) const {
    if (atlasIndex >= VT_MAX_ATLASES) return 0;

    f32 totalUsed = 0;
    f32 totalAvailable = (f32)atlasSize_ * (f32)atlasSize_;

    for (u32 i = 0; i < shelfCount_[atlasIndex]; i++) {
        totalUsed += (f32)shelves_[atlasIndex][i].width * (f32)shelves_[atlasIndex][i].height;
    }

    return totalUsed / totalAvailable;
}

// ============================================================================
// Page Table Analysis
// ============================================================================

u32 FrostVirtualTexturing::countResidentPages() const {
    u32 count = 0;
    for (u32 i = 0; i < pageTableSize_; i++) {
        if (legacyPageTable_[i].status & 1) count++;
    }
    return count;
}

u32 FrostVirtualTexturing::countStreamingPages() const {
    u32 count = 0;
    for (u32 i = 0; i < pageTableSize_; i++) {
        if (legacyPageTable_[i].status & 2) count++;
    }
    return count;
}

f32 FrostVirtualTexturing::computePageTableFragmentation() const {
    u32 usedSlots = 0;
    u32 freeSlots = 0;
    u32 maxFreeBlock = 0;
    u32 currentFreeBlock = 0;

    for (u32 i = 0; i < pageTableSize_; i++) {
        if (legacyPageTable_[i].status & 1) {
            usedSlots++;
            maxFreeBlock = std::max(maxFreeBlock, currentFreeBlock);
            currentFreeBlock = 0;
        } else {
            freeSlots++;
            currentFreeBlock++;
        }
    }
    maxFreeBlock = std::max(maxFreeBlock, currentFreeBlock);

    return maxFreeBlock > 0 ? (f32)freeSlots / (f32)maxFreeBlock : 0;
}

// ============================================================================
// Streaming Analysis
// ============================================================================

f32 FrostVirtualTexturing::computeStreamingBandwidth() const {
    // Estimate streaming bandwidth needed
    u32 streamingCount = countStreamingPages();
    f32 bytesPerPage = (f32)(VT_PAGE_SIZE * VT_PAGE_SIZE * 4);  // RGBA
    return (f32)streamingCount * bytesPerPage / (1024.0f * 1024.0f);  // MB
}

f32 FrostVirtualTexturing::computeCacheHitRate() const {
    u32 hits = 0;
    u32 misses = 0;

    for (u32 i = 0; i < feedbackCount_; i++) {
        u32 addr = feedbackBuffer_[i].pageAddress;
        const VTPageTableEntry* entry = getPageEntry(addr);

        if (entry && (entry->status & 1)) {
            hits++;
        } else {
            misses++;
        }
    }

    u32 total = hits + misses;
    return total > 0 ? (f32)hits / (f32)total : 0;
}

// ============================================================================
// Mip Level Analysis
// ============================================================================

u32 FrostVirtualTexturing::computeLoadedMipLevels() const {
    u32 maxMip = 0;
    for (u32 i = 0; i < pageTableSize_; i++) {
        if (legacyPageTable_[i].status & 1) {
            VTPageAddress addr = computePageAddress(i);
            maxMip = std::max(maxMip, (u32)addr.mipLevel);
        }
    }
    return maxMip + 1;
}

f32 FrostVirtualTexturing::computeMipLevelDistribution() const {
    Vector<u32> mipCounts;
    mipCounts.resize(VT_MIP_LEVELS, 0);

    for (u32 i = 0; i < pageTableSize_; i++) {
        if (legacyPageTable_[i].status & 1) {
            VTPageAddress addr = computePageAddress(i);
            if (addr.mipLevel < VT_MIP_LEVELS) {
                mipCounts[addr.mipLevel]++;
            }
        }
    }

    // Return entropy of mip distribution
    f32 entropy = 0;
    u32 total = legacyResidentPages_;
    if (total == 0) return 0;

    for (u32 i = 0; i < mipCounts.size(); i++) {
        if (mipCounts[i] > 0) {
            f32 p = (f32)mipCounts[i] / (f32)total;
            entropy -= p * log2f(p + 0.0001f);
        }
    }

    return entropy;
}

// ============================================================================
// Texture Statistics
// ============================================================================

u32 FrostVirtualTexturing::getTotalTextureMemory() const {
    u32 memory = 0;
    for (u32 i = 0; i < textureCount_; i++) {
        const VTTextureDesc& desc = textures_[i];
        u32 pagesX = (desc.virtualWidth + VT_PAGE_SIZE - 1) / VT_PAGE_SIZE;
        u32 pagesY = (desc.virtualHeight + VT_PAGE_SIZE - 1) / VT_PAGE_SIZE;
        memory += pagesX * pagesY * VT_PAGE_SIZE * VT_PAGE_SIZE * 4;
    }
    return memory;
}

u32 FrostVirtualTexturing::getAtlasMemoryUsage() const {
    return activeAtlases_ * atlasSize_ * atlasSize_ * 4;
}

f32 FrostVirtualTexturing::computeMemoryEfficiency() const {
    u32 virtualMem = getTotalTextureMemory();
    u32 physicalMem = getAtlasMemoryUsage();
    return physicalMem > 0 ? (f32)virtualMem / (f32)physicalMem : 0;
}

// ============================================================================
// Debug and Visualization
// ============================================================================

void FrostVirtualTexturing::getStats(u32& resident, u32& streaming,
                                       u32& total, f32& utilization) const {
    resident = legacyResidentPages_;
    streaming = streamingPages_;
    total = totalVirtualPages_;
    utilization = atlasUtilization();
}

Vector<Vec3> FrostVirtualTexturing::getAtlasVisualization() const {
    Vector<Vec3> visualization;
    visualization.resize(atlasSize_ * atlasSize_);

    for (u32 a = 0; a < activeAtlases_; a++) {
        for (u32 p = 0; p < maxPagesPerAtlas_; p++) {
            u32 pageX = p % pagesPerAtlas_;
            u32 pageY = p / pagesPerAtlas_;

            Vec3 color(0.2f);  // empty page

            // Check if page is used
            for (u32 t = 0; t < pageTableSize_; t++) {
                const VTPageTableEntry& entry = legacyPageTable_[t];
                if ((entry.status & 1) && entry.location.atlasIndex == a) {
                    u32 px = entry.location.pageX;
                    u32 py = entry.location.pageY;
                    if (px == pageX * VT_PAGE_SIZE && py == pageY * VT_PAGE_SIZE) {
                        color = Vec3(0.8f, 0.2f, 0.2f);  // used page
                        break;
                    }
                }
            }

            // Fill page region
            for (u32 y = 0; y < VT_PAGE_SIZE; y++) {
                for (u32 x = 0; x < VT_PAGE_SIZE; x++) {
                    u32 vizX = a * atlasSize_ + pageX * VT_PAGE_SIZE + x;
                    u32 vizY = pageY * VT_PAGE_SIZE + y;
                    if (vizX < atlasSize_ && vizY < atlasSize_) {
                        visualization[vizY * atlasSize_ + vizX] = color;
                    }
                }
            }
        }
    }

    return visualization;
}

// ============================================================================
// Page Feedback + Streaming Pipeline
// ============================================================================

void FrostVirtualTexturing::setConfig(const Config& config) {
    config_ = config;
    feedbackQueueCapacity_ = config_.feedbackQueueCapacity;
    pageTtlFrames_ = config_.pageTtlFrames;
    if (atlas_.pagesPerRow == 0 || atlas_.pageSize != config_.pageSize ||
        atlas_.width != config_.atlasWidth || atlas_.height != config_.atlasHeight) {
        initializeAtlas(config_.atlasWidth, config_.atlasHeight, config_.pageSize);
    }
}

void FrostVirtualTexturing::initializeAtlas(u32 width, u32 height, u32 pageSize) {
    atlas_.width = width ? width : config_.atlasWidth;
    atlas_.height = height ? height : config_.atlasHeight;
    atlas_.pageSize = pageSize ? pageSize : config_.pageSize;
    atlas_.pagesPerRow = atlas_.pageSize > 0 ? atlas_.width / atlas_.pageSize : 0;
    atlas_.pagesPerCol = atlas_.pageSize > 0 ? atlas_.height / atlas_.pageSize : 0;
    atlasSlots_.resize(atlas_.pagesPerRow * atlas_.pagesPerCol, 0);
    nextFreeSlot_ = 0;
}

void FrostVirtualTexturing::captureFeedback(u32 viewportW, u32 viewportH,
                                            const f32* feedbackBuffer,
                                            u32 bufferWidth, u32 bufferHeight) {
    feedbackQueue_.clear();
    streamingStats_.feedbackEntries = 0;
    if (!config_.enableFeedback || !feedbackBuffer) return;
    if (feedbackQueueCapacity_ == 0 || bufferWidth == 0 || bufferHeight == 0) return;

    // Downsample stride when the feedback buffer is larger than the viewport
    u32 stepX = (viewportW > 0 && bufferWidth > viewportW)
                    ? (bufferWidth + viewportW - 1) / viewportW : 1;
    u32 stepY = (viewportH > 0 && bufferHeight > viewportH)
                    ? (bufferHeight + viewportH - 1) / viewportH : 1;

    HashMap<u64, u32> seen;
    for (u32 y = 0; y < bufferHeight && feedbackQueue_.size() < feedbackQueueCapacity_; y += stepY) {
        for (u32 x = 0; x < bufferWidth && feedbackQueue_.size() < feedbackQueueCapacity_; x += stepX) {
            f32 encoded = feedbackBuffer[y * bufferWidth + x];
            u32 bits = 0;
            memcpy(&bits, &encoded, sizeof(u32));

            u32 pageX = bits & 0xFFFFu;
            u32 pageY = (bits >> 16) & 0xFFFFu;
            u32 mipLevel = (bits >> 24) & 0xFFu;

            u64 key = pageKey(pageX, pageY, mipLevel);
            if (seen.contains(key)) continue;
            seen[key] = (u32)feedbackQueue_.size();

            feedbackQueue_.push_back(FeedbackEntry(pageX, pageY, mipLevel, mipLevel));
        }
    }

    streamingStats_.feedbackEntries = (u32)feedbackQueue_.size();
}

void FrostVirtualTexturing::requestPage(u32 pageX, u32 pageY, u32 mipLevel, u32 priority) {
    u64 key = pageKey(pageX, pageY, mipLevel);
    HashMap<u64, u32>::Iterator it = pageIndex_.find(key);
    if (it != pageIndex_.end()) {
        VirtualPage& page = virtualPages_[it.value()];
        page.priority = std::max(page.priority, priority);
        page.lastRequestFrame = frameCount_;
        return;
    }

    virtualPages_.push_back(VirtualPage(pageX, pageY, mipLevel, priority));
    VirtualPage& page = virtualPages_.back();
    page.lastRequestFrame = frameCount_;
    pageIndex_[key] = (u32)(virtualPages_.size() - 1);
    streamingStats_.pagesRequested++;
}

u32 FrostVirtualTexturing::resolvePendingRequests(u32 maxRequestsPerFrame, u32 frameIndex) {
    if (atlas_.pagesPerRow == 0 || atlas_.pagesPerCol == 0) initializeAtlas(0, 0, 0);

    u32 resolved = 0;
    for (u32 i = 0; i < maxRequestsPerFrame; i++) {
        i32 best = -1;
        for (u32 p = 0; p < virtualPages_.size(); p++) {
            const VirtualPage& page = virtualPages_[p];
            if (page.resident) continue;
            if (best < 0 ||
                page.priority < virtualPages_[(u32)best].priority ||
                (page.priority == virtualPages_[(u32)best].priority &&
                 page.lastRequestFrame < virtualPages_[(u32)best].lastRequestFrame)) {
                best = (i32)p;
            }
        }
        if (best < 0) break;

        i32 slot = findFreeAtlasSlot();
        if (slot < 0) break;

        VirtualPage& page = virtualPages_[(u32)best];
        u32 slotX = (u32)slot % atlas_.pagesPerRow;
        u32 slotY = (u32)slot / atlas_.pagesPerRow;
        page.atlasX = slotX * atlas_.pageSize;
        page.atlasY = slotY * atlas_.pageSize;
        page.resident = true;
        page.lastRequestFrame = frameIndex;
        atlasSlots_[(usize)slot] = 1;
        streamingStats_.pagesResident++;
        streamingStats_.bytesStreamed += (u64)atlas_.pageSize * (u64)atlas_.pageSize * 4u;
        resolved++;
    }

    return resolved;
}

i32 FrostVirtualTexturing::findFreeAtlasSlot() const {
    if (atlasSlots_.empty()) return -1;
    for (u32 i = 0; i < atlasSlots_.size(); i++) {
        u32 slot = (nextFreeSlot_ + i) % (u32)atlasSlots_.size();
        if (atlasSlots_[slot] == 0) return (i32)slot;
    }
    return -1;
}

void FrostVirtualTexturing::atlasSlotForPage(u32 pageIndex, u32& x, u32& y) const {
    x = 0;
    y = 0;
    if (pageIndex >= virtualPages_.size() || atlas_.pageSize == 0) return;
    const VirtualPage& page = virtualPages_[pageIndex];
    if (!page.resident) return;
    x = page.atlasX / atlas_.pageSize;
    y = page.atlasY / atlas_.pageSize;
}

void FrostVirtualTexturing::packPage(u32 pageIndex) {
    if (pageIndex >= virtualPages_.size() || atlas_.pageSize == 0) return;
    if (atlas_.pagesPerRow == 0 || atlas_.pagesPerCol == 0) return;

    VirtualPage& page = virtualPages_[pageIndex];
    u32 slotX = page.atlasX / atlas_.pageSize;
    u32 slotY = page.atlasY / atlas_.pageSize;
    if (slotX >= atlas_.pagesPerRow || slotY >= atlas_.pagesPerCol) return;

    page.atlasX = slotX * atlas_.pageSize;
    page.atlasY = slotY * atlas_.pageSize;
    page.resident = true;
    atlasSlots_[slotY * atlas_.pagesPerRow + slotX] = 1;
}

void FrostVirtualTexturing::updateStreaming(f32 dt, u32 frameIndex, u32 maxRequestsPerFrame) {
    frameCount_ = frameIndex;
    streamingStats_.feedbackEntries = (u32)feedbackQueue_.size();

    resolvePendingRequests(maxRequestsPerFrame, frameIndex);

    if (pageTtlFrames_ == 0 || atlas_.pageSize == 0) return;

    bool evictedAny = true;
    while (evictedAny) {
        evictedAny = false;
        u32 lru = 0xFFFFFFFFu;
        u64 oldest = 0xFFFFFFFFFFFFFFFFull;

        for (u32 i = 0; i < virtualPages_.size(); i++) {
            const VirtualPage& page = virtualPages_[i];
            if (!page.resident) continue;
            if (page.lastRequestFrame > frameIndex) continue;
            if (frameIndex - page.lastRequestFrame <= pageTtlFrames_) continue;
            if (page.lastRequestFrame < oldest) {
                oldest = page.lastRequestFrame;
                lru = i;
            }
        }

        if (lru == 0xFFFFFFFFu) break;

        VirtualPage& page = virtualPages_[lru];
        u32 slotX = atlas_.pageSize > 0 ? page.atlasX / atlas_.pageSize : 0;
        u32 slotY = atlas_.pageSize > 0 ? page.atlasY / atlas_.pageSize : 0;
        if (slotX < atlas_.pagesPerRow && slotY < atlas_.pagesPerCol) {
            atlasSlots_[slotY * atlas_.pagesPerRow + slotX] = 0;
            nextFreeSlot_ = slotY * atlas_.pagesPerRow + slotX;
        }
        page.resident = false;
        streamingStats_.pagesResident--;
        streamingStats_.pagesEvicted++;
        evictedAny = true;
    }
}

u32 FrostVirtualTexturing::getPendingRequestCount() const {
    u32 count = 0;
    for (u32 i = 0; i < virtualPages_.size(); i++) {
        if (!virtualPages_[i].resident) count++;
    }
    return count;
}

void FrostVirtualTexturing::resetStreaming() {
    feedbackQueue_.clear();
    virtualPages_.clear();
    pageIndex_.clear();
    atlasSlots_.clear();
    atlas_ = VirtualAtlas();
    atlas_.pageSize = 64;
    nextFreeSlot_ = 0;
    streamingStats_ = StreamingStats();
}

// ============================================================================
// Hardened Page Cache — LRU Tile Residency + Feedback Request Queue
// ============================================================================

u32 FrostVirtualTexturing::virtualTileCount() const {
    u32 grid = pageSize_ > 0 ? (virtualResolution_ + pageSize_ - 1) / pageSize_ : 0;
    u32 total = 0;
    for (u32 mip = 0; mip < VT_MIP_LEVELS && grid > 0; mip++) {
        total += grid * grid;
        if (grid == 1) break;
        grid = (grid + 1) / 2;
    }
    return total;
}

void FrostVirtualTexturing::ensurePageTableSize() {
    usize needed = (usize)virtualTileCount();
    if (pageTable_.size() == needed) return;

    for (usize i = residentPages_.size(); i > 0; i--) {
        usize idx = i - 1;
        if (residentPages_[idx] >= needed) {
            residentPages_.erase(idx);
            if (residentCount_ > 0) residentCount_--;
        }
    }

    for (usize i = pageRequests_.size(); i > 0; i--) {
        usize idx = i - 1;
        if (pageRequests_[idx].tileIndex >= needed) {
            pageRequests_.erase(idx);
        }
    }

    pageTable_.resize(needed);
}

u32 FrostVirtualTexturing::findFreePhysicalSlot() const {
    u32 totalSlots = pagesPerRow_ * pagesPerColumn_;
    for (u32 slot = 0; slot < totalSlots; slot++) {
        u32 slotX = slot % pagesPerRow_;
        u32 slotY = slot / pagesPerRow_;
        bool used = false;
        for (usize i = 0; i < residentPages_.size(); i++) {
            const PageTableEntry& entry = pageTable_[residentPages_[i]];
            if (entry.physicalX == slotX && entry.physicalY == slotY) {
                used = true;
                break;
            }
        }
        if (!used) return slot;
    }
    return 0xFFFFFFFF;
}

u32 FrostVirtualTexturing::allocatePage(u32 pageX, u32 pageY, u32 mip, u32 tile) {
    (void)pageX;
    (void)pageY;
    (void)mip;

    ensurePageTableSize();
    if (tile >= pageTable_.size()) return 0xFFFFFFFF;

    PageTableEntry& entry = pageTable_[tile];
    if (entry.resident) {
        entry.lastUsedFrame = frameCount_;
        return entry.physicalY * pagesPerRow_ + entry.physicalX;
    }

    if (residentCount_ >= maxResidentPages_) {
        evictLeastRecentlyUsed();
    }

    u32 slot = findFreePhysicalSlot();
    if (slot == 0xFFFFFFFF) return 0xFFFFFFFF;

    entry.physicalX = slot % pagesPerRow_;
    entry.physicalY = slot / pagesPerRow_;
    entry.resident = true;
    entry.loading = false;
    entry.lastUsedFrame = frameCount_;

    residentPages_.push_back(tile);
    residentCount_++;
    uploadedPages_++;

    return slot;
}

void FrostVirtualTexturing::freePage(u32 tile) {
    if (tile >= pageTable_.size()) return;
    PageTableEntry& entry = pageTable_[tile];
    if (!entry.resident) return;

    entry.resident = false;
    entry.loading = false;

    for (usize i = 0; i < residentPages_.size(); i++) {
        if (residentPages_[i] == tile) {
            residentPages_.erase(i);
            break;
        }
    }

    if (residentCount_ > 0) residentCount_--;
}

const PageTableEntry* FrostVirtualTexturing::getPageTableEntry(u32 tile) const {
    if (tile >= pageTable_.size()) return nullptr;
    return &pageTable_[tile];
}

void FrostVirtualTexturing::requestPage(u32 pageX, u32 pageY, u32 mip, u32 tile, u32 priority) {
    ensurePageTableSize();
    if (tile >= pageTable_.size()) return;

    for (usize i = 0; i < pageRequests_.size(); i++) {
        if (pageRequests_[i].tileIndex == tile) {
            if (priority > pageRequests_[i].priority) {
                pageRequests_[i].priority = priority;
            }
            return;
        }
    }

    pageRequests_.push_back(PageRequest(pageX, pageY, mip, tile, priority));
}

u32 FrostVirtualTexturing::processRequests(u32 maxPerFrame) {
    if (maxPerFrame == 0 || pageRequests_.empty()) return 0;

    for (usize i = 0; i < pageRequests_.size(); i++) {
        usize best = i;
        for (usize j = i + 1; j < pageRequests_.size(); j++) {
            if (pageRequests_[j].priority > pageRequests_[best].priority) {
                best = j;
            }
        }
        if (best != i) {
            PageRequest tmp = pageRequests_[i];
            pageRequests_[i] = pageRequests_[best];
            pageRequests_[best] = tmp;
        }
    }

    u32 count = maxPerFrame < pageRequests_.size() ? maxPerFrame : (u32)pageRequests_.size();
    u32 processed = 0;

    for (u32 i = 0; i < count; i++) {
        const PageRequest& req = pageRequests_[i];
        bool hit = req.tileIndex < pageTable_.size() && pageTable_[req.tileIndex].resident;
        u32 slot = allocatePage(req.pageX, req.pageY, req.mipLevel, req.tileIndex);
        if (slot != 0xFFFFFFFF) processed++;
        updateCacheStats(hit);
    }

    pageRequests_.erase(0, (usize)count);
    return processed;
}

u32 FrostVirtualTexturing::evictLeastRecentlyUsed() {
    if (residentPages_.empty()) return 0xFFFFFFFF;

    u32 oldestFrame = 0xFFFFFFFF;
    u32 victimTile = 0xFFFFFFFF;
    usize victimIndex = 0;

    for (usize i = 0; i < residentPages_.size(); i++) {
        u32 tile = residentPages_[i];
        const PageTableEntry& entry = pageTable_[tile];
        if (entry.lastUsedFrame < oldestFrame) {
            oldestFrame = entry.lastUsedFrame;
            victimTile = tile;
            victimIndex = i;
        }
    }

    if (victimTile == 0xFFFFFFFF) return 0xFFFFFFFF;

    PageTableEntry& entry = pageTable_[victimTile];
    entry.resident = false;
    entry.loading = false;

    residentPages_.erase(victimIndex);
    if (residentCount_ > 0) residentCount_--;

    return victimTile;
}

void FrostVirtualTexturing::updateCacheStats(bool hit) {
    if (hit) {
        cacheHitCount_++;
    } else {
        cacheMissCount_++;
    }
}

void FrostVirtualTexturing::resetStats() {
    uploadedPages_ = 0;
    cacheHitCount_ = 0;
    cacheMissCount_ = 0;
}

void FrostVirtualTexturing::setPageSize(u32 pageSize) {
    if (pageSize == 0) return;
    pageSize_ = pageSize;
    ensurePageTableSize();
}

void FrostVirtualTexturing::setVirtualResolution(u32 resolution) {
    if (resolution == 0) return;
    virtualResolution_ = resolution;
    ensurePageTableSize();
}

void FrostVirtualTexturing::setMaxResidentPages(u32 maxResident) {
    if (maxResident == 0) return;
    maxResidentPages_ = maxResident;
    while (residentCount_ > maxResidentPages_) {
        evictLeastRecentlyUsed();
    }
}

} // namespace Frost
