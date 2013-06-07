

/* for the constants... */
#define _USE_MATH_DEFINES
#undef __STRICT_ANSI__
#include <math.h>
#include <time.h>

#define SGS_INTERNAL
#define SGS_REALLY_INTERNAL
#define SGS_USE_FILESYSTEM

#include "sgs_int.h"

#define FLAG( a, b ) (((a)&(b))!=0)
#define STDLIB_INFO( info ) return sgs_Printf( C, SGS_INFO, info );
#define STDLIB_WARN( warn ) return sgs_Printf( C, SGS_WARNING, warn );



/* path helper functions */

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



/*  - - - - - - - - - -
	
	F O R M A T T I N G

*/

static sgs_SizeVal fmt_pack_numitems(
	SGS_CTX, const char* str, sgs_SizeVal size )
{
	sgs_SizeVal cnt = 0, first = 1, mult = 0;
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
		case 'c': case 'w': case 'l': case 'q':
		case 'f': case 'd': case 'p':
			cnt += mult ? mult : 1;
			mult = 0;
			break;
		case 's':
			cnt++;
			mult = 0;
			break;
		/* misc. */
		case 'x':
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
	return cnt;
}

static sgs_SizeVal fmt_pack_numbytes(
	SGS_CTX, const char* str, sgs_SizeVal size )
{
	sgs_SizeVal cnt = 0, first = 1, mult = 0;
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
		case 'c': cnt += mult ? mult : 1; mult = 0; break;
		case 'w': cnt += ( mult ? mult : 1 ) * 2; mult = 0; break;
		case 'l': case 'f': cnt += ( mult ? mult : 1 ) * 4; mult = 0; break;
		case 'q': case 'd': cnt += ( mult ? mult : 1 ) * 8; mult = 0; break;
		case 'p': cnt += ( mult ? mult : 1 ) * sizeof( size_t ); mult = 0; break;
		case 's':
			cnt += mult;
			mult = 0;
			break;
		/* misc. */
		case 'x':
			cnt += mult;
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
	return cnt;
}

static sgs_SizeVal fmt_pack( SGS_CTX,
	const char* str, sgs_SizeVal size, MemBuf* bfr )
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
				int size = 1, off = 0;
				char bb[ 8 ];
				sgs_Integer i;
				if( !sgs_ParseInt( C, si, &i ) )
					return si;
				si++;
				if( c == 'w' ) size = 2;
				else if( c == 'l' ) size = 4;
				else if( c == 'q' ) size = 8;
				else if( c == 'p' ) size = sizeof( size_t );
				if( O32_HOST_ORDER == O32_BIG_ENDIAN )
					off = 7 - size;
				memcpy( bb, ((char*)&i) + off, size );
				if( invert )
				{
					int a, b;
					for( a = 0, b = size - 1; a < b; a++, b-- )
					{
						char bbt = bb[ a ];
						bb[ a ] = bb[ b ];
						bb[ b ] = bbt;
					}
				}
				membuf_appbuf( bfr, C, bb, size );
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
					membuf_appbuf( bfr, C, bb, 4 );
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
					membuf_appbuf( bfr, C, bb, 8 );
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
				membuf_appbuf( bfr, C, astr, asize );
				while( asize < mult )
				{
					membuf_appchr( bfr, C, '\0' );
					asize++;
				}
			}
			mult = 0;
			break;
		case 'x':
			if( !mult )
				mult = 1;
			while( mult-- > 0 )
				membuf_appchr( bfr, C, '\0' );
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
				int size = 1, off = 0;
				sgs_Integer i = 0;
				char bb[ 8 ];

				if( c == 'w' ) size = 2;
				else if( c == 'l' ) size = 4;
				else if( c == 'q' ) size = 8;
				else if( c == 'p' ) size = sizeof( size_t );
				if( O32_HOST_ORDER == O32_BIG_ENDIAN )
					off = 7 - size;

				memcpy( bb, data, size );
				data += size;

				if( invert )
				{
					int a, b;
					for( a = 0, b = size - 1; a < b; a++, b-- )
					{
						char bbt = bb[ a ];
						bb[ a ] = bb[ b ];
						bb[ b ] = bbt;
					}
				}

				memcpy( ((char*)&i) + off, bb, size );
				if( sign )
				{
					const sgs_Integer SIGN = -1;
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
	sgs_SizeVal size, numitems, ret;
	
	SGSFN( "fmt_pack" );
	
	if( sgs_StackSize( C ) < 1 ||
		!sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1+ arguments: string, ..." )

	numitems = fmt_pack_numitems( C, str, size );
	if( sgs_StackSize( C ) < numitems + 1 )
	{
		sgs_Printf( C, SGS_WARNING, 
			"expected at least %d arguments, got %d\n",
			numitems + 1, sgs_StackSize( C ) );
		return 0;
	}

	{
		MemBuf bfr = membuf_create();
		ret = fmt_pack( C, str, size, &bfr ) - 1;
		if( ret == numitems )
			sgs_PushStringBuf( C, bfr.ptr, bfr.size );
		else
			sgs_Printf( C, SGS_WARNING, 
				"error in arguments, could not read all" );
		membuf_destroy( &bfr, C );
		return ret == numitems;
	}
}

static int sgsstd_fmt_pack_count( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	
	SGSFN( "fmt_pack_count" );
	
	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: string" )

	sgs_PushInt( C, fmt_pack_numitems( C, str, size ) );
	return 1;
}

static int sgsstd_fmt_unpack( SGS_CTX )
{
	sgs_SizeVal bytes, ret;
	char* str, *data;
	sgs_SizeVal size, datasize;
	
	SGSFN( "fmt_unpack" );
	
	if( sgs_StackSize( C ) != 2 ||
		!sgs_ParseString( C, 0, &str, &size ) ||
		!sgs_ParseString( C, 1, &data, &datasize ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 2 arguments: string, string" )

	bytes = fmt_pack_numbytes( C, str, size );
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
	sgs_SizeVal size;
	
	SGSFN( "fmt_pack_size" );
	
	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: string" )

	sgs_PushInt( C, fmt_pack_numbytes( C, str, size ) );
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
	
	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: string" )

	{
		MemBuf B = membuf_create();
		char* strend = str + size;
		while( str < strend - 2 )
		{
			uint32_t merged = (str[0]<<16) | (str[1]<<8) | (str[2]);
			char bb[ 4 ] =
			{
				b64_table[ (merged>>18) & 0x3f ],
				b64_table[ (merged>>12) & 0x3f ],
				b64_table[ (merged>>6 ) & 0x3f ],
				b64_table[ (merged    ) & 0x3f ]
			};
			membuf_appbuf( &B, C, bb, 4 );
			str += 3;
		}
		/* last bytes */
		if( str < strend )
		{
			char bb[ 4 ];
			uint32_t merged = str[0]<<16;
			if( str < strend - 1 )
				merged |= str[1]<<8;

			bb[ 0 ] = b64_table[ (merged>>18) & 0x3f ];
			bb[ 1 ] = b64_table[ (merged>>12) & 0x3f ];
			bb[ 2 ] = str < strend-1 ? b64_table[ (merged>>6) & 0x3f ] : '=';
			bb[ 3 ] = '=';
			membuf_appbuf( &B, C, bb, 4 );
		}
		sgs_PushStringBuf( C, B.ptr, B.size );
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
	
	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: string" )

	{
		MemBuf B = membuf_create();
		char* beg = str;
		char* strend = str + size;
		while( str < strend - 3 )
		{
			char bb[ 3 ];
			uint32_t merged;
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
			bb[ 0 ] = (merged>>16) & 0xff;
			bb[ 1 ] = (merged>>8) & 0xff;
			bb[ 2 ] = merged & 0xff;
			membuf_appbuf( &B, C, bb, 3 - no );
			str += 4;
			if( no )
				break;
		}
		if( str < strend )
			sgs_Printf( C, SGS_WARNING, "extra bytes detected and ignored" );
		sgs_PushStringBuf( C, B.ptr, B.size );
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

static int _flt_write_rep( char* out, sgs_Real R, int sci, struct fmtspec* F )
{
	/*
		sci = 1  =>  scientific mode, F->prec digits after point
		sci = 0  =>  readability mode, F->prec digits of precision
		sci = -1 =>  decimal mode, F->prec digits after point
	*/
#define DBL_MDST 0.000000000000000001

	int mandigs, minprec = 0, maxprec = 999, exp10 = 0;

	char* obeg = out;
	uint64_t data;
	memcpy( &data, &R, sizeof( R ) );
	int sgn = ( data >> 63 ) & 1;
	int xpn = ( data >> 52 ) & 0x7ff;
	uint64_t mnt = data & 0xfffffffffffffULL;

	if( R < 0 ) R = -R;

	if( xpn == 0 && mnt == 0 )
	{
		out[0] = '0';
		return 1;
	}
	if( xpn == 0x7ff )
	{
		out[0] = "+-"[ sgn ];
		out[1] = '#';
		if( mnt == 0 ){ out[2] = 'I'; out[3] = 'N'; out[4] = 'F'; }
		else { out[2] = 'N'; out[3] = 'a'; out[4] = 'N'; }
		return 5;
	}

	mandigs = 17;
	if( sci == 0 )
	{
		mandigs = F->prec;
	}
	else
	{
		minprec = maxprec = F->prec;
	}

	if( sci == 0 )
	{
		double xpmov = floor( fabs( log10( R ) ) ) + 1;
		int xpdigs = xpmov + 1;
		int scisc = mandigs + log10( xpmov + 1 ) + 1 + 3;
		int decsc = mandigs + 1 +
			( xpn > 1024 ?
				MAX( xpdigs - mandigs + 2, 0 ) :
				xpdigs - 2
			);
		sci = scisc < decsc ? 1 : -1;
		/*
		printf( "scisc = %d, decsc = %d, mandigs = %d, xpdigs = %d, xpd = %d\n"
			, scisc, decsc, mandigs, xpdigs, xpn );*/
	}

	if( sgn )
		*out++ = '-';
	if( sci > 0 )
	{
		/* pull out exponent */
		exp10 = (int) floor( log10( R ) );
		R /= pow( 10, exp10 );
	}

	/* render the double */
	{
		int mdc = 0, pdc = 0, hasdot = 0;
		double qf = pow( 10, floor( log10( R ) ) );
		if( qf >= 1 )
		{
			while( qf >= 1 )
			{
				*out++ = '0' + (int) floor( fmodf( R / qf, 10.0 ) );
				qf /= 10.0;
				mdc++;
			}
		}
		else
			*out++ = '0';
		if( mdc < mandigs && pdc < maxprec && qf > DBL_MDST )
		{
			double qfb = 0.1;
			*out++ = '.';
			hasdot = 1;
			while( qfb > qf )
			{
				*out++ = '0';
				qfb /= 10.0;
			}
		}
		while( mdc < mandigs && pdc < maxprec && qf > DBL_MDST )
		{
			*out++ = '0' + (int) floor( fmodf( R / qf, 10.0 ) );
			qf /= 10.0;
			mdc++; pdc++;
		}
		while( pdc < minprec )
		{
			*out++ = '0';
			pdc++;
		}

		if( ( F->type == 'g' || F->type == 'G' ) && hasdot )
		{
			out--;
			while( *out == '0' )
				out--;
			if( *out == '.' )
				out--;
			out++;
		}
	}

	/* write the exponent part */
	if( sci > 0 )
	{
		int qf;
		*out++ = F->type == 'G' || F->type == 'E' ? 'E' : 'e';
		*out++ = exp10 >= 0 ? '+' : '-';
		if( exp10 < 0 ) exp10 = -exp10;
		qf = (int) pow( 10, floor( log10( exp10 ) ) );
		if( !qf )
			*out++ = '0';
		while( qf )
		{
			*out++ = '0' + ( exp10 / qf ) % 10;
			qf /= 10;
		}
	}
	return out - obeg;
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
			sgs_Integer I;
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
			size = 1 + (int) floor( log( MAX(1,I) ) / log( radix ) ) + sign;

			if( size < F->padcnt && !F->padrgt )
				_padbuf( B, C, F->padchr, F->padcnt - size );
			for( i = size - 1; i >= 0; --i )
			{
				int cv = ( I / (sgs_Integer) pow( radix, i ) ) % radix;
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
			char data[ 350 ];
			int size, sci = 0;
			sgs_Real R;
			if( !sgs_ParseReal( C, (*psi)++, &R ) )
				goto error;
			if( F->prec < 0 )
				F->prec = 6;

			if( F->type == 'f' ) sci = -1;
			else if( F->type == 'e' || F->type == 'E' ) sci = 1;

			size = _flt_write_rep( data, R, sci, F );

			if( size < F->padcnt && !F->padrgt )
				_padbuf( B, C, F->padchr, F->padcnt - size );
			membuf_appbuf( B, C, data, size );
			if( size < F->padcnt && F->padrgt )
				_padbuf( B, C, F->padchr, F->padcnt - size );
		}
		break;
	case 's': case 'c':
		{
			char* str;
			sgs_SizeVal size;
			if( F->type == 'c' &&
				sgs_Convert( C, (*psi), SVT_STRING ) != SGS_SUCCESS )
				goto error;
			if( !sgs_ParseString( C, (*psi)++, &str, &size ) )
				goto error;
			if( size > F->prec && F->prec >= 0 )
				size = F->prec;
			if( size < F->padcnt && !F->padrgt )
				_padbuf( B, C, F->padchr, F->padcnt - size );
			membuf_appbuf( B, C, str, size );
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
	int ssz = sgs_StackSize( C ), numitem = 0, si = 1;
	
	SGSFN( "fmt_text" );
	
	if( ssz < 1 || !sgs_ParseString( C, 0, &fmt, &fmtsize ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1+ arguments: string, ..." )

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

	sgs_PushStringBuf( C, B.ptr, B.size );
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

static void* sgsstd_fmtstream_functable[];
#define SGSFS_HDR sgsstd_fmtstream_t* hdr = (sgsstd_fmtstream_t*) data->data

#define fs_getreadsize( hdr, lim ) MIN( hdr->buffill - hdr->bufpos, lim )

static int fs_refill( SGS_CTX, sgsstd_fmtstream_t* fs )
{
	int ret, needs = fs->buffill == fs->bufsize || fs->buffill == 0;
	char* str;
	sgs_SizeVal size;
	if( fs->buffill > fs->bufpos )
	{
		memmove( fs->buffer,
			fs->buffer + fs->bufpos,
			fs->buffill - fs->bufpos );
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
			memcpy( fs->buffer + fs->bufpos, str, size );
		fs->buffill += size;
		fs->state = FMTSTREAM_STATE_READ;
		sgs_Pop( C, 1 );
	}
	return 1;
}


static int sgsstd_fmtstream_destroy( SGS_CTX, sgs_VarObj* data, int dco )
{
	SGSFS_HDR;
	sgs_ReleaseOwned( C, &hdr->source, dco );
	sgs_Dealloc( hdr->buffer );
	sgs_Dealloc( hdr );
	return SGS_SUCCESS;
}

#define SGSFS_IHDR( name ) \
	sgs_VarObj* data; \
	sgsstd_fmtstream_t* hdr; \
	SGSFN( "fmtstream." #name ); \
	if( !sgs_Method( C ) || \
		!sgs_IsObject( C, 0, sgsstd_fmtstream_functable ) )\
		STDLIB_WARN( "not called on a fmtstream" ) \
	data = sgs_GetObjectData( C, 0 ); \
	hdr = (sgsstd_fmtstream_t*) data->data; \
	UNUSED( hdr );
/* after this, the counting starts from 1 because of sgs_Method */

static int sgsstd_fmtstreamI_read( SGS_CTX )
{
	sgs_SizeVal numbytes;
	MemBuf B = membuf_create();
	sgs_Integer numbi;
	
	SGSFS_IHDR( read )
	
	if( sgs_StackSize( C ) != 2 ||
		!sgs_ParseInt( C, 1, &numbi ) ||
		numbi < 0 || numbi >= (1LL<<31) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: int [0-2^31)" )
	
	numbytes = numbi;
	if( numbytes )
	{
		while( hdr->state != FMTSTREAM_STATE_END )
		{
			sgs_SizeVal readamt = fs_getreadsize( hdr, numbytes );
			if( readamt )
				membuf_appbuf( &B, C, hdr->buffer + hdr->bufpos, readamt );
			numbytes -= readamt;
			hdr->bufpos += readamt;
			if( numbytes <= 0 )
				break;
			if( !fs_refill( C, hdr ) )
			{
				membuf_destroy( &B, C );
				STDLIB_WARN( "unexpected read error" )
			}
		}
	}
	sgs_PushStringBuf( C, B.ptr, B.size );
	membuf_destroy( &B, C );
	return 1;
}

static int sgsstd_fmtstreamI_getchar( SGS_CTX )
{
	int chr = -1, ssz, asint = 0;
	SGSFS_IHDR( getchar )
	ssz = sgs_StackSize( C );
	
	if( ssz < 1 || ssz > 2 ||
		( ssz == 2 && !sgs_ParseBool( C, 1, &asint ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 0-1 arguments: [bool]" )
	
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
		chr = hdr->buffer[ hdr->bufpos++ ];
		break;
	}
	if( asint )
		sgs_PushInt( C, chr );
	else
	{
		char cc = (uint8_t) chr;
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

static int fs_check_cc( const char* str, sgs_SizeVal size, uint8_t c )
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

static int sgsstd_fmtstreamI_readcc( SGS_CTX )
{
	char* ccstr;
	sgs_SizeVal numbytes, ccsize;
	MemBuf B = membuf_create();
	sgs_Integer numbi = 0x7fffffff;
	int ssz = sgs_StackSize( C );
	
	SGSFS_IHDR( readcc )
	
	if( ssz < 1 || ssz > 2 ||
		!sgs_ParseString( C, 1, &ccstr, &ccsize ) ||
		( ssz >= 2 && ( !sgs_ParseInt( C, 2, &numbi ) ||
		numbi < 0 || numbi >= (1LL<<31) ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1-2 arguments: string[, int [0-2^31)]" )

	if( !fs_validate_cc( C, ccstr, ccsize ) )
		STDLIB_WARN( "error in character class" )
	
	numbytes = numbi;
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
				membuf_appchr( &B, C, c );
			}
			numbytes -= readamt;
			hdr->bufpos += readamt;
			if( numbytes <= 0 )
				break;
			if( !fs_refill( C, hdr ) )
			{
				membuf_destroy( &B, C );
				STDLIB_WARN( "unexpected read error" )
			}
		}
	}
	sgs_PushStringBuf( C, B.ptr, B.size );
	membuf_destroy( &B, C );
	return 1;
}

static int sgsstd_fmtstreamI_skipcc( SGS_CTX )
{
	char* ccstr;
	sgs_SizeVal numbytes, ccsize, numsc = 0;
	sgs_Integer numbi = 0x7fffffff;
	int ssz = sgs_StackSize( C );
	
	SGSFS_IHDR( skipcc )
	
	if( ssz < 1 || ssz > 2 ||
		!sgs_ParseString( C, 1, &ccstr, &ccsize ) ||
		( ssz >= 2 && ( !sgs_ParseInt( C, 2, &numbi ) ||
		numbi < 0 || numbi >= (1LL<<31) ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1-2 arguments: string[, int [0-2^31)]" )

	if( !fs_validate_cc( C, ccstr, ccsize ) )
		STDLIB_WARN( "error in character class" )
	
	numbytes = numbi;
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
			if( !fs_refill( C, hdr ) )
			{
				STDLIB_WARN( "fmtstream.skipcc(): unexpected read error" )
			}
		}
	}
	sgs_PushInt( C, numsc );
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
	if( 0 == strcmp( str, "read" ) ) IFN( sgsstd_fmtstreamI_read )
	else if( 0 == strcmp( str, "getchar" ) ) IFN( sgsstd_fmtstreamI_getchar )
	else if( 0 == strcmp( str, "readcc" ) ) IFN( sgsstd_fmtstreamI_readcc )
	else if( 0 == strcmp( str, "skipcc" ) ) IFN( sgsstd_fmtstreamI_skipcc )
	
	else if( 0 == strcmp( str, "at_end" ) ){
		sgs_PushBool( C, hdr->state == FMTSTREAM_STATE_END );
		return SGS_SUCCESS; }
	else if( 0 == strcmp( str, "stream_offset" ) ){
		sgs_PushInt( C, hdr->streamoff + hdr->bufpos );
		return SGS_SUCCESS; }
	
	return SGS_ENOTFND;
}

static void* sgsstd_fmtstream_functable[] =
{
	SOP_DESTRUCT, sgsstd_fmtstream_destroy,
	SOP_GETINDEX, sgsstd_fmtstream_getindex,
};

static int sgsstd_fmt_parser( SGS_CTX )
{
	sgs_Integer bufsize = 1024;
	int ssz = sgs_StackSize( C );
	
	SGSFN( "fmt_parser" );
	
	if( ssz < 1 || ssz > 2 ||
		( ssz >= 2 && !sgs_ParseInt( C, 1, &bufsize ) ) ||
		bufsize <= 0 )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1-2 arguments: <source>[, int(>0)]" )

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
		hdr->buffer = sgs_Alloc_n( char, hdr->bufsize );
		hdr->buffill = 0;
		hdr->bufpos = 0;
		hdr->state = FMTSTREAM_STATE_INIT;
		sgs_PushObject( C, hdr, sgsstd_fmtstream_functable );
		return 1;
	}
}


#define FN( x ) { #x, sgsstd_##x }

static const sgs_RegFuncConst f_fconsts[] =
{
	FN( fmt_pack ), FN( fmt_pack_count ),
	FN( fmt_unpack ), FN( fmt_pack_size ),
	FN( fmt_base64_encode ), FN( fmt_base64_decode ),
	FN( fmt_text ), FN( fmt_parser ),
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

#define FILE_READ 1
#define FILE_WRITE 2

#define FST_UNKNOWN 0
#define FST_FILE 1
#define FST_DIR 2

static int sgsstd_io_setcwd( SGS_CTX )
{
	int ret;
	char* str;
	sgs_SizeVal size;
	
	SGSFN( "io_setcwd" );

	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: string" )

	ret = chdir( str );
	sgs_PushBool( C, ret == 0 );
	return 1;
}

static int sgsstd_io_getcwd( SGS_CTX )
{
	SGSFN( "io_getcwd" );
	
	if( sgs_StackSize( C ) != 0 )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 0 arguments" )

	{
		/* XPC WARNING: getcwd( NULL, 0 ) relies on undefined behavior */
		char* cwd = getcwd( NULL, 0 );
		if( cwd )
		{
			sgs_PushString( C, cwd );
			free( cwd );
			return 1;
		}
		else
			return 0;
	}
}

static int sgsstd_io_rename( SGS_CTX )
{
	int ret;
	char* path, *nnm;
	sgs_SizeVal psz, nnmsz;
	
	SGSFN( "io_rename" );

	if( sgs_StackSize( C ) != 2 ||
		!sgs_ParseString( C, 0, &path, &psz ) ||
		!sgs_ParseString( C, 1, &nnm, &nnmsz ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 2 arguments: string, string" )

	if( !path_replast( C, 0, 1 ) ||
		!sgs_ParseString( C, -1, &nnm, &nnmsz ) )
	{
		sgs_PushBool( C, FALSE );
		return 1;
	}

	ret = rename( path, nnm );
	sgs_PushBool( C, ret == 0 );
	return 1;
}

static int sgsstd_io_file_exists( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	
	SGSFN( "io_file_exists" );

	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: string" )

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

	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: string" )

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

	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: string" )

	{
		struct stat data;
		if( stat( str, &data ) != 0 )
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
		return !sgs_GlobalCall( C, "dict", 10, 1 );
	}
}

static int sgsstd_io_dir_create( SGS_CTX )
{
	int ret;
	char* str;
	sgs_SizeVal size;
	
	SGSFN( "io_dir_create" );

	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: string" )

	ret = mkdir( str
#ifndef WIN32
		,0777
#endif
	);
	sgs_PushBool( C, ret == 0 );
	return 1;
}

static int sgsstd_io_dir_delete( SGS_CTX )
{
	int ret;
	char* str;
	sgs_SizeVal size;
	
	SGSFN( "io_dir_delete" );

	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: string" )

	ret = rmdir( str );
	sgs_PushBool( C, ret == 0 );
	return 1;
}

static int sgsstd_io_file_delete( SGS_CTX )
{
	int ret;
	char* str;
	sgs_SizeVal size;
	
	SGSFN( "io_delete" );

	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: string" )

	ret = remove( str );
	sgs_PushBool( C, ret == 0 );
	return 1;
}

static int sgsstd_io_file_write( SGS_CTX )
{
	char* path, *data;
	sgs_SizeVal psz, dsz;
	
	SGSFN( "io_file_write" );

	if( sgs_StackSize( C ) != 2 ||
		!sgs_ParseString( C, 0, &path, &psz ) ||
		!sgs_ParseString( C, 1, &data, &dsz ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 2 arguments: string, string" )

	{
		sgs_SizeVal wsz;
		FILE* fp = fopen( path, "wb" );
		if( !fp )
			STDLIB_WARN( "failed to create file" )
		wsz = fwrite( data, 1, dsz, fp );
		fclose( fp );
		if( wsz < dsz )
			STDLIB_WARN( "failed to write to file" )
	}

	sgs_PushBool( C, TRUE );
	return 1;
}

static int sgsstd_io_file_read( SGS_CTX )
{
	char* path;
	sgs_SizeVal psz;
	
	SGSFN( "io_file_read" );

	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &path, &psz ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: string" )

	{
		sgs_SizeVal len, rd;
		FILE* fp = fopen( path, "rb" );
		if( !fp )
			STDLIB_WARN( "failed to open file" )
		fseek( fp, 0, SEEK_END );
		len = ftell( fp );
		fseek( fp, 0, SEEK_SET );

		sgs_PushStringBuf( C, NULL, len );
		rd = fread( sgs_GetStringPtr( C, -1 ), 1, len, fp );
		fclose( fp );
		if( rd < len )
			STDLIB_WARN( "failed to read file" )

		return 1;
	}
}


#define FVAR (*(FILE**)&data->data)
#define FVNO_BEGIN if( FVAR ) {
#define FVNO_END( name ) } else \
	STDLIB_WARN( "file." #name "() - file is not opened" )

static void* sgsstd_file_functable[];

#define FIF_INIT( fname ) \
	sgs_VarObj* data; \
	SGSFN( "file." #fname ); \
	if( !sgs_Method( C ) || \
		sgs_ItemType( C, 0 ) != SVT_OBJECT || \
		( data = sgs_GetObjectData( C, 0 ) )->iface != sgsstd_file_functable ) \
		STDLIB_WARN( "expected file as 'this'" )


static int sgsstd_fileP_offset( SGS_CTX, FILE* fp )
{
	if( !fp )
		STDLIB_WARN( "file.offset - file is not opened" )
	
	sgs_PushInt( C, ftell( fp ) );
	return SGS_SUCCESS;
}

static int sgsstd_fileP_size( SGS_CTX, FILE* fp )
{
	if( !fp )
		STDLIB_WARN( "file.size - file is not opened" )

	{
		long pos = ftell( fp );
		fseek( fp, 0, SEEK_END );
		sgs_PushInt( C, ftell( fp ) );
		fseek( fp, pos, SEEK_SET );
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
	sgs_Integer flags;

	FIF_INIT( open )

	if( sgs_StackSize( C ) != 3 ||
		!sgs_ParseString( C, 1, &path, NULL ) ||
		!sgs_ParseInt( C, 2, &flags ) ||
		!( ff = flags & ( FILE_READ | FILE_WRITE ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 2 arguments: string, int (!= 0)" )

	if( FVAR )
		fclose( FVAR );

	FVAR = fopen( path, g_io_fileflagmodes[ ff ] );

	sgs_PushBool( C, !!FVAR );
	return 1;
}

static int sgsstd_fileI_close( SGS_CTX )
{
	int res = 0;

	FIF_INIT( close )

	if( sgs_StackSize( C ) != 1 )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 0 arguments" )

	if( FVAR )
	{
		res = 1;
		fclose( FVAR );
		FVAR = NULL;
	}

	sgs_PushBool( C, res );
	return 1;
}

static int sgsstd_fileI_read( SGS_CTX )
{
	MemBuf mb = membuf_create();
	char bfr[ 1024 ];
	sgs_Integer size;
	FIF_INIT( read )

	if( sgs_StackSize( C ) != 2 ||
		!sgs_ParseInt( C, 1, &size ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: int" )

	FVNO_BEGIN
		while( size > 0 )
		{
			int read = fread( bfr, 1, MIN( size, 1024 ), FVAR );
			if( read <= 0 )
				break;
			membuf_appbuf( &mb, C, bfr, read );
			size -= 1024;
		}
		sgs_PushStringBuf( C, mb.ptr, mb.size );
		membuf_destroy( &mb, C );
		return 1;
	FVNO_END( write )
}

static int sgsstd_fileI_write( SGS_CTX )
{
	char* str;
	sgs_SizeVal strsize;

	FIF_INIT( write )

	if( sgs_StackSize( C ) != 2 ||
		!sgs_ParseString( C, 1, &str, &strsize ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: string" )

	FVNO_BEGIN
		sgs_PushBool( C, fwrite( str, strsize, 1, FVAR ) );
		return 1;
	FVNO_END( write )
}

#define SGS_SEEK_SET 0
#define SGS_SEEK_CUR 1
#define SGS_SEEK_END 2
static int sgsstd_fileI_seek( SGS_CTX )
{
	sgs_Integer off, mode = SEEK_SET;
	FIF_INIT( seek )
	int ssz = sgs_StackSize( C );
	int seekmodes[ 3 ] = { SEEK_SET, SEEK_CUR, SEEK_END };

	if( ssz < 2 || ssz > 3 ||
		!sgs_ParseInt( C, 1, &off ) ||
		( ssz >= 3 && !sgs_ParseInt( C, 2, &mode ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1-2 arguments: int[, int]" )

	if( mode < 0 || mode > 2 )
		STDLIB_WARN( "'mode' not one of SEEK_(SET|CUR|END)" )

	FVNO_BEGIN
		sgs_PushBool( C, !fseek( FVAR, off, seekmodes[ mode ] ) );
		return 1;
	FVNO_END( eof )
}

static int sgsstd_fileI_flush( SGS_CTX )
{
	FIF_INIT( close )

	if( sgs_StackSize( C ) != 1 )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 0 arguments" )

	FVNO_BEGIN
		sgs_PushBool( C, !fflush( FVAR ) );
		return 1;
	FVNO_END( flush )
}

static int sgsstd_fileI_setbuf( SGS_CTX )
{
	sgs_Integer size;
	FIF_INIT( setbuf )

	if( sgs_StackSize( C ) != 2 ||
		!sgs_ParseInt( C, 1, &size ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: int" )

	FVNO_BEGIN
		sgs_PushBool( C, !setvbuf( FVAR, NULL, size ? _IOFBF : _IONBF, size ) );
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

	if( 0 == strcmp( str, "offset" ) ){ return sgsstd_fileP_offset( C, FVAR ); }
	if( 0 == strcmp( str, "size" ) ){ return sgsstd_fileP_size( C, FVAR ); }
	if( 0 == strcmp( str, "error" ) ){ return sgsstd_fileP_error( C, FVAR ); }
	if( 0 == strcmp( str, "eof" ) ){ return sgsstd_fileP_eof( C, FVAR ); }

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
	if( FVAR )
		fclose( FVAR );
	return SGS_SUCCESS;
}

static int sgsstd_file_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	UNUSED( data );
	if( type == SVT_BOOL )
	{
		sgs_PushBool( C, !!FVAR );
		return SGS_SUCCESS;
	}
	if( type == SVT_STRING || type == SGS_CONVOP_TOTYPE )
	{
		sgs_PushString( C, "file" );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}


static void* sgsstd_file_functable[] =
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
	sgs_Integer flags;
	FILE* fp = NULL;
	
	SGSFN( "io_file" );

	if( sgs_StackSize( C ) == 0 )
		goto pushobj;

	if( sgs_StackSize( C ) != 2 ||
		!sgs_ParseString( C, 0, &path, NULL ) ||
		!sgs_ParseInt( C, 1, &flags ) ||
		!( ff = flags & ( FILE_READ | FILE_WRITE ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 0 or 2 arguments: string, int (!= 0)" )

	fp = fopen( path, g_io_fileflagmodes[ ff ] );

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

static void* sgsstd_dir_functable[];

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
		v.type = SVT_OBJECT;
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

static void* sgsstd_dir_functable[] =
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

	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &path, NULL ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: string" )

	dp = opendir( path );
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

#define FN( x ) { #x, sgsstd_##x }

static const sgs_RegFuncConst i_fconsts[] =
{
	FN( io_getcwd ), FN( io_setcwd ),
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

static int sgsstd_pow( SGS_CTX )
{
	sgs_Real b, e;
	
	SGSFN( "pow" );
	
	if( sgs_StackSize( C ) != 2 ||
		!sgs_ParseReal( C, 0, &b ) ||
		!sgs_ParseReal( C, 1, &e ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 2 arguments: real, real" )
	if( ( b < 0 && e != (sgs_Real) (sgs_Integer) e )
		|| ( b == 0 && e < 0 ) )
		STDLIB_WARN( "mathematical error" )
	sgs_PushReal( C, pow( b, e ) );
	return 1;
}

static int sgsstd_sqrt( SGS_CTX )
{
	sgs_Real arg0;
	
	SGSFN( "sqrt" );
	
	if( sgs_StackSize( C ) != 1 || !sgs_ParseReal( C, 0, &arg0 ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: real" )
	if( arg0 < 0 )
		STDLIB_WARN( "mathematical error" )
	sgs_PushReal( C, sqrtf( arg0 ) );
	return 1;
}
static int sgsstd_asin( SGS_CTX )
{
	sgs_Real arg0;
	
	SGSFN( "asin" );
	
	if( sgs_StackSize( C ) != 1 || !sgs_ParseReal( C, 0, &arg0 ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: real" )
	if( arg0 < -1 || arg0 > 1 )
		STDLIB_WARN( "mathematical error" )
	sgs_PushReal( C, asin( arg0 ) );
	return 1;
}
static int sgsstd_acos( SGS_CTX )
{
	sgs_Real arg0;
	
	SGSFN( "acos" );
	
	if( sgs_StackSize( C ) != 1 || !sgs_ParseReal( C, 0, &arg0 ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: real" )
	if( arg0 < -1 || arg0 > 1 )
		STDLIB_WARN( "mathematical error" )
	sgs_PushReal( C, acos( arg0 ) );
	return 1;
}

static int sgsstd_log( SGS_CTX )
{
	sgs_Real x, b;
	
	SGSFN( "log" );
	
	if( sgs_StackSize( C ) != 2 ||
		!sgs_ParseReal( C, 0, &x ) ||
		!sgs_ParseReal( C, 1, &b ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 2 arguments: real, real" )
	if( x <= 0 || b <= 0 || b == 1 )
		STDLIB_WARN( "mathematical error" )
	sgs_PushReal( C, log( x ) / log( b ) );
	return 1;
}

#define MATHFUNC_CN( name, orig ) \
static int sgsstd_##name( SGS_CTX ) { \
	sgs_Real arg0; \
	SGSFN( #name ); \
	if( sgs_StackSize( C ) != 1 || \
		!sgs_ParseReal( C, 0, &arg0 ) ) \
		STDLIB_WARN( "unexpected arguments; " \
			"function expects 1 argument: real" ) \
	sgs_PushReal( C, orig( arg0 ) ); \
	return 1; }
#define MATHFUNC( name ) MATHFUNC_CN( name, name )

#define MATHFUNC2_CN( name, orig ) \
static int sgsstd_##name( SGS_CTX ) { \
	sgs_Real arg0, arg1; \
	SGSFN( #name ); \
	if( sgs_StackSize( C ) != 2 || \
		!sgs_ParseReal( C, 0, &arg0 ) || \
		!sgs_ParseReal( C, 1, &arg1 ) ) \
		STDLIB_WARN( "unexpected arguments; " \
			"function expects 2 arguments: real, real" ) \
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


static const sgs_RegRealConst m_rconsts[] =
{
	{ "M_PI", SGS_PI },
	{ "M_E", SGS_E },
};

#define FN( x ) { #x, sgsstd_##x }

static const sgs_RegFuncConst m_fconsts[] =
{
	FN( abs ), FN( floor ), FN( ceil ), FN( round ),
	FN( pow ), FN( sqrt ), FN( log ),
	FN( sin ), FN( cos ), FN( tan ),
	FN( asin ), FN( acos ), FN( atan ), FN( atan2 ),
};

SGSRESULT sgs_LoadLib_Math( SGS_CTX )
{
	int ret;
	ret = sgs_RegRealConsts( C, m_rconsts, ARRAY_SIZE( m_rconsts ) );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_RegFuncConsts( C, m_fconsts, ARRAY_SIZE( m_fconsts ) );
	return ret;
}



#if 0
/* libraries -  N A T I V E  */

static int sgsstd_pointerI_tobool( SGS_CTX, sgs_VarObj* data )
{
	return sgs_PushBool( C, data->data != NULL );
}

static int sgsstd_pointerI_toint( SGS_CTX, sgs_VarObj* data )
{
	return sgs_PushInt( C, (sgs_Integer) (size_t) data->data );
}

static int sgsstd_pointerI_tostring( SGS_CTX, sgs_VarObj* data )
{
	char buf[ 32 ];
	sprintf( buf, "pointer (%p)", data->data );
	return sgs_PushString( C, buf );
}

static int sgsstd_pointerI_gettype( SGS_CTX, sgs_VarObj* data )
{
	UNUSED( data );
	return sgs_PushString( C, "native_pointer" );
}

void* sgsstd_pointer_iface[] =
{
	SOP_TOBOOL, sgsstd_pointerI_tobool,
	SOP_TOINT, sgsstd_pointerI_toint,
	SOP_TOSTRING, sgsstd_pointerI_tostring,
	SOP_GETTYPE, sgsstd_pointerI_gettype,
	SOP_END
};

static int sgsstd_native_pointer( SGS_CTX )
{
	void* val = 0;

	if( sgs_StackSize( C ) > 1 )
		STDLIB_WARN( "native_pointer() - unexpected arguments; "
			"function expects 0-1 arguments: [int]" )

	if( sgs_StackSize( C ) == 1 )
		val = (void*) (size_t) sgs_ToInt( C, 0 );

	sgs_PushObject( C, val, sgsstd_pointer_iface );
	return SGS_SUCCESS;
}


#define NTYPE_VOID   0
#define NTYPE_CHAR   1
#define NTYPE_UCHAR  2
#define NTYPE_SHORT  3
#define NTYPE_USHORT 4
#define NTYPE_INT    5
#define NTYPE_UINT   6
#define NTYPE_LONG   7
#define NTYPE_ULONG  8
#define NTYPE_LLONG  9
#define NTYPE_ULLONG 10
#define NTYPE_FLOAT  11
#define NTYPE_DOUBLE 12
#define NTYPE_LDBL   13
#define NTYPE_BOOL   14
#define NTYPE_SIZE_T 15
#define NTYPE_PTRDIFF_T 16
#define NTYPE_V8     17
#define NTYPE_V16    18
#define NTYPE_V32    19
#define NTYPE_V64    20
#define NTYPE_POINTER 21

#define NCALL_CDECL   1
#define NCALL_STDCALL 2

typedef
struct sgsstd_nfunc_hdr_s
{
	sgs_Variable fname;
	void* funcptr;
	int ret;
	int* args;
	int argc;
}
sgsstd_nfunc_hdr;

#define SGSNFUNC_HDR sgsstd_nfunc_hdr* hdr = (sgsstd_nfunc_hdr*) data->data

static int sgsstd_nfuncI_destruct( SGS_CTX, sgs_VarObj* data )
{
	SGSNFUNC_HDR;
	sgs_Release( C, &hdr->fname );
	if( hdr->args )
		sgs_Free( hdr->args );
	sgs_Free( hdr );
	return SGS_SUCCESS;
}

static int sgsstd_nfuncI_call( SGS_CTX, sgs_VarObj* data )
{
	return SGS_SUCCESS;
}

static int sgsstd_nfuncI_tostring( SGS_CTX, sgs_VarObj* data )
{
	SGSNFUNC_HDR;
	return
		sgs_PushString( C, "native function (" ) || 
		sgs_PushVariable( C, &hdr->fname ) ||
		sgs_PushString( C, ")" ) ||
		sgs_StringMultiConcat( C, 3 );
}

static int sgsstd_nfuncI_gettype( SGS_CTX, sgs_VarObj* data )
{
	UNUSED( data );
	return sgs_PushString( C, "native_function" );
}

void* sgsstd_nfunc_iface[] =
{
	SOP_DESTRUCT, sgsstd_nfuncI_destruct,
	SOP_CALL, sgsstd_nfuncI_call,
	SOP_TOSTRING, sgsstd_nfuncI_tostring,
	SOP_GETTYPE, sgsstd_nfuncI_gettype,
	SOP_END
};


static int sgsstd_native_import_symbol( SGS_CTX )
{
	int i, argc = sgs_StackSize( C ) - 4;
	char* fnstr, *pnstr;
	sgs_SizeVal fnsize, pnsize;
	sgs_Integer cty = 0, rty = 0;
	sgsstd_nfunc_hdr proto, *nfunc;

	if( argc < 0 ||
		!sgs_ParseString( C, 0, &fnstr, &fnsize ) ||
		!sgs_ParseString( C, 1, &pnstr, &pnsize ) ||
		!sgs_ParseInt( C, 2, &cty ) ||
		!sgs_ParseInt( C, 3, &rty ) )
		STDLIB_WARN( "native_import_symbol() - unexpected arguments; "
			"function expects 4+ arguments: string, string, int, int, [int]+" )

	if( cty < NCALL_CDECL || cty > NCALL_STDCALL )
		STDLIB_WARN( "native_import_symbol() - invalid call type" )

	if( rty < NTYPE_VOID || rty > NTYPE_POINTER )
		STDLIB_WARN( "native_import_symbol() - invalid return type" )

	if( argc )
		proto.args = sgs_Alloc_n( int, argc );
	else
		proto.args = NULL;
	for( i = 0; i < argc; ++i )
	{
		sgs_Integer aty = 0;
		if( !sgs_ParseInt( C, i + 4, &aty ) ||
			aty < NTYPE_VOID || aty > NTYPE_POINTER )
		{
			char ebuf[ 32 ];
			sprintf( ebuf, "native_import_symbol() - "
				"invalid argument %d type", i + 1 );
			sgs_Free( proto.args );
			STDLIB_WARN( ebuf )
		}
		proto.args[ i ] = aty;
	}

	i = sgs_GetProcAddress( fnstr, pnstr, &proto.funcptr );
	if( i != 0 )
	{
		sgs_Free( proto.args );

		if( i == SGS_XPC_NOFILE ) STDLIB_WARN( 
			"native_import_symbol() - file not found" )
		else if( i == SGS_XPC_NOPROC ) STDLIB_WARN(
			"native_import_symbol() - procedure not found" )
		else if( i == SGS_XPC_NOTSUP ) STDLIB_WARN( "native_import_symbol()"
			" - feature is not supported on this platform" )
		else STDLIB_WARN( "native_import_symbol() - unknown error occured" )
	}

	proto.argc = argc;
	proto.ret = rty;
	proto.fname = *sgs_StackItem( C, 1 );
	sgs_Acquire( C, &proto.fname );

	nfunc = sgs_Alloc( sgsstd_nfunc_hdr );
	memcpy( nfunc, &proto, sizeof( proto ) );
	sgs_PushObject( C, nfunc, sgsstd_nfunc_iface );
	return SGS_SUCCESS;
}

static const sgs_RegIntConst n_iconsts[] =
{
#define DEFTYPE( name ) { "NTYPE_" #name, NTYPE_##name }
	DEFTYPE( VOID ),
	DEFTYPE( CHAR ), DEFTYPE( UCHAR ),
	DEFTYPE( SHORT ), DEFTYPE( USHORT ),
	DEFTYPE( INT ), DEFTYPE( UINT ),
	DEFTYPE( LONG ), DEFTYPE( ULONG ),
	DEFTYPE( LLONG ), DEFTYPE( ULLONG ),
	DEFTYPE( FLOAT ), DEFTYPE( DOUBLE ), DEFTYPE( LDBL ),
	DEFTYPE( BOOL ),
	DEFTYPE( SIZE_T ), DEFTYPE( PTRDIFF_T ),
	DEFTYPE( V8 ), DEFTYPE( V16 ), DEFTYPE( V32 ),
	DEFTYPE( V64 ), DEFTYPE( POINTER ),
#undef DEFTYPE
	{ "eNCALL_CDECL", NCALL_CDECL },
	{ "eNCALL_STDCALL", NCALL_STDCALL },
};

static const sgs_RegFuncConst n_fconsts[] =
{
	FN( native_pointer ), FN( native_import_symbol ),
};

SGSRESULT sgs_LoadLib_Native( SGS_CTX )
{
	int ret;
	ret = sgs_RegIntConsts( C, n_iconsts, ARRAY_SIZE( n_iconsts ) );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_RegFuncConsts( C, n_fconsts, ARRAY_SIZE( n_fconsts ) );
	return ret;
}
#endif



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
	
	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, NULL ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: string" )

	sgs_PushInt( C, system( str ) );
	return 1;
}

static int sgsstd_os_getenv( SGS_CTX )
{
	char* str;
	
	SGSFN( "os_getenv" );

	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, NULL ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: string" )

	str = getenv( str );
	if( str )
		sgs_PushString( C, str );
	return !!str;
}

static int sgsstd_os_putenv( SGS_CTX )
{
	char* str;
	
	SGSFN( "os_putenv" );

	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, NULL ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: string" )

	sgs_PushBool( C, putenv( str ) == 0 );
	return 1;
}


static int sgsstd_os_time( SGS_CTX )
{
	time_t ttv;
	sgs_Real tz = 0;
	sgs_Integer outsecs = 0;
	int ssz = sgs_StackSize( C );
	
	SGSFN( "os_time" );
	
	if( ssz < 0 || ssz > 1 ||
		( ssz >= 1 && !sgs_ParseReal( C, 0, &tz ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 optional argument - int" )
	
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
	int asstr = 0, ssz = sgs_StackSize( C );
	
	SGSFN( "os_get_timezone" );
	
	if( ssz < 0 || ssz > 1 ||
		( ssz >= 1 && !sgs_ParseBool( C, 0, &asstr ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 optional argument - bool" )

	{
		double diff;
		time_t ttv, t1, t2;
		/* magic */
		time( &ttv );
		t1 = mktime( gmtime( &ttv ) );
		t2 = mktime( localtime( &ttv ) );
		diff = difftime( t2, t1 ) / 3600;
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
	at[0] = '0' + ( ( what / 10 ) % 10 );
	at[1] = '0' + ( what % 10 );
}

static void put4digs( char* at, int what )
{
	at[0] = '0' + ( ( what / 1000 ) % 10 );
	at[1] = '0' + ( ( what / 100 ) % 10 );
	at[2] = '0' + ( ( what / 10 ) % 10 );
	at[3] = '0' + ( what % 10 );
}

static int sgsstd_os_date_string( SGS_CTX )
{
	time_t ttv;
	char* fmt, *fmtend;
	sgs_SizeVal fmtsize;
	sgs_Integer uts;
	struct tm T;
	MemBuf B = membuf_create();
	int ssz = sgs_StackSize( C );
	
	SGSFN( "os_date_string" );
	
	if( ssz < 1 || ssz > 2 ||
		!sgs_ParseString( C, 0, &fmt, &fmtsize ) ||
		( ssz >= 2 && !sgs_ParseInt( C, 1, &uts ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1-2 arguments: string[, int]" )

	{
		int Y, M, D, H, m, s;
		if( ssz < 2 )
			time( &ttv );
		else
			ttv = uts;
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
						char fmt[3] = { '%', c, 0 };
						strftime( pbuf, 256, fmt, &T );
						membuf_appbuf( &B, C, pbuf, strlen( pbuf ) );
					}
					break;
				case 'C': put2digs( swp, Y / 100 ); sz = 2; break;
				case 'd': put2digs( swp, D ); sz = 2; break;
				case 'e':
					if( D > 9 )
						swp[0] = '0' + ( ( D / 10 ) % 10 );
					else swp[0] = ' ';
					swp[1] = '0' + ( D % 10 );
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
					swp[2] = '0' + ( T.tm_yday + 1 ) % 10;
					sz = 3; break;
				case 'm': put2digs( swp, M ); sz = 2; break;
				case 'M': put2digs( swp, m ); sz = 2; break;
				case 'p': membuf_appbuf( &B, C, "AMPM" + H/12*2, 2 ); break;
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
				case 'u': swp[0] = '0' + T.tm_wday; sz = 1; break;
				case 'w': swp[0] = '0'+(T.tm_wday?T.tm_wday:7); sz = 1; break;
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
					sprintf( swp, "%" PRId64, (sgs_Integer) ttv );
					sz = strlen( swp ); break;
				/* leftovers */
				case '%': membuf_appchr( &B, C, '%' ); break;
				default: membuf_appbuf( &B, C, fmt - 2, 2 ); break;
				}
				if( sz )
					membuf_appbuf( &B, C, swp, sz );
			}
			else
				membuf_appchr( &B, C, c );
		}

		sgs_PushStringBuf( C, B.ptr, B.size );
		membuf_destroy( &B, C );
		return 1;
	}
}

static int sgsstd_os_parse_time( SGS_CTX )
{
	time_t ttv;
	sgs_Integer uts;
	struct tm T;
	int ssz = sgs_StackSize( C );
	
	SGSFN( "os_parse_time" );
	
	if( ssz < 0 || ssz > 1 ||
		( ssz >= 1 && !sgs_ParseInt( C, 1, &uts ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1-2 arguments: string[, int]" )

	if( ssz >= 1 )
		ttv = uts;
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
	sgs_Integer p[6];
	struct tm T = {0};
	int ssz = sgs_StackSize( C );
	
	SGSFN( "os_make_time" );
	
	if( ssz < 1 || ssz > 6 ||
		( ssz >= 1 && !sgs_ParseInt( C, 0, p+0 ) ) ||
		( ssz >= 2 && !sgs_ParseInt( C, 1, p+1 ) ) ||
		( ssz >= 3 && !sgs_ParseInt( C, 2, p+2 ) ) ||
		( ssz >= 4 && !sgs_ParseInt( C, 3, p+3 ) ) ||
		( ssz >= 5 && !sgs_ParseInt( C, 4, p+4 ) ) ||
		( ssz >= 6 && !sgs_ParseInt( C, 5, p+5 ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1-6 arguments of type 'int'" )

	if( ssz >= 1 ) T.tm_sec = p[0];
	if( ssz >= 2 ) T.tm_min = p[1];
	if( ssz >= 3 ) T.tm_hour = p[2];
	if( ssz >= 4 ) T.tm_mday = p[3];
	if( ssz >= 5 ) T.tm_mon = p[4] - 1;
	if( ssz >= 6 ) T.tm_year = p[5] - 1900;
	sgs_PushInt( C, mktime( &T ) );
	return 1;
}


static const sgs_RegFuncConst o_fconsts[] =
{
	FN( os_gettype ), FN( os_command ),
	FN( os_getenv ), FN( os_putenv ),
	FN( os_time ), FN( os_get_timezone ), FN( os_date_string ),
	FN( os_parse_time ), FN( os_make_time ),
};

SGSRESULT sgs_LoadLib_OS( SGS_CTX )
{
	int ret;
	ret = sgs_RegFuncConsts( C, o_fconsts, ARRAY_SIZE( o_fconsts ) );
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
	int argc;
	char* str;
	sgs_SizeVal size;
	sgs_Integer flags = 0, i1, i2;
	
	SGSFN( "string_cut" );

	argc = sgs_StackSize( C );
	if( argc < 2 || argc > 4 ||
		!sgs_ParseString( C, 0, &str, &size ) ||
		( i2 = size - 1 ) < 0 || /* comparison should always fail */
		!sgs_ParseInt( C, 1, &i1 ) ||
		( argc >= 3 && !sgs_ParseInt( C, 2, &i2 ) ) ||
		( argc >= 4 && !sgs_ParseInt( C, 3, &flags ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 2-4 arguments: string, int, [int], [int]" );

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
		sgs_PushStringBuf( C, str + i1, i2 - i1 + 1 );
	}
	return 1;
}

static int sgsstd_string_part( SGS_CTX )
{
	int argc;
	char* str;
	sgs_SizeVal size;
	sgs_Integer flags = 0, i1, i2;
	
	SGSFN( "string_part" );

	argc = sgs_StackSize( C );
	if( argc < 2 || argc > 4 ||
		!sgs_ParseString( C, 0, &str, &size ) ||
		!sgs_ParseInt( C, 1, &i1 ) ||
		( i2 = size - i1 ) &0 || /* test should always fail */
		( argc >= 3 && !sgs_ParseInt( C, 2, &i2 ) ) ||
		( argc >= 4 && !sgs_ParseInt( C, 3, &flags ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 2-4 arguments: string, int, [int], [int]" );

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
		sgs_PushStringBuf( C, str + i1, i2 - i1 + 1 );
	}
	return 1;
}

static int sgsstd_string_reverse( SGS_CTX )
{
	int argc;
	char* str, *sout;
	sgs_SizeVal size, i;
	
	SGSFN( "string_reverse" );

	argc = sgs_StackSize( C );
	if( argc != 1 || !sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: string" );

	sgs_PushStringBuf( C, NULL, size );
	sout = sgs_GetStringPtr( C, -1 );

	for( i = 0; i < size; ++i )
		sout[ size - i - 1 ] = str[ i ];

	return 1;
}

static int sgsstd_string_pad( SGS_CTX )
{
	int argc;
	char* str, *pad = " ", *sout;
	sgs_SizeVal size, padsize = 1;
	sgs_Integer tgtsize, flags = sgsRIGHT, lpad = 0, i;
	
	SGSFN( "string_pad" );

	argc = sgs_StackSize( C );
	if( ( argc < 2 || argc > 4 ) ||
		!sgs_ParseString( C, 0, &str, &size ) ||
		!sgs_ParseInt( C, 1, &tgtsize ) ||
		( argc >= 3 && !sgs_ParseString( C, 2, &pad, &padsize ) ) ||
		( argc >= 4 && !sgs_ParseInt( C, 3, &flags ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 2-4 arguments: string, int, [string], [int]" );

	if( tgtsize <= size || !FLAG( flags, sgsLEFT | sgsRIGHT ) )
	{
		sgs_PushItem( C, 0 );
		return 1;
	}

	sgs_PushStringBuf( C, NULL, tgtsize );
	sout = sgs_GetStringPtr( C, -1 );
	if( FLAG( flags, sgsLEFT ) )
	{
		if( FLAG( flags, sgsRIGHT ) )
		{
			sgs_Integer pp = tgtsize - size;
			lpad = pp / 2 + pp % 2;
		}
		else
			lpad = tgtsize - size;
	}

	memcpy( sout + lpad, str, size );
	for( i = 0; i < lpad; ++i )
		sout[ i ] = pad[ i % padsize ];
	size += lpad;
	while( size < tgtsize )
	{
		sout[ size ] = pad[ size % padsize ];
		size++;
	}

	return 1;
}

static int sgsstd_string_repeat( SGS_CTX )
{
	int argc;
	char* str, *sout;
	sgs_SizeVal size;
	sgs_Integer count;
	
	SGSFN( "string_repeat" );

	argc = sgs_StackSize( C );
	if( argc != 2 ||
		!sgs_ParseString( C, 0, &str, &size ) ||
		!sgs_ParseInt( C, 1, &count ) || count < 0 )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 2 arguments: string, int (>= 0)" );

	sgs_PushStringBuf( C, NULL, count * size );
	sout = sgs_GetStringPtr( C, -1 );
	while( count-- )
	{
		memcpy( sout, str, size );
		sout += size;
	}
	return 1;
}

static int sgsstd_string_count( SGS_CTX )
{
	int argc, overlap = FALSE;
	char* str, *sub, *strend;
	sgs_SizeVal size, subsize, ret = 0;
	
	SGSFN( "string_count" );

	argc = sgs_StackSize( C );
	if( argc < 2 || argc > 3 ||
		!sgs_ParseString( C, 0, &str, &size ) ||
		!sgs_ParseString( C, 1, &sub, &subsize ) || subsize <= 0 ||
		( argc == 3 && !sgs_ParseBool( C, 2, &overlap ) ) )
		STDLIB_WARN( "unexpected arguments; function "
			"expects 2-3 arguments: string, string (length > 0), [bool]" );

	strend = str + size - subsize;
	while( str <= strend )
	{
		if( strncmp( str, sub, subsize ) == 0 )
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
	int argc;
	char* str, *sub, *strend, *ostr;
	sgs_SizeVal size, subsize; 
	sgs_Integer from = 0;
	
	SGSFN( "string_find" );

	argc = sgs_StackSize( C );
	if( argc < 2 || argc > 3 ||
		!sgs_ParseString( C, 0, &str, &size ) ||
		!sgs_ParseString( C, 1, &sub, &subsize ) || subsize <= 0 ||
		( argc == 3 && !sgs_ParseInt( C, 2, &from ) ) )
		STDLIB_WARN( "unexpected arguments; function "
			"expects 2-3 arguments: string, string (length > 0), [int]" );

	strend = str + size - subsize;
	ostr = str;
	str += from >= 0 ? from : MAX( 0, size + from );
	while( str <= strend )
	{
		if( strncmp( str, sub, subsize ) == 0 )
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
	int argc;
	char* str, *sub, *ostr;
	sgs_SizeVal size, subsize;
	sgs_Integer from = -1;
	
	SGSFN( "string_find_rev" );

	argc = sgs_StackSize( C );
	if( argc < 2 || argc > 3 ||
		!sgs_ParseString( C, 0, &str, &size ) ||
		!sgs_ParseString( C, 1, &sub, &subsize ) || subsize <= 0 ||
		( argc == 3 && !sgs_ParseInt( C, 2, &from ) ) )
		STDLIB_WARN( "unexpected arguments; function "
			"expects 2-3 arguments: string, string (length > 0), [int]" );

	ostr = str;
	str += from >= 0 ?
		MIN( from, size - subsize ) :
		MIN( size - subsize, size + from );
	while( str >= ostr )
	{
		if( strncmp( str, sub, subsize ) == 0 )
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
	int matchcount = 0, matchcap = NUMSM, curmatch;
#undef NUMSM

	char* strend = str + size - subsize;
	char* ptr = str, *i, *o;
	int32_t outlen;

	/* subsize = 0 handled by parent */

	while( ptr <= strend )
	{
		if( strncmp( ptr, sub, subsize ) == 0 )
		{
			if( matchcount == matchcap )
			{
				matchcap *= 4;
				int32_t* nm = sgs_Alloc_n( int32_t, matchcap );
				memcpy( nm, matches, sizeof( int32_t ) * matchcount );
				if( matches != sma )
					sgs_Dealloc( matches );
				matches = nm;
			}
			matches[ matchcount++ ] = ptr - str;

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
		int len = mp - i;
		if( len )
			memcpy( o, i, len );
		i += len;
		o += len;

		memcpy( o, rep, repsize );
		i += subsize;
		o += repsize;
	}
	if( i < strend )
	{
		memcpy( o, i, strend - i );
	}

	if( matches != sma )
		sgs_Dealloc( matches );

	return 1;
}
static int _stringrep_as
(
	SGS_CTX,
	char* str, int32_t size,
	sgs_Variable* subarr,
	char* rep, int32_t repsize
)
{
	char* substr;
	sgs_SizeVal subsize;
	sgs_Variable var;
	int32_t i, arrsize = sgs_ArraySize( C, subarr );
	if( arrsize < 0 )
		goto fail;

	for( i = 0; i < arrsize; ++i )
	{
		if( !sgs_ArrayGet( C, subarr, i, &var ) )   goto fail;
		sgs_PushVariable( C, &var );
		sgs_Release( C, &var );
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
static int _stringrep_aa
(
	SGS_CTX, char* str, int32_t size,
	sgs_Variable* subarr,
	sgs_Variable* reparr
)
{
	char* substr, *repstr;
	sgs_SizeVal subsize, repsize;
	sgs_Variable var;
	int32_t i, arrsize = sgs_ArraySize( C, subarr ),
		reparrsize = sgs_ArraySize( C, reparr );
	if( arrsize < 0 || reparrsize < 0 )
		goto fail;

	for( i = 0; i < arrsize; ++i )
	{
		if( !sgs_ArrayGet( C, subarr, i, &var ) )   goto fail;
		sgs_PushVariable( C, &var );
		sgs_Release( C, &var );
		if( !sgs_ParseString( C, -1, &substr, &subsize ) )
			goto fail;

		if( !sgs_ArrayGet( C, reparr, i % reparrsize, &var ) )   goto fail;
		sgs_PushVariable( C, &var );
		sgs_Release( C, &var );
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
	int argc, isarr1, isarr2, ret;
	char* str, *sub, *rep;
	sgs_Variable var1, var2;
	sgs_SizeVal size, subsize, repsize;
	
	SGSFN( "string_replace" );

	argc = sgs_StackSize( C );
	if( argc != 3 )
		goto invargs;

	sgs_GetStackItem( C, 1, &var1 );
	sgs_GetStackItem( C, 2, &var2 );
	isarr1 = sgs_IsArray( C, &var1 );
	isarr2 = sgs_IsArray( C, &var2 );

	if( !sgs_ParseString( C, 0, &str, &size ) )
		goto invargs;

	if( isarr1 && isarr2 )
	{
		return _stringrep_aa( C, str, size, &var1, &var2 );
	}

	if( isarr2 )
		goto invargs;

	ret = sgs_ParseString( C, 2, &rep, &repsize );
	if( isarr1 && ret )
	{
		return _stringrep_as( C, str, size, &var1, rep, repsize );
	}

	if( sgs_ParseString( C, 1, &sub, &subsize ) && ret )
	{
		if( subsize == 0 )
		{
			sgs_PushVariable( C, &var1 );
			return 1;
		}
		return _stringrep_ss( C, str, size, sub, subsize, rep, repsize );
	}

invargs:
	STDLIB_WARN( "unexpected arguments; function expects "
		"3 arguments: string, ((string|array), string) | (string, string)" )
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
	int argc;
	char* str, *strend, *list = " \t\r\n";
	sgs_SizeVal size, listsize = 4;
	sgs_Integer flags = sgsLEFT | sgsRIGHT;
	
	SGSFN( "string_trim" );

	argc = sgs_StackSize( C );
	if( argc < 1 || argc > 3 ||
		!sgs_ParseString( C, 0, &str, &size ) ||
		( argc >= 2 && !sgs_ParseString( C, 1, &list, &listsize ) ) ||
		( argc >= 3 && !sgs_ParseInt( C, 2, &flags ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1-3 arguments: string, [string], [int]" );

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

	sgs_PushStringBuf( C, str, strend - str );
	return 1;
}

static int sgsstd_string_implode( SGS_CTX )
{
	sgs_Variable arr;
	sgs_SizeVal i, asize;
	
	SGSFN( "string_implode" );

	if( sgs_StackSize( C ) != 2 ||
		!sgs_GetStackItem( C, 0, &arr ) ||
		!sgs_IsArray( C, &arr ) ||
		!sgs_ParseString( C, 1, NULL, NULL ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 2 arguments: array, string" )

	asize = sgs_ArraySize( C, &arr );
	if( !asize )
	{
		sgs_PushString( C, "" );
		return 1;
	}
	for( i = 0; i < asize; ++i )
	{
		sgs_Variable var;
		if( i )
			sgs_PushItem( C, 1 );
		if( !sgs_ArrayGet( C, &arr, i, &var ) )
			STDLIB_WARN( "failed to read from array" )
		sgs_PushVariable( C, &var );
		sgs_Release( C, &var );
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

	if( sgs_StackSize( C ) != 2 ||
		!sgs_ParseString( C, 0, &a, &asize ) ||
		!sgs_ParseString( C, 1, &b, &bsize ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 2 arguments: string, string" )
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
			sgs_PushStringBuf( C, pp, p - pp );
			pp = p + bsize;
			p = _findpos( pp, asize - ( pp - a ), b, bsize );
		}

		sgs_PushStringBuf( C, pp, a + asize - pp );
	}

	return sgs_PushArray( C, sgs_StackSize( C ) - ssz ) == SGS_SUCCESS;
}

static int sgsstd_string_charcode( SGS_CTX )
{
	char* a;
	sgs_SizeVal asize, argc = sgs_StackSize( C );
	sgs_Integer off = 0;
	
	SGSFN( "string_charcode" );

	if( argc < 1 || argc > 2 ||
		!sgs_ParseString( C, 0, &a, &asize ) ||
		( argc == 2 && !sgs_ParseInt( C, 1, &off ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1-2 arguments: string[, int]" )

	if( off < 0 )
		off += asize;

	if( off < 0 || off >= (sgs_Integer) asize )
		STDLIB_WARN( "index out of bounds" )

	sgs_PushInt( C, (unsigned char) a[ off ] );
	return 1;
}

static int sgsstd_string_frombytes( SGS_CTX )
{
	char* buf;
	int hasone = 0;
	sgs_SizeVal size, i = 0;
	sgs_Variable arr;
	sgs_Integer onecode;
	
	SGSFN( "string_frombytes" );

	if( sgs_StackSize( C ) != 1 ||
		!sgs_GetStackItem( C, 0, &arr ) ||
		( !sgs_IsArray( C, &arr ) &&
			!( hasone = sgs_ParseInt( C, 0, &onecode ) ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: array|int" )

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

	size = sgs_ArraySize( C, &arr );
	if( size < 0 )
		goto fail;

	sgs_PushStringBuf( C, NULL, size );
	buf = sgs_ToString( C, -1 );
	if( sgs_PushIterator( C, 0 ) < 0 )
		goto fail;

	while( sgs_IterAdvance( C, -1 ) > 0 )
	{
		sgs_Integer b;
		if( sgs_IterPushData( C, -1, FALSE, TRUE ) < 0 )
			goto fail;
		b = sgs_GetInt( C, -1 );
		if( b < 0 || b > 255 )
			STDLIB_WARN( "invalid byte value" )
		buf[ i++ ] = b;
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

	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: string" )

	while( size > 0 )
	{
		uint32_t outchar = SGS_UNICODE_INVCHAR;
		int ret = sgs_utf8_decode( str, size, &outchar );
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
	sgs_Variable arr;
	
	SGSFN( "string_utf8_encode" );

	if( sgs_StackSize( C ) != 1 ||
		!sgs_GetStackItem( C, 0, &arr ) ||
		!sgs_IsArray( C, &arr ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: array" )

	size = sgs_ArraySize( C, &arr );
	if( size < 0 )
		goto fail;

	if( sgs_PushIterator( C, 0 ) < 0 )
		goto fail;

	while( sgs_IterAdvance( C, -1 ) > 0 )
	{
		int cnt;
		char tmp[ 4 ];
		sgs_Integer cp;
		if( sgs_IterPushData( C, -1, FALSE, TRUE ) < 0 )
			goto fail;
		cp = sgs_GetInt( C, -1 );
		cnt = sgs_utf8_encode( cp, tmp );
		if( !cnt )
		{
			strcpy( tmp, SGS_UNICODE_INVCHAR_STR );
			cnt = SGS_UNICODE_INVCHAR_LEN;
		}
		membuf_appbuf( &buf, C, tmp, cnt );
		sgs_Pop( C, 1 );
	}
	sgs_PushStringBuf( C, buf.ptr, buf.size );
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
	
	SGSFN( "string_format" );
	
	int ssz = sgs_StackSize( C ), numitem = 0;
	if( ssz < 1 || !sgs_ParseString( C, 0, &fmt, &fmtsize ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1+ arguments: string, ..." )

	fmtend = fmt + fmtsize;
	while( fmt < fmtend )
	{
		struct fmtspec F;
		char c = *fmt++;
		if( c == '{' )
		{
			int stkid = 0, sio, ret;
			numitem++;
			while( fmt < fmtend && *fmt >= '0' && *fmt <= '9' )
			{
				stkid *= 10;
				stkid += *fmt++ - '0';
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
						"parsing error in item %d", numitem );
					return 0;
				}
			}
			else if( *fmt != '}' )
			{
				membuf_destroy( &B, C );
				sgs_Printf( C, SGS_WARNING, 
					"parsing error in item %d", numitem );
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

	sgs_PushStringBuf( C, B.ptr, B.size );
	membuf_destroy( &B, C );
	return 1;
}



#define FN( x ) { #x, sgsstd_##x }

static const sgs_RegIntConst s_iconsts[] =
{
	{ "NO_REV_INDEX", sgsNO_REV_INDEX },
	{ "STRICT_RANGES", sgsSTRICT_RANGES },
	{ "LEFT", sgsLEFT },
	{ "RIGHT", sgsRIGHT },
};

static const sgs_RegFuncConst s_fconsts[] =
{
	FN( string_cut ), FN( string_part ),
	FN( string_reverse ), FN( string_pad ),
	FN( string_repeat ), FN( string_count ),
	FN( string_find ), FN( string_find_rev ),
	FN( string_replace ), FN( string_trim ),
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



/*  - - - -
	
	T Y P E
	
*/

#define EXPECT_ONEARG() \
	if( sgs_StackSize( C ) != 1 ) STDLIB_WARN( "expected 1 argument" )

static int sgsstd_type_get( SGS_CTX )
{
	SGSFN( "type_get" );
	EXPECT_ONEARG()
	sgs_PushInt( C, sgs_ItemType( C, 0 ) );
	return 1;
}

static int sgsstd_typeof( SGS_CTX )
{
	SGSFN( "typeof" );
	EXPECT_ONEARG()
	sgs_TypeOf( C );
	return 1;
}

static int sgsstd_type_cast( SGS_CTX )
{
	int argc;
	sgs_Integer ty;
	
	SGSFN( "type_cast" );

	argc = sgs_StackSize( C );
	if( argc < 1 || argc > 2 ||
		!sgs_ParseInt( C, 1, &ty ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 2 arguments: any, int" )

	sgs_Convert( C, 0, ty );
	sgs_Pop( C, 1 );
	return 1;
}


static int sgsstd_type_is_numeric( SGS_CTX )
{
	int res, ty = sgs_ItemType( C, 0 );
	
	SGSFN( "type_is_numeric" );
	EXPECT_ONEARG()

	if( ty == SVT_NULL || ty == SVT_FUNC ||
		ty == SVT_CFUNC || ty == SVT_OBJECT )
		res = FALSE;

	else
		res = ty != SVT_STRING || sgs_IsNumericString(
			sgs_GetStringPtr( C, 0 ), sgs_GetStringSize( C, 0 ) );

	sgs_PushBool( C, res );
	return 1;
}

#define OBJECT_HAS_IFACE( outVar, O, iFace ) \
	{ void** ptr = O->iface; outVar = 0; \
	while( *ptr ){ if( *ptr == iFace ){ outVar = 1; \
		break; } ptr += 2; } }

static int sgsstd_type_is_callable( SGS_CTX )
{
	int res, ty = sgs_ItemType( C, 0 );
	
	SGSFN( "type_is_callable" );
	EXPECT_ONEARG()

	if( ty != SVT_FUNC && ty != SVT_CFUNC && ty != SVT_OBJECT )
		res = FALSE;

	else if( ty == SVT_OBJECT )
	{
		sgs_VarObj* O = sgs_GetObjectData( C, 0 );
		OBJECT_HAS_IFACE( res, O, SOP_CALL )
	}
	else
		res = TRUE;

	sgs_PushBool( C, res );
	return 1;
}


#define FN( x ) { #x, sgsstd_##x }

static const sgs_RegFuncConst t_fconsts[] =
{
	FN( type_get ), FN( typeof ), FN( type_cast ),
	FN( type_is_numeric ), FN( type_is_callable ),
};

static const sgs_RegIntConst t_iconsts[] =
{
	{ "TYPE_NULL", SVT_NULL },
	{ "TYPE_BOOL", SVT_BOOL },
	{ "TYPE_INT", SVT_INT },
	{ "TYPE_REAL", SVT_REAL },
	{ "TYPE_STRING", SVT_STRING },
	{ "TYPE_FUNC", SVT_FUNC },
	{ "TYPE_CFUNC", SVT_CFUNC },
	{ "TYPE_OBJECT", SVT_OBJECT },
	{ "TYPE_COUNT", SVT__COUNT },
};


SGSRESULT sgs_LoadLib_Type( SGS_CTX )
{
	int ret;
	ret = sgs_RegIntConsts( C, t_iconsts, ARRAY_SIZE( t_iconsts ) );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_RegFuncConsts( C, t_fconsts, ARRAY_SIZE( t_fconsts ) );
	return ret;
}

