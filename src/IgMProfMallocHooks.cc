//<<<<<< INCLUDES                                                       >>>>>>
#include "config.h"
#include "IgMProfTreeSingleton.h"
#include "IgMProfMallocHooks.h"
#include "IgMProfConfiguration.h"
#include <cassert>
#include <unistd.h>
#include <stdlib.h>
#ifdef __HAS_MALLOC_H__
#include <malloc.h>
#endif

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>
//<<<<<< PRIVATE VARIABLE DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC VARIABLE DEFINITIONS                                    >>>>>>
//<<<<<< CLASS STRUCTURE INITIALIZATION                                 >>>>>>
//<<<<<< PRIVATE FUNCTION DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

void *(*old_malloc_hook)(size_t, const void *);
void *(*old_realloc_hook)(void *ptr, size_t size, const void *caller);
void *(*old_memalign_hook)(size_t alignment, size_t size, const void *caller);
void (*old_free_hook)(void *ptr, const void *caller);

void 
*IGUANA_memdebug_malloc_hook (size_t size, const void * /* caller */)
{
    void *result;
        
    IGUANA_memdebug_disable_hooks();    
    
    result = malloc (size);
    IgMProfMallocTreeSingleton::instance()->addCurrentStacktrace(size, 2, (memAddress_t) result);    
    IGUANA_memdebug_enable_hooks();    

    return result;    
}

void 
*IGUANA_memdebug_realloc_hook (void *ptr, size_t size, const void * /*caller*/)
{
    void *result;

    IGUANA_memdebug_disable_hooks ();    

    result = realloc (ptr, size);    

    // FIXME: does realloc call malloc? How do we handle it?

    IGUANA_memdebug_enable_hooks ();    

    return result;    
}

void 
*IGUANA_memdebug_memalign_hook (size_t alignment, size_t size, const void * /*caller*/)
{
    void *result;

    IGUANA_memdebug_disable_hooks ();    
    result = memalign (alignment,size);

    //FIXME: should we get the memory allocated with memalign?

    IGUANA_memdebug_enable_hooks();    
    
    return result;    
}

void 
IGUANA_memdebug_free_hook (void *ptr, const void * /* caller */)
{ 
    IGUANA_memdebug_disable_hooks();    

    
    IgMProfMallocTreeSingleton::instance()->removeAllocation ((memAddress_t) ptr);    

    free(ptr);    
    IGUANA_memdebug_enable_hooks();    
}

void 
IGUANA_memdebug_initialize_hook (void)
{ 
    static bool done = false;
    if (done) return;
    done = true;

    write (2, "Memory profiler loaded.\nWarning: this could slow down your application significantly\n", 85);	    

    old_malloc_hook = __malloc_hook;
    old_realloc_hook = __realloc_hook;
    old_free_hook = __free_hook;
    old_memalign_hook = __memalign_hook;    
    __malloc_hook = IGUANA_memdebug_malloc_hook;    
    __realloc_hook = IGUANA_memdebug_realloc_hook;    
    __memalign_hook = IGUANA_memdebug_memalign_hook;    
    __free_hook = IGUANA_memdebug_free_hook;    
}

void 
IGUANA_memdebug_enable_hooks (void)
{
    __malloc_hook = IGUANA_memdebug_malloc_hook;    
    __realloc_hook = IGUANA_memdebug_realloc_hook;    
    __memalign_hook = IGUANA_memdebug_memalign_hook;    
    __free_hook = IGUANA_memdebug_free_hook;
}

void 
IGUANA_memdebug_disable_hooks (void)
{
    __malloc_hook = old_malloc_hook; 
    __realloc_hook = old_realloc_hook;    
    __free_hook = old_free_hook;
    __memalign_hook = old_memalign_hook;
}
