cmake_minimum_required(VERSION 3.20)
project(efmt_sandbox CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# 为什么这里有这么多行：目标是"GitHub clone 下来就能编"。
#
# 代码里所有 include 都是本库的目录约定形状（efmt 88 处 / etl 23 处）：
#     #include <middleware/efmt/core/format.hpp>
#     #include <middleware/etl/string.h>
# 但仓库里【没有】middleware/ 这一层 —— 它原本只由 tests/run_check.ps1 用 junction
# 临时搭在 tests/include/ 下，而 tests/include/ 被 .gitignore 忽略。
# 于是新人 clone 后的第一条编译错误就是 "middleware/efmt/core/format.hpp: No such file"，
# 补上它之后紧接着又是 "middleware/etl/array.h: No such file"。
#
# 这里在【构建目录】里自建同样的一层，不污染源码树、不需要管理员权限：
#   优先 junction(Win) / symlink(其它) —— 活链接，改了 efmt/ 里的头文件立刻生效；
#   建不出来才退回复制（configure 时会明确提示"改了头文件要重跑 configure"）。
#
# ETL 是外部依赖（本仓库不带），位置依次查找：-DETL_ROOT > 环境变量 EFMT_ETL_INCLUDE
# > 常见相邻目录 > 明确报错。以前默认值写死了一台机器的绝对路径，configure 静默通过、
# 编译期才炸，换了机器只能靠猜。
# ============================================================================

get_filename_component(EFMT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# ---------------------------------------------------------------------------
# 1) 找 ETL。两种写法都收（很多人两种都写过，干脆都认）：
#      <...>/etl-master/include        ← 包含 etl/ 子目录的那一层
#      <...>/etl-master/include/etl    ← 直接指向 etl/ 本身
#    环境变量 EFMT_ETL_INCLUDE 与 tests/run_check.ps1 的 -EtlInclude 同义（指 etl/ 本身），
#    传父目录也不会错 —— 两种都过规范化这一步。
# ---------------------------------------------------------------------------
set(ETL_ROOT "" CACHE PATH "ETL 位置：包含 etl/ 子目录的那一层，或直接指向 etl/ 本身")

# 把候选路径规范化成"etl/ 目录本身"；看着不像 ETL 就返回空串
function(efmt_normalize_etl candidate out)
    foreach(_d "${candidate}" "${candidate}/etl")
        if(EXISTS "${_d}/string.h" AND EXISTS "${_d}/platform.h")
            get_filename_component(_d "${_d}" ABSOLUTE)
            set(${out} "${_d}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out} "" PARENT_SCOPE)
endfunction()

set(_etl_dir "")
if(ETL_ROOT)
    efmt_normalize_etl("${ETL_ROOT}" _etl_dir)
elseif(DEFINED ENV{EFMT_ETL_INCLUDE})
    efmt_normalize_etl("$ENV{EFMT_ETL_INCLUDE}" _etl_dir)
else()
    foreach(_cand ../etl-master/include ../etl/include third_party/etl/include)
        efmt_normalize_etl("${EFMT_ROOT}/${_cand}" _etl_dir)
        if(_etl_dir)
            break()
        endif()
    endforeach()
endif()

if(NOT _etl_dir)
    message(FATAL_ERROR
        "找不到 ETL（沙盒里的 elog 需要它）。查找过：ETL_ROOT=\"${ETL_ROOT}\"、"
        "环境变量 EFMT_ETL_INCLUDE、\n相邻目录 ../etl-master/include | ../etl/include | third_party/etl/include\n"
        "  1) 取一份 ETL：https://github.com/ETLCPP/etl\n"
        "  2) 重新 configure，把 ETL_ROOT 指到下面任意一种：\n"
        "       cmake -S sandbox -B build -G Ninja -DETL_ROOT=<...>/etl-master/include\n"
        "       cmake -S sandbox -B build -G Ninja -DETL_ROOT=<...>/etl-master/include/etl\n"
        "     也可以设环境变量 EFMT_ETL_INCLUDE=<...>/etl-master/include/etl\n"
        "     （CLion：Settings -> CMake -> CMake options 填 -DETL_ROOT=...）\n"
        "  注意：不要把 etl/ 本身当 include 根写进代码路径 —— ETL 自带 string.h 等系统同名头，\n"
        "        直接 #include <string.h> 会抓到 ETL 的；本库统一走 <middleware/etl/...>。")
endif()

# ---------------------------------------------------------------------------
# 2) 在构建目录里搭出 middleware/efmt 与 middleware/etl
# ---------------------------------------------------------------------------
function(efmt_make_middleware name target)
    set(_link "${CMAKE_CURRENT_BINARY_DIR}/middleware/${name}")
    if(EXISTS "${_link}")
        # 已存在就沿用（不删除：删链接有跟进去删目标目录的风险）。
        # 换了 ETL / 换了仓库位置就删掉 build 目录重新 configure。
        message(STATUS "middleware/${name}：已存在，沿用 ${_link}")
        return()
    endif()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/middleware")

    if(WIN32)
        # junction 不需要管理员权限，也不受"开发者模式"限制。
        # 注意 mklink 是 cmd 内建命令，不认正斜杠（"/01_Workspace" 会被当成开关），
        # 必须先把两条路径转成本机反斜杠形式。
        file(TO_NATIVE_PATH "${_link}"  _native_link)
        file(TO_NATIVE_PATH "${target}" _native_target)
        execute_process(COMMAND cmd /c mklink /J "${_native_link}" "${_native_target}"
                        OUTPUT_QUIET ERROR_QUIET)
        if(EXISTS "${_link}")
            message(STATUS "middleware/${name} -> ${target}（junction，改头文件立刻生效）")
            return()
        endif()
        file(COPY "${target}/" DESTINATION "${_link}")
        message(STATUS "middleware/${name} -> ${target}（复制，改了头文件要重跑 configure）")
    else()
        # 软链建不出来（不给权限）时 COPY_ON_ERROR 自动退回复制
        file(CREATE_LINK "${target}" "${_link}" SYMBOLIC COPY_ON_ERROR RESULT _rc)
        if(IS_SYMLINK "${_link}")
            message(STATUS "middleware/${name} -> ${target}（symlink，改头文件立刻生效）")
        else()
            message(STATUS "middleware/${name} -> ${target}（复制，改了头文件要重跑 configure）")
        endif()
    endif()
endfunction()

efmt_make_middleware(efmt "${EFMT_ROOT}/efmt")
efmt_make_middleware(etl  "${_etl_dir}")

# ---------------------------------------------------------------------------
# 3) 目标：include 根只有两个 —— 自建的 middleware 层 + 仓库根（elog/ecli/eserde/matchit）
# ---------------------------------------------------------------------------
add_executable(sandbox main.cpp)
target_include_directories(sandbox PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(MSVC)
    target_compile_options(sandbox PRIVATE /W4 /utf-8)
else()
    target_compile_options(sandbox PRIVATE -Wall -Wextra)
endif()

# 想试输出风格：CLion → Settings → CMake → CMake options 加 -DEFMT_DERIVE_SHOW_TYPE=1
set(EFMT_DERIVE_SHOW_TYPE 0 CACHE STRING "推导输出是否带类型名（1 = point { x = 3, y = 4 }）")
target_compile_definitions(sandbox PRIVATE EFMT_DERIVE_SHOW_TYPE=${EFMT_DERIVE_SHOW_TYPE})
