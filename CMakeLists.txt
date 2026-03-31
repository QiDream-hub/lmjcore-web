cmake_minimum_required(VERSION 3.10)
project(lmjcore-web VERSION 1.0.0)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2")

# 设置 LMJCore 路径
set(LMJCORE_PATH ${CMAKE_SOURCE_DIR}/thirdparty/LMJCore)
set(LMJCORE_CORE_PATH ${LMJCORE_PATH}/core)

# 依赖查找
find_path(LMDB_INCLUDE_DIR NAMES lmdb.h)
find_library(LMDB_LIBRARY NAMES lmdb)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUV REQUIRED libuv)
pkg_check_modules(LIBUUID REQUIRED uuid)

# 头文件路径
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${LMJCORE_CORE_PATH}/include          # LMJCore 主头文件
    ${LMDB_INCLUDE_DIR}
    ${LIBUV_INCLUDE_DIRS}
    ${LIBUUID_INCLUDE_DIRS}
)

# LMJCore 源文件
set(LMJCORE_SOURCES
    ${LMJCORE_CORE_PATH}/src/lmjcore.c
)

# LMJCore Toolkit 源文件（可选，根据需要添加）
set(LMJCORE_TOOLKIT_SOURCES
    ${LMJCORE_PATH}/Toolkit/ptr_uuid_gen/src/lmjcore_uuid_gen.c
    ${LMJCORE_PATH}/Toolkit/result_parser/src/result_parser.c
)

# 主项目源文件
set(SOURCES
    src/main.c
    src/lmjcore_web.c
    src/http_server.c
    src/chain_query.c
    src/ptr_generator.c
)

# 生成可执行文件
add_executable(lmjcore-web ${SOURCES} ${LMJCORE_SOURCES} ${LMJCORE_TOOLKIT_SOURCES})

# 链接库
target_link_libraries(lmjcore-web
    ${LMDB_LIBRARY}
    ${LIBUV_LIBRARIES}
    ${LIBUUID_LIBRARIES}
    pthread
    rt
    m                                               # 数学库，LMJCore可能需要
)

# 如果 LMJCore 需要特定的编译选项
target_compile_definitions(lmjcore-web PRIVATE
    LMJCORE_BUILD
)

# 可选：生成编译数据库（方便 IDE 和工具）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 安装
install(TARGETS lmjcore-web DESTINATION bin)

# 可选：添加测试
enable_testing()
add_test(NAME test_client 
         COMMAND ${CMAKE_COMMAND} -E echo "Run python tests/test_client.py"
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})