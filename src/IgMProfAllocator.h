#ifndef MEM_PROF_LIB_IG_MPROF_ALLOCATOR_H
# define MEM_PROF_LIB_IG_MPROF_ALLOCATOR_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "Ig_Imports/MemProfLib/interface/config.h"
# include <vector>

//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>
//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>

/* This is a workaround to be able to use std::map also in mallocs_hooks:
   in a multithreaded enviromenment the std:allocator of a map could be 
   in a locked state when entering the hook, so that a second call to it blocks.
   For this reason we have written a separate allocator.
   FIXME: put this in proper english...;-)
 */
template <class _Tp> class MEM_PROF_LIB_API IgMProfAllocator : public std::allocator<_Tp>
{
    typedef std::__default_alloc_template<true, -1> _Alloc; // The underlying allocator.
public:
    template <class _Tp1> struct rebind { typedef IgMProfAllocator<_Tp1> other; };    

    IgMProfAllocator() throw() {}
    IgMProfAllocator(const IgMProfAllocator&x) throw() : std::allocator<_Tp> (x) {}
    template <class _Tp1> IgMProfAllocator(const IgMProfAllocator<_Tp1>&x) throw() : std::allocator<_Tp> (x) {}
    ~IgMProfAllocator() throw() {}

    _Tp* allocate(typename std::allocator<_Tp>::size_type __n, const void* = 0) {
	return __n != 0 ? static_cast<_Tp*>(_Alloc::allocate(__n * sizeof(_Tp))) : 0;
    }
    void deallocate(typename std::allocator<_Tp>::pointer __p, typename std::allocator<_Tp>::size_type __n) {
	_Alloc::deallocate(__p, __n * sizeof(_Tp));
    }
};

//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

#endif // MEM_PROF_LIB_IG_MPROF_ALLOCATOR_H
