//<<<<<< INCLUDES                                                       >>>>>>

#include "Ig_Tools/IgProf/src/IgProfPool.h"
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>

#if ! defined MAP_ANONYMOUS && defined MAP_ANON
# define MAP_ANONYMOUS MAP_ANON
#endif

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>
//<<<<<< PRIVATE VARIABLE DEFINITIONS                                   >>>>>>

static pthread_mutex_t		s_lock = PTHREAD_MUTEX_INITIALIZER;
static IgProfPool::Released	*s_free = 0;

//<<<<<< PUBLIC VARIABLE DEFINITIONS                                    >>>>>>
//<<<<<< CLASS STRUCTURE INITIALIZATION                                 >>>>>>
//<<<<<< PRIVATE FUNCTION DEFINITIONS                                   >>>>>>

// Create temporary file for spooling profile data.  We create
// another file descriptor for the same file for the read side,
// available via the readfd() method.  We need to use open() to
// make two separate descriptors so we have a separate current
// position pointer for each of read and write side.
//
// We immediately unlink() the file so there is no chance of
// leaving the data files behind.  We don't need the name of
// the file ourselves, we just use the file descriptors.
static void
initFile (int *fd, int pool)
{
    static const char *mytmpdir = getenv ("IGPROF_TMPDIR");
    char fname [1024];
    sprintf (fname, "%.900s/igprof-data.%ld.%d.XXXXXX",
	     (mytmpdir ? mytmpdir : "/tmp"),
	     (long) getpid(), pool);
    if ((fd [0] = mkstemp (fname)) < 0) abort ();
    if ((fd [1] = open (fname, O_RDONLY, 0)) < 0) abort ();
    unlink (fname);
}

// Initialise a memory mapping.
static bool
initMapping (IgProfPool::Mapping &buf)
{
    buf.data = mmap (0, buf.size, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buf.data == MAP_FAILED)
	return false;

    buf.status = IgProfPool::READY;
    return true;
}

// Helper function for writing out larger amounts of data.
static void
flushwrite (int fd, void *data, size_t n)
{
    while (n > 0)
    {
	ssize_t ret = write (fd, data, n);
	if (ret < 0 && errno == EINTR)
	    continue;
	else if (ret < 0)
	    break;
	else
	{
	    data = (char *) data + ret;
	    n -= ret;
	}
    }
}

//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

/** Create a profiling buffer.

    Once the buffer has been created, call #readfd() to get a read
    side file desciptor for accessing the profile data.

    @a id	An identifier for this buffer, used for file names.
    @a buffered	Flag to indicate data can be buffered in memory, as
                opposed to writing everything on disk.  If enabled,
		as much data as possible is kept in memory mapped
		regions and only information about the mapping is
		is written to the file.  Up to #MAPPING times #size
		bytes of data will be buffered in memory.  If the
		flag is disabled, all data written to the file.  
    @a size	The pool size in bytes.  */

IgProfPool::IgProfPool (int id, bool buffered, bool shared,
		        unsigned int size /* = DEFAULT_SIZE */)
  : m_released (new Released [MAPPINGS]), // See release()/flush()/finish() re leak
    m_buffer (0),
    m_current (0),
    m_end (0),
    m_id (id),
    m_currentmap (-1),
    m_buffered (buffered),
    m_shared (shared),
    m_slow (false),
    m_nslow (0)
{
    initFile (m_fd, id);

    // Mark all mappings initially unused.
    for (int i = 0; i < MAPPINGS; ++i)
    {
	m_mappings [i].status = VOID;
	m_mappings [i].data = 0;
	m_mappings [i].size = size;

	m_released [i].next = 0;
	m_released [i].owner = this;
	m_released [i].index = i;
    }

    // Create the first mapping and point our write pointer there.
    mapping (0);

    // If we are shared (= multi-threaded single pool), have a lock.
    if (m_shared) pthread_mutex_init (&m_lock, 0);
}

/** Release a profiling buffer.  This is a no-op, everything else
    is taken care of in the finish() method.  */
IgProfPool::~IgProfPool (void)
{}

/** Return the read side file descriptor.  */
int
IgProfPool::readfd (void)
{
    return m_fd [1];
}

/** Append an entry to the pool.  The pool is automatically flushed
    to the backing store when it fills up.

    @a stack		The stack trace.
    @a depth		The number of entries in stack trace.
    @a counters		The counter value changes.
    @a ncounters	The number of entries in #counters. */
void
IgProfPool::push (void **stack, int depth, Entry *counters, int ncounters)
{
    if (m_fd [0] == -1)
	return;

    if (m_shared)
	pthread_mutex_lock (&m_lock);

    if (depth < 0) depth = 0;
    int words = 1 + 2 + depth + 5 * ncounters;
    if (m_end - m_current < words+2) flush ();
    if (m_end - m_current < words+2) abort ();

    *m_current++ = words-1;
    *m_current++ = STACK;
    *m_current++ = depth;
    while (--depth >= 0)
        *m_current++ = (unsigned long) stack[depth];

    for (int i = 0; i < ncounters; ++i)
    {
        *m_current++ = counters[i].type;
	*m_current++ = (unsigned long) counters[i].counter;
	*m_current++ = (unsigned long) counters[i].peakcounter;
	*m_current++ = counters[i].amount;
	*m_current++ = counters[i].resource;
    }

    // Yield every 32 allocations and snooze every 1024.
    int yieldcycle = (m_slow ? (++m_nslow % 1024) : 0);
    bool yieldsome = (m_slow && (yieldcycle % 32) == 1);
    bool yieldmore = (m_slow && (yieldcycle == 1));

    if (m_shared)
	pthread_mutex_unlock (&m_lock);

    // If we have outrun the collector, pace ourselves to give the
    // collector more time.  This does not work if we are holding
    // locks preventing the collector from running, but helps most
    // of the time the collector to follow aggressive allocation.
    // We occasionally sleep to force ourselves off the processor
    // on SMP systems with free CPU capacity.
    if (yieldsome)
	if (yieldmore)
	    usleep (1000);
	else
	    sched_yield ();
}

/** Switch to mapping @a i. */
void
IgProfPool::mapping (int i)
{
    // Switch to the other mapping.  Set up if necessary.
    // If we fail to allocate memory for new mappings, crash.
    if (m_mappings[i].status == VOID && ! initMapping (m_mappings [i]))
	abort ();

    // Point our pointers to this mapping now.
    m_buffer = (unsigned long *) m_mappings [i].data;
    m_current = m_buffer;
    m_end = m_buffer + m_mappings [i].size / sizeof (*m_buffer);
    m_mappings [i].status = ACTIVE;
    m_currentmap = i;
}

/** Collect returned free mappings for this pool.  */

/** Flush the pool, the current write area has filled up.  */
void
IgProfPool::flush (void)
{
    // If we are in buffered mode, we write out a reference to the
    // current mapping and a pointer to ourselves so the reader can
    // can call release(), and then switch to a new mapping.  If we
    // are out of mappings or in unbuffered mode, we write the data
    // to a new temporary file and write to the main output file a
    // reference to the temporary file.
    //
    // The reason we use memory mappings as much as possible is that
    // the memory profiler in particular can produce vast amounts of
    // profile data very quickly.  If we were to write it all to the
    // disk, the performance would quickly cap at disk write speed.
    // Keeping as much as we can afford, up to 100MB, in the memory
    // allows us to pass maximum possible data volume on the fastest
    // available path.  We fall back on writing to disk only if the
    // read side cannot keep up.
    //
    // We use several output files, one for every major output hunk,
    // so that the read side can reclaim the space as it reads the
    // profile data.  It helps the main use case, buffered mode, at
    // the cost of using up potentially quite many file descriptors
    // in the unbuffered mode.
    //
    // It is very important not to block in this call.  We often get
    // called from places holding locks, which also prevents the read
    // side from making progress.  For example when we are called
    // global constructors ran by dlopen(), the dynamic linker is
    // locked and the read side cannot complete tosymbol's dladdr()
    // calls.  This is the reason we cannot use pipes to communicate
    // with the reader side either: pipes can only store relatively
    // small amounts of data.  If we (writer) write more than the
    // pipe capacity, the reader needs to clear the pipe, except it
    // would not be able to do so if we were already holding a lock 
    // it would need to complete the task.  We  do require a buffer
    // with guaranteed "infinite" capacity to avoid such dead locks.

    // Add an end marker for this region (file, memory) so the
    // reader knows to close up and release the resources.  Our
    // callers make sure there is always space for this.
    *m_current++ = 1;
    *m_current++ = END;

    // See if any of our buffers have been returned.
    if (m_buffered)
    {
	pthread_mutex_lock (&s_lock);
	Released **r = &s_free;
	while (*r)
	{
	    // If it's one of ours, mark it free and remove from list.
	    // Otherwise move forward in the linked list.  (We don't
	    // actually ever delete anything, the Released objects
	    // were created in our constructor and are reused again.)
	    if ((*r)->owner == this)
	    {
		m_mappings [(*r)->index].status = READY;
		*r = (*r)->next;
	    }
	    else
	        r = &(*r)->next;
	}
	pthread_mutex_unlock (&s_lock);
    }

    // First find the mapping we could use next.
    int free = -1;
    for (int i = 0; i < MAPPINGS && free == -1; ++i)
	if (m_mappings [i].status == VOID
	    || m_mappings [i].status == READY)
	    free = i;

    // If we are buffered, switch mappings if we can.
    if (m_buffered && free >= 0)
    {
	// Mark the mapping full and waiting for release().
	m_mappings [m_currentmap].status = FULL;

	// Write out a reference to this buffer.
	unsigned long entry [5] = {
	    // Entry header: number of elements and type.
	    // Details to get back to me for release() call.
	    4, MEMREF, (unsigned long) &m_released [m_currentmap],

	    // Buffer address and number of bytes to read.
	    (unsigned long) m_buffer,
	    (unsigned long) (m_current - m_buffer) * sizeof (unsigned long)
	};

	flushwrite (m_fd [0], entry, sizeof (entry));

	// Switch mappings now.
	mapping (free);

	// Tell push() we don't need to run slowly now.
	m_slow = false;
    }

    // Either we are unbuffered or we are out of free mappings.
    else
    {
        // Write the current buffer into a temporary file, then
        // write out a reference to that file into our main output.
	// Yes, we write the file descriptor itself to the file...
	// The reader will close that file (which is what makes
	// this less than optimal for unbuffered case!).
	size_t bytes = (m_current - m_buffer) * sizeof (*m_buffer);
        int    fds [2];

        initFile (fds, m_id);
        flushwrite (fds[0], m_buffer, bytes);
        close (fds[0]);

        unsigned long entry [3] = { 2, FILEREF, fds[1] };
        flushwrite (m_fd [0], entry, sizeof (entry));

	// Mark this buffer same available again.
        m_current = m_buffer;

	// Tell push() to pace itself if we outran the collector.
	if (m_buffered) m_slow = true, m_nslow = 0;
    }
}

/** Close up the profiling pool.  */
void
IgProfPool::finish (void)
{
    if (m_shared) pthread_mutex_lock (&m_lock);

    // Flush out any pending data.
    if (m_current > m_buffer)
	flush ();

    // Send an end marker on the main file.
    *m_current++ = 1;
    *m_current++ = END;
    flushwrite (m_fd [0], m_buffer, 2*sizeof(*m_buffer));

    // If we are not buffered, release the memory.  If we
    // are buffered, m_released may be leaked as it needs
    // to stay alive beyond the life time of this pool.
    // The same applies to the memory mapped regions.
    if (! m_buffered)
    {
	delete [] m_released;
	munmap (m_mappings[0].data, m_mappings [0].size);
    }

    // Close out the write side.
    int fd = m_fd [0];
    m_fd [0] = -1;
    if (m_shared) pthread_mutex_unlock (&m_lock);
    close (fd);
}

/** Release a mapping.  Called by the read side when it has fully
    processed a memory reference we sent earlier.  This is a static
    function so it can be called even when the pool itself has been
    destroyed.  We just stick the reference to the "Released" item
    on a local list.  It may either be picked up by next flush(),
    or simply dropped on the floor if the pool has been destroyed. */
void
IgProfPool::release (unsigned long info)
{
    // The info was actually a pointer to one of the "Released"
    // allocated in the IgProfPool constructor.  We just stick
    // that in a list.  This is designed so that we don't need
    // to allocate or free any memory at this stage.
    Released *r = (Released *) info;
    pthread_mutex_lock (&s_lock);
    r->next = s_free;
    s_free = r;
    pthread_mutex_unlock (&s_lock);
}
