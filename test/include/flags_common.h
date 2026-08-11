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
#ifndef _TEST_FLAGS_COMMON_H
#define _TEST_FLAGS_COMMON_H

#define DEFINE_CNT(g) size_t g##_CNT = sizeof(g) / sizeof(g[0]) - 1

#define MAX_FLAGS_STR_LEN 1000
typedef struct FlagInfoStruct {
	char *flagname;
	flags_t flagval;
} FLAGINFO;

typedef enum {
	APP_SIZE_TINY = 1,
	APP_SIZE_SMALL,
	APP_SIZE_MEDIUM,
	APP_SIZE_LARGE
} APP_DATASET_SIZE;

typedef enum {
	APP_TYPE_INT = 1,
	APP_TYPE_FSTRING, /* Fixed length string */
	APP_TYPE_VSTRING, /* Variable length string */
	APP_TYPE_FBYTES, /* Fixed length bytes */
	APP_TYPE_VBYTES /* Variable length bytes */
} APP_ITEM_TYPE;

#define OP_TABLE	1
#define OP_INMEMTREE	2
#define OP_ONDISKTREE	3

extern FLAGINFO APP_DATASET_SIZES[];
extern size_t APP_DATASET_SIZES_CNT;
extern char *get_app_dataset_size_string(flags_t flag);
extern FLAGINFO APP_ITEM_TYPES[];
extern size_t APP_ITEM_TYPES_CNT;
extern char *get_app_item_type_string(flags_t flag);
extern FLAGINFO OP_TYPES[];
extern size_t OP_TYPES_CNT;
extern char *get_op_type_string(flags_t flag);

extern int get_string_indx(FLAGINFO *flags, const char *str);
extern int get_flag_indx(FLAGINFO *flags, flags_t flag);
extern char *get_unique_flag_string(FLAGINFO *flags, flags_t inFlag);
extern char *get_flags_string(FLAGINFO *flags, flags_t inFlag, char *buff);
extern char *get_comb_flags_string(FLAGINFO *uniqflags, FLAGINFO *orflags,
    flags_t inFlag, char *buff);
extern void sprintf_flaginfos(FLAGINFO *flags, char *buf,
    const char *prefix, const char *delim);
extern void sprintf_flaginfos2(FLAGINFO *flags1, FLAGINFO *flags2, char *buf,
    const char *prefix, const char *delim);
extern flags_t set_flag_value(FLAGINFO *flags, char *str,
    flags_t *valp, int keep);
extern flags_t set_unique_flag(FLAGINFO *flags, char *str, flags_t *valp);
extern flags_t set_or_flags(FLAGINFO *flags, char *str, flags_t *valp);
extern flags_t set_comb_flags(FLAGINFO *uniqflags, FLAGINFO *orflags,
    char *str, flags_t *valp);

/* APP */


#include "van_flags_common.h"

#endif /* _TEST_FLAGS_COMMON_H */
