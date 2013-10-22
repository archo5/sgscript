

#define SGS_INTERNAL
#define SGS_REALLY_INTERNAL

#include "sgs_int.h"


typedef
struct _sgs_JIT_Trace
{
	int fast;
	func_t* func;
	int32_t beg, end;
}
sgs_JIT_Trace;


typedef
struct _sgs_JIT_Data
{
	sgs_JIT_Trace trace;
	sgs_JIT_Trace* curr;
}
sgs_JIT_Data;


void sgsJIT_Init( SGS_CTX )
{
	sgs_JIT_Data* data = sgs_Alloc( sgs_JIT_Data );
	data->curr = NULL;
	C->jitdata = data;
}

void sgsJIT_Destroy( SGS_CTX )
{
	sgs_Dealloc( C->jitdata );
}


void sgsJIT_CB_FI( SGS_CTX )
{
}

void sgsJIT_CB_FO( SGS_CTX )
{
}

void sgsJIT_CB_NI( SGS_CTX )
{
	sgs_JIT_Data* data = (sgs_JIT_Data*) C->jitdata;
	sgs_StackFrame* sf = C->sf_last;
	uint32_t ci = *sf->iptr;
	
	if( data->curr )
	{
		/* add data to trace: types used, whether jump was taken etc. */
	}
	
	if( SGS_INSTR_GET_OP( ci ) == SI_JUMP &&
		( !data->curr || data->curr->end != sf->iptr - sf->code ) )
	{
		int jmp = (int) (int16_t) SGS_INSTR_GET_E( ci );
		if( jmp < 0 )
		{
			data->trace.func = sf->func ? sf->func->data.F : NULL;
			data->trace.end = sf->iptr - sf->code;
			data->trace.beg = data->trace.end + 1 + jmp;
			data->trace.fast = 1;
			data->curr = &data->trace;
			printf( "backwards jump! (%d, trace=[%d,%d])\n", jmp, data->trace.beg, data->trace.end );
		}
	}
}

