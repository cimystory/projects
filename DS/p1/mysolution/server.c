#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <sys/stat.h>
#include "../include/dirtree.h"

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

// structs used in marshalling and un-marshalling of data
#pragma pack(0)
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

/*
 * Function definition
 */
void do_process(int opcode, request_header_t* header, int sessfd);
void recursive_gettree(struct dirtreenode *, int sessfd);

int main(int argc, char**argv) {
    char buf[MAXMSGLEN];
    char *serverport;
    unsigned short port;
    int sockfd, sessfd, rv, i;
    struct sockaddr_in srv, cli;
    socklen_t sa_size;
    // Get environment variable indicating the port of the server
    serverport = getenv("serverport15440");
    if (serverport) port = (unsigned short)atoi(serverport);
    else port=15440;
    
    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);    // TCP/IP socket
    if (sockfd<0) err(1, 0);             // in case of error
    
    // setup address structure to indicate server port
    memset(&srv, 0, sizeof(srv));        // clear it first
    srv.sin_family = AF_INET;            // IP family
    srv.sin_addr.s_addr = htonl(INADDR_ANY);    // don't care IP address
    srv.sin_port = htons(port);          // server port

    // bind to our port
    rv = bind(sockfd, (struct sockaddr*)&srv, sizeof(struct sockaddr));
    if (rv<0) err(1,0);
    
    // start listening for connections
    rv = listen(sockfd, 5);
    if (rv<0) err(1,0);
    
    // main server loop, handle clients one at a time
    while(1) {        
        // wait for next client, get session socket
        sa_size = sizeof(struct sockaddr_in);
        sessfd = accept(sockfd, (struct sockaddr *)&cli, &sa_size);
        if (sessfd<0) err(1,0);
        // fork into child after accepting a connection
        pid_t pid = fork();
        if(pid == 0){ // in child
            close(sockfd);
            int rcv_len = 0;
            int total_len = sizeof(request_header_t);
            void * buffer;
            int opcode;
            while((rv=recv(sessfd, buf, total_len , 0)) > 0){
                total_len = *(int *)buf;
                buffer = malloc(total_len);
                memcpy(buffer, buf, rv);
                rcv_len = rv;
                while(rcv_len < total_len){
                    int diff = total_len - rcv_len;
                    if((rv=recv(sessfd, buffer+rcv_len, diff , 0)) > 0){
                        rcv_len += rv;
                    }
                }
                request_header_t *header = (request_header_t *) buffer;
                opcode = header->opcode;
                //call the method for processing the request
                do_process(opcode, header, sessfd);                                                                                                                           
                free(buffer);
                rcv_len = 0;
                total_len = sizeof(request_header_t);
            }
            if (rv<0) err(1,0);
            close(sessfd);
            exit(0);
        }
    }   
    fprintf(stderr,"server shutting down cleanly\n");
    close (sessfd);
    close(sockfd);
    return 0;
}

/*
* This method makes system calls based on the opcode value
* and sends the retrun value to the client
*/
void do_process(int opcode, request_header_t *header, int sessfd){
    if(opcode == OPEN){
        open_request_header_t *open_header = (open_request_header_t*)header->data;
        int file_len = open_header->filename_len;
        int flag = open_header->flag;
        mode_t mode = open_header->mode;
        char *open_filename = (char *)open_header->data;
        open_filename[file_len] = 0;
        int fd_open = open(open_filename,flag,mode);
        int err_no = errno; 
        // malloc buffer of size 8, for return value and errno
        void *rbuf = malloc(8);
        memcpy(rbuf,&fd_open,sizeof(int));
        memcpy(rbuf+4,&err_no,sizeof(int));
        send(sessfd, rbuf, 8,0);
        free(rbuf);
    }
    else if(opcode == CLOSE){
        close_request_header_t *close_header = (close_request_header_t*)header->data;
        int fd = close_header->fd;
        int close_fd = close(fd);
        void *rbuf = malloc(8);
        memcpy(rbuf,&close_fd,sizeof(int));
        memcpy(rbuf+4,&errno,sizeof(int));
        send(sessfd, rbuf, 8, 0);
        free(rbuf);
    }
    else if(opcode == READ){
        read_request_header_t *read_header = (read_request_header_t*)header->data;
        int fd = read_header->fd;
        size_t nbyte = read_header->nbyte;
        void *rbuf = malloc(nbyte);
        size_t rnbyte = read(fd,rbuf,nbyte);
        // use flag to handle length of read buffer in case of negative return value
        int flag = 0;
        if((int)rnbyte > 0){
            flag = rnbyte;
        }
        int r_total_len = sizeof(read_response_header_t) + sizeof(request_header_t) + flag;
        void *res_buffer = malloc(r_total_len);
        request_header_t *header = (request_header_t *)res_buffer;
        read_response_header_t *read_res_header = (read_response_header_t *) header->data;
        void* read_buf = (void *)read_res_header->data;
        header->opcode = READ;
        header->total_len = r_total_len;
        read_res_header->buf_size = rnbyte;
        read_res_header->err_no = errno;
        // if read returned -1, nothing to copy into buf
        if((int) rnbyte > 0){
            memcpy(read_buf,rbuf,rnbyte);
        }
        send(sessfd, res_buffer, r_total_len, 0);
        free(res_buffer);
        free(rbuf);
    }
    else if(opcode == WRITE){
        write_request_header_t *write_header = (write_request_header_t *)header->data;
        int fd = write_header->fd;
        size_t nbyte = write_header->nbyte;
        void *buf = (void *)write_header->data;
        int nbwrite = write(fd,buf,nbyte);
        void *rbuf = malloc(8);
        memcpy(rbuf,&nbwrite,sizeof(int));
        memcpy(rbuf+4,&errno,sizeof(int));
        send(sessfd, rbuf, 8, 0);
        free(rbuf);            
    }
    else if(opcode == LSEEK){
        lseek_request_header_t *lseek_header = (lseek_request_header_t*)header->data;
        // read data sent by client
        int fd = lseek_header->fd;
        off_t offset = lseek_header->offset;
        int whence = lseek_header->whence;
        off_t r_offset = lseek(fd, offset, whence);
        void *rbuf = malloc(12);
        memcpy(rbuf,&errno,sizeof(int));
        memcpy(rbuf+4,&r_offset,sizeof(off_t));
        send(sessfd, rbuf, 8, 0);
        free(rbuf);    
    }
    else if(opcode == UNLINK){
        unlink_request_header_t *unlink_header = (unlink_request_header_t*)header->data;
        int file_len = unlink_header->filename_len;
        char *ul_filename = (char *)unlink_header->data;
        ul_filename[file_len] = 0;
        int fd_unlink = unlink(ul_filename);
        void *rbuf = malloc(8);     //pack return value and errono
        memcpy(rbuf,&fd_unlink,sizeof(int));
        memcpy(rbuf+4,&errno,sizeof(int));
        send(sessfd, rbuf, 8,0);
        free(rbuf);
    }
    else if(opcode == XSTAT){
        // unmarshall data
        xstat_request_header_t *xstat_header = (xstat_request_header_t*)header->data;
        int vers = xstat_header->vers;
        int name_len = xstat_header->name_len;
        char *name = (char *)xstat_header->data;
        name[name_len] = 0;
        void *rbuf = malloc(sizeof(struct stat));
        // call to xstat
        size_t r_val = __xstat(vers,name,rbuf);
        // marshall response
        int r_total_len = sizeof(xstat_response_header_t) + sizeof(request_header_t) + sizeof(struct stat);
        void *res_buffer = malloc(r_total_len);
        request_header_t *header = (request_header_t *)res_buffer;
        xstat_response_header_t *xstat_res_header = (xstat_response_header_t *) header->data;
        void* xstat_buf = (void *)xstat_res_header->data;
        header->opcode = XSTAT;
        header->total_len = r_total_len;
        xstat_res_header->buf_len = sizeof(struct stat);
        xstat_res_header->err_no = errno;
        xstat_res_header->r_val = r_val;
        memcpy(xstat_buf,rbuf,sizeof(struct stat));
        // send response to client
        send(sessfd, res_buffer, r_total_len, 0);
        free(res_buffer);
        free(rbuf);
    }
    else if(opcode == GETDIRENT){
        getdirent_request_header_t *gdirent_header = (getdirent_request_header_t*)header->data;
        int fd = gdirent_header->fd;
        int nbyte = gdirent_header->nbyte;
        long basep = gdirent_header->basep;
        void *rbuf = malloc(nbyte);
        // call to getdirentries
        int rnbyte = getdirentries(fd,rbuf,nbyte,&basep);        
        int r_total_len = sizeof(getdirent_response_header_t) + sizeof(request_header_t) + rnbyte;
        // packe data to be sent to the server
        void *res_buffer = malloc(r_total_len);
        request_header_t *header = (request_header_t *)res_buffer;
        getdirent_response_header_t *gdirent_res_header = (getdirent_response_header_t *) header->data;
        void* read_buf = (void *)gdirent_res_header->data;
        header->opcode = GETDIRENT;
        header->total_len = r_total_len;
        gdirent_res_header->buf_size = rnbyte;
        gdirent_res_header->err_no = errno;
        gdirent_res_header->r_val = rnbyte;
        memcpy(read_buf,rbuf,rnbyte);
        send(sessfd, res_buffer, r_total_len, 0);
        free(res_buffer);       
        free(rbuf);
    }
    else if(opcode == GETDIRTREE){
        getdirtree_request_header_t *gdn_header = (getdirtree_request_header_t*)header->data;
        int path_len = gdn_header->path_len;
        char *path = gdn_header->data;
        path[path_len] = 0;
        struct dirtreenode* gdn;
        gdn = getdirtree(path);
        // if getdirtree encounters error
        if(gdn == NULL){
            int total_len = 0;
            send(sessfd, &total_len, 4,0);
        }
        else{
            recursive_gettree(gdn,sessfd);
            //Free the tree using the library call
            freedirtree(gdn);
        }
    }
}

void recursive_gettree(struct dirtreenode* gdn, int sessfd){
    int nchild = gdn->num_subdirs;
    int name_len = strlen(gdn->name);
    int total_len = sizeof(getdirtree_response_t) + name_len; 
    // marshall node into rbuf
    void *rbuf = malloc(total_len);
    getdirtree_response_t* gdt_res = (getdirtree_response_t*) rbuf;
    char* filename = (char *)gdt_res->data;
    gdt_res->total_len = total_len;
    gdt_res->name_len = name_len;
    gdt_res->num_sd = nchild;
    memcpy(filename,gdn->name,name_len);
    // send marshalled node to client
    send(sessfd, rbuf, total_len, 0);
    free(rbuf);
    // recurse through every child of the node
    int i = 0;
    for(i=0; i<nchild; i++){
        recursive_gettree(gdn->subdirs[i] ,sessfd);
    }
}
