#pragma once

// Procedural 8x8 bitmap font. Glyphs are defined as string patterns and baked
// into a texture atlas at runtime (see FontRenderer). Covers ASCII 32..126
// plus a few extras (arrows, degrees) used by the HUD.

#include "Core/Types.h"
#include <unordered_map>

namespace Crux {

class Font {
public:
    Font();

    // Bakes all glyphs into a single RGBA8 atlas. Returns the atlas and the
    // pixel metrics used for glyph lookup.
    struct Atlas {
        u8* data = nullptr;
        u32 width = 0, height = 0;
        u32 glyphW = 8, glyphH = 8;
        i32 rows = 16, cols = 16;
    };

    const Atlas& atlas() const { return atlas_; }
    Atlas atlas_;

    // Rasterizes a string into a single texture quad list is handled by the
    // font renderer; here we provide the glyph index helper.
    static i32 glyphIndex(char c);
};

}
