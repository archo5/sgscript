

#include "sgscript.h"
#include "sgs_proc.h"


#define FLAG( where, which ) (((where)&(which))!=0)

#define STDLIB_WARN( warn ) { sgs_Printf( C, SGS_WARNING, -1, warn ); return 0; }
#define STDLIB_ERROR( err ) { sgs_Printf( C, SGS_ERROR, -1, err ); return 0; }

#define CHKARGS( cnt ) \
	if( sgs_StackSize( C ) != cnt ) { \
		sgs_Printf( C, SGS_ERROR, -1, "Incorrect number of arguments, need " #cnt "." ); \
		return 0; }


typedef union intreal_s
{
	sgs_Integer i;
	sgs_Real r;
}
intreal_t;

static SGS_INLINE int stdlib_is_numericstring( const char* str, int32_t size )
{
	intreal_t out;
	const char* ostr = str;
	return util_strtonum( &str, str + size, &out.i, &out.r ) != 0 && str != ostr;
}


static SGS_INLINE int stdlib_isoneof( char c, char* from, int fsize )
{
	char* fend = from + fsize;
	while( from < fend )
	{
		if( c == *from )
			return TRUE;
		from++;
	}
	return FALSE;
}


int stdlib_tobool( SGS_CTX, int arg, int* out );
int stdlib_toint( SGS_CTX, int arg, sgs_Integer* out );
int stdlib_tostring( SGS_CTX, int arg, char** out, sgs_Integer* size );

