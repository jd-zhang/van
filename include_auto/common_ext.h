
/* src/common/van_alloc.c */
extern int _van_calloc(void *arg, size_t nelem, size_t elsize, void *storep);
extern int _van_malloc(void *arg, size_t size, void *storep);
extern int _van_realloc(void *arg, void *ptr, size_t size, void *storep);
extern int _van_strdup(void *arg, const char *str, char **destp);
extern void _van_free(void *arg, void *ptr);

/* src/common/van_alloc_debug.c */
extern int _van_alloc_stat_reset();
extern ALLOC_STAT _van_alloc_stat_ret();
extern int _van_alloc_stat_print(FILE *f, const char *prefix);

/* src/common/van_hash.c */
extern uint32 _van_hash_func1(const void *key, uint32 len);
extern uint32 _van_hash_func2(const void *key, uint32 len);
extern uint32 _van_hash_func3(const void *key, uint32 len);
extern uint32 _van_hash_func4(const void *key, uint32 len);

/* src/common/van_hash_fnv.c */
extern uint64_t _van_hash_fnv64(const void *string, size_t len);
/* src/common/van_hash_city.c */
extern uint64_t _van_hash_city64(const void *s, size_t len);

/* src/common/van_io.c */
extern int _van_path_isabsolute(const char *path);
extern int _van_file_open(const char *path, int oflag, mode_t omode, int *fdp);
extern int _van_file_fsync(int fd);
extern int _van_file_close(int fd);
extern int _van_file_openread(const char *path, off_t start, 
    size_t nbytes, void *strp);
extern int _van_file_openreadall(const char *path,
    void *strp, size_t *np);
extern int _van_file_readall(int fd, size_t nbytes, void *strp);
extern int _van_file_readblk(int fd, uint32 blksize, uint32 blk,
    off_t start, void *blkaddr);
extern int _van_file_readblk_alloc(int fd, uint32 blksize,
    uint32 blk, off_t start, void *retp);
extern int _van_file_writeblk(int fd, uint32 blksize, uint32 blk,
    off_t start, const void *blkaddr);
extern int _van_file_lseek(int fd, off_t pos);
extern int _van_file_read(int fd, void *buf, size_t nbytes, off_t pos);
extern int _van_file_write(int fd, const void *buf, size_t nbytes, off_t pos);
extern int _van_file_append(int fd, const void *buf, size_t nbytes);
extern int _van_file_stat(int fd, struct stat *st);
extern int _van_file_getfid(int fd, char *fidp);

/* src/common/van_syserr.c */
extern int _van_getsys_error();
extern int _van_error_path(const char *str);
extern void _van_abort();
