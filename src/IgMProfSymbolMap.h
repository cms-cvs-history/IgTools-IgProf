#ifndef MEM_PROF_LIB_IG_MPROF_SYMBOL_LIST_H
# define MEM_PROF_LIB_IG_MPROF_SYMBOL_LIST_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "Ig_Imports/MemProfLib/interface/config.h"
# include <ext/hash_map>
# include <dlfcn.h>
# include <execinfo.h>

//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>
//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>
/** This is a map of runtime symbols of the executable. 
    Symbols are cached after the first time they are accessed so that dladdr() lookup overhead
    is reduced.
 */
class MEM_PROF_LIB_API IgMProfSymbolMap 
{
private:
    typedef __gnu_cxx::hash_map<memAddress_t, memAddress_t, __gnu_cxx::hash<int>,
    			        std::equal_to<int>, IgMProfAllocator<memAddress_t> > dladdrMap_t;    

    dladdrMap_t m_dladdrmap;
public:
    IgMProfSymbolMap (void)
	{
	}
    
    /**\return the best match of symbol which could contain the passed address.
     */
    memAddress_t closerSymbol(memAddress_t address)
	{
	    // Convert the address to function address; cache
	    // dladdr lookups to avoid significant overhead.
	    memAddress_t &remapped = m_dladdrmap [address];
	    if (! remapped)
	    {
		Dl_info info;
		if (dladdr ((void *) address, &info) && info.dli_fname && info.dli_fname[0] && info.dli_saddr)
		    address = (memAddress_t) info.dli_saddr;
		remapped = address;
	    }	    

	    return remapped;
	}
    
    // implicit copy constructor
    // implicit assignment operator
    // implicit destructor
};

//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

#endif // MEM_PROF_LIB_IG_MPROF_SYMBOL_LIST_H
