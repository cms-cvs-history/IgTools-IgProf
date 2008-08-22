#ifndef IG_PROF_IG_PROF_ATOMIC_H
# define IG_PROF_IG_PROF_ATOMIC_H

# if __i386__ || __ppc__
# else
#  error Sorry this platform is not supported.
# endif

//<<<<<< INCLUDES                                                       >>>>>>
//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>

typedef int IgProfAtomic; // correct for all supported platforms for now

//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>
//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>

inline IgProfAtomic
IgProfAtomicInc (volatile IgProfAtomic *val)
{
# if __i386__
    IgProfAtomic result;
    __asm__ __volatile__
	("   lock; xaddl %0, (%1); incl %0;"
	 : "=r" (result)
	 : "r" (val), "0" (1)
	 : "cc", "memory");
    return result;
# elif __ppc__
    IgProfAtomic result;
    __asm__ __volatile__
	("   lwsync\n"
	 "1: lwarx %0, 0, %1\n"
	 "   addic %0, %0, 1\n"
	 "   stwcx. %0, 0, %1\n"
	 "   bne- 1b\n"
	 "   isync\n"
	 : "=&r" (result)
	 : "r" (val)
	 : "cc", "memory");
    return result;
# endif
}

inline IgProfAtomic
IgProfAtomicDec (volatile IgProfAtomic *val)
{
# if __i386__
    IgProfAtomic result;
    __asm__ __volatile__
	("lock; xaddl %0, (%1); decl %0;"
	 : "=r" (result)
	 : "r" (val), "0" (-1)
	 : "cc", "memory");
    return result;
# elif __ppc__
    IgProfAtomic result;
    __asm__ __volatile__
	("   lwsync\n"
	 "1: lwarx %0, 0, %1\n"
	 "   addic %0, %0, -1\n"
	 "   stwcx. %0, 0, %1\n"
	 "   bne- 1b\n"
	 "   isync\n"
	 : "=&r" (result)
	 : "r" (val)
	 : "cc", "memory");
    return result;
# endif
}

//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

#endif // IG_PROF_IG_PROF_ATOMIC_H
