#ifndef _VAN_IO_H
#define _VAN_IO_H

#define ABS_ROOT '/'

struct _van_iofile {
	char *path;
	int fd;
	int oflag;
	mode_t mode;	/* File mode */
	off_t woff;	/* Write offset: When to use? */
	off_t roff;	/* Read offset: Whento use? */
};

#endif
