#ifndef _VAN_TEST_COMMON_H
#define _VAN_TEST_COMMON_H

/* Internal headers */
#include "van_int.h"

#define DEF_DIRMODE (S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH)
#define DEF_FILEMODE (S_IRWXU|S_IRGRP|S_IROTH)
#define CMD_BUF_LEN 500
#define MAX_BUF_LEN 500
#define MAX_STRBUF_LEN 1000

#define TYPES_NUM 5

/* Used for del and others*/
#define TYPE_INT 1
#define TYPE_STRING 2
#define TYPE_STRUCT 3
#define TYPE_FBYTES 4
#define TYPE_VBYTES 5

/* Used for put */
#define KEY_INT_DATA_INT 1
#define KEY_INT_DATA_STRING 2
#define KEY_INT_DATA_STRUCT 3
#define KEY_INT_DATA_FBYTES 4
#define KEY_INT_DATA_VBYTES 5
#define KEY_STRING_DATA_INT 6
#define KEY_STRING_DATA_STRING 7
#define KEY_STRING_DATA_STRUCT 8
#define KEY_STRING_DATA_FBYTES 9
#define KEY_STRING_DATA_VBYTES 10
#define KEY_STRUCT_DATA_INT 11
#define KEY_STRUCT_DATA_STRING 12
#define KEY_STRUCT_DATA_STRUCT 13
#define KEY_STRUCT_DATA_FBYTES 14
#define KEY_STRUCT_DATA_VBYTES 15
#define KEY_FBYTES_DATA_INT 16
#define KEY_FBYTES_DATA_STRING 17
#define KEY_FBYTES_DATA_STRUCT 18
#define KEY_FBYTES_DATA_FBYTES 19
#define KEY_FBYTES_DATA_VBYTES 20
#define KEY_VBYTES_DATA_INT 21
#define KEY_VBYTES_DATA_STRING 22
#define KEY_VBYTES_DATA_STRUCT 23
#define KEY_VBYTES_DATA_FBYTES 24
#define KEY_VBYTES_DATA_VBYTES 25

#define MAX_RECORD_LEN 	5242880

/* Records Number Settings */
#ifdef __CYGWIN__
#define TEST_NUM 5000
#else
#define TEST_NUM 5000
#endif

#define CHK_RET(ret, str) do { \
	int _ret = (ret); \
	if (_ret) { \
		fprintf(stderr, "[%lu][%lu] %s:%d Error(%d, %s)\n", \
		    (unsigned long)getpid(), (unsigned long)pthread_self(), \
		    __FILE__, __LINE__, _ret, (str)); \
		exit(1); \
	} \
} while(0)

#define TEST_ASSERT(expr) do { \
	if(!(expr)) { \
		printf("Assert Failure(%s:%d): "#expr, __FILE__, __LINE__); \
		exit(1); \
	} \
	}while(0)

#define TEST_ASSERT2(expr, ...) do { \
	if(!(expr)) { \
		printf(__VA_ARGS__); \
		printf("Assert Failure(%s:%d): "#expr, __FILE__, __LINE__); \
		exit(1); \
	} \
	}while(0)

extern int is_cygwin();

extern int getrandomstring(char *buf, int fixedlen);
extern int getrandomstring2(char *buf, int minlen, int maxlen);

extern char *getstrtime(char *buf, int len);
extern char *getstrtime2(char *buf, int len, time_t t1);

/* Other headers */

#endif
