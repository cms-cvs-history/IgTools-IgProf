//<<<<<< INCLUDES                                                       >>>>>>

#include "Ig_Tools/IgProf/interface/IgMProfTotalMem.h"
#include "Ig_Tools/IgProf/src/IgMProfLinearRep.h"

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>
//<<<<<< PRIVATE VARIABLE DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC VARIABLE DEFINITIONS                                    >>>>>>
//<<<<<< CLASS STRUCTURE INITIALIZATION                                 >>>>>>
//<<<<<< PRIVATE FUNCTION DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

int totalAllocatedMemory (void)
{
    return IgMProfLinearSingleton::instance ()->totalAllocatedMemory ();
}
