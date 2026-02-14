# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Voxen is a Minecraft-inspired voxel open-world rendering project built with DirectX 11 and C++20. It implements advanced graphics techniques including deferred shading with MSAA, cascade shadow mapping, SSAO, PBR lighting, and real-time world generation with multithreaded chunk management.

## Build System

This project uses Visual Studio 2022 with MSBuild.

**Build Commands:**
```bash
# Build Debug configuration
msbuild voxen.sln /p:Configuration=Debug /p:Platform=x64

# Build Release configuration
msbuild voxen.sln /p:Configuration=Release /p:Platform=x64

# Clean build
msbuild voxen.sln /t:Clean /p:Configuration=Release /p:Platform=x64
```

**Running the application:**
```bash
# Debug build
x64\Debug\voxen.exe

# Release build
x64\Release\voxen.exe
```

The built executables expect the `assets` and `shaders` directories to be accessible relative to the working directory. When running from the repository root, the executable looks for these at `voxen/assets/` and `voxen/shaders/`.

## Code Organization

### Core Architecture

**Entry Point:**
- `voxen/srcs/main.cpp` - Initializes and runs the App class

**Main Application Loop:**
- `App` class (`App.h/cpp`) - Main application class managing the window, DirectX initialization, and render loop
  - Initializes all subsystems (Window, DirectX, GUI, Scene)
  - Updates and renders each frame with ~60 FPS target
  - Manages input (keyboard/mouse) and cursor locking

**Graphics Core:**
- `Graphics` namespace (`Graphics.h/cpp`) - Central graphics management singleton
  - Initializes DirectX 11 device, context, swap chain
  - Creates and manages all render targets, depth buffers, shader resources
  - Defines Pipeline State Objects (PSOs) for different render passes
  - All GPU resources are externally defined in this namespace

**Rendering Pipeline:**
The application uses a deferred rendering pipeline with the following major passes:
1. **Shadow Map Pass** - Cascade shadow mapping with 3 cascades
2. **G-Buffer Fill** - Position, Normal, Albedo, Coverage, MER (Metallic/Emission/Roughness)
3. **MSAA Edge Masking** - Detects edges requiring MSAA
4. **SSAO Rendering** - Screen-space ambient occlusion
5. **Deferred Shading** - Combines G-Buffer with lighting (PBR-based)
6. **Forward Rendering** - Skybox, clouds, transparency, water
7. **Post-Processing** - Bloom, tone mapping

### Chunk System

**ChunkManager (Singleton):**
- `ChunkManager` class (`ChunkManager.h/cpp`) - Manages all chunks in the world
  - Uses object pooling for chunk allocation (`CHUNK_POOL_SIZE`)
  - Chunks are 32x32x32 blocks (`Chunk::CHUNK_SIZE`)
  - Multithreaded loading and patching via `std::future`
  - Manages chunk lifecycle: load, update, patch, render, unload
  - Implements frustum culling for chunks
  - Handles dependency tracking for chunk patches via `m_patchDependencyMap`

**Chunk Structure:**
- `Chunk` class (`Chunk.h/cpp`) - Individual 32x32x32 block region
  - Stores blocks in a 3D array with padding: `m_blocks[CHUNK_SIZE_P][CHUNK_SIZE_P][CHUNK_SIZE_P]`
  - Generates mesh vertices using binary greedy meshing algorithm
  - Separates geometry into: opaque, semi-alpha, transparency, low-LOD
  - Stores instances (grass, flowers, vines) in `m_instanceMap`
  - World generation happens in `Initialize()` method

**Mesh Optimization:**
- Binary greedy meshing algorithm for efficient vertex generation
- Block data stored in bit-packed format to reduce memory usage
- Separate vertex/index buffers for different alpha modes

### World Generation

**Terrain Generation:**
- `Terrain` class (`Terrain.h`) - Noise-based terrain generation
  - Uses FastNoise library for procedural height maps
  - Defines terrain height and basic structure

**Biome System:**
- `Biome` class (`Biome.h/cpp`) - Biome type determination
  - Multiple biomes (Plains, Desert, Savanna, Forest, Taiga, Tundra, Swamp, Ocean)
  - Climate-based (temperature/humidity) biome selection

**Tree Generation:**
- `Tree` class (`Tree.h/cpp`) - Procedural tree placement and structure
  - Biome-specific tree types (Oak, Birch, Acacia, Spruce, Snowy Spruce)
  - Trees can span multiple chunks, requiring patch propagation

**Block System:**
- `Block` class (`Block.h/cpp`) - Individual block data
  - Stores block type, face visibility flags, AO values in bit-packed format
  - Over 50 block types defined in `BLOCK_TYPE` enum

### Key Subsystems

**Camera:**
- `Camera` class (`Camera.h/cpp`) - FPS-style camera with frustum
  - `MAX_RENDER_DISTANCE` determines chunk loading radius
  - Updates view/projection matrices
  - Used for frustum culling

**Lighting:**
- `Light` class (`Light.h/cpp`) - Directional light with cascade shadow maps
  - `CASCADE_NUM = 3` shadow cascades
  - Updates shadow matrices for each cascade

**Shader Management:**
- Shaders located in `voxen/shaders/`
- Common shader code in `Common.hlsli`
- Major shaders: `BasicVS/PS.hlsl`, `ShadingBasicPS.hlsl`, `SkyboxVS/PS.hlsl`, `SsaoPS.hlsl`
- Shadow mapping: `ShadowGS.hlsl` expands geometry to 3 cascades

**Post-Processing:**
- `PostEffect` class (`PostEffect.h/cpp`) - Bloom and tone mapping
  - Multi-pass bloom downsampling/upsampling
  - Linear tone mapping

**Water Rendering:**
- Planar reflection using mirror render pass
- Depth-based transparency and refraction
- Underwater filtering effect

**Skybox and Clouds:**
- `Skybox` class - Procedural sky without cubemap, dynamic day/night cycle
- `Cloud` class - Noise-based cloud mesh generation with transparency

## Development Patterns

**Singleton Pattern:**
Many core systems use static singleton pattern:
- `ChunkManager::GetInstance()`
- Graphics namespace (static members)

**Resource Management:**
- DirectX resources use `ComPtr<>` smart pointers
- GPU buffers are centrally managed in Graphics namespace
- Chunk object pooling for efficient memory reuse

**Multithreading:**
- Chunk loading uses `std::future` with thread pool pattern
- Patching system runs asynchronously
- Max thread counts controlled by `m_initThreadCount` and `m_patchThreadCount`

**Data Organization:**
- Headers in `voxen/headers/`
- Source files in `voxen/srcs/`
- Assets in `voxen/assets/` (textures, models)
- Shaders in `voxen/shaders/` (HLSL)

**Coordinate Systems:**
- World space: absolute block positions
- Chunk space: relative to chunk origin (0-31 for each axis)
- Chunk positions are in chunk coordinates (world_pos / CHUNK_SIZE)

## Important Constraints

**Chunk Patching:**
When modifying blocks at chunk boundaries, adjacent chunks must be patched. The system uses:
- `m_patchDependencyMap` - Tracks which chunks need updating when a chunk changes
- `PropagatePatchByEdgeBlock()` - Propagates patches to neighboring chunks
- Edge blocks (position 0 or 31 in local space) trigger neighbor patches

**Block Visibility:**
- Blocks store face visibility to cull hidden faces
- Ambient occlusion (AO) values are computed during mesh generation
- Greedy meshing merges adjacent faces of same block type

**Instance Rendering:**
Grass, flowers, and vines use instanced rendering:
- Mesh generated by `MeshGenerator` namespace
- Instance data stored per-chunk in `m_instanceMap`
- Rendered with `RenderInstance()` using instancing

**Memory Considerations:**
- Chunk pool size is fixed (`CHUNK_POOL_SIZE`)
- Instance buffer size is limited (`MAX_INSTANCE_BUFFER_SIZE`)
- Chunks beyond render distance are unloaded to free memory

**Shadow Mapping:**
- 3 cascade shadow maps at different resolutions
- Geometry shader duplicates geometry to 3 render targets
- Shadow matrix calculations in Light class

## Graphics Pipeline Details

**G-Buffer Layout:**
1. Position Buffer (RGB: world position)
2. Normal-Edge Buffer (RGB: normal, A: edge mask)
3. Albedo Buffer (RGB: base color)
4. Coverage Buffer (A: coverage mask for MSAA)
5. MER Buffer (R: metallic, G: emission, B: roughness)

**MSAA Strategy:**
- Full-screen MSAA is expensive, so edge detection is used
- Only edge pixels are shaded with MSAA (4x samples)
- Non-edge pixels use standard single-sample shading

**Texture Atlas:**
- Block textures stored in atlas (`blockAtlasMap`)
- Normal maps in separate atlas (`normalAtlasMap`)
- MER maps (Metallic/Emission/Roughness) in third atlas (`merAtlasMap`)
- Color maps for grass/foliage/water biome tinting

## Common Modification Patterns

**Adding a new block type:**
1. Add enum value to `BLOCK_TYPE` in `Enums.h`
2. Update `Block.cpp` to set block properties (transparent, semi-alpha, etc.)
3. Add texture coordinates in relevant shader or texture atlas

**Modifying rendering:**
1. Update relevant shader in `voxen/shaders/`
2. No shader recompilation command needed - shaders are loaded at runtime
3. Restart application to see changes

**Adjusting chunk loading:**
1. Modify `Camera::MAX_RENDER_DISTANCE` to change view distance
2. Update `ChunkManager::CHUNK_COUNT` calculation accordingly
3. Consider memory implications of `CHUNK_POOL_SIZE`

**Performance tuning:**
- SSAO quality: Adjust sample count in `SsaoPS.hlsl`
- Shadow quality: Modify `SHADOW_WIDTH/HEIGHT` in `App.h`
- Chunk loading: Tune thread counts in ChunkManager

## Documentation

Detailed documentation for individual features is located in `docs/`:
- `docs/gpu/` - GPU-side rendering techniques
- `docs/cpu/` - CPU-side systems (chunk management, world generation, mesh optimization)

Each subdirectory contains technical explanations and diagrams for specific features.
