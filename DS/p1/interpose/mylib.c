#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
 
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>

// The following line declares a function pointer with the same prototype as the open function.  
int (*orig_open)(const char *pathname, int flags, ...);  // mode_t mode is needed when flags includes O_CREAT
int (*orig_close)(int filedes);
ssize_t (*orig_read)(int filedes, void *buf, size_t nbyte);
ssize_t (*orig_write)(int filedes, const void *buf, size_t nbyte);
off_t (*orig_lseek)(int filedes, off_t offset, int whence);
int (*orig_unlink)(const char *pathname);

// This is our replacement for the open function from libc.
int open(const char *pathname, int flags, ...) {
	mode_t m=0;
	if (flags & O_CREAT) {
		va_list a;
		va_start(a, flags);
		m = va_arg(a, mode_t);
		va_end(a);
	}
	// we just print a message, then call through to the original open function (from libc)
	fprintf(stderr, "mylib: open called for path %s\n", pathname);
	return orig_open(pathname, flags, m);
}

int close (int filedes){
	fprintf(stderr, "mylib: close called for fd - %d\n", filedes);
	return orig_close(filedes);
}

ssize_t read(int filedes, void *buf, size_t nbyte){
	fprintf(stderr, "mylib: read called for fd - %d\n", filedes);
	return orig_read(filedes, buf, nbyte);
}

ssize_t write(int filedes, const void *buf, size_t nbyte){
	fprintf(stderr, "mylib: write called for fd - %d\n", filedes);
	return orig_write(filedes, buf, nbyte);
}
off_t lseek(int filedes, off_t offset, int whence){
	fprintf(stderr, "mylib: lseek called for fd - %d\n", filedes);
	return orig_lseek(filedes, offset, whence);
}
int unlink(const char *pathname){
	fprintf(stderr, "mylib: unlink called for path - %s\n", pathname);
	return orig_unlink(pathname);
}
// This function is automatically called when program is started
void _init(void) {
	// set function pointer orig_open to point to the original open function
	orig_open = dlsym(RTLD_NEXT, "open");
	orig_close = dlsym(RTLD_NEXT, "close");
	orig_read = dlsym(RTLD_NEXT, "read");
	orig_write = dlsym(RTLD_NEXT, "write");
	orig_lseek = dlsym(RTLD_NEXT, "lseek");
	orig_unlink = dlsym(RTLD_NEXT, "unlink");
	fprintf(stderr, "Init mylib\n");
}


