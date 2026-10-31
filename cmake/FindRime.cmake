# 只接受 prepare_rime_core.py 以固定源码构建的核心；不查找宿主库或官方插件整包。
# 普通客户端配置不联网、不编译依赖；缺失或不匹配时继续使用系统输入法。
set(Rime_VERSION "1.17.0")
set(Rime_FOUND FALSE)
set(Rime_LIBRARIES "")
set(Rime_INCLUDE_DIRS "")
set(Rime_COPY_FILES "")
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  return()
endif()

set(QM_RIME_ROOT "${PROJECT_SOURCE_DIR}/tmp/ime/rime-core/install" CACHE PATH "Pinned core-only librime runtime prepared by qmclient_scripts/ime/prepare_rime_core.py")
set(_qm_rime_recipe "${PROJECT_SOURCE_DIR}/qmclient_scripts/ime/prepare_rime_core.py")
set(_qm_rime_lock "${PROJECT_SOURCE_DIR}/qmclient_scripts/ime/rime_core_sources.json")
set(_qm_rime_receipt "${QM_RIME_ROOT}/rime-core-build.txt")
if(NOT EXISTS "${_qm_rime_receipt}")
  message(STATUS "Built-in pinyin: pinned core runtime is not prepared; system IME fallback remains enabled")
  return()
endif()

file(STRINGS "${_qm_rime_receipt}" _qm_rime_fields)
file(SHA256 "${_qm_rime_lock}" _qm_rime_lock_sha)
file(SHA256 "${_qm_rime_recipe}" _qm_rime_recipe_sha)
foreach(_qm_rime_required IN ITEMS
    "format=qmc-rime-core-v1" "version=1.17.0" "platform=windows-msvc-x64"
    "plugins=none" "crt=MT" "source_lock_sha256=${_qm_rime_lock_sha}"
    "recipe_sha256=${_qm_rime_recipe_sha}")
  if(NOT "${_qm_rime_required}" IN_LIST _qm_rime_fields)
    message(WARNING "Built-in pinyin: core runtime receipt does not match the pinned recipe; prepare it again. System IME fallback remains enabled")
    return()
  endif()
endforeach()

set(_qm_rime_files "rime.dll;include/rime_api.h;RIME-LICENSES.txt;rime-core-sources.json")
set(_qm_rime_keys "dll_sha256;header_sha256;notices_sha256;source_lock_sha256")
foreach(_qm_rime_index RANGE 0 3)
  list(GET _qm_rime_files ${_qm_rime_index} _qm_rime_file)
  list(GET _qm_rime_keys ${_qm_rime_index} _qm_rime_key)
  if(NOT EXISTS "${QM_RIME_ROOT}/${_qm_rime_file}")
    message(WARNING "Built-in pinyin: incomplete core runtime (${_qm_rime_file}); system IME fallback remains enabled")
    return()
  endif()
  file(SHA256 "${QM_RIME_ROOT}/${_qm_rime_file}" _qm_rime_sha)
  if(NOT "${_qm_rime_key}=${_qm_rime_sha}" IN_LIST _qm_rime_fields)
    message(WARNING "Built-in pinyin: core runtime checksum mismatch (${_qm_rime_file}); system IME fallback remains enabled")
    return()
  endif()
endforeach()

# 引擎通过 LoadLibraryExW 加载 DLL；接口目标只传递头目录和能力宏，不链接导入库。
if(NOT TARGET Rime::Rime)
  add_library(Rime::Rime INTERFACE IMPORTED)
  set_target_properties(Rime::Rime PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${QM_RIME_ROOT}/include"
    INTERFACE_COMPILE_DEFINITIONS CONF_QM_RIME)
endif()
set(Rime_FOUND TRUE)
set(Rime_LIBRARIES Rime::Rime)
set(Rime_INCLUDE_DIRS "${QM_RIME_ROOT}/include")
# 现有 COPY_FILES 同时用于运行目录和发布打包，许可及来源回执随 DLL 一起复制。
set(Rime_COPY_FILES
  "${QM_RIME_ROOT}/rime.dll"
  "${QM_RIME_ROOT}/RIME-LICENSES.txt"
  "${QM_RIME_ROOT}/rime-core-sources.json"
  "${_qm_rime_receipt}")
message(STATUS "Built-in pinyin: pinned core-only librime ${Rime_VERSION} (${QM_RIME_ROOT})")
