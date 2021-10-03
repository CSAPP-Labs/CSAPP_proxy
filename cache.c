/* $begin cache */
#include "csapp.h"
#include "cache.h"

/* common design considerations

 * LECTURE NOTES:
 * reader/writer model
 * multiple readers should be able to fetch. only one writer at a time. but this proxy does no writing, only GET?
 * though they should be able to fetch simultaneously, object list rearrangement is always restricted.
 * so in a thread, first do the reading, then update recency if the object isn't already at front
 * no need to parse response, but an industrial proxy might
 * don't use str fcns on binaries
 * should cache complete HTTP response (headers+body)
 * bucketize by same URL request
 * LRU (least recently used) eviction policy; reading AND writing counts as usage
 * max amount of data the proxy will use is MAX_CACHE_SIZE + T*MAX_OBJECT_SIZE where T is #active connections
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
    * lookup hits shift the object to most recent - front of LL
    * adding new entry - also front of LL, most recent
    * evict the LRU - worth maintaining the last node
 * maintain cumulative size of objects in cache
 * LL should be stack-maintained because dynamic, and not an actual dynamic heap for a cache frame of
   persisting size as the proxy operates.

 * not certain what a web object entails. where in a response does metadata end, and object begin?
 * in maintaining cache/object size, only count web object bytes, ignore others. but store entire response.

 * first step in design is safe copying of (binary data) all objects to and from buffers. (know size of ENTIRE response, and of obj)
 * buffer can be MAX_OBJECT_SIZE, but doesn't make sense to cache as such. realloc to smaller where possible, when caching?
 * then, an object capsule for the response, identifiable by a client request
 * obj capsule should know the size of the web object. metadata also stored as uncounted overhead.
 * then, verifiably serving a request from cache without contacting the server
 * then a linked list of the cached objects, used according to object/cache limits
    * the thread-safe locking must be implemented simultaneously to avoid corruption


 */



/* three main operations which should be locked */


add_cache_entry() {

    /* don't cache if size(response) > MAX_OBJECT_SIZE */

}


/* evict when run out of cache space, size(cache) + size(new_entry) > MAX_CACHE_SIZE*/
remove_cache_entry()



lookup_cache_entry()




/* $end cache */
