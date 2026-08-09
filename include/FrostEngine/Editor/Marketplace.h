#pragma once

// ============================================================================
// FrostEngine Marketplace — Asset store for sharing/purchasing content
// ============================================================================
// Framework for a built-in marketplace where developers can:
//   - Browse and download assets (textures, meshes, scripts, materials)
//   - Upload and publish their own assets
//   - Rate and review assets
//   - Manage a local library of downloaded assets
//   - License tracking (free, CC0, MIT, commercial)
// ============================================================================

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include "Core/Log.h"

namespace Frost {

enum class AssetLicense : u8 {
    Free = 0,       // Completely free, no restrictions
    CC0,            // Public domain
    MIT,            // MIT license
    CC_BY,          // Attribution required
    CC_BY_SA,       // Attribution + share-alike
    Commercial,     // Paid license
    Custom,
};

enum class AssetCategory : u8 {
    Mesh = 0,
    Texture,
    Material,
    Script,
    Shader,
    Audio,
    Animation,
    Prefab,
    Scene,
    Plugin,
    Template,
    COUNT
};

struct MarketplaceAsset {
    u64 id;
    String name;
    String description;
    String author;
    String version;
    AssetCategory category;
    AssetLicense license;
    f32 rating;             // 0-5 stars
    u32 downloadCount;
    u32 reviewCount;
    u64 fileSizeBytes;
    f32 price;              // 0 = free
    Vector<String> tags;
    Vector<String> dependencies;
    String previewImagePath;
    bool installed;
    bool updateAvailable;
    String localPath;
    String remoteURL;
};

struct MarketplaceReview {
    u64 assetID;
    String author;
    f32 rating;
    String comment;
    u64 timestamp;
    bool helpful;
};

struct MarketplaceCategory {
    AssetCategory category;
    String name;
    String description;
    u32 assetCount;
    f32 totalDownloads;
};

class Marketplace {
public:
    bool init(const char* cachePath = nullptr) {
        cachePath_ = cachePath ? cachePath : ".frostmarket";
        loadLocalLibrary();
        return true;
    }

    // ---- Browse ----
    u32 searchByName(const char* query, Vector<u32>& results) const {
        results.clear();
        for (u32 i = 0; i < catalog_.size(); i++) {
            if (catalog_[i].name.find(query) != String::npos) {
                results.pushBack(i);
            }
        }
        return (u32)results.size();
    }

    u32 searchByCategory(AssetCategory cat, Vector<u32>& results) const {
        results.clear();
        for (u32 i = 0; i < catalog_.size(); i++) {
            if (catalog_[i].category == cat) results.pushBack(i);
        }
        return (u32)results.size();
    }

    u32 searchByTag(const char* tag, Vector<u32>& results) const {
        results.clear();
        for (u32 i = 0; i < catalog_.size(); i++) {
            for (const auto& t : catalog_[i].tags) {
                if (t == tag) { results.pushBack(i); break; }
            }
        }
        return (u32)results.size();
    }

    // ---- Install / Uninstall ----
    bool installAsset(u32 catalogIndex) {
        if (catalogIndex >= catalog_.size()) return false;
        MarketplaceAsset& asset = catalog_[catalogIndex];
        if (asset.installed) return true;

        FROST_LOG_INFO("[Marketplace] installing '%s' v%s by %s",
                       asset.name.data(), asset.version.data(), asset.author.data());
        asset.installed = true;
        asset.localPath = cachePath_ + "/" + asset.name;
        saveLocalLibrary();
        return true;
    }

    bool uninstallAsset(u32 catalogIndex) {
        if (catalogIndex >= catalog_.size()) return false;
        MarketplaceAsset& asset = catalog_[catalogIndex];
        asset.installed = false;
        asset.localPath.clear();
        saveLocalLibrary();
        return true;
    }

    // ---- Reviews ----
    void addReview(u64 assetID, const char* author, f32 rating, const char* comment) {
        MarketplaceReview review;
        review.assetID = assetID;
        review.author = author;
        review.rating = rating;
        review.comment = comment;
        review.timestamp = 0;
        review.helpful = false;
        reviews_.pushBack(review);

        // Update asset rating
        for (auto& a : catalog_) {
            if (a.id == assetID) {
                f32 total = a.rating * a.reviewCount + rating;
                a.reviewCount++;
                a.rating = total / a.reviewCount;
                break;
            }
        }
    }

    // ---- Publish ----
    u64 publishAsset(const MarketplaceAsset& asset) {
        MarketplaceAsset pub = asset;
        pub.id = nextAssetID_++;
        pub.rating = 0;
        pub.downloadCount = 0;
        pub.reviewCount = 0;
        pub.installed = false;
        catalog_.pushBack(pub);
        return pub.id;
    }

    // ---- Categories ----
    void getCategories(Vector<MarketplaceCategory>& cats) const {
        cats.clear();
        for (u32 c = 0; c < (u32)AssetCategory::COUNT; c++) {
            MarketplaceCategory cat;
            cat.category = (AssetCategory)c;
            switch (cat.category) {
                case AssetCategory::Mesh: cat.name = "3D Meshes"; break;
                case AssetCategory::Texture: cat.name = "Textures"; break;
                case AssetCategory::Material: cat.name = "Materials"; break;
                case AssetCategory::Script: cat.name = "Scripts"; break;
                case AssetCategory::Shader: cat.name = "Shaders"; break;
                case AssetCategory::Audio: cat.name = "Audio"; break;
                case AssetCategory::Animation: cat.name = "Animations"; break;
                case AssetCategory::Prefab: cat.name = "Prefabs"; break;
                case AssetCategory::Scene: cat.name = "Scenes"; break;
                case AssetCategory::Plugin: cat.name = "Plugins"; break;
                case AssetCategory::Template: cat.name = "Templates"; break;
                default: cat.name = "Unknown"; break;
            }
            cat.assetCount = 0;
            cat.totalDownloads = 0;
            for (const auto& a : catalog_) {
                if (a.category == cat.category) {
                    cat.assetCount++;
                    cat.totalDownloads += a.downloadCount;
                }
            }
            cats.pushBack(cat);
        }
    }

    // ---- Top rated ----
    void topRated(Vector<u32>& out, u32 count = 10) const {
        out.clear();
        for (u32 i = 0; i < catalog_.size(); i++) out.pushBack(i);

        // Simple sort by rating
        for (u32 i = 1; i < out.size(); i++) {
            for (u32 j = i; j > 0 && catalog_[out[j]].rating > catalog_[out[j-1]].rating; j--) {
                u32 tmp = out[j]; out[j] = out[j-1]; out[j-1] = tmp;
            }
        }
        if (out.size() > count) out.erase(count, out.size());
    }

    // ---- Most downloaded ----
    void mostDownloaded(Vector<u32>& out, u32 count = 10) const {
        out.clear();
        for (u32 i = 0; i < catalog_.size(); i++) out.pushBack(i);
        for (u32 i = 1; i < out.size(); i++) {
            for (u32 j = i; j > 0 && catalog_[out[j]].downloadCount > catalog_[out[j-1]].downloadCount; j--) {
                u32 tmp = out[j]; out[j] = out[j-1]; out[j-1] = tmp;
            }
        }
        if (out.size() > count) out.erase(count, out.size());
    }

    const MarketplaceAsset& asset(u32 idx) const { return catalog_[idx]; }
    u32 assetCount() const { return (u32)catalog_.size(); }

    // ---- Seed with demo content ----
    void loadDemoCatalog() {
        addAsset("Stylized Trees Pack", "Low-poly stylized trees", "FrostTeam", "Mesh",
                 AssetCategory::Mesh, AssetLicense::CC0, 4.8f, 15200, 42, "trees,stylized,forest");
        addAsset("PBR Rock Textures", "4K PBR rock material set", "GeoAssets", "Texture",
                 AssetCategory::Texture, AssetLicense::MIT, 4.6f, 8900, 28, "rock,pbr,4k");
        addAsset("Fantasy Fonts", "5 fantasy display fonts", "TypeCraft", "Font",
                 AssetCategory::Material, AssetLicense::Free, 4.3f, 12400, 35, "fonts,fantasy,ui");
        addAsset("Water Shader Pro", "Realistic water with reflections", "ShaderLab", "Shader",
                 AssetCategory::Shader, AssetLicense::MIT, 4.9f, 22100, 67, "water,reflection,ocean");
        addAsset("Character Controller", "Third-person character controller", "GameDev Co", "Script",
                 AssetCategory::Script, AssetLicense::MIT, 4.5f, 9800, 31, "character,controller,third-person");
        addAsset("Sci-Fi Interior Pack", "Modular sci-fi room pieces", "SpaceForge", "Mesh",
                 AssetCategory::Mesh, AssetLicense::Commercial, 4.7f, 6700, 19, "scifi,interior,modular");
        addAsset("Ambient Sound Pack", "50+ ambient nature sounds", "AudioFrog", "Audio",
                 AssetCategory::Audio, AssetLicense::CC0, 4.4f, 11300, 44, "ambient,nature,forest");
        addAsset("Procedural Sky", "Dynamic day/night sky system", "SkyWorks", "Shader",
                 AssetCategory::Shader, AssetLicense::MIT, 4.8f, 18600, 52, "sky,procedural,day-night");
        addAsset("Inventory System", "Complete inventory with UI", "UIDesigns", "Script",
                 AssetCategory::Script, AssetLicense::MIT, 4.2f, 7400, 22, "inventory,ui,rpg");
        addAsset("Terrain Textures", "12 terrain material textures", "TerraForge", "Texture",
                 AssetCategory::Texture, AssetLicense::CC_BY, 4.6f, 14200, 38, "terrain,ground,materials");
    }

private:
    void addAsset(const char* name, const char* desc, const char* author, const char* ver,
                  AssetCategory cat, AssetLicense lic, f32 rating, u32 downloads, u32 reviews,
                  const char* tags) {
        MarketplaceAsset a;
        a.id = nextAssetID_++;
        a.name = name;
        a.description = desc;
        a.author = author;
        a.version = ver;
        a.category = cat;
        a.license = lic;
        a.rating = rating;
        a.downloadCount = downloads;
        a.reviewCount = reviews;
        a.fileSizeBytes = (u64)(downloads * 1024);
        a.price = 0;
        a.installed = false;
        a.updateAvailable = false;

        // Parse comma-separated tags
        const char* p = tags;
        while (*p) {
            while (*p == ' ') p++;
            const char* start = p;
            while (*p && *p != ',') p++;
            a.tags.pushBack(String(start, (u32)(p - start)));
            if (*p == ',') p++;
        }

        catalog_.pushBack(a);
    }

    void loadLocalLibrary() {}
    void saveLocalLibrary() {}

    Vector<MarketplaceAsset> catalog_;
    Vector<MarketplaceReview> reviews_;
    u64 nextAssetID_ = 1;
    String cachePath_;
};

} // namespace Frost
