//<<<<<< INCLUDES                                                       >>>>>>

#include "Ig_Imports/MemProfLib/interface/IgMProfTotalMem.h"
#include "Ig_Imports/MemProfLib/src/IgMProfLinearRep.h"

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
