#ifndef IG_PROF_IG_PROF_POOL_H
# define IG_PROF_IG_PROF_POOL_H

//<<<<<< INCLUDES                                                       >>>>>>

# include <pthread.h>

//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>
//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>

/** Per-profiler and per-thread pool for gathering profile data.

    The pool collects sequences of profile entries.  When the pool
    fills up, entries collected so far are written to the backing
    store, usually either a temporary file or a pipe to a background
    listening thread.

    Each profile entry consists of a profiler identification, a stack
    trace and set of counter value changes.  The data is encoded such
    that the reader can parse it meaningfully.  All the data is raw
    and entirely specific to the running process: it is not possible
    to interpret the data outside the process.  The profiler reads
    back the pool data in the same program to produce the result.  */
class IgProfPool
{
public:
    /// Default profile data pool size.
    static const unsigned int	DEFAULT_SIZE = 32*1024*1024;

    /// Maximum number of live #Mapping buffers.
    static const int		MAPPINGS = 32;

    /// Type of pool entry.
    enum EntryType
    {
	END,		//< Profile data end marker (internal use only).
	FILEREF,	//< Data in a separate file (internal use only).
	MEMREF,		//< Data in a memory region (internal use only).
	STACK,		//< Stack trace (internal use only).
	TICK,		//< Tick a counter.
	MAX,		//< Max a counter.
	ACQUIRE,	//< Acquire a resource.
	RELEASE		//< Release a resource.
    };

    /// Parameter data for counter entries (TICK, MAX, ACQUIRE, RELEASE).
    struct Entry
    {
	EntryType	type;		//< Entry type.
	void		*counter;	//< Address of the value counter.
	void		*peakcounter;	//< Address of the peak counter.
	unsigned long	amount;		//< Resource size.
	unsigned long	resource;	//< Resource id.
    };

    /// Status of memory mapping.
    enum MappingStatus
    {
	VOID,				//< Unallocated and unused.
	READY,				//< Allocated and free.
	ACTIVE,				//< Currently in use.
	FULL				//< Waiting to be drained.
    };

    /// Memory mapped profile data region.
    struct Mapping
    {
	MappingStatus	status;		//< Current status.
	void		*data;		//< Start of mapping.
	unsigned long	size;		//< Size of mapping.
    };

    /// Information about released mappings.
    struct Released
    {
	 Released	*next;
	 IgProfPool	*owner;
	 int		index;
    };

    IgProfPool (int id, bool buffered, bool shared,
		unsigned int size = DEFAULT_SIZE);
    ~IgProfPool (void);

    // Producer side interface.
    void		push (void **stack, int depth,
			      Entry *counters, int ncounters);

    // Infrastructure and consumer side interface.
    int			readfd (void);
    void		finish (void);
    static void		release (unsigned long info);

private:
    void		flush (void);
    void		mapping (int i);

    pthread_mutex_t	m_lock;
    Mapping		m_mappings [MAPPINGS];
    Released		*m_released;
    int			m_fd [2];
    unsigned long	*m_buffer;
    unsigned long	*m_current;
    unsigned long	*m_end;
    int			m_id;
    int			m_currentmap;
    bool		m_buffered;
    bool		m_shared;
    bool		m_slow;
    int			m_nslow;
};

//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

#endif // IG_PROF_IG_PROF_POOL_H
