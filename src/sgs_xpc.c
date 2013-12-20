

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOSERVICE
#  define NOUSER
#  define NOWH
#  define NOMCX
#  define NOMINMAX
#  include <windows.h>
#  define SGS_MAX_PATH 4096
#elif defined(__linux__)
#  include <dlfcn.h>
#  include <unistd.h>
#  include <stdlib.h>
#  ifndef PATH_MAX
#    define PATH_MAX 4096
#  endif
#  define SGS_MAX_PATH PATH_MAX
#endif

#include "sgs_xpc.h"


int sgs_GetProcAddress( const char* file, const char* proc, void** out )
{
#ifdef WIN32
	HMODULE mod;
	UINT pe;
	WCHAR widepath[ SGS_MAX_PATH + 1 ];
	WCHAR abspath[ SGS_MAX_PATH + 1 ];
	WCHAR* pathstr;
	DWORD ret;
	
	ret = MultiByteToWideChar( CP_UTF8, 0, file, -1, widepath, SGS_MAX_PATH );
	if( ret == 0 )
		return SGS_XPC_NOFILE;
	widepath[ SGS_MAX_PATH ] = 0;
	
	pathstr = widepath;
	ret = GetFullPathNameW( widepath, SGS_MAX_PATH, abspath, NULL );
	if( ret > 0 && ret < SGS_MAX_PATH )
	{
		abspath[ SGS_MAX_PATH ] = 0;
		pathstr = abspath;
	}

	pe = SetErrorMode( SEM_FAILCRITICALERRORS );
	mod = LoadLibraryW( pathstr );
	SetErrorMode( pe );
	ret = GetLastError();
	if( !mod )
	{
		if( ret == ERROR_MOD_NOT_FOUND )
			return SGS_XPC_NOFILE;
		return SGS_XPC_LDFAIL;
	}

	*out = (void*) GetProcAddress( mod, proc );
	if( !*out )
		return SGS_XPC_NOPROC;

	return 0;

#elif defined(__linux__)
	void* lib;
	char abspath[ SGS_MAX_PATH + 1 ];
	
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

