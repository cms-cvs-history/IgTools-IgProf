//<<<<<< INCLUDES                                                       >>>>>>

#include "IgMProfStacktraceFlatNode.h"

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>
//<<<<<< PRIVATE VARIABLE DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC VARIABLE DEFINITIONS                                    >>>>>>
//<<<<<< CLASS STRUCTURE INITIALIZATION                                 >>>>>>
//<<<<<< PRIVATE FUNCTION DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

IgMProfFlatNode::IgMProfFlatNode (const std::string libname, const std::string symname)
    : m_allocs (0), m_calls (0), m_childrenAllocs (0), m_libname (libname), m_symname (symname) {}

void 
IgMProfFlatNode::addCaller (int address) 
{ 
    m_callers.insert (address); 
}

void 
IgMProfFlatNode::addChild (int address, allocationSize_t amount) 
{
    std::pair<int,allocationSize_t> &child = m_children [address];
    child.first++;
    child.second += amount;
}
