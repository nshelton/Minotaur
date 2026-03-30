cmake_minimum_required(VERSION 3.21)
project(Minotaur LANGUAGES CXX OBJCXX)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(glog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(CURL REQUIRED)

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
  src/*.cpp
  src/*.mm
)

# Add Windows icon resource
if(WIN32)
  set(RESOURCE_FILES resources/app.rc)
endif()

# Add macOS icon
if(APPLE)
  set(MACOSX_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/app_icon.icns)
  set_source_files_properties(${MACOSX_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

add_executable(minotaur ${SRC_FILES} ${RESOURCE_FILES} ${MACOSX_ICON})

if(APPLE)
  set_target_properties(minotaur PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_ICON_FILE app_icon.icns
    MACOSX_BUNDLE_BUNDLE_NAME "Minotaur"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.minotaur.app"
  )
endif()

target_include_directories(minotaur PRIVATE src)

target_link_libraries(minotaur PRIVATE glad::glad glfw imgui::imgui glog::glog fmt::fmt nlohmann_json::nlohmann_json CURL::libcurl)

if(WIN32)
  target_link_libraries(minotaur PRIVATE opengl32 ole32 windowscodecs)
elseif(APPLE)
  find_package(OpenGL REQUIRED)
  target_link_libraries(minotaur PRIVATE OpenGL::GL "-framework IOKit" "-framework CoreFoundation" "-framework AppKit")
else()
  find_package(OpenGL REQUIRED)
  target_link_libraries(minotaur PRIVATE OpenGL::GL)
endif()

enable_testing()

add_executable(minotaur_tests
  tests/test_kdtree.cpp
)

target_include_directories(minotaur_tests PRIVATE src)
target_link_libraries(minotaur_tests PRIVATE GTest::gtest GTest::gtest_main)

add_test(NAME minotaur_kdtree COMMAND minotaur_tests)

add_executable(motion_planner_tests
  tests/test_motion_planner.cpp
  src/plotters/MotionPlanner.cpp
)

target_include_directories(motion_planner_tests PRIVATE src)
target_link_libraries(motion_planner_tests PRIVATE GTest::gtest GTest::gtest_main)

add_test(NAME motion_planner COMMAND motion_planner_tests)

add_executable(filter_tests
  tests/test_filters.cpp
)

target_include_directories(filter_tests PRIVATE src)
target_link_libraries(filter_tests PRIVATE GTest::gtest GTest::gtest_main glog::glog fmt::fmt)

add_test(NAME filters COMMAND filter_tests)
