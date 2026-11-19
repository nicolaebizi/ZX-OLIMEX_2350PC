# Host (PC) unit tests for the portable stream classes.
#
#   cmake -S test -B build/test
#   make -C build/test
#   (cd build/test && ctest --output-on-failure)
#
cmake_minimum_required(VERSION 3.12)

project(pico-emu-utils-tests C CXX)

set(CMAKE_CXX_STANDARD 17)
add_compile_options(-Wall -g -fsanitize=address,undefined)
add_link_options(-fsanitize=address,undefined)

get_filename_component(PICO_FOLDER ${CMAKE_CURRENT_LIST_DIR}/../.. ABSOLUTE)
get_filename_component(PICO_PROJECT_FOLDER ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)

include(CTest)

add_library(Unity STATIC ${PICO_FOLDER}/Unity/src/unity.c)
target_include_directories(Unity PUBLIC ${PICO_FOLDER}/Unity/src)

add_executable(streams_tests
  ${PICO_PROJECT_FOLDER}/src/InputStream.cpp
  ${PICO_PROJECT_FOLDER}/src/BufferedInputStream.cpp
  test_streams.cpp
)
target_include_directories(streams_tests PRIVATE ${PICO_PROJECT_FOLDER}/src)
target_link_libraries(streams_tests Unity)
add_test(NAME streams_suite COMMAND streams_tests)
set_tests_properties(streams_suite PROPERTIES TIMEOUT 10)
