

/* for the constants... */
#define _USE_MATH_DEFINES
#undef __STRICT_ANSI__
#include <math.h>

#include "sgs_std.h"



#define MATHFUNC( name ) \
static int sgsstd_##name( SGS_CTX ) { \
	CHKARGS( 1 ); \
	sgs_PushReal( C, name( sgs_ToReal( C, -1 ) ) ); \
	return 1; }

#define MATHFUNC2( name ) \
static int sgsstd_##name( SGS_CTX ) { \
	CHKARGS( 2 ); \
	sgs_PushReal( C, name( sgs_ToReal( C, -2 ), sgs_ToReal( C, -1 ) ) ); \
	return 1; }

MATHFUNC( abs )
MATHFUNC( sqrt )
MATHFUNC( log )
MATHFUNC( log10 )
MATHFUNC( exp )
MATHFUNC( floor )
MATHFUNC( ceil )
MATHFUNC( sin )
MATHFUNC( cos )
MATHFUNC( tan )
MATHFUNC( asin )
MATHFUNC( acos )
MATHFUNC( atan )

MATHFUNC2( pow )
MATHFUNC2( atan2 )
MATHFUNC2( fmod )


static const sgs_RegRealConst m_rconsts[] =
{
	{ "vPI", M_PI },
	{ "vE", M_E },
};

#define FN( x ) { #x, sgsstd_##x }

static const sgs_RegFuncConst m_fconsts[] =
{
	FN( abs ), FN( sqrt ), FN( log ), FN( log10 ), FN( exp ), FN( floor ), FN( ceil ),
	FN( sin ), FN( cos ), FN( tan ), FN( asin ), FN( acos ), FN( atan ),
	FN( pow ), FN( atan2 ), FN( fmod ),
};

void sgs_LoadLib_Math( SGS_CTX )
{
	sgs_RegRealConsts( C, m_rconsts, ARRAY_SIZE( m_rconsts ) );
	sgs_RegFuncConsts( C, m_fconsts, ARRAY_SIZE( m_fconsts ) );
}



#define sgsfNO_REV_INDEX 1
#define sgsfSTRICT_RANGES 2
#define sgsfLEFT 1
#define sgsfRIGHT 2

static SGS_INLINE int32_t idx2off( int32_t size, int32_t i )
{
	if( -i > size || i >= size ) return -1;
	return i < 0 ? size + i : i;
}

static int sgsstd_string_cut( SGS_CTX )
{
	int argc;
	char* str;
	sgs_Integer size, flags = 0, i1, i2;

	argc = sgs_StackSize( C );
	if( argc < 2 || argc > 4 ||
		!stdlib_tostring( C, 0, &str, &size ) ||
		( i2 = size - 1 ) < 0 || /* comparison should always fail */
		!stdlib_toint( C, 1, &i1 ) ||
		( argc >= 3 && !stdlib_toint( C, 2, &i2 ) ) ||
		( argc >= 4 && !stdlib_toint( C, 3, &flags ) ) )
		STDLIB_WARN( "string_cut() - unexpected arguments; function expects 2-4 arguments: string, int, [int], [int]" );

	if( FLAG( flags, sgsfNO_REV_INDEX ) && ( i1 < 0 || i2 < 0 ) )
		STDLIB_WARN( "string_cut() - detected negative indices" );

	i1 = i1 < 0 ? size + i1 : i1;
	i2 = i2 < 0 ? size + i2 : i2;
	if( FLAG( flags, sgsfSTRICT_RANGES ) && ( i1 > i2 || i1 < 0 || i2 < 0 || i1 >= size || i2 >= size ) )
		STDLIB_WARN( "string_cut() - invalid character range" );

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
	sgs_Integer size, flags = 0, i1, i2;

	argc = sgs_StackSize( C );
	if( argc < 2 || argc > 4 ||
		!stdlib_tostring( C, 0, &str, &size ) ||
		!stdlib_toint( C, 1, &i1 ) ||
		( i2 = size - i1 ) < 0 || /* comparison should always fail */
		( argc >= 3 && !stdlib_toint( C, 2, &i2 ) ) ||
		( argc >= 4 && !stdlib_toint( C, 3, &flags ) ) )
		STDLIB_WARN( "string_part() - unexpected arguments; function expects 2-4 arguments: string, int, [int], [int]" );

	if( FLAG( flags, sgsfNO_REV_INDEX ) && ( i1 < 0 || i2 < 0 ) )
		STDLIB_WARN( "string_part() - detected negative indices" );

	i1 = i1 < 0 ? size + i1 : i1;
	i2 = i2 < 0 ? size + i2 : i2;
	if( FLAG( flags, sgsfSTRICT_RANGES ) && ( i1 < 0 || i1 + i2 < 0 || i2 < 0 || i1 >= size || i1 + i2 > size ) )
		STDLIB_WARN( "string_part() - invalid character range" );

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
	sgs_Integer size, i;

	argc = sgs_StackSize( C );
	if( argc != 1 || !stdlib_tostring( C, 0, &str, &size ) )
		STDLIB_WARN( "string_reverse() - unexpected arguments; function expects 1 argument: string" );

	sgs_PushStringBuf( C, NULL, size );
	sout = var_cstr( sgs_StackItem( C, -1 ) );

	for( i = 0; i < size; ++i )
		sout[ size - i - 1 ] = str[ i ];

	return 1;
}

static int sgsstd_string_pad( SGS_CTX )
{
	int argc;
	char* str, *pad = " ", *sout;
	sgs_Integer size, padsize = 1, tgtsize, flags = sgsfLEFT | sgsfRIGHT, lpad = 0, i;

	argc = sgs_StackSize( C );
	if( ( argc < 2 || argc > 4 ) ||
		!stdlib_tostring( C, 0, &str, &size ) ||
		!stdlib_toint( C, 1, &tgtsize ) ||
		( argc >= 3 && !stdlib_tostring( C, 2, &pad, &padsize ) ) ||
		( argc >= 4 && !stdlib_toint( C, 3, &flags ) ) )
		STDLIB_WARN( "string_pad() - unexpected arguments; function expects 2-4 arguments: string, int, [string], [int]" );

	if( tgtsize <= size || !FLAG( flags, sgsfLEFT | sgsfRIGHT ) )
	{
		sgs_PushVariable( C, sgs_StackItem( C, 0 ) );
		return 1;
	}

	sgs_PushStringBuf( C, NULL, tgtsize );
	sout = var_cstr( sgs_StackItem( C, -1 ) );
	if( FLAG( flags, sgsfLEFT ) )
	{
		if( FLAG( flags, sgsfRIGHT ) )
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
	sgs_Integer size, count;

	argc = sgs_StackSize( C );
	if( argc != 2 ||
		!stdlib_tostring( C, 0, &str, &size ) ||
		!stdlib_toint( C, 1, &count ) || count < 0 )
		STDLIB_WARN( "string_repeat() - unexpected arguments; function expects 2 arguments: string, int (>= 0)" );

	sgs_PushStringBuf( C, NULL, count * size );
	sout = var_cstr( sgs_StackItem( C, -1 ) );
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
	sgs_Integer size, subsize, ret = 0;

	argc = sgs_StackSize( C );
	if( argc < 2 || argc > 3 ||
		!stdlib_tostring( C, 0, &str, &size ) ||
		!stdlib_tostring( C, 1, &sub, &subsize ) || subsize <= 0 ||
		( argc == 3 && !stdlib_tobool( C, 2, &overlap ) ) )
		STDLIB_WARN( "string_count() - unexpected arguments; function expects 2-3 arguments: string, string (length > 0), [bool]" );

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
	sgs_Integer size, subsize, from = 0;

	argc = sgs_StackSize( C );
	if( argc < 2 || argc > 3 ||
		!stdlib_tostring( C, 0, &str, &size ) ||
		!stdlib_tostring( C, 1, &sub, &subsize ) || subsize <= 0 ||
		( argc == 3 && !stdlib_toint( C, 2, &from ) ) )
		STDLIB_WARN( "string_find() - unexpected arguments; function expects 2-3 arguments: string, string (length > 0), [int]" );

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
	sgs_Integer size, subsize, from = -1;

	argc = sgs_StackSize( C );
	if( argc < 2 || argc > 3 ||
		!stdlib_tostring( C, 0, &str, &size ) ||
		!stdlib_tostring( C, 1, &sub, &subsize ) || subsize <= 0 ||
		( argc == 3 && !stdlib_toint( C, 2, &from ) ) )
		STDLIB_WARN( "string_find_rev() - unexpected arguments; function expects 2-3 arguments: string, string (length > 0), [int]" );

	ostr = str;
	str += from >= 0 ? MIN( from, size - subsize ) : MIN( size - subsize, size + from );
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

static int sgsstd_string_trim( SGS_CTX )
{
	int argc;
	char* str, *strend, *list = " \t\r\n";
	sgs_Integer size, listsize = 4, flags = sgsfLEFT | sgsfRIGHT;

	argc = sgs_StackSize( C );
	if( argc < 1 || argc > 3 ||
		!stdlib_tostring( C, 0, &str, &size ) ||
		( argc >= 2 && !stdlib_tostring( C, 1, &list, &listsize ) ) ||
		( argc >= 3 && !stdlib_toint( C, 2, &flags ) ) )
		STDLIB_WARN( "string_trim() - unexpected arguments; function expects 1-3 arguments: string, [string], [int]" );

	if( !FLAG( flags, sgsfLEFT | sgsfRIGHT ) )
	{
		sgs_PushVariable( C, sgs_StackItem( C, 0 ) );
		return 1;
	}

	strend = str + size;
	if( flags & sgsfLEFT )
	{
		while( str < strend && stdlib_isoneof( *str, list, listsize ) )
			str++;
	}
	if( flags & sgsfRIGHT )
	{
		while( str < strend && stdlib_isoneof( *(strend-1), list, listsize ) )
			strend--;
	}

	sgs_PushStringBuf( C, str, strend - str );
	return 1;
}



#define FN( x ) { #x, sgsstd_##x }

static const sgs_RegIntConst s_iconsts[] =
{
	{ "fNO_REV_INDEX", sgsfNO_REV_INDEX },
	{ "fSTRICT_RANGES", sgsfSTRICT_RANGES },
	{ "fLEFT", sgsfLEFT },
	{ "fRIGHT", sgsfRIGHT },
};

static const sgs_RegFuncConst s_fconsts[] =
{
	FN( string_cut ), FN( string_part ), FN( string_reverse ), FN( string_pad ),
	FN( string_repeat ), FN( string_count ), FN( string_find ), FN( string_find_rev ),
	FN( string_trim ),
};

void sgs_LoadLib_String( SGS_CTX )
{
	sgs_RegIntConsts( C, s_iconsts, ARRAY_SIZE( s_iconsts ) );
	sgs_RegFuncConsts( C, s_fconsts, ARRAY_SIZE( s_fconsts ) );
}


#define EXPECT_ONEARG( N ) \
	if( sgs_StackSize( C ) != 1 ){ \
		sgs_Printf( C, SGS_WARNING, -1, #N ": 1 argument expected" ); \
		return 0;}

#define typechk_func( N, T ) \
static int sgsstd_##N( SGS_CTX ){ \
	EXPECT_ONEARG( N ) \
	sgs_PushBool( C, sgs_ItemType( C, 0 ) == T ); \
	return 1;}

typechk_func( is_null, SVT_NULL )
typechk_func( is_bool, SVT_BOOL )
typechk_func( is_int, SVT_INT )
typechk_func( is_real, SVT_REAL )
typechk_func( is_string, SVT_STRING )
typechk_func( is_func, SVT_FUNC )
typechk_func( is_cfunc, SVT_CFUNC )
typechk_func( is_object, SVT_OBJECT )

#undef typechk_func

static int sgsstd_is_numeric( SGS_CTX )
{
	int res;
	sgs_Variable* var;
	EXPECT_ONEARG( is_numeric )

	var = sgs_StackItem( C, 0 );

	if( var->type == SVT_NULL || var->type == SVT_FUNC || var->type == SVT_CFUNC || var->type == SVT_OBJECT )
		res = FALSE;

	else
		res = var->type != SVT_STRING || stdlib_is_numericstring( var_cstr( var ), var->data.S->size );

	sgs_PushBool( C, res );
	return 1;
}

#define OBJECT_HAS_IFACE( outVar, objVar, iFace ) \
	{ void** ptr = objVar->data.O->iface; outVar = 0; \
	while( *ptr ){ if( *ptr == iFace ){ outVar = 1; \
		break; } ptr += 2; } }

static int sgsstd_is_callable( SGS_CTX )
{
	int res;
	sgs_Variable* var;
	EXPECT_ONEARG( is_callable )

	var = sgs_StackItem( C, 0 );

	if( var->type != SVT_FUNC && var->type != SVT_CFUNC && var->type != SVT_OBJECT )
		res = FALSE;

	else if( var->type == SVT_OBJECT )
		OBJECT_HAS_IFACE( res, var, SOP_CALL )

	else
		res = TRUE;

	sgs_PushBool( C, res );
	return 1;
}

static int sgsstd_is_switch( SGS_CTX )
{
	int res;
	sgs_Variable* var;
	EXPECT_ONEARG( is_switch )

	var = sgs_StackItem( C, 0 );

	if( var->type == SVT_FUNC || var->type == SVT_CFUNC )
		res = FALSE;

	else if( var->type == SVT_STRING )
		res = stdlib_is_numericstring( var_cstr( var ), var->data.S->size );

	else if( var->type == SVT_OBJECT )
		OBJECT_HAS_IFACE( res, var, SOP_TOBOOL )

	else
		res = TRUE;

	sgs_PushBool( C, res );
	return 1;
}

static int sgsstd_is_printable( SGS_CTX )
{
	int res;
	sgs_Variable* var;
	EXPECT_ONEARG( is_printable )

	var = sgs_StackItem( C, 0 );

	if( var->type == SVT_NULL || var->type == SVT_FUNC || var->type == SVT_CFUNC )
		res = FALSE;

	else if( var->type == SVT_OBJECT )
		OBJECT_HAS_IFACE( res, var, SOP_TOSTRING )

	else
		res = TRUE;

	sgs_PushBool( C, res );
	return 1;
}


static int sgsstd_type_get( SGS_CTX )
{
	EXPECT_ONEARG( type_get )

	sgs_PushInt( C, sgs_StackItem( C, 0 )->type );
	return 1;
}

static int sgsstd_type_cast( SGS_CTX )
{
	int argc;
	sgs_Integer ty;

	argc = sgs_StackSize( C );
	if( argc < 1 || argc > 2 ||
		!stdlib_toint( C, 1, &ty ) )
		STDLIB_WARN( "type_cast() - unexpected arguments; function expects 2 arguments: any, int" );

	vm_convert_stack( C, 0, ty );
	sgs_Pop( C, 1 );
	return 1;
}

static int sgsstd_typeof( SGS_CTX )
{
	CHKARGS( 1 );
	sgs_TypeOf( C );
	return 1;
}


#define FN( x ) { #x, sgsstd_##x }

static const sgs_RegFuncConst t_fconsts[] =
{
	FN( is_null ), FN( is_bool ), FN( is_int ), FN( is_real ),
	FN( is_string ), FN( is_func ), FN( is_cfunc ), FN( is_object ),
	FN( is_numeric ), FN( is_callable ), FN( is_switch ), FN( is_printable ),
	FN( type_get ), FN( type_cast ), FN( typeof ),
};

static const sgs_RegIntConst t_iconsts[] =
{
	{ "tNULL", SVT_NULL },
	{ "tBOOL", SVT_BOOL },
	{ "tINT", SVT_INT },
	{ "tREAL", SVT_REAL },
	{ "tSTRING", SVT_STRING },
	{ "tFUNC", SVT_FUNC },
	{ "tCFUNC", SVT_CFUNC },
	{ "tOBJECT", SVT_OBJECT },
	{ "t_COUNT", SVT__COUNT },
};


void sgs_LoadLib_Type( SGS_CTX )
{
	sgs_RegIntConsts( C, t_iconsts, ARRAY_SIZE( t_iconsts ) );
	sgs_RegFuncConsts( C, t_fconsts, ARRAY_SIZE( t_fconsts ) );
}

