//<<<<<< INCLUDES                                                       >>>>>>

#include "IgMProfTreeLeafFactory.h"
#include "IgMProfTreeLeaf.h"

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>
//<<<<<< PRIVATE VARIABLE DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC VARIABLE DEFINITIONS                                    >>>>>>
//<<<<<< CLASS STRUCTURE INITIALIZATION                                 >>>>>>
//<<<<<< PRIVATE FUNCTION DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

IgMProfTreeLeafFactory::IgMProfTreeLeafFactory()
    :m_allocatedLeafCount(0),
     m_poolSize(10000)
{
}

IgMProfTreeLeaf *IgMProfTreeLeafFactory::create(void)
{
    if(m_allocatedLeafCount == 0)
    {
	m_leafPool = new IgMProfTreeLeaf[m_poolSize];
	m_allocatedLeafCount = m_poolSize;
    }
    return &m_leafPool[--m_allocatedLeafCount];
}

