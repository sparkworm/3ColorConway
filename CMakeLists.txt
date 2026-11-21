cmake_minimum_required(VERSION 3.16)
project(3ColorConway LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)

add_executable(3ColorConway main.cpp)
target_link_libraries(3ColorConway PRIVATE glfw OpenGL::GL)
