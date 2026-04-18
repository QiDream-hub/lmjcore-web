# LMJCore-Web

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](https://github.com/your-repo/lmjcore-web)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C Standard](https://img.shields.io/badge/C-11-blue.svg)](https://en.cppreference.com/w/c/11)

**LMJCore-Web** 是一个基于 LMJCore 存储引擎的轻量级 HTTP 服务，提供 RESTful API 访问接口。

---

## 🚀 快速开始

### 安装依赖

```bash
# Ubuntu/Debian
sudo apt-get install liblmdb-dev cmake build-essential

# Arch Linux
sudo pacman -S lmdb cmake base-devel
```

### 编译

```bash
mkdir build && cd build
cmake ..
make
```

### 运行

```bash
# 使用默认配置启动
./lmjcore_server

# 指定端口和数据库路径
./lmjcore_server -p 9000 -d /data/lmjcore

# 使用配置文件
./lmjcore_server -C /etc/lmjcore.conf

# 守护进程模式
./lmjcore_server --daemon

# 查看帮助
./lmjcore_server --help
```

服务器默认启动在 `http://localhost:8080`

---

## 📋 配置方式

LMJCore-Web 支持多种配置方式：

| 方式 | 说明 | 优先级 |
|------|------|--------|
| 命令行参数 | `-p 9000 -d /data/lmjcore` | 最高 |
| 配置文件 | `lmjcore.conf` INI 格式 | 中 |
| 默认值 | 内置默认配置 | 最低 |

**示例配置文件** (`lmjcore.conf`)：

```ini
# 网络配置
host = 0.0.0.0
port = 8080

# 数据库配置
db_path = ./lmjcore_data
map_size = 10M

# 运行模式
daemon = false
log_level = 1
```

详细配置请参考 [设计文档](doc/lmjcore-web/lmjcore_web.md#7-配置管理)

---

## 📖 API 概览

### 对象操作

| 方法 | 端点 | 说明 |
|------|------|------|
| `POST` | `/obj` | 创建空对象 |
| `GET` | `/obj/{ptr}` | 获取完整对象 |
| `GET` | `/obj/{ptr}/{member}` | 获取成员值 |
| `PUT` | `/obj/{ptr}/{member}` | 设置成员值 |
| `DELETE` | `/obj/{ptr}/{member}` | 删除成员 |
| `GET` | `/obj/query?path=...` | 链式查询 |

> **⚠️ 注意**：删除对象的**最后一个成员**会导致对象本身被删除（继承自 LMDB 的空键自动删除行为）。详见 [API 参考文档](doc/lmjcore-web/API_REFERENCE.md#5-删除成员)

### 集合操作

| 方法 | 端点 | 说明 |
|------|------|------|
| `POST` | `/set` | 创建空集合 |
| `GET` | `/set/{ptr}` | 获取完整集合 |
| `POST` | `/set/{ptr}/elements` | 添加元素 |
| `DELETE` | `/set/{ptr}/elements` | 删除元素 |

> **⚠️ 注意**：删除集合的**最后一个元素**会导致集合本身被删除（继承自 LMDB 的空键自动删除行为）。详见 [API 参考文档](doc/lmjcore-web/API_REFERENCE.md#10-删除元素)

### 工具接口

| 方法 | 端点 | 说明 |
|------|------|------|
| `GET` | `/ptr/{ptr}/exist` | 检查指针是否存在 |
| `GET` | `/health` | 健康检查 |

---

## 📋 使用示例

### 1. 创建对象

```bash
curl -X POST http://localhost:8080/obj
# 响应：{"ptr":"01abc123def456..."}
```

### 2. 设置成员值

```bash
curl -X PUT http://localhost:8080/obj/01abc123.../name \
  -H "Content-Type: application/json" \
  -d '{"value":"Alice"}'
```

### 3. 获取成员值

```bash
curl http://localhost:8080/obj/01abc123.../name
# 响应：{"member":"name","value":"Alice","type":"raw"}
```

### 4. 链式查询

```bash
curl "http://localhost:8080/obj/query?path=01abc123...user.profile.name"
# 响应：{"path":"...","value":"Alice","type":"raw"}
```

### 5. 集合操作

```bash
# 创建集合
curl -X POST http://localhost:8080/set
# 响应：{"ptr":"02def456..."}

# 添加元素
curl -X POST http://localhost:8080/set/02def456.../elements \
  -H "Content-Type: application/json" \
  -d '{"value":"apple"}'
```

---

## 🏗️ 核心特性

| 特性 | 说明 |
|------|------|
| **RESTful API** | 标准的 REST 风格接口 |
| **事务管理** | 请求级自动事务，超时控制 |
| **链式查询** | 支持嵌套路径解析 |
| **自动类型识别** | 智能识别指针/原始数据/空值 |
| **高性能** | 基于 LMDB 存储引擎 |

---

## 📁 项目结构

```
lmjcore-web/
├── CMakeLists.txt          # CMake 配置
├── README.md               # 本文件
├── doc/                    # 文档
│   └── lmjcore-web/        # 详细设计文档
├── include/                # 头文件
│   ├── config.h            # 配置管理
│   ├── error_codes.h       # 错误码定义
│   ├── error_response.h    # 错误响应构建
│   ├── handle_utils.h      # 工具函数
│   ├── http_parser.h       # HTTP 解析
│   ├── http_server.h       # HTTP 服务器
│   ├── lmjcore_handle.h    # 处理器声明
│   └── routes.h            # 路由注册
├── src/                    # 源代码
│   ├── config.c            # 配置解析
│   ├── handlers/           # 处理器模块
│   │   ├── obj_handle.c    # 对象处理器
│   │   ├── set_handle.c    # 集合处理器
│   │   ├── utils_handle.c  # 工具处理器
│   │   └── handle_utils.c  # 通用工具
│   ├── http_parser.c
│   ├── http_server.c
│   ├── main.c
│   └── routes.c
├── lmjcore.conf            # 示例配置文件
└── thirdparty/             # 第三方依赖
    ├── LMJCore/
    └── URLRouter/
```

---

## ⚙️ 命令行选项

| 参数 | 简写 | 默认值 | 说明 |
|------|------|--------|------|
| `--host` | `-H` | `0.0.0.0` | 监听地址 |
| `--port` | `-p` | `8080` | 监听端口 |
| `--db-path` | `-d` | `./lmjcore_data` | 数据库路径 |
| `--map-size` | `-m` | `10M` | 内存映射大小 (支持 K/M/G) |
| `--max-connections` | `-c` | `128` | 最大连接数 |
| `--txn-timeout` | `-t` | `5` | 事务超时 (秒) |
| `--config` | `-C` | `lmjcore.conf` | 配置文件路径 |
| `--daemon` | `-D` | `false` | 守护进程模式 |
| `--log-level` | `-l` | `1` | 日志级别 (0-3) |
| `--help` | `-h` | - | 显示帮助 |

---

## 🔧 开发

### 代码模块化

项目采用模块化设计，处理器按功能分离：

- **obj_handle.c** - 对象 CRUD 操作
- **set_handle.c** - 集合 CRUD 操作
- **utils_handle.c** - 工具接口（健康检查、指针验证）
- **handle_utils.c** - 通用工具函数（指针转换、路径解析、值编解码）

### 错误处理

使用统一的错误响应构建函数：

```c
// 返回参数无效
RETURN_ERROR_INVALID_PARAM(response);

// 返回实体不存在
RETURN_ERROR_NOT_FOUND("Object", response);

// 返回内存分配失败
RETURN_ERROR_NO_MEMORY(response);
```

---

## 📚 文档

- [详细设计文档](doc/lmjcore-web/README.md) - 架构设计和技术细节
- [API 参考文档](doc/lmjcore-web/API_REFERENCE.md) - 完整 API 文档

---

## 📄 许可证

MIT License

---

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！
