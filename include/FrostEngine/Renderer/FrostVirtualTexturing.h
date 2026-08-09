#pragma once

// ============================================================================
// FrostEngine FrostVirtualTexturing — Virtual Texturing System
// ============================================================================
// Proprietary feedback-driven virtual texturing with streaming compression.
// Uses giant virtual textures (16K+) divided into pages, with feedback-driven
// streaming, BC7 compression, and shelf packing for physical atlas management.
// ============================================================================

#include "Core/Types.h"
#include "Core/Vec3.h"
#include "Core/Math.h"
#include "Core/Vector.h"

namespace Frost {

// ============================================================================
// Virtual texturing structures
// ============================================================================

static constexpr u32 VT_MAX_VIRTUAL_PAGES = 16 * 16 * 1024;  // 16K x 16K virtual
static constexpr u32 VT_PAGE_SIZE = 128;
static constexpr u32 VT_ATLAS_SIZE = 4096;
static constexpr u32 VT_MAX_ATLASES = 16;
static constexpr u32 VT_MIP_LEVELS = 8;
static constexpr u32 VT_FEEDBACK_BUFFER_SIZE = 1024 * 1024;  // 1M entries

// Virtual page address (packed)
struct VTPageAddress {
    u16 tileX;          // tile X in virtual texture
    u16 tileY;          // tile Y in virtual texture
    u8 mipLevel;        // mip level
    u8 textureID;       // which virtual texture

    VTPageAddress() : tileX(0), tileY(0), mipLevel(0), textureID(0) {}

    u32 pack() const {
        return ((u32)textureID << 24) | ((u32)mipLevel << 16) |
               ((u32)tileY << 8) | (u32)tileX;
    }

    static VTPageAddress unpack(u32 packed) {
        VTPageAddress a;
        a.tileX = (u16)(packed & 0xFF);
        a.tileY = (u16)((packed >> 8) & 0xFF);
        a.mipLevel = (u8)((packed >> 16) & 0xFF);
        a.textureID = (u8)((packed >> 24) & 0xFF);
        return a;
    }
};

// Physical atlas location
struct VTAtlasLocation {
    u16 atlasIndex;     // which physical atlas
    u16 pageX;          // page X in atlas
    u16 pageY;          // page Y in atlas
    u16 padding;

    VTAtlasLocation() : atlasIndex(0), pageX(0), pageY(0), padding(0) {}

    bool isValid() const { return atlasIndex < VT_MAX_ATLASES; }
};

// Page table entry
struct VTPageTableEntry {
    VTAtlasLocation location;       // physical location
    u8 status;                      // bit 0: loaded, bit 1: streaming, bit 2: resident
    u8 priority;                    // streaming priority
    u16 refCount;                   // reference count
    f32 lastAccessTime;             // for LRU eviction
    f32 screenCoverage;             // screen space coverage

    VTPageTableEntry() : status(0), priority(0), refCount(0),
                         lastAccessTime(0), screenCoverage(0) {}
};

// Shelf allocator for physical atlas packing
struct VTShelf {
    u16 x;              // current x position
    u16 y;              // shelf y position
    u16 width;          // shelf width used
    u16 height;         // shelf height (page size)
    u16 maxHeight;      // max height available

    VTShelf() : x(0), y(0), width(0), height(VT_PAGE_SIZE), maxHeight(0) {}

    bool canFit(u16 pageWidth) const {
        return (x + pageWidth) <= (VT_ATLAS_SIZE / VT_PAGE_SIZE) * VT_PAGE_SIZE;
    }
};

// Feedback buffer entry
struct VTFeedbackEntry {
    u32 pageAddress;    // packed virtual page address
    u16 screenX;        // screen position (for priority)
    u16 screenY;
    f32 coverage;       // screen coverage

    VTFeedbackEntry() : pageAddress(0), screenX(0), screenY(0), coverage(0) {}
};

// Physical atlas page data (BC7 compressed)
struct VTAtlasPage {
    u8 data[VT_PAGE_SIZE * VT_PAGE_SIZE * 4];  // RGBA (BC7 compressed in practice)
    u16 pageX;
    u16 pageY;
    bool dirty;

    VTAtlasPage() : pageX(0), pageY(0), dirty(false) {
        memset(data, 0, sizeof(data));
    }
};

// Virtual texture descriptor
struct VTTextureDesc {
    u32 virtualWidth;       // virtual texture width in pixels
    u32 virtualHeight;      // virtual texture height in pixels
    u32 tileSize;           // tile size in pixels
    u32 mipLevels;          // number of mip levels
    u32 textureID;          // unique ID
    bool streamed;          // whether this texture is streamed

    VTTextureDesc() : virtualWidth(4096), virtualHeight(4096),
                      tileSize(VT_PAGE_SIZE), mipLevels(VT_MIP_LEVELS),
                      textureID(0), streamed(true) {}
};

// ============================================================================
// Main FrostVirtualTexturing system
// ============================================================================

class FrostVirtualTexturing {
public:
    FrostVirtualTexturing();
    ~FrostVirtualTexturing();

    bool init(u32 atlasSize = VT_ATLAS_SIZE);
    void shutdown();
    void reset();

    // Register a virtual texture
    u32 registerTexture(const VTTextureDesc& desc);

    // Feedback pass: record which pages are needed
    void feedbackPass(const Vector<u32>& visiblePageAddresses,
                      const Vector<f32>& coverages);

    // Process feedback: determine which pages to stream
    void processFeedback();

    // Stream pages from disk (simulated)
    void streamPages();

    // Pack pages into physical atlas using shelf algorithm
    void packPages();

    // Update page table
    void updatePageTable();

    // Evict least-used pages when atlas is full
    void evictPages(u32 pagesNeeded);

    // Get page table entry for a virtual address
    const VTPageTableEntry* getPageEntry(u32 virtualAddress) const;
    VTPageTableEntry* getPageEntryMut(u32 virtualAddress);

    // Get physical atlas location for a page
    VTAtlasLocation getPhysicalLocation(u32 virtualAddress) const;

    // Compute UV transform from virtual to physical
    void computeUVTransform(u32 virtualAddress, f32& uOffset, f32& vOffset,
                            f32& uScale, f32& vScale) const;

    // Get atlas page data
    const VTAtlasPage& getAtlasPage(u32 atlasIndex, u32 pageX, u32 pageY) const;

    // Mip streaming: load lower mips first, upgrade
    void streamMipLevels(u32 virtualAddress, u32 targetMip);

    // Statistics
    u32 totalVirtualPages() const { return totalVirtualPages_; }
    u32 residentPages() const { return residentPages_; }
    u32 streamingPages() const { return streamingPages_; }
    u32 evictionCount() const { return evictionCount_; }
    f32 atlasUtilization() const;
    f32 lastProcessTimeMs() const { return lastProcessTimeMs_; }

private:
    // Shelf packing
    bool shelfPackPage(u32 virtualAddress, VTAtlasLocation& result);
    void initShelfAllocator(u32 atlasIndex);
    u16 findBestShelf(u16 pageWidth, u32 atlasIndex) const;

    // Atlas compaction and optimization
    void compactAtlas();
    void optimizeShelfPacking();
    f32 computeShelfUtilization(u32 atlasIndex) const;

    // Page table analysis
    u32 countResidentPages() const;
    u32 countStreamingPages() const;
    f32 computePageTableFragmentation() const;

    // Streaming analysis
    f32 computeStreamingBandwidth() const;
    f32 computeCacheHitRate() const;

    // Mip level analysis
    u32 computeLoadedMipLevels() const;
    f32 computeMipLevelDistribution() const;

    // Statistics (extended)
    u32 getTotalTextureMemory() const;
    u32 getAtlasMemoryUsage() const;
    f32 computeMemoryEfficiency() const;
    void getStats(u32& resident, u32& streaming, u32& total, f32& utilization) const;
    Vector<Vec3> getAtlasVisualization() const;

    // Page streaming
    void loadPageFromDisk(VTPageAddress addr, VTAtlasPage& page);
    void decompressPage(const u8* compressed, u8* output, u32 size);
    void generateMipData(const u8* src, u8* dst, u32 srcW, u32 srcH);

    // Feedback processing
    void sortFeedbackByPriority();
    void mergeDuplicateFeedback();
    void computePagePriorities();

    // LRU eviction
    u32 findLRUPage() const;
    void evictSinglePage(u32 virtualAddress);

    // Page table management
    void markPageResident(u32 virtualAddress, VTAtlasLocation location);
    void markPageStreaming(u32 virtualAddress);
    void markPageEvicted(u32 virtualAddress);

    // Virtual address computation
    u32 computeVirtualAddress(VTPageAddress addr) const;
    VTPageAddress computePageAddress(u32 virtualAddress) const;
    u32 computeMipAddress(u32 baseAddress, u32 mipLevel, u32 tileSize) const;

    // Atlas management
    void initAtlas(u32 index);
    bool allocateAtlasPage(VTAtlasLocation& result);

    // Configuration
    u32 atlasSize_;
    u32 pagesPerAtlas_;
    u32 maxPagesPerAtlas_;

    // Page table
    Vector<VTPageTableEntry> pageTable_;
    u32 pageTableSize_;

    // Physical atlases
    Vector<VTAtlasPage> atlases_[VT_MAX_ATLASES];
    VTShelf shelves_[VT_MAX_ATLASES][128];  // shelves per atlas
    u32 shelfCount_[VT_MAX_ATLASES];
    u32 activeAtlases_;

    // Feedback buffer
    Vector<VTFeedbackEntry> feedbackBuffer_;
    u32 feedbackCount_;

    // Texture registry
    Vector<VTTextureDesc> textures_;
    u32 textureCount_;

    // Statistics
    u32 totalVirtualPages_;
    u32 residentPages_;
    u32 streamingPages_;
    u32 evictionCount_;
    f32 lastProcessTimeMs_;
    u32 frameCount_;

    bool initialized_;
};

} // namespace Frost
