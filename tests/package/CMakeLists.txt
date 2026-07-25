cmake_minimum_required(VERSION 3.20)
project(gpu_package_consumer LANGUAGES C CXX)

find_package(gpu 0.1 CONFIG REQUIRED)

get_target_property(gpu_target_type gpu::gpu TYPE)
if(gpu_target_type STREQUAL "STATIC_LIBRARY")
  foreach(dependency IN ITEMS us::us ds::ds)
    get_target_property(dependency_type ${dependency} TYPE)
    if(NOT dependency_type STREQUAL "STATIC_LIBRARY")
      message(FATAL_ERROR
        "static gpu package requires static ${dependency}"
      )
    endif()
  endforeach()
endif()

add_executable(gpu-package-consumer main.c)
target_link_libraries(gpu-package-consumer PRIVATE gpu::gpu)

add_executable(gpu-package-consumer-cxx main.cpp)
target_link_libraries(gpu-package-consumer-cxx PRIVATE gpu::gpu)
