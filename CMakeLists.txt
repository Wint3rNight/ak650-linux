cmake_minimum_required(VERSION 3.20)
project(ak650 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(ak650core STATIC
  src/device.cpp
  src/image.cpp
  src/third_party.cpp)
target_include_directories(ak650core PUBLIC src)
target_include_directories(ak650core SYSTEM PRIVATE third_party)
target_compile_options(ak650core PRIVATE -Wall -Wextra)
# stb and msf_gif are vendored as-is; their warnings aren't ours to fix.
set_source_files_properties(src/third_party.cpp PROPERTIES COMPILE_OPTIONS -w)

add_executable(ak650 src/main.cpp)
target_link_libraries(ak650 PRIVATE ak650core)
target_compile_options(ak650 PRIVATE -Wall -Wextra)

include(GNUInstallDirs)
install(TARGETS ak650 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

include(CTest)
if(BUILD_TESTING)
  add_executable(ak650_tests tests/test_image.cpp)
  target_link_libraries(ak650_tests PRIVATE ak650core)
  target_compile_definitions(ak650_tests PRIVATE
    TEST_PATTERN="${CMAKE_SOURCE_DIR}/test-pattern.gif")
  add_test(NAME image COMMAND ak650_tests)
endif()
