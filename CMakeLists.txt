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
add_test(NAME regress COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/regress.py
  --binary $<TARGET_FILE:ignition> --data "${GAME_DIR}"
  --report ${CMAKE_BINARY_DIR}/regression-report.json)
add_test(NAME verification_tools COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/test_verification.py)
add_test(NAME headless_trace_contract COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/test_verification.py
  --binary $<TARGET_FILE:ignition> --data "${GAME_DIR}" HeadlessTests)
add_test(NAME solo_replay COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/solo_replay.py
  --binary $<TARGET_FILE:ignition> --data "${GAME_DIR}"
  --report ${CMAKE_BINARY_DIR}/solo-replay-report.json)
add_executable(gearbox_test tests/gearbox.c)
target_link_libraries(gearbox_test PRIVATE ignition_core)
add_test(NAME gearbox COMMAND gearbox_test)
add_executable(track_gates_test tests/track_gates.c)
target_link_libraries(track_gates_test PRIVATE ignition_core)
add_test(NAME track_gates COMMAND track_gates_test)
add_executable(track_ai_records_test tests/track_ai_records.c)
target_link_libraries(track_ai_records_test PRIVATE ignition_core)
add_test(NAME track_ai_records COMMAND track_ai_records_test)
add_executable(ai_route_choice_test tests/ai_route_choice.c)
target_link_libraries(ai_route_choice_test PRIVATE ignition_core)
add_test(NAME ai_route_choice COMMAND ai_route_choice_test)
add_executable(ai_schedule_test tests/ai_schedule.c)
target_link_libraries(ai_schedule_test PRIVATE ignition_core)
add_test(NAME ai_schedule COMMAND ai_schedule_test)
add_executable(track_route_selection_test tests/track_route_selection.c)
target_link_libraries(track_route_selection_test PRIVATE ignition_core)
add_test(NAME track_route_selection COMMAND track_route_selection_test)
add_executable(track_topology_test tests/track_topology.c)
target_link_libraries(track_topology_test PRIVATE ignition_core)
add_test(NAME track_topology COMMAND track_topology_test)
add_executable(track_real_assets_test tests/track_real_assets.c)
target_link_libraries(track_real_assets_test PRIVATE ignition_core)
add_test(NAME track_real_assets COMMAND track_real_assets_test "${GAME_DIR}")
add_executable(pos_test tests/pos.c)
target_link_libraries(pos_test PRIVATE ignition_core)
add_test(NAME pos COMMAND pos_test)

add_executable(body_step_test tests/body_step.c)
target_link_libraries(body_step_test PRIVATE ignition_core)
add_executable(car_kernels_test tests/car_kernels.c)
target_link_libraries(car_kernels_test PRIVATE ignition_core)
if(NOT MSVC)
  target_compile_options(body_step_test PRIVATE -Wall -Wextra)
  target_compile_options(car_kernels_test PRIVATE -Wall -Wextra)
  target_compile_options(track_gates_test PRIVATE -Wall -Wextra)
  target_compile_options(track_ai_records_test PRIVATE -Wall -Wextra)
  target_compile_options(track_route_selection_test PRIVATE -Wall -Wextra)
  target_compile_options(track_topology_test PRIVATE -Wall -Wextra)
  target_compile_options(track_real_assets_test PRIVATE -Wall -Wextra)
  target_compile_options(ai_route_choice_test PRIVATE -Wall -Wextra)
  target_compile_options(ai_schedule_test PRIVATE -Wall -Wextra)
endif()
option(IGN_VERIFY_ORIGINAL "Compare physics and car kernels with original LE bytes (requires Python unicorn)" OFF)
if(IGN_VERIFY_ORIGINAL)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME ai_schedule_original
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/ai_schedule_original.py
            --original "${GAME_DIR}/MAINDOS.EXE" --runner $<TARGET_FILE:ai_schedule_test>
            --report ${CMAKE_BINARY_DIR}/ai-schedule-original-report.json)
  add_test(NAME physics_original
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/physics_original.py
            --original "${GAME_DIR}/MAINDOS.EXE" --runner $<TARGET_FILE:body_step_test>
            --report ${CMAKE_BINARY_DIR}/physics-original-report.json)
  add_test(NAME car_original
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/car_original.py
            --original "${GAME_DIR}/MAINDOS.EXE" --runner $<TARGET_FILE:car_kernels_test>
            --report ${CMAKE_BINARY_DIR}/car-original-report.json)
  add_test(NAME car_kernel_contract
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_verification.py
            --runner $<TARGET_FILE:car_kernels_test> CarKernelTests)
  add_test(NAME input_original
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/input_original.py
            --original "${GAME_DIR}/MAINDOS.EXE" --runner $<TARGET_FILE:car_kernels_test>
            --report ${CMAKE_BINARY_DIR}/input-original-report.json)
  add_test(NAME race_time_original
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/race_time_original.py
            --original "${GAME_DIR}/MAINDOS.EXE" --runner $<TARGET_FILE:car_kernels_test>
            --report ${CMAKE_BINARY_DIR}/race-time-original-report.json)
  add_test(NAME respawn_original
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/respawn_original.py
            --original "${GAME_DIR}/MAINDOS.EXE" --runner $<TARGET_FILE:car_kernels_test>
            --report ${CMAKE_BINARY_DIR}/respawn-original-report.json)
  add_test(NAME lap_original
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lap_original.py
            --original "${GAME_DIR}/MAINDOS.EXE" --runner $<TARGET_FILE:car_kernels_test>
            --report ${CMAKE_BINARY_DIR}/lap-original-report.json)
endif()
