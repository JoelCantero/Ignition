cmake_minimum_required(VERSION 3.24)
project(Ignition C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL3 REQUIRED CONFIG)

add_library(ignition_core STATIC
  src/game/assets.c
  src/game/ai.c
  src/game/car.c
  src/game/physics.c
  src/game/race.c
  src/game/track.c
  src/render/raster.c
)
target_include_directories(ignition_core PUBLIC src)
if(NOT MSVC)
  target_compile_options(ignition_core PRIVATE -Wall -Wextra)
  target_link_libraries(ignition_core PUBLIC m)
endif()

add_executable(ignition src/platform/main.c)
target_link_libraries(ignition PRIVATE ignition_core SDL3::SDL3)
if(NOT MSVC)
  target_compile_options(ignition PRIVATE -Wall -Wextra)
endif()

# Flat ELF image of the original executable for disassemblers.
set(GAME_DIR "/Users/Shared/Epic Games/Ignition/ignition" CACHE PATH "Original game data")
add_custom_target(elf
  COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/le2elf.py "${GAME_DIR}/MAINDOS.EXE"
          ${CMAKE_BINARY_DIR}/maindos.elf --map ${CMAKE_BINARY_DIR}/maindos.json
  VERBATIM)

# Deterministic headless regression (needs the game data); run with `ctest --test-dir build`.
enable_testing()
add_test(NAME regress COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/regress.py --binary $<TARGET_FILE:ignition>)
