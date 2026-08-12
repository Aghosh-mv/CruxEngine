# FrostEngine

A lightweight 3D engine written in C++17 with a zero-dependency OpenGL renderer
(libGL + libX11 only) and a wind-glider flight demo called **Angin**.

## Features

- **Renderer** (real GPU passes): cascaded shadow maps, water reflection,
  depth prepass + SSAO, procedural sky, streamed terrain LOD, water plane,
  particles, PBR materials, bloom/tonemap/vignette post-processing.
- **InputSystem**: keyboard, mouse, up to 4 gamepads, rebindable action system
  (Button / Axis1D / Axis2D), deadzones, rumble, event queue, text input.
- **Engine scaffolding**: animation (clips, skeletons, blend, IK, state machines),
  scene graph + serialization + streaming, Kitris scripting VM, particle/audio/
  physics/network systems, and renderer R&D scaffolds (virtual texturing,
  TCSM virtual shadows, SVO, Lumen-style GI, NRC denoising, GPU-driven rendering,
  path tracing, photon mapping, nanite-style clusters).

> Note: the R&D systems above are engine-side architectures; the scene that
> actually renders today uses the core PBR pipeline.

## Building

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/FrostGame
```

Requires: CMake ≥ 3.20, a C++17 compiler, and X11/OpenGL dev libraries.

## Installing

```sh
./packaging/install.sh          # installs frostgame to /usr/local (or ~/.local)
```

Or build a `.deb`:

```sh
./packaging/build-deb.sh        # outputs dist/frostengine_<version>_amd64.deb
sudo dpkg -i dist/frostengine_0.1.0_amd64.deb
frostgame
```

## Controls (Angin demo)

W/S pitch · A/D roll · Q/E yaw · Shift boost · R reset · P pause · Esc quit

## License

TBD.
