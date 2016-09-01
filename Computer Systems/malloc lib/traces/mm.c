/* 
 * Simple, 32-bit and 64-bit clean allocator based on implicit free
 * lists, first-fit placement, and boundary tag coalescing, as described
 * in the CS:APP3e text. Blocks must be aligned to doubleword (8 byte) 
 * boundaries. Minimum block size is 16 bytes. 
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
/* #define DEBUG */
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

#define checkheap(lineno)// mm_checkheap(lineno)

/*
 * If NEXT_FIT defined use next fit search, else use first-fit search 
 */
#define NEXT_FITx

/* Basic constants and macros */
#define WSIZE       4       /* Word and header/footer size (bytes) */ 
#define DSIZE       8       /* Double word size (bytes) */
#define CHUNKSIZE  (1<<12)  /* Extend heap by this amount (bytes) */  

#define MAX(x, y) ((x) > (y)? (x) : (y))  

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc)) 

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p))            
#define PUT(p, val)  (*(unsigned int *)(p) = (val))    


/*--------------------------------------------------------------------*/
/* Read and write a DSIZE word at address p */
#define GETD(p)      (*(unsigned long *)(p))
#define PUTD(p, val) (*(unsigned long *)(p) = (val))

#define GETDL(p)      (*(char* *)(p))
#define PUTDL(p, val) (*(char* *)(p) = (val))

/* Pointer to pointers of previous and next free block address*/
#define PREV_FBLK_PTR(bp) ((char *)(((char *)(bp) + (3 * WSIZE))))
#define NEXT_FBLK_PTR(bp) ((char *)((char *)(bp) + (WSIZE)))

/* Pointer to the previous and next block*/
#define PREV_FREE_BLOCK(bp) (char *)(GETDL(PREV_FBLK_PTR(bp)))
#define NEXT_FREE_BLOCK(bp) (char *)(GETDL(NEXT_FBLK_PTR(bp)))
/* -------------------------------------------------------------------*/

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)                   
#define GET_ALLOC(p) (GET(p) & 0x1)                    

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp))                      
#define FTRP(bp) ((char *)(bp) + GET_SIZE(bp) - WSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE((char *)(bp)))
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - WSIZE)))

/* Global variables */
static char *heap_listp = 0;  /* Pointer to first block */  
#ifdef NEXT_FIT
static char *rover;           /* Next fit rover */
#endif

/* Function prototypes for internal helper routines */
static void *extend_heap(size_t words);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static int in_heap(const void *p);

inline static char *enqueueBlock(char * const block){

    REQUIRES(block != NULL);
    REQUIRES(in_heap(block));
//    printf("ENQ\n");
//    printf("Address of block being enqueued - %p, current pointer in heap_list - %p, and size of this free block - %d\n",block, GETDL(NEXT_FBLK_PTR(heap_listp)), GET_SIZE(block));
    PUTDL(NEXT_FBLK_PTR(block),GETDL(NEXT_FBLK_PTR(heap_listp)));
    PUTDL(PREV_FBLK_PTR(block),heap_listp);

    PUTDL(NEXT_FBLK_PTR(heap_listp),block);
//    printf("Address of block being enqueued - %p, current pointer in heap_list - %p, and size of this free block - %d\n",block, GETDL(NEXT_FBLK_PTR(heap_listp)), GET_SIZE(block));
    /* If there is a free block next*/
  //  char * nextblk;
    char* nextblk = NEXT_FREE_BLOCK(block);
    if(!GET_ALLOC(nextblk)){
        PUTDL(PREV_FBLK_PTR(nextblk),block);
//        printf("Not the first free block\n");
    }
//   printf("Enqueue first free block is - %p\n", NEXT_FREE_BLOCK(heap_listp));
//   printf("Enqueue next block pointer of the first block - %p\n", NEXT_FREE_BLOCK(block));
//   checkheap(2);
   return block;
}


/* 
 * mm_init - Initialize the memory manager 
 */
int mm_init(void) 
{
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(5*WSIZE)) == (void *)-1) 
        return -1;
   // PUT(heap_listp, 0);                          /* Alignment padding */
    PUT(heap_listp, PACK((2*DSIZE), 1)); /* Prologue header */
    PUTDL((heap_listp + (1*WSIZE)),(heap_listp + (4*WSIZE))); /*Pointer to next free */
    PUT(heap_listp + (3*WSIZE), PACK((2*DSIZE), 1)); /* Prologue footer */ 
    PUT(heap_listp + (4*WSIZE), PACK(0, 1));     /* Epilogue header */
    //heap_listp += (1*WSIZE);
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
    char *bp;      
    checkheap(5);
//    printf("In malloc  size - %zu\n", size);
    if (heap_listp == 0){
//        printf("Heap being inited\n");
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
//    printf("Size of asize - %zu\n", asize);
    /* Search the free list for a fit */
    //printf("Value of heap - %p", NEXT_FREE_BLOCK(heap_listp));
//    printf("Before call to find_first, value in heap - %p\n", NEXT_FREE_BLOCK(heap_listp));
    if ((bp = find_fit(asize)) != NULL) {  
        place(bp, asize);                  
        char * retp = (bp + (WSIZE));
        checkheap(1);
//        printf("***** Malloc return address - %p\n", retp);
        return retp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize,CHUNKSIZE);                 
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)  
        return NULL;                                  
    place(bp, asize);
    checkheap(1);
//    printf("**** Malloc return address - %p\n", (bp + WSIZE));                               
    return (bp + (WSIZE));
} 

/* 
 * free - Free a block 
 */
void free(void *bp)
{
//   printf("YOU HAVE ENTERED THE FREE ZONE!\n");
    bp = (char *)bp - WSIZE;
//    printf("Value of block to be freed - %p\n", bp);
    if (bp == 0) 
        return;
    if(!in_heap(bp)){
        return;
    }

    size_t size = GET_SIZE(HDRP(bp));
    if (heap_listp == 0){
        mm_init();
    }

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
    checkheap(6);
}

/*
 * realloc - Naive implementation of realloc
 */
void *realloc(void *ptr, size_t size)
{
    size_t oldsize;
    void *newptr;

    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0) {
        mm_free(ptr);
        return 0;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if(ptr == NULL) {
        return mm_malloc(size);
    }

    newptr = mm_malloc(size);

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

/* 
 * mm_checkheap - Check the heap for correctness. Helpful hint: You
 *                can call this function using mm_checkheap(__LINE__);
 *                to identify the line number of the call site.
 */
void mm_checkheap(int lineno)  
{ 
    lineno = lineno; /* keep gcc happy */
    printf("*****************************\n");
    printf("Value in heap_listp - %p\n", NEXT_FREE_BLOCK(heap_listp));
    char *bp;
    printf("*** The Free List **\n");
    for (bp = NEXT_FREE_BLOCK(heap_listp); GET_SIZE(HDRP(bp)) > 0; bp = NEXT_FREE_BLOCK(bp)) {
            printf("Alloc status of block - %d\n",GET_ALLOC(HDRP(bp)));
            printf("Size of free block - %d\n", GET_SIZE(HDRP(bp)));
        }
    printf("The block list\n");
    for (bp = (heap_listp+(5*WSIZE)) ; in_heap(bp) ; bp = NEXT_BLKP(bp)) {
        printf("Address of current block - %p", HDRP(bp));
        printf("Size and alloc of block in heap - %d & %d\n", GET_SIZE(HDRP(bp)), GET_ALLOC(HDRP(bp)));
        printf("Header of next block in heap - %p\n", NEXT_BLKP(bp));  
     }
     printf("***********************************************8\n");
//    char *np = NEXT_FREE_BLOCK(heap_listp);
//    printf("Size of first free block - %d\n", GET_SIZE(np));
}

/* 
 * The remaining routines are internal helper routines 
 */

/* 
 * extend_heap - Extend heap with free block and return its block pointer
 */
static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE; 
    if ((long)(bp = mem_sbrk(size)) == -1)  
        return NULL;                                        

//    printf("This is extend. Word - %zu, size - %zu\n", words, size);
    /* Initialize free block header/footer and the epilogue header */
    PUT(bp, PACK(size, 0));         /* Free block header */   
    PUT(FTRP(bp), PACK(size, 0));         /* Free block footer */   
//  PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */ 
//    printf("In Extend heap -- Value at header - %d\n", GET_SIZE(bp));
    /* Coalesce if the previous block was free */
    return coalesce(bp);                                          
}

inline static void deleteBlock(void *bp){
    char *block_prev = PREV_FREE_BLOCK(bp);
    char *block_next = NEXT_FREE_BLOCK(bp);
//    printf("In delete value of heap_list - %p\n", heap_listp);
    if(!GET_ALLOC(block_next))
        PUTDL(PREV_FBLK_PTR(block_next), block_prev);
    PUTDL(NEXT_FBLK_PTR(block_prev), block_next);
//    printf("In delete value of heap_list - %p\n", heap_listp);
}


/*
 * coalesce - Boundary tag coalescing. Return ptr to coalesced block
 */

static void *coalesce(void *bp){
size_t next_alloc;
//    checkheap(1);
    if(!in_heap(FTRP(bp)+WSIZE)){
        next_alloc = (unsigned int)1;
//        printf("Coalesce - here we check if it is in heap\n");
//        return enqueueBlock(bp);
    }
    else
        next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
//    if(GET_SIZE(FTRP(PREV_BLKP(bp))) > 0 )   
    size_t prev_alloc = GET_ALLOC(bp - WSIZE);
//    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

 //   printf("Prev alloc - %zu, Next alloc - %zu, current size - %zu\n", prev_alloc, next_alloc, size); 
 //   printf("\n");
    if (prev_alloc && next_alloc) {            /* Case 1 */
 //       printf("Both are allocated\n");
        return enqueueBlock(bp);
    }

    else if (prev_alloc && !next_alloc) {      /* If only next is free */
  //      printf("Only next is free\n");
        char *nextbp = NEXT_BLKP(bp); 
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size,0));
        /* remove next from list */
        deleteBlock(nextbp);
        /* add new block to enqueue */
        char * rptr = enqueueBlock(bp);
        return rptr;
    }

    else if (!prev_alloc && next_alloc) {      /* If only previous is free */
 //       printf("Only previous is free\n");
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
        /* Do nothing*/
    }

    else {                                     /* If both are free */
 //       printf("Both are empty\n");
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + 
            GET_SIZE(FTRP(NEXT_BLKP(bp)));
//        printf("Total size to be freed - %zu\n",size);
        deleteBlock(NEXT_BLKP(bp));
        deleteBlock(PREV_BLKP(bp));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
//        printf("Address of the new free block - %p\n", bp);
        /* Remove both prev and next free block from the list */
 //       deleteBlock(NEXT_BLKP(bp));
 //       deleteBlock(PREV_BLKP(bp));
 //       printf("HElllllo!\n");
        return enqueueBlock(bp);
        /* Add block to enqueue */
    }
    return bp;
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
 //       printf("Calling enqueue from place. Value of header of bp - %d\n", GET_SIZE(bp));
        enqueueBlock(bp);
    }
    else { 
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
        //deleteBlock(bp);
    }
}

/* 
 * find_fit - Find a fit for a block with asize bytes 
 */
inline static void *find_fit(size_t asize)
{ 
    /* First-fit search */
    void *bp;
//    printf("In first fit\n");
    bp = NEXT_FREE_BLOCK(heap_listp);
//    printf("First fit - Value in heap_listp - %p\n", NEXT_FREE_BLOCK(heap_listp));
//    printf("FIrst fit - Value of size of free block - %d\n", GET_SIZE(bp));
    for (bp = NEXT_FREE_BLOCK(heap_listp); GET_SIZE(HDRP(bp)) > 0; bp = NEXT_FREE_BLOCK(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
//            printf("Size of free block - %d\n", GET_SIZE(HDRP(bp)));
            return bp;
        }
    }
    return NULL; /* No fit */
}
/*
 *  * Return whether the pointer is in the heap.
 *   * May be useful for debugging.
 *    */
inline static int in_heap(const void *p) {
    return p <= mem_heap_hi() && p >= mem_heap_lo();
}

/*
 *  * Return whether the pointer is aligned.
 *   * May be useful for debugging.
 *    */
inline static int aligned(const void *p) {
    return (size_t)ALIGN(p) == (size_t)p;
}
