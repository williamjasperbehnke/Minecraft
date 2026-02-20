cmake_minimum_required(VERSION 3.20)

# CMake 4.x compatibility: some third-party deps (notably glad v0.x) still
# declare very old policy baselines. Setting this minimum allows configuring
# them without requiring CLI flags.
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

project(voxel_clone LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Keep builds reproducible/offline-friendly once deps are fetched.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

option(VOXEL_BUILD_TESTS "Build tests" ON)
option(VOXEL_ENABLE_STRICT_WARNINGS "Enable strict warning flags" OFF)
option(VOXEL_ENABLE_CLANG_TIDY "Enable clang-tidy during compilation" OFF)

include(FetchContent)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
)
FetchContent_Declare(
  glad
  GIT_REPOSITORY https://github.com/Dav1dde/glad.git
  GIT_TAG v0.1.36
)
FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG master
)

FetchContent_MakeAvailable(glfw glm glad stb)

find_package(OpenGL REQUIRED)
find_package(OpenAL)

enable_testing()

add_library(voxel_lib
  src/core/Logger.cpp
  src/gfx/Shader.cpp
  src/gfx/TextureAtlas.cpp
  src/gfx/ChunkMesh.cpp
  src/gfx/HudRenderer.cpp
  src/game/Camera.cpp
  src/game/AudioSystem.cpp
  src/game/GameRules.cpp
  src/game/PlayerController.cpp
  src/game/ItemDropSystem.cpp
  src/game/Inventory.cpp
  src/game/DebugMenu.cpp
  src/voxel/Chunk.cpp
  src/voxel/LightingSolver.cpp
  src/voxel/ChunkMesher.cpp
  src/voxel/Raycaster.cpp
  src/voxel/ChunkIO.cpp
  src/world/WorldGen.cpp
  src/world/World.cpp
)

target_include_directories(voxel_lib PUBLIC
  include
  ${glad_SOURCE_DIR}/include
  ${stb_SOURCE_DIR}
)

target_link_libraries(voxel_lib PUBLIC glfw glm::glm OpenGL::GL)

if(VOXEL_ENABLE_STRICT_WARNINGS)
  if(MSVC)
    target_compile_options(voxel_lib PRIVATE /W4 /permissive-)
  else()
    target_compile_options(voxel_lib PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endif()

if(VOXEL_ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy)
  if(CLANG_TIDY_EXE)
    set_target_properties(voxel_lib PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
  else()
    message(WARNING "VOXEL_ENABLE_CLANG_TIDY=ON but clang-tidy was not found")
  endif()
endif()

find_program(CLANG_TIDY_EXE NAMES clang-tidy)
if(CLANG_TIDY_EXE)
  add_custom_target(clang_tidy
    COMMAND ${CLANG_TIDY_EXE} -p ${CMAKE_BINARY_DIR}
            src/core/Logger.cpp
            src/gfx/Shader.cpp
            src/gfx/TextureAtlas.cpp
            src/gfx/ChunkMesh.cpp
            src/gfx/HudRenderer.cpp
            src/game/Camera.cpp
            src/game/AudioSystem.cpp
            src/game/GameRules.cpp
            src/game/PlayerController.cpp
            src/game/ItemDropSystem.cpp
            src/game/Inventory.cpp
            src/game/DebugMenu.cpp
            src/voxel/Chunk.cpp
            src/voxel/ChunkMesher.cpp
            src/voxel/Raycaster.cpp
            src/voxel/ChunkIO.cpp
            src/world/WorldGen.cpp
            src/world/World.cpp
            src/main.cpp
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running clang-tidy on project sources"
  )
endif()

find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
  add_custom_target(format
    COMMAND ${CLANG_FORMAT_EXE} -i
            ${CMAKE_SOURCE_DIR}/include/core/Logger.hpp
            ${CMAKE_SOURCE_DIR}/include/gfx/ChunkMesh.hpp
            ${CMAKE_SOURCE_DIR}/include/gfx/HudRenderer.hpp
            ${CMAKE_SOURCE_DIR}/include/gfx/Shader.hpp
            ${CMAKE_SOURCE_DIR}/include/gfx/TextureAtlas.hpp
            ${CMAKE_SOURCE_DIR}/include/game/AudioSystem.hpp
            ${CMAKE_SOURCE_DIR}/include/game/Camera.hpp
            ${CMAKE_SOURCE_DIR}/include/game/DebugMenu.hpp
            ${CMAKE_SOURCE_DIR}/include/game/GameRules.hpp
            ${CMAKE_SOURCE_DIR}/include/game/Inventory.hpp
            ${CMAKE_SOURCE_DIR}/include/game/ItemDropSystem.hpp
            ${CMAKE_SOURCE_DIR}/include/game/PlayerController.hpp
            ${CMAKE_SOURCE_DIR}/include/voxel/Block.hpp
            ${CMAKE_SOURCE_DIR}/include/voxel/Chunk.hpp
            ${CMAKE_SOURCE_DIR}/include/voxel/ChunkIO.hpp
            ${CMAKE_SOURCE_DIR}/include/voxel/ChunkMesher.hpp
            ${CMAKE_SOURCE_DIR}/include/voxel/Raycaster.hpp
            ${CMAKE_SOURCE_DIR}/include/world/ChunkCoord.hpp
            ${CMAKE_SOURCE_DIR}/include/world/World.hpp
            ${CMAKE_SOURCE_DIR}/include/world/WorldGen.hpp
            ${CMAKE_SOURCE_DIR}/src/core/Logger.cpp
            ${CMAKE_SOURCE_DIR}/src/gfx/ChunkMesh.cpp
            ${CMAKE_SOURCE_DIR}/src/gfx/HudRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/gfx/Shader.cpp
            ${CMAKE_SOURCE_DIR}/src/gfx/TextureAtlas.cpp
            ${CMAKE_SOURCE_DIR}/src/game/AudioSystem.cpp
            ${CMAKE_SOURCE_DIR}/src/game/Camera.cpp
            ${CMAKE_SOURCE_DIR}/src/game/DebugMenu.cpp
            ${CMAKE_SOURCE_DIR}/src/game/GameRules.cpp
            ${CMAKE_SOURCE_DIR}/src/game/Inventory.cpp
            ${CMAKE_SOURCE_DIR}/src/game/ItemDropSystem.cpp
            ${CMAKE_SOURCE_DIR}/src/game/PlayerController.cpp
            ${CMAKE_SOURCE_DIR}/src/main.cpp
            ${CMAKE_SOURCE_DIR}/src/voxel/Chunk.cpp
            ${CMAKE_SOURCE_DIR}/src/voxel/ChunkIO.cpp
            ${CMAKE_SOURCE_DIR}/src/voxel/ChunkMesher.cpp
            ${CMAKE_SOURCE_DIR}/src/voxel/Raycaster.cpp
            ${CMAKE_SOURCE_DIR}/src/world/World.cpp
            ${CMAKE_SOURCE_DIR}/src/world/WorldGen.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_chunk.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_raycast.cpp
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Formatting source files with clang-format"
  )
  add_custom_target(format_check
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/.clang-format ${CMAKE_BINARY_DIR}/.clang-format
    COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror
            ${CMAKE_SOURCE_DIR}/include/core/Logger.hpp
            ${CMAKE_SOURCE_DIR}/include/gfx/ChunkMesh.hpp
            ${CMAKE_SOURCE_DIR}/include/gfx/HudRenderer.hpp
            ${CMAKE_SOURCE_DIR}/include/gfx/Shader.hpp
            ${CMAKE_SOURCE_DIR}/include/gfx/TextureAtlas.hpp
            ${CMAKE_SOURCE_DIR}/include/game/AudioSystem.hpp
            ${CMAKE_SOURCE_DIR}/include/game/Camera.hpp
            ${CMAKE_SOURCE_DIR}/include/game/DebugMenu.hpp
            ${CMAKE_SOURCE_DIR}/include/game/GameRules.hpp
            ${CMAKE_SOURCE_DIR}/include/game/Inventory.hpp
            ${CMAKE_SOURCE_DIR}/include/game/ItemDropSystem.hpp
            ${CMAKE_SOURCE_DIR}/include/game/PlayerController.hpp
            ${CMAKE_SOURCE_DIR}/include/voxel/Block.hpp
            ${CMAKE_SOURCE_DIR}/include/voxel/Chunk.hpp
            ${CMAKE_SOURCE_DIR}/include/voxel/ChunkIO.hpp
            ${CMAKE_SOURCE_DIR}/include/voxel/ChunkMesher.hpp
            ${CMAKE_SOURCE_DIR}/include/voxel/Raycaster.hpp
            ${CMAKE_SOURCE_DIR}/include/world/ChunkCoord.hpp
            ${CMAKE_SOURCE_DIR}/include/world/World.hpp
            ${CMAKE_SOURCE_DIR}/include/world/WorldGen.hpp
            ${CMAKE_SOURCE_DIR}/src/core/Logger.cpp
            ${CMAKE_SOURCE_DIR}/src/gfx/ChunkMesh.cpp
            ${CMAKE_SOURCE_DIR}/src/gfx/HudRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/gfx/Shader.cpp
            ${CMAKE_SOURCE_DIR}/src/gfx/TextureAtlas.cpp
            ${CMAKE_SOURCE_DIR}/src/game/AudioSystem.cpp
            ${CMAKE_SOURCE_DIR}/src/game/Camera.cpp
            ${CMAKE_SOURCE_DIR}/src/game/DebugMenu.cpp
            ${CMAKE_SOURCE_DIR}/src/game/GameRules.cpp
            ${CMAKE_SOURCE_DIR}/src/game/Inventory.cpp
            ${CMAKE_SOURCE_DIR}/src/game/ItemDropSystem.cpp
            ${CMAKE_SOURCE_DIR}/src/game/PlayerController.cpp
            ${CMAKE_SOURCE_DIR}/src/main.cpp
            ${CMAKE_SOURCE_DIR}/src/voxel/Chunk.cpp
            ${CMAKE_SOURCE_DIR}/src/voxel/ChunkIO.cpp
            ${CMAKE_SOURCE_DIR}/src/voxel/ChunkMesher.cpp
            ${CMAKE_SOURCE_DIR}/src/voxel/Raycaster.cpp
            ${CMAKE_SOURCE_DIR}/src/world/World.cpp
            ${CMAKE_SOURCE_DIR}/src/world/WorldGen.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_chunk.cpp
            ${CMAKE_SOURCE_DIR}/tests/test_raycast.cpp
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking source formatting with clang-format"
  )
endif()

if(OpenAL_FOUND)
  target_link_libraries(voxel_lib PUBLIC OpenAL::OpenAL)
  target_compile_definitions(voxel_lib PUBLIC VOXEL_HAS_OPENAL=1)
  if(APPLE)
    target_compile_options(voxel_lib PRIVATE -Wno-deprecated-declarations)
  endif()
else()
  message(WARNING "OpenAL not found: audio will be disabled")
endif()

if(TARGET glad)
  target_link_libraries(voxel_lib PUBLIC glad)
elseif(TARGET glad::glad)
  target_link_libraries(voxel_lib PUBLIC glad::glad)
else()
  message(FATAL_ERROR "glad target not found")
endif()

add_executable(voxel_clone src/main.cpp)
target_link_libraries(voxel_clone PRIVATE voxel_lib)

if(VOXEL_BUILD_TESTS)
  add_executable(test_chunk tests/test_chunk.cpp)
  target_link_libraries(test_chunk PRIVATE voxel_lib)

  add_executable(test_raycast tests/test_raycast.cpp)
  target_link_libraries(test_raycast PRIVATE voxel_lib)

  add_test(NAME test_chunk COMMAND test_chunk)
  add_test(NAME test_raycast COMMAND test_raycast)
endif()
