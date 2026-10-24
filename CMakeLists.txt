cmake_minimum_required(VERSION 3.20)
project(smacx LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SMACX_HYBRID "Build hybrid.dll (32-bit, injected into the original terranx.exe)" OFF)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
add_compile_options(-Wall -Wextra -Wno-unused-parameter -fwrapv -fno-strict-aliasing)

# --- engine sources and generated bindings ----------------------------------------------------
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS src/engine/*.cpp src/platform/*.cpp)
file(GLOB_RECURSE ENGINE_HEADERS CONFIGURE_DEPENDS src/engine/*.h src/platform/*.h)
file(GLOB HEADLESS_SOURCES_FOR_GEN CONFIGURE_DEPENDS src/headless/*.cpp)
set(GEN_DIR ${CMAKE_BINARY_DIR}/generated)
add_custom_command(
  OUTPUT ${GEN_DIR}/native_stubs.cpp ${GEN_DIR}/hybrid_bindings.cpp ${GEN_DIR}/status.tsv
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/src/tools/gen_bindings.py ${GEN_DIR} ${CMAKE_SOURCE_DIR}/src
  DEPENDS ${ENGINE_SOURCES} ${ENGINE_HEADERS} ${HEADLESS_SOURCES_FOR_GEN} ${CMAKE_SOURCE_DIR}/src/tools/gen_bindings.py
  COMMENT "Generating original-function bindings")

if(SMACX_HYBRID)
  add_library(hybrid SHARED ${ENGINE_SOURCES} ${GEN_DIR}/hybrid_bindings.cpp harness/oracle/oracle.cpp
              harness/oracle/fcompare.cpp)
  # Standard ebp frames like the original's MSVC code: keeps the dead-stack image left by
  # reconstructed functions closer to the original's (see harness/README.md, stack poisoning).
  target_compile_options(hybrid PRIVATE -fno-omit-frame-pointer)
  target_compile_definitions(hybrid PRIVATE SMACX_HYBRID ORACLE_WITH_ENGINE)
  target_include_directories(hybrid PRIVATE src harness/oracle harness)
  target_link_options(hybrid PRIVATE -static)
  set_target_properties(hybrid PROPERTIES PREFIX "")
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
  file(GLOB HEADLESS_SOURCES CONFIGURE_DEPENDS src/headless/*.cpp)
  add_executable(smacx_headless src/app/headless.cpp ${HEADLESS_SOURCES})
  target_link_libraries(smacx_headless PRIVATE smacx_engine)
  target_include_directories(smacx_headless PRIVATE harness)
endif()
