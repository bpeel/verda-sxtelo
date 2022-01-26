# This is a stub CMake file to sneakily divert to building with meson

cmake_minimum_required(VERSION 3.10)
project(Anagrams)

add_custom_target(CROSS_TXT
  COMMAND ${CMAKE_SOURCE_DIR}/generate_cross.py
          "${CMAKE_C_COMPILER}"
          "${CMAKE_C_COMPILER_TARGET}"
          "${CMAKE_C_FLAGS}"
          "${CMAKE_ANDROID_ARCH}"
  DEPENDS ${CMAKE_SOURCE_DIR}/generate_cross.py
  )

add_library(freetype SHARED stub.c)

add_custom_command(
  TARGET freetype
  POST_BUILD
  COMMAND ${CMAKE_SOURCE_DIR}/build-freetype-ninja.sh
  ARGS $<TARGET_FILE:freetype>
  )

add_library(anagrams SHARED stub.c)

add_custom_command(
  TARGET anagrams
  POST_BUILD
  COMMAND ${CMAKE_SOURCE_DIR}/build-lib-ninja.sh
  ARGS $<TARGET_FILE:anagrams>
)

add_dependencies(anagrams freetype CROSS_TXT)
add_dependencies(freetype CROSS_TXT)
