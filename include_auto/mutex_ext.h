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
#ifndef _MUTEX_EXT_H
#define _MUTEX_EXT_H

/* src/mutex/van_mutex.c */

/* src/mutex/van_mutex_debug.c */
extern int _van_rwlock_stat_reset();
extern VAN_RWLOCK_STAT _van_rwlock_stat_ret();
extern int _van_rwlock_stat_print(FILE *f, const char *prefix);
extern int _van_mutex_stat_reset();
extern VAN_MUTEX_STAT _van_mutex_stat_ret();
extern int _van_mutex_stat_print(FILE *f, const char *prefix);
extern int _van_cond_stat_reset();
extern VAN_COND_STAT _van_cond_stat_ret();
extern int _van_cond_stat_print(FILE *f, const char *prefix);

#endif
