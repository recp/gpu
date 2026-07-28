if(NOT EXISTS "${GPU_USL_PACKER}")
  message(FATAL_ERROR "USL packer not found: ${GPU_USL_PACKER}")
endif()
if(NOT EXISTS "${GPU_USL_SOURCE}")
  message(FATAL_ERROR "USL source not found: ${GPU_USL_SOURCE}")
endif()

if(GPU_USL_SOURCE MATCHES "\\.usl$")
  string(REGEX REPLACE "\\.usl$" ".us" GPU_USL_ARTIFACT "${GPU_USL_SOURCE}")
else()
  set(GPU_USL_ARTIFACT "${GPU_USL_SOURCE}.us")
endif()
if(NOT EXISTS "${GPU_USL_ARTIFACT}")
  message(FATAL_ERROR "USL artifact not found: ${GPU_USL_ARTIFACT}")
endif()

if(NOT GPU_USL_TARGET)
  set(GPU_USL_TARGET wgsl)
endif()
if(NOT GPU_USL_TARGET_PROFILE)
  set(GPU_USL_TARGET_PROFILE none)
endif()

set(GPU_USL_PACK_ARGS
    --target "${GPU_USL_TARGET}" "${GPU_USL_TARGET_PROFILE}")
if(GPU_USL_CAPS)
  string(REPLACE "," ";" GPU_USL_CAP_LIST "${GPU_USL_CAPS}")
  foreach(GPU_USL_CAP IN LISTS GPU_USL_CAP_LIST)
    list(APPEND GPU_USL_PACK_ARGS --cap "${GPU_USL_CAP}")
  endforeach()
endif()
list(APPEND GPU_USL_PACK_ARGS "${GPU_USL_ARTIFACT}")

execute_process(
  COMMAND "${GPU_USL_PACKER}" ${GPU_USL_PACK_ARGS}
  RESULT_VARIABLE GPU_USL_PACK_RESULT
)
if(NOT GPU_USL_PACK_RESULT EQUAL 0)
  message(FATAL_ERROR
    "Failed to pack ${GPU_USL_TARGET} payload: ${GPU_USL_ARTIFACT}")
endif()
