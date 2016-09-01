#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include "dirtree.h"

#define OPEN 1
#define CLOSE 2
#define WRITE 3
#define READ 4
#define LSEEK 5
#define UNLINK 6
#define XSTAT 7
#define GETDIRENT 8
#define GETDIRTREE 9
#define MAXMSGLEN 100
#define OFFSET 66666 // used to offset FDs sent by server
#pragma pack(0)

//Structs used for communication between client and server

typedef struct{
    int total_len;
    int opcode;
    unsigned char data[0];
} request_header_t;

typedef struct{
    int flag;
    mode_t mode;
    int filename_len;
    unsigned char data[0];
} open_request_header_t;

typedef struct{
    int fd;
} close_request_header_t;

typedef struct{
    int err_no;
    int buf_size;
    unsigned char data[0];
} read_response_header_t;

typedef struct{
    int fd;
    size_t nbyte;
} read_request_header_t;

typedef struct{
    int fd;
    size_t nbyte;
    int buf_size;
    unsigned char data[0];
} write_request_header_t;

typedef struct{
    int fd;
    off_t offset;
    int whence;
} lseek_request_header_t;

typedef struct{
    int filename_len;
    unsigned char data[0];
} unlink_request_header_t;

typedef struct{
    int vers;
    int name_len;
    unsigned char data[0];
} xstat_request_header_t;

typedef struct{
    int err_no;
    int r_val;
    int buf_len;
    unsigned char data[0];
} xstat_response_header_t;

typedef struct{
    int err_no;
    int r_val;
    int buf_size;
    unsigned char data[0];
} getdirent_response_header_t;

typedef struct{
    int fd;
    size_t nbyte;
    long basep;
} getdirent_request_header_t;

typedef struct{
    int path_len;
    unsigned char data[0];
} getdirtree_request_header_t;

typedef struct{
    int total_len;
    int name_len;
    int num_sd;
    unsigned char data[0];
} getdirtree_response_t;

int init_client();
char output[MAXMSGLEN] = {0};
int socketfd = 0;
char buf[MAXMSGLEN+1];
int rv;
int open_fd;

// The following line declares a function pointer with the same prototype as the open function.  
int (*orig_open)(const char *pathname, int flags, ...);  // mode_t mode is needed when flags includes O_CREAT
int (*orig_close)(int filedes);
ssize_t (*orig_read)(int filedes, void *buf, size_t nbyte);
ssize_t (*orig_write)(int filedes, const void *buf, size_t nbyte);
off_t (*orig_lseek)(int filedes, off_t offset, int whence);
int (*orig_unlink)(const char *pathname);
int (*orig___xstat)(int vers, const char* name, struct stat* buf);
int (*orig_getdirentries)(int fd, char* buf, int nbytes, long* basep);
struct dirtreenode* (*orig_getdirtree)( const char *path );
void (*orig_freedirtree)( struct dirtreenode* dt );
struct dirtreenode* recursive_tree();
void recursive_free(struct dirtreenode* dt);

/*
*  Replacement for the system calls from libic.
*/
int open(const char *pathname, int flags, ...) {
    mode_t m=0;
    if (flags & O_CREAT) {
        va_list a;
        va_start(a, flags);
        m = va_arg(a, mode_t);
        va_end(a);
    }
    fprintf(stderr, "Open called from client - %s\n",pathname);
    int len_f = strlen(pathname);
    int total_len = sizeof(request_header_t)+sizeof(open_request_header_t)+len_f+1;
    //malloc length sizeof the buffer to be sent
    void *buffer = malloc(total_len);
    //populate the content of the buffer
    request_header_t *header = (request_header_t *)buffer;
    open_request_header_t *open_header = (open_request_header_t*)header->data;
    char *open_filename = (char *)open_header->data;
    header->opcode = OPEN;
    header->total_len = total_len;
    open_header->filename_len = len_f;
    open_header->flag = flags;
    open_header->mode = m;
    strcpy(open_filename,pathname);
    //send buffer to the server
    send(socketfd, buffer, total_len, 0);
    rv = recv(socketfd, buf, 8, 0);   	    // get message
    if (rv<0) err(1,0);
    buf[rv]=0;
    open_fd = *(int *) buf;		    // read response
    errno = *(int *)(buf+4);		    // update error message
    //Offset valid FD received from server
    if(open_fd >= 0){
        open_fd = open_fd + OFFSET;
    }
    free(buffer);
    return open_fd;
}

int close (int filedes){
    //If FD was not received from server - call orig_close
    if(filedes < OFFSET){
        return orig_close(filedes);
    }
    int total_len = sizeof(request_header_t) + sizeof(int);
    void *buffer = malloc(total_len);
    request_header_t *header = (request_header_t *)buffer;
    close_request_header_t *close_header  = (close_request_header_t *) header->data;
    header->opcode = CLOSE;
    header->total_len = total_len;
    close_header->fd = filedes - OFFSET;
    send(socketfd, buffer, total_len, 0);
    //get 8 bytes as this is the expected message length
    rv = recv(socketfd, buf, 8, 0);
    if (rv<0) err(1,0);                     
    buf[rv]=0;
    int close_fd = *(int *) buf;
    errno = *(int *)(buf+4);
    free(buffer);
    return close_fd;
}

ssize_t read(int filedes, void *rbuf, size_t nbyte){
    //If FD was not received from server - call orig_read
    if(filedes < OFFSET){
        fprintf(stderr, "Calling orig_read\n");
        return orig_read(filedes, rbuf, nbyte);
    }
    int total_len = sizeof(request_header_t) + sizeof(read_request_header_t);
    void *buffer = malloc(total_len);
    request_header_t *header = (request_header_t *) buffer;
    read_request_header_t *read_req_header = (read_request_header_t*)header->data;
    header->opcode = READ;
    header->total_len = total_len;
    read_req_header->fd = filedes - OFFSET;
    read_req_header->nbyte = nbyte;
    send(socketfd, buffer, total_len, 0);
    int rcv_len = 0;
    void *res_buf;
    // read the first few bytes as these will have the total length of 
    // the buffer to be malloc-ed
    if((rv = recv(socketfd, buf, 8, 0)) > 0){
        total_len = *(int *)buf;
        res_buf = malloc(total_len);
        memcpy(res_buf,buf,rv);
        rcv_len = rv;
        // receive rest of the response
        while(rcv_len < total_len){
            int diff = total_len - rcv_len;
            if((rv = recv(socketfd, res_buf+rcv_len, diff, 0)) > 0){
                rcv_len += rv;
            }
        }
    }
    //unmarshalling response
    header = (request_header_t *) res_buf;
    read_response_header_t * read_res_header = (read_response_header_t *) header->data;
    int rnbyte = read_res_header->buf_size;
    errno = read_res_header->err_no;
    //copy content in rbuf only if read was success
    if(rnbyte >= 0){
        memcpy(rbuf,read_res_header->data,rnbyte);
    }
    free(buffer);
    free(res_buf);
    return rnbyte;
}

ssize_t write(int filedes, const void *wbuf, size_t nbyte){
    //If FD was not received from server - call orig_write
    if(filedes < OFFSET){
        return orig_write(filedes, wbuf, nbyte);
    }
    // packing request in a buffer
    fprintf(stderr, "WRITE sending call to server\n");
    int total_len = sizeof(request_header_t) + sizeof(write_request_header_t)  + nbyte;
    void *buffer = malloc(total_len);
    request_header_t *header = (request_header_t *)buffer;
    write_request_header_t *write_header = (write_request_header_t*)header->data;
    void *write_buf = (void *)write_header->data;
    header->opcode = WRITE;
    header->total_len = total_len;
    write_header->fd = filedes - OFFSET;
    write_header->nbyte = nbyte;
    write_header->buf_size = nbyte;
    memcpy(write_buf,wbuf,nbyte);
    send(socketfd, buffer, total_len, 0);
    rv = recv(socketfd, buf, 8, 0);	// get message
    if (rv<0) err(1,0);             	
    buf[rv]=0;
    size_t nby  = *(int *) buf;
    errno = *(int *)(buf+4);
    free(buffer);
    return nby;
}

off_t lseek(int filedes, off_t offset, int whence){
    //If FD was not received from server - call orig_lseek
    if(filedes < OFFSET){
        return orig_lseek(filedes, offset, whence);
    }
    int total_len = sizeof(request_header_t) + sizeof(lseek_request_header_t);
    void *buffer = malloc(total_len);
    request_header_t *header = (request_header_t *)buffer;
    lseek_request_header_t *lseek_header = (lseek_request_header_t *)header->data;
    header->opcode = LSEEK;
    header->total_len = total_len;
    lseek_header->fd = filedes - OFFSET;
    lseek_header->offset = offset;
    lseek_header->whence = whence;
    send(socketfd, buffer, total_len, 0);
    rv = recv(socketfd, buf, 8, 0);   // get message
    if (rv<0) err(1,0);
    buf[rv]=0;
    int r_offset = *(off_t *) (buf+4);
    errno = *(int *)(buf);
    free(buffer);
    return r_offset;
}

int unlink(const char *pathname){
    int len_f = strlen(pathname);
    int total_len = sizeof(request_header_t) + sizeof(unlink_request_header_t) + len_f + 1;
    void *buffer = malloc(total_len);
    request_header_t *header = (request_header_t *)buffer;
    unlink_request_header_t *unlink_header = (unlink_request_header_t*)header->data;
    char *unlink_filename = (char *)unlink_header->data;
    header->opcode = UNLINK;
    header->total_len = total_len;
    unlink_header->filename_len = len_f;
    strcpy(unlink_filename,pathname);
    send(socketfd, buffer, total_len, 0);
    rv = recv(socketfd, buf, 8, 0);
    if (rv<0) err(1,0);
    buf[rv]=0;
    int unlink_fd = *(int *) buf;
    errno = *(int *)(buf+4);
    free(buffer);
    return unlink_fd;
}

int __xstat(int vers, const char* name, struct stat* xbuf)
{
    int name_len = strlen(name);
    int total_len = sizeof(request_header_t) + sizeof(xstat_request_header_t) + name_len + 1;
    void *buffer = malloc(total_len);
    request_header_t *header = (request_header_t *) buffer;
    xstat_request_header_t *xstat_req_header = (xstat_request_header_t*)header->data;
    char* request_name = (char *) xstat_req_header->data;
    header->opcode = XSTAT;
    header->total_len = total_len;
    xstat_req_header->vers = vers;
    xstat_req_header->name_len = name_len;
    strcpy(request_name,name);
    send(socketfd, buffer, total_len, 0);
    int rcv_len = 0;
    void *res_buf;
    // Receive response, first 4 bytes contain the size of 
    // the total response
    if((rv = recv(socketfd, buf, 8, 0)) > 0){
        total_len = *(int *)buf;
        res_buf = malloc(total_len);
        memcpy(res_buf,buf,rv);
        rcv_len = rv;
        while(rcv_len < total_len){
            int diff = total_len - rcv_len;
            if((rv = recv(socketfd, res_buf+rcv_len, diff, 0)) > 0){
                rcv_len += rv;
            }
        }
    }
    // unpack data sent by the server
    header = (request_header_t *) res_buf;
    xstat_response_header_t * xstat_res_header = (xstat_response_header_t *) header->data;
    int buf_size = xstat_res_header->buf_len;
    int ret_val = xstat_res_header->r_val;
    errno = xstat_res_header->err_no;
    memcpy(xbuf,xstat_res_header->data,buf_size);
    free(buffer);
    free(res_buf);
    return ret_val;
}

int getdirentries(int fd, char* gbuf, int nbytes, long* basep)
{
    // If fd was not returned by the server
    if(fd < OFFSET){
        return orig_getdirentries(fd, gbuf, nbytes, basep);
    }
    int total_len = sizeof(request_header_t) + sizeof(getdirent_request_header_t);
    void *buffer = malloc(total_len);
    request_header_t *header = (request_header_t *) buffer;
    getdirent_request_header_t *gdirent_req_header = (getdirent_request_header_t*)header->data;
    header->opcode = GETDIRENT;
    header->total_len = total_len;
    gdirent_req_header->fd = fd - OFFSET;
    gdirent_req_header->nbyte = nbytes;
    gdirent_req_header->basep = *basep;
    send(socketfd, buffer, total_len, 0);
    int rcv_len = 0;
    void *res_buf;
    // read the firt few bytes, the first four have the total_len
    if((rv = recv(socketfd, buf, 8, 0)) > 0){
        total_len = *(int *)buf;
        res_buf = malloc(total_len);
        memcpy(res_buf,buf,rv);
        rcv_len = rv;
        while(rcv_len < total_len){
            int diff = total_len - rcv_len;
            if((rv = recv(socketfd, res_buf+rcv_len, diff, 0)) > 0){
                rcv_len += rv;
            }
        }
    }
    // un-marshall response
    header = (request_header_t *) res_buf;
    getdirent_response_header_t * gdirent_res_header = (getdirent_response_header_t *) header->data;
    int buf_size = gdirent_res_header->buf_size;
    errno = gdirent_res_header->err_no;
    int r_val = gdirent_res_header->r_val;
    memcpy(gbuf,gdirent_res_header->data,buf_size);
    free(buffer);
    free(res_buf);
    return r_val;
}

struct dirtreenode* getdirtree( const char *path )
{   
    // marshall data into buffer
    int path_len = strlen(path);
    int total_len = sizeof(request_header_t) + sizeof(getdirtree_request_header_t) + path_len + 1;
    void *buffer = malloc(total_len);
    request_header_t *header = (request_header_t *) buffer;
    getdirtree_request_header_t *gdn_req_header = (getdirtree_request_header_t*)header->data;
    char* path_name = (char *) gdn_req_header->data;
    header->opcode = GETDIRTREE;
    header->total_len = total_len;
    gdn_req_header->path_len = path_len;
    strcpy(path_name,path);
    // send request to server
    send(socketfd, buffer, total_len, 0);
    free(buffer);
    // construct tree from response and return root
    return recursive_tree();
}

/*
* Recursively recv every node in DFS pattern
*/
struct dirtreenode* recursive_tree(){
    int rcv_len = 0;
    void *res_buf;
    if((rv = recv(socketfd, buf, sizeof(getdirtree_response_t), 0)) > 0){
        int total_len = *(int *)buf;
        // If getdirtree for root returned error
        if(total_len == 0){
            return NULL;
        }
        res_buf = malloc(total_len);
        memcpy(res_buf,buf,rv);
        rcv_len = rv;
        while(rcv_len < total_len){
            int diff = total_len - rcv_len;
            if((rv = recv(socketfd, res_buf+rcv_len, diff, 0)) > 0){
                rcv_len += rv;
            }
        }
    }
    // Un-marshall data and create a node dtnode
    getdirtree_response_t* gdtres = (getdirtree_response_t *) res_buf;
    struct dirtreenode* dtnode = malloc(sizeof(struct dirtreenode));        
    int nchild = gdtres->num_sd;
    char* dirname = malloc(gdtres->name_len + 1);
    memcpy(dirname,gdtres->data,gdtres->name_len);
    dirname[gdtres->name_len] = 0;
    dtnode->name = dirname;
    dtnode->num_subdirs = nchild;
    dtnode->subdirs = malloc(nchild*sizeof(struct dirtreenode *));
    int i = 0;
    // Recursively parse through each child node of dtnode
    for(i=0; i<nchild; i++){
        dtnode->subdirs[i] = recursive_tree();
    }
    free(res_buf);
    return dtnode;
}
/*
* call to function that free the tree recursively
*/
void freedirtree( struct dirtreenode* dt )
{
    recursive_free(dt);
}

/*
* DFS search, start by freeing leaves
* and move towards the root
*/
void recursive_free(struct dirtreenode* dt){
    int nchild = dt->num_subdirs;
    int i = 0;
    for (i = 0; i < nchild; i++){
        recursive_free(dt->subdirs[i]);
    }
    free(dt->subdirs);
    free(dt->name);
    free(dt);
}

//Init function is automatically called when program is started
void _init(void) {
    socketfd = init_client();
    orig_open = dlsym(RTLD_NEXT, "open");
    orig_close = dlsym(RTLD_NEXT, "close");
    orig_read = dlsym(RTLD_NEXT, "read");
    orig_write = dlsym(RTLD_NEXT, "write");
    orig_lseek = dlsym(RTLD_NEXT, "lseek");
    orig_unlink = dlsym(RTLD_NEXT, "unlink");
    orig___xstat = dlsym(RTLD_NEXT, "__xstat");
    orig_getdirentries = dlsym(RTLD_NEXT, "getdirentries");
    orig_getdirtree = dlsym(RTLD_NEXT, "getdirtree");
    orig_freedirtree = dlsym(RTLD_NEXT, "freedirtree");
    fprintf(stderr, "Init mylib\n");
}

int init_client() {
    char *serverip;
    char *serverport;
    unsigned short port;
    int sockfd;
    struct sockaddr_in srv;
    // Get environment variable indicating the ip address of the server
    serverip = getenv("server15440");
    if (!serverip){
        fprintf(stderr, "Environment variable server15440 not found.  Using 127.0.0.1\n");
        serverip = "127.0.0.1";
    }
    
    // Get environment variable indicating the port of the server
    serverport = getenv("serverport15440");
    if (!serverport){
        fprintf(stderr, "Environment variable serverport15440 not found.  Using 15440\n");
        serverport = "15440";
    }
    port = (unsigned short)atoi(serverport);
    
    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);	// TCP/IP socket
    if (sockfd<0) err(1, 0);			// in case of error
    
    // setup address structure to point to server
    memset(&srv, 0, sizeof(srv));		// clear it first
    srv.sin_family = AF_INET;			// IP family
    srv.sin_addr.s_addr = inet_addr(serverip);	// IP address of server
    srv.sin_port = htons(port);			// server port

    // actually connect to the server
    rv = connect(sockfd, (struct sockaddr*)&srv, sizeof(struct sockaddr));
    if (rv<0) err(1,0);
    return sockfd;
}
