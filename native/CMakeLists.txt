cmake_minimum_required(VERSION 3.19)
project(AirTizenCore C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

option(AIRTIZEN_BUILD_NACL_MODULE "Build the complete WGT-embedded NaCl receiver" OFF)
option(AIRTIZEN_BUILD_TIZEN_SERVICE "Build service_app executable (requires Tizen native SDK)" OFF)

if(AIRTIZEN_BUILD_NACL_MODULE)
  include_directories(SYSTEM "$ENV{NACL_SDK_ROOT}/include" "$ENV{NACL_SDK_ROOT}/include/newlib")
endif()

set(AIRPLAY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/AirPlayServerLib/lib")
set(AIRPLAY_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-airplay")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_donor_sources.py")
execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_donor_sources.py"
          "${AIRPLAY_DIR}" "${AIRPLAY_GENERATED_DIR}"
  COMMAND_ERROR_IS_FATAL ANY
)
include_directories(
  include
  ports
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
if(BUILD_TESTING AND NOT AIRTIZEN_BUILD_NACL_MODULE AND NOT AIRTIZEN_BUILD_TIZEN_SERVICE)
  add_subdirectory("${AIRPLAY_DIR}/fdk-aac" fdk-aac)
endif()
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

# The bundled fixed-point codec and ref10 crypto require wrapping signed arithmetic.
# Make that arithmetic part of the compiler contract instead of allowing optimizers
# to assume it never overflows. Sanitizers remain enabled; no diagnostic suppression.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  if(TARGET fdk-aac)
    target_compile_options(fdk-aac PRIVATE -fwrapv)
  endif()
  target_compile_options(playfair PRIVATE -fwrapv)
  set_source_files_properties(${AIRPLAY_LIB_SRC} PROPERTIES COMPILE_OPTIONS "-fwrapv")
endif()

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
set(AIRTIZEN_MEDIA_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/../../.airtizen-deps/media" CACHE PATH "Target FFmpeg installation prefix")
set(ENV{PKG_CONFIG_PATH} "${AIRTIZEN_MEDIA_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
pkg_check_modules(AIRTIZEN_AVCODEC REQUIRED IMPORTED_TARGET libavformat>=61 libavcodec>=61 libavutil>=59)

add_library(airtizen_core STATIC
  src/airtizen_core.c
  src/airtizen_json.c
  src/airtizen_alac.c
  src/airtizen_aac.c
  src/airtizen_h264.c
  src/airtizen_video.c
  src/airtizen_playback.c
  src/airtizen_metadata.c
  src/airtizen_mdns.c
  ports/tizen_audio_output.c
  ports/tizen_socket_backend.c
  ${AIRPLAY_LIB_SRC}
)
target_link_libraries(airtizen_core PRIVATE Threads::Threads playfair plist m PkgConfig::AIRTIZEN_AVCODEC)

add_library(airtizen_runtime_lib STATIC
  src/airtizen_runtime.c
  src/airtizen_control_http.c
)
target_compile_definitions(airtizen_runtime_lib PRIVATE AIRTIZEN_RUNTIME_NO_MAIN=1)
target_link_libraries(airtizen_runtime_lib PUBLIC airtizen_core Threads::Threads)

if(AIRTIZEN_BUILD_NACL_MODULE)
  if(AIRTIZEN_BUILD_TIZEN_SERVICE)
    message(FATAL_ERROR "NaCl and native ServiceApp are different build targets")
  endif()
  set(NACL_SDK "$ENV{NACL_SDK_ROOT}")
  add_executable(airtizen_receiver nacl/receiver_module.cc nacl/audio_queue.c)
  set_target_properties(airtizen_receiver PROPERTIES SUFFIX ".nexe")
  target_include_directories(airtizen_receiver PRIVATE "${NACL_SDK}/include" nacl)
  target_link_directories(airtizen_receiver PRIVATE "${NACL_SDK}/lib/clang-newlib_arm/Release")
  target_link_libraries(airtizen_receiver PRIVATE airtizen_runtime_lib nacl_io ppapi_cpp ppapi pthread)
else()
  add_executable(airtizen_runtime src/airtizen_runtime.c src/airtizen_control_http.c)
  target_link_libraries(airtizen_runtime PRIVATE airtizen_core Threads::Threads)
endif()

if(AIRTIZEN_BUILD_TIZEN_SERVICE)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(TIZEN_SERVICE REQUIRED IMPORTED_TARGET service-app dlog audio-io)
  # Compile the audio boundary and status for Tizen, not the Linux test sink.
  target_compile_definitions(airtizen_core PRIVATE __TIZEN__=1)
  target_compile_definitions(airtizen_runtime_lib PRIVATE __TIZEN__=1)
  target_compile_definitions(airtizen_runtime PRIVATE __TIZEN__=1)
  target_link_libraries(airtizen_core PRIVATE PkgConfig::TIZEN_SERVICE)
  add_executable(airtizen_tizen_service src/airtizen_tizen_service.c)
  target_link_libraries(airtizen_tizen_service PRIVATE airtizen_runtime_lib PkgConfig::TIZEN_SERVICE)
endif()

if(BUILD_TESTING AND NOT AIRTIZEN_BUILD_TIZEN_SERVICE AND NOT AIRTIZEN_BUILD_NACL_MODULE)
  add_executable(airtizen_nacl_platform_test ../tests/test_nacl_platform.c)
  target_link_libraries(airtizen_nacl_platform_test PRIVATE airtizen_core)
  target_compile_options(airtizen_nacl_platform_test PRIVATE -UNDEBUG)
  add_test(NAME nacl_platform_and_audio_teardown COMMAND airtizen_nacl_platform_test)
  set_tests_properties(nacl_platform_and_audio_teardown PROPERTIES TIMEOUT 5)
  add_executable(airtizen_nacl_audio_test ../tests/test_nacl_audio.c nacl/audio_queue.c)
  target_include_directories(airtizen_nacl_audio_test PRIVATE nacl)
  target_link_libraries(airtizen_nacl_audio_test PRIVATE Threads::Threads)
  target_compile_options(airtizen_nacl_audio_test PRIVATE -UNDEBUG)
  add_test(NAME nacl_audio_queue COMMAND airtizen_nacl_audio_test)
  add_executable(airtizen_core_test ../tests/test_core.c src/airtizen_core.c src/airtizen_json.c src/airtizen_video.c src/airtizen_metadata.c)
  target_link_libraries(airtizen_core_test PRIVATE Threads::Threads m PkgConfig::AIRTIZEN_AVCODEC)
  # Assertions remain enabled even when the application is built in Release mode.
  target_compile_options(airtizen_core_test PRIVATE -UNDEBUG)
  add_executable(airtizen_codec_test ../tests/test_codec.c)
  target_link_libraries(airtizen_codec_test PRIVATE airtizen_core fdk-aac)
  target_compile_options(airtizen_codec_test PRIVATE -UNDEBUG)
  add_executable(airtizen_alac_test ../tests/test_alac.c)
  target_link_libraries(airtizen_alac_test PRIVATE airtizen_core)
  target_compile_options(airtizen_alac_test PRIVATE -UNDEBUG)
  target_compile_definitions(airtizen_alac_test PRIVATE AIRTIZEN_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures")
  add_executable(airtizen_video_test ../tests/test_video.c)
  target_link_libraries(airtizen_video_test PRIVATE airtizen_runtime_lib PkgConfig::AIRTIZEN_AVCODEC)
  target_compile_options(airtizen_video_test PRIVATE -UNDEBUG)
  target_compile_definitions(airtizen_video_test PRIVATE AIRTIZEN_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures")
  add_executable(airtizen_mirror_test ../tests/test_mirror_transport.c)
  target_link_libraries(airtizen_mirror_test PRIVATE airtizen_core)
  target_compile_options(airtizen_mirror_test PRIVATE -UNDEBUG)
  add_executable(airtizen_metadata_test ../tests/test_metadata.c src/airtizen_metadata.c)
  target_link_libraries(airtizen_metadata_test PRIVATE Threads::Threads)
  target_compile_options(airtizen_metadata_test PRIVATE -UNDEBUG)
  add_test(NAME music_metadata COMMAND airtizen_metadata_test)
  add_test(NAME encrypted_mirror_transport COMMAND airtizen_mirror_test)
  set_tests_properties(encrypted_mirror_transport PROPERTIES TIMEOUT 10)
  add_test(NAME fragmented_h264 COMMAND airtizen_video_test)
  add_test(NAME encrypted_alac COMMAND airtizen_alac_test)
  add_test(NAME encrypted_aac COMMAND airtizen_codec_test)
  add_test(NAME core_callbacks COMMAND airtizen_core_test)
  add_test(NAME runtime_integration COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_runtime.py")
  set_tests_properties(runtime_integration PROPERTIES ENVIRONMENT "AIRTIZEN_BINARY=$<TARGET_FILE:airtizen_runtime>" TIMEOUT 60)
endif()
