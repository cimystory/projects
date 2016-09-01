/*
 * andrewid - vbhutada
 * cache.h - prototype and definiton for cache.c
 */
#include<stdio.h>

#define MAX_CACHE_SIZE 1049000 //Max cache size
#define MAX_OBJECT_SIZE 102400 //Max object size

#include "csapp.h"

/* Struct used for each cache line
 * valid checks if the cache content is valid
 * tag value will be the url
 * content will contain the web object
 * LRU updates after every read and write
 * len stores the length of the content
 * */
typedef struct{
    int valid;
    char tag[MAXLINE];
    char content[MAX_OBJECT_SIZE];
    int LRU;
    int len;
} cache_line;

/*Helper functions */
/* read from cache */
int read_cache(char *uri, char *response);
/* write to cache */
int write_cache(char *uri, char *content, int len);
/* find least recently used line*/
int findLRU(cache_line *cache);
/* initialize cache */
void init_cache();
