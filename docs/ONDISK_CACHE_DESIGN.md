# VanDB 块缓存（ondisk cache）设计与实现

> 本文记录块缓存子系统的**设计决策与实现过程**，随设计变更持续更新。
> 目标：① 实现 LRU 淘汰 + 防扫描污染 ② 让 `store->ondisk_blksize` 生效。

## 目录

- [一、目标与约束](#一目标与约束)
- [二、第一步：改造前的现状（扫描结果）](#二第一步改造前的现状扫描结果)
- [三、第二步：设计](#三第二步设计)
- [四、实现：第三步（淘汰逻辑）](#四实现第三步淘汰逻辑)
- [五、实现：第四步（接入 ondisk tree）](#五实现第四步接入-ondisk-tree)
- [六、已改动文件清单](#六已改动文件清单)
- [七、剩余步骤](#七剩余步骤)
- [八、变更记录](#八变更记录)

---

## 一、目标与约束

### 目标

1. **LRU 淘汰 + 防扫描污染**：缓存由 `store->cache_size` 控制，有默认值；只读缓存（淘汰直接丢弃不回写）；引用中的块不可淘汰；全表扫描不能把热数据挤出缓存。
2. **让 `ondisk_blksize` 生效**：新建 ondisktree 使用 `store->ondisk_blksize`；读取已有 ondisktree 使用文件元数据里的值。

### 硬约束

- 不修改已有 ondisktree 文件的格式
- 不改变已有 ondisktree 的读取行为
- 只有新创建的 ondisktree 才使用 store 的设置
- 保持与现有代码风格一致（`_van_` 前缀、`VAN_*` 错误码、`_van_malloc/free` 系列、`VAN_MUTEX_*` 宏）
- 防扫描污染不可省略
- 淘汰必须检查引用计数

---

## 二、第一步：改造前的现状（扫描结果）

### 2.1 原有数据结构

```c
struct _ondisk_cache_block {
	TAILQ_ENTRY(_ondisk_cache_block)	links;       /* 哈希桶链表 */
	TAILQ_ENTRY(_ondisk_cache_block)	tree_links;  /* 树链表 */
	pthread_mutex_t		mtx_load_cond;
	pthread_cond_t		load_cond;
	int			load;
	size_t		hash_indx;
	uint32		refcount;
	char		fileid[OD_FILEID_LEN];
	uint32		blkid;
	char		page[1];   /* 变长 */
};

struct _ondisk_cache_header {
	pthread_mutex_t	mtx_hash;
	TAILQ_HEAD(_odch, _ondisk_cache_block) head;
};
```

### 2.2 关键结论

| 项 | 改造前状态 |
|---|---|
| `store->cache_size` | 有字段、有 setter/getter，但**只有 `printinfo` 打印它**，无任何代码据此淘汰 |
| `store->ondisk_blksize` | 默认 `OD_DEF_BLKSIZE` = **1MB**；**ondisktree 完全没读它** |
| 默认 `cache_size` | `cache_header_size * ondisk_blksize` = 64 × 1MB = 64MB（**推导值**，非显式） |
| 创建时 blksize | `_van_ondisktree_init()` 中 `tree->blksize = OD_DEF_BLKSIZE` **硬编码** |
| 读取时 blksize | `_van_ondisktree_initwithmeta()` 中 `tree->blksize = meta->blksize` ✅ **已正确使用文件自身值** |
| 磁盘读入口 | `_van_file_readblk`（`van_io.c:230`），**仅 `cache_get` 调用** → 读路径已全部走缓存 |
| 块释放 | `cache_put` **只做 `refcount--`，从不真正释放** → 块只增不减，内存无限增长 |

### 2.3 扫描中发现的两个既有 bug

**bug ① 重复加锁**（`ondisktree_cache.c` 原 99-110 行）

```c
VAN_MUTEX_LOCK(block, mtx_load_cond, ret);      /* 加锁 1 */
if (block->load) {
	VAN_MUTEX_LOCK(block, mtx_load_cond, ret);  /* 加锁 2：同一把非递归锁 */
	goto ready;                                 /* 且从不 unlock */
}
```

**bug ② 分配失败未释放桶锁**（原 114-116 行）：`_van_calloc` 失败时直接 `return`，第 81 行拿到的 `mtx_hash` 未释放。

### 2.4 其他观察

- 块同时挂在 **2 个链表**上（哈希桶 + 树链表），新增 LRU 链表后所有插/摘点都要同步。
- 两条清理路径并存：`_van_ondisk_cache_destroy`（遍历桶）与 `_van_ondisktree_close`（遍历树链表）。
- 同一 store 下不同树的 blksize **可以不同** → 容量统计必须用字节，且**每块必须自己记录大小**。
- 死常量：`NCACHE_PER_HEADER`、`DEF_CACHE_NBLOCK`、`tree->blk_cachesize`（定义后无人使用）。

---

## 三、第二步：设计

### 3.1 数据结构新增

**`ONDISK_CACHE_BLOCK`**

| 字段 | 说明 |
|---|---|
| `lru_links` | 新增，全局 SLRU 链表游标（块现在同时在 3 个链表上） |
| `blksize` | 新增，本块 page 的字节数 |
| `in_protected` | 新增，1 = protected 段，0 = probation 段 |
| `refcount` | `uint32` → `atomic_uint` |

**不新增 `access_count` / `last_access_ticks`**：SLRU 用"在哪条链表上"天然表达访问次数（probation = 访问过 1 次，protected = ≥2 次）。

**`ONDISK_CACHE_HEADER`：不新增。** 它是 per-bucket 的（默认 64 个桶），放不了全局链表头。

**新增 `ONDISK_CACHE_LRU`，嵌入 `VAN_STORE`：**

```c
struct _ondisk_cache_lru {
	pthread_mutex_t	mtx_lru;
	TAILQ_HEAD(_odprob, _ondisk_cache_block) probation;   /* 淘汰候选，尾 = LRU */
	TAILQ_HEAD(_odprot, _ondisk_cache_block) protected;   /* 热数据 */
	atomic_size_t	bytes_total;
	atomic_size_t	bytes_prot;
	size_t		stat_hits, stat_misses, stat_evictions;
	size_t		stat_promotions, stat_demotions;
};

#define	OD_LRU_PROTECTED_PCT	50
#define	CACHE_BLOCK_BYTES(b)	(SSZA(ONDISK_CACHE_BLOCK, page) + (size_t)(b)->blksize)
```

### 3.2 防扫描污染选型：**分段 LRU（Segmented LRU）**

**为什么选 SLRU（方案 B）**

1. **零摩擦**：只需两个 `TAILQ` 头 + 每块一个 entry，项目已重度使用 `sys/queue.h`。LRU-K 需要每块保存 K 次访问时间并比较，侵入性大得多。
2. **同时满足"全局 LRU 链表"要求**：probation 链表就是 LRU 淘汰链，protected 只是热数据保护区，一个结构兼顾两件事。
3. **抗扫描是结构性的，不是启发式的**：扫描块只被访问一次 → 停在 probation → 还来不及晋升就被淘汰；扫描前就热的数据在 protected 里，压根不在候选序列中。
4. **只有一个调节点**（protected 占比），且有界：扫描量再大也只能占满 probation。
5. **O(1) 操作**，有成熟工程先例（InnoDB young/old、众多 KV 存储）。

**为什么不选 LRU-K（方案 A）**：默认配置下只有约 64 个块，历史信息几乎没有区分度；且它只回答"淘汰谁"，不提供"从哪条链摘"的结构，等于 SLRU 的复杂度 + 额外历史维护。

**为什么不选扫描检测（方案 C）**：启发式，冷启动 / 建索引 / 首次加载某棵树时会误伤真正的热数据；且只降低插入优先级，无法保护"扫描开始前就已热"的块——而这正是防污染的核心目标。

**四条运作规则**

```
【插入】新块（未命中）   -> probation 头部，in_protected = 0
【命中】在 probation     -> 晋升到 protected 头部
        在 protected     -> 移到 protected 头部
【封顶】protected 字节 > cache_size * 50% -> protected 尾部降级回 probation（不淘汰）
【淘汰】优先 probation 尾部；probation 无可用则先降级 protected 尾部再淘汰
        refcount > 0 的块跳过
```

### 3.3 容量统计与超容策略

- 单位一律用**字节**（不同树 blksize 可能不同）。
- 插入前：`while (bytes_total + needed > cache_size) evict_one();`
- **best-effort**：所有候选都被引用时**允许临时超过 `cache_size`**，照常插入。让 `cache_get` 失败或阻塞远比多用一点内存糟糕，超出部分由后续 `put`/`get` 回收。

### 3.4 线程安全

**引用计数：读用原子，改在桶锁内**

| 操作 | 方式 |
|---|---|
| `get` 命中 `refcount++` / `put` 的 `--` | **桶锁内**（`atomic_fetch_add/sub`） |
| 淘汰候选预检 | 无锁 `atomic_load`（仅快速筛选） |
| 淘汰"确认可摘除" | **持有该块桶锁后重新检查**（唯一权威判断） |
| `VAN_ASSERT`、调试打印 | 无锁 `atomic_load` |

因为所有修改都在桶锁内，不存在"原子与锁各改一半"的不一致；原子化只带来"可在锁外安全观察"。**淘汰必须回到桶锁内复核**：预检后、拿锁前，另一个线程可能命中并 `refcount++`。

**全局加锁顺序（唯一，代码注释中已标注）**

```
store->cache_lru.mtx_lru  ->  cache_header[i].mtx_hash
        (外层)                        (内层)
```

> 2026-10 修订：原先最外层还有 `tree->odfile.mtx_head`（每棵树一条块链表）。该链表已删除——
> 淘汰路径拿不到"该块属于哪棵树"，无法在销毁前摘链，导致已释放的块仍挂在树链表上
> （`_van_ondisk_cache_get()` 插入时写悬空的 `tqe_prev`、关树时二次释放）。现在块只挂在
> 哈希桶与 SLRU 链表上，关树时按 `fileid` 扫桶清理，见 4.5。

三条硬规则：

1. 任何路径都按上述顺序获取，绝不反向。
2. **持有桶锁时绝不拿 LRU 锁**（那会是 bucket→LRU，反向）。命中路径做法：桶锁内 `refcount++` → **释放桶锁** → 再拿 LRU 锁晋升。安全依据是 `refcount > 0` 保证块不会被淘汰。
3. **磁盘 I/O 绝不持锁**。

---

## 四、实现：第三步（淘汰逻辑）

### 4.1 新增/改写的函数（`src/ondisktree/ondisktree_cache.c`）

| 函数 | 作用 |
|---|---|
| `cache_lru_remove()` (static) | 从所在 SLRU 链表摘除并回退字节计数（调用者持 `mtx_lru`） |
| `cache_lru_promote()` (static) | 晋升 / 移到 MRU 端 |
| `cache_prot_tail()` (static) | 取 protected 链表尾 |
| `cache_lru_cap_protected()` (static) | protected 超限时降级到 probation |
| `cache_evict()` (static) | **核心**：从 probation 尾淘汰直到腾出 `needed` 字节 |
| `_van_ondisk_cache_init()` | 增加 SLRU 链表与计数器的初始化 |
| `_van_ondisk_cache_destroy()` | 遍历桶时**同步摘除 LRU 链表** |
| `_van_ondisk_cache_get()` | 重写：命中/未命中两条路径 |
| `_van_ondisk_cache_put()` | 引用计数原子化 + **条件触发**淘汰 |
| `_van_ondisk_cache_discard_fileid()` | **替代 `discard()`**：关树时按 `fileid` 扫全部桶，摘除并销毁该文件的所有块 |
| `_van_ondisk_cache_block_destory()` | 不变 |

### 4.2 `cache_evict` 要点

```
循环直到 bytes_total + needed <= cache_size:
    从 probation 尾部（TAILQ_FOREACH_REVERSE）找第一个 refcount == 0 的块
    若 probation 无可用候选且尚未降级过:
        cache_lru_cap_protected()   /* 降级 protected 尾 */
        continue
    若仍无候选: break               /* best-effort，允许超容 */
    加锁顺序 LRU -> bucket:
        取该块桶锁
        【在桶锁内】复核 refcount == 0
            是 -> 摘哈希桶 + 摘 LRU + 释放 + stat_evictions++
            否 -> 解锁并 break（与读者竞争，本次放弃）
```

### 4.3 未命中路径（含 bug ② 修复）

关键顺序调整：**先释放桶锁，再分配内存和读盘**。

```
桶锁内查找 -> 未找到 -> 释放桶锁
  -> _van_calloc（失败也不会漏锁，bug ② 由构造消除）
  -> 初始化块（refcount = 1，blksize = meta->blksize）
  -> LRU 锁：cache_evict(需要字节数) + 插入 probation + bytes_total += 
  -> 释放 LRU 锁
  -> _van_file_readblk（不持任何锁）
  -> LRU 锁 -> 桶锁：二次查找
        已存在（并发创建了同一个块）-> 摘掉自己的、改用对方的
        否则 -> 插入哈希桶；插入树链表；load = 1；broadcast
```

二次查找是必需的：读盘期间另一个线程可能已经创建并发布了同一个块。否则哈希桶里会留下两块相同 key 的块，其中一块 `refcount` 永久为 1、永远无法淘汰。

**清理责任用两个标志界定**（`owned` / `on_lru`）：

| 情形 | owned | on_lru | 错误处理 |
|---|---|---|---|
| `calloc` 后、`MUTEX/COND_INIT` 失败 | 1 | 0 | 只释放，不摘链（尚未入链） |
| 已插入 probation、读盘失败 | 1 | 1 | 摘 LRU 链 + 释放 |
| 竞态中改用对方的块 | **0** | 0 | 不清理（我们只持有引用） |
| 已发布进哈希桶 | **0** | 1 | 不清理（他人可能持有引用） |

这样避免了两类错误：对未入链的块执行 `TAILQ_REMOVE`，以及误删属于其他线程的块。

### 4.4 bug ① 修复：统一改为"加锁 → 循环等待 → 解锁"

```c
if (!block->load) {
	VAN_MUTEX_LOCK(block, mtx_load_cond, ret);
	while (!block->load) {
		VAN_COND_WAIT(block, load_cond, &block->mtx_load_cond, ret1);
		if (ret != 0) break;
	}
	VAN_MUTEX_UNLOCK(block, mtx_load_cond, ret1);
}
```

单次加锁、循环等待（防虚假唤醒）、任何出口都解锁。原来"加锁 → 再加锁 → 带锁 goto"的结构已消除。

### 4.5 两条清理路径的 LRU 摘除

**`_van_ondisktree_close`（`ondisktree.c`）** —— 改为调用 `_van_ondisk_cache_discard_fileid()`，该函数按 `LRU -> bucket` 顺序扫每一个桶，`memcmp` 块的 `fileid` 命中后摘除哈希桶与 SLRU 链表并销毁：

```c
if (tree->meta != NULL)
	ret1 = _van_ondisk_cache_discard_fileid(store, tree->meta->fileid);
```

- 块只挂在哈希桶 + SLRU 链表上，销毁后不会在任何链表留下悬空指针 —— 这是原先 `tree->odfile.head`
  链表做不到的事（淘汰者无法摘链），也是本次修订的重点。
- 被读者持有（`refcount > 0`）的块跳过，交给它最后一次 `put` 回收，避免引入新的 UAF。
- 关树是低频操作；代价从"O(该树的块数)"变为"O(缓存总块数)"，默认 64MB / 1MB ≈ 64 个块，可忽略。

**`_van_ondisk_cache_destroy`（`ondisktree_cache.c`）** —— 遍历每个桶时先取 LRU 锁再取桶锁，逐块 `cache_lru_remove()` + 摘哈希桶 + 销毁；最后销毁 `mtx_lru`。顺序 `LRU -> bucket` ✅

若不同步摘除，LRU 链表会残留指向已释放块的指针，下一次淘汰即 use-after-free。

### 4.6 默认值修正

`_van_store_open()` 中 `cache_size` 默认值由推导式改为显式常量：

```c
if (store->cache_size == 0)
	store->cache_size = OD_DEF_CACHESIZE;   /* 64MB */
```

（原为 `cache_header_size * ondisk_blksize`，恰好也是 64MB 但语义不明。）

---

## 五、实现：第四步（接入 ondisk tree）

### 5.1 `ondisk_blksize` 注入点

放在 `_van_ondisktree_create()` 中，`_van_ondisktree_init()` 之后：

```c
tree->table = table;
ret = _van_ondisktree_init(tree);
if (ret == 0) {
	if (table != NULL && table->store != NULL &&
	    table->store->ondisk_blksize != 0)
		tree->blksize = table->store->ondisk_blksize;
	*treep = tree;
}
```

**为什么放在 `create` 而不是 `init`**

- `_van_ondisktree_init()` 的签名只有 `ONDISK_TREE *`，**拿不到 store**，改它需要动签名和所有调用点。
- `create` 有 `table`，`table->store` 可达，且是**所有建树路径的必经之处**。

**覆盖时机为什么必须在 `_van_ondisktree_initmeta()` 之前**

`_van_ondisktree_open()` 对新文件走 `_van_ondisktree_initmeta()`，其中 `meta->blksize = tree->blksize` 会把块大小**持久化进文件元数据**。`create` 只在内存里建树、不调 `open`，所以在 `create` 里覆盖必然早于 `initmeta`。一旦写进元数据，块大小就归文件所有了。

**为什么已有树不受影响**

已有文件的 open 路径是 `_van_ondisktree_read()` → `_van_ondisktree_initwithmeta()`，其中 `tree->blksize = meta->blksize` 会**覆盖**我们在 `create` 里设的值。这正是"读取已有 ondisktree 使用文件自身 blksize"的要求，无需改动。

**默认值下行为不变**：`store->ondisk_blksize` 默认就是 `OD_DEF_BLKSIZE`，与 `init` 里的硬编码相同，所以不设置时结果与改造前完全一致。

### 5.2 建树路径覆盖检查

`_van_ondisktree_create()` 共 3 个调用点，注入点全部覆盖：

| 调用点 | 场景 |
|---|---|
| `src/table/van_table.c:492` | 打开表时构建 ondisk tree |
| `src/table/van_table.c:935` | 新建 ondisk 文件 |
| `src/ondisktree/ondisktree_merge.c:245` | **合并时新建结果树** |

**已知行为（需知悉）**：merge 产生的是一个**全新文件**（`VAN_OPEN_CREAT | VAN_OPEN_TRUNC` → 走 `initmeta`），因此它使用 `store->ondisk_blksize`，而**不是**源树的块大小。若 store 的设置在某些树创建之后被改过，合并结果会与源树的块大小不同。这符合"新建树使用 store 设置"的语义，但值得记住。

### 5.3 顺带清理的两个既有警告

| 位置 | 问题 | 处理 |
|---|---|---|
| `_van_ondisktree_init_cache()` | `size_t nblks` 只赋值不使用（连带 `meta` 也可去除） | 删除死变量，并补注释说明缓存归 store 所有、按树无初始化工作 |
| `_van_ondisktree_initmeta()` | `err:` 标签无 `goto` 使用 | 删除标签，保留 `return (ret)` |

### 5.4 注释补充

按你的要求补了两处维护性注释：

- **`owned` / `on_lru` 标志**：在声明处写明含义，并列出三条维护规则——`owned == 0` 的块绝不销毁；`on_lru == 0` 的块绝不 `cache_lru_remove()`；把 `block` 改指向 `found` 前必须先置 `owned = 0`。
- **命中路径加锁顺序**：显式标注 `LRU -> bucket，绝不反向`，并说明"先放桶锁再取 LRU 锁"之所以安全，是因为已持有的引用保证块不会被淘汰。

### 5.5 验证

`make libvan.a` 全量构建零错误零警告（两个既有警告已消除）；`make sqlcli` + `smoke.sql` 输出与改造前一致。

> 注意：`ondisk_blksize` 真正生效的**功能验证**（设置一个非默认块大小并确认写入文件的 meta）留到第五步测试用例 1，本步只验证了默认值下的行为不变。

---

## 六、已改动文件清单

| 文件 | 改动 |
|---|---|
| `include/van_declare.h` | 新增 `ONDISK_CACHE_LRU` 类型别名 |
| `include/van_ondisk_cache.h` | 块新增 `lru_links`/`blksize`/`in_protected`，`refcount` 改原子；新增 `ONDISK_CACHE_LRU` 结构、`OD_LRU_PROTECTED_PCT`、`CACHE_BLOCK_BYTES` 宏 |
| `include/van_store.h` | 嵌入 `ONDISK_CACHE_LRU cache_lru` |
| `include_auto/ondisktree_ext.h` | 声明 `_van_ondisk_cache_discard()` |
| `src/ondisktree/ondisktree_cache.c` | SLRU 全部实现、get/put 重写、bug ①② 修复、destroy 同步 LRU；第四步补充 `owned`/`on_lru` 与加锁顺序注释 |
| `src/ondisktree/ondisktree.c` | 第三步：关闭路径改用 `_van_ondisk_cache_discard()`、移除不再使用的 `indx`；第四步：`create` 中注入 `store->ondisk_blksize`、清理 `nblks`/`err` 两个既有警告 |
| `src/store/van_store.c` | `cache_size` 默认值改为 `OD_DEF_CACHESIZE` |
| `docs/ONDISK_CACHE_DESIGN.md` | 本文档（随设计/实现持续更新） |

**验证**：`make libvan.a` 全量构建**零错误零警告**（含原有两处警告已消除）；`make sqlcli` + `smoke.sql` 冒烟测试输出与改造前一致，无回归。

---

## 七、剩余步骤

- **第五步：测试** —— 4 个用例：
  1. 新建 ondisktree 使用 `store->ondisk_blksize`
  2. 读取已有 ondisktree 使用文件自身的 blksize
  3. 全表扫描后热数据仍在缓存中
  4. 缓存满时 LRU 淘汰正常工作

---

## 八、变更记录

| 日期 | 步骤 | 内容 |
|---|---|---|
| 2026-09-27 | 第一步 | 完成代码扫描，输出现状与两个既有 bug |
| 2026-09-27 | 第二步 | 确定 SLRU 方案、数据结构新增、锁顺序、容量策略 |
| 2026-09-27 | 第三步 | 实现淘汰逻辑、重写 get/put、修复 bug ①②、同步清理路径；建立本文档 |
| 2026-09-27 | 第三步（自查） | 修正竞态分支与 `MUTEX_INIT` 失败路径的清理缺陷，引入 `owned`/`on_lru` 标志 |
| 2026-09-27 | 第四步 | `create` 中注入 `store->ondisk_blksize`；清理两个既有警告；补充维护注释 |
