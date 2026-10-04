# Atlas

A Vulkan-based, C++20 game/graphics engine with desktop and Android runtimes, an ImGui editor, and an ECS (entt) built in.

## Layout

- `engine/` — core shared library (`atlas_engine`): Vulkan renderer, ECS, scene/asset system, scripting
- `desktop/` — desktop runtime executable (`atlas_desktop`)
- `editor/` — ImGui-based editor
- `android/` — Android app (Gradle + CMake), mirrors the desktop runtime pattern
- `assets/` — models, textures, cubemaps, shaders
- `scripts/` — asset/shader build helpers (`compile_shaders.py`, `bake_cubemaps.py`, `generate_brdf_lut.py`, `generate_ltc.py`)
- `docs/` — project documentation

### Entry point pattern

`EntryPoint.hpp` owns both `main()` (desktop) and `android_main()` (Android). Each runtime module implements `Atlas::CreateApplication(ApplicationCommandLineArgs)`; the engine itself has no platform entry point.

## Requirements

- CMake 3.22+
- C++20 compiler (MSVC on Windows, verified under Visual Studio 2022)
- Vulkan SDK (glslc for shader compilation)
- Python 3 (for asset/shader build scripts)

## Building

Clone with submodules:

```
git clone --recurse-submodules <repo-url>
```

Configure and build (Windows, from a Visual Studio developer environment):

```
cmake -B build -G Ninja
cmake --build build --target atlas_desktop
```

Compile shaders before running:

```
python scripts/compile_shaders.py
```

This compiles `engine/assets/shaders/*.{vert,frag,comp,r*}` to `.spv` next to their source.

## Running

Run the desktop executable from the repository root so asset paths resolve correctly:

```
build/bin/atlas_desktop.exe
```

## Dependencies

Third-party libraries are pulled in as git submodules under `engine/dependencies/` and `editor/dependencies/` (KTX-Software, VulkanMemoryAllocator, entt, glfw, glm, nlohmann/json, stb, tinygltf, tinyobjloader, tracy, imgui, lunasvg, ImViewGuizmo).
