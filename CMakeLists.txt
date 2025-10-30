cmake_minimum_required(VERSION 3.21)
project(MinotaurTriangle LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)

add_executable(minotaur
  src/main.cpp
  src/app/App.cpp
  src/screens/MainScreen.cpp
  src/mvc/TriangleView.cpp
  src/mvc/TriangleController.cpp
)

target_include_directories(minotaur PRIVATE src)

if (WIN32)
  target_link_libraries(minotaur PRIVATE glad::glad glfw opengl32)
else()
  target_link_libraries(minotaur PRIVATE glad::glad glfw)
endif()
