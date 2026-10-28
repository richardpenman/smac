cmake_minimum_required(VERSION 3.20)
project(smacx LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SMACX_HYBRID "Build oracle.dll (32-bit, injected into the original terranx.exe)" OFF)
option(SMACX_SANITIZE "Native only: build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(SMACX_COVERAGE "Native only: build with gcov coverage (tools/coverage_report.py)" OFF)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
# Code generation must keep the original's (MSVC, 32-bit x86) semantics:
# -fwrapv: signed overflow wraps; -fno-strict-aliasing: the engine reads game state through other
# types; -fsigned-char: char is signed as in MSVC (the default on x86, not on Linux arm64);
# -ffp-contract=off: no fused multiply-add (arm64), so floating point rounds like the original's
# x87 code at 53-bit precision (= SSE2 double).
# -fno-aggressive-loop-optimizations: the original indexes tables out of bounds in places that are
# preserved on purpose (BUG(original)); without it GCC may assume such loops end early.
if(MSVC)
  # cl.exe or clang-cl (experimental: native results are verified with GCC and Clang only).
  # cl.exe: /d2UndefIntOverflow- keeps the optimizer from assuming that signed overflow cannot
  # happen; it has no type-based alias analysis, char is signed and /fp:precise does not contract.
  add_compile_options(/W3 /bigobj /permissive- /Zc:__cplusplus /wd4100 /wd4244 /wd4267 /wd4996)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options(/d2UndefIntOverflow-)
  else()  # clang-cl
    add_compile_options(-fwrapv -fno-strict-aliasing -ffp-contract=off)
  endif()
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter -fwrapv -fno-strict-aliasing -fsigned-char
                      -ffp-contract=off)
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag(-fno-aggressive-loop-optimizations SMACX_HAS_NO_AGGRESSIVE_LOOP_OPT)
  if(SMACX_HAS_NO_AGGRESSIVE_LOOP_OPT)  # GCC only
    add_compile_options(-fno-aggressive-loop-optimizations)
  endif()
endif()
if(NOT SMACX_HYBRID AND SMACX_SANITIZE)
  # Overflow checks stay off: -fwrapv makes signed wrapping defined, as in the original.
  add_compile_options(-fsanitize=address,undefined -fno-sanitize=signed-integer-overflow
                      -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()
if(NOT SMACX_HYBRID AND SMACX_COVERAGE)
  add_compile_options(--coverage -O0)
  add_compile_definitions(SMACX_COVERAGE)
  add_link_options(--coverage)
endif()

# --- engine sources and generated bindings ----------------------------------------------------
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS src/engine/*.cpp src/platform/*.cpp)
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/src/platform/sdl/")  # native UI backend (smacx only)
file(GLOB_RECURSE ENGINE_HEADERS CONFIGURE_DEPENDS src/engine/*.h src/platform/*.h)
file(GLOB HEADLESS_SOURCES_FOR_GEN CONFIGURE_DEPENDS src/headless/*.cpp)
# gen_bindings also reads the UI's declarations and definitions (src/ui: ADDR_NATIVE functions).
file(GLOB UI_FILES_FOR_GEN CONFIGURE_DEPENDS src/ui/*.cpp src/ui/*.h)
set(GEN_DIR ${CMAKE_BINARY_DIR}/generated)
add_custom_command(
  OUTPUT ${GEN_DIR}/native_stubs.cpp ${GEN_DIR}/hybrid_bindings.cpp ${GEN_DIR}/status.tsv
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/src/tools/gen_bindings.py ${GEN_DIR} ${CMAKE_SOURCE_DIR}/src
  DEPENDS ${ENGINE_SOURCES} ${ENGINE_HEADERS} ${HEADLESS_SOURCES_FOR_GEN} ${UI_FILES_FOR_GEN} ${CMAKE_SOURCE_DIR}/src/tools/gen_bindings.py
  COMMENT "Generating original-function bindings")

if(SMACX_HYBRID)
  # The oracle (oracle.dll and its launcher), which records the references of the original. Since
  # the hybrid freeze (docs/CLEANUP_PLAN.md section 2, cleanup step 20) the game state is native
  # objects and hybrid.dll (the reconstruction patched into terranx.exe, fcompare, golden
  # recording) is built from the freeze commit only: see docs/DEVELOPMENT.md.
  add_library(oracle SHARED harness/oracle/oracle.cpp)
  target_include_directories(oracle PRIVATE harness/oracle harness src)
  target_compile_definitions(oracle PRIVATE SMACX_HYBRID)  # rules_dump.h binds engine globals
  target_link_options(oracle PRIVATE -static)
  set_target_properties(oracle PROPERTIES PREFIX "")
  add_executable(oracle_launch harness/oracle/launch.c)
  target_link_options(oracle_launch PRIVATE -static)
else()
  add_library(smacx_engine STATIC ${ENGINE_SOURCES} ${GEN_DIR}/native_stubs.cpp)
  target_include_directories(smacx_engine PUBLIC src)
  if(WIN32)
    target_link_libraries(smacx_engine PUBLIC ws2_32)  # the online transport (platform/net_tcp)
  endif()
  # The headless UI (src/headless) is the default implementation of the engine's UI bridge
  # (engine/ui_bridge.h): a static library with one object file per UI area, linked after the
  # executable's own objects. An executable that defines every function of an area (smacx: src/ui)
  # replaces it, as the linker then never pulls that object in; replacing an area only in part is
  # a duplicate-symbol link error. Engine and headless UI call each other (static library cycle).
  file(GLOB HEADLESS_SOURCES CONFIGURE_DEPENDS src/headless/*.cpp)
  add_library(smacx_ui_headless STATIC ${HEADLESS_SOURCES})
  target_link_libraries(smacx_ui_headless PUBLIC smacx_engine)
  target_link_libraries(smacx_engine PUBLIC smacx_ui_headless)
  # Golden records (src/app/golden.cpp): the replay table of the functions fcompare records.
  add_custom_command(
    OUTPUT ${GEN_DIR}/golden_funcs.h
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/src/tools/golden.py gen ${GEN_DIR}/golden_funcs.h
    DEPENDS ${CMAKE_SOURCE_DIR}/harness/oracle/fcompare_table.h ${CMAKE_SOURCE_DIR}/re/original_names.tsv
            ${CMAKE_SOURCE_DIR}/src/tools/golden.py
    COMMENT "Generating the golden replay table")
  add_executable(smacx_headless src/app/headless.cpp src/app/golden.cpp ${GEN_DIR}/golden_funcs.h)
  target_link_libraries(smacx_headless PRIVATE smacx_engine)
  target_include_directories(smacx_headless PRIVATE harness ${GEN_DIR})
  # Portability smoke test (no game data needed): ctest, or run smacx_smoke.
  enable_testing()
  add_executable(smacx_smoke tests/smoke.cpp)
  target_link_libraries(smacx_smoke PRIVATE smacx_engine)
  add_test(NAME smoke COMMAND smacx_smoke WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
  # Unit tests (doctest, no game data needed): ctest, or run smacx_unit (doctest options, e.g. -s).
  file(GLOB UNIT_SOURCES CONFIGURE_DEPENDS tests/unit/*.cpp)
  add_executable(smacx_unit ${UNIT_SOURCES})
  target_include_directories(smacx_unit PRIVATE third_party tests/unit)
  target_link_libraries(smacx_unit PRIVATE smacx_engine)
  add_test(NAME unit COMMAND smacx_unit WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
  # Golden records (tests/golden, harness/README.md "Golden records"): the original's results of
  # functions, replayed on the states of their scenarios. Need the game's data (game/,
  # src/data/data_image.bin; late140 also its saved game): skipped without them.
  foreach(scenario tiny20 late140)
    add_test(NAME golden_${scenario}
             COMMAND smacx_headless harness/scenarios/${scenario}.cmd --golden tests/golden
                     --outdir ${CMAKE_BINARY_DIR}/golden_out/${scenario}
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(golden_${scenario} PROPERTIES SKIP_RETURN_CODE 77 LABELS data)
  endforeach()
  # The names table (re/original_names.tsv) must match the ADDR declarations.
  add_test(NAME names_table COMMAND ${Python3_EXECUTABLE} src/tools/names.py check
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # Native game with the SDL2 UI (src/ui, src/platform/sdl). Uses tools/sdl2 when present.
  find_package(SDL2 CONFIG QUIET HINTS ${CMAKE_SOURCE_DIR}/tools/sdl2/lib/cmake/SDL2)
  find_package(SDL2_ttf CONFIG QUIET HINTS ${CMAKE_SOURCE_DIR}/tools/sdl2/lib/cmake/SDL2_ttf)
  if(SDL2_FOUND AND SDL2_ttf_FOUND)
    file(GLOB UI_SOURCES CONFIGURE_DEPENDS src/ui/*.cpp src/platform/sdl/*.cpp)
    # The default ini (shipped with releases, and built in for when it is missing).
    file(READ "${CMAKE_SOURCE_DIR}/harness/oracle/runtime/Alpha Centauri.Ini" ini_hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," ini_bytes "${ini_hex}")
    file(CONFIGURE OUTPUT ${GEN_DIR}/default_ini.h
         CONTENT "// Generated from harness/oracle/runtime/Alpha Centauri.Ini.\nstatic const unsigned char kDefaultIni[] = {${ini_bytes}};\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${CMAKE_SOURCE_DIR}/harness/oracle/runtime/Alpha Centauri.Ini")
    add_executable(smacx src/app/smacx.cpp src/app/gamedata.cpp ${UI_SOURCES})
    target_include_directories(smacx PRIVATE ${GEN_DIR})
    target_link_libraries(smacx PRIVATE smacx_engine SDL2::SDL2 SDL2_ttf::SDL2_ttf)
    if(WIN32)
      target_link_libraries(smacx PRIVATE shell32 ole32)
      # Releases: a GUI-subsystem program (no console window; messages go to message boxes).
      option(SMACX_WIN32_GUI "Windows: build smacx without a console window" OFF)
      if(SMACX_WIN32_GUI)
        set_target_properties(smacx PROPERTIES WIN32_EXECUTABLE ON)
      endif()
    endif()
  else()
    message(STATUS "SDL2 or SDL2_ttf not found: smacx (native UI) is not built")
  endif()
endif()
