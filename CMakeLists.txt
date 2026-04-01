cmake_minimum_required(VERSION 3.21)
if(APPLE)
  project(Minotaur LANGUAGES CXX OBJCXX)
else()
  project(Minotaur LANGUAGES CXX)
endif()


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
  )
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

find_package(glog CONFIG QUIET)
if(NOT glog_FOUND)
  FetchContent_Declare(
    glog
    GIT_REPOSITORY https://github.com/google/glog.git
    GIT_TAG        v0.7.1
    GIT_SHALLOW    TRUE
  )
  set(WITH_GFLAGS OFF CACHE BOOL "" FORCE)
  set(WITH_GTEST OFF CACHE BOOL "" FORCE)
  set(WITH_UNWIND OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(glog)
endif()

option(BUILD_APP "Build the main Minotaur application" ON)

if(BUILD_APP)
  find_package(glfw3 CONFIG REQUIRED)
  find_package(glad CONFIG REQUIRED)
  find_package(imgui CONFIG REQUIRED)
  find_package(fmt CONFIG REQUIRED)
  find_package(nlohmann_json CONFIG REQUIRED)
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
  tests/test_blur_filter.cpp
  tests/test_threshold_filter.cpp
  tests/test_levels_filter.cpp
  tests/test_trace_filter.cpp
  tests/test_simplify_filter.cpp
  src/filters/bitmap/BlurFilter.cpp
  src/filters/bitmap/ThresholdFilter.cpp
  src/filters/bitmap/LevelsFilter.cpp
  src/filters/bitmap/TraceFilter.cpp
  src/filters/pathset/SimplifyFilter.cpp
)

target_include_directories(filter_tests PRIVATE src)
target_link_libraries(filter_tests PRIVATE GTest::gtest GTest::gtest_main glog::glog)

add_test(NAME filter_tests COMMAND filter_tests)

add_executable(filterchain_tests
  tests/test_filterchain.cpp
)

target_include_directories(filterchain_tests PRIVATE src tests)
target_link_libraries(filterchain_tests PRIVATE GTest::gtest glog::glog)

add_test(NAME filterchain_tests COMMAND filterchain_tests)

add_executable(generator_tests
  tests/test_generators.cpp
)

target_include_directories(generator_tests PRIVATE src)
target_link_libraries(generator_tests PRIVATE GTest::gtest glog::glog)

add_test(NAME generator_tests COMMAND generator_tests)

if(BUILD_APP)
  add_executable(serialization_tests
    tests/test_serialization.cpp
    src/utils/Serialization.cpp
    src/utils/ImageCompression.cpp
    src/utils/VectorFont.cpp
    src/Page.cpp
    src/filters/FilterRegistry.cpp
    src/generators/GeneratorRegistry.cpp
    src/filters/bitmap/BlurFilter.cpp
    src/filters/bitmap/ThresholdFilter.cpp
    src/filters/bitmap/LevelsFilter.cpp
    src/filters/bitmap/TraceFilter.cpp
    src/filters/bitmap/TraceBlobsFilter.cpp
    src/filters/bitmap/CannyFilter.cpp
    src/filters/bitmap/LineHatchFilter.cpp
    src/filters/bitmap/SkeletonizeFilter.cpp
    src/filters/bitmap/ClaheFilter.cpp
    src/filters/bitmap/BitmapToFloatFilter.cpp
    src/filters/bitmap/FloatToPathFilter.cpp
    src/filters/bitmap/FloatBlurFilter.cpp
    src/filters/bitmap/VoronoiStipplingFilter.cpp
    src/filters/bitmap/ErodeDilateFilter.cpp
    src/filters/bitmap/FlowFieldHatchFilter.cpp
    src/filters/bitmap/FlowSnakeFilter.cpp
    src/filters/bitmap/RotateFilter.cpp
    src/filters/bitmap/ConcentricOutlineFilter.cpp
    src/filters/pathset/SimplifyFilter.cpp
    src/filters/pathset/SmoothFilter.cpp
    src/filters/pathset/OptimizePathsFilter.cpp
    src/filters/pathset/LaplacianSmoothFilter.cpp
    src/filters/pathset/CurlNoiseFilter.cpp
    src/filters/pathset/RegularSubdivisionFilter.cpp
  )

  target_include_directories(serialization_tests PRIVATE src)
  target_link_libraries(serialization_tests PRIVATE
    GTest::gtest glog::glog fmt::fmt nlohmann_json::nlohmann_json glad::glad CURL::libcurl
  )

  add_test(NAME serialization_tests COMMAND serialization_tests)
endif()
