cmake_minimum_required(VERSION 3.22.1)
project(v1_readonly_overlay)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JNI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../jni)
set(IMGUI_DIR ${JNI_DIR}/Modules/ImGui)

file(GLOB IMGUI_SOURCES
    ${IMGUI_DIR}/*.cpp
    ${IMGUI_DIR}/backends/imgui_impl_android.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

add_library(MEOW SHARED
    core_engine.cpp
    imgui_renderer.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(MEOW PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_compile_definitions(MEOW PRIVATE IMGUI_IMPL_OPENGL_ES3)

find_library(log-lib log)
find_library(android-lib android)
find_library(EGL-lib EGL)
find_library(GLESv3-lib GLESv3)

target_link_libraries(MEOW
    ${log-lib}
    ${android-lib}
    ${EGL-lib}
    ${GLESv3-lib}
)
