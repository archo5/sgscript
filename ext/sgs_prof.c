

#define SGS_INTERNAL
#include <sgs_int.h>

#include "sgs_prof.h"


static int initProfMode1( SGS_PROF )
{
	return 0;
}

static int dumpProfMode1( SGS_PROF )
{
	return 0;
}



#define TOPCNT (SGS_SI_COUNT+1)

static void mode2hook( void* userdata, SGS_CTX, int evid )
{
	SGS_PROF = (sgs_Prof*) userdata;
	double TM = sgs_GetTime();
	if( P->instr >= 0 )
	{
		double dif = TM - P->starttime;
		P->ictrs[ P->instr ] += dif >= 0 ? dif : 0;
		P->instr = -1;
	}
	if( evid == SGS_HOOK_STEP )
	{
		sgs_StackFrame* sf = sgs_GetFramePtr( C, 1 );
		P->instr = SGS_INSTR_GET_OP( *sf->iptr );
		P->iexcs[ P->instr ]++;
		P->starttime = TM;
	}
	else
	{
		if( P->prev != SGS_HOOK_STEP )
		{
			double dif = TM - P->starttime;
			P->ictrs[ SGS_SI_COUNT ] += dif >= 0 ? dif : 0;
		}
		P->starttime = TM;
	}
	P->prev = evid;
}

static int initProfMode2( SGS_PROF )
{
	int i;
	P->instr = -1;
	P->ictrs = sgs_Malloc( P->C, sizeof( double ) * TOPCNT );
	P->iexcs = sgs_Malloc( P->C, sizeof( uint32_t ) * TOPCNT );
	for( i = 0; i < TOPCNT; ++i )
	{
		P->ictrs[ i ] = 0;
		P->iexcs[ i ] = 0;
	}
	sgs_SetHookFunc( P->C, mode2hook, P );
	return 1;
}

typedef struct { int i; uint32_t c; double t; } icts;
static int its_sort( const void* p1, const void* p2 )
{
	icts* t1 = (icts*) p1;
	icts* t2 = (icts*) p2;
	if( t1->t != t2->t )
		return t1->t > t2->t ? -1 : 1;
	else if( t1->c != t2->c )
		return t1->c > t2->c ? -1 : 1;
	return 0;
}

static int dumpProfMode2( SGS_PROF )
{
	int i;
	icts* temp;
	uint32_t totalcnt = 0, c;
	double total = 0, t;
	sgs_Writef( P->C, "--- Time by VM instruction ---\n" );
	sgs_Writef( P->C, "|      NAME      |     TIME     |    COUNT    |\n" );
	temp = (icts*) sgs_Malloc( P->C, sizeof( icts ) * TOPCNT );
	for( i = 0; i < TOPCNT; ++i )
	{
		t = P->ictrs[ i ];
		c = P->iexcs[ i ];
		total += t;
		totalcnt += c;
		temp[ i ].t = t;
		temp[ i ].i = i;
		temp[ i ].c = c;
	}
	qsort( temp, TOPCNT, sizeof( icts ), its_sort );
	for( i = 0; i < TOPCNT; ++i )
	{
		const char* str = temp[ i ].i == SGS_SI_COUNT ?
			"! native code" :
			sgs_CodeString( SGS_CODE_OP, temp[ i ].i );
		sgs_Writef( P->C, "| %14s - %12f - %11d |\n", str, temp[ i ].t, temp[ i ].c );
	}
	sgs_Free( P->C, temp );
	sgs_Writef( P->C, "///\n| %14s - %12f - %11d |\n---\n", "Total", total, totalcnt );
	return 1;
}


SGSBOOL sgs_ProfInit( SGS_CTX, SGS_PROF, int mode )
{
	sgs_GetHookFunc( C, &P->hfn, &P->hctx );
	P->C = C;
	P->mode = mode;
	P->prev = 0;
	P->ictrs = NULL;
	P->iexcs = NULL;

	if( mode == SGS_PROF_FUNCTIME )
		return initProfMode1( P );
	else if( mode == SGS_PROF_OPTIME )
		return initProfMode2( P );
	return 0;
}

SGSBOOL sgs_ProfClose( SGS_PROF )
{
	if( P->ictrs )
		sgs_Free( P->C, P->ictrs );
	if( P->iexcs )
		sgs_Free( P->C, P->iexcs );
	sgs_SetHookFunc( P->C, P->hfn, P->hctx );
	return 1;
}

SGSBOOL sgs_ProfDump( SGS_PROF )
{
	if( P->mode == SGS_PROF_FUNCTIME )
		return dumpProfMode1( P );
	else if( P->mode == SGS_PROF_OPTIME )
		return dumpProfMode2( P );
	return 0;
}
