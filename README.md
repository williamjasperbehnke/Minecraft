# Voxel Clone (C++20 + OpenGL)

Implemented:
- Chunk streaming with `load=8`, `unload=10`
- Background worker queue for load/generate + greedy meshing
- Main-thread GPU mesh upload
- Chunk persistence (`ChunkIO`) with load-before-generate fallback
- DDA raycast block interaction (LMB remove, RMB place)
- `ctest` regression tests for chunk storage + raycaster
- GitHub Actions CI running configure/build/ctest

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVOXEL_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/voxel_clone
```

Or use the helper script:

```bash
./scripts/build.sh --run
```

Useful flags:
- `--clean` remove `build/` first
- `--debug` build Debug instead of Release
- `--no-tests` skip test targets

## Controls
- `WASD`: move
- `Space` / `Left Shift`: up/down (free-fly camera)
- Mouse: look
- `LMB`: remove block
- `RMB`: place block (`DIRT`)
- `F3`: cycle render mode (`Textured` -> `Flat` -> `Wireframe`)
- `F1`: toggle on-screen debug panel (mouse-interactive)
- Debug menu interaction: click `-` / `+` buttons to adjust values

## Assets
- Put texture atlas at `assets/atlas.png` (16x16 tiles expected).
- If missing, runtime fallback checker atlas is generated.
