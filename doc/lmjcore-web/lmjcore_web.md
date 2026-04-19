# LMJCore-Web 详细设计文档

## 1. 项目概述

**LMJCore-Web** 是一个基于 LMJCore 存储引擎的轻量级 HTTP 服务，提供 RESTful API 访问接口。

### 1.1 项目定位

| 属性 | 描述 |
|------|------|
| **类型** | HTTP 服务器 / API 服务 |
| **语言** | C (C11 标准) |
| **核心依赖** | LMDB, LMJCore, URLRouter, llhttp |
| **构建系统** | CMake |
| **目标平台** | Linux, macOS |

### 1.2 核心特性

| 特性 | 说明 |
|------|------|
| **RESTful API** | 提供标准的 REST 风格接口 |
| **事务管理** | 请求级自动事务，支持超时控制 |
| **链式查询** | 支持嵌套路径解析查询 |
| **自动类型识别** | 智能识别指针引用、原始数据、空值 |
| **集合支持** | 支持无序集合的 CRUD 操作 |
| **高性能 HTTP 解析** | 基于 llhttp 实现，100% RFC 7230 兼容 |

---

## 2. 技术架构

### 2.1 系统架构图

```
┌─────────────────────────────────────────────────────────┐
│                    HTTP Client                          │
└─────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────┐
│                   HTTP Server Layer                     │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐ │
│  │http_parser  │  │   routes    │  │http_server      │ │
│  │(解析请求)   │  │(路由分发)   │  │(监听/响应)      │ │
│  └─────────────┘  └─────────────┘  └─────────────────┘ │
└─────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────┐
│                 Handler Layer                           │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐ │
│  │obj_handle   │  │set_handle   │  │utils_handle     │ │
│  │(对象操作)   │  │(集合操作)   │  │(工具接口)       │ │
│  └─────────────┘  └─────────────┘  └─────────────────┘ │
│  ┌─────────────────────────────────────────────────────┐│
│  │              handle_utils (通用工具)                ││
│  │  [指针转换] [路径解析] [值编解码] [响应构建]        ││
│  └─────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────┐
│                      LMJCore                            │
│                 (核心存储引擎)                           │
└─────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────┐
│                      LMDB                               │
│                 (底层存储引擎)                           │
└─────────────────────────────────────────────────────────┘
```

### 2.2 模块分层

| 层级 | 模块 | 职责 |
|------|------|------|
| **网络层** | `http_server.c/h` | TCP 监听、连接管理、线程池 |
| **协议层** | `http_parser.c/h` | HTTP 请求/响应解析与构建（基于 llhttp） |
| **路由层** | `routes.c/h` | URL 路由注册与匹配 |
| **业务层** | `handlers/*.c` | 对象/集合/工具处理器 |
| **工具层** | `handle_utils.c/h` | 指针转换、路径解析、值编解码 |
| **存储层** | LMJCore + LMDB | 数据持久化 |

---

## 3. 模块化设计

### 3.1 源代码结构

```
src/
├── main.c                  # 程序入口
├── http_server.c           # HTTP 服务器实现
├── http_parser.c           # HTTP 解析器实现
├── routes.c                # 路由注册
└── handlers/               # 处理器模块
    ├── obj_handle.c        # 对象相关处理器
    ├── set_handle.c        # 集合相关处理器
    ├── utils_handle.c      # 工具相关处理器
    └── handle_utils.c      # 通用工具函数
```

### 3.2 头文件结构

```
include/
├── error_codes.h           # 错误码定义与 HTTP 映射
├── error_response.h        # 统一错误响应构建
├── handle_utils.h          # 工具函数声明与类型定义
├── http_parser.h           # HTTP 解析器接口
├── http_server.h           # HTTP 服务器接口
├── lmjcore_handle.h        # 处理器声明（聚合头文件）
└── routes.h                # 路由注册接口
```

### 3.3 模块职责

#### 3.3.0 HTTP 解析器 (`http_parser.c/h`)

基于 [llhttp](https://github.com/nodejs/llhttp) 实现的 HTTP 协议解析器。

| 函数 | 说明 |
|------|------|
| `http_parser_create()` | 创建解析器上下文 |
| `http_parser_execute()` | 执行流式解析 |
| `http_parser_get_request()` | 获取解析后的请求结构 |
| `http_parser_reset()` | 重置解析器状态（支持 Keep-Alive） |
| `http_parser_destroy()` | 销毁解析器 |
| `http_build_response()` | 构建 HTTP 响应 |

**回调函数**：
- `on_url` - URL 解析完成回调
- `on_body` - 请求体数据回调
- `on_header_field` - 头部字段回调
- `on_header_value` - 头部值回调
- `on_message_complete` - 请求完成回调

**特性**：
- 100% RFC 7230 兼容
- 支持分块传输编码（Chunked Transfer Encoding）
- 支持 HTTP 管道化（Pipelining）
- 自动处理边缘场景

#### 3.3.1 对象处理器 (`obj_handle.c`)

| 函数 | HTTP 方法 | 路径 | 说明 |
|------|-----------|------|------|
| `handle_obj_create` | POST | `/obj` | 创建空对象 |
| `handle_obj_get` | GET | `/obj/{ptr}` | 获取完整对象 |
| `handle_obj_member_get` | GET | `/obj/{ptr}/{member}` | 获取成员值 |
| `handle_obj_member_put` | PUT | `/obj/{ptr}/{member}` | 设置成员值 |
| `handle_obj_member_del` | DELETE | `/obj/{ptr}/{member}` | 删除成员 |
| `handle_obj_del` | DELETE | `/obj/{ptr}` | 删除完整对象 |
| `handle_obj_query` | GET | `/obj/query?path=...` | 链式查询 |

#### 3.3.2 集合处理器 (`set_handle.c`)

| 函数 | HTTP 方法 | 路径 | 说明 |
|------|-----------|------|------|
| `handle_set_create` | POST | `/set` | 创建空集合 |
| `handle_set_get` | GET | `/set/{ptr}` | 获取完整集合 |
| `handle_set_add` | POST | `/set/{ptr}/elements` | 添加元素 |
| `handle_set_remove` | DELETE | `/set/{ptr}/elements` | 删除元素 |
| `handle_set_del` | DELETE | `/set/{ptr}` | 删除完整集合 |

#### 3.3.3 工具处理器 (`utils_handle.c`)

| 函数 | HTTP 方法 | 路径 | 说明 |
|------|-----------|------|------|
| `handle_ptr_exist` | GET | `/ptr/{ptr}/exist` | 检查指针是否存在 |
| `handle_health` | GET | `/health` | 健康检查 |

#### 3.3.4 通用工具 (`handle_utils.c`)

| 函数 | 说明 |
|------|------|
| `route_params_get` | 从路由参数中获取指定索引的参数 |
| `json_get_string` | 从 JSON 对象中提取字符串值 |
| `lmjcore_ptr_from_hex` | 十六进制字符串 → 二进制指针 |
| `lmjcore_ptr_to_hex` | 二进制指针 → 十六进制字符串 |
| `lmjcore_parse_query_path` | 解析链式查询路径 |
| `lmjcore_encode_value` | 编码值为存储格式 |
| `lmjcore_decode_value` | 解码存储格式的值 |

---

## 4. 数据存储设计

### 4.1 指针存储格式

所有值统一添加 1 字节类型标记：

```
┌──────────────────────────────────────────┐
│ 原始数据：  [0x00][data...]              │
│ 指针引用：  [0x01][17B 指针]             │
│ 空值：      [0x02]                       │
└──────────────────────────────────────────┘
```

### 4.2 指针表示

- **格式**: 34 位十六进制字符串
- **类型前缀**: 
  - `01` = 对象 (LMJCORE_OBJ)
  - `02` = 集合 (LMJCORE_SET)
- **示例**: `01abc123def456789012345678901234`

### 4.3 链式查询路径

```
格式：<指针>.<member1>.<member2>.<member3>

示例：01abc123.user.profile.name
URL:  GET /obj/query?path=01abc123.user.profile.name
```

---

## 5. 错误处理

### 5.1 错误响应构建

使用统一的错误响应构建函数（`error_response.h`）：

```c
// 便捷宏
RETURN_ERROR_INVALID_PARAM(response);      // 参数无效
RETURN_ERROR_MISSING_PARAM("ptr", response); // 缺少参数
RETURN_ERROR_INVALID_PTR(response);        // 指针格式无效
RETURN_ERROR_NOT_FOUND("Object", response); // 实体不存在
RETURN_ERROR_NO_MEMORY(response);          // 内存分配失败
RETURN_ERROR_TXN_FAILED("begin", response); // 事务错误
RETURN_ERROR_BODY_PARSE(response);         // 请求体解析失败
```

### 5.2 错误码映射

| LMJCore 错误码 | HTTP 状态码 | 说明 |
|----------------|-------------|------|
| `LMJCORE_SUCCESS` | 200 | 成功 |
| `LMJCORE_ERROR_ENTITY_NOT_FOUND` | 404 | 实体不存在 |
| `LMJCORE_ERROR_MEMBER_NOT_FOUND` | 404 | 成员不存在 |
| `LMJCORE_ERROR_INVALID_PARAM` | 400 | 参数无效 |
| `LMJCORE_ERROR_PATH_PARSE` | 400 | 路径解析错误 |
| `LMJCORE_ERROR_SET_NOT_SUPPORTED` | 400 | 集合不支持链式解析 |
| `LMJCORE_ERROR_TXN_TIMEOUT` | 408 | 事务超时 |
| `LMJCORE_ERROR_READONLY_TXN` | 405 | 方法不允许 |
| `LMJCORE_ERROR_MEMORY_ALLOCATION` | 500 | 内存分配失败 |

### 5.3 扩展错误码范围

```
-32100 ~ -32199 (LMJCore 网络壳扩展)

-32101: LMJCORE_ERROR_TXN_TIMEOUT
-32102: LMJCORE_ERROR_TXN_BEGIN_FAILED
-32121: LMJCORE_ERROR_PATH_PARSE
-32141: LMJCORE_ERROR_SET_NOT_SUPPORTED
-32161: LMJCORE_ERROR_HTTP_PARSE
-32181: LMJCORE_ERROR_SERVER_INIT
```

---

## 6. 事务管理

### 6.1 事务模型

- **自动管理**: 每个 HTTP 请求自动开启/提交/回滚事务
- **超时控制**: 默认 5 秒超时，返回 408 错误
- **链式查询**: 多次解析在同一事务内完成

### 6.2 事务流程

```
请求到达
    │
    ▼
开启事务 (lmjcore_txn_begin)
    │
    ▼
执行业务逻辑
    │
    ├── 成功 → 提交事务 (lmjcore_txn_commit)
    │
    └── 失败 → 回滚事务 (lmjcore_txn_abort)
    │
    ▼
返回响应
```

---

## 7. 配置管理

### 7.1 配置方式

LMJCore-Web 支持三种配置方式，优先级从高到低为：

1. **命令行参数** - 最高优先级，直接覆盖其他配置
2. **配置文件** - 推荐方式，便于管理和版本控制
3. **默认值** - 内置默认配置

### 7.2 命令行参数

```bash
./lmjcore_server [OPTIONS]
```

| 参数 | 简写 | 说明 | 默认值 |
|------|------|------|--------|
| `--host <addr>` | `-H` | 监听地址 | `0.0.0.0` |
| `--port <port>` | `-p` | 监听端口 | `8080` |
| `--db-path <path>` | `-d` | LMDB 数据库路径 | `./lmjcore_data` |
| `--map-size <size>` | `-m` | 内存映射大小 (支持 K/M/G 后缀) | `10M` |
| `--max-connections <n>` | `-c` | 最大连接数 | `128` |
| `--txn-timeout <sec>` | `-t` | 事务超时 (秒) | `5` |
| `--config <file>` | `-C` | 配置文件路径 | `lmjcore.conf` |
| `--daemon` | `-D` | 守护进程模式 | `false` |
| `--log-level <0-3>` | `-l` | 日志级别 (0=DEBUG, 3=ERROR) | `1` |
| `--help` | `-h` | 显示帮助信息 | - |

**使用示例**：

```bash
# 快速启动（使用默认配置）
./lmjcore_server

# 指定端口和数据库路径
./lmjcore_server -p 9000 -d /data/lmjcore

# 使用配置文件并以后台模式运行
./lmjcore_server -C /etc/lmjcore.conf --daemon

# 调试模式（显示详细配置）
./lmjcore_server -l 0
```

### 7.3 配置文件

配置文件采用简单的 `key = value` 格式，支持 `#` 和 `;` 注释。

**示例配置文件** (`lmjcore.conf`)：

```ini
# ===========================================
# LMJCore-Web 配置文件
# ===========================================

# ----------------------------
# 网络配置
# ----------------------------

# 监听地址 (默认：0.0.0.0)
host = 0.0.0.0

# 监听端口 (默认：8080)
port = 8080

# ----------------------------
# 数据库配置
# ----------------------------

# LMDB 数据库存储路径 (默认：./lmjcore_data)
db_path = ./lmjcore_data

# 内存映射大小 (默认：10MB，支持 K/M/G 后缀)
map_size = 10M

# ----------------------------
# 连接配置
# ----------------------------

# 最大并发连接数 (默认：128)
max_connections = 128

# 事务超时时间 (秒) (默认：5 秒)
txn_timeout = 5

# ----------------------------
# 运行模式配置
# ----------------------------

# 是否以守护进程模式运行 (默认：false)
daemon = false

# 日志级别 (0=DEBUG, 1=INFO, 2=WARN, 3=ERROR)
log_level = 1
```

### 7.4 服务器配置结构

```c
typedef struct {
    char *host;              // 监听地址 (默认："0.0.0.0")
    int port;                // 监听端口 (默认：8080)
    const char *db_path;     // LMDB 数据库路径
    size_t map_size;         // 内存映射大小 (默认：10MB)
    lmjcore_ptr_generator_fn fn; // 指针生成函数
    unsigned int env_flags;  // 环境标志 (默认：0)
    int max_connections;     // 最大连接数 (默认：128)
} server_config_t;
```

### 7.5 默认配置值

| 参数 | 默认值 | 说明 |
|------|--------|------|
| 监听地址 | `0.0.0.0` | 监听所有网络接口 |
| 监听端口 | `8080` | HTTP 服务端口 |
| 数据库路径 | `./lmjcore_data` | LMDB 数据目录 |
| 内存映射 | `10MB` | LMDB 映射大小 |
| 最大连接数 | `128` | 并发连接上限 |
| 事务超时 | `5 秒` | 请求级事务超时 |
| 日志级别 | `INFO` | 1=INFO |

---

## 8. 关键约束

| 约束项 | 限制值 | 说明 |
|--------|--------|------|
| 成员名长度 | ≤ 493 字节 | 受 LMDB Key 限制 |
| 路径深度 | ≤ 100 层 | 防止栈溢出 |
| 事务超时 | 默认 5 秒 | 防止长时间阻塞 |
| 集合特性 | 无序 | 不保证插入顺序 |
| 写事务 | 串行 | 依赖 LMDB 单写者模型 |

---

## 9. 编译与构建

### 9.1 依赖要求

| 依赖 | 版本要求 | 说明 |
|------|----------|------|
| CMake | >= 3.10 | 构建系统 |
| GCC/Clang | 支持 C11 | 编译器 |
| LMDB | 任意稳定版本 | 底层存储引擎 |
| llhttp | >= 2.0 | HTTP 解析库（Node.js 同款） |

**安装 llhttp**（如系统未预装）：

```bash
# Arch Linux
pacman -S llhttp

# Debian/Ubuntu
apt-get install libllhttp-dev

# 从源码编译
git clone https://github.com/nodejs/llhttp.git
cd llhttp
make
sudo make install
```

### 9.2 CMake 配置

```cmake
# 查找系统 llhttp 库
find_path(LLHTTP_INCLUDE_DIR
    NAMES llhttp.h
    PATHS /usr/include /usr/local/include
)

find_library(LLHTTP_LIBRARY
    NAMES llhttp
    PATHS /usr/lib /usr/local/lib
)

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

# 链接库
target_link_libraries(lmjcore_server
    ${LMDB_LIBRARY}
    ${LLHTTP_LIBRARY}
    pthread
    rt
    m
)
```

---

## 10. 部署指南

### 10.1 生产环境部署

** systemd 服务配置** (`/etc/systemd/system/lmjcore.service`)：

```ini
[Unit]
Description=LMJCore HTTP Server
After=network.target

[Service]
Type=simple
User=lmjcore
Group=lmjcore
WorkingDirectory=/opt/lmjcore
ExecStart=/opt/lmjcore/lmjcore_server -C /etc/lmjcore.conf
Restart=on-failure
RestartSec=5
LimitNOFILE=65535

[Install]
WantedBy=multi-user.target
```

**启用服务**：

```bash
# 创建用户
sudo useradd -r -s /bin/false lmjcore

# 安装文件
sudo mkdir -p /opt/lmjcore
sudo cp build/lmjcore_server /opt/lmjcore/
sudo cp lmjcore.conf /etc/lmjcore.conf

# 设置权限
sudo chown -R lmjcore:lmjcore /opt/lmjcore

# 启用服务
sudo systemctl daemon-reload
sudo systemctl enable lmjcore
sudo systemctl start lmjcore

# 检查状态
sudo systemctl status lmjcore
```

### 10.2 守护进程模式

也可直接使用内置的守护进程模式：

```bash
./lmjcore_server --config /etc/lmjcore.conf --daemon
```

### 10.3 日志管理

LMJCore-Web 内置了统一的日志系统，支持级别控制和彩色输出。

**日志级别**：

| 级别 | 值 | 说明 | 输出内容 |
|------|-----|------|----------|
| DEBUG | 0 | 调试信息 | 所有日志 |
| INFO  | 1 | 一般信息 | INFO 及以上 |
| WARN  | 2 | 警告信息 | WARN 及以上 |
| ERROR | 3 | 错误信息 | 仅 ERROR |

**日志格式**：

```
[时间戳] 级别 [文件：行号] 消息
```

**示例输出**：

```
[2026-04-17 21:07:35.883] INFO  [routes.c:90] Routes registered successfully:
[2026-04-17 21:07:37.710] INFO  [http_server.c:51] 127.0.0.1 - GET /health -> 200
[2026-04-17 21:07:37.714] WARN  [http_server.c:49] 127.0.0.1 - GET /invalid -> 404
```

**配置方式**：

```bash
# 命令行设置日志级别
./lmjcore_server -l 0  # DEBUG
./lmjcore_server -l 1  # INFO (默认)
./lmjcore_server -l 2  # WARN
./lmjcore_server -l 3  # ERROR

# 配置文件设置
log_level = 1  # 在 lmjcore.conf 中
```

**特性**：

- 自动彩色输出（终端模式下）
- 警告和错误自动输出到 stderr
- 守护进程模式自动禁用颜色
- 线程安全的日志输出

---

## 11. 设计原则

1. **保持简单**: 只做基础 CRUD，不实现复杂查询
2. **透明存储**: 客户端感知指针和类型
3. **性能优先**: 充分利用 LMDB 特性
4. **明确边界**: 清晰划分网络壳与内核职责
5. **可扩展性**: 预留批量操作、缓存等扩展接口

---

## 12. 未来扩展

| 功能 | 状态 | 说明 |
|------|------|------|
| 批量操作 | 待实现 | 支持批量插入/更新/删除 |
| 缓存层 | 待实现 | 热点数据缓存 |
| 监控接口 | 待实现 | Prometheus 指标导出 |
| 认证授权 | 待实现 | API Token 验证 |
| 跨域支持 | 待实现 | CORS 头配置 |

---

## 13. llhttp 迁移说明

### 13.1 迁移背景

项目最初使用自研 HTTP 解析器（约 300 行手写解析逻辑），后迁移至 [llhttp](https://github.com/nodejs/llhttp) —— Node.js 同款高性能 HTTP 解析器。

### 13.2 迁移收益

| 维度 | 原实现 | 迁移后 |
|------|--------|--------|
| **代码量** | ~300 行手写解析逻辑 | ~50 行回调注册 |
| **RFC 合规性** | 基础支持 | 100% 兼容 RFC 7230 |
| **性能** | 一般 | 业界最快 |
| **边缘场景** | 手动处理 | 自动处理 |
| **维护成本** | 自行修复 bug | 社区维护 |

### 13.3 技术实现

**枚举冲突解决**：
由于 llhttp.h 的 `HTTP_GET` 等枚举值与项目 URLRouter 的 `router.h` 中枚举值命名冲突，采用了隔离方案：
- 在 `http_parser.c` 中不直接包含 `router.h`
- 使用内部类型定义进行解析
- 在返回请求时进行枚举值转换

**API 设计**：
```c
// 创建解析器
http_parser_context_t *ctx = NULL;
http_parser_create(&ctx);

// 执行解析
int ret = http_parser_execute(ctx, data, data_len);

// 获取请求
http_request_t *req = http_parser_get_request(ctx);

// 清理资源
http_parser_destroy(ctx);
```

### 13.4 系统要求

llhttp 库已通过系统包管理器安装：
- **Arch Linux**: `pacman -S llhttp`
- **Debian/Ubuntu**: `apt-get install libllhttp-dev`

项目使用系统库而非子模块，简化了依赖管理。

---

## 14. 项目结构

```
lmjcore-web/
├── CMakeLists.txt          # CMake 构建配置
├── README.md               # 项目说明文档
├── lmjcore.conf            # 示例配置文件
├── QWEN.md                 # 开发上下文文档
├── compile_commands.json   # 编译数据库 (LSP 使用)
├── doc/                    # 详细设计文档
│   └── lmjcore-web/
│       ├── lmjcore_web.md      # 架构设计文档
│       └── API_REFERENCE.md    # API 参考文档
├── include/                # 公共头文件
│   ├── config.h            # 配置管理接口
│   ├── error_codes.h       # 错误码定义与 HTTP 映射
│   ├── error_response.h    # 统一错误响应构建宏
│   ├── handle_utils.h      # 工具函数声明
│   ├── http_parser.h       # HTTP 解析器接口
│   ├── http_server.h       # HTTP 服务器接口
│   ├── lmjcore_handle.h    # 处理器声明 (聚合头文件)
│   └── routes.h            # 路由注册接口
├── src/                    # 源代码
│   ├── main.c              # 程序入口
│   ├── config.c            # 配置解析实现
│   ├── routes.c            # 路由注册
│   ├── http_server.c       # HTTP 服务器实现
│   ├── http_parser.c       # HTTP 解析器实现 (基于 llhttp)
│   └── handlers/           # 处理器模块
│       ├── obj_handle.c    # 对象 CRUD 处理器
│       ├── set_handle.c    # 集合 CRUD 处理器
│       ├── utils_handle.c  # 工具接口处理器
│       └── handle_utils.c  # 通用工具函数
├── tests/                  # 测试文件
│   └── api_test.html       # API 测试工具 (浏览器)
└── thirdparty/             # 第三方子模块
    ├── LMJCore/            # LMJCore 存储引擎
    └── URLRouter/          # URL 路由库
```

---

## 15. 参考文档

- [API 参考文档](./API_REFERENCE.md) - 完整 API 文档
