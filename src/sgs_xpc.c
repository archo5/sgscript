

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#define NOSERVICE
#define NOUSER
#define NONLS
#define NOWH
#define NOMCX
#define NOMINMAX
#include <windows.h>
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
#endif

	return SGS_XPC_NOTSUP;
}

