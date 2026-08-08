#pragma once

// All engine shaders are embedded as C++ raw strings so the engine has no
// runtime dependency on shader file paths. Editing these regenerates the
// pipeline at the next build.

namespace Crux {
namespace ShaderSource {

// ---------------------------------------------------------------- PBR main
extern const char* pbrVert;
extern const char* pbrFrag;

// --------------------------------------------------------------- terrain
extern const char* terrainVert;
extern const char* terrainFrag;

// ----------------------------------------------------------------- water
extern const char* waterVert;
extern const char* waterFrag;

// ------------------------------------------------------------------ sky
extern const char* skyVert;
extern const char* skyFrag;

// ------------------------------------------------------------- particles
extern const char* particleVert;
extern const char* particleFrag;

// ------------------------------------------------------------- shadows
extern const char* shadowVert;
extern const char* shadowFrag;
extern const char* terrainShadowVert;

// ---------------------------------------------------- depth / ssao / rays
extern const char* depthVert;
extern const char* depthFrag;
extern const char* ssaoFrag;
extern const char* godrayFrag;

// ------------------------------------------------------- postprocessing
extern const char* postVert;
extern const char* postFrag;
extern const char* blurVert;
extern const char* blurFrag;
extern const char* compositeFrag;

// ------------------------------------------------------------------ text
extern const char* textVert;
extern const char* textFrag;

// ------------------------------------------------------------------- hud
extern const char* hudRectFrag;

// ----------------------------------------------------------------- debug
extern const char* debugVert;
extern const char* debugFrag;

}
}
