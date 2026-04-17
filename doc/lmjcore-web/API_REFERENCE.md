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
| 对象操作 | 6 | 对象的 CRUD 和链式查询 |
| 集合操作 | 4 | 集合的 CRUD |
| 工具接口 | 2 | 健康检查和指针验证 |
| **总计** | **12** | |

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

---

### 6. 链式查询

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

### 7. 创建集合

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

---

### 8. 获取完整集合

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
- 集合是无序的，不保证元素插入顺序
- 支持指针引用作为元素

---

### 9. 添加元素

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

### 10. 删除元素

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

---

## 工具接口 API

### 11. 检查指针是否存在

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

---

### 12. 健康检查

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
| 201 | Created（创建成功） |
| 400 | Bad Request（请求参数错误） |
| 404 | Not Found（资源不存在） |
| 405 | Method Not Allowed（方法不允许） |
| 408 | Request Timeout（事务超时） |
| 409 | Conflict（元素已存在） |
| 500 | Internal Server Error（服务器错误） |

### 错误响应格式

```json
{
  "error": "错误描述信息"
}
```

### 常见错误

| 错误信息 | HTTP 状态码 | 说明 |
|----------|-------------|------|
| `Invalid parameters` | 400 | 参数无效 |
| `Missing ptr parameter` | 400 | 缺少 ptr 参数 |
| `Invalid pointer format` | 400 | 指针格式无效 |
| `Object not found` | 404 | 对象不存在 |
| `Set not found` | 404 | 集合不存在 |
| `Member not found` | 404 | 成员不存在 |
| `Failed to allocate memory` | 500 | 内存分配失败 |
| `Failed to begin transaction` | 500 | 事务开启失败 |
| `Failed to commit transaction` | 500 | 事务提交失败 |

---

## 使用示例

### cURL 示例

```bash
# 1. 创建对象
curl -X POST http://localhost:8080/obj

# 2. 设置成员值
curl -X PUT http://localhost:8080/obj/01abc123.../name \
  -H "Content-Type: application/json" \
  -d '{"value":"Alice"}'

# 3. 获取成员值
curl http://localhost:8080/obj/01abc123.../name

# 4. 链式查询
curl "http://localhost:8080/obj/query?path=01abc123...user.profile.name"

# 5. 创建集合
curl -X POST http://localhost:8080/set

# 6. 添加元素到集合
curl -X POST http://localhost:8080/set/02def456.../elements \
  -H "Content-Type: application/json" \
  -d '{"value":"apple"}'

# 7. 获取完整集合
curl http://localhost:8080/set/02def456...

# 8. 检查指针是否存在
curl http://localhost:8080/ptr/01abc123.../exist

# 9. 健康检查
curl http://localhost:8080/health
```

### JavaScript 示例

```javascript
// 创建对象
const createObject = async () => {
  const response = await fetch('http://localhost:8080/obj', {
    method: 'POST'
  });
  const data = await response.json();
  console.log('Created object:', data.ptr);
  return data.ptr;
};

// 设置成员值
const setMember = async (ptr, member, value) => {
  const response = await fetch(`http://localhost:8080/obj/${ptr}/${member}`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ value })
  });
  return await response.json();
};

// 获取成员值
const getMember = async (ptr, member) => {
  const response = await fetch(`http://localhost:8080/obj/${ptr}/${member}`);
  return await response.json();
};

// 链式查询
const queryPath = async (path) => {
  const response = await fetch(`http://localhost:8080/obj/query?path=${encodeURIComponent(path)}`);
  return await response.json();
};

// 使用示例
(async () => {
  const ptr = await createObject();
  await setMember(ptr, 'name', 'Alice');
  const member = await getMember(ptr, 'name');
  console.log('Member value:', member.value);
  
  const result = await queryPath(`${ptr}.name`);
  console.log('Query result:', result);
})();
```

---

## 版本历史

| 版本 | 日期 | 变更 |
|------|------|------|
| 1.0.0 | 2024-01-01 | 初始版本 |
