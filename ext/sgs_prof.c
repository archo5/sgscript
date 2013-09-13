

#define SGS_INTERNAL
#include <sgs_int.h>

#include "sgs_prof.h"



static void mode1hook( void* userdata, SGS_CTX, int evid )
{
	SGS_PROF = (sgs_Prof*) userdata;
	double TM = sgs_GetTime();
	if( P->hfn )
		P->hfn( P->hctx, C, evid );

	if( evid == SGS_HOOK_ENTER )
	{
		const char* fname = "<error>";
		sgs_StackFrame* sf = sgs_GetFramePtr( C, 1 );
		sgs_StackFrameInfo( C, sf, &fname, NULL, NULL );
		membuf_appbuf( &P->keytmp, C, fname, strlen( fname ) );
		membuf_appchr( &P->keytmp, C, 0 );
		membuf_appbuf( &P->timetmp, C, &TM, sizeof(TM) );
	}
	else if( evid == SGS_HOOK_EXIT && P->keytmp.size )
	{
		double prevTM;
		HTPair* pair;
		int prevzeroat = P->keytmp.size - 1;
		while( prevzeroat --> 0 )
			if( P->keytmp.ptr[ prevzeroat ] == 0 )
				break;

		prevTM = AS_DOUBLE( P->timetmp.ptr + P->timetmp.size - sizeof(double) );
		pair = ht_find( &P->timings, P->keytmp.ptr, P->keytmp.size,
			sgs_HashFunc( P->keytmp.ptr, P->keytmp.size ) );
		if( pair )
		{
			AS_DOUBLE( pair->ptr ) += TM - prevTM;
		}
		else
		{
			double* val = sgs_Alloc( double );
			*val = TM - prevTM;
			ht_set( &P->timings, C, P->keytmp.ptr, P->keytmp.size, val );
		}


		membuf_resize( &P->keytmp, C, prevzeroat ? prevzeroat + 1 : 0 );
		membuf_resize( &P->timetmp, C, P->timetmp.size - sizeof(TM) );
	}
}

static int initProfMode1( SGS_PROF )
{
	P->keytmp = membuf_create();
	P->timetmp = membuf_create();
	ht_init( &P->timings, P->C, 4 );
	sgs_SetHookFunc( P->C, mode1hook, P );
	return 1;
}

static void freetimingsfunc( HTPair* pair, void* userdata )
{
	SGS_CTX = (sgs_Context*) userdata;
	sgs_Dealloc( pair->ptr );
}

static void freeProfMode1( SGS_PROF )
{
	membuf_destroy( &P->keytmp, P->C );
	membuf_destroy( &P->timetmp, P->C );
	ht_iterate( &P->timings, freetimingsfunc, P->C );
	ht_free( &P->timings, P->C );
}

static int dpm1sf( const void* p1, const void* p2 )
{
	const HTPair* v1 = *(const HTPair**) p1;
	const HTPair* v2 = *(const HTPair**) p2;
	int cmpsz = v1->str->size < v2->str->size ? v1->str->size : v2->str->size;
	int ret = memcmp( str_cstr( v1->str ), str_cstr( v2->str ), cmpsz );
	if( !ret )
		ret = v1->str->size - v2->str->size;
	return ret;
}


struct dtf_clos
{
	HTPair** arr;
	int* off;
};
static void dumptimingsfunc( HTPair* p, void* userdata )
{
	struct dtf_clos* D = (struct dtf_clos*) userdata;
	D->arr[ (*D->off)++ ] = p;
}

static int dumpProfMode1( SGS_PROF )
{
	int i, off = 0;
	HTPair **pbuf = (HTPair**)
		sgs_Malloc( P->C, sizeof(HTPair*) * P->timings.load );
	
	struct dtf_clos D;
	{
		D.arr = pbuf;
		D.off = &off;
	}
	ht_iterate( &P->timings, dumptimingsfunc, &D );
	
	qsort( pbuf, off, sizeof( HTPair* ), dpm1sf );
	
	sgs_Writef( P->C, "--- Time by call stack frame ---\n" );
	for( i = 0; i < off; ++i )
	{
		const char *s, *send;
		HTPair* p = pbuf[ i ];
		s = str_cstr( p->str );
		send = s + p->str->size;
		while( s < send )
		{
			if( s != str_cstr( p->str ) )
				sgs_Writef( P->C, "::" );
			sgs_Writef( P->C, "%s", s );
			s += strlen( s ) + 1;
		}
		sgs_Writef( P->C, " - %f\n", AS_DOUBLE( p->ptr ) );
	}
	sgs_Writef( P->C, "---\n" );
	sgs_Free( P->C, pbuf );
	
	return 1;
}



#define TOPCNT (SGS_SI_COUNT+1)

static void mode2hook( void* userdata, SGS_CTX, int evid )
{
	SGS_PROF = (sgs_Prof*) userdata;
	double TM = sgs_GetTime();
	if( P->hfn )
		P->hfn( P->hctx, C, evid );

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
	P->ictrs = (double*) sgs_Malloc( P->C, sizeof( double ) * TOPCNT );
	P->iexcs = (int32_t*) sgs_Malloc( P->C, sizeof( int32_t ) * TOPCNT );
	for( i = 0; i < TOPCNT; ++i )
	{
		P->ictrs[ i ] = 0;
		P->iexcs[ i ] = 0;
	}
	sgs_SetHookFunc( P->C, mode2hook, P );
	return 1;
}

static void freeProfMode2( SGS_PROF )
{
	sgs_Free( P->C, P->ictrs );
	sgs_Free( P->C, P->iexcs );
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
	P->hfn = NULL;
	P->hctx = NULL;
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
	if( P->mode == SGS_PROF_FUNCTIME )
		freeProfMode1( P );
	else if( P->mode == SGS_PROF_OPTIME )
		freeProfMode2( P );
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
