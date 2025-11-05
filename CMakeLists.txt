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

add_executable(minotaur
  src/main.cpp
  src/app/App.cpp
  src/screens/MainScreen.cpp
  src/screens/MainScreen.gui.cpp

  src/Interaction.cpp
  src/Renderer.cpp
  src/Camera.cpp
  src/Page.cpp

  src/render/LineRenderer.cpp
  src/render/BitmapRenderer.cpp
  src/render/FloatImageRenderer.cpp
  src/utils/VectorFont.cpp
  src/utils/PathSetGenerator.cpp
  src/utils/BitmapGenerator.cpp
  src/utils/Serialization.cpp
  src/utils/ImageLoader.cpp

  # Serial/Plotter
  src/serial/SerialController.cpp
  src/plotters/AxidrawController.cpp
  src/plotters/MotionPlanner.cpp
  src/plotters/PlotSpooler.cpp

  # Filters
  src/filters/FilterRegistry.cpp
  src/filters/bitmap/BlurFilter.cpp
  src/filters/bitmap/ThresholdFilter.cpp
  src/filters/pathset/SimplifyFilter.cpp
  src/filters/bitmap/TraceFilter.cpp
  src/filters/bitmap/TraceBlobsFilter.cpp
  src/filters/bitmap/SkeletonizeFilter.cpp
  src/filters/bitmap/LevelsFilter.cpp
  src/filters/bitmap/LineHatchFilter.cpp
  src/filters/pathset/SmoothFilter.cpp
  src/filters/pathset/LaplacianSmoothFilter.cpp
  src/filters/pathset/OptimizePathsFilter.cpp
  src/filters/pathset/CurlNoiseFilter.cpp
  src/filters/bitmap/CannyFilter.cpp
  src/filters/bitmap/ClaheFilter.cpp
  src/filters/bitmap/BitmapToFloatFilter.cpp

)

target_include_directories(minotaur PRIVATE src)

if (WIN32)
  target_link_libraries(minotaur PRIVATE glad::glad glfw imgui::imgui opengl32 glog::glog fmt::fmt nlohmann_json::nlohmann_json)
  target_link_libraries(minotaur PRIVATE ole32 windowscodecs)
endif()

enable_testing()

add_executable(minotaur_tests
  tests/test_kdtree.cpp
)

target_include_directories(minotaur_tests PRIVATE src)
target_link_libraries(minotaur_tests PRIVATE GTest::gtest GTest::gtest_main)

add_test(NAME minotaur_kdtree COMMAND minotaur_tests)
