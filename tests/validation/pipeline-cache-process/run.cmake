if(NOT DEFINED PIPELINE_EXE OR
   NOT DEFINED PAIR_EXE OR
   NOT DEFINED ARTIFACT OR
   NOT DEFINED BACKEND OR
   NOT DEFINED CACHE_PATH)
  message(FATAL_ERROR "pipeline-cache process test arguments are incomplete")
endif()

set(common_args
  "${ARTIFACT}"
  "${BACKEND}"
  "4"
  "1"
)

execute_process(
  COMMAND "${PIPELINE_EXE}"
          ${common_args}
          disk-produce
          "${CACHE_PATH}"
  RESULT_VARIABLE produce_result
  OUTPUT_VARIABLE produce_output
  ERROR_VARIABLE produce_error
)

if("${produce_result}" STREQUAL "0")
  execute_process(
    COMMAND "${PAIR_EXE}"
            --pair
            "${PIPELINE_EXE}"
            ${common_args}
            disk-reopen
            "${CACHE_PATH}"
    RESULT_VARIABLE concurrent_result
    OUTPUT_VARIABLE concurrent_output
    ERROR_VARIABLE concurrent_error
  )
else()
  set(concurrent_result 1)
endif()

if("${produce_result}" STREQUAL "0" AND
   "${concurrent_result}" STREQUAL "0")
  execute_process(
    COMMAND "${PIPELINE_EXE}"
            ${common_args}
            disk-reopen
            "${CACHE_PATH}"
    RESULT_VARIABLE reopen_result
    OUTPUT_VARIABLE reopen_output
    ERROR_VARIABLE reopen_error
  )
else()
  set(reopen_result 1)
endif()

file(REMOVE
  "${CACHE_PATH}"
  "${CACHE_PATH}.lock"
  "${CACHE_PATH}.meta"
)
file(REMOVE_RECURSE "${CACHE_PATH}.vkpb")

if(NOT "${produce_result}" STREQUAL "0")
  message(FATAL_ERROR
    "pipeline-cache produce failed (${produce_result})\n"
    "${produce_output}${produce_error}")
endif()
if(NOT "${concurrent_result}" STREQUAL "0")
  message(FATAL_ERROR
    "concurrent pipeline-cache producers failed (${concurrent_result})\n"
    "${concurrent_output}${concurrent_error}")
endif()
if(NOT "${reopen_result}" STREQUAL "0")
  message(FATAL_ERROR
    "pipeline-cache reopen failed (${reopen_result})\n"
    "${reopen_output}${reopen_error}")
endif()
