


#define SGS_PERFEVENT( x ) x
#define BUILDING_SGS 1
#define SGS_INSTRUMENTED_BUILD
#define SGS_INTERNAL_STRINGTABLES
#define SGS_USE_FILESYSTEM
#include "../src/sgs_util.h"


#define MAX_PRECALL_ENTRIES 1000000


/* --- PROFILING HELPERS --- */
double sgs_hqtime()
{
#ifdef _WIN32
	LARGE_INTEGER c, f;
	QueryPerformanceCounter( &c );
	QueryPerformanceFrequency( &f );
	return (double) c.QuadPart / (double) f.QuadPart;
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
	float time;
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
	g_precall_tmp.time = (float) ( t - ps_precall_start_time );
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


int main( int argc, char* argv[] )
{
	int count;
	ps_precall* pc;
	SGS_CTX;
	puts( "--- SGScript microbenchmark ---" );
	
	puts( "> function calls" );
	g_precall_at = g_precall_buf;
	g_precall_end = g_precall_buf + MAX_PRECALL_ENTRIES;
	C = sgs_CreateEngine();
	sgs_ExecString( C, ""
		"function a(){}"
		"for( i = 0; i < 100000; ++i )"
		"{"
		"	a(); a(); a(); a(); a();"
		"}"
	);
	sgs_DestroyEngine( C );
	
	count = (int) ( g_precall_at - g_precall_buf );
	printf( "- %u call events triggered\n", count );
	double total_overhead = 0;
	for( pc = g_precall_buf; pc < g_precall_end; ++pc )
	{
		total_overhead += pc->time;
	}
	printf( "- %.4f msec. total overhead\n", total_overhead * 1000 );
	printf( "- %.8f msec. average overhead\n", total_overhead * 1000 / count );
	
	return 0;
}

