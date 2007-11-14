#ifndef IG_PROF_IG_PROF_POOL_H
# define IG_PROF_IG_PROF_POOL_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "IgTools/IgProf/src/IgProfTrace.h"
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
    static const unsigned int	DEFAULT_SIZE = 512*1024;

    /// Maximum number of live #Mapping buffers.
    static const int		MAPPINGS = 128;

    /// Type of information entry written to output stream.
    enum HeaderType 
    {
	END,		//< End of profile data, nothing follows header.
	FILEREF,	//< Reference to a pool in another file.
	MEMREF		//< Reference to a pool in a memory region.
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
	IgProfTrace	buffer;		//< Trace buffer.
    };

    /// Information about released mappings.
    struct Released
    {
	 Released	*next;
	 IgProfPool	*owner;
	 int		index;
    };

    // Options
    /** Flag to indicate data can be buffered in memory, as opposed to
	writing everything on disk.  If enabled, as much data as
	possible is kept in memory mapped regions and only information
	about the mapping is is written to the file.  Up to #MAPPING
	times #size bytes of data will be buffered in memory.  If the
	flag is disabled, all data written to the file.  */
    static const int OptBuffered = 256;

    /** Flag to indicate the pool is shared among threads.  */
    static const int OptShared = 512;

    IgProfPool (int id, int options, unsigned int size = DEFAULT_SIZE);
    ~IgProfPool (void);

    // Producer side interface.
    void		push (void **stack, int depth,
			      IgProfTrace::Record *recs, int nrecs);

    // Infrastructure and consumer side interface.
    int			readfd (void);
    void		finish (void);
    static void		release (uintptr_t info);

private:
    void		flush (void);
    void		mapping (int i);

    pthread_mutex_t	m_lock;
    Mapping		m_mappings [MAPPINGS];
    Released		*m_released;
    int			m_fd [2];

    int			m_id;
    int			m_options;
    int			m_current;
    bool		m_dirty;
    bool		m_buffered;
    bool		m_shared;
    bool		m_slow;
    int			m_nslow;
};

//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

#endif // IG_PROF_IG_PROF_POOL_H
