# LMJCore Web Server

LMJCore 的 HTTP 网络壳，提供 RESTful API 访问 LMJCore 数据库。

## 特性

- ✅ 指针链式查询：`/ptr/{ptr}/path.to.value`
- ✅ 对象和集合的 CRUD 操作
- ✅ 自动指针生成（UUID 或计数器）
- ✅ 审计和修复功能
- ✅ 只读/读写事务支持

## 编译

```bash
mkdir build && cd build
cmake ..
make