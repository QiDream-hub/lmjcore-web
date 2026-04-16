# LMJCore-Web 项目上下文

## 项目概述

**LMJCore-Web** 是一个基于 LMJCore 存储引擎的轻量级 HTTP 服务器，提供 RESTful API 访问接口。

| 属性 | 描述 |
|------|------|
| **类型** | HTTP 服务器 / API 服务 |
| **语言** | C (C11 标准) |
| **核心依赖** | LMDB, LMJCore, URLRouter, llhttp |
| **构建系统** | CMake |
| **目标平台** | Linux, macOS |

### 核心特性

- **RESTful API** - 标准的 REST 风格接口
- **事务管理** - 请求级自动事务，超时控制
- **链式查询** - 支持嵌套路径解析
- **自动类型识别** - 智能识别指针/原始数据/空值
- **高性能** - 基于 LMDB 存储引擎 + llhttp HTTP 解析

---

## 项目结构

```
lmjcore-web/
├── CMakeLists.txt          # CMake 构建配置
├── README.md               # 项目说明文档
├── QWEN.md                 # 本文件 - 开发上下文
├── compile_commands.json   # 编译数据库 (LSP 使用)
├── doc/                    # 详细设计文档
│   └── lmjcore-web/
│       ├── lmjcore_web.md      # 架构设计文档
│       └── API_REFERENCE.md    # API 参考文档
├── include/                # 公共头文件
│   ├── error_codes.h       # 错误码定义与 HTTP 映射
│   ├── error_response.h    # 统一错误响应构建宏
│   ├── handle_utils.h      # 工具函数声明
│   ├── http_parser.h       # HTTP 解析器接口
│   ├── http_server.h       # HTTP 服务器接口
│   ├── lmjcore_handle.h    # 处理器声明 (聚合头文件)
│   └── routes.h            # 路由注册接口
├── src/                    # 源代码
│   ├── main.c              # 程序入口
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

## 构建与运行

### 依赖要求

| 依赖 | 安装命令 (Arch Linux) | 说明 |
|------|----------------------|------|
| CMake | `pacman -S cmake` | >= 3.10 |
| LMDB | `pacman -S lmdb` | 底层存储引擎 |
| llhttp | `pacman -S llhttp` | HTTP 解析库 |
| GCC/Clang | `pacman -S base-devel` | C11 编译器 |

### 编译步骤

```bash
# 1. 初始化子模块 (首次克隆后)
git submodule update --init --recursive

# 2. 创建并进入 build 目录
mkdir build && cd build

# 3. 配置 CMake
cmake ..

# 4. 编译
make

# 5. 运行服务器
./lmjcore_server
```

服务器默认启动在 `http://localhost:8080`

### 测试

打开浏览器访问 `tests/api_test.html` 或使用 curl：

```bash
# 健康检查
curl http://localhost:8080/health

# 创建对象
curl -X POST http://localhost:8080/obj

# 获取对象
curl http://localhost:8080/obj/{ptr}
```

---

## API 概览

### 对象操作

| 方法 | 端点 | 说明 |
|------|------|------|
| `POST` | `/obj` | 创建空对象 |
| `GET` | `/obj/{ptr}` | 获取完整对象 |
| `GET` | `/obj/{ptr}/{member}` | 获取成员值 |
| `PUT` | `/obj/{ptr}/{member}` | 设置成员值 |
| `DELETE` | `/obj/{ptr}/{member}` | 删除成员 |
| `GET` | `/obj/query?path=...` | 链式查询 |

### 集合操作

| 方法 | 端点 | 说明 |
|------|------|------|
| `POST` | `/set` | 创建空集合 |
| `GET` | `/set/{ptr}` | 获取完整集合 |
| `POST` | `/set/{ptr}/elements` | 添加元素 |
| `DELETE` | `/set/{ptr}/elements` | 删除元素 |

### 工具接口

| 方法 | 端点 | 说明 |
|------|------|------|
| `GET` | `/ptr/{ptr}/exist` | 检查指针是否存在 |
| `GET` | `/health` | 健康检查 |

---

## 开发约定

### 代码风格

- **命名规范**: 小写 + 下划线 (`snake_case`)
- **函数命名**: 模块前缀 + 功能描述 (如 `http_server_init`, `handle_obj_get`)
- **头文件保护**: `#ifndef HEADER_NAME_H` 风格
- **注释**: Doxygen 风格 (`@brief`, `@param`, `@return`)

### 模块化设计

处理器按功能分离在 `src/handlers/` 目录：

| 文件 | 职责 |
|------|------|
| `obj_handle.c` | 对象 CRUD 操作 |
| `set_handle.c` | 集合 CRUD 操作 |
| `utils_handle.c` | 工具接口 (健康检查、指针验证) |
| `handle_utils.c` | 通用工具函数 (指针转换、路径解析、值编解码) |

### 错误处理

使用统一的错误响应构建宏 (`error_response.h`)：

```c
RETURN_ERROR_INVALID_PARAM(response);      // 参数无效
RETURN_ERROR_NOT_FOUND("Object", response); // 实体不存在
RETURN_ERROR_NO_MEMORY(response);          // 内存分配失败
RETURN_ERROR_TXN_FAILED("begin", response); // 事务错误
```

错误码定义在 `error_codes.h`，范围 `-32100 ~ -32199`，并自动映射到 HTTP 状态码。

### 事务管理

- 每个 HTTP 请求自动开启/提交/回滚事务
- 默认 5 秒超时，返回 408 错误
- 链式查询在同一事务内完成

---

## 技术细节

### 数据存储格式

所有值统一添加 1 字节类型标记：

```
┌──────────────────────────────────────────┐
│ 原始数据：  [0x00][data...]              │
│ 指针引用：  [0x01][17B 指针]             │
│ 空值：      [0x02]                       │
└──────────────────────────────────────────┘
```

指针格式：34 位十六进制字符串
- `01` 前缀 = 对象 (LMJCORE_OBJ)
- `02` 前缀 = 集合 (LMJCORE_SET)

### 关键约束

| 约束项 | 限制值 | 说明 |
|--------|--------|------|
| 成员名长度 | ≤ 493 字节 | 受 LMDB Key 限制 |
| 路径深度 | ≤ 100 层 | 防止栈溢出 |
| 事务超时 | 默认 5 秒 | 防止长时间阻塞 |

### llhttp 集成

项目使用 Node.js 同款 [llhttp](https://github.com/nodejs/llhttp) 进行 HTTP 解析：
- 100% RFC 7230 兼容
- 支持分块传输编码
- 支持 HTTP 管道化

---

## 相关文档

- [README.md](./README.md) - 快速开始指南
- [doc/lmjcore-web/lmjcore_web.md](./doc/lmjcore-web/lmjcore_web.md) - 详细设计文档
- [doc/lmjcore-web/API_REFERENCE.md](./doc/lmjcore-web/API_REFERENCE.md) - 完整 API 文档
