
/* src/recovery/van_recovery.c */
extern int _van_add_recovery_int(VAN_STORE *, VAN_DISTAB *, int (*func)(
    VAN_STORE *, VAN_DATUM *, VAN_LSN *, van_recops, void *), uint32);
extern int _van_init_rec(VAN_STORE *store);


