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
  src/game/pos.c
  src/game/ai.c
  src/game/car.c
  src/game/ghost.c
  src/game/skid.c
  src/game/fx.c
  src/game/weather.c
  src/game/physics.c
  src/game/race.c
  src/game/track.c
  src/render/raster.c
  src/render/hud.c
  src/render/camera.c
)
target_include_directories(ignition_core PUBLIC src)
if(NOT MSVC)
  target_compile_options(ignition_core PRIVATE -Wall -Wextra)
  target_link_libraries(ignition_core PUBLIC m)
endif()

add_executable(ignition src/platform/main.c src/platform/audio.c src/platform/sound.c)
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
add_executable(gearbox_test tests/gearbox.c)
target_link_libraries(gearbox_test PRIVATE ignition_core)
add_test(NAME gearbox COMMAND gearbox_test)
add_executable(pos_test tests/pos.c)
target_link_libraries(pos_test PRIVATE ignition_core)
add_test(NAME pos COMMAND pos_test)

add_executable(body_step_test tests/body_step.c)
target_link_libraries(body_step_test PRIVATE ignition_core)
if(NOT MSVC)
  target_compile_options(body_step_test PRIVATE -Wall -Wextra)
endif()
option(IGN_VERIFY_ORIGINAL "Compare body_step with original LE bytes (requires Python unicorn)" OFF)
if(IGN_VERIFY_ORIGINAL)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME physics_original
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/physics_original.py
            --original "${GAME_DIR}/MAINDOS.EXE" --runner $<TARGET_FILE:body_step_test>
            --report ${CMAKE_BINARY_DIR}/physics-original-report.json)
endif()
