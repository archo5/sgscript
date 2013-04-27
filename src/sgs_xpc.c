

#ifdef WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOSERVICE
#  define NOUSER
#  define NONLS
#  define NOWH
#  define NOMCX
#  define NOMINMAX
#  include <windows.h>
#elif defined(__linux__)
#  include <dlfcn.h>
#endif

#include "sgs_xpc.h"


int sgs_GetProcAddress( const char* file, const char* proc, void** out )
{
#ifdef WIN32
	HMODULE mod;

	mod = LoadLibraryA( file );
	if( !mod )
		return SGS_XPC_NOFILE;

	*out = (void*) GetProcAddress( mod, proc );
	if( !*out )
		return SGS_XPC_NOPROC;

	return 0;

#elif defined(__linux__)
	void* lib = dlopen( file, RTLD_NOW );
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

