/*
 * Copyright (c) 2026 VanDB Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef _TABLE_EXT_H
#define _TABLE_EXT_H

#if defined(__cplusplus)
extern "C" {
#endif

/* src/table/van_table.c */
extern int _van_table_create(VAN_STORE *store, VAN_TABLE **tablep);
extern int _van_table_init(VAN_TABLE *table);
extern int _van_table_open(VAN_TABLE *table, const char *tb_name,
    flags_t flags);
extern int _van_table_start_threads(VAN_TABLE *table);
extern int _van_table_close(VAN_TABLE *table, flags_t flags);
extern int _van_table_curticks(VAN_TABLE *table, ticks_t *tkp);
extern int _van_table_read_odt_paths(VAN_TABLE *table);
extern int _van_table_write_odt_paths(VAN_TABLE *table);
extern int _van_table_put_record(VAN_TABLE *table, VAN_DATUM *key,
    VAN_DATUM *data, flags_t flags);
extern int _van_table_get_record(VAN_TABLE *table, VAN_DATUM *key,
    VAN_DATUM *data, flags_t flags);
extern int _van_table_del_record(VAN_TABLE *table, VAN_DATUM *key,
    flags_t flags);
extern int _van_table_new_odfilename(VAN_TABLE *table,
    char *buf, size_t buflen);
extern int _van_table_add_odtpath(VAN_TABLE *table, ONDISK_TREE *odtree);
extern int _van_table_replace_odtpath(VAN_TABLE *table, ONDISK_TREE *odlow,
    ONDISK_TREE *odhigh, ONDISK_TREE *odnew);
extern int _van_table_clear_odtitem(VAN_TABLE *table,
    VAN_TABLE_ODT_ITEM *item);
extern int _van_table_copy_odtitem(VAN_TABLE *table, VAN_TABLE_ODT_ITEM *src,
    VAN_TABLE_ODT_ITEM *dst);
extern int _van_table_move_odtitem(VAN_TABLE *table, VAN_TABLE_ODT_ITEM *src,
    VAN_TABLE_ODT_ITEM *dst);
extern int _van_table_compact_odtiems(VAN_TABLE *table);
extern int _van_table_new_dumpfile(VAN_TABLE *table,
    VAN_TABLE_ODT_ITEM **itemp, ONDISK_TREE **odtreep);

extern int _van_table_dump_frozen(VAN_TABLE *table, ONDISK_TREE **odtreep);
extern int _van_table_dump_inmem(VAN_TABLE *table);
extern int _van_table_replace_odt_items(VAN_TABLE *table,
    size_t st, size_t et, ONDISK_TREE *newodtree);
extern void _van_table_show_odt_items(VAN_TABLE *table, FILE *f);

/* src/table/van_table_dump.c */
extern int _van_table_freeze(VAN_TABLE *table);
extern int _van_table_dump_frozen_init(VAN_TABLE *table);
extern void *_van_table_frozen_dump_thread(void *arg);
extern int _van_table_dump_join_init(VAN_TABLE *table);
extern void *_van_table_dump_join_thread(void *arg);
extern int _van_table_frozen_free_init(VAN_TABLE *table);
extern void *_van_table_frozen_free_thread(void *arg);

/* src/table/van_table_merge.c */
extern int _van_table_merge_scan_init(VAN_TABLE *table);
extern void *_van_table_merge_scan_run(void *arg);
extern int _van_table_merge_scan(VAN_TABLE *table);
extern int _van_table_new_merge_job(VAN_TABLE *table, size_t low,
    size_t high, size_t cnt);
extern int _van_table_clear_merging(VAN_TABLE *table, size_t low, size_t high);
extern int _van_table_merge_clean(VAN_TABLE *table);
extern int _van_table_update_merge(VAN_TABLE *table,
    const VAN_TABLE_MERGE_JOB *jobarg);
extern void *_van_table_merge_job_run(void *arg);

/* src/table/van_table_op.c */
extern int _van_table_op_create(VAN_TABLE *table, VAN_OPERATION **tbopp,
    flags_t cflag);
extern int _van_table_op_init(VAN_OPERATION *tbop);
extern int _van_table_op_close(VAN_OPERATION *tbop);
extern int _van_table_op_destory(VAN_OPERATION *tbop);
extern int _van_table_op_del(VAN_OPERATION *tbop,
    VAN_DATUM *key, flags_t flags);
extern int _van_table_op_put(VAN_OPERATION *tbop, VAN_DATUM *key,
    VAN_DATUM *data, flags_t flags);
extern int _van_table_op_get(VAN_OPERATION *tbop, VAN_DATUM *key,
    VAN_DATUM *data, flags_t flags);

/* src/table/van_table_path.c */
extern int _van_table_path(VAN_TABLE *table, const char *file, char **tp);
extern int _van_table_path_noalloc(VAN_TABLE *table,
    char *buf, size_t buflen, const char *file);

#ifdef __cplusplus
}
#endif

#endif
