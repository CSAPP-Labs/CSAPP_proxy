/* $begin cache */
#include "cache.h"

/* common design considerations

 * issues:
 * ISSUE 1: all requests just read the same one thing from cache. 
 * SOLVED: allocate heap memory for url instead of using a buffer which resides
 *      on the stack and thus may wrongly be read elsewhere 

 * ISSUE 2: thrashing seems to be happening; image-sized objects evicted soon after being cached on
 * some subpage in apimages.com
 * SOLVED:
 * the AP "today in history"  subpage seems to have enough objects (total more than cache size) to cause 
 * thrashing - flushing much of the cache when refreshing, such that it is not mostly hits when
 * the refresh is done. it also flushes all older pages visited before it
 * otherwise lots of hits when switching between other, smaller pages even after the cache is filled

 */


/* initialize at start of proxy operation */
void initialize_cache() 
{
    /* cache performance parameters */
    hits = 0;
    misses = 0;
    removals = 0;
    additions = 0;
    entries = 0;

    /* functional parameters*/
    cache_size = 0;
    first_entry = NULL;
    last_entry = NULL;

    /* initialize Posix semaphores */
    Sem_init(&mutex, 0, 1);
}

void add_cache_entry(char *proxy_buf, char *url_ptr, int obj_bytes, int hdr_bytes) 
{
    ++misses;
    ++additions;

    /* create new cache_entry_t object in memory */
    cache_entry_t *entry = NULL;
    entry = (cache_entry_t *) Malloc(sizeof(cache_entry_t));

    /* object values */
    entry->obj_size = obj_bytes;
    entry->hdr_size = hdr_bytes;
    entry->url = url_ptr; 
    entry->buf = proxy_buf;
    
    P(&mutex);
    /* keep removing LRUs till enough space; header size not counted */
    while ((cache_size + obj_bytes) > MAX_CACHE_SIZE ) {
        remove_cache_entry();
    }
    cache_size+=obj_bytes;
    ++entries;

    /* debug: entry info upon adding */
    printf("MISS: ADD NEW\n");
    // printout_entry(entry, "ADD ENTRY");

    /* insert new entry at start of list */
    prepend_to_cache(entry);

    /* last entry to be set the first time */
    if (last_entry == NULL)
        last_entry = first_entry;

    V(&mutex);
}

/* move a PRESENT entry to start of cache list; looked-up entry could be anywhere on the list */
void update_cache_recency(cache_entry_t *entry) 
{
    if (entry == first_entry)
        return;

    /* re-knit the linked list, taking out the identified entry */
    /* if entry has a successor, then the succ's predecessor should be that of the entry */
    if (entry->succ != NULL)
        (entry->succ)->pred = entry->pred;

    /* if entry has a predecessor, then the pred's successor should be that of the entry */
    if (entry->pred != NULL)
        (entry->pred)->succ = entry->succ;

    /* if the entry is also the last entry with a valid pred, then that pred is the new last entry */
    if ((entry == last_entry) && (entry->pred != NULL))
        last_entry = entry->pred;

    /* insert entry at start of list */
    prepend_to_cache(entry);
}


/* evict when run out of cache space, size(cache) + size(new_entry) > MAX_CACHE_SIZE*/
void remove_cache_entry() 
{
    ++removals;
    --entries;
    cache_entry_t *future_last = last_entry->pred; 

    /* debug: entry info before removing */
    printf("REMOVE\n");
    // cache_checker("Prior to removing entry");
    // printout_entry(last_entry, "REMOVE ENTRY");
    // printout_cache_performance("PERFORMANCE AT ENTRY REMOVAL"); 

    /* cut LRU entry off  */
    cache_size -= (last_entry->obj_size);
    if (last_entry->pred != NULL)
        (last_entry->pred)->succ = NULL;

    /* free the last entry's cached response, its url, then the entry struct itself */
    Free(last_entry->buf);
    Free(last_entry->url);
    Free(last_entry);

    last_entry = future_last;    
    if (last_entry != NULL) /* shouldn't happen */
        last_entry->succ = NULL;

    /* debug info: after removing */
    // cache_checker("After removing entry");
}


/* return a pointer to the cached object struct, or NULL if not found */
cache_entry_t *lookup_cache_entry(char* url_ptr) 
{
    cache_entry_t *candidate;

    P(&mutex);
    /* iterate through cache list */
    if (first_entry == NULL) {
        V(&mutex);
        return NULL;
    }

    for (candidate = first_entry; candidate != NULL; candidate = candidate->succ) {
        if ( strcmp(candidate->url, url_ptr) == 0) {
            ++hits;

            /* debug: entry info upon hit */
            printf("HIT\n");
            // printout_entry(candidate, "URL HIT");

            /* list recency maintenance for existing entry */
            update_cache_recency(candidate);  
            V(&mutex);
            return candidate;
        }
    }
    V(&mutex);

    /* no matching response found */
    return NULL;
}

void prepend_to_cache(cache_entry_t *entry) 
{
    entry->succ = first_entry;
    entry->pred = NULL;
    if (first_entry)
        first_entry->pred = entry;
    first_entry = entry;
}


/* debug helpers */

void printout_entry(cache_entry_t *entry, char *msg)
{
    printf("%s: Cache size [%d / %d], entry size [%d / %d], URL:\n%s\n\n", msg, 
        cache_size, MAX_CACHE_SIZE, entry->obj_size, MAX_OBJECT_SIZE, entry->url);  
}

void printout_cache_performance(char *msg)
{
    printf("%s:\nCache size [%d / %d], entries [%d], hits / misses: [%d / %d], removals: [%d], additions: [%d], hit/miss ratio [%f]\n", 
        msg, cache_size, MAX_CACHE_SIZE, entries, hits, misses, removals, additions, ((float)hits / (float)misses));  
}

void cache_checker(char *msg) 
{
    printf("Cache check caller context: %s\n", msg);
    // P(&mutex);
    cache_entry_t *current_entry;
    int count = 0;

    /* check if cache edges are OK */
    if (first_entry == NULL) {
        printf("First entry is null.\n");
        if (last_entry == NULL) {
            printf("Last entry is null.\n");
        }
        else {
            printf("First entry null, last entry NOT null. Error.\n");
            exit(0);
        }
    }

    /* iterate through cache list */
    printf("\n\nCache size [%d / %d], entries [%d], first: [%p], last: [%p]\n", 
        cache_size, MAX_CACHE_SIZE, entries, first_entry, last_entry);
    for (current_entry = first_entry; current_entry != NULL; current_entry = current_entry->succ) {
        if ( (count < 2) || (count > (entries - 2))) {
            printf("\nEntry size [%d / %d], current: [%p], pred:[%p], succ:[%p] URL:\n%s\n", 
                current_entry->obj_size, MAX_OBJECT_SIZE, current_entry, current_entry->pred, 
                current_entry->succ, current_entry->url);
        } else {
            printf("|"); /* avoid clutter of info on entries in middle of list */
        }
        count++;
    }
    printf("LIST PRINTED.\n");

    // V(&mutex);
}


/* $end cache */
