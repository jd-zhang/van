#include "van_int.h"

#ifdef USE_TC_MALLOC
#define g_calloc tc_calloc
#define g_malloc tc_malloc
#define g_realloc tc_realloc
#define g_free tc_free
#elif defined(USE_JE_MALLOC)
#define g_calloc je_calloc
#define g_malloc je_malloc
#define g_realloc je_realloc
#define g_free je_free
#else
#define g_calloc calloc
#define g_malloc malloc
#define g_realloc realloc
#define g_free free
#endif

int _van_calloc(void *arg, size_t nelem, size_t elsize, void *storep) {
	void *p;
	int ret;

	VAN_ASSERT(storep != NULL);

	p = g_calloc(nelem, elsize);
	if (p == NULL) {
		ret = _van_getsys_error();
	} else {
		ret = 0;
		*(void **)storep = p;
	}
	alloc_stats.ncalloc++;

	return (ret);
}

int _van_malloc(void *arg, size_t size, void *storep) {
	void *p;
	int ret;

	VAN_ASSERT(storep != NULL);

	p = g_malloc(size);
	if (p == NULL) {
		ret = _van_getsys_error();
	} else {
		ret = 0;
		*(void **)storep = p;
	}
	alloc_stats.nmalloc++;

	return (ret);
}

int _van_realloc(void *arg, void *ptr, size_t size, void *storep) {
	int ret;
	void *p;

	VAN_ASSERT(storep != NULL);

	p = g_realloc(ptr, size);
	if (p == NULL) {
		ret = _van_getsys_error();
	} else {
		ret = 0;
		*(void **)storep = p;
	}
	alloc_stats.nrealloc++;

	return (ret);
}

int _van_strdup(void *arg, const char *str, char **destp) {
	int ret;
	size_t sz;
	void *buf;
	
	ret = 0;
	if (str == NULL) {
		*destp = NULL;
		return (ret);
	}

	sz = strlen(str) + 1;
	ret = _van_malloc(arg, sz, &buf);
	if (ret != 0)
		return (ret);
	memcpy(buf, str, sz);
	*destp = buf;

	return (ret);
}

void _van_free(void *arg, void *ptr) {
	if (ptr != NULL)
		g_free(ptr);
	alloc_stats.nfree++;
}
