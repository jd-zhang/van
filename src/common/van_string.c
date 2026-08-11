#include "van_int.h"

int _van_strarray_alloc(VAN_STRING_ARRAY **arrp, size_t initsz) {
	int ret;
	size_t sz;
	VAN_STRING_ARRAY *arr;

	arr = NULL;
	ret = 0;
	sz = initsz;
	if (sz == 0)
		sz = DEF_STRARR_INITCNT;

	ret = _van_calloc(NULL, 1, sizeof(VAN_STRING_ARRAY), &arr);
	CHK_ERR(ret);

	arr->capacity = sz;
	arr->size = 0;
	ret = _van_calloc(NULL, sz, sizeof(char *), &arr->array);
	CHK_ERR(ret);

	VAN_ASSERT(arrp != NULL);
	*arrp = arr;
err:
	if (ret != 0) {
		if (arr != NULL)
			_van_free(NULL, arr);
	}
	return (ret);
}

int _van_strarray_free(VAN_STRING_ARRAY *arr) {
	int ret;
	size_t i;

	ret = 0;
	if (arr == NULL)
		return (ret);

	for (i = 0; i < arr->size; i++) {
		VAN_ASSERT(arr->array[i] != NULL);
		_van_free(NULL, arr->array[i]);
		arr->array[i] = NULL;
	}
	_van_free(NULL, arr->array);
	arr->array = NULL;
	_van_free(NULL, arr);

	return (ret);
}

int _van_strarray_add(VAN_STRING_ARRAY *arr, const char *str) {
	int ret;
	char *bstr;
	size_t i, new_capacity, len;

	ret = 0;
	bstr = NULL;
	if (str == NULL)
		return (ret);

	if (arr->size >= arr->capacity) {
		new_capacity = arr->capacity * 2;
		ret = _van_realloc(NULL, arr->array, 
		    new_capacity * sizeof(char *), &arr->array);
		for (i = arr->size; i < new_capacity; i++)
			arr->array[i] = NULL;
		arr->capacity = new_capacity;
	}
	
	len = strlen(str);
	ret = _van_malloc(NULL, len + 1, &bstr);
	CHK_ERR(ret);
	memcpy(bstr, str, len + 1);
	arr->array[arr->size] = bstr;
	arr->size++;

err:
	return (ret);
}

int _van_strarray_print(VAN_STRING_ARRAY *arr, FILE *f, const char *delim) {
	int ret;
	size_t i;

	ret = 0;
	if (arr == NULL)
		return (ret);
	if (delim == NULL)
		delim = "";

	for (i = 0; i < arr->size; i++) {
		if (arr->array[i] != NULL) {
			fprintf(f, "%s%s", arr->array[i], delim);
		}
	}

	return (ret);
}

int _van_strarray_sort(VAN_STRING_ARRAY *arr, qsortcmpfunc func) {
	int ret;

	ret = 0;
	if (arr == NULL)
		return (ret);
	qsort(arr->array, arr->size, sizeof(char *), func);

	return (ret);
}
