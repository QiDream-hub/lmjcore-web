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
    # 添加更严格的警告选项
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic -O2 -g")
    
    # 额外的警告选项（可选）
    # -Wconversion: 警告隐式类型转换
    # -Wshadow: 警告变量遮蔽
    # set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wconversion -Wshadow")
endif()

# -----------------------------------------------------------
# 2. 查找依赖 (LMDB, llhttp)
# -----------------------------------------------------------

# 使用 pkg-config 查找 LMDB（如果可用）
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LMDB QUIET lmdb)
endif()

if(NOT LMDB_FOUND)
    # 回退到传统查找方式
    find_path(LMDB_INCLUDE_DIR
        NAMES lmdb.h
        PATHS /usr/include /usr/local/include /opt/local/include
    )

    find_library(LMDB_LIBRARY
        NAMES lmdb
        PATHS /usr/lib /usr/local/lib /opt/local/lib
    )
    
    if(LMDB_INCLUDE_DIR)
        set(LMDB_INCLUDE_DIRS ${LMDB_INCLUDE_DIR})
    endif()
    if(LMDB_LIBRARY)
        set(LMDB_LIBRARIES ${LMDB_LIBRARY})
    endif()
endif()

# 检查结果
if(NOT LMDB_INCLUDE_DIRS OR NOT LMDB_LIBRARIES)
    message(FATAL_ERROR "LMDB not found. Please install lmdb development package.")
else()
    message(STATUS "Found LMDB Include: ${LMDB_INCLUDE_DIRS}")
    message(STATUS "Found LMDB Library: ${LMDB_LIBRARIES}")
endif()

# 使用 pkg-config 查找 llhttp（如果可用）
if(PkgConfig_FOUND)
    pkg_check_modules(LLHTTP QUIET llhttp)
endif()

if(NOT LLHTTP_FOUND)
    # 回退到传统查找方式
    find_path(LLHTTP_INCLUDE_DIR
        NAMES llhttp.h
        PATHS /usr/include /usr/local/include /opt/local/include
    )

    find_library(LLHTTP_LIBRARY
        NAMES llhttp
        PATHS /usr/lib /usr/local/lib /opt/local/lib
    )
    
    if(LLHTTP_INCLUDE_DIR)
        set(LLHTTP_INCLUDE_DIRS ${LLHTTP_INCLUDE_DIR})
    endif()
    if(LLHTTP_LIBRARY)
        set(LLHTTP_LIBRARIES ${LLHTTP_LIBRARY})
    endif()
endif()

# 检查结果
if(NOT LLHTTP_INCLUDE_DIRS OR NOT LLHTTP_LIBRARIES)
    message(FATAL_ERROR "llhttp not found. Please install llhttp development package.")
else()
    message(STATUS "Found llhttp Include: ${LLHTTP_INCLUDE_DIRS}")
    message(STATUS "Found llhttp Library: ${LLHTTP_LIBRARIES}")
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
    ${SERVER_SRC_DIR}/config.c
    ${SERVER_SRC_DIR}/log.c
    ${SERVER_SRC_DIR}/http_server.c
    ${SERVER_SRC_DIR}/http_parser.c
    ${SERVER_SRC_DIR}/handlers/batch_handle.c
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
# 5. 添加子模块（在 add_executable 之前）
# -----------------------------------------------------------
add_subdirectory(${THIRDPARTY_DIR}/cJSON)

# -----------------------------------------------------------
# 6. 构建可执行文件
# -----------------------------------------------------------
add_executable(lmjcore_server ${ALL_SOURCES})

# 添加头文件搜索路径
target_include_directories(lmjcore_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/handlers
    ${LMDB_INCLUDE_DIRS}
    ${LLHTTP_INCLUDE_DIRS}
    ${THIRDPARTY_DIR}/LMJCore/core/include
    ${THIRDPARTY_DIR}/LMJCore/Toolkit/ptr_uuid_gen/include
    ${THIRDPARTY_DIR}/URLRouter/include
    ${THIRDPARTY_DIR}/cJSON
)

# -----------------------------------------------------------
# 7. 链接库
# -----------------------------------------------------------
target_link_libraries(lmjcore_server
    # 1. 先链接第三方库 (LMDB)
    ${LMDB_LIBRARIES}

    # 2. llhttp HTTP 解析库（系统库）
    ${LLHTTP_LIBRARIES}

    # 3. cJSON JSON 解析库（子模块，库名为小写 cjson）
    cjson

    # 4. 再链接系统库
    pthread
    rt
    m
)

# -----------------------------------------------------------
# 8. 安装规则
# -----------------------------------------------------------
install(TARGETS lmjcore_server DESTINATION bin)
install(FILES lmjcore.conf DESTINATION etc OPTIONAL)