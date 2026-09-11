# 名牌 MSDF 字形图集生成工具（离线资源制作）。
# 刻意独立成小工程：正常客户端构建不引入 msdfgen，仅在需要重烤图集时手动构建一次。
cmake_minimum_required(VERSION 3.16)
project(qm_nameplate_msdf_atlas CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED QM_PROJECT_ROOT)
  message(FATAL_ERROR "QM_PROJECT_ROOT must point at the QmClient repository root")
endif()

set(MSDF_ROOT "${QM_PROJECT_ROOT}/vendor/msdfgen")
set(FREETYPE_ROOT "${QM_PROJECT_ROOT}/ddnet-libs/freetype")

if(NOT EXISTS "${MSDF_ROOT}/msdfgen.h")
  message(FATAL_ERROR "vendor/msdfgen is missing; run: git submodule update --init vendor/msdfgen")
endif()

file(GLOB MSDF_CORE_SOURCES "${MSDF_ROOT}/core/*.cpp")
add_library(msdfgen-core STATIC ${MSDF_CORE_SOURCES})
target_include_directories(msdfgen-core PUBLIC "${MSDF_ROOT}")
target_compile_definitions(msdfgen-core PUBLIC MSDFGEN_USE_CPP11 MSDFGEN_PUBLIC=)
target_compile_features(msdfgen-core PUBLIC cxx_std_11)

add_executable(qm-nameplate-msdf-atlas main.cpp)

if(WIN32)
  target_include_directories(qm-nameplate-msdf-atlas PRIVATE "${FREETYPE_ROOT}/include")
  target_link_libraries(qm-nameplate-msdf-atlas PRIVATE msdfgen-core "${FREETYPE_ROOT}/windows/lib64/freetype.lib")
else()
  find_package(Freetype REQUIRED)
  target_link_libraries(qm-nameplate-msdf-atlas PRIVATE msdfgen-core Freetype::Freetype)
endif()
