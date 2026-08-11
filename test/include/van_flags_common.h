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
#ifndef _TEST_VAN_FLAGS_COMMON_H
#define _TEST_VAN_FLAGS_COMMON_H

extern FLAGINFO VAN_ALLOC_FLAGS[];
extern size_t VAN_ALLOC_FLAGS_CNT;
extern char *get_van_alloc_flag_string(flags_t flag);

extern FLAGINFO VAN_CURSOR_ALLOC_FLAGS[];
extern size_t VAN_CURSOR_ALLOC_FLAGS_CNT;
extern char *get_van_cursor_alloc_flags_string(flags_t flags, char *buff);

extern FLAGINFO VAN_CURSOR_GET_FLAGS[];
extern size_t VAN_CURSOR_GET_FLAGS_CNT;
extern char *get_van_cursor_get_flag_string(flags_t flag);

extern FLAGINFO VAN_CURSOR_MIXGET_FLAGS[];
extern size_t VAN_CURSOR_MIXGET_FLAGS_CNT;
extern char *get_van_cursor_mixget_flags_string(flags_t flags, char *buff);

extern FLAGINFO VAN_INMEMTREE_PUT_FLAGS[];
extern size_t VAN_INMEMTREE_PUT_FLAGS_CNT;
extern char *get_van_inmemtree_put_flag_string(flags_t flag);

extern FLAGINFO VAN_ODTREE_OPEN_FLAGS[];
extern size_t VAN_ODTREE_OPEN_FLAGS_CNT;
extern char *get_van_odtree_open_flags_string(flags_t flags, char *buff);

extern FLAGINFO ERR_RETURN_CODES[];
extern size_t ERR_RETURN_CODES_CNT;
extern char *get_van_err_return_code_string(flags_t flag);

#endif
