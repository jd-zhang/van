# VanDB SQL 层 · 设计决策记录

> 本文回答"**为什么这么设计**"，而不是"是什么"。
> 节号对应关系：本文 §1.x = 主文档 §4.x。

> 完整版本见 [SQL_IMPLEMENTATION.md](SQL_IMPLEMENTATION.md)（主文档，含全部细节与附录）。
>
> | 文档 | 内容 |
> |---|---|
> | [SQL_IMPLEMENTATION.md](SQL_IMPLEMENTATION.md) | 完整实现文档 |
> | [SQL_ARCHITECTURE.md](SQL_ARCHITECTURE.md) | 架构图 + 数据流 |
> | [SQL_DESIGN_DECISIONS.md](SQL_DESIGN_DECISIONS.md) | 设计决策记录 |
> | [SQL_ROADMAP.md](SQL_ROADMAP.md) | 未来扩展计划 |

---

## 设计决策

### 1.1 为什么行存储用这种 Key 格式？

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

**为什么不把主键值直接当 Key？** 对于整数主键，本实现**实际上就是这么做的**（见 §1.5）—— `rowid = 主键值`。区别只在于：无主键或非整数主键时，回退到合成的 `next_rowid`。这样"主键查询 = 一次点查"这个最大收益拿到了，同时不必为非整数主键设计一套 Key 编码规则。

### 1.2 为什么元数据存在 KV 里而不是单独文件？

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

### 1.3 为什么类型系统这么设计？

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

### 1.4 NULL 的语义是怎么处理的？

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

### 1.5 主键的作用和约束是怎么实现的？

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

### 1.6 其他值得记录的实现取舍

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

