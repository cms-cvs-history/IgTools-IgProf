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
            // Removing the __extension__ gives a warning which
            // is acknowledged as a language problem in the C++ Standard Core 
            // Language Defect Report
            //
            // http://www.open-std.org/jtc1/sc22/wg21/docs/cwg_defects.html#195
            //
            // since the suggested decision seems to be that the syntax should
            // actually be "Conditionally-Supported Behavior" in some 
            // future C++ standard I simply silence the warning.
	    m_func = __extension__ (probeFunc *) dlsym (0, "_Z20totalAllocatedMemoryv");
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
