

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOSERVICE
#  define NOUSER
#  define NONLS
#  define NOWH
#  define NOMCX
#  define NOMINMAX
#  include <windows.h>
#  define SGS_MAX_PATH 4096
#elif defined(__linux__)
#  include <dlfcn.h>
#  define SGS_MAX_PATH PATH_MAX
#endif

#include "sgs_xpc.h"


int sgs_GetProcAddress( const char* file, const char* proc, void** out )
{
#ifdef WIN32
	HMODULE mod;
	UINT pe;
	char abspath[ SGS_MAX_PATH + 1 ];
	DWORD ret;
	
	ret = GetFullPathNameA( file, SGS_MAX_PATH, abspath, NULL );
	if( ret > 0 && ret < SGS_MAX_PATH )
	{
		abspath[ SGS_MAX_PATH ] = 0;
		file = abspath;
	}

	pe = SetErrorMode( SEM_FAILCRITICALERRORS );
	mod = LoadLibraryA( file );
	SetErrorMode( pe );
	if( !mod )
		return SGS_XPC_NOFILE;

	*out = (void*) GetProcAddress( mod, proc );
	if( !*out )
		return SGS_XPC_NOPROC;

	return 0;

#elif defined(__linux__)
	void* lib;
	char* abspath[ SGS_MAX_PATH + 1 ];
	
	if( realpath( file, abspath ) )
	{
		abspath[ SGS_MAX_PATH ] = 0;
		file = abspath;
	}
	
	lib = dlopen( file, RTLD_NOW );
	if( !lib )
		return SGS_XPC_NOFILE;

	*out = (void*) dlsym( lib, proc );
	if( !*out )
		return SGS_XPC_NOPROC;

	return 0;

#else
	return SGS_XPC_NOTSUP;
	
#endif
}

