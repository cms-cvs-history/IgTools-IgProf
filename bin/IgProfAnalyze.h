#ifndef IG_PROF_ANALYZE
#define IG_PROF_ANALYZE

#include <classlib/iotools/InputStream.h>
#include <classlib/iotools/StorageInputStream.h>
#include <classlib/iotools/BufferInputStream.h>
#include <classlib/iobase/File.h>
#include <classlib/iobase/Filename.h>
#include <classlib/utils/DebugAids.h>
#include <classlib/utils/StringOps.h>
#include <classlib/utils/Regexp.h>
#include <classlib/utils/RegexpMatch.h>
#include <string>
#include <list>
#include <iostream>
#include <sys/stat.h>
#include <map>

void pickUpMax (int &old, int candidate)
{
	if (candidate > old)
		old = candidate;
}

class Counter
{
public:
	typedef int StorageType;

	Counter (int type, int counts=0, int freqs=0)
	: m_next (this),
	  m_counts (counts),
	  m_freqs (freqs),
	  m_cumulativeCounts (0),
	  m_cumulativeFreqs (0),
	  m_type (type)
	{ 
	}
	
	Counter (const std::string &counterName, int counts=0, int freqs=0)
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

	StorageType freqs (void) { return m_freqs; }
	StorageType counts (void) { return m_counts; }
	StorageType cumulativeFreqs (void) {return m_cumulativeFreqs; }
	StorageType cumulativeCounts (void) {return m_cumulativeCounts; }
	void addFreqs (int value) { m_freqs += value;}
	void addCounts (int value) {
		if (s_isMaxMask & (1 << this->m_type))
			{ pickUpMax (this->m_counts, value);}
		else
			{ m_counts += value; }
	}
	
	void accumulateFreqs (int value) { m_cumulativeFreqs += value; }
	void accumulateCounts (int value) { 
		if (s_isMaxMask & (1 << this->m_type))
			{ pickUpMax (this->m_cumulativeCounts, value); }
		else
			{ this->m_cumulativeCounts += value; }
	}
	// TODO: Create a "RingManipulator" rather than having all this static stuff
	//  	 in the Counter class????
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
		
		static lat::Regexp isMaxRE ("_MAX");
		if (isMaxRE.match (name))
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

private:
	static IdCache s_countersByName;
	static int s_lastAdded;
	static int s_isMaxMask;
	static int s_ticksCounterId;
	static int s_keyValue;
	static std::string s_keyName;
	Counter *m_next;
	int m_type;
	StorageType m_counts;
	StorageType m_freqs;
	StorageType m_cumulativeCounts;
	StorageType m_cumulativeFreqs;
};

int Counter::s_isMaxMask = 0;
int Counter::s_ticksCounterId = -1;
int Counter::s_keyValue = -1;
std::string Counter::s_keyName;


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
	static const int BUFFER_SIZE=10000000; 
	FileOpener (void)
		: m_posInBuffer (BUFFER_SIZE),
		  m_lastInBuffer (BUFFER_SIZE),
		  m_eof (false)
	{
	}
	
	virtual ~FileOpener (void)
	{
		for (StreamsIterator i = m_streams.begin ();
			 i != m_streams.end ();
			 i++)
		{
			delete *i;
		}
	}
	
	lat::InputStream &stream (void)
	{
		return *m_streams.back ();
	}

	void addStream (lat::InputStream *stream)
	{
		m_streams.push_back (stream);
	}
	
	void readLine (void)
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
		ASSERT (remainingsSize <= BUFFER_SIZE);
		if (remainingsSize == BUFFER_SIZE)
		{
			// TODO: handle the case in which the line is longer than BUFFER_SIZE.
			std::cerr << "Line too long!!" << std::endl;
			exit (1);
		}
		if (remainingsSize)
			memmove (m_buffer, m_buffer + beginInBuffer, remainingsSize);
		int readSize = this->stream ().read (m_buffer + remainingsSize, BUFFER_SIZE-remainingsSize);
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
	typedef Streams::iterator StreamsIterator;
	Streams m_streams;
	char m_buffer[BUFFER_SIZE];
	int m_posInBuffer;
	int m_lastInBuffer;
	int m_eof;
	const char *m_curString;
	int m_curStringSize;
};

class FileReader : public FileOpener
{
public:
	FileReader (const std::string &filename)
	: FileOpener (),
	  m_file (filename)
	{	
		lat::StorageInputStream *storageInput = new lat::StorageInputStream (&m_file);
		addStream (storageInput);
		addStream (new lat::BufferInputStream (storageInput));
	}
	~FileReader (void)
	{
		m_file.close ();
	}
private:
	lat::File m_file;
};

class PathCollection
{
public:
	typedef lat::StringList Paths;
	typedef Paths::const_iterator Iterator;
	PathCollection (char *variableName)
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
thousands (int value, int leftPadding=0)
{
	// Converts an integer value to a string
	// adding `'` to separate thousands and possibly
	ASSERT (value >= 0);
	ASSERT (leftPadding >= 0);
	int n = 1; int digitCount = 0;
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


int maxOf ( int amount, ...)
{
  int i,val,greater;
  va_list vl;
  va_start(vl,amount);
  greater=va_arg(vl,int);
  for (i=1;i<amount;i++)
  {
    val=va_arg(vl,int);
    greater=(greater>val)?greater:val;
  }
  va_end(vl);
  return greater;
}

std::string
toString (int value)
{
	// FIXME: not thread safe... Do we actually care? Probably not.
	static char buffer [1024];
	sprintf (buffer,"%d",value);
	return buffer;
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
	
	int operator() (int position, int base=10)
	{
		return strtol (m_string + m_match->matchPos (position), 0, base);
	}
private:
	const char *m_string;
	lat::RegexpMatch *m_match;
};

class AlignedPrinter
{
public:
	AlignedPrinter (int size)
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
	int m_size;
};

class FractionPrinter
{
public:
	FractionPrinter (int sizeN, int sizeD)
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
	int m_sizeN;
	int m_sizeD;
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
		return std::find (m_symbols.begin (), m_symbols.end (), name) != m_symbols.end (); }
	
private:
	typedef std::list<std::string> SymbolNames; 
	SymbolNames m_symbols;
};

#endif