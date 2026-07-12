cmake_minimum_required(VERSION 3.20)

if(APPLE)
  project(gpu VERSION 0.1.0 LANGUAGES C OBJC)
else()
  project(gpu VERSION 0.1.0 LANGUAGES C)
endif()

include(GNUInstallDirs)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(GPU_ENABLE_VULKAN "Build the Vulkan backend" OFF)
option(GPU_BUILD_TESTS "Build GPU validation tests" OFF)
option(GPU_BUILD_SAMPLES "Build GPU samples" OFF)

set(GPU_USL_ROOT "" CACHE PATH "UniversalShading/us checkout")
set(GPU_USL_INCLUDE_DIR "" CACHE PATH "USL public include directory")
set(GPU_USL_LIBRARY "" CACHE FILEPATH "Prebuilt USL library")

set(GPU_CORE_SOURCES
  src/init.c
  src/api/barrier.c
  src/api/buffer.c
  src/api/cmd/cmdbuf.c
  src/api/cmd/cmdque.c
  src/api/compute.c
  src/api/descr/descriptor.c
  src/api/device.c
  src/api/frame.c
  src/api/instance.c
  src/api/library.c
  src/api/pass/renderpass.c
  src/api/query.c
  src/api/render/pipeline.c
  src/api/render/rce.c
  src/api/sampler.c
  src/api/surface.c
  src/api/swapchain.c
  src/api/texture.c
  src/api/vertex.c
  src/backend/backend.c
  src/backend/gl/api.c
  src/backend/gl/impl/device.c
  src/backend/gl/impl/render/pipeline.c
)

add_library(gpu ${GPU_CORE_SOURCES})
add_library(gpu::gpu ALIAS gpu)

set_target_properties(gpu PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED YES
  C_EXTENSIONS NO
  POSITION_INDEPENDENT_CODE ON
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_include_directories(gpu
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(BUILD_SHARED_LIBS)
  target_compile_definitions(gpu PRIVATE GPU_EXPORTS)
else()
  target_compile_definitions(gpu PUBLIC GPU_STATIC)
endif()

if(MSVC)
  target_compile_options(gpu PRIVATE /W3)
  target_compile_definitions(gpu PRIVATE
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_DEPRECATE
  )
else()
  target_compile_options(gpu PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
  )
endif()

if(NOT GPU_USL_ROOT)
  if(DEFINED ENV{USL_ROOT} AND EXISTS "$ENV{USL_ROOT}/us/CMakeLists.txt")
    set(GPU_USL_ROOT "$ENV{USL_ROOT}" CACHE PATH "UniversalShading/us checkout" FORCE)
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../UniversalShading/us/us/CMakeLists.txt")
    get_filename_component(
      _gpu_usl_root
      "${CMAKE_CURRENT_SOURCE_DIR}/../UniversalShading/us"
      ABSOLUTE
    )
    set(GPU_USL_ROOT "${_gpu_usl_root}" CACHE PATH "UniversalShading/us checkout" FORCE)
  endif()
endif()

if(TARGET us)
  target_link_libraries(gpu PRIVATE us)
elseif(GPU_USL_ROOT AND EXISTS "${GPU_USL_ROOT}/us/CMakeLists.txt")
  set(US_USE_TEST OFF CACHE BOOL "" FORCE)
  add_subdirectory("${GPU_USL_ROOT}/us" "${CMAKE_BINARY_DIR}/_deps/us")
  target_link_libraries(gpu PRIVATE us)
elseif(GPU_USL_INCLUDE_DIR AND GPU_USL_LIBRARY)
  add_library(gpu_usl UNKNOWN IMPORTED)
  set_target_properties(gpu_usl PROPERTIES
    IMPORTED_LOCATION "${GPU_USL_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${GPU_USL_INCLUDE_DIR}"
  )
  target_link_libraries(gpu PRIVATE gpu_usl)
else()
  message(FATAL_ERROR
    "USL not found. Set GPU_USL_ROOT or GPU_USL_INCLUDE_DIR and GPU_USL_LIBRARY."
  )
endif()

if(APPLE)
  target_sources(gpu PRIVATE
    src/backend/mt/init.c
    src/backend/mt/impl/impl.c
    src/backend/mt/impl/instance.c
    src/backend/mt/impl/surface.c
    src/backend/mt/impl/buffer.m
    src/backend/mt/impl/cmd/cmdbuf.m
    src/backend/mt/impl/cmd/cmdque.m
    src/backend/mt/impl/compute.m
    src/backend/mt/impl/depthstencil.m
    src/backend/mt/impl/device.m
    src/backend/mt/impl/frame.m
    src/backend/mt/impl/library.m
    src/backend/mt/impl/render/pipeline.m
    src/backend/mt/impl/render/rce.m
    src/backend/mt/impl/renderpass.m
    src/backend/mt/impl/swapchain.m
    src/backend/mt/impl/vertex.m
  )

  find_library(GPU_FRAMEWORK_FOUNDATION Foundation REQUIRED)
  find_library(GPU_FRAMEWORK_METAL Metal REQUIRED)
  find_library(GPU_FRAMEWORK_METALKIT MetalKit REQUIRED)
  find_library(GPU_FRAMEWORK_QUARTZCORE QuartzCore REQUIRED)
  if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    find_library(GPU_FRAMEWORK_UI UIKit REQUIRED)
  else()
    find_library(GPU_FRAMEWORK_UI AppKit REQUIRED)
  endif()

  target_link_libraries(gpu PRIVATE
    ${GPU_FRAMEWORK_FOUNDATION}
    ${GPU_FRAMEWORK_METAL}
    ${GPU_FRAMEWORK_METALKIT}
    ${GPU_FRAMEWORK_QUARTZCORE}
    ${GPU_FRAMEWORK_UI}
  )
elseif(WIN32)
  target_sources(gpu PRIVATE
    src/backend/dx12/init.c
    src/backend/dx12/impl/buffer.c
    src/backend/dx12/impl/cmdque.c
    src/backend/dx12/impl/compute.c
    src/backend/dx12/impl/descr/descpool.c
    src/backend/dx12/impl/device.c
    src/backend/dx12/impl/format.c
    src/backend/dx12/impl/frame.c
    src/backend/dx12/impl/instance.c
    src/backend/dx12/impl/library.c
    src/backend/dx12/impl/pipeline.c
    src/backend/dx12/impl/query.c
    src/backend/dx12/impl/renderpass.c
    src/backend/dx12/impl/rce.c
    src/backend/dx12/impl/sampler.c
    src/backend/dx12/impl/surface.c
    src/backend/dx12/impl/swapchain.c
    src/backend/dx12/impl/texture.c
  )
  target_link_libraries(gpu PRIVATE d3d12 d3dcompiler dxgi dxguid)
endif()

if(GPU_ENABLE_VULKAN)
  find_package(Vulkan REQUIRED)
  target_compile_definitions(gpu PRIVATE GPU_ENABLE_VULKAN=1)
  target_sources(gpu PRIVATE
    src/backend/vk/init.c
    src/backend/vk/impl/buffer.c
    src/backend/vk/impl/cmdque.c
    src/backend/vk/impl/compute.c
    src/backend/vk/impl/debug.c
    src/backend/vk/impl/descriptor.c
    src/backend/vk/impl/device.c
    src/backend/vk/impl/display.c
    src/backend/vk/impl/format.c
    src/backend/vk/impl/frame.c
    src/backend/vk/impl/instance.c
    src/backend/vk/impl/library.c
    src/backend/vk/impl/pipeline.c
    src/backend/vk/impl/query.c
    src/backend/vk/impl/rce.c
    src/backend/vk/impl/renderpass.c
    src/backend/vk/impl/sampler.c
    src/backend/vk/impl/surface.c
    src/backend/vk/impl/swapchain.c
    src/backend/vk/impl/texture.c
  )
  if(APPLE)
    target_sources(gpu PRIVATE src/backend/vk/impl/surface_apple.m)
  endif()
  target_link_libraries(gpu PRIVATE Vulkan::Vulkan)
endif()

if(GPU_BUILD_TESTS)
  enable_testing()

  if(WIN32)
    add_executable(gpu-dx12-queue-test tests/dx12/queue.c)
    target_link_libraries(gpu-dx12-queue-test PRIVATE gpu)
    set_target_properties(gpu-dx12-queue-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )

    if(TARGET us)
      add_custom_command(TARGET gpu-dx12-queue-test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:us>
                $<TARGET_FILE_DIR:gpu-dx12-queue-test>
      )
    endif()
    if(TARGET ds)
      add_custom_command(TARGET gpu-dx12-queue-test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:ds>
                $<TARGET_FILE_DIR:gpu-dx12-queue-test>
      )
    endif()

    add_test(NAME dx12-queue COMMAND gpu-dx12-queue-test)

    add_executable(gpu-dx12-barrier-test tests/dx12/barrier.c)
    target_link_libraries(gpu-dx12-barrier-test PRIVATE gpu dxguid)
    set_target_properties(gpu-dx12-barrier-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )

    foreach(runtime_target us ds)
      if(TARGET ${runtime_target})
        add_custom_command(TARGET gpu-dx12-barrier-test POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  $<TARGET_FILE:${runtime_target}>
                  $<TARGET_FILE_DIR:gpu-dx12-barrier-test>
        )
      endif()
    endforeach()

    add_test(NAME dx12-barrier COMMAND gpu-dx12-barrier-test)

    add_executable(gpu-dx12-depth-test tests/dx12/depth.c)
    target_link_libraries(gpu-dx12-depth-test PRIVATE gpu)
    set_target_properties(gpu-dx12-depth-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )

    foreach(runtime_target us ds)
      if(TARGET ${runtime_target})
        add_custom_command(TARGET gpu-dx12-depth-test POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  $<TARGET_FILE:${runtime_target}>
                  $<TARGET_FILE_DIR:gpu-dx12-depth-test>
        )
      endif()
    endforeach()

    add_test(NAME dx12-depth-pipeline
      COMMAND gpu-dx12-depth-test
              "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-dx12-usl/triangle.us"
    )

    add_executable(gpu-dx12-texture-test tests/dx12/texture.c)
    target_link_libraries(gpu-dx12-texture-test PRIVATE gpu)
    set_target_properties(gpu-dx12-texture-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )

    foreach(runtime_target us ds)
      if(TARGET ${runtime_target})
        add_custom_command(TARGET gpu-dx12-texture-test POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  $<TARGET_FILE:${runtime_target}>
                  $<TARGET_FILE_DIR:gpu-dx12-texture-test>
        )
      endif()
    endforeach()

    add_test(NAME dx12-texture
      COMMAND gpu-dx12-texture-test
              "${CMAKE_CURRENT_SOURCE_DIR}/samples/textured-quad-dx12-usl/textured_quad.us"
    )
  endif()
endif()

if(GPU_BUILD_SAMPLES AND WIN32)
  add_executable(gpu-triangle-dx12-usl
    samples/triangle-dx12-usl/main.c
  )
  target_link_libraries(gpu-triangle-dx12-usl PRIVATE gpu user32)
  set_target_properties(gpu-triangle-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  foreach(runtime_target us ds)
    if(TARGET ${runtime_target})
      add_custom_command(TARGET gpu-triangle-dx12-usl POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${runtime_target}>
                $<TARGET_FILE_DIR:gpu-triangle-dx12-usl>
      )
    endif()
  endforeach()

  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-dx12-usl/triangle.us")
    add_custom_command(TARGET gpu-triangle-dx12-usl POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-dx12-usl/triangle.us"
              $<TARGET_FILE_DIR:gpu-triangle-dx12-usl>
    )
  endif()

  add_executable(gpu-textured-quad-dx12-usl
    samples/textured-quad-dx12-usl/main.c
  )
  target_link_libraries(gpu-textured-quad-dx12-usl PRIVATE gpu user32)
  set_target_properties(gpu-textured-quad-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  foreach(runtime_target us ds)
    if(TARGET ${runtime_target})
      add_custom_command(TARGET gpu-textured-quad-dx12-usl POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${runtime_target}>
                $<TARGET_FILE_DIR:gpu-textured-quad-dx12-usl>
      )
    endif()
  endforeach()

  if(EXISTS
     "${CMAKE_CURRENT_SOURCE_DIR}/samples/textured-quad-dx12-usl/textured_quad.us")
    add_custom_command(TARGET gpu-textured-quad-dx12-usl POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CMAKE_CURRENT_SOURCE_DIR}/samples/textured-quad-dx12-usl/textured_quad.us"
              $<TARGET_FILE_DIR:gpu-textured-quad-dx12-usl>
    )
  endif()

  add_executable(gpu-compute-buffer-dx12-usl
    samples/compute-buffer-dx12-usl/main.c
  )
  target_link_libraries(gpu-compute-buffer-dx12-usl PRIVATE gpu)
  set_target_properties(gpu-compute-buffer-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  foreach(runtime_target us ds)
    if(TARGET ${runtime_target})
      add_custom_command(TARGET gpu-compute-buffer-dx12-usl POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${runtime_target}>
                $<TARGET_FILE_DIR:gpu-compute-buffer-dx12-usl>
      )
    endif()
  endforeach()

  if(EXISTS
     "${CMAKE_CURRENT_SOURCE_DIR}/samples/compute-buffer-dx12-usl/compute_buffer.us")
    add_custom_command(TARGET gpu-compute-buffer-dx12-usl POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CMAKE_CURRENT_SOURCE_DIR}/samples/compute-buffer-dx12-usl/compute_buffer.us"
      $<TARGET_FILE_DIR:gpu-compute-buffer-dx12-usl>
    )
  endif()

  add_executable(gpu-compute-render-dx12-usl
    samples/compute-buffer-dx12-usl/window.c
  )
  target_link_libraries(gpu-compute-render-dx12-usl PRIVATE gpu user32)
  set_target_properties(gpu-compute-render-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  foreach(runtime_target us ds)
    if(TARGET ${runtime_target})
      add_custom_command(TARGET gpu-compute-render-dx12-usl POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${runtime_target}>
                $<TARGET_FILE_DIR:gpu-compute-render-dx12-usl>
      )
    endif()
  endforeach()

  if(EXISTS
     "${CMAKE_CURRENT_SOURCE_DIR}/samples/compute-buffer-dx12-usl/compute_buffer.us")
    add_custom_command(TARGET gpu-compute-render-dx12-usl POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CMAKE_CURRENT_SOURCE_DIR}/samples/compute-buffer-dx12-usl/compute_buffer.us"
              $<TARGET_FILE_DIR:gpu-compute-render-dx12-usl>
    )
  endif()

  if(GPU_BUILD_TESTS)
    add_test(
      NAME dx12-compute-buffer
      COMMAND gpu-compute-buffer-dx12-usl
              "${CMAKE_CURRENT_SOURCE_DIR}/samples/compute-buffer-dx12-usl/compute_buffer.us"
    )
  endif()
endif()

install(TARGETS gpu
  EXPORT gpuTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/gpu DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT gpuTargets
  NAMESPACE gpu::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gpu
)
