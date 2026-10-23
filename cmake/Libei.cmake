# Prefer the system libraries; use the pinned upstream source when unavailable.
option(NKCF_USE_BUNDLED_LIBEI "Always build the bundled libei/libeis" OFF)
if(NOT NKCF_USE_BUNDLED_LIBEI)
  pkg_check_modules(LIBEIS QUIET IMPORTED_TARGET libeis-1.0>=1.6)
  if(BUILD_TESTING)
    pkg_check_modules(LIBEI QUIET IMPORTED_TARGET libei-1.0>=1.6)
  endif()
endif()

if(NOT NKCF_USE_BUNDLED_LIBEI AND LIBEIS_FOUND AND (NOT BUILD_TESTING OR LIBEI_FOUND))
  message(STATUS "Using system libeis ${LIBEIS_VERSION}")
  add_library(nkcf-libeis INTERFACE)
  target_link_libraries(nkcf-libeis INTERFACE PkgConfig::LIBEIS)
  if(BUILD_TESTING)
    add_library(nkcf-libei INTERFACE)
    target_link_libraries(nkcf-libei INTERFACE PkgConfig::LIBEI)
  endif()
  return()
endif()

set(libei_source "${PROJECT_SOURCE_DIR}/third_party/libei")
if(NOT EXISTS "${libei_source}/meson.build")
  message(FATAL_ERROR
    "Bundled libei is required. Run: git submodule update --init --recursive")
endif()
find_program(NKCF_MESON_EXECUTABLE meson REQUIRED)
find_program(NKCF_NINJA_EXECUTABLE ninja REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import jinja2"
  RESULT_VARIABLE jinja_result ERROR_QUIET)
if(NOT jinja_result EQUAL 0)
  message(FATAL_ERROR "Bundled libei requires the Python jinja2 module")
endif()

include(ExternalProject)
set(libei_build "${CMAKE_CURRENT_BINARY_DIR}/bundled-libei")
set(libei_targets eis util)
set(libei_client disabled)
set(libei_byproducts "${libei_build}/src/libeis.a" "${libei_build}/src/libutil.a")
if(BUILD_TESTING)
  set(libei_client enabled)
  list(APPEND libei_targets ei)
  list(APPEND libei_byproducts "${libei_build}/src/libei.a")
endif()
message(STATUS "Using bundled libei/libeis 1.6.0 (static)")
ExternalProject_Add(nkcf-bundled-libei
  SOURCE_DIR "${libei_source}"
  BINARY_DIR "${libei_build}"
  DOWNLOAD_COMMAND ""
  UPDATE_COMMAND ""
  CONFIGURE_COMMAND "${CMAKE_COMMAND}"
    "-DNKCF_LIBEI_SOURCE=<SOURCE_DIR>" "-DNKCF_LIBEI_BUILD=<BINARY_DIR>"
    "-DNKCF_MESON=${NKCF_MESON_EXECUTABLE}" "-DNKCF_CC=${CMAKE_C_COMPILER}"
    "-DNKCF_LIBEI_CLIENT=${libei_client}"
    -P "${CMAKE_CURRENT_LIST_DIR}/ConfigureLibei.cmake"
  BUILD_COMMAND "${NKCF_MESON_EXECUTABLE}" compile -C <BINARY_DIR> ${libei_targets}
  BUILD_ALWAYS TRUE
  BUILD_BYPRODUCTS ${libei_byproducts}
  INSTALL_COMMAND ""
)

add_library(nkcf-libei-util STATIC IMPORTED)
set_target_properties(nkcf-libei-util PROPERTIES
  IMPORTED_LOCATION "${libei_build}/src/libutil.a"
  INTERFACE_LINK_LIBRARIES m)
add_dependencies(nkcf-libei-util nkcf-bundled-libei)
foreach(component IN ITEMS eis ei)
  if(component STREQUAL "ei" AND NOT BUILD_TESTING)
    continue()
  endif()
  add_library(nkcf-lib${component} STATIC IMPORTED)
  set_target_properties(nkcf-lib${component} PROPERTIES
    IMPORTED_LOCATION "${libei_build}/src/lib${component}.a"
    INTERFACE_INCLUDE_DIRECTORIES "${libei_source}/src"
    INTERFACE_LINK_LIBRARIES nkcf-libei-util)
  add_dependencies(nkcf-lib${component} nkcf-bundled-libei)
endforeach()
# Static linking keeps installation self-contained without replacing system libraries.
install(FILES "${libei_source}/COPYING"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME}/libei")
