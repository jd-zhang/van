#include "van_int.h"
#include "van.h"

/* Allocate the datum and data separately */
int _van_datum_clone(const VAN_DATUM *src, VAN_DATUM **dstp) {
	int ret;
	VAN_DATUM *dst;

	VAN_ASSERT(dstp != NULL);
	ret = _van_malloc(NULL, sizeof(VAN_DATUM), &dst);
	if (ret != 0)
		return (ret);
	memcpy(dst, src, sizeof(VAN_DATUM));

	/* Null data */
	if (src->data == NULL || src->size == 0) {
		dst->data = NULL;
		return (ret);
	}

	ret = _van_malloc(NULL, dst->size, &dst->data);
	if (ret != 0) {
		_van_free(NULL, dst);
		return (ret);
	}
	memcpy(dst->data, src->data, dst->size);

	*dstp = dst;

	return (ret);	
}

int _van_datum_copy(const VAN_DATUM *src, VAN_DATUM *dst) {
	int ret;
	void *p;

	VAN_ASSERT(dst != NULL);
	ret = 0;
	p = NULL;
	if (src == NULL)
		return (ret);
	if (src->size == 0) {
		dst->size = 0;
		return (ret);
	}

	switch (dst->flags & VAN_MEM_MASK) {
		case VAN_MALLOC:
			ret = _van_malloc(NULL, src->size, &p);
			break;
		case VAN_REALLOC:
			ret = _van_realloc(NULL, dst->data, src->size, &p);
			break;
		case VAN_USERMEM:
			if (src->size > dst->ulen)
				ret = VAN_BUFFER_SMALL;
			break;
		default:
			ret = _van_error_path(
			    "Invalid flag to _van_datum_copy!");
			break;
	}
	if (ret == 0) {
		dst->size = src->size;
		if (p != NULL)
			dst->data = p;
		memcpy(dst->data, src->data, dst->size);
	} else if (p != NULL)
		_van_free(NULL, p);

	return (ret);
}

int _van_datum_copyfree(VAN_DATUM *dst) {
	switch (dst->flags & VAN_MEM_MASK) {
		case VAN_MALLOC:
		case VAN_REALLOC:
			if (dst->data != NULL)
				_van_free(NULL, dst->data);
			dst->data = NULL;
			break;
		default: /* No actions currently */
			break;
	}
	dst->size = 0;

	return (0);
}

/* Just one allocation action */
int _van_datum_clone_once(const VAN_DATUM *src, VAN_DATUM **dstp) {
	int ret;
	VAN_DATUM *dst;
	size_t size;

	size = sizeof(VAN_DATUM) + src->size;
	ret = _van_malloc(NULL, size, &dst);
	if (ret != 0)
		return (ret);
	memcpy(dst, src, sizeof(VAN_DATUM));

	/* Null data */
	if (src->data == NULL || src->size == 0) {
		dst->data = NULL;
		return (ret);
	}

	dst->data = dst + 1;
	memcpy(dst->data, src->data, dst->size);

	return (ret);
}

/* Free the datum and data separately */
void _van_datum_free(VAN_DATUM *ptr) {
	if (ptr->data != NULL) {
		_van_free(NULL, ptr->data);
		ptr->data = NULL;
		ptr->size = 0;
	}
	_van_free(NULL, ptr);
}

/* Just one free action */
void _van_datum_free_once(VAN_DATUM *ptr) {
	_van_free(NULL, ptr);
}

/* Just free the VAN_DATUM.data */
void _van_data_free_data(VAN_DATUM *ptr) {
	if (ptr == NULL || ptr->data == NULL)
		return ;

	_van_free(NULL, ptr->data);
	ptr->data = NULL;
	ptr->size = 0;
}

int _van_datum_replace(VAN_DATUM *ptr, void *newd, size_t newlen) {
	int ret;

	VAN_ASSERT(ptr != NULL);
	ret = 0;
	/* Free old data */
	if (ptr->data != NULL) {
		_van_free(NULL, ptr->data);
		ptr->data = NULL;
		ptr->size = 0;
	}

	/* Means just free */
	if (newlen == 0)
		return (ret);

	ret = _van_malloc(NULL, newlen, &ptr->data);
	if (ret != 0)
		return (ret);
	memcpy(ptr->data, newd, newlen);
	ptr->size = newlen;

	return (ret);
}

int _van_datum_uercopy(void *d, size_t len, VAN_DATUM **vdp) {
	int ret;
	VAN_DATUM *vd;

	VAN_ASSERT(vdp != NULL);

	ret = _van_calloc(NULL, 1, sizeof(VAN_DATUM), &vd);
	if (ret != 0)
		return (ret);
	*vdp = vd;

	/* Null string */
	if (len == 0 || d == NULL)
		return (ret);

	ret = _van_malloc(NULL, len, &vd->data);
	memcpy(vd->data, d, len);
	vd->size = len;
	
	return (ret);
}

/* 
 * Default comparison for VAN_DATUM:
 * lexical comparison.
 */
int _van_datum_defcmp(const VAN_DATUM *dt1, const VAN_DATUM *dt2) {
	size_t i, limit;
	const char *p1, *p2;

	if (dt1 == NULL && dt2 == NULL)
		return (0);
	if (dt1 == NULL && dt2 != NULL)
		return (-1);
	if (dt1 != NULL && dt2 == NULL)
		return (1);

	/* Both NON-NULL case */
	limit = (dt1->size > dt2->size ? dt2->size : dt1->size);
	p1 = dt1->data;
	p2 = dt2->data;
	for (i = 0; i < limit; i++) {
		if (p1[i] != p2[i])
			return (p1[i] > p2[i] ? 1 : -1);
	}
	
	if (dt1->size == dt2->size)
		return (0);

	return (dt1->size > dt2->size ? 1 : -1);
}
