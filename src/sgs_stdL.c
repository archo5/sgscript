

/* for the constants... */
#define _USE_MATH_DEFINES
#undef __STRICT_ANSI__
#include <math.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>
#include <float.h>
#include <locale.h>

#define SGS_INTERNAL
#define SGS_REALLY_INTERNAL
#define SGS_USE_FILESYSTEM

#include "sgs_int.h"
#include "sgs_regex.h"

#define FN( x ) { #x, sgsstd_##x }
#define FLAG( a, b ) (((a)&(b))!=0)
#define STREQ( a, b ) (0==strcmp(a,b))
#define STDLIB_INFO( info ) return sgs_Printf( C, SGS_INFO, info );
#define STDLIB_WARN( warn ) return sgs_Printf( C, SGS_WARNING, warn );


SGS_DECLARE sgs_ObjCallback sgsstd_file_functable[ 7 ];


/* path helper functions */

#if 0
static int32_t findlastof( const char* str, int32_t len, const char* of )
{
	const char* ptr = str + len;
	while( ptr-- > str )
	{
		const char* pof = of;
		while( *pof )
		{
			if( *pof == *ptr )
				return ptr - str;
			pof++;
		}
	}
	return -1;
}

static int path_replast( SGS_CTX, int from, int with )
{
	sgs_SizeVal size, pos;
	char* buf = sgs_ToStringBuf( C, from, &size );
	if( !buf || ( pos = findlastof( buf, size, "/\\" ) ) < 0 )
	{
		return !sgs_PushItem( C, with );
	}
	sgs_PushStringBuf( C, buf, pos + 1 );
	return !( sgs_PushItem( C, with ) || sgs_StringConcat( C ) );
}
#endif


/*  - - - - - - - - - -
	
	F O R M A T T I N G

*/

static void fmt_pack_stats(
	SGS_CTX, const char* str, sgs_SizeVal size,
	sgs_SizeVal* outnumitems, sgs_SizeVal* outnumbytes )
{
	sgs_SizeVal numbytes = 0, numitems = 0, first = 1, mult = 0;
	const char* sb = str, *strend = str + size;
	while( str < strend )
	{
		char c = *str++;
		switch( c )
		{
		/* multipliers */
		case '0': case '1': case '2': case '3':
		case '4': case '5': case '6': case '7':
		case '8': case '9':
			mult *= 10;
			mult += c - '0';
			break;
		/* endianness */
		case '=': case '<': case '>': case '@':
		/* sign */
		case '+': case '-':
			mult = 0;
			break;
		/* types */
		case 'c': numbytes += mult ? mult : 1; numitems += mult ? mult : 1; mult = 0; break;
		case 'w': numbytes += ( mult ? mult : 1 ) * 2; numitems += mult ? mult : 1; mult = 0; break;
		case 'l': case 'f': numbytes += ( mult ? mult : 1 ) * 4; numitems += mult ? mult : 1; mult = 0; break;
		case 'q': case 'd': numbytes += ( mult ? mult : 1 ) * 8; numitems += mult ? mult : 1; mult = 0; break;
		case 'p': numbytes += ( mult ? mult : 1 ) * (sgs_SizeVal) sizeof( size_t ); numitems += mult ? mult : 1; mult = 0; break;
		case 's':
			numbytes += mult ? mult : 1;
			numitems++;
			mult = 0;
			break;
		/* misc. */
		case 'x':
			numbytes += mult ? mult : 1;
			mult = 0;
			break;
		case ' ': case '\t': case '\n': case '\r': break;
		default:
			if( first )
			{
				first = 0;
				sgs_Printf( C, SGS_WARNING, "invalid character"
				" at position %d (there might be more)", ( str - sb + 1 ) );
			}
			mult = 0;
			break;
		}
	}
	
	if( outnumitems ) *outnumitems = numitems;
	if( outnumbytes ) *outnumbytes = numbytes;
}

static sgs_SizeVal fmt_pack( SGS_CTX,
	const char* str, sgs_SizeVal size, char* bfr )
{
	int invert = 0;
	sgs_SizeVal si = 1, mult = 0;
	const char* strend = str + size;
	while( str < strend )
	{
		char c = *str++;
		switch( c )
		{
		/* multipliers */
		case '0': case '1': case '2': case '3':
		case '4': case '5': case '6': case '7':
		case '8': case '9':
			mult *= 10;
			mult += c - '0';
			break;
		/* modifiers */
		case '=': invert = 0; mult = 0; break;
		case '<': invert = O32_HOST_ORDER != O32_LITTLE_ENDIAN; mult = 0; break;
		case '>': invert = O32_HOST_ORDER != O32_BIG_ENDIAN; mult = 0; break;
		case '@': invert = 1; mult = 0; break;
		case '+': case '-': mult = 0; break;
		/* data */
		case 'c': case 'w': case 'l': case 'q':
		case 'p':
			if( !mult )
				mult = 1;
			while( mult-- > 0 )
			{
				int padsize = 1, off = 0;
				char bb[ 8 ];
				sgs_Int i;
				if( !sgs_ParseInt( C, si, &i ) )
					return si;
				si++;
				if( c == 'w' ) padsize = 2;
				else if( c == 'l' ) padsize = 4;
				else if( c == 'q' ) padsize = 8;
				else if( c == 'p' ) padsize = sizeof( size_t );
				if( O32_HOST_ORDER == O32_BIG_ENDIAN )
					off = 7 - padsize;
				memcpy( bb, ((char*)&i) + off, (size_t) padsize );
				if( invert )
				{
					int a, b;
					for( a = 0, b = padsize - 1; a < b; a++, b-- )
					{
						char bbt = bb[ a ];
						bb[ a ] = bb[ b ];
						bb[ b ] = bbt;
					}
				}
				memcpy( bfr, bb, (size_t) padsize );
				bfr += padsize;
			}
			mult = 0;
			break;
		case 'f': case 'd':
			if( !mult )
				mult = 1;
			while( mult-- > 0 )
			{
				sgs_Real f;
				if( !sgs_ParseReal( C, si, &f ) )
					return si;
				si++;
				if( c == 'f' )
				{
					char bb[ 4 ];
					float f32 = (float) f;
					memcpy( bb, &f32, 4 );
					if( invert )
					{
						char bbt;
						bbt = bb[ 0 ]; bb[ 0 ] = bb[ 3 ]; bb[ 3 ] = bbt;
						bbt = bb[ 1 ]; bb[ 1 ] = bb[ 2 ]; bb[ 2 ] = bbt;
					}
					memcpy( bfr, bb, 4 );
					bfr += 4;
				}
				else
				{
					char bb[ 8 ];
					memcpy( bb, &f, 8 );
					if( invert )
					{
						char bbt;
						bbt = bb[ 0 ]; bb[ 0 ] = bb[ 7 ]; bb[ 7 ] = bbt;
						bbt = bb[ 1 ]; bb[ 1 ] = bb[ 6 ]; bb[ 6 ] = bbt;
						bbt = bb[ 2 ]; bb[ 2 ] = bb[ 5 ]; bb[ 5 ] = bbt;
						bbt = bb[ 3 ]; bb[ 3 ] = bb[ 4 ]; bb[ 4 ] = bbt;
					}
					memcpy( bfr, bb, 8 );
					bfr += 8;
				}
			}
			mult = 0;
			break;
		case 's':
			{
				char* astr;
				sgs_SizeVal asize;
				if( !sgs_ParseString( C, si, &astr, &asize ) )
					return si;
				si++;
				if( mult < 1 )
					mult = 1;
				if( asize > mult )
					asize = mult;
				memcpy( bfr, astr, (size_t) asize );
				bfr += asize;
				while( asize < mult )
				{
					*bfr++ = '\0';
					asize++;
				}
			}
			mult = 0;
			break;
		case 'x':
			if( !mult )
				mult = 1;
			while( mult-- > 0 )
				*bfr++ = '\0';
			mult = 0;
			break;
		case ' ': case '\t': case '\n': case '\r': break;
		default:
			mult = 0;
			break;
		}
	}
	return si;
}

static int fmt_unpack( SGS_CTX, const char* str,
	sgs_SizeVal size, const char* data, sgs_SizeVal datasize )
{
	int invert = 0, sign = 0;
	sgs_SizeVal si = 0, mult = 0;
	const char* strend = str + size, *dataend = data + datasize;
	while( str < strend )
	{
		char c = *str++;
		switch( c )
		{
		/* multipliers */
		case '0': case '1': case '2': case '3':
		case '4': case '5': case '6': case '7':
		case '8': case '9':
			mult *= 10;
			mult += c - '0';
			break;
		/* modifiers */
		case '=': invert = 0; mult = 0; break;
		case '<': invert = O32_HOST_ORDER != O32_LITTLE_ENDIAN; mult = 0; break;
		case '>': invert = O32_HOST_ORDER != O32_BIG_ENDIAN; mult = 0; break;
		case '@': invert = 1; mult = 0; break;
		case '+': sign = 0; mult = 0; break;
		case '-': sign = 1; mult = 0; break;
		/* data */
		case 'c': case 'w': case 'l': case 'q':
		case 'p':
			if( !mult )
				mult = 1;
			while( mult-- > 0 )
			{
				int padsize = 1, off = 0;
				sgs_Int i = 0;
				char bb[ 8 ];

				if( c == 'w' ) padsize = 2;
				else if( c == 'l' ) padsize = 4;
				else if( c == 'q' ) padsize = 8;
				else if( c == 'p' ) padsize = sizeof( size_t );
				if( O32_HOST_ORDER == O32_BIG_ENDIAN )
					off = 7 - padsize;

				memcpy( bb, data, (size_t) padsize );
				data += padsize;

				if( invert )
				{
					int a, b;
					for( a = 0, b = padsize - 1; a < b; a++, b-- )
					{
						char bbt = bb[ a ];
						bb[ a ] = bb[ b ];
						bb[ b ] = bbt;
					}
				}

				memcpy( ((char*)&i) + off, bb, (size_t) padsize );
				if( sign )
				{
					const sgs_Int SIGN = -1;
#define ESB( i, mask ) { if( i > mask ) i = ( i & mask ) | ( SIGN & ~mask ); }
					if( c == 'c' )      ESB( i, 0x7f )
					else if( c == 'w' ) ESB( i, 0x7fff )
					else if( c == 'l' ) ESB( i, 0x7fffffff )
#undef ESB
				}
				sgs_PushInt( C, i );
				si++;
			}
			mult = 0;
			break;
		case 'f': case 'd':
			if( !mult )
				mult = 1;
			while( mult-- > 0 )
			{
				char bb[ 8 ];
				if( c == 'f' )
				{
					float f32;
					memcpy( bb, data, 4 );
					data += 4;
					if( invert )
					{
						char bbt;
						bbt = bb[ 0 ]; bb[ 0 ] = bb[ 3 ]; bb[ 3 ] = bbt;
						bbt = bb[ 1 ]; bb[ 1 ] = bb[ 2 ]; bb[ 2 ] = bbt;
					}
					memcpy( &f32, bb, 4 );
					sgs_PushReal( C, f32 );
				}
				else
				{
					double f64;
					memcpy( bb, data, 8 );
					data += 8;
					if( invert )
					{
						char bbt;
						bbt = bb[ 0 ]; bb[ 0 ] = bb[ 7 ]; bb[ 7 ] = bbt;
						bbt = bb[ 1 ]; bb[ 1 ] = bb[ 6 ]; bb[ 6 ] = bbt;
						bbt = bb[ 2 ]; bb[ 2 ] = bb[ 5 ]; bb[ 5 ] = bbt;
						bbt = bb[ 3 ]; bb[ 3 ] = bb[ 4 ]; bb[ 4 ] = bbt;
					}
					memcpy( &f64, bb, 8 );
					sgs_PushReal( C, f64 );
				}
				si++;
			}
			mult = 0;
			break;
		case 's':
			{
				if( mult < 1 )
					mult = 1;
				sgs_PushStringBuf( C, data, mult );
				data += mult;
				si++;
			}
			mult = 0;
			break;
		case 'x':
			if( !mult )
				mult = 1;
			data += mult;
			mult = 0;
			break;
		case ' ': case '\t': case '\n': case '\r': break;
		default:
			mult = 0;
			break;
		}
	}
	sgs_BreakIf( data > dataend );
	UNUSED( dataend );
	return si;
}

static int sgsstd_fmt_pack( SGS_CTX )
{
	char* str;
	sgs_SizeVal size, numitems = 0, numbytes = 0, ret;
	
	SGSFN( "fmt_pack" );

	if( !sgs_LoadArgs( C, "m", &str, &size ) )
		return 0;

	fmt_pack_stats( C, str, size, &numitems, &numbytes );
	if( sgs_StackSize( C ) < numitems + 1 )
	{
		sgs_Printf( C, SGS_WARNING, 
			"expected at least %d arguments, got %d\n",
			numitems + 1, sgs_StackSize( C ) );
		return 0;
	}

	{
		sgs_PushStringBuf( C, NULL, numbytes );
		ret = fmt_pack( C, str, size, sgs_GetStringPtr( C, -1 ) ) - 1;
		if( ret != numitems )
			sgs_Printf( C, SGS_WARNING, "error in arguments, could not read all" );
		return ret == numitems;
	}
}

static int sgsstd_fmt_pack_count( SGS_CTX )
{
	char* str;
	sgs_SizeVal size, numitems = 0;
	
	SGSFN( "fmt_pack_count" );
	
	if( !sgs_LoadArgs( C, "m", &str, &size ) )
		return 0;
	
	fmt_pack_stats( C, str, size, &numitems, NULL );
	sgs_PushInt( C, numitems );
	return 1;
}

static int sgsstd_fmt_unpack( SGS_CTX )
{
	sgs_SizeVal bytes = 0, ret;
	char* str, *data;
	sgs_SizeVal size, datasize;
	
	SGSFN( "fmt_unpack" );
	
	if( !sgs_LoadArgs( C, "mm", &str, &size, &data, &datasize ) )
		return 0;
	
	fmt_pack_stats( C, str, size, NULL, &bytes );
	if( bytes > datasize )
		STDLIB_WARN( "not enough data to successfully unpack" )
	ret = fmt_unpack( C, str, size, data, datasize );
	if( ret < 0 || sgs_PushArray( C, ret ) != SGS_SUCCESS )
		return 0;
	return 1;
}

static int sgsstd_fmt_pack_size( SGS_CTX )
{
	char* str;
	sgs_SizeVal size, bytes = 0;
	
	SGSFN( "fmt_pack_size" );
	
	if( !sgs_LoadArgs( C, "m", &str, &size ) )
		return 0;
	
	fmt_pack_stats( C, str, size, NULL, &bytes );
	sgs_PushInt( C, bytes );
	return 1;
}


static const char* b64_table =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef"
	"ghijklmnopqrstuvwxyz0123456789+/";

static int sgsstd_fmt_base64_encode( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	
	SGSFN( "fmt_base64_encode" );

	if( !sgs_LoadArgs( C, "m", &str, &size ) )
		return 0;

	{
		MemBuf B = membuf_create();
		char* strend = str + size;
		while( str < strend - 2 )
		{
			int merged = (str[0]<<16) | (str[1]<<8) | (str[2]);
			char bb[ 4 ];
			{
				bb[0] = b64_table[ (merged>>18) & 0x3f ];
				bb[1] = b64_table[ (merged>>12) & 0x3f ];
				bb[2] = b64_table[ (merged>>6 ) & 0x3f ];
				bb[3] = b64_table[ (merged    ) & 0x3f ];
			}
			membuf_appbuf( &B, C, bb, 4 );
			str += 3;
		}
		/* last bytes */
		if( str < strend )
		{
			char bb[ 4 ];
			int merged = str[0]<<16;
			if( str < strend - 1 )
				merged |= str[1]<<8;

			bb[ 0 ] = b64_table[ (merged>>18) & 0x3f ];
			bb[ 1 ] = b64_table[ (merged>>12) & 0x3f ];
			bb[ 2 ] = str < strend-1 ? b64_table[ (merged>>6) & 0x3f ] : '=';
			bb[ 3 ] = '=';
			membuf_appbuf( &B, C, bb, 4 );
		}
		if( B.size > 0x7fffffff )
		{
			membuf_destroy( &B, C );
			STDLIB_WARN( "generated more string data than allowed to store" );
		}
		sgs_PushStringBuf( C, B.ptr, (sgs_SizeVal) B.size );
		membuf_destroy( &B, C );
		return 1;
	}
}

static SGS_INLINE int findintable( const char* ct, char c )
{
	int p = 0;
	while( *ct )
	{
		if( *ct == c )
			return p;
		ct++;
		p++;
	}
	return -1;
}

static int sgsstd_fmt_base64_decode( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	
	SGSFN( "fmt_base64_decode" );

	if( !sgs_LoadArgs( C, "m", &str, &size ) )
		return 0;

	{
		MemBuf B = membuf_create();
		char* beg = str;
		char* strend = str + size;
		while( str < strend - 3 )
		{
			char bb[ 3 ];
			int merged;
			int e = 0, i1, i2, i3 = 0, i4 = 0, no = 0;
			if( str[3] == '=' ){ no = 1; }
			if( no && str[2] == '=' ){ no = 2; }
			i1 = findintable( b64_table, str[0] );
			i2 = findintable( b64_table, str[1] );
			if( no<2 ) i3 = findintable( b64_table, str[2] );
			if( no<1 ) i4 = findintable( b64_table, str[3] );
#define warnbyte( pos ) sgs_Printf( C, SGS_WARNING, \
	"fmt_base64_decode() - wrong byte value at position %d", pos );
			if( i1 < 0 ){ warnbyte( str-beg+1 ); e = 1; }
			else if( i2 < 0 ){ warnbyte( str-beg+2 ); e = 1; }
			else if( i3 < 0 ){ warnbyte( str-beg+3 ); e = 1; }
			else if( i4 < 0 ){ warnbyte( str-beg+4 ); e = 1; }
#undef warnbyte
			if( e )
			{
				membuf_destroy( &B, C );
				return 0;
			}
			merged = (i1<<18) | (i2<<12) | (i3<<6) | (i4);
			bb[ 0 ] = (char)( (merged>>16) & 0xff );
			bb[ 1 ] = (char)( (merged>>8) & 0xff );
			bb[ 2 ] = (char)( merged & 0xff );
			membuf_appbuf( &B, C, bb, (size_t) ( 3 - no ) );
			str += 4;
			if( no )
				break;
		}
		if( str < strend )
			sgs_Printf( C, SGS_WARNING, "extra bytes detected and ignored" );
		/* WP: generated string is 1/(ceil(n/3)*4) of original in length (less) */
		sgs_PushStringBuf( C, B.ptr, (sgs_SizeVal) B.size );
		membuf_destroy( &B, C );
		return 1;
	}
}


struct fmtspec
{
	char* end;
	sgs_SizeVal padcnt;
	sgs_SizeVal prec;
	int padrgt;
	char padchr;
	char type;
};

static int parse_fmtspec( struct fmtspec* out, char* fmt, char* fmtend )
{
	if( fmt >= fmtend ) return 0;

	out->padcnt = 0;
	out->prec = -1;
	out->padrgt = 0;
	out->padchr = ' ';
	out->type = *fmt++;

	if( out->type == '{' )
	{
		out->end = fmt;
		return 1;
	}

	if( !isoneof( out->type, "bodxXfgGeEsc{" ) )
		return 0;

	if( fmt >= fmtend ) return 0;

	while( fmt < fmtend && *fmt >= '0' && *fmt <= '9' )
	{
		out->padcnt *= 10;
		out->padcnt += *fmt++ - '0';
	}

	if( *fmt == '.' )
	{
		out->prec = 0;
		fmt++;
		while( fmt < fmtend && *fmt >= '0' && *fmt <= '9' )
		{
			out->prec *= 10;
			out->prec += *fmt++ - '0';
		}
	}

	if( *fmt == 'r' )
	{
		fmt++;
		out->padrgt = 1;
	}

	if( fmt < fmtend-1 && *fmt == 'p' )
	{
		fmt++;
		out->padchr = *fmt++;
	}

	if( fmt >= fmtend || *fmt != '}' ) return 0;
	out->end = ++fmt;
	return 1;
}

static void _padbuf( MemBuf* B, SGS_CTX, char pc, int cnt )
{
	while( cnt --> 0 )
		membuf_appchr( B, C, pc );
}

static int commit_fmtspec( SGS_CTX, MemBuf* B, struct fmtspec* F, int* psi )
{
	switch( F->type )
	{
	case 'b': case 'o': case 'd': case 'x': case 'X':
		{
			static const char* hextbl = "0123456789abcdef0123456789ABCDEF";
			const char* tbl = hextbl;
			int radix, size, i, sign = 0;
			sgs_Int I;
			if( !sgs_ParseInt( C, (*psi)++, &I ) )
				goto error;

			if( F->type == 'b' ) radix = 2;
			else if( F->type == 'o' ) radix = 8;
			else if( F->type == 'd' ) radix = 10;
			else radix = 16;

			if( F->type == 'X' )
				tbl += 16;
			
			if( I < 0 )
			{
				sign = 1;
				I = -I;
			}
			size = 1 + (int) floor( log( (double) MAX(1,I) ) / log( (double) radix ) ) + sign;

			if( size < F->padcnt && !F->padrgt )
				_padbuf( B, C, F->padchr, F->padcnt - size );
			for( i = size - 1; i >= 0; --i )
			{
				/* WP: conversion does not affect range */
				int cv = (int) ( I / (sgs_Int) pow( (double) radix, i ) ) % radix;
				if( sign )
					membuf_appchr( B, C, '-' );
				membuf_appchr( B, C, tbl[ cv ] );
			}
			if( size < F->padcnt && F->padrgt )
				_padbuf( B, C, F->padchr, F->padcnt - size );
		}
		break;
	case 'f': case 'g': case 'G': case 'e': case 'E':
		{
#define FLT_MAXSIZE (3 + DBL_MANT_DIG - DBL_MIN_EXP)
			char data[ FLT_MAXSIZE + 1 ];
			char tmpl[ 32 ];
			int size;
			sgs_Real R;
			if( !sgs_ParseReal( C, (*psi)++, &R ) )
				goto error;
			if( F->prec < 0 )
				F->prec = 6;
			
			sprintf( tmpl, "%%.%"PRId32"%c", F->prec, F->type );
			snprintf( data, FLT_MAXSIZE, tmpl, R );
			data[ FLT_MAXSIZE ] = 0;
			size = (int) strlen( data );

			if( size < F->padcnt && !F->padrgt )
				_padbuf( B, C, F->padchr, F->padcnt - size );
			membuf_appbuf( B, C, data, (size_t) size );
			if( size < F->padcnt && F->padrgt )
				_padbuf( B, C, F->padchr, F->padcnt - size );
		}
		break;
	case 's': case 'c':
		{
			char* str;
			sgs_SizeVal size;
			if( F->type == 'c' &&
				sgs_Convert( C, (*psi), VTC_STRING ) != SGS_SUCCESS )
				goto error;
			if( !sgs_ParseString( C, (*psi)++, &str, &size ) )
				goto error;
			if( size > F->prec && F->prec >= 0 )
				size = F->prec;
			if( size < F->padcnt && !F->padrgt )
				_padbuf( B, C, F->padchr, F->padcnt - size );
			membuf_appbuf( B, C, str, (size_t) size );
			if( size < F->padcnt && F->padrgt )
				_padbuf( B, C, F->padchr, F->padcnt - size );
		}
		break;
	case '{':
		membuf_appchr( B, C, '{' );
		break;
	default:
		goto error;
	}
	return 1;

error:
	membuf_appbuf( B, C, "#error#", 7 );
	return 0;
}

static int sgsstd_fmt_text( SGS_CTX )
{
	char* fmt, *fmtend;
	sgs_SizeVal fmtsize;
	MemBuf B = membuf_create();
	int numitem = 0, si = 1;
	
	SGSFN( "fmt_text" );
	
	if( !sgs_LoadArgs( C, "m", &fmt, &fmtsize ) )
		return 0;
	
	fmtend = fmt + fmtsize;
	while( fmt < fmtend )
	{
		struct fmtspec F;
		char c = *fmt++;
		if( c == '{' )
		{
			int sio = si, ret = parse_fmtspec( &F, fmt, fmtend );
			numitem++;
			fmt = F.end;
			if( !ret )
			{
				membuf_destroy( &B, C );
				sgs_Printf( C, SGS_WARNING, 
					"parsing error in item %d", numitem );
				return 0;
			}
			if( !commit_fmtspec( C, &B, &F, &si ) )
			{
				sgs_Printf( C, SGS_WARNING, 
					"could not read item %d (arg. %d)", numitem, sio );
			}
		}
		else
			membuf_appchr( &B, C, c );
	}
	
	if( B.size > 0x7fffffff )
	{
		membuf_destroy( &B, C );
		STDLIB_WARN( "generated more string data than allowed to store" );
	}
	/* WP: error condition */
	sgs_PushStringBuf( C, B.ptr, (sgs_SizeVal) B.size );
	membuf_destroy( &B, C );
	return 1;
}


#define FMTSTREAM_STATE_INIT 0
#define FMTSTREAM_STATE_READ 1
#define FMTSTREAM_STATE_END  2

typedef struct sgsstd_fmtstream_s
{
	sgs_Variable source;
	char* buffer;
	sgs_SizeVal streamoff;
	sgs_SizeVal bufsize;
	sgs_SizeVal buffill;
	sgs_SizeVal bufpos;
	int state;
}
sgsstd_fmtstream_t;

SGS_DECLARE sgs_ObjCallback sgsstd_fmtstream_functable[ 5 ];
#define SGSFS_HDR sgsstd_fmtstream_t* hdr = (sgsstd_fmtstream_t*) data->data

#define fs_getreadsize( hdr, lim ) MIN( hdr->buffill - hdr->bufpos, lim )

static int fs_refill( SGS_CTX, sgsstd_fmtstream_t* fs )
{
	int ret, needs = fs->buffill == fs->bufsize || fs->buffill == 0;
	char* str;
	sgs_SizeVal size;
	if( fs->buffill > fs->bufpos )
	{
		/* WP: conversion does not affect range */
		memmove( fs->buffer,
			fs->buffer + fs->bufpos,
			(size_t) ( fs->buffill - fs->bufpos ) );
	}
	fs->buffill -= fs->bufpos;
	fs->streamoff += fs->bufpos;
	fs->bufpos = 0;
	
	if( fs->bufsize > fs->buffill && needs )
	{
		sgs_PushInt( C, fs->bufsize - fs->buffill );
		sgs_PushVariable( C, &fs->source );
		ret = sgs_Call( C, 1, 1 );
		if( ret != SGS_SUCCESS )
			return FALSE;
		if( sgs_ItemType( C, -1 ) == SVT_NULL )
		{
			sgs_Pop( C, 1 );
			fs->state = FMTSTREAM_STATE_END;
			return -1;
		}
		if( !sgs_ParseString( C, -1, &str, &size ) ||
			size > fs->bufsize - fs->buffill )
			return FALSE;
		if( size )
			/* WP: conversion does not affect range */
			memcpy( fs->buffer + fs->bufpos, str, (size_t) size );
		fs->buffill += size;
		fs->state = FMTSTREAM_STATE_READ;
		sgs_Pop( C, 1 );
	}
	return 1;
}


static int sgsstd_fmtstream_destroy( SGS_CTX, sgs_VarObj* data, int unused )
{
	SGSFS_HDR;
	UNUSED( unused );
	sgs_Release( C, &hdr->source );
	sgs_Dealloc( hdr->buffer );
	sgs_Dealloc( hdr );
	return SGS_SUCCESS;
}

#define SGSFS_IHDR( name ) \
	sgsstd_fmtstream_t* hdr; \
	int method_call = sgs_Method( C ); \
	SGSFN( "fmtstream." #name ); \
	if( !sgs_IsObject( C, 0, sgsstd_fmtstream_functable ) )\
		return sgs_ArgErrorExt( C, 0, method_call, "fmtstream", "" ); \
	hdr = (sgsstd_fmtstream_t*) sgs_GetObjectData( C, 0 ); \
	UNUSED( hdr );
/* after this, the counting starts from 1 because of sgs_Method */

static int sgsstd_fmtstreamI_read( SGS_CTX )
{
	sgs_SizeVal numbytes;
	MemBuf B = membuf_create();
	
	SGSFS_IHDR( read )

	if( !sgs_LoadArgs( C, "@>l", &numbytes ) )
		return 0;
	
	if( numbytes )
	{
		while( hdr->state != FMTSTREAM_STATE_END )
		{
			sgs_SizeVal readamt = fs_getreadsize( hdr, numbytes );
			if( readamt )
				/* WP: conversion does not affect range */
				membuf_appbuf( &B, C, hdr->buffer + hdr->bufpos, (size_t) readamt );
			numbytes -= readamt;
			hdr->bufpos += readamt;
			if( numbytes <= 0 )
				break;
			if( !readamt && !fs_refill( C, hdr ) )
			{
				membuf_destroy( &B, C );
				STDLIB_WARN( "unexpected read error" )
			}
		}
	}
	/* WP: conversion does not affect range; string limit */
	sgs_PushStringBuf( C, B.ptr, (sgs_SizeVal) B.size );
	membuf_destroy( &B, C );
	return 1;
}

static int sgsstd_fmtstreamI_getchar( SGS_CTX )
{
	int chr = -1, asint = 0, peek = 0;
	SGSFS_IHDR( getchar )

	if( !sgs_LoadArgs( C, "@>|bb", &peek, &asint ) )
		return 0;
	
	while( hdr->state != FMTSTREAM_STATE_END )
	{
		sgs_SizeVal readamt = fs_getreadsize( hdr, 1 );
		if( !readamt )
		{
			if( !fs_refill( C, hdr ) )
			{
				STDLIB_WARN( "unexpected read error" )
			}
			continue;
		}
		chr = hdr->buffer[ hdr->bufpos ];
		if( !peek )
			hdr->bufpos++;
		break;
	}
	if( asint )
		sgs_PushInt( C, chr );
	else
	{
		char cc = (char) chr;
		sgs_PushStringBuf( C, &cc, 1 );
	}
	return 1;
}

/*
	Character class format
	cclass: ["^"] crlist
	crlist: critem [crlist]
	critem: <any> | <any> "-" <any> | "--"
*/

static int fs_validate_cc( SGS_CTX, const char* str, sgs_SizeVal size )
{
	if( size && *str == '^' )
		size--;
	return !!size;
}

static int fs_check_cc( const char* str, sgs_SizeVal size, char c )
{
	int match = 0, invert = 0;
	const char *strend = str + size;
	if( !size )
		return 1;
	if( *str == '^' )
	{
		invert = 1;
		str++;
	}
	while( str < strend )
	{
		if( str + 1 < strend && *(str+1) == '-' )
		{
			if( *str == '-' )
			{
				match |= *str == c;
				str += 1;
			}
			else if( str + 2 < strend )
			{
				match |= c >= *str && c <= *(str+2);
				str += 2;
			}
		}
		else
		{
			match |= *str == c;
		}
		str++;
		if( match )
			break;
	}
	return match ^ invert;
}

static int _stream_readcc( SGS_CTX, sgsstd_fmtstream_t* hdr,
	MemBuf* B, sgs_SizeVal numbytes, char* ccstr, sgs_SizeVal ccsize )
{
	if( numbytes > 0 )
	{
		while( hdr->state != FMTSTREAM_STATE_END )
		{
			sgs_SizeVal readamt = fs_getreadsize( hdr, 1 );
			if( readamt )
			{
				char c = hdr->buffer[ hdr->bufpos ];
				if( !fs_check_cc( ccstr, ccsize, c ) )
					break;
				membuf_appchr( B, C, c );
			}
			numbytes -= readamt;
			hdr->bufpos += readamt;
			if( numbytes <= 0 )
				break;
			if( !readamt && !fs_refill( C, hdr ) )
			{
				STDLIB_WARN( "unexpected read error" )
			}
		}
	}
	
	return 1;
}

static int sgsstd_fmtstreamI_readcc( SGS_CTX )
{
	int ret;
	char* ccstr;
	sgs_SizeVal numbytes = 0x7fffffff, ccsize;
	MemBuf B = membuf_create();
	
	SGSFS_IHDR( readcc )

	if( !sgs_LoadArgs( C, "@>m|l", &ccstr, &ccsize, &numbytes ) )
		return 0;

	if( !fs_validate_cc( C, ccstr, ccsize ) )
		STDLIB_WARN( "error in character class" )
	
	ret = _stream_readcc( C, hdr, &B, numbytes, ccstr, ccsize );
	if( ret )
		/* WP: conversion does not affect range; string limit */
		sgs_PushStringBuf( C, B.ptr, (sgs_SizeVal) B.size );
	membuf_destroy( &B, C );
	return ret;
}

static int sgsstd_fmtstreamI_skipcc( SGS_CTX )
{
	char* ccstr;
	sgs_SizeVal numbytes = 0x7fffffff, ccsize, numsc = 0;
	
	SGSFS_IHDR( skipcc )

	if( !sgs_LoadArgs( C, "@>m|l", &ccstr, &ccsize, &numbytes ) )
		return 0;

	if( !fs_validate_cc( C, ccstr, ccsize ) )
		STDLIB_WARN( "error in character class" )
	
	if( numbytes )
	{
		while( hdr->state != FMTSTREAM_STATE_END )
		{
			sgs_SizeVal readamt = fs_getreadsize( hdr, 1 );
			if( readamt )
			{
				char c = hdr->buffer[ hdr->bufpos ];
				if( !fs_check_cc( ccstr, ccsize, c ) )
					break;
				numsc++;
			}
			numbytes -= readamt;
			hdr->bufpos += readamt;
			if( numbytes <= 0 )
				break;
			if( !readamt && !fs_refill( C, hdr ) )
			{
				STDLIB_WARN( "fmtstream.skipcc(): unexpected read error" )
			}
		}
	}
	sgs_PushInt( C, numsc );
	return 1;
}

static int sgsstd_fmtstreamI_read_real( SGS_CTX )
{
	SGSBOOL ret, conv = TRUE;
	sgs_SizeVal numbytes = 128;
	MemBuf B = membuf_create();
	
	SGSFS_IHDR( read_real )
	if( !sgs_LoadArgs( C, "@>|bl", &conv, &numbytes ) )
		return 0;
	
	ret = _stream_readcc( C, hdr, &B, numbytes, "-+0-9.eE", 8 );
	if( ret )
	{
		if( conv )
			sgs_PushReal( C, util_atof( B.ptr, B.size ) );
		else if( B.size > 0x7fffffff )
		{
			membuf_destroy( &B, C );
			STDLIB_WARN( "read more data than allowed to store" );
		}
		else
			sgs_PushStringBuf( C, B.ptr, (sgs_SizeVal) B.size );
	}
	membuf_destroy( &B, C );
	return ret;
}

static int sgsstd_fmtstreamI_read_int( SGS_CTX )
{
	SGSBOOL ret, conv = TRUE;
	sgs_SizeVal numbytes = 128;
	MemBuf B = membuf_create();
	
	SGSFS_IHDR( read_int )
	if( !sgs_LoadArgs( C, "@>|bl", &conv, &numbytes ) )
		return 0;
	
	ret = _stream_readcc( C, hdr, &B, numbytes, "-+0-9A-Fa-fxob", 14 );
	if( ret )
	{
		if( conv )
			sgs_PushInt( C, util_atoi( B.ptr, B.size ) );
		else if( B.size > 0x7fffffff )
		{
			membuf_destroy( &B, C );
			STDLIB_WARN( "read more data than allowed to store" );
		}
		else
			sgs_PushStringBuf( C, B.ptr, (sgs_SizeVal) B.size );
	}
	membuf_destroy( &B, C );
	return ret;
}

static int sgsstd_fmtstreamI_read_binary_int( SGS_CTX )
{
	SGSBOOL ret, conv = TRUE;
	sgs_SizeVal numbytes = 128;
	MemBuf B = membuf_create();
	
	SGSFS_IHDR( read_binary_int )
	
	membuf_appbuf( &B, C, "0b", 2 );
	
	if( !sgs_LoadArgs( C, "@>|bl", &conv, &numbytes ) )
		return 0;
	
	ret = _stream_readcc( C, hdr, &B, numbytes, "0-1", 3 );
	if( ret )
	{
		if( conv )
			sgs_PushInt( C, util_atoi( B.ptr, B.size ) );
		else if( B.size > 0x7fffffff )
		{
			membuf_destroy( &B, C );
			STDLIB_WARN( "read more data than allowed to store" );
		}
		else
			sgs_PushStringBuf( C, B.ptr, (sgs_SizeVal) B.size );
	}
	membuf_destroy( &B, C );
	return ret;
}

static int sgsstd_fmtstreamI_read_octal_int( SGS_CTX )
{
	SGSBOOL ret, conv = TRUE;
	sgs_SizeVal numbytes = 128;
	MemBuf B = membuf_create();
	
	SGSFS_IHDR( read_octal_int )
	
	membuf_appbuf( &B, C, "0o", 2 );
	
	if( !sgs_LoadArgs( C, "@>|bl", &conv, &numbytes ) )
		return 0;
	
	ret = _stream_readcc( C, hdr, &B, numbytes, "0-7", 3 );
	if( ret )
	{
		if( conv )
			sgs_PushInt( C, util_atoi( B.ptr, B.size ) );
		else if( B.size > 0x7fffffff )
		{
			membuf_destroy( &B, C );
			STDLIB_WARN( "read more data than allowed to store" );
		}
		else
			sgs_PushStringBuf( C, B.ptr, (sgs_SizeVal) B.size );
	}
	membuf_destroy( &B, C );
	return ret;
}

static int sgsstd_fmtstreamI_read_decimal_int( SGS_CTX )
{
	SGSBOOL ret, conv = TRUE;
	sgs_SizeVal numbytes = 128;
	MemBuf B = membuf_create();
	
	SGSFS_IHDR( read_decimal_int )
	if( !sgs_LoadArgs( C, "@>|bl", &conv, &numbytes ) )
		return 0;
	
	ret = _stream_readcc( C, hdr, &B, numbytes, "-+0-9", 5 );
	if( ret )
	{
		if( conv )
			sgs_PushInt( C, util_atoi( B.ptr, B.size ) );
		else if( B.size > 0x7fffffff )
		{
			membuf_destroy( &B, C );
			STDLIB_WARN( "read more data than allowed to store" );
		}
		else
			sgs_PushStringBuf( C, B.ptr, (sgs_SizeVal) B.size );
	}
	membuf_destroy( &B, C );
	return ret;
}

static int sgsstd_fmtstreamI_read_hex_int( SGS_CTX )
{
	SGSBOOL ret, conv = TRUE;
	sgs_SizeVal numbytes = 128;
	MemBuf B = membuf_create();
	
	SGSFS_IHDR( read_hex_int )
	
	membuf_appbuf( &B, C, "0x", 2 );
	
	if( !sgs_LoadArgs( C, "@>|bl", &conv, &numbytes ) )
		return 0;
	
	ret = _stream_readcc( C, hdr, &B, numbytes, "0-9a-fA-F", 9 );
	if( ret )
	{
		if( conv )
			sgs_PushInt( C, util_atoi( B.ptr, B.size ) );
		else if( B.size > 0x7fffffff )
		{
			membuf_destroy( &B, C );
			STDLIB_WARN( "read more data than allowed to store" );
		}
		else
			sgs_PushStringBuf( C, B.ptr, (sgs_SizeVal) B.size );
	}
	membuf_destroy( &B, C );
	return ret;
}

static int sgsstd_fmtstreamI_check( SGS_CTX )
{
	char* chkstr, chr = 0, chr2;
	sgs_SizeVal chksize, numchk = 0;
	SGSBOOL partial = FALSE, ci = FALSE;
	
	SGSFS_IHDR( skipcc )
	if( !sgs_LoadArgs( C, "@>m|bb", &chkstr, &chksize, &ci, &partial ) )
		return 0;
	
	while( numchk < chksize )
	{
		while( hdr->state != FMTSTREAM_STATE_END )
		{
			sgs_SizeVal readamt = fs_getreadsize( hdr, 1 );
			if( !readamt )
			{
				if( !fs_refill( C, hdr ) )
				{
					STDLIB_WARN( "unexpected read error" )
				}
				continue;
			}
			chr = hdr->buffer[ hdr->bufpos ];
			break;
		}
		chr2 = chkstr[ numchk ];
		if( chr == chr2 || ( ci && tolower( (int)chr ) == tolower( (int)chr2 ) ) )
		{
			hdr->bufpos++;
			numchk++;
		}
		else
			break;
	}
	
	if( partial )
		sgs_PushInt( C, numchk );
	else
		sgs_PushBool( C, numchk == chksize );
	return 1;
}



static int sgsstd_fmtstream_getindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	char* str;
	sgs_SizeVal size;
	SGSFS_HDR;
	if( !sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "fmt_parser: could not read property string" )
	
#define IFN( x ) { sgs_PushCFunction( C, x ); return SGS_SUCCESS; }
	if STREQ( str, "read" ) IFN( sgsstd_fmtstreamI_read )
	else if STREQ( str, "getchar" ) IFN( sgsstd_fmtstreamI_getchar )
	else if STREQ( str, "readcc" ) IFN( sgsstd_fmtstreamI_readcc )
	else if STREQ( str, "skipcc" ) IFN( sgsstd_fmtstreamI_skipcc )
	else if STREQ( str, "read_real" ) IFN( sgsstd_fmtstreamI_read_real )
	else if STREQ( str, "read_int" ) IFN( sgsstd_fmtstreamI_read_int )
	else if STREQ( str, "read_binary_int" ) IFN( sgsstd_fmtstreamI_read_binary_int )
	else if STREQ( str, "read_octal_int" ) IFN( sgsstd_fmtstreamI_read_octal_int )
	else if STREQ( str, "read_decimal_int" ) IFN( sgsstd_fmtstreamI_read_decimal_int )
	else if STREQ( str, "read_hex_int" ) IFN( sgsstd_fmtstreamI_read_hex_int )
	else if STREQ( str, "check" ) IFN( sgsstd_fmtstreamI_check )
	
	else if( 0 == strcmp( str, "at_end" ) ){
		sgs_PushBool( C, hdr->state == FMTSTREAM_STATE_END );
		return SGS_SUCCESS; }
	else if( 0 == strcmp( str, "stream_offset" ) ){
		sgs_PushInt( C, hdr->streamoff + hdr->bufpos );
		return SGS_SUCCESS; }
	
	return SGS_ENOTFND;
}

static sgs_ObjCallback sgsstd_fmtstream_functable[ 5 ] =
{
	SOP_DESTRUCT, sgsstd_fmtstream_destroy,
	SOP_GETINDEX, sgsstd_fmtstream_getindex,
	SOP_END
};

static int sgsstd_fmt_parser( SGS_CTX )
{
	sgs_Int bufsize = 1024;
	
	SGSBASEFN( "fmt_parser" );

	if( !sgs_LoadArgs( C, "?p|i", &bufsize ) )
		return 0;

	/* test call: reading 0 bytes should return an empty string */
	sgs_PushInt( C, 0 );
	sgs_PushItem( C, 0 );
	if( sgs_Call( C, 1, 1 ) != SGS_SUCCESS )
		STDLIB_WARN( "test call did not succeed; "
			"is the source function correctly specified?" )
	sgs_Pop( C, 1 );

	{
		sgsstd_fmtstream_t* hdr = sgs_Alloc( sgsstd_fmtstream_t );
		sgs_GetStackItem( C, 0, &hdr->source );
		sgs_Acquire( C, &hdr->source );
		hdr->streamoff = 0;
		hdr->bufsize = (int) bufsize;
		hdr->buffer = sgs_Alloc_n( char, (size_t) hdr->bufsize );
		hdr->buffill = 0;
		hdr->bufpos = 0;
		hdr->state = FMTSTREAM_STATE_INIT;
		sgs_PushObject( C, hdr, sgsstd_fmtstream_functable );
		return 1;
	}
}


typedef struct _stringread_t
{
	sgs_Variable S;
	sgs_SizeVal off;
}
stringread_t;


static int srt_call( SGS_CTX, sgs_VarObj* data, int smth )
{
	sgs_Int amt;
	stringread_t* srt = (stringread_t*) data->data;
	if( !sgs_ParseInt( C, 0, &amt ) || amt > 0x7fffffff )
		return SGS_EINVAL;
	if( srt->off >= srt->S.data.S->size )
		return 0;
	else
	{
		/* WP: string limit */
		sgs_SizeVal rn = MIN( (sgs_SizeVal) srt->S.data.S->size - srt->off, (sgs_SizeVal) amt );
		sgs_PushStringBuf( C, str_cstr( srt->S.data.S ) + srt->off, rn );
		srt->off += rn;
		return 1;
	}
}

static int srt_destruct( SGS_CTX, sgs_VarObj* data, int unused )
{
	stringread_t* srt = (stringread_t*) data->data;
	UNUSED( unused );
	sgs_Release( C, &srt->S );
	return SGS_SUCCESS;
}

static sgs_ObjCallback srt_iface[] =
{
	SOP_CALL, srt_call,
	SOP_DESTRUCT, srt_destruct,
	SOP_END
};

static int sgsstd_fmt_string_parser( SGS_CTX )
{
	stringread_t* srt;
	sgs_Int off = 0, bufsize = 1024;
	
	SGSFN( "fmt_string_parser" );
	if( !sgs_LoadArgs( C, "?m|ii", &off, &bufsize ) )
		return 0;
	
	srt = (stringread_t*) sgs_PushObjectIPA( C, sizeof(stringread_t), srt_iface );
	sgs_GetStackItem( C, 0, &srt->S );
	sgs_BreakIf( srt->S.type != VTC_STRING );
	sgs_Acquire( C, &srt->S );
	srt->off = (sgs_SizeVal) off;
	sgs_StoreItem( C, 0 );
	sgs_SetStackSize( C, 1 );
	sgs_PushInt( C, bufsize );
	return sgsstd_fmt_parser( C );
}


typedef struct _fileread_t
{
	sgs_Variable F;
}
fileread_t;


static int frt_call( SGS_CTX, sgs_VarObj* data, int smth )
{
	sgs_Int amt;
	FILE* fp;
	fileread_t* frt = (fileread_t*) data->data;
	if( !sgs_ParseInt( C, 0, &amt ) || amt > 0x7fffffff )
		return SGS_EINVAL;
	fp = (FILE*) frt->F.data.O->data;
	if( !fp || feof( fp ) )
		return 0;
	sgs_PushVariable( C, &frt->F );
	sgs_PushInt( C, amt );
	sgs_PushItem( C, -2 );
	sgs_PushProperty( C, "read" );
	if( sgs_ThisCall( C, 1, 1 ) )
		return SGS_EINPROC;
	return 1;
}

static int frt_destruct( SGS_CTX, sgs_VarObj* data, int unused )
{
	fileread_t* frt = (fileread_t*) data->data;
	UNUSED( unused );
	sgs_Release( C, &frt->F );
	return SGS_SUCCESS;
}

static sgs_ObjCallback frt_iface[] =
{
	SOP_CALL, frt_call,
	SOP_DESTRUCT, frt_destruct,
	SOP_END
};

static int sgsstd_fmt_file_parser( SGS_CTX )
{
	fileread_t* frt;
	sgs_Int bufsize = 1024;
	
	SGSFN( "fmt_file_parser" );
	if( !sgs_LoadArgs( C, "?o|i", sgsstd_file_functable, &bufsize ) )
		return 0;
	
	frt = (fileread_t*) sgs_PushObjectIPA( C, sizeof(fileread_t), frt_iface );
	sgs_GetStackItem( C, 0, &frt->F );
	sgs_BreakIf( frt->F.type != VTC_OBJECT );
	sgs_Acquire( C, &frt->F );
	sgs_StoreItem( C, 0 );
	sgs_SetStackSize( C, 1 );
	sgs_PushInt( C, bufsize );
	return sgsstd_fmt_parser( C );
}


static int sgsstd_fmt_charcc( SGS_CTX )
{
	char* chs, *ccs;
	sgs_SizeVal chsz, ccsz;
	SGSFN( "fmt_charcc" );
	
	if( !sgs_LoadArgs( C, "mm", &chs, &chsz, &ccs, &ccsz ) )
		return 0;
	
	if( chsz < 1 )
		STDLIB_WARN( "argument 1 (string) needs at least one character" )
	
	/* WP: sign is irrelevant */
	sgs_PushBool( C, fs_check_cc( ccs, ccsz, *chs ) );
	return 1;
}


static const sgs_RegFuncConst f_fconsts[] =
{
	FN( fmt_pack ), FN( fmt_pack_count ),
	FN( fmt_unpack ), FN( fmt_pack_size ),
	FN( fmt_base64_encode ), FN( fmt_base64_decode ),
	FN( fmt_text ), FN( fmt_parser ),
	FN( fmt_string_parser ), FN( fmt_file_parser ),
	FN( fmt_charcc ),
};

SGSRESULT sgs_LoadLib_Fmt( SGS_CTX )
{
	int ret;
	ret = sgs_RegFuncConsts( C, f_fconsts, ARRAY_SIZE( f_fconsts ) );
	return ret;
}



/*  - - - - -       - - - - - -
	
	I N P U T   /   O U T P U T
	
*/

#define CRET( suc ) sgs_PushBool( C, sgs_Errno( C, suc ) ); return 1;

#define FILE_READ 1
#define FILE_WRITE 2

#define FST_UNKNOWN 0
#define FST_FILE 1
#define FST_DIR 2

static int sgsstd_io_setcwd( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	
	SGSFN( "io_setcwd" );

	if( !sgs_LoadArgs( C, "m", &str, &size ) )
		return 0;
	
	CRET( sgsXPC_SetCurrentDirectory( str ) == 0 );
}

static int sgsstd_io_getcwd( SGS_CTX )
{
	char* cwd;
	SGSFN( "io_getcwd" );
	
	if( !sgs_LoadArgs( C, "." ) )
		return 0;
	
	cwd = sgsXPC_GetCurrentDirectory();
	sgs_Errno( C, cwd != NULL );
	if( cwd )
	{
		sgs_PushString( C, cwd );
		free( cwd );
		return 1;
	}
	else
		return 0;
}

static int sgsstd_io_getexecpath( SGS_CTX )
{
	char* path;
	SGSFN( "io_getexecpath" );
	
	if( !sgs_LoadArgs( C, "." ) )
		return 0;
	
	path = sgsXPC_GetModuleFileName();
	sgs_Errno( C, path != NULL );
	if( path )
	{
		sgs_PushString( C, path );
		free( path );
		return 1;
	}
	else
		return 0;
}

static int sgsstd_io_rename( SGS_CTX )
{
	char* path, *nnm;
	sgs_SizeVal psz, nnmsz;
	
	SGSFN( "io_rename" );

	if( !sgs_LoadArgs( C, "mm", &path, &psz, &nnm, &nnmsz ) )
		return 0;
	
	CRET( rename( path, nnm ) == 0 );
}

static int sgsstd_io_file_exists( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	
	SGSFN( "io_file_exists" );

	if( !sgs_LoadArgs( C, "m", &str, &size ) )
		return 0;

	{
		FILE* fp = fopen( str, "rb" );
		sgs_PushBool( C, !!fp );
		if( fp ) fclose( fp );
		return 1;
	}
}

static int sgsstd_io_dir_exists( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	
	SGSFN( "io_dir_exists" );

	if( !sgs_LoadArgs( C, "m", &str, &size ) )
		return 0;

	{
		DIR* dp = opendir( str );
		sgs_PushBool( C, !!dp );
		if( dp ) closedir( dp );
		return 1;
	}
}

static int sgsstd_io_stat( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	
	SGSFN( "io_stat" );

	if( !sgs_LoadArgs( C, "m", &str, &size ) )
		return 0;

	{
		struct stat data;
		if( !sgs_Errno( C, stat( str, &data ) == 0 ) )
			return 0;

		/* --- */
		sgs_PushString( C, "atime" );
		sgs_PushInt( C, data.st_atime );
		sgs_PushString( C, "ctime" );
		sgs_PushInt( C, data.st_ctime );
		sgs_PushString( C, "mtime" );
		sgs_PushInt( C, data.st_mtime );
		sgs_PushString( C, "type" );
		if( data.st_mode & S_IFDIR )
			sgs_PushInt( C, FST_DIR );
		else if( data.st_mode & S_IFREG )
			sgs_PushInt( C, FST_FILE );
		else
			sgs_PushInt( C, FST_UNKNOWN );
		sgs_PushString( C, "size" );
		sgs_PushInt( C, data.st_size );
		return sgs_PushDict( C, 10 );
	}
}

static int sgsstd_io_dir_create( SGS_CTX )
{
	int ret;
	char* str;
	sgs_SizeVal size;
	sgs_Int mode = 0777;
	
	SGSFN( "io_dir_create" );

	if( !sgs_LoadArgs( C, "m|i", &str, &size, &mode ) )
		return 0;

	ret = mkdir( str
#ifndef _WIN32
		,mode
#endif
	);
	CRET( ret == 0 );
}

static int sgsstd_io_dir_delete( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	
	SGSFN( "io_dir_delete" );

	if( !sgs_LoadArgs( C, "m", &str, &size ) )
		return 0;
	
	CRET( rmdir( str ) == 0 );
}

static int sgsstd_io_file_delete( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	
	SGSFN( "io_file_delete" );

	if( !sgs_LoadArgs( C, "m", &str, &size ) )
		return 0;
	
	CRET( remove( str ) == 0 );
}

static int sgsstd_io_file_write( SGS_CTX )
{
	char* path, *data;
	sgs_SizeVal psz, dsz;
	
	SGSFN( "io_file_write" );

	if( !sgs_LoadArgs( C, "mm", &path, &psz, &data, &dsz ) )
		return 0;

	{
		sgs_SizeVal wsz;
		FILE* fp = fopen( path, "wb" );
		if( !fp )
		{
			sgs_Errno( C, 0 );
			STDLIB_WARN( "failed to create file" )
		}
		errno = 0;
		/* WP: string limit */
		wsz = (sgs_SizeVal) fwrite( data, 1, (size_t) dsz, fp );
		if( wsz < dsz )
			sgs_Errno( C, 0 );
		fclose( fp );
		if( wsz < dsz )
			STDLIB_WARN( "failed to write to file" )
	}
	
	sgs_Errno( C, 1 );
	sgs_PushBool( C, TRUE );
	return 1;
}

static int sgsstd_io_file_read( SGS_CTX )
{
	char* path;
	sgs_SizeVal psz;
	
	SGSFN( "io_file_read" );

	if( !sgs_LoadArgs( C, "m", &path, &psz ) )
		return 0;

	{
		sgs_SizeVal len, rd;
		FILE* fp = fopen( path, "rb" );
		if( !fp )
		{
			sgs_Errno( C, 0 );
			STDLIB_WARN( "failed to open file" )
		}
		fseek( fp, 0, SEEK_END );
		len = ftell( fp );
		fseek( fp, 0, SEEK_SET );

		sgs_PushStringBuf( C, NULL, len );
		errno = 0;
		/* WP: string limit */
		rd = (sgs_SizeVal) fread( sgs_GetStringPtr( C, -1 ), 1, (size_t) len, fp );
		if( rd < len )
			sgs_Errno( C, 0 );
		fclose( fp );
		if( rd < len )
			STDLIB_WARN( "failed to read file" )

		return 1;
	}
}


#define FVAR ((FILE*)data)
#define IFVAR ((FILE*)data->data)
#define FVNO_BEGIN if( FVAR ) {
#define FVNO_END( name ) } else \
	STDLIB_WARN( "file." #name "() - file is not opened" )

/* sgsstd_file_functable declaration is at the top */

#define FIF_INIT( fname ) \
	void* data; \
	int method_call = sgs_Method( C ); \
	SGSFN( "file." #fname ); \
	if( !sgs_IsObject( C, 0, sgsstd_file_functable ) ) \
		return sgs_ArgErrorExt( C, 0, method_call, "file", "" ); \
	data = sgs_GetObjectData( C, 0 );


static int sgsstd_fileP_offset( SGS_CTX, FILE* fp )
{
	long pos;
	if( !fp )
		STDLIB_WARN( "file.offset - file is not opened" )
	
	pos = ftell( fp );
	sgs_Errno( C, pos >= 0 );
	sgs_PushInt( C, pos );
	return SGS_SUCCESS;
}

static int sgsstd_fileP_size( SGS_CTX, FILE* fp )
{
	if( !fp )
		STDLIB_WARN( "file.size - file is not opened" )

	{
		long size;
		fpos_t pos;
		if( fgetpos( fp, &pos ) < 0 )
		{
			sgs_Errno( C, 0 );
			return SGS_EINPROC;
		}
		fseek( fp, 0, SEEK_END );
		if( ( size = ftell( fp ) ) < 0 )
		{
			sgs_Errno( C, 0 );
			return SGS_EINPROC;
		}
		sgs_PushInt( C, size );
		fsetpos( fp, &pos );
		sgs_Errno( C, 1 );
		return SGS_SUCCESS;
	}
}

static int sgsstd_fileP_error( SGS_CTX, FILE* fp )
{
	if( !fp )
		STDLIB_WARN( "file.error - file is not opened" )

	sgs_PushBool( C, ferror( fp ) );
	return SGS_SUCCESS;
}

static int sgsstd_fileP_eof( SGS_CTX, FILE* fp )
{
	if( !fp )
		STDLIB_WARN( "file.eof - file is not opened" )

	sgs_PushBool( C, feof( fp ) );
	return SGS_SUCCESS;
}


static const char* g_io_fileflagmodes[] = { NULL, "rb", "wb", "wb+" };

static int sgsstd_fileI_open( SGS_CTX )
{
	int ff;
	char* path;
	sgs_Int flags;

	FIF_INIT( open )
	
	if( !sgs_LoadArgs( C, "@>si", &path, &flags ) )
		return 0;
	
	if( !( ff = flags & ( FILE_READ | FILE_WRITE ) ) )
		STDLIB_WARN( "argument 2 (flags) must be either FILE_READ or FILE_WRITE or both" )

	if( FVAR )
		fclose( FVAR );

	sgs_SetObjectData( C, 0, fopen( path, g_io_fileflagmodes[ ff ] ) );
	
	CRET( !!FVAR );
}

static int sgsstd_fileI_close( SGS_CTX )
{
	int res = 0;

	FIF_INIT( close )

	if( sgs_StackSize( C ) != 1 )
		STDLIB_WARN( "function expects 0 arguments" )

	if( FVAR )
	{
		res = 1;
		fclose( FVAR );
		sgs_SetObjectData( C, 0, NULL );
	}

	sgs_PushBool( C, res );
	return 1;
}

static int sgsstd_fileI_read( SGS_CTX )
{
	MemBuf mb = membuf_create();
	char bfr[ 1024 ];
	sgs_Int size;
	FIF_INIT( read )
	
	if( !sgs_LoadArgs( C, "@>i", &size ) )
		return 0;
	
	if( size < 0 || size > 0x7fffffff )
		STDLIB_WARN( "attempted to read a negative or huge amount of bytes" );

	FVNO_BEGIN
		while( size > 0 )
		{
			size_t numread = fread( bfr, 1, (size_t) MIN( size, 1024 ), FVAR );
			if( numread <= 0 )
			{
				if( ferror( FVAR ) )
					sgs_Errno( C, 0 );
				break;
			}
			membuf_appbuf( &mb, C, bfr, numread );
			size -= 1024;
		}
		sgs_BreakIf( mb.size > 0x7fffffff );
		sgs_PushStringBuf( C, mb.ptr, (sgs_SizeVal) mb.size );
		membuf_destroy( &mb, C );
		sgs_Errno( C, 1 );
		return 1;
	FVNO_END( write )
}

static int sgsstd_fileI_write( SGS_CTX )
{
	char* str;
	sgs_SizeVal strsize;

	FIF_INIT( write )
	
	if( !sgs_LoadArgs( C, "@>m", &str, &strsize ) )
		return 0;

	FVNO_BEGIN
		/* WP: string limit */
		sgs_PushBool( C, fwrite( str, 1, (size_t) strsize, FVAR ) == strsize );
		return 1;
	FVNO_END( write )
}

#define SGS_SEEK_SET 0
#define SGS_SEEK_CUR 1
#define SGS_SEEK_END 2
static int sgsstd_fileI_seek( SGS_CTX )
{
	static const int seekmodes[ 3 ] = { SEEK_SET, SEEK_CUR, SEEK_END };
	sgs_Int off, mode = SEEK_SET;
	FIF_INIT( seek )
	
	if( !sgs_LoadArgs( C, "@>ii", &off, &mode ) )
		return 0;

	if( mode < 0 || mode > 2 )
		STDLIB_WARN( "'mode' not one of SEEK_(SET|CUR|END)" )

	FVNO_BEGIN
		sgs_PushBool( C, !fseek( FVAR, (sgs_SizeVal) off, seekmodes[ mode ] ) );
		return 1;
	FVNO_END( eof )
}

static int sgsstd_fileI_flush( SGS_CTX )
{
	FIF_INIT( close )

	if( sgs_StackSize( C ) != 1 )
		STDLIB_WARN( "function expects 0 arguments" )

	FVNO_BEGIN
		sgs_PushBool( C, !fflush( FVAR ) );
		return 1;
	FVNO_END( flush )
}

static int sgsstd_fileI_setbuf( SGS_CTX )
{
	sgs_Int size;
	FIF_INIT( setbuf )
	
	if( !sgs_LoadArgs( C, "@>i", &size ) )
		return 0;

	FVNO_BEGIN
		sgs_PushBool( C, !setvbuf( FVAR, NULL, size ? _IOFBF : _IONBF, (size_t) size ) );
		return 1;
	FVNO_END( setbuf )
}


static int sgsstd_file_getindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	char* str;
	sgs_SizeVal len;
	if( !prop )
		return SGS_ENOTSUP;
	if( !sgs_ParseString( C, 0, &str, &len ) )
		return SGS_EINVAL;

	if( 0 == strcmp( str, "offset" ) ){ return sgsstd_fileP_offset( C, IFVAR ); }
	if( 0 == strcmp( str, "size" ) ){ return sgsstd_fileP_size( C, IFVAR ); }
	if( 0 == strcmp( str, "error" ) ){ return sgsstd_fileP_error( C, IFVAR ); }
	if( 0 == strcmp( str, "eof" ) ){ return sgsstd_fileP_eof( C, IFVAR ); }

#define IFN( x ) { sgs_PushCFunction( C, x ); return SGS_SUCCESS; }
	if( 0 == strcmp( str, "open" ) ) IFN( sgsstd_fileI_open )
	if( 0 == strcmp( str, "close" ) ) IFN( sgsstd_fileI_close )
	if( 0 == strcmp( str, "read" ) ) IFN( sgsstd_fileI_read )
	if( 0 == strcmp( str, "write" ) ) IFN( sgsstd_fileI_write )
	if( 0 == strcmp( str, "seek" ) ) IFN( sgsstd_fileI_seek )
	if( 0 == strcmp( str, "flush" ) ) IFN( sgsstd_fileI_flush )
	if( 0 == strcmp( str, "setbuf" ) ) IFN( sgsstd_fileI_setbuf )
#undef IFN

	return SGS_ENOTFND;
}

static int sgsstd_file_destruct( SGS_CTX, sgs_VarObj* data, int dch )
{
	UNUSED( C );
	UNUSED( dch );
	if( IFVAR )
		fclose( IFVAR );
	return SGS_SUCCESS;
}

static int sgsstd_file_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	UNUSED( data );
	if( type == SVT_BOOL )
	{
		sgs_PushBool( C, !!IFVAR );
		return SGS_SUCCESS;
	}
	if( type == SVT_STRING || type == SGS_CONVOP_TOTYPE )
	{
		sgs_PushString( C, "file" );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}


static sgs_ObjCallback sgsstd_file_functable[ 7 ] =
{
	SOP_GETINDEX, sgsstd_file_getindex,
	SOP_CONVERT, sgsstd_file_convert,
	SOP_DESTRUCT, sgsstd_file_destruct,
	SOP_END,
};

static int sgsstd_io_file( SGS_CTX )
{
	int ff;
	char* path;
	sgs_Int flags;
	FILE* fp = NULL;
	
	SGSFN( "io_file" );

	if( sgs_StackSize( C ) == 0 )
		goto pushobj;
	
	if( !sgs_LoadArgs( C, "si", &path, &flags ) )
		return 0;
	
	if( !( ff = flags & ( FILE_READ | FILE_WRITE ) ) )
		STDLIB_WARN( "argument 2 (flags) must be either FILE_READ or FILE_WRITE or both" )

	fp = fopen( path, g_io_fileflagmodes[ ff ] );
	sgs_Errno( C, !!fp );

pushobj:
	sgs_PushObject( C, fp, sgsstd_file_functable );
	return 1;
}

#undef FVAR


typedef struct _sgsstd_dir_t
{
	DIR* dir;
	char* name;
}
sgsstd_dir_t;


#define DIR_HDR sgsstd_dir_t* hdr = (sgsstd_dir_t*) data->data

SGS_DECLARE sgs_ObjCallback sgsstd_dir_functable[ 7 ];

static int sgsstd_dir_destruct( SGS_CTX, sgs_VarObj* data, int dco )
{
	DIR_HDR;
	UNUSED( C );
	if( hdr->dir ) closedir( hdr->dir );
	sgs_Dealloc( hdr );
	return SGS_SUCCESS;
}

static int sgsstd_dir_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	if( type == SVT_STRING || type == SGS_CONVOP_TOTYPE )
	{
		sgs_PushString( C, "directory_iterator" );
		return SGS_SUCCESS;
	}
	else if( type == SGS_CONVOP_TOITER )
	{
		sgs_Variable v;
		v.type = VTC_OBJECT;
		v.data.O = data;
		sgs_PushVariable( C, &v );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int sgsstd_dir_getnext( SGS_CTX, sgs_VarObj* data, int what )
{
	DIR_HDR;
	if( !what )
	{
		struct dirent* de = readdir( hdr->dir );
		hdr->name = de ? de->d_name : NULL;
		return hdr->name ? 1 : 0;
	}
	else
	{
		if( !hdr->name )
			return SGS_EINVAL;

		if( what & SGS_GETNEXT_KEY )
			sgs_PushBool( C,
				!( hdr->name[0] == '.' && ( hdr->name[1] == '\0' ||
				( hdr->name[1] == '.' && hdr->name[2] == '\0' ) ) ) );
		if( what & SGS_GETNEXT_VALUE )
			sgs_PushString( C, hdr->name );
	}
	return SGS_SUCCESS;
}

static sgs_ObjCallback sgsstd_dir_functable[ 7 ] =
{
	SOP_DESTRUCT, sgsstd_dir_destruct,
	SOP_CONVERT, sgsstd_dir_convert,
	SOP_GETNEXT, sgsstd_dir_getnext,
	SOP_END,
};

static int sgsstd_io_dir( SGS_CTX )
{
	char* path;
	DIR* dp = NULL;
	sgsstd_dir_t* hdr;
	
	SGSFN( "io_dir" );
	
	if( !sgs_LoadArgs( C, "s", &path ) )
		return 0;

	dp = opendir( path );
	sgs_Errno( C, !!dp );
	if( !dp )
		STDLIB_WARN( "failed to open directory" )

	hdr = sgs_Alloc( sgsstd_dir_t );
	hdr->dir = dp;
	hdr->name = NULL;

	sgs_PushObject( C, hdr, sgsstd_dir_functable );
	return 1;
}


static const sgs_RegRealConst i_rconsts[] =
{
	{ "FILE_READ", FILE_READ },
	{ "FILE_WRITE", FILE_WRITE },

	{ "SEEK_SET", SGS_SEEK_SET },
	{ "SEEK_CUR", SGS_SEEK_CUR },
	{ "SEEK_END", SGS_SEEK_END },

	{ "FST_UNKNOWN", FST_UNKNOWN },
	{ "FST_FILE", FST_FILE },
	{ "FST_DIR", FST_DIR },
};

static const sgs_RegFuncConst i_fconsts[] =
{
	FN( io_getcwd ), FN( io_setcwd ),
	FN( io_getexecpath ),
	FN( io_rename ),
	FN( io_file_exists ), FN( io_dir_exists ), FN( io_stat ),
	FN( io_dir_create ), FN( io_dir_delete ),
	FN( io_file_delete ),
	FN( io_file_write ), FN( io_file_read ),
	FN( io_file ), FN( io_dir ),
};

SGSRESULT sgs_LoadLib_IO( SGS_CTX )
{
	int ret;
	ret = sgs_RegRealConsts( C, i_rconsts, ARRAY_SIZE( i_rconsts ) );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_RegFuncConsts( C, i_fconsts, ARRAY_SIZE( i_fconsts ) );
	return ret;
}



/*  - - - -
	
	M A T H
	
*/

#define SGS_PI 3.14159265358979323846
#define SGS_E 2.7182818284590452354

static sgs_Real myround( sgs_Real x )
{
	return floor( x + 0.5 );
}

static sgs_Real deg2rad( sgs_Real x ){ return x * SGS_PI / 180; }
static sgs_Real rad2deg( sgs_Real x ){ return x * 180 / SGS_PI; }

static int sgsstd_pow( SGS_CTX )
{
	sgs_Real b, e;
	
	SGSFN( "pow" );
	
	if( !sgs_LoadArgs( C, "rr", &b, &e ) )
		return 0;
	
	if( ( b < 0 && e != (sgs_Real) (sgs_Int) e )
		|| ( b == 0 && e < 0 ) )
		STDLIB_WARN( "mathematical error" )
	sgs_PushReal( C, pow( b, e ) );
	return 1;
}

static int sgsstd_sqrt( SGS_CTX )
{
	sgs_Real arg0;
	
	SGSFN( "sqrt" );
	
	if( !sgs_LoadArgs( C, "r", &arg0 ) )
		return 0;
	
	if( arg0 < 0 )
		STDLIB_WARN( "mathematical error" )
	sgs_PushReal( C, sqrt( arg0 ) );
	return 1;
}
static int sgsstd_asin( SGS_CTX )
{
	sgs_Real arg0;
	
	SGSFN( "asin" );
	
	if( !sgs_LoadArgs( C, "r", &arg0 ) )
		return 0;
	
	if( arg0 < -1 || arg0 > 1 )
		STDLIB_WARN( "mathematical error" )
	sgs_PushReal( C, asin( arg0 ) );
	return 1;
}
static int sgsstd_acos( SGS_CTX )
{
	sgs_Real arg0;
	
	SGSFN( "acos" );
	
	if( !sgs_LoadArgs( C, "r", &arg0 ) )
		return 0;
	
	if( arg0 < -1 || arg0 > 1 )
		STDLIB_WARN( "mathematical error" )
	sgs_PushReal( C, acos( arg0 ) );
	return 1;
}

static int sgsstd_log( SGS_CTX )
{
	sgs_Real x, b;
	
	SGSFN( "log" );
	
	if( !sgs_LoadArgs( C, "rr", &x, &b ) )
		return 0;
	
	if( x <= 0 || b <= 0 || b == 1 )
		STDLIB_WARN( "mathematical error" )
	sgs_PushReal( C, log( x ) / log( b ) );
	return 1;
}

#define MATHFUNC_CN( name, orig ) \
static int sgsstd_##name( SGS_CTX ) { \
	sgs_Real arg0; \
	SGSFN( #name ); \
	if( !sgs_LoadArgs( C, "r", &arg0 ) ) \
		return 0; \
	sgs_PushReal( C, orig( arg0 ) ); \
	return 1; }
#define MATHFUNC( name ) MATHFUNC_CN( name, name )

#define MATHFUNC2_CN( name, orig ) \
static int sgsstd_##name( SGS_CTX ) { \
	sgs_Real arg0, arg1; \
	SGSFN( #name ); \
	if( !sgs_LoadArgs( C, "rr", &arg0, &arg1 ) ) \
		return 0; \
	sgs_PushReal( C, orig( arg0, arg1 ) ); \
	return 1; }
#define MATHFUNC2( name ) MATHFUNC2_CN( name, name )

MATHFUNC_CN( abs, fabs )
MATHFUNC( floor )
MATHFUNC( ceil )
MATHFUNC_CN( round, myround )

MATHFUNC( sin )
MATHFUNC( cos )
MATHFUNC( tan )
MATHFUNC( atan )
MATHFUNC2( atan2 )
MATHFUNC( deg2rad )
MATHFUNC( rad2deg )


static const sgs_RegRealConst m_rconsts[] =
{
	{ "M_PI", SGS_PI },
	{ "M_E", SGS_E },
};

static const sgs_RegFuncConst m_fconsts[] =
{
	FN( abs ), FN( floor ), FN( ceil ), FN( round ),
	FN( pow ), FN( sqrt ), FN( log ),
	FN( sin ), FN( cos ), FN( tan ),
	FN( asin ), FN( acos ), FN( atan ), FN( atan2 ),
	FN( deg2rad ), FN( rad2deg ),
};

SGSRESULT sgs_LoadLib_Math( SGS_CTX )
{
	int ret;
	ret = sgs_RegRealConsts( C, m_rconsts, ARRAY_SIZE( m_rconsts ) );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_RegFuncConsts( C, m_fconsts, ARRAY_SIZE( m_fconsts ) );
	return ret;
}



/*  - - - - - - - - -   - - - - - -
	
	O P E R A T I N G   S Y S T E M
	
*/

static int sgsstd_os_gettype( SGS_CTX )
{
	sgs_PushString( C, SGS_OS_TYPE );
	return 1;
}

static int sgsstd_os_command( SGS_CTX )
{
	char* str;
	
	SGSFN( "os_command" );
	
	if( !sgs_LoadArgs( C, "s", &str ) )
		return 0;

	sgs_PushInt( C, system( str ) );
	return 1;
}

static int sgsstd_os_getenv( SGS_CTX )
{
	char* str;
	
	SGSFN( "os_getenv" );
	
	if( !sgs_LoadArgs( C, "s", &str ) )
		return 0;

	str = getenv( str );
	if( str )
		sgs_PushString( C, str );
	return !!str;
}

static int sgsstd_os_putenv( SGS_CTX )
{
	char* str;
	
	SGSFN( "os_putenv" );
	
	if( !sgs_LoadArgs( C, "s", &str ) )
		return 0;

	sgs_PushBool( C, putenv( str ) == 0 );
	return 1;
}


static int sgsstd_os_time( SGS_CTX )
{
	time_t ttv;
	sgs_Real tz = 0;
	sgs_Int outsecs = 0;
	int ssz = sgs_StackSize( C );
	
	SGSFN( "os_time" );
	
	if( !sgs_LoadArgs( C, "|r", &tz ) )
		return 0;
	
	time( &ttv );
	if( ssz )
	{
		outsecs = mktime( gmtime( &ttv ) );
		outsecs += ((int)(tz * 2)) * 1800;
	}
	else
		outsecs = ttv;
	sgs_PushInt( C, outsecs );
	return 1;
}

static int sgsstd_os_get_timezone( SGS_CTX )
{
	int asstr = 0;
	
	SGSFN( "os_get_timezone" );
	
	if( !sgs_LoadArgs( C, "|b", &asstr ) )
		return 0;

	{
		double diff;
		time_t ttv, t1, t2;
		/* magic */
		time( &ttv );
		t1 = mktime( gmtime( &ttv ) );
		t2 = mktime( localtime( &ttv ) );
		diff = (double) ( difftime( t2, t1 ) / 1800 ) / 2.0;
		if( !asstr )
			sgs_PushReal( C, diff );
		else
		{
			char bfr[ 32 ];
			sprintf( bfr, "%c%02d:%02d", diff >= 0 ? '+' : '-',
				(int) diff, ((int) diff * 60 ) % 60 );
			sgs_PushString( C, bfr );
		}
		return 1;
	}
}


static void put2digs( char* at, int what )
{
	at[0] = (char)( '0' + ( ( what / 10 ) % 10 ) );
	at[1] = (char)( '0' + ( what % 10 ) );
}

static void put4digs( char* at, int what )
{
	at[0] = (char)( '0' + ( ( what / 1000 ) % 10 ) );
	at[1] = (char)( '0' + ( ( what / 100 ) % 10 ) );
	at[2] = (char)( '0' + ( ( what / 10 ) % 10 ) );
	at[3] = (char)( '0' + ( what % 10 ) );
}

static int sgsstd_os_date_string( SGS_CTX )
{
	time_t ttv;
	char* fmt, *fmtend;
	sgs_SizeVal fmtsize;
	sgs_Int uts;
	struct tm T;
	MemBuf B = membuf_create();
	int ssz = sgs_StackSize( C );
	
	SGSFN( "os_date_string" );
	
	if( !sgs_LoadArgs( C, "m|i", &fmt, &fmtsize, &uts ) )
		return 0;

	{
		int Y, M, D, H, m, s;
		if( ssz < 2 )
			time( &ttv );
		else
			ttv = (time_t) uts;
		T = *localtime( &ttv );

		Y = T.tm_year + 1900;
		M = T.tm_mon + 1;
		D = T.tm_mday;
		H = T.tm_hour;
		m = T.tm_min;
		s = T.tm_sec;

		fmtend = fmt + fmtsize;
		while( fmt < fmtend )
		{
			char c = *fmt++;
			if( c == '%' && fmt < fmtend )
			{
				int sz = 0;
				char swp[ 32 ];
				c = *fmt++;
				switch( c )
				{
				/* handle locale-specific/complex cases with strftime */
				case 'a': case 'A': case 'b': case 'B':
				case 'c': case 'x': case 'X': case 'Z':
				case 'U': case 'W':
					{
						char pbuf[ 256 ];
						char ft_fmt[3] = { '%', 0, 0 };
						ft_fmt[1] = c;
						strftime( pbuf, 256, ft_fmt, &T );
						membuf_appbuf( &B, C, pbuf, strlen( pbuf ) );
					}
					break;
				case 'C': put2digs( swp, Y / 100 ); sz = 2; break;
				case 'd': put2digs( swp, D ); sz = 2; break;
				case 'e':
					if( D > 9 )
						swp[0] = (char)( '0' + ( ( D / 10 ) % 10 ) );
					else swp[0] = ' ';
					swp[1] = (char)( '0' + ( D % 10 ) );
					sz = 2; break;
				case 'F':
					put4digs( swp, Y ); swp[4] = '-';
					put2digs( swp + 5, M ); swp[7] = '-';
					put2digs( swp + 8, D ); sz = 10;
					break;
				case 'H': put2digs( swp, H ); sz = 2; break;
				case 'I': put2digs( swp, H%12 ? H%12 : 12 ); sz = 2; break;
				case 'j':
					put2digs( swp, ( T.tm_yday + 1 ) / 10 );
					swp[2] = (char)( '0' + ( T.tm_yday + 1 ) % 10 );
					sz = 3; break;
				case 'm': put2digs( swp, M ); sz = 2; break;
				case 'M': put2digs( swp, m ); sz = 2; break;
				case 'p': membuf_appbuf( &B, C, &"AMPM"[ H/12*2 ], 2 ); break;
				case 'R':
					put2digs( swp, H ); swp[2] = ':';
					put2digs( swp + 3, m ); sz = 5;
					break;
				case 'S': put2digs( swp, s ); sz = 2; break;
				case 'T':
					put2digs( swp, H ); swp[2] = ':';
					put2digs( swp + 3, m ); swp[5] = ':';
					put2digs( swp + 6, s ); sz = 8;
					break;
				case 'u': swp[0] = (char)( '0' + T.tm_wday ); sz = 1; break;
				case 'w': swp[0] = (char)( '0' + (T.tm_wday?T.tm_wday:7) ); sz = 1; break;
				case 'y': put2digs( swp, Y ); sz = 2; break;
				case 'Y': put4digs( swp, Y ); sz = 4; break;
				/* special additions */
				case 'f': /* the file-safe format: %Y-%m-%d_%H-%M-%S" */
					put4digs( swp, Y ); swp[4] = '-';
					put2digs( swp + 5, M ); swp[7] = '-';
					put2digs( swp + 8, D ); swp[10] = '_';
					put2digs( swp + 11, H ); swp[13] = '-';
					put2digs( swp + 14, m ); swp[16] = '-';
					put2digs( swp + 17, s ); sz = 19;
					break;
				case 't': /* the UNIX timestamp */
					sprintf( swp, "%" PRId64, (sgs_Int) ttv );
					sz = (int) strlen( swp ); break;
				/* leftovers */
				case '%': membuf_appchr( &B, C, '%' ); break;
				default: membuf_appbuf( &B, C, fmt - 2, 2 ); break;
				}
				if( sz )
					membuf_appbuf( &B, C, swp, (size_t) sz );
			}
			else
				membuf_appchr( &B, C, c );
		}
		
		if( B.size > 0x7fffffff )
		{
			membuf_destroy( &B, C );
			STDLIB_WARN( "generated more string data than allowed to store" );
		}
		/* WP: error condition */
		sgs_PushStringBuf( C, B.ptr, (sgs_SizeVal) B.size );
		membuf_destroy( &B, C );
		return 1;
	}
}

static int sgsstd_os_parse_time( SGS_CTX )
{
	time_t ttv;
	sgs_Int uts;
	struct tm T;
	int ssz = sgs_StackSize( C );
	
	SGSFN( "os_parse_time" );
	
	if( !sgs_LoadArgs( C, "|i", &uts ) )
		return 0;

	if( ssz >= 1 )
		ttv = (time_t) uts;
	else
		time( &ttv );
	T = *localtime( &ttv );

	sgs_PushString( C, "year" );
	sgs_PushInt( C, T.tm_year + 1900 );
	sgs_PushString( C, "month" );
	sgs_PushInt( C, T.tm_mon + 1 );
	sgs_PushString( C, "day" );
	sgs_PushInt( C, T.tm_mday );
	sgs_PushString( C, "weekday" );
	sgs_PushInt( C, T.tm_wday ? T.tm_wday : 7 );
	sgs_PushString( C, "yearday" );
	sgs_PushInt( C, T.tm_yday + 1 );
	sgs_PushString( C, "hours" );
	sgs_PushInt( C, T.tm_hour );
	sgs_PushString( C, "minutes" );
	sgs_PushInt( C, T.tm_min );
	sgs_PushString( C, "seconds" );
	sgs_PushInt( C, T.tm_sec );
	sgs_PushDict( C, sgs_StackSize( C ) - ssz );
	return 1;
}

static int sgsstd_os_make_time( SGS_CTX )
{
	sgs_Int p[6];
	struct tm T = {0};
	int ssz = sgs_StackSize( C );
	
	SGSFN( "os_make_time" );
	
	if( !sgs_LoadArgs( C, "i|iiiii", p, p+1, p+2, p+3, p+4, p+5 ) )
		return 0;

	if( ssz >= 1 ) T.tm_sec = (int) p[0];
	if( ssz >= 2 ) T.tm_min = (int) p[1];
	if( ssz >= 3 ) T.tm_hour = (int) p[2];
	if( ssz >= 4 ) T.tm_mday = (int) p[3];
	if( ssz >= 5 ) T.tm_mon = (int) p[4] - 1;
	if( ssz >= 6 ) T.tm_year = (int) p[5] - 1900;
	sgs_PushInt( C, mktime( &T ) );
	return 1;
}


static int sgsstd_os_get_locale( SGS_CTX )
{
	sgs_Int which;
	
	SGSFN( "os_get_locale" );
	if( !sgs_LoadArgs( C, "i", &which ) )
		return 0;
	
	sgs_PushString( C, setlocale( (int) which, NULL ) );
	return 1;
}

static int sgsstd_os_set_locale( SGS_CTX )
{
	char* nlstr;
	sgs_Int which;
	
	SGSFN( "os_set_locale" );
	if( !sgs_LoadArgs( C, "is", &which, &nlstr ) )
		return 0;
	
	sgs_PushBool( C, !!setlocale( (int) which, nlstr ) );
	return 1;
}

static int sgsstd_os_get_locale_format( SGS_CTX )
{
#ifndef SGS_INVALID_LCONV
	struct lconv* lc = localeconv();
#endif
	
	sgs_SetStackSize( C, 0 );
	
#ifndef SGS_INVALID_LCONV
#define PLK( name ) sgs_PushString( C, #name );
#define PLS( name ) PLK( name ); sgs_PushString( C, lc->name );
#define PLI( name ) PLK( name ); sgs_PushInt( C, lc->name );
	PLS( decimal_point );
	PLS( thousands_sep );
	PLS( grouping );
	PLS( int_curr_symbol );
	PLS( currency_symbol );
	PLS( mon_decimal_point );
	PLS( mon_thousands_sep );
	PLS( mon_grouping );
	PLS( positive_sign );
	PLS( negative_sign );
	PLI( frac_digits );
	PLI( p_cs_precedes );
	PLI( n_cs_precedes );
	PLI( p_sep_by_space );
	PLI( n_sep_by_space );
	PLI( p_sign_posn );
	PLI( n_sign_posn );
	PLI( int_frac_digits );
#endif
	
	sgs_PushDict( C, sgs_StackSize( C ) );
	return 1;
}

static int sgsstd_os_locale_strcmp( SGS_CTX )
{
	char *a, *b;
	
	SGSFN( "os_locale_strcmp" );
	if( !sgs_LoadArgs( C, "ss", &a, &b ) )
		return 0;
	
	sgs_PushInt( C, strcoll( a, b ) );
	return 1;
}


static const sgs_RegIntConst o_iconsts[] =
{
	{ "LC_ALL", LC_ALL },
	{ "LC_COLLATE", LC_COLLATE },
	{ "LC_MONETARY", LC_MONETARY },
	{ "LC_NUMERIC", LC_NUMERIC },
	{ "LC_TIME", LC_TIME },
};

static const sgs_RegFuncConst o_fconsts[] =
{
	FN( os_gettype ), FN( os_command ),
	FN( os_getenv ), FN( os_putenv ),
	FN( os_time ), FN( os_get_timezone ), FN( os_date_string ),
	FN( os_parse_time ), FN( os_make_time ),
	FN( os_get_locale ), FN( os_set_locale ),
	FN( os_get_locale_format ), FN( os_locale_strcmp ),
};

SGSRESULT sgs_LoadLib_OS( SGS_CTX )
{
	int ret;
	ret = sgs_RegFuncConsts( C, o_fconsts, ARRAY_SIZE( o_fconsts ) );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_RegIntConsts( C, o_iconsts, ARRAY_SIZE( o_iconsts ) );
	return ret;
}



/*  - - - - - - -   - - - - - - - - - - -
	
	R E G U L A R   E X P R E S S I O N S
	
*/

#define REGEX_RETURN_CAPTURED 1
#define REGEX_RETURN_OFFSETS 2
#define REGEX_RETURN_BOTH (REGEX_RETURN_CAPTURED|REGEX_RETURN_OFFSETS)

static int _regex_init( SGS_CTX, srx_Context** pR, char* ptrn )
{
	srx_Context* R;
	int errnpos[2] = {0,0};
	char conchar, *delpos;
	
	if( !*ptrn )
		STDLIB_WARN( "argument 2 (pattern) is empty" )
	conchar = *ptrn;
	delpos = strchr( ptrn + 1, conchar );
	if( !delpos )
		STDLIB_WARN( "unmatched pattern/modifier separator defined at character 0" )
	
	*delpos = 0; /* slightly evil */
	R = srx_CreateExt( ptrn + 1, delpos + 1, errnpos, C->memfunc, C->mfuserdata );
	*delpos = conchar;
	if( !R )
	{
		const char* errstr = "unknown error";
		switch( errnpos[0] )
		{
		case RXEINMOD: errstr = "invalid modifier"; break;
		case RXEPART : errstr = "partial (sub-)expression"; break;
		case RXEUNEXP: errstr = "unexpected character"; break;
		case RXERANGE: errstr = "invalid range (min > max)"; break;
		case RXELIMIT: errstr = "too many digits"; break;
		case RXEEMPTY: errstr = "expression is effectively empty"; break;
		case RXENOREF: errstr = "the specified backreference cannot be used here"; break;
		}
		return sgs_Printf( C, SGS_WARNING, "failed to parse the pattern"
			" - %s at character %d", errstr, errnpos[1] );
	}
	*pR = R;
	
	return 1;
}

static int _regex_match( SGS_CTX, srx_Context* R, char* str, sgs_SizeVal off, sgs_Int flags )
{
	int ret = srx_Match( R, str, off );
	if( ret )
	{
		if( flags & REGEX_RETURN_BOTH )
		{
			int i, numcaps = srx_GetCaptureCount( R );
			for( i = 0; i < numcaps; ++i )
			{
				const char *cf, *ct;
				if( srx_GetCapturedPtrs( R, i, &cf, &ct ) )
				{
					if( flags & REGEX_RETURN_CAPTURED )
						/* WP: string limit */
						sgs_PushStringBuf( C, cf, (sgs_SizeVal) ( ct - cf ) );
					if( flags & REGEX_RETURN_OFFSETS )
					{
						sgs_PushInt( C, cf - str );
						sgs_PushInt( C, ct - str );
					}
					if( ( flags & REGEX_RETURN_BOTH ) > 1 )
						sgs_PushArray( C, flags & REGEX_RETURN_BOTH );
				}
				else
					sgs_PushNull( C );
			}
			sgs_PushArray( C, numcaps );
			return -1;
		}
	}
	return ret;
}

static int sgsstd_re_match( SGS_CTX )
{
	char *str, *ptrn;
	int ret;
	sgs_SizeVal strsize, off = 0;
	sgs_Int flags = 0;
	srx_Context* R;
	
	SGSFN( "re_match" );
	
	if( !sgs_LoadArgs( C, "ms|il", &str, &strsize, &ptrn, &flags, &off ) )
		return 0;
	
	if( off < 0 ) off += strsize;
	if( off < 0 || off > strsize )
		STDLIB_WARN( "argument 5 (offset) out of bounds" )
	
	if( !_regex_init( C, &R, ptrn ) )
		return 0;
	
	ret = _regex_match( C, R, str, off, flags );
	if( ret >= 0 )
		sgs_PushBool( C, ret );
	
	srx_Destroy( R );
	return 1;
}

static int sgsstd_re_match_all( SGS_CTX )
{
	char *str, *ptrn;
	int ret, cnt = 0, noff;
	sgs_SizeVal strsize, off = 0;
	sgs_Int flags = 0;
	srx_Context* R;
	
	SGSFN( "re_match_all" );
	
	if( !sgs_LoadArgs( C, "ms|il", &str, &strsize, &ptrn, &flags, &off ) )
		return 0;
	
	if( off < 0 ) off += strsize;
	if( off < 0 || off > strsize )
		STDLIB_WARN( "argument 5 (offset) out of bounds" )
	
	if( !_regex_init( C, &R, ptrn ) )
		return 0;
	
	while( ( ret = _regex_match( C, R, str, off, flags ) ) != 0 )
	{
		srx_GetCaptured( R, 0, NULL, &noff );
		if( off != noff )
			off = noff;
		else
			off++;
		cnt++;
	}
	if( flags & REGEX_RETURN_BOTH )
		sgs_PushArray( C, cnt );
	else
		sgs_PushInt( C, cnt );
	
	srx_Destroy( R );
	return 1;
}

static int sgsstd_re_replace( SGS_CTX )
{
	char *str, *ptrn, *rep, *ret;
	srx_Context* R = NULL;
	
	SGSFN( "re_replace" );
	
	if( !sgs_LoadArgs( C, "sss", &str, &ptrn, &rep ) )
		return 0;
	
	if( !_regex_init( C, &R, ptrn ) )
		return 0;
	
	ret = srx_Replace( R, str, rep );
	sgs_PushString( C, ret );
	srx_FreeReplaced( R, ret );
	
	return 1;
}


static const sgs_RegIntConst r_iconsts[] =
{
	{ "RE_RETURN_CAPTURED", REGEX_RETURN_CAPTURED },
	{ "RE_RETURN_OFFSETS", REGEX_RETURN_OFFSETS },
	{ "RE_RETURN_BOTH", REGEX_RETURN_BOTH },
};

static const sgs_RegFuncConst r_fconsts[] =
{
	FN( re_match ), FN( re_match_all ),
	FN( re_replace ),
};

SGSRESULT sgs_LoadLib_RE( SGS_CTX )
{
	int ret;
	ret = sgs_RegIntConsts( C, r_iconsts, ARRAY_SIZE( r_iconsts ) );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_RegFuncConsts( C, r_fconsts, ARRAY_SIZE( r_fconsts ) );
	return ret;
}



/*  - - - - - -
	
	S T R I N G
	
*/

#define sgsNO_REV_INDEX 1
#define sgsSTRICT_RANGES 2
#define sgsLEFT 1
#define sgsRIGHT 2

static SGS_INLINE int32_t idx2off( int32_t size, int32_t i )
{
	if( -i > size || i >= size ) return -1;
	return i < 0 ? size + i : i;
}

static int sgsstd_string_cut( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	sgs_Int flags = 0, i1, i2;
	
	SGSFN( "string_cut" );
	
	if( !sgs_LoadArgs( C, "mi", &str, &size, &i1 ) )
		return 0;
	
	i2 = size - 1;
	if( sgs_LoadArgsExt( C, 2, "|ii", &i2, &flags ) < 1 )
		return 0;

	if( FLAG( flags, sgsNO_REV_INDEX ) && ( i1 < 0 || i2 < 0 ) )
		STDLIB_WARN( "detected negative indices" );

	i1 = i1 < 0 ? size + i1 : i1;
	i2 = i2 < 0 ? size + i2 : i2;
	if( FLAG( flags, sgsSTRICT_RANGES ) &&
		( i1 > i2 || i1 < 0 || i2 < 0 || i1 >= size || i2 >= size ) )
		STDLIB_WARN( "invalid character range" );

	if( i1 > i2 || i1 >= size || i2 < 0 )
		sgs_PushStringBuf( C, "", 0 );
	else
	{
		i1 = MAX( 0, MIN( i1, size - 1 ) );
		i2 = MAX( 0, MIN( i2, size - 1 ) );
		sgs_PushStringBuf( C, str + i1, (sgs_SizeVal) i2 - (sgs_SizeVal) i1 + 1 );
	}
	return 1;
}

static int sgsstd_string_part( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	sgs_Int flags = 0, i1, i2;
	
	SGSFN( "string_part" );
	
	if( !sgs_LoadArgs( C, "mi", &str, &size, &i1 ) )
		return 0;
	
	i2 = size - i1;
	if( sgs_LoadArgsExt( C, 2, "|ii", &i2, &flags ) < 1 )
		return 0;

	if( FLAG( flags, sgsNO_REV_INDEX ) && ( i1 < 0 || i2 < 0 ) )
		STDLIB_WARN( "detected negative indices" );

	i1 = i1 < 0 ? size + i1 : i1;
	i2 = i2 < 0 ? size + i2 : i2;
	if( FLAG( flags, sgsSTRICT_RANGES ) &&
		( i1 < 0 || i1 + i2 < 0 || i2 < 0 || i1 >= size || i1 + i2 > size ) )
		STDLIB_WARN( "invalid character range" );

	if( i2 <= 0 || i1 >= size || i1 + i2 < 0 )
		sgs_PushStringBuf( C, "", 0 );
	else
	{
		i2 += i1 - 1;
		i1 = MAX( 0, MIN( i1, size - 1 ) );
		i2 = MAX( 0, MIN( i2, size - 1 ) );
		sgs_PushStringBuf( C, str + i1, (sgs_SizeVal) i2 - (sgs_SizeVal) i1 + 1 );
	}
	return 1;
}

static int sgsstd_string_reverse( SGS_CTX )
{
	char* str, *sout;
	sgs_SizeVal size, i;
	
	SGSFN( "string_reverse" );
	
	if( !sgs_LoadArgs( C, "m", &str, &size ) )
		return 0;

	sgs_PushStringBuf( C, NULL, size );
	sout = sgs_GetStringPtr( C, -1 );

	for( i = 0; i < size; ++i )
		sout[ size - i - 1 ] = str[ i ];

	return 1;
}

static int sgsstd_string_pad( SGS_CTX )
{
	char* str, *pad = " ", *sout;
	sgs_SizeVal size, padsize = 1;
	sgs_Int tgtsize, flags = sgsRIGHT, lpad = 0, i;
	
	SGSFN( "string_pad" );
	
	if( !sgs_LoadArgs( C, "mi|mi", &str, &size, &tgtsize, &pad, &padsize, &flags ) )
		return 0;

	if( tgtsize <= size || !FLAG( flags, sgsLEFT | sgsRIGHT ) )
	{
		sgs_PushItem( C, 0 );
		return 1;
	}

	sgs_PushStringBuf( C, NULL, (sgs_SizeVal) tgtsize );
	sout = sgs_GetStringPtr( C, -1 );
	if( FLAG( flags, sgsLEFT ) )
	{
		if( FLAG( flags, sgsRIGHT ) )
		{
			sgs_Int pp = tgtsize - size;
			lpad = pp / 2 + pp % 2;
		}
		else
			lpad = tgtsize - size;
	}
	
	/* WP: string limit */
	memcpy( sout + lpad, str, (size_t) size );
	for( i = 0; i < lpad; ++i )
		sout[ i ] = pad[ i % padsize ];
	size += (sgs_SizeVal) lpad;
	while( size < tgtsize )
	{
		sout[ size ] = pad[ size % padsize ];
		size++;
	}

	return 1;
}

static int sgsstd_string_repeat( SGS_CTX )
{
	char* str, *sout;
	sgs_SizeVal size;
	sgs_Int count;
	
	SGSFN( "string_repeat" );
	
	if( !sgs_LoadArgs( C, "mi", &str, &size, &count ) )
		return 0;
	
	if( count < 0 )
		STDLIB_WARN( "argument 2 (count) must be at least 0" )

	sgs_PushStringBuf( C, NULL, (sgs_SizeVal) count * size );
	sout = sgs_GetStringPtr( C, -1 );
	while( count-- )
	{
		/* WP: string limit */
		memcpy( sout, str, (size_t) size );
		sout += size;
	}
	return 1;
}

static int sgsstd_string_count( SGS_CTX )
{
	int overlap = FALSE;
	char* str, *sub, *strend;
	sgs_SizeVal size, subsize, ret = 0;
	
	SGSFN( "string_count" );

	if( !sgs_LoadArgs( C, "mm|b", &str, &size, &sub, &subsize, &overlap ) )
		return 0;

	if( subsize <= 0 )
		STDLIB_WARN( "argument 2 (substring) length must be bigger than 0" )

	strend = str + size - subsize;
	while( str <= strend )
	{
		/* WP: string limit */
		if( strncmp( str, sub, (size_t) subsize ) == 0 )
		{
			ret++;
			str += overlap ? 1 : subsize;
		}
		else
			str++;
	}

	sgs_PushInt( C, ret );
	return 1;
}

static int sgsstd_string_find( SGS_CTX )
{
	char* str, *sub, *strend, *ostr;
	sgs_SizeVal size, subsize; 
	sgs_SizeVal from = 0;
	
	SGSFN( "string_find" );

	if( !sgs_LoadArgs( C, "mm|l", &str, &size, &sub, &subsize, &from ) )
		return 0;

	if( subsize <= 0 )
		STDLIB_WARN( "argument 2 (substring) length must be bigger than 0" )

	strend = str + size - subsize;
	ostr = str;
	str += from >= 0 ? from : MAX( 0, size + from );
	while( str <= strend )
	{
		/* WP: string limit */
		if( strncmp( str, sub, (size_t) subsize ) == 0 )
		{
			sgs_PushInt( C, str - ostr );
			return 1;
		}
		str++;
	}

	return 0;
}

static int sgsstd_string_find_rev( SGS_CTX )
{
	char* str, *sub, *ostr;
	sgs_SizeVal size, subsize, from = -1;
	
	SGSFN( "string_find_rev" );

	if( !sgs_LoadArgs( C, "mm|l", &str, &size, &sub, &subsize, &from ) )
		return 0;

	if( subsize <= 0 )
		STDLIB_WARN( "argument 2 (substring) length must be bigger than 0" )

	ostr = str;
	str += from >= 0 ?
		MIN( from, size - subsize ) :
		MIN( size - subsize, size + from );
	while( str >= ostr )
	{
		/* WP: string limit */
		if( strncmp( str, sub, (size_t) subsize ) == 0 )
		{
			sgs_PushInt( C, str - ostr );
			return 1;
		}
		str--;
	}

	return 0;
}

static int _stringrep_ss
(
	SGS_CTX,
	char* str, int32_t size,
	char* sub, int32_t subsize,
	char* rep, int32_t repsize
)
{
	/* the algorithm:
		- find matches, count them, predict size of output string
		- readjust matches to fit the process of replacing
		- rewrite string with replaced matches
	*/
#define NUMSM 32 /* statically-stored matches */
	int32_t sma[ NUMSM ];
	int32_t* matches = sma;
	int32_t matchcount = 0, matchcap = NUMSM, curmatch;
#undef NUMSM
	
	char* strend = str + size - subsize;
	char* ptr = str, *i, *o;
	int32_t outlen;
	
	/* subsize = 0 handled by parent */
	
	while( ptr <= strend )
	{
		/* WP: string limit */
		if( strncmp( ptr, sub, (size_t) subsize ) == 0 )
		{
			if( matchcount == matchcap )
			{
				int32_t* nm;
				
				matchcap *= 4;
				/* WP: string limit */
				nm = sgs_Alloc_n( int32_t, (size_t) matchcap );
				memcpy( nm, matches, sizeof( int32_t ) * (size_t) matchcount );
				if( matches != sma )
					sgs_Dealloc( matches );
				matches = nm;
			}
			/* WP: string limit */
			matches[ matchcount++ ] = (sgs_SizeVal) ( ptr - str );
			
			ptr += subsize;
		}
		else
			ptr++;
	}
	
	outlen = size + ( repsize - subsize ) * matchcount;
	sgs_PushStringBuf( C, NULL, outlen );
	
	i = str;
	o = sgs_GetStringPtr( C, -1 );
	strend = str + size;
	curmatch = 0;
	while( i < strend && curmatch < matchcount )
	{
		char* mp = str + matches[ curmatch++ ];
		ptrdiff_t len = mp - i;
		sgs_BreakIf( len < 0 );
		if( len )
			memcpy( o, i, (size_t) len );
		i += len;
		o += len;
		
		/* WP: string limit */
		memcpy( o, rep, (size_t) repsize );
		i += subsize;
		o += repsize;
	}
	if( i < strend )
	{
		/* WP: always non-negative */
		memcpy( o, i, (size_t)( strend - i ) );
	}
	
	if( matches != sma )
		sgs_Dealloc( matches );
	
	return 1;
}
static int _stringrep_as
(
	SGS_CTX,
	char* str, int32_t size,
	char* rep, int32_t repsize
)
{
	char* substr;
	sgs_SizeVal subsize;
	int32_t i, arrsize = sgs_ArraySize( C, 1 );
	if( arrsize < 0 )
		goto fail;

	for( i = 0; i < arrsize; ++i )
	{
		if( sgs_PushNumIndex( C, 1, i ) )   goto fail;
		if( !sgs_ParseString( C, -1, &substr, &subsize ) )
			goto fail;

		if( !_stringrep_ss( C, str, size, substr, subsize, rep, repsize ) )
			goto fail;

		if( sgs_PopSkip( C, i > 0 ? 2 : 1, 1 ) != SGS_SUCCESS )
			goto fail;

		str = sgs_GetStringPtr( C, -1 );
		size = sgs_GetStringSize( C, -1 );
	}

	return 1;

fail:
	return 0;
}
static int _stringrep_aa( SGS_CTX, char* str, int32_t size )
{
	char* substr, *repstr;
	sgs_SizeVal subsize, repsize;
	int32_t i, arrsize = sgs_ArraySize( C, 1 ),
		reparrsize = sgs_ArraySize( C, 2 );
	if( arrsize < 0 || reparrsize < 0 )
		goto fail;

	for( i = 0; i < arrsize; ++i )
	{
		if( sgs_PushNumIndex( C, 1, i ) )   goto fail;
		if( !sgs_ParseString( C, -1, &substr, &subsize ) )
			goto fail;

		if( sgs_PushNumIndex( C, 2, i % reparrsize ) )   goto fail;
		if( !sgs_ParseString( C, -1, &repstr, &repsize ) )
			goto fail;

		if( !_stringrep_ss( C, str, size, substr, subsize, repstr, repsize ) )
			goto fail;

		if( sgs_PopSkip( C, i > 0 ? 3 : 2, 1 ) != SGS_SUCCESS )
			goto fail;

		str = sgs_GetStringPtr( C, -1 );
		size = sgs_GetStringSize( C, -1 );
	}

	return 1;

fail:
	return 0;
}
static int sgsstd_string_replace( SGS_CTX )
{
	int isarr1, isarr2, ret;
	char* str, *sub, *rep;
	sgs_SizeVal size, subsize, repsize;
	
	SGSFN( "string_replace" );

	isarr1 = sgs_ItemTypeExt( C, 1 ) == VTC_ARRAY;
	isarr2 = sgs_ItemTypeExt( C, 2 ) == VTC_ARRAY;

	if( !sgs_ParseString( C, 0, &str, &size ) )
		return sgs_FuncArgError( C, 0, SVT_STRING, 0 );

	if( isarr1 && isarr2 )
	{
		return _stringrep_aa( C, str, size );
	}

	if( isarr2 )
		return sgs_FuncArgError( C, 2, SVT_STRING, 0 );

	ret = sgs_ParseString( C, 2, &rep, &repsize );
	if( isarr1 && ret )
	{
		return _stringrep_as( C, str, size, rep, repsize );
	}

	if( sgs_ParseString( C, 1, &sub, &subsize ) && ret )
	{
		if( subsize == 0 )
		{
			sgs_PushItem( C, 1 );
			return 1;
		}
		return _stringrep_ss( C, str, size, sub, subsize, rep, repsize );
	}
	
	if( sgs_ItemType( C, 1 ) != SVT_STRING && !isarr1 )
		return sgs_ArgErrorExt( C, 1, 0, "array or string", "" );
	if( isarr1 )
	{
		if( sgs_ItemType( C, 2 ) != SVT_STRING && !isarr2 )
			return sgs_ArgErrorExt( C, 2, 0, "array or string", "" );
	}
	else
	{
		if( sgs_ItemType( C, 2 ) != SVT_STRING )
			return sgs_ArgErrorExt( C, 2, 0, "string", "" );
	}
	STDLIB_WARN( "unhandled argument error" )
}

static int sgsstd_string_translate( SGS_CTX )
{
	SGSFN( "string_translate" );
	
	if( !sgs_LoadArgs( C, "?m" ) )
		return 0;
	if( sgs_PushIterator( C, 1 ) )
		return sgs_ArgErrorExt( C, 1, 0, "iterable", "" );
	
	while( sgs_IterAdvance( C, -1 ) > 0 )
	{
		char *str, *substr, *repstr;
		sgs_SizeVal size, subsize, repsize;
		if( sgs_IterPushData( C, -1, 1, 1 ) ||
			!sgs_ParseString( C, 0, &str, &size ) ||
			!sgs_ParseString( C, -2, &substr, &subsize ) ||
			!sgs_ParseString( C, -1, &repstr, &repsize ) )
			STDLIB_WARN( "failed to read data" )
		_stringrep_ss( C, str, size, substr, subsize, repstr, repsize );
		sgs_StoreItem( C, 0 );
		sgs_Pop( C, 2 );
	}
	
	sgs_SetStackSize( C, 1 );
	return 1;
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

static int sgsstd_string_trim( SGS_CTX )
{
	char* str, *strend, *list = " \t\r\n";
	sgs_SizeVal size, listsize = 4;
	sgs_Int flags = sgsLEFT | sgsRIGHT;
	
	SGSFN( "string_trim" );
	
	if( !sgs_LoadArgs( C, "m|mi", &str, &size, &list, &listsize, &flags ) )
		return 0;

	if( !FLAG( flags, sgsLEFT | sgsRIGHT ) )
	{
		sgs_PushItem( C, 0 );
		return 1;
	}

	strend = str + size;
	if( flags & sgsLEFT )
	{
		while( str < strend && stdlib_isoneof( *str, list, listsize ) )
			str++;
	}
	if( flags & sgsRIGHT )
	{
		while( str < strend && stdlib_isoneof( *(strend-1), list, listsize ) )
			strend--;
	}
	
	/* WP: string limit */
	sgs_PushStringBuf( C, str, (sgs_SizeVal)( strend - str ) );
	return 1;
}

static int sgsstd_string_toupper( SGS_CTX )
{
	char* str, *strend;
	sgs_SizeVal size;
	
	SGSFN( "string_toupper" );
	
	if( !sgs_LoadArgs( C, "m", &str, &size ) )
		return 0;
	
	sgs_PushStringBuf( C, str, size );
	str = sgs_GetStringPtr( C, -1 );
	strend = str + size;
	while( str < strend )
	{
		/* WP: this is awful */
		*str = (char) toupper( (int)*str );
		str++;
	}
	return 1;
}

static int sgsstd_string_tolower( SGS_CTX )
{
	char* str, *strend;
	sgs_SizeVal size;
	
	SGSFN( "string_tolower" );
	
	if( !sgs_LoadArgs( C, "m", &str, &size ) )
		return 0;
	
	sgs_PushStringBuf( C, str, size );
	str = sgs_GetStringPtr( C, -1 );
	strend = str + size;
	while( str < strend )
	{
		/* WP: this is awful */
		*str = (char) tolower( (int)*str );
		str++;
	}
	return 1;
}

static int sgsstd_string_compare( SGS_CTX )
{
	int ret = 0;
	char *str1, *str2;
	sgs_SizeVal str1size, str2size;
	sgs_SizeVal from = 0, max = 0;
	
	SGSFN( "string_compare" );
	
	if( !sgs_LoadArgs( C, "mm|ll", &str1, &str1size, &str2, &str2size, &max, &from ) )
		return 0;
	
	if( from == 0 )
	{
		if( max > 0 )
		{
			str1size = MIN( str1size, max );
			str2size = MIN( str2size, max );
		}
		/* WP: string limit */
		ret = memcmp( str1, str2, (size_t) MIN( str1size, str2size ) );
		if( !ret )
		{
			if( str1size < str2size ) ret = -1;
			else if( str1size > str2size ) ret = 1;
		}
		else if( ret > 0 )
			ret = 1;
		else
			ret = -1;
	}
	else
	{
		sgs_SizeVal i1, i2, from1, from2, to1, to2;
		from1 = from < 0 ? str1size + from : from;
		from2 = from < 0 ? str2size + from : from;
		to1 = max > 0 ? from1 + max : str1size;
		to2 = max > 0 ? from2 + max : str2size;
		i1 = from1;
		i2 = from2;
		for(;;)
		{
			int b1 = i1 < 0 ? -1 : ( i1 >= str1size ? -1 : str1[ i1 ] );
			int b2 = i2 < 0 ? -1 : ( i2 >= str2size ? -1 : str2[ i2 ] );
			if( b1 < b2 )
				ret = -1;
			if( b1 > b2 )
				ret = 1;
			i1++;
			i2++;
			if( ret || i1 >= to1 || i2 >= to2 )
				break;
		}
		if( !ret )
		{
			sgs_SizeVal sz1 = to1 - from1, sz2 = to2 - from2;
			if( sz1 < sz2 )
				ret = -1;
			else if( sz1 > sz2 )
				ret = 1;
		}
	}
	sgs_PushInt( C, ret );
	return 1;
}

static int sgsstd_string_implode( SGS_CTX )
{
	sgs_SizeVal i, asize;
	
	SGSFN( "string_implode" );
	
	if( !sgs_LoadArgs( C, "a?m", &asize ) )
		return 0;

	if( !asize )
	{
		sgs_PushString( C, "" );
		return 1;
	}
	for( i = 0; i < asize; ++i )
	{
		if( i )
			sgs_PushItem( C, 1 );
		if( sgs_PushNumIndex( C, 0, i ) )
			STDLIB_WARN( "failed to read from array" )
	}
	sgs_StringMultiConcat( C, i * 2 - 1 );
	return 1;
}

static char* _findpos( char* a, sgs_SizeVal asize, char* b, sgs_SizeVal bsize )
{
	char* aend = a + asize - bsize;
	char* pend = b + bsize;
	while( a <= aend )
	{
		char* x = a, *p = b;
		while( p < pend )
			if( *x++ != *p++ )
				goto notthis;
		return a;
notthis:
		a++;
	}
	return NULL;
}

static int sgsstd_string_explode( SGS_CTX )
{
	char* a, *b, *p, *pp;
	sgs_SizeVal asize, bsize, ssz;
	
	SGSFN( "string_explode" );
	
	if( !sgs_LoadArgs( C, "mm", &a, &asize, &b, &bsize ) )
		return 0;
	
	ssz = sgs_StackSize( C );
	
	if( !bsize )
	{
		p = a + asize;
		while( a < p )
			sgs_PushStringBuf( C, a++, 1 );
	}
	else
	{
		pp = a;
		p = _findpos( a, asize, b, bsize );
		
		while( p )
		{
			/* WP: string limit */
			sgs_PushStringBuf( C, pp, (sgs_SizeVal)( p - pp ) );
			pp = p + bsize;
			p = _findpos( pp, (sgs_SizeVal)( asize - ( pp - a ) ), b, bsize );
		}
		
		/* WP: string limit */
		sgs_PushStringBuf( C, pp, (sgs_SizeVal)( a + asize - pp ) );
	}

	return sgs_PushArray( C, sgs_StackSize( C ) - ssz ) == SGS_SUCCESS;
}

static int sgsstd_string_charcode( SGS_CTX )
{
	char* a;
	sgs_SizeVal asize;
	sgs_Int off = 0;
	
	SGSFN( "string_charcode" );
	
	if( !sgs_LoadArgs( C, "m|i", &a, &asize, &off ) )
		return 0;

	if( off < 0 )
		off += asize;

	if( off < 0 || off >= (sgs_Int) asize )
		STDLIB_WARN( "index out of bounds" )

	sgs_PushInt( C, (unsigned char) a[ off ] );
	return 1;
}

static int sgsstd_string_frombytes( SGS_CTX )
{
	char* buf;
	int hasone = 0;
	sgs_SizeVal size, i = 0;
	sgs_Int onecode;
	
	SGSFN( "string_frombytes" );

	if( sgs_StackSize( C ) != 1 ||
		( ( size = sgs_ArraySize( C, 0 ) ) < 0 &&
			!( hasone = sgs_ParseInt( C, 0, &onecode ) ) ) )
		return sgs_ArgErrorExt( C, 0, 0, "array or int", "" );

	if( hasone )
	{
		if( onecode < 0 || onecode > 255 )
			STDLIB_WARN( "invalid byte value" )
		else
		{
			char c = (char) onecode;
			sgs_PushStringBuf( C, &c, 1 );
			return 1;
		}
	}

	sgs_PushStringBuf( C, NULL, size );
	buf = sgs_ToString( C, -1 );
	if( sgs_PushIterator( C, 0 ) < 0 )
		goto fail;

	while( sgs_IterAdvance( C, -1 ) > 0 )
	{
		sgs_Int b;
		if( sgs_IterPushData( C, -1, FALSE, TRUE ) < 0 )
			goto fail;
		b = sgs_GetInt( C, -1 );
		if( b < 0 || b > 255 )
			STDLIB_WARN( "invalid byte value" )
		buf[ i++ ] = (char) b;
		sgs_Pop( C, 1 );
	}
	sgs_Pop( C, 1 );
	return 1;

fail:
	STDLIB_WARN( "failed to read the array" )
}

static int sgsstd_string_utf8_decode( SGS_CTX )
{
	char* str;
	sgs_SizeVal size, cc = 0;
	
	SGSFN( "string_utf8_decode" );
	
	if( !sgs_LoadArgs( C, "m", &str, &size ) )
		return 0;
	
	while( size > 0 )
	{
		uint32_t outchar = SGS_UNICODE_INVCHAR;
		/* WP: string limit */
		int ret = sgs_utf8_decode( str, (size_t) size, &outchar );
		ret = abs( ret );
		str += ret;
		size -= ret;
		sgs_PushInt( C, outchar );
		cc++;
	}
	return sgs_PushArray( C, cc ) == SGS_SUCCESS;
}

static int sgsstd_string_utf8_encode( SGS_CTX )
{
	MemBuf buf = membuf_create();
	sgs_SizeVal size;
	
	SGSFN( "string_utf8_encode" );
	
	if( !sgs_LoadArgs( C, "a", &size ) )
		return 0;
	
	if( sgs_PushIterator( C, 0 ) < 0 )
		goto fail;
	
	while( sgs_IterAdvance( C, -1 ) > 0 )
	{
		int cnt;
		char tmp[ 4 ];
		sgs_Int cp;
		if( sgs_IterPushData( C, -1, FALSE, TRUE ) < 0 )
			goto fail;
		cp = sgs_GetInt( C, -1 );
		cnt = sgs_utf8_encode( (uint32_t) cp, tmp );
		if( !cnt )
		{
			strcpy( tmp, SGS_UNICODE_INVCHAR_STR );
			cnt = SGS_UNICODE_INVCHAR_LEN;
		}
		/* WP: pointless */
		membuf_appbuf( &buf, C, tmp, (size_t) cnt );
		sgs_Pop( C, 1 );
	}
	if( buf.size > 0x7fffffff )
	{
		membuf_destroy( &buf, C );
		STDLIB_WARN( "generated more string data than allowed to store" );
	}
	/* WP: error condition */
	sgs_PushStringBuf( C, buf.ptr, (sgs_SizeVal) buf.size );
	membuf_destroy( &buf, C );
	return 1;

fail:
	membuf_destroy( &buf, C );
	STDLIB_WARN( "failed to read the array" )
}


static int sgsstd_string_format( SGS_CTX )
{
	char* fmt, *fmtend;
	sgs_SizeVal fmtsize;
	MemBuf B = membuf_create();
	int numitem = 0;
	
	SGSFN( "string_format" );
	
	if( !sgs_LoadArgs( C, "m", &fmt, &fmtsize ) )
		return 0;

	fmtend = fmt + fmtsize;
	while( fmt < fmtend )
	{
		struct fmtspec F;
		char c = *fmt++;
		if( c == '{' )
		{
			int stkid = 0, sio, ret;
			char* tcp = fmt;
			numitem++;
			while( fmt < fmtend && *fmt >= '0' && *fmt <= '9' )
			{
				stkid *= 10;
				stkid += *fmt++ - '0';
			}
			
			if( tcp == fmt )
			{
				membuf_appchr( &B, C, c );
				if( *fmt == '{' )
					fmt++;
				continue;
			}
			if( *fmt == ':' )
			{
				fmt++;
				ret = parse_fmtspec( &F, fmt, fmtend );
				fmt = F.end;
				if( !ret )
				{
					membuf_destroy( &B, C );
					sgs_Printf( C, SGS_WARNING, 
						"parsing error in item %d - failed to parse format part", numitem );
					return 0;
				}
			}
			else if( *fmt != '}' )
			{
				membuf_destroy( &B, C );
				sgs_Printf( C, SGS_WARNING, 
					"parsing error in item %d - unexpected symbol (%c)", numitem, *fmt );
				return 0;
			}
			else
			{
				fmt++;
				F.type = 'c';
				F.padcnt = 0;
				F.padchr = ' ';
				F.prec = -1;
			}

			sio = stkid;
			if( !commit_fmtspec( C, &B, &F, &stkid ) )
			{
				sgs_Printf( C, SGS_WARNING, 
					"could not read item %d (arg. %d)", numitem, sio );
			}
		}
		else
			membuf_appchr( &B, C, c );
	}
	
	if( B.size > 0x7fffffff )
	{
		membuf_destroy( &B, C );
		STDLIB_WARN( "generated more string data than allowed to store" );
	}
	/* WP: error condition */
	sgs_PushStringBuf( C, B.ptr, (sgs_SizeVal) B.size );
	membuf_destroy( &B, C );
	return 1;
}


static const sgs_RegIntConst s_iconsts[] =
{
	{ "STRING_NO_REV_INDEX", sgsNO_REV_INDEX },
	{ "STRING_STRICT_RANGES", sgsSTRICT_RANGES },
	{ "STRING_PAD_LEFT", sgsLEFT },
	{ "STRING_PAD_RIGHT", sgsRIGHT },
	{ "STRING_TRIM_LEFT", sgsLEFT },
	{ "STRING_TRIM_RIGHT", sgsRIGHT },
};

static const sgs_RegFuncConst s_fconsts[] =
{
	FN( string_cut ), FN( string_part ),
	FN( string_reverse ), FN( string_pad ),
	FN( string_repeat ), FN( string_count ),
	FN( string_find ), FN( string_find_rev ),
	FN( string_replace ), FN( string_translate ),
	FN( string_trim ),
	FN( string_toupper ), FN( string_tolower ),
	FN( string_compare ),
	FN( string_implode ), FN( string_explode ),
	FN( string_charcode ), FN( string_frombytes ),
	FN( string_utf8_decode ), FN( string_utf8_encode ),
	FN( string_format ),
};

SGSRESULT sgs_LoadLib_String( SGS_CTX )
{
	int ret;
	ret = sgs_RegIntConsts( C, s_iconsts, ARRAY_SIZE( s_iconsts ) );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_RegFuncConsts( C, s_fconsts, ARRAY_SIZE( s_fconsts ) );
	return ret;
}


