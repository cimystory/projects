/* 
 * andrewid - vbhtuada
 * This program implements a naive cache
 * The structure of the cache is an array of struct - cache_line
 * It is one dimensional array.
 * The stucture of cache_line is defined in cache.h
 * The cache is thread safe as it allows conccurent read.
 * But only one thread can write to the cache at one point of time
 * LRU is used for eviction. It is not perfectly LRU as write to the
 * LRU can be done concurrently by multiple threads
 *
 */
#include "cache.h"

/* number of lines in cache */
static int max_line = MAX_CACHE_SIZE/sizeof(cache_line);
static cache_line *cache;
int readcnt = 0;
sem_t mutex, w;
int least = 0;

/*
 * Initialize cache - mallocs cache =
 * size of each line * max number of lines
 * set valid of each line to 0
 */
void init_cache(){
    sem_init(&mutex, 0, 1);
    sem_init(&w, 0, 1);
    int i;
    cache = (cache_line *)malloc(max_line * sizeof(cache_line));
    for(i = 0; i < max_line; i++){
        cache[i].valid = 0;
    }
}

/*
 * read_cahce - check each line for validity. 
 * If content is valid compare tag value which is uri
 * If hit, update the content of *response
 * Return 1 if cache hit else 2
 * We use semaphores for concurrnet reading of cache
 * and letting a thread only after all threads are done reading
 * We use readers lock even when we are updating the LRU value
 * So it is almost LRU eviction and not strictly LRU
 * LRU is updated after every Hit
 * */
int read_cache(char *uri, char *response){
    P(&mutex);
    readcnt++;
    if(readcnt == 1) // First in 
        P(&w);
    V(&mutex);
    int i;
    for(i = 0; i < max_line; i++){
        if(cache[i].valid == 1){
            if(!strcmp(cache[i].tag, uri)){
                memset(response, 0, MAX_OBJECT_SIZE);
                memcpy(response,cache[i].content, cache[i].len);
                cache[i].LRU = least++;
                P(&mutex);
                readcnt--;
                if(readcnt == 0)
                    V(&w);
                V(&mutex);
                return 1;
            }
        }
    }
    P(&mutex);
    readcnt--; 
    if(readcnt == 0)
        V(&w);
    V(&mutex);
    return 2;
}

/*
 * write_cache - write to a cahce
 * Checks if there is vacancy in the cache if valid is 0
 * If cache is full call findLRU
 * write at the index returned by findLRU
 * This is a thread safe fuction. Only one thread can write
 * to the cache at a time
 * LRU updated after every write
 * */

int write_cache(char *uri, char *content1, int len){
    P(&w);
    int i;
    int min;
    for(i = 0; i < max_line; i++){
        if(cache[i].valid == 0){  //Check if line is free
            cache[i].valid = 1;
            strcpy(cache[i].tag, uri);
            memset(cache[i].content, 0 , MAX_OBJECT_SIZE);
            memcpy(cache[i].content, content1, len);
            cache[i].LRU = least++;
            cache[i].len = len;
            V(&w); 
            return 1;
        }
    }
    //if cache is full
    min = findLRU(cache);
    cache[min].valid = 1;
    strcpy(cache[min].tag,uri);
    memset(cache[min].content, 0 , MAX_OBJECT_SIZE);
    memcpy(cache[min].content,content1, len);
    cache[min].LRU = least++;
    cache[min].len = len;
    V(&w);
    return 1;
}

/*
 * findLRU - checks the LRU value of all the lines in
 * the cache. Returns index with minimum LRU value
 * */
int findLRU(cache_line *cache){
    int i;
    int smallest = least;
    int minIndex = 0;
    for(i = 0; i < max_line; i++){
        if(smallest > cache[i].LRU){
            smallest = cache[i].LRU;
            minIndex = i;
        }
    }
    return minIndex;
}
