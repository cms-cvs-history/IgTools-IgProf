#ifndef MEM_PROF_LIB_CONFIG_H
# define MEM_PROF_LIB_CONFIG_H

//<<<<<< INCLUDES                                                       >>>>>>

//<<<<<< PUBLIC DEFINES                                                 >>>>>>

/** @def MEM_PROF_LIB_API
  @brief A macro that controls how entities of this shared library are
         exported or imported on Windows platforms (the macro expands
         to nothing on all other platforms).  The definitions are
         exported if #MEM_PROF_LIB_BUILD_DLL is defined, imported
         if #MEM_PROF_LIB_BUILD_ARCHIVE is not defined, and left
         alone if latter is defined (for an archive library build).  */

/** @def MEM_PROF_LIB_BUILD_DLL
  @brief Indicates that the header is included during the build of
         a shared library of this package, and all entities marked
	 with #MEM_PROF_LIB_API should be exported.  */

/** @def MEM_PROF_LIB_BUILD_ARCHIVE
  @brief Indicates that this library is or was built as an archive
         library, not as a shared library.  Lack of this indicates
         that the header is included during the use of a shared
         library of this package, and all entities marked with
         #MEM_PROF_LIB_API should be imported.  */

# ifndef MEM_PROF_LIB_API
#  ifdef _WIN32
#    if defined MEM_PROF_LIB_BUILD_DLL
#      define MEM_PROF_LIB_API __declspec(dllexport)
#    elif ! defined MEM_PROF_LIB_BUILD_ARCHIVE
#      define MEM_PROF_LIB_API __declspec(dllimport)
#    endif
#  endif
# endif

# ifndef MEM_PROF_LIB_API
#  define MEM_PROF_LIB_API
# endif

#endif // MEM_PROF_LIB_CONFIG_H
