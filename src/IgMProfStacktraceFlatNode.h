#ifndef IG_PROF_IG_STACKTRACE_FLAT_NODE_H
# define IG_PROF_IG_STACKTRACE_FLAT_NODE_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "Ig_Tools/IgProf/interface/config.h"
# include "IgMProfTypedefs.h"
# include <string>
# include <set>
# include <map>

//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>
//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>

struct IgMProfFlatNode {
    allocationSize_t			m_allocs;
    unsigned long long			m_calls;
    allocationSize_t			m_childrenAllocs;
    std::string				m_libname;
    std::string				m_symname;
    std::set<int>			m_callers;
    std::map<int,std::pair<int,allocationSize_t> >	m_children;

    IgMProfFlatNode (const std::string libname, const std::string symname);
    
    void addCaller (int address);    
    void addChild (int address, allocationSize_t amount);    
};


//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

#endif // IG_PROF_IG_STACKTRACE_FLAT_NODE_H
