
/* src/datum/datu.c */
extern int _van_datum_clone(const VAN_DATUM *src, VAN_DATUM **dstp);
extern int _van_datum_copy(const VAN_DATUM *src, VAN_DATUM *dst);
extern int _van_datum_copyfree(VAN_DATUM *dst);
extern int _van_datum_clone_once(const VAN_DATUM *src, VAN_DATUM **dstp);
extern void _van_datum_free(VAN_DATUM *ptr);
extern void _van_datum_free_once(VAN_DATUM *ptr);
extern void _van_data_free_data(VAN_DATUM *ptr);
extern int _van_datum_replace(VAN_DATUM *ptr, void *newd, size_t newlen);
extern int _van_datum_uercopy(void *d, size_t len, VAN_DATUM **vdp);
extern int _van_datum_defcmp(const VAN_DATUM *dt1, const VAN_DATUM *dt2);

