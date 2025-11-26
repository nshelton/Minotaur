cmake_minimum_required(VERSION 3.21)
project(Minotaur LANGUAGES CXX)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(glog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
  src/*.cpp
)

# Add Windows icon resource
if(WIN32)
  set(RESOURCE_FILES resources/app.rc)
endif()

add_executable(minotaur ${SRC_FILES} ${RESOURCE_FILES})

target_include_directories(minotaur PRIVATE src)

target_link_libraries(minotaur PRIVATE glad::glad glfw imgui::imgui opengl32 glog::glog fmt::fmt nlohmann_json::nlohmann_json)
target_link_libraries(minotaur PRIVATE ole32 windowscodecs)

enable_testing()

add_executable(minotaur_tests
  tests/test_kdtree.cpp
)

target_include_directories(minotaur_tests PRIVATE src)
target_link_libraries(minotaur_tests PRIVATE GTest::gtest GTest::gtest_main)

add_test(NAME minotaur_kdtree COMMAND minotaur_tests)
