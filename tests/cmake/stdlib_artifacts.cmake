foreach(required GPU_SOURCE_DIR GPU_TEST_ROOT GPU_TEST_FIXTURE)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

set(ENV{USL_STDLIB_VERSION} "1.0.0")

function(run)
  execute_process(COMMAND ${ARGN}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${ARGN}\n${output}\n${error}")
  endif()
endfunction()

function(check_build label changed)
  run("${CMAKE_COMMAND}" --build "${build}" --config Release)
  file(SHA256 "${artifact}" hash)
  file(TIMESTAMP "${artifact}" timestamp "%s" UTC)
  file(SHA256 "${plain}" plainHash)
  file(TIMESTAMP "${plain}" plainTimestamp "%s" UTC)
  if(NOT plainHash STREQUAL plainBefore OR
     NOT plainTimestamp STREQUAL plainTimeBefore)
    message(FATAL_ERROR "${label}: unrelated shader rebuilt")
  endif()
  if(changed)
    if(timestamp STREQUAL artifactTime)
      message(FATAL_ERROR "${label}: stdlib artifact did not rebuild")
    endif()
  elseif(NOT timestamp STREQUAL artifactTime OR
         NOT hash STREQUAL artifactHash)
    message(FATAL_ERROR "${label}: unchanged shader rebuilt")
  endif()
  set(artifactHash "${hash}" PARENT_SCOPE)
  set(artifactTime "${timestamp}" PARENT_SCOPE)
  message(STATUS "${label}: passed")
endfunction()

set(inputs "${GPU_TEST_ROOT}/inputs")
set(build "${GPU_TEST_ROOT}/build")
set(cache "${inputs}/stdlib/cache/v2/1.0.0/std/probe.ustd")
set(artifact "${build}/windows/artifacts/stdlib/probe.us")
set(plain "${build}/windows/artifacts/plain/plain.us")
file(REMOVE_RECURSE "${inputs}" "${build}")
file(MAKE_DIRECTORY "${inputs}/stdlib/std" "${inputs}/package"
  "${inputs}/stdlib/cache/v2/1.0.0/std")
file(WRITE "${inputs}/stdlib/STDLIB_VERSION" "1.0.0\n")
file(WRITE "${inputs}/probe.usl"
  "import <std/probe>\nkern(1, 1, 1) probe(inout out: [float] #buffer(0)) { out[0] = probe_value() }\n")
file(WRITE "${inputs}/plain.usl"
  "kern(1, 1, 1) plain(inout out: [float] #buffer(0)) { out[0] = 7.0 }\n")

foreach(value 1 2)
  file(WRITE "${inputs}/package/probe.usl"
    "pub fn probe_value() -> float { return ${value}.0 }\n")
  run("${CMAKE_COMMAND}" -E env USL_EMIT_BYTECODE=1 USL_ARTIFACT_ONLY=1
    USL_BYTECODE_EMBED_SOURCE=1 USL_STRICT_IEEE=1 USL_NO_BACKEND_SIDECAR=1
    "${GPU_TEST_FIXTURE}" dx12 "${inputs}/package/probe.usl")
  configure_file("${inputs}/package/probe.us"
    "${inputs}/package/probe-${value}.ustd" COPYONLY)
endforeach()
configure_file("${inputs}/package/probe-1.ustd" "${cache}" COPYONLY)

run("${CMAKE_COMMAND}" -S "${GPU_SOURCE_DIR}/tests/cmake/stdlib-artifacts"
  -B "${build}" "-DGPU_SOURCE_DIR=${GPU_SOURCE_DIR}"
  "-DGPU_TEST_INPUTS=${inputs}" "-DGPU_TEST_FIXTURE=${GPU_TEST_FIXTURE}")
run("${CMAKE_COMMAND}" --build "${build}" --config Release)
file(SHA256 "${artifact}" artifactHash)
file(TIMESTAMP "${artifact}" artifactTime "%s" UTC)
file(SHA256 "${plain}" plainBefore)
file(TIMESTAMP "${plain}" plainTimeBefore "%s" UTC)
check_build("no changes" FALSE)

run("${CMAKE_COMMAND}" -E sleep 1)
set(oldHash "${artifactHash}")
configure_file("${inputs}/package/probe-2.ustd" "${cache}" COPYONLY)
check_build("cache replacement" TRUE)
if(artifactHash STREQUAL oldHash)
  message(FATAL_ERROR "cache replacement: shader still uses old module")
endif()

run("${CMAKE_COMMAND}" -E sleep 1)
set(oldHash "${artifactHash}")
file(REMOVE "${cache}")
file(WRITE "${inputs}/stdlib/std/probe.usl"
  "pub fn probe_value() -> float { return 1.0 }\n")
check_build("cache removal and source fallback" TRUE)
if(artifactHash STREQUAL oldHash)
  message(FATAL_ERROR "cache removal: shader still uses cached module")
endif()

run("${CMAKE_COMMAND}" -E sleep 1)
set(oldHash "${artifactHash}")
file(WRITE "${inputs}/stdlib/std/probe.usl"
  "pub fn probe_value() -> float { return 3.0 }\n")
check_build("source edit" TRUE)
if(artifactHash STREQUAL oldHash)
  message(FATAL_ERROR "source edit: shader still uses old module")
endif()

run("${CMAKE_COMMAND}" -E sleep 1)
configure_file("${inputs}/package/probe-2.ustd" "${cache}" COPYONLY)
check_build("cache addition" TRUE)

run("${CMAKE_COMMAND}" -E sleep 1)
run("${CMAKE_COMMAND}" -E touch "${inputs}/stdlib/STDLIB_VERSION")
check_build("version dependency" TRUE)
check_build("final no changes" FALSE)
