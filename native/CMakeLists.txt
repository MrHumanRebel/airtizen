cmake_minimum_required(VERSION 3.16)
project(AirTizenCore C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(
  include
  ../third_party/AirPlayServerLib/include
  ../third_party/AirPlayServerLib/lib
)

file(GLOB AIRPLAY_LIB_SRC
  ../third_party/AirPlayServerLib/lib/*.c
  ../third_party/AirPlayServerLib/lib/crypto/*.c
  ../third_party/AirPlayServerLib/lib/curve25519/*.c
  ../third_party/AirPlayServerLib/lib/ed25519/*.c
)

set(AIRTIZEN_CORE_SRC
  src/airtizen_core.c
  ports/tizen_audio_output.c
  ports/tizen_socket_backend.c
)

add_library(airtizen_core STATIC
  ${AIRTIZEN_CORE_SRC}
  ${AIRPLAY_LIB_SRC}
)

find_package(Threads REQUIRED)
target_link_libraries(airtizen_core PRIVATE Threads::Threads)

add_executable(airtizen_runtime
  src/airtizen_runtime.c
)

target_link_libraries(airtizen_runtime PRIVATE airtizen_core Threads::Threads)
