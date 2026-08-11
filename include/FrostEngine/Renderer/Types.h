#pragma once

#include "Core/Types.h"
#include "Core/String.h"
#include "Core/Vector.h"
#include "Core/UniquePtr.h"
#include "Core/Vec3.h"
#include "Core/Mat4.h"

namespace Frost {
namespace Renderer {

struct Buffer;

enum class BackendType : u8 {
    Vulkan,
    DirectX12,
    OpenGL
};

enum class Format : u32 {
    Unknown = 0,
    R8G8B8A8_UNorm = 1,
    R8G8B8A8_SRGB = 2,
    R16G16B16A16_Float = 3,
    R32G32B32A32_Float = 4,
    R32G32B32_Float = 5,
    D16_UNorm = 6,
    D24_UNorm_S8_UInt = 7,
    D32_Float = 8,
    D32_Float_S8_UInt = 9,
    BC1_UNorm = 10,
    BC2_UNorm = 11,
    BC3_UNorm = 12,
    BC4_UNorm = 13,
    BC5_UNorm = 14,
    BC6H_UFloat = 15,
    BC7_UNorm = 16,
};

enum class TextureType : u8 {
    Tex2D,
    Tex3D,
    CubeMap,
};

enum class SampleCount : u8 {
    Samples1 = 1,
    Samples2 = 2,
    Samples4 = 4,
    Samples8 = 8,
    Samples16 = 16,
};

enum class PrimitiveTopology : u8 {
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,
    TriangleFan,
    PatchList,
};

enum class ShaderStage : u8 {
    Vertex,
    Fragment,
    Compute,
    Geometry,
    TessControl,
    TessEval,
    RayGen,
    Intersection,
    Miss,
    ClosestHit,
    AnyHit,
};

enum class BlendOp : u8 {
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
};

enum class BlendFunc : u8 {
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
    ConstColor,
    OneMinusConstColor,
    ConstAlpha,
    OneMinusConstAlpha,
};

enum class CompareOp : u8 {
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always,
};

enum class StencilOp : u8 {
    Keep,
    Zero,
    Replace,
    IncrementClamp,
    DecrementClamp,
    Invert,
    IncrementWrap,
    DecrementWrap,
};

enum class CullMode : u8 {
    None,
    Front,
    Back,
    FrontAndBack,
};

enum class FillMode : u8 {
    Point,
    Wireframe,
    Solid,
};

enum class ResourceState : u8 {
    Undefined = 0,
    VertexBuffer = 1,
    ConstantBuffer = 2,
    RenderTarget = 3,
    DepthWrite = 4,
    DepthRead = 5,
    UnorderedAccess = 6,
    ShaderResource = 7,
    CopySrc = 8,
    CopyDst = 9,
    Present = 10,
    AccelStructRead = 11,
    AccelStructWrite = 12,
    RayTracing = 13,
};

struct Viewport {
    f32 x = 0, y = 0;
    f32 width = 0, height = 0;
    f32 minDepth = 0.0f, maxDepth = 1.0f;
    
    Viewport() = default;
    Viewport(f32 w, f32 h) : x(0), y(0), width(w), height(h) {}
    Viewport(f32 x, f32 y, f32 w, f32 h) : x(x), y(y), width(w), height(h) {}
    Viewport(f32 x, f32 y, f32 w, f32 h, f32 minD, f32 maxD) 
        : x(x), y(y), width(w), height(h), minDepth(minD), maxDepth(maxD) {}
};

struct Scissor {
    i32 left = 0, top = 0;
    i32 right = 0, bottom = 0;
};

struct RenderPassDesc {
    Vector<Format> colorAttachments;
    Format depthAttachment = Format::Unknown;
    bool clearColor = true;
    bool clearDepth = true;
};

struct Color {
    f32 r = 0, g = 0, b = 0, a = 0;
    Color() = default;
    Color(f32 r, f32 g, f32 b, f32 a = 1.0f) : r(r), g(g), b(b), a(a) {}
    static Color black() { return Color(0, 0, 0, 1); }
    static Color white() { return Color(1, 1, 1, 1); }
    static Color red() { return Color(1, 0, 0, 1); }
    static Color green() { return Color(0, 1, 0, 1); }
    static Color blue() { return Color(0, 0, 1, 1); }
    static Color transparent() { return Color(0, 0, 0, 0); }
};

struct VertexAttribute {
    String name;
    u32 location;
    Format format;
    u32 offset;
};

struct BlendState {
    bool enabled = false;
    BlendFunc srcColor = BlendFunc::SrcAlpha;
    BlendFunc dstColor = BlendFunc::OneMinusSrcAlpha;
    BlendOp colorOp = BlendOp::Add;
    BlendFunc srcAlpha = BlendFunc::One;
    BlendFunc dstAlpha = BlendFunc::Zero;
    BlendOp alphaOp = BlendOp::Add;
};

struct DepthStencilState {
    bool depthEnabled = true;
    bool depthWrite = true;
    CompareOp depthFunc = CompareOp::Less;
    bool stencilEnabled = false;
    u8 stencilReadMask = 0xFF;
    u8 stencilWriteMask = 0xFF;
};

struct RasterState {
    FillMode fillMode = FillMode::Solid;
    CullMode cullMode = CullMode::Back;
    bool frontCounterClockwise = false;
    i32 depthBias = 0;
    f32 slopeScaleDepthBias = 0.0f;
};

struct PipelineStateDesc {
    String vertexShader;
    String fragmentShader;
    String computeShader;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    BlendState blendState;
    DepthStencilState depthState;
    RasterState rasterState;
    Vector<VertexAttribute> vertexAttributes;
    Format renderTargetFormat = Format::R8G8B8A8_UNorm;
    Format depthFormat = Format::D32_Float;
    u32 numRenderTargets = 1;
};

enum class BufferUsage : u32 {
    Vertex = 1 << 0,
    Index = 1 << 1,
    Constant = 1 << 2,
    RenderTarget = 1 << 3,
    UnorderedAccess = 1 << 4,
    ShaderResource = 1 << 5,
    RayTracing = 1 << 6,
};

enum class MemoryUsage : u8 {
    Default,
    Upload,
    Readback,
    Explicit,
};

struct BufferDesc {
    u64 size = 0;
    BufferUsage usage = BufferUsage::Vertex;
    MemoryUsage memoryUsage = MemoryUsage::Default;
    bool cpuAccessible = false;
    String name;
};

enum class Filter : u8 {
    Nearest,
    Linear,
    Cubic,
};

enum class AddressMode : u8 {
    Repeat,
    MirroredRepeat,
    Clamp,
    Border,
    MirrorOnce,
};

enum class BorderColor : u8 {
    TransparentBlack,
    OpaqueBlack,
    OpaqueWhite,
};

struct TextureDesc {
    u32 width = 1;
    u32 height = 1;
    u32 depth = 1;
    u32 mipLevels = 1;
    u32 arrayLayers = 1;
    Format format = Format::R8G8B8A8_UNorm;
    TextureType type = TextureType::Tex2D;
    SampleCount sampleCount = SampleCount::Samples1;
    bool generateMips = false;
    bool cpuAccessible = false;
    bool rayTracing = false;
    Buffer* initialData = nullptr;
    String name;
};

struct SamplerDesc {
    Filter minFilter = Filter::Linear;
    Filter magFilter = Filter::Linear;
    Filter mipFilter = Filter::Linear;
    AddressMode addressU = AddressMode::Clamp;
    AddressMode addressV = AddressMode::Clamp;
    AddressMode addressW = AddressMode::Clamp;
    CompareOp compareOp = CompareOp::Always;
    f32 mipLodBias = 0.0f;
    f32 maxAnisotropy = 1.0f;
    f32 minLod = 0.0f;
    f32 maxLod = 16.0f;
    BorderColor borderColor = BorderColor::TransparentBlack;
};

enum class ImageLayout : u32 {
    Undefined = 0,
    General = 1,
    ColorAttachment = 2,
    DepthAttachment = 3,
    ShaderRead = 4,
    TransferSrc = 5,
    TransferDst = 6,
    Preinitialized = 7,
    DepthReadOnly = 8,
    Present = 9,
};

struct CommandListDesc {
    bool compute = false;
    bool raytracing = false;
    String name;
};

}
}