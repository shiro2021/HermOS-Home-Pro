cmake_minimum_required(VERSION 3.16)
project(hermes_agent_desktop CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(HERMES_DEPS "" CACHE PATH "directory with imgui/, glfw/, json/ (see fetch-deps.sh)")
set(HERMES_FONT "" CACHE FILEPATH "TTF to embed")
if(NOT HERMES_DEPS OR NOT HERMES_FONT)
  message(FATAL_ERROR "set -DHERMES_DEPS=... and -DHERMES_FONT=...")
endif()

# ---- generated font header
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(FONT_HEADER ${CMAKE_BINARY_DIR}/gen/font_data.h)
add_custom_command(OUTPUT ${FONT_HEADER}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/gen
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/embed-font.py ${HERMES_FONT} ${FONT_HEADER} font_inter_regular
  DEPENDS ${HERMES_FONT} ${CMAKE_SOURCE_DIR}/embed-font.py)

# ---- the platform-neutral core (+ the platform's HTTPS backend)
add_library(hermes_core STATIC src/agent.cpp)
target_include_directories(hermes_core PUBLIC src ${HERMES_DEPS}/json/single_include)
if(WIN32)
  target_sources(hermes_core PRIVATE src/http_winhttp.cpp)
  target_link_libraries(hermes_core PUBLIC winhttp shell32 ole32)
else()
  find_package(CURL REQUIRED)
  target_sources(hermes_core PRIVATE src/http_curl.cpp)
  target_link_libraries(hermes_core PUBLIC CURL::libcurl)
endif()

# ---- tests of the core (no network)
add_executable(agent_test tests/agent_test.cpp)
target_link_libraries(agent_test hermes_core)

# ---- GLFW (X11 only on Linux: no Wayland development packages needed)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
add_subdirectory(${HERMES_DEPS}/glfw ${CMAKE_BINARY_DIR}/glfw EXCLUDE_FROM_ALL)

# ---- Dear ImGui
set(IMGUI ${HERMES_DEPS}/imgui)
add_library(imgui STATIC
  ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
  ${IMGUI}/backends/imgui_impl_glfw.cpp ${IMGUI}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${IMGUI} ${IMGUI}/backends)
target_link_libraries(imgui PUBLIC glfw)

# ---- the program
add_executable(hermes-agent src/main.cpp ${FONT_HEADER})
target_include_directories(hermes-agent PRIVATE ${CMAKE_BINARY_DIR}/gen)
target_link_libraries(hermes-agent hermes_core imgui glfw)
if(WIN32)
  target_link_libraries(hermes-agent opengl32 gdi32)
  target_link_options(hermes-agent PRIVATE -mwindows -static -static-libgcc -static-libstdc++)
  target_link_options(agent_test PRIVATE -static -static-libgcc -static-libstdc++)
else()
  target_link_libraries(hermes-agent GL pthread dl)
endif()
