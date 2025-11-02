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
  src/render/ImageRenderer.cpp
  src/utils/PathSetGenerator.cpp
  src/utils/BitmapGenerator.cpp
  src/utils/Serialization.cpp
  src/utils/ImageLoader.cpp

)

target_include_directories(minotaur PRIVATE src)

if (WIN32)
  target_link_libraries(minotaur PRIVATE glad::glad glfw imgui::imgui opengl32 glog::glog fmt::fmt nlohmann_json::nlohmann_json)
  target_link_libraries(minotaur PRIVATE ole32 windowscodecs)
endif()
