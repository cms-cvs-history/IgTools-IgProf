//<<<<<< INCLUDES                                                       >>>>>>

#include "Ig_Tools/IgProf/src/IgProfTrace.h"
#include "Ig_Tools/IgProf/src/IgProf.h"
#include "Ig_Tools/IgHook/interface/IgHookTrace.h"
#include <memory.h>
#include <stdio.h>

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>
//<<<<<< PRIVATE VARIABLE DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC VARIABLE DEFINITIONS                                    >>>>>>
//<<<<<< CLASS STRUCTURE INITIALIZATION                                 >>>>>>
//<<<<<< PRIVATE FUNCTION DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

static const unsigned int BINARY_HASH = 128;
static const unsigned int SYMBOL_HASH_SMALL = 4*1024;
static const unsigned int SYMBOL_HASH_LARGE = 32*1024;
static const unsigned int RESOURCE_HASH_SMALL = 2*1024;
static const unsigned int RESOURCE_HASH_LARGE = 256*1024;
static const unsigned int RESOURCE_HASH_USELARGE = 4*1024*1024;
static const unsigned int MINIMUM_SIZE
    = (IgProfTrace::MAX_DEPTH * sizeof (IgProfTrace::StackCache)
       + RESOURCE_HASH_SMALL * sizeof (IgProfTrace::PoolIndex)
       + IgProfTrace::MAX_DEPTH * (sizeof (IgProfTrace::Stack)
				   + sizeof (IgProfTrace::Counter)
				   + sizeof (IgProfTrace::Resource)));

inline uint32_t
IgProfTrace::hash (Address key)
{
    // Reduced version of Bob Jenkins' hash function at:
    //   http://www.burtleburtle.net/bob/c/lookup3.c
    // Simply converted to operate on known sized fixed
    // integral keys and no initial value (initval = 0).
    //
    // in the end get the bin with 2^BITS mask:
    //   uint32_t bin = hash(key) & (((uint32_t)1 << BITS)-1);
#  define rot(x,k) (((x)<<(k)) | ((x)>>(32-(k))))
    uint32_t a, b, c;
    a = b = c = 0xdeadbeef + sizeof (key);
    b += key >> 32; // for 64-bit systems, may warn on 32-bit systems
    a += key & 0xffffffffU;
    c ^= b; c -= rot(b,14);
    a ^= c; a -= rot(c,11);
    b ^= a; b -= rot(a,25);
    c ^= b; c -= rot(b,16);
    a ^= c; a -= rot(c,4);
    b ^= a; b -= rot(a,14);
    c ^= b; c -= rot(b,24);
    return c;
#  undef rot
}
 
IgProfTrace::PoolIndex
IgProfTrace::child (Header *h, PoolIndex parent, void *address)
{
    // Search for the child's call address in the child stack frames.
    PoolIndex *kidx = &fromEnd<Stack> (h, parent)->children;
    while (*kidx)
    {
	Stack *kid = fromEnd<Stack> (h, *kidx);
	if (kid->address == address)
	    return *kidx;

	if ((char *) kid->address > (char *) address)
	    break;

	kidx = &kid->sibling;
    }

    // If we didn't find it, add a new child to address-sorted order.
    PoolIndex next = *kidx;
    Stack *kid = allocEnd<Stack> (h, *kidx);
    kid->address = address;
#if IGPROF_DEBUG
    kid->parent = parent;
#endif
    kid->sibling = next;
    kid->children = 0;
    kid->counters = 0;

    return *kidx;
}

IgProfTrace::Symbol *
IgProfTrace::getSymbol (void *address) const
{
    Header    *h = (Header *) m_start;
    uint32_t  bin = hash ((Address) address) & (SYMBOL_HASH_SMALL-1);
    PoolIndex *link = fromHash (h, h->symtable, bin);

    while (*link)
    {
	Symbol *sym = fromEnd<Symbol> (h, *link);
	if (sym->address == address)
	    return sym;
	if ((char *) sym->address > (char *) address)
	    return 0;
	link = &sym->next;
    }

    return 0;
}

/** Initialise a detached trace buffer.  */
IgProfTrace::IgProfTrace (void)
    : m_start (0)
{}

/** Initialise a trace buffer.  */
void
IgProfTrace::setup (void *start, unsigned int size, int opts, ExtendFunc ef /* = 0 */)
{
    IGPROF_ASSERT (size >= (unsigned int) MINIMUM_SIZE);
    IGPROF_ASSERT (! m_start);
    m_start = (unsigned char *) start;
    Header *h = (Header *) start;
    h->options = opts;
    h->size = size;
    h->ressize = ((opts & OptResources) ? RESOURCE_HASH_SMALL : 0);
    if (h->ressize && size >= RESOURCE_HASH_USELARGE)
	h->ressize = RESOURCE_HASH_LARGE;
    h->restable = sizeof (Header);
    h->resstart = h->restable + h->ressize * sizeof (PoolIndex) - 1;
    h->resfree = 0;
    h->freestart = h->resstart + 1;
    h->symcache = size - ((opts & OptSymbolAddress) ? SYMBOL_HASH_LARGE * sizeof (PoolIndex) : 0);
    h->symtable = h->symcache - ((opts & OptSymbolAddress) ? SYMBOL_HASH_SMALL * sizeof (PoolIndex) : 0);
    h->bintable = h->symtable - ((opts & OptSymbolAddress) ? BINARY_HASH * sizeof (PoolIndex) : 0);
    h->callcache = h->bintable - MAX_DEPTH * sizeof(StackCache);
    h->freeend = h->callcache - sizeof (Stack);
    h->extend = ef;

    IGPROF_ASSERT (h->resstart < h->size);
    IGPROF_ASSERT (h->freestart < h->freeend);

    // Zero lookup caches and tree root.
    if (opts & OptResources)
	memset (m_start + h->restable, 0, h->resstart - h->restable);
    memset (m_start + h->freeend, 0, size - h->freeend);
}

void
IgProfTrace::attach (void *start)
{
    IGPROF_ASSERT (! m_start);
    IGPROF_ASSERT (start);
    m_start = (unsigned char *) start;
}

void
IgProfTrace::detach (void)
{
    IGPROF_ASSERT (m_start);
    m_start = 0;
}

inline IgProfTrace::Counter *
IgProfTrace::initCounter (Header *h, PoolIndex &cidx, CounterDef *def, PoolIndex stackidx)
{
    Counter *ctr = allocEnd<Counter> (h, cidx);
    ctr->ticks = 0;
    ctr->value = 0;
    ctr->peak = 0;
    ctr->def = def;
    ctr->stack = stackidx;
    ctr->next = 0;
    ctr->resources = 0;
    return ctr;
}

inline bool
IgProfTrace::findResource (Header *h,
			   Record &rec,
			   uint32_t &resbin,
			   PoolIndex *&rlink,
			   Resource *&res,
			   CounterDef *def)
{
    // Locate the resource in the hash table.
    char *base = (char *) h + h->resstart;
    resbin = hash(rec.resource) & (h->ressize - 1);
    rlink = fromHash (h, h->restable, resbin);

    while (PoolIndex ridx = *rlink)
    {
	// Open-coded version of fromStart() for optimisation.
        IGPROF_ASSERT (ridx < h->freestart - h->resstart);
	Resource *myres = (Resource *) (base + ridx);
	if (myres->resource == rec.resource && myres->def == def)
	{
	    res = myres;
	    return true;
	}
	if (myres->resource > rec.resource)
	    return false;
	rlink = &myres->nexthash;
    }

    return false;
}

inline void
IgProfTrace::release (Header *h, PoolIndex *rlink, Resource *res, Counter *ctr)
{
    IGPROF_ASSERT (rlink);
    IGPROF_ASSERT (*rlink);
    IGPROF_ASSERT (res);
    IGPROF_ASSERT (res->counter != FREED);
    IGPROF_ASSERT ((res->counter & HIGH_BIT) == ACQUIRE_BIT);
    IGPROF_ASSERT ((char *) res == (char *) h + h->resstart + *rlink);
    IGPROF_ASSERT (ctr);
    IGPROF_ASSERT (ctr->resources);

    // Deduct the resource from the counter.
    IGPROF_ASSERT (ctr->value >= res->size);
    IGPROF_ASSERT (ctr->ticks > 0);
    ctr->value -= res->size;
    ctr->ticks--;

    // Unchain from hash and counter lists.
    PoolIndex ridx = *rlink;
    *rlink = res->nexthash;

    if (res->prevlive)
    {
	Resource *prev = fromStart (h, res->prevlive);
	IGPROF_ASSERT (prev->nextlive == ridx);
	prev->nextlive = res->nextlive;
    }
    else
    {
	IGPROF_ASSERT (ctr->resources == ridx);
	ctr->resources = res->nextlive;
    }

    if (res->nextlive)
    {
	Resource *next = fromStart (h, res->nextlive);
	IGPROF_ASSERT (next->prevlive == ridx);
	next->prevlive = res->prevlive;
    }

    // Put it on free list.
    memset (res, 0, sizeof (*res));
    res->nextlive = h->resfree;
    res->counter = FREED;
    h->resfree = ridx;
}

void
IgProfTrace::release (Header *h, Record &rec)
{
    // Locate the resource in the hash table.
    uint32_t  resbin;
    PoolIndex *rlink;
    Resource  *res = 0;
    bool      found = findResource (h, rec, resbin, rlink, res, rec.def);

    // If we found a previous record, release.
    if (found && (res->counter & HIGH_BIT) == ACQUIRE_BIT)
	release (h, rlink, res, fromEnd<Counter> (h, res->counter & ~HIGH_BIT));

    // Otherwise if none found, create a new release record.
    else if (! found)
    {
	PoolIndex cidx;
	Counter *ctr = initCounter (h, cidx, rec.def, 0);

	PoolIndex ridx;
	res = allocStart (h, ridx);
	res->resource = rec.resource;
	res->size = 0;
	res->nexthash = *rlink;
	res->prevlive = 0;
	res->nextlive = 0;
	res->counter = cidx | RELEASE_BIT;
	res->def = rec.def;
	ctr->resources = *rlink = ridx;
    }

    // Otherwise it was a release record already and we discoard this
    // one as a spurious release of something we didn't see allocated.
}

void
IgProfTrace::acquire (Header *h, Record &rec, Counter *ctr, PoolIndex *cidx)
{
    IGPROF_ASSERT (ctr);
    IGPROF_ASSERT (cidx);
    IGPROF_ASSERT (*cidx);

    // Locate the resource in the hash table.
    uint32_t  resbin;
    PoolIndex *rlink;
    Resource  *res = 0;
    bool      found = findResource (h, rec, resbin, rlink, res, ctr->def);

    // If we have an acquire, cancel it's effects.
    if (found && (res->counter & HIGH_BIT) == ACQUIRE_BIT)
    {
	IgProf::debug ("New %s resource 0x%lx of %ju bytes was never freed in %p\n",
		       ctr->def->name, rec.resource, res->size, h);
#if IGPROF_DEBUG
	for (PoolIndex p = ctr->stack, x = 1; p; ++x)
	{
	    Stack *s = fromEnd<Stack> (h, p);
	    if (s->address && h->options & OptSymbolAddress)
	    {
		uint32_t symbin = hash ((Address) s->address) & (SYMBOL_HASH_SMALL-1);
		PoolIndex symlink = *fromHash (h, h->symtable, symbin);
		Symbol    *sym = 0;
		while (symlink)
		{
		    sym = fromEnd<Symbol> (h, symlink);
		    if (sym->address == s->address)
			break;
		    symlink = sym->next;
		}

		if (symlink)
		    IgProf::debug ("  [%u] %10p (%s)\n", x, s->address, sym->name);
		else
		    IgProf::debug ("  [%u] %10p (?)\n", x, s->address);
	    }
	    else if (s->address)
	        IgProf::debug ("  [%u] %10p (?)\n", x, s->address);

	    p = s->parent;
	}
#endif

	// Release the resource.
	release (h, rlink, res, fromEnd<Counter> (h, res->counter & ~HIGH_BIT));

	// Proceed as if we didn't find anything.
	found = false;
    }

    // If not found, or it's a release, insert to the lists.
    // Keep the release record as described in class documentation.
    if (! found || (res->counter & HIGH_BIT) == RELEASE_BIT)
    {
	PoolIndex ridx;
	res = allocStart (h, ridx);
	res->resource = rec.resource;
	res->size = rec.amount;
	res->nexthash = *rlink;
	res->prevlive = 0;
	res->nextlive = ctr->resources;
	res->counter = *cidx | ACQUIRE_BIT;
	res->def = rec.def;
	ctr->resources = *rlink = ridx;
	if (res->nextlive) fromStart (h, res->nextlive)->prevlive = ridx;
    }
}

void *
IgProfTrace::round (Header *h, void *address)
{
    // Look up the address in call address to symbol address cache.
    void      *symaddr = address;
    uint32_t  bin = hash ((Address) address) & (SYMBOL_HASH_LARGE-1);
    PoolIndex *link = fromHash (h, h->symcache, bin);
    SymCache  *cached = 0;

    while (*link)
    {
	cached = fromEnd<SymCache> (h, *link);
        // If we found it, return the saved address.
	if (cached->calladdr == address)
	    return cached->symaddr;
	if ((char *) cached->calladdr > (char *) address)
	    break;
	link = &cached->next;
    }

    // If we didn't find it, convert the call address to a symbol
    // address.  Then look up the symbol in the symbol table, and
    // if not present add to the symbol cache, symbol table and
    // the library hashes.
    const char *binary;
    Symbol     sym = { address, 0, 0, 0, 0, 0, -1 };
    Symbol     *s;
    if (IgHookTrace::symbol (address, sym.name, binary, sym.symoffset, sym.binoffset))
	sym.address = symaddr = (void *) ((Address) address - sym.symoffset);

    // Hook up the cache entry to sort order in the hash list.
    PoolIndex next = *link;
    cached = allocEnd<SymCache> (h, *link);
    cached->calladdr = address;
    cached->symaddr = symaddr;
    cached->next = next;

    // Look up in the symbol table.
    bin = hash ((Address) symaddr) & (SYMBOL_HASH_SMALL-1);
    link = fromHash (h, h->symtable, bin);
    bool found = false;
    while (*link)
    {
	s = fromEnd<Symbol> (h, *link);
	if (s->address == sym.address)
	{
	    found = true;
	    break;
	}
	else if ((char *) s->address > (char *) sym.address)
	    break;

	link = &s->next;
    }

    // If not found, hook up
    if (! found)
    {
	// Hook up the symbol into sorted hash list order.
	sym.next = *link;
	s = allocEnd<Symbol> (h, *link);
	*s = sym;

	// Find and if necessary create the binary and hook into the symbol.
	bin = hash ((Address) binary) & (BINARY_HASH-1);
	link = fromHash (h, h->bintable, bin);
	while (*link)
	{
	    Binary *binobj = fromEnd<Binary> (h, *link);
	    if (binobj->name == binary)
	    {
		s->binary = *link;
		break;
	    }
	    link = &binobj->next;
	}

	if (! s->binary)
	{
	    Binary *binobj = allocEnd<Binary> (h, s->binary);
	    binobj->name = binary;
	    binobj->next = 0;
	    binobj->id = -1;
	    *link = s->binary;
	}
    }

    // Return the new symbol address.
    return cached->symaddr;
}

/** Push a call frame and its records into the buffer.  Returns @c
    true if this was successful, and @c false if the buffer does not
    have enough space.  */
bool
IgProfTrace::push (void **stack, int depth, Record *recs, int nrecs)
{
    if (depth < 0) depth = 0;

    // Check there is enough space available.  This is an over-estimate
    // which, if met, guarantees success further down the line.
    Header   *h = (Header *) m_start;
    uint32_t capacity = h->freeend - h->freestart;
    uint32_t required
	= (depth * (sizeof (Stack) + sizeof (SymCache)
		    + sizeof (Symbol) + sizeof (Binary))
	   + nrecs * (sizeof (Counter) + sizeof (Resource)));
    if (capacity < required)
	return false;

    // Look up call stack in the cache
    StackCache	*cache = (StackCache *) (m_start + h->callcache);
    PoolIndex	stackidx = stackRoot ();

    for (int i = 0, valid = 1; i < depth && i < MAX_DEPTH; ++i)
    {
	void *address = stack[depth-i-1];
	if (valid && cache[i].address == address)
	    stackidx = cache[i].index;
	else
	{
	    // Convert address to a symbol address in compressed tree.
	    // In doing so, cache symbol translation results.
	    if (h->options & OptSymbolAddress)
		address = round (h, address);

	    // Look up this call stack child now, then cache result.
	    stackidx = child (h, stackidx, address);
	    cache [i].address = address;
	    cache [i].index = stackidx;
	    valid = 0;
	}
    }

    // OK, we now have our final call stack node.  Update its counters
    // and the resource allocations as defined by "recs".
    Stack *stackframe = fromEnd<Stack> (h, stackidx);
    for (int i = 0; i < nrecs; ++i)
    {
	PoolIndex *cidx = 0;
	Counter	  *ctr = 0;

	// If it's a release acquisition or normal tick, update counter.
	if (recs[i].type & (COUNT | ACQUIRE))
	{
	    // Locate the counter.
	    cidx = &stackframe->counters;
	    while (*cidx)
	    {
		ctr = fromEnd<Counter> (h, *cidx);
		if (ctr->def == recs[i].def)
		    break;
		cidx = &ctr->next;
	    }

	    // If not found, add it.
	    if (! ctr || ctr->def != recs[i].def)
		ctr = initCounter (h, *cidx, recs[i].def, stackidx);

	    if (recs[i].def->type == TICK || recs[i].def->type == TICK_PEAK)
		ctr->value += recs[i].amount;
	    else if (recs[i].def->type == MAX && ctr->value < recs[i].amount)
		ctr->value = recs[i].amount;

	    if (recs[i].def->type == TICK_PEAK && ctr->value > ctr->peak)
		ctr->peak = ctr->value;

	    ctr->ticks += recs[i].ticks;
	}

	// Handle resource record for acquisition.
	if (recs[i].type & ACQUIRE)
	    acquire (h, recs[i], ctr, cidx);

	// Handle resource record for release.  Note these don't have
	// any call stack so "stackframe" refers to the call tree root.
	if (recs[i].type & RELEASE)
	    release (h, recs[i]);
    }

    return true;
}

void
IgProfTrace::pushextend (void **stack, int depth, Record *recs, int &nrecs)
{
    while (! push (stack, depth, recs, nrecs))
    {
        Header *h = (Header *) m_start;
	unsigned char *start = m_start;
	unsigned int oldsize = h->size;
	unsigned int newsize = h->size;
	h->extend (start, newsize, h->freestart, h->freeend);
	IgProf::debug ("extended profile buffer %p from %lu to %lu bytes"
		       " to insert stack of %d levels and %d resource records\n",
		       this, oldsize, newsize, depth, nrecs);

        Header *newh = (Header *) start;
        IGPROF_ASSERT (start);
        IGPROF_ASSERT (oldsize <= newsize);
        IGPROF_ASSERT (newh->size == oldsize);
        m_start = (unsigned char *) start;
        newh->freeend += newsize - oldsize;
        newh->callcache += newsize - oldsize;
        newh->bintable += newsize - oldsize;
        newh->symtable += newsize - oldsize;
        newh->symcache += newsize - oldsize;
        newh->size = newsize;
    }
    nrecs = 0;
}

void
IgProfTrace::merge (void *data)
{
    static const int	NREC = 128;
    Header		*hother = (Header *) data;
    Record		recs [NREC];
    int			rec = 0;
    IgProfTrace		other;
    other.attach (data);

    // Release RELEASEd resources in other.  These are by definition
    // not linked from the call stack, and represent resources that
    // were allocated previous to "other", i.e. the entire set of the
    // resources represents the "allocation edge" of "other".  Hence
    // it's safe to process all of them in one go, and only after that
    // process all the ACQUIREs as a part of the call stack traversal.
    unsigned		nres = (hother->freestart - (hother->resstart+1)) / sizeof (Resource);
    Resource		*res = (Resource *) ((char *) data + hother->resstart+1);

    for (unsigned n = 0; n < nres; ++n)
    {
	if (res[n].counter != FREED && (res[n].counter & HIGH_BIT) == RELEASE_BIT)
	{
	    if (rec == NREC)
		pushextend (0, 0, recs, rec);

	    recs[rec].type = RELEASE;
	    recs[rec].def = res[n].def;
	    recs[rec].amount = res[n].size;
	    recs[rec].ticks = 1;
	    recs[rec].resource = res[n].resource;
	    rec++;
	}
    }

    if (rec)
	pushextend (0, 0, recs, rec);

    // Scan stack tree and insert each call stack.  We process all
    // resource ACQUIRE entries at this stage as well.  These are the
    // records left from "other" at the time it was flushed.  We
    // cannibalise the stack cache of "other" for a temporary scratch
    // pad for maintaining both the current call stack (void *[]) and
    // the stack for the tree traversal itself (Stack *[]).  We also
    // mutate the trees in "other" to avoid needing scratch space.
    int		depth = 0;
    void	**callstack = (void **) ((char *) data + hother->callcache) + MAX_DEPTH-1;
    Stack	**treestack = (Stack **) (callstack + 1) + MAX_DEPTH-1;
    treestack [-depth] = fromEnd<Stack> (hother, other.stackRoot ());
    callstack [-depth] = treestack [-depth]->address; // null really
    while (depth >= 0)
    {
	rec = 0;

	// Process counters at this call stack level.
	PoolIndex *idx = &treestack[-depth]->counters;
	while (*idx)
	{
	    Counter *c = fromEnd<Counter> (hother, *idx);
	    *idx = c->next;
	    if (c->ticks && ! c->resources)
	    {
		if (rec == NREC)
		    pushextend (callstack-depth, depth-1, recs, rec);

		recs[rec].type = COUNT;
		recs[rec].def = c->def;
		recs[rec].amount = c->value;
		recs[rec].ticks = c->ticks;
		rec++;
	    }
	    else if (c->ticks)
	    {
		PoolIndex *ridx = &c->resources;
		while (*ridx)
		{
		    if (rec == NREC)
			pushextend (callstack-depth, depth-1, recs, rec);

		    Resource *r = fromStart (hother, *ridx);
		    recs[rec].type = COUNT | ACQUIRE;
		    recs[rec].def = c->def;
		    recs[rec].amount = r->size;
		    recs[rec].ticks = 1;
		    recs[rec].resource = r->resource;
		    rec++;

		    *ridx = r->nextlive;
		}
	    }

	    // Adjust peak counter if necessary.  This is strictly
	    // speaking not correct: we should be updating the peak
	    // counter _before_ releasing resources, otherwise we may
	    // update it too late to have an effect.  For now live
	    // with the deficiency; consider possible corrections.
	    if (c->def->type == TICK_PEAK && c->peak > c->value)
	    {
		    if (rec == NREC)
			pushextend (callstack-depth, depth-1, recs, rec);

		    recs[rec].type = COUNT | ACQUIRE | RELEASE;
		    recs[rec].def = c->def;
		    recs[rec].amount = c->peak - c->value;
		    recs[rec].ticks = 1;
		    recs[rec].resource = ~((Address) 0);
		    rec++;
	    }
	}

	if (rec)
	    pushextend (callstack-depth, depth-1, recs, rec);

	idx = &treestack[-depth]->children;
	if (*idx)
	{
	    // Add the next child to current stack.
	    ++depth;
	    treestack[-depth] = fromEnd<Stack> (hother, *idx);
	    callstack[-depth] = treestack[-depth]->address;
	    *idx = treestack[-depth]->sibling;
	}
	else
	{
	    // Return back to the level above.
	    --depth;
	}
    }
}

#define INDENT(d) for (int i = 0; i < d; ++i) fputc (' ', stderr)

void
IgProfTrace::debugStack (Header *h, PoolIndex idx, int depth)
{
    Stack *s;
    Counter *c;
    Resource *r;

    s = fromEnd<Stack> (h, idx);
    INDENT(2*depth);
    fprintf (stderr, "STACK %d %u %p %u %u\n",
	     depth, idx, s->address, s->sibling, s->children);

    for (PoolIndex cidx = s->counters; cidx; cidx = c->next)
    {
	c = fromEnd<Counter> (h, cidx);
	INDENT(2*depth+1);
	fprintf (stderr, "COUNTER %u %s %ju %ju %ju\n",
		 cidx, c->def->name, c->ticks, c->value, c->peak);

	for (PoolIndex ridx = c->resources; ridx; ridx = r->nextlive)
	{
	    r = fromStart (h, ridx);
	    INDENT(2*depth+2);
	    fprintf (stderr, "RESOURCE %u (%u %u) %s %ju %ju\n",
		     ridx, r->prevlive, r->nextlive,
		     (r->counter & HIGH_BIT) == ACQUIRE_BIT ? "ACQUIRE" : "RELEASE",
		     (uintmax_t) r->resource, (uintmax_t) r->size);
	}
    }

    for (PoolIndex kidx = s->children; kidx; kidx = s->sibling)
    {
	s = fromEnd<Stack> (h, kidx);
	debugStack (h, kidx, depth+1);
    }
}

void
IgProfTrace::debugSymCache (void)
{
    Header *h = (Header *) m_start;
    PoolIndex size = (h->size - h->symcache)/sizeof(PoolIndex);
    fprintf (stderr, "SYMBOL CACHE (%u slots):\n", size);
    for (PoolIndex i = 0; i < size; ++i)
    {
	unsigned j = 0;
	PoolIndex link = *fromHash (h, h->symcache, i);
	while (link)
	{
	    SymCache *sym = fromEnd<SymCache> (h, link);
	    fprintf (stderr, "  [%u.%-3u = %-10u] %p => %p\n",
		     i, j++, link, sym->calladdr, sym->symaddr);
	    link = sym->next;
	}
    }
}

void
IgProfTrace::debugSymTable (void)
{
    Header *h = (Header *) m_start;
    PoolIndex size = (h->symcache - h->symtable)/sizeof(PoolIndex);
    fprintf (stderr, "SYMBOL TABLE (%u slots):\n", size);
    for (PoolIndex i = 0; i < size; ++i)
    {
	unsigned j = 0;
	PoolIndex link = *fromHash (h, h->symtable, i);
	while (link)
	{
	    Symbol *sym = fromEnd<Symbol> (h, link);
	    Binary *bin = sym->binary ? fromEnd<Binary> (h, sym->binary) : 0;
	    fprintf (stderr, "  [%u.%-3u = %-10u] %p => %s in %s\n",
		     i, j++, link, sym->address,
		     sym && sym->name ? sym->name : "(nil)",
		     bin && bin->name ? bin->name : "(nil)");
	    link = sym->next;
	}
    }
}

void
IgProfTrace::debug (void)
{
    Header *h = (Header *) m_start;
    fprintf (stderr, "TRACE BUFFER %p:\n", m_start);
    fprintf (stderr, " OPTIONS:   %d\n", h->options);
    fprintf (stderr, " SIZE:      %u\n", h->size);
    fprintf (stderr, " RESSIZE:   %u\n", h->ressize);
    fprintf (stderr, " RESTABLE:  %u\n", h->restable);
    fprintf (stderr, " RESSTART:  %u\n", h->resstart);
    fprintf (stderr, " RESFREE:   %u\n", h->resfree);
    fprintf (stderr, " FREESTART: %u\n", h->freestart);
    fprintf (stderr, " FREEEND:   %u\n", h->freeend);
    fprintf (stderr, " CALLCACHE: %u\n", h->callcache);
    fprintf (stderr, " BINTABLE:  %u\n", h->bintable);
    fprintf (stderr, " SYMTABLE:  %u\n", h->symtable);
    fprintf (stderr, " SYMCACHE:  %u\n", h->symcache);

    // debugSymCache ();
    // debugSymTable ();
    // debugStack (h, stackRoot (), 0);
    // debugResources (h);
}
