/* Andrew ID - vbhutada
 *
 * This is a tiny proxy. It accepts http request.
 * It implements threading and caching. Thread can
 * read parallely from cache but only one thread 
 * can write to it at time.
 * cache.h and cache.c have the cache funtionalities
 * Cache is a global variable
 *
 * */

#include <stdio.h>

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0(X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_conn_hdr = "Proxy-Connection: close\r\n";
static const char *host_key_hdr = "Host: ";

#include "csapp.h"
#include "cache.h"

/* function executed by each thread */
void doit(int fd);
/* read request header */
void read_requesthdrs(rio_t *rp, char* freq, char *host_hdr);
/* parse the uri */
int parse_uri(char *uri, char *filename, char *host_hdr, char* port_hdr);
void *thread(void *vargp);

/* this function takes port number as input parameter
 * initializes the cache
 * opens a connection over the port number and 
 * listens for a request
 * For every connectio request executes a new thread */
int main(int argc, char **argv) 
{
    init_cache();
    int listenfd, *connfdp;
    pthread_t tid;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }
    /* ignore SIGPIPE signal */
    Signal(SIGPIPE, SIG_IGN);
    listenfd = Open_listenfd(argv[1]);
    while (1) {
	clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
	if((*connfdp = accept(listenfd, (SA *)&clientaddr, &clientlen)) < 0){
            free(connfdp);
            exit(1);
        }
        Pthread_create(&tid, NULL, thread, connfdp);
    }
}

/* This funciton detaches the new thread from the peer thread 
 * And executes the doit function for each thread
 * when the thread returns it closes the fd */
void *thread(void *vargp){
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp); //free the fd memory allocated in main
    doit(connfd);
    Close(connfd);
    return NULL;
}

/* This function executes the main functionalities of the proxy
 * It reads the request from the client
 * Checks if the cache has a valid entry
 * Else opens a connection with the server, write its response 
 * in the cache and to the client fd */

void doit(int fd) 
{
    int clientfd, len, read_size, ln1;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE]; 
    char buf1[MAXLINE], read_content[MAX_OBJECT_SIZE];
    char filename[MAXLINE], freq[MAXLINE];
    char host_hdr[MAXLINE], port_hdr[MAXLINE];
    rio_t rio, rio_1;
    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE))
        return;
    sscanf(buf, "%s %s %s", method, uri, version);
    strcpy(version, "HTTP/1.0");
    /* If request is not GET ignore */
    if (strcasecmp(method, "GET")){
        return;
    }
    /* Parse URI from GET request */
    parse_uri(uri, filename, host_hdr, port_hdr);
    sprintf(freq, "%s %s %s\r\n",method, filename, version);
    read_requesthdrs(&rio, freq, host_hdr);
    strcat(freq,user_agent_hdr);
    strcat(freq,connection_hdr);
    strcat(freq,proxy_conn_hdr);
    strcat(freq,host_key_hdr);
    strcat(freq,host_hdr);
    strcat(freq,"\r\n");
    strcat(freq,"\r\n");
    /* check if cache has a valid matching entry */
    if((len = read_cache(uri,read_content)) == 1){
        /*write cached response to the client fd */
        if((ln1 = Rio_writen(fd,read_content,sizeof(read_content))) < 0){
            return;
        }
    }
    else{ /*open connection with server fd */
        clientfd = Open_clientfd(host_hdr,port_hdr);
        Rio_readinitb(&rio_1, clientfd);
        read_size = 0;
        /* write request to server fd */
        if((ln1 = Rio_writen(clientfd, freq, strlen(freq))) < 0){
            Close(clientfd);
            return;  
        }
        memset(read_content, 0, MAX_OBJECT_SIZE);
        /* read response from server fd */
        while((len = Rio_readnb(&rio_1, buf1, MAXLINE)) > 0){
            /* write the response to client fd */
            if((ln1 = Rio_writen(fd,buf1,len)) < 0){
                return;
            }
            /* If the response size is less that the max size*/
            if((read_size+len) < MAX_OBJECT_SIZE)
                memcpy((read_content + read_size),buf1,len);
            read_size += len;
        }
        /* if reading from server fd fails*/
        if(len == -1){
            return;
        }
        /*If the size of response is less than Max size*/
        if(read_size < MAX_OBJECT_SIZE){
            write_cache(uri, read_content, read_size); //write to cache
        }
    }
}

/* read_requesthdrs - read HTTP request headers
 * ignore connection, proxy-conn, user-agent headers
 * Check for host uri. Filter out port number */
void read_requesthdrs(rio_t *rp, char* freq, char* host_hdr) 
{
    char buf[MAXLINE];
    char key[MAXLINE];
    char value[MAXLINE];
    int m;
    char *ur4;
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    while(strcmp(buf, "\r\n")) {
        sscanf(buf, "%s %s", key, value);
        /* If there is Host header keep it */
        if(!strcasecmp(key, "Host:")){
           ur4 = strstr(value, ":"); //Filter out port number
           if(ur4){
               m = ur4 - (char *)value;
           }
           else
               m = strlen(value);
            strncpy(host_hdr,value, m);
        }
        /* Ignore these headers */
        else if(strcasecmp(key, "Connection:") 
            && strcasecmp(key, "Proxy-Connection:") 
                && strcasecmp(key, "User-Agent:")){
            strcat(freq, buf);
        }
        /* read next line of header */
	Rio_readlineb(rp, buf, MAXLINE);
    }
    return;
}

/*
 * parse_uri - parse URI into filename, host, port number and version
 * copy the paresed value in the request parameters
 * Considers only http request
 */

int parse_uri(char *uri, char *filename, char *host_hdr, char* port_hdr) 
{
    char* ur1;
    char *ur2;
    char *ur3;
    int n = 0;
    int m = 0;
    ur1 = uri+7;
    ur2 = strstr(ur1, "/");
    if(!ur2){
        strcat(ur1, "/");
        ur2 = strstr(ur1, "/");
    }
    n = ur2 - ur1;
    ur3 = strstr(ur1,":");
    if(ur3 && ur3 < ur2){ //there is a port number
       n = ur3 - ur1;
       m = ur2 - ur3 - 1;
      strncpy(port_hdr,ur3 + 1,m);
    }
    else{ //if there is no port number use default
        strcpy(port_hdr,"80");
    }
    strncpy(host_hdr,ur1,n);
    strcpy(filename,ur2);
    return 0;
}

/*
 * Rio_writen - wrapper function for rioi_writen
 * return -1 for all error types as the proxy should 
 * not exit. only thread should return
 */
int Rio_writen(int fd, void *usrbuf, size_t n)
{
    if (rio_writen(fd, usrbuf, n) != n){
        return -1;
    }
    return n;
}

/*
 * Rio_readnb - wrapper function for rio_readnb
 * return -1 for all error types. As the proxy should 
 * not exit only the thread should return
 */
ssize_t Rio_readnb(rio_t *rp, void *usrbuf, size_t n)
{
    ssize_t rc;
    if ((rc = rio_readnb(rp, usrbuf, n)) < 0){
        return -1;
    }
    return rc;
}

/*
 * Rio_readlineb - wrapper function for rio_readlinenb
 * return 0 for all erros. As the proxy should not
 * exit and only the thread should return
 */
ssize_t Rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen)
{
    ssize_t rc;

    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0){
        return 0;
    }
    return rc;
}

