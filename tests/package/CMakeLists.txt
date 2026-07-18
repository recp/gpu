cmake_minimum_required(VERSION 3.20)
project(gpu_package_consumer LANGUAGES C)

find_package(gpu 0.1 CONFIG REQUIRED)

add_executable(gpu-package-consumer main.c)
target_link_libraries(gpu-package-consumer PRIVATE gpu::gpu)
