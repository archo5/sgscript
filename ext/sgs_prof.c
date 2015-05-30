

#include <math.h>

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
		sgs_membuf_appbuf( &P->timetmp, C, &TM, sizeof(TM) );
	}
	else if( evid == SGS_HOOK_EXIT )
	{
		double prevTM;
		sgs_VHTVar* pair;
		
		sgs_StackFrame* sf = sgs_GetFramePtr( C, 0 );
		sgs_membuf_resize( &P->keytmp, C, 0 );
		while( sf )
		{
			const char* fname = "<error>";
			sgs_StackFrameInfo( C, sf, &fname, NULL, NULL );
			sgs_membuf_appbuf( &P->keytmp, C, fname, strlen( fname ) );
			sgs_membuf_appchr( &P->keytmp, C, 0 );
			sf = sf->next;
		}
		
		SGS_AS_DOUBLE( prevTM, P->timetmp.ptr + P->timetmp.size - sizeof(double) );
		pair = sgs_vht_get_str( &P->timings, P->keytmp.ptr, (uint32_t) P->keytmp.size,
			sgs_HashFunc( P->keytmp.ptr, P->keytmp.size ) );
		if( pair )
		{
			pair->val.data.R += TM - prevTM;
		}
		else
		{
			sgs_Variable val;
			sgs_InitReal( &val, TM - prevTM );
			sgs_PushStringBuf( C, P->keytmp.ptr, (sgs_SizeVal) P->keytmp.size );
			sgs_vht_set( &P->timings, C, C->stack_top-1, &val );
			sgs_Pop( C, 1 );
		}
		
		sgs_membuf_resize( &P->timetmp, C, P->timetmp.size - sizeof(TM) );
	}
}

static int initProfMode1( SGS_PROF )
{
	P->keytmp = sgs_membuf_create();
	P->timetmp = sgs_membuf_create();
	sgs_vht_init( &P->timings, P->C, 128, 128 );
	sgs_SetHookFunc( P->C, mode1hook, P );
	return 1;
}

static void freeProfMode1( SGS_PROF )
{
	sgs_membuf_destroy( &P->keytmp, P->C );
	sgs_membuf_destroy( &P->timetmp, P->C );
	sgs_vht_free( &P->timings, P->C );
}

static int dpm1sf( const void* p1, const void* p2 )
{
	const sgs_VHTVar* v1 = (const sgs_VHTVar*) p1;
	const sgs_VHTVar* v2 = (const sgs_VHTVar*) p2;
	const sgs_iStr* str1 = v1->key.data.S;
	const sgs_iStr* str2 = v2->key.data.S;
	uint32_t cmpsz = str1->size < str2->size ? str1->size : str2->size;
	int ret = memcmp( sgs_str_c_cstr( str1 ), sgs_str_c_cstr( str2 ), cmpsz );
	if( !ret )
		ret = (int)( str1->size - str2->size );
	return ret;
}


static int dumpProfMode1( SGS_PROF )
{
	int i;
	sgs_VHTVar* pbuf = (sgs_VHTVar*)
		sgs_Malloc( P->C, sizeof(sgs_VHTVar) * (size_t) P->timings.size );
	
	memcpy( pbuf, P->timings.vars, sizeof(sgs_VHTVar) * (size_t) P->timings.size );
	
	qsort( pbuf, (size_t) P->timings.size, sizeof(sgs_VHTVar), dpm1sf );
	
	sgs_Writef( P->C, "--- Time by call stack frame ---\n" );
	for( i = 0; i < P->timings.size; ++i )
	{
		const char *s, *send;
		sgs_VHTVar* p = pbuf + i;
		s = sgs_var_cstr( &p->key );
		send = s + p->key.data.S->size;
		while( s < send )
		{
			if( s != sgs_var_cstr( &p->key ) )
				sgs_Writef( P->C, "::" );
			sgs_Writef( P->C, "%s", s );
			s += strlen( s ) + 1;
		}
		sgs_Writef( P->C, " - %f\n", p->val.data.R );
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
	P->iexcs = (uint32_t*) sgs_Malloc( P->C, sizeof( uint32_t ) * TOPCNT );
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
	const icts* t1 = (const icts*) p1;
	const icts* t2 = (const icts*) p2;
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



typedef struct _mode3data
{
	size_t numallocs;
	size_t numfrees;
	size_t numblocks;
	double szdelta;
}
mode3data;

static void mode3hook( void* userdata, SGS_CTX, int evid )
{
	SGS_SHCTX_USE;
	SGS_PROF = (sgs_Prof*) userdata;
	mode3data CD = { S->numallocs, S->numfrees, S->numblocks, (double) S->memsize };
	if( P->hfn )
		P->hfn( P->hctx, C, evid );

	if( evid == SGS_HOOK_ENTER )
	{
		sgs_membuf_appbuf( &P->timetmp, C, &CD, sizeof(CD) );
	}
	else if( evid == SGS_HOOK_EXIT )
	{
		mode3data prevCD, *PD;
		sgs_VHTVar* pair;
		
		sgs_StackFrame* sf = sgs_GetFramePtr( C, 0 );
		sgs_membuf_resize( &P->keytmp, C, 0 );
		while( sf )
		{
			const char* fname = "<error>";
			sgs_StackFrameInfo( C, sf, &fname, NULL, NULL );
			sgs_membuf_appbuf( &P->keytmp, C, fname, strlen( fname ) );
			sgs_membuf_appchr( &P->keytmp, C, 0 );
			sf = sf->next;
		}
		
		memcpy( &prevCD, P->timetmp.ptr + P->timetmp.size - sizeof(CD), sizeof(CD) );
		pair = sgs_vht_get_str( &P->timings, P->keytmp.ptr, (uint32_t) P->keytmp.size,
			sgs_HashFunc( P->keytmp.ptr, P->keytmp.size ) );
		if( pair )
		{
			PD = (mode3data*) pair->val.data.P;
		}
		else
		{
			sgs_Variable val;
			val.type = SGS_VT_PTR;
			val.data.P = PD = (mode3data*) sgs_Malloc( C, sizeof(CD) );
			memset( PD, 0, sizeof(*PD) );
			sgs_PushStringBuf( C, P->keytmp.ptr, (sgs_SizeVal) P->keytmp.size );
			sgs_vht_set( &P->timings, C, C->stack_top-1, &val );
			sgs_Pop( C, 1 );
		}
		
		PD->numallocs += CD.numallocs - prevCD.numallocs;
		PD->numfrees += CD.numfrees - prevCD.numfrees;
		PD->numblocks += CD.numblocks - prevCD.numblocks;
		PD->szdelta += CD.szdelta - prevCD.szdelta;
		
		sgs_membuf_resize( &P->timetmp, C, P->timetmp.size - sizeof(CD) );
	}
}

static int initProfMode3( SGS_PROF )
{
	initProfMode1( P );
	sgs_SetHookFunc( P->C, mode3hook, P );
	return 1;
}

static int freeProfMode3( SGS_PROF )
{
	sgs_SizeVal i;
	for( i = 0; i < P->timings.size; ++i )
	{
		sgs_Free( P->C, P->timings.vars[ i ].val.data.P );
	}
	freeProfMode1( P );
	return 1;
}

static int dpm3sf( const void* p1, const void* p2 )
{
	const sgs_VHTVar* v1 = (const sgs_VHTVar*) p1;
	const sgs_VHTVar* v2 = (const sgs_VHTVar*) p2;
	const sgs_iStr* str1 = v1->key.data.S;
	const sgs_iStr* str2 = v2->key.data.S;
	const mode3data* D1 = (const mode3data*) v1->val.data.P;
	const mode3data* D2 = (const mode3data*) v2->val.data.P;
	size_t cmpsz = str1->size < str2->size ? str1->size : str2->size;
	int ret = (int)( ( D2->numallocs + D2->numfrees ) - ( D1->numallocs + D1->numfrees ) );
	if( !ret )
	{
		double abs1 = fabs( D2->szdelta );
		double abs2 = fabs( D1->szdelta );
		ret = abs1 == abs2 ? 0 : ( abs1 < abs2 ? -1 : 1 );
	}
	if( !ret ) ret = memcmp( sgs_str_c_cstr( str1 ), sgs_str_c_cstr( str2 ), cmpsz );
	if( !ret ) ret = (int)( str1->size - str2->size );
	return ret;
}

static int dumpProfMode3( SGS_PROF )
{
	int i;
	sgs_VHTVar* pbuf = (sgs_VHTVar*) sgs_Malloc( P->C, sizeof(sgs_VHTVar) * (size_t) P->timings.size );
	
	memcpy( pbuf, P->timings.vars, sizeof(sgs_VHTVar) * (size_t) P->timings.size );
	
	qsort( pbuf, (size_t) P->timings.size, sizeof(sgs_VHTVar), dpm3sf );
	
	sgs_Writef( P->C, "--- Memory usage by call stack frame ---\n" );
	for( i = 0; i < P->timings.size; ++i )
	{
		const char *s, *send;
		sgs_VHTVar* p = pbuf + i;
		mode3data* PD = (mode3data*) p->val.data.P;
		s = sgs_var_cstr( &p->key );
		send = s + p->key.data.S->size;
		while( s < send )
		{
			if( s != sgs_var_cstr( &p->key ) )
				sgs_Writef( P->C, "::" );
			sgs_Writef( P->C, "%s", s );
			s += strlen( s ) + 1;
		}
		sgs_Writef( P->C, " - %d allocs, %d frees, %d delta blocks, %.3f delta memory (kB)\n",
			PD->numallocs, PD->numfrees, PD->numblocks, PD->szdelta / 1024.0 );
	}
	sgs_Writef( P->C, "---\n" );
	sgs_Free( P->C, pbuf );
	
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
	else if( mode == SGS_PROF_MEMUSAGE )
		return initProfMode3( P );
	return 0;
}

SGSBOOL sgs_ProfClose( SGS_PROF )
{
	if( P->mode == SGS_PROF_FUNCTIME )
		freeProfMode1( P );
	else if( P->mode == SGS_PROF_OPTIME )
		freeProfMode2( P );
	else if( P->mode == SGS_PROF_MEMUSAGE )
		freeProfMode3( P );
	sgs_SetHookFunc( P->C, P->hfn, P->hctx );
	return 1;
}

SGSBOOL sgs_ProfDump( SGS_PROF )
{
	if( P->mode == SGS_PROF_FUNCTIME )
		return dumpProfMode1( P );
	else if( P->mode == SGS_PROF_OPTIME )
		return dumpProfMode2( P );
	else if( P->mode == SGS_PROF_MEMUSAGE )
		return dumpProfMode3( P );
	return 0;
}
