//<<<<<< INCLUDES                                                       >>>>>>

#include <cassert>
#include <dlfcn.h>

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>
//<<<<<< PRIVATE VARIABLE DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC VARIABLE DEFINITIONS                                    >>>>>>
//<<<<<< CLASS STRUCTURE INITIALIZATION                                 >>>>>>
//<<<<<< PRIVATE FUNCTION DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

class MemProbe 
{
    typedef int probeFunc(void);
    probeFunc *m_func;    
    bool m_profilerRunning;    
    int m_start;
    int m_currentSize;    
public:
    MemProbe (void)
	:m_profilerRunning (false),
	 m_start (0),
	 m_currentSize (0)
	{
	    m_func = (probeFunc *) dlsym (0, "_Z20totalAllocatedMemoryv");
	    if ( m_func != 0)
		m_profilerRunning = true;	    
	}
    
    void start (void)
	{
	    if (m_profilerRunning)
		m_start = m_func ();	    
	}

    void stop (void)
	{	    
	    if (m_profilerRunning)
		m_currentSize += m_func () - m_start;
	    m_start = 0;	    
	}    

    int amount (void)
	{
	    return m_currentSize;	    
	}    
};


int
main ()
{
    MemProbe probe;
    probe.start ();
    new char[100];
    probe.stop ();
    assert (probe.amount () == 100);    
}
