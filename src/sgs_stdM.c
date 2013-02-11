

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


#define FN( x ) { #x, sgsstd_##x }

static const sgs_RegFuncConst fconsts[] =
{
	FN( abs ), FN( sqrt ), FN( log ), FN( log10 ), FN( exp ), FN( floor ), FN( ceil ),
	FN( sin ), FN( cos ), FN( tan ), FN( asin ), FN( acos ), FN( atan ),
	FN( pow ), FN( atan2 ), FN( fmod ),
};

void sgs_LoadLib_Math( SGS_CTX )
{
	sgs_RegFuncConsts( C, fconsts, ARRAY_SIZE( fconsts ) );
}
