#ifndef MEM_PROF_LIB_IG_MPROF_BOOTSTRAP_H
# define MEM_PROF_LIB_IG_MPROF_BOOTSTRAP_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "Ig_Imports/MemProfLib/interface/config.h"
# include "signal.h"
//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>
class IgMProfTreeTextBrowser;
class IgMProfLinearBrowser;
//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>

class MEM_PROF_LIB_API IgMProfBootstrap
{
public:
    IgMProfBootstrap (void);
    ~IgMProfBootstrap (void);       
    static void dumpStatus (void);
    static void abortSigHandler (int);    
private:
    static sighandler_t m_oldAbortSigHandler;    
    static IgMProfTreeTextBrowser *m_mallocBrowser;
    static IgMProfTreeTextBrowser *m_profileBrowser;
    static IgMProfLinearBrowser *m_leaksBrowser;    
};

static IgMProfBootstrap bootstrap;

//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

#endif // MEM_PROF_LIB_IG_MPROF_BOOTSTRAP_H
