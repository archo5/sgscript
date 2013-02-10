

#include "sgs_std.h"


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

static const sgs_RegFuncConst fconsts[] =
{
	FN( is_null ), FN( is_bool ), FN( is_int ), FN( is_real ),
	FN( is_string ), FN( is_func ), FN( is_cfunc ), FN( is_object ),
	FN( is_numeric ), FN( is_callable ), FN( is_switch ), FN( is_printable ),
	FN( type_get ), FN( type_cast ), FN( typeof ),
};

static const sgs_RegIntConst iconsts[] =
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
	sgs_RegIntConsts( C, iconsts, ARRAY_SIZE( iconsts ) );
	sgs_RegFuncConsts( C, fconsts, ARRAY_SIZE( fconsts ) );
}

