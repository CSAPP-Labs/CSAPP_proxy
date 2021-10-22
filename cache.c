/* $begin cache */
// #include "csapp.h"
#include "cache.h"

/* common design considerations

 * LECTURE NOTES:
 * reader/writer model
 * multiple readers should be able to fetch. only one writer at a time. but this proxy does no writing, only GET?
 * though they should be able to fetch simultaneously, object list rearrangement is always restricted.
 * so in a thread, first do the reading, then update recency if the object isn't already at front
 * no need to parse response, but an industrial proxy might
 * don't use str fcns on binaries
 * should cache complete HTTP response (headers+body), but size is measured by body?
 * bucketize by same URL request
 * LRU (least recently used) eviction policy; reading AND writing counts as usage
 * max amount of data the proxy will use is MAX_CACHE_SIZE + T*MAX_OBJECT_SIZE where T is #active connections
    * this is an argument for prethreaded design; Tmax = nr of worker threads;
 * many readers can peacefully coexist, but a writer must synchronize

 * concurrency problems: 
 * if proxy hangs, may be forgetting to unlock
 * if cache corrupted, may be forgetting to lock

 * DESIGN COMMENTARY:
 * if the received web object is within the object size limit, proxy should cache it
 * larger objects get passed to the client without buffering/caching
 * if a later instance of a client requests a cached object, the proxy can send without reconnecting

 * no need for a strict cache design (no: specified block size, set size, line number)
 * no concern for memory contiguity; maintained by OS
 * but approximate the LRU policy. can be done via linked list of objects.
    * each node should have a key generated based on URL of request. find hashing modules?
    * when such a request is made again, it should match with the key of some node, if cached
    * a lookup "hit" shifts the object to most recent - front of LL
    * adding new entry - also front of LL, most recent
    * evict the LRU - worth maintaining the last node
 * maintain cumulative size of objects in cache, in a global var
 * in maintaining cache/object size, only count web object bytes, ignore others. but store entire response.


 * first step in design is safe copying of (binary data) all objects to and from buffers. (know size of ENTIRE response, and of obj)
 * buffer can be MAX_OBJECT_SIZE, but doesn't make sense to cache as such. realloc to smaller where possible, when caching?
 * then, an object capsule for the response, identifiable by a client request
 * identify by URL? some kind of hash key? or a tree structure? a list of linked lists for each URL hostpage?
 * obj capsule should know the size of the web object. metadata also stored as uncounted overhead.
 * then, verifiably serving a request from cache without contacting the server
 * then a linked list of the cached objects, used according to object/cache limits
    * the thread-safe locking must be implemented simultaneously to avoid corruption

 * maybe spawn multiple cache structures, each dedicated to responses from a single host, 
 * their sum total of sizes must be smaller than MAX_CACHE_SIZE



 * CURRENT ISSUE: all requests just read the same one thing from cache. also seem to take the address after / and 
 * append it to host

 * issue 2: thrashing seems to be happening; image-sized stuff not being cached.
 * need to have an idea of how many requests are made when entering a page with many objects.
 * count the total requests, numbers of hits and of misses. so it roughly works
 * just need a good set of observable parameters
 * to study its performance

 * the AP today in history history subpage seems to have enough objects to cause thrashing when going back
 * and forth, but otherwise lots of hits when switching between other pages even after
 * the cache is filled


 */




/* initialize at start of proxy operation */
void initialize_cache() 
{

    cache_size = 0;
    entries = 0;
    first_entry = NULL;
    last_entry = NULL;

    /* initialize Posix semaphores */
    Sem_init(&mutex, 0, 1);

}


void add_cache_entry(char *proxy_buf, char *url_ptr, int obj_bytes, int hdr_bytes) 
{
    int removal_signal = 0;

    printf("MISS: ADD NEW\n");
    // printf("ADD ENTRY: Cache size [%d / %d], entry size [%d / %d], URL:\n%s\n\n", 
    //     cache_size, MAX_CACHE_SIZE, obj_bytes, MAX_OBJECT_SIZE, url_ptr);

    P(&mutex);
    /* create new cache_entry_t object in memory */
    cache_entry_t *entry = NULL;
    entry = (cache_entry_t *) Malloc(sizeof(cache_entry_t));

    /* object values */
    entry->obj_size = obj_bytes;
    entry->hdr_size = hdr_bytes;
    entry->url = url_ptr; 
    entry->buf = proxy_buf;

    
    /* keep removing LRUs till enough space; header size not counted */
    while ((cache_size + obj_bytes) > MAX_CACHE_SIZE ) {
        remove_cache_entry();
        removal_signal++;
    }
    cache_size+=obj_bytes;
    entries++;

    /* insert new entry at start of list */
    entry->succ = first_entry;
    entry->pred = NULL;
    if (first_entry)
        first_entry->pred = entry;
    first_entry = entry;

    /* last entry to be set the first time */
    if (last_entry == NULL)
        last_entry = first_entry;

    /* if removal happened, show the cache list after this entry has been added and compare it with
     * the first in the list */
    // if (removal_signal != 0)  {
    //     printf("ADDED ENTRY: current ptr [%p], Cache size [%d / %d], entry size [%d / %d], URL:\n%s\n\n", 
    //         entry, cache_size, MAX_CACHE_SIZE, obj_bytes, MAX_OBJECT_SIZE, url_ptr);   
    //     cache_checker();
    // }

    V(&mutex);

}

/* move a PRESENT entry to start of cache list. the looked-up entry could be anywhere on the list */
void update_cache_recency(cache_entry_t *entry) 
{
    if (entry == first_entry)
        return;

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
    entry->succ = first_entry;
    entry->pred = NULL;
    if (first_entry)
        first_entry->pred = entry;
    first_entry = entry;

}


/* evict when run out of cache space, size(cache) + size(new_entry) > MAX_CACHE_SIZE*/
void remove_cache_entry() 
{
    printf("REMOVE\n");

    cache_entry_t *future_last = last_entry->pred;
    // printf("BEFORE REMOVE\n");
    // cache_checker();

    entries--;

    // printf("REMOVE ENTRY: Cache size [%d / %d], entry size [%d / %d], ptr:%p\n\n", 
    //     cache_size, MAX_CACHE_SIZE, last_entry->obj_size, MAX_OBJECT_SIZE, last_entry);   

    /* cut LRU entry off  */
    cache_size -= (last_entry->obj_size);
    if (last_entry->pred != NULL)
        (last_entry->pred)->succ = NULL;

    /* remove the last entry's cached response, its url, then the entry struct itself */
    Free(last_entry->buf);
    Free(last_entry->url);
    Free(last_entry);

    last_entry = future_last;    

    if (last_entry != NULL) /* shouldn't happen */
        last_entry->succ = NULL;


    // printf("AFTER REMOVE\n");
    // cache_checker();

}


/* return a pointer to the cached object struct, or NULL if not found */
/* multiple readers should be able to look it up */
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
            printf("HIT\n");
            // printf("URL HIT: Cache size [%d / %d]; entry size [%d / %d], URL:\n%s\n\n", 
            //     cache_size, MAX_CACHE_SIZE, candidate->obj_size, MAX_OBJECT_SIZE, candidate->url);

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





void cache_checker() 
{

    // P(&mutex);
    cache_entry_t *current_entry;
    int count = 0;

    

    /* iterate through cache list */
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

    printf("\n\nCache size [%d / %d], first: [%p], last: [%p]\n", cache_size, MAX_CACHE_SIZE, first_entry, last_entry);

    for (current_entry = first_entry; current_entry != NULL; current_entry = current_entry->succ) {

        if ( (count < 2) || (count > (entries - 2))) {
            printf("\nEntry size [%d / %d], current: [%p], pred:[%p], succ:[%p] URL:\n%s\n", 
                current_entry->obj_size, MAX_OBJECT_SIZE, current_entry, current_entry->pred, 
                current_entry->succ, current_entry->url);
        } else {
            printf("|");
        }

        count++;

    }
    printf("LIST PRINTED.\n");

    // V(&mutex);

}



/* $end cache */
