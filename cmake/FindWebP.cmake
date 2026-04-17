if(NOT PREFER_BUNDLED_LIBS)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(PC_WebP libwebp)
    if(PC_WebP_FOUND)
      set(WebP_FOUND ON)
      set(WebP_BUNDLED OFF)
      set(WebP_DEP)
      set(WebP_INCLUDE_DIRS ${PC_WebP_INCLUDE_DIRS})
      set(WebP_LIBRARY_DIRS ${PC_WebP_LIBRARY_DIRS})
    endif()
  endif()
endif()

if(NOT WebP_FOUND)
  set_extra_dirs_lib(WebP webp)
endif()

find_library(WebP_LIBRARY
  NAMES webp libwebp
  HINTS ${HINTS_WebP_LIBDIR} ${PC_WebP_LIBDIR} ${PC_WebP_LIBRARY_DIRS}
  PATHS ${PATHS_WebP_LIBDIR}
  ${CROSSCOMPILING_NO_CMAKE_SYSTEM_PATH}
)

find_library(WebPDEMUX_LIBRARY
  NAMES webpdemux libwebpdemux
  HINTS ${HINTS_WebP_LIBDIR} ${PC_WebP_LIBDIR} ${PC_WebP_LIBRARY_DIRS}
  PATHS ${PATHS_WebP_LIBDIR}
  ${CROSSCOMPILING_NO_CMAKE_SYSTEM_PATH}
)

find_library(WebPMUX_LIBRARY
  NAMES webpmux libwebpmux
  HINTS ${HINTS_WebP_LIBDIR} ${PC_WebP_LIBDIR} ${PC_WebP_LIBRARY_DIRS}
  PATHS ${PATHS_WebP_LIBDIR}
  ${CROSSCOMPILING_NO_CMAKE_SYSTEM_PATH}
)

set_extra_dirs_include(WebP webp "${WebP_LIBRARY}")
find_path(WebP_INCLUDEDIR
  NAMES webp/decode.h
  HINTS ${HINTS_WebP_INCLUDEDIR} ${PC_WebP_INCLUDEDIR} ${PC_WebP_INCLUDE_DIRS}
  PATHS ${PATHS_WebP_INCLUDEDIR}
  ${CROSSCOMPILING_NO_CMAKE_SYSTEM_PATH}
)

mark_as_advanced(WebP_LIBRARY WebPDEMUX_LIBRARY WebPMUX_LIBRARY WebP_INCLUDEDIR)

if(WebP_LIBRARY AND WebP_INCLUDEDIR)
  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(WebP DEFAULT_MSG WebP_LIBRARY WebP_INCLUDEDIR)

  set(WebP_LIBRARIES ${WebP_LIBRARY})
  if(WebPDEMUX_LIBRARY)
    list(APPEND WebP_LIBRARIES ${WebPDEMUX_LIBRARY})
  endif()
  if(WebPMUX_LIBRARY)
    list(APPEND WebP_LIBRARIES ${WebPMUX_LIBRARY})
  endif()
  if(NOT WebP_INCLUDE_DIRS)
    set(WebP_INCLUDE_DIRS ${WebP_INCLUDEDIR})
  endif()
endif()

set(WebP_COPY_FILES)
