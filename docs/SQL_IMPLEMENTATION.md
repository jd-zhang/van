# VanDB SQL 层实现文档

> 适用版本：SQL schema version `vansql1`
> 代码位置：`src/sql/*.c`、`include/van_sql.h`、`include/sql_internal.h`、`include_auto/sql_ext.h`、`cli/vansql.c`

**这是主文档（完整版）。** 另有三份按主题拆分的专题文档，内容均摘自本文，便于单独阅读：

| 文档 | 内容 | 对应本文 |
|---|---|---|
| [SQL_ARCHITECTURE.md](SQL_ARCHITECTURE.md) | 架构图 + 数据流（分层图、调用关系、数据结构、CREATE/SELECT 数据流） | 第一部分、第二部分、§3.1、§3.3、§5.2 |
| [SQL_DESIGN_DECISIONS.md](SQL_DESIGN_DECISIONS.md) | 设计决策记录（"为什么这么设计"） | 第四部分 |
| [SQL_ROADMAP.md](SQL_ROADMAP.md) | 已知限制与未来扩展计划 | 第六部分 |

> 专题文档中的节号已重新编号（其 §1.x 对应本文 §4.x / §6.x），每份开头都标注了对应关系。

---

## 目录

- [第零部分：代码扫描结果（文件职责 / 关键函数 / 调用关系）](#第零部分代码扫描结果文件职责--关键函数--调用关系)
- [第一部分：总体架构](#第一部分总体架构)
- [第二部分：核心数据结构](#第二部分核心数据结构)
- [第三部分：关键流程](#第三部分关键流程)
- [第四部分：关键设计决策](#第四部分关键设计决策)
- [第五部分：KV 层交互](#第五部分kv-层交互)
- [第六部分：已知限制和未来扩展](#第六部分已知限制和未来扩展)
- [第七部分：实现 SQL 层对原有代码的改动](#第七部分实现-sql-层对原有代码的改动)

---

## 第零部分：代码扫描结果（文件职责 / 关键函数 / 调用关系）

### 0.1 文件清单与职责

| 文件 | 主要职责 |
|---|---|
| `include/van_sql.h` | **公开 API**。错误码、类型枚举 `sql_type_t`、`SQL_VALUE`/`SQL_COLUMN`/`SQL_SCHEMA`/`SQL_TABLE`/`SQL_RESULTSET`/`SQL_SESSION` 结构体、会话与值操作函数声明。只前向声明 `struct _van_store`/`struct _van_table`，**不依赖引擎内部头文件**，可被外部消费者直接包含。 |
| `include/sql_internal.h` | **内部共享声明**。包含 `van_int.h` + `van_sql.h`；定义词法记号 `SQL_TOKEN`/`SQL_TOKENSTREAM`、关键字枚举 `sql_kw`、AST 节点 `SQL_EXPR`/`SQL_STMT`、`SQL_ROW`、以及目录常量（`SQL_CATALOG_TABLE`、`SQL_SCHEMA_VERSION`、`SQL_KVSIZE`）。**不对外安装**。 |
| `include_auto/sql_ext.h` | **跨模块内部函数声明表**。把每个 `.c` 的内部接口集中导出，便于模块间调用与 future 重构。 |
| `src/sql/sql_ast.c` | 内存分配器（`sql_calloc`/`sql_free`/`sql_strdup`/`sql_strlower`）与 **AST 节点构造函数**（`sql_expr_*`、`sql_stmt_alloc`）。 |
| `src/sql/sql_lex.c` | **词法分析**。把 SQL 文本切成 `SQL_TOKEN` 流；处理标识符、关键字、整数/浮点、字符串（含转义与 `''` 双写）、`X'..'`/`B'..'` 位串、标点与注释。 |
| `src/sql/sql_parse.c` | **语法分析**。递归下降，产出 `SQL_STMT`。负责 CREATE/DROP/INSERT/SELECT/UPDATE/DELETE 及表达式优先级。 |
| `src/sql/sql_type.c` | **类型系统**。类型关键字 → `sql_type_t`（`sql_type_parse`）、值转换（`sql_value_cast` 及 `cast_to_*`）、日期/时间/时间戳的解析与格式化、取当前时间。 |
| `src/sql/sql_value.c` | **值的生命周期与语义**。`sql_value_init/clear/copy/set_*`、`sql_value_cmp`、`sql_value_tostring`、目录用的十六进制编解码（`sql_value_to_hex`/`from_hex`）、结果集容器、`sql_errstr`、`sql_type_name`。 |
| `src/sql/sql_row.c` | **行编解码**。行 Key 的 8 字节大端编码、记录（行）的二进制编解码。 |
| `src/sql/sql_catalog.c` | **元数据目录 + KV 适配层**。表句柄生命周期、schema 的序列化/反序列化、目录表的读写、**SQL 层与引擎之间唯一的 KV 出入口**（`sql_kv_put/get/del`）。 |
| `src/sql/sql_exec.c` | **执行器**。表达式求值（三值逻辑）、`sql_stmt_exec` 分发、CREATE/DROP/INSERT/SELECT/UPDATE/DELETE 六个执行函数。 |
| `src/sql/sql_session.c` | **会话管理**。`sql_session_open/close`、语句循环 `session_run`（lex 一次，循环 parse→exec）。 |
| `cli/vansql.c` | **命令行工具**。参数解析、`-c`/`-f`/交互模式、`.tables`/`.schema` 元命令、结果输出格式化。 |

### 0.2 各文件关键函数

#### `sql_lex.c`

| 函数 | 作用 |
|---|---|
| `sql_lex(sql, len, &ts)` | 入口，把文本切成记号流。`len==0` 表示按 NUL 结尾整串处理。 |
| `sql_kw_lookup(text, len)` / `sql_kw_lookup_exact` | 关键字查表，命中则置 `tk->kw`。 |
| `sql_lex_quoted(...)` | 扫描单引号字符串。**两趟游标**：`i` 先做定长/计数扫描，`j` 再做实际拷贝（见 §4.6）。 |
| `sql_lex_bitstring(...)` | `X'AABB'` / `B'0101'` 十六进制与二进制字面量。 |
| `sql_ts_peek(ts, n)` / `sql_ts_consume(ts)` | 语法分析器的前看与消费原语。 |
| `sql_tokenstream_free` | 释放记号流（含 `sval` 与输入副本）。 |

#### `sql_parse.c`

| 函数 | 作用 |
|---|---|
| `sql_parse(ts, &stmt)` | 入口，按首记号关键字分派到 6 个语句解析函数。 |
| `parse_create_table` / `parse_drop_table` / `parse_insert` / `parse_select` / `parse_update` / `parse_delete` | 各语句的递归下降解析。 |
| `parse_column_def` | 列定义（类型 + `NOT NULL` + `DEFAULT` + `PRIMARY KEY`）。 |
| `parse_type` | 调用 `sql_type_parse`，处理 `VARCHAR(32)` 这类带长度参数的类型。 |
| `parse_primary` / `parse_unary` / `parse_mul` / `parse_add` / `parse_predicate` / `parse_not` / `parse_and` / `parse_expr` | **表达式优先级链**（见 §1.4）。 |
| `parse_exprlist` | `(a, b, c)` 表达式列表，供 `IN (...)` 与 `VALUES (...)` 使用。 |
| `accept_kw` / `expect_kw` / `accept_punct` / `expect_punct` | 记号匹配与报错。 |
| `token_value` | 把 `TK_INTEGER/TK_FLOAT/TK_STRING/TK_BLOB` 记号转成 `SQL_VALUE` 字面量。 |

#### `sql_ast.c`

| 函数 | 作用 |
|---|---|
| `sql_calloc` / `sql_free` / `sql_strdup` / `sql_strndup` / `sql_strlower` | 统一内存与字符串工具（`sql_strlower` 用于大小写不敏感的名字查找）。 |
| `sql_expr_alloc` / `sql_expr_free` | AST 节点分配与递归释放。 |
| `sql_expr_literal` / `sql_expr_column` / `sql_expr_binary` / `sql_expr_unary` / `sql_expr_not` / `sql_expr_isnull` / `sql_expr_in` / `sql_expr_between` / `sql_expr_like` / `sql_expr_func` | 各类表达式节点构造。 |
| `sql_exprlist_alloc` / `sql_exprlist_add` / `sql_exprlist_free` | 表达式列表容器。 |
| `sql_stmt_alloc` / `sql_stmt_free` | 语句节点分配与释放。 |

#### `sql_type.c`

| 函数 | 作用 |
|---|---|
| `sql_type_parse(kw, has_arg, arg, &type, &length)` | 类型关键字 → `sql_type_t` + 声明长度。 |
| `sql_value_cast(src, type, length, dst)` | 总入口，按目标类型分派；**允许 `dst` 与 `src` 别名**（内部先拷贝到 `tmp`）。 |
| `cast_to_int` / `cast_to_float` / `cast_to_string` / `cast_to_binary` / `cast_to_date` / `cast_to_time` / `cast_to_timestamp` | 各类具体转换规则。 |
| `sql_value_numeric(val, &d)` | 取数值（整数走 `ival`，浮点走 `dval`）。 |
| `sql_string_to_date/time/timestamp`、`sql_date/time/timestamp_to_string` | 时间类型与文本互转。 |
| `sql_now_date/time/timestamp` | `CURRENT_DATE/TIME/TIMESTAMP` 求值。 |

#### `sql_value.c`

| 函数 | 作用 |
|---|---|
| `sql_value_init` / `sql_value_clear` / `sql_value_copy` | 生命周期三件套；`clear` 释放 `sval` 并复位。 |
| `sql_value_set_null` / `set_int` / `set_double` / `set_string` / `from_text` | 构造器。 |
| `sql_value_cmp(a, b)` | **全序比较**，跨类型按"类别"定序（见 §4.4）。 |
| `sql_value_tostring` | 转文本；字符串原样、二进制转大写十六进制、时间转 ISO 文本。 |
| `sql_value_to_hex` / `sql_value_from_hex` | 目录里存 `DEFAULT` 值用的十六进制编解码。 |
| `sql_resultset_alloc/free/setcolnames/addrow/print` | 结果集容器与表格化打印。 |
| `sql_errstr` / `sql_type_name` | 诊断。 |

#### `sql_row.c`

| 函数 | 作用 |
|---|---|
| `sql_rowkey_encode` / `sql_rowkey_decode` | rowid ↔ 8 字节大端 Key。 |
| `sql_row_encode(schema, vals, &buf, &len)` | 按 schema 把一行值序列化成记录。 |
| `sql_row_decode(schema, data, len, vals)` | 反序列化；解码前先 `sql_value_clear` 每个槽位。 |
| `sql_row_values_clear` | 批量释放。 |
| `fixed_size(type)`（static） | 定长类型的载荷字节数。 |

#### `sql_catalog.c`

| 函数 | 作用 |
|---|---|
| `sql_catalog_open` / `close` / `load` | 目录表生命周期与启动时加载全部 schema。 |
| `sql_schema_serialize` / `sql_schema_deserialize` | schema ↔ 文本。 |
| `sql_catalog_save_schema` / `sql_catalog_drop` | 写入/删除一个表的 schema，并重写表清单。 |
| `sql_table_alloc` / `free` / `open` / `close` / `ensure_open` | 表句柄（含底层 `VAN_TABLE` 的开关）。 |
| `sql_session_find` / `sql_session_addtable` / `sql_session_deltable` | 会话内表注册表。 |
| `sql_kv_put` / `sql_kv_get` / `sql_kv_del` / `sql_kv_free` | **rowid 寻址的 KV 出入口**。 |
| `cat_put` / `cat_get` / `cat_del`（static） | **按名字寻址的目录 KV 出入口**。 |

#### `sql_exec.c`

| 函数 | 作用 |
|---|---|
| `sql_stmt_exec(sess, stmt, out, &nrows)` | 执行入口；只读检查 + 按类型分发。 |
| `exec_create_table` / `exec_drop_table` / `exec_insert` / `exec_select` / `exec_update` / `exec_delete` | 六个执行函数。 |
| `build_schema`（static） | `SQL_STMT` 的列定义 → `SQL_SCHEMA`，同时定位主键。 |
| `insert_one_row`（static） | 单行插入的全部逻辑（列映射、默认值、rowid 选取、编码、写 KV）。 |
| `sql_expr_eval(sess, schema, row, expr, res)` | 表达式求值，产出 `SQL_VALUE`。 |
| `sql_expr_test(..., &tv)` | 表达式作为谓词求值，产出三值 `TRI_*`。 |
| `eval_compare` / `eval_arith` / `eval_like` / `like_match` / `tri_from_value` / `result_bool`（static） | 求值内部件。 |
| `fetch_row` / `rowid_key_free` / `row_values_alloc`（static） | 扫描与取行工具。 |
| `selrow_cmp`（static） | `qsort` 比较器，配合文件级静态变量 `sort_order`/`sort_norder`。 |
| `sql_schema_findcol` | **列名解析**（小写名线性查找）。 |

#### `sql_session.c`

| 函数 | 作用 |
|---|---|
| `sql_session_open(dir, flags, &sess)` | 建目录、开 store、开目录并加载 schema。 |
| `sql_session_close` | 关所有表、关目录、关 store。 |
| `session_run`（static） | **语句循环**：lex 一次，然后 parse→exec→free 循环。`strict_one` 决定是否拒绝尾部多余记号。 |
| `sql_session_exec` / `sql_session_query` / `sql_session_executescript` | 三个公开入口，差异仅在 `resultp` 与 `strict_one`。 |

### 0.3 调用关系

**整体主链路：**

```
cli/vansql.c  main
   └─ sql_session_open ──── van_store_create / store->open
   │                    └─ sql_catalog_open ── sql_table_open ── van_table_create/open/start_threads
   │                                       └─ sql_catalog_load ── cat_get ── sql_schema_deserialize
   └─ run_sql ── sql_session_executescript ─┐
                 sql_session_exec          ├─ session_run
                 sql_session_query ────────┘
                                    ├─ sql_lex ─────► SQL_TOKENSTREAM
                                    ├─ sql_parse ──► SQL_STMT
                                    │     ├─ parse_create_table / parse_insert / parse_select / ...
                                    │     └─ parse_expr ─► parse_and ─► parse_not ─► parse_predicate
                                    │                          └─► parse_add ─► parse_mul ─► parse_unary ─► parse_primary
                                    │                                └─ sql_expr_binary / sql_expr_column / ... (sql_ast.c)
                                    └─ sql_stmt_exec
                                          ├─ exec_create_table ── build_schema ── sql_table_alloc/open
                                          │                    └─ sql_catalog_save_schema ── sql_schema_serialize ── cat_put
                                          ├─ exec_insert ── insert_one_row ── sql_expr_eval
                                          │                              ├─ sql_value_cast (sql_type.c)
                                          │                              ├─ sql_row_encode (sql_row.c)
                                          │                              └─ sql_kv_put ── table->put
                                          ├─ exec_select ── fetch_row ── sql_kv_get ── table->get
                                          │                          └─ sql_row_decode
                                          │              ├─ sql_expr_test ── sql_expr_eval
                                          │              ├─ qsort ── selrow_cmp ── sql_value_cmp
                                          │              └─ sql_resultset_addrow / _print
                                          ├─ exec_update ── fetch_row ── sql_expr_test
                                          │              ├─ sql_value_cast
                                          │              ├─ sql_row_encode ── sql_kv_put
                                          │              └─ sql_kv_del
                                          └─ exec_delete ── fetch_row ── sql_expr_test ── sql_kv_del
```

**依赖方向（无环）：**

```
sql_ast   ◄──────────── 所有模块（内存 + AST 构造）
sql_lex   ──► sql_ast
sql_parse ──► sql_ast, sql_lex, sql_type, sql_value
sql_type  ──► sql_ast, sql_value
sql_value ──► sql_ast, sql_type
sql_row   ──► sql_ast, sql_value
sql_catalog ─► sql_ast, sql_row, sql_value, sql_type
sql_exec  ──► sql_ast, sql_value, sql_type, sql_row, sql_catalog
sql_session ► sql_lex, sql_parse, sql_exec, sql_catalog
cli/vansql ► van_sql.h（仅公开 API）
```

关键约束：**`sql_catalog.c` 是唯一直接触碰引擎 `VAN_TABLE` 的模块**。其他模块想要读写数据，必须经过 `sql_kv_put/get/del`。这条边界让"引擎换实现"或"加缓存/索引"时只需改一个文件。

---

## 第一部分：总体架构

### 1.1 分层

```
┌──────────────────────────────────────────────────────────┐
│  CLI 层          cli/vansql.c                             │
│  参数解析 / 交互 REPL / 结果打印 / .tables .schema        │
└────────────────────────┬─────────────────────────────────┘
                         │ sql_session_open / exec / query
┌────────────────────────▼─────────────────────────────────┐
│  会话层          sql_session.c                            │
│  一次 lex，循环 parse → exec → free                       │
│  只读检查、结果集归属（打印 vs 交给调用方）                │
└──────┬────────────────────────────────┬──────────────────┘
       │                                │
┌──────▼──────────┐            ┌────────▼──────────────────┐
│  前端            │            │  执行层  sql_exec.c        │
│  sql_lex.c       │            │  表达式求值（三值逻辑）    │
│  sql_parse.c     │            │  扫描 / 过滤 / 投影 / 排序 │
│  sql_ast.c       │            │  CREATE/DROP/INSERT/...    │
└──────┬──────────┘            └────────┬──────────────────┘
       │  SQL_STMT                      │
       └───────────────┬────────────────┘
                       │
       ┌───────────────▼────────────────────────────┐
       │  支撑层                                     │
       │  sql_type.c   类型转换                      │
       │  sql_value.c  值语义 / 比较 / 结果集         │
       │  sql_row.c    行编解码                      │
       └───────────────┬────────────────────────────┘
                       │
       ┌───────────────▼────────────────────────────┐
       │  目录 + KV 适配层   sql_catalog.c           │
       │  sql_kv_put/get/del   cat_put/get/del       │
       │  schema 序列化 / 表句柄                     │
       └───────────────┬────────────────────────────┘
                       │
       ┌───────────────▼────────────────────────────┐
       │  KV 引擎                                    │
       │  VAN_STORE（目录/恢复日志）                  │
       │  VAN_TABLE → inmemtree + ondisktree + txn   │
       └────────────────────────────────────────────┘
```

### 1.2 每层职责与边界

| 层 | 职责 | 明确的**不做** |
|---|---|---|
| **Lexer** `sql_lex.c` | 字符 → 记号。识别关键字、标识符、字面量、标点。**一次性把整条脚本切完**。 | 不做语法判断；不关心记号顺序；不解析语义。 |
| **Parser** `sql_parse.c` | 记号 → `SQL_STMT`（AST）。保证语法与优先级正确。 | **不做名字绑定**（不查列名是否存在）、不做类型检查、不做常量折叠。`EX_COLUMN` 节点此时只有一个字符串名字。 |
| **Analyzer** | ⚠️ **本实现没有独立的 Analyzer/ binder 阶段**。名字解析（`sql_schema_findcol`）在**求值期、逐行**发生。 | 见 §4.7 的取舍说明。 |
| **Executor** `sql_exec.c` | 解释执行 AST：扫描、过滤、投影、排序、分页，并把变更写回 KV。同时承担类型强制转换（`sql_value_cast`）与约束检查（NOT NULL / 主键唯一）。 | 不做查询优化/计划选择；没有基于代价的决策。 |
| **支撑层** `sql_type.c` `sql_value.c` `sql_row.c` | 纯函数式的类型与编码服务，无状态（除全局时间源）。 | 不持有会话或表状态。 |
| **目录 + KV 适配** `sql_catalog.c` | 元数据持久化、表句柄管理、**封装引擎 KV 接口**。 | 不解释 SQL，不理解表达式。 |

### 1.3 层与层之间的接口

| 边界 | 输入 | 输出 | 契约 |
|---|---|---|---|
| CLI → 会话 | `const char *sql`、`FILE *out` | 返回码；`SQL_RESULTSET **`（`sql_session_query`） | 调用方拥有 `*rsp`，需 `sql_resultset_free`。 |
| 会话 → Lexer | SQL 文本（`NUL` 结尾） | `SQL_TOKENSTREAM *` | 会话拥有并在结束时 `sql_tokenstream_free`。 |
| 会话 → Parser | `SQL_TOKENSTREAM *`（游标 `pos` 由 parser 推进） | `SQL_STMT *` | 会话拥有并在每条语句后 `sql_stmt_free`。parser **只向前推进游标**，一条脚本里可连续解析多条。 |
| 会话 → Executor | `SQL_SESSION *`、`SQL_STMT *`、`FILE *out` | 返回码 + `*nrowsp` | 只读标志在此检查；`sess->resultp` 非空时结果集所有权移交调用方。 |
| Executor → 表达式求值 | `(schema, row, expr)` | `SQL_VALUE`（`sql_expr_eval`）或三值 `int`（`sql_expr_test`） | `row` 可为 `NULL`（用于 `LIMIT 3` 这类无行上下文的求值）。 |
| Executor → 类型转换 | `SQL_VALUE src` + 目标 `sql_type_t` + 声明长度 | `SQL_VALUE dst` | 允许 `dst == src`（内部拷贝到临时值）。 |
| Executor → 目录/KV | `SQL_TABLE *` + `rowid` | `SQL_OK` / 错误码 | `sql_kv_get` 返回的内存由调用方 `sql_kv_free`。**找不到行不是错误**：`*datap == NULL` 且返回 `SQL_OK`。 |

### 1.4 表达式优先级链（递归下降）

`sql_parse.c` 用一层函数对应一个优先级，从低到高：

```
parse_expr      → OR            (最低)
  parse_and     → AND
    parse_not   → NOT
      parse_predicate → 比较 = != <> < <= > >=
                      → IS [NOT] NULL
                      → [NOT] BETWEEN .. AND ..
                      → [NOT] IN ( .. )
                      → [NOT] LIKE ..
        parse_add     → + -      （字符串 + 兼作拼接）
          parse_mul   → * / %
            parse_unary → 一元 + -
              parse_primary → 字面量 / 列名 / ( expr ) / CURRENT_DATE|TIME|TIMESTAMP / 函数
```

**为什么把 `NOT` 放在 `AND` 之下、`predicate` 之上？** 因为 SQL 里 `NOT a = b` 必须解释为 `NOT (a = b)`，而 `NOT` 又要能修饰 `IN`/`BETWEEN`/`LIKE`。放在这一层，`parse_predicate` 自己消费掉前缀 `NOT`（用于 `NOT IN`/`NOT LIKE`/`NOT BETWEEN`），剩下的 `NOT` 交给 `parse_not` 包成 `EX_NOT` 节点 —— 两种形态各得其所，且 `NOT NOT x` 自然递归成立。

---

## 第二部分：核心数据结构

### 2.1 类型系统：`sql_type_t`

```c
/* include/van_sql.h */
typedef enum {
	SQL_T_NULL = 0,		/* Untyped NULL literal		*/
	SQL_T_SMALLINT = 1,	/* 16 bit signed integer	*/
	SQL_T_INT = 2,		/* 32 bit signed integer	*/
	SQL_T_BIGINT = 3,	/* 64 bit signed integer	*/
	SQL_T_FLOAT = 4,	/* 32 bit IEEE float		*/
	SQL_T_DOUBLE = 5,	/* 64 bit IEEE double		*/
	SQL_T_VARCHAR = 6,	/* Variable length string	*/
	SQL_T_CHAR = 7,		/* Blank padded string		*/
	SQL_T_TEXT = 8,		/* Unlimited string		*/
	SQL_T_BINARY = 9,	/* Zero padded byte string	*/
	SQL_T_VARBINARY = 10,	/* Variable length byte string	*/
	SQL_T_BLOB = 11,	/* Unlimited byte string	*/
	SQL_T_DATE = 12,	/* Days since 1970-01-01	*/
	SQL_T_TIME = 13,	/* Microseconds since midnight	*/
	SQL_T_TIMESTAMP = 14	/* Milliseconds since epoch	*/
} sql_type_t;
```

注释里写明：**枚举的数值是持久化格式的一部分，不得重排或重新编号**。这是因为 schema 序列化时直接写 `col->type` 的数字。

配套的分类宏（`SQL_TYPE_IS_INT/FLOAT/NUMERIC/STRING/BINARY/TEMPORAL/HAS_LENGTH`）让"类别判断"集中在一处，避免在 `sql_value_cmp`、`cast_to_*`、`sql_row_encode` 里重复写 `switch`。

内部表示统一为整数/浮点/字节串三选一：

- 整数族与时间族 → `SQL_VALUE.ival`（`int64`）
- 浮点族 → `SQL_VALUE.dval`（`double`），同时冗余填 `ival`
- 字符族与二进制族 → `SQL_VALUE.sval` + `slen`

### 2.2 值的表示：`SQL_VALUE`

```c
/* include/van_sql.h */
typedef struct _sql_value {
	sql_type_t	type;		/* Declared/converted type	*/
	int		isnull;		/* SQL NULL			*/
	int64		ival;		/* Integers & temporal values	*/
	double		dval;		/* Approximate numbers		*/
	unsigned char	*sval;		/* Strings & binary data	*/
	size_t		slen;		/* Byte count of sval		*/
} SQL_VALUE;
```

设计要点：

1. **NULL 是标志位，不是类型**。`type` 仍然记录"这一列/这个表达式原本是什么类型"，所以 `sql_value_set_null(&v, SQL_T_DATE)` 能得到一个"DATE 类型的 NULL"。这让 `ORDER BY`、比较、编码都能知道该按什么类别处理，而不必特殊分支。
2. **`sval` 永远是自有拷贝**。`sql_value_copy` 深拷贝字节串；`sql_value_clear` 释放并复位（`memset` + `type = SQL_T_NULL` + `isnull = 1`）。统一的所有权规则避免了"这个字符串该不该 free"的二义。
3. **`ival` 与 `dval` 冗余填充**（`set_int` 同时填 `dval`，`set_double` 同时填 `ival`）。这让 `sql_value_numeric()` 只需要看 `SQL_TYPE_IS_FLOAT(type)` 就能选出正确字段，算术与比较路径因此非常短。
4. **固定大小、无指针数组**，可以按值放进数组（`SQL_VALUE *vals`）并按索引访问，这正是行投影需要的。

生命周期约定：

```c
SQL_VALUE v;
sql_value_init(&v);        /* type = SQL_T_NULL, isnull = 1 */
...                        /* set_* 内部会先 clear，可重复赋值 */
sql_value_clear(&v);       /* 释放 sval 并复位 */
```

### 2.3 元数据：`SQL_COLUMN` / `SQL_SCHEMA` / `SQL_TABLE`

```c
typedef struct _sql_column {
	char		*name;		/* Original spelling		*/
	char		*lname;		/* Lower case name for lookup	*/
	sql_type_t	type;
	size_t		length;		/* Declared length, 0 if none	*/
	int		not_null;	/* NOT NULL constraint		*/
	int		primary_key;	/* Column level PRIMARY KEY	*/
	int		has_default;	/* DEFAULT was given		*/
	SQL_VALUE	defval;		/* The default value		*/
} SQL_COLUMN;

typedef struct _sql_schema {
	char		*name;
	char		*lname;
	size_t		ncols;
	size_t		capacity;
	SQL_COLUMN	*cols;
	int		pk_indx;	/* Rowid alias column, -1 none	*/
	uint64		next_rowid;	/* Next synthetic row id	*/
} SQL_SCHEMA;

typedef struct _sql_table {
	char		*name;		/* SQL name			*/
	char		*lname;		/* Lower case SQL name		*/
	char		*kvname;	/* VanDB table name		*/
	SQL_SCHEMA	*schema;
	struct _van_table *table;	/* NULL until opened		*/
	int		opened;
} SQL_TABLE;
```

要点：

- **`name` + `lname` 双份**：SQL 标识符大小写不敏感但显示要保留原样。所有查找走 `lname`（`strcmp` 精确匹配），输出走 `name`。
- **`SQL_SCHEMA` 是"可序列化单元"**，`SQL_TABLE` 是"运行期句柄"。两者分离，是为了让序列化函数只关心 schema，不必处理引擎句柄。
- **`next_rowid` 存在 schema 里**：它是表的持久状态，每次分配后若增长就要回写目录（见 §3.2 第 7 步）。
- **`pk_indx` 是 rowid 别名列的索引**，`-1` 表示没有。语义见 §4.5。
- **`kvname` 目前恒等于 `name`**，保留这个字段是为了将来"SQL 表名 → 物理表名"的映射（比如加索引后需要独立的物理表）。

### 2.4 AST 节点

语句类型与表达式类型：

```c
/* include/sql_internal.h */
typedef enum {
	STMT_CREATE_TABLE = 1, STMT_DROP_TABLE, STMT_INSERT,
	STMT_SELECT, STMT_UPDATE, STMT_DELETE
} sql_stmt_type;

typedef enum {
	OP_PLUS = 1, OP_MINUS, OP_MUL, OP_DIV, OP_MOD,
	OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE, OP_AND, OP_OR
} sql_op;

typedef enum {
	EX_LITERAL = 1, EX_COLUMN, EX_UNARY, EX_BINARY, EX_NOT,
	EX_ISNULL, EX_IN, EX_BETWEEN, EX_LIKE, EX_FUNC
} sql_expr_type;
```

表达式节点（**联合式结构，所有形态共用一个 struct**）：

```c
struct _sql_expr {
	sql_expr_type	xtype;
	sql_op		op;		/* EX_UNARY / EX_BINARY		*/
	sql_kw		kw;		/* EX_FUNC			*/
	int		negate;		/* NOT LIKE / NOT IN / ...	*/
	SQL_VALUE	lit;		/* EX_LITERAL			*/
	char		*name;		/* EX_COLUMN / EX_FUNC		*/
	char		*lname;
	SQL_EXPR	*left;
	SQL_EXPR	*right;
	SQL_EXPR	*third;		/* BETWEEN upper bound		*/
	SQL_EXPRLIST	*list;		/* IN value list		*/
};
```

| 节点 | 用到的字段 | 含义 |
|---|---|---|
| `EX_LITERAL` | `lit` | 字面量（含 `NULL`）。 |
| `EX_COLUMN` | `name`, `lname` | 列引用。**此时未绑定索引**，执行期才解析。 |
| `EX_UNARY` | `op`, `left` | 一元 `+` / `-`。 |
| `EX_BINARY` | `op`, `left`, `right` | 算术（`+ - * / %`）、比较（`= != < <= > >=`）、逻辑（`AND` / `OR`）。 |
| `EX_NOT` | `left` | `NOT <谓词>`。 |
| `EX_ISNULL` | `left`, `negate` | `IS [NOT] NULL`。 |
| `EX_IN` | `left`, `list`, `negate` | `[NOT] IN (...)`。 |
| `EX_BETWEEN` | `left`, `right`, `third`, `negate` | `[NOT] BETWEEN low AND high`。 |
| `EX_LIKE` | `left`, `right`, `negate` | `[NOT] LIKE pattern`。 |
| `EX_FUNC` | `kw`, `name` | `CURRENT_DATE/TIME/TIMESTAMP`。 |

**为什么用联合式而不是每种节点一个 struct？** 因为本层的节点种类少、字段高度重叠（都有 `left`/`right`），联合式让 `sql_expr_free` 只需一段递归、让求值器只需一个 `switch`。代价是结构体略大、字段含义依赖 `xtype` —— 在"最小 SQL 层"的规模下这个代价远小于收益。

语句节点（同样是联合式）：

```c
typedef struct _sql_stmt {
	sql_stmt_type	type;

	/* CREATE TABLE / DROP TABLE / INSERT / UPDATE / DELETE target */
	char		*tablename;
	char		*ltablename;
	int		if_exists;
	int		if_not_exists;

	/* CREATE TABLE */
	SQL_COLUMN	*columns;
	size_t		ncols;
	size_t		capcols;

	/* INSERT */
	char		**inscols;
	char		**linscols;
	size_t		ninscols;
	size_t		capins;
	size_t		caplins;
	SQL_EXPRLIST	**insert_rows;	/* Each row is an expression list */
	size_t		nrows_ins;
	size_t		caprows_ins;

	/* SELECT */
	SQL_SELECT_ITEM	*select_items;
	size_t		nselect;
	size_t		capselect;
	int		select_star;
	int		distinct;
	SQL_EXPR	*where;
	SQL_ORDER_ITEM	*order_by;
	size_t		norder;
	size_t		caporder;
	SQL_EXPR	*limit;
	SQL_EXPR	*offset;

	/* UPDATE */
	SQL_SET_ITEM	*set_items;
	size_t		nset;
	size_t		capset;
} SQL_STMT;
```

三个小结构体：

```c
typedef struct _sql_select_item {	/* 投影项 */
	SQL_EXPR	*expr;
	char		*alias;		/* AS 后的名字	*/
	char		*lalias;
} SQL_SELECT_ITEM;

typedef struct _sql_order_item {	/* ORDER BY 项 */
	SQL_EXPR	*expr;
	int		desc;
} SQL_ORDER_ITEM;

typedef struct _sql_set_item {		/* SET col = expr */
	char		*colname;
	char		*lcolname;
	int		resolved;	/* Resolved column index	*/
	SQL_EXPR	*expr;
} SQL_SET_ITEM;
```

### 2.5 逻辑计划 / 物理计划

> **如实说明：本实现没有独立的逻辑计划或物理计划。**

执行器是**直接的 AST 解释器（火山模型都没有，连迭代器抽象也没有）**。`exec_select` 是一个函数，按顺序做完"扫描 → 过滤 → 投影 → 去重 → 排序 → 分页"，没有中间表示。

这不是遗漏，而是刻意的取舍：

| 有计划的方案 | 本项目当前方案 |
|---|---|
| AST → 逻辑计划（关系代数树）→ 物理计划（算子树）→ 执行 | AST → 直接解释执行 |
| 优点：可优化（谓词下推、连接顺序、索引选择） | 优点：代码量小、路径短、易调试 |
| 代价：需要优化器框架、代价模型、规则集 | 代价：无法做查询优化，执行策略写死 |
| 适合：多表、索引、聚合 | 适合：单表、全表扫描、无索引 |

**运行时唯一接近"计划"的东西**是 SELECT 执行期的两个结构体：

```c
typedef struct _selrow {	/* 物化的一行中间结果 */
	size_t		seq;	/* 原始扫描序号，保证排序稳定 */
	SQL_VALUE	*proj;	/* 投影后的值 */
	SQL_VALUE	*keys;	/* ORDER BY 的排序键，预先求值 */
} SELROW;

/* qsort_r 不可移植，用文件级静态变量传递比较上下文 */
static const SQL_ORDER_ITEM *sort_order;
static size_t sort_norder;
```

`keys` 是**预先求值的排序键**：扫描阶段就把 `ORDER BY` 的表达式算好存下来，排序时只做 `sql_value_cmp`，避免 `qsort` 的 `O(n log n)` 次比较中反复求值表达式。这是一个小型的"物化"优化。

如果要引入真正的计划层，建议的插入点在 §6.2 详述。

### 2.6 行的表示：`SQL_ROW` 与线上格式

`SQL_ROW` 在运行期几乎不用（解码结果直接是 `SQL_VALUE *vals` 数组），它的定义是：

```c
typedef struct _sql_row {
	uint64		rowid;
	size_t		ncols;
	SQL_VALUE	*vals;
} SQL_ROW;
```

**真正重要的是线上（wire）格式**，定义在 `sql_row.c` 头部注释：

```
Row layout inside the value stored for a row id:

	[2 bytes: number of columns, big endian]
	per column:
		[1 byte:  0 => SQL NULL, 1 => present]
		[4 bytes: payload length, omitted when NULL]
		[n bytes: payload]

Fixed width types use exactly their wire size as the payload:

	SMALLINT 2, INT 4, BIGINT 8, DATE 4, TIME 8, TIMESTAMP 8
	FLOAT 4 and DOUBLE 8 IEEE 754 in network byte order

Everything else (character and binary strings) is stored verbatim.
```

即：

```
+------------------+------------------+---------------------------+----
| ncols (2B, BE)   | present (1B)     | len (4B, BE) | payload      | ...
+------------------+------------------+---------------------------+----
                     ^ NULL 时只写 0，后面 len/payload 全省
```

要点：

- **自描述**：头部有列数，解码时能校验 `ncols == schema->ncols`，schema 不匹配直接报 `SQL_ERR_STORE`，不会静默读错。
- **NULL 只占 1 字节**：省空间，也让"是否有值"的判断是一次字节读取。
- **定长类型也带长度字段**：看似冗余，但让解码循环无需区分类型就能跳过（`p += 4` 后按 `fixed_size` 读），代码统一。
- **全部大端（网络序）**：与字节序无关，数据文件可跨机器搬运；同时大端整数在 KV 里**字典序 == 数值序**，这是 §4.1 的关键。
- **有符号整数按补码存储**，解码时按 `sz*8` 做符号扩展回 `int64`：

```c
/* sql_row.c, sql_row_decode() */
if (sz < 8 && (mag >> (sz * 8 - 1)) & 1) {
	uint64 mask = ~((uint64)0) << (sz * 8);
	mag |= mask;
}
v = (int64)mag;
```

- **浮点数**用 `memcpy` 把 IEEE 754 位模式搬进/搬出 `uint32`/`uint64`，避免任何平台相关的浮点表示假设。

### 2.7 结果集

```c
typedef struct _sql_resultset {
	size_t		ncols;
	char		**colnames;
	size_t		nrows;
	size_t		caprows;
	SQL_VALUE	**rows;		/* nrows rows of ncols values	*/
} SQL_RESULTSET;
```

二维"行指针数组"而非一维扁平数组，这样 `addrow` 时 `realloc` 只需搬指针，不搬值。

### 2.8 会话

```c
typedef struct _sql_session {
	struct _van_store *store;
	char		*dir;
	size_t		ntables;
	size_t		cap_tables;
	SQL_TABLE	**tables;
	struct _sql_table *catalog;	/* Schema catalog		*/
	int		opened;
	flags_t		oflags;
	SQL_RESULTSET	**resultp;	/* 非 NULL 时结果集交给调用方 */
	size_t		rows_affected;
	size_t		last_rowid;
} SQL_SESSION;
```

`resultp` 是一个"输出模式开关"：`NULL` 表示执行完直接打印，非 `NULL` 表示把结果集指针写回去、由调用方负责释放。这避免了给 `exec` 系列函数再增加一条打印/不打印的参数链。

---

## 第三部分：关键流程

### 3.1 CREATE TABLE 的完整流程

**SQL 文本 → KV 写入**

输入示例：

```sql
CREATE TABLE IF NOT EXISTS users (
	id INT PRIMARY KEY,
	name VARCHAR(32) NOT NULL,
	age SMALLINT DEFAULT 18
);
```

#### 数据流总览（ASCII）
```
   SQL 文本
   +---------------------------------------------------------------+
   | const char *
                                                 |
   | "CREATE TABLE IF NOT EXISTS users (id INT PRIMARY KEY, ...)"
 |
   +------------------------------+--------------------------------+
                                   |  sql_lex(sql, 0, &ts)
                                   v
   +---------------------------------------------------------------+
   | LEXER       sql_lex.c : sql_lex()
                            |
   +------------------------------+--------------------------------+
                                   |
                                   |   SQL_TOKENSTREAM
                                   |    ├─ SQL_TOKEN { TK_IDENT, kw=KW_CREATE }
                                   |    ├─ SQL_TOKEN { TK_IDENT, kw=KW_TABLE  }
                                   |    ├─ SQL_TOKEN { TK_IDENT, kw=KW_IF     }
                                   |    ├─ SQL_TOKEN { TK_IDENT, "users"      }
                                   |    ├─ SQL_TOKEN { TK_PUNCT, "("          }
                                   |    ├─ SQL_TOKEN { TK_IDENT, "id"         }
                                   |    ├─ SQL_TOKEN { TK_IDENT, kw=KW_INT    }
                                   |    └─ ...
                                   v
   +---------------------------------------------------------------+
   | PARSER      sql_parse.c : sql_parse() -> parse_create_table()
|
   |                         -> parse_column_def() -> parse_type()
|
   +------------------------------+--------------------------------+
                                   |  节点对象本身由下一层创建
                                   v
   +---------------------------------------------------------------+
   | AST         sql_ast.c : sql_stmt_alloc()
                     |
   |                         sql_strndup() / sql_strlower()
       |
   +------------------------------+--------------------------------+
                                   |
                                   |   SQL_STMT
                                   |    ├─ type          = STMT_CREATE_TABLE
                                   |    ├─ tablename     = "users"   <- sql_strndup
                                   |    ├─ ltablename    = "users"   <- sql_strlower
                                   |    ├─ if_not_exists = 1
                                   |    ├─ ncols         = 3
                                   |    └─ columns[3] : SQL_COLUMN
                                   |         ├─ [0] { name="id",   type=SQL_T_INT,      primary_key=1 }
                                   |         ├─ [1] { name="name", type=SQL_T_VARCHAR,  length=32,
                                   |         │        not_null=1 }
                                   |         └─ [2] { name="age",  type=SQL_T_SMALLINT, has_default=1,
                                   |                  defval={ SQL_T_INT, ival=18 } }
                                   v
   +---------------------------------------------------------------+
   | ANALYZER    [无] 本实现没有独立的语义分析阶段
                |
   |             列级校验其实在 parse_create_table() 收尾已做完：
 |
   |               列名重复 / VARCHAR 缺长度 / DEFAULT 转型
       |
   |             表级检查则下沉到 Executor：
                      |
   |               重名检查 / 主键能否作 rowid 别名
               |
   +------------------------------+--------------------------------+
                                   |  （透传，结构不变）SQL_STMT
                                   v
   +---------------------------------------------------------------+
   | EXECUTOR    sql_exec.c : sql_stmt_exec()
                     |
   |                          -> exec_create_table()
              |
   |                            ├─ build_schema()
                 |
   |                            ├─ sql_table_alloc()  [catalog.c]
 |
   |                            ├─ sql_table_open()   [catalog.c]
 |
   |                            └─ sql_catalog_save_schema()
      |
   +------------------------------+--------------------------------+
                                   |
                                   |   SQL_SCHEMA                       <- build_schema() 深拷贝
                                   |    ├─ name/lname  = "users"
                                   |    ├─ cols[3]     = columns[] 深拷贝（含 defval）
                                   |    ├─ pk_indx     = 0    (INT -> 可作 rowid 别名)
                                   |    └─ next_rowid  = 1
                                   |   
                                   |   SQL_TABLE                        <- sql_table_alloc()
                                   |    ├─ name/lname/kvname = "users"
                                   |    ├─ schema -> 上面的 SQL_SCHEMA
                                   |    └─ table = NULL -> VAN_TABLE（sql_table_open 后）
                                   |   
                                   |   char *                           <- sql_schema_serialize()
                                   |    "Vvansql1\nTusers\nCid\t2\t0\t0\t1\t0\t-\n"
                                   |    "Cname\t6\t32\t1\t0\t0\t-\n...R1\n"
                                   v
   +---------------------------------------------------------------+
   | KV          sql_catalog.c : cat_put() -> table->put()
        |
   +------------------------------+--------------------------------+
                                   |
                                   |   VAN_DATUM x 2
                                   |    ├─ { key="users",            val= schema 文本 }
                                   |    └─ { key="__van_sql_tables", val="users\n"     }
                                   v
                    +------------------+
                    |  目录表 KV 落盘  |
                    +------------------+
```

**读图要点：**

- **Parser 与 AST 是两层而非一层**：`sql_parse.c` 负责"读记号、决定结构"，`sql_ast.c` 负责"造节点对象"。Parser 不自己 `malloc`，一律调 `sql_ast.c` 的 `sql_stmt_alloc()` / `sql_strndup()`。
- **Analyzer 一格是虚的**：本实现没有独立的语义分析阶段，图中保留它只为对齐传统 SQL 架构，实际工作被拆进 Parser 收尾与 Executor 开头（见 §1.2）。
- **Executor 一口气产出三种结构**：`SQL_SCHEMA`（持久元数据）、`SQL_TABLE`（运行期句柄）、`char *`（落盘文本）。前两者会被会话长期持有，文本用完即弃。

#### 阶段总览：跨文件调用链

| # | 文件 | 函数 | 数据结构变化 |
|---|---|---|---|
| 0 | `sql_session.c` | `session_run()` | `const char *sql`（文本）→ 调 lex/parse/exec |
| 1 | `sql_lex.c` | `sql_lex()` | 文本 → `SQL_TOKENSTREAM`（`SQL_TOKEN[]`） |
| 2 | `sql_parse.c` | `sql_parse()` | 看首记号 `kw == KW_CREATE` → 定 `type = STMT_CREATE_TABLE` |
| 3 | **`sql_ast.c`** | **`sql_stmt_alloc()`** | → 空 `SQL_STMT`（`type` 已定，其余清零） |
| 4 | `sql_parse.c` | `parse_create_table()` | 记号 → 填充 `SQL_STMT`（表名 + `SQL_COLUMN[]`） |
| 5 | `sql_session.c` | `session_run()` | `SQL_STMT` → 交给 `sql_stmt_exec()` |
| 6 | `sql_exec.c` | `sql_stmt_exec()` | 按 `stmt->type` 分发 |
| 7 | **`sql_exec.c`** | **`exec_create_table()`** | `SQL_STMT` → `SQL_SCHEMA` → `SQL_TABLE` → KV |

#### 阶段 A：解析（`sql_parse.c` + `sql_ast.c`）—— 文本 → `SQL_STMT`

**A1. `sql_parse()`（`sql_parse.c`）** 只做分派，不解析细节：

```c
tk = peek(ts, 0);
switch (tk->kw) {
	case KW_CREATE: type = STMT_CREATE_TABLE; break;
	...
}
sql_stmt_alloc(type, &stmt);          /* ← sql_ast.c */
switch (type) {
	case STMT_CREATE_TABLE: ret = parse_create_table(ts, stmt); break;
	...
}
```

**A2. `sql_stmt_alloc()`（`sql_ast.c`）—— 这是 `sql_ast.c` 在本流程中的核心角色。**

它用 `sql_calloc()` 分配一个 `SQL_STMT` 并置 `stmt->type`。**注意：AST 节点对象本身由 `sql_ast.c` 创建，`sql_parse.c` 只负责往里填字段** —— 这条分工让"节点如何分配/释放"只有一个地方说了算（`sql_ast.c` 的 `sql_stmt_alloc` / `sql_stmt_free`）。

同一阶段还用到 `sql_ast.c` 的字符串工具：`sql_strndup()`（拷原始拼写）与 `sql_strlower()`（生成小写查找名）。

**A3. `parse_create_table()`（`sql_parse.c`）** 逐记号填充：

| 记号 | 动作 | 写入字段 |
|---|---|---|
| `CREATE` `TABLE` | `sql_ts_consume` + `expect_kw(KW_TABLE)` | — |
| `IF NOT EXISTS` | `expect_kw(KW_NOT)` + `expect_kw(KW_EXISTS)` | `stmt->if_not_exists = 1` |
| `users` | `parse_ident()` → `sql_strndup` + `sql_strlower`（`sql_ast.c`） | `stmt->tablename` / `stmt->ltablename` |
| `(` | `expect_punct("(")` | — |
| 每行列定义 | `grow_vec(&stmt->columns, &stmt->capcols, ...)` 扩容<br>`sql_column_init()`（`sql_value.c`）<br>`parse_column_def()` | `stmt->columns[n]`、`stmt->ncols++` |
| `PRIMARY KEY(col)` | 表级主键：回查已解析列的 `lname`，命中则置位 | `stmt->columns[i].primary_key = 1` |
| `,` | 继续下一列 | — |
| `)` | `expect_punct(")")` | — |

**A4. `parse_column_def()`（`sql_parse.c`）** 填充单个 `SQL_COLUMN`：

| 记号 | 调用 | 写入字段 |
|---|---|---|
| `id` | `parse_ident()` | `col->name` / `col->lname` |
| `INT` | `parse_type()` → **`sql_type_parse()`（`sql_type.c`）** | `col->type` / `col->length` |
| `PRIMARY KEY` | `expect_kw(KW_PRIMARY)` + `expect_kw(KW_KEY)` | `col->primary_key = 1` |
| `NOT NULL` | `accept_kw(KW_NOT)` + `expect_kw(KW_NULL)` | `col->not_null = 1` |
| `DEFAULT 18` | `accept_kw(KW_DEFAULT)` → `token_value()` | `col->defval`、`col->has_default = 1` |

**A5. `parse_create_table()` 收尾做四项校验**（列完才做，因为重复列名/表级主键要等全部列解析完）：

1. `stmt->ncols != 0`；
2. `stmt->columns[i].lname` 两两 `strcmp`，重复 → `SQL_ERR_PARSE`；
3. `SQL_TYPE_HAS_LENGTH(type)` 且 `length == 0` → `SQL_ERR_PARSE`（`VARCHAR` 没写长度）；
4. 有 `DEFAULT` 的，用 **`sql_value_cast()`（`sql_type.c`）** 把默认值转成列类型 —— 所以 `DEFAULT` 在**解析期**就已定型，执行期不必再转换。

#### 阶段 B：执行（`sql_exec.c`）—— `SQL_STMT` → `SQL_SCHEMA` → KV

`sql_stmt_exec()` 按 `stmt->type` 分发到 `exec_create_table()`，随后：

1. **重名检查**：`sql_session_find(sess, stmt->ltablename)`。
   - 已存在且 `if_not_exists` → 直接返回 `SQL_OK`（静默跳过）。
   - 已存在且无 `IF NOT EXISTS` → 返回 `SQL_ERR_DUP_TABLE`。
2. **构建 schema**（`build_schema()`）：把 `stmt->columns[]` 深拷贝成 `SQL_SCHEMA`，同时：
   - 遇到第二个 `primary_key` 列 → 返回 `SQL_ERR_FEATURE`（**复合主键需要索引，当前不支持**）。
   - 记录 `schema->pk_indx`。
   - 最后一道收敛：**主键不是整数类型则 `pk_indx = -1`**（不能当 rowid 别名）。
   - `next_rowid = 1`（新表从 1 开始）。
3. **创建表句柄** `sql_table_alloc()`：分配 `SQL_TABLE`，生成 `name`/`lname`/`kvname`。
   - 此处校验表名：长度 ∈ `(0, SQL_MAX_IDENT_LEN]`，且**不能以 `__van_` 开头**（保留给目录，防止用户覆盖元数据）。
4. **登记到会话** `sql_session_addtable()`：加入 `sess->tables[]` 数组（必要时 `realloc` 扩容）。
5. **打开底层引擎表** `sql_table_open()`：
   ```c
   van_table_create((VAN_STORE *)sess->store, &table);
   table->set_inmem_threshold(table, SQL_TABLE_THRESHOLD);   /* 64 MB */
   table->open(table, tb->kvname, VAN_TABLE_SKIP_THREAD_START);
   table->start_threads(table);
   ```
   失败则 `sql_session_deltable()` 回滚会话内的登记。
6. **持久化 schema** `sql_catalog_save_schema()`：
   - `sql_schema_serialize()` 生成文本（格式见 §4.2）。
   - `cat_put(sess, schema->lname, text, strlen(text))` —— **KV 写入 #1**。
   - `save_tablelist()` 重写 `__van_sql_tables` 键 —— **KV 写入 #2**。

#### 阶段 C：数据结构演变一览

以本文开头的示例 SQL 为例，数据形态的逐次变化：

```
① const char *
   "CREATE TABLE IF NOT EXISTS users (id INT PRIMARY KEY, ...)"

② SQL_TOKENSTREAM                                    ← sql_lex()
   tokens[] = { TK_IDENT kw=KW_CREATE, TK_IDENT kw=KW_TABLE,
                TK_IDENT kw=KW_IF, ..., TK_IDENT "users",
                TK_PUNCT "(", TK_IDENT "id", TK_IDENT kw=KW_INT, ... }

③ SQL_STMT                                           ← sql_stmt_alloc() [sql_ast.c]
   type        = STMT_CREATE_TABLE
   tablename   = "users"          ltablename = "users"      ← parse_ident() [sql_ast.c 的 sql_strndup/sql_strlower]
   if_not_exists = 1
   ncols       = 3
   columns[0]  = { name="id",   lname="id",   type=SQL_T_INT,     length=0,
                   not_null=0, primary_key=1, has_default=0 }
   columns[1]  = { name="name", lname="name", type=SQL_T_VARCHAR, length=32,
                   not_null=1, primary_key=0, has_default=0 }
   columns[2]  = { name="age",  lname="age",  type=SQL_T_SMALLINT, length=0,
                   not_null=0, primary_key=0, has_default=1,
                   defval={ type=SQL_T_INT, isnull=0, ival=18 } }   ← 解析期已 sql_value_cast 过

④ SQL_SCHEMA                                         ← build_schema() [sql_exec.c]
   name="users"  lname="users"  ncols=3  cols[]=③ 的深拷贝
   pk_indx = 0          /* 第 0 列是 INT → 可作 rowid 别名 */
   next_rowid = 1

⑤ SQL_TABLE                                          ← sql_table_alloc() [sql_catalog.c]
   name="users"  lname="users"  kvname="users"
   schema = ④   table = NULL   opened = 0
   → 登记进 SQL_SESSION.tables[]                      ← sql_session_addtable()

⑥ VAN_TABLE（引擎句柄）                               ← sql_table_open()
   ⑤.table = <引擎对象>, opened = 1

⑦ char *（schema 文本）                               ← sql_schema_serialize()
   "Vvansql1\nTusers\nCid\t2\t0\t0\t1\t0\t-\n"
   "Cname\t6\t32\t1\t0\t0\t-\nCage\t1\t0\t0\t0\t1\t0101...\nR1\n"

⑧ VAN_DATUM × 2                                      ← cat_put()
   { key="users",             val=⑦ }   → 目录表 put #1
   { key="__van_sql_tables",  val="users\n" } → 目录表 put #2
```

**触发的 KV 操作：** 2 次 put（都在目录表）。

**注意：**

- 用户数据表此时是空的，但引擎已经为它建好了物理表（阶段 B 第 6 步）。所以"CREATE TABLE"的代价里包含一个 `van_table_create` + `open`，而不只是两次目录写入。
- **`SQL_STMT` 与 `SQL_SCHEMA` 是两份独立拷贝**（`build_schema()` 深拷贝每个 `SQL_COLUMN`，包括 `defval`）。这是有意的：`SQL_STMT` 在语句执行完立即 `sql_stmt_free()`，而 `SQL_SCHEMA` 要活到会话结束。若共享指针会出现悬垂引用。
- 同一条语句的 `next_rowid` 只在这个时刻初始化为 1；后续 INSERT 会推进它并回写目录（见 §3.2）。

### 3.2 INSERT 的完整流程

入口 `exec_insert()` → 对 `stmt->insert_rows[]` 每一行调用 `insert_one_row()`。

**步骤：**

1. **定位表**：`sql_session_find()` + `sql_table_ensure_open()`（惰性打开：表在 `sql_catalog_load()` 后只是登记了句柄，真正 `open` 推迟到首次访问）。
2. **列映射**（`insert_target_col()`）：
   - 无列名列表（`INSERT INTO t VALUES (...)`）→ 第 `i` 个值映射到第 `i` 列（位置映射）。
   - 有列名列表 → 每个值通过 `sql_schema_findcol(schema, linscols[i])` 解析成列索引。
   - 同时用 `targets[]` 数组标记"哪些列被显式赋值了"（未提及的记 `ncols` 作为哨兵）。
3. **求值 + 类型转换**：对这一行的每个表达式
   ```c
   sql_expr_eval(sess, schema, NULL, row->items[i], &raw);          /* row 上下文为 NULL */
   sql_value_cast(&raw, schema->cols[cindx].type,
                  schema->cols[cindx].length, &conv);
   sql_value_copy(&conv, &vals[cindx]);
   ```
   注意 `sql_expr_eval` 的 `row` 传 `NULL`：INSERT 的 `VALUES` 里不允许引用列（不允许 `VALUES (age + 1)`），所以没有行上下文。
4. **补齐未提及的列**（顺序：先 DEFAULT，后 NULL）：
   ```c
   if (schema->cols[i].has_default)
       sql_value_copy(&schema->cols[i].defval, &vals[i]);
   else {
       sql_value_set_null(&vals[i], schema->cols[i].type);
       if (schema->cols[i].not_null)
           return SQL_ERR_NOT_NULL;      /* 无默认值且 NOT NULL */
   }
   ```
5. **选取 rowid**（这是主键语义的核心）：
   ```c
   if (schema->pk_indx >= 0) {
       if (vals[pk].isnull) {
           rowid = schema->next_rowid;                 /* 自动分配 */
           sql_value_set_int(&vals[pk], coltype, rowid); /* 回填到 PK 列 */
       } else {
           if (!SQL_TYPE_IS_INT(vals[pk].type)) return SQL_ERR_TYPE;
           if (vals[pk].ival <= 0)           return SQL_ERR_TYPE;
           rowid = (uint64)vals[pk].ival;              /* PK 值即 rowid */
       }
       if (rowid_key_free(sess, tb, rowid) == 0)
           return SQL_ERR_DUP_KEY;                     /* 主键冲突 */
   } else
       rowid = schema->next_rowid;
   ```
6. **推进 `next_rowid`**：
   ```c
   if (rowid >= schema->next_rowid) {
       schema->next_rowid = rowid + 1;
       dirty = 1;                    /* 需要回写目录 */
   }
   ```
7. **编码并写入**：`sql_row_encode()` → `sql_kv_put(sess, tb, rowid, buf, blen)`。
8. **必要时持久化新 `next_rowid`**：`if (dirty) sql_catalog_save_schema(sess, schema)`。

**触发的 KV 操作（每行）：**

| 情形 | 操作 |
|---|---|
| 有整数主键 | 1 次 `get`（查 `rowid_key_free`）+ 1 次 `put` |
| 无主键 | 1 次 `put` |
| `next_rowid` 增长 | 额外 1 次目录 `put`（schema）+ 1 次目录 `put`（表清单） |

**多值插入的原子性：** `exec_insert()` 逐行调用 `insert_one_row()`，中途失败时**已写入的行保留**，只把 `sess->rows_affected` 设为已成功的行数。代码注释明确写了这点：

```c
/*
 * Rows already written stay: there is no
 * statement level rollback in this layer.
 */
```

### 3.3 SELECT 的完整流程

输入示例：

```sql
SELECT id, age * 2 AS doubled FROM users WHERE age >= 25 ORDER BY id DESC LIMIT 3;
```

#### 数据流总览（ASCII）

```
   SQL 文本
   +---------------------------------------------------------------+
   | const char *                                                 |
   | "SELECT id, age*2 AS doubled FROM users WHERE age >= 25"     |
   | " ORDER BY id DESC LIMIT 3"                                  |
   +------------------------------+--------------------------------+
                                   |  sql_lex(sql, 0, &ts)
                                   v
   +---------------------------------------------------------------+
   | LEXER       sql_lex.c : sql_lex()                            |
   +------------------------------+--------------------------------+
                                   |
                                   |   SQL_TOKENSTREAM
                                   |    ├─ SQL_TOKEN { TK_IDENT, kw=KW_SELECT }
                                   |    ├─ SQL_TOKEN { TK_IDENT, "id"          }
                                   |    ├─ SQL_TOKEN { TK_PUNCT, "*"           }
                                   |    ├─ SQL_TOKEN { TK_IDENT, kw=KW_FROM    }
                                   |    ├─ SQL_TOKEN { TK_IDENT, "users"       }
                                   |    ├─ SQL_TOKEN { TK_IDENT, kw=KW_WHERE   }
                                   |    └─ ...
                                   v
   +---------------------------------------------------------------+
   | PARSER      sql_parse.c : sql_parse() -> parse_select()      |
   |             优先级链: parse_expr -> parse_and -> parse_not   |
   |                     -> parse_predicate -> parse_add          |
   |                        -> parse_mul -> parse_unary           |
   |                           -> parse_primary                   |
   +------------------------------+--------------------------------+
                                   |  节点对象由下一层创建
                                   v
   +---------------------------------------------------------------+
   | AST         sql_ast.c : sql_stmt_alloc()                     |
   |             表达式节点由 sql_expr_* 系列构造器创建           |
   |             sql_expr_column / literal / binary / ...         |
   +------------------------------+--------------------------------+
                                   |
                                   |   SQL_STMT
                                   |    ├─ type        = STMT_SELECT
                                   |    ├─ tablename   = "users"
                                   |    ├─ select_star = 0     distinct = 0
                                   |    ├─ select_items[2] : SQL_SELECT_ITEM
                                   |    │   ├─[0]{ expr = EX_COLUMN(lname="id"), alias=NULL }
                                   |    │   └─[1]{ expr = EX_BINARY(OP_MUL,
                                   |    │                   left  = EX_COLUMN(lname="age"),
                                   |    │                   right = EX_LITERAL(2)),
                                   |    │         alias = "doubled", lalias = "doubled" }
                                   |    ├─ where = EX_BINARY(OP_GE)
                                   |    │            ├─ left  = EX_COLUMN(lname="age")
                                   |    │            └─ right = EX_LITERAL(25)
                                   |    ├─ order_by[1] : SQL_ORDER_ITEM
                                   |    │   └─[0]{ expr = EX_COLUMN(lname="id"), desc = 1 }
                                   |    ├─ limit  = EX_LITERAL(3)
                                   |    └─ offset = NULL
                                   v
   +---------------------------------------------------------------+
   | ANALYZER    [无] 本实现没有独立的语义分析阶段                |
   |             EX_COLUMN 此刻仍只是字符串 lname，未绑定下标     |
   |             绑定发生在 Executor 求值期：sql_schema_findcol() |
   +------------------------------+--------------------------------+
                                   |  （透传，结构不变）SQL_STMT
                                   v
   +---------------------------------------------------------------+
   | EXECUTOR    sql_exec.c : sql_stmt_exec() -> exec_select()    |
   |             扫描 -> 过滤 -> 投影 -> 去重 -> 排序 -> 分页     |
   +------------------------------+--------------------------------+
                                   |
                                   |   逐行求值中：
                                   |     SQL_VALUE[]  一行（ncols 个，解码自 KV）
                                   |      ├─ [0] id  = { SQL_T_INT,      ival=1  }
                                   |      └─ [2] age = { SQL_T_SMALLINT, ival=30 }
                                   |
                                   |     SELROW { seq, proj[], keys[] }
                                   |      ├─ proj[2]  投影结果（表达式已求值）
                                   |      └─ keys[1]  ORDER BY 键（扫描期预求值）
                                   |
                                   |     SQL_RESULTSET { ncols, colnames[], rows[][] }
                                   v
   +---------------------------------------------------------------+
   | KV          sql_catalog.c : sql_kv_get() -> table->get()     |
   +------------------------------+--------------------------------+
                                   |
                                   |   VAN_DATUM（每行一次 get，共 next_rowid-1 次）
                                   |    ├─ key = 8 字节大端 rowid
                                   |    └─ val = 行记录字节串 -> sql_row_decode()
                                   v
                    +------------------+
                    |  结果集输出/返回  |
                    +------------------+
```

#### 阶段总览：跨文件调用链

| # | 文件 | 函数 | 数据结构变化 |
|---|---|---|---|
| 0 | `sql_session.c` | `session_run()` | `const char *sql` → 调 lex/parse/exec |
| 1 | `sql_lex.c` | `sql_lex()` | 文本 → `SQL_TOKENSTREAM` |
| 2 | `sql_parse.c` | `sql_parse()` | 首记号 `kw == KW_SELECT` → 定 `type = STMT_SELECT` |
| 3 | **`sql_ast.c`** | **`sql_stmt_alloc()`** | → 空 `SQL_STMT`（`type` 已定，其余清零） |
| 4 | `sql_parse.c` | `parse_select()` | 记号 → 填充 `SQL_STMT`（投影项 / WHERE / ORDER BY / LIMIT） |
| 5 | `sql_session.c` | `session_run()` | `SQL_STMT` → 交给 `sql_stmt_exec()` |
| 6 | `sql_exec.c` | `sql_stmt_exec()` | 按 `stmt->type` 分发 |
| 7 | **`sql_exec.c`** | **`exec_select()`** | `SQL_STMT` → `SQL_VALUE[]` → `SELROW` → `SQL_RESULTSET` |

#### 阶段 A：解析（`sql_parse.c` + `sql_ast.c`）—— 文本 → `SQL_STMT`

**A1. `sql_parse()`** 按首记号 `KW_SELECT` 定 `STMT_SELECT`，调 `sql_stmt_alloc()`（`sql_ast.c`）建节点，再分派给 `parse_select()`。

**A2. `parse_select()`** 按子句顺序填充：

| 子句 | 动作 | 写入字段 |
|---|---|---|
| `SELECT` | `sql_ts_consume` | — |
| `DISTINCT` / `ALL` | `accept_kw(KW_DISTINCT)` / 跳过 `KW_ALL` | `stmt->distinct = 1` |
| `*` | `tok_is_punct("*")` | `stmt->select_star = 1`（不再解析投影项） |
| 投影项 | `parse_expr()` → 可选 `AS alias`（`parse_ident()`）<br>`grow_vec(&stmt->select_items, ...)` | `stmt->select_items[n]`、`stmt->nselect++` |
| `FROM users` | `expect_kw(KW_FROM)` + `parse_ident()` | `stmt->tablename` / `stmt->ltablename` |
| `FROM t alias` | 表别名会遮蔽表名，**直接拒绝** | 返回 `SQL_ERR_FEATURE` |
| `WHERE ...` | `accept_kw(KW_WHERE)` + `parse_expr()` | `stmt->where` |
| `ORDER BY ...` | `parse_expr()` + `KW_DESC`/`KW_ASC` | `stmt->order_by[n]`、`oitem->desc`、`stmt->norder++` |
| `LIMIT n` | `parse_expr()` | `stmt->limit` |
| `OFFSET n` | `parse_expr()`（`LIMIT` 后可选；单独出现时**要求后面必须跟 `LIMIT`**） | `stmt->offset` |

**A3. `sql_ast.c` 的角色 —— 比 CREATE TABLE 更重要。** 这里是 **`sql_expr_*` 系列构造器的主战场**：

| 构造器 | 产出节点 | 本例用到 |
|---|---|---|
| `sql_expr_column()` | `EX_COLUMN`（`sql_strndup` + `sql_strlower` 生成 `name`/`lname`） | `id`、`age` |
| `sql_expr_literal()` | `EX_LITERAL`（深拷贝 `SQL_VALUE`） | `2`、`25`、`3` |
| `sql_expr_binary()` | `EX_BINARY`（算术/比较/AND/OR） | `age * 2`、`age >= 25` |
| `sql_expr_unary()` | `EX_UNARY` | 一元 `+`/`-` |
| `sql_expr_not()` | `EX_NOT` | `NOT` |
| `sql_expr_isnull()` | `EX_ISNULL` | `IS [NOT] NULL` |
| `sql_expr_in()` | `EX_IN` | `[NOT] IN (...)` |
| `sql_expr_between()` | `EX_BETWEEN` | `[NOT] BETWEEN` |
| `sql_expr_like()` | `EX_LIKE` | `[NOT] LIKE` |
| `sql_expr_func()` | `EX_FUNC` | `CURRENT_DATE/TIME/TIMESTAMP` |

**A4. 关键点：`EX_COLUMN` 此刻仍未绑定。** 它只有 `lname`（字符串），没有列下标。名字解析推迟到执行期、对每一行调用 `sql_schema_findcol()` —— 这正是 §1.2 所说"本实现缺少独立 Analyzer"的直接体现（代价：每行每列一次线性查表）。

#### 阶段 B：执行（`sql_exec.c`）—— `SQL_STMT` → `SQL_RESULTSET`

`sql_stmt_exec()` 分发到 `exec_select()`，随后：

1. **定位并打开表**：`sql_session_find()` + `sql_table_ensure_open()`；取 `schema`、`ncols`。
2. **构建投影计划**（决定输出几列、每列叫什么）：
   - `SELECT *` → `nproj = ncols`，列名直接取 `schema->cols[i].name`。
   - 否则 → `nproj = stmt->nselect`，列名由 `projection_name()` 决定：
     ```
     alias（AS 指定）  >  列名（表达式是纯列引用时）  >  "expr"
     ```
3. **求值 `LIMIT` / `OFFSET`**：`eval_int_arg()` 把表达式求值后转成 `BIGINT`；必须是非负整数，否则 `SQL_ERR_ARG`。注意这里 `sql_expr_eval` 的 `schema` 和 `row` 都传 `NULL` —— 分页参数不依赖行。
4. **全表扫描**（核心）：
   ```c
   maxrid = schema->next_rowid;
   for (rid = 1; rid < maxrid; rid++) {
       fetch_row(sess, tb, rid, vals, &found);   /* sql_kv_get + sql_row_decode */
       if (!found) continue;                     /* 被删除的行留下空洞 */
       ...
   }
   ```
   `fetch_row()` = `sql_kv_get()` → 若 `*datap == NULL` 则 `found = 0` → 否则 `sql_row_decode()` → `sql_kv_free(data)`。
5. **过滤**：`sql_expr_test(sess, schema, vals, stmt->where, &tv)`，**只有 `tv == TRI_TRUE` 才保留**。`TRI_FALSE` 和 `TRI_UNKNOWN` 都丢弃（SQL 语义：WHERE 只接受真）。
6. **投影**：为通过过滤的行分配 `proj[nproj]`
   - `select_star` → 直接 `sql_value_copy(&vals[i], &proj[i])`（省一次表达式求值，这是简化后的写法）。
   - 否则 → `sql_expr_eval(sess, schema, vals, item->expr, &tmp)` 再拷贝。
7. **求值排序键**：对 `stmt->order_by[i]` 逐个 `sql_expr_eval` 存入 `keys[i]`。**预先求值**，排序阶段就不再碰表达式。
8. **DISTINCT 去重**：拿当前 `proj` 与**已收集的所有行**逐个 `sql_value_cmp` 比较，`nproj` 列全相等则判为重复并丢弃。
   > 代码注释：`DISTINCT is applied here, before the ordering.` —— 去重在排序**之前**。
9. **排序**：
   ```c
   if (stmt->norder > 0 && nrows > 1) {
       sort_order = stmt->order_by;
       sort_norder = stmt->norder;
       qsort(rows, nrows, sizeof(SELROW), selrow_cmp);
   }
   ```
   `selrow_cmp` 依次比较每个排序键，`desc` 时取反；**全部相等时比较 `seq`**（原始扫描序号），保证排序**稳定**。
10. **分页**：先跳过 `OFFSET` 行，再按 `LIMIT` 截断：
    ```c
    for (i = 0; i < nrows; i++) {
        if (i < start) continue;              /* OFFSET */
        if (has_limit && printed >= limit) break;
        sql_resultset_addrow(rs, rows[i].proj);
        printed++;
    }
    ```
11. **输出**：
    - `sess->resultp != NULL` → `*sess->resultp = rs`，**所有权移交调用方**。
    - 否则 → `sql_resultset_print(rs, out)` 后 `sql_resultset_free(rs)`。
12. **清理**：释放 `rows[]` 里每一行的 `proj`/`keys`、列名数组、`vals`。

#### 阶段 C：数据结构演变一览

以本文开头的示例 `SELECT id, age*2 AS doubled FROM users WHERE age >= 25 ORDER BY id DESC LIMIT 3` 为例：

```
① SQL_TOKENSTREAM                    ← sql_lex()

② SQL_STMT                           ← sql_stmt_alloc() [sql_ast.c]
   type         = STMT_SELECT
   tablename    = "users"   ltablename = "users"
   select_star  = 0         distinct   = 0
   nselect      = 2
   select_items[0] = { expr = EX_COLUMN{lname="id"},           alias = NULL }
   select_items[1] = { expr = EX_BINARY{OP_MUL,                alias = "doubled"
                         left  = EX_COLUMN{lname="age"},       lalias= "doubled"
                         right = EX_LITERAL{SQL_T_INT, ival=2} },
   where        = EX_BINARY{OP_GE, EX_COLUMN{lname="age"}, EX_LITERAL{ival=25}}
   order_by[0]  = { expr = EX_COLUMN{lname="id"}, desc = 1 }
   limit        = EX_LITERAL{ival=3}      offset = NULL

   ★ 此刻所有 EX_COLUMN 都只有 lname 字符串，没有任何列下标

③ SQL_VALUE[] 一行（ncols 个）       ← fetch_row(): sql_kv_get + sql_row_decode
   [0] id  = { type=SQL_T_INT,      isnull=0, ival=1  }
   [1] ... = { ... }
   [2] age = { type=SQL_T_SMALLINT, isnull=0, ival=30 }

④ 求值期绑定                         ← sql_expr_eval(): sql_schema_findcol()
   EX_COLUMN{lname="age"}  -->  indx = 2  -->  row[2]
   （每行、每个列引用都要查一次，这是"缺少 Analyzer"的代价）

⑤ SELROW                             ← 每行一个，物化在内存
   seq  = 0            （原始扫描序号，保证排序稳定）
   proj[2] = { {SQL_T_INT, ival=1}, {SQL_T_BIGINT, ival=60} }   投影已求值
   keys[1] = { {SQL_T_INT, ival=1} }                            ORDER BY 键已求值

⑥ SELROW[] 排序后                    ← qsort(selrow_cmp)
   按 keys[0] 降序；keys 全相等时按 seq 升序（稳定）

⑦ SQL_RESULTSET                      ← sql_resultset_addrow()（跳过 OFFSET、按 LIMIT 截断）
   ncols    = 2
   colnames = { "id", "doubled" }
   nrows    = 3
   rows[][] = ⑤proj 的深拷贝
```

**触发的 KV 操作：** `next_rowid - 1` 次 `get`（**每行一次，删过的行也照样读一次**）。这是当前最大的性能瓶颈，见 §6。

### 3.4 UPDATE / DELETE 的完整流程

#### UPDATE

1. **定位表** + `sql_table_ensure_open()`。
2. **预解析 SET 目标列**：`sql_schema_findcol()` 把每个 `set_items[i].lcolname` 解析成列索引存进 `targets[]`。**这一步在扫描之外**，列名错误不必等到扫到某一行才报错。
3. **全表扫描** `rid = 1 .. next_rowid-1`，`fetch_row()` + `WHERE` 过滤（同 SELECT 第 4、5 步）。
4. **逐项求值并回写**：
   ```c
   for (i = 0; i < stmt->nset; i++) {
       cindx = targets[i];
       sql_expr_eval(sess, schema, vals, stmt->set_items[i].expr, &conv);
       sql_value_cast(&conv, schema->cols[cindx].type,
                      schema->cols[cindx].length, &vals[cindx]);
       if (vals[cindx].isnull && schema->cols[cindx].not_null)
           return SQL_ERR_NOT_NULL;
       if ((int)cindx == schema->pk_indx)
           pkchanged = 1;
   }
   ```
   注意 `sql_expr_eval` 传的是**当前行 `vals`**，所以 `SET age = age + 1` 能正确读到旧值。
5. **主键变更处理**（最复杂的分支）：
   ```c
   if (pkchanged) {
       if (vals[pk].isnull)                    return SQL_ERR_NOT_NULL;
       if (!SQL_TYPE_IS_INT(...) || ival <= 0) return SQL_ERR_TYPE;
       newrid = (uint64)vals[pk].ival;
       if (newrid != rid && rowid_key_free(sess, tb, newrid) == 0)
           return SQL_ERR_DUP_KEY;
   }
   ```
6. **写回**：`sql_row_encode()` → `sql_kv_put(sess, tb, newrid, buf, buf_len)`。
7. **主键真的变了才搬移**：
   ```c
   if (newrid != rid) {
       sql_kv_del(sess, tb, rid);              /* 删旧 Key */
       if (newrid >= schema->next_rowid) {
           schema->next_rowid = newrid + 1;
           sql_catalog_save_schema(sess, schema);
       }
   }
   ```

**为什么必须"先 put 新 Key，再 del 旧 Key"？** 如果先删旧的，中途失败就会丢行；先写新的，即使删旧失败也只是留下一行重复数据（下次扫描会读到两份），数据不会凭空消失。这是一个偏向"不丢数据"的顺序选择。

**触发的 KV 操作：** 每行 1 次 `get`；命中的行 1 次 `put`，主键变更时再 +1 次 `del`；`next_rowid` 增长时 +2 次目录 `put`。

#### DELETE

1. **定位表** + 打开。
2. **全表扫描** + `WHERE` 过滤（同上）。
3. **删除**：`sql_kv_del(sess, tb, rid)`，计数 `ndeleted++`。

> **注意：删除是"留空洞"的。** `schema->next_rowid` 不回退，被删掉的 rowid 不会重用。所以 `DELETE FROM t` 之后再 `SELECT`，扫描仍然会遍历 `1..next_rowid-1`，只不过每个 `get` 都返回"不存在"。

**触发的 KV 操作：** 每行 1 次 `get`，命中的行 1 次 `del`。

#### DROP TABLE（补充）

`exec_drop_table()` 有一个容易忽略的关键点：

```c
/*
 * Remove the rows before the table disappears: row ids start at
 * one again for a table created later under the same name, so
 * leaving the rows behind would resurrect them.
 */
limit = tb->schema->next_rowid;
for (rid = 1; rid < limit; rid++)
    sql_kv_del(sess, tb, rid);
```

**必须显式删掉所有行**，因为新建同名表时 `next_rowid` 从 1 重新开始，残留的旧行会被"复活"。

### 3.5 WHERE 条件的求值流程

#### 三值逻辑

```c
#define	TRI_FALSE	0
#define	TRI_TRUE	1
#define	TRI_UNKNOWN	(-1)
```

两个入口分工明确：

| 函数 | 返回 | 用途 |
|---|---|---|
| `sql_expr_eval(sess, schema, row, expr, res)` | `SQL_VALUE` | 求**值**（投影、算术、`SET` 右值） |
| `sql_expr_test(sess, schema, row, expr, &tv)` | `TRI_*` | 求**真假**（`WHERE`、`AND/OR` 操作数） |

`sql_expr_test` 就是 `sql_expr_eval` 外面包一层 `tri_from_value()`：

```c
static int tri_from_value(const SQL_VALUE *v) {
	if (v == NULL || v->isnull) return TRI_UNKNOWN;
	if (SQL_TYPE_IS_NUMERIC(v->type)) {
		if (sql_value_numeric(v, &d) != SQL_OK) return TRI_TRUE;
		return (d != 0 ? TRI_TRUE : TRI_FALSE);
	}
	if (is_stringish(v)) return (v->slen != 0 ? TRI_TRUE : TRI_FALSE);
	return (v->ival != 0 ? TRI_TRUE : TRI_FALSE);
}
```

`UNKNOWN` 如何表示成 `SQL_VALUE`？—— **一个 `isnull = 1` 的整数值**：

```c
static int result_bool(SQL_VALUE *res, int val) {
	ret = sql_value_set_int(res, SQL_T_INT, (int64)val);
	if (val == TRI_UNKNOWN)
		res->isnull = 1;      /* UNKNOWN 就是 NULL */
	return SQL_OK;
}
```

这与 SQL 标准一致：布尔表达式的 `UNKNOWN` 就是布尔类型的 `NULL`。

#### 各节点的求值规则

| 节点 | 规则 |
|---|---|
| `EX_LITERAL` | 直接 `sql_value_copy(&expr->lit, res)`。 |
| `EX_COLUMN` | `sql_schema_findcol(schema, expr->lname, &indx)` → `sql_value_copy(&row[indx], res)`。**这里是名字绑定的真正发生点**。 |
| `EX_UNARY` | 操作数 NULL → 结果 NULL；否则取数值取负，保持原类型（浮点回浮点，整数回整数）。 |
| `EX_BINARY` 比较 | **任一侧 NULL → `TRI_UNKNOWN`**（不抛错）。否则 `eval_compare()` 得 `cmp`，再按 `op` 映射成真假。 |
| `EX_BINARY` 算术 | 任一侧 NULL → 结果 NULL；`/` 和 `%` 除以 0 → **结果 NULL**（不报错，这也是 SQL 的常见行为）。 |
| `EX_BINARY` AND | `FALSE` 优先（短路）：有 FALSE → FALSE；否则有 UNKNOWN → UNKNOWN；否则 TRUE。 |
| `EX_BINARY` OR | `TRUE` 优先：有 TRUE → TRUE；否则有 UNKNOWN → UNKNOWN；否则 FALSE。 |
| `EX_NOT` | UNKNOWN → UNKNOWN；否则取反。 |
| `EX_ISNULL` | 只看 `isnull`，**永不产生 UNKNOWN**。`negate` 时取反。 |
| `EX_IN` | 左侧 NULL → UNKNOWN。逐个比较：命中 → TRUE；**遇到 NULL 元素先记 `unknown = 1` 继续**；全部不命中且有 NULL → UNKNOWN；全部不命中且无 NULL → FALSE。 |
| `EX_BETWEEN` | 左侧 NULL → UNKNOWN。否则展开成 `x >= low AND x <= high`（代码里构造两个临时 `EX_BINARY` 节点复用比较逻辑），再按 AND 的三值表合并。 |
| `EX_LIKE` | 任一侧 NULL → UNKNOWN。否则 `like_match()` 递归匹配。 |
| `EX_FUNC` | 仅支持 `CURRENT_DATE/TIME/TIMESTAMP`，其余返回 `SQL_ERR_FEATURE`。 |

#### `IN` 的三值处理细节

```c
found = 0; unknown = 0;
for (i = 0; i < expr->list->nitems; i++) {
	sql_expr_eval(..., expr->list->items[i], &b);
	if (b.isnull) { unknown = 1; continue; }      /* 记住，但继续找 */
	eval_compare(&a, &b, &cmp);
	if (cmp == 0) { found = 1; break; }           /* 找到了就短路 */
}
if (found)        cmp = TRI_TRUE;
else if (unknown) cmp = TRI_UNKNOWN;
else              cmp = TRI_FALSE;
```

`1 IN (2, NULL)` → FALSE（没有 NULL 也能确定不在其中…… 等等，这里实现是：没找到且 `unknown=1` → UNKNOWN）。严格 SQL 语义下 `1 IN (2, NULL)` 应为 UNKNOWN，本实现返回 UNKNOWN，一致。而 `1 IN (1, NULL)` → 短路命中，返回 TRUE，也符合标准。

#### 比较时的类型亲和（affinity）

`eval_compare()` 实现了 MySQL 风格的**宽松比较**：

```c
if (SQL_TYPE_IS_NUMERIC(a->type) && is_stringish(b)) {
	ret = sql_value_cast(b, SQL_T_DOUBLE, 0, &tmp);   /* 字符串当数字 */
	if (ret == SQL_OK) ok = 1;
} else if (SQL_TYPE_IS_NUMERIC(b->type) && is_stringish(a)) { ... }

if (ok) { /* 两侧都按 double 比 */ }
/* 数字 vs 字符串转换失败，再退回到按类别比较 */
if (SQL_TYPE_IS_TEMPORAL(a->type) && is_stringish(b)) {
	if (sql_value_cast(b, a->type, 0, &tmp) == SQL_OK) { ... }
}
/* 兜底 */
*cmp = sql_value_cmp(a, b);
```

顺序是：**数字 ↔ 字符串先试数字比 → 时间 ↔ 字符串先试时间比 → 兜底按类别定序**。所以 `WHERE joined > '2024-01-01'` 能正确按日期比，而 `WHERE name > 5` 会把 `name` 当数字比。

#### LIKE

```c
static int like_match(const unsigned char *s, size_t slen,
    const unsigned char *p, size_t plen) {
	if (plen == 0) return (slen == 0 ? 1 : 0);
	if (slen == 0) {
		for (j = 0; j < plen; j++)
			if (p[j] != '%') return 0;      /* 只有全 % 能匹配空串 */
		return 1;
	}
	if (*p == '%') {
		for (i = 0; i <= slen; i++)         /* 回溯：试每个切分点 */
			if (like_match(s + i, slen - i, p + 1, plen - 1)) return 1;
		return 0;
	}
	if (*p == '_' || tolower(*p) == tolower(*s))
		return (like_match(s + 1, slen - 1, p + 1, plen - 1));
	return 0;
}
```

- 支持 `%`（任意长度）和 `_`（单字符）。
- **大小写不敏感**（`tolower` 双侧比较）。
- 不支持 `ESCAPE` 子句。
- **递归回溯实现**：`%` 会尝试所有切分点，最坏情况是指数复杂度。见 §6.1。

---

## 第四部分：关键设计决策

### 4.1 为什么行存储用这种 Key 格式？

**Key = 8 字节大端 rowid。**

理由逐条：

1. **引擎能力约束，也是最根本的原因。** `sql_internal.h` 里写得很直白：
   ```c
   /*
    * There is no table level cursor in the engine yet, so a full scan walks
    * the synthetic row ids, which are encoded as big endian 64 bit values.
    */
   ```
   SQL 层拿不到"遍历整张表"的游标，只能按 Key 做点查（`get`/`put`/`del`）。要让扫描成为可能，就必须**自己构造一个可枚举的 Key 空间** —— 单调递增的整数是最简单的选择。

2. **大端（网络字节序）让字典序 == 数值序。** B+ 树按字节比较 Key，大端整数 1、2、3… 的字节序与数值序一致，因此：
   - 表内记录在物理上**按插入顺序聚簇**；
   - 将来一旦能用上游标，范围扫描（`rowid BETWEEN a AND b`）直接可用，无需任何转换；
   - 数据文件与字节序无关，可跨机器搬运。

3. **定长 8 字节 Key。** 让每次点查的 Key 构造就是一次栈上 `memset` + 8 次移位（`datum_key()`），没有堆分配，扫描热路径上非常轻。

4. **rowid 从 1 开始、单调递增、永不重用。** 带来三个性质：
   - 扫描上界就是 `schema->next_rowid`，不需要额外的"最大 rowid"索引；
   - 删除只留空洞，不做行搬迁，删除代价 O(1)；
   - 行的**插入顺序 = rowid 顺序**，所以无 `ORDER BY` 时结果天然按插入序，符合用户直觉（SQLite 的 `rowid` 也是这个思路）。

   代价是删除很多行后扫描会读很多空 Key（见 §6.1）。

5. **Value 自描述（带列数、逐列 presence + length）。** 代价是每行多几个字节，换来：
   - 解码能校验 `ncols == schema->ncols`，**schema 不匹配立刻报错而非静默错位**；
   - 定长/变长统一处理，解码循环不分支；
   - NULL 只占 1 字节。

**为什么不把主键值直接当 Key？** 对于整数主键，本实现**实际上就是这么做的**（见 §4.5）—— `rowid = 主键值`。区别只在于：无主键或非整数主键时，回退到合成的 `next_rowid`。这样"主键查询 = 一次点查"这个最大收益拿到了，同时不必为非整数主键设计一套 Key 编码规则。

### 4.2 为什么元数据存在 KV 里而不是单独文件？

`sql_catalog.c` 的注释给了直接答案：

```c
/*
 * The VanDB engine does not remember which tables exist in a store: a
 * table is simply a directory-less set of files opened by name and the
 * recovery log refers to tables by their id.  The SQL layer therefore
 * keeps its own catalog in a dedicated table:
 *
 *	key   "__van_sql_tables"	-> newline separated table names
 *	key   <table name>	-> serialized schema (see below)
 */
```

具体理由：

1. **引擎不提供"列出所有表"的能力。** 表是按名字打开的，恢复日志按 id 引用表，引擎自己没有表清单。SQL 层必须自己记，而"记在哪里"只有两个选择：单独文件，或者 KV。

2. **复用引擎的事务与恢复能力。** 这是最大的收益。写进 KV 的目录变更会走引擎的日志与恢复路径、享有一致性保证；而单独文件需要**自己实现一套 WAL / 原子重命名 / 崩溃恢复**，等于在 SQL 层重做一遍引擎已经做过的事。

3. **不引入第二套持久化机制。** 备份、复制、快照只需要处理引擎的数据文件。若目录是单独文件，所有这些工具都要额外记得带上它，且要保证两者版本一致 —— 这是真实项目里常见的一致性问题来源。

4. **天然的原子写入粒度。** 一次 `cat_put` 就是一个 KV 写入，schema 作为一个整体值写入，不存在"写了一半的 schema"。

**代价与应对：**

- 目录本身占用一个保留表名 `__van_sql_catalog`，并由 `SQL_RESERVED_PREFIX`（`"__van_"`）在 `sql_table_alloc()` 里拦截用户的同名表。
- 读目录要一次 `get`。因为 `sql_catalog_load()` 在会话打开时一次性把所有 schema 读进内存并缓存到 `sess->tables[]`，运行期不再读目录（除非 `next_rowid` 增长要回写）。

**为什么 schema 序列化成文本而不是二进制？** 注释里说：

```c
/*
 * The serialized schema is text so that it stays readable in the data
 * files, one record per line, fields separated by tabs:
 *
 *	V<version>
 *		T<table name>
 *		C<name>\t<type id>\t<length>\t<not null>\t<pk>\t<has default>\t<hex>
 *		R<next row id>
 */
```

调试价值是第一位的：用 `strings`/`grep` 就能在出问题时看到表结构，不必写专门的 dump 工具。同时文本格式天然带版本号（`Vvansql1`），反序列化第一步就校验：

```c
case 'V':
	if (nf != 1 || strcmp(fields[0] + 1, SQL_SCHEMA_VERSION) != 0)
		goto err;      /* 版本不匹配 → SQL_ERR_CATALOG */
```

这给了将来做格式迁移的入口。代价是解析开销与体积，但 schema 只在 DDL 和会话启动时读写，不在热路径上。

`DEFAULT` 值（一个任意的 `SQL_VALUE`）在文本里怎么存？—— 十六进制：

```c
/*
 * The catalog stores DEFAULT values as text, so a value is encoded as
 *   [1 byte isnull][1 byte type id][payload]
 * and the whole thing is printed as hexadecimal digits.  The payload is
 * the 8 byte big endian magnitude for numbers, a length prefixed byte
 * string otherwise.
 */
```

这样 DEFAULT 里即使有 TAB、换行、非 UTF-8 字节，也不会破坏"Tab 分隔、换行分记录"的文本结构。

### 4.3 为什么类型系统这么设计？

1. **枚举值是持久化格式的一部分。** 头文件里明确警告 "do not reorder or renumber them"。这让类型 id 可以直接写进 schema 文本，代价是失去了重排的自由 —— 换来的是序列化/反序列化不需要映射表。

2. **14 个类型，覆盖而不是精确。** 支持 INT/BIGINT/SMALLINT、VARCHAR/CHAR/TEXT、FLOAT/DOUBLE、DATE/TIME/TIMESTAMP、BINARY/VARBINARY/BLOB。这是"够用"的集合，没有 DECIMAL、没有 BOOLEAN（用整数）。

3. **运行期表示只有三种**（`ival` / `dval` / `sval`），14 个类型映射到这三种：
   - 减少了解码路径的分支；
   - 时间类型统一是 `int64`（DATE = 距 1970-01-01 的天数，TIME = 距午夜的微秒数，TIMESTAMP = 距 epoch 的毫秒数）。**选整数而不是 `struct tm`，是为了让时间比较退化成整数比较**，排序与索引都能直接用；
   - 代价是 TIME 和 TIMESTAMP 精度不同（微秒 vs 毫秒），这是有意的：TIMESTAMP 用毫秒已经够用且 8 字节能表示到很远，TIME 用微秒才能表达一天内的精确时刻。

4. **`sql_value_cast` 是唯一的转换入口**，且**允许 `dst` 与 `src` 别名**：
   ```c
   /*
    * Convert 'src' to 'type'.  'dst' may alias 'src'.  ...
    */
   ```
   实现方式是先 `sql_value_copy(src, &tmp)` 再转换。这条契约让 UPDATE 能安全地写出 `sql_value_cast(&conv, ..., &vals[cindx])` 这种"就地转换"的代码，调用方不必准备临时变量。

5. **转换是"严格但宽容"的**：
   - 字符串 → 数字：解析失败报 `SQL_ERR_CONVERT`（这是 `eval_compare` 里"退化到类别比较"的依据）；
   - 数字 → 字符串：先 `tostring` 再套长度限制，**超长报 `SQL_ERR_TYPE`（不截断）**。这是刻意的选择：静默截断会丢数据，而 SQL 标准里超长本就是错误；
   - 二进制不参与时间/数字转换，直接 `SQL_ERR_TYPE`；
   - NULL 一律穿透：任何 `cast_to_*` 看到 `src->isnull` 就返回目标类型的 NULL。

6. **声明长度只在字符串/二进制上强制**，且上限 `SQL_MAX_CHARLEN = 65535`。注释说明长度单位是字节而非字符：**"the layer has no character set support"** —— 没有字符集支持，所以不假装能做字符计数。

### 4.4 NULL 的语义是怎么处理的？

**表示：** `SQL_VALUE.isnull` 标志位 + 保留 `type`。

```c
int sql_value_set_null(SQL_VALUE *val, sql_type_t type) {
	sql_value_clear(val);
	val->type = type;          /* 仍然记得是什么类型 */
	val->isnull = 1;
	return SQL_OK;
}
```

**为什么 NULL 还要带类型？** 因为很多操作需要知道"这个空位按什么类别处理"：编码时要知道不用写 payload、比较时要知道归入 `SQL_CMP_NULL` 类、`ORDER BY` 需要稳定定序。如果 NULL 没有类型，这些地方都要靠外部传入列类型，接口会变复杂。

**传播规则：**

| 场景 | 行为 |
|---|---|
| 算术 `+ - * / %` | 任一操作数 NULL → 结果 NULL |
| 比较 `= != < <= > >=` | 任一操作数 NULL → **UNKNOWN**（不是 FALSE） |
| `AND` | FALSE 优先，其次 UNKNOWN |
| `OR` | TRUE 优先，其次 UNKNOWN |
| `NOT` | UNKNOWN → UNKNOWN |
| `IS NULL` / `IS NOT NULL` | **永不为 UNKNOWN**，只返回 TRUE/FALSE |
| `WHERE` | **只有 TRUE 通过**；FALSE 与 UNKNOWN 都过滤掉 |
| `CAST(NULL AS T)` | 仍是 T 类型的 NULL |
| 除零 `/ 0`、`% 0` | 结果 NULL（不报错） |

**排序：** NULL 排在最前。

```c
/*
 * Compare two values, -1 / 0 / 1.  NULL sorts before everything else,
 * then numbers, temporal values, strings and finally binary blobs.
 */
#define	SQL_CMP_NULL		0
#define	SQL_CMP_NUMERIC		1
#define	SQL_CMP_TEMPORAL	2
#define	SQL_CMP_STRING		3
#define	SQL_CMP_BINARY		4
```

同一类别内按自然序比较；**跨类别按类别号定序**，保证 `sql_value_cmp` 是一个全序（total order），这样 `qsort` 和 DISTINCT 去重都有确定的行为。

同类别内的细节：
- 数字：只要有一侧是浮点就按 `double` 比，否则按 `int64` 比（避免精度损失）；
- 时间：**只有同类型才按整数比**，否则落到下面的字符串比较分支（用 ISO 文本表示，恰好按时间序排列）—— 注释说得很清楚：*"everything else falls back to the ISO text representation which sorts chronologically"*；
- 字符串/二进制：逐字节比较，等长前缀后按长度定序。

**输出：** `sql_resultset_print()` 把 NULL 渲染成**空字段**（宽度按 0 计），非 NULL 的空串才渲染成 `NULL` 字面量之外的东西。这是从冒烟测试输出可以看到的行为。

### 4.5 主键的作用和约束是怎么实现的？

**核心设计：整数主键 = rowid 别名。** 这是向 SQLite 借的思路。

```c
int		pk_indx;	/* Rowid alias column, -1 none	*/
```

**建立时**（`build_schema()`）：

```c
if (src->primary_key) {
	if (schema->pk_indx >= 0) {
		/* Composite keys need an index. */
		return SQL_ERR_FEATURE;      /* 复合主键直接拒绝 */
	}
	schema->pk_indx = (int)i;
}
/* A rowid alias is only possible for integer primary keys. */
if (schema->pk_indx >= 0 && !SQL_TYPE_IS_INT(schema->cols[schema->pk_indx].type))
	schema->pk_indx = -1;
```

两条规则：
1. **复合主键 → `SQL_ERR_FEATURE`。** 因为复合键无法映射到单个 rowid，必须有真正的索引结构才能做唯一性检查。
2. **非整数主键 → `pk_indx = -1`。** 主键标记保留在 `col->primary_key`（信息性），但**不做 rowid 别名、不做唯一性检查**。`sql_schema_deserialize()` 里有同样的收敛逻辑。这是一个**诚实降级**：不假装支持，也不因此报错。

**插入时**（`insert_one_row()`）的作用：

| 情况 | 行为 |
|---|---|
| 主键列给了正整数值 | `rowid = 该值` |
| 主键列给了 NULL 或未提供 | `rowid = next_rowid`，并**回填到主键列**（所以 `SELECT id` 能看到自动分配的值） |
| 主键列给了 ≤ 0 或非整数 | `SQL_ERR_TYPE` |
| 该 rowid 已被占用 | `SQL_ERR_DUP_KEY`（通过 `rowid_key_free()` 预检） |

**唯一性是怎么保证的？** —— **靠 rowid 在 KV 里天然唯一**。不需要额外的唯一索引，不需要扫描查重：主键冲突的检查就是一次 `get`，代价 O(log N)。这是"主键即 rowid"最大的收益。

**更新时**：改主键等于改 rowid，所以走"put 新 Key + del 旧 Key"，并做同样的冲突预检（`newrid != rid && rowid_key_free(...) == 0` → `SQL_ERR_DUP_KEY`）。

**代价：** 主键值被限制为正整数。这与标准 SQL 的"任意类型主键"不同，但换来的是零额外存储的主键索引。

### 4.6 其他值得记录的实现取舍

**词法器字符串扫描用两个游标。** `sql_lex_quoted()` 先用一个游标 `i` 做"计数扫描"（算出需要多大缓冲、并定位到闭合引号之后），再用另一个游标 `j` 做"拷贝扫描"（实际解码转义）：

```c
	i = 1;		/* Skip the opening quote. */
	n = 1;		/* Room for the trailing NUL. */
	while (i < end) { ... 计数 ... }        /* 第一趟：定长 */
done:
	res = sql_calloc(n + 1, 1);
	/*
	 * 'i' already points past the closing quote, the copy pass uses
	 * its own cursor.
	 */
	for (j = 1; j < end;) { ... 拷贝 ... }  /* 第二趟：解码 */
	*consumedp = i;                         /* 消费量仍用 i */
```

**为什么？** 因为"跳过起始引号"后，同一个游标既要负责定位（找到闭合引号、告诉调用方消费了多少字符），又要负责拷贝（写入结果缓冲）。混用一个游标会让写入位置和读取位置互相干扰，导致字符串内容错乱（这个项目实际踩过这个坑：含字符串字面量的 INSERT 报"非法记号"）。分成两个职责单一的游标后，语义清晰且不易出错。

**`qsort_r` 不可移植，用文件级静态变量传上下文。**

```c
/*
 * qsort_r is not portable, so the comparator reads its context from
 * these two variables.  Executing statements is not re-entrant anyway.
 */
static const SQL_ORDER_ITEM *sort_order;
static size_t sort_norder;
```

前提（"执行语句本身不可重入"）在注释里写明，是个显式接受的限制。

**惰性打开表。** `sql_catalog_load()` 只登记句柄，真正 `van_table_open()` 推迟到 `sql_table_ensure_open()` 首次访问。会话打开一个有 100 张表的库时，不会去开 100 个物理表。

**`sql_expr_eval` 的 `row` 参数可为 `NULL`。** 这让同一个函数既能求行上下文相关的表达式（`WHERE age > 25`），又能求独立表达式（`LIMIT 3`、INSERT 的 `VALUES`）。代价是 `EX_COLUMN` 在 `row == NULL` 时返回 `SQL_ERR_NO_COLUMN`。

---

## 第五部分：KV 层交互

### 5.1 用到的引擎接口

SQL 层通过 `sql_catalog.c` 单向使用引擎。用到的接口：

**store 层（`VAN_STORE`）：**

| 调用 | 位置 | 用途 |
|---|---|---|
| `van_store_create(&store)` | `sql_session_open()` | 创建 store 对象 |
| `store->open(store, dir, 0)` | `sql_session_open()` | 打开数据库目录 |
| `store->close(store)` | `sql_session_close()` | 关闭（**注意：会释放 store 结构本身**） |

**table 层（`VAN_TABLE`）：**

| 调用 | 位置 | 用途 |
|---|---|---|
| `van_table_create((VAN_STORE *)sess->store, &table)` | `sql_table_open()` | 创建表对象 |
| `table->set_inmem_threshold(table, 64MB)` | `sql_table_open()` | 设置内存树阈值 |
| `table->open(table, kvname, VAN_TABLE_SKIP_THREAD_START)` | `sql_table_open()` | 打开物理表 |
| `table->start_threads(table)` | `sql_table_open()` | 启动后台线程 |
| `table->close(table, 0)` | `sql_table_close()` | 关闭表 |
| `table->put(table, &key, &val, 0)` | `sql_kv_put()` / `cat_put()` | 写入 |
| `table->get(table, &key, &val, 0)` | `sql_kv_get()` / `cat_get()` | 读取 |
| `table->del(table, &key, 0)` | `sql_kv_del()` / `cat_del()` | 删除 |

**datum / 内存：**

| 调用 | 用途 |
|---|---|
| `VAN_DATUM` | Key/Value 的通用容器（`data`/`size`/`ulen`/`flags`） |
| `VAN_MALLOC` 标志 | 让引擎为 `get` 的结果分配内存 |
| `_van_datum_copyfree(&val)` | 释放引擎分配的 datum |
| `_van_free(NULL, p)` | `sql_kv_free()` 释放 `get` 返回的数据 |
| `VAN_NOTFOUND` / `VAN_DELETED` | 判"键不存在"，**不算错误** |

**关键契约：** `sql_kv_get()` 把 `VAN_NOTFOUND`/`VAN_DELETED` 翻译成 `SQL_OK` + `*datap == NULL`：

```c
ret = tb->table->get(tb->table, &key, &val, 0);
if (ret == VAN_NOTFOUND || ret == VAN_DELETED) {
	if (val.data != NULL) _van_datum_copyfree(&val);
	return SQL_OK;          /* 行不存在：不是错误，*datap 保持 NULL */
}
```

这个约定让上层扫描循环只需要 `if (!found) continue;`，不必区分"空表"、"删过的行"和"读失败"。

### 5.2 每次 SQL 操作对应的 KV 操作

设 `N = schema->next_rowid - 1`（已分配过的 rowid 数），`M` = 命中行数。

| SQL | KV 操作 | 复杂度 |
|---|---|---|
| `CREATE TABLE` | 目录 2 × put（schema + 表清单）；另建一个物理表 | O(1) |
| `DROP TABLE` | `N` × del（清空所有行）；目录 1 × del + 1 × put（表清单） | O(N log N) |
| `INSERT`（1 行，有整数 PK） | 1 × get（查重）+ 1 × put；`next_rowid` 增长时 + 目录 2 × put | O(log N) |
| `INSERT`（1 行，无 PK） | 1 × put；`next_rowid` 增长时 + 目录 2 × put | O(log N) |
| `INSERT`（k 行） | 上述 × k | O(k log N) |
| `SELECT`（无 WHERE） | `N` × get | **O(N log N)** |
| `SELECT`（有 WHERE） | `N` × get（`WHERE` 在 SQL 层过滤，**不下推**） | **O(N log N)** |
| `UPDATE` | `N` × get + `M` × put（`M` 中主键变更的再 +1 × del） | O(N log N) |
| `DELETE` | `N` × get + `M` × del | O(N log N) |
| 会话首次打开 | 目录 1 × get（表清单）+ `T` × get（每张表的 schema） | O(T) |

**三个必须知道的事实：**

1. **任何带 WHERE 的语句都是全表扫描。** 没有索引，`WHERE` 永远在 SQL 层逐行求值。
2. **删过的行照样被读一次。** rowid 不重用，`DELETE FROM t` 之后 `SELECT` 仍然遍历 `1..N`，每个 `get` 都返回"不存在"。
3. **`next_rowid` 的回写是"整个 schema 重写一次"。** 每次自增都要 `sql_schema_serialize()` + 一次目录 put + 一次表清单 put。批量插入多行时，只有**最后**超过当前 `next_rowid` 的那次会触发（因为 `if (rowid >= schema->next_rowid)`），所以 `INSERT ... VALUES (...), (...), (...)` 通常只回写一次。

### 5.3 元数据与用户数据如何区分

三重隔离：

| 机制 | 实现 | 位置 |
|---|---|---|
| **独立的物理表** | 元数据全在 `__van_sql_catalog` 表里，用户表各占一个同名物理表 | `SQL_CATALOG_TABLE` |
| **名字前缀保留** | `sql_table_alloc()` 拒绝任何以 `__van_` 开头的表名 | `SQL_RESERVED_PREFIX` |
| **两套 Key 空间** | 用户数据按 **8 字节大端 rowid** 寻址（`sql_kv_put/get/del`）；元数据按 **字符串名** 寻址（`cat_put/get/del`） | 见下 |

```c
/* 用户数据：Key 是 8 字节 rowid */
static void datum_key(uint64 rowid, VAN_DATUM *key, unsigned char *keybuf) {
	sql_rowkey_encode(rowid, keybuf);
	key->data = keybuf;
	key->size = SQL_KVSIZE;    /* sizeof(uint64) == 8 */
	...
}

/* 元数据：Key 是字符串名 */
k.data = (void *)key;
k.size = strlen(key);
```

两套 Key 空间**物理上不在同一个表**，所以不会冲突；即便在同一个表里，8 字节定长 Key 与变长字符串 Key 也不会碰撞。

目录内部还分两层键：

```
"__van_sql_tables"  →  "users\nmetrics\nsales\n"     （表清单，换行分隔）
"users"             →  "Vvansql1\nTusers\nCid\t2\t..."（某个表的 schema）
```

`__van_sql_tables` 本身也是以 `__van_` 开头的，因此它**不会**被误当成用户表条目 —— 这个设计让"遍历所有表"和"读某张表"用同一套 `cat_get` 完成，不需要为清单特判。

---

## 第六部分：已知限制和未来扩展

### 6.1 当前不支持哪些 SQL 特性？为什么不支持？

| 不支持 | 原因 |
|---|---|
| **JOIN** | 需要多表扫描与连接算法；当前 `SQL_STMT` 只有一个 `tablename`，执行器的 `row` 是单个表的 `SQL_VALUE[]`。 |
| **子查询** | 需要 `EX_SUBQUERY` 节点且求值器要能递归执行语句；当前 `sql_expr_eval` 拿不到执行上下文。 |
| **聚合函数 / GROUP BY / HAVING** | 需要分组阶段与聚合状态机；当前 `exec_select` 是"逐行投影并收进数组"的单趟流程。 |
| **复合主键** | 无法映射到单个 rowid。代码注释：*"Composite keys need an index."* → 返回 `SQL_ERR_FEATURE`。 |
| **非整数主键的唯一性** | 需要二级索引才能做唯一检查；当前降级为"仅信息性"，`pk_indx = -1`。 |
| **ALTER TABLE** | 需要重写全表数据并做 schema 迁移（旧行是 2 字节列数 + 旧列布局）。 |
| **CREATE INDEX / 索引** | 需要索引的持久化、维护（每次 DML 同步更新）与查询期选择（ planner ）。 |
| **VIEW、权限、事务 SQL**（`BEGIN`/`COMMIT`） | 超出"最小 SQL 层"范围；事务能力由引擎提供，但未在 SQL 层暴露。 |
| **`ESCAPE` 子句、正则** | LIKE 只支持 `%` 和 `_`。 |
| **`NULLS FIRST/LAST`** | NULL 恒排最前，由 `SQL_CMP_NULL = 0` 决定。 |
| **UNIQUE / CHECK / FOREIGN KEY** | 除 PK（整数）与 NOT NULL 外无其他约束。 |
| **多语句原子性** | `exec_insert` 注释明确：*"there is no statement level rollback in this layer"*。多行 INSERT 中途失败，已写入的行保留。 |
| **预处理语句 / 参数绑定** | 每次执行都重新 lex + parse。 |
| **字符集与排序规则** | 长度按字节计（注释：*"the layer has no character set support"*）。 |
| **复合主键、多列 IN、标量子查询** 等语法细节 | 解析器未实现。 |

**已知的性能/正确性隐患：**

1. **全表扫描的 get 次数。** 每行一次 `get`，删过的行也读一次。
2. **`LIKE` 的指数回溯。** `like_match()` 对 `%` 尝试所有切分点，`'%a%a%a%a%a%a%a'` 这类模式会退化。生产化前应改成 DP 或双指针算法。
3. **`exec_select` 全量物化。** 所有命中行的投影 + 排序键都在内存里，没有流式输出或 top-N 优化。`SELECT * FROM huge_table ORDER BY x LIMIT 10` 也会把所有行读进内存再排序。
4. **DISTINCT 是 O(n²)。** 每行都要与已收集的所有行逐列比较。
5. **`SQL_MAX_ROWS_DEFAULT`（1024×1024）已定义但当前未强制。** `SQL_MAX_IDENT_LEN`（128）和 `SQL_MAX_COLUMNS`（256）在 parser 与 catalog 里是强制的。
6. **`qsort` 上下文用文件级静态变量**，语句执行不可重入（已在注释中声明）。

### 6.2 如果要加 JOIN / 子查询 / 聚合，需要改哪些地方？

#### JOIN

| 改动点 | 具体内容 |
|---|---|
| `SQL_STMT` | 增加 `from_items` 列表（表名 + 别名 + 连接类型 + ON 条件），替换单一 `tablename`。 |
| `sql_parse.c` | 新增 `parse_from()`；`parse_primary` 要支持**限定名** `t.col`。 |
| 名字解析 | `sql_schema_findcol()` 现在签名是单 schema；需要改成"在多 schema 上下文中按限定名/别名查找"，并处理歧义（已有 `SQL_ERR_AMBIGUOUS` 错误码预留）。 |
| 行表示 | `exec_select` 里的 `SQL_VALUE *vals` 是单表一行；需要扩展成"每个源表一个 `SQL_VALUE[]`"，或引入带来源标记的行结构。 |
| 执行算法 | 新增连接算子。当前**没有游标**，最简单可行的方案是**嵌套循环**：外表全表扫描（`rowid = 1..N`），内表也全表扫描并对每行求值 `ON`。若先加上游标（见 §6.3），可做索引嵌套循环。 |
| 投影/排序 | `projection_name()` 与 `sql_expr_eval()` 的 `schema` 参数要变成 schema 集合。 |

**建议：** 先把 `sql_expr_eval` 的 `schema` 参数抽象成一个"求值上下文"（`struct eval_ctx { 表列表, 当前各表行 }`），这是一切多表特性的共同前置改动。

#### 子查询

| 改动点 | 具体内容 |
|---|---|
| AST | 新增 `EX_SUBQUERY` 节点，携带一个完整的 `SQL_STMT *`。 |
| `sql_parse.c` | `parse_primary` 支持 `( SELECT ... )`；`parse_predicate` 支持 `IN ( SELECT ... )` 与 `EXISTS (...)`。 |
| 求值器 | `sql_expr_eval` 需要能执行语句 —— 即需要访问 `SQL_SESSION`。签名里已经有 `sess`，这点**已经预留好了**；真正缺的是"执行子查询并把结果作为值/集合返回"的逻辑。 |
| 相关子查询 | 需要把外层行上下文传进内层求值，即求值上下文要能嵌套。 |
| 标量与集合 | 标量子查询返回单个 `SQL_VALUE`；`IN (subquery)` 需要展开成值列表或做存在性判断。 |

**建议：** 先做**非相关**子查询（可独立执行一次、结果缓存），相关性支持留到求值上下文重构之后。

#### 聚合 / GROUP BY

| 改动点 | 具体内容 |
|---|---|
| AST | `SQL_STMT` 增加 `group_by`（`SQL_EXPR*` 列表）与 `having`（`SQL_EXPR*`）；`SQL_SELECT_ITEM` 需要标记"是否含聚合"。 |
| `EX_FUNC` | 现在只支持 `CURRENT_*`；需要扩展为通用函数调用（名称 + 参数列表），并区分"标量函数"与"聚合函数"。 |
| 聚合状态 | 新增聚合累加器（`COUNT`/`SUM`/`MIN`/`MAX`/`AVG`），分组键用 `sql_value_cmp` 比较（已有全序，可直接用）。 |
| **执行流程重构** | 这是最大的改动。当前 `exec_select` 是单趟"扫描→过滤→投影→去重→排序→分页"；聚合需要在**投影之前插入分组阶段**，且 `HAVING` 在分组后过滤、`ORDER BY`/`LIMIT` 在最后。 |
| 校验 | 需要检测"SELECT 列表中非聚合列不在 GROUP BY 中"这类语义错误 —— 这类检查正是 §1.2 里说的"缺失的 Analyzer"该做的事。 |

**建议：** 借这个机会把 `exec_select` 拆成流水线（见下）。

### 6.3 如果要加索引，架构上需要怎么调整？

**当前最大的障碍：SQL 层没有范围扫描能力。** 它只做点查。而**引擎其实是有游标的** —— `include/van_inmem_tree.h` 有 `INMEM_TREECURSOR`、`include/van_ondisk_tree.h` 有 `ONDISK_TREECURSOR`，都提供 `get`/`put`/`del`。SQL 层目前完全没用它们。

所以加索引的路线分两步：

**第一步：打通范围扫描**

1. 在 `sql_catalog.c` 增加基于游标的扫描接口（如 `sql_kv_scan(tb, start_key, end_key, cb)`），封装引擎游标。
2. 让 `exec_select` / `exec_update` / `exec_delete` 在能定位 rowid 区间时改用范围扫描，替代"逐个 `get`"。

这一步**单独就能解决 §6.1 第 1 条性能隐患**（删过的行不再被逐个读取），且不引入任何新语义，是性价比最高的一步。

**第二步：引入二级索引**

| 改动点 | 具体内容 |
|---|---|
| 目录 | schema 增加索引定义（列、唯一性、物理表名）。`sql_schema_serialize()` 要加新的记录行（如 `I<name>\t<cols>\t<unique>\t<kvname>`），并**提升 `SQL_SCHEMA_VERSION`** 到 `vansql2`（反序列化已有版本校验入口）。 |
| 物理存储 | 每个索引一个独立 KV 表，Key = `编码后的列值 + 8 字节 rowid`（保证唯一且可按值范围扫描），Value = 空或回表所需信息。 |
| 维护 | `insert_one_row` / `exec_update` / `exec_delete` 每次写行都要同步更新索引。这是正确性风险最集中的地方 —— 建议先把三者统一到一个"写行"函数里，避免遗漏。 |
| 查询改写 | `exec_select` 需要能识别 `WHERE` 里的**可索引谓词**（形如 `col OP 常量`），把"全表扫描 + SQL 层过滤"改写成"索引范围扫描 + 回表"。 |
| 规划 | 需要在 §1.2 说的"缺失的 Analyzer/Planner"位置引入最简单的规则式选择：有可用索引就用，否则全表扫描。 |
| 唯一索引 | 唯一性检查变成"插入前往索引里查一次"，这**顺带解决了 §4.5 里非整数主键不支持唯一约束的问题**。 |

**架构建议（适用于以上三者）：** 在 `sql_exec.c` 之上引入一层**计划（Plan）**，把现在的 `exec_select` 拆成可组合的流水线：

```
Scan（全表 / 索引范围）
  → Filter（WHERE）
  → Join（可选）
  → Group（可选，含聚合）
  → Having（可选）
  → Project（投影）
  → Distinct（可选）
  → Sort（ORDER BY）
  → Limit/OFFSET
  → Materialize（结果集）
```

每个阶段是一个小而独立的函数/结构体，由 `sql_stmt_exec` 按 AST 组装。这样：

- 加 JOIN = 在流水线里插一个阶段；
- 加聚合 = 插 Group/Having 两个阶段；
- 加索引 = 只改 Scan 阶段的选择逻辑，其余阶段不动。

这也是把 §1.2 里"缺失的 Analyzer"补上的自然时机：名字绑定、类型检查、聚合合法性校验都应在**计划构建期**一次性完成，而不是像现在这样在求值期逐行做（`sql_schema_findcol` 每行每列都要线性查一次，这本身也是一处可优化点）。

---

## 第七部分：实现 SQL 层对原有代码的改动

> 本节所有结论均以 git 提交 **`f0f65d2 draf version for sql layer`** 为准，不依赖记忆。
> 复核命令：`git show --name-status --format= f0f65d2`

### 7.1 一句话总览

SQL 层是**纯增量改动**：

- **新增 15 个文件**，共 **8129 行**生产代码（`src/sql` 6910 + 头文件 647 + `cli/` 572）；
- **只修改 1 个原有文件** —— 根 `Makefile`（+32 / -2 行）；
- **引擎 / KV 层零改动**，既有接口一个没动。

这是个值得强调的结果：**VanDB 引擎本身（inmemtree / ondisktree / txn / store / table / log）完全没有为 SQL 层做过任何适配**。SQL 层全部建立在既有 KV API 之上。

### 7.2 新增了哪些文件

#### 头文件（3 个，647 行）

| 文件 | 行数 | 定位 |
|---|---|---|
| `include/van_sql.h` | 245 | **公开 API**。只前向声明 `struct _van_store`/`struct _van_table`，不依赖 `van_int.h`，外部消费者可直接包含。 |
| `include/sql_internal.h` | 271 | **内部共享声明**。包含 `van_int.h` + `van_sql.h`；记号/AST/`SQL_ROW`/目录常量。**不对外安装**。 |
| `include_auto/sql_ext.h` | 131 | 跨模块内部函数声明表。 |

> 三者分层是刻意的：`van_sql.h` → `sql_internal.h` → `sql_ext.h` 构成"公开 / 内部类型 / 内部函数"三级，避免把引擎内部结构泄漏给 API 使用者。

#### `src/sql/` 新增 9 个实现文件（6910 行）

| 文件 | 行数 | 职责 |
|---|---|---|
| `sql_exec.c` | 1723 | 表达式求值 + 六个语句执行器（最大的一个） |
| `sql_parse.c` | 1237 | 递归下降语法分析 |
| `sql_type.c` | 869 | 类型系统、转换、日期时间 |
| `sql_catalog.c` | 837 | 元数据目录 + **唯一的 KV 适配层** |
| `sql_value.c` | 772 | 值语义、比较、结果集、错误码 |
| `sql_lex.c` | 587 | 词法分析 |
| `sql_ast.c` | 354 | 内存分配器 + AST 节点构造 |
| `sql_row.c` | 328 | 行编解码 |
| `sql_session.c` | 203 | 会话管理与语句循环 |

#### 命令行工具（2 个，572 行）

| 文件 | 行数 | 说明 |
|---|---|---|
| `cli/vansql.c` | 549 | 交互式 shell，`-c`/`-f`/`-readonly`/`-column`/`-list`，`.tables`/`.schema`/`.mode`/`.stats` 元命令 |
| `cli/Makefile` | 23 | 独立构建脚本 |

#### 测试脚手架（不计入生产代码）

`tmp_sqltest/` 下新增 19 个 `.sql`（`s2`–`s15`、`sA`–`sD`、`smoke.sql` 等）、10 个 `.sh` 驱动脚本，以及调试用的 `lexdbg.c`。这些是开发期验证用的，不参与构建。

### 7.3 修改了哪些原有文件：**只有根 `Makefile`**

```
$ git show --name-status --format= f0f65d2 | grep -v tmp_sqltest
M	Makefile                 ← 唯一被修改的原有文件
A	cli/Makefile
A	cli/vansql.c
A	include/sql_internal.h
A	include/van_sql.h
A	include_auto/sql_ext.h
A	src/sql/sql_*.c          （9 个）
```

根 `Makefile` 共 5 处改动（+32 / -2）：

**① 新增 `sqlcli` 目标**

```make
# The SQL layer comes with its own command line shell.
sqlcli: libvan.a
	cd cli && $(MAKE)
```

**② `VAN_OBJS` 追加 9 个 SQL 目标文件** —— 让 `libvan.a` 包含 SQL 层

```make
-	 van_table_path.o txn.o
+	 van_table_path.o txn.o \
+	 sql_ast.o sql_catalog.o sql_exec.o sql_lex.o sql_parse.o \
+	 sql_row.o sql_session.o sql_type.o sql_value.o
```

**③ 新增 `SQL_OBJS` 变量**（单独列出 SQL 目标，便于将来只为 SQL 层做增量构建）

```make
SQL_OBJS=	sql_ast.o sql_catalog.o sql_exec.o sql_lex.o sql_parse.o \
	sql_row.o sql_session.o sql_type.o sql_value.o
```

**④ `clean` 目标级联到 `cli/`**

```make
 clean:
 	cd test && make clean && rm -f libvan.a
+	cd cli && make clean
 	rm -fr *.o
 	rm -fr *.a
```

**⑤ 追加 9 条编译规则**（`# src/sql` 段），全部沿用既有 `$(CPPFLAGS) $(CFLAGS)`，没有为 SQL 层引入任何特殊编译选项：

```make
# src/sql
sql_ast.o: src/sql/sql_ast.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $?
...（其余 8 条同式）
```

### 7.4 原来的 KV 层接口有没有变化：**没有**

这是本节最重要的结论。证据与理由：

**证据（已实测）：**

```console
$ git diff --name-only --diff-filter=M f0f65d2^ f0f65d2
Makefile                      ← 整个提交里唯一被修改的文件

$ git diff --name-only --diff-filter=M f0f65d2^ f0f65d2 -- src include include_auto test
                              ← 无输出：引擎、头文件、测试目录零改动
```

`--diff-filter=M` 只挑"修改"、排除"新增"，因此这两条命令不受新增的 SQL 文件干扰。结论：**没有任何一个既有引擎文件被修改** —— `src/common/`、`src/inmemtree/`、`src/ondisktree/`、`src/store/`、`src/table/`、`src/txn/`、`src/log/`、`src/datum/`、以及 `include/` 下所有既有头文件，全部零改动。

**SQL 层只用既有 API**，没有要求引擎新增任何能力：

| 类别 | 用到的既有接口 |
|---|---|
| store | `van_store_create()`、`store->open()`、`store->close()` |
| table | `van_table_create()`、`table->open()`、`table->close()`、`table->put()`、`table->get()`、`table->del()`、`table->start_threads()`、`table->set_inmem_threshold()` |
| datum | `VAN_DATUM`、`VAN_MALLOC`、`_van_datum_copyfree()`、`_van_free()` |
| 返回码 | `VAN_NOTFOUND`、`VAN_DELETED` |
| 标志 | `VAN_TABLE_SKIP_THREAD_START` |

**SQL 层自己引入的约定，引擎完全不感知**：

- 数据 Key = 8 字节大端 rowid —— 编解码全在 `sql_row.c`，引擎只看到一串字节；
- 元数据放在名为 `__van_sql_catalog` 的表里 —— 对引擎而言就是一张普通表；
- `VAN_NOTFOUND` / `VAN_DELETED` 被翻译成"`SQL_OK` + 空结果"—— 这层语义转换在 `sql_catalog.c` 内，是 SQL 层的解释，不是引擎的改动。

> 换句话说：**引擎不知道自己上面跑着一个 SQL 层**。这个解耦正是 §0.3 所说"`sql_catalog.c` 是唯一触碰 `VAN_TABLE` 的模块"带来的直接收益。

**对既有测试的影响：** `test/` 目录未被修改。SQL 目标文件被加入 `libvan.a`，对既有用例而言只是多了符号，不影响原有行为。

### 7.5 编译系统：`cli/Makefile`（新增）的取舍

```make
# Every vanilla object lives in the root of the tree, link them straight
# into the shell so that it does not need the shared libvan.a at run time.
VAN_OBJS= $(sort $(wildcard ../*.o))
CLIBS=	-lpthread -lm
```

**为什么直接链根目录的 `.o`，而不是链 `libvan.a`？**

因为根 `Makefile` 里 `libvan.a` 实际上是这样生成的：

```make
libvan.a: $(VAN_OBJS)
	$(CC) -shared -o $@ $(VAN_OBJS) $(CLIBS)
```

**它是用 `-shared` 造出来的共享库，只是名字叫 `.a`**。链它会导致 `vansql` 运行时依赖这个共享对象，需要处理 `-rpath` 或库搜索路径，部署时容易踩坑。直接链 `.o` 得到的是一个**自包含的可执行文件**，拷到哪都能跑。

代价是 `vansql` 必须等根目录的 `.o` 先编好（由 `sqlcli: libvan.a` 这个依赖保证顺序），且可执行文件体积更大。对一个命令行 shell 来说这个交换是划算的。

另外 `CLIBS` 里比根 `Makefile` 多了 `-lm` —— SQL 层用到了 `fmod()`（`sql_exec.c` 的 `OP_MOD`），需要数学库。

### 7.6 构建与运行时的目录变化

| 阶段 | 变化 |
|---|---|
| 构建 | 根目录新增 9 个 `sql_*.o`；`libvan.a` 体积变大；新增 `cli/vansql.o` 与 `cli/vansql` |
| 运行时（数据目录） | 多出一个 **目录表** `__van_sql_catalog`（元数据）；每张 SQL 表对应一个同名物理表 |
| 清理 | `make clean` 现会级联清理 `cli/` |

### 7.7 如何自行复核

```bash
# 哪些文件是新增，哪些是修改（A=新增 M=修改）
git show --name-status --format= f0f65d2 | grep -v tmp_sqltest

# 根 Makefile 的确切改动
git diff f0f65d2^ f0f65d2 -- Makefile

# 整个提交里"被修改"的文件清单 —— 期望只有一行：Makefile
git diff --name-only --diff-filter=M f0f65d2^ f0f65d2

# 引擎 / 头文件 / 测试目录里"被修改"的文件 —— 期望无输出
git diff --name-only --diff-filter=M f0f65d2^ f0f65d2 -- src include include_auto test

# 确认当前工作区没有未提交的改动
git status --short
```

## 附录 A：错误码

```c
#define	SQL_OK			0
#define	SQL_ERR_LEX		(-1)	/* 非法记号		*/
#define	SQL_ERR_PARSE		(-2)	/* 语法错误		*/
#define	SQL_ERR_NOMEM		(-3)	/* 内存不足		*/
#define	SQL_ERR_NO_TABLE	(-4)	/* 表不存在		*/
#define	SQL_ERR_DUP_TABLE	(-5)	/* 表已存在		*/
#define	SQL_ERR_DUP_KEY		(-6)	/* 主键/唯一冲突	*/
#define	SQL_ERR_NO_COLUMN	(-7)	/* 列不存在		*/
#define	SQL_ERR_AMBIGUOUS	(-8)	/* 名字有歧义		*/
#define	SQL_ERR_TYPE		(-9)	/* 类型不匹配		*/
#define	SQL_ERR_NOT_NULL	(-10)	/* NOT NULL 违反	*/
#define	SQL_ERR_ARG		(-11)	/* 参数错误		*/
#define	SQL_ERR_FEATURE		(-12)	/* 特性未支持		*/
#define	SQL_ERR_STORE		(-13)	/* 存储引擎失败		*/
#define	SQL_ERR_CATALOG		(-14)	/* 目录损坏		*/
#define	SQL_ERR_CONVERT		(-15)	/* 值转换失败		*/
#define	SQL_ERR_LIMIT		(-16)	/* 超出内部限制		*/
#define	SQL_ERR_READONLY	(-17)	/* 会话只读		*/
#define	SQL_ERR_UNKNOWN		(-99)	/* 其他		*/
```

`SQL_ERR_AMBIGUOUS` 目前**预留但未使用**（单表查询不可能有歧义），JOIN 引入后会用到。

## 附录 B：目录持久化格式

```
表: __van_sql_catalog

键 "__van_sql_tables"  值:  "<表名>\n<表名>\n..."         （换行分隔）
键 "<表名小写>"         值:  见下
```

单表 schema 文本：

```
Vvansql1                                  版本，必须匹配
Tusers                                    表名（原始拼写）
Cid\t2\t0\t0\t1\t0\t-                     列：名/类型id/长度/notnull/pk/hasdefault/默认值的hex
Cname\t6\t32\t1\t0\t0\t-                 VARCHAR(32) NOT NULL
Cage\t1\t0\t0\t0\t1\t0101...             有 DEFAULT，hex 编码
R6                                        next_rowid
```

- 字段以 Tab 分隔，一记录一行。
- 无 `DEFAULT` 时第 7 字段写 `-`。
- 类型 id 直接是 `sql_type_t` 的数值（**不可重排**）。

## 附录 C：构建与冒烟测试

```bash
# 构建 SQL 命令行工具（会先构建引擎目标文件，再链接成自包含的 vansql）
make sqlcli

# 在一个干净目录上跑冒烟脚本
rm -rf /tmp/sqlrun && mkdir -p /tmp/sqlrun
cli/vansql -f tmp_sqltest/smoke.sql /tmp/sqlrun

# 交互式
cli/vansql /tmp/sqlrun
```

支持的命令行形式：

```
vansql [-readonly] [-column|-list] [dir [sql ...]]
vansql -c sql [-readonly] [dir]
vansql -f script [-readonly] [dir]
```

交互模式下的元命令：

| 命令 | 作用 |
|---|---|
| `.tables` | 列出所有表 |
| `.schema [表名]` | 显示建表语句（不指定表名则显示全部） |
| `.mode column\|list` | 切换输出格式 |
| `.stats` | 开关行数统计显示 |
| `.help` | 显示帮助 |
| `.quit` / `.exit` / `.q` | 退出 |
