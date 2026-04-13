# CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(lmjcore_server VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 编译选项
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2 -g")
endif()

# 查找依赖
find_path(LMDB_INCLUDE_DIR
    NAMES lmdb.h
    HINTS /usr/include /usr/local/include
)

find_library(LMDB_LIBRARY
    NAMES lmdb liblmdb
    HINTS /usr/lib /usr/local/lib
)

if(NOT LMDB_INCLUDE_DIR OR NOT LMDB_LIBRARY)
    message(FATAL_ERROR "LMDB not found. Please install lmdb development package.")
endif()

# 设置路径
set(LMJCORE_SERVER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(LMJCORE_SERVER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(THIRDPARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)

# LMJCore 路径
set(LMJCore_DIR ${THIRDPARTY_DIR}/LMJCore/core)
set(LMJCore_INCLUDE_DIR ${LMJCore_DIR}/include)
set(LMJCore_SRC_DIR ${LMJCore_DIR}/src)

# LMJCore Toolkit 路径
set(LMJCoreToolkit_DIR ${THIRDPARTY_DIR}/LMJCore/Toolkit/ptr_uuid_gen)
set(LMJCoreToolkit_SRC ${LMJCoreToolkit_DIR}/src/lmjcore_uuid_gen.c)

# URLRouter 路径（注意：目录名是 URLRouer 不是 URLRouter）
set(URLRouter_DIR ${THIRDPARTY_DIR}/URLRouer)
set(URLRouter_SRC 
    ${URLRouter_DIR}/router.c
    ${URLRouter_DIR}/route_tree.c
    ${URLRouter_DIR}/pattern_compiler.c
    ${URLRouter_DIR}/extractor.c
)

# 源文件
set(SOURCES
    ${LMJCORE_SERVER_SRC_DIR}/main.c
    ${LMJCORE_SERVER_SRC_DIR}/routes.c
    ${LMJCORE_SERVER_SRC_DIR}/http_server.c
    ${LMJCORE_SERVER_SRC_DIR}/http_parser.c
    ${LMJCORE_SERVER_SRC_DIR}/lmjcore_handle.c
    ${LMJCore_SRC_DIR}/lmjcore.c
    ${LMJCoreToolkit_SRC}
    ${URLRouter_SRC}
)

# 头文件路径
include_directories(
    ${LMJCORE_SERVER_INCLUDE_DIR}
    ${LMJCore_INCLUDE_DIR}
    ${LMJCoreToolkit_DIR}/include
    ${URLRouter_DIR}
    ${LMDB_INCLUDE_DIR}
)

# 可执行文件
add_executable(lmjcore_server ${SOURCES})

# 链接库
target_link_libraries(lmjcore_server
    ${LMDB_LIBRARY}
    pthread
    rt
    m
)

# 安装规则
install(TARGETS lmjcore_server DESTINATION bin)

# 测试
enable_testing()
add_subdirectory(tests)