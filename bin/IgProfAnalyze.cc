#include "IgProfAnalyze.h"
#include <classlib/utils/DebugAids.h>
#include <classlib/utils/Callback.h>
#include <classlib/utils/Hook.h>
#include <classlib/utils/Error.h>
#include <classlib/utils/Signal.h>
#include <classlib/utils/Regexp.h>
#include <classlib/iobase/File.h>
#include <classlib/iobase/TempFile.h>
#include <classlib/iotools/StorageInputStream.h>
#include <classlib/iotools/BufferInputStream.h>
#include <classlib/iotools/IOChannelOutputStream.h>
#include <iostream>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <deque>
#include <list>
#include <map>
#include <set>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <cstdio>

#define IGPROF_MAX_DEPTH 1000

void dummy (void) {}

void
usage ()
{
  std::cerr << "igprof-analyse\n"
               "  [-r/--report KEY[,KEY]...] [-o/--order ORDER]\n"
               "  [-p/--paths] [-c/--calls] [--value peak|normal]\n"
               "  [-F/--filter-module FILE] [ -f FILTER[,FILTER...] ]\n"
               "  [-mr/--merge-regexp REGEXP]\n"
               "  [-nf/--no-filter] [-lf/--list-filters]\n"
               "  { [-t/--text], [-s/--sqlite] }\n"
               "  [--libs] [--demangle] [--gdb] [-v/--verbose]\n"
               "  [--] [FILE]...\n" << std::endl;
}

class SymbolInfo
{
public:
  std::string NAME;
  FileInfo  *FILE;
  int     FILEOFF;
  SymbolInfo(const char *name, FileInfo *file, int fileoff)
    : NAME (name), FILE (file), FILEOFF (fileoff), RANK (-1) {}
  int rank (void) { return RANK; }
  void setRank (int rank) { RANK = rank; }
private:
  int RANK;
};

class NodeInfo
{
public:
  typedef std::list <NodeInfo *> Nodes;
  typedef Nodes::iterator Iterator;
    
  Nodes CHILDREN;
  Counter *COUNTERS;
  
  NodeInfo (SymbolInfo *symbol)
  : COUNTERS (0), SYMBOL (symbol), m_reportSymbol (0) {};
  NodeInfo *getChildrenBySymbol (SymbolInfo *symbol)
  {
    ASSERT(symbol);
    for (Nodes::const_iterator i = CHILDREN.begin ();
       i != CHILDREN.end ();
       i++)
    {
      ASSERT(SYMBOL);
      if ((*i)->SYMBOL->NAME == symbol->NAME)
        return *i;
    }
    return 0;
  }
 
  Nodes::iterator begin(void) { return CHILDREN.begin(); }
  Nodes::iterator end(void) { return CHILDREN.end(); }
  
  void printDebugInfo (int level=0)
  {
    std::string indent (level*4, ' ');
    std::cerr << indent << "Node: " << this
          << " Symbol name: " << this->symbol ()->NAME
          << " File name: " << this->symbol ()->FILE->NAME
          << std::endl;
    for (Nodes::const_iterator i = CHILDREN.begin ();
    i != CHILDREN.end ();
    i++)
    {(*i)->printDebugInfo (level+1);}
  }
  
  Counter &counter (const std::string &name)
  { return this->counter (Counter::getIdForCounterName (name)); }

  Counter &counter (int id)
  {
    Counter *result = Counter::getCounterInRing (this->COUNTERS, id);
    ASSERT (result);
    return *result;
  }
  
  void removeChild (NodeInfo *node) {
    ASSERT (node);
    Nodes::iterator new_end = std::remove_if (CHILDREN.begin (), 
              CHILDREN.end (), 
              std::bind2nd(std::equal_to<NodeInfo *>(), node));
    if (new_end != CHILDREN.end ())
    { CHILDREN.erase (new_end, CHILDREN.end ()); }
  }
  
  SymbolInfo *symbol (void) const
  { return m_reportSymbol ? m_reportSymbol : SYMBOL; }
  
  void reportSymbol (SymbolInfo *reportSymbol)
  { m_reportSymbol = reportSymbol; }
  
  SymbolInfo *reportSymbol (void) const
  {
    return m_reportSymbol;
  }
  SymbolInfo *originalSymbol (void) const
  {
    return SYMBOL;
  }
  void setSymbol (SymbolInfo *symbol)
  {
    SYMBOL = symbol;
    m_reportSymbol = 0;
  }
private:
  SymbolInfo *SYMBOL; 
  SymbolInfo *m_reportSymbol;
};


class ProfileInfo 
{
private:
  struct FilesComparator
  {
    bool operator()(FileInfo *f1, FileInfo *f2) const
    {
      return strcmp(f1->NAME.c_str(), f2->NAME.c_str()) < 0;
    }
  };
  
public:
  typedef std::set<FileInfo *, FilesComparator> Files;
  typedef std::vector<SymbolInfo *> Syms;
  typedef std::vector<NodeInfo *> Nodes;
  typedef std::vector<int> Counts;
  typedef std::map<std::string, Counts> CountsMap;
  typedef std::map<std::string, std::string> Freqs;
  typedef std::map<std::string, std::string> LeaksMap;
  typedef std::map<std::string, SymbolInfo*> SymCacheByFile;
  typedef std::map<std::string, SymbolInfo*> SymCacheByName;
  typedef std::set<SymbolInfo*> SymCache;

  ProfileInfo (void)
  {
    FileInfo *unknownFile = new FileInfo ("<unknown>", false);
    SymbolInfo *spontaneousSym = new SymbolInfo ("<spontaneous>", unknownFile, 0);
    m_spontaneous = new NodeInfo (spontaneousSym);
    m_files.insert (unknownFile);
    m_syms.push_back (spontaneousSym);
    m_nodes.push_back (m_spontaneous);  
  };

  Files & files(void) { return m_files; }
  Syms & syms(void) { return m_syms; }
  Nodes & nodes(void) { return m_nodes; }
  CountsMap & counts(void) { return m_counts; }
  Freqs  & freqs(void) { return m_freqs; }
  NodeInfo *spontaneous(void) { return m_spontaneous; }
  SymCacheByFile &symcacheByFile(void) { return m_symcacheFile; }
  SymCache &symcache(void) { return m_symcache; }
private:
  Files m_files;
  Syms m_syms;
  Nodes m_nodes;
  CountsMap m_counts;
  Freqs m_freqs;
  LeaksMap m_leaks;
  SymCacheByFile m_symcacheFile;
  SymCache m_symcache;
  NodeInfo  *m_spontaneous;
};

class IgProfFilter;
class RegexpFilter;

class Configuration 
{
public:
  typedef std::list<IgProfFilter *> Filters;

  enum OutputType {
    TEXT=0,
    XML=1,
    HTML=2,
    SQLITE=3
  };
  enum Ordering {
    DESCENDING=-1,
    ASCENDING=1
  };
  
  Configuration (void);

  Filters & filters (void) {return m_filters;}
  
  void addFilter (IgProfFilter *filter) {
    if (m_filtersEnabled)
    { m_filters.push_back(filter); }
  }

  void setKey (const std::string &value) {m_key = value; };
  bool hasKey (void) { return ! m_key.empty (); }
  std::string key (void) { return m_key; }
  int keyId (void) 
  {
    int id = Counter::getIdForCounterName (m_key);
    ASSERT (id != -1);
    return id;
  }

  void setShowLib (bool value) { m_showLib = value;}
  bool showLib (void) { return m_showLib; }

  void setCallgrind (bool value)  { m_callgrind = value ? 1 : 0; }
  bool callgrind (void) { return m_callgrind == 1; }
  bool isCallgrindDefined (void) { return m_callgrind != -1; }

  void setShowCalls (bool value) { m_showCalls = value ? 1 : 0; }
  bool isShowCallsDefined (void) { return m_showCalls != -1; }
  bool showCalls (void) { return m_showCalls == 1; }

  void setDoDemangle (bool value) { m_doDemangle = value;}
  bool doDemangle (void) { return m_doDemangle; }

  void setUseGdb (bool value) { m_useGdb = value;}
  bool useGdb (void) { return m_useGdb; }

  void setShowPaths (bool value)  { m_showPaths = value;}
  bool showPaths (void) { return m_showPaths; }

  void setVerbose (bool value) { m_verbose = value;}
  bool verbose (void) { return m_verbose; }

  void setOutputType (enum OutputType value) { m_outputType = value; }
  OutputType outputType (void) { return m_outputType; }

  void setOrdering (enum Ordering value) {m_ordering = value; }
  Ordering ordering (void) { return m_ordering; }

  void setNormalValue (bool value) {m_normalValue = value; }
  bool normalValue (void) {return m_normalValue; }
 
  void setTickPeriod(float value) {m_tickPeriod = value; }
  float tickPeriod(void) {return m_tickPeriod; }
  
  void setMergeLibraries (bool value) {m_mergeLibraries = value; }
  bool mergeLibraries (void) { return m_mergeLibraries; }

  void setRegexpFilter (RegexpFilter *filter)
  {
    m_regexpFilter = filter;
  }

  RegexpFilter *regexpFilter (void)
  {
    return m_regexpFilter;
  }

  void disableFilters(void) 
  { 
    m_filtersEnabled = false; 
    m_filters.erase(m_filters.begin(), m_filters.end());
    // TODO: remove dummy assertion. (bbc800a) 
    ASSERT(m_filters.empty());
  }
private:
  Filters m_filters;
  std::string m_key;
  OutputType  m_outputType;
  Ordering  m_ordering;
  bool m_showLib;
  int m_callgrind;
  bool m_doDemangle;
  bool m_useGdb;
  bool m_showPaths;
  int m_showCalls;
  bool m_verbose;
  bool m_normalValue;
  bool m_filtersEnabled;
  float m_tickPeriod;
  bool m_mergeLibraries;
  RegexpFilter *m_regexpFilter;
};

Configuration::Configuration ()
 :m_key (""),
  m_outputType (Configuration::TEXT),
  m_ordering (Configuration::ASCENDING),
  m_showLib (false),
  m_callgrind (-1),
  m_doDemangle (false),
  m_useGdb (false),
  m_showPaths (false),
  m_showCalls (-1),
  m_verbose (false),
  m_normalValue (true),
  m_filtersEnabled (true),
  m_tickPeriod(0.01),
  m_regexpFilter(0)
{
}


class IgProfAnalyzerApplication
{
public:
  typedef std::list <std::string> ArgsList;

  IgProfAnalyzerApplication (int argc, const char **argv);
  void run (void);
  ArgsList::const_iterator parseArgs (const ArgsList &args);
  ProfileInfo* readAllDumps (ArgsList::const_iterator &begin,
             ArgsList::const_iterator &end);
  void readDump (ProfileInfo &prof, const std::string& filename);
  void analyse (ProfileInfo &prof);
  void callgrind (ProfileInfo &prof);
  void prepdata (ProfileInfo &prof);
private:
  Configuration *m_config;
  int m_argc;
  const char **m_argv;
};


IgProfAnalyzerApplication::IgProfAnalyzerApplication (int argc, const char **argv)
:m_config (new Configuration ()),
 m_argc (argc),
 m_argv (argv)
{
}

ProfileInfo *
IgProfAnalyzerApplication::readAllDumps (ArgsList::const_iterator &begin,
                       ArgsList::const_iterator &end)
{
  std::cerr << "Reading profile data" << std::endl;
  ProfileInfo *prof = new ProfileInfo;
  
  for (ArgsList::const_iterator profileFilename = begin;
       profileFilename != end;
       profileFilename++)
  {
    this->readDump (*prof, *profileFilename);
    if (m_config->verbose ())
      { std::cerr << std::endl; }
  }
  return prof;
}

float
parseHeaders (const std::string &headerLine)
{
  lat::Regexp matchHeader ("^P=\\(.*T=(.*)\\)");
  lat::RegexpMatch match;
  
  if (!matchHeader.match (headerLine, 0, 0, &match))
  {
    std::cerr << "\nThis does not look like an igprof profile stats:\n  "
              << headerLine << std::endl;
    exit (1);
  }
  ASSERT(match.numCaptures() == 1);
  std::string result = match.matchString(headerLine.c_str(), 1);
  return atof(result.c_str());
}

static int s_counter = 0;

void 
printProgress (void)
{
  s_counter = (s_counter + 1) % 100000;
  if (! s_counter)
    std::cerr << "o";
}

int 
index (const std::string &s, char c)
{
  int pos = 0;
  for (std::string::const_iterator i = s.begin ();
     i != s.end ();
     i++)
  {
    if (*i == c)
      return pos;
    pos++;
  }
  return -1;
}

class Position 
{
public:
  unsigned int operator ()(void) { return m_pos; }
  void operator ()(unsigned int newPos) { m_pos = newPos; }
private:
  unsigned int m_pos;
};



std::string
symlookup (FileInfo *file, int fileoff, const std::string& symname, bool useGdb)
{
  // This helper function beautifies a symbol name in the following fashion:
  // * If the useGdb option is true, it uses the symbol offset to look up,
  //   via nm, the closest matching symbol.
  // * If the useGdb option is not given and the symbol starts with @?
  // * If any of the above match, it simply 
  ASSERT(file);
  std::string result = symname;
  if ((lat::StringOps::find (symname, "@?") == 0) && (file->NAME != "") && (fileoff > 0))
  {
    char buffer[1024];
    if (file->NAME == "<dynamically generated>" )
    {
      sprintf(buffer, "@?0x%x{<dynamically-generated>}", fileoff);
      result = std::string() + buffer;
    }
    else
    {
      sprintf (buffer, "+%d}",fileoff);
      result = std::string ("@{")
               + lat::Filename (file->NAME).asFile().nondirectory().name()
               + buffer;
    }
  }
  
  if (useGdb && lat::Filename(file->NAME).isRegular ())
  {
    const char *name = file->symbolByOffset(fileoff); 
    if (name) return name; 
  }
  return result;
}

void
printSyntaxError (const std::string &text, 
          const std::string &filename, 
          int line, int position)
{
  std::cerr << filename << ":" << "line " << line
    << ", character " << position <<" unexpected input:\n"
        << text << "\n"
    << std::string (position, ' ') << "^\n"
    << std::endl;
}

class FilterBase
{
public:
  enum FilterType
  {     
    PRE = 1,  
    POST = 2,     
    BOTH = 3          
  };                      
    
  virtual ~FilterBase (void) {}
  virtual std::string name (void) const = 0;
  virtual enum FilterType type (void) const = 0;
};

template <class T>
class Filter : public FilterBase
{
public:
  virtual void pre (T *parent, T *node) {};
  virtual void post (T *parent, T *node) {};
};

class IgProfFilter : public Filter<NodeInfo> 
{
public:
  IgProfFilter (void)
   : m_prof (0) {}
  
  virtual void init (ProfileInfo *prof)
  { m_prof = prof; }
protected:
  ProfileInfo::Syms &syms (void) { return m_prof->syms (); }
  ProfileInfo::Nodes &nodes (void) { return m_prof->nodes (); } 
private:
  ProfileInfo  *m_prof;
};

class AddCumulativeInfoFilter : public IgProfFilter 
{
public:
  virtual void pre (NodeInfo *parent, NodeInfo *node)
  {
    ASSERT(node);
    Counter *initialCounter = node->COUNTERS;
    if (! initialCounter) return;
    
    Counter *counter= initialCounter;
    int loopCount = 0;
    do
    { 
      ASSERT (loopCount++ < 32);
      ASSERT (counter);
      ASSERT (counter->cfreq() == 0);
      ASSERT (counter->ccnt() == 0);

      counter->cfreq() = counter->freq();
      counter->ccnt() = counter->cnt();
      counter = counter->next();
    } while (initialCounter != counter);
  }

  virtual void post (NodeInfo *parent,
                 NodeInfo *node)
  {
    ASSERT (node);
    Counter *initialCounter = node->COUNTERS;
    if (!parent) 
    {
      return;
    }
    if (!initialCounter) return;
    Counter *counter = initialCounter;
    int loopCount = 0;
    do
    {
      ASSERT (loopCount++ < 32);
      ASSERT (parent);
      ASSERT (counter);
      Counter *parentCounter = Counter::addCounterToRing (parent->COUNTERS, counter->id());
      ASSERT (parentCounter);
      parentCounter->cfreq() += counter->cfreq();
      
      if (counter->isMax()) {
        if (parentCounter->ccnt() < counter->ccnt()) {
          parentCounter->ccnt() = counter->ccnt();
        }
      }
      else {
        parentCounter->ccnt() += counter->ccnt();
      }
      counter = counter->next ();
    } while (initialCounter != counter);
  }
  virtual std::string name (void) const { return "cumulative info"; }
  virtual enum FilterType type (void) const { return BOTH; }
};

class CheckTreeConsistencyFilter : public IgProfFilter
{
public:
  CheckTreeConsistencyFilter()
  :m_noParentCount(0)
  {}
  virtual void post (NodeInfo *parent,
                     NodeInfo *node)
  {
    if (!parent) {
      m_noParentCount++;
      ASSERT (m_noParentCount == 1);
    }
    Counter *initialCounter = node->COUNTERS;
    if (! initialCounter) return;
    Counter *counter= initialCounter;
    ASSERT(counter);
    do
    {
      ASSERT (counter->cnt() >= 0);
      ASSERT (counter->freq() >= 0);
      ASSERT (counter->ccnt() >= 0);
      ASSERT (counter->cfreq() >= 0);
      counter = counter->next();
    } while (initialCounter != counter);
  }
  virtual std::string name (void) const { return "Check consitency of tree"; }
  virtual enum FilterType type (void) const {return POST; }
private:
  int m_noParentCount;
};

class PrintTreeFilter : public IgProfFilter
{
public:
  PrintTreeFilter(void)
  {}

  virtual void pre (NodeInfo *parent,
                     NodeInfo *node)
  {
    m_parentStack.erase(std::find(m_parentStack.begin(), m_parentStack.end(), parent), m_parentStack.end());
    m_parentStack.push_back(parent);  
    
    std::cerr << std::string (2*(m_parentStack.size()-1),' ') << node->symbol()->NAME; 
    Counter *initialCounter = node->COUNTERS;
    if (! initialCounter) {std::cerr << std::endl; return;}
    Counter *counter= initialCounter;
    ASSERT(counter);
    do
    {
      std::cerr << " C" << counter->id() << "[" << counter->cnt() << ", "
                << counter->freq() << ", "
                << counter->ccnt() << ", "
                << counter->cfreq() << "]";
      counter = counter->next();
    } while (initialCounter != counter);
    std::cerr << std::endl;
  }
  virtual std::string name (void) const { return "Printing tree structure"; }
  virtual enum FilterType type (void) const {return PRE; }
private:
  std::vector<NodeInfo *> m_parentStack; 
};

void mergeToNode (NodeInfo *parent, NodeInfo *node)
{
  NodeInfo::Nodes::const_iterator i = node->CHILDREN.begin();
  while (i != node->CHILDREN.end())
  {
    NodeInfo *nodeChild = *i;
    ASSERT (nodeChild);
    ASSERT (nodeChild->symbol());
    NodeInfo *parentChild = parent->getChildrenBySymbol(nodeChild->symbol());
    
    // If the child is not already child of parent, simply add it.
    if (!parentChild)
    {
      parent->CHILDREN.push_back(nodeChild);
      node->removeChild(*i++);
      continue;
    }
    
    // If the child is child of the parent, accumulate all its counters to the child of the parent
    // and recursively merge it.
    while (Counter *nodeChildCounter = Counter::popCounterFromRing(nodeChild->COUNTERS))
    {
      ASSERT(nodeChildCounter);
      Counter *parentChildCounter = Counter::addCounterToRing(parentChild->COUNTERS,
                                                              nodeChildCounter->id());
      parentChildCounter->freq() += nodeChildCounter->freq();
      if (nodeChildCounter->isMax()) {
        if (parentChildCounter->cnt() < nodeChildCounter->cnt()) {
          parentChildCounter->cnt() = nodeChildCounter->cnt();
        }
      }
      else {
        parentChildCounter->cnt() += nodeChildCounter->cnt();
      }
    }
    mergeToNode(parentChild, nodeChild);
    ++i;
  }
  if (node != parent->getChildrenBySymbol(node->symbol()))
  {
    ASSERT (node->symbol());
    std::cerr << node->symbol()->NAME << std::endl;
    ASSERT (parent->getChildrenBySymbol(node->symbol()));
    ASSERT (parent->getChildrenBySymbol(node->symbol())->symbol());
    std::cerr << parent->getChildrenBySymbol(node->symbol())->symbol()->NAME << std::endl;
  }
  ASSERT(node == parent->getChildrenBySymbol(node->symbol()));
  unsigned int numOfChildren = parent->CHILDREN.size();
  parent->removeChild(node);
  ASSERT(numOfChildren == parent->CHILDREN.size() + 1);   
  ASSERT(!parent->getChildrenBySymbol(node->symbol()));
}

void mergeCountersToNode (NodeInfo *source, NodeInfo *dest)
{
  ASSERT (source);
  ASSERT (dest);
  int countersCount = 0;
  if (! source->COUNTERS)
  {
    return;
  }

  Counter *initialCounter = source->COUNTERS;
  ASSERT(initialCounter);
  Counter *sourceCounter = initialCounter;
  do
  {
    ++countersCount;
    Counter *destCounter = Counter::addCounterToRing (dest->COUNTERS,
                            sourceCounter->id ());
    ASSERT(countersCount < 32);
    ASSERT(sourceCounter);
    ASSERT(sourceCounter->freq() >= 0);
    ASSERT(sourceCounter->cnt() >= 0);
    destCounter->freq() += sourceCounter->freq ();
    if (destCounter->isMax()) {
      if (destCounter->cnt() < sourceCounter->cnt()) {
        destCounter->cnt() = sourceCounter->cnt();
      }
    }
    else {
      destCounter->cnt() += sourceCounter->cnt ();
    }
    ASSERT (destCounter->cnt() >= sourceCounter->cnt());
    ASSERT (destCounter->freq() >= sourceCounter->freq());
    ASSERT (destCounter->ccnt() == 0);
    ASSERT (destCounter->cfreq() == 0);
    ASSERT (sourceCounter->ccnt() == 0);
    ASSERT (sourceCounter->cfreq() == 0);
    sourceCounter = sourceCounter->next();
  } while (sourceCounter != initialCounter);
}



class CollapsingFilter : public IgProfFilter
{
public:
  // On the way down add extra nodes for libraries.
  virtual void pre (NodeInfo *parent, NodeInfo *node)
  {
    if (!parent)
      return;
    ASSERT(parent);
    ASSERT(node);
    ASSERT(node->symbol());
    ASSERT(node->symbol()->FILE);

    std::deque<NodeInfo *> todos;
    todos.insert (todos.begin(), node->begin(), node->end());
    node->CHILDREN.clear();  
    convertSymbol(node);

    while (!todos.empty()) 
    {
      NodeInfo *todo = todos.front();
      todos.pop_front();

      // Obtain a SymbolInfo with the filename, rather than the actual symbol name.
      convertSymbol(todo);

      // * If the parent has the same SymbolInfo, we merge the node to the parent
      // and add its children to the todo list.
      // * If there is already a child of the parent with the same symbol info,
      //   we merge with it.
      // * Otherwise we simply re-add the node.
      if (todo->symbol() == node->symbol())
      {
        todos.insert(todos.end(), todo->begin(), todo->end());
        mergeCountersToNode(todo, node);
      }
      else if (NodeInfo *same = node->getChildrenBySymbol(todo->symbol()))
      {
        same->CHILDREN.insert(same->end(), todo->begin(), todo->end());
        mergeCountersToNode(todo, same);
      }
      else
      {
        node->CHILDREN.push_back(todo);  
      }
    }
  }
  virtual enum FilterType type(void) const {return PRE; }
protected:
  virtual void convertSymbol(NodeInfo *node) = 0;
};

class UseFileNamesFilter : public CollapsingFilter 
{
  typedef std::map<std::string, SymbolInfo *> FilenameSymbols;
public:
  virtual std::string name(void) const { return "unify nodes by library."; }
  virtual enum FilterType type(void) const {return PRE; }
protected:
  void convertSymbol(NodeInfo *node)
  {
    FilenameSymbols::iterator i = m_filesAsSymbols.find (node->symbol()->FILE->NAME);
    SymbolInfo *fileInfo;
    if (i == m_filesAsSymbols.end())
    {
      fileInfo = new SymbolInfo(node->symbol()->FILE->NAME.c_str(),
                                node->symbol()->FILE, 0);
      m_filesAsSymbols.insert(FilenameSymbols::value_type(node->symbol()->FILE->NAME,
                                                          fileInfo));
    }
    else
      fileInfo = i->second;
    node->setSymbol(fileInfo);
  }
private:
  FilenameSymbols m_filesAsSymbols;
};

class RegexpFilter : public CollapsingFilter
{
  typedef std::map<std::string, SymbolInfo *> CollapsedSymbols;
  typedef std::list<std::pair<lat::Regexp *, std::string> > RegexpList; 
public:
  virtual std::string name(void) const { return "collapsing nodes using regular expression."; }
  virtual enum FilterType type (void) const { return PRE; }

  void addSubstitution(lat::Regexp *re, const std::string &with)
  {
    m_regexp.push_back (RegexpList::value_type (re, with));
  }
protected:
  void convertSymbol(NodeInfo *node)
  {
    if (m_symbols.find (node->symbol()->NAME) != m_symbols.end())
      return;

    for (RegexpList::iterator i = m_regexp.begin();
         i != m_regexp.end();
         i++)
    {
      std::string mutantString;

      if (i->first->match(node->symbol()->NAME))
      {
        mutantString = node->symbol()->NAME;
      }
      else if (node->symbol()->FILE && i->first->match(node->symbol()->FILE->NAME))
      {
        mutantString = node->symbol()->FILE->NAME;
      }
      else
      {
        continue;
      }

      std::string translatedName = lat::StringOps::replace(mutantString, 
                                                           *(i->first),
                                                           i->second);

      CollapsedSymbols::iterator i = m_symbols.find(translatedName);
      if (i == m_symbols.end())
      {
        SymbolInfo *newInfo = new SymbolInfo(translatedName.c_str(),
                                             node->symbol()->FILE, 0);
        m_symbols.insert(CollapsedSymbols::value_type(translatedName,
                                                      newInfo));
        node->setSymbol(newInfo);
      }
      else
        node->setSymbol(i->second);

      break;
    }
  }
  CollapsedSymbols m_symbols;
  RegexpList m_regexp;
};

class RemoveIgProfFilter : public IgProfFilter
{
public:
  virtual void post (NodeInfo *parent,
            NodeInfo *node)
  {
    if (!parent)
      return;
    
    ASSERT (node);
    ASSERT (node->originalSymbol ());
    ASSERT (node->originalSymbol ()->FILE);
 
    if (strstr (node->originalSymbol ()->FILE->NAME.c_str (), "IgProf.")
      || strstr (node->originalSymbol ()->FILE->NAME.c_str (), "IgHook."))
    {
      mergeToNode (parent, node);
    }
  }
  
  virtual std::string name (void) const {return "igprof remover"; }
  virtual enum FilterType type (void) const {return POST; }
};

class RemoveStdFilter : public IgProfFilter
{
public:
  /// Merge use by C++ std namespace entities to parents.
  
  virtual void post (NodeInfo *parent,
             NodeInfo *node)
  {
    if (!parent)
      return;
    ASSERT (node);
    ASSERT (node->originalSymbol ());
    // Check if the symbol refers to a definition in the c++ "std" namespace.
    const char *symbolName = node->originalSymbol ()->NAME.c_str ();
  
    if (*symbolName++ != '_' || *symbolName++ != 'Z')
      return;
    if (strncmp (symbolName, "NSt", 3) || strncmp (symbolName, "St", 2))
      return;
      
    // Yes it was.  Squash resource usage to the caller and hide this
      // function from the call tree.  (Note that the std entry may end
      // up calling something in the user space again, so we don't want
      // to lose that information.)
    std::cerr << "Symbol " << node->originalSymbol ()->NAME << " is "
              << " in " << node->originalSymbol ()->FILE->NAME 
              << ". Merging." << std::endl;
    mergeToNode (parent, node);
  }
  virtual std::string name (void) const { return "remove std"; }
  virtual enum FilterType type (void) const { return POST; }
};


class CallInfo
{ 
public:
  int64_t VALUES[3];
  SymbolInfo *SYMBOL;

  CallInfo(SymbolInfo *symbol)
  :SYMBOL(symbol)
  {
    memset(VALUES, 0, 3*sizeof(int64_t));
  }
};  

template<class T>
struct CompareBySymbol
{
  bool operator () (T *a, T *b) const
  {
    return a->SYMBOL < b->SYMBOL;
  }
};

class FlatInfo
{
public:
  typedef std::set<CallInfo*, CompareBySymbol<CallInfo> > Calls;
  typedef std::set<SymbolInfo *> Callers;
  typedef std::map<SymbolInfo *, FlatInfo *> FlatMap;

  CallInfo *getCallee (SymbolInfo *symbol, bool create=false)
  {
    static CallInfo dummy (0);
    ASSERT (symbol);
        dummy.SYMBOL = symbol;
    Calls::const_iterator i = CALLS.find (&dummy);
    if (i != CALLS.end ())
    { return *i; }
    if (!create) {
      std::cerr << symbol->NAME << std::endl;
    }
    ASSERT(create);
    CallInfo *callInfo = new CallInfo(symbol);
    this->CALLS.insert(callInfo);
    return callInfo;
  }
  
  static FlatMap &flatMap (void)
  {
    static FlatMap s_flatMap;
    return s_flatMap;
  }
  
  static FlatInfo *first (void)
  {
    ASSERT (s_first);
    return s_first;
  }
  
  static FlatInfo *get (SymbolInfo *sym, bool create=true)
  {
    FlatMap::iterator i = FlatInfo::flatMap ().find (sym);
    if (i != FlatInfo::flatMap ().end ())
      return i->second;

    if (!create)
      return 0;

    FlatInfo *result = new FlatInfo (sym);
    flatMap ().insert (FlatMap::value_type (sym, result));
    if (! s_first) s_first = result;
    return result;
  }
  
  std::string filename (void)
  {
    ASSERT(SYMBOL);
    ASSERT(SYMBOL->FILE);
    return SYMBOL->FILE->NAME;
  }
  
  const char *name (void)
  {
    ASSERT(SYMBOL);
    return SYMBOL->NAME.c_str ();
  }
  
  Callers CALLERS;
  Calls CALLS;
  SymbolInfo *SYMBOL;
  int DEPTH;
  int rank (void) {
    ASSERT (SYMBOL);
    return SYMBOL->rank (); 
  }
  void setRank (int rank) {
    ASSERT (SYMBOL);
    SYMBOL->setRank (rank); 
  }

  int64_t SELF_KEY[3];
  int64_t CUM_KEY[3];
protected:
  FlatInfo (SymbolInfo *symbol)
  : SYMBOL (symbol), DEPTH (-1) {
    memset(SELF_KEY, 0, 3*sizeof(int64_t));
    memset(CUM_KEY, 0, 3*sizeof(int64_t));
  }
private:
  static int s_keyId;
  static FlatInfo *s_first;
};

int FlatInfo::s_keyId = -1;
FlatInfo *FlatInfo::s_first = 0;

class SymbolInfoFactory 
{
public:
  typedef std::map<std::string, SymbolInfo *> SymbolsByName;

  SymbolInfoFactory (ProfileInfo *prof, bool useGdb)
   :m_prof (prof), m_useGdb (useGdb)
  {}

  SymbolInfo *getSymbol (unsigned int id)
  {
    ASSERT ( id <= m_symbols.size ());
    return m_symbols[id];
  }

  FileInfo *getFile (unsigned int id)
  {
    ASSERT (id <= m_files.size ());
    return m_files[id];
  }

  FileInfo *createFileInfo (const std::string &origname, unsigned int fileid)
  {
    
    static PathCollection paths ("PATH");
    if ((m_files.size () >= fileid + 1)  && m_files[fileid] == 0){
      std::cerr << "Error in igprof input file." << std::endl;
      exit (1);
    }
    // FIXME: die if exists $filebyid{$1};
    std::string absname = origname;
    if (index (origname, '/') == -1)
    { absname = paths.which (origname); }

    absname = lat::Filename(absname).truename();
    // TODO:
    // $absname = (abs_path($origname) || $origname)
    //     if length($origname);
    FilesByName::iterator fileIter = m_namedFiles.find (absname);
    if (fileIter != m_namedFiles.end ())
    { return fileIter->second; }
    else
    {
      FileInfo *file;
      
      if (lat::Filename(absname).isDirectory() == true)
      {
        file = new FileInfo("<dynamically generated>", false);
      }
      else 
      {
        file = new FileInfo (absname, m_useGdb); 
      }
 
      m_namedFiles.insert (FilesByName::value_type (absname, file));
      int oldsize = m_files.size ();
      int missingSize = fileid + 1 - oldsize; 
      if (missingSize > 0)
      { 
        m_files.resize (fileid + 1);
        for (int i = oldsize; i < oldsize + missingSize; i++)
        { ASSERT (m_files[i] == 0); }
      }
      m_files[fileid] = file;
      return file;
    }
  }

  
  SymbolInfo *createSymbolInfo (const std::string &line, unsigned int symid, 
                         Position &pos, int lineCount)
  {
    // Regular expressions matching the file and symbolname information.
    static lat::Regexp fRE ("F(\\d+)\\+(-?\\d+) N=\\((.*?)\\)\\)\\+\\d+\\s*");
    static lat::Regexp fWithFilenameRE ("F(\\d+)=\\((.*?)\\)\\+(-?\\d+) N=\\((.*?)\\)\\)\\+\\d+\\s*");
    static lat::RegexpMatch match;
    
    FileInfo *file = 0;
    std::string symname;
    unsigned int fileoff;

    match.reset ();
    
    if (fRE.match (line, pos (), 0, &match))
    {
      IntConverter getIntMatch (line, &match);
      fileoff = getIntMatch (2);
      symname = match.matchString (line, 3);
      int fileId = getIntMatch (1);
      file = getFile(fileId);
      ASSERT(file);
    }
    else if (fWithFilenameRE.match (line, pos (), 0, &match))
    {
      IntConverter getIntMatch (line, &match);
      fileoff = getIntMatch (3);
      symname = match.matchString (line, 4);
      file = createFileInfo (match.matchString (line, 2),
                             getIntMatch (1));
      ASSERT(file);
    }
    else
    {
      ASSERT (false);
      printSyntaxError (line, file->NAME, lineCount, pos ());
      exit (1);
    }

    pos (match.matchEnd ());
    
    symname = symlookup(file, fileoff, symname, m_useGdb);
    
    SymbolInfoFactory::SymbolsByName::iterator symiter = namedSymbols().find(symname);
    
    if (symiter != namedSymbols().end())
    {
      ASSERT(symiter->second);
      if (m_symbols.size() < symid+1) {
        m_symbols.resize(symid+1);
      }
      m_symbols[symid] = symiter->second;
      ASSERT(getSymbol(symid) == symiter->second);
      return symiter->second;
    }
   
    SymbolInfo *sym = new SymbolInfo (symname.c_str (), file, fileoff);
    namedSymbols ().insert (SymbolInfoFactory::SymbolsByName::value_type (symname, sym));
    ASSERT (symid >= m_symbols.size ()); 
    m_symbols.resize (symid + 1);
    m_symbols[symid] = sym;
    return sym;
  }

  
  static SymbolsByName &namedSymbols (void)
  {
    static SymbolsByName s_namedSymbols;
    return s_namedSymbols;
  }
  
private:
  typedef std::vector<FileInfo *> Files;
  typedef std::map<std::string, FileInfo *> FilesByName;
  typedef std::vector<SymbolInfo *> Symbols;
  Files m_files;
  Symbols m_symbols;
  FilesByName m_namedFiles;
  ProfileInfo *m_prof;
  bool m_useGdb;
};

struct SuffixOps
{
  static void splitSuffix (const std::string &fullSymbol, 
                 std::string &oldSymbol, 
                 std::string &suffix)
  {
    size_t tickPos = fullSymbol.rfind ("'");
    if (tickPos == std::string::npos)
    {
            oldSymbol = fullSymbol;
            suffix = "";
            return;
    }
    ASSERT (tickPos < fullSymbol.size ());
    oldSymbol.assign (fullSymbol.c_str (), tickPos - 1);
    suffix.assign (fullSymbol.c_str () + tickPos);
  }

  static std::string removeSuffix (const std::string &fullSymbol)
  {
    size_t tickPos = fullSymbol.rfind ("'");
    if (tickPos == std::string::npos)
    { return fullSymbol; }
    ASSERT (tickPos < fullSymbol.size ());
    return std::string (fullSymbol.c_str (), tickPos - 1);
  }
};

class TreeMapBuilderFilter : public IgProfFilter
{
public:
  TreeMapBuilderFilter (ProfileInfo *prof, Configuration *config)
  :m_prof (prof), m_zeroCounter (-1) {
    int id = Counter::getIdForCounterName (config->key ());
    ASSERT (id != -1);
    m_keyId = id;
    ASSERT (m_zeroCounter.ccnt() == 0);
    ASSERT (m_zeroCounter.cfreq () == 0);
    ASSERT (m_zeroCounter.cnt () == 0);
    ASSERT (m_zeroCounter.freq () == 0);   
  }
 
  /** Creates the GPROF like output. 
    
    * Gets invoked with a node and it's parent.
    * Finds a unique name for the node, keeping into account recursion.
    * Gets the unique FlatInfo associated to the symbols. Recursive calls result in same symbol.
    * Calculates the depths in the tree of the node.
    * Gets the relevant counter associated to the node.
    * For the case in which there is a parent.
      * Get the counter for the parent.
      * Find the parent unique symbol.
      * Find the parent unique FlatInfo.
      * Find the FlatInfo associated to the node, when it was called by the parent.
      * If the above does not exist, create it and insert it among those which can be reached from the parent.
    * Accumulate counts.
   */
  virtual void pre (NodeInfo *parent, NodeInfo *node)
  {
    ASSERT (node);
    SymbolInfo *sym = symfor (node);
    ASSERT (sym);
    FlatInfo *symnode = FlatInfo::get (sym);
    ASSERT (symnode);
    if (symnode->DEPTH < 0 || int(seen().size()) < symnode->DEPTH)
      symnode->DEPTH = int(seen().size());

    Counter *nodeCounter = Counter::getCounterInRing (node->COUNTERS, m_keyId);
    if (!nodeCounter) 
      { return; }

    bool isMax = nodeCounter->isMax();

    if (parent)
    {
      SymbolInfo *parsym = parent->symbol ();
      FlatInfo *parentInfo = FlatInfo::get (parsym, false);
      ASSERT (parentInfo);

      symnode->CALLERS.insert (parsym);

      CallInfo *callInfo = parentInfo->getCallee(sym, true);
      
      if (isMax) 
      {
        if (callInfo->VALUES[0] < nodeCounter->ccnt()) {
          callInfo->VALUES[0] = nodeCounter->ccnt();
        }
      }
      else {
        callInfo->VALUES[0] += nodeCounter->ccnt();
      }
      callInfo->VALUES[1] += nodeCounter->cfreq(); 
      callInfo->VALUES[2]++;
    }
    
    // Do SELF_KEY
    if (isMax) {
      if (symnode->SELF_KEY[0] < nodeCounter->cnt()) {
        symnode->SELF_KEY[0] = nodeCounter->cnt();
      }
    }
    else {
      symnode->SELF_KEY[0] += nodeCounter->cnt();
    }
    symnode->SELF_KEY[1] += nodeCounter->freq();
    symnode->SELF_KEY[2]++;
    
    // Do CUM_KEY
    if (isMax) {
      if (symnode->CUM_KEY[0] < nodeCounter->ccnt()) {
        symnode->CUM_KEY[0] = nodeCounter->ccnt();
      }
    }
    else {
      symnode->CUM_KEY[0] += nodeCounter->ccnt();
    }
    symnode->CUM_KEY[1] += nodeCounter->cfreq();
    symnode->CUM_KEY[2]++;
  }

  virtual void post (NodeInfo *parent,
             NodeInfo *node)
  {
    ASSERT (node);
    ASSERT (node->symbol ());
    if (seen ().count(node->symbol()->NAME) <= 0)
    {
      std::cerr << "Error: " << node->symbol()->NAME << std::endl;
    }
    ASSERT (seen ().count(node->symbol()->NAME) > 0);
    seen ().erase (node->symbol ()->NAME);
    ASSERT (seen ().count(node->symbol()->NAME) == 0);
  }

  virtual std::string name () const { return "tree map builder"; }
  virtual enum FilterType type () const { return BOTH; }

  typedef std::map<SymbolInfo *, FlatInfo *> FlatMap;
private:
  typedef std::map<std::string, SymbolInfo *>  SeenSymbols;

  SymbolInfo *symfor (NodeInfo *node) 
  {
    ASSERT (node);
    SymbolInfo *reportSymbol = node->reportSymbol ();
    if (reportSymbol)
    {
      seen ().insert (SeenSymbols::value_type (reportSymbol->NAME, 
                                             reportSymbol));
      return reportSymbol;
    }
    
    std::string suffix = "";
    
    ASSERT (node->originalSymbol ());
    std::string symbolName = node->originalSymbol ()->NAME;
    
    SeenSymbols::iterator i = seen ().find (symbolName);
    if (i != seen ().end ())
    {
      std::string newName = getUniqueName (symbolName);
      SymbolInfoFactory::SymbolsByName &namedSymbols = SymbolInfoFactory::namedSymbols ();
      SymbolInfoFactory::SymbolsByName::iterator s = namedSymbols.find (newName);
      if (s == namedSymbols.end ())
      {
        SymbolInfo *originalSymbol = node->originalSymbol ();
        reportSymbol = new SymbolInfo (newName.c_str (),
                              originalSymbol->FILE,
                              originalSymbol->FILEOFF);
        namedSymbols.insert (SymbolInfoFactory::SymbolsByName::value_type (newName, 
                                                             reportSymbol));
      }
      else
        reportSymbol = s->second;
    }
    ASSERT (node);
    node->reportSymbol (reportSymbol);
    ASSERT (node->symbol ());
    seen ().insert (SeenSymbols::value_type (node->symbol ()->NAME, 
                         node->symbol ()));
    return node->symbol ();
  }
  
  static std::string getUniqueName (const std::string &symbolName)
  {
    int index = 2;
    std::string origname = SuffixOps::removeSuffix (symbolName);
    std::string candidate = origname;
    
    do 
    {
      candidate = origname + "'" + toString (index++);
    } while (seen ().find (candidate) != seen ().end ());
    return candidate;   
  }

  static SeenSymbols &seen (void)
  {
    static SeenSymbols s_seen;
    return s_seen;
  }

  ProfileInfo *m_prof;
  Counter m_zeroCounter;
  int m_keyId;
};

class TextStreamer
{
public:
  TextStreamer (lat::File *file)
  :m_file (file) {}
  
  TextStreamer &operator<< (const std::string &string)
  { 
    m_file->write (string.c_str (), string.size ());
    return *this;
  }
  TextStreamer &operator<< (const char *text)
  { m_file->write (text, strlen (text)); return *this; }
  
  TextStreamer &operator<< (int num)
  {
    char buffer[32];
    sprintf (buffer, "%d", num);
    m_file->write (buffer, strlen (buffer));
    return *this;
  }
  
private:
  lat::File *m_file;
};

static lat::Regexp SYMCHECK_RE ("IGPROF_SYMCHECK <(.*)>");
static lat::Regexp STARTS_AT_RE (".*starts at .* <([A-Za-z0-9_]+)(\\+\\d+)?>");
static lat::Regexp NO_LINE_NUMBER ("^No line number .*? <([A-Za-z0-9_]+)(\\+\\d+)?>");


void symremap (ProfileInfo &prof, std::vector<FlatInfo *> infos, bool usegdb, bool demangle)
{
  typedef std::vector<FlatInfo *> FlatInfos;

  if (usegdb)
  {
    lat::Filename tmpFilename ("/tmp/igprof-analyse.gdb.XXXXXXXX");
    lat::File *file = lat::TempFile::file (tmpFilename);
    TextStreamer out (file);
    out << "set width 10000\n";

    std::multimap<FileInfo *, SymbolInfo *> fileAndSymbols;
    
    for (FlatInfos::const_iterator i = infos.begin ();
         i != infos.end ();
         i++)
    {
      SymbolInfo *sym = (*i)->SYMBOL;
      if (!sym)
        continue;
      if ((! sym->FILE) || (! sym->FILEOFF) || (!sym->FILE->NAME.size()))
      {
        continue;
      }
      if (sym->FILE->symbolByOffset(sym->FILEOFF)) {
        continue;
      }
      fileAndSymbols.insert(std::pair<FileInfo *, SymbolInfo *>(sym->FILE, sym));
    }

    FileInfo *prevfile = 0;
    
    for (std::multimap<FileInfo *, SymbolInfo *>::iterator i = fileAndSymbols.begin();
         i != fileAndSymbols.end();
         i++) 
    {
      SymbolInfo *sym = i->second;
      FileInfo *fileInfo =i->first;

      ASSERT(sym);
      ASSERT(file);
      prof.symcache ().insert(sym);
      
      if (!lat::StringOps::contains(fileInfo->NAME, "<dynamically")) 
      {
        if (fileInfo != prevfile)
        {
          out << "file " << fileInfo->NAME << "\n";
          prevfile = fileInfo; 
        }
        out << "echo IGPROF_SYMCHECK <" << (intptr_t) sym << ">\\n\n"
               "info line *" << toString (sym->FILEOFF)<< "\n";
      }
    }
    file->close ();
    delete file;
    
    PipeReader gdb ("gdb --batch --command=" + std::string (tmpFilename));
    
    std::string oldname;
    std::string suffix;
    SymbolInfo *sym = 0;
    
    while (gdb.output())
    {
      std::string line;
      std::getline (gdb.output(), line);
      
      if (!gdb.output())
        break;
      if (line.empty ())
        continue;

      lat::RegexpMatch match;

      
      if (SYMCHECK_RE.match(line, 0, 0, &match))
      {
        ProfileInfo::SymCache::iterator symitr = prof.symcache ().find((SymbolInfo *)(atol(match.matchString (line, 1).c_str())));
        ASSERT(symitr !=prof.symcache().end());
        sym = *symitr;
        SuffixOps::splitSuffix (sym->NAME, oldname, suffix);
      }
      else if (STARTS_AT_RE.match(line, 0, 0, &match))
      {
        ASSERT (sym);
        sym->NAME = match.matchString (line, 1) + suffix;
        sym = 0; suffix = ""; 
      }
      else if (NO_LINE_NUMBER.match(line, 0, 0, &match))
      {
        ASSERT (sym);
        sym->NAME = match.matchString (line, 1) + suffix;
        sym = 0; suffix = "";
      }
    }
    unlink (tmpFilename);
  }

  if (demangle)
  {
    lat::Filename tmpFilename ("/tmp/igprof-analyse.c++filt.XXXXXXXX");
    lat::File *file = lat::TempFile::file (tmpFilename);
    TextStreamer out (file);

    for (FlatInfos::const_iterator i = infos.begin(); i != infos.end(); i++)
    {
      SymbolInfo *symbol = (*i)->SYMBOL;
      ASSERT (symbol);
      out << (intptr_t) (symbol) << ": " << symbol->NAME << "\n";
    }
    file->close ();
    delete file;

    lat::File symbolFile (tmpFilename);
    PipeReader cppfilt ("c++filt", &symbolFile);
    
    while (cppfilt.output ())
    {
      std::string line;
      std::getline (cppfilt.output (), line);
      if (! cppfilt.output ())
        break;
      if (line.empty ())
        continue;
      const char *lineStart = line.c_str ();
      char *endptr = 0;

      SymbolInfo *symbolPtr = (SymbolInfo *)(strtol (lineStart, &endptr, 10));
      ASSERT (endptr != lineStart);
      ASSERT (*endptr == ':');
      symbolPtr->NAME.assign (endptr+2, lineStart+line.size () - (endptr+2));
    }
    unlink (tmpFilename);
  }
}

class MallocFilter : public IgProfFilter
{
public:
  virtual void init (ProfileInfo *prof) {
    IgProfFilter::init (prof);
    m_filter = "malloc", "calloc", "realloc", "memalign", "posix_memalign", 
               "valloc", "zmalloc", "zcalloc", "zrealloc", "_Znwj", 
               "_Znaj", "_Znam";
  }
  
  virtual void post (NodeInfo *parent, NodeInfo *node)
  {
    ASSERT (node);
    ASSERT (node->symbol ());
    ASSERT (m_filter.contains (std::string ("_Znaj")));
    if (!parent) {
     return;
    }
    if (!node->COUNTERS) {
      return;
    }
    if (! m_filter.contains (node->symbol ()->NAME)) 
    {
      return;
    }
    this->addCountsToParent (parent, node);
  }
  
  virtual std::string name (void) const { return "malloc"; }
  virtual enum FilterType type (void) const { return POST; }
  
private:
  void addCountsToParent (NodeInfo *parent, NodeInfo *node)
  {
    ASSERT (parent);
    ASSERT (node);
    ASSERT (m_filter.contains (node->originalSymbol ()->NAME));
    int countersCount = 0;
    ASSERT (node->COUNTERS);
    Counter *initialCounter = node->COUNTERS;
    ASSERT(initialCounter);
    Counter *childCounter = initialCounter;
    do
    {
      ++countersCount;
      Counter *parentCounter = Counter::addCounterToRing (parent->COUNTERS, 
                                childCounter->id ());
      ASSERT(countersCount < 32);
      ASSERT(parentCounter);
      ASSERT(childCounter->freq() >= 0);
      ASSERT(childCounter->cnt() >= 0);
      parentCounter->freq() += childCounter->freq ();
      if (parentCounter->isMax()) {
        if (parentCounter->cnt() < childCounter->cnt()) {
          parentCounter->cnt() = childCounter->cnt();
        }
      }
      else {
        parentCounter->cnt() += childCounter->cnt ();
      }
      ASSERT (parentCounter->cnt() >= childCounter->cnt());
      ASSERT (parentCounter->freq() >= childCounter->freq());
      ASSERT (childCounter->ccnt() == 0);
      ASSERT (childCounter->cfreq() == 0);
      ASSERT (parentCounter->ccnt() == 0);
      ASSERT (parentCounter->cfreq() == 0);
      childCounter = childCounter->next();
    } while (childCounter != initialCounter);

   parent->removeChild (node);
  }
  SymbolFilter m_filter;
};

class IgProfGccPoolAllocFilter : public IgProfFilter
{
public:
  virtual void init (ProfileInfo *prof) {
    IgProfFilter::init (prof);
    m_filter = "_ZNSt24__default_alloc_templateILb1ELi0EE14_S_chunk_allocEjRi",
               "_ZNSt24__default_alloc_templateILb1ELi0EE9_S_refillEj",
               "_ZNSt24__default_alloc_templateILb1ELi0EE8allocateEj";
  }

  virtual void post (NodeInfo *parent, NodeInfo *node) 
  {
    if (! m_filter.contains (node->symbol ()->NAME)) return;
    parent->removeChild (node);
  }
  virtual std::string name (void) const { return "gcc_pool_alloc"; }
  virtual enum FilterType type (void) const { return POST; }
private:
  SymbolFilter m_filter;
};

class TreeInfoFilter : public IgProfFilter
{
public:
  virtual void init (ProfileInfo *prof) {
    IgProfFilter::init (prof);
  }

  virtual void pre (NodeInfo *parent, NodeInfo *node) 
  {
    Counter *i = node->COUNTERS;
    while (i)
      {i = i->next (); if (i == node->COUNTERS) break;}
  }
  virtual std::string name (void) const { return "tree_info_filter"; }
  virtual enum FilterType type (void) const { return PRE; }
private:
  SymbolFilter m_filter;
};

// Regular expressions matching the symbol information header.
static lat::Regexp vWithDefinitionRE ("V(\\d+)=\\((.*?)\\):\\((\\d+),(\\d+)(,(\\d+))?\\)\\s*");

static int
parseStackLine(const char *line, std::vector<NodeInfo *> &nodestack)
{
  // Matches the same as matching "^C(\\d+)\\s*" and resize nodestack to $1.
  if ((line[0] == 0) || line[0] != 'C')
    return 0;
  char *endptr = 0;
  int newPosition = strtol (line+1, &endptr, 10) - 1;
  if (endptr == line+1)
  { return 0; }

  do 
  { ++endptr; } while (*endptr == ' ' || *endptr == '\t');
  
  int stackSize = nodestack.size ();
  ASSERT (newPosition <= stackSize);
  ASSERT (newPosition >= 0);
  int difference = newPosition - stackSize;
  if (difference > 0)
    nodestack.resize (newPosition);
  else
    nodestack.erase (nodestack.begin () + newPosition, nodestack.end ());
  return endptr - line;
}

static bool
parseFunctionRef(const char *lineStart, Position &pos, 
                 unsigned int &symid, unsigned int fileoff) 
{
  const char *line = lineStart + pos ();
  // Matches "FN(\\d+)\\+\\d+\\s*" and sets symid = $1
  if (line[0] != 'F' && line[1] != 'N')
  { return false; }
  char *endptr = 0;
  int fnRef = strtol (line+2, &endptr, 10);
  if (endptr == line + 2)
  {return false; }
  if (*endptr != '+' )
  { return false; }
  char *endptr2 = 0;
  int offset = strtol (endptr, &endptr2, 10);
  if (endptr == endptr2)
  { return false; }
  
  symid = fnRef;
  fileoff = offset;

  while (*endptr2 == ' ' || *endptr2 == '\t')
  { ++endptr2; }
  pos (endptr2 - lineStart);
  return true;
}

static bool
parseFunctionDef(const char *lineStart, Position &pos, unsigned int &symid)
{
  const char *line = lineStart + pos ();
  // Matches FN(\\d+)=\\( and sets symid = $1
  if (line[0] != 'F' && line[1] != 'N')
  { return false; }
  char *endptr = 0;
  int fnRef = strtol (line+2, &endptr, 10);
  if (endptr == line + 2)
  {return false; }
  if (*endptr++ != '=')
  { return false; }
  if (*endptr++ != '(')
  { return false; }
  
  symid = fnRef;

  pos (endptr - lineStart);
  return true;
}

static bool
parseCounterVal (const char *lineStart, Position &pos, 
                 int &ctrid, int64_t &ctrfreq, 
                 int64_t &ctrvalNormal, int64_t &ctrvalPeak)
{
  // Matches "V(\\d+):\\((\\d+),(\\d+)(,(\\d+))?\\)\\s*" and then sets the arguments accordingly. 
  const char *line = lineStart + pos ();
  
  if (line[0] != 'V')
  { return false; }
  
  char *endptr = 0;
  int cntRef = strtol (++line, &endptr, 10);
  if (endptr == line || *endptr != ':' || *++endptr != '(')
  { return false; }
  
  char *endptr2 = 0;
  int64_t count1 = strtoll (++endptr, &endptr2, 10);
  if (endptr2 == endptr || *endptr2 != ',')
  { return false; }

  char *endptr3 = 0;
  int64_t count2 = strtoll (++endptr2, &endptr3, 10);
  if (endptr3 == endptr2 || *endptr3 != ',')
  { return false; }
  
  char *endptr4 = 0;
  int64_t count3 = strtoll (++endptr3, &endptr4, 10);
  if (endptr3 == endptr4)
  { return false; }

  if (*endptr4++ != ')')
  { return false; }
  
  ctrid = cntRef;
  ctrfreq = count1;
  ctrvalNormal = count2;
  ctrvalPeak= count3;
  while (*endptr4 == ' ' || *endptr4 == '\t')
  { endptr4++; }
  pos (endptr4 - lineStart);
  return true;
}

static bool
parseCounterDef(const std::string &line, int pos, int flags, lat::RegexpMatch *match)
{
  return vWithDefinitionRE.match (line, pos, 0, match);
}

static bool
parseLeak (const char *lineStart, Position &pos, int &leakAddress, int64_t &leakSize)
{
  // ";LK=\\(0x[\\da-z]+,\\d+\\)\\s*"
  const char *line = lineStart + pos ();
  if (*line != ';' || *++line != 'L' || *++line != 'K' || *++line != '=' 
    || *++line != '(' || *++line != '0' || *++line != 'x')
  { return false; }
  
  char *endptr = 0;
  int address = strtol (++line, &endptr, 16);
  if (endptr == line)
  { return false; }
  
  if (*endptr++ != ',')
  { return false; }
  
  char *endptr2 = 0;
  int64_t size = strtoll (endptr, &endptr2, 10);
  if (endptr == line)
  { return false; }
  if (*endptr2++ != ')')
  { return false; }
  
  leakAddress = address;
  leakSize = size;
  
  while (*endptr2 == ' ' || *endptr2 == '\t')
  { endptr2++; }
  
  pos (endptr2 - lineStart);

  return true;
}


void
IgProfAnalyzerApplication::readDump (ProfileInfo &prof, const std::string &filename)
{
  ProfileInfo::Nodes &nodes = prof.nodes ();
  typedef std::vector<NodeInfo *> Nodes;
  Nodes nodestack;
  nodestack.reserve (IGPROF_MAX_DEPTH);
  
  if (m_config->verbose ())
    std::cerr << "Parsing igprof output file: " << filename << std::endl;
  FileReader reader (filename);
  
  std::string line;
  line.reserve (FileOpener::INITIAL_BUFFER_SIZE);
  reader.readLine ();
  reader.assignLineToString (line);
  m_config->setTickPeriod(parseHeaders (line));
  
  PathCollection paths ("PATH");
  
  int lineCount = 1;
  lat::RegexpMatch match;

  Counter::setKeyName (m_config->key ());
  SymbolInfoFactory symbolsFactory (&prof, m_config->useGdb ());
  
  while (! reader.eof ())
  {
    // One node per line.
    // int fileid;
    unsigned int fileoff = 0xdeadbeef;
    // int ctrval;
    // int ctrfreq;
    Position pos;

    printProgress ();
    reader.readLine ();
    reader.assignLineToString (line);
  
    int newPos = parseStackLine (line.c_str (), nodestack);
    if (! newPos) 
      continue;
    
    pos (newPos);

    
    // Find out the information about the current stack line.
    SymbolInfo *sym;

    unsigned int symid = 0xdeadbeef;
    
    if (line.size() <= pos())
    {
      printSyntaxError (line, filename, lineCount, pos ());
      exit (1);
    }
    else  if (line.size () > pos()+2
          && parseFunctionRef (line.c_str (), pos, symid, fileoff))
    {
      sym = symbolsFactory.getSymbol (symid);
      if (!sym) {
        std::cerr << "Error at line " << lineCount << ": symbol with id " 
                  << symid << " was referred before being defined in input file.\n" 
                  << "> " << line << std::endl; 
        exit(1);
      }
    }
    else if (line.size() > pos()+2
         && parseFunctionDef (line.c_str (), pos, symid))
    {
      sym = symbolsFactory.createSymbolInfo (line, symid, pos, lineCount);
    }
    else
    {
      printSyntaxError (line, filename, lineCount, pos ());
      exit (1);
    }
    
    NodeInfo* parent = nodestack.empty () ? prof.spontaneous () : nodestack.back ();
    
    NodeInfo* child = parent ? parent->getChildrenBySymbol (sym) : 0;
    
    if (! child)
    {
      child = new NodeInfo (sym);
      nodes.push_back (child);
      if (parent)
      { parent->CHILDREN.push_back (child); }
    }

    nodestack.push_back (child);
    
    match.reset ();

    // Read the counter information.
    while (true)
    {
      int ctrid;
      int64_t ctrval;
      int64_t ctrfreq;
      int64_t ctrvalNormal;
      int64_t ctrvalPeak;
      int leakAddress;
      int64_t leakSize;
      
      if (line.size () == pos())
      { break; }
      else if (line.size() >= pos()+2
           && parseCounterVal (line.c_str (), pos, ctrid, ctrfreq, ctrvalNormal, ctrvalPeak))
      {
        // FIXME: should really do:
        // $ctrname = $ctrbyid{$1} || die;

        ctrval = m_config->normalValue () ? ctrvalNormal : ctrvalPeak;
      }
      else if (line.size() >= pos()+2
           && parseCounterDef (line, pos (), 0, &match))
      {
        // FIXME: should really do:
        // die if exists $ctrbyid{$1};
        std::string ctrname = match.matchString (line, 2);
        IntConverter getIntMatch (line, &match);
        ctrid = getIntMatch (1);
        Counter::addNameToIdMapping (ctrname, ctrid,
                                     (ctrname == "PERF_TICKS" 
                                      && ! m_config->callgrind ()));
        ctrfreq = getIntMatch (3);
        ctrval = m_config->normalValue () ? getIntMatch (4)
                            : getIntMatch (6);
        pos (match.matchEnd ());
        match.reset ();
      }
      else if (line.size() >= pos()+3
           && parseLeak (line.c_str (), pos, leakAddress, leakSize))
      {
        //# FIXME: Ignore leak descriptors for now
        continue;
      }
      else
      {
        printSyntaxError (line, filename, lineCount, pos ());
        exit (1);
      }

      Counter *counter = Counter::addCounterToRing (child->COUNTERS, ctrid);
      ASSERT(counter->id() == ctrid);
      ASSERT (counter == Counter::getCounterInRing(child->COUNTERS, ctrid));
      ASSERT (counter);
    
      if (m_config->hasKey () && ! Counter::isKey (ctrid)) continue;
      
      counter->freq() += ctrfreq;
      counter->cnt() += ctrval;
    }
    lineCount++;
  }
}

void
printAvailableCounters (const Counter::IdCache &cache)
{
  typedef Counter::IdCache::const_iterator iterator;
  lat::StringList tempList;
  for (iterator i = cache.begin (); i != cache.end (); i++)
  { tempList.push_back ((*i).first); }
  std::cerr << "No profile counter selected for reporting, please select one of: "
        << lat::StringOps::join (tempList, std::string (",")) << std::endl;
  exit (1);
}

template <class T>
class StackItem
{
public:
  typedef T Node;
  StackItem (Node *parent, Node *pre, Node *post)
  : m_parent (parent),
    m_pre (pre),
    m_post (post)
  {}
  Node *parent (void) { return m_parent; }
  Node *prev (void) { return m_pre; }
  Node *post (void) { return m_post; }
private:
  Node *m_parent;
  Node *m_pre;
  Node *m_post;
};

template <class T>
class StackManipulator
{
public:
  typedef StackItem<T> Item;
  typedef typename Item::Node Node;
  typedef typename Node::Iterator NodesIterator;
  typedef typename std::list<Item> Container;
  
  StackManipulator (Container *stack, T *first)
  : m_stack (stack) {
    m_stack->push_back (Item (0, first, 0));
  }
  
  void addChildrenToStack (Node *node)
  {
    ASSERT (node);
    for (NodesIterator i = node->begin (); 
       i != node->end (); i++)
    { m_stack->push_back (Item (node, *i, 0)); }
  }
  
  void addToStack (Node *parent, Node *prev)
  { m_stack->push_back (Item (parent, 0, prev)); }

private:
  Container *m_stack;
};

template <class T>
void walk (T *first, Filter<T> *filter=0)
{
  // TODO: Apply more than one filter at the time.
  //     This method applies one filter at the time. Is it worth to do
  //     the walk only once for all the filters? Should increase locality
  //     as well...
  ASSERT (filter);
  ASSERT (first);
  typedef StackManipulator<T> Manipulator;
  typedef typename Manipulator::Container Stack;
  typedef typename Manipulator::Item Item;
  typedef typename Manipulator::Node Node;

  Stack stack; 
  Manipulator manipulator (&stack, first);

  while (!stack.empty ())
  {
    Item item = stack.back (); stack.pop_back ();
    Node *node = item.prev ();
    Node *parent = item.parent ();
    if (node)
    {
      if ( filter->type () & FilterBase::PRE)
        { filter->pre (parent, node); }
      if ( filter->type () & FilterBase::POST)
        { manipulator.addToStack (parent, node); }
      manipulator.addChildrenToStack (node);
    }
    else
    {
      filter->post (parent, item.post ());
    }
  }
}

void
IgProfAnalyzerApplication::prepdata (ProfileInfo& prof/*, // FIXME: is all this actually needed???
                   std::list<int> &ccnt, 
                   std::list<int> &cfreq*/)
{
  for (Configuration::Filters::const_iterator i = m_config->filters ().begin ();
     i != m_config->filters ().end ();
     i++)
  {
    (*i)->init (&prof);
    if (m_config->verbose ())
    { std::cerr << "Applying filter " << (*i)->name () 
                    << "." << std::endl; }
      walk<NodeInfo>(prof.spontaneous(), *i);
  }

  if (m_config->mergeLibraries())
  {
//    walk<NodeInfo> (prof.spontaneous(), new PrintTreeFilter);

    if (m_config->verbose ())
    {
      std::cerr << "Merge nodes belonging to the same library." << std::endl;
    }
    UseFileNamesFilter *filter = new UseFileNamesFilter;
    walk<NodeInfo> (prof.spontaneous(), filter);
//    walk<NodeInfo> (prof.spontaneous(), new PrintTreeFilter);
  }

  if (m_config->regexpFilter())
  {
    if (m_config->verbose ())
    {
      std::cerr << "Merge nodes using user-provided regular expression." << std::endl;
    }
    walk<NodeInfo> (prof.spontaneous(), m_config->regexpFilter());
  }

  if (m_config->verbose ())
  {
    std::cerr << "Summing counters" << std::endl;
  }
  IgProfFilter *sumFilter = new AddCumulativeInfoFilter ();
  walk<NodeInfo> (prof.spontaneous(), sumFilter);
  walk(prof.spontaneous(), new CheckTreeConsistencyFilter());
}

class FlatInfoComparator 
{
public:
  FlatInfoComparator (int counterId, int ordering, bool cumulative=true)
  :m_counterId (counterId),
   m_ordering (ordering),
   m_cumulative (cumulative)
  {}
  bool operator() (FlatInfo *a, FlatInfo *b)
  {
    int64_t cmp;
    cmp = cmpnodekey (a, b);
    if (cmp > 0) return true;
    else if (cmp < 0) return false;
    cmp = cmpcallers (a, b);
    if (cmp > 0) return true;
    else if (cmp < 0) return false;
    return strcmp (a->name (), b->name ()) < 0;
  }
private:
  int64_t cmpnodekey (FlatInfo *a, FlatInfo *b)
  {
    int64_t aVal = a->CUM_KEY[0];
    int64_t bVal = b->CUM_KEY[0];
    return  -1 * m_ordering * (bVal - aVal);
  }

  int cmpcallers (FlatInfo *a, FlatInfo *b)
  {
    return b->DEPTH - a->DEPTH;
  }
  
  int m_counterId;
  int m_ordering;
  int m_cumulative;
};


class GProfRow
{
public:
  int FILEOFF;
  float PCT;
  
  void initFromInfo (FlatInfo *info)
  {
    ASSERT(info);
    m_info = info;
  }

  const char *name()
  {
    return m_info->name ();
  }

  const char *filename()
  {
    return m_info->filename ().c_str();
  }
  
  const unsigned int depth()
  {
    return m_info->DEPTH;
  }

  const unsigned int rank() 
  {
    return m_info->rank();
  }
  
  intptr_t symbolId()
  {
    return (intptr_t) (m_info->SYMBOL);
  }
  
  intptr_t fileId()
  {
    return (intptr_t) (m_info->SYMBOL->FILE);
  } 
private:
  FlatInfo *m_info;
};

class OtherGProfRow : public GProfRow
{
public:
  int64_t SELF_COUNTS;
  int64_t CHILDREN_COUNTS;
  int64_t SELF_CALLS;
  int64_t TOTAL_CALLS;
  int64_t SELF_PATHS;
  int64_t TOTAL_PATHS;
 
  OtherGProfRow(void)
  :SELF_COUNTS(0), CHILDREN_COUNTS(0), SELF_CALLS(0),
   TOTAL_CALLS(0), SELF_PATHS(0), TOTAL_PATHS(0)
  {}
};

std::ostream & operator<<(std::ostream &stream, OtherGProfRow& row)
{
  stream << "[" << row.SELF_COUNTS << " " << row.CHILDREN_COUNTS << " "
         << row.SELF_CALLS << " " << row.SELF_CALLS << " "
         << row.SELF_PATHS << " " << row.TOTAL_PATHS << "]" << std::endl;
  return stream;
}
    

template <int ORDER>
struct CompareCallersRow
{
  bool operator () (OtherGProfRow *a, OtherGProfRow *b)
  { 
    int64_t callsDiff = ORDER * (a->SELF_COUNTS - b->SELF_COUNTS);
    int64_t cumDiff = ORDER * (a->CHILDREN_COUNTS - b->CHILDREN_COUNTS);
    if (callsDiff) return callsDiff < 0;
    if (cumDiff) return cumDiff < 0;
    return strcmp(a->name(), b->name()) < 0;
  }
};

/* This is the class which represent an entry in the gprof style flat output.
 *
 * CUM is the accumulated counts for that entry, including the count for the children.
 * SELF is the accumulated counts for only that entry, excluding the counts coming from children.
 * KIDS is the accumulated counts for the entries that are called by that entry.
 *
 * NOTE: one should have CUM = SELF+KIDS,  so I don't quite understand why I actually need one of the three.
 *
 * SELF_ALL I don't remember what it is.
 * CUM_ALL I don't remember what it is.
 */

class MainGProfRow : public GProfRow 
{
public:
  typedef std::set <OtherGProfRow *, CompareCallersRow<1> > Callers;
  typedef std::set <OtherGProfRow *, CompareCallersRow<-1> > Calls;
  
  int64_t CUM;
  int64_t SELF;
  int64_t KIDS;
  int64_t SELF_ALL[3];
  int64_t CUM_ALL[3];
  
  Callers CALLERS;
  Calls CALLS;
};

float percent (int64_t a, int64_t b)
{
  double value = static_cast<double>(a) / static_cast<double>(b);
  if (value < 0. || value > 1.0) 
  {
    std::cerr << "Something is wrong. Invalid percentage value: " << value * 100. << std::endl;
    std::cerr << "N: " << a << " D: " << b << std::endl;
    std::cerr << "Quitting." << std::endl;  
    // FIXME: remove assertion and simply quit.
    ASSERT(false);
    exit(1);
  }
  return value * 100.;
}

class GProfMainRowBuilder 
{
public:
  GProfMainRowBuilder (FlatInfo *info, int64_t totals)
   : m_info (info), m_row (0), m_callmax (0), m_totals(totals)
  {
    init ();
  }

  void addCaller (SymbolInfo *callerSymbol)
  { 
    ASSERT (m_row);
    FlatInfo *origin = FlatInfo::flatMap ()[callerSymbol];
    CallInfo *thisCall = origin->getCallee(m_info->SYMBOL);
   
    ASSERT(thisCall);
    if (!thisCall->VALUES[0])
      { return; }
    OtherGProfRow *callrow = new OtherGProfRow ();
    callrow->initFromInfo (origin);
    callrow->PCT = percent (thisCall->VALUES[0], m_totals);
    
    callrow->SELF_COUNTS = thisCall->VALUES[0];
    callrow->CHILDREN_COUNTS = origin->CUM_KEY[0];
    
    callrow->SELF_CALLS = thisCall->VALUES[1];
    callrow->TOTAL_CALLS = origin->CUM_KEY[1]; 

    callrow->SELF_PATHS = thisCall->VALUES[2]; 
    callrow->TOTAL_PATHS = origin->CUM_KEY[2];
    m_row->CALLERS.insert (callrow);
  }  
  
  void addCallee (CallInfo *thisCall)
  {
    // calleeInfo is the global information about this symbol
    // thisCall contains the information when this symbol is called by m_info
    ASSERT (m_row);
    FlatInfo *calleeInfo = FlatInfo::flatMap()[thisCall->SYMBOL];

    if (!thisCall->VALUES[0])
      { return; }

    if (m_callmax < thisCall->VALUES[0])
      m_callmax = thisCall->VALUES[0];

    OtherGProfRow *callrow = new OtherGProfRow ();
    ASSERT (calleeInfo);
    callrow->initFromInfo (calleeInfo);
    callrow->PCT = percent (thisCall->VALUES[0], m_totals);

    callrow->SELF_COUNTS = thisCall->VALUES[0];
    callrow->CHILDREN_COUNTS = calleeInfo->CUM_KEY[0];
    
    callrow->SELF_CALLS = thisCall->VALUES[1];
    callrow->TOTAL_CALLS = calleeInfo->CUM_KEY[1];
    
    callrow->SELF_PATHS = thisCall->VALUES[2]; 
    callrow->TOTAL_PATHS = calleeInfo->CUM_KEY[2];
    
    m_row->CALLS.insert (callrow);
    //ASSERT (callrow->SELF_CALLS <= callrow->TOTAL_CALLS);
  }
  
  void init (void)
  {
    m_row = new MainGProfRow ();
    m_row->initFromInfo (m_info);
    m_row->PCT = percent (m_info->CUM_KEY[0], m_totals);
    m_row->CUM = m_info->CUM_KEY[0];
    m_row->SELF = m_info->SELF_KEY[0];   
  }

  MainGProfRow *build (bool isMax)
  {
    ASSERT (m_row);
    if (isMax) {
      m_row->KIDS = m_callmax;
    }
    else {
      m_row->KIDS = m_info->CUM_KEY[0] - m_info->SELF_KEY[0];
    }
    memcpy (m_row->CUM_ALL, m_info->CUM_KEY, 3*sizeof(int64_t));
    memcpy (m_row->SELF_ALL, m_info->SELF_KEY, 3*sizeof(int64_t));
    return m_row;
  }
private:
  FlatInfo *m_info;
  MainGProfRow *m_row;
  int64_t m_callmax;
  int64_t m_totals;
  MainGProfRow *m_mainCallrow; 
};

void
printHeader (const char *description, const char *kind, 
       bool showpaths, bool showcalls, int maxval, int maxcnt)
{
  std::cout << "\n" << std::string (70, '-') << "\n" 
        << description << "\n\n";
  std::cout << "% total  ";
  (AlignedPrinter (maxval)) (kind);
  if (showcalls) { (AlignedPrinter (maxcnt)) ("Calls"); }
  if (showpaths) { (AlignedPrinter (maxcnt)) ("Paths"); }
  std::cout << "Function\n";
}

int64_t
max (int64_t a, int64_t b)
{
  return a > b ? a : b;
}

class SortRowBySelf
{
public:
  bool operator () (MainGProfRow *a, MainGProfRow *b) {
    return a->SELF > b->SELF;
    //if (a->SELF != b->SELF) return a->SELF > b->SELF;
    //if (a->DEPTH != b->DEPTH) return a->DEPTH < b->DEPTH;
    //return a->NAME < b->NAME;
  }
};

void
IgProfAnalyzerApplication::analyse(ProfileInfo &prof)
{
  prepdata(prof);
  if (m_config->verbose())
    { std::cerr << "Building call tree map" << std::endl; }
  IgProfFilter *callTreeBuilder = new TreeMapBuilderFilter(&prof, m_config);
  walk(prof.spontaneous(), callTreeBuilder);
  // Sorting flat entries
  if (m_config->verbose ())
    { std::cerr << "Sorting" << std::endl; }
  int rank = 1;
  typedef std::vector <FlatInfo *> FlatVector;
  FlatVector sorted; 
  if (FlatInfo::flatMap().empty()) {
    std::cerr << "Could not find any information to print." << std::endl; 
    exit(1);
  }

  for (FlatInfo::FlatMap::const_iterator i = FlatInfo::flatMap ().begin ();
     i != FlatInfo::flatMap ().end ();
       i++)
  { sorted.push_back (i->second); }
  
  sort (sorted.begin(), sorted.end(), FlatInfoComparator(m_config->keyId (),
        m_config->ordering ()));
  
  for (FlatVector::const_iterator i = sorted.begin(); i != sorted.end(); i++)
  { (*i)->setRank (rank++); }
  
  if (m_config->doDemangle () || m_config->useGdb ())
  {
    if (m_config->verbose ())
      std::cerr << "Resolving symbols" << std::endl;
    symremap (prof, sorted, m_config->useGdb (), m_config->doDemangle ());
  }
 
  if (sorted.empty()) {
    std::cerr << "Could not find any sorted information to print." << std::endl;
    exit(1);
  }

  if (m_config->verbose ())
    std::cerr << "Generating report" << std::endl;
  
  int keyId = m_config->keyId ();

  bool isMax = Counter::isMax(keyId);
  
  FlatInfo *first = FlatInfo::first();
  ASSERT (first);

  int64_t totals = first->CUM_KEY[0];
  int64_t totfreq = first->CUM_KEY[1];

  if (totals < 0) {
    std::cerr << "There is something weird going on for " << first->name() << " in " << first->filename() << "\n"
              << "Cumulative counts is negative: " << totals << "!!!\n" << std::endl;
    exit(1);
  }
  ASSERT(totfreq >= 0);
  
  typedef std::vector <MainGProfRow *> CumulativeSortedTable;
  typedef CumulativeSortedTable FinalTable;
  typedef std::vector <MainGProfRow *> SelfSortedTable;
  
  FinalTable table;
 
  for (FlatVector::const_iterator i = sorted.begin ();
     i != sorted.end ();
     i++)
  {
    FlatInfo *info = *i;
    if (!info->CUM_KEY[0])
      continue;

    // Sort calling and called functions.
    // FIXME: should sort callee and callers
    GProfMainRowBuilder builder (info, totals);
   
    for (FlatInfo::Callers::const_iterator j = info->CALLERS.begin ();
       j != info->CALLERS.end ();
       j++)
    { builder.addCaller (*j); }

    for (FlatInfo::Calls::const_iterator j = info->CALLS.begin ();
       j != info->CALLS.end ();
       j++)
    { builder.addCallee (*j); }
    table.push_back (builder.build (isMax)); 
  }

  SelfSortedTable selfSortedTable;
  
  for (FinalTable::const_iterator i = table.begin ();
     i != table.end ();
     i++)
  {
    selfSortedTable.push_back (*i);
  }
  
  stable_sort (selfSortedTable.begin (), selfSortedTable.end (), SortRowBySelf ());
   
  if (m_config->outputType () == Configuration::TEXT)
  {
    bool showcalls = m_config->showCalls ();
    bool showpaths = m_config->showPaths ();
    bool showlibs = m_config->showLib ();
    std::cout << "Counter: " << m_config->key () << std::endl;
    bool isPerfTicks = m_config->key () == "PERF_TICKS";
    float tickPeriod = m_config->tickPeriod();

    int maxcnt=0;
    if (isPerfTicks && ! m_config->callgrind()) {
      maxcnt = max (8, max (thousands (static_cast<double>(totals) * tickPeriod, 0, 2).size (), 
                            thousands (static_cast<double>(totfreq) * tickPeriod, 0, 2).size ()));
    }
    else {
      maxcnt = max (8, max (thousands (totals).size (), 
                       thousands (totfreq).size ()));
    }
    int maxval = maxcnt + (isPerfTicks ? 1 : 0);

    std::string basefmt = isPerfTicks ? "%.2f" : "%s";
    FractionPrinter valfmt (maxval, maxval);
    FractionPrinter cntfmt (maxcnt, maxcnt);
    
    printHeader ("Flat profile (cumulative >= 1%)", "Total", 
           showpaths, showcalls, maxval, maxcnt);
    
    for (FinalTable::const_iterator i = table.begin ();
       i != table.end ();
       i++)
    {
      MainGProfRow &row = **i; 
      printf ("%7.1f  ", row.PCT);
      if (isPerfTicks && ! m_config->callgrind()) {
        printf ("%*s  ", maxval, thousands (static_cast<double>(row.CUM) * tickPeriod, 0, 2).c_str());
      } else {
        printf ("%*s  ", maxval, thousands (row.CUM).c_str ());
      }
      PrintIf p (maxcnt);
      p (showcalls, thousands (row.CUM_ALL[1]));
      p (showpaths, thousands (row.SELF_ALL[2]));
      printf ("%s [%d]", row.name(), row.rank());
      if (showlibs) { std::cout << row.filename(); }
      std::cout << "\n";
      if (row.PCT < 1.)
        break;
    }
    
    std::cout << "\n";
    
    printHeader ("Flat profile (self >= 0.01%)", "Self", 
           showpaths, showcalls, maxval, maxcnt);
    
    for (SelfSortedTable::const_iterator i = selfSortedTable.begin ();
      i != selfSortedTable.end ();
      i++)
    {
      MainGProfRow &row = **i;
      float pct = percent (row.SELF, totals);
        
      printf ("%7.2f  ", pct);
       
      if (isPerfTicks && ! m_config->callgrind()) {
        printf ("%*s  ", maxval, thousands (static_cast<double>(row.SELF) * tickPeriod, 0, 2).c_str());
      }
      else {
        printf ("%*s  ", maxval, thousands (row.SELF).c_str ());
      }
      PrintIf p (maxcnt);
      p (showcalls, thousands (row.SELF_ALL[1]));
      p (showpaths, thousands (row.SELF_ALL[2]));
      printf ("%s [%d]", row.name(), row.rank());
      if (showlibs) { std::cout << row.filename(); }
      std::cout << "\n";
      if (pct < 0.01)
        break;
    }
    std::cout << "\n\n" << std::string (70, '-') << "\n";
    std::cout << "Call tree profile (cumulative)\n";
    
    for (FinalTable::const_iterator i = table.begin ();
       i != table.end ();
       i++)
    {
      int64_t divlen = 34+3*maxval 
             + (showcalls ? 1 : 0)*(2*maxcnt+5) 
             + (showpaths ? 1 : 0)*(2*maxcnt+5);
      
      std::cout << "\n";
      for (int x = 0 ; x < ((1+divlen)/2); x++) {printf ("- "); }
      std::cout << std::endl;

      MainGProfRow &mainRow = **i;

      if ((mainRow.rank() % 10) == 1)
      { 
        printf ("%-8s", "Rank");
        printf ("%% total  ");
        (AlignedPrinter (maxval)) ("Self");
        valfmt ("Self", "Children");
        printf ("  ");
        if (showcalls) {cntfmt ("Calls", "Total"); printf ("  ");}
        
        if (showpaths) {cntfmt ("Paths", "Total"); printf ("  ");}
        printf ("Function\n");
      
      } 
      
      for (MainGProfRow::Callers::const_iterator c = mainRow.CALLERS.begin ();
         c != mainRow.CALLERS.end ();
         c++)
      {
        OtherGProfRow &row = **c;
        std::cout << std::string (8, ' ');
        printf ("%7.1f  ", row.PCT);
        ASSERT (maxval);
        std::cout << std::string (maxval, '.') << "  ";
        if (isPerfTicks && ! m_config->callgrind()) {
          valfmt (thousands (static_cast<double>(row.SELF_COUNTS) * tickPeriod, 0, 2), 
                  thousands (static_cast<double>(row.CHILDREN_COUNTS) * tickPeriod, 0, 2));
        } else {
          valfmt (thousands (row.SELF_COUNTS), thousands (row.CHILDREN_COUNTS));
        }
        
        printf ("  ");
        if (showcalls) 
        { 
          cntfmt (thousands (row.SELF_CALLS), 
              thousands (row.TOTAL_CALLS));
          printf ("  ");
        }
        if (showpaths)
        {
          cntfmt (thousands (row.SELF_PATHS), 
                    thousands (row.TOTAL_PATHS));
          printf ("  ");
        }
        printf ("  %s [%d]", row.name(), row.rank());
        if (showlibs) {std::cout << "  " << row.filename(); }
        std::cout << "\n";
      }
      
      char rankBuffer[256];
      sprintf (rankBuffer, "[%d]", mainRow.rank());
      printf ("%-8s", rankBuffer);
      printf ("%7.1f  ", mainRow.PCT);
      if (isPerfTicks && ! m_config->callgrind()) {
        (AlignedPrinter (maxval)) (thousands (static_cast<double>(mainRow.CUM) * tickPeriod, 0, 2));
        valfmt (thousands (static_cast<double>(mainRow.SELF) * tickPeriod, 0, 2),
                thousands (static_cast<double>(mainRow.KIDS) * tickPeriod, 0, 2));
      }
      else {
        (AlignedPrinter (maxval)) (thousands (mainRow.CUM));
        valfmt (thousands (mainRow.SELF), thousands (mainRow.KIDS));
      }
      printf ("  ");
      if (showcalls) 
      { (AlignedPrinter (maxcnt)) (thousands (mainRow.CUM_ALL[1]));
          (AlignedPrinter (maxcnt)) (""); printf (" "); }
      if (showpaths)
        { (AlignedPrinter (maxcnt)) (thousands (mainRow.SELF_ALL[2]));
          (AlignedPrinter (maxcnt)) (""); printf (" "); }
      
      std::cout << mainRow.name();
      
      if (showlibs) { std::cout << mainRow.filename(); }
      std::cout << "\n";
      
      for (MainGProfRow::Calls::const_iterator c = mainRow.CALLS.begin ();
         c != mainRow.CALLS.end ();
         c++)
      {
        OtherGProfRow &row = **c;
        std::cout << std::string (8, ' ');
        printf ("%7.1f  ", row.PCT);
        std::cout << std::string (maxval, '.') << "  ";
        
        if (isPerfTicks && ! m_config->callgrind()) {
          valfmt (thousands (static_cast<double>(row.SELF_COUNTS) * tickPeriod, 0, 2),
                  thousands (static_cast<double>(row.CHILDREN_COUNTS) * tickPeriod, 0, 2));
        } else {
          valfmt (thousands (row.SELF_COUNTS), thousands (row.CHILDREN_COUNTS));
        }
        
        printf ("  ");
        
        if (showcalls) 
        { 
          cntfmt (thousands (row.SELF_CALLS), 
                  thousands (row.TOTAL_CALLS)); 
          printf ("  ");
        }
        if (showpaths)
        {
          cntfmt (thousands (row.SELF_PATHS), 
                    thousands (row.TOTAL_PATHS));
          printf ("  ");
        }
        printf ("  %s [%d]", row.name(), row.rank());
    
        if (showlibs) {std::cout << "  " << row.filename(); }
        std::cout << "\n";
      }
    }
  }
  else if (m_config->outputType () == Configuration::SQLITE)
  {
    std::cout << "DROP TABLE IF EXISTS files;\n"
                 "DROP TABLE IF EXISTS symbols;\n"
                 "DROP TABLE IF EXISTS mainrows;\n"
                 "DROP TABLE IF EXISTS children;\n"
                 "DROP TABLE IF EXISTS parents;\n"
                 "DROP TABLE IF EXISTS summary;\n\n"
                 "CREATE TABLE summary (\n"
                 "counter TEXT,\n"
                 "total_count INTEGER,\n"
                 "total_freq INTEGER,\n"
                 "tick_period REAL\n"
                 ");\n\n"
                 "CREATE TABLE files (\n"
                 "id INTEGER PRIMARY KEY,\n"
                 "name TEXT\n"
                 ");\n\n"
                 "CREATE TABLE symbols (\n" 
                 "id INTEGER PRIMARY KEY,\n"
                 "name TEXT,\n"
                 "filename_id INT CONSTRAINT file_id_exists REFERENCES files(id)\n"
                 ");\n\n"
                 "CREATE TABLE mainrows (\n"
                 "id INTEGER PRIMARY KEY,\n"
                 "symbol_id INT CONSTRAINT symbol_id_exists REFERENCES symbol(id),\n"
                 "self_count INTEGER,\n"
                 "cumulative_count INTEGER,\n"
                 "kids INTEGER,\n"
                 "self_calls INTEGER,\n"
                 "total_calls INTEGER,\n"
                 "self_paths INTEGER,\n"
                 "total_paths INTEGER\n"
                 ");\n\n"
                 "CREATE TABLE children (\n"
                 "self_id INT CONSTRAINT self_exist REFERENCES mainrows(id),\n"
                 "parent_id INT CONSTRAINT parent_exists REFERENCES mainrows(id),\n"
                 "from_parent_count INTEGER,\n" 
                 "from_parent_calls INTEGER,\n"
                 "from_parent_paths INTEGER\n"
                 ");\n\n"
                 "CREATE TABLE parents (\n"
                 "self_id INT CONSTRAINT self_exists REFERENCES mainrows(id),\n"
                 "child_id INT CONSTRAINT child_exists REFERENCES mainrows(id),\n"
                 "to_child_count INTEGER,\n"
                 "to_child_calls INTEGER,\n"
                 "to_child_paths INTEGER\n"
                 ");\n\n\nBEGIN EXCLUSIVE TRANSACTION;\n"
                 "INSERT INTO summary (counter, total_count, total_freq, tick_period) VALUES(\""
                 << m_config->key () << "\", " << totals << ", " << totfreq << ", " << m_config->tickPeriod() << ");\n\n";
                 
    for (FinalTable::const_iterator i = table.begin ();
       i != table.end ();
       i++)
    {
      MainGProfRow &mainRow = **i;
      std::cout << "INSERT OR IGNORE INTO files (id, name) VALUES (" 
                << mainRow.fileId() << ", \"" << mainRow.filename() << "\");\n"
                "INSERT OR IGNORE INTO symbols (id, name, filename_id) VALUES (" 
                << mainRow.symbolId() << ", \"" << mainRow.name() << "\", " 
                << mainRow.fileId() << ");\n"
                "INSERT INTO mainrows (id, symbol_id, self_count, cumulative_count, kids, self_calls, total_calls, self_paths, total_paths) VALUES ("
                << mainRow.rank() << ", " << mainRow.symbolId() << ", " 
                << mainRow.SELF << ", " << mainRow.CUM << ", " << mainRow.KIDS << ", " 
                << mainRow.SELF_ALL[1] << ", " << mainRow.CUM_ALL[1] << ", " 
                << mainRow.SELF_ALL[2] << ", " << mainRow.CUM_ALL[2] << ");\n";

      for (MainGProfRow::Callers::const_iterator c = mainRow.CALLERS.begin ();
           c != mainRow.CALLERS.end ();
          c++)
      {
        OtherGProfRow &row = **c;
        std::cout << "INSERT INTO parents (self_id, child_id, to_child_count, to_child_calls, to_child_paths) VALUES ("
                  << row.rank() << ", " << mainRow.rank() << ", "
                  << row.SELF_COUNTS << ", " << row.SELF_CALLS << ", " << row.SELF_PATHS << ");\n";
      }

      for (MainGProfRow::Calls::const_iterator c = mainRow.CALLS.begin ();
           c != mainRow.CALLS.end ();
           c++)
      {
        OtherGProfRow &row = **c;
        std::cout << "INSERT INTO children (self_id, parent_id, from_parent_count, from_parent_calls, from_parent_paths) VALUES ("
                  << row.rank() << ", " << mainRow.rank() << ", "
                  << row.SELF_COUNTS << ", "<< row.SELF_CALLS << ", " << row.SELF_PATHS << ");\n";

      }
    }
    std::cout << "END TRANSACTION;\n" << std::endl;
  }
  else
  {
    ASSERT (false);
  }
}




void
IgProfAnalyzerApplication::callgrind (ProfileInfo &prof)
{
  ASSERT(false);
}



void 
IgProfAnalyzerApplication::run (void)
{
  ArgsList args; 
  for (int i = 0; i < m_argc; i++) 
  {
    args.push_back (m_argv[i]);
  }
  ArgsList::const_iterator firstFile = this->parseArgs (args);
  m_config->addFilter(new RemoveIgProfFilter());
  ASSERT (firstFile != args.end ());
  ArgsLeftCounter left (args.end ());
  ASSERT (left (firstFile));
  ArgsList::const_iterator lastFile = args.end ();
  ProfileInfo *prof = readAllDumps (firstFile, lastFile);

  if (!Counter::countersByName ().size ())
  { 
    std::cerr << "No counter values in profile data." << std::endl; 
    exit (1);
  }
  
  if (m_config->key () == "")
  {
    if (Counter::countersByName ().size () > 1)
    {
      printAvailableCounters (Counter::countersByName ());
    }
    else
    {
      m_config->setKey ((*(Counter::countersByName ().begin ())).first);
    }
  }
  if (! m_config->isShowCallsDefined ())
  {
    if (lat::StringOps::contains (m_config->key (), "MEM_"))
      { m_config->setShowCalls (true); }
    else
      { m_config->setShowCalls (false); }
  }
  if (m_config->callgrind ())
    { callgrind (*prof); }
  else
    { analyse (*prof); }
}


IgProfAnalyzerApplication::ArgsList::const_iterator
IgProfAnalyzerApplication::parseArgs (const ArgsList &args)
{
  
  ArgsList::const_iterator arg = args.begin ();

  while (++arg != args.end ())
  {
    NameChecker is (*arg);
    ArgsLeftCounter left (args.end ());
    if (is("--help"))
    { usage (); exit (1); }
    else if (is ("--verbose", "-v"))
    { m_config->setVerbose (true); }
    else if (is ("--report", "-r") && left (arg))
    {
      std::string key = *(++arg);
      m_config->setKey(key);
      if (lat::StringOps::find (key, "MEM_") != -1)
        m_config->addFilter(new MallocFilter());
      if (key == "MEM_LIVE")
        m_config->addFilter(new IgProfGccPoolAllocFilter());
    }
    else if (is ("--value") && left (arg) > 1)
    {
      std::string type = *(++arg);
      if (type == "peak") {m_config->setNormalValue (false);}
      else if (type == "normal") {m_config->setNormalValue (true);}
      else {
        std::cerr << "Unexpected --value argument " << type << std::endl;
        exit(1);
      }
    }
    else if (is ("--merge-libraries", "-ml"))
    {
      m_config->setMergeLibraries (true);
    }
    else if (is ("--order", "-o"))
    {
      std::string order = *(arg++);
      if (order == "ascending") {m_config->setOrdering(Configuration::ASCENDING);}
      else if (order == "descending") {m_config->setOrdering(Configuration::DESCENDING);}
      else {
        std::cerr << "Unexpected --order / -o argument " << order << std::endl;
        exit(1);
      }
      // TESTME: Implement --order option. (e201134) 
      // { $order = $ARGV[1] eq 'ascending' ? -1 : 1; shift (@ARGV); shift (@ARGV); }
    }
    else if (is ("--filter-file", "-F"))
    {
      std::cerr << "Option " << *arg << " is not supported for the moment." << std::endl;
      exit(1);
      // TODO: Implement --filter-file.
      // { push (@filterfiles, $ARGV[1]); shift (@ARGV); shift (@ARGV); }
    }
    else if (is ("--filter", "-f"))
    {
      std::cerr << "Option " << *arg << " is not supported for the moment." << std::endl;
      exit(1); 
      // TODO: Implement user filters. (84dd354) 
      // push (@userfilters, split(/,/, $ARGV[1])); shift (@ARGV); shift (@ARGV); }
    }
    else if (is ("--no-filter", "-nf"))
    {
      // TODO: Implement the --no-filter option. (4572c80) 
      //{ @filters = (); shift (@ARGV); }     
      m_config->disableFilters();
    }
    else if (is ("--list-filters", "-lf"))
    {
      ASSERT (false);
      // TODO: Implement external filters. (e3b0572) 
      // my %filters = map { s/igprof_filter_(.*)_(pre|post)/$1/g; $_ => 1}
      //        grep(/^igprof_filter_.*_(pre|post)$/, keys %{::});
      // print "Available filters are: @{[sort keys %filters]}\n";
      // print "Selected filters are: @filters @userfilters\n";
      // exit(0);
    }
    else if (is ("--libs", "-l"))
    {
      m_config->setShowLib(true);
    }
    else if (is ("--callgrind", "-C"))
    {
      ASSERT (false);
      // TODO: implement callgrind output (7e2618e)
      // { $callgrind = 1; shift (@ARGV); }
    }
    else if (is ("--text", "-t"))
    { m_config->setOutputType (Configuration::TEXT); }
    else if (is ("--sqlite", "-s"))
    { m_config->setOutputType (Configuration::SQLITE); }
    else if (is ("--demangle", "-d"))
    { m_config->setDoDemangle (true); }
    else if (is ("--gdb", "-g"))
    { m_config->setUseGdb (true); }
    else if (is ("--paths", "-p"))
    {
      m_config->setShowPaths (true);
    }
    else if (is ("--calls", "-c"))
    {
      m_config->setShowCalls (true);
    }
    else if (is ("--merge-regexp", "-mr") && left (arg))
    {
      std::string re = *(++arg);
      const char *regexpOption = re.c_str();
      RegexpFilter *filter = new RegexpFilter;

      while (*regexpOption)
      {
        if (*regexpOption++ != 's')
        {
          std::cerr << "Error in regular expression: " << regexpOption << std::endl;
          exit (1);
        }
        char separator[] = {0, 0};
        separator[0] = *regexpOption++;
        int reSize = strcspn (regexpOption, separator);
        std::string re (regexpOption, reSize);
        regexpOption += reSize;
        if (*regexpOption++ != separator[0])
        {
          std::cerr << "Error in regular expression: " << regexpOption << std::endl;
          exit (1);
        }
        int withSize = strcspn (regexpOption, separator);
        std::string with (regexpOption, withSize);
        regexpOption += withSize;

        if (*regexpOption++ != separator[0])
        {
          std::cerr << "Error in regular expression: " << regexpOption << std::endl;
          exit (1);
        }
        if (*regexpOption && *regexpOption++ != ';')
        {
          std::cerr << "Error in regular expression: " << regexpOption << std::endl;
        }
        filter->addSubstitution(new lat::Regexp (re), with);
      }

      m_config->setRegexpFilter (filter);
    }
    else if (is ("--"))
    {
      ASSERT (false);
      return ++arg;
    }
    else if ((*arg)[0] == '-')
    {
      std::cerr << "Unknown option " << (*arg) << std::endl;
      usage (); exit (1);
    }
    else
    {
      return arg;
    };
  }
  std::cerr << "ERROR: No input files specified" << std::endl;
  exit (1);
}

void 
userAborted(int)
{
  std::cerr << "\nUser interrupted." << std::endl;
  exit(1);
}

int 
main (int argc, const char **argv)
{
  lat::Signal::handleFatal (argv [0]);
  signal (SIGINT, userAborted);
  try 
  {
    IgProfAnalyzerApplication *app = new IgProfAnalyzerApplication (argc, argv);
    app->run ();
  } 
  catch (lat::Error &e) {

    std::cerr << "Internal error \"" << e.explain() << "\".\n"
                 "Oh my, you have found a bug in igprof-analyse!\n"
                 "Please file a bug report and some mean to reproduce it to:\n\n"
                 "  https://savannah.cern.ch/bugs/?group=cmssw\n\n" << std::endl;
  }
  catch (std::exception &e) {
    std::cerr << "Internal error: \""
              << e.what() << "\".\n" 
                 "\nOh my, you have found a bug in igprof-analyse!\n"
                 "Please file a bug report and some mean to reproduce it to:\n\n"
                 "  https://savannah.cern.ch/bugs/?group=cmssw\n\n" << std::endl;
  }
  catch (...) 
  {
    std::cerr << "Internal error.\n"
                 "Oh my, you have found a bug in igprof-analyse!\n"
                 "Please file a bug report and some mean to reproduce it to:\n\n"
                 "  https://savannah.cern.ch/bugs/?group=cmssw\n\n" << std::endl;
  }
}
