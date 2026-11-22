cmake_minimum_required(VERSION 3.16)
project(3ColorConway LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

# Fetch the pinned third-party sources so builds use reproducible dependency versions.
include(FetchContent)
# Generate and build a GLAD loader containing the OpenGL 4.3 core API.
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.8
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glad)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
    GIT_SHALLOW TRUE
)
# Dear ImGui has no CMake project, so fetch its sources and build the core/backends below.
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    cmake_policy(PUSH)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(imgui)
    cmake_policy(POP)
endif()

list(APPEND CMAKE_MODULE_PATH "${glad_SOURCE_DIR}/cmake")
set(GLAD_CMAKE_DIR "${glad_SOURCE_DIR}/cmake")
set(GLAD_SOURCES_DIR "${glad_SOURCE_DIR}")
include(GladConfig)
glad_add_library(glad_gl_core_43 REPRODUCIBLE API gl:core=4.3)

find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)

add_library(imgui STATIC
    # Build Dear ImGui itself plus the GLFW input and OpenGL rendering backends.
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
)
target_include_directories(imgui PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

add_executable(3ColorConway
    src/main.cpp
    src/application.cpp
    src/camera.cpp
    src/persistence.cpp
    src/shaders.cpp
    src/simulation.cpp
    src/ui.cpp
)
# Keep application headers private and provide the configured runtime shader path.
target_include_directories(3ColorConway PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(3ColorConway PRIVATE glfw OpenGL::GL glad_gl_core_43 imgui)
target_compile_definitions(3ColorConway PRIVATE
    SHADER_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/shaders"
)

if(BUILD_TESTING)
    # Persistence tests link only the non-graphics code they exercise.
    add_executable(persistence_tests
        tests/persistence_tests.cpp
        src/persistence.cpp
        src/simulation.cpp
    )
    target_include_directories(persistence_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    add_test(NAME persistence_tests COMMAND persistence_tests)
endif()

# Copy GLSL files beside the executable so it can load them when launched from the build directory.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")
foreach(SHADER update.comp paint.comp display.vert display.frag)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER}"
        COPYONLY
    )
endforeach()
