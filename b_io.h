/**************************************************************
* Class:  CSC-415-01 Fall 2021
* Names: Jasmine Stapleton-Hart 
*	Arianna Yuan 
*	Nathaniel Miller 
* Student IDs:
* 	921356953
*	920898911
*	922024360
* GitHub Name: arianna-y
* 
* Group Name: Vile System
* Project: Basic File System
*
* File: b_io.h
*
* Description: Interface of basic I/O functions
*
**************************************************************/

#ifndef _B_IO_H
#define _B_IO_H
#include <fcntl.h>

typedef int b_io_fd;

b_io_fd b_open (char * filename, int flags);
int b_read (b_io_fd fd, char * buffer, int count);
int b_write (b_io_fd fd, char * buffer, int count);
int b_seek (b_io_fd fd, off_t offset, int whence);
int b_close (b_io_fd fd);

#endif

