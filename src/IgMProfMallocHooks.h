#ifndef MEM_PROF_LIB_IG_MPROF_MALLOC_HOOKS_H
# define MEM_PROF_LIB_IG_MPROF_MALLOC_HOOKS_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "Ig_Imports/MemProfLib/interface/config.h"
# include <stdlib.h>

//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>
//<<<<<< PUBLIC VARIABLES                                               >>>>>>

/** Variables to save original hooks. */
extern void *(*old_malloc_hook) (size_t, const void *);
extern void *(*old_realloc_hook) (void *ptr, size_t size, const void *caller);
extern void *(*old_memalign_hook) (size_t alignment, size_t size, const void *caller);
extern void (*old_free_hook) (void *ptr, const void *caller);

/** New hooks definitions*/
void *IGUANA_memdebug_malloc_hook (size_t size, const void *caller);
void *IGUANA_memdebug_realloc_hook (void *ptr, size_t size, const void *caller);
void *IGUANA_memdebug_memalign_hook (size_t alignment, size_t size, const void *caller);
void IGUANA_memdebug_free_hook (void *ptr, const void *caller);
void IGUANA_memdebug_initialize_hook (void);
/* Functions to start and stop the malloc profiler
   FIXME: absolutely NON thread safe. Please don't use yet.
*/
void IGUANA_memdebug_enable_hooks (void);
void IGUANA_memdebug_disable_hooks (void);

//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>


//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

#endif // MEM_PROF_LIB_IG_MPROF_MALLOC_HOOKS_H
