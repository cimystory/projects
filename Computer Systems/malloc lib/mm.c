/* Name - Vrushali Bhutada
 * Andrew ID - vbhutada
 * This is a 64-bit clean allocator based on segregated free lists,
 * first-fit placement, and boundary tag coalescing.
 * Each block is aligned to doubleword (8 byte) boundaries.
 * Minimum block size is 24 bytes. Each free block contains a pointer
 * to next and previous free block in its list. The header pointers of
 * each list are placed in the prolouge block. There are 13 free lists
 * in this emplementation.
 * In the program the pointers are pointing to the header of each block
 * This design does not have an epilogue block
 * There is a list end marker with value 0/1 placed right after the prologue
 * it is pointed to by last node of every list
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include "contracts.h"

#include "mm.h"
#include "memlib.h"

/* If you want debugging output, use the following macro.  When you hand
 *  * in, remove the #define DEBUG line. */
/*  #define DEBUG */
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
#else
# define dbg_printf(...)
#endif

/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(p) (((size_t)(p) + (ALIGNMENT-1)) & ~0x7)

/* Macro to mm_checkheap */
#define checkheap(lineno) //mm_checkheap(lineno)

/* Basic constants and macros */
#define WSIZE       4       /* Word and header/footer size (bytes) */ 
#define DSIZE       8       /* Double word size (bytes) */
#define CHUNKSIZE  (1<<8)  /* Extend heap by this amount (bytes) */  

#define MAX(x, y) ((x) > (y)? (x) : (y))  

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc)) 

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p))            
#define PUT(p, val)  (*(unsigned int *)(p) = (val))    

/* Read and write the pointer value stored in p */
#define GETDL(p)      (*(char* *)(p))
#define PUTDL(p, val) (*(char* *)(p) = (val))

/*Given block ptr bp,compute address of pointers to next and prev free block*/
#define PREV_FBLK_PTR(bp) ((char *)(((char *)(bp) + (3 * WSIZE))))
#define NEXT_FBLK_PTR(bp) ((char *)((char *)(bp) + (WSIZE)))

/*Given block ptr bp,compute the address of previous and next free block*/
#define PREV_FREE_BLOCK(bp) (char *)(GETDL(PREV_FBLK_PTR(bp)))
#define NEXT_FREE_BLOCK(bp) (char *)(GETDL(NEXT_FBLK_PTR(bp)))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)                   
#define GET_ALLOC(p) (GET(p) & 0x1)                    

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp))                      
#define FTRP(bp) ((char *)(bp) + GET_SIZE(bp) - WSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE((char *)(bp)))
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - WSIZE)))

/* Macros to define the bucket size of segregated fee list */
#define BKT1 249
#define BKT2 1025
#define BKT3 2609
#define BKT4 3609
#define BKT5 4609
#define BKT6 5609
#define BKT7 6609 
#define BKT8 7609
#define BKT9 8609
#define BKT10 9609
#define BKT11 10609
#define BKT12 11609

/* Global variables */
static char *heap_listp = 0;  /* Pointer to first block */  
static int lfree = 0; /* Used to check if last block in heap is free*/

/* Function prototypes for internal helper routines */
static void *extend_heap(size_t words); /* Expand heap size*/
static void place(void *bp, size_t asize); /*Place an allocated block in heap*/
static void *find_fit(size_t asize); /* Find first free block of size asize */
static void *coalesce(void *bp); /* Coalesce adjacent free blocks into one */
static int in_heap(const void *p); /* Check if pointer is in heap */

/* EnqueueBlock - This function places a free block in front of the list.
 * It checks the size of the free block and places the block in its
 * segregated list.
 * Updates the previous and next pointers of the block, updates the
 * header pointer to the list and previous pointer of next free
 * block in the list 
 * */
inline static char *enqueueBlock(char * const block){
    void * fhp; 

    /* Set list header pointer based on size of block bieng enqueued */
    if(GET_SIZE(block) < BKT1)
        fhp = heap_listp;
    else if(GET_SIZE(block)<BKT2)
        fhp = heap_listp + (DSIZE);
    else if(GET_SIZE(block)<BKT3)
        fhp = heap_listp + (2*DSIZE);
    else if(GET_SIZE(block)<BKT4)
        fhp = heap_listp + (3*DSIZE);
    else if(GET_SIZE(block)<BKT5)
        fhp = heap_listp + (4*DSIZE);
    else if(GET_SIZE(block)<BKT6)
        fhp = heap_listp + (5*DSIZE);
    else if(GET_SIZE(block)<BKT7)
        fhp = heap_listp + (6*DSIZE);
    else if(GET_SIZE(block)<BKT8)
        fhp = heap_listp + (7*DSIZE);
    else if(GET_SIZE(block)<BKT9)
        fhp = heap_listp + (8*DSIZE);
    else if(GET_SIZE(block)<BKT10)
        fhp = heap_listp + (9*DSIZE);
    else if(GET_SIZE(block)<BKT11)
        fhp = heap_listp + (10*DSIZE);
    else if(GET_SIZE(block)<BKT12)
        fhp = heap_listp + (11*DSIZE);
    else
        fhp = heap_listp + (12*DSIZE);

    /*Update next pointer value of block*/
    PUTDL(NEXT_FBLK_PTR(block),GETDL(NEXT_FBLK_PTR(fhp)));
    PUTDL(PREV_FBLK_PTR(block),(fhp)); /*Update prev pointer value of block*/
    PUTDL(NEXT_FBLK_PTR(fhp),block); /* Update list header pointer */
    void* nextblk = NEXT_FREE_BLOCK(block);
    if(!GET_ALLOC(nextblk)){   /*If next free block is allocated, update*/
        PUTDL(PREV_FBLK_PTR(nextblk),block); /*pointer to prev free block*/
    }
   return block;
}

/* 
 * mm_init - Initialize the memory manager
 * The pointers to segregated free lists are placed 
 * between the header and footer of prologue block
 */
int mm_init(void) 
{
    lfree = 0;
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(29*WSIZE)) == (void *)-1) 
        return -1;
    PUT(heap_listp, PACK((14*DSIZE), 1)); /* Prologue header */

    /*Headers of segregated free list. Initialized pointing to epilogue block*/
    PUTDL((heap_listp + (1*WSIZE)),(heap_listp + (28*WSIZE)));
    PUTDL(heap_listp + (3*WSIZE),(heap_listp + (28*WSIZE)));
    PUTDL(heap_listp + (5*WSIZE),(heap_listp + (28*WSIZE))); 
    PUTDL(heap_listp + (7*WSIZE),(heap_listp + (28*WSIZE)));
    PUTDL(heap_listp + (9*WSIZE),(heap_listp + (28*WSIZE)));
    PUTDL(heap_listp + (11*WSIZE),(heap_listp + (28*WSIZE)));
    PUTDL(heap_listp + (13*WSIZE),(heap_listp + (28*WSIZE))); 
    PUTDL(heap_listp + (15*WSIZE),(heap_listp + (28*WSIZE)));
    PUTDL(heap_listp + (17*WSIZE),(heap_listp + (28*WSIZE)));
    PUTDL(heap_listp + (19*WSIZE),(heap_listp + (28*WSIZE)));
    PUTDL(heap_listp + (21*WSIZE),(heap_listp + (28*WSIZE)));
    PUTDL(heap_listp + (23*WSIZE),(heap_listp + (28*WSIZE)));
    PUTDL(heap_listp + (25*WSIZE),(heap_listp + (28*WSIZE)));

    PUT(heap_listp + (27*WSIZE), PACK((14*DSIZE),1)); /*Prologue footer*/
    PUT(heap_listp + (28*WSIZE), PACK(0, 1));     /* Epilogue header */ 
    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) 
        return -1;
    return 0;
}

/* 
 * malloc - Allocate a block with at least size bytes of payload 
 */
void *malloc(size_t size) 
{
    size_t asize;      /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    void *bp;
    if (heap_listp == 0){
        mm_init();
    }
    /* Ignore spurious requests */
    if (size == 0)
        return NULL;
    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= (2*DSIZE))                                          
        asize = 3*DSIZE;                                        
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {  
        place(bp, asize);                  
        void * retp = (bp + (WSIZE));
        return retp;
    }
    /* last block on heap is free - get only the required momory*/
    asize = asize - lfree;
    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize,CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)  
        return NULL;
    asize = asize + lfree;
    lfree = 0;
    place(bp, asize);     
    checkheap(__LINE__);                
    return (bp + (WSIZE));
}

/* 
 * free - Free a block 
 */
void free(void *bp)
{
    if (bp == 0) 
        return;

    bp = (void *)bp - WSIZE;

    if(!in_heap(bp)){
        return;
    }
    size_t size = GET_SIZE(HDRP(bp));
    if (heap_listp == 0){
        mm_init();
    }
    /*update header and footer of block */
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

/*
 * realloc - Reallocates size memory at a new pointer
 * and copies old data to it 
 */
void *realloc(void *ptr, size_t size)
{
    size_t oldsize;
    void *newptr;
    void *nptr;

    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0) {
        mm_free(ptr);
        return 0;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if(ptr == NULL) {
        return malloc(size);
    }

    nptr = ptr - WSIZE;
    /*If pointer is not in heap or free*/
    if(!in_heap(nptr) || !GET_ALLOC(nptr)){
        return 0;
    }

    newptr = malloc(size);
    /* If realloc() fails the original block is left untouched  */
    if(!newptr) {
        return 0;
    }

    /* Copy the old data. */
    oldsize = GET_SIZE(HDRP(ptr));
    if(size < oldsize) oldsize = size;
    memcpy(newptr, ptr, oldsize);

    /* Free the old block. */
    mm_free(ptr);

    return newptr;
}

/* Calloc - Allocates memory for an array of nmemb 
 * elements of size bytes
 * All bytes are initialized to 0
 */
void *calloc(size_t nmemb, size_t size){

    void *newptr;
    size_t num = nmemb * size;

    newptr = malloc(num);
 
    if(!newptr) //if malloc fails
        return NULL;

    memset(newptr, 0, num);
    return newptr;
}

/*
 * checkblock - Check alignment of each block and
 *              if header and footer values match
 */
static void checkblock(void *bp) 
{
    if ((size_t)(bp + WSIZE) % 8){
        printf("Error: %p is not doubleword aligned\n", bp);
        exit(1);
    }
    if (GET(HDRP(bp)) != GET(FTRP(bp))){
        printf("Error: %p header do not match footer\n", bp);
       exit(1);
    }
}

/* 
 * mm_checkheap - Check the heap for correctness.
 * Check correctness of header
 * Calls checkblock for all blocks iteratively
 * Check if next and previous pointers are consistent
 * Check if the pointers in free list are in_heap
 * Compare the number of free blocks iteratively and in free list
 * Check if end marker (0/1) is in place
 */
void mm_checkheap(int lineno)  
{ 
    void *bp = heap_listp;
    int x = 0;
    int nfblk = 0; /* free block counter */

    /* Check alignment of header */
    if((GET_SIZE(HDRP(heap_listp)) != 14*DSIZE)||!GET_ALLOC(HDRP(heap_listp))){
        printf("Error - Line: %d Bad prologue header\n", lineno);
        exit(1);
    }
    bp =  heap_listp + (28*WSIZE);

    /*Check if end marker is in place */
    if(GET_SIZE(bp) && !GET_ALLOC(bp)){
        printf("List end pointer not in place\n");
        exit(1);
    }
    /* Check each block iteratively */
    for (bp+=WSIZE; in_heap(bp); bp = NEXT_BLKP(bp)){
        checkblock(bp);
        /*Count number of free blocks*/
        if(!GET_ALLOC(bp))
            nfblk+=1;   
    }

    /* Check all the segregated free lists */
    for(x = 0; x < 13; x++){
        for (bp = NEXT_FREE_BLOCK(heap_listp+(x*DSIZE));GET_SIZE(HDRP(bp)) > 0;
            bp = NEXT_FREE_BLOCK(bp)){

            /* Check if block is marked as free */
            if(GET_ALLOC(HDRP(bp))){
                printf("Error: %p - Block in free list is allocated\n", bp);
                exit(1);
            }
            if(!in_heap(NEXT_FREE_BLOCK(bp))){
                printf("Error: %p - Next block pointer not in heap\n", bp);
                exit(1);
            }

            if(!in_heap(PREV_FREE_BLOCK(bp))){
                printf("Error: %p - Previous block pointer not in heap\n",bp);
                exit(1);
            }

            /* Given bp, check if prev pointer of the next block points to it*/
            /*Ignore if next block is epilogue*/
            if(GET_SIZE(NEXT_FREE_BLOCK(bp)) > 0){
                if(bp != PREV_FREE_BLOCK(NEXT_FREE_BLOCK(bp))){
                    printf("Error: %p Pointer" 
                        "in next block inconsistent\n", bp);
                    exit(1);
                }
            }
            /* Given bp, check if next pointer of the prev block points to it*/
            /*Ignore if prev block is header*/
            if(PREV_FREE_BLOCK(bp) > (heap_listp + (28*WSIZE)))
                if(bp != NEXT_FREE_BLOCK(PREV_FREE_BLOCK(bp))){
                    printf("Error: %p pointer" 
                        "in previous block inconsistent\n", bp);
                    exit(1);
                }
            nfblk-=1; /*Decrement free count block*/

            /* Check size of block belong to right list */
            if(x==0){
                if (GET_SIZE(bp) >= BKT1){
                    printf("Incorrect size in list - 0\n");
                    exit(1);
                }
            }
            else if(x==1){                
                if (GET_SIZE(bp) > BKT2){
                    printf("Incorrect size in list - 1\n");
                    exit(1);
                }
            }
            else if (x<12) {
                unsigned int check = x*1000+609;
                if(GET_SIZE(bp) > check){
                    printf("Incorrect size in list - %d\n",x);
                    exit(1);
                }
            }
        }
    }
    /* Check if number of free blocks in heap and free list match*/
    if(nfblk){
        printf("Number of free blocks don't match! - %d\n", nfblk);
        exit(1);
    }
}

/* 
 * The remaining routines are internal helper routines 
 */

/* 
 * extend_heap - Extend heap with free block and 
 * return its block header pointer
 */
inline static void *extend_heap(size_t words){
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE; 
    if ((long)(bp = mem_sbrk(size)) == -1)  
        return NULL;                                        

    /* Initialize free block header/footer and the epilogue header */
    PUT(bp, PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    /* Coalesce if the previous block was free */
    return coalesce(bp);                                          
}

/*
 * deleteBlock - delete block from list 
 * Update previous and next pointers of
 * next and previous block
 */

inline static void deleteBlock(void *bp){
    char *block_prev = PREV_FREE_BLOCK(bp);
    char *block_next = NEXT_FREE_BLOCK(bp);
    if(!GET_ALLOC(block_next))
        PUTDL(PREV_FBLK_PTR(block_next), block_prev);
    PUTDL(NEXT_FBLK_PTR(block_prev), block_next);
}

/*
 * coalesce - Boundary tag coalescing. Return ptr to coalesced block header
 */

inline static void *coalesce(void *bp){
    size_t next_alloc;
    char* rptr;
 
    if(!in_heap(FTRP(bp)+WSIZE)) //Check if the next free block is in heap
        next_alloc = (unsigned int)1;
    else
        next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));

    size_t prev_alloc = GET_ALLOC(bp - WSIZE);
    size_t size = GET_SIZE(HDRP(bp));
    if (prev_alloc && next_alloc) { /* Previous and next block allocated */
         rptr = enqueueBlock(bp);
    }
    else if(prev_alloc && !next_alloc){ /* If only Next block is free */
        char *nextbp = NEXT_BLKP(bp);
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        deleteBlock(nextbp); /* remove next from list */
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size,0));
        /* add new block to enqueue */
        rptr = enqueueBlock(bp);
    }

    else if (!prev_alloc && next_alloc) { /* If only previous block is free */
        
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        deleteBlock(PREV_BLKP(bp));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        rptr = enqueueBlock(PREV_BLKP(bp));
    }
    else {        /* If previous and next block are free */
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + 
            GET_SIZE(FTRP(NEXT_BLKP(bp)));
        deleteBlock(NEXT_BLKP(bp));
        deleteBlock(PREV_BLKP(bp));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
        rptr = enqueueBlock(bp);
    }
    return rptr;
}

/* 
 * place - Place block of asize bytes at start of free block bp 
 *         and split if remainder would be at least minimum block size
 */
inline static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));   
    deleteBlock(bp);
    if ((csize - asize) >= (3*DSIZE)) { 
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize-asize, 0));
        PUT(FTRP(bp), PACK(csize-asize, 0));
        enqueueBlock(bp);
    }
    else { 
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/* 
 * find_fit - Find a fit for a block with asize bytes
 * update lfree with size of last free block if it is
 * also the last block in allocated memory  
 */
inline static void *find_fit(size_t asize)
{ 
    /* First-fit search */
    void *bp;
    char *hp = NULL; /*Pointer to track if last free block on */
    int i = 0; /* offset for segregated header */

    /* offset to header of segregated list of minimum fitting bucket size */
    if(asize < BKT1) i = 0;
    else if (asize < BKT2) i=1;
    else if (asize < BKT3) i=2;
    else if (asize < BKT4) i=3;
    else if (asize < BKT5) i=4;
    else if (asize < BKT6) i=5;
    else if (asize < BKT7) i=6;
    else if (asize < BKT8) i=7;
    else if (asize < BKT9) i=8;
    else if (asize < BKT10) i=9;
    else if (asize < BKT11) i=10;
    else if (asize < BKT12) i=11;
    else i = 12;

    for(; i < 13; i++){ /*Check lists iteratively with increasing bucket size*/
        for (bp = NEXT_FREE_BLOCK(heap_listp + (i * DSIZE)); 
            GET_SIZE(HDRP(bp)) > 0; bp = NEXT_FREE_BLOCK(bp)){
            if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))){
                return bp;
            }
            hp = bp;
        }
    }
    if((hp != NULL)){
        bp = NEXT_BLKP(hp);
        if(!in_heap(bp)){ /* If last free block is last block in memomry */
            lfree = GET_SIZE(hp);
        }
    }
    return NULL; /* No fit */
}

/*
* Return whether the pointer is in the heap
*/
inline static int in_heap(const void *p) {
    return p <= mem_heap_hi() && p >= mem_heap_lo();
}
