#ifndef IG_PROF_IG_PROF_TRACE_H
# define IG_PROF_IG_PROF_TRACE_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "IgTools/IgProf/src/IgProfMacros.h"
# include "IgTools/IgProf/src/IgProf.h"
# include <limits.h>
# include <stdint.h>

//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>
//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>

/** A relocateable and resizeable profiler trace buffer.

    Each trace buffer tracks stack traces and their linked profiling
    counter values.  Optionally the buffer may also account resources
    linked with the counters: tracking which call stack acquired which
    yet-unreleased resource and the size of the resource.

    The buffer owner is responsible for arranging correct handling of
    the buffer in presence of multiple threads.  Buffers that do not
    track resources can be made thread-local and access does not be
    guarded.  Resource-accounting buffers must be shared among all the
    threads able to acquire or release the resource -- the resource
    records are entered into the buffer in the order of the profile
    events, but there is no other concept of time and therefore it
    would not be possible to merge buffers from multiple threads into
    a canonical time order later on.  In the rare situation the
    resources are thread-local the buffer can be thread-local too,
    otherwise the caller must ensure atomic access to the buffer.

    Each buffer can be either an incremental, fixed size intermediate
    buffer, or a resizeable final results buffer.  The former are used
    by the profiling modules as intermediate data gathering pool and
    flushed to the backend when filled up.  The profiling backend has
    one final trace buffer that accumulates results from all the
    profiling modules, merging in an incremental buffer at a time.

    If necessary the trace buffer can be written to and read from disk
    as a binary blob, or relocated in memory.  The buffer data contains
    no pointers, only integer references relative to the beginning and
    end of the buffer.

    The resource tracking requires significant care.  Profiling
    modules have to be able to flush their filled up trace buffers in
    order to make progress.  In particular they cannot wait on the
    backend to read the data as the calling context may hold locks
    which prevent the backend from making progress reading profile
    data.  It may well happen the buffer needs to be flushed after a
    resource was acquired but before it was released, so the records
    of the two events end up in different buffers.  The trace buffer
    guarantees a degree of relative time ordering so that the backend
    can interpret the data correctly.  On the other hand the buffer
    only keeps the most recent state without wasting space on history.

    The resource order guarantees are arranged as follows.  Resource
    records are added and removed only via the hash table, so the
    profiling side always sees the situation through the linked list
    from the hash bin; it only takes the first entry in the list.  The
    backend ignores the hash table and reads the resource vector from
    lowest address to the highest, and thus sees the history in that
    order.  Therefore when a resource acquisition record is added, the
    buffer places it after the release record in the resource vector
    order, but before in the hash table linked list.  This allows the
    backend to merge release and acquisition coming in separate
    buffers, and to simply remove live acquisitions that are freed
    within the same buffer.

    Technically a trace buffer is a single contiguous memory pool
    referenced with integer indices either from the lowest or the
    highest pool address.  At the beginning or lowest address of the
    buffer is a header.  The stack trace and counter information is
    stored starting from the highest address, growing downwards
    positioned relative to the high address.  At the very top is a
    cache area for quick translation of call trees to stack nodes in
    the buffer.  If the buffer tracks resource information, the header
    is immediately followed by a hash table, and then a (mostly)
    time-ordered vector of resources, growing upwards and positioned
    relative to the end of the hash table.  Between the resources and
    the stack frame data is free space; stack frames and counters
    allocate from top down and the resources from bottom up.  If the
    buffer is resized, the top and bottom parts can be copied into the
    new pool as such without fixing up any references.  If the hash
    table is resized the resources are simply moved further up and the
    table rebinned.

    The stack trace is represented as a tree of nodes keyed by call
    address.  Each stack frame has a singly linked list of children,
    the addresses called from that stack frame.  A frame also has a
    singly linked list of profiling counters associated with the call
    tree.  Each counter may point to a list of resources known to be
    live within that buffer; the resources linked with the counter
    form a singly linked list.  The root of the stack trace is a null
    frame: one with null call address.

    The resource hash table provides quick access to the most recent
    record on each profiled resource.  Each bin points to a resource
    in the bin; the resources in the same bin form a singly linked
    list, a list separate from the singly linked list for resources by
    counter.  A live resource points back to the counter owning that
    resource, permitting that counter to be deducted when the resource
    is released.  (When resource acquisition and release fall into
    different buffers, the release record is linked to a a stand-alone
    counter not linked with any stack frame.  When the latter buffer
    is merged into the final buffer, the right counter is updated at
    that time.)

    Typically the call addresses are raw instruction addresses when
    the profile data is collected and rounded to the nearest symbol
    in the backend gathering the data, but this is not enforced by
    the buffer in any manner.  Though note that it is *not* safe to
    try to round the call addresses to function addresses when the
    data is being collected by a profiling module!

    The memory is allocated and the pool otherwise managed by using
    raw operating system pritimives: anonymous memory mappings and by
    reading and writing directly to raw file descriptors.  The buffer
    avoids calling any non-trivial library calls.  The buffer
    implementation is safe for use in asynchronous signals provided
    the caller avoids re-enter the same buffer from nested
    signals.  */
class IgProfTrace
{
public:
    /** Byte position in pool, zero is equivalent to null pointer.
        References to resources are "positive" and index from the
	beginning of the pool (or rather, from after the resource
	hash table).  References to stack frames and counters are
	"negative" and index from the end of the pool.  Thus if
	the pool is resized the resource hash table and the stack
	frame data can be copied unmodified to the new pool and
	the unallocated gap in the middle expands.  */
    typedef unsigned int PoolIndex;

    /** A value that might be an address, usually memory resource.  */
    typedef uintptr_t Address;

    /** A large-sized accumulated value for counters.  */
    typedef uintmax_t CounterValue;

    /** Deepest supported stack depth. */
    static const int MAX_DEPTH = 400;

    /** Prototype for callback to extend the buffer.  */
    typedef void (*ExtendFunc) (unsigned char *&buf, unsigned &size,
			        unsigned lo, unsigned hi);

    /** Pool header, always in the beginning of the pool.  */
    struct Header
    {
	int		options;	//< Buffer configuration options.
	PoolIndex	size;		//< Size of this pool.
	PoolIndex	ressize;	//< Size of the resource hash.
	PoolIndex	restable;	//< Start of the resources hash.
	PoolIndex	resstart;	//< Start of resources.
	PoolIndex	resfree;	//< Resource free list.
	PoolIndex	freestart;	//< Start of free area.
	PoolIndex	freeend;	//< One past the end of free area.
	PoolIndex	callcache;	//< Start of address cache.
	PoolIndex	bintable;	//< Start of the binaries hash.
	PoolIndex	symtable;	//< Start of the symbol hash.
	PoolIndex	symcache;	//< Start of the symbol cache hash.
	ExtendFunc	extend;		//< Extension function.
    };

    /* The hash table is a vector of #PoolIndex'es to a #Resource.
       The #PoolIndex position values start after the hash table so
       that the hash table itself can be resized as needed, which
       happens whenever the pool is resized.  */

    /// Structure for call stack cache at the end.
    struct StackCache
    {
	void		*address;
	PoolIndex	index;
    };

    /// Description of a binary module associated with a symbol.
    struct Binary
    {
	const char	*name;		//< Binary name if known.
	PoolIndex	next;		//< Next binary in the hash bin list.
	int		id;		//< Output time id.
    };

    /// Description of a symbol behind a call address, linked in hash table.
    struct Symbol
    {
	void		*address;	//< Call address.
	const char	*name;		//< Symbol name if known.
	int		symoffset;	//< Offset from beginning of symbol.
	int		binoffset;	//< Offset from beginning of library.
	PoolIndex	binary;		//< Binary object owning this symbol.
	PoolIndex	next;		//< Next symbol in the hash bin list.
	int		id;		//< Output time id.
    };

    /// Hash table cache entry for call address to symbol address mappings.
    struct SymCache
    {
	void		*calladdr;
	void		*symaddr;
	PoolIndex	next;
    };

    /// Stack trace node.
    struct Stack
    {
	void		*address;	//< Call address.
#if IGPROF_DEBUG
	PoolIndex	parent;		//< Parent #Stack frame.
#endif
	PoolIndex	sibling;	//< Next #Stack child of the same parent.
	PoolIndex	children;	//< Child #Stack of this call level.
	PoolIndex	counters;	//< Index of first #Counter or zero.
    };

    /// Counter type.
    enum CounterType
    {
	TICK,				//< Ticked cumulative counter.
	TICK_PEAK,			//< Ticked and keep also the peak value.
	MAX				//< Maximum-value counter.
    };

    /// Counter definition.
    struct CounterDef
    {
	const char	*name;		//< Name of the counter.
	CounterType	type;		//< Type of the counter.
	int		id;		//< Output time id.
    };

    /// Counter value.
    struct Counter
    {
	CounterValue	ticks;		//< Number of individual values.
	CounterValue	value;		//< Accumulated counter value.
	CounterValue	peak;		//< Maximum value at any time.
	CounterDef	*def;		//< Counter defnition for this counter.
	PoolIndex	stack;		//< Index of the owner stack node.
	PoolIndex	next;		//< Next counter in the stack's chain.
	PoolIndex	resources;	//< Index of the live resources tracked.
    };

    /* The resource hash table is followed by a list of resources.

       Each resource is part of two singly linked lists: one for
       the hash table and another for the live resources ("leaks")
       attached to a counter.  Updating a resource always updates
       both linked lists as appropriate.

       When a resource is acquired, the hash table is searched for an
       existing entry for the resource.  If none exists, the resource
       is entered into the first free resource position, either first
       in the free list if there is one, or next slot in the resource
       vector.  Otherwise if an #ACQUIRE record already exists, it is
       first freed and the search is repeated, on the assumption the
       profiler lost track of when the resource was freed.  If the
       entry is a #RELEASE it by definition is not linked to a stack
       frame and cannot be freed, so a new #ACQUIRE entry is allocated
       and linked into the hash bin, in front of all #RELEASES in the
       hash bin linked list.  In this case the #ACQUIRE is always
       allocated from the free pool area, never from the free list, to
       guarantee that the backend sees first the #RELEASE, then the
       #ACQUIRE.  Once all this is done the counters are updated as
       appropriate.  In other words, the hash table refers to any one
       resource at most twice, and all the bin linked list always has
       first all #ACQUIRE first, then all #RELEASE entries.

       When a resource is released, if the resource is still known in
       the same trace buffer, the #ACQUIRE record is removed from the
       singly linked lists of both the counter and the hash table bin
       and put on the free list ("nextlive" chains the free list).  If
       the resource is not known in the same trace buffer, for example
       if the buffers were switched after the resource was acquired, a
       #RELEASE entry is added, referenced from the hash table bin and
       with a link to a stand-alone counter that does not reference a
       stack frame.  If the resource is known in the trace buffer but
       only as a #RELEASE, the second #RELEASE is discarded on the
       assumption that the resource was acquired when the profiler was
       not active.

       The record type is stored in the high bit of "counter".  */

    static const PoolIndex HIGH_BIT_NR = sizeof (PoolIndex) * CHAR_BIT - 1;
    static const PoolIndex HIGH_BIT = 1 << HIGH_BIT_NR;
    static const PoolIndex ACQUIRE_BIT = 0;                //< Resource was acquired.
    static const PoolIndex RELEASE_BIT = 1 << HIGH_BIT_NR; //< Resource was released.
    static const PoolIndex FREED = ~0U;

    /// Data for a resource.
    struct Resource
    {
	Address		resource;	//< Resource identifier.
	CounterValue	size;		//< Size of the resource.
	PoolIndex	nexthash;	//< Next resource in same hash bin.
	PoolIndex	prevlive;	//< Previous live resource in the same counter.
	PoolIndex	nextlive;	//< Next live resource in the same counter.
	PoolIndex	counter;	//< Counter tracking this resource.
	CounterDef	*def;		//< Cached counter definition reference.
    };

    /// Bitmask of properties the record covers.
    typedef unsigned int RecordType;
    static const RecordType COUNT	= 1;
    static const RecordType ACQUIRE	= 2;
    static const RecordType RELEASE	= 4;

    /// Structure used by callers to record values.
    struct Record
    {
	RecordType	type;
	CounterDef	*def;
	CounterValue	amount;
	CounterValue	ticks;
	Address		resource;
    };

    // Buffer configuration options.
    static const int OptResources = 1;	    //< Buffer stores resources.
    static const int OptSymbolAddress = 2;  //< Map addresses to symbols.

    IgProfTrace (void);
    // Implicit copy constructor
    // Implicit assignment operator
    // Implicit destructor

    bool		valid (void) const;
    void		setup (void *start, unsigned int size, int opts,
		    	       ExtendFunc ef = 0);
    void		attach (void *start);
    void		detach (void);

    bool		push (void **stack, int depth, Record *recs, int nrecs);
    void		merge (void *data);

    PoolIndex		stackRoot (void) const;
    Stack *		getStack (PoolIndex index) const;
    Counter *		getCounter (PoolIndex index) const;
    Resource *		getResource (PoolIndex index) const;
    Symbol *		getSymbol (void *address) const;
    Binary *		getBinary (PoolIndex index) const;

private:
    static uint32_t	hash (Address key);
    static PoolIndex *	fromHash (Header *h, PoolIndex base, PoolIndex index);
    static Resource *	fromStart (Header *h, PoolIndex index);
    static Resource *	allocStart (Header *h, PoolIndex &index);
    template <class T>
    static T *		fromEnd (Header *h, PoolIndex index);
    template <class T>
    static T *		allocEnd (Header *h, PoolIndex &index);

    static void *	round (Header *h, void *address);
    static PoolIndex	child (Header *h, PoolIndex node, void *address);
    static Counter *	initCounter (Header *h,
				     PoolIndex &cidx,
				     CounterDef *def,
				     PoolIndex stackidx);
    static bool		findResource (Header *h,
				      Record &rec,
				      uint32_t &resbin,
				      PoolIndex *&rlink,
				      Resource *&res,
				      CounterDef *def);
    static void		release (Header *h,
				 PoolIndex *rlink,
				 Resource *res,
				 Counter *ctr);
    static void		release (Header *h, Record &rec);
    static void		acquire (Header *h,
				 Record &rec,
				 Counter *ctr,
				 PoolIndex *cidx);

    void		pushextend (void **stack, int depth, Record *recs, int &nrecs);
    void		debug (void);
    void		debugSymCache (void);
    void		debugSymTable (void);
    static void		debugStack (Header *h, PoolIndex idx, int depth);

    unsigned char	*m_start;
};

//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

/** Check whether the buffer is validly attached.  */
inline bool
IgProfTrace::valid (void) const
{ return m_start != 0; }

inline IgProfTrace::PoolIndex *
IgProfTrace::fromHash (Header *h, PoolIndex base, PoolIndex bin)
{
    IGPROF_ASSERT (base > 0);
    return (PoolIndex *) ((char *) h + base) + bin;
}

inline IgProfTrace::Resource *
IgProfTrace::fromStart (Header *h, PoolIndex index)
{
    IGPROF_ASSERT (index);
    IGPROF_ASSERT (index < h->freestart - h->resstart);
    return (Resource *) ((char *) h + h->resstart + index);
}

inline IgProfTrace::Resource *
IgProfTrace::allocStart (Header *h, PoolIndex &index)
{
    if (h->resfree)
    {
	Resource *r = fromStart (h, index = h->resfree);
	h->resfree = r->nextlive;
	return r;
    }
    else
    {
	IGPROF_ASSERT (h->freeend - h->freestart >= sizeof (Resource));
	index = h->freestart - h->resstart;
	h->freestart += sizeof (Resource);
        return (Resource *) ((char *) h + h->resstart + index);
    }
}

template <class T>
inline T *
IgProfTrace::fromEnd (Header *h, PoolIndex index)
{
    IGPROF_ASSERT (index);
    IGPROF_ASSERT (index <= h->callcache - h->freeend);
    return (T *) ((char *) h + h->callcache - index);
}

template <class T>
inline T *
IgProfTrace::allocEnd (Header *h, PoolIndex &index)
{
    IGPROF_ASSERT (h->freeend - h->freestart >= sizeof (T));
    h->freeend -= sizeof (T);
    index = h->callcache - h->freeend;
    return fromEnd<T> (h, index);
}

inline IgProfTrace::Stack *
IgProfTrace::getStack (PoolIndex index) const
{ return fromEnd<Stack> ((Header *) m_start, index); }

inline IgProfTrace::Counter *
IgProfTrace::getCounter (PoolIndex index) const
{ return fromEnd<Counter> ((Header *) m_start, index); }

inline IgProfTrace::Resource *
IgProfTrace::getResource (PoolIndex index) const
{ return fromStart ((Header *) m_start, index); }

inline IgProfTrace::Binary *
IgProfTrace::getBinary (PoolIndex index) const
{ return fromEnd<Binary> ((Header *) m_start, index); }

inline IgProfTrace::PoolIndex
IgProfTrace::stackRoot (void) const
{ return sizeof (Stack); }

#endif // IG_PROF_IG_PROF_TRACE_H
