# LMJCore 网络壳需求总结

### 一、项目定位
基于 LMJCore 存储引擎的 HTTP 网络服务壳，提供 RESTful API 访问接口，支持链式查询、事务管理、自动类型识别等特性。

---

### 二、核心技术决策

#### 2.1 指针存储格式（方案B）
所有值统一添加1字节类型标记：
```c
#define LMJCORE_VALUE_TYPE_RAW   0x00  // 原始数据
#define LMJCORE_VALUE_TYPE_PTR   0x01  // 指针引用
#define LMJCORE_VALUE_TYPE_NULL  0x02  // 空值

// 存储格式
// 原始数据: [0x00][data...]
// 指针引用: [0x01][17B指针]
// 空值:     [0x02]
```

#### 2.2 链式查询语法
```
GET /obj/query?path=[指针].path.to.value

示例：
GET /obj/query?path=01abc123.user.profile.name
GET /obj/query?path=01abc123.user.email%40example.com  (URL编码)
```

#### 2.3 事务模型
- 每个 HTTP 请求自动开启/提交/回滚事务
- 链式查询的多次解析在同一事务内完成
- 写事务也支持读取操作
- 设置事务超时（默认5秒），超时返回408错误

#### 2.4 集合处理
- 集合不参与链式解析，遇到集合返回错误
- 集合元素支持指针引用和原始数据
- 返回完整集合，不预览指针内容

#### 2.5 指针表示
- 所有指针统一使用34位十六进制字符串
- 提供字符串与二进制指针的转换工具

#### 2.6 并发控制
- 采用后写者胜策略，不实现乐观锁
- 依赖 LMDB 单写者模型自动串行化

---

### 三、API 设计

#### 3.1 对象操作
| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/obj` | 创建空对象，返回指针字符串 |
| GET | `/obj/{ptr}` | 获取完整对象（所有成员） |
| GET | `/obj/{ptr}/{member}` | 获取成员值，自动识别类型 |
| PUT | `/obj/{ptr}/{member}` | 设置成员值，自动检测指针/原始数据 |
| DELETE | `/obj/{ptr}/{member}` | 删除成员 |
| GET | `/obj/query?path={path}` | 链式查询，支持嵌套路径 |

#### 3.2 集合操作
| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/set` | 创建空集合，返回指针字符串 |
| GET | `/set/{ptr}` | 获取完整集合（所有元素） |
| POST | `/set/{ptr}/elements` | 添加元素（Body: `{"value": "..."}`） |
| DELETE | `/set/{ptr}/elements` | 删除元素（Body: `{"value": "..."}`） |

#### 3.3 工具接口
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/ptr/{ptr}/exist` | 检查指针是否存在 |
| GET | `/ptr/encode` | 编码查询路径（可选工具） |

---

### 四、响应格式

#### 4.1 对象成员响应
```json
// 原始值
{"member": "name", "value": "Alice", "type": "raw"}

// 指针引用
{"member": "user", "value": "01abc123...", "type": "ref"}

// 空值
{"member": "email", "value": null, "type": "null"}
```

#### 4.2 完整对象响应
```json
{
  "ptr": "01abc123",
  "members": [
    {"name": "name", "type": "raw", "value": "Alice"},
    {"name": "user", "type": "ref", "value": "01def456"},
    {"name": "email", "type": "null", "value": null}
  ],
  "count": 3
}
```

#### 4.3 完整集合响应
```json
{
  "ptr": "02def456",
  "elements": [
    {"type": "raw", "value": "apple"},
    {"type": "raw", "value": "banana"},
    {"type": "ref", "value": "01abc123"}
  ],
  "count": 3
}
```

#### 4.4 链式查询响应
```json
// 返回原始值
{"path": "01abc123.user.name", "value": "Alice", "type": "raw"}

// 返回指针
{"path": "01abc123.user", "value": "01def456", "type": "ref"}
```

---

### 五、错误处理

#### 5.1 错误码映射
| LMJCore 错误码 | HTTP 状态码 | 说明 |
|----------------|-------------|------|
| `LMJCORE_SUCCESS` | 200 | 成功 |
| `LMJCORE_ERROR_ENTITY_NOT_FOUND` | 404 | 实体不存在 |
| `LMJCORE_ERROR_MEMBER_NOT_FOUND` | 404 | 成员不存在 |
| `LMJCORE_ERROR_INVALID_PARAM` | 400 | 参数无效 |
| `LMJCORE_ERROR_PATH_PARSE` | 400 | 路径解析错误 |
| `LMJCORE_ERROR_MEMBER_TOO_LONG` | 400 | 成员名超长 |
| `LMJCORE_ERROR_SET_NOT_SUPPORTED` | 400 | 集合不支持链式解析 |
| `LMJCORE_ERROR_TXN_TIMEOUT` | 408 | 事务超时 |
| `LMJCORE_ERROR_READONLY_TXN` | 405 | 方法不允许 |
| `LMJCORE_ERROR_MEMORY_ALLOCATION` | 500 | 内存分配失败 |

#### 5.2 新增错误码
```c
#define LMJCORE_ERROR_TXN_TIMEOUT        -32101  // 事务超时
#define LMJCORE_ERROR_PATH_PARSE         -32102  // 路径解析错误
#define LMJCORE_ERROR_SET_NOT_SUPPORTED  -32103  // 集合不支持链式解析
#define LMJCORE_ERROR_INVALID_TYPE       -32104  // 无效的值类型标记
```

---

### 六、实现要点

#### 6.1 路径解析
- URL 解码
- 按 `.` 分割路径
- 第一部分作为起始指针（34位十六进制）
- 支持最多100层嵌套

#### 6.2 值编码/解码
- 写入时自动添加类型标记
- 读取时自动识别类型并移除标记
- 指针字符串与二进制互转

#### 6.3 链式查询执行
- 在同一事务内循环解析
- 每次操作前检查超时
- 遇到集合返回错误
- 支持返回指针或原始值

#### 6.4 事务管理
```c
typedef struct {
    lmjcore_txn *txn;
    time_t start_time;
    unsigned int timeout_seconds;  // 默认5秒
} request_ctx;

int begin_request_txn(server_ctx *ctx, request_ctx *req_ctx, bool is_readonly);
int check_txn_timeout(request_ctx *req_ctx);
```

---

### 七、配置参数

```c
typedef struct {
    char *db_path;              // LMDB 数据库路径
    size_t map_size;            // 内存映射大小（默认10MB）
    unsigned int env_flags;     // 环境标志（默认安全模式）
    unsigned int txn_timeout;   // 事务超时秒数（默认5）
    size_t max_path_depth;      // 最大路径深度（默认100）
    bool enable_cache;          // 是否启用缓存（预留）
} server_config;
```

---

### 八、开发优先级

#### 第一阶段（MVP）
- [ ] HTTP 服务器基础框架
- [ ] 对象 CRUD 操作
- [ ] 集合 CRUD 操作
- [ ] 请求级事务管理
- [ ] 基础错误处理
- [ ] 指针工具函数

#### 第二阶段
- [ ] 链式查询实现
- [ ] 路径解析器
- [ ] 值类型自动识别
- [ ] 事务超时控制
- [ ] 集合元素支持指针引用

#### 第三阶段
- [ ] 批量操作支持
- [ ] 性能优化
- [ ] 监控接口
- [ ] 调试工具

---

### 九、关键约束

1. **成员名长度** ≤ 493 字节（受 LMDB Key 限制）
2. **路径深度** ≤ 100 层
3. **事务超时** 默认 5 秒
4. **集合无序**，不保证插入顺序
5. **写事务串行**，依赖 LMDB 单写者模型
6. **指针唯一性** 由上层保证，内核不校验

---

### 十、设计原则

1. **保持简单**：不实现复杂查询，只做基础 CRUD
2. **透明存储**：客户端感知指针和类型，不隐藏底层细节
3. **性能优先**：利用 LMDB 特性，减少不必要开销
4. **明确边界**：清晰定义网络壳与 LMJCore 内核的职责划分
5. **可扩展性**：预留批量操作、缓存等扩展接口
