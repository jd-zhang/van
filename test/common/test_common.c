#include "test_common.h"

static char LETTERS[] = {
	'a', 'b', 'c', 'd', 'e', 'f', 'g',
	'h', 'i', 'j', 'k', 'l', 'm', 'n',
	'o', 'p', 'q', 'r', 's', 't',
	'u', 'v', 'w', 'x', 'y', 'z',
	'A', 'B', 'C', 'D', 'E', 'F', 'G',
	'H', 'I', 'J', 'K', 'L', 'M', 'N',
	'O', 'P', 'Q', 'R', 'S', 'T',
	'U', 'V', 'W', 'X', 'Y', 'Z', '_',
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'
};
static int LETTERS_SIZE = sizeof(LETTERS)/sizeof(LETTERS[0]);

int is_cygwin(){
#ifdef __CYGWIN__
	return 1;
#endif
	return 0;
}

int getrandomstring(char *buf, int fixedlen) {
	int len = 0, i;
	len = fixedlen;
	for (i = 0; i < len; i++) {
		buf[i] = LETTERS[rand() % LETTERS_SIZE];
	}
	return len;
}

int getrandomstring2(char *buf, int minlen, int maxlen) {
	int len = 0, i;

	if (minlen == maxlen)
		len = maxlen;
	else 
		len = minlen + rand() % (maxlen - minlen + 1);

	if (len == 0)
		len = 1;
	for (i = 0; i < len; i++) {
		buf[i] = LETTERS[rand() % LETTERS_SIZE];
	}
	return len;
}

int init_buf(char *buf, int len) {
	int i = 0;
	for(i = 0; i < len; i++) {
		buf[i] = LETTERS[i % LETTERS_SIZE];
	}
	return len;
}

unsigned int get_2power(unsigned int indx) {
	return 1 << indx;
}


char *getstrtime(char *buf, int len) {
	time_t cur_time;
	struct tm *tmp = NULL;
	(void)time(&cur_time);
	tmp = localtime(&cur_time);
	strftime(buf, len, "%Y:%m:%d:%H:%M:%S", tmp);
	return buf;
}

char *getstrtime2(char *buf, int len, time_t t1) {
	struct tm *tmp = NULL;
	tmp = localtime(&t1);
	strftime(buf, len, "%Y:%m:%d:%H:%M:%S", tmp);
	return buf;
}
