cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0083)
  cmake_policy(SET CMP0083 NEW)
endif()
project(niri-kdeconnect-fix VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

include(CTest)
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(CheckLinkerFlag)
include(CheckPIESupported)
include(GNUInstallDirs)

option(NKCF_ENABLE_HARDENING "Enable compiler and linker hardening flags" ON)
set(NKCF_PORTAL_USE_IN "niri"
  CACHE STRING "Semicolon-separated XDG_CURRENT_DESKTOP values where this portal backend should be visible"
)

function(nkcf_flag_id out flag)
  string(MAKE_C_IDENTIFIER "${flag}" flag_id)
  string(TOUPPER "${flag_id}" flag_id)
  set(${out} "${flag_id}" PARENT_SCOPE)
endfunction()

function(nkcf_add_compile_flag_if_supported target flag)
  nkcf_flag_id(flag_id "${flag}")
  check_c_compiler_flag("${flag}" "NKCF_C_HAS_${flag_id}")
  check_cxx_compiler_flag("${flag}" "NKCF_CXX_HAS_${flag_id}")
  if(NKCF_C_HAS_${flag_id})
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C>:${flag}>)
  endif()
  if(NKCF_CXX_HAS_${flag_id})
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${flag}>)
  endif()
endfunction()

function(nkcf_add_link_flag_if_supported target flag)
  nkcf_flag_id(flag_id "${flag}")
  check_linker_flag(CXX "${flag}" "NKCF_LINK_HAS_${flag_id}")
  if(NKCF_LINK_HAS_${flag_id})
    target_link_options(${target} PRIVATE "${flag}")
  endif()
endfunction()

function(nkcf_apply_target_defaults target)
  target_compile_options(${target} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic>
  )

  if(NKCF_ENABLE_HARDENING AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_property(TARGET ${target} PROPERTY POSITION_INDEPENDENT_CODE TRUE)
    target_compile_definitions(${target} PRIVATE
      $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:_FORTIFY_SOURCE=3>
    )
    foreach(flag
      -fPIE
      -fstack-protector-strong
      -fstack-clash-protection
      -fcf-protection=full
      -Wformat=2
    )
      nkcf_add_compile_flag_if_supported(${target} "${flag}")
    endforeach()
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C>:-Werror=format-security>)
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Werror=format-security>)
    endif()
    foreach(flag
      -pie
      LINKER:-z,relro
      LINKER:-z,now
      LINKER:-z,noexecstack
    )
      nkcf_add_link_flag_if_supported(${target} "${flag}")
    endforeach()
  endif()
endfunction()

if(NKCF_ENABLE_HARDENING AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  check_pie_supported()
endif()
set(NKCF_EXPECT_PIE OFF)
if(NKCF_ENABLE_HARDENING AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_LINK_PIE_SUPPORTED)
  set(NKCF_EXPECT_PIE ON)
endif()

find_package(PkgConfig REQUIRED)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core DBus)

pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client>=1.20)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon>=1.5)
include(cmake/Libei.cmake)

find_program(WAYLAND_SCANNER_EXECUTABLE wayland-scanner REQUIRED)

set(PROTOCOL_SOURCES)
foreach(PROTOCOL_NAME
  xdg-output-unstable-v1
  virtual-keyboard-unstable-v1
  wlr-virtual-pointer-unstable-v1
)
  set(PROTOCOL_XML ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${PROTOCOL_NAME}.xml)
  set(PROTOCOL_HEADER ${CMAKE_CURRENT_BINARY_DIR}/protocols/${PROTOCOL_NAME}-client-protocol.h)
  set(PROTOCOL_CODE ${CMAKE_CURRENT_BINARY_DIR}/protocols/${PROTOCOL_NAME}-protocol.c)

  add_custom_command(
    OUTPUT ${PROTOCOL_HEADER}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/protocols
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} client-header ${PROTOCOL_XML} ${PROTOCOL_HEADER}
    DEPENDS ${PROTOCOL_XML}
    VERBATIM
  )
  add_custom_command(
    OUTPUT ${PROTOCOL_CODE}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/protocols
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} private-code ${PROTOCOL_XML} ${PROTOCOL_CODE}
    DEPENDS ${PROTOCOL_XML} ${PROTOCOL_HEADER}
    VERBATIM
  )
  list(APPEND PROTOCOL_SOURCES ${PROTOCOL_HEADER} ${PROTOCOL_CODE})
endforeach()

add_executable(niri-kdeconnect-portal
  ${PROTOCOL_SOURCES}
  src/eis_input_bridge.cpp
  src/input_state.cpp
  src/key_resolver.cpp
  src/main.cpp
  src/portal_backend.cpp
  src/wayland_input.cpp
)

target_include_directories(niri-kdeconnect-portal PRIVATE
  src
  ${CMAKE_CURRENT_BINARY_DIR}/protocols
)
target_link_libraries(niri-kdeconnect-portal PRIVATE
  Qt6::Core
  Qt6::DBus
  nkcf-libeis
  PkgConfig::WAYLAND_CLIENT
  PkgConfig::XKBCOMMON
)
set(NKCF_DESKFLOW_EXECUTABLE "${CMAKE_INSTALL_PREFIX}/bin/deskflow-core"
  CACHE FILEPATH "Additional exact Deskflow core executable allowed for native portal sessions"
)
target_compile_definitions(niri-kdeconnect-portal PRIVATE
  NKCF_DESKFLOW_EXECUTABLE="${NKCF_DESKFLOW_EXECUTABLE}"
)
nkcf_apply_target_defaults(niri-kdeconnect-portal)

if(BUILD_TESTING)
  add_executable(key-resolver-test
    src/key_resolver.cpp
    tests/key_resolver_test.cpp
  )
  target_include_directories(key-resolver-test PRIVATE src)
  target_link_libraries(key-resolver-test PRIVATE
    Qt6::Core
    PkgConfig::XKBCOMMON
  )
  nkcf_apply_target_defaults(key-resolver-test)
  add_test(NAME key-resolver-test COMMAND key-resolver-test)

  add_executable(security-policy-test
    tests/security_policy_test.cpp
  )
  target_include_directories(security-policy-test PRIVATE src)
  target_link_libraries(security-policy-test PRIVATE
    Qt6::Core
    PkgConfig::XKBCOMMON
  )
  nkcf_apply_target_defaults(security-policy-test)
  add_test(NAME security-policy-test COMMAND security-policy-test)

  add_executable(deskflow-input-test
    tests/deskflow_input_test.cpp
    src/eis_input_bridge.cpp
    src/input_state.cpp
    src/key_resolver.cpp
  )
  target_include_directories(deskflow-input-test PRIVATE src)
  target_link_libraries(deskflow-input-test PRIVATE
    Qt6::Core
    nkcf-libeis
    nkcf-libei
    PkgConfig::XKBCOMMON
    PkgConfig::WAYLAND_CLIENT
  )
  nkcf_apply_target_defaults(deskflow-input-test)
  add_test(NAME deskflow-input-test COMMAND deskflow-input-test)

  add_executable(eis-repeat-test tests/eis_repeat_test.cpp)
  target_link_libraries(eis-repeat-test PRIVATE Qt6::Core nkcf-libeis nkcf-libei)
  nkcf_apply_target_defaults(eis-repeat-test)
  add_test(NAME eis-repeat-test COMMAND eis-repeat-test)
  set_tests_properties(eis-repeat-test PROPERTIES TIMEOUT 15)

  pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server>=1.20)
  set(TEST_SERVER_HEADERS)
  foreach(PROTOCOL_NAME virtual-keyboard-unstable-v1 wlr-virtual-pointer-unstable-v1)
    set(SERVER_HEADER ${CMAKE_CURRENT_BINARY_DIR}/protocols/${PROTOCOL_NAME}-server-protocol.h)
    add_custom_command(OUTPUT ${SERVER_HEADER}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/protocols
      COMMAND ${WAYLAND_SCANNER_EXECUTABLE} server-header
        ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${PROTOCOL_NAME}.xml ${SERVER_HEADER}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${PROTOCOL_NAME}.xml
      VERBATIM
    )
    list(APPEND TEST_SERVER_HEADERS ${SERVER_HEADER})
  endforeach()
  add_executable(wayland-keysym-test
    tests/wayland_keysym_test.cpp src/wayland_input.cpp src/input_state.cpp src/key_resolver.cpp
    ${PROTOCOL_SOURCES} ${TEST_SERVER_HEADERS}
  )
  target_include_directories(wayland-keysym-test PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/protocols)
  target_link_libraries(wayland-keysym-test PRIVATE Qt6::Core PkgConfig::WAYLAND_CLIENT
    PkgConfig::WAYLAND_SERVER PkgConfig::XKBCOMMON Threads::Threads)
  nkcf_apply_target_defaults(wayland-keysym-test)
  add_test(NAME wayland-keysym-test
    COMMAND ${CMAKE_COMMAND} -E env --unset=WAYLAND_DISPLAY --unset=WAYLAND_SOCKET --unset=DISPLAY
      $<TARGET_FILE:wayland-keysym-test>)
  set_tests_properties(wayland-keysym-test PROPERTIES TIMEOUT 15)

  find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session REQUIRED)
  add_executable(portal-session-test
    tests/portal_session_test.cpp
    src/portal_backend.cpp
    src/eis_input_bridge.cpp
    src/input_state.cpp
    src/key_resolver.cpp
  )
  target_include_directories(portal-session-test PRIVATE src)
  target_compile_definitions(portal-session-test PRIVATE NKCF_DESKFLOW_EXECUTABLE="${NKCF_DESKFLOW_EXECUTABLE}")
  target_link_libraries(portal-session-test PRIVATE
    Qt6::Core Qt6::DBus nkcf-libeis nkcf-libei
    PkgConfig::XKBCOMMON PkgConfig::WAYLAND_CLIENT
  )
  nkcf_apply_target_defaults(portal-session-test)
  add_test(NAME portal-session-test
    COMMAND ${CMAKE_COMMAND} -E env --unset=WAYLAND_DISPLAY --unset=WAYLAND_SOCKET --unset=DISPLAY
      ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:portal-session-test>
  )
  set_tests_properties(portal-session-test PROPERTIES TIMEOUT 30)

endif()

configure_file(data/niri-kdeconnect.portal.in niri-kdeconnect.portal @ONLY)
configure_file(data/org.freedesktop.impl.portal.desktop.niri_kdeconnect.service.in
  org.freedesktop.impl.portal.desktop.niri_kdeconnect.service
  @ONLY
)
configure_file(data/niri-kdeconnect-portal.service.in niri-kdeconnect-portal.service @ONLY)

install(TARGETS niri-kdeconnect-portal RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/niri-kdeconnect.portal
  DESTINATION ${CMAKE_INSTALL_DATADIR}/xdg-desktop-portal/portals
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.niri_kdeconnect.service
  DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/niri-kdeconnect-portal.service
  DESTINATION ${CMAKE_INSTALL_DATADIR}/systemd/user
)

if(BUILD_TESTING)
  add_test(NAME portal-metadata-test
    COMMAND ${CMAKE_COMMAND}
      -DNKCF_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
      -DNKCF_INSTALL_FULL_BINDIR=${CMAKE_INSTALL_FULL_BINDIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/portal_metadata_test.cmake
  )
  add_test(NAME hardening-readelf-test
    COMMAND ${CMAKE_COMMAND}
      -DNKCF_EXECUTABLE=$<TARGET_FILE:niri-kdeconnect-portal>
      -DNKCF_EXPECT_PIE=${NKCF_EXPECT_PIE}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/hardening_readelf_test.cmake
  )
endif()
