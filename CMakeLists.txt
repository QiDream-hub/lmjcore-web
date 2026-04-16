cmake_minimum_required(VERSION 3.10)
project(lmjcore_server VERSION 1.0.0 LANGUAGES C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) 

# -----------------------------------------------------------
# 1. 全局配置
# -----------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 编译选项
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # 添加 -g 以便调试，-O2 用于优化
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2 -g")
endif()

# -----------------------------------------------------------
# 2. 查找依赖 (LMDB)
# -----------------------------------------------------------

# 1. 查找头文件 (必须是 lmdb.h)
find_path(LMDB_INCLUDE_DIR
    NAMES lmdb.h
    PATHS /usr/include /usr/local/include
)

# 2. 查找库文件 (名字是 lmdb)
find_library(LMDB_LIBRARY
    NAMES lmdb
    PATHS /usr/lib /usr/local/lib
)

# 3. 检查结果
if(NOT LMDB_INCLUDE_DIR OR NOT LMDB_LIBRARY)
    message(FATAL_ERROR "LMDB not found. Please install lmdb development package.")
else()
    message(STATUS "Found LMDB Include: ${LMDB_INCLUDE_DIR}")
    message(STATUS "Found LMDB Library: ${LMDB_LIBRARY}")
endif()

# -----------------------------------------------------------
# 3. 定义路径变量
# -----------------------------------------------------------
set(SERVER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(THIRDPARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)

# LMJCore 路径
set(LMJCore_DIR ${THIRDPARTY_DIR}/LMJCore/core)
set(LMJCore_SRC_DIR ${LMJCore_DIR}/src)

# LMJCore Toolkit 路径
set(LMJCoreToolkit_DIR ${THIRDPARTY_DIR}/LMJCore/Toolkit/ptr_uuid_gen)

# URLRouter 路径
set(URLRouter_DIR ${THIRDPARTY_DIR}/URLRouter/src)

# -----------------------------------------------------------
# 4. 收集源文件
# -----------------------------------------------------------

# 4.1 主程序源文件
set(SERVER_SOURCES
    ${SERVER_SRC_DIR}/main.c
    ${SERVER_SRC_DIR}/routes.c
    ${SERVER_SRC_DIR}/http_server.c
    ${SERVER_SRC_DIR}/http_parser.c
    ${SERVER_SRC_DIR}/handlers/obj_handle.c
    ${SERVER_SRC_DIR}/handlers/set_handle.c
    ${SERVER_SRC_DIR}/handlers/utils_handle.c
    ${SERVER_SRC_DIR}/handlers/handle_utils.c
)

# 4.2 LMJCore 核心源文件
set(LMJCORE_LIB_SOURCES
    ${LMJCore_SRC_DIR}/lmjcore.c
)

# 4.3 Toolkit 源文件
set(TOOLKIT_SOURCES
    ${LMJCoreToolkit_DIR}/src/lmjcore_uuid_gen.c
)

# 4.4 URLRouter 源文件
set(URLROUTER_SOURCES
    ${URLRouter_DIR}/router.c
    ${URLRouter_DIR}/route_tree.c
    ${URLRouter_DIR}/pattern_compiler.c
    ${URLRouter_DIR}/extractor.c
    ${URLRouter_DIR}/lexer.c
    ${URLRouter_DIR}/feature_compiler.c
    ${URLRouter_DIR}/extractor_compiler.c
)

# 汇总所有源文件
set(ALL_SOURCES
    ${SERVER_SOURCES}
    ${LMJCORE_LIB_SOURCES}
    ${TOOLKIT_SOURCES}
    ${URLROUTER_SOURCES}
)

# -----------------------------------------------------------
# 5. 构建可执行文件
# -----------------------------------------------------------
add_executable(lmjcore_server ${ALL_SOURCES})

# 查找系统 llhttp 库
find_path(LLHTTP_INCLUDE_DIR
    NAMES llhttp.h
    PATHS /usr/include /usr/local/include
)

find_library(LLHTTP_LIBRARY
    NAMES llhttp
    PATHS /usr/lib /usr/local/lib
)

if(NOT LLHTTP_INCLUDE_DIR OR NOT LLHTTP_LIBRARY)
    message(FATAL_ERROR "llhttp not found. Please install llhttp development package.")
else()
    message(STATUS "Found llhttp Include: ${LLHTTP_INCLUDE_DIR}")
    message(STATUS "Found llhttp Library: ${LLHTTP_LIBRARY}")
endif()

# 添加头文件搜索路径
target_include_directories(lmjcore_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/handlers
    ${LMDB_INCLUDE_DIR}
    ${LLHTTP_INCLUDE_DIR}
    ${THIRDPARTY_DIR}/LMJCore/core/include
    ${THIRDPARTY_DIR}/LMJCore/Toolkit/ptr_uuid_gen/include
    ${THIRDPARTY_DIR}/URLRouter/include
)

# -----------------------------------------------------------
# 6. 链接库 (重点修改这里)
# -----------------------------------------------------------
target_link_libraries(lmjcore_server
    # 1. 先链接第三方库 (LMDB)
    # 注意：确保 ${LMDB_LIBRARY} 解析出来的是 /usr/lib/liblmdb.so
    ${LMDB_LIBRARY}

    # 2. llhttp HTTP 解析库（系统库）
    ${LLHTTP_LIBRARY}

    # 3. 再链接系统库
    # pthread, rt, m 等系统库通常放在后面
    pthread
    rt
    m
)