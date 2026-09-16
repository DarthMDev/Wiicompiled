cmake_minimum_required(VERSION 3.25)
project(metal_rt_probe LANGUAGES CXX OBJCXX)

if(NOT APPLE)
  message(FATAL_ERROR "The Metal hardware-ray-tracing probe requires an Apple platform")
endif()

# The APIs used here first appeared in macOS 11 / iOS 14. The standalone probe
# uses Aurora's pinned Dawn package without changing the renderer's deployment target.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(Dawn CONFIG REQUIRED)
add_executable(metal_rt_probe main.mm)
target_compile_options(metal_rt_probe PRIVATE -fobjc-arc -Wall -Wextra)
target_link_libraries(metal_rt_probe PRIVATE dawn::webgpu_dawn
  "-framework Foundation" "-framework Metal")

enable_testing()
add_test(NAME metal_rt_probe COMMAND metal_rt_probe)
set_tests_properties(metal_rt_probe PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
