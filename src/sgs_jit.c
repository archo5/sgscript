

#define SGS_INTERNAL
#define SGS_REALLY_INTERNAL

#include "sgs_int.h"


#define JIT_TRACE_MAX_SIZE 1024 /* 1 kB */


typedef
struct _traceitem
{
	uint32_t instr;
	uint32_t vt0, vt1, vt2;
	uint8_t op, flags;
}
traceitem;

#define JIT_TRACEITEM_NOVAR    0xffff
#define JIT_TRACEITEM_RESTART  0xffffffff

#define JIT_TRACEITEM_V0CONST  0x01
#define JIT_TRACEITEM_V1CONST  0x02
#define JIT_TRACEITEM_V2CONST  0x04
#define JIT_TRACEITEM_JUMPED   0x10


#define JIT_STATE_DISCARDED  0
#define JIT_STATE_TRACING    1
#define JIT_STATE_TRACED     2
#define JIT_STATE_COMPILED   3

#define JIT_FLAG_RESTARTED   0x01


typedef
struct _sgs_JIT_Trace
{
	uint8_t state;
	uint8_t flags;
	func_t* func;
	int32_t beg, end;
	MemBuf data;
}
sgs_JIT_Trace;


typedef
struct _sgs_JIT_Data
{
	sgs_JIT_Trace trace;
	sgs_JIT_Trace* curr;
}
sgs_JIT_Data;



/* OpInfo */
#define OI_HAS0 0x01
#define OI_HAS1 0x02
#define OI_HAS2 0x04
#define OI_RET0 0x08 | OI_HAS0
#define OI_RET1 0x10 | OI_HAS1
#define OI_RET2 0x20 | OI_HAS2

#define OI_PB OI_RET0 | OI_HAS1
#define OI_PA OI_PB | OI_HAS2

static uint8_t baseline_flags[] =
{
	/* nop */
	0,
	/* push, *removed* */
	OI_HAS1, 0,
	/* retn, jump, jmpt, jmpf, call */
	0, 0, OI_HAS2, OI_HAS2, OI_HAS2,
	/* forprep, forload, forjump */
	OI_RET0 | OI_HAS1, OI_HAS0 | OI_RET1 | OI_RET2, OI_HAS2,
	/* loadconst, getvar, setvar, getprop, setprop, getindex, setindex */
	OI_RET2, OI_RET0 | OI_HAS1, OI_HAS1 | OI_HAS2,
	OI_RET0 | OI_HAS1 | OI_HAS2, OI_HAS0 | OI_HAS1 | OI_HAS2,
	OI_RET0 | OI_HAS1 | OI_HAS2, OI_HAS0 | OI_HAS1 | OI_HAS2,
	/* genclsr, pushclsr, makeclsr, getclsr, setclsr */
	0, OI_HAS0, OI_RET0 | OI_HAS1, OI_RET0, OI_HAS2,
	/* set, mconcat, concat, negate, bool_inv, invert */
	OI_PB, OI_RET0, OI_PA, OI_PB, OI_PB, OI_PB,
	/* inc, dec, add, sub, mul, div, mod */
	OI_PB, OI_PB, OI_PA, OI_PA, OI_PA, OI_PA, OI_PA,
	/* and, or, xor, lsh, rsh */
	OI_PA, OI_PA, OI_PA, OI_PA, OI_PA,
	/* seq, sneq, eq, neq, lt, gte, gt, lte */
	OI_PA, OI_PA, OI_PA, OI_PA, OI_PA, OI_PA, OI_PA, OI_PA,
	/* array, dict */
	OI_RET0, OI_RET0,
};

static void sgsJIT_ProcFrame( SGS_CTX, sgs_StackFrame* sf, sgs_JIT_Trace* trace )
{
	if( trace->data.size >= JIT_TRACE_MAX_SIZE )
	{
		/* trace is getting too big, drop it */
		trace->state = JIT_STATE_DISCARDED;
		
		printf( "trace too long!\n" );
		
		if( trace->flags & JIT_FLAG_RESTARTED )
		{
			trace->state = JIT_STATE_TRACED;
			
			printf( "... but it was restarted!\n" );
		}
	}
	else if( trace->end == sf->lptr - sf->code )
	{
		/* end of trace */
		traceitem ti;
		memset( &ti, 0, sizeof(ti) );
		
		ti.instr = JIT_TRACEITEM_RESTART;
		membuf_appbuf( &trace->data, C, &ti, sizeof(ti) );
		
		trace->flags |= JIT_FLAG_RESTARTED;
		
		printf( "trace restarted!\n" );
	}
	else
	{
		const sgs_Variable* consts = sgs_func_consts( sf->func->data.F );
		uint32_t ci = *sf->iptr;
		uint8_t ci_op = SGS_INSTR_GET_OP( ci );
		uint16_t ci_a = (uint16_t) SGS_INSTR_GET_A( ci );
		uint16_t ci_b = (uint16_t) SGS_INSTR_GET_B( ci );
		uint16_t ci_c = (uint16_t) SGS_INSTR_GET_C( ci );
		uint8_t ci_flags = baseline_flags[ ci_op ];
		traceitem ti = { ci, JIT_TRACEITEM_NOVAR, JIT_TRACEITEM_NOVAR,
			JIT_TRACEITEM_NOVAR, ci_op, 0 };
		
		if( ci_flags & OI_HAS0 )
		{
			if( ci_a & 0x100 )
			{
				ti.vt0 = consts[ ci_a & 0xff ].type;
				ti.flags |= JIT_TRACEITEM_V0CONST;
			}
			else
				ti.vt0 = C->stack_off[ ci_a ].type;
		}
		if( ci_flags & OI_HAS1 )
		{
			if( ci_b & 0x100 )
			{
				ti.vt1 = consts[ ci_b & 0xff ].type;
				ti.flags |= JIT_TRACEITEM_V1CONST;
			}
			else
				ti.vt1 = C->stack_off[ ci_b ].type;
		}
		if( ci_flags & OI_HAS2 )
		{
			if( ci_c & 0x100 )
			{
				ti.vt2 = consts[ ci_c & 0xff ].type;
				ti.flags |= JIT_TRACEITEM_V2CONST;
			}
			else
				ti.vt2 = C->stack_off[ ci_c ].type;
		}
		
		if( sf->iptr != sf->lptr )
			ti.flags |= JIT_TRACEITEM_JUMPED;
		
		membuf_appbuf( &trace->data, C, &ti, sizeof(ti) );
	}
}



void sgsJIT_Init( SGS_CTX )
{
	sgs_JIT_Data* data = sgs_Alloc( sgs_JIT_Data );
	data->curr = NULL;
	C->jitdata = data;
	
	memset( &data->trace, 0, sizeof(data->trace) );
	data->trace.data = membuf_create();
}

void sgsJIT_Destroy( SGS_CTX )
{
	sgs_JIT_Data* data = (sgs_JIT_Data*) C->jitdata;
	membuf_destroy( &data->trace.data, C );
	
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
	uint32_t ci = *sf->lptr;
	
	if( data->curr && data->curr->state == JIT_STATE_TRACING )
	{
		/* add data to trace: types used, whether constant, whether jump was taken etc. */
		sgsJIT_ProcFrame( C, sf, data->curr );
	}
	
	if( SGS_INSTR_GET_OP( ci ) == SI_JUMP &&
		( !data->curr || data->curr->end != sf->lptr - sf->code ) )
	{
		int jmp = (int) (int16_t) SGS_INSTR_GET_E( ci );
		if( jmp < 0 )
		{
			data->trace.func = sf->func ? sf->func->data.F : NULL;
			data->trace.end = sf->lptr - sf->code;
			data->trace.beg = data->trace.end + 1 + jmp;
			data->trace.state = JIT_STATE_TRACING;
			data->curr = &data->trace;
			printf( "backwards jump! (%d, trace=[%d,%d])\n", jmp, data->trace.beg, data->trace.end );
		}
	}
}

