#include "IgProfAnalyze.h"
#include <classlib/utils/DebugAids.h>
#include <classlib/utils/Callback.h>
#include <classlib/utils/Hook.h>
#include <classlib/utils/Error.h>
#include <classlib/utils/Signal.h>
#include <classlib/utils/Regexp.h>
#include <classlib/utils/Argz.h>
#include <classlib/iobase/SubProcess.h>
#include <classlib/iobase/Pipe.h>
#include <classlib/iobase/File.h>
#include <classlib/iobase/TempFile.h>
#include <classlib/iotools/StorageInputStream.h>
#include <classlib/iotools/BufferInputStream.h>
#include <classlib/iotools/IOChannelInputStream.h>
#include <classlib/iotools/InputStream.h>
#include <classlib/iotools/InputStreamBuf.h>
#include <iostream>
#include <cstdarg>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define IGPROF_MAX_DEPTH 1000


void dummy (void) {}


void
usage ()
{
	std::cerr << "igprof-analyse\n"
		 		 "  [-r/--report KEY[,KEY]...] [-o/--order ORDER]\n"
		 		 "  [-p/--paths] [-c/--calls] [--value peak|normal]\n"
				 "  [-F/--filter-module FILE] [ -f FILTER[,FILTER...] ]\n"
				 "  [-nf/--no-filter] [-lf/--list-filters]\n"
				 "  { [-x/--xml] | [-h/--html] | [-t/--text] }\n"
				 "  [--libs] [--demangle] [--gdb] [-v/--verbose]\n"
				 "  [--] [FILE]...\n" << std::endl;
}


class ProfileInfo 
{
public:
	struct CounterInfo
	{
	};
	
	struct FileInfo
	{
		std::string NAME;
		FileInfo (void) : NAME ("") {}
		FileInfo (const std::string &name)
		: NAME (name) {}
	};
	
	struct SymbolInfo
	{
		std::string NAME;
		FileInfo 	*FILE;
		int 		FILEOFF;
		SymbolInfo (const char *name, FileInfo *file, int fileoff)
			: NAME (name), FILE (file), FILEOFF (fileoff), RANK (-1) {};
		int rank (void) { return RANK; }
		void setRank (int rank) { RANK = rank; }

	private:
		int RANK;
	};

	class NodeInfo
	{
	public:
		typedef std::list <NodeInfo *> Nodes;
		
		Nodes CHILDREN;
		Counter *COUNTERS;
		
		NodeInfo (SymbolInfo *symbol)
		: COUNTERS (0), SYMBOL (symbol), m_reportSymbol (0) {};
		NodeInfo *getChildrenBySymbol (SymbolInfo *symbol)
		{
			for (Nodes::const_iterator i = CHILDREN.begin ();
				 i != CHILDREN.end ();
				 i++)
			{
				if ((*i)->SYMBOL->NAME == symbol->NAME)
					return *i;
			}
			return 0;
		}
		
		
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
	private:
		SymbolInfo *SYMBOL;	
		SymbolInfo *m_reportSymbol;
	};

	typedef std::vector<FileInfo *> Files;
	typedef std::vector<SymbolInfo *> Syms;
	typedef std::vector<NodeInfo *> Nodes;
	typedef std::vector<int> Counts;
	typedef std::map<std::string, Counts> CountsMap;
	typedef std::map<std::string, std::string> Freqs;
	typedef std::map<std::string, std::string> LeaksMap;
	typedef std::map<std::string, SymbolInfo*> SymCacheByFile;
	typedef std::map<std::string, SymbolInfo*> SymCacheByName;

	ProfileInfo (void);

	Files & files(void) { return m_files; }
	Syms & syms(void) { return m_syms; }
	Nodes & nodes(void) { return m_nodes; }
	CountsMap & counts(void) { return m_counts; }
	Freqs  & freqs(void) { return m_freqs; }
	NodeInfo *spontaneous(void) { return m_spontaneous; }
	SymCacheByFile &symcacheByFile(void) { return m_symcacheFile; }
	SymCacheByName &symcacheByName(void) { return m_symcacheSymbol; }
private:
	Files m_files;
	Syms m_syms;
	Nodes m_nodes;
	CountsMap m_counts;
	Freqs m_freqs;
	LeaksMap m_leaks;
	SymCacheByFile m_symcacheFile;
	SymCacheByName m_symcacheSymbol;
	NodeInfo  *m_spontaneous;
};

ProfileInfo::ProfileInfo ()
{
	FileInfo *unknownFile = new FileInfo ("<unknown>");
	SymbolInfo *spontaneousSym = new SymbolInfo ("<spontaneous>", unknownFile, 0);
	m_spontaneous = new NodeInfo (spontaneousSym);
	m_files.push_back (unknownFile);
	m_syms.push_back (spontaneousSym);
	m_nodes.push_back (m_spontaneous);
}

class IgProfFilter;

class Configuration 
{
public:
	typedef std::list<IgProfFilter *> Filters;

	enum OutputType {
		TEXT=0,
		XML=1,
		HTML=2
	};
	enum Ordering {
		DESCENDING=-1,
		ASCENDING=1
	};
	
	Configuration (void);

	Filters & filters (void) {return m_filters;}
	void addFilter (IgProfFilter *filter) {};

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
	bool showLib (void)	{ return m_showLib; }

	void setCallgrind (bool value) 	{ m_callgrind = value ? 1 : 0; }
	bool callgrind (void) { return m_callgrind == 1; }
	bool isCallgrindDefined (void) { return m_callgrind != -1; }

	void setShowCalls (bool value) { m_showCalls = value ? 1 : 0; }
	bool isShowCallsDefined (void) { return m_showCalls != -1; }
	bool showCalls (void) { return m_showCalls == 1; }

	void setDoDemangle (bool value)	{ m_doDemangle = value;}
	bool doDemangle (void) { return m_doDemangle; }

	void setUseGdb (bool value)	{ m_useGdb = value;}
	bool useGdb (void) { return m_useGdb; }

	void setShowPaths (bool value)	{ m_showPaths = value;}
	bool showPaths (void) { return m_showPaths; }

	void setVerbose (bool value) { m_verbose = value;}
	bool verbose (void) { return m_verbose; }

	void setOutputType (enum OutputType value) { m_outputType = value; }
	bool outputType (void) { return m_outputType; }

	void setOrdering (enum Ordering value) {m_ordering = value; }
	Ordering ordering (void) { return m_ordering; }

	void setNormalValue (bool value) {m_normalValue = value; }
	bool normalValue (void) {return m_normalValue; }
	
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
  m_normalValue (true)
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

void
checkHeaders (const std::string &headerLine)
{
	lat::Regexp matchHeader ("^P=\\(.*T=(.*)\\)");
	if (!matchHeader.match (headerLine))
	{
		std::cerr << "\nThis does not look like an igprof profile stats:\n  ";
		std::cerr << headerLine << std::endl;
		exit (1);
	}
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
symlookup (ProfileInfo &prof, ProfileInfo::FileInfo *file, 
		   int fileoff, const std::string& symname, bool useGdb)
{
	// TODO: implement the symbol resolution stuff.
	if ((lat::StringOps::find (symname, "@?") == 0) && (file->NAME != "") && (fileoff > 0))
	{
        char buffer[32];
        sprintf (buffer, "+%d}",fileoff);
		return std::string ("@{") 
		       + lat::Filename (file->NAME).asFile ().nondirectory ().name () 
		       + buffer;
	}
	return symname;
	
	ProfileInfo::SymCacheByName &cacheByName = prof.symcacheByName ();
	ProfileInfo::SymCacheByFile &cacheByFile = prof.symcacheByFile ();
    (void) cacheByName;
	if (useGdb)
	{
		lat::Filename filename (file->NAME);
		if (cacheByFile.find (static_cast<const char *>(filename)) != cacheByFile.end () 
			&& filename.isRegular ())
		{
			// int vmbase = 0;
			lat::Argz objectDumpCmdline (std::string ("objdump -p ") + std::string (filename));
			lat::SubProcess objectDump (objectDumpCmdline.argz ());
			
		}
	}
	
	ASSERT (false);
	return symname;
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

class IgProfFilter 
{
public:
	enum FilterType 
	{
		PRE = 1,
		POST = 2,
		BOTH = 3
	};
	
	IgProfFilter (void)
	 : m_prof (0) {}
	
	virtual ~IgProfFilter (void) {}
	
	virtual void init (ProfileInfo *prof)
	{ m_prof = prof; }
	
	virtual void pre (ProfileInfo::NodeInfo *parent,
					     ProfileInfo::NodeInfo *node) {};
	virtual void post (ProfileInfo::NodeInfo *parent,
					     ProfileInfo::NodeInfo *node) {};
	virtual std::string name (void) const = 0;
	virtual enum FilterType type (void) const = 0;
protected:
	ProfileInfo::Syms &syms (void) { return m_prof->syms (); }
	ProfileInfo::Nodes &nodes (void) { return m_prof->nodes (); } 
private:
	ProfileInfo  *m_prof;
};

class AddCumulativeInfoFilter : public IgProfFilter 
{
public:
	virtual void post (ProfileInfo::NodeInfo *parent,
				         ProfileInfo::NodeInfo *node)
	{
		ASSERT (node);
		if (!parent) return;
		Counter *initialCounter = node->COUNTERS;
		if (! initialCounter) return;
		Counter *next = initialCounter;
		int loopCount = 0;
		do
		{
			ASSERT (loopCount++ < 32);
			int id = next->id ();
			ASSERT (parent);
			Counter *parentCounter = Counter::addCounterToRing (parent->COUNTERS, 
																id);
			next->accumulateCounts (next->counts ());
			next->accumulateFreqs (next->freqs ());
			parentCounter->accumulateCounts (next->cumulativeCounts ());
			parentCounter->accumulateFreqs (next->cumulativeFreqs ());
			next = next->next ();
		} while (initialCounter != next);
	}
	virtual std::string name (void) const { return "cumulative info"; }
	virtual enum FilterType type (void) const {return POST; }
};

void mergeToNode (ProfileInfo::NodeInfo *parent, 
					ProfileInfo::NodeInfo *node)
{
	ProfileInfo::NodeInfo::Nodes::const_iterator i = node->CHILDREN.begin ();
	while (i != node->CHILDREN.end ())
	{
		ProfileInfo::NodeInfo *nodeChild = *i;
		ASSERT (nodeChild->symbol ());
		ProfileInfo::NodeInfo *parentChild = parent->getChildrenBySymbol (nodeChild->symbol ());
		
		// If the child is not already child of parent, simply add it.
		if (!parentChild)
		{
			parent->CHILDREN.push_back (nodeChild);
			node->removeChild (*i++);
			continue;
		}
		
		// If the child is child of the parent, accumulate all its counters to the child of the parent
		// and recursively merge it.
		while (Counter *nodeChildCounter = Counter::popCounterFromRing (nodeChild->COUNTERS))
		{
			Counter *parentChildCounter = Counter::addCounterToRing (parentChild->COUNTERS,
																	 nodeChildCounter->id ());
			parentChildCounter->addFreqs (nodeChildCounter->freqs ());
			parentChildCounter->addCounts (nodeChildCounter->counts ());
		}
		mergeToNode (parentChild, nodeChild);
		++i;
	}
	ASSERT (node == parent->getChildrenBySymbol (node->symbol ()));
	unsigned int numOfChildren = parent->CHILDREN.size ();
	parent->removeChild (node);
	ASSERT (numOfChildren == parent->CHILDREN.size () + 1);		
	ASSERT (!parent->getChildrenBySymbol (node->symbol ()));
}


class RemoveIgProfFilter : public IgProfFilter
{
public:
	virtual void post (ProfileInfo::NodeInfo *parent,
					   ProfileInfo::NodeInfo *node)
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
	
	virtual void post (ProfileInfo::NodeInfo *parent,
					   ProfileInfo::NodeInfo *node)
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
				  << " in " << node->originalSymbol ()->FILE->NAME <<". Merging." << std::endl;
		mergeToNode (parent, node);
	}
	virtual std::string name (void) const { return "remove std"; }
	virtual enum FilterType type (void) const { return POST; }
};


class FlatInfo
{
public:
	struct CompareBySymbol
	{
		bool operator () (FlatInfo *a, FlatInfo *b) const
		{
			return a->SYMBOL < b->SYMBOL;
		}
	};
	
	typedef std::set<FlatInfo *, CompareBySymbol> ReferenceList;
	typedef std::map<ProfileInfo::SymbolInfo *, FlatInfo *> FlatMap;

	static FlatInfo *findBySymbol (ReferenceList &list, 
								   ProfileInfo::SymbolInfo *symbol)
	{
		static FlatInfo dummy (0, 0xdeadbeef);
		ASSERT (symbol);
        dummy.SYMBOL = symbol;
		ReferenceList::const_iterator i = list.find (&dummy);
		if (i != list.end ())
		{ return *i; }
		return 0;
	}
	
	static FlatMap &flatMap (void)
	{
		static FlatMap s_flatMap;
		return s_flatMap;
	}

	static void setKeyId (int id)
	{
		s_keyId = id;
	}
	
	static FlatInfo *first (void)
	{
		ASSERT (s_first);
		return s_first;
	}
	
	static FlatInfo *get (ProfileInfo::SymbolInfo *sym, bool create=true)
	{
		FlatMap::iterator i = FlatInfo::flatMap ().find (sym);
		if (i != FlatInfo::flatMap ().end ())
			return i->second;

		if (!create)
			return 0;

		FlatInfo *result = new FlatInfo (sym, s_keyId);
		flatMap ().insert (FlatMap::value_type (sym, result));
		if (! s_first) s_first = result;
		return result;
	}
	
	static FlatInfo *clone (FlatInfo *info)
	{
		FlatInfo *result = new FlatInfo (info->SYMBOL, s_keyId);
		result->DEPTH = info->DEPTH;
		result->REFS = info->REFS;
		return result;
	}
	
	std::string filename (void)
	{
		return SYMBOL->FILE->NAME;
	}
	
	const char *name (void)
	{
		return SYMBOL->NAME.c_str ();
	}
	
	ReferenceList CALLERS;
	ReferenceList CALLS;
	ProfileInfo::SymbolInfo *SYMBOL;
	Counter *COUNTERS;
	int DEPTH;
	int REFS;
	int rank (void) {return SYMBOL->rank (); }
	void setRank (int rank) {SYMBOL->setRank (rank); }

protected:
	FlatInfo (ProfileInfo::SymbolInfo *symbol, int id)
	: SYMBOL (symbol), COUNTERS (0), DEPTH (0), REFS (0) {
		ASSERT (id != -1);
		Counter::addCounterToRing (COUNTERS, id);
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
	SymbolInfoFactory (ProfileInfo *prof, bool useGdb)
	 :m_prof (prof), m_useGdb (useGdb)
	{}

	ProfileInfo::SymbolInfo *getSymbol (unsigned int id)
	{
		ASSERT ( id <= m_symbols.size ());
		return m_symbols[id];
	}

	ProfileInfo::FileInfo *getFile (unsigned int id)
	{
		ASSERT (id <= m_files.size ());
		return m_files[id];
	}

	ProfileInfo::FileInfo *createFileInfo (const std::string &origname, unsigned int fileid)
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

		// TODO:
		// $absname = (abs_path($origname) || $origname)
		//     if length($origname);
		FilesByName::iterator fileIter = m_namedFiles.find (absname);
		if (fileIter != m_namedFiles.end ())
		{ return fileIter->second; }
		else
		{ 
			ProfileInfo::FileInfo *file = new ProfileInfo::FileInfo (absname); 
			m_namedFiles.insert (FilesByName::value_type (absname, file));
			int oldsize = m_files.size ();
			int missingSize = fileid + 1 - oldsize; 
			if (missingSize > 0)
			{ m_files.resize (fileid + 1);
				for (int i = oldsize; i < oldsize + missingSize; i++)
				{ ASSERT (m_files[i] == 0); }
			}
			m_files[fileid] = file;
			return file;
		}
	}

	
	ProfileInfo::SymbolInfo *createSymbolInfo (const std::string &line, unsigned int symid, 
											   Position &pos, int lineCount)
	{
		// Regular expressions matching the file and symbolname information.
		static lat::Regexp fRE ("F(\\d+)\\+(-?\\d+) N=\\((.*?)\\)\\)\\+\\d+\\s*");
		static lat::Regexp fWithFilenameRE ("F(\\d+)=\\((.*?)\\)\\+(-?\\d+) N=\\((.*?)\\)\\)\\+\\d+\\s*");
		static lat::RegexpMatch match;
		
		ProfileInfo::FileInfo *file = 0;
		std::string symname;
		unsigned int fileoff;

		match.reset ();
		
		if (fRE.match (line, pos (), 0, &match))
		{
			IntConverter getIntMatch (line, &match);
			fileoff = getIntMatch (2);
			symname = match.matchString (line, 3);
			file = getFile (getIntMatch (1));
		}
		else if (fWithFilenameRE.match (line, pos (), 0, &match))
		{
			IntConverter getIntMatch (line, &match);
			fileoff = getIntMatch (3);
			symname = match.matchString (line, 4);
			file = createFileInfo (match.matchString (line, 2), 
								   getIntMatch (1));
		}
		else
		{
			ASSERT (false);
			printSyntaxError (line, file->NAME, lineCount, pos ());
			exit (1);
		}

		pos (match.matchEnd ());
		
		symname = symlookup (*m_prof, file, fileoff, symname, m_useGdb);

		ProfileInfo::SymbolInfo *sym = namedSymbols()[symname];
		if (! sym)
		{
			sym = new ProfileInfo::SymbolInfo (symname.c_str (), file, fileoff);
			namedSymbols ().insert (SymbolsByName::value_type (symname, sym));
			ASSERT (symid >= m_symbols.size ()); 
			m_symbols.resize (symid + 1);
			m_symbols[symid] = sym;
		}
		return sym;
	}

	typedef std::map<std::string, ProfileInfo::SymbolInfo *> SymbolsByName;
	
	static SymbolsByName &namedSymbols (void)
	{
		static SymbolsByName s_namedSymbols;
		return s_namedSymbols;
	}
	
private:
	typedef std::vector<ProfileInfo::FileInfo *> Files;
	typedef std::map<std::string, ProfileInfo::FileInfo *> FilesByName;
	typedef std::vector<ProfileInfo::SymbolInfo *> Symbols;
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
		unsigned int tickPos = fullSymbol.rfind ("'");
		if (tickPos == std::string::npos)
		{
            oldSymbol = fullSymbol;
            suffix = "";
            return;
		}
		ASSERT (tickPos < fullSymbol.size ());
		oldSymbol.assign (fullSymbol.c_str (), tickPos - 1);
		suffix.assign (fullSymbol.c_str () + tickPos + 1);
	}

	static std::string removeSuffix (const std::string &fullSymbol)
	{
		unsigned int tickPos = fullSymbol.rfind ("'");
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
		FlatInfo::setKeyId (id);
		ASSERT (m_zeroCounter.cumulativeCounts () == 0);
		ASSERT (m_zeroCounter.cumulativeFreqs () == 0);
		ASSERT (m_zeroCounter.counts () == 0);
		ASSERT (m_zeroCounter.freqs () == 0);		
	}
	
	virtual void pre (ProfileInfo::NodeInfo *parent,
					  ProfileInfo::NodeInfo *node)
	{
		ASSERT (node);
		//std::cerr << "\nNow parsing at depth " << seen ().size() << std::endl;
		ProfileInfo::SymbolInfo *sym = symfor (node);
		ASSERT (sym);
		//std::cerr << "Actual symbol name " << sym->NAME << std::endl;
		FlatInfo *recursiveNode = FlatInfo::get (sym);
		ASSERT (recursiveNode);
		//std::cerr << "FlatInfo associated: " << recursiveNode << std::endl;
		recursiveNode->DEPTH = seen ().size ();

		Counter *nodeCounter = Counter::getCounterInRing (node->COUNTERS, 
														  m_keyId);
		
		ASSERT (nodeCounter);
		
		if (parent)
		{
			//std::cerr << "Parent : ";
			Counter *parentCounter = Counter::getCounterInRing (parent->COUNTERS, 
															  	m_keyId);
			ASSERT (parentCounter);
	
			ProfileInfo::SymbolInfo *parsym = parent->symbol ();
			//std::cerr << parsym->NAME << std::endl;
			
			FlatInfo *parentInfo = FlatInfo::get (parsym, false);
			ASSERT (parentInfo);

			if (! FlatInfo::findBySymbol (recursiveNode->CALLERS, parsym))
				recursiveNode->CALLERS.insert (parentInfo);

			FlatInfo *nodeInfo = FlatInfo::findBySymbol (parentInfo->CALLS, 
												     	 sym);
			if (!nodeInfo)
			{
				//std::cerr << "   Creating instance of symbol " << sym->NAME
				//		  << " to add to parent" << std::endl;
				nodeInfo = FlatInfo::clone (recursiveNode);
				parentInfo->CALLS.insert (nodeInfo);
				ASSERT (nodeInfo->rank () == recursiveNode->rank ());
				ASSERT (FlatInfo::flatMap ()[sym] != nodeInfo);
			}
			ASSERT (nodeInfo->SYMBOL->NAME == sym->NAME);
			ASSERT (parentInfo->SYMBOL->NAME == parsym->NAME);

			//nodeInfo->COUNTERS->addCounts (nodeCounter->cumulativeCounts ());
			//nodeInfo->COUNTERS->addFreqs (nodeCounter->cumulativeFreqs ());
			nodeInfo->COUNTERS->accumulateCounts (nodeCounter->cumulativeCounts ());
			nodeInfo->COUNTERS->accumulateFreqs (nodeCounter->cumulativeFreqs ());
			nodeInfo->REFS++;
			ASSERT (recursiveNode != nodeInfo);
		}
		
		Counter *flatCounter = Counter::getCounterInRing (recursiveNode->COUNTERS, m_keyId);
		recursiveNode->REFS++;
		flatCounter->addCounts (nodeCounter->counts ());
		flatCounter->addFreqs (nodeCounter->freqs ());
		flatCounter->accumulateCounts (nodeCounter->cumulativeCounts ());
		flatCounter->accumulateFreqs (nodeCounter->cumulativeFreqs ());
		ASSERT (flatCounter->freqs () <= flatCounter->cumulativeFreqs ());
	}

	virtual void post (ProfileInfo::NodeInfo *parent,
					   ProfileInfo::NodeInfo *node)
	{
		ASSERT (node);
		ASSERT (node->symbol ());
		//std::cerr << "Removing: " << node->symbol ()->NAME << std::endl;
		ASSERT (seen ().count(node->symbol()->NAME) > 0);
		seen ().erase (node->symbol ()->NAME);
		ASSERT (seen ().count(node->symbol()->NAME) == 0);
	}

	virtual std::string name () const { return "tree map builder"; }
	virtual enum FilterType type () const { return BOTH; }

	typedef std::map<ProfileInfo::SymbolInfo *, FlatInfo *> FlatMap;
private:
	typedef std::map<std::string, ProfileInfo::SymbolInfo *>  SeenSymbols;

	ProfileInfo::SymbolInfo *symfor (ProfileInfo::NodeInfo *node) 
	{
		ASSERT (node);
		//std::cerr << "symfor " << origsym << " " << origsym->NAME << std::endl;
		ProfileInfo::SymbolInfo *reportSymbol = node->reportSymbol ();
		if (reportSymbol)
		{
			seen ().insert (SeenSymbols::value_type (reportSymbol->NAME, 
				                                     reportSymbol));
			//std::cerr << " -> returning " << reportSymbol << " " << reportSymbol->NAME << std::endl;
			return reportSymbol;
		}
		
		std::string suffix = "";
		
		ASSERT (node->originalSymbol ());
		std::string symbolName = node->originalSymbol ()->NAME;
		
		SeenSymbols::iterator i = seen ().find (symbolName);
		if (i != seen ().end ())
		{
			std::string newName = getUniqueName (symbolName);
			//std::cerr << " -> using unique name " << newName << std::endl;
			SymbolInfoFactory::SymbolsByName &namedSymbols = SymbolInfoFactory::namedSymbols ();
			//if (namedSymbols.find (newName) != namedSymbols.end ())
			//{
			//	std::cerr << "Symbols seen:" << symbolName << " -> " << newName << std::endl;
			//	for (SymbolInfoFactory::SymbolsByName::const_iterator i = namedSymbols.begin ();
			//		 i != namedSymbols.end ();
			//		 i++)
			//		std::cerr << "   key: " << i->first << " -> " << i->second;
			//	std::cerr << newName << std::endl;
			//	ASSERT (false);
			//}

			//ASSERT (seen ().find (newName) == seen ().end ());
			SymbolInfoFactory::SymbolsByName::iterator s = namedSymbols.find (newName);
			if (s == namedSymbols.end ())
			{
				ProfileInfo::SymbolInfo *originalSymbol = node->originalSymbol ();
				reportSymbol = new ProfileInfo::SymbolInfo (newName.c_str (),
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
		//std::cerr << " -> returning " << node->symbol() << " " << node->symbol()->NAME << std::endl;
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
private:
	lat::File *m_file;
};

void symremap (ProfileInfo &prof, bool usegdb, bool demangle)
{
	if (usegdb)
	{
		lat::Filename tmpFilename ("/tmp/igprof-analyse.gdb.XXXXXXXX");
		lat::File *file = lat::TempFile::file (tmpFilename);
		lat::Filename prevfile ("");
		TextStreamer out (file);
		out << "set width 10000\n";

		for (ProfileInfo::Syms::const_iterator i = prof.syms ().begin ();
			 i != prof.syms ().end ();
			 i++)
		{
			ASSERT (*i);
			ProfileInfo::SymbolInfo *symPtr = *i;
			ProfileInfo::SymbolInfo &sym = *symPtr;
			if ((! sym.FILE) || (! sym.FILEOFF) || (sym.FILE->NAME != ""))
				continue;
			if (sym.FILE->NAME != prevfile)
				out << "file " << sym.FILE->NAME << "\n";
			out << "echo IGPROF_SYMCHECK <" << toString (int(symPtr)) << ">\\n\n";
			out << "info line *" << toString (sym.FILEOFF);
			prevfile = sym.FILE->NAME; 
		}
		file->close ();
		
		lat::Argz args (std::string ("gdb --batch --command=") + std::string (tmpFilename));
		lat::Pipe pipe;
		lat::SubProcess gdb (args.argz (), 
							 lat::SubProcess::One
							 | lat::SubProcess::Read,
							 &pipe);
		lat::IOChannelInputStream is (pipe.source ());
		lat::InputStreamBuf  isbuf (&is);
		std::istream istd (&isbuf);
		
		lat::Regexp SYMCHECK_RE ("IGPROF_SYMCHECK<.*>");
		lat::Regexp STARTS_AT_RE ("starts at .* <([A-Za-z0-9_]+)(\\+\\d+)?>");
		lat::Regexp NO_LINE_NUMBER ("^No line number .* <([A-Za-z0-9_]+)(\\+\\d+)?>");
		
		std::string oldname;
		std::string suffix;
		ProfileInfo::SymbolInfo *sym = 0;
		
		while (istd)
		{
			std::string line;
			std::getline (istd, line);
			
			if (!istd)
				break;
			if (line.empty ())
				continue;

			lat::RegexpMatch match;

			if (SYMCHECK_RE.match (line, 0, 0, &match))
			{
				sym = prof.symcacheByName ()[match.matchString (line, 1)];
				SuffixOps::splitSuffix (sym->NAME, oldname, suffix);
			}
			else if (STARTS_AT_RE.match (line, 0, 0, &match))
			{
				ASSERT (sym);
				sym->NAME = match.matchString (line, 1) + "'" + suffix;
				suffix = "";
				sym = 0; 
			}
			else
			{
			    //TODO: Implement here...
                ASSERT (false);
			}
		}
		ASSERT (false);
	}
	ASSERT (false);
}

class MallocFilter : public IgProfFilter
{
public:
	virtual void init (ProfileInfo *prof) {
		IgProfFilter::init (prof);
		m_filter = "malloc", "calloc", "realloc", "memalign", 
				   "posix_memalign", "valloc", "zmalloc", "zcalloc", 
				   "zrealloc", "_Znwj", "_Znaj", "_Znam";
	}
	
	virtual void post (ProfileInfo::NodeInfo *parent,
					     ProfileInfo::NodeInfo *node)
	{
		ASSERT (node);
		ASSERT (node->symbol ());
		ASSERT (m_filter.contains (std::string ("_Znaj")));
		
		if (! m_filter.contains (node->symbol ()->NAME)) 
		{
			return;
		}
		this->addCountsToParent (parent, node);
	}
	
	virtual std::string name (void) const { return "malloc"; }
	virtual enum FilterType type (void) const { return POST; }
	
private:
	void addCountsToParent (ProfileInfo::NodeInfo *parent,
							ProfileInfo::NodeInfo *node)
	{
		ASSERT (parent);
		ASSERT (node);
		ASSERT (m_filter.contains (node->originalSymbol ()->NAME));
		int countersCount = 0;
		ASSERT (node->COUNTERS);
		while (Counter *childCounter = Counter::popCounterFromRing (node->COUNTERS))
		{
			++countersCount;
			Counter *parentCounter = Counter::addCounterToRing (parent->COUNTERS, 
																childCounter->id ());
			parentCounter->addFreqs (childCounter->freqs ());
			parentCounter->addCounts (childCounter->counts ());
			ASSERT (parentCounter->cumulativeCounts () == 0);
			ASSERT (parentCounter->cumulativeFreqs () == 0);
		}
		ASSERT (countersCount);
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

	virtual void post (ProfileInfo::NodeInfo *parent,
					     ProfileInfo::NodeInfo *node) 
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

	virtual void pre (ProfileInfo::NodeInfo *parent,
					  ProfileInfo::NodeInfo *node) 
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
parseStackLine(const char *line, 
			   std::vector<ProfileInfo::NodeInfo *> &nodestack)
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
parseFunctionRef(const char *lineStart, Position &pos, unsigned int &symid, unsigned int fileoff) 
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
				 int &ctrid, int &ctrfreq, int &ctrvalNormal, int &ctrvalStrange)
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
	ctrvalStrange = count3;
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
parseLeak (const char *lineStart, Position &pos, int &leakAddress, int &leakSize)
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
	typedef std::vector<ProfileInfo::NodeInfo *> Nodes;
	Nodes nodestack;
	nodestack.reserve (IGPROF_MAX_DEPTH);
	
	if (m_config->verbose ())
		std::cerr << " X" << filename << std::endl;
	FileReader reader (filename);
	
	std::string line;
	line.reserve (FileOpener::BUFFER_SIZE);
	reader.readLine ();
	reader.assignLineToString (line);
	checkHeaders (line);
	
//	vWithDefinitionRE.study();
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
		ProfileInfo::SymbolInfo *sym;

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
		
		ProfileInfo::NodeInfo* parent = nodestack.empty () ? prof.spontaneous () : nodestack.back ();
		ProfileInfo::NodeInfo* child = parent ? parent->getChildrenBySymbol (sym) : 0;
		
		if (! child)
		{
			child = new ProfileInfo::NodeInfo (sym);
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
			int ctrval;
			int ctrfreq;
			int ctrvalNormal;
			int ctrvalStrange;
			int leakAddress;
			int leakSize;
			
			if (line.size () == pos())
			{ break; }
			else if (line.size() >= pos()+2
					 && parseCounterVal (line.c_str (), pos, ctrid, ctrfreq, ctrvalNormal, ctrvalStrange))
			{
				// FIXME: should really do:
				// $ctrname = $ctrbyid{$1} || die;

				ctrval = m_config->normalValue () ? ctrvalNormal : ctrvalStrange;
			}
			else if (line.size() >= pos()+2
					 && parseCounterDef (line, pos (), 0, &match))
			{
				// FIXME: should really do:
				// die if exists $ctrbyid{$1};
				std::string ctrname = match.matchString (line, 2);
				IntConverter getIntMatch (line, &match);
				ctrid = getIntMatch (1);
				Counter::addNameToIdMapping (ctrname, ctrid, (ctrname == "PERF_TICKS" && ! m_config->callgrind ()));
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
			ASSERT (counter);
		
			if (m_config->hasKey () && ! Counter::isKey (ctrid)) continue;
			if (! m_config->hasKey () && Counter::ringSize (counter) > 1) continue;
			
			counter->addFreqs (ctrfreq);
			counter->addCounts (ctrval);
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

class StackItem
{
public:
	typedef ProfileInfo::NodeInfo Node;
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

class StackManipulator
{
public:
	StackManipulator (std::list<StackItem> *stack)
	: m_stack (stack) {}
	
	typedef StackItem::Node::Nodes::iterator NodesIterator;
	
	void addChildrenToStack (StackItem::Node *node)
	{
		for (NodesIterator i = node->CHILDREN.begin (); 
			 i != node->CHILDREN.end (); i++)
		{ m_stack->push_back (StackItem (node, *i, 0)); }
	}
	
	void addToStack (StackItem::Node *parent,
					 StackItem::Node *prev)
	{ m_stack->push_back (StackItem (parent, 0, prev)); }

	void initStack (ProfileInfo &prof)
	{ m_stack->push_back (StackItem (0, prof.spontaneous (), 0)); }
private:
	std::list<StackItem> *m_stack;
};

void
walk (ProfileInfo &prof, IgProfFilter *filter=0)
{
	// TODO: Apply more than one filter at the time.
	// 		 This method applies one filter at the time. Is it worth to do
	//		 the walk only once for all the filters? Should increase locality
	//		 as well...
	ASSERT (filter);
	std::list<StackItem> stack;
	StackManipulator manipulator (&stack);
	manipulator.initStack (prof);
	
	while (!stack.empty ())
	{
		StackItem item = stack.back (); stack.pop_back ();
		StackItem::Node *node = item.prev ();
		StackItem::Node *parent = item.parent ();
		if (node)
		{
			if ( filter->type () & IgProfFilter::PRE)
				{ filter->pre (parent, node); }
			if ( filter->type () & IgProfFilter::POST)
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
			walk (prof, *i);
	}

	
	if (m_config->verbose ())
	{
		std::cerr << "Summing counters" << std::endl;
	}
	IgProfFilter *sumFilter = new AddCumulativeInfoFilter ();
	walk (prof, sumFilter);
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
		int cmp;
		cmp = cmpnodekey (a, b);
		if (cmp > 0) return true;
		else if (cmp < 0) return false;
		cmp = cmpcallers (a, b);
		if (cmp > 0) return true;
		else if (cmp < 0) return false;
		return strcmp (a->name (), b->name ());
	}
private:
	int cmpnodekey (FlatInfo *a, FlatInfo *b)
	{
		Counter *aCounter = Counter::getCounterInRing (a->COUNTERS, m_counterId);
		Counter *bCounter = Counter::getCounterInRing (b->COUNTERS, m_counterId);
		if (!aCounter) return -1;
		if (!bCounter) return 1;
		int aVal;
		int bVal;
		if (m_cumulative)
		{
			aVal = aCounter->cumulativeCounts ();
			bVal = bCounter->cumulativeCounts ();
		}
		else
		{
			aVal = aCounter->counts ();
			bVal = bCounter->counts ();
		}
		int result = -1 * m_ordering * (bVal - aVal);
		//std::cerr << bVal << " " << aVal << " "<< result << std::endl;
		return result;
	}

	int cmpcallers (FlatInfo *a, FlatInfo *b)
	{
		return a->DEPTH < b->DEPTH;
	}
	
	int m_counterId;
	int m_ordering;
	int m_cumulative;
};


class GProfRow
{
public:
	std::string NAME;
	std::string FILENAME;
	int RANK;
	int FILEOFF;
	float PCT;
	int DEPTH;
	
	void initFromInfo (FlatInfo *info)
	{
		RANK = info->rank ();
		NAME = info->name ();
		FILENAME = info->filename ();
		DEPTH = info->DEPTH;
	}
};

class OtherGProfRow : public GProfRow
{
public:
	int SELF_COUNTS;
	int CHILDREN_COUNTS;
	int SELF_CALLS;
	int TOTAL_CALLS;
	int SELF_PATHS;
	int TOTAL_PATHS;
	
	void printDebugInfo (void)
	{
		std::cerr << SELF_COUNTS << " " << CHILDREN_COUNTS << " ";
		std::cerr << SELF_CALLS << " " << SELF_CALLS << " ";
		std::cerr << SELF_PATHS << " " << TOTAL_PATHS << std::endl;
	}
};

template <int ORDER>
struct CompareCallersRow
{
	bool operator () (OtherGProfRow *a, OtherGProfRow *b)
	{ 
		int callsDiff = ORDER * (a->SELF_CALLS - b->SELF_CALLS);
		int cumDiff = ORDER * (a->SELF_COUNTS - b->SELF_COUNTS);
		if (callsDiff) return callsDiff < 0;
		if (cumDiff) return cumDiff < 0;
		return a->NAME < b->NAME;
	}
};

class MainGProfRow : public GProfRow 
{
public:
	typedef std::set <OtherGProfRow *, CompareCallersRow<1> > Callers;
	typedef std::set <OtherGProfRow *, CompareCallersRow<-1> > Calls;

	struct CountsData
	{
		int COUNTS;
		int FREQS;
		CountsData (int counts=0, int freqs=0)
		:COUNTS (counts), FREQS (freqs) {}
	};

	int CUM;
	int SELF;
	int KIDS;
	CountsData SELFALL;
	CountsData CUMALL;
	
	void printDebugInfo (void)
	{
		std::cerr << "SELFALL.COUNTS:" << SELFALL.COUNTS <<  " SELFALL.FREQS:" << SELFALL.FREQS << " ";
		std::cerr << "CUMFALL.COUNTS:" << CUMALL.COUNTS <<  " CUMALL.FREQS:" << CUMALL.FREQS << std::endl;
	}
	
	Callers CALLERS;
	Calls CALLS;
};

float percent (int a, int b)
{
	return static_cast<float> (a) / static_cast<float> (b) * 100.;
}

class GProfMainRowBuilder 
{
public:
	GProfMainRowBuilder (FlatInfo *info, int keyId, int totals, int totfreq)
	 : m_info (info), m_row (0), m_keyId (keyId), 
	   m_callmax (0), m_totals (totals), m_totfreq (totfreq)
	{
		m_selfCounter = getKeyCounter (m_info->COUNTERS);
		init ();
	}
	
	void addCallee (FlatInfo *calleeInfo)
	{
		ASSERT (m_row);
		FlatInfo *origin = FlatInfo::flatMap ()[m_info->SYMBOL];
		FlatInfo *thisCall = origin->findBySymbol (origin->CALLS, calleeInfo->SYMBOL);

		if (m_callmax < m_selfCounter->counts ())
			m_callmax = m_selfCounter->counts ();

		Counter *thisCounter = getKeyCounter (thisCall->COUNTERS);
		Counter *otherCounter = getKeyCounter (calleeInfo->COUNTERS);
		
		if (!otherCounter->cumulativeCounts ())
			{ return; }
		OtherGProfRow *callrow = new OtherGProfRow ();
		callrow->initFromInfo (calleeInfo);
		callrow->PCT = percent (thisCounter->cumulativeCounts (), m_totals);
		callrow->SELF_COUNTS = thisCounter->cumulativeCounts ();
		callrow->CHILDREN_COUNTS = otherCounter->cumulativeCounts ();
		callrow->SELF_CALLS = thisCounter->cumulativeFreqs ();
		callrow->TOTAL_CALLS = otherCounter->cumulativeFreqs ();
		callrow->SELF_PATHS = thisCall->REFS; 
		callrow->TOTAL_PATHS = calleeInfo->REFS;
		m_row->CALLS.insert (callrow);
		ASSERT (callrow->SELF_CALLS <= callrow->TOTAL_CALLS);
		
	}
	
	void addCaller (FlatInfo *callerInfo)
	{ 
		ASSERT (m_row);
		FlatInfo *origin = FlatInfo::flatMap ()[callerInfo->SYMBOL];
		FlatInfo *thisCall = origin->findBySymbol (callerInfo->CALLS, m_info->SYMBOL);
		
		Counter *thisCounter = getKeyCounter (thisCall->COUNTERS);
		Counter *otherCounter = getKeyCounter (callerInfo->COUNTERS);
		if (!otherCounter->cumulativeCounts ())
			{ return; }
		OtherGProfRow *callrow = new OtherGProfRow ();
		callrow->initFromInfo (callerInfo);
		callrow->PCT = percent (thisCounter->cumulativeCounts (), m_totals);
		callrow->SELF_COUNTS = thisCounter->cumulativeCounts ();
		callrow->CHILDREN_COUNTS = otherCounter->cumulativeCounts ();
		callrow->SELF_CALLS = thisCounter->cumulativeFreqs ();
		callrow->TOTAL_CALLS = otherCounter->cumulativeFreqs ();
		callrow->SELF_PATHS = thisCall->REFS; 
		callrow->TOTAL_PATHS = callerInfo->REFS;
		m_row->CALLERS.insert (callrow);
		// ASSERT ((callrow->SELF_CALLS <= callrow->TOTAL_CALLS) || (! isChild));
	}

	
  	void init (void)
	{
		m_row = new MainGProfRow ();
		m_row->initFromInfo (m_info);
		m_row->PCT = percent (m_selfCounter->cumulativeCounts (), m_totals);
		m_row->CUM = m_selfCounter->cumulativeCounts ();
		m_row->SELF = m_selfCounter->counts ();		
		m_row->SELFALL = MainGProfRow::CountsData (m_selfCounter->counts (), 
									 			   m_selfCounter->freqs ());
		m_row->CUMALL = MainGProfRow::CountsData (m_selfCounter->cumulativeCounts (), 
												  m_selfCounter->cumulativeFreqs ());
	}

	MainGProfRow *build (void)
	{
		ASSERT (m_row);
		m_row->KIDS = m_selfCounter->isMax () ? m_callmax : m_selfCounter->cumulativeCounts () - m_selfCounter->counts ();
		return m_row;
	}
private:

	Counter *getKeyCounter (Counter *counter)
	{
		return Counter::getCounterInRing (counter, m_keyId);
	}

	FlatInfo *m_info;
	MainGProfRow *m_row;
	unsigned int m_keyId;
	int m_callmax;
	int m_totals;
	int m_totfreq;
	Counter *m_selfCounter;
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

int
max (int a, int b)
{
	return a > b ? a : b;
}

class SortRowBySelf
{
public:
	bool operator () (MainGProfRow *a, MainGProfRow *b) {
		int diffSelf = a->SELF - b->SELF;
		if (diffSelf) return diffSelf > 0;
		int diffDepth = a->DEPTH - b->DEPTH;
		if (diffDepth) return diffDepth > 0;
		return a->NAME < b->NAME;
	}
};

void
IgProfAnalyzerApplication::analyse (ProfileInfo &prof)
{
	prepdata (prof);
	if (m_config->verbose ())
		{ std::cerr << "Building call tree map" << std::endl; }
	IgProfFilter *callTreeBuilder = new TreeMapBuilderFilter (&prof, m_config);
	walk (prof, callTreeBuilder);
	// Sorting flat entries
	if (m_config->verbose ())
		{ std::cerr << "Sorting" << std::endl; }
	int rank = 1;
	typedef std::vector <FlatInfo *> FlatVector;
	FlatVector sorted;	
	for (FlatInfo::FlatMap::const_iterator i = FlatInfo::flatMap ().begin ();
		 i != FlatInfo::flatMap ().end ();
	     i++)
	{ sorted.push_back (i->second); }
	
	sort (sorted.begin (), sorted.end (), FlatInfoComparator (m_config->keyId (),
														  	  m_config->ordering ()));
	for (FlatVector::const_iterator i = sorted.begin ();
		 i != sorted.end ();
		 i++)
	{ (*i)->setRank (rank++); }
	
	if (m_config->doDemangle () || m_config->useGdb ())
	{
		if (m_config->verbose ())
			std::cerr << "Resolving symbols" << std::endl;
		// TODO: Enable symremap
		//symremap (prof, m_config->useGdb (), m_config->doDemangle ());
	}
	
	if (m_config->verbose ())
		std::cerr << "Generating report\n" << std::endl;
	
	int keyId = m_config->keyId ();
	
	Counter *topCounter = Counter::getCounterInRing (FlatInfo::first ()->COUNTERS, 
											 		 keyId);
	int totals = topCounter->cumulativeCounts ();
	int totfreq = topCounter->cumulativeFreqs ();
	
	typedef std::vector <MainGProfRow *> CumulativeSortedTable;
	typedef CumulativeSortedTable FinalTable;
	typedef std::vector <MainGProfRow *> SelfSortedTable;
	
	FinalTable table;
	
	for (FlatVector::const_iterator i = sorted.begin ();
		 i != sorted.end ();
		 i++)
	{
		FlatInfo *info = *i;
		// FIXME: Can we assume we have only the counters on the nodes 
		// 		  which have cumulativeCounts () > 0?
		Counter *counter = Counter::getCounterInRing ((*i)->COUNTERS, keyId);
		ASSERT (counter);
		if (!counter->cumulativeCounts ())
			continue;
		// Sort calling and called functions.
		// FIXME: should sort callee and callers
		GProfMainRowBuilder builder (info, keyId, totals, totfreq);
		
		for (FlatInfo::ReferenceList::const_iterator j = info->CALLERS.begin ();
			 j != info->CALLERS.end ();
			 j++)
		{ builder.addCaller (*j); }

		for (FlatInfo::ReferenceList::const_iterator j = info->CALLS.begin ();
			 j != info->CALLS.end ();
			 j++)
		{ builder.addCallee (*j); }
		table.push_back (builder.build ()); 
	}

	SelfSortedTable selfSortedTable;
	
	for (FinalTable::const_iterator i = table.begin ();
		 i != table.end ();
		 i++)
	{
		selfSortedTable.push_back (*i);
	}
	
	sort (selfSortedTable.begin (), selfSortedTable.end (), SortRowBySelf ());
		
	if (m_config->outputType () == Configuration::TEXT)
	{
		bool showpaths = m_config->showPaths ();
		bool showcalls = m_config->showCalls ();
		bool showlibs = m_config->showLib ();
		std::cout << "Counter: " << m_config->key () << std::endl;
		int maxcnt = max (8,
						  max (thousands (totals).size (), 
						       thousands (totfreq).size ()));
		bool isPerfTicks = m_config->key () == "PERF_TICKS";
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
			printf ("%*s  ", maxval, thousands (row.CUM).c_str ());
			PrintIf p (maxcnt);
			p (showpaths, thousands (row.CUMALL.COUNTS));
			p (showcalls, thousands (row.CUMALL.FREQS));
			printf ("%s [%d]", row.NAME.c_str (), row.RANK);
			if (showlibs) { std::cerr << row.FILENAME; }
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
			printf ("%*s  ", maxval, thousands (row.SELF).c_str ());
			PrintIf p (maxcnt);
			p (showcalls, thousands (row.SELFALL.FREQS));
			p (showpaths, thousands (row.SELFALL.FREQS));
			printf ("%s [%d]", row.NAME.c_str (), row.RANK);
			if (showlibs) { std::cout << row.FILENAME; }
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
			int divlen = 34+3*maxval 
						 + (showcalls ? 1 : 0)*(2*maxcnt+5) 
						 + (showpaths ? 1 : 0)*(2*maxcnt+5);
			
			std::cout << "\n";
			for (int x = 0 ; x < ((1+divlen)/2); x++) {printf ("- "); }
			std::cout << std::endl;

			MainGProfRow &mainRow = **i;

			if ((mainRow.RANK % 10) == 1)
			{	
				printf ("%-8s", "Rank");
				printf ("%% total  ");
				(AlignedPrinter (maxcnt)) ("Self");
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
				valfmt (thousands (row.SELF_COUNTS), thousands (row.CHILDREN_COUNTS));
				printf ("  ");
				if (showcalls) 
				{ 
					cntfmt (thousands (row.SELF_CALLS), 
							thousands (row.TOTAL_CALLS));
				}
				if (showpaths)
				{
					cntfmt (thousands (row.SELF_PATHS), 
				            thousands (row.TOTAL_PATHS));
				}
				printf ("    %s [%d]", row.NAME.c_str (), row.RANK);
				if (showlibs) {std::cout << "  " << row.FILENAME; }
				std::cout << "\n";
			}
			
			char rankBuffer[256];
			sprintf (rankBuffer, "[%d]", mainRow.RANK);
			printf ("%-8s", rankBuffer);
			printf ("%7.1f  ", mainRow.PCT);
			(AlignedPrinter (maxval)) (thousands (mainRow.CUM));
			valfmt (thousands (mainRow.SELF), thousands (mainRow.KIDS));
			printf ("  ");
			if (showcalls) 
			{ (AlignedPrinter (maxcnt)) (thousands (mainRow.CUMALL.FREQS));
				  (AlignedPrinter (maxcnt)) (""); printf (" "); }
			if (showpaths)
				{ (AlignedPrinter (maxcnt)) (thousands (mainRow.CUMALL.COUNTS));
				  (AlignedPrinter (maxcnt)) (""); printf (" "); }
			
			std::cout << mainRow.NAME;
			
			if (showlibs) { std::cout << mainRow.FILENAME; }
			std::cout << "\n";
			
			for (MainGProfRow::Calls::const_iterator c = mainRow.CALLS.begin ();
				 c != mainRow.CALLS.end ();
				 c++)
			{
				OtherGProfRow &row = **c;
				std::cout << std::string (8, ' ');
				printf ("%7.1f  ", row.PCT);
				std::cout << std::string (maxval, '.') << "  ";
				valfmt (thousands (row.SELF_COUNTS), thousands (row.CHILDREN_COUNTS));
				printf ("  ");
				
				if (showcalls) 
				{ cntfmt (thousands (row.SELF_CALLS), 
						  thousands (row.TOTAL_CALLS)); }
				if (showpaths)
				{
					cntfmt (thousands (row.SELF_PATHS), 
				            thousands (row.TOTAL_PATHS));
				}
				printf ("    %s [%d]", row.NAME.c_str (), row.RANK);
    
				if (showlibs) {std::cout << "  " << row.FILENAME; }
				std::cout << "\n";
			}
		}
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
	std::cerr << *firstFile << std::endl;
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
		if (lat::StringOps::contains ("MEM", m_config->key ()))
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
			m_config->setKey (key);
			if (lat::StringOps::find (key, "MEM_") != -1)
				m_config->filters ().push_back (new MallocFilter ());
			if (key == "MEM_LIVE")
				m_config->filters ().push_back (new IgProfGccPoolAllocFilter ());
			m_config->filters ().push_back (new RemoveIgProfFilter ());
		}
		else if (is ("--value") && left (arg) > 1)
		{
			std::string type = *(arg++);
			if (type == "peak") {m_config->setNormalValue (false);}
			else if (type == "normal") {m_config->setNormalValue (true);}
			else {
				std::cerr << "Unexpected --value argument " << type << std::endl;
			}
		}
		else if (is ("--order", "-o"))
		{
			ASSERT (false);
			// TODO:
			// { $order = $ARGV[1] eq 'ascending' ? -1 : 1; shift (@ARGV); shift (@ARGV); }
		}
		else if (is ("--filter-file", "-F"))
		{
			ASSERT (false);
			// TODO:
		    // { push (@filterfiles, $ARGV[1]); shift (@ARGV); shift (@ARGV); }
		}
		else if (is ("--filter", "-f"))
		{
			ASSERT (false);
			//TODO:
			// push (@userfilters, split(/,/, $ARGV[1])); shift (@ARGV); shift (@ARGV); }
		}
		else if (is ("--no-filter", "-nf"))
		{
			ASSERT (false);
		    // TODO:
			//{ @filters = (); shift (@ARGV); }			
		}
		else if (is ("--list-filters", "-lf"))
		{
			ASSERT (false);
			// TODO:
			// my %filters = map { s/igprof_filter_(.*)_(pre|post)/$1/g; $_ => 1}
			// 	      grep(/^igprof_filter_.*_(pre|post)$/, keys %{::});
			// print "Available filters are: @{[sort keys %filters]}\n";
			// print "Selected filters are: @filters @userfilters\n";
			// exit(0);
		}
		else if (is ("--libs", "-l"))
		{
			ASSERT (false);
			// TODO:
		    // { $showlibs = 1; shift (@ARGV); }
		}
		else if (is ("--callgrind", "-C"))
		{
			ASSERT (false);
			// TODO:
			// { $callgrind = 1; shift (@ARGV); }
		}
		else if (is ("--xml", "-x"))
		{
			ASSERT (false);
			// TODO:
			// { $output = "xml"; shift (@ARGV); }
		}
		else if (is ("--html", "-h"))
		{
			ASSERT (false);
			// TODO:
			// { $output = "html"; shift (@ARGV); }
		}
		else if (is ("--text", "-t"))
		{ m_config->setOutputType (Configuration::TEXT); }
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

int 
main (int argc, const char **argv)
{
	lat::Signal::handleFatal (argv [0]);
	IgProfAnalyzerApplication *app = new IgProfAnalyzerApplication (argc, argv);
	app->run ();
}
