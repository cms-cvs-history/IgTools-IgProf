#ifndef IG_PROF_IG_MPROF_LINEAR_REP_H
# define IG_PROF_IG_MPROF_LINEAR_REP_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "Ig_Tools/IgProf/interface/config.h"
# include "IgMProfAllocator.h"
# include <map>

//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>
//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>

#define MAX_STACKTRACE_SIZE 11   

class IG_PROF_API IgMProfLinearRep
{
public:    
    struct AllocationStruct
    {
	int m_size;
	int m_backtraceLog[MAX_STACKTRACE_SIZE-4];    
    };

    typedef std::map<int, 
	AllocationStruct*, 
	std::less<int>, 
	IgMProfAllocator<AllocationStruct*> > AllocationsMap;

    AllocationsMap m_allocationsMap;    

    IgMProfLinearRep (void);
    
    int totalAllocatedMemory (void);    
    int totalLeaksSize (void);    
    int numberOfLeaks (void);
    int maxLiveHeapSize (void);    
    void addAllocation (int size, void *ptr);    
    void deleteAllocation (void *ptr);
    // implicit copy constructor
    // implicit assignment operator
    // implicit destructor
private:
    int m_maximalAmount;
    int m_currentAmount;
};

class IgMProfLinearSingleton
{
private:
    static IgMProfLinearRep *__instance;
public:
    static IgMProfLinearRep *instance (void);    
};


//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

#endif // IG_PROF_IG_MPROF_LINEAR_REP_H
