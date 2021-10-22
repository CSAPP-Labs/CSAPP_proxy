#ifndef __CACHE_H__
#define __CACHE_H__

#include "csapp.h"

/* cache entry structure */
typedef struct cache_entry {
    struct cache_entry *pred;
    struct cache_entry *succ;
    int obj_size;
    int hdr_size;
    char *url;
    char *buf;
} cache_entry_t;

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* cache parameters */
volatile int cache_size;
volatile int entries;
cache_entry_t *first_entry;
cache_entry_t *last_entry; /* should always be the LRU */

/* sync parameters for Posix semaphores */
sem_t mutex;


/* cache routines */
void initialize_cache();
void add_cache_entry(char *proxy_buf, char *url_ptr, int obj_bytes, int hdr_bytes);
void update_cache_recency(cache_entry_t *entry);
void remove_cache_entry();
cache_entry_t *lookup_cache_entry(char* url_ptr);
void cache_checker();


#endif /* __CACHE_H__ */
