#ifndef IG_PROF_ANALYZE
#define IG_PROF_ANALYZE

#include <classlib/iotools/InputStream.h>
#include <classlib/iotools/StorageInputStream.h>
#include <classlib/iotools/BufferInputStream.h>
#include <classlib/iobase/File.h>
#include <classlib/iobase/FileError.h>
#include <classlib/iobase/Filename.h>
#include <classlib/utils/DebugAids.h>
#include <classlib/utils/StringOps.h>
#include <classlib/utils/Regexp.h>
#include <classlib/utils/RegexpMatch.h>
#include <classlib/utils/Argz.h>
#include <classlib/iobase/Pipe.h>
#include <classlib/iotools/IOChannelInputStream.h>
#include <classlib/iotools/InputStream.h>
#include <classlib/iotools/InputStreamBuf.h>
#include <classlib/zip/GZIPInputStream.h>
#include <classlib/zip/BZIPInputStream.h>
#include <classlib/zip/ZipInputStream.h>
#include <classlib/iobase/SubProcess.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <string>
#include <list>
#include <iostream>
#include <cmath>
#include <sys/stat.h>
#include <map>
#include <stdint.h>

bool isWhitespace(char c)
{
  //^ \t\n\r\f\v
  if (c == ' '
      || c == '\t'
      || c == '\n'
      || c == '\r'
      || c == '\f'
      || c == '\v') return true;
  return false;
}

class IntConverter
{
public:
  IntConverter (const std::string &string, lat::RegexpMatch *match)
  :m_string (string.c_str ()),
   m_match(match) {}

  IntConverter (const char *string, lat::RegexpMatch *match)
  :m_string (string),
   m_match (match) {}
  
  int64_t operator() (int position, int base=10)
  {
    return strtoll (m_string + m_match->matchPos (position), 0, base);
  }
private:
  const char *m_string;
  lat::RegexpMatch *m_match;
};


class Counter
{
public:
  typedef int64_t StorageType;

  Counter (int type, StorageType counts=0,  StorageType freqs=0)
  : m_type (type),
    m_next (this),
    m_counts (counts),
    m_freqs (freqs),
    m_cumulativeCounts (0),
    m_cumulativeFreqs (0)
  { 
  }
  
  Counter (const std::string &counterName, StorageType counts=0, StorageType freqs=0)
  {
    Counter::Counter (getIdForCounterName (counterName), counts, freqs);
  }
  
  void printDebugInfo (void)
  {
    std::cerr << "Id: " << m_type
          << " Counts: " << m_counts
          << " Freqs: " << m_freqs 
          << " Cumulative Counts: " << m_cumulativeCounts 
          << " Cumulative Freqs: " << m_cumulativeFreqs << std::endl;
  }
  
  Counter *next (void) {return m_next; }
  void setNext (Counter *next) { m_next = next; }

  int id (void) {return m_type; }
  
  bool isMax (void) { return Counter::isMax (m_type); }
  
  void add (Counter *counter)
  {
    counter->setNext (this->next ());
    this->setNext (counter);
  }

  // TODO: Create a "RingManipulator" rather than having all this static stuff
  //     in the Counter class????
  static int getIdForCounterName (const std::string &name) 
  {
    IdCache::const_iterator i = countersByName ().find (name);    
    if (i == countersByName ().end ())
      return -1;
    return i->second; 
  }

  static void addNameToIdMapping (const std::string &name, int id, bool isTick)
  {
    ASSERT (id < 31);
    ASSERT ((s_isMaxMask & (1 << id)) == 0);
    ASSERT (countersByName ().find (name) == countersByName ().end ());
    
    if (name.find("_MAX") != std::string::npos)
    { s_isMaxMask |= 1 << id; }
    countersByName ().insert (Counter::IdCache::value_type (name, id));
    if (isTick)
    { s_ticksCounterId = id; }
    if (s_keyName == name)
    { s_keyValue = id; }
  }

  static int isMax (const std::string &name)
  {
    IdCache::const_iterator i = countersByName ().find (name);
    ASSERT (i != countersByName ().end ());
    return isMax (i->second);
  }

  static bool isMax (int id)
  {
    ASSERT ((id < 32) && (id >= 0));
    return (s_isMaxMask & (1 << id));   
  }

  static Counter* getCounterInRing (Counter *&initialCounter, int id)
  {
    return Counter::popCounterFromRing (initialCounter, id, false);
  }

  static int ringSize (Counter *initialCounter)
  {
    Counter *i = initialCounter;
    if (! i)
    { return 0; }
    
    int size = 1;
    while (i->next () != initialCounter)
    {
      i = i->m_next;
      size++;
    };
    return size;
  }


  static Counter *addCounterToRing (Counter *&initialCounter, int id)
  {
    Counter *counter = Counter::getCounterInRing (initialCounter, id);
    if (counter) { return counter; }
    counter = new Counter (id);
    if (! initialCounter)
    { initialCounter = counter; }
    else
    { initialCounter->add (counter); }
    return counter;
  }

  static Counter *popCounterFromRing (Counter *&initialCounter, int id=-1, bool pop=true)
  {
    if (!initialCounter)
      return 0;
      
    Counter *i = initialCounter->next ();
    Counter *prev = initialCounter;
    
    while (i)
    {
      ASSERT (i);
      ASSERT (i->next ());
      if (id == -1 || (i->id () == id))
      {
        if (pop && (i == i->next ())) { initialCounter = 0; }
        else if (pop && prev) { 
          prev->setNext (i->next ()); 
          if (i == initialCounter) initialCounter = prev;
        } 
        return i;
      }
      prev = i;
      i = i->next ();
      if (i == initialCounter->next ())
        return 0;
    }
    return 0;
  }

  static bool isKey (int id)
  {
    return s_keyValue == id;
  }
  
  static void setKeyName (const std::string& name)
  { s_keyName = name; }

  typedef std::map<std::string, int> IdCache;

  static IdCache &countersByName (void)
  { 
    static IdCache s_countersByName;
    return s_countersByName;
  }

  StorageType &cnt() { return m_counts; }
  StorageType &freq() { return m_freqs; }
  StorageType &ccnt() { return m_cumulativeCounts; }
  StorageType &cfreq() { return m_cumulativeFreqs; }
private:
  static IdCache s_countersByName;
  static int s_lastAdded;
  static int s_isMaxMask;
  static int s_ticksCounterId;
  static int s_keyValue;
  static std::string s_keyName;
  int m_type;
  Counter *m_next;
  StorageType m_counts;
  StorageType m_freqs;
  StorageType m_cumulativeCounts;
  StorageType m_cumulativeFreqs;
};

int Counter::s_isMaxMask = 0;
int Counter::s_ticksCounterId = -1;
int Counter::s_keyValue = -1;
std::string Counter::s_keyName;

class PipeReader
{
public:
  PipeReader (const std::string &args, lat::IOChannel *source=0)
    : m_argz(args),
      m_is(0),
      m_isbuf(0),
      m_cmd(0)
  {
    static lat::File s_stderr(lat::Filename::null(), lat::IOFlags::OpenRead | lat::IOFlags::OpenWrite);
    lat::IOChannel *nil = 0;

    if (!source)
    {   
      m_cmd = new lat::SubProcess(m_argz.argz(), 
                                lat::SubProcess::One | lat::SubProcess::Read | lat::SubProcess::NoCloseError,
                                &m_pipe, nil, &s_stderr);
    }
    else
    { 
      m_cmd = new lat::SubProcess(m_argz.argz(), 
                                lat::SubProcess::One | lat::SubProcess::Read | lat::SubProcess::NoCloseError,
                                source, m_pipe.sink (), &s_stderr);
    }
    m_is = new lat::IOChannelInputStream(m_pipe.source ());
    m_isbuf = new lat::InputStreamBuf(m_is);
    m_istd  = new std::istream (m_isbuf);    
  }
  
  ~PipeReader (void)
  {
    if (m_pipe.source()) {
      m_pipe.source()->close();
    }
    delete m_isbuf;
    delete m_istd;
    delete m_is;
    delete m_cmd;
 }
  
  std::istream &output (void)
  {
    return *m_istd;
  }
private:
  lat::Argz m_argz;
  lat::Pipe m_pipe;
  std::istream *m_istd;
  lat::IOChannelInputStream *m_is;
  lat::InputStreamBuf *m_isbuf;
  lat::SubProcess *m_cmd;
};

bool skipWhitespaces(const char *srcbuffer, const char **destbuffer)
{
  if (!isWhitespace(*srcbuffer++))
    return false;
  while (isWhitespace(*srcbuffer))
    { srcbuffer++; }
  *destbuffer = srcbuffer;
  return true;
}

bool skipString(const char *strptr, const char *srcbuffer, const char **dstbuffer)
{
  // Skips strings of the form '\\s+strptr\\s+' starting from buffer.
  // Returns a pointer to the first char which does not match the above regexp,
  // or 0 in case the regexp is not matched.
  if(strncmp(srcbuffer, strptr, strlen(strptr))) return false;
  *dstbuffer = srcbuffer + strlen(strptr);
  return true;
}

class FileInfo
{
public:
  typedef int Offset;
private:
  struct CacheItem {
    CacheItem (Offset offset, const std::string &name)
    :OFFSET(offset), NAME(name) {};
    Offset OFFSET;
    std::string NAME;
  };

  typedef std::vector<CacheItem> SymbolCache;
  
  struct CacheItemComparator {
    bool operator()(const CacheItem& a, 
                    const int &b) const
    { return a.OFFSET < b; }
      
    bool operator()(const int& a, 
                   const CacheItem &b) const
    { return a < b.OFFSET; }    
  };
public:
  std::string NAME;
  FileInfo (void) : NAME ("<dynamically generated>") {}
  FileInfo (const std::string &name, bool useGdb)
  : NAME (name)
  {
    if (useGdb)
    {
      ASSERT(lat::Filename(name).isDirectory() == false);
      this->createOffsetMap();
    }
  }

  const char *symbolByOffset(Offset offset)
  {
    if (m_symbolCache.empty())
    { return 0; }

    SymbolCache::iterator i = lower_bound(m_symbolCache.begin(),
                                          m_symbolCache.end(),
                                          offset, CacheItemComparator());
    if (i->OFFSET == offset)
      return i->NAME.c_str();
    
    if (i == m_symbolCache.begin())
    { return m_symbolCache.begin()->NAME.c_str(); }

    --i;

    return i->NAME.c_str(); 
  }

  Offset next(Offset offset)
  {
    SymbolCache::iterator i = upper_bound(m_symbolCache.begin(),
                                          m_symbolCache.end(),
                                          offset, CacheItemComparator());
    if (i == m_symbolCache.end())
      return 0;
    return i->OFFSET;      
  }
private:
  void createOffsetMap (void)
  {
    // FIXME: On macosx we should really use otool
    #ifndef __APPLE__
    std::string commandLine = "objdump -p " + std::string(NAME);
    PipeReader objdump(commandLine);
    
    std::string oldname;
    std::string suffix;
    int vmbase = 0;
    bool matched = false;

    while (objdump.output())
    {
      // Checks the following regexp
      //    
      //    LOAD\\s+off\\s+(0x[0-9A-Fa-f]+)\\s+vaddr\\s+(0x[0-9A-Fa-f]+)
      // 
      // and sets vmbase to be $2 - $1 of the first matched entry.
      
      std::string line;
      std::getline(objdump.output(), line);
    
      if (!objdump.output()) break;
      if (line.empty()) continue;
      
      const char *lineptr = line.c_str();
      if (!skipWhitespaces(lineptr, &lineptr)) continue;
      if (!skipString("LOAD", lineptr, &lineptr)) continue;
      if (!skipWhitespaces(lineptr, &lineptr)) continue;
      if (!skipString("off", lineptr, &lineptr)) continue;
      char *endptr = 0;
      int initialBase = strtol(lineptr, &endptr, 16);
      if (lineptr == endptr) continue;
      lineptr = endptr;
      if (!skipWhitespaces(lineptr, &lineptr)) continue;
      if (!skipString("vaddr", lineptr, &lineptr)) continue;
      if (!skipWhitespaces(lineptr, &lineptr)) continue;
      int finalBase = strtol(lineptr, &endptr, 16);
      if (lineptr == endptr) continue;
      vmbase=finalBase - initialBase;
      matched = true;
      break;
    }
    
    if (!matched)
    {
      std::cerr << "Cannot determine VM base address for " 
                << NAME << std::endl;
      std::cerr << "Error while running `objdump -p " + std::string(NAME) + "`" << std::endl;
      exit(1);
    }
    
    PipeReader nm("nm -t d -n " + std::string(NAME));
    while (nm.output())
    {
      std::string line;
      std::getline(nm.output (), line);
    
      if (!nm.output()) break;
      if (line.empty()) continue;
      // If line does not match "^(\\d+) \\S (\S+)$", exit.
      const char *begin = line.c_str();
      char *endptr = 0;
      int address = strtol(begin, &endptr, 10);
      if (endptr == begin) { continue; }

      if (*endptr++ != ' ') { continue; }

      if (isWhitespace(*endptr++)) { continue; }
      if (*endptr++ != ' ') { continue; }
      char *symbolName = endptr;
      
      while (*endptr && !isWhitespace(*endptr))
      {
        endptr++;
      }
      if (*endptr != 0)
        { continue; }
      // If line starts with '.' forget about it.
      if (symbolName[0] == '.')
        { continue; }
      // Create a new symbol with the given fileoffset.
      // The symbol is automatically saved in the FileInfo cache by offset.
      // If a symbol with the same offset is already there, the new one 
      // replaces the old one.
      int offset = address-vmbase;
      if (m_symbolCache.size() && (m_symbolCache.back().OFFSET == offset))
      {
        m_symbolCache.back().NAME = symbolName;
      }
      else
      {
        m_symbolCache.push_back(CacheItem(address-vmbase, symbolName));
      }
    }
    #endif /* __APPLE__ */
  }

  SymbolCache m_symbolCache;
};



class NameChecker
{
public:
  NameChecker (const std::string& arg) : m_arg (arg) {};
  bool operator() (const char *fullname) {return m_arg == fullname; }
  bool operator() (const char *fullname, const char *abbr)
  { return (m_arg == fullname) || (m_arg == abbr); }
private:
  const std::string m_arg; 
};

class ArgsLeftCounter
{
public:
  typedef std::list<std::string> ArgsList;
  ArgsLeftCounter (const ArgsList::const_iterator& end) : m_end (end) {};
  int operator () (ArgsList::const_iterator arg)
  {
    int size = 0;
    while (arg++ != m_end) { size++; }
    return size;
  }
private:
  const ArgsList::const_iterator m_end;
};


class FileOpener 
{
public:
  static const int INITIAL_BUFFER_SIZE=40000000;
  FileOpener (void)
    : m_buffer (new char[INITIAL_BUFFER_SIZE]),
      m_posInBuffer (INITIAL_BUFFER_SIZE),
      m_lastInBuffer (INITIAL_BUFFER_SIZE),
      m_eof (false),
      m_bufferSize (INITIAL_BUFFER_SIZE)
  {
  }
  
  virtual ~FileOpener (void)
  {
    for (Streams::reverse_iterator i = m_streams.rbegin ();
       i != m_streams.rend ();
       i++)
    {
      delete *i;
    }
    free(m_buffer);
  }
  
  lat::InputStream &stream (void)
  {
    return *m_streams.back ();
  }

  void addStream (lat::InputStream *stream)
  {
    m_streams.push_back (stream);
  }

  void resizeBuffer (int size)
  {
    char * buffer = new char[size];
    memmove (buffer, m_buffer, m_bufferSize);
    delete[] m_buffer;
    m_buffer = buffer;
    m_bufferSize = size;
  } 
 
  void readLine ()
  {
    int beginInBuffer = m_posInBuffer;

    while (m_posInBuffer < m_lastInBuffer)
    {
      if (m_buffer[m_posInBuffer++] == '\n')
      {
        m_curString = m_buffer + beginInBuffer;
        m_curStringSize = m_posInBuffer-beginInBuffer-1;
        return;
      }
    }
    int remainingsSize = m_lastInBuffer-beginInBuffer;
    ASSERT (remainingsSize <= m_bufferSize);

    if (remainingsSize == m_bufferSize)
    {
      resizeBuffer (remainingsSize * 2);
    }

    if (remainingsSize)
    {
      memmove (m_buffer, m_buffer + beginInBuffer, remainingsSize);
    }
 
    int readSize = this->stream ().read (m_buffer + remainingsSize, m_bufferSize-remainingsSize);

    if (!readSize)
    { 
      m_eof = true;
      m_curString = m_buffer;
      m_curStringSize = remainingsSize;
      return;
    }

    m_posInBuffer = 0;
    m_lastInBuffer = remainingsSize + readSize;
    return this->readLine ();
  }
  
  void assignLineToString (std::string &str)
  {
    str.assign (m_curString, m_curStringSize);
  }
  
  bool eof (void) {return m_eof;}
private:
  typedef std::list<lat::InputStream *> Streams; 
  Streams m_streams;
  char *m_buffer;
  int m_posInBuffer;
  int m_lastInBuffer;
  int m_eof;
  const char *m_curString;
  int m_curStringSize;
  int m_bufferSize;
};

class FileReader : public FileOpener
{
public:
  FileReader (const std::string &filename)
  : FileOpener (),
    m_file (openFile (filename))
  { 
    ASSERT (m_file);
    lat::StorageInputStream *storageInput = new lat::StorageInputStream (m_file);
    lat::BufferInputStream *bufferStream = new lat::BufferInputStream (storageInput);
    addStream (storageInput);
    addStream (bufferStream);

    FILE *f = fopen(filename.c_str(), "r"); 
    fread(m_fileHeader, 4, 1, f);
    fclose(f);
    
    if (m_fileHeader[0] == 0x1f 
        && m_fileHeader[1] == 0x8b) 
    { addStream (new lat::GZIPInputStream(bufferStream)); } 
    else if (m_fileHeader[3] == 0x04 
             && m_fileHeader[2] == 0x03
             && m_fileHeader[1] == 0x4b
             && m_fileHeader[0] == 0x50) 
    { addStream (new lat::ZipInputStream(bufferStream)); } 
    else if (m_fileHeader[0] == 'B' 
             && m_fileHeader[1] == 'Z' 
             && m_fileHeader[2] == 'h') 
    { addStream (new lat::BZIPInputStream(bufferStream)); }
  }
  ~FileReader (void)
  {
    m_file->close ();
  }
private:
    static lat::File *openFile (const std::string &filename)
    {
        try 
        {
            lat::File *file = new lat::File (filename);
            return file;
        }
        catch (lat::FileError &e)
        {
            std::cerr << "ERROR: Unable to open file " << filename << " for input." 
                      << std::endl;
            exit (1);
        }
    }
  lat::File *m_file;
  unsigned char m_fileHeader[4];
};

class PathCollection
{
public:
  typedef lat::StringList Paths;
  typedef Paths::const_iterator Iterator;
  PathCollection (const char *variableName)
  {
    char *value = getenv (variableName);
    if (!value)
      return;
    m_paths = lat::StringOps::split (value, ':', lat::StringOps::TrimEmpty);
  }
  
  std::string which (const std::string &name)
  {
    for (Iterator s = m_paths.begin ();
       s != m_paths.end ();
       s++)
    {
      lat::Filename filename (*s, name);
      if (filename.exists ())
        return std::string (filename);
    }
    return "";
  }
  
  Iterator begin (void) {return m_paths.begin ();}
  Iterator end (void) {return m_paths.end ();}  
private:
  Paths m_paths;
};

std::string 
thousands (int64_t value, int leftPadding=0)
{
  // Converts an integer value to a string
  // adding `'` to separate thousands and possibly
  ASSERT (value >= 0);
  ASSERT (leftPadding >= 0);
  int64_t n = 1; int digitCount = 0;
  std::string result = "";
  if (!value)
    result = "0";
    
  char d[2];
  d[1] = 0;
   
  while ((value / n))
  {
    int digit = (value / n) % 10;
    ASSERT (digit < 10);
    if ((! digitCount) && (n != 1))
    { result = "'" + result; }
    d[0] = static_cast<char> ('0'+ static_cast<char> (digit));
    result = std::string (d) + result;
    n *= 10;
    digitCount = (digitCount + 1) % 3;
  }
  
  if (leftPadding)
  {
    ASSERT (leftPadding-digitCount > 0);
    result = std::string ("", leftPadding-digitCount) + result;
  }
  return result;
}

std::string
thousands (double value, int leftPadding, int decimalPositions)
{
  value = round (value * 100.) / 100.;
  int padding = leftPadding-decimalPositions;
  std::string result = thousands(int64_t(value), padding > 0 ? padding : 0);
  ASSERT(decimalPositions < 63);
  char buffer[64];
  double decimal = value-int64_t(value);
  sprintf(buffer+1, "%.2f", decimal);
  buffer[decimalPositions+3] = 0;
  return result + &buffer[2];
}


std::string
toString (int64_t value)
{
  // FIXME: not thread safe... Do we actually care? Probably not.
  static char buffer [1024];
  sprintf (buffer,"%lld",value);
  return buffer;
}


class AlignedPrinter
{
public:
  AlignedPrinter (size_t size)
  :m_size (size)
  {
  }
    
  void operator ()(const std::string &n)
  {
    printf ("%*s", m_size, n.c_str ());
    printf ("  ");
    fflush (stdout);
  }
private:
  size_t m_size;
};

class FractionPrinter
{
public:
  FractionPrinter (size_t sizeN, size_t sizeD)
  :m_sizeN (sizeN), m_sizeD (sizeD) 
  {
  }
  
  void operator ()(const std::string &n, const std::string &d)
  {
    printf ("%*s", m_sizeN, n.c_str ());
    char denBuffer[256];
    sprintf (denBuffer, " / %%-%ds", m_sizeD);
    printf (denBuffer, d.c_str ());
  }
  
private:
  size_t m_sizeN;
  size_t m_sizeD;
};

class PrintIf
{
public:
  PrintIf (int size)
  :m_size (size)
  {}
  
  void operator ()(bool condition, const std::string &value)
  {
    if (condition)
    { printf ("%*s  ", m_size, value.c_str ()); }
  }
private:
  int m_size;
};

class SymbolFilter
{
public:
  SymbolFilter &operator= (const char *symbolName)
  { return this->addFilteredSymbol (symbolName); }
  
  SymbolFilter &operator, (const char *symbolName)
  { return this->addFilteredSymbol (symbolName); }

  SymbolFilter &addFilteredSymbol (const char *symbolName)
  {
    m_symbols.push_back (symbolName); return *this; }
  
  bool contains (const std::string &name)
  {
    bool result = std::find (m_symbols.begin (), m_symbols.end (), name) != m_symbols.end ();
    return result; 
  }
  
private:
  typedef std::list<std::string> SymbolNames; 
  SymbolNames m_symbols;
};

#endif
