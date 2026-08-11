#include "test_common.h"

static int misc_t1_1_cmp(const void *d1, const void *d2) {
	const char **str1, **str2;

	str1 = (const char **)d1;
	str2 = (const char **)d2;
	return strcmp(*str1, *str2);

}

int misc_t1_1() {
	int ret;
	VAN_STRING_ARRAY *arr;
	size_t i, cnt, len;
	char strbuf[MAX_BUF_LEN];

	ret = 0;
	len = 15;
	cnt = 10;

	ret = _van_strarray_alloc(&arr, 20);
	for (i = 0; i < cnt; i++) {
		memset(strbuf, 0, sizeof(strbuf));
		getrandomstring(strbuf, len);
		fprintf(stderr, "%s\n", strbuf);
		ret = _van_strarray_add(arr, strbuf);
	}
	fprintf(stderr, "========= before sort ========\n");
	ret = _van_strarray_print(arr, stderr, "\n");

	ret = _van_strarray_sort(arr, misc_t1_1_cmp);
	fprintf(stderr, "========= after sort ========\n");
	ret = _van_strarray_print(arr, stderr, "\n");

	ret = _van_strarray_free(arr);
	return (ret);
}
