function(gpu_sample_stdlib_dependencies outVar stdlibRoot)
  file(GLOB_RECURSE dependencies CONFIGURE_DEPENDS
    "${stdlibRoot}/*.usl"
    "${stdlibRoot}/cache/v2/*.ustd"
  )
  if(EXISTS "${stdlibRoot}/STDLIB_VERSION")
    list(APPEND dependencies "${stdlibRoot}/STDLIB_VERSION")
  endif()
  set(${outVar} "${dependencies}" PARENT_SCOPE)
endfunction()
