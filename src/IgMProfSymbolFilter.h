#ifndef IG_PROF_IG_MPROF_SYMBOL_FILTER_H
# define IG_PROF_IG_MPROF_SYMBOL_FILTER_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "Ig_Tools/IgProf/interface/config.h"
# include "IgMProfTypedefs.h"
# include <iostream>
# include <fstream>
# include <string>
# include <set>
# include <map>
# include <dlfcn.h>

//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>
//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>

class IG_PROF_API IgMProfSymbolFilter
{
public:
    IgMProfSymbolFilter (void);    
    
    bool updateFilterMap (void);
    
    virtual bool filter (memAddress_t address);
private:
    std::map<std::string, int> m_filtersNotDoneSet;    
    std::set<memAddress_t> m_filterSet;    
    int m_dynamicCount;    
    int m_symbolsToBeExcluded;    
};

//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

#endif // IG_PROF_IG_MPROF_SYMBOL_FILTER_H
