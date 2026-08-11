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
#ifndef _LOG_EXT_H
#define _LOG_EXT_H

/* src/log/van_log_cursor.c */
extern int _van_logrec_cget(VAN_LOG_CURSOR *lgc, VAN_LSN *lsn,
    VAN_DATUM *datum, flags_t flags);

/* src/log/van_log_file.c */
extern int _van_log_listfiles(VAN_LOG *logp, int *cntp);
extern int _van_log_islogfile(const char *lpath);
extern int _van_log_newfile(VAN_LOG *logp);
extern int _van_log_openfile(VAN_LOG *logp, uint32 fnum,
    VAN_LOGFILE_INFO **infopp);
extern int _van_logc_openfile(VAN_LOG_CURSOR *lgc, VAN_LSN *lsn);

/* src/log/van_log_open.c */
extern int _van_log_open(VAN_STORE *store);
extern int _van_log_close(VAN_STORE *store);
extern int _van_log_init(VAN_LOG *logp);
extern int _van_log_cleanup(VAN_LOG *logp);

/* src/log/van_log_record.c */
extern int _van_put_logrecord(VAN_STORE *store, VAN_TABLE *table, VAN_TXN *txnp,
    VAN_LSN *ret_lsnp, uint32 flags, uint32 rectype, uint32 size,
    VAN_LOG_RECSPEC *spec, ...);
extern int _van_read_logrecord(VAN_STORE *store, VAN_TABLE **tablepp, void *td,
    void* data, VAN_LOG_RECSPEC *spec, uint32 size, void **argp);
extern int _van_print_logrecord(VAN_STORE *store, VAN_DATUM *datap, VAN_LSN *lsnp, 
    const char *str, VAN_LOG_RECSPEC *spec, void *info);
extern int _van_logrec_init(VAN_STORE *store, uint32 orig_len,
    char **bufp, uint32 *bufsizep, int *typep, VAN_LSN *lsnp);
extern int _van_logrec_write(VAN_STORE *store, const char *buf,
    uint32 bufsize, int type);

#endif
