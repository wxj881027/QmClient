# SDL3 直接用 ddnet-libs 里的预编译库，构建期不再编译 SDL 源码。
# 版本策略：不绑定某一个补丁号，但要求不低于最低可用版本（见 QM_SDL3_MIN_VERSION）。
set(QM_SDL3_MIN_VERSION "3.4.18")

if(NOT CMAKE_CROSSCOMPILING)
  find_package(PkgConfig QUIET)
  pkg_check_modules(PC_SDL3 sdl3)
endif()

set_extra_dirs_lib(SDL3 sdl)
find_library(SDL3_LIBRARY
  NAMES SDL3
  HINTS ${HINTS_SDL3_LIBDIR} ${PC_SDL3_LIBDIR} ${PC_SDL3_LIBRARY_DIRS}
  PATHS ${PATHS_SDL3_LIBDIR}
  ${CROSSCOMPILING_NO_CMAKE_SYSTEM_PATH}
)
if(PREFER_BUNDLED_LIBS)
  set(CMAKE_FIND_FRAMEWORK FIRST)
else()
  set(CMAKE_FIND_FRAMEWORK LAST)
endif()
set_extra_dirs_include(SDL3 sdl "${SDL3_LIBRARY}")
find_path(SDL3_INCLUDEDIR SDL3/SDL.h
  HINTS ${HINTS_SDL3_INCLUDEDIR} ${PC_SDL3_INCLUDE_DIRS}
  PATHS ${PATHS_SDL3_INCLUDEDIR}
  ${CROSSCOMPILING_NO_CMAKE_SYSTEM_PATH}
)

# 版本从随库分发的头文件读取，不再把版本号写死一份在 CMake 里。
set(SDL3_VERSION "")
if(SDL3_INCLUDEDIR AND EXISTS "${SDL3_INCLUDEDIR}/SDL3/SDL_version.h")
  file(READ "${SDL3_INCLUDEDIR}/SDL3/SDL_version.h" QM_SDL3_VERSION_HEADER)
  foreach(QM_SDL3_PART MAJOR MINOR MICRO)
    if(QM_SDL3_VERSION_HEADER MATCHES "#define SDL_${QM_SDL3_PART}_VERSION[ \t]+([0-9]+)")
      set(QM_SDL3_${QM_SDL3_PART}_VERSION "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  if(DEFINED QM_SDL3_MAJOR_VERSION AND DEFINED QM_SDL3_MINOR_VERSION AND DEFINED QM_SDL3_MICRO_VERSION)
    set(SDL3_VERSION "${QM_SDL3_MAJOR_VERSION}.${QM_SDL3_MINOR_VERSION}.${QM_SDL3_MICRO_VERSION}")
  endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SDL3
  REQUIRED_VARS SDL3_LIBRARY SDL3_INCLUDEDIR
  VERSION_VAR SDL3_VERSION
)

mark_as_advanced(SDL3_LIBRARY SDL3_INCLUDEDIR)

if(SDL3_FOUND)
  if(NOT SDL3_VERSION)
    message(FATAL_ERROR "无法从 ${SDL3_INCLUDEDIR}/SDL3/SDL_version.h 读取 SDL3 版本")
  endif()
  if(SDL3_VERSION VERSION_LESS QM_SDL3_MIN_VERSION)
    message(FATAL_ERROR
      "SDL3 版本过低：${SDL3_VERSION}，要求 >= ${QM_SDL3_MIN_VERSION}（ddnet-libs/sdl）")
  endif()

  set(SDL3_LIBRARIES ${SDL3_LIBRARY})
  set(SDL3_INCLUDE_DIRS ${SDL3_INCLUDEDIR})

  is_bundled(SDL3_BUNDLED "${SDL3_LIBRARY}")
  set(SDL3_COPY_FILES)
  set(SDL3_COPY_DIRS)
  if(SDL3_BUNDLED)
    if(TARGET_OS STREQUAL "windows")
      set(SDL3_COPY_FILES "${EXTRA_SDL3_LIBDIR}/SDL3.dll")
      if(TARGET_BITS EQUAL 32)
        list(APPEND SDL3_COPY_FILES
          "${EXTRA_SDL3_LIBDIR}/libgcc_s_dw2-1.dll"
        )
      endif()
    elseif(TARGET_OS STREQUAL "mac")
      set(SDL3_COPY_DIRS "${EXTRA_SDL3_LIBDIR}/SDL3.framework")
    endif()
  endif()
  if(ANDROID)
    file(WRITE "${CMAKE_BINARY_DIR}/qm_sdl3_java_dir.txt"
      "${PROJECT_SOURCE_DIR}/ddnet-libs/sdl/java\n")
  endif()
elseif(TARGET_OS AND EXISTS "${PROJECT_SOURCE_DIR}/ddnet-libs/sdl")
  # 预编译包存在但缺当前平台切片时，给出可执行的提示而不是笼统的“未找到”。
  message(FATAL_ERROR
    "ddnet-libs/sdl 缺少 ${TARGET_OS}/${LIB_DIR} 的 SDL3 预编译库；"
    "请在有对应工具链的机器上运行 qmclient_scripts/build_sdl3_prebuilt.sh <SDL3 源码目录> ${TARGET_OS}")
endif()
