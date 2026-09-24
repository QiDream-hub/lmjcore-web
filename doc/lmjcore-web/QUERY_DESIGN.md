# 链式查询（深度访问）设计

## 1. 定位与边界

| 接口 | 职责 |
|------|------|
| `GET /obj/query`（`handle_obj_query`） | **深度访问原语**：一条路径 → 一个叶子值。只读、单值、O(深度) |
| `GET/POST /batch`（`handle_batch_get/post`） | **整合层**：一次请求内多条操作，共享同一只读事务快照 |

因此本接口**不做**：模式匹配 / 通配符、多值结果集、集合 fan-out、子树投影。
好处是单条查询的代价与对象大小无关（全程零成员枚举），也不需要引入 Stride 或
自建模式编译器；"多路径 + 一致性读"由 batch 承担。

## 2. 路径语法

```
<34 位十六进制指针>.<成员名>[.<成员名>...]
```

- **先按 `.` 切分，再逐段 `url_decode`**：成员名中的字面 `.` 写作 `%2E`，
  因此 `file.txt`、`user.name` 这类成员名可以正常寻址。
- 根段必须是**对象**指针（`01` 前缀）；集合指针（`02`）返回 `SET_NOT_SUPPORTED`。
- 空段（`01ab..a`、`01ab.a.`）与缺少成员段（仅 `01ab`）都是语法错误——不再静默丢弃空段。
- 段数上限由 `query_max_depth` 控制（默认 64），超出返回 `PATH_TOO_DEEP`。

> 分隔符为什么是 `.` 而不是 `/`：`router_create('/')` 会按 `/` 切分 URL，
> 而路由模式 `/obj/query?path=…` 形状固定，路径里出现 `/` 会产生额外段并导致路由失败；
> 且查询深度可变，无法用固定形状的路由树表达。`.` 不在路由分隔符中，天然安全。
> `%2E` 转义在任何内联分隔符方案下都无法避免（成员名是任意字节），因此不是额外代价。

## 3. 执行流程

```c
parse(path)                      // 切分 + 逐段 URL 解码 + 深度检查
ptr = segments[0]                // 必须 01 开头
for i in 1..last:
    if i < last:                 // 中间段
        member_get(ptr, seg[i], probe[18])
        BUFFER_TOO_SMALL  -> TYPE_MISMATCH      // 值比指针编码长，必不是引用
        probe[0] != PTR   -> TYPE_MISMATCH      // 类型标记优先
        ptr = probe[1..17]
        ptr[0] != OBJ     -> SET_NOT_SUPPORTED / TYPE_MISMATCH
    else:                        // 叶子
        member_get_capped(ptr, seg[last], max_value_bytes)
        decode_value(...)        -> {value, type}
```

### 18 字节探测的不变量

`lmjcore_obj_member_get` 是"要么全拷贝、要么不拷贝"：缓冲区不足时返回
`LMJCORE_ERROR_BUFFER_TOO_SMALL`，**既不拷贝也不回传所需长度**（无法只读 1 字节类型标记）。
而指针引用编码恰为 `1 + 17 = 18` 字节，所以用 18 字节探测：

- 读成功 → 以 `value[0]` 类型标记为准（项目约定）；
- 返回缓冲区不足 → 值必然比指针编码长 → 不是引用，**无需把大值读进来**。

> 若将来新增更长的引用编码，此不变量必须同步调整（`query_path.c` 有注释标记）。

## 4. 存储类型标记的用法

项目约定 `value[0]` 标识存储类型：`0x00` RAW / `0x01` PTR / `0x02` NULL。
链式查询在两处使用它：

1. **中间段判定**（类型标记优先，尺寸推断兜底）：`PTR` 才可能继续下钻；
   RAW/NULL 立即判为"不是引用"，并把观测到的类型回填到错误的 `found` 字段。
2. **引用载荷首字节 = 实体类型**：`OBJ` 继续，`SET` 报"集合不支持成员访问"。
3. 叶子段仍走 `lmjcore_decode_value`（同一约定）。

## 5. 错误模型

| 场景 | 错误码 | HTTP | error |
|------|--------|------|-------|
| 路径语法错误（空段 / 缺成员段） | `PATH_PARSE` | 400 | `Invalid query path` |
| 根段不是合法指针 | `PATH_INVALID_PTR` | 400 | `Invalid pointer format` |
| 根段/中间目标是集合 | `SET_NOT_SUPPORTED` | 400 | `Set does not support member access` |
| 中间值不是引用 | `TYPE_MISMATCH` | 400 | `Intermediate value is not a reference` |
| 超过深度上限 | `PATH_TOO_DEEP` | 400 | `Query path too deep` |
| 段 URL 解码失败 | `PATH_URL_DECODE` | 400 | `Failed to decode query path segment` |
| 中间对象为空实体 | `ENTITY_NOT_FOUND` | 404 | `Object not found` |
| 成员不存在 / 已注册但无值 | `MEMBER_NOT_FOUND` | 404 | `Member not found` |
| 叶子值超过上限 | `VALUE_TOO_LARGE` | 413 | `Value too large` |

定位字段（可选出现）：

- `at`：失败位置 `<指针>/<段>/<段>`；
- `segment`：失败段索引（`0` = 根指针段，`1` = 第一个成员段）；
- `found`：中间值不是引用时实际观测到的存储类型（`raw` / `null` / `set`）；
- `limit`：413 时生效的上限。

## 6. 事务与批量契约

- 链式查询始终**只读**，通过 `handle_txn_begin(hp, &txn, LMJCORE_TXN_READONLY)` /
  `handle_txn_end(hp, txn, false)` 管理事务。
- 在 `GET /batch` 中 `hp->txn` 已存在，因此**复用共享事务、不自行提交或回滚**，
  多条深度读共享同一 MVCC 快照。
- `GET /batch` 与 `POST /batch` 都是**全有或全无**：任一操作返回 ≥400 即回滚整批，
  响应 `{"success":false,"failed_at":N,"details":{…}}`。需要容错时请拆成多次 batch 调用。

## 7. 上限与配置

| 配置项（`lmjcore.conf`） | 默认值 | 说明 |
|---|---|---|
| `query_max_depth` | 64 | 成员段数上限（不含根指针段），与写入侧 `LMJCORE_NEST_DEPTH_MAX` 对称 |
| `max_value_bytes` | 8192 | 单个成员值 / 查询叶子值上限，`/obj/{ptr}/{member}` 共用 |

默认值单一来源：`include/handle_utils.h` 的 `HANDLE_DEFAULT_*`，`config.h` 与
`http_server_init` 均引用它。

传输层守卫：响应缓冲固定 `32768` 字节；一旦 `http_build_response` 放不下，
服务器返回 **JSON 413**（而不是无 body 的 500）。成员值上限与该守卫共同保证
"值过大"始终能得到可读的诊断。

## 8. 已知限制

- 请求体/请求行总长受 `read_http_request` 单次 `recv` 与 `request_buffer[16384]` 限制；
- 中间段只能穿越对象，不能穿越集合（集合元素是"值"而非"成员名"）；
- 叶子是引用时**不自动解引用**，直接返回指针与 `object`/`set` 类型；
- "已注册但无值"的成员统一报 `Member not found`；若需与之区分，可用
  `lmjcore_obj_member_value_exist` 再判一次；
- `GET /obj/{ptr}` 等列表接口的响应仍可能超过传输缓冲，此时由全局守卫降级为 413。

## 9. 模块划分

```
include/query_path.h        路径解析 + 深度访问（纯存储层，不涉及 HTTP）
src/handlers/query_path.c
src/handlers/query_handle.c HTTP 处理器：事务、错误映射、JSON 渲染
include/handle_utils.h      lmjcore_obj_member_get_capped()（受限读取，与成员接口共用）
```

## 10. 测试要点

- 多层下钻、含 `.` 的成员名（`%2E`）、叶子为 raw/object/set/null；
- 中间值为 raw/null/set 的三种失败，验证 `found` 与 `at`/`segment`；
- 空对象、成员不存在、集合根、非十六进制指针、空段、缺成员段、深度超限；
- 大值：8000B 正常返回（增长重试）、9000B 触发 413 且带 `limit`；
- 大响应（>32KB 的 `GET /obj/{ptr}`）降级为 JSON 413；
- batch：多条深度读共享事务；一条失败则整批 400 且 `failed_at` 正确；
- 共享事务在链式查询失败后仍可继续使用（未被 abort）。
