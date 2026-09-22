cmake_minimum_required(VERSION 3.20)
project(efmt_sandbox CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 库的 include 根（和 tests/run_check.ps1 用的两条 -I 完全一致）：
#   <repo>/tests/include → <middleware/efmt/...>、<middleware/etl/...>
#   <repo>               → <elog/elog.hpp>
set(EFMT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ETL 依赖（sandbox 显式）：ETL_ROOT 必须指向 ETL 的**父目录**（包含 etl/ 子目录的
# 那一层），用户代码用官方风格 #include <etl/string.h>。不要把 etl/ 本身加进
# include 路径——ETL 自带 string.h/limits.h/utility.h 等同名头，会遮蔽 C/C++
# 系统头（MinGW 下 <cstring> 会抓到 ETL 的 string.h，整条 include 链崩掉）。
# ETL 位置变了就 -DETL_ROOT=新路径 覆盖（CLion：Settings → CMake → CMake options）。
set(ETL_ROOT
        "E:/02_KnowledgeBase/00_Personal/01_learing_project/etl-master/include"
        CACHE FILEPATH
        "ETL include parent（包含 etl/ 子目录）")

add_executable(sandbox main.cpp)
target_include_directories(sandbox PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${ETL_ROOT})

if(MSVC)
    target_compile_options(sandbox PRIVATE /W4 /utf-8)
else()
    target_compile_options(sandbox PRIVATE -Wall -Wextra)
endif()

# 想试输出风格：CLion → Settings → CMake → CMake options 加 -DEFMT_DERIVE_SHOW_TYPE=1
set(EFMT_DERIVE_SHOW_TYPE 0 CACHE STRING "推导输出是否带类型名（1 = point { x = 3, y = 4 }）")
target_compile_definitions(sandbox PRIVATE EFMT_DERIVE_SHOW_TYPE=${EFMT_DERIVE_SHOW_TYPE})
