cmake_minimum_required(VERSION 3.20)
project(FrostEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

# ============================================================================
# Source files - Core
# ============================================================================
set(FROST_CORE_SOURCES
    src/Core/Math.cpp
    src/Core/Noise.cpp
    src/Core/ECS.cpp
)

# ============================================================================
# Source files - Platform
# ============================================================================
set(FROST_PLATFORM_SOURCES
    src/Platform/Window.cpp
)

# ============================================================================
# Source files - Renderer
# ============================================================================
set(FROST_RENDERER_SOURCES
    src/Renderer/Gl.cpp
    src/Renderer/Shader.cpp
    src/Renderer/ShaderSource_pbr.cpp
    src/Renderer/ShaderSource_extra.cpp
    src/Renderer/ShaderSource_compute.cpp
    src/Renderer/Mesh.cpp
    src/Renderer/Texture.cpp
    src/Renderer/Material.cpp
    src/Renderer/Camera.cpp
    src/Renderer/Font.cpp
    src/Renderer/Renderer.cpp
    src/Renderer/LightingSystem.cpp
    src/Renderer/ComputeManager.cpp
    src/Renderer/GPUProfiler.cpp
    src/Renderer/Upscaler.cpp
    src/Renderer/VolumetricLighting.cpp
    src/Renderer/FrostRadiance.cpp
    src/Renderer/FrostCluster.cpp
    src/Renderer/FrostPhotonMapper.cpp
    src/Renderer/FrostWaveletGI.cpp
    src/Renderer/FrostPathTracer.cpp
    src/Renderer/FrostNeuralDenoiser.cpp
    src/Renderer/FrostVirtualTexturing.cpp
    src/Renderer/LumenSystem.cpp
    src/Renderer/NaniteSystem.cpp
    src/Renderer/VirtualShadowMaps.cpp
    src/Renderer/RayTracingSystem.cpp
    src/Renderer/MegaLights.cpp
    src/Renderer/OcclusionCulling.cpp
)

# ============================================================================
# Source files - Assets
# ============================================================================
set(FROST_ASSETS_SOURCES
    src/Assets/MeshFactory.cpp
    src/Assets/TextureFactory.cpp
    src/Assets/ProceduralAssetSynthesis.cpp
    src/Assets/MaterialLibrary.cpp
    src/Assets/AssetPipeline.cpp
)

# ============================================================================
# Source files - Physics
# ============================================================================
set(FROST_PHYSICS_SOURCES
    src/Physics/RigidBody.cpp
    src/Physics/WindField.cpp
    src/Physics/PhysicsWorld.cpp
    src/Physics/DestructionSystem.cpp
)

# ============================================================================
# Source files - Audio
# ============================================================================
set(FROST_AUDIO_SOURCES
    src/Audio/AudioSystem.cpp
)

# ============================================================================
# Source files - Particles
# ============================================================================
set(FROST_PARTICLE_SOURCES
    src/Particles/ParticleSystem.cpp
)

# ============================================================================
# Source files - Animation
# ============================================================================
set(FROST_ANIMATION_SOURCES
    src/Animation/AnimationSystem.cpp
)

# ============================================================================
# Source files - AI
# ============================================================================
set(FROST_AI_SOURCES
    src/AI/AISystem.cpp
)

# ============================================================================
# Source files - Network
# ============================================================================
set(FROST_NETWORK_SOURCES
    src/Network/NetworkSystem.cpp
)

# ============================================================================
# Source files - Scene
# ============================================================================
set(FROST_SCENE_SOURCES
    src/Scene/Scene.cpp
    src/Scene/SceneGraph.cpp
    src/Scene/SceneSerializer.cpp
    src/Scene/SceneStreaming.cpp
)

# ============================================================================
# Source files - Scripting (Kitris)
# ============================================================================
set(FROST_SCRIPTING_SOURCES
    src/Scripting/KitrisLexer.cpp
    src/Scripting/KitrisParser.cpp
    src/Scripting/KitrisVM.cpp
    src/Scripting/KitrisBytecode.cpp
    src/Scripting/KitrisStdlib.cpp
    src/Scripting/KitrisDebugger.cpp
    src/Scripting/ScriptBridge.cpp
)

# ============================================================================
# Source files - Editor
# ============================================================================
set(FROST_EDITOR_SOURCES
    src/Editor/Editor.cpp
    src/Editor/CodeEditor.cpp
    src/Editor/Marketplace.cpp
    src/Editor/Viewport.cpp
    src/Editor/PropertyInspector.cpp
    src/Editor/SceneHierarchy.cpp
    src/Editor/AssetBrowser.cpp
    src/Editor/Console.cpp
)

# ============================================================================
# Source files - Game
# ============================================================================
set(FROST_GAME_SOURCES
    src/Game/Game.cpp
)

# ============================================================================
# All sources combined
# ============================================================================
set(FROST_SOURCES
    ${FROST_CORE_SOURCES}
    ${FROST_PLATFORM_SOURCES}
    ${FROST_RENDERER_SOURCES}
    ${FROST_ASSETS_SOURCES}
    ${FROST_PHYSICS_SOURCES}
    ${FROST_SCENE_SOURCES}
    ${FROST_SCRIPTING_SOURCES}
    ${FROST_EDITOR_SOURCES}
    ${FROST_GAME_SOURCES}
    ${FROST_AUDIO_SOURCES}
    ${FROST_PARTICLE_SOURCES}
    ${FROST_ANIMATION_SOURCES}
    ${FROST_AI_SOURCES}
    ${FROST_NETWORK_SOURCES}
)

add_library(FrostEngine STATIC ${FROST_SOURCES})

target_include_directories(FrostEngine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/FrostEngine>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/FrostEngine/Renderer>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(FrostEngine PUBLIC
    Threads::Threads
    ${CMAKE_DL_LIBS}
    m
)

target_compile_definitions(FrostEngine PUBLIC
    FROST_VERSION_MAJOR=0
    FROST_VERSION_MINOR=1
    FROST_VERSION_PATCH=0
    FROST_PLATFORM_LINUX=1
)

add_executable(FrostGame src/main.cpp)
target_link_libraries(FrostGame PRIVATE FrostEngine X11 Xext GL)

install(TARGETS FrostEngine EXPORT FrostEngineTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
install(DIRECTORY include/ DESTINATION include/FrostEngine)