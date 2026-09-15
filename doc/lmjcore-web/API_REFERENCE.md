# LMJCore-Web API 参考文档

## 快速开始

### 启动服务器

```bash
# 使用默认配置启动
./lmjcore_server

# 指定端口
./lmjcore_server -p 9000

# 使用配置文件
./lmjcore_server -C /etc/lmjcore.conf

# 查看帮助
./lmjcore_server --help
```

### 配置说明

| 配置方式 | 说明 |
|----------|------|
| 命令行参数 | 最高优先级，支持 `-p`, `-d`, `-C` 等 |
| 配置文件 | `lmjcore.conf` INI 格式 |
| 默认值 | 内置默认配置 |

详细配置请参考 [设计文档](./lmjcore_web.md#7-配置管理)

---

## API 概览

| 类别 | 端点数量 | 说明 |
|------|----------|------|
| 对象操作 | 8 | 对象的 CRUD、原子创建填充和链式查询 |
| 集合操作 | 6 | 集合的 CRUD 和原子创建填充 |
| 工具接口 | 2 | 健康检查和指针验证 |
| 批量操作 | 2 | GET /batch（只读）、POST /batch（写事务） |
| **总计** | **18** | |

---

## 核心概念

### 指针（Pointer）

LMJCore 使用 **17 字节全局唯一指针** 标识所有实体：

- **格式**: 34 位十六进制字符串
- **类型前缀**:
  - `01` = 对象 (LMJCORE_OBJ)
  - `02` = 集合 (LMJCORE_SET)
- **示例**: `01abc123def456789012345678901234`

### 对象 vs 集合

| 维度 | 对象 | 集合 |
|------|------|------|
| **用途** | 键值对容器 | 无序元素容器 |
| **指针前缀** | `01` | `02` |
| **存储** | 成员名在 `set` 库，值在 `main` 库 | 元素在 `set` 库 |
| **访问** | 通过成员名点查 | 遍历或判断存在性 |
| **典型场景** | 配置、用户信息、文档 | 标签、权限组、唯一值列表 |

### 存储模型

LMJCore 使用两个 LMDB 数据库协同工作：

| 空间 | 名称 | 用途 | 关键特性 |
|------|------|------|----------|
| **集合区** | `set` | 存储实体的关联项集合（成员名或元素） | 启用 `MDB_DUPSORT`，按字典序自动排序，**不保留插入顺序** |
| **主存储区** | `main` | 存储具体值 | Key = `[17B 实体指针][成员名]`，点查 O(1) |

### 实体存在性规则

**核心原则**: **实体的存在性由 `set` 库定义**。

| 状态 | 判定条件 |
|------|----------|
| **有效实体** | `set` 中存在以其指针为 Key、且至少有一个 value 的条目 |
| **空实体（不存在）** | `set` 中无对应 Key：刚创建尚未写入成员/元素，或成员/元素已被全部删除 |

> ⚠️ **重要**: 由于 `set` 基于 LMDB 的重复键（`MDB_DUPSORT`）实现，**不允许空键**。当删除实体的最后一个成员/元素时，该键会被 LMDB 自动删除，实体随之消失。

### 集合的无序性

LMJCore 中的集合是**无序集合**，具有以下特性：

| 特性 | 说明 |
|------|------|
| **无序性** | 不保留插入顺序，按字典序自动排序 |
| **唯一性** | 同一元素在集合中只能出现一次 |
| **高效查找** | 可快速判断元素是否存在 |
| **集合运算** | 天然支持并集、交集等操作（基于排序） |

> 💡 **如果需要有序列表**: 在元素值中编码顺序信息（如前 4 字节为小端序下标）。

---

## 批量操作 API

批量操作 API 通过 HTTP 方法区分事务类型：

| 方法 | 端点 | 事务类型 | 允许的操作 |
|------|------|----------|------------|
| `GET` | `/batch` | 只读事务 | 仅 `GET` |
| `POST` | `/batch` | 写事务 | `GET`/`PUT`/`POST`/`DELETE` |

### 15. GET /batch - 只读批量操作

**请求**
```http
GET /batch
Content-Type: application/json

{
  "operations": [
    {
      "method": "GET",
      "path": "/obj/01abc123..."
    },
    {
      "method": "GET",
      "path": "/obj/01abc123.../name"
    },
    {
      "method": "GET",
      "path": "/set/02def456..."
    }
  ]
}
```

**请求体字段**
| 字段 | 类型 | 说明 |
|------|------|------|
| `operations` | array | 操作列表，最大 1000 个操作，仅允许 `GET` 操作 |

**说明**
- 使用只读事务（`LMJCORE_TXN_READONLY`），保证读取一致性
- 如操作中包含 `PUT`/`POST`/`DELETE`，返回 400 错误

---

### 16. POST /batch - 写操作批量操作

**请求**
```http
POST /batch
Content-Type: application/json

{
  "operations": [
    {
      "method": "PUT",
      "path": "/obj/01abc123.../name",
      "body": {"value": "Alice"}
    },
    {
      "method": "GET",
      "path": "/obj/01abc123..."
    },
    {
      "method": "DELETE",
      "path": "/obj/01abc123.../temp"
    }
  ]
}
```

**请求体字段**
| 字段 | 类型 | 说明 |
|------|------|------|
| `operations` | array | 操作列表，最大 1000 个操作 |

**操作结构**
| 字段 | 类型 | 说明 |
|------|------|------|
| `method` | string | HTTP 方法：`GET`、`PUT`、`POST`、`DELETE` |
| `path` | string | 完整路径，如 `/obj/{ptr}`、`/obj/{ptr}/{member}`、`/set/{ptr}` |
| `body` | object | 可选，仅 `PUT`/`POST` 需要，格式 `{"value": "..."}` |

**成功响应（GET /batch 和 POST /batch 相同）**
```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "success": true,
  "results": [
    {
      "status": 200,
      "body": {"success": true}
    },
    {
      "status": 200,
      "body": {
        "ptr": "01abc123def456789012345678901234",
        "type": "object"
      }
    },
    {
      "status": 200,
      "body": {"success": true}
    }
  ]
}
```

**错误响应**
```http
HTTP/1.1 400 Bad Request
Content-Type: application/json

{
  "success": false,
  "failed_at": 1,
  "details": {
    "error": "Object not found"
  }
}
```

**错误响应字段**
| 字段 | 类型 | 说明 |
|------|------|------|
| `success` | boolean | 始终为 `false` |
| `failed_at` | number | 失败操作的索引（从 0 开始） |
| `details` | object/string | 错误详情（JSON 对象或字符串） |

**说明**
- **原子性**: 所有操作在同一事务内执行，任一操作失败则全部回滚
- **GET /batch**: 使用只读事务（`LMJCORE_TXN_READONLY`），仅允许 `GET` 操作
- **POST /batch**: 使用写事务，支持所有操作类型，限制总时长（默认 5 秒超时）
- **操作限制**: 最多支持 1000 个并发操作
- **错误处理**: 失败时返回 `failed_at` 字段指示失败位置，便于调试

**响应格式说明**
- `results` 数组中的每个元素对应一个操作
- `status` 字段为该操作的 HTTP 状态码
- `body` 字段为该操作的响应体（自动解析为 JSON 对象或保留为字符串）

**使用示例**

示例 1：批量设置对象属性
```json
{
  "operations": [
    {
      "method": "PUT",
      "path": "/obj/01abc123def456789012345678901234/name",
      "body": {"value": "Alice"}
    },
    {
      "method": "PUT",
      "path": "/obj/01abc123def456789012345678901234/age",
      "body": {"value": "30"}
    },
    {
      "method": "GET",
      "path": "/obj/01abc123def456789012345678901234"
    }
  ]
}
```

示例 2：创建对象后批量设置（需要客户端先获取指针）
```bash
# 步骤 1：创建对象，获取指针
curl -X POST http://localhost:8080/obj
# 返回：{"ptr": "01abc123def456789012345678901234"}

# 步骤 2：使用指针批量设置属性
curl -X POST http://localhost:8080/batch \
  -H "Content-Type: application/json" \
  -d '{
    "operations": [
      {"method": "PUT", "path": "/obj/01abc123.../name", "body": {"value": "Alice"}},
      {"method": "PUT", "path": "/obj/01abc123.../age", "body": {"value": "30"}}
    ]
  }'
```

示例 3：只读事务批量查询（使用 GET /batch）
```json
{
  "operations": [
    {"method": "GET", "path": "/obj/01abc123..."},
    {"method": "GET", "path": "/obj/01abc123.../name"},
    {"method": "GET", "path": "/set/02def456..."}
  ]
}
```

```bash
curl -X GET http://localhost:8080/batch \
  -H "Content-Type: application/json" \
  -d '{
    "operations": [
      {"method": "GET", "path": "/obj/01abc123..."},
      {"method": "GET", "path": "/obj/01abc123.../name"},
      {"method": "GET", "path": "/set/02def456..."}
    ]
  }'
```

**注意事项**
- 批量操作**不支持别名引用**：每个操作的 `path` 必须使用完整的指针字符串
- 如需在批量操作中引用新创建的对象指针，需分两步：先创建获取指针，再批量操作
- 失败时事务自动回滚，已执行的操作不会生效
- `GET /batch` 仅允许 `GET` 操作，`POST /batch` 允许所有操作类型

---

## 对象操作 API

### 1. 创建对象

**请求**
```http
POST /obj
```

**响应**
```http
HTTP/1.1 201 Created
Content-Type: application/json

{
  "ptr": "01abc123def456789012345678901234"
}
```

**说明**
- 创建一个空对象
- 返回对象的指针字符串（34 位十六进制）
- 指针前缀 `01` 表示对象类型
- **空对象不存在**: 刚创建的对象在 `set` 库中没有条目，`entity_exist` 返回 0

---

### 2. 获取完整对象

**请求**
```http
GET /obj/{ptr}
```

**路径参数**
| 参数 | 类型 | 说明 |
|------|------|------|
| `ptr` | string | 对象指针（34 位十六进制） |

**成功响应**
```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "ptr": "01abc123def456789012345678901234",
  "members": [
    {
      "name": "username",
      "value": "alice",
      "type": "raw"
    },
    {
      "name": "profile",
      "value": "01def456789012345678901234567890",
      "type": "ref"
    },
    {
      "name": "email",
      "value": null,
      "type": "null"
    }
  ],
  "count": 3
}
```

**错误响应**
```http
HTTP/1.1 404 Not Found
Content-Type: application/json

{
  "error": "Object not found"
}
```

---

### 3. 获取成员值

**请求**
```http
GET /obj/{ptr}/{member}
```

**路径参数**
| 参数 | 类型 | 说明 |
|------|------|------|
| `ptr` | string | 对象指针 |
| `member` | string | 成员名 |

**成功响应**
```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "member": "username",
  "value": "alice",
  "type": "raw"
}
```

**值类型说明**
| type | 说明 | value 格式 |
|------|------|-----------|
| `raw` | 原始数据 | 字符串 |
| `ref` | 指针引用 | 34 位十六进制字符串 |
| `null` | 空值 | null |

**错误响应**
```http
HTTP/1.1 404 Not Found
Content-Type: application/json

{
  "error": "Member not found"
}
```

---

### 4. 设置成员值

**请求**
```http
PUT /obj/{ptr}/{member}
Content-Type: application/json

{
  "value": "string value or 01abc123... pointer"
}
```

**路径参数**
| 参数 | 类型 | 说明 |
|------|------|------|
| `ptr` | string | 对象指针 |
| `member` | string | 成员名 |

**请求体**
| 字段 | 类型 | 说明 |
|------|------|------|
| `value` | string | 要设置的值（原始数据或指针字符串） |

**成功响应**
```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "success": true
}
```

**说明**
- 自动识别值类型：
  - 34 位十六进制且以 `01` 或 `02` 开头 → 指针引用
  - `null` → 空值
  - 其他 → 原始数据

---

### 5. 删除成员

**请求**
```http
DELETE /obj/{ptr}/{member}
```

**路径参数**
| 参数 | 类型 | 说明 |
|------|------|------|
| `ptr` | string | 对象指针 |
| `member` | string | 成员名 |

**成功响应**
```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "success": true
}
```

**⚠️ 重要说明：删除最后一个成员会导致对象被删除**

由于 LMJCore 基于 LMDB 存储引擎，对象的成员列表存储在 LMDB 的 `set` 空间中。LMDB 有一个关键特性：

> **当删除一个键的所有值时，该键会被 LMDB 自动删除（不允许空键）。**

这意味着：
- 删除对象的**最后一个成员**后，该对象指针在 `set` 中将不存在
- 对象的存在性由 `set` 定义，因此**对象本身被视为已删除**
- 后续对该对象的访问将返回 404 错误

**示例**：
```js
// 初始状态：对象有一个成员
{ "name": "Alice" }

// 删除最后一个成员
DELETE /obj/01abc123.../name

// 结果：对象不再存在
GET /obj/01abc123...  // → 404 Not Found
```

**建议**：
- 避免删除对象的最后一个成员
- 如需清空对象，可保留一个占位成员（如 `_empty: null`）
- 在应用层追踪对象的状态，避免意外删除

---

### 6. 创建对象并填充成员（原子操作，支持嵌套）

在一个事务内完成「创建对象 + 填充全部成员值」，避免两步操作（先 `POST /obj` 再逐个 `PUT`）之间留下半成品对象。

**请求**
```http
POST /obj/init
Content-Type: application/json

{
  "username": "alice",
  "age": 30,
  "friend": "01abc123def456789012345678901234",
  "email": null,
  "note": "",
  "profile": {"city": "北京", "tags": ["a", "b"]},
  "ip": ["192.168.1.1", "192.168.1.2"]
}
```

**请求体**
- 请求体直接是**一个 JSON 对象**（成员映射，键即成员名），不再需要 `members` 包装字段
- 空对象 `{}` 允许（等价于 `POST /obj`，返回空对象指针）

**成员值规则**（由统一的嵌套工具处理）：
| JSON 值 | 存储类型 | 说明 |
|---------|----------|------|
| JSON object | `ref` → 嵌套 obj | 同一事务内递归创建嵌套对象并填充，成员值指向其指针 |
| JSON array | `ref` → 嵌套 set | 同一事务内递归创建嵌套集合并添加元素，成员值指向其指针 |
| 34 位十六进制且以 `01`/`02` 开头的字符串 | `ref` | 引用已存在的对象/集合 |
| 空字符串 `""`、字面量 `"null"`、JSON `null` | `null` | 空值 |
| 其他字符串 | `raw` | 原始数据 |
| JSON number | `raw` | 按其 JSON 数字文本存储（如 `21` → 存 `"21"`） |
| JSON boolean | `raw` | `true` → `"true"`，`false` → `"false"` |

- 嵌套深度上限 64，超出返回 400
- 嵌套创建的子实体失败时与整体一起回滚，不留任何半成品

**成功响应**
```http
HTTP/1.1 201 Created
Content-Type: application/json

{
  "ptr": "01abc123def456789012345678901234",
  "member_count": 8
}
```

**错误响应（自动回滚）**
```http
HTTP/1.1 400 Bad Request
Content-Type: application/json

{
  "error": "member name must not be empty (in member '') (in element [0]) (in member 'bad')"
}
```

**错误响应字段**
| 字段 | 类型 | 说明 |
|------|------|------|
| `error` | string | 失败原因（在前）与逐层失败位置（在后，如 `(in member 'x') (in element [0])`） |

**说明**
- **原子性**: 创建对象、全部成员填充与所有嵌套子实体创建在**同一个事务**内完成
  - 任一环节失败（成员名超长、嵌套过深、事务超时等）→ 事务整体**回滚**，不留下任何对象/集合
  - 成功后指针才对外可见，可直接用于 `GET /obj/{ptr}`；嵌套对象/集合可通过其 `ref` 值继续 `GET`
- **返回指针**: 成功响应中的 `ptr` 即新对象指针（前缀 `01`），`member_count` 为根对象实际写入的成员数量
- **事务复用**: 当通过 `POST /batch` 的共享事务调用时，本接口不自行提交/回滚，交由批量事务统一处理
- **版本变更（1.3.0）**: 请求体由 `{"members":{...}}` 改为直接的对象映射；成员值类型由"仅字符串/null"扩展为任意 JSON（对象/数组自动嵌套创建）

---

### 7. 删除完整对象

**请求**
```http
DELETE /obj/{ptr}
```

**路径参数**
| 参数 | 类型 | 说明 |
|------|------|------|
| `ptr` | string | 对象指针（34 位十六进制） |

**成功响应**
```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "success": true
}
```

**说明**
- 完全删除对象及其所有成员
- 同时删除 `set` 库中的成员列表和 `main` 库中的所有成员值
- 删除后该对象指针变为无效

**错误响应**
```http
HTTP/1.1 404 Not Found
Content-Type: application/json

{
  "error": "Object not found"
}
```

---

### 8. 链式查询

**请求**
```http
GET /obj/query?path={path}
```

**查询参数**
| 参数 | 类型 | 说明 |
|------|------|------|
| `path` | string | 查询路径（格式：`<指针>.<member1>.<member2>...`） |

**示例**
```http
GET /obj/query?path=01abc123.user.profile.name
```

**成功响应**
```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "path": "01abc123.user.profile.name",
  "value": "alice",
  "type": "raw"
}
```

**错误响应**
```http
HTTP/1.1 404 Not Found
Content-Type: application/json

{
  "error": "Member not found"
}
```

```http
HTTP/1.1 400 Bad Request
Content-Type: application/json

{
  "error": "Intermediate value is not a reference"
}
```

**说明**
- 支持多层嵌套路径
- 中间值必须是指针引用（`ref` 类型）才能继续解析
- 遇到集合类型返回错误

---

## 集合操作 API

### 9. 创建集合

**请求**
```http
POST /set
```

**响应**
```http
HTTP/1.1 201 Created
Content-Type: application/json

{
  "ptr": "02def456789012345678901234567890"
}
```

**说明**
- 创建一个空集合
- 指针前缀 `02` 表示集合类型
- **空集合不存在**: 刚创建的集合在 `set` 库中没有条目，`entity_exist` 返回 0

---

### 10. 获取完整集合

**请求**
```http
GET /set/{ptr}
```

**路径参数**
| 参数 | 类型 | 说明 |
|------|------|------|
| `ptr` | string | 集合指针 |

**成功响应**
```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "ptr": "02def456789012345678901234567890",
  "elements": [
    {
      "value": "apple",
      "type": "raw"
    },
    {
      "value": "01abc123def456789012345678901234",
      "type": "ref"
    },
    {
      "value": null,
      "type": "null"
    }
  ],
  "count": 3
}
```

**说明**
- 集合是无序的，不保证插入顺序
- 支持指针引用作为元素

---

### 11. 添加元素

**请求**
```http
POST /set/{ptr}/elements
Content-Type: application/json

{
  "value": "element value"
}
```

**路径参数**
| 参数 | 类型 | 说明 |
|------|------|------|
| `ptr` | string | 集合指针 |

**请求体**
| 字段 | 类型 | 说明 |
|------|------|------|
| `value` | string | 要添加的元素值 |

**成功响应**
```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "success": true
}
```

**错误响应**
```http
HTTP/1.1 409 Conflict
Content-Type: application/json

{
  "error": "Element already exists"
}
```

---

### 12. 删除元素

**请求**
```http
DELETE /set/{ptr}/elements
Content-Type: application/json

{
  "value": "element value"
}
```

**路径参数**
| 参数 | 类型 | 说明 |
|------|------|------|
| `ptr` | string | 集合指针 |

**请求体**
| 字段 | 类型 | 说明 |
|------|------|------|
| `value` | string | 要删除的元素值 |

**成功响应**
```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "success": true
}
```

**⚠️ 重要说明：删除最后一个元素会导致集合被删除**

由于 LMJCore 基于 LMDB 存储引擎，集合的元素存储在 LMDB 的 `set` 空间中。LMDB 有一个关键特性：

> **当删除一个键的所有值时，该键会被 LMDB 自动删除（不允许空键）。**

这意味着：
- 删除集合的**最后一个元素**后，该集合指针在 `set` 中将不存在
- 集合的存在性由 `set` 定义，因此**集合本身被视为已删除**
- 后续对该集合的访问将返回 404 错误

**示例**：
```js
// 初始状态：集合有一个元素
["apple"]

// 删除最后一个元素
DELETE /set/02def456.../elements
{ "value": "apple" }

// 结果：集合不再存在
GET /set/02def456...  // → 404 Not Found
```

**建议**：
- 避免删除集合的最后一个元素
- 如需清空集合，可保留一个占位元素
- 在应用层追踪集合的状态，避免意外删除

---

### 13. 删除完整集合

**请求**
```http
DELETE /set/{ptr}
```

**路径参数**
| 参数 | 类型 | 说明 |
|------|------|------|
| `ptr` | string | 集合指针（34 位十六进制） |

**成功响应**
```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "success": true
}
```

**说明**
- 完全删除集合及其所有元素
- 删除 `set` 库中该集合指针对应的所有元素
- 删除后该集合指针变为无效

**错误响应**
```http
HTTP/1.1 404 Not Found
Content-Type: application/json

{
  "error": "Set not found"
}
```

---

### 14. 创建集合并填充元素（原子操作，支持嵌套）

在一个事务内完成「创建集合 + 添加全部元素」，避免两步操作（先 `POST /set` 再逐个 `POST /set/{ptr}/elements`）之间留下半成品集合。

**请求**
```http
POST /set/init
Content-Type: application/json

["apple", 21, "192.168.1.1", {"name": "小李"}, ["x", "y"], null, ""]
```

**请求体**
- 请求体直接是**一个 JSON 数组**（元素列表），不再需要 `elements` 包装字段
- 空数组 `[]` 允许（等价于 `POST /set`，返回空集合指针）

**元素值规则**（由统一的嵌套工具处理）：
| JSON 值 | 存储类型 | 说明 |
|---------|----------|------|
| JSON object | `ref` → 嵌套 obj | 同一事务内递归创建嵌套对象并填充，元素指向其指针 |
| JSON array | `ref` → 嵌套 set | 同一事务内递归创建嵌套集合并添加元素，元素指向其指针 |
| 34 位十六进制且以 `01`/`02` 开头的字符串 | `ref` | 引用已存在的对象/集合 |
| 空字符串 `""`、字面量 `"null"`、JSON `null` | `null` | 空值 |
| 其他字符串 | `raw` | 原始数据 |
| JSON number | `raw` | 按其 JSON 数字文本存储（如 `21` → 存 `"21"`） |
| JSON boolean | `raw` | `true` → `"true"`，`false` → `"false"` |

- 嵌套深度上限 64，超出返回 400
- 嵌套创建的子实体失败时与整体一起回滚，不留任何半成品
- **去重语义**：集合自动去重，请求中重复的元素（包括重复的空值/`null`）不报错

**成功响应**
```http
HTTP/1.1 201 Created
Content-Type: application/json

{
  "ptr": "02def456789012345678901234567890",
  "element_count": 6
}
```

**错误响应（自动回滚）**
```http
HTTP/1.1 400 Bad Request
Content-Type: application/json

{
  "error": "nesting depth exceeds max 64 (in element [0]) (in element [0])"
}
```

**错误响应字段**
| 字段 | 类型 | 说明 |
|------|------|------|
| `error` | string | 失败原因（在前）与逐层失败位置（在后） |

**说明**
- **原子性**: 创建集合、全部元素添加与所有嵌套子实体创建在**同一个事务**内完成
  - 任一环节失败（元素超长、嵌套过深、事务超时等）→ 事务整体**回滚**，不留下任何集合/对象
  - 成功后指针才对外可见，可直接用于 `GET /set/{ptr}`；嵌套对象/集合可通过其 `ref` 值继续 `GET`
- **返回指针**: 成功响应中的 `ptr` 即新集合指针（前缀 `02`），`element_count` 为该集合实际包含的去重后元素数量
- **事务复用**: 当通过 `POST /batch` 的共享事务调用时，本接口不自行提交/回滚，交由批量事务统一处理
- **版本变更（1.3.0）**: 请求体由 `{"elements":[...]}` 改为直接的 JSON 数组；元素值类型由"仅字符串/null"扩展为任意 JSON（对象/数组自动嵌套创建）

---

## 工具接口 API

### 15. 检查指针是否存在

**请求**
```http
GET /ptr/{ptr}/exist
```

**路径参数**
| 参数 | 类型 | 说明 |
|------|------|------|
| `ptr` | string | 要检查的指针 |

**存在响应**
```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "exist": true,
  "type": "object"
}
```

**不存在响应**
```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "exist": false
}
```

**类型说明**
| type | 说明 |
|------|------|
| `object` | 对象类型（指针前缀 `01`） |
| `set` | 集合类型（指针前缀 `02`） |

**说明**
- 存在性由 `set` 库定义：只有当 `set` 中存在以其指针为 Key 且至少有一个 value 的条目时，实体才存在
- 刚创建的空对象/空集合返回 `exist: false`

---

### 16. 健康检查

**请求**
```http
GET /health
```

**响应**
```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "status": "ok",
  "uptime": 12345
}
```

**字段说明**
| 字段 | 说明 |
|------|------|
| `status` | 服务状态（`ok` 表示正常） |
| `uptime` | 运行时间（秒） |

---

## 错误码参考

### HTTP 状态码

| 状态码 | 说明 |
|--------|------|
| 200 | 成功 |
| 201 | 创建成功 |
| 400 | 参数错误 |
| 404 | 实体不存在 |
| 405 | 方法不允许 |
| 408 | 事务超时 |
| 409 | 元素已存在 |
| 500 | 服务器错误 |

### LMJCore 错误码

| 错误码 | 值 | HTTP 映射 | 说明 |
|--------|-----|----------|------|
| `LMJCORE_SUCCESS` | 0 | 200 | 成功 |
| `LMJCORE_ERROR_ENTITY_NOT_FOUND` | -32001 | 404 | 实体不存在 |
| `LMJCORE_ERROR_MEMBER_NOT_FOUND` | -32002 | 404 | 成员不存在 |
| `LMJCORE_ERROR_INVALID_PARAM` | -32003 | 400 | 参数无效 |
| `LMJCORE_ERROR_PATH_PARSE` | -32121 | 400 | 路径解析错误 |
| `LMJCORE_ERROR_SET_NOT_SUPPORTED` | -32141 | 400 | 集合不支持链式解析 |
| `LMJCORE_ERROR_TXN_TIMEOUT` | -32101 | 408 | 事务超时 |
| `LMJCORE_ERROR_READONLY_TXN` | -32020 | 405 | 方法不允许 |
| `LMJCORE_ERROR_MEMORY_ALLOCATION` | -32030 | 500 | 内存分配失败 |

---

## 附录：存储模型详解

### set 库（集合区）

| 属性 | 说明 |
|------|------|
| **用途** | 存储实体的关联项集合（对象的成员名列表、集合的元素列表） |
| **Key** | 实体指针（17 字节） |
| **Value** | 成员名（对象）或元素值（集合） |
| **特性** | 启用 `MDB_DUPSORT`，允许重复 Key，按 Value 字典序自动排序 |
| **空键行为** | 不允许空键：删除最后一个 value 时，Key 被 LMDB 自动删除 |

### main 库（主存储区）

| 属性 | 说明 |
|------|------|
| **用途** | 存储对象成员的具体值 |
| **Key** | `[17B 实体指针][成员名]` |
| **Value** | 二进制原始数据或指针引用 |
| **Key 限制** | ≤ 511 字节 → 成员名最大 493 字节 |

### 值存储格式

所有值统一添加 1 字节类型标记：

```
┌──────────────────────────────────────────┐
│ 原始数据：  [0x00][data...]              │
│ 指针引用：  [0x01][17B 指针]             │
│ 空值：      [0x02]                       │
└──────────────────────────────────────────┘
```

---

## 参考文档

- [LMJCore 概念指南](../../thirdparty/LMJCore/doc/core/LMJCore 概念指南.md)
- [LMJCore 核心存储模型](../../thirdparty/LMJCore/doc/core/LMJCore 核心存储模型.md)
- [LMJCore 核心设计定义](../../thirdparty/LMJCore/doc/core/LMJCore 核心设计定义.md)
- [LMJCore 事务模型](../../thirdparty/LMJCore/doc/core/LMJCore 事务模型.md)
