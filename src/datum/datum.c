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
#include "van_int.h"
#include "van.h"

size_t _van_datum_size(const VAN_DATUM *ptr) {
	size_t sz;

	sz = 0;
	if (ptr != 0) {
		sz += sizeof(VAN_DATUM);
		if (ptr->data != NULL) {
			if (ptr->ulen != 0)
				sz += ptr->ulen;
			else if (ptr->size != 0)
				sz += ptr->size;
		}
	}

	return (sz);
}

/* Allocate the datum and data separately */
int _van_datum_clone(const VAN_DATUM *src, VAN_DATUM **dstp) {
	int ret;
	VAN_DATUM *dst;

	VAN_ASSERT(dstp != NULL);
	ret = 0;
	/* Null src */
	if (src == NULL) {
		*dstp = NULL;
		return (ret);
	}

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
	/* Null src */
	if (src == NULL)
		return (ret);
	/* Null data */
	if (src->size == 0 || src->data == NULL) {
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

	/* Null dst */
	if (dst == NULL)
		return (0);

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
	
	VAN_ASSERT(dst != NULL);
	ret = 0;
	/* Null src */
	if (src == NULL) {
		*dstp = NULL;
		return (ret);
	}

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
	if (ptr != NULL && ptr->data != NULL) {
		_van_free(NULL, ptr->data);
		ptr->data = NULL;
		ptr->size = 0;
	}
	if (ptr != NULL)
		_van_free(NULL, ptr);
}

/* Just one free action */
void _van_datum_free_once(VAN_DATUM *ptr) {
	if (ptr != NULL)
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
	if (newlen == 0 || newd == NULL)
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
	const unsigned char *p1, *p2;

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

/*
 * Default comparison for VAN_DATUM:
 * lexical comparison.
 */
int _van_table_datum_defcmp(const VAN_TABLE *table,
    const VAN_DATUM *dt1, const VAN_DATUM *dt2) {
	table = NULL;
	return _van_datum_defcmp(dt1, dt2);
}
