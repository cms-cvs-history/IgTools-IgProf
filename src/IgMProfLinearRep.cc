//<<<<<< INCLUDES                                                       >>>>>>

#include "Ig_Tools/IgProf/src/IgMProfLinearRep.h"
#include <iostream>
#include <execinfo.h>

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>
//<<<<<< PRIVATE VARIABLE DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC VARIABLE DEFINITIONS                                    >>>>>>
//<<<<<< CLASS STRUCTURE INITIALIZATION                                 >>>>>>
//<<<<<< PRIVATE FUNCTION DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

IgMProfLinearRep::IgMProfLinearRep (void)
    :m_maximalAmount (0),
     m_currentAmount (0)
{
}

int 
IgMProfLinearRep::totalAllocatedMemory (void)
{
    return m_currentAmount;    
}

int 
IgMProfLinearRep::totalLeaksSize (void)
{
    int sum=0;    
    for (AllocationsMap::iterator i = m_allocationsMap.begin (); 
	 i != m_allocationsMap.end (); 
	 i++)
    {
	sum += i->second->m_size;	
    }    
    return sum;    
}

int 
IgMProfLinearRep::numberOfLeaks (void)
{
    return m_allocationsMap.size ();    
}

int 
IgMProfLinearRep::maxLiveHeapSize (void)
{
    return m_maximalAmount;	    
}

void 
IgMProfLinearRep::addAllocation (int size, void *ptr)
{
    m_currentAmount += size;
    if (m_currentAmount > m_maximalAmount) 
	m_maximalAmount = m_currentAmount;   
    
    AllocationStruct *tmp = new AllocationStruct ();

    tmp->m_size = size;

    void *backtraceLog[MAX_STACKTRACE_SIZE];    
    int symbolsSize = backtrace (backtraceLog, MAX_STACKTRACE_SIZE)-3;

    for (int i = 0 ; i < symbolsSize ; i++)
    {	
	tmp->m_backtraceLog[i] = (int) backtraceLog[i+3];	
    }

    for (int i = symbolsSize ; i < MAX_STACKTRACE_SIZE-3 ; i++)
    {
	tmp->m_backtraceLog[i] = 0;	
    }
    
    m_allocationsMap[(int) ptr] = tmp;    
}

void 
IgMProfLinearRep::deleteAllocation (void *ptr)
{
    //    assert (allocationsMap.find ((int)ptr) != allocationsMap.end ());        
    //    assert (allocationsMap.size () != 0);
    if (m_allocationsMap.size () != 0 && 
	m_allocationsMap.find ((int)ptr) != m_allocationsMap.end ())
    {	
	m_currentAmount -= m_allocationsMap[(int) ptr]->m_size;
	delete m_allocationsMap[(int) ptr];	
	m_allocationsMap.erase ((int) ptr);	
    }    
}    

IgMProfLinearRep *IgMProfLinearSingleton::__instance = 0;

IgMProfLinearRep *
IgMProfLinearSingleton::instance (void)
{
    if (__instance == 0)
    {
	__instance = new IgMProfLinearRep;		
    }
    return __instance;
}    

