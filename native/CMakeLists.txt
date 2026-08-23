cmake_minimum_required(VERSION 3.16)
project(AirTizenCore C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(AIRTIZEN_BUILD_TIZEN_SERVICE "Build service_app executable (requires Tizen native SDK)" OFF)

set(AIRPLAY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/AirPlayServerLib/lib")
set(AIRPLAY_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-airplay")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_donor_sources.py"
          "${AIRPLAY_DIR}" "${AIRPLAY_GENERATED_DIR}"
  COMMAND_ERROR_IS_FATAL ANY
)
include_directories(
  include
  "${AIRPLAY_GENERATED_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/AirPlayServerLib/include"
  "${AIRPLAY_DIR}"
  "${AIRPLAY_DIR}/fdk-aac/libAACdec/include"
  "${AIRPLAY_DIR}/fdk-aac/libAACenc/include"
  "${AIRPLAY_DIR}/fdk-aac/libFDK/include"
  "${AIRPLAY_DIR}/fdk-aac/libMpegTPDec/include"
  "${AIRPLAY_DIR}/fdk-aac/libMpegTPEnc/include"
  "${AIRPLAY_DIR}/fdk-aac/libPCMutils/include"
  "${AIRPLAY_DIR}/fdk-aac/libSBRdec/include"
  "${AIRPLAY_DIR}/fdk-aac/libSBRenc/include"
  "${AIRPLAY_DIR}/fdk-aac/libSYS/include"
)

# Donor CMake files contain decoder/support sources omitted by the old glob.
add_subdirectory("${AIRPLAY_DIR}/fdk-aac" fdk-aac)
add_subdirectory("${AIRPLAY_DIR}/playfair" playfair)
add_subdirectory("${AIRPLAY_DIR}/plist" plist)

file(GLOB AIRPLAY_LIB_SRC
  "${AIRPLAY_DIR}/*.c"
  "${AIRPLAY_DIR}/crypto/*.c"
  "${AIRPLAY_DIR}/curve25519/*.c"
  "${AIRPLAY_DIR}/ed25519/*.c"
)
# AirTizen provides mDNS itself. airplay.c is video-only and contains C++ syntax.
list(FILTER AIRPLAY_LIB_SRC EXCLUDE REGEX "/(airplay|dnssd)\\.c$")
list(FILTER AIRPLAY_LIB_SRC EXCLUDE REGEX "/(raop|raop_buffer|raop_rtp_mirror)\\.c$")
list(APPEND AIRPLAY_LIB_SRC
  "${AIRPLAY_GENERATED_DIR}/raop.generated.c"
  "${AIRPLAY_GENERATED_DIR}/raop_buffer.generated.c"
  "${AIRPLAY_GENERATED_DIR}/raop_rtp_mirror.generated.c"
)

find_package(Threads REQUIRED)

add_library(airtizen_core STATIC
  src/airtizen_core.c
  src/airtizen_mdns.c
  ports/tizen_audio_output.c
  ports/tizen_socket_backend.c
  ${AIRPLAY_LIB_SRC}
)
target_link_libraries(airtizen_core PRIVATE Threads::Threads fdk-aac playfair plist m)

add_library(airtizen_runtime_lib STATIC
  src/airtizen_runtime.c
  src/airtizen_control_http.c
)
target_compile_definitions(airtizen_runtime_lib PRIVATE AIRTIZEN_RUNTIME_NO_MAIN=1)
target_link_libraries(airtizen_runtime_lib PUBLIC airtizen_core Threads::Threads)

add_executable(airtizen_runtime src/airtizen_runtime.c src/airtizen_control_http.c)
target_link_libraries(airtizen_runtime PRIVATE airtizen_core Threads::Threads)

if(AIRTIZEN_BUILD_TIZEN_SERVICE)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(TIZEN_SERVICE REQUIRED service-app dlog audio-io)
  add_executable(airtizen_tizen_service src/airtizen_tizen_service.c)
  target_include_directories(airtizen_tizen_service PRIVATE ${TIZEN_SERVICE_INCLUDE_DIRS})
  target_compile_options(airtizen_tizen_service PRIVATE ${TIZEN_SERVICE_CFLAGS_OTHER})
  target_link_directories(airtizen_tizen_service PRIVATE ${TIZEN_SERVICE_LIBRARY_DIRS})
  target_link_libraries(airtizen_tizen_service PRIVATE airtizen_runtime_lib ${TIZEN_SERVICE_LIBRARIES})
endif()
