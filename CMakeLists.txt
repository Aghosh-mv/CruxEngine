cmake_minimum_required(VERSION 3.20)
project(CruxEngine VERSION 0.1.0 LANGUAGES CXX)

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
set(CRUX_CORE_SOURCES
    src/Core/Math.cpp
    src/Core/Noise.cpp
    src/Core/ECS.cpp
)

# ============================================================================
# Source files - Platform
# ============================================================================
set(CRUX_PLATFORM_SOURCES
    src/Platform/Window.cpp
)

# ============================================================================
# Source files - Renderer
# ============================================================================
set(CRUX_RENDERER_SOURCES
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
)

# ============================================================================
# Source files - Assets
# ============================================================================
set(CRUX_ASSETS_SOURCES
    src/Assets/MeshFactory.cpp
    src/Assets/TextureFactory.cpp
    src/Assets/ProceduralAssetSynthesis.cpp
    src/Assets/MaterialLibrary.cpp
    src/Assets/AssetPipeline.cpp
)

# ============================================================================
# Source files - Physics
# ============================================================================
set(CRUX_PHYSICS_SOURCES
    src/Physics/RigidBody.cpp
    src/Physics/WindField.cpp
    src/Physics/PhysicsWorld.cpp
)

# ============================================================================
# Source files - Scene
# ============================================================================
set(CRUX_SCENE_SOURCES
    src/Scene/Scene.cpp
    src/Scene/SceneGraph.cpp
    src/Scene/SceneSerializer.cpp
)

# ============================================================================
# Source files - Scripting (Kitris)
# ============================================================================
set(CRUX_SCRIPTING_SOURCES
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
set(CRUX_EDITOR_SOURCES
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
set(CRUX_GAME_SOURCES
    src/Game/Game.cpp
)

# ============================================================================
# All sources combined
# ============================================================================
set(CRUX_SOURCES
    ${CRUX_CORE_SOURCES}
    ${CRUX_PLATFORM_SOURCES}
    ${CRUX_RENDERER_SOURCES}
    ${CRUX_ASSETS_SOURCES}
    ${CRUX_PHYSICS_SOURCES}
    ${CRUX_SCENE_SOURCES}
    ${CRUX_SCRIPTING_SOURCES}
    ${CRUX_EDITOR_SOURCES}
    ${CRUX_GAME_SOURCES}
)

add_library(CruxEngine STATIC ${CRUX_SOURCES})

target_include_directories(CruxEngine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/CruxEngine>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(CruxEngine PUBLIC
    Threads::Threads
    ${CMAKE_DL_LIBS}
    m
)

target_compile_definitions(CruxEngine PUBLIC
    CRUX_VERSION_MAJOR=0
    CRUX_VERSION_MINOR=1
    CRUX_VERSION_PATCH=0
    CRUX_PLATFORM_LINUX=1
)

add_executable(CruxGame src/main.cpp)
target_link_libraries(CruxGame PRIVATE CruxEngine X11 Xext GL)

install(TARGETS CruxEngine EXPORT CruxEngineTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
install(DIRECTORY include/ DESTINATION include/CruxEngine)