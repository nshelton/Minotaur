cmake_minimum_required(VERSION 3.21)
project(Minotaur LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

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
  src/utils/PathSetGenerator.cpp

)

target_include_directories(minotaur PRIVATE src)

if (WIN32)
  target_link_libraries(minotaur PRIVATE glad::glad glfw imgui::imgui opengl32)
else()
  target_link_libraries(minotaur PRIVATE glad::glad glfw imgui::imgui)
endif()
