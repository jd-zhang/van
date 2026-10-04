# kvserver

基于 [VanDB](..)（C 语言实现的 LSM-Tree KV 存储引擎）的 REST 服务。

引擎本身只提供 C 接口，HTTP / JSON / 配置这些“脏活”不在 C 代码里做：
用 **SWIG** 把一小层 C 封装暴露给 Python，再用 **Flask** 实现 HTTP 层。
引擎的 `table->get/put/del` 是线程安全的，因此 Flask 以 `threaded=True` 运行。

```
    HTTP 客户端
        │  JSON / Base64
        ▼
    Flask (server/app.py)          ← Python：协议、编码、错误码映射
        │
        ▼
    KVStore (server/kv_client.py)  ← Python：异常映射、生命周期
        │
        ▼
    kvstore.py + _kvstore.so       ← SWIG 生成的扩展（二进制安全的 typemap）
        │
        ▼
    engine/kvstore.c               ← C：kv_open / kv_get / kv_put / kv_del
        │
        ▼
    VanDB 引擎 ../libvan.a          ← C：LSM-Tree，dump / merge / WAL
```

---

## 1. 运行环境要求

### 操作系统

| 环境 | 支持情况 |
|---|---|
| Linux | **已验证**。开发与测试均在 Oracle Linux Server 8.10（内核 `5.15.0-206.153.7.1.el8uek.x86_64`） |
| macOS | **未验证** |
| Windows | **未验证** |

引擎依赖 pthread、`syslog.h`、`mqueue.h` 等 Linux/POSIX 接口，其他平台需要先让
VanDB 本身能编译通过。

### 必需组件（下表中的版本为实际验证过的版本）

| 组件 | 版本 | 说明 |
|---|---|---|
| GCC | 8.5.0（Red Hat 8.5.0-21.0.1） | 编译 wrapper 与 `_kvstore.so`，需要 `-fPIC` |
| GNU Make | 4.2.1 | 驱动 `kvserver/Makefile` |
| SWIG | 3.0.12 | 由 `engine/kvstore.i` 生成 `kvstore_wrap.c` |
| Python | **3.11.13** | 必须是 3.11+：`tomllib` 从 3.11 起进入标准库 |

> ⚠️ 本机 `/usr/bin/python3` 是 **3.6.8**，没有 `tomllib`，**不能**用来跑本服务。
> 请始终使用 `python3.11`。Makefile 里的 `PYTHON` 变量也已经写死为 `python3.11`。

### Python 依赖

`requirements.txt` 只有一条直接依赖：

```
Flask>=3.0
```

实际安装的版本：

| 包 | 版本 |
|---|---|
| Flask | 3.1.3 |
| Werkzeug | 3.1.9 |
| Jinja2 | 3.1.6 |
| Click | 8.5.0 |
| itsdangerous | 2.2.0 |
| Blinker | 1.9.0 |
| MarkupSafe | 3.0.4 |

安装：

```bash
python3.11 -m pip install --user -r requirements.txt
```

### VanDB 引擎依赖

| 项 | 值 |
|---|---|
| 静态库 | `../libvan.a`（相对 `kvserver/`，即仓库根目录） |
| 头文件 | `../include`、`../include_auto` |
| 链接 | `-lpthread -lm` |

> **注意**：`libvan.a` 虽然叫 `.a`，实际是用 `-shared` 生成的 **ELF 共享库**。
> 因此 `_kvstore.so` 运行时必须能找到它。Makefile 已经用绝对路径把它记进
> `DT_NEEDED`，并额外加了 `-Wl,-rpath,<仓库根目录>`，所以正常编译后无需设置
> `LD_LIBRARY_PATH`。

`libvan.a` 不存在时，`make` 会自动到上一层目录去构建它。

### 检查环境

```bash
python3.11 --version     # 期望 Python 3.11.x（3.11 以下无 tomllib）
swig -version            # 期望 SWIG Version 3.0.12
gcc --version            # 期望 gcc 8.x
make --version           # 期望 GNU Make 4.x
ls -la ../libvan.a       # 引擎库必须存在
python3.11 -c "import flask, tomllib; print(flask.__version__)"
```

---

## 2. 编译方法

```bash
cd kvserver
make
```

`make` 依次做三件事：

1. `swig -python -o kvstore_wrap.c engine/kvstore.i`
   → 生成 `kvstore_wrap.c`（C 侧胶水）和 `kvstore.py`（Python 侧影子模块）
2. 编译 `kvstore_wrap.o` 与 `engine/kvstore.o`（`-fPIC`，因为要进共享库）
3. 链接成 **`_kvstore.so`**

产物都放在 `kvserver/` 目录下：

| 产物 | 说明 |
|---|---|
| `_kvstore.so` | Python 扩展模块，**名字必须是这个**（`kvstore.py` 会 `import _kvstore`） |
| `kvstore.py` | SWIG 生成的 Python 包装，`server/kv_client.py` 导入它 |
| `kvstore_wrap.c`、`*.o` | 中间产物 |

清理：

```bash
make clean
```

### 常见编译错误

| 现象 | 原因与处理 |
|---|---|
| `fatal error: kvstore.h: No such file or directory`（编译 `kvstore_wrap.c` 时） | 缺 `-Iengine`。Makefile 已带，若手工编译需自己加 |
| `unknown type name 'VAN_DATUM'` | `engine/kvstore.c` 必须 `#include "van_int.h"`（`VAN_DATUM` 的 typedef 在 `van_declare.h`，由 `van_int.h` 引入） |
| `_kvstore.so` 能编译但 `import kvstore` 报 `ModuleNotFoundError: No module named '_kvstore'` | `.so` 没生成成功，或不在 `kvstore.py` 同一目录 |
| 运行时 `libvan.a: cannot open shared object file` | rpath 没生效，设置 `LD_LIBRARY_PATH=<仓库根目录>` 后重试 |
| `make: warning: Clock skew detected` | 文件时间戳来自其他机器（rsync 常见），无害；`make clean && make` 可消除 |

---

## 3. 配置文件说明

默认读取 `./kvserver.toml`（即 `kvserver/` 目录下的那个）。
也可以用 `-c` 指定别的位置：

```bash
./kvserver -c /path/to/config.toml
```

格式（TOML）：

```toml
[kvstore]
host = "0.0.0.0"
port = 8080
data_dir = "./data"
table_name = "kvstore"
cache_size = "1G"
inmem_threshold = "200M"

[http]
read_timeout = 30
write_timeout = 30
```

### 字段说明

| 字段 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `host` | string | `"0.0.0.0"` | 监听地址 |
| `port` | int | `8080` | 监听端口 |
| `data_dir` | string | `"./data"` | 数据目录。**不存在时会被自动创建**（引擎不会自己建目录） |
| `table_name` | string | `"kvstore"` | VanDB 表名 |
| `cache_size` | string | `"1G"` | 引擎块缓存大小，支持 `K`/`M`/`G` 后缀 |
| `inmem_threshold` | string | `"200M"` | 内存表（memtable）阈值，超过即触发 dump；支持 `K`/`M`/`G` 后缀 |
| `read_timeout` | int | `30` | HTTP 读超时（秒）。**当前已被解析但未实际生效**，见 [已知限制](#9-已知限制) |
| `write_timeout` | int | `30` | HTTP 写超时（秒）。同上 |

### `cache_size` / `inmem_threshold` 的写法

- 字符串，后缀 `K` / `M` / `G`，**大小写不敏感**
- `"1G"` = 1073741824 字节，`"200M"` = 209715200 字节
- 纯数字也可以，如 `"1048576"`
- 允许带小数，如 `"1.5G"`
- 写 `0` 或省略表示“不设置，保留引擎默认值”

---

## 4. 如何运行 kvserver

### 启动

```bash
cd kvserver
./kvserver                          # 用默认配置 ./kvserver.toml
./kvserver -c /path/to/config.toml  # 指定配置
```

> `python3` 3.6 跑不起来（缺 `tomllib`），请用 `./kvserver`（shebang 已写
> `#!/usr/bin/env python3.11`）。若直接调用解释器：`python3.11 kvserver -c ...`。

### 正常启动的输出

```
starting kvserver: Config(host='0.0.0.0', port=8080, data_dir='./data', table_name='kvstore', cache_size=1073741824, inmem_threshold=209715200)
 * Serving Flask app 'server.app'
 * Debug mode: off
WARNING: This is a development server. Do not use it in a production deployment. Use a production WSGI server instead.
 * Running on all addresses (0.0.0.0)
 * Running on http://127.0.0.1:8080
 * Running on http://192.168.0.107:8080
Press CTRL+C to quit
```

第一行是自己打印的（含解析后的字节数，可用来确认 `1G` / `200M` 有没有按预期解析），
其余是 Flask 开发服务器的输出。

### 停止

`Ctrl-C`。进程退出时会走 `KVStore.__exit__`，正确关闭 table 与 store；
已写入的数据在下次启动时仍然可见（WAL + on-disk 树）。

---

## 5. REST API 用法

### 通用规则

- `Content-Type: application/json`
- 字符编码：UTF-8
- **value 强制 Base64**（请求体里的 `value` 是 Base64，响应里的 `data` 也是 Base64）
- **key 默认 text**（按 UTF-8 编码），可选 `base64`（用于任意二进制 key）
- 成功：`HTTP 200` + `{"code": 0, "code_name": "VAN_OK", ...}`
- 失败：对应 HTTP 状态码 + `{"code": <负数>, "code_name": "VAN_XXX", "error": "..."}`

### 错误码表

| C 错误码 | JSON `code` | `code_name` | HTTP 状态码 | `error` 消息 |
|---|---|---|---|---|
| `KV_OK` (0) | `0` | `VAN_OK` | 200 | （无） |
| `KV_NOTFOUND` (-30988) | `-1` | `VAN_NOTFOUND` | 404 | `key not found` |
| `KV_INVALID` (-30987) | `-2` | `VAN_INVALID` | 400 | `invalid argument` |
| `KV_BAD` (-30984) | `-3` | `VAN_BAD` | 500 | `internal error` |

> JSON 里用 `-1/-2/-3` 这套小数字，C 层返回的是引擎自己的大负数
> （`-30988` 等），映射在 `server/app.py` 的 `JSON_CODE` 表里完成。

### API 1：PUT

```
POST /api/v1/put
Content-Type: application/json
```

```json
{
  "key": "user:1001",
  "key_encoding": "text",
  "value": "aGVsbG8="
}
```

| 字段 | 必填 | 说明 |
|---|---|---|
| `key` | 是 | 字符串。`key_encoding="text"` 时按 UTF-8 编码；`"base64"` 时按 Base64 解码 |
| `key_encoding` | 否 | `"text"`（默认）或 `"base64"` |
| `value` | 是 | **Base64 字符串**，解码后的字节才真正入库 |

成功（HTTP 200）：

```json
{"code": 0, "code_name": "VAN_OK"}
```

失败（HTTP 400）：

```json
{"code": -2, "code_name": "VAN_INVALID", "error": "value is not valid base64"}
```

其他可能的 400 消息：`body must be a JSON object`、`missing 'key'`、
`'key' must be a string`、`missing 'value'`、`'value' must be a base64 string`、
`key_encoding must be 'text' or 'base64'`、`key is not valid base64`。

### API 2：GET

```
GET /api/v1/get?key=user:1001
GET /api/v1/get?key=AAEC//4=&key_encoding=base64
```

| 参数 | 必填 | 说明 |
|---|---|---|
| `key` | 是 | 同 PUT |
| `key_encoding` | 否 | `"text"`（默认）或 `"base64"` |

成功（HTTP 200）：

```json
{"code": 0, "code_name": "VAN_OK", "data": "aGVsbG8="}
```

失败（HTTP 404）：

```json
{"code": -1, "code_name": "VAN_NOTFOUND", "error": "key not found"}
```

> `?key=` 后面是空字符串时表示 **空 key**，这是合法的（见示例）。
> 真正缺失的是 `key` 这个参数本身，那会返回 400。

### API 3：DELETE

```
DELETE /api/v1/delete?key=user:1001
DELETE /api/v1/delete?key=AAEC//4=&key_encoding=base64
```

参数同 GET。

成功（HTTP 200）：

```json
{"code": 0, "code_name": "VAN_OK"}
```

**删除不存在的 key 也算成功（幂等）。**

失败（HTTP 400）：

```json
{"code": -2, "code_name": "VAN_INVALID", "error": "missing 'key' parameter"}
```

### 其他响应

| 场景 | 状态码 | `error` |
|---|---|---|
| 访问不存在的路径 | 404 | `no such endpoint` |
| 方法用错（如 `PUT /api/v1/get`） | 400 | `method not allowed` |

---

## 6. 完整示例

以下假设服务已在 `localhost:8080` 运行。

### 示例 1：PUT 文本值

```bash
# value = "hello"
# Base64("hello") = "aGVsbG8="
curl -X POST http://localhost:8080/api/v1/put \
  -H "Content-Type: application/json" \
  -d '{"key":"greeting","value":"aGVsbG8="}'
```

```json
{"code": 0, "code_name": "VAN_OK"}
```

### 示例 2：GET 文本值

```bash
curl "http://localhost:8080/api/v1/get?key=greeting"
```

```json
{"code": 0, "code_name": "VAN_OK", "data": "aGVsbG8="}
```

客户端解码：

```python
import base64, json
resp = json.loads(response)
value = base64.b64decode(resp["data"])   # b"hello"
```

### 示例 3：PUT 二进制 key

```bash
# key   = b"\x00\x01\x02\xff\xfe"   Base64 = "AAEC//4="
# value = "world"                    Base64 = "d29ybGQ="
curl -X POST http://localhost:8080/api/v1/put \
  -H "Content-Type: application/json" \
  -d '{"key":"AAEC//4=","key_encoding":"base64","value":"d29ybGQ="}'
```

```json
{"code": 0, "code_name": "VAN_OK"}
```

取回（用同样的 base64 key）：

```bash
curl "http://localhost:8080/api/v1/get?key=AAEC//4=&key_encoding=base64"
```

```json
{"code": 0, "code_name": "VAN_OK", "data": "d29ybGQ="}
```

> key 和 value 都是按**字节**处理的，NUL 字节也能原样往返——
> SWIG 层用的是 `(指针, 长度)`，不走 C 字符串。

### 示例 4：GET 不存在的 key

```bash
curl "http://localhost:8080/api/v1/get?key=nonexistent"
```

```json
{"code": -1, "code_name": "VAN_NOTFOUND", "error": "key not found"}
```

HTTP 状态码为 **404**。

### 示例 5：DELETE

```bash
curl -X DELETE "http://localhost:8080/api/v1/delete?key=greeting"
```

```json
{"code": 0, "code_name": "VAN_OK"}
```

再删一次仍然是 `{"code": 0, "code_name": "VAN_OK"}`（幂等）。

---

## 7. Python 客户端示例

需要 `requests`（`pip install requests`）：

```python
import base64
import requests

BASE = "http://localhost:8080/api/v1"


def put(key, value, key_encoding="text"):
    if isinstance(key, bytes):
        key = base64.b64encode(key).decode("ascii")
        key_encoding = "base64"
    if isinstance(value, str):
        value = value.encode("utf-8")
    encoded_value = base64.b64encode(value).decode("ascii")

    body = {"key": key, "value": encoded_value}
    if key_encoding != "text":
        body["key_encoding"] = key_encoding

    return requests.post(f"{BASE}/put", json=body).json()


def get(key, key_encoding="text"):
    if isinstance(key, bytes):
        key = base64.b64encode(key).decode("ascii")
        key_encoding = "base64"

    params = {"key": key}
    if key_encoding != "text":
        params["key_encoding"] = key_encoding

    result = requests.get(f"{BASE}/get", params=params).json()
    if result["code"] == 0:
        result["data_decoded"] = base64.b64decode(result["data"])
    return result


def delete(key, key_encoding="text"):
    if isinstance(key, bytes):
        key = base64.b64encode(key).decode("ascii")
        key_encoding = "base64"

    params = {"key": key}
    if key_encoding != "text":
        params["key_encoding"] = key_encoding

    return requests.delete(f"{BASE}/delete", params=params).json()


# 文本
put("greeting", "hello")
print(get("greeting"))          # data_decoded = b"hello"

# 二进制 key 与二进制 value
put(b"\x00\x01\x02\xff\xfe", b"\x00\xff")
print(get(b"\x00\x01\x02\xff\xfe"))   # data_decoded = b"\x00\xff"

print(delete("greeting"))
```

也可以直接使用引擎封装（不经 HTTP）：

```python
import sys
sys.path.insert(0, "/path/to/van/kvserver")   # 让 kvstore.py / _kvstore.so 可导入

from server.config import Config
from server.kv_client import KVStore

config = Config("/path/to/van/kvserver/kvserver.toml")
with KVStore(config) as db:
    db.put(b"k", b"v")
    print(db.get(b"k"))     # b"v"
    db.delete(b"k")
```

`KVStore.get()` 对不存在的 key 抛 `KeyError`，参数问题抛 `ValueError`，
引擎内部错误抛 `RuntimeError`。

---

## 8. 已知限制

1. **空值不可区分于缺失。**
   引擎用 `data == NULL` 表示空值，而这正好也是“从没见过这个 key”的返回。
   所以 `kv_put(k, b"")` 能成功，但随后的 `get` 返回 404。HTTP 层对这两种情况
   都回答 404。

2. **`kv_open` 的失败路径不回滚。**
   中途失败时无法销毁已经建了一半的 store / table（引擎没有对应的销毁接口），
   因此打开失败被当作致命错误直接返回。

3. **用的是 Flask 自带开发服务器**（`threaded=True`）。
   不适合生产环境，请换成 gunicorn / uWSGI 之类的 WSGI 服务器。

4. **引擎缺陷：`table->get` 对不存在的 key 返回 0 + 空 datum，而不是 `VAN_NOTFOUND`。**
   根因在 `_van_table_op_get()`：它在搜索 on-disk 树之前执行
   `VAN_MUTEX_LOCK(table, odt_mtx, ret)`，这个宏把 `ret` 覆盖成了 0，
   于是内存树未命中产生的 `VAN_NOTFOUND` 被吞掉。
   SQL 层之所以不受影响，是因为它还额外检查了 `data == NULL`
   （`sql_kv_get()` 给出的 `*datap == NULL` 被 `fetch_row()` 当成“不存在”）。
   **本服务的绕过方式**：`engine/kvstore.c` 里同样把 `v.data == NULL`
   判定为 `KV_NOTFOUND`，因此对外的行为是正确的。

5. **`read_timeout` / `write_timeout` 目前不生效。**
   配置里会解析这两个值，但启动用的是 Flask 开发服务器，没有把它传下去。
   字段保留，等换成生产 WSGI 服务器时再接上。

6. **单库单表。**
   `engine/kvstore.c` 用一个静态全局的 store + table，一个进程只能服务配置里
   的那一张表。

---

## 9. 目录结构

```
kvserver/
  ├── Makefile              # 编译 SWIG 扩展（生成 _kvstore.so）
  ├── kvserver.toml         # 配置文件
  ├── requirements.txt      # Python 依赖（只有 Flask）
  ├── kvserver              # 启动脚本（可执行，shebang 为 python3.11）
  ├── README.md             # 本文档
  ├── engine/
  │   ├── kvstore.i         # SWIG 接口文件（二进制安全的 in/argout typemap）
  │   ├── kvstore.c         # SWIG 封装层（C 代码）
  │   └── kvstore.h         # SWIG 封装层的头文件
  └── server/
      ├── __init__.py
      ├── app.py            # Flask 应用：三个端点 + 错误码映射
      ├── kv_client.py      # 封装 _kvstore.so 的 KVStore 类
      └── config.py         # 读取 TOML 配置（含 "1G"/"200M" 解析）
```

`make` 之后还会多出这些**生成物**（`make clean` 可清除）：

```
  ├── _kvstore.so           # Python 扩展模块
  ├── kvstore.py            # SWIG 生成的 Python 包装
  ├── kvstore_wrap.c        # SWIG 生成的 C 胶水代码
  ├── kvstore_wrap.o
  └── engine/kvstore.o
```

运行后 `data_dir`（默认 `./data`）下会生成引擎的数据与日志文件。
