# LMJCore-Web 项目说明文档

## 1. 项目概述

**LMJCore-Web** 是一个基于 LMJCore 存储引擎的轻量级 HTTP 网络服务壳，提供 RESTful API 访问接口。该项目将 LMJCore 的核心功能封装为 HTTP 服务，支持链式查询、事务管理、自动类型识别等特性。

### 1.1 项目定位

- **类型**: HTTP 服务器 / API 网关
- **语言**: C (C11 标准)
- **核心依赖**: LMDB, LMJCore, URLRouter
- **构建系统**: CMake

### 1.2 核心特性

| 特性 | 说明 |
|------|------|
| RESTful API | 提供标准的 REST 风格接口 |
| 事务管理 | 请求级自动事务，支持超时控制 |
| 链式查询 | 支持嵌套路径解析查询 |
| 自动类型识别 | 智能识别指针引用、原始数据、空值 |
| 集合支持 | 支持无序集合的 CRUD 操作 |

---

## 2. 项目结构

```
lmjcore-web/
├── CMakeLists.txt          # CMake 构建配置
├── README.md               # 项目需求文档
├── doc/                    # 文档目录
│   ├── lmjcore-web/        # 项目说明文档
│   └── progress_report.md  # 项目进度报告
├── include/                # 头文件目录
│   ├── error_codes.h       # 错误码定义
│   ├── http_parser.h       # HTTP 解析器接口
│   ├── http_server.h       # HTTP 服务器接口
│   ├── lmjcore_handle.h    # LMJCore 处理器接口
│   └── routes.h            # 路由注册接口
├── src/                    # 源代码目录
│   ├── main.c              # 程序入口
│   ├── http_server.c       # HTTP 服务器实现
│   ├── http_parser.c       # HTTP 解析器实现
│   ├── routes.c            # 路由处理实现
│   └── lmjcore_handle.c    # LMJCore 处理器实现
├── tests/                  # 测试代码目录
│   └── httpServer.c        # HTTP 服务器测试
└── thirdparty/             # 第三方依赖
    ├── LMJCore/            # LMJCore 核心库
    └── URLRouer/           # URL 路由库
```

---

## 3. 技术架构

### 3.1 核心组件

```
┌─────────────────────────────────────────────────────────┐
│                    HTTP Client                          │
└─────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────┐
│                   HTTP Server                           │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐ │
│  │http_parser  │  │   routes    │  │http_server      │ │
│  │(解析请求)   │  │(路由分发)   │  │(监听/响应)      │ │
│  └─────────────┘  └─────────────┘  └─────────────────┘ │
└─────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────┐
│                 LMJCore Handle Layer                    │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐ │
│  │Value Codec  │  │Path Parser  │  │Transaction Mgr  │ │
│  │(值编解码)   │  │(路径解析)   │  │(事务管理)       │ │
│  └─────────────┘  └─────────────┘  └─────────────────┘ │
└─────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────┐
│                      LMDB                               │
│                 (底层存储引擎)                           │
└─────────────────────────────────────────────────────────┘
```

### 3.2 模块说明

| 模块 | 文件 | 职责 |
|------|------|------|
| HTTP 服务器 | `http_server.c/h` | 监听端口、处理连接、发送响应 |
| HTTP 解析器 | `http_parser.c/h` | 解析 HTTP 请求报文 |
| 路由管理 | `routes.c/h` | URL 路由注册与分发 |
| 值处理器 | `lmjcore_handle.c/h` | LMJCore 数据操作封装 |
| 错误处理 | `error_codes.h` | 统一错误码定义与 HTTP 映射 |

---

## 4. API 接口设计

### 4.1 对象操作接口

| 方法 | 路径 | 说明 |
|------|------|------|
| `POST` | `/obj` | 创建空对象，返回指针 |
| `GET` | `/obj/{ptr}` | 获取完整对象 |
| `GET` | `/obj/{ptr}/{member}` | 获取成员值 |
| `PUT` | `/obj/{ptr}/{member}` | 设置成员值 |
| `DELETE` | `/obj/{ptr}/{member}` | 删除成员 |
| `GET` | `/obj/query?path={path}` | 链式查询 |

**示例：链式查询**
```bash
GET /obj/query?path=01abc123.user.profile.name
```

### 4.2 集合操作接口

| 方法 | 路径 | 说明 |
|------|------|------|
| `POST` | `/set` | 创建空集合 |
| `GET` | `/set/{ptr}` | 获取完整集合 |
| `POST` | `/set/{ptr}/elements` | 添加元素 |
| `DELETE` | `/set/{ptr}/elements` | 删除元素 |

### 4.3 工具接口

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/ptr/{ptr}/exist` | 检查指针是否存在 |
| `GET` | `/health` | 健康检查 |

---

## 5. 数据格式

### 5.1 指针存储格式

所有值统一添加 1 字节类型标记：

```
原始数据：  [0x00][data...]
指针引用：  [0x01][17B 指针]
空值：      [0x02]
```

### 5.2 指针表示

- 所有指针统一使用 **34 位十六进制字符串**
- 类型前缀：`01` = 对象，`02` = 集合

### 5.3 响应格式

**对象成员响应：**
```json
{"member": "name", "value": "Alice", "type": "raw"}
```

**完整对象响应：**
```json
{
  "ptr": "01abc123...",
  "members": [
    {"name": "name", "type": "raw", "value": "Alice"},
    {"name": "user", "type": "ref", "value": "01def456"}
  ],
  "count": 2
}
```

**集合响应：**
```json
{
  "ptr": "02def456...",
  "elements": [
    {"type": "raw", "value": "apple"},
    {"type": "ref", "value": "01abc123"}
  ],
  "count": 2
}
```

---

## 6. 错误处理

### 6.1 错误码映射

| LMJCore 错误码 | HTTP 状态码 | 说明 |
|----------------|-------------|------|
| `LMJCORE_SUCCESS` | 200 | 成功 |
| `LMJCORE_ERROR_ENTITY_NOT_FOUND` | 404 | 实体不存在 |
| `LMJCORE_ERROR_INVALID_PARAM` | 400 | 参数无效 |
| `LMJCORE_ERROR_TXN_TIMEOUT` | 408 | 事务超时 |
| `LMJCORE_ERROR_READONLY_TXN` | 405 | 方法不允许 |
| `LMJCORE_ERROR_MEMORY_ALLOCATION` | 500 | 内存分配失败 |

### 6.2 扩展错误码

```c
// 事务相关
#define LMJCORE_ERROR_TXN_TIMEOUT        -32101
#define LMJCORE_ERROR_TXN_BEGIN_FAILED   -32102

// 路径解析相关
#define LMJCORE_ERROR_PATH_PARSE         -32121
#define LMJCORE_ERROR_PATH_TOO_DEEP      -32122

// HTTP 相关
#define LMJCORE_ERROR_HTTP_PARSE         -32161
#define LMJCORE_ERROR_HTTP_BODY_MISSING  -32164
```

---

## 7. 编译与运行

### 7.1 依赖要求

- **CMake**: >= 3.10
- **编译器**: GCC 或 Clang (支持 C11)
- **LMDB**: 需安装开发包
- **LMJCore**: 位于 `thirdparty/LMJCore/core`
- **URLRouter**: 位于 `thirdparty/URLRouer`

### 7.2 编译步骤

```bash
# 1. 创建构建目录
mkdir build && cd build

# 2. 配置 CMake
cmake ..

# 3. 编译
make

# 4. 运行测试
ctest
```

### 7.3 运行服务器

```bash
./build/lmjcore_server
```

**默认配置：**
- 监听地址：`0.0.0.0:8080`
- 数据库路径：`./lmjcore_data`
- 内存映射：10MB
- 最大连接数：128

---

## 8. 配置参数

```c
typedef struct {
    char *host;              // 监听地址
    int port;                // 监听端口
    const char *db_path;     // LMDB 数据库路径
    size_t map_size;         // 内存映射大小
    unsigned int env_flags;  // 环境标志
    int max_connections;     // 最大连接数
    lmjcore_ptr_generator_fn fn; // 指针生成函数
} server_config_t;
```

---

## 9. 设计原则

1. **保持简单**: 只做基础 CRUD，不实现复杂查询
2. **透明存储**: 客户端感知指针和类型
3. **性能优先**: 充分利用 LMDB 特性
4. **明确边界**: 清晰划分网络壳与内核职责
5. **可扩展性**: 预留批量操作、缓存等扩展接口

---

## 10. 关键约束

| 约束项 | 限制值 |
|--------|--------|
| 成员名长度 | ≤ 493 字节 |
| 路径深度 | ≤ 100 层 |
| 事务超时 | 默认 5 秒 |
| 集合特性 | 无序，不保证插入顺序 |
| 写事务 | 串行 (依赖 LMDB 单写者模型) |

---