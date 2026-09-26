if(NOT METAL_COMPILER OR NOT METALLIB_COMPILER)
  message(FATAL_ERROR "Metal shader tools were not discovered before including BuildMetalShaders.cmake")
endif()

set(METAL_SHADER_SOURCE "${PROJECT_SOURCE_DIR}/data/shader/metal/qmclient.metal")
set(METAL_SHADER_OUTPUT_DIR "${PROJECT_BINARY_DIR}/data/shader/metal")
set(METAL_SHADER_AIR "${METAL_SHADER_OUTPUT_DIR}/qmclient.air")
set(METAL_SHADER_LIBRARY "${METAL_SHADER_OUTPUT_DIR}/qmclient.metallib")

set(METAL_DEPLOYMENT_FLAGS)
if(TARGET_OS STREQUAL "ios")
  if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    message(FATAL_ERROR "iOS Metal shaders require CMAKE_OSX_DEPLOYMENT_TARGET")
  endif()
  list(APPEND METAL_DEPLOYMENT_FLAGS "-mios-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
elseif(TARGET_OS STREQUAL "mac")
  if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    message(FATAL_ERROR "macOS Metal shaders require CMAKE_OSX_DEPLOYMENT_TARGET")
  endif()
  list(APPEND METAL_DEPLOYMENT_FLAGS "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()

add_custom_command(
  OUTPUT "${METAL_SHADER_AIR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${METAL_SHADER_OUTPUT_DIR}"
  COMMAND "${XCRUN_PROGRAM}" --sdk "${METAL_SDK}" metal ${METAL_DEPLOYMENT_FLAGS} -c "${METAL_SHADER_SOURCE}" -I "${PROJECT_SOURCE_DIR}/src/engine/client/backend/metal" -o "${METAL_SHADER_AIR}"
  DEPENDS
    "${METAL_SHADER_SOURCE}"
    "${PROJECT_SOURCE_DIR}/src/engine/client/backend/metal/metal_types.h"
  COMMENT "Compiling Metal shader qmclient.metal"
  VERBATIM
)

add_custom_command(
  OUTPUT "${METAL_SHADER_LIBRARY}"
  COMMAND "${XCRUN_PROGRAM}" --sdk "${METAL_SDK}" metallib "${METAL_SHADER_AIR}" -o "${METAL_SHADER_LIBRARY}"
  DEPENDS "${METAL_SHADER_AIR}"
  COMMENT "Linking Metal library qmclient.metallib"
  VERBATIM
)

set(METAL_SHADER_FILE_LIST "data/shader/metal/qmclient.metallib" CACHE STRING "Metal shader file list" FORCE)
add_custom_target(build_metal_shaders DEPENDS "${METAL_SHADER_LIBRARY}")
