

#include <stdlib.h>
#include <errno.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOSERVICE
#  define NOUSER
#  define NOWH
#  define NOMCX
#  define NOMINMAX
#  include <windows.h>
#  define SGS_MAX_PATH 4096
#else
#  include <string.h>
#  include <dlfcn.h>
#  include <sys/stat.h>
#  include <unistd.h>
#  ifndef PATH_MAX
#    define PATH_MAX 4096
#  endif
#  define SGS_MAX_PATH PATH_MAX
#endif

#include "sgs_xpc.h"


int sgsXPC_GetProcAddress( const char* file, const char* proc, void** out )
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
		if( ret == ERROR_BAD_EXE_FORMAT )
			return SGS_XPC_NOTLIB;
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


char* sgsXPC_GetCurrentDirectory()
{
#ifdef WIN32
	DWORD buf16_size, buf8_size;
	WCHAR* buf16;
	char* buf8;
	
	buf16_size = GetCurrentDirectoryW( 0, NULL );
	if( buf16_size == 0 )
	{
		errno = EACCES;
		return NULL;
	}
	buf16 = (WCHAR*) malloc( sizeof(WCHAR) * buf16_size );
	if( GetCurrentDirectoryW( buf16_size, buf16 ) == 0 )
	{
		free( buf16 );
		errno = EACCES;
		return NULL;
	}
	
	buf8_size = WideCharToMultiByte( CP_UTF8, 0, buf16, buf16_size, NULL, 0, NULL, NULL );
	if( buf8_size == 0 )
	{
		free( buf16 );
		errno = EACCES;
		return NULL;
	}
	buf8 = (char*) malloc( buf8_size );
	if( WideCharToMultiByte( CP_UTF8, 0, buf16, buf16_size, buf8, buf8_size, NULL, NULL ) == 0 )
	{
		free( buf16 );
		free( buf8 );
		errno = EACCES;
		return NULL;
	}
	free( buf16 );
	
	return buf8;
	
#else
	char stack_buf[ SGS_MAX_PATH ];
	char* buf;
	int cur_size = SGS_MAX_PATH, max_size = SGS_MAX_PATH * 10;
	
	buf = getcwd( NULL, 0 );
	if( buf )
		return buf;
	
	errno = 0;
	buf = stack_buf;
	for(;;)
	{
		if( getcwd( buf, cur_size ) )
		{
			if( buf == stack_buf )
			{
				size_t len = strlen( buf ) + 1;
				buf = (char*) malloc( len );
				memcpy( buf, stack_buf, len );
			}
			return buf;
		}
		if( errno != ERANGE || cur_size >= max_size )
		{
			if( buf != stack_buf )
				free( buf );
			return NULL;
		}
		cur_size *= 2;
		if( buf == stack_buf )
			buf = malloc( cur_size );
		else
			buf = realloc( buf, cur_size );
		if( !buf )
		{
			errno = ENOMEM;
			return NULL;
		}
	}
	return NULL;
	
#endif
}

int sgsXPC_SetCurrentDirectory( char* path )
{
#ifdef WIN32
	size_t path_len;
	int buf16_size;
	WCHAR stack_buf16[ SGS_MAX_PATH ];
	WCHAR* buf16;
	BOOL result;
	
	buf16 = stack_buf16;
	path_len = strlen( path );
	buf16_size = MultiByteToWideChar( CP_UTF8, 0, path, path_len + 1, NULL, 0 );
	if( buf16_size == 0 )
	{
		errno = EACCES;
		return -1;
	}
	if( buf16_size > SGS_MAX_PATH )
		buf16 = (WCHAR*) malloc( sizeof(WCHAR) * buf16_size );
	if( MultiByteToWideChar( CP_UTF8, 0, path, path_len + 1, buf16, buf16_size ) == 0 )
	{
		free( buf16 );
		errno = EACCES;
		return -1;
	}
	result = SetCurrentDirectoryW( buf16 );
	if( buf16 != stack_buf16 )
		free( buf16 );
	if( !result )
	{
		DWORD error;
		error = GetLastError();
		if( error == ERROR_ACCESS_DENIED )
			errno = EACCES;
		else
			errno = ENOENT;
	}
	return result ? 0 : -1;
	
#else
	return chdir( path );
	
#endif
}

char* sgsXPC_GetModuleFileName()
{
#ifdef WIN32
	WCHAR buf16[ 32768 ];
	DWORD buf16_size, buf8_size;
	char* buf8;
	
	buf16_size = GetModuleFileNameW( NULL, buf16, 32768 );
	if( buf16_size == 0 || buf16_size >= 32768 )
	{
		errno = EACCES;
		return NULL;
	}
	buf16[ buf16_size ] = 0;
	
	buf8_size = WideCharToMultiByte( CP_UTF8, 0, buf16, buf16_size + 1, NULL, 0, NULL, NULL );
	if( buf8_size == 0 )
	{
		errno = EACCES;
		return NULL;
	}
	buf8 = (char*) malloc( buf8_size );
	if( WideCharToMultiByte( CP_UTF8, 0, buf16, buf16_size + 1, buf8, buf8_size, NULL, NULL ) == 0 )
	{
		free( buf8 );
		errno = EACCES;
		return NULL;
	}
	
	return buf8;
	
#else
	char stack_buf[ SGS_MAX_PATH ];
	char* buf;
	int cur_size = SGS_MAX_PATH, max_size = SGS_MAX_PATH * 10;
	
	errno = 0;
	buf = stack_buf;
	for(;;)
	{
		int res = readlink( "/proc/self/exe", buf, cur_size );
		if( res >= 0 && res < cur_size )
		{
			if( buf == stack_buf )
			{
				size_t len = strlen( buf ) + 1;
				buf = (char*) malloc( len );
				memcpy( buf, stack_buf, len );
			}
			return buf;
		}
		if( errno != ERANGE || cur_size >= max_size )
		{
			if( buf != stack_buf )
				free( buf );
			return NULL;
		}
		cur_size *= 2;
		if( buf == stack_buf )
			buf = malloc( cur_size );
		else
			buf = realloc( buf, cur_size );
		if( !buf )
		{
			errno = ENOMEM;
			return NULL;
		}
	}
	return NULL;
	
#endif
}

