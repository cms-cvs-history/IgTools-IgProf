#ifndef IG_PROF_IG_MPROF_LINEAR_BROWSER_H
# define IG_PROF_IG_MPROF_LINEAR_BROWSER_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "Ig_Tools/IgProf/interface/config.h"
# include "Ig_Tools/IgProf/src/IgMProfTypedefs.h"
# include <string>
# include <fstream>
# include <map>

//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>

class IgMProfLinearRep;

//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>

class IG_PROF_API IgMProfLinearBrowser
{
private:
    IgMProfLinearRep *m_rep;    
    std::string m_filename;
    std::ofstream m_leaksOut;    
public:
    IgMProfLinearBrowser (IgMProfLinearRep *rep, const char *filename);

    struct SortByAllocs 
    {
	std::map<memAddress_t, int> *m_allocs;	
	SortByAllocs (std::map<memAddress_t, int> *map)
	    :m_allocs (map)
	    {	
	    }

	bool operator () (memAddress_t a1, memAddress_t a2)
	    {
		if ((*m_allocs)[a1] > (*m_allocs)[a2])
		    return true;
		else return false;		
	    }	
    };
    
    
    void dump (void);    
    // implicit copy constructor
    // implicit assignment operator
    // implicit destructor
};

//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

#endif // IG_PROF_IG_MPROF_LINEAR_BROWSER_H
