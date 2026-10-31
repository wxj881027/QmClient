cmake_minimum_required(VERSION 3.22)
project(QmImeEngineProbe LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "The IME engine probe requires Windows MSVC and the pinned Rime core")
endif()

get_filename_component(QM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)

set(IME_DIR "${QM_ROOT}/src/game/client/components/qmclient/ime")
add_executable(qm-ime-engine-probe
  engine_probe.cpp
  "${IME_DIR}/qm_ime_engine.cpp"
  "${IME_DIR}/qm_ime_lexicon.cpp"
  "${IME_DIR}/qm_ime_merge.cpp"
  "${IME_DIR}/qm_ime_runtime.cpp"
  "${QM_ROOT}/src/engine/shared/qm_ime_provider.cpp")
foreach(BASE_FILE color dbg fs log mem secure sphore str system thread time windows)
  target_sources(qm-ime-engine-probe PRIVATE "${QM_ROOT}/src/base/${BASE_FILE}.cpp")
endforeach()
foreach(UNICODE_FILE confusables confusables_data tolower tolower_data)
  target_sources(qm-ime-engine-probe PRIVATE "${QM_ROOT}/src/base/unicode/${UNICODE_FILE}.cpp")
endforeach()
target_include_directories(qm-ime-engine-probe PRIVATE
  "${QM_ROOT}/src" "${QM_ROOT}/cmake-build-release/src"
  "${QM_ROOT}/tmp/ime/rime-core/install/include")
target_compile_definitions(qm-ime-engine-probe PRIVATE CONF_QM_RIME NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(qm-ime-engine-probe PRIVATE /utf-8 /W3)
target_link_libraries(qm-ime-engine-probe PRIVATE ws2_32 shell32 shlwapi version user32 advapi32 bcrypt ole32 iphlpapi)
