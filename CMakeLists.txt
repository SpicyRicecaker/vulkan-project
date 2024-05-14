set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

cmake_minimum_required(VERSION 3.10)
project(vulkan_project)

add_executable(vulkan_project main.cpp "lib.h")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

if(WIN32)
  # Enable Hot Reload for MSVC compilers if supported.
  if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
  endif()
elseif(APPLE)
  include(CMakePrintHelpers)
  
  set(Vulkan_LIBRARY "$ENV{VULKAN_SDK}/lib/libvulkan.dylib")
  set(Vulkan_INCLUDE_DIR "$ENV{VULKAN_SDK}/include")
  
  # cmake_print_variables(Vulkan_LIBRARY Vulkan_INCLUDE_DIR)
  # cmake_print_variables(CMAKE_INCLUDE_PATH CMAKE_SYSTEM_INCLUDE_PATH CMAKE_PREFIX_PATH)
endif()

find_package(Vulkan REQUIRED shaderc_combined)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

target_link_libraries(vulkan_project Vulkan::Vulkan Vulkan::Headers Vulkan::shaderc_combined glfw glm::glm fmt::fmt)
