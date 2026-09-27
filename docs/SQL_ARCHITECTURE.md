# VanDB SQL 层 · 架构图与数据流

> 本文以**图**为主：分层结构、调用关系、核心数据结构，以及各语句从 SQL 文本到 KV 落盘的数据流。
> 逐步的文字说明见主文档 [SQL_IMPLEMENTATION.md](SQL_IMPLEMENTATION.md)。

> 完整版本见 [SQL_IMPLEMENTATION.md](SQL_IMPLEMENTATION.md)（主文档，含全部细节与附录）。
>
> | 文档 | 内容 |
> |---|---|
> | [SQL_IMPLEMENTATION.md](SQL_IMPLEMENTATION.md) | 完整实现文档 |
> | [SQL_ARCHITECTURE.md](SQL_ARCHITECTURE.md) | 架构图 + 数据流 |
> | [SQL_DESIGN_DECISIONS.md](SQL_DESIGN_DECISIONS.md) | 设计决策记录 |
> | [SQL_ROADMAP.md](SQL_ROADMAP.md) | 未来扩展计划 |

---

## 目录

- [一、总体架构](#一总体架构)
- [二、调用关系总览](#二调用关系总览)
- [三、核心数据结构](#三核心数据结构)
- [四、数据流：CREATE TABLE](#四数据流create-table)
- [五、数据流：SELECT](#五数据流select)
- [六、KV 操作映射总表](#六kv-操作映射总表)

---

## 一、总体架构


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


---

## 二、调用关系总览

### 2.1 主链路调用图


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


---

## 三、核心数据结构


### 3.1 类型系统：`sql_type_t`

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

### 3.2 值的表示：`SQL_VALUE`

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

### 3.3 元数据：`SQL_COLUMN` / `SQL_SCHEMA` / `SQL_TABLE`

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

### 3.4 AST 节点

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

### 3.5 逻辑计划 / 物理计划

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

### 3.6 行的表示：`SQL_ROW` 与线上格式

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

### 3.7 结果集

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

### 3.8 会话

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


---

## 四、数据流：CREATE TABLE

示例语句：

```sql
CREATE TABLE IF NOT EXISTS users (
	id INT PRIMARY KEY,
	name VARCHAR(32) NOT NULL,
	age SMALLINT DEFAULT 18
);
```

### 4.1 数据流图

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


### 4.2 数据结构演变一览


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


---

## 五、数据流：SELECT

示例语句：

```sql
SELECT id, age * 2 AS doubled FROM users WHERE age >= 25 ORDER BY id DESC LIMIT 3;
```

### 5.1 数据流图

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


### 5.2 数据结构演变一览


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


---

## 六、KV 操作映射总表


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

