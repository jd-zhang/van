# VanDB SQL 层 · 已知限制与未来扩展计划

> 本文回答"**现在不支持什么、为什么不支持、以后怎么加**"。
> 节号对应关系：本文 §1.x = 主文档 §6.x。

> 完整版本见 [SQL_IMPLEMENTATION.md](SQL_IMPLEMENTATION.md)（主文档，含全部细节与附录）。
>
> | 文档 | 内容 |
> |---|---|
> | [SQL_IMPLEMENTATION.md](SQL_IMPLEMENTATION.md) | 完整实现文档 |
> | [SQL_ARCHITECTURE.md](SQL_ARCHITECTURE.md) | 架构图 + 数据流 |
> | [SQL_DESIGN_DECISIONS.md](SQL_DESIGN_DECISIONS.md) | 设计决策记录 |
> | [SQL_ROADMAP.md](SQL_ROADMAP.md) | 未来扩展计划 |

---

## 已知限制与未来扩展

### 1.1 当前不支持哪些 SQL 特性？为什么不支持？

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

### 1.2 如果要加 JOIN / 子查询 / 聚合，需要改哪些地方？

#### JOIN

| 改动点 | 具体内容 |
|---|---|
| `SQL_STMT` | 增加 `from_items` 列表（表名 + 别名 + 连接类型 + ON 条件），替换单一 `tablename`。 |
| `sql_parse.c` | 新增 `parse_from()`；`parse_primary` 要支持**限定名** `t.col`。 |
| 名字解析 | `sql_schema_findcol()` 现在签名是单 schema；需要改成"在多 schema 上下文中按限定名/别名查找"，并处理歧义（已有 `SQL_ERR_AMBIGUOUS` 错误码预留）。 |
| 行表示 | `exec_select` 里的 `SQL_VALUE *vals` 是单表一行；需要扩展成"每个源表一个 `SQL_VALUE[]`"，或引入带来源标记的行结构。 |
| 执行算法 | 新增连接算子。当前**没有游标**，最简单可行的方案是**嵌套循环**：外表全表扫描（`rowid = 1..N`），内表也全表扫描并对每行求值 `ON`。若先加上游标（见 §1.3），可做索引嵌套循环。 |
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

### 1.3 如果要加索引，架构上需要怎么调整？

**当前最大的障碍：SQL 层没有范围扫描能力。** 它只做点查。而**引擎其实是有游标的** —— `include/van_inmem_tree.h` 有 `INMEM_TREECURSOR`、`include/van_ondisk_tree.h` 有 `ONDISK_TREECURSOR`，都提供 `get`/`put`/`del`。SQL 层目前完全没用它们。

所以加索引的路线分两步：

**第一步：打通范围扫描**

1. 在 `sql_catalog.c` 增加基于游标的扫描接口（如 `sql_kv_scan(tb, start_key, end_key, cb)`），封装引擎游标。
2. 让 `exec_select` / `exec_update` / `exec_delete` 在能定位 rowid 区间时改用范围扫描，替代"逐个 `get`"。

这一步**单独就能解决 §1.1 第 1 条性能隐患**（删过的行不再被逐个读取），且不引入任何新语义，是性价比最高的一步。

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

