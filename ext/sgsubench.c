


#define SGS_PERFEVENT( x ) x
#define BUILDING_SGS 1
#define SGS_INSTRUMENTED_BUILD
#define SGS_INTERNAL_STRINGTABLES
#define SGS_USE_FILESYSTEM
#include "../src/sgs_util.h"


#define MAX_PRECALL_ENTRIES 1000000


/* --- PROFILING HELPERS --- */
static LARGE_INTEGER freq;
double sgs_hqtime()
{
#ifdef _WIN32
	LARGE_INTEGER c;
	QueryPerformanceCounter( &c );
	return (double) c.QuadPart / (double) freq.QuadPart;
#else
	timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	static const double S2NS = 1.0 / 1000000000.0;
	return ts.tv_sec + S2NS * ts.tv_nsec;
#endif
}

#define CALLSRC_NATIVE 0
#define CALLSRC_SCRIPT 1
#define FUNCTYPE_SGS 0
#define FUNCTYPE_CFN 1
#define FUNCTYPE_OBJ 2
typedef struct ps_precall
{
	double time;
	unsigned sendargs;
	unsigned recvargs;
	unsigned callsrc : 1;
	unsigned functype : 2;
	unsigned sendthis : 1;
	unsigned recvthis : 1;
}
ps_precall;

double ps_precall_start_time;
ps_precall g_precall_tmp;
ps_precall g_precall_buf[ MAX_PRECALL_ENTRIES ];
ps_precall* g_precall_at;
ps_precall* g_precall_end;
#define ps_precall_begin( _sendargs, _sendthis, _callsrc ) \
	g_precall_tmp.sendargs = (unsigned) (_sendargs); \
	g_precall_tmp.sendthis = 0x1 & (unsigned) (_sendthis); \
	g_precall_tmp.callsrc = 0x1 & (unsigned) (_callsrc); \
	ps_precall_start_time = sgs_hqtime();
void ps_precall_end( unsigned recvargs, unsigned recvthis, unsigned functype )
{
	double t = sgs_hqtime();
	g_precall_tmp.recvargs = recvargs;
	g_precall_tmp.recvthis = recvthis & 0x1;
	g_precall_tmp.functype = functype & 0x3;
	g_precall_tmp.time = t - ps_precall_start_time;
	if( g_precall_at < g_precall_end )
		*g_precall_at++ = g_precall_tmp;
}
/* --- */


#include "../src/sgs_bcg.c"
#include "../src/sgs_ctx.c"
#include "../src/sgs_fnt.c"
#include "../src/sgs_proc.c"
#include "../src/sgs_regex.c"
#include "../src/sgs_srlz.c"
#include "../src/sgs_std.c"
#include "../src/sgs_stdL.c"
#include "../src/sgs_tok.c"
#include "../src/sgs_util.c"
#include "../src/sgs_xpc.c"


double ovh_div( double s, unsigned c )
{
	return c == 0 ? -1.0 : s * 1000.0 / c;
}

int main( int argc, char* argv[] )
{
	double msrovr = 0;
	ps_precall* pc;
	SGS_CTX;
#ifdef _WIN32
	QueryPerformanceFrequency( &freq );
#endif
	puts( "--- SGScript microbenchmark ---" );
	
	puts( "> function calls" );
	g_precall_at = g_precall_buf;
	g_precall_end = g_precall_buf + MAX_PRECALL_ENTRIES;
	
	/* measure overhead */
#define OVH_COUNT 100000
	for( int i = 0; i < OVH_COUNT; ++i )
	{
		ps_precall_begin( 0, 0, 0 );
		ps_precall_end( 0, 0, 0 );
		msrovr += g_precall_buf[ i ].time;
	}
	msrovr /= OVH_COUNT;
	printf( "-- measurement overhead: %.8f msec/call --\n", msrovr * 1000 );
	g_precall_at = g_precall_buf;
	g_precall_end = g_precall_buf + MAX_PRECALL_ENTRIES;
	
	C = sgs_CreateEngine();
	sgs_ExecString( C, ""
		"function _zero(){}"
		"function __one(a){}"
		"function __two(a,b){}"
		"function three(a,b,c){}"
		"function _four(a,b,c,d){}"
		"for( i = 0; i < 100000; ++i )"
		"{"
		"	_zero(); _zero(1); _zero(1, 2); _zero(1, 2, 3); _zero(1, 2, 3, 4);"
		"	__one(); __one(1); __one(1, 2); __one(1, 2, 3); __one(1, 2, 3, 4);"
		"	__two(); __two(1); __two(1, 2); __two(1, 2, 3); __two(1, 2, 3, 4);"
		"	three(); three(1); three(1, 2); three(1, 2, 3); three(1, 2, 3, 4);"
		"	_four(); _four(1); _four(1, 2); _four(1, 2, 3); _four(1, 2, 3, 4);"
		"}"
	);
	sgs_DestroyEngine( C );
	
	/* overview */
	{
		double total_overhead = 0;
		unsigned count = (unsigned) ( g_precall_at - g_precall_buf );
		printf( "- %u call events triggered\n", count );
		for( pc = g_precall_buf; pc < g_precall_end; ++pc )
		{
			total_overhead += pc->time - msrovr;
		}
		printf( "- %.4f msec total overhead\n", total_overhead * 1000 );
		printf( "- %.8f msec/call average overhead\n", ovh_div( total_overhead, count ) );
	}
	/* by sent counts */
	{
		int i;
		double times[5] = { 0.0, 0.0, 0.0, 0.0, 0.0 };
		unsigned counts[5] = { 0, 0, 0, 0, 0 };
		const char* pfxs[5] = { "0: ", "1: ", "2: ", "3: ", "4+:" };
		for( pc = g_precall_buf; pc < g_precall_end; ++pc )
		{
			if( pc->functype != FUNCTYPE_SGS )
				continue;
			unsigned ssend = pc->sendargs + pc->sendthis;
			unsigned tgt = ssend < 4 ? ssend : 4;
			times[ tgt ] += pc->time - msrovr;
			counts[ tgt ] += 1;
		}
		puts( "-- overhead by sent argument count ---" );
		for( i = 0; i < 5; ++i )
			printf( "- %s %.8f msec/call\n", pfxs[ i ], ovh_div( times[ i ], counts[ i ] ) );
	}
	/* by received counts */
	{
		int i;
		double times[5] = { 0.0, 0.0, 0.0, 0.0, 0.0 };
		unsigned counts[5] = { 0, 0, 0, 0, 0 };
		const char* pfxs[5] = { "0: ", "1: ", "2: ", "3: ", "4+:" };
		for( pc = g_precall_buf; pc < g_precall_end; ++pc )
		{
			if( pc->functype != FUNCTYPE_SGS )
				continue;
			unsigned srecv = pc->recvargs + pc->recvthis;
			unsigned tgt = srecv < 4 ? srecv : 4;
			times[ tgt ] += pc->time - msrovr;
			counts[ tgt ] += 1;
		}
		puts( "-- overhead by received argument count ---" );
		for( i = 0; i < 5; ++i )
			printf( "- %s %.8f msec/call\n", pfxs[ i ], ovh_div( times[ i ], counts[ i ] ) );
	}
	/* by count diffs */
	{
		double same_sum = 0, less_sum = 0, more_sum = 0;
		unsigned same_argc = 0, less_argc = 0, more_argc = 0;
		for( pc = g_precall_buf; pc < g_precall_end; ++pc )
		{
			if( pc->functype != FUNCTYPE_SGS )
				continue;
			unsigned ssend = pc->sendargs + pc->sendthis;
			unsigned srecv = pc->recvargs + pc->recvthis;
			if( ssend == srecv )
			{
				same_sum += pc->time - msrovr;
				same_argc += 1;
			}
			else if( ssend < srecv )
			{
				less_sum += pc->time - msrovr;
				less_argc += 1;
			}
			else
			{
				more_sum += pc->time - msrovr;
				more_argc += 1;
			}
		}
		puts( "-- overhead by argument count differences --" );
		printf( "- same: %.8f msec/call\n", ovh_div( same_sum, same_argc ) );
		printf( "- less: %.8f msec/call\n", ovh_div( less_sum, less_argc ) );
		printf( "- more: %.8f msec/call\n", ovh_div( more_sum, more_argc ) );
	}
	
	return 0;
}

