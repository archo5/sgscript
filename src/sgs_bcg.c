

#include "sgs_int.h"


typedef int32_t sgs_rcpos_t;
#define rcpos_t sgs_rcpos_t

typedef struct sgs_BreakInfo sgs_BreakInfo;
struct sgs_BreakInfo
{
	sgs_BreakInfo* next;
	uint32_t jdoff;  /* jump data offset */
	uint16_t numlp;  /* which loop */
	uint8_t  iscont; /* is a "continue"? */
};

#ifndef SGS_MAX_DEFERRED_BLOCKS
#  define SGS_MAX_DEFERRED_BLOCKS 256
#endif

typedef struct sgs_BlockInfo sgs_BlockInfo;
struct sgs_BlockInfo
{
	sgs_BlockInfo* parent;
	size_t defer_start;
};

typedef struct sgs_LoopInfo sgs_LoopInfo;
struct sgs_LoopInfo
{
	sgs_LoopInfo* parent;
	sgs_BlockInfo* block;
};

typedef struct sgs_CompFunc
{
	sgs_MemBuf consts;
	sgs_MemBuf code;
	sgs_MemBuf lnbuf;
	uint8_t gotthis; /* bool */
	uint8_t numargs; /* guaranteed to be <= 255 by a test in `preparse_arglist` */
	uint8_t numtmp; /* reg. count (0-255, incl. numargs) - numargs (0-255) */
	uint8_t numclsr;
	uint8_t inclsr;
}
sgs_CompFunc;

#define SGS_FCV_MODE_NUMFOR 1
typedef struct sgs_FCVar
{
	const char* name;
	uint16_t nmlength;
	uint8_t mode;
}
sgs_FCVar;

struct sgs_FuncCtx
{
	int32_t func;
	sgs_rcpos_t regs, lastreg;
	sgs_MemBuf vars;
	sgs_MemBuf gvars;
	sgs_MemBuf clsr;
	int inclsr, outclsr, syncdepth;
	int32_t loops;
	sgs_BreakInfo* binfo;
	sgs_BlockInfo* blocks;
	sgs_LoopInfo* loopinfo;
	sgs_FTNode* defers[ SGS_MAX_DEFERRED_BLOCKS ];
	size_t num_defers;
	sgs_CompFunc cfunc;
};


#define over_limit( x, lim ) ((x)>(lim)||(x)<(-lim))


/* register allocation */

static SGS_INLINE rcpos_t comp_reg_alloc( SGS_CTX )
{
	rcpos_t out = C->fctx->regs++;
	if( out > 0xff )
	{
		C->state |= SGS_HAS_ERRORS | SGS_MUST_STOP;
		sgs_Msg( C, SGS_ERROR, "Max. register count exceeded" );
	}
	return out;
}

static SGS_INLINE rcpos_t comp_reg_alloc_n( SGS_CTX, int n )
{
	rcpos_t out;
	if( n < 1 )
		return -1;
	out = comp_reg_alloc( C );
	n--;
	while( n --> 0 )
	{
		if( SGS_HAS_FLAG( C->state, SGS_MUST_STOP ) )
			break;
		comp_reg_alloc( C );
	}
	return out;
}

/* must exceed ranges and look obvious after % 256 */
#define SGS_RCPOS_UNSPEC 65791 /* 65536 + 255 */
static SGS_INLINE void comp_reg_ensure( SGS_CTX, rcpos_t* ppos )
{
	sgs_BreakIf( SGS_CONSTVAR( *ppos ) ); /* constants cannot be output registers */
	if( *ppos == SGS_RCPOS_UNSPEC )
		*ppos = comp_reg_alloc( C );
}

static SGS_INLINE void comp_reg_unwind( SGS_CTX, rcpos_t pos )
{
	sgs_BreakIf( pos > C->fctx->regs );
	if( C->fctx->regs > C->fctx->lastreg )
		C->fctx->lastreg = C->fctx->regs;
	C->fctx->regs = pos;
}


static void fctx_binfo_add( SGS_CTX, sgs_FuncCtx* fctx,
	uint32_t ioff, uint16_t loop, uint8_t iscont )
{
	sgs_BreakInfo* binfo = sgs_Alloc( sgs_BreakInfo );
	binfo->jdoff = ioff;
	binfo->numlp = loop;
	binfo->iscont = iscont;
	binfo->next = fctx->binfo;
	fctx->binfo = binfo;
}

static void fctx_binfo_rem( SGS_CTX, sgs_FuncCtx* fctx, sgs_BreakInfo* prev )
{
	sgs_BreakInfo* pn;
	if( prev )
	{
		pn = prev->next;
		prev->next = prev->next->next;
		sgs_Dealloc( pn );
	}
	else
	{
		pn = fctx->binfo;
		fctx->binfo = fctx->binfo->next;
		sgs_Dealloc( pn );
	}
}

static void fctx_block_push( sgs_FuncCtx* fctx, sgs_BlockInfo* bdata, sgs_LoopInfo* ldata )
{
	bdata->defer_start = fctx->num_defers;
	bdata->parent = fctx->blocks;
	fctx->blocks = bdata;
	if( ldata )
	{
		ldata->block = bdata;
		ldata->parent = fctx->loopinfo;
		fctx->loopinfo = ldata;
	}
}

static void fctx_block_pop( sgs_FuncCtx* fctx, sgs_BlockInfo* bdata, sgs_LoopInfo* ldata )
{
	sgs_BreakIf( bdata != fctx->blocks );
	fctx->num_defers = bdata->defer_start;
	fctx->blocks = bdata->parent;
	if( ldata )
	{
		sgs_BreakIf( ldata != fctx->loopinfo );
		fctx->loopinfo = ldata->parent;
	}
}

static void fctx_defer_add( SGS_CTX, sgs_FTNode* stmt )
{
	sgs_FuncCtx* fctx = C->fctx;
	if( fctx->num_defers >= SGS_MAX_DEFERRED_BLOCKS )
	{
		C->state |= SGS_HAS_ERRORS | SGS_MUST_STOP;
		sgs_Msg( C, SGS_ERROR, "exceeded deferred block limit" );
	}
	else
		fctx->defers[ fctx->num_defers++ ] = stmt;
}


static int find_var( sgs_MemBuf* S, const char* str, unsigned len )
{
	sgs_FCVar *vp,
		*vstart = (sgs_FCVar*) S->ptr,
		*vend = (sgs_FCVar*)( S->ptr + S->size );
	vp = vstart;
	while( vp != vend )
	{
		if( vp->nmlength == len && !memcmp( vp->name, str, len ) )
			return vp - vstart;
		vp++;
	}
	return -1;
}

static sgs_FCVar* find_nth_var( sgs_MemBuf* S, int which )
{
	sgs_FCVar
		*vstart = (sgs_FCVar*) S->ptr,
		*vend = (sgs_FCVar*)( S->ptr + S->size );
	if( which < 0 || which >= ( vend - vstart ) )
		return NULL;
	return vstart + which;
}

static int add_var( sgs_MemBuf* S, SGS_CTX, const char* str, unsigned len )
{
	int pos = find_var( S, str, len );
	if( pos < 0 )
	{
		sgs_FCVar nv = { str, len, 0 };
		sgs_membuf_appbuf( S, C, &nv, sizeof(nv) );
		return SGS_TRUE;
	}
	return SGS_FALSE;
}

static void expand_varlist( sgs_MemBuf* S, SGS_CTX )
{
	size_t i, count = S->size / sizeof( sgs_FCVar );
	for( i = 0; i < count; ++i )
	{
		sgs_FCVar* vstart = (sgs_FCVar*) S->ptr;
		if( vstart[ i ].mode == SGS_FCV_MODE_NUMFOR )
		{
			static const sgs_FCVar nvs[] =
			{
				{ SGS_STRLITBUF( "(num.for init)" ), 0 },
				{ SGS_STRLITBUF( "(num.for end)" ), 0 },
				{ SGS_STRLITBUF( "(num.for step)" ), 0 },
			};
			sgs_membuf_insbuf( S, C, ( i + 1 ) * sizeof( sgs_FCVar ), &nvs, sizeof(nvs) );
			count = S->size / sizeof( sgs_FCVar );
			i += 3;
		}
	}
}

#define find_varT( S, tok ) \
	find_var( S, (char*) (tok) + 2, tok[1] )
#define add_varT( S, C, tok ) \
	add_var( S, C, (char*) (tok) + 2, tok[1] )


static sgs_FuncCtx* fctx_create( SGS_CTX )
{
	sgs_FuncCtx* fctx = sgs_Alloc( sgs_FuncCtx );
	fctx->func = SGS_TRUE;
	fctx->regs = 0;
	fctx->lastreg = -1;
	fctx->vars = sgs_membuf_create();
	fctx->gvars = sgs_membuf_create();
	fctx->clsr = sgs_membuf_create();
	fctx->inclsr = 0;
	fctx->outclsr = 0;
	fctx->syncdepth = 0;
	fctx->loops = 0;
	fctx->binfo = NULL;
	fctx->blocks = NULL;
	fctx->loopinfo = NULL;
	fctx->num_defers = 0;
	
	add_var( &fctx->gvars, C, SGS_STRLITBUF( "_G" ) );
	
	fctx->cfunc.consts = sgs_membuf_create();
	fctx->cfunc.code = sgs_membuf_create();
	fctx->cfunc.lnbuf = sgs_membuf_create();
	fctx->cfunc.gotthis = SGS_FALSE;
	fctx->cfunc.numargs = 0;
	fctx->cfunc.numtmp = 0;
	fctx->cfunc.numclsr = 0;
	fctx->cfunc.inclsr = 0;
	
	return fctx;
}

static void fctx_destroy( SGS_CTX, sgs_FuncCtx* fctx )
{
	sgs_CompFunc* func = &fctx->cfunc;
	sgs_Variable* vbeg = SGS_ASSUME_ALIGNED( func->consts.ptr, sgs_Variable );
	sgs_Variable* vend = SGS_ASSUME_ALIGNED( func->consts.ptr + func->consts.size, sgs_Variable );
	sgs_Variable* var = vbeg;
	while( var < vend )
	{
		sgs_Release( C, var );
		var++;
	}

	sgs_membuf_destroy( &func->code, C );
	sgs_membuf_destroy( &func->consts, C );
	sgs_membuf_destroy( &func->lnbuf, C );
	
	while( fctx->binfo )
		fctx_binfo_rem( C, fctx, NULL );
	sgs_membuf_destroy( &fctx->vars, C );
	sgs_membuf_destroy( &fctx->gvars, C );
	sgs_membuf_destroy( &fctx->clsr, C );
	sgs_Dealloc( fctx );
}

#if SGS_DUMP_BYTECODE || ( SGS_DEBUG && SGS_DEBUG_DATA )
static void fctx_dumpvarlist( SGS_CTX, sgs_MemBuf* S )
{
	sgs_FCVar *vp,
		*vstart = (sgs_FCVar*) S->ptr,
		*vend = (sgs_FCVar*)( S->ptr + S->size );
	vp = vstart;
	while( vp != vend )
	{
		if( vp != vstart )
			sgs_ErrWrite( C, SGS_STRLITBUF( ", " ) );
		sgs_ErrWrite( C, vp->name, vp->nmlength );
		vp++;
	}
}
static void fctx_dump( SGS_CTX, sgs_FuncCtx* fctx )
{
	sgs_ErrWritef( C, "Type: %s\n", fctx->func ? "Function" : "Main code" );
	sgs_ErrWritef( C, "Globals: " ); fctx_dumpvarlist( C, &fctx->gvars ); sgs_ErrWritef( C, "\n" );
	sgs_ErrWritef( C, "Variables: " ); fctx_dumpvarlist( C, &fctx->vars ); sgs_ErrWritef( C, "\n" );
	sgs_ErrWritef( C, "Closures in=%d out=%d : ", fctx->inclsr, fctx->outclsr );
	fctx_dumpvarlist( C, &fctx->clsr ); printf( "\n" );
}
#endif


static void dump_rcpos( SGS_CTX, int arg )
{
	char rc = SGS_CONSTVAR( arg ) ? 'C' : 'R';
	arg = SGS_CONSTDEC( arg );
	sgs_ErrWritef( C, "%c%d", rc, arg );
}

static void dump_const( SGS_CTX, char which, int arg, const sgs_Variable* consts )
{
	if( SGS_CONSTVAR( arg ) == 0 )
		return;
	arg = SGS_CONSTDEC( arg );
	sgs_ErrWritef( C, " ; %c(C%d){", which, arg );
	sgsVM_VarDump( C, &consts[ arg ] );
	sgs_ErrWritef( C, "}" );
}

enum sgs_OpDumpType
{
	ODT_NOP = 0,
	ODT_PUSH,
	ODT_INT,
	ODT_RET1,
	
	ODT_RETN,
	ODT_JMP,
	ODT_CONDJMP,
	ODT_CALL,
	
	ODT_UNOP,
	ODT_BINOP,
	ODT_MCONCAT,
	ODT_FORLOAD,
	
	ODT_NFOR,
	ODT_LOADCONST,
	ODT_SETVAR,
	ODT_ARRPUSH,
	
	ODT_NUMBERS,
	ODT_MAKECLSR,
	ODT_GETCLSR,
	ODT_SETCLSR,
	
	ODT_ARRAY,
	ODT_DICTMAP,
	ODT_CLASS,
	ODT_RSYM,
	
	ODT__COUNT,
};
#define SHIFT2BITS( b, c, sh ) (((b)|((c)<<1))<<sh)
static const uint8_t sgs_OpReferConsts[] =
{
	/* push=B */ SHIFT2BITS( 1, 0, 2 ) | /* ret1=C */ SHIFT2BITS( 0, 1, 6 ),
	/* condjmp=C */ SHIFT2BITS( 0, 1, 4 ),
	/* unop=B */ SHIFT2BITS( 1, 0, 0 ) | /* binop=B,C */ SHIFT2BITS( 1, 1, 2 ),
	/* setvar=B,C */ SHIFT2BITS( 1, 1, 4 ) | /* arrpush=C */ SHIFT2BITS( 0, 1, 6 ),
	/* makeclsr=B */ SHIFT2BITS( 1, 0, 2 ) | /* setclsr=C */ SHIFT2BITS( 0, 1, 6 ),
	/* class=B,C */ SHIFT2BITS( 1, 1, 4 ) | /* rsym=B,C */ SHIFT2BITS( 1, 1, 6 ),
};
#undef SHIFT2BITS
SGS_CASSERT( SGS_ARRAY_SIZE( sgs_OpReferConsts ) * 4 >= ODT__COUNT, good_odt_rc_count );
static const uint8_t sgs_OpDumpType[] =
{
	/* NOP  */ ODT_NOP,
	
	/* PUSH */ ODT_PUSH,
	/* INT  */ ODT_INT,
	
	/* RET1 */ ODT_RET1,
	/* RETN */ ODT_RETN,
	/* JUMP */ ODT_JMP,
	/* JMPT */ ODT_CONDJMP,
	/* JMPF */ ODT_CONDJMP,
	/* JMPN */ ODT_CONDJMP,
	/* CALL */ ODT_CALL,
	
	/* FORPREP  */ ODT_UNOP,
	/* FORLOAD  */ ODT_FORLOAD,
	/* FORJUMP  */ ODT_CONDJMP,
	/* NFORPREP */ ODT_NFOR,
	/* NFORJUMP */ ODT_NFOR,
	
	/* LOADCONST */ ODT_LOADCONST,
	/* GETVAR    */ ODT_UNOP,
	/* SETVAR    */ ODT_SETVAR,
	/* GETPROP   */ ODT_BINOP,
	/* SETPROP   */ ODT_BINOP,
	/* GETINDEX  */ ODT_BINOP,
	/* SETINDEX  */ ODT_BINOP,
	/* ARRPUSH   */ ODT_ARRPUSH,
	
	/* CLSRINFO */ ODT_NUMBERS,
	/* MAKECLSR */ ODT_MAKECLSR,
	/* GETCLSR  */ ODT_GETCLSR,
	/* SETCLSR  */ ODT_SETCLSR,
	
	/* SET      */ ODT_UNOP,
	/* MCONCAT  */ ODT_MCONCAT,
	/* NEGATE   */ ODT_UNOP,
	/* BOOL_INV */ ODT_UNOP,
	/* INVERT   */ ODT_UNOP,
	
	/* INC */ ODT_UNOP,
	/* DEC */ ODT_UNOP,
	/* ADD */ ODT_BINOP,
	/* SUB */ ODT_BINOP,
	/* MUL */ ODT_BINOP,
	/* DIV */ ODT_BINOP,
	/* MOD */ ODT_BINOP,
	
	/* AND */ ODT_BINOP,
	/* OR  */ ODT_BINOP,
	/* XOR */ ODT_BINOP,
	/* LSH */ ODT_BINOP,
	/* RSH */ ODT_BINOP,
	
	/* SEQ    */ ODT_BINOP,
	/* SNEQ   */ ODT_BINOP,
	/* EQ     */ ODT_BINOP,
	/* NEQ    */ ODT_BINOP,
	/* LT     */ ODT_BINOP,
	/* GTE    */ ODT_BINOP,
	/* GT     */ ODT_BINOP,
	/* LTE    */ ODT_BINOP,
	/* RAWCMP */ ODT_BINOP,
	
	/* ARRAY   */ ODT_ARRAY,
	/* DICT    */ ODT_DICTMAP,
	/* MAP     */ ODT_DICTMAP,
	/* CLASS   */ ODT_CLASS,
	/* NEW     */ ODT_UNOP,
	/* RSYM    */ ODT_RSYM,
	/* COTRT   */ ODT_UNOP,
	/* COTRF   */ ODT_UNOP,
	/* COABORT */ ODT_CONDJMP,
	/* YLDJMP  */ ODT_CONDJMP,
};
SGS_CASSERT( SGS_ARRAY_SIZE( sgs_OpDumpType ) == SGS_SI_COUNT, good_instr_odt_count );

void sgsBC_DumpOpcode( SGS_CTX, const sgs_instr_t* ptr, size_t count,
	const sgs_instr_t* numstart, const sgs_Variable* consts, const sgs_LineNum* lines )
{
	const sgs_instr_t* pend = ptr + count;
	for( ; ptr < pend; ptr++ )
	{
		sgs_instr_t I = *ptr;
		int op = SGS_INSTR_GET_OP( I ), argA = SGS_INSTR_GET_A( I ),
			argB = SGS_INSTR_GET_B( I ), argC = SGS_INSTR_GET_C( I ),
			argE = SGS_INSTR_GET_E( I ), odt;
		
		if( numstart )
		{
			sgs_ErrWritef( C, "    %04d ", (int)( ptr - numstart ) );
		}
		if( lines )
		{
			sgs_ErrWritef( C, "[line %d] ", lines[ ptr - numstart ] );
		}
		else
		{
			sgs_ErrWritef( C, "| " );
		}
		
		if( op >= SGS_SI_COUNT )
		{
			sgs_ErrWritef( C, "<error> (op=%d A=%d B=%d C=%d E=%d)\n",
				op, argA, argB, argC, argE );
			continue;
		}
		
		sgs_ErrWritef( C, "%s ", sgs_CodeString( SGS_CODE_OP, op ) );
		
		odt = sgs_OpDumpType[ op ];
		switch( odt )
		{
		case ODT_NOP: break;
		case ODT_PUSH: dump_rcpos( C, argB ); break;
		case ODT_INT: sgs_ErrWritef( C, "%d", argC ); break;
		case ODT_RET1: dump_rcpos( C, argC ); break;
		case ODT_RETN: sgs_ErrWritef( C, "%d", argA ); break;
		case ODT_JMP: sgs_ErrWritef( C, "%d", (int) (int16_t) argE ); break;
		case ODT_CONDJMP: dump_rcpos( C, argC );
			sgs_ErrWritef( C, ", %d", (int) (int16_t) argE ); break;
		case ODT_CALL: sgs_ErrWritef( C, "args: %d - %d expect: %d%s",
			argB & 0xff, argC, argA, ( argB & 0x100 ) ? ", method" : "" );
			break;
		case ODT_UNOP:
			sgs_ErrWritef( C, "R%" PRId32" <= ", argA );
			dump_rcpos( C, argB );
			break;
		case ODT_BINOP:
			sgs_ErrWritef( C, "R%" PRId32" <= ", argA );
			dump_rcpos( C, argB );
			sgs_ErrWritef( C, ", " );
			dump_rcpos( C, argC );
			break;
		case ODT_MCONCAT: dump_rcpos( C, argA ); sgs_ErrWritef( C, " [%d]", argB ); break;
		case ODT_FORLOAD:
			dump_rcpos( C, argA );
			sgs_ErrWritef( C, " => " );
			if( argB < 0x100 )
				dump_rcpos( C, argB );
			else
				sgs_ErrWritef( C, "<off>" );
			sgs_ErrWritef( C, ", " );
			if( argC < 0x100 )
				dump_rcpos( C, argC );
			else
				sgs_ErrWritef( C, "<off>" );
			break;
		case ODT_NFOR: sgs_ErrWritef( C, "(%s) ", argC & 0x100 ? "real" : "int" );
			dump_rcpos( C, argC & 0xff ); sgs_ErrWritef( C, ", %d", (int) (int16_t) argE ); break;
		case ODT_LOADCONST: dump_rcpos( C, argC );
			sgs_ErrWritef( C, " <= C%d: ", argE );
			sgsVM_VarDump( C, &consts[ argE ] ); break;
		case ODT_SETVAR: dump_rcpos( C, argB );
			sgs_ErrWritef( C, " <= " ); dump_rcpos( C, argC ); break;
		case ODT_ARRPUSH: sgs_ErrWritef( C, "R%d <= ", argA ); dump_rcpos( C, argC ); break;
		case ODT_NUMBERS: sgs_ErrWritef( C, "%d %d %d", argA, argB, argC ); break;
		case ODT_MAKECLSR: dump_rcpos( C, argA );
			sgs_ErrWritef( C, " <= " ); dump_rcpos( C, argB );
			sgs_ErrWritef( C, " [%d]", argC ); break;
		case ODT_GETCLSR: dump_rcpos( C, argA ); sgs_ErrWritef( C, " <= CL%d", argB ); break;
		case ODT_SETCLSR: sgs_ErrWritef( C, "CL%d <= ", argB ); dump_rcpos( C, argC ); break;
		case ODT_ARRAY: sgs_ErrWritef( C, "args:%d output:", argE ); dump_rcpos( C, argC ); break;
		case ODT_DICTMAP: sgs_ErrWritef( C, "output:" ); dump_rcpos( C, argC ); break;
		case ODT_CLASS:
			sgs_ErrWritef( C, "output:R%d", argA );
			sgs_ErrWritef( C, ", name:" ); dump_rcpos( C, argB );
			sgs_ErrWritef( C, ", inhname:%s", argC == argA ? "<none>" : "" );
			if( argC != argA ) dump_rcpos( C, argC );
			break;
		case ODT_RSYM:
			sgs_ErrWritef( C, "name:" ); dump_rcpos( C, argB );
			sgs_ErrWritef( C, " value:" ); dump_rcpos( C, argC ); break;
		}
		
		/* dump relevant constants */
		{
			uint8_t bits = ( sgs_OpReferConsts[ odt / 4 ] >> ( odt % 4 * 2 ) ) & 0x3;
			if( bits & 1 )
				dump_const( C, 'B', argB, consts );
			if( bits & 2 )
				dump_const( C, 'C', argC, consts );
		}
		
		sgs_ErrWritef( C, "\n" );
	}
}


/* simplifies writing code */
#define SGS_FNTCMP_ARGS SGS_CTX, sgs_CompFunc* func, sgs_FTNode* node
static void add_instr( SGS_FNTCMP_ARGS, sgs_instr_t I )
{
	sgs_LineNum ln = sgsT_LineNum( node->token );
	sgs_membuf_appbuf( &func->lnbuf, C, &ln, sizeof( ln ) );
	sgs_membuf_appbuf( &func->code, C, &I, sizeof( I ) );
}
#define INSTR_N( i, n ) add_instr( C, func, n, i )
#define INSTR( i )      INSTR_N( i, node )
#define INSTR_WRITE( op, a, b, c ) INSTR( SGS_INSTR_MAKE( op, a, b, c ) )
#define INSTR_WRITE_EX( op, ex, c ) INSTR( SGS_INSTR_MAKE_EX( op, ex, c ) )
#define INSTR_WRITE_PCH() INSTR_WRITE( 63, 0, 0, 0 )

#define QPRINT( str ) sgs_Msg( C, SGS_ERROR, "[line %d] " str, sgsT_LineNum( node->token ) )
#define QINTERR( id ) sgs_Msg( C, SGS_INTERR, "[line %d] error SGSINT%d [%s:%d,v" SGS_VERSION "]", \
	sgsT_LineNum( node->token ), id, __FILE__, __LINE__ )



static int preparse_varlists( SGS_FNTCMP_ARGS );

static int preparse_varlist( SGS_FNTCMP_ARGS )
{
	int ret = SGS_TRUE;
	node = node->child;
	while( node )
	{
		if( node->type != SGS_SFT_IDENT && node->type != SGS_SFT_KEYWORD && node->type != SGS_SFT_ARGMT )
			goto cont; /* compatibility with explists */
		if( find_varT( &C->fctx->clsr, node->token ) >= 0 )
			goto cont; /* closure */
		if( find_varT( &C->fctx->gvars, node->token ) >= 0 )
		{
			QPRINT( "Variable storage redefined: global -> local" );
			return SGS_FALSE;
		}
		add_varT( &C->fctx->vars, C, node->token );
		if( node->child )
			ret &= preparse_varlists( C, func, node );
cont:
		node = node->next;
	}
	return ret;
}

static int register_gv( SGS_FNTCMP_ARGS )
{
	if( find_varT( &C->fctx->clsr, node->token ) >= 0 )
	{
		QPRINT( "Variable storage redefined: closure -> global" );
		return SGS_FALSE;
	}
	if( find_varT( &C->fctx->vars, node->token ) >= 0 )
	{
		QPRINT( "Variable storage redefined: local -> global" );
		return SGS_FALSE;
	}
	add_varT( &C->fctx->gvars, C, node->token );
	return SGS_TRUE;
}

static int preparse_gvlist( SGS_FNTCMP_ARGS )
{
	int ret = SGS_TRUE;
	node = node->child;
	while( node )
	{
		if( !register_gv( C, func, node ) )
			return SGS_FALSE;
		if( node->child )
			ret &= preparse_varlists( C, func, node );
		node = node->next;
	}
	return ret;
}

static int preparse_varlists( SGS_FNTCMP_ARGS )
{
	int ret = 1;
	while( node )
	{
		if( node->type == SGS_SFT_VARLIST )
			ret &= preparse_varlist( C, func, node );
		else if( node->type == SGS_SFT_GVLIST )
			ret &= preparse_gvlist( C, func, node );
		else if( node->type == SGS_SFT_CLASS )
		{
			sgs_FTNode* ch = node->child;
			ret &= register_gv( C, func, ch );
			ch = ch->next;
			while( ch )
			{
				ret &= preparse_varlists( C, func, ch );
				ch = ch->next;
			}
		}
		else if( node->type == SGS_SFT_KEYWORD && node->token && sgsT_IsKeyword( node->token, "this" ) )
		{
			int pos = find_var( &C->fctx->vars, "this", 4 );
			if( pos < 0 )
			{
				sgs_FCVar nv = { SGS_STRLITBUF( "this" ), 0 };
				sgs_membuf_insbuf( &C->fctx->vars, C, 0, &nv, sizeof(nv) );
			}
			func->gotthis = SGS_TRUE;
		}
		else if( node->type == SGS_SFT_OPER )
		{
			if( SGS_ST_OP_ASSIGN( *node->token ) && node->child )
			{
				if( node->child->type == SGS_SFT_IDENT )
				{
					/* add_var calls find_var internally but - GVARS vs VARS - note the difference */
					if( find_varT( &C->fctx->gvars, node->child->token ) == -1 &&
						find_varT( &C->fctx->clsr, node->child->token ) == -1 )
						add_varT( &C->fctx->vars, C, node->child->token );
				}
				if( node->child->type == SGS_SFT_EXPLIST )
					ret &= preparse_varlist( C, func, node->child );
			}
			ret &= preparse_varlists( C, func, node->child );
		}
		else if( node->type == SGS_SFT_FORNUMI || node->type == SGS_SFT_FORNUMR )
		{
			if( find_varT( &C->fctx->gvars, node->token ) >= 0 )
			{
				QPRINT( "Variable storage redefined (numeric for index variable cannot be global): global -> local" );
				ret = SGS_FALSE;
			}
			else
			{
				add_varT( &C->fctx->vars, C, node->child->token );
				find_nth_var( &C->fctx->vars, find_varT( &C->fctx->vars, node->child->token ) )->mode = SGS_FCV_MODE_NUMFOR;
			}
			
			ret &= preparse_varlists( C, func, node->child->next );
		}
		else if( node->type == SGS_SFT_FOREACH )
		{
			if( find_varT( &C->fctx->gvars, node->token ) >= 0 )
			{
				QPRINT( "Variable storage redefined (foreach key variable cannot be global): global -> local" );
				ret = SGS_FALSE;
			}
			else
			{
				if( node->child->type != SGS_SFT_NULL )
					add_varT( &C->fctx->vars, C, node->child->token );
				if( node->child->next->type != SGS_SFT_NULL )
					add_varT( &C->fctx->vars, C, node->child->next->token );
			}

			ret &= preparse_varlists( C, func, node->child->next );
		}
		else if( node->type == SGS_SFT_FUNC )
		{
			sgs_FTNode* N = node->child->next->next->next;
			if( N && N->type == SGS_SFT_IDENT )
			{
				if( find_varT( &C->fctx->gvars, N->token ) == -1 && /* if the variable hasn't been .. */
					find_varT( &C->fctx->clsr, N->token ) == -1 ) /* .. created before */
					add_varT( C->fctx->func ? &C->fctx->vars : &C->fctx->gvars, C, N->token );
			}
		}
		else if( node->child )
			ret &= preparse_varlists( C, func, node->child );
		node = node->next;
	}
	return ret;
}

static int preparse_closures( SGS_FNTCMP_ARGS, int decl )
{
	int ret;
	node = node->child;
	while( node )
	{
		ret = add_varT( &C->fctx->clsr, C, node->token );
		if( !ret && decl )
		{
			QPRINT( "Cannot redeclare used variables with the same name" );
			return 0;
		}
		if( ret )
		{
			C->fctx->outclsr++;
			if( decl )
				C->fctx->inclsr++;
		}
		node = node->next;
	}
	return 1;
}

static int preparse_clsrlists( SGS_FNTCMP_ARGS )
{
	int ret = 1;
	while( node )
	{
		if( node->type == SGS_SFT_FUNC )
			ret &= preparse_closures( C, func, node->child->next, 0 );
		else if( node->child )
			ret &= preparse_clsrlists( C, func, node->child );
		node = node->next;
	}
	return ret;
}

static int preparse_arglist( SGS_FNTCMP_ARGS )
{
	node = node->child;
	while( node )
	{
		if( func->numargs == 255 )
		{
			QPRINT( "Argument count exceeded (max. 255 arguments)" );
			return 0;
		}
		if( !add_varT( &C->fctx->vars, C, node->token ) )
		{
			QPRINT( "Cannot redeclare arguments with the same name" );
			return 0;
		}
		func->numargs++;
		node = node->next;
	}
	return 1;
}

static int preparse_funcorder( SGS_FNTCMP_ARGS )
{
	sgs_FTNode* sub = node->child, *psub = NULL, *nsub;
	if( sub )
	{
		psub = sub;
		sub = sub->next; /* skip first item as it cannot be swapped (with itself) */
	}
	while( sub )
	{
		if( sub->type == SGS_SFT_FUNC /* function */ &&
			!sub->child->next->child /* no closures */ &&
			sub->child->next->next->next /* has a name .. */ &&
			sub->child->next->next->next->type == SGS_SFT_IDENT /* .. that is simple */ )
		{
			/* move to front */
			nsub = sub->next;
			if( psub )
				psub->next = nsub;
			sub->next = node->child;
			node->child = sub;
			sub = nsub;
		}
		else
		{
			psub = sub;
			sub = sub->next;
		}
	}
	return 1;
}


#define add_const_HDR \
	sgs_Variable* vbeg = SGS_ASSUME_ALIGNED( func->consts.ptr, sgs_Variable ); \
	sgs_Variable* vend = SGS_ASSUME_ALIGNED( func->consts.ptr + func->consts.size, sgs_Variable ); \
	sgs_Variable* var = vbeg; \
	sgs_Variable nvar;

static rcpos_t add_const_null( SGS_CTX, sgs_CompFunc* func )
{
	add_const_HDR;
	while( var < vend )
	{
		if( var->type == SGS_VT_NULL )
			return (rcpos_t) ( var - vbeg ); /* WP: const limit */
		var++;
	}

	nvar.type = SGS_VT_NULL;
	sgs_membuf_appbuf( &func->consts, C, &nvar, sizeof( nvar ) );
	return (rcpos_t) ( vend - vbeg ); /* WP: const limit */
}

static rcpos_t add_const_b( SGS_CTX, sgs_CompFunc* func, sgs_Bool bval )
{
	add_const_HDR;
	while( var < vend )
	{
		if( var->type == SGS_VT_BOOL && var->data.B == bval )
			return (rcpos_t) ( var - vbeg ); /* WP: const limit */
		var++;
	}

	nvar.type = SGS_VT_BOOL;
	nvar.data.B = bval;
	sgs_membuf_appbuf( &func->consts, C, &nvar, sizeof( nvar ) );
	return (rcpos_t) ( vend - vbeg ); /* WP: const limit */
}

static rcpos_t add_const_ip( SGS_CTX, sgs_CompFunc* func, sgs_Int ival, int ptr )
{
	add_const_HDR;
	while( var < vend )
	{
		if( var->type == SGS_VT_INT && var->data.I == ival )
			return (rcpos_t) ( var - vbeg ); /* WP: const limit */
		var++;
	}
	
	if( ptr )
	{
		nvar.type = SGS_VT_PTR;
		nvar.data.P = (void*)(intptr_t) ival;
	}
	else
	{
		nvar.type = SGS_VT_INT;
		nvar.data.I = ival;
	}
	sgs_membuf_appbuf( &func->consts, C, &nvar, sizeof( nvar ) );
	return (rcpos_t) ( vend - vbeg ); /* WP: const limit */
}
#define add_const_i( C, func, ival ) add_const_ip( C, func, ival, 0 )
#define add_const_p( C, func, ival ) add_const_ip( C, func, ival, 1 )

static rcpos_t add_const_r( SGS_CTX, sgs_CompFunc* func, sgs_Real rval )
{
	add_const_HDR;
	while( var < vend )
	{
		if( var->type == SGS_VT_REAL && var->data.R == rval )
			return (rcpos_t) ( var - vbeg ); /* WP: const limit */
		var++;
	}

	nvar.type = SGS_VT_REAL;
	nvar.data.R = rval;
	sgs_membuf_appbuf( &func->consts, C, &nvar, sizeof( nvar ) );
	return (rcpos_t) ( vend - vbeg ); /* WP: const limit */
}

static rcpos_t add_const_s( SGS_CTX, sgs_CompFunc* func, uint32_t len, const char* str )
{
	add_const_HDR;
	while( var < vend )
	{
		if( var->type == SGS_VT_STRING && var->data.S->size == len
			&& memcmp( sgs_var_cstr( var ), str, len ) == 0 )
			return (rcpos_t) ( var - vbeg ); /* WP: const limit */
		var++;
	}
	
	sgs_BreakIf( len > 0x7fffffff );

	sgsVM_VarCreateString( C, &nvar, str, (int32_t) len );
	sgs_membuf_appbuf( &func->consts, C, &nvar, sizeof( nvar ) );
	return (rcpos_t) ( vend - vbeg ); /* WP: const limit */
}

static void varinfo_add( sgs_MemBuf* out, SGS_CTX, sgs_MemBuf* vars, int base, uint32_t icount )
{
	static const uint32_t zero32 = 0;
	
	sgs_FCVar *vp,
		*vstart = (sgs_FCVar*) vars->ptr,
		*vend = (sgs_FCVar*)( vars->ptr + vars->size );
	for( vp = vstart; vp != vend; ++vp )
	{
		/* ignore _G since it's always available */
		if( vp->nmlength != 2 || vp->name[0] != '_' || vp->name[1] != 'G' )
		{
			uint8_t len = (uint8_t) vp->nmlength;
			/* encoding:
				local = 1-based positive
				global = 0
				closure = -1-based negative
			*/
			int16_t off = base ? base * ( vp - vstart + 1 ) : 0;
			
			sgs_membuf_appbuf( out, C, &zero32, sizeof(zero32) );
			sgs_membuf_appbuf( out, C, &icount, sizeof(icount) );
			sgs_membuf_appbuf( out, C, &off, sizeof(off) );
			sgs_membuf_appbuf( out, C, &len, sizeof(len) );
			sgs_membuf_appbuf( out, C, vp->name, len );
		}
	}
}

sgs_iFunc* sgsBC_ConvertFunc( SGS_CTX, sgs_FuncCtx* nfctx,
	const char* funcname, size_t fnsize, sgs_LineNum lnum )
{
	sgs_CompFunc* nf = &nfctx->cfunc;
	
	sgs_Variable strvar;
	sgs_iFunc* F = sgs_Alloc_a( sgs_iFunc, nf->consts.size + nf->code.size );

#if 0
	sgs_SetErrOutputFunc( C, sgs_StdOutputFunc, stderr );
	sgsBC_DumpOpcode( C, (sgs_instr_t*) nf->code.ptr, nf->code.size / sizeof(sgs_instr_t),
		(sgs_instr_t*) nf->code.ptr, (sgs_Variable*) nf->consts.ptr, (sgs_LineNum*) nf->lnbuf.ptr );
	printf("%d != %d\n", nf->code.size / sizeof(sgs_instr_t), nf->lnbuf.size / sizeof(sgs_LineNum) );
#endif
	sgs_BreakIf( nf->code.size / sizeof(sgs_instr_t) != nf->lnbuf.size / sizeof(sgs_LineNum) );

	F->refcount = 1;
	/* WP: const/instruction limits */
	F->size = (uint32_t) ( nf->consts.size + nf->code.size );
	F->instr_off = (uint32_t) nf->consts.size;
	F->gotthis = nf->gotthis;
	F->numargs = nf->numargs;
	F->numtmp = nf->numtmp;
	F->numclsr = nf->numclsr;
	F->inclsr = nf->inclsr;

	{
		size_t lnc = nf->lnbuf.size / sizeof( sgs_LineNum );
		F->lineinfo = sgs_Alloc_n( sgs_LineNum, lnc );
		memcpy( F->lineinfo, nf->lnbuf.ptr, nf->lnbuf.size );
	}
	/* WP: string limit */
	sgsVM_VarCreateString( C, &strvar, funcname, (sgs_SizeVal) fnsize );
	F->sfuncname = strvar.data.S;
	F->linenum = lnum;
	/* WP: string limit */
	if( C->filename )
		sgsVM_VarCreateString( C, &strvar, C->filename, (sgs_SizeVal) strlen( C->filename ) );
	else
		sgsVM_VarCreateString( C, &strvar, "", 0 );
	F->sfilename = strvar.data.S;
	
	memcpy( sgs_func_consts( F ), nf->consts.ptr, nf->consts.size );
	memcpy( sgs_func_bytecode( F ), nf->code.ptr, nf->code.size );
	
	/* transfer ownership by making it look like constants don't exist here anymore */
	nf->consts.size = 0;
	
	/* produce variable info */
	{
		uint32_t varinfosize = 0, icount = sgs_func_instr_count( F );
		sgs_MemBuf varinfo = sgs_membuf_create();
		sgs_membuf_appbuf( &varinfo, C, &varinfosize, sizeof(varinfosize) );
		varinfo_add( &varinfo, C, &nfctx->vars, 1, icount );
		varinfo_add( &varinfo, C, &nfctx->gvars, 0, icount );
		varinfo_add( &varinfo, C, &nfctx->clsr, -1, icount );
		varinfosize = varinfo.size;
		memcpy( varinfo.ptr, &varinfosize, sizeof(varinfosize) );
		F->dbg_varinfo = varinfo.ptr;
	}
	
	return F;
}

static rcpos_t add_const_f( SGS_CTX, sgs_CompFunc* func, sgs_FuncCtx* nfctx,
	const char* funcname, size_t fnsize, sgs_LineNum lnum )
{
	sgs_Variable nvar;
	rcpos_t pos;
	sgs_iFunc* F = sgsBC_ConvertFunc( C, nfctx, funcname, fnsize, lnum );
	
	/* WP: const limit */
	pos = (rcpos_t) ( func->consts.size / sizeof( nvar ) );
	nvar.type = SGS_VT_FUNC;
	nvar.data.F = F;
	sgs_membuf_appbuf( &func->consts, C, &nvar, sizeof( nvar ) );
	return pos;
}

static int op_pick_opcode( int oper, int binary )
{
	if( !binary )
	{
		if( oper == SGS_ST_OP_ADD ) return 0;
		if( oper == SGS_ST_OP_SUB )	return SGS_SI_NEGATE;
		if( oper == SGS_ST_OP_NOT ) return SGS_SI_BOOL_INV;
		if( oper == SGS_ST_OP_INV ) return SGS_SI_INVERT;
	}

	switch( oper )
	{
	case SGS_ST_OP_ADD: case SGS_ST_OP_ADDEQ: return SGS_SI_ADD;
	case SGS_ST_OP_SUB: case SGS_ST_OP_SUBEQ: return SGS_SI_SUB;
	case SGS_ST_OP_MUL: case SGS_ST_OP_MULEQ: return SGS_SI_MUL;
	case SGS_ST_OP_DIV: case SGS_ST_OP_DIVEQ: return SGS_SI_DIV;
	case SGS_ST_OP_MOD: case SGS_ST_OP_MODEQ: return SGS_SI_MOD;

	case SGS_ST_OP_AND: case SGS_ST_OP_ANDEQ: return SGS_SI_AND;
	case SGS_ST_OP_OR: case SGS_ST_OP_OREQ: return SGS_SI_OR;
	case SGS_ST_OP_XOR: case SGS_ST_OP_XOREQ: return SGS_SI_XOR;
	case SGS_ST_OP_LSH: case SGS_ST_OP_LSHEQ: return SGS_SI_LSH;
	case SGS_ST_OP_RSH: case SGS_ST_OP_RSHEQ: return SGS_SI_RSH;

	/* handled by a special case */
	case SGS_ST_OP_CAT: case SGS_ST_OP_CATEQ: return SGS_SI_NOP;

	case SGS_ST_OP_SEQ: return SGS_SI_SEQ;
	case SGS_ST_OP_SNEQ: return SGS_SI_SNEQ;
	case SGS_ST_OP_EQ: return SGS_SI_EQ;
	case SGS_ST_OP_NEQ: return SGS_SI_NEQ;
	case SGS_ST_OP_LESS: return SGS_SI_LT;
	case SGS_ST_OP_LEQ: return SGS_SI_LTE;
	case SGS_ST_OP_GRTR: return SGS_SI_GT;
	case SGS_ST_OP_GEQ: return SGS_SI_GTE;
	case SGS_ST_OP_RWCMP: return SGS_SI_RAWCMP;

	default: return 0;
	}
}


static SGSBOOL compile_node( SGS_FNTCMP_ARGS );
static SGSBOOL compile_node_r( SGS_FNTCMP_ARGS, rcpos_t* out );
static SGSBOOL compile_node_w( SGS_FNTCMP_ARGS, rcpos_t src );
static SGSBOOL compile_node_rrw( SGS_FNTCMP_ARGS, rcpos_t dst );
static SGSBOOL compile_oper( SGS_FNTCMP_ARGS, rcpos_t* arg, int out, int expect );


static void compile_defers( SGS_CTX, sgs_CompFunc* func, sgs_BlockInfo* until )
{
	size_t i, end = until ? until->defer_start : 0;
	for( i = C->fctx->num_defers; i > end; )
	{
		--i;
		compile_node( C, func, C->fctx->defers[ i ] );
	}
}

#define BLOCK_BEGIN { sgs_BlockInfo binfo; fctx_block_push( C->fctx, &binfo, NULL );
#define BLOCK_END compile_defers( C, func, &binfo ); fctx_block_pop( C->fctx, &binfo, NULL ); }
#define LOOP_BEGIN { sgs_BlockInfo binfo; sgs_LoopInfo linfo; fctx_block_push( C->fctx, &binfo, &linfo );
#define LOOP_END compile_defers( C, func, &binfo ); fctx_block_pop( C->fctx, &binfo, &linfo ); }




static rcpos_t const_maybeload( SGS_FNTCMP_ARGS, rcpos_t cid )
{
	if( cid > 65535 )
	{
		QPRINT( "Maximum number of constants exceeded" );
		C->state |= SGS_MUST_STOP;
		return 0;
	}
	if( cid < 256 )
		return SGS_CONSTENC( cid );
	else
	{
		rcpos_t out = comp_reg_alloc( C );
		INSTR_WRITE_EX( SGS_SI_LOADCONST, cid, out );
		return out;
	}
}

#define BC_CONSTENC( cid ) const_maybeload( C, func, node, cid )


static void compile_ident( SGS_FNTCMP_ARGS, rcpos_t* out )
{
	rcpos_t pos = add_const_s( C, func, node->token[ 1 ], (const char*) node->token + 2 );
	*out = BC_CONSTENC( pos );
}


static SGSBOOL compile_ident_r( SGS_FNTCMP_ARGS, rcpos_t* out )
{
	rcpos_t pos;
	if( sgsT_IsKeyword( node->token, "null" ) )
	{
		pos = add_const_null( C, func );
		*out = BC_CONSTENC( pos );
		return 1;
	}
	if( sgsT_IsKeyword( node->token, "true" ) )
	{
		pos = add_const_b( C, func, SGS_TRUE );
		*out = BC_CONSTENC( pos );
		return 1;
	}
	if( sgsT_IsKeyword( node->token, "false" ) )
	{
		pos = add_const_b( C, func, SGS_FALSE );
		*out = BC_CONSTENC( pos );
		return 1;
	}
	if( *node->token == SGS_ST_KEYWORD )
	{
		if( sgsT_IsKeyword( node->token, "this" ) )
		{
			if( func->gotthis )
			{
				*out = 0;
				return 1;
			}
			else
			{
				QINTERR( 1021 );
				return 0;
			}
		}
		QPRINT( "Cannot read from specified keyword" );
		return 0;
	}
	if( sgsT_IsIdent( node->token, "__LINE__" ) )
	{
		pos = add_const_i( C, func, sgsT_LineNum( node->token ) );
		*out = BC_CONSTENC( pos );
		return 1;
	}

	/* closures */
	if( ( pos = find_varT( &C->fctx->clsr, node->token ) ) >= 0 )
	{
		comp_reg_ensure( C, out );
		INSTR_WRITE( SGS_SI_GETCLSR, *out, pos, 0 );
		return 1;
	}

	if( C->fctx->func )
	{
		rcpos_t gpos = find_varT( &C->fctx->gvars, node->token );
		if( gpos >= 0 )
			pos = -1;
		else
		{
			pos = find_varT( &C->fctx->vars, node->token );
			if( pos < 0 )
				pos = -1; /* read from globals by default */
		}
	}
	else
	{
		pos = find_varT( &C->fctx->vars, node->token );
	}

	if( pos >= 0 )
	{
		*out = pos;
	}
	else
	{
		comp_reg_ensure( C, out );
		compile_ident( C, func, node, &pos );
		INSTR_WRITE( SGS_SI_GETVAR, *out, pos, 0 );
	}
	return 1;
}

static SGSBOOL compile_ident_w( SGS_FNTCMP_ARGS, rcpos_t src )
{
	rcpos_t pos;
	if( *node->token == SGS_ST_KEYWORD )
	{
		QPRINT( "Cannot write to reserved keywords" );
		return 0;
	}

	if( ( pos = find_varT( &C->fctx->clsr, node->token ) ) >= 0 )
	{
		INSTR_WRITE( SGS_SI_SETCLSR, 0, pos, src );
		return 1;
	}

	if( C->fctx->func )
	{
		rcpos_t gpos = find_varT( &C->fctx->gvars, node->token );
		if( gpos >= 0 )
			pos = -1;
		else
		{
			add_varT( &C->fctx->vars, C, node->token );
			pos = find_varT( &C->fctx->vars, node->token );
		}
	}
	else
	{
		pos = find_varT( &C->fctx->vars, node->token );
	}

	if( pos >= 0 )
	{
		/* optimization */
		if( pos != src )
		{
			INSTR_WRITE( SGS_SI_SET, pos, src, 0 );
		}
	}
	else
	{
		compile_ident( C, func, node, &pos );
		INSTR_WRITE( SGS_SI_SETVAR, 0, pos, src );
	}
	return 1;
}

static SGSBOOL compile_const( SGS_FNTCMP_ARGS, rcpos_t* opos )
{
	if( *node->token == SGS_ST_NUMINT )
	{
		sgs_Int val;
		SGS_AS_INTEGER( val, node->token + 1 );
		*opos = BC_CONSTENC( add_const_i( C, func, val ) );
	}
	else if( *node->token == SGS_ST_NUMREAL )
	{
		sgs_Real val;
		SGS_AS_REAL( val, node->token + 1 );
		*opos = BC_CONSTENC( add_const_r( C, func, val ) );
	}
	else if( *node->token == SGS_ST_NUMPTR )
	{
		sgs_Int val;
		SGS_AS_INTEGER( val, node->token + 1 );
		*opos = BC_CONSTENC( add_const_p( C, func, val ) );
	}
	else if( *node->token == SGS_ST_STRING )
	{
		uint32_t val;
		sgs_TokenList next = sgsT_Next( node->token );
		SGS_AS_UINT32( val, node->token + 1 );
		
		if( sgsT_IsIdent( next, "c" ) )
		{
			if( val != 1 )
			{
				QINTERR( 1013 );
				return 0;
			}
			*opos = BC_CONSTENC( add_const_i( C, func, *((const char*) node->token + 5) ) );
		}
		else
		{
			*opos = BC_CONSTENC( add_const_s( C, func, val, (const char*) node->token + 5 ) );
		}
	}
	else
	{
		QINTERR( 1011 );
		return 0;
	}
	return 1;
}


static SGSBOOL compile_regcopy( SGS_FNTCMP_ARGS, rcpos_t srcpos, rcpos_t dstpos )
{
	INSTR_WRITE( SGS_SI_SET, dstpos, srcpos, 0 );
	return 1;
}

static SGSBOOL compile_fcall( SGS_FNTCMP_ARGS, rcpos_t* out, int expect )
{
	/* IF (ternary-like) */
	if( sgsT_IsKeyword( node->child->token, "if" ) )
	{
		sgs_FTNode* n = node->child->next->child;
		int argc = 0;
		size_t csz1, csz2, csz3;
		rcpos_t exprpos = SGS_RCPOS_UNSPEC, srcpos = SGS_RCPOS_UNSPEC, retpos = SGS_RCPOS_UNSPEC;
		if( expect > 1 )
		{
			QPRINT( "'if' pseudo-function cannot be used as input for expression writes with multiple outputs" );
			return 0;
		}
		while( n )
		{
			argc++;
			n = n->next;
		}
		if( argc != 3 )
		{
			QPRINT( "'if' pseudo-function requires exactly 3 arguments" );
			return 0;
		}
		
		if( expect )
			retpos = comp_reg_alloc( C );
		
		n = node->child->next->child;
		if( !compile_node_r( C, func, n, &exprpos ) ) return 0;
		
		n = n->next;
		INSTR_WRITE_PCH();
		csz1 = func->code.size;
		if( !compile_node_r( C, func, n, &srcpos ) ||
			!compile_regcopy( C, func, n, srcpos, retpos ) ) return 0;
		
		n = n->next;
		INSTR_WRITE_PCH();
		csz2 = func->code.size;
		if( !compile_node_r( C, func, n, &srcpos ) ||
			!compile_regcopy( C, func, n, srcpos, retpos ) ) return 0;
		
		csz3 = func->code.size;
		
		{
			uint32_t instr1, instr2;
			instr1 = SGS_INSTR_MAKE_EX( SGS_SI_JMPF, ( csz2 - csz1 ) / SGS_INSTR_SIZE, exprpos );
			instr2 = SGS_INSTR_MAKE_EX( SGS_SI_JUMP, ( csz3 - csz2 ) / SGS_INSTR_SIZE, 0 );
			memcpy( func->code.ptr + csz1 - 4, &instr1, sizeof(instr1) );
			memcpy( func->code.ptr + csz2 - 4, &instr2, sizeof(instr2) );
		}
		
		*out = retpos;
		return 1;
	}
	/* SYNC/RACE (wait until all threads have finished/at least one of threads has finished) */
	else if( sgsT_IsKeyword( node->child->token, "sync" ) || sgsT_IsKeyword( node->child->token, "race" ) )
	{
		int i, argc = 0;
		size_t csz1;
		rcpos_t boolpos, srcpos;
		sgs_FTNode* n = node->child->next->child;
		int israce = sgsT_IsKeyword( node->child->token, "race" );
		
		if( expect > 1 )
		{
			QPRINT( "'sync' pseudo-function cannot be used as input for expression writes with multiple outputs" );
			return 0;
		}
		while( n )
		{
			argc++;
			n = n->next;
		}
		if( argc < 1 )
		{
			QPRINT( "'sync' pseudo-function requires [1;255] arguments" );
			return 0;
		}
		
		C->fctx->syncdepth++;
		if( C->fctx->syncdepth == 1 )
		{
			INSTR_WRITE( SGS_SI_INT, 0, 0, SGS_INT_RESET_WAIT_TIMER );
		}
		
		csz1 = func->code.size; /* the jump-back spot */
		
		boolpos = comp_reg_alloc( C );
		/* initialize bool to 0 if race and 1 if sync */
		INSTR_WRITE( SGS_SI_SET, boolpos, BC_CONSTENC( add_const_b( C, func, !israce ) ), 0 );
		
		i = 0;
		n = node->child->next->child;
		while( n )
		{
			srcpos = SGS_RCPOS_UNSPEC;
			if( !compile_node_r( C, func, n, &srcpos ) )
			{
				C->fctx->syncdepth--;
				return 0;
			}
			/* set bool to 1 if finished in race or 0 if not finished in sync */
			INSTR_WRITE( israce ? SGS_SI_COTRT : SGS_SI_COTRF, boolpos, srcpos, 0 );
			i++;
			n = n->next;
		}
		
		if( israce )
		{
			INSTR_WRITE_EX( SGS_SI_COABORT, ( csz1 - func->code.size ) / SGS_INSTR_SIZE - 1, boolpos );
		}
		
		if( C->fctx->syncdepth == 1 )
		{
			INSTR_WRITE_EX( SGS_SI_YLDJMP, ( csz1 - func->code.size ) / SGS_INSTR_SIZE - 1, boolpos );
		}
		C->fctx->syncdepth--;
		
		*out = boolpos;
		comp_reg_unwind( C, boolpos + expect );
		return 1;
	}
	else
	{
		sgs_FTNode* n;
		int i = 0, gotthis = 0, regc = 0, pfxfuncmode = 0, isthreadfunc;
		rcpos_t argpos, funcpos, fnargpos, objpos, argend;
		
		if( node->type == SGS_SFT_THRCALL ) pfxfuncmode = 1;
		if( node->type == SGS_SFT_STHCALL ) pfxfuncmode = 2;
		if( node->type == SGS_SFT_NEWCALL ) pfxfuncmode = 3;
		isthreadfunc = pfxfuncmode == 1 || pfxfuncmode == 2;
		
		/* count the required number of registers */
		if( isthreadfunc || ( node->child->type == SGS_SFT_OPER && /* thread always has 'this' */
			( *node->child->token == SGS_ST_OP_MMBR || *node->child->token == SGS_ST_OP_NOT ) ) )
		{
			gotthis = 1;
			regc++;
		}
		n = node->child->next->child;
		while( n )
		{
			regc++;
			n = n->next;
		}
		regc++; /* function register */
		if( isthreadfunc )
			regc++; /* 'thread_create'/'subthread_create' function register */
		argpos = comp_reg_alloc_n( C, regc > expect ? regc : expect );
		funcpos = argpos;
		if( isthreadfunc )
		{
			const char* key = pfxfuncmode == 1 ? "thread_create" : "subthread_create";
			rcpos_t strpos = add_const_s( C, func, (uint32_t) strlen( key ), key );
			INSTR_WRITE( SGS_SI_GETVAR, argpos, BC_CONSTENC( strpos ), 0 );
			gotthis = 0;
			funcpos++;
		}
		
		argend = argpos + regc - 1;
		objpos = funcpos + 1;
		fnargpos = funcpos + ( gotthis || isthreadfunc ? 2 : 1 );
		
		/* return register positions for expected data */
		*out = argpos;
		
		/* load function (for properties, object too) */
		if( node->child->type == SGS_SFT_OPER &&
			( *node->child->token == SGS_ST_OP_MMBR || *node->child->token == SGS_ST_OP_NOT ) )
		{
			sgs_FTNode* ncc = node->child->child;
			rcpos_t proppos = SGS_RCPOS_UNSPEC;
			
			if( !compile_node_rrw( C, func, ncc, objpos ) ) return 0; /* read object */
			if( *node->child->token == SGS_ST_OP_MMBR )
			{
				if( ncc->next->type == SGS_SFT_IDENT )
				{
					/* make string property key constant */
					compile_ident( C, func, ncc->next, &proppos );
				}
				else
				{
					/* load property key */
					if( !compile_node_r( C, func, ncc->next, &proppos ) ) return 0;
				}
				/* function as property of object */
				INSTR_WRITE( SGS_SI_GETPROP, funcpos, objpos, proppos );
			}
			else
			{
				/* function from own variable */
				if( !compile_node_rrw( C, func, ncc->next, funcpos ) ) return 0;
			}
		}
		else
		{
			if( objpos != fnargpos )
			{
				/* object does not apply, insert null at position */
				rcpos_t nullpos = add_const_null( C, func );
				INSTR_WRITE_EX( SGS_SI_LOADCONST, nullpos, objpos );
			}
			/* function from own variable */
			if( !compile_node_rrw( C, func, node->child, funcpos ) ) return 0;
		}
		
		/* load arguments */
		i = 0;
		{
			/* passing objects where the call is formed appropriately */
			n = node->child->next->child;
			while( n )
			{
				if( !compile_node_rrw( C, func, n, fnargpos + i ) ) return 0;
				i++;
				n = n->next;
			}
		}
		
		/* compile call */
		if( pfxfuncmode == 3 )
		{
			/* new <class/ctor>(..) */
			INSTR_WRITE( SGS_SI_NEW, 0, argpos, argend );
		}
		else
		{
			INSTR_WRITE( SGS_SI_CALL, expect, gotthis ? argpos | 0x100 : argpos, argend );
		}
		
		comp_reg_unwind( C, argpos + expect );

		return 1;
	}
}

static SGSBOOL compile_index_r( SGS_FNTCMP_ARGS, rcpos_t* out )
{
	comp_reg_ensure( C, out );
	{
		rcpos_t var = SGS_RCPOS_UNSPEC, name = SGS_RCPOS_UNSPEC;
		rcpos_t regpos = C->fctx->regs;
		
		if( !compile_node_r( C, func, node->child, &var ) ||
			!compile_node_r( C, func, node->child->next, &name ) )
			return 0;
		
		INSTR_WRITE( SGS_SI_GETINDEX, *out, var, name );
		comp_reg_unwind( C, regpos );
	}
	return 1;
}

static SGSBOOL compile_index_w( SGS_FNTCMP_ARGS, rcpos_t src )
{
	rcpos_t var = SGS_RCPOS_UNSPEC, name = SGS_RCPOS_UNSPEC;
	rcpos_t regpos = C->fctx->regs;
	
	if( !compile_node_r( C, func, node->child, &var ) ||
		!compile_node_r( C, func, node->child->next, &name ) )
		return 0;
	
	if( SGS_CONSTVAR( var ) )
	{
		QPRINT( "Cannot set indexed value of a constant" );
		return 0;
	}
	
	INSTR_WRITE( SGS_SI_SETINDEX, var, name, src );
	comp_reg_unwind( C, regpos );
	return 1;
}

static SGSBOOL compile_clspfx_w( SGS_FNTCMP_ARGS, rcpos_t src )
{
	rcpos_t var = SGS_RCPOS_UNSPEC, name;
	rcpos_t regpos = C->fctx->regs;
	
	if( !compile_node_r( C, func, node, &var ) ) return 0;
	
	compile_ident( C, func, node->child, &name );
	
	INSTR_WRITE( SGS_SI_SETINDEX, var, name, src );
	comp_reg_unwind( C, regpos );
	return 1;
}

static SGSBOOL compile_midxset( SGS_FNTCMP_ARGS, rcpos_t* out, int isprop )
{
	sgs_FTNode* mapi;
	
	if( !compile_node_r( C, func, node->child, out ) ) return 0;
	{
		rcpos_t regpos = C->fctx->regs;
		mapi = node->child->next->child;
		while( mapi )
		{
			rcpos_t name = SGS_RCPOS_UNSPEC, src = SGS_RCPOS_UNSPEC;
			if( *mapi->token == SGS_ST_STRING )
			{
				compile_const( C, func, mapi, &name );
			}
			else if( *mapi->token == SGS_ST_IDENT )
			{
				compile_ident( C, func, mapi, &name );
			}
			else
			{
				if( !compile_node_r( C, func, mapi, &name ) ) return 0;
			}
			mapi = mapi->next;
			
			if( !compile_node_r( C, func, mapi, &src ) ) return 0;
			mapi = mapi->next;
			
			INSTR_WRITE( isprop ? SGS_SI_SETPROP : SGS_SI_SETINDEX, *out, name, src );
			comp_reg_unwind( C, regpos );
		}
	}
	return 1;
}


static SGSBOOL compile_node_rrw( SGS_FNTCMP_ARGS, rcpos_t dst )
{
	sgs_rcpos_t ireg = dst, bkup = C->fctx->regs;
	
	if( !compile_node_r( C, func, node, &ireg ) ) return 0;
	
	if( ireg != dst )
	{
		INSTR_WRITE( SGS_SI_SET, dst, ireg, 0 );
	}
	
	comp_reg_unwind( C, bkup );
	
	return 1;
}

static SGSBOOL compile_node_rw( SGS_FNTCMP_ARGS, sgs_FTNode* outnode )
{
	int ret;
	sgs_rcpos_t ireg, dstreg = SGS_RCPOS_UNSPEC, bkup = C->fctx->regs;
	
	if( outnode->type == SGS_SFT_IDENT ||
		outnode->type == SGS_SFT_KEYWORD )
	{
		dstreg = find_varT( &C->fctx->vars, outnode->token );
		if( dstreg < 0 )
			dstreg = SGS_RCPOS_UNSPEC;
	}
	ireg = dstreg;
	
	ret = compile_node_r( C, func, node, &ireg ) &&
		compile_node_w( C, func, outnode, ireg );
	
	comp_reg_unwind( C, bkup );
	
	return ret;
}

static int compile_mconcat( SGS_FNTCMP_ARGS, rcpos_t* arg )
{
	int numch = 0;
	sgs_FTNode* cur = node->child;
	rcpos_t oreg = comp_reg_alloc( C );
	if( C->state & SGS_MUST_STOP )
		return 0;
	
	/* get source data registers */
	while( cur )
	{
		rcpos_t ireg = SGS_RCPOS_UNSPEC, bkup = C->fctx->regs;
		
		if( !compile_node_r( C, func, cur, &ireg ) )
			return 0;
		INSTR_WRITE( SGS_SI_PUSH, 0, ireg, 0 );
		numch++;
		cur = cur->next;
		
		comp_reg_unwind( C, bkup );
	}
	
	INSTR_WRITE( SGS_SI_MCONCAT, oreg, numch, 0 );
	
	*arg = oreg;
	return 1;
}

static SGSBOOL compile_oper( SGS_FNTCMP_ARGS, rcpos_t* arg, int out, int expect )
{
	int assign = SGS_ST_OP_ASSIGN( *node->token );
	
	/* Error suppression op */
	if( *node->token == SGS_ST_OP_ERSUP )
	{
		size_t csz;
		INSTR_WRITE( SGS_SI_INT, 0, 0, SGS_INT_ERRSUP_INC );
		csz = func->code.size;
		
		if( out && expect )
		{
			if( !compile_node_r( C, func, node->child, arg ) ) return 0;
		}
		else
		{
			if( !compile_node( C, func, node->child ) ) return 0;
		}
		
		if( func->code.size > csz )
		{
			/* write INT ERRSUP_DEC if any code was written */
			INSTR_WRITE( SGS_SI_INT, 0, 0, SGS_INT_ERRSUP_DEC );
		}
		else
		{
			/* otherwise, remove the already written INT ERRSUP_INC */
			func->code.size -= 4;
			func->lnbuf.size -= 2;
		}
		return 1;
	}
	
	/* Boolean ops */
	if( SGS_ST_OP_BOOL( *node->token ) || SGS_ST_OP_FNN( *node->token ) )
	{
		int jin;
		rcpos_t ireg1 = SGS_RCPOS_UNSPEC, ireg2 = SGS_RCPOS_UNSPEC, oreg = 0;
		size_t csz, csz2;
		
		if( !assign )
			oreg = comp_reg_alloc( C );
		if( C->state & SGS_MUST_STOP )
			goto fail;
		
		/* get source data register */
		if( !compile_node_r( C, func, node->child, &ireg1 ) ) goto fail;
		
		/* write cond. jump */
		jin = ( *node->token == SGS_ST_OP_BLAND || *node->token == SGS_ST_OP_BLAEQ ) ? SGS_SI_JMPT :
			( ( *node->token == SGS_ST_OP_BLOR || *node->token == SGS_ST_OP_BLOEQ ) ? SGS_SI_JMPF : SGS_SI_JMPN );
		INSTR_WRITE_PCH();
		csz = func->code.size;
		
		/* compile write of value 1 */
		if( assign )
		{
			if( !compile_node_w( C, func, node->child, ireg1 ) ) goto fail;
		}
		else
		{
			INSTR_WRITE( SGS_SI_SET, oreg, ireg1, 0 );
		}
		
		INSTR_WRITE_PCH();
		csz2 = func->code.size;
		
		/* fix-up jump 1 */
		{
			sgs_instr_t instr;
			/* WP: instruction limit */
			ptrdiff_t jmp_off = (ptrdiff_t) ( func->code.size - csz ) / SGS_INSTR_SIZE;
			instr = SGS_INSTR_MAKE_EX( jin, jmp_off, ireg1 );
			memcpy( func->code.ptr + csz - 4, &instr, sizeof(instr) );
		}
		
		/* get source data register 2 */
		if( !compile_node_r( C, func, node->child->next, &ireg2 ) ) goto fail;
		
		/* compile write of value 2 */
		if( assign )
		{
			if( !compile_node_w( C, func, node->child, ireg2 ) ) goto fail;
		}
		else
		{
			INSTR_WRITE( SGS_SI_SET, oreg, ireg2, 0 );
		}
		
		/* fix-up jump 2 */
		{
			sgs_instr_t instr;
			/* WP: instruction limit */
			ptrdiff_t jmp_off = (ptrdiff_t) ( func->code.size - csz2 ) / SGS_INSTR_SIZE;
			instr = SGS_INSTR_MAKE_EX( SGS_SI_JUMP, jmp_off, 0 );
			memcpy( func->code.ptr + csz2 - 4, &instr, sizeof(instr) );
		}
		
		/* re-read from assignments */
		if( assign )
		{
			if( !compile_node_r( C, func, node->child, arg ) ) goto fail;
		}
		else
			*arg = oreg;
	}
	else
	/* Increment / decrement */
	if( *node->token == SGS_ST_OP_INC || *node->token == SGS_ST_OP_DEC )
	{
		rcpos_t ireg = SGS_RCPOS_UNSPEC, oreg;
		
		/* register with input data */
		if( !compile_node_r( C, func, node->child, &ireg ) ) goto fail;
		
		/* output register selection */
		oreg = expect && node->type == SGS_SFT_OPER_P ? comp_reg_alloc( C ) : ireg;
		if( C->state & SGS_MUST_STOP )
			goto fail;
		if( oreg != ireg )
		{
			INSTR_WRITE( SGS_SI_SET, oreg, ireg, 0 );
		}
		
		/* write bytecode */
		INSTR_WRITE( *node->token == SGS_ST_OP_INC ? SGS_SI_INC : SGS_SI_DEC, ireg, ireg, 0 );
		
		*arg = oreg;
		
		/* compile writeback */
		if( !compile_node_w( C, func, node->child, ireg ) ) goto fail;
	}
	/* Assignment */
	else if( assign )
	{
		/* 1 operand */
		if( *node->token == SGS_ST_OP_SET )
		{
			if( node->child->type == SGS_SFT_EXPLIST )
			{
				sgs_FTNode* n;
				int i, xpct = 0;
				int32_t bkup;
				rcpos_t freg;
				if( node->child->next->type != SGS_SFT_FCALL )
				{
					QPRINT( "Expression writes only allowed with function call reads" );
					goto fail;
				}
				
				/* multiwrite */
				n = node->child->child;
				while( n )
				{
					xpct++;
					n = n->next;
				}
				
				if( !compile_fcall( C, func, node->child->next, &freg, xpct ) ) goto fail;
				
				bkup = C->fctx->regs;
				n = node->child->child;
				for( i = 0; i < xpct; ++i )
				{
					if( !compile_node_w( C, func, n, freg + i ) ) goto fail;
					
					comp_reg_unwind( C, bkup );
					
					if( i == 0 && expect )
					{
						comp_reg_ensure( C, arg );
						INSTR_WRITE( SGS_SI_SET, *arg, freg, 0 );
					}
					
					n = n->next;
				}
			}
			else
			{
				if( !compile_node_rw( C, func, node->child->next, node->child ) ) goto fail;
				if( expect && !compile_node_r( C, func, node->child, arg ) ) goto fail;
			}
		}
		/* 2+ operands (MCONCAT only) */
		else if( *node->token == SGS_ST_OP_CATEQ && node->child &&
			node->child->next )
		{
			rcpos_t oreg;
			if( !compile_mconcat( C, func, node, &oreg ) )
				goto fail;
			*arg = oreg;
			
			/* compile write */
			if( !compile_node_w( C, func, node->child, oreg ) ) goto fail;
		}
		/* 2 operands */
		else
		{
			int op;
			rcpos_t ireg1 = SGS_RCPOS_UNSPEC, ireg2 = SGS_RCPOS_UNSPEC, oreg = comp_reg_alloc( C );
			if( C->state & SGS_MUST_STOP )
				goto fail;
			
			if( !node->child || !node->child->next )
			{
				QINTERR( 1012 );
				goto fail;
			}
			
			/* get source data registers */
			if( !compile_node_r( C, func, node->child, &ireg1 ) ||
				!compile_node_r( C, func, node->child->next, &ireg2 ) ) goto fail;
			
			/* compile op */
			op = op_pick_opcode( *node->token, 1 );
			INSTR_WRITE( op, oreg, ireg1, ireg2 );
			
			if( !compile_node_w( C, func, node->child, oreg ) ) goto fail;
			if( !compile_node_r( C, func, node->child, arg ) ) goto fail;
		}
	}
	/* Any other */
	else
	{
		rcpos_t ireg1 = SGS_RCPOS_UNSPEC, ireg2 = SGS_RCPOS_UNSPEC, oreg;
		
		if( /* no operands, unary used as binary, binary used as unary */
			( !node->child ) ||
			( SGS_ST_OP_UNARY( *node->token ) && !SGS_ST_OP_BINARY( *node->token ) && node->child->next ) ||
			( !SGS_ST_OP_UNARY( *node->token ) && SGS_ST_OP_BINARY( *node->token ) && !node->child->next )
		)
		{
			QPRINT( "Invalid expression" );
			goto fail;
		}
		
		if( *node->token == SGS_ST_OP_MMBR )
		{
			/* oreg points to output register if "out", source register otherwise */
			if( out )
			{
				oreg = comp_reg_alloc( C );
				if( C->state & SGS_MUST_STOP )
					goto fail;
				*arg = oreg;
			}
			else
				oreg = *arg;
			
			/* get source data registers */
			if( !compile_node_r( C, func, node->child, &ireg1 ) ) goto fail;
			
			if( node->child->next->type == SGS_SFT_IDENT )
				compile_ident( C, func, node->child->next, &ireg2 );
			else
			{
				if( !compile_node_r( C, func, node->child->next, &ireg2 ) ) goto fail;
			}
			
			/* compile op */
			if( out )
				INSTR_WRITE( SGS_SI_GETPROP, oreg, ireg1, ireg2 );
			else
			{
				if( SGS_CONSTVAR( ireg1 ) )
				{
					QPRINT( "Cannot set property of a constant" );
					goto fail;
				}
				INSTR_WRITE( SGS_SI_SETPROP, ireg1, ireg2, oreg );
			}
		}
		/* 2+ operands (MCONCAT only) */
		else if( *node->token == SGS_ST_OP_CAT && node->child &&
			node->child->next )
		{
			if( !compile_mconcat( C, func, node, arg ) )
				goto fail;
		}
		else
		{
			int op;
			
			oreg = comp_reg_alloc( C );
			if( C->state & SGS_MUST_STOP )
				goto fail;
			
			/* get source data registers */
			if( !compile_node_r( C, func, node->child, &ireg1 ) ) goto fail;
			if( node->child->next )
			{
				if( !compile_node_r( C, func, node->child->next, &ireg2 ) ) goto fail;
			}
			
			*arg = oreg;
			
			/* compile op */
			op = op_pick_opcode( *node->token, !!node->child->next );
			if( !op )
				INSTR_WRITE( SGS_SI_SET, oreg, ireg1, 0 );
			else if( node->child->next )
				INSTR_WRITE( op, oreg, ireg1, ireg2 );
			else
				INSTR_WRITE( op, oreg, ireg1, 0 );
		}
	}
	
	return 1;
	
fail:
	C->state |= SGS_HAS_ERRORS;
	return 0;
}


static SGSBOOL compile_breaks( SGS_FNTCMP_ARGS, uint8_t iscont )
{
	sgs_BreakInfo* binfo = C->fctx->binfo, *prev = NULL;
	while( binfo )
	{
		if( binfo->numlp == C->fctx->loops && binfo->iscont == iscont )
		{
			/* WP: jump limit */
			ptrdiff_t off = (ptrdiff_t) ( func->code.size - binfo->jdoff ) / SGS_INSTR_SIZE - 1;
			if( over_limit( off, 32767 ) )
			{
				QPRINT( "Max. jump limit exceeded @ break/continue; reduce size of loops" );
				return 0;
			}
			sgs_instr_t instr = SGS_INSTR_MAKE_EX( SGS_SI_JUMP, off, 0 );
			memcpy( func->code.ptr + binfo->jdoff, &instr, sizeof(instr) );
			binfo = binfo->next;
			fctx_binfo_rem( C, C->fctx, prev );
		}
		else
		{
			prev = binfo;
			binfo = binfo->next;
		}
	}
	return 1;
}




static void rpts( sgs_MemBuf* out, SGS_CTX, sgs_FTNode* root )
{
	switch( root->type )
	{
	case SGS_SFT_IDENT:
		sgs_membuf_appbuf( out, C, root->token + 2, root->token[1] );
		break;
	case SGS_SFT_CLSPFX:
		sgs_membuf_appbuf( out, C, root->token + 2, root->token[1] );
		sgs_membuf_appchr( out, C, '.' );
		rpts( out, C, root->child );
		break;
	case SGS_SFT_OPER:
		switch( *root->token )
		{
		case SGS_ST_OP_MMBR:
			rpts( out, C, root->child );
			sgs_membuf_appchr( out, C, '.' );
			rpts( out, C, root->child->next );
			break;
		}
		break;
	}
}


static void prefix_bytecode( SGS_CTX, sgs_CompFunc* func, int args )
{
	sgs_MemBuf ncode = sgs_membuf_create();
	sgs_MemBuf nlnbuf = sgs_membuf_create();
	
	if( C->fctx->outclsr > C->fctx->inclsr )
	{
		int i;
		sgs_instr_t I;
		uint16_t ln = 0;

		for( i = 0; i < args; ++i )
		{
			sgs_FCVar* result;
			int which;
			result = find_nth_var( &C->fctx->vars, i );
			sgs_BreakIf( !result );
			if( !result )
				continue;
			which = find_var( &C->fctx->clsr, result->name, result->nmlength );
			if( which < 0 )
				continue;
			I = SGS_INSTR_MAKE( SGS_SI_SETCLSR, 0, which, i );
			sgs_membuf_appbuf( &ncode, C, &I, sizeof( I ) );
			sgs_membuf_appbuf( &nlnbuf, C, &ln, sizeof( ln ) );
		}
	}

	sgs_membuf_appbuf( &ncode, C, func->code.ptr, func->code.size );
	sgs_membuf_appbuf( &nlnbuf, C, func->lnbuf.ptr, func->lnbuf.size );
	
	/* append one extra return instruction to avoid VM bounds check */
	{
		sgs_instr_t I = SGS_INSTR_MAKE( SGS_SI_RETN, 0, 0, 0 );
		uint16_t ln = 0;
		sgs_membuf_appbuf( &ncode, C, &I, sizeof( I ) );
		sgs_membuf_appbuf( &nlnbuf, C, &ln, sizeof( ln ) );
	}
	
	sgs_membuf_destroy( &func->code, C );
	sgs_membuf_destroy( &func->lnbuf, C );
	
	/* WP: both lastreg and args cannot exceed 255, lastreg includes args */
	func->numtmp = (uint8_t) ( C->fctx->lastreg - args );
	func->code = ncode;
	func->lnbuf = nlnbuf;
}


static int compile_fn_base( SGS_FNTCMP_ARGS, int args )
{
	BLOCK_BEGIN;
	
	if( !preparse_clsrlists( C, func, node ) ) return 0;
	if( !preparse_varlists( C, func, node ) ) return 0;
	expand_varlist( &C->fctx->vars, C );
	comp_reg_alloc_n( C, C->fctx->vars.size / sizeof( sgs_FCVar ) );
	args += func->gotthis;
	
	if( !preparse_funcorder( C, func, node ) ) return 0;
	if( !compile_node( C, func, node ) ) return 0;
	
	comp_reg_unwind( C, 0 );
	
	if( C->fctx->lastreg > 0xff )
	{
		QPRINT( "Max. register count exceeded" );
		return 0;
	}
	if( C->fctx->outclsr > 0xff )
	{
		QPRINT( "Max. closure count exceeded" );
		return 0;
	}
	
	BLOCK_END;
	
	prefix_bytecode( C, func, args );
	/* WP: closure limit */
	func->numclsr = (uint8_t) C->fctx->outclsr;
	func->inclsr = (uint8_t) C->fctx->inclsr;
	
#if SGS_DUMP_BYTECODE || ( SGS_DEBUG && SGS_DEBUG_DATA )
	{
		sgs_OutputFunc oldoutf;
		void* oldoutc;
		sgs_GetErrOutputFunc( C, &oldoutf, &oldoutc );
		sgs_SetErrOutputFunc( C, sgs_StdOutputFunc, stderr );
		
		fctx_dump( C, C->fctx );
		sgs_ErrWritef( C, "function (this=%s args=%d tmp=%d clsr=%d inclsr=%d)\n",
			func->gotthis ? "Y" : "n", func->numargs, func->numtmp, func->numclsr, func->inclsr );
		sgsBC_DumpEx( C, func->consts.ptr, func->consts.size, func->code.ptr, func->code.size,
			(sgs_LineNum*) func->lnbuf.ptr );
		
		sgs_SetErrOutputFunc( C, oldoutf, oldoutc );
	}
#endif
	
	return 1;
}

static SGSBOOL compile_func( SGS_FNTCMP_ARGS, rcpos_t* out )
{
	sgs_FuncCtx* fctx = fctx_create( C ), *bkfctx = C->fctx;
	sgs_CompFunc* nf = &fctx->cfunc;

	sgs_FTNode* n_arglist = node->child;
	sgs_FTNode* n_uselist = n_arglist->next;
	sgs_FTNode* n_body = n_uselist->next;
	sgs_FTNode* n_name = n_body->next;

	/* pre-context-change closure-apply */
	if( !preparse_closures( C, func, n_uselist, 0 ) ) goto fail;

	C->fctx = fctx;

	if( !preparse_closures( C, nf, n_uselist, 1 ) ||
		!preparse_arglist( C, nf, n_arglist ) ||
		!compile_fn_base( C, nf, n_body, nf->numargs ) ) goto fail;
	
	C->fctx = bkfctx;

	{
		sgs_MemBuf ffn = sgs_membuf_create();
		if( n_name )
			rpts( &ffn, C, n_name );
		*out = BC_CONSTENC( add_const_f( C, func, fctx, ffn.ptr, ffn.size, sgsT_LineNum( node->token ) ) );
		sgs_membuf_destroy( &ffn, C );
		
		if( fctx->inclsr > 0 )
		{
			int i;
			rcpos_t ro = comp_reg_alloc( C );
			INSTR_WRITE( SGS_SI_MAKECLSR, ro, *out, fctx->inclsr );
			if( fctx->inclsr )
			{
				sgs_FTNode* uli = n_uselist->child;
				for( i = 0; i < fctx->inclsr; i += 3 )
				{
					int a, b = 0, c = 0;
					a = find_varT( &bkfctx->clsr, uli->token );
					uli = uli->next;
					if( uli )
					{
						b = find_varT( &bkfctx->clsr, uli->token );
						uli = uli->next;
						if( uli )
						{
							c = find_varT( &bkfctx->clsr, uli->token );
							uli = uli->next;
						}
					}
					INSTR_WRITE( SGS_SI_CLSRINFO, a, b, c );
				}
			}
			*out = ro;
		}
	}
	fctx_destroy( C, fctx );
	return 1;

fail:
	C->fctx = bkfctx;
	fctx_destroy( C, fctx );
	C->state |= SGS_HAS_ERRORS;
	return 0;
}


static SGSBOOL compile_node_w( SGS_FNTCMP_ARGS, rcpos_t src )
{
	rcpos_t bkup = C->fctx->regs;
	
	switch( node->type )
	{
	case SGS_SFT_IDENT:
	case SGS_SFT_KEYWORD:
		if( !compile_ident_w( C, func, node, src ) ) goto fail;
		break;
		
	case SGS_SFT_CONST:
	case SGS_SFT_FUNC:
	case SGS_SFT_ARRLIST:
	case SGS_SFT_DCTLIST:
	case SGS_SFT_MAPLIST:
		QPRINT( "Cannot write to constants" );
		goto fail;
		
	case SGS_SFT_OPER:
	case SGS_SFT_OPER_P:
		if( !compile_oper( C, func, node, &src, 0, 1 ) ) goto fail;
		break;
		
	case SGS_SFT_FCALL:
	case SGS_SFT_THRCALL:
	case SGS_SFT_STHCALL:
	case SGS_SFT_NEWCALL:
		{
			rcpos_t dummy = SGS_RCPOS_UNSPEC;
			if( !compile_fcall( C, func, node, &dummy, 0 ) ) goto fail;
		}
		break;
		
	case SGS_SFT_INDEX:
		if( !compile_index_w( C, func, node, src ) ) goto fail;
		break;
		
	case SGS_SFT_MIDXSET:
		QPRINT( "Cannot write to multi-index-set expression" );
		break;
		
	case SGS_SFT_MPROPSET:
		QPRINT( "Cannot write to multi-property-set expression" );
		break;
		
	case SGS_SFT_EXPLIST:
		QPRINT( "Expression writes only allowed with function call reads" );
		goto fail;
		
	case SGS_SFT_CLSPFX:
		if( !compile_clspfx_w( C, func, node, src ) ) goto fail;
		break;
		
	default:
		QINTERR( 1003 );
		goto fail;
	}
	
	comp_reg_unwind( C, bkup );
	return 1;
	
fail:
	C->state |= SGS_HAS_ERRORS;
	return 0;
}
static SGSBOOL compile_node_r( SGS_FNTCMP_ARGS, rcpos_t* out )
{
	sgs_BreakIf( out == NULL );
	switch( node->type )
	{
	case SGS_SFT_IDENT:
	case SGS_SFT_KEYWORD:
	case SGS_SFT_CLSPFX:
		if( !compile_ident_r( C, func, node, out ) ) goto fail;
		break;
		
	case SGS_SFT_CONST:
		if( !compile_const( C, func, node, out ) ) goto fail;
		break;
		
	case SGS_SFT_FUNC:
		if( !compile_func( C, func, node, out ) ) goto fail;
		break;
		
	case SGS_SFT_ARRLIST:
		{
			int args = 0, off = 0;
			sgs_FTNode* n = node->child;
			while( n )
			{
				args++;
				n = n->next;
			}
			comp_reg_ensure( C, out );
			INSTR_WRITE_EX( SGS_SI_ARRAY, args, *out );
			n = node->child;
			while( n )
			{
				rcpos_t bkup = C->fctx->regs, vpos = SGS_RCPOS_UNSPEC;
				if( !compile_node_r( C, func, n, &vpos ) )
					goto fail;
				INSTR_WRITE( SGS_SI_ARRPUSH, *out, 0, vpos );
				comp_reg_unwind( C, bkup );
				off++;
				n = n->next;
			}
		}
		break;
	case SGS_SFT_DCTLIST:
	case SGS_SFT_MAPLIST:
		{
			int args = 0;
			sgs_FTNode* n = node->child;
			rcpos_t kpos, vpos, bkup;
			comp_reg_ensure( C, out );
			bkup = C->fctx->regs;
			INSTR_WRITE_EX( node->type == SGS_SFT_DCTLIST ? SGS_SI_DICT : SGS_SI_MAP, 0, *out );
			while( n )
			{
				if( args % 2 == 0 )
				{
					kpos = SGS_RCPOS_UNSPEC;
					if( n->type != SGS_SFT_ARGMT )
					{
						if( !compile_node_r( C, func, n, &kpos ) )
							goto fail;
					}
					else if( *n->token == SGS_ST_STRING )
						compile_const( C, func, n, &kpos );
					else
						compile_ident( C, func, n, &kpos );
				}
				else
				{
					vpos = SGS_RCPOS_UNSPEC;
					if( !compile_node_r( C, func, n, &vpos ) )
						goto fail;
				}
				if( args % 2 == 1 )
				{
					INSTR_WRITE( SGS_SI_SETINDEX, *out, kpos, vpos );
					comp_reg_unwind( C, bkup );
				}
				args++;
				n = n->next;
			}
			sgs_BreakIf( args % 2 != 0 );
		}
		break;
		
	case SGS_SFT_OPER:
	case SGS_SFT_OPER_P:
		if( !compile_oper( C, func, node, out, 1, 1 ) ) goto fail;
		break;
		
	case SGS_SFT_FCALL:
	case SGS_SFT_THRCALL:
	case SGS_SFT_STHCALL:
	case SGS_SFT_NEWCALL:
		if( !compile_fcall( C, func, node, out, 1 ) ) goto fail;
		break;
		
	case SGS_SFT_INDEX:
		if( !compile_index_r( C, func, node, out ) ) goto fail;
		break;
		
	case SGS_SFT_MIDXSET:
		if( !compile_midxset( C, func, node, out, 0 ) ) goto fail;
		break;
		
	case SGS_SFT_MPROPSET:
		if( !compile_midxset( C, func, node, out, 1 ) ) goto fail;
		break;
		
	case SGS_SFT_EXPLIST:
		{
			sgs_FTNode* n = node->child;
			if( !n )
			{
				QPRINT( "Empty expression found" );
				goto fail;
			}
			while( n )
			{
				if( !compile_node_r( C, func, n, out ) )
					goto fail;
				n = n->next;
			}
		}
		break;
		
	default:
		QINTERR( 1002 );
		goto fail;
	}
	return 1;
	
fail:
	C->state |= SGS_HAS_ERRORS;
	return 0;
}

static SGSBOOL compile_for_explist( SGS_FNTCMP_ARGS, rcpos_t* out )
{
	sgs_FTNode* n;
	
	if( node->type != SGS_SFT_EXPLIST )
	{
		QINTERR( 1004 );
		goto fail;
	}
	
	n = node->child;
	while( n )
	{
		if( !compile_node_r( C, func, n, out ) )
			goto fail;
		n = n->next;
	}
	return 1;
	
fail:
	return 0;
}

static SGSBOOL compile_node( SGS_FNTCMP_ARGS )
{
	rcpos_t tmpin = SGS_RCPOS_UNSPEC, bkup = C->fctx->regs;
	
	switch( node->type )
	{
	/* ignore these items if they're leading in statements */
	case SGS_SFT_IDENT:
	case SGS_SFT_KEYWORD:
	case SGS_SFT_CONST:
	case SGS_SFT_ARRLIST:
	case SGS_SFT_DCTLIST:
	case SGS_SFT_MAPLIST:
		break;

	case SGS_SFT_OPER:
	case SGS_SFT_OPER_P:
		if( !compile_oper( C, func, node, &tmpin, 1, 0 ) ) goto fail;
		break;

	case SGS_SFT_INDEX:
		if( !compile_index_r( C, func, node, &tmpin ) ) goto fail;
		break;
		
	case SGS_SFT_MIDXSET:
		if( !compile_midxset( C, func, node, &tmpin, 0 ) ) goto fail;
		break;
		
	case SGS_SFT_MPROPSET:
		if( !compile_midxset( C, func, node, &tmpin, 1 ) ) goto fail;
		break;
		
	case SGS_SFT_FCALL:
	case SGS_SFT_THRCALL:
	case SGS_SFT_STHCALL:
	case SGS_SFT_NEWCALL:
		if( !compile_fcall( C, func, node, &tmpin, 0 ) ) goto fail;
		break;

	case SGS_SFT_EXPLIST:
		{
			sgs_FTNode* n = node->child;
			while( n )
			{
				if( !compile_node( C, func, n ) )
					goto fail;
				n = n->next;
			}
		}
		break;

	case SGS_SFT_RETURN:
		{
			rcpos_t regstate = C->fctx->regs;
			sgs_FTNode* n = node->child;
			if( n && n->next == NULL )
			{
				/* one value */
				rcpos_t arg = SGS_RCPOS_UNSPEC;
				if( !compile_node_r( C, func, n, &arg ) ) goto fail;
				/* could run out of registers if related expressions are too complicated:
					TODO unwinding from returned registers / better allocation algorithm */
				compile_defers( C, func, 0 );
				INSTR_WRITE( SGS_SI_RET1, 0, 0, arg );
			}
			else
			{
				int num = 0;
				while( n )
				{
					rcpos_t arg = SGS_RCPOS_UNSPEC;
					if( !compile_node_r( C, func, n, &arg ) ) goto fail;
					INSTR_WRITE( SGS_SI_PUSH, 0, arg, 0 );
					comp_reg_unwind( C, regstate );
					n = n->next;
					num++;
				}
				compile_defers( C, func, 0 );
				INSTR_WRITE( SGS_SI_RETN, num, 0, 0 );
			}
			comp_reg_unwind( C, regstate );
		}
		break;

	case SGS_SFT_BLOCK:
		node = node->child;
		BLOCK_BEGIN;
		while( node )
		{
			rcpos_t regstate = C->fctx->regs;
			if( !compile_node( C, func, node ) ) goto fail;
			node = node->next;
			comp_reg_unwind( C, regstate );
		}
		BLOCK_END;
		break;

	case SGS_SFT_IFELSE:
		{
			rcpos_t arg = SGS_RCPOS_UNSPEC;
			rcpos_t regstate = C->fctx->regs;
			
			if( !compile_node_r( C, func, node->child, &arg ) ) goto fail;
			comp_reg_unwind( C, regstate );
			INSTR_WRITE_PCH();
			{
				size_t jp1, jp2 = 0, jp3 = 0;
				jp1 = func->code.size;

				regstate = C->fctx->regs;
				
				if( !compile_node( C, func, node->child->next ) ) goto fail;
				comp_reg_unwind( C, regstate );

				if( node->child->next->next )
				{
					INSTR_WRITE_PCH();
					jp2 = func->code.size;
					{
						sgs_instr_t instr;
						/* WP: jump limit */
						ptrdiff_t jmp_off = (ptrdiff_t) ( jp2 - jp1 ) / SGS_INSTR_SIZE;
						if( over_limit( jmp_off, 32767 ) )
						{
							QPRINT( "Max. jump limit exceeded @ if/else; reduce size of construct" );
							goto fail;
						}
						instr = SGS_INSTR_MAKE_EX( SGS_SI_JMPF, jmp_off, arg );
						memcpy( func->code.ptr + jp1 - 4, &instr, sizeof(instr) );
					}

					regstate = C->fctx->regs;
					
					if( !compile_node( C, func, node->child->next->next ) ) goto fail;
					jp3 = func->code.size;
					{
						sgs_instr_t instr;
						/* WP: jump limit */
						ptrdiff_t jmp_off = (ptrdiff_t) ( jp3 - jp2 ) / SGS_INSTR_SIZE;
						if( over_limit( jmp_off, 32767 ) )
						{
							QPRINT( "Max. jump limit exceeded @ if/else; reduce size of construct" );
							goto fail;
						}
						instr = SGS_INSTR_MAKE_EX( SGS_SI_JUMP, jmp_off, 0 );
						memcpy( func->code.ptr + jp2 - 4, &instr, sizeof(instr) );
					}
					comp_reg_unwind( C, regstate );
				}
				else
				{
					sgs_instr_t instr;
					/* WP: jump limit */
					ptrdiff_t jmp_off = (ptrdiff_t) ( func->code.size - jp1 ) / SGS_INSTR_SIZE;
					if( over_limit( jmp_off, 32767 ) )
					{
						QPRINT( "Max. jump limit exceeded @ if/else; reduce size of construct" );
						goto fail;
					}
					instr = SGS_INSTR_MAKE_EX( SGS_SI_JMPF, jmp_off, arg );
					memcpy( func->code.ptr + jp1 - 4, &instr, sizeof(instr) );
				}
			}
		}
		break;

	case SGS_SFT_WHILE:
		{
			size_t codesize;
			rcpos_t arg = SGS_RCPOS_UNSPEC;
			rcpos_t regstate = C->fctx->regs;
			C->fctx->loops++;
			codesize = func->code.size;
			
			if( !compile_node_r( C, func, node->child, &arg ) ) goto fail; /* test */
			comp_reg_unwind( C, regstate );
			INSTR_WRITE_PCH();
			{
				ptrdiff_t off;
				size_t jp1, jp2 = 0;
				jp1 = func->code.size;

				LOOP_BEGIN;
				regstate = C->fctx->regs;
				
				if( !compile_node( C, func, node->child->next ) ) goto fail; /* while */
				comp_reg_unwind( C, regstate );
				LOOP_END;

				if( !compile_breaks( C, func, node, 1 ) )
					goto fail;

				jp2 = func->code.size;
				/* WP: jump limit */
				off = (ptrdiff_t) ( codesize - jp2 ) / SGS_INSTR_SIZE - 1;
				if( over_limit( off, 32767 ) )
				{
					QPRINT( "Max. jump limit exceeded @ while; reduce size of loop" );
					goto fail;
				}
				INSTR_WRITE_EX( SGS_SI_JUMP, off, 0 );
				{
					sgs_instr_t instr;
					instr = SGS_INSTR_MAKE_EX( SGS_SI_JMPF, ( jp2 - jp1 ) / SGS_INSTR_SIZE + 1, arg );
					memcpy( func->code.ptr + jp1 - 4, &instr, sizeof(instr) );
				}
			}
			if( !compile_breaks( C, func, node, 0 ) )
				goto fail;
			C->fctx->loops--;
		}
		break;

	case SGS_SFT_DOWHILE:
		{
			size_t codesize;
			rcpos_t regstate = C->fctx->regs;
			rcpos_t arg = SGS_RCPOS_UNSPEC;
			ptrdiff_t off;
			C->fctx->loops++;
			codesize = func->code.size;
			{
				LOOP_BEGIN;
				if( !compile_node( C, func, node->child->next ) ) goto fail; /* while */
				comp_reg_unwind( C, regstate );
				LOOP_END;

				if( !compile_breaks( C, func, node, 1 ) )
					goto fail;
			}
			
			if( !compile_node_r( C, func, node->child, &arg ) ) goto fail; /* test */
			comp_reg_unwind( C, regstate );
			/* WP: jump limit */
			off = (ptrdiff_t) ( codesize - func->code.size ) / SGS_INSTR_SIZE - 1;
			if( over_limit( off, 32767 ) )
			{
				QPRINT( "Max. jump limit exceeded @ do/while; reduce size of loop" );
				goto fail;
			}
			INSTR_WRITE_EX( SGS_SI_JMPT, off, arg );
			if( !compile_breaks( C, func, node, 0 ) )
				goto fail;
			C->fctx->loops--;
		}
		break;

	case SGS_SFT_FOR:
		{
			size_t codesize;
			rcpos_t regstate = C->fctx->regs;
			rcpos_t arg = SGS_RCPOS_UNSPEC;
			C->fctx->loops++;
			
			if( !compile_node( C, func, node->child ) ) goto fail; /* init */
			comp_reg_unwind( C, regstate );
			codesize = func->code.size;
			
			if( !compile_for_explist( C, func, node->child->next, &arg ) ) goto fail; /* test */
			comp_reg_unwind( C, regstate );
			if( arg != SGS_RCPOS_UNSPEC )
			{
				/* for(;<expr>;) - test expression required to make a conditional jump */
				INSTR_WRITE_PCH();
			}
			{
				ptrdiff_t off;
				size_t jp1, jp2 = 0;
				jp1 = func->code.size;

				LOOP_BEGIN;
				if( !compile_node( C, func, node->child->next->next->next ) ) goto fail; /* block */
				comp_reg_unwind( C, regstate );
				LOOP_END;

				if( !compile_breaks( C, func, node, 1 ) )
					goto fail;
				
				if( !compile_node( C, func, node->child->next->next ) ) goto fail; /* incr */
				comp_reg_unwind( C, regstate );

				jp2 = func->code.size;
				/* WP: jump limit */
				off = (ptrdiff_t) ( codesize - jp2 ) / SGS_INSTR_SIZE - 1;
				if( over_limit( off, 32767 ) )
				{
					QPRINT( "Max. jump limit exceeded @ for; reduce size of loop" );
					goto fail;
				}
				INSTR_WRITE_EX( SGS_SI_JUMP, off, 0 );
				if( arg != SGS_RCPOS_UNSPEC )
				{
					sgs_instr_t instr;
					instr = SGS_INSTR_MAKE_EX( SGS_SI_JMPF, ( jp2 - jp1 ) / SGS_INSTR_SIZE + 1, arg );
					memcpy( func->code.ptr + jp1 - 4, &instr, sizeof(instr) );
				}
			}
			if( !compile_breaks( C, func, node, 0 ) )
				goto fail;
			C->fctx->loops--;
		}
		break;
		
	case SGS_SFT_FORNUMI:
	case SGS_SFT_FORNUMR:
		{
			sgs_FTNode* numexprs = node->child->next->child;
			rcpos_t regstate = C->fctx->regs;
			rcpos_t constpos, arg = find_varT( &C->fctx->vars, node->child->token );
			C->fctx->loops++;
			
			if( numexprs->next )
			{
				compile_node_rrw( C, func, numexprs, arg + 1 );
				compile_node_rrw( C, func, numexprs->next, arg + 2 );
				if( numexprs->next->next )
				{
					/* start, end, incr */
					compile_node_rrw( C, func, numexprs->next->next, arg + 3 );
				}
				else
				{
					/* start, end, [1] */
					goto fornum_add_default_incr;
				}
			}
			else
			{
				/* [0], end, [1] */
				constpos = node->type == SGS_SFT_FORNUMI
					? add_const_i( C, func, 0 )
					: add_const_r( C, func, 0 );
				INSTR_WRITE( SGS_SI_SET, arg + 1, BC_CONSTENC( constpos ), 0 );
				
				compile_node_rrw( C, func, numexprs, arg + 2 );
				
fornum_add_default_incr:
				constpos = node->type == SGS_SFT_FORNUMI
					? add_const_i( C, func, 1 )
					: add_const_r( C, func, 1 );
				INSTR_WRITE( SGS_SI_SET, arg + 3, BC_CONSTENC( constpos ), 0 );
			}
			
			/* for initialization */
			INSTR_WRITE_PCH();
			
			{
				ptrdiff_t off;
				size_t jp1, jp2 = 0;
				jp1 = func->code.size;
				int xarg = arg | ( node->type == SGS_SFT_FORNUMR ? 0x100 : 0 );
				
				LOOP_BEGIN;
				if( !compile_node( C, func, node->child->next->next ) ) goto fail; /* block */
				comp_reg_unwind( C, regstate );
				LOOP_END;
				
				if( !compile_breaks( C, func, node, 1 ) )
					goto fail;
				
				jp2 = func->code.size;
				/* WP: jump limit */
				off = (ptrdiff_t) ( jp1 - jp2 ) / SGS_INSTR_SIZE - 1;
				if( over_limit( off + 2, 32767 ) )
				{
					QPRINT( "Max. jump limit exceeded @ numeric for; reduce size of loop" );
					goto fail;
				}
				INSTR_WRITE_EX( SGS_SI_NFORJUMP, off, xarg );
				{
					sgs_instr_t instr;
					instr = SGS_INSTR_MAKE_EX( SGS_SI_NFORPREP, ( jp2 - jp1 ) / SGS_INSTR_SIZE + 1, xarg );
					memcpy( func->code.ptr + jp1 - 4, &instr, sizeof(instr) );
				}
			}
			if( !compile_breaks( C, func, node, 0 ) )
				goto fail;
			C->fctx->loops--;
		}
		break;
		
	case SGS_SFT_FOREACH:
		{
			size_t codesize, jp1, jp2;
			rcpos_t var = SGS_RCPOS_UNSPEC, iter, optkeyreg = -1, optvalreg = -1;
			rcpos_t regstate, regstate2;
			regstate2 = C->fctx->regs;
			
			/* init */
			var = SGS_RCPOS_UNSPEC;
			if( !compile_node_r( C, func, node->child->next->next, &var ) ) goto fail; /* get variable */
			
			iter = comp_reg_alloc( C );
			if( node->child->type != SGS_SFT_NULL ) optkeyreg = comp_reg_alloc( C );
			if( node->child->next->type != SGS_SFT_NULL ) optvalreg = comp_reg_alloc( C );
			regstate = C->fctx->regs;
			C->fctx->loops++;
			
			INSTR_WRITE( SGS_SI_FORPREP, iter, var, 0 );
			comp_reg_unwind( C, regstate );
			
			/* iterate */
			codesize = func->code.size;
			INSTR_WRITE_PCH();
			jp1 = func->code.size;
			INSTR_WRITE( SGS_SI_FORLOAD, iter, optkeyreg, optvalreg );
			
			{
				ptrdiff_t off = 0;
				
				/* write to key variable */
				if( node->child->type != SGS_SFT_NULL && !compile_ident_w( C, func, node->child, optkeyreg ) ) goto fail;
				
				/* write to value variable */
				if( node->child->next->type != SGS_SFT_NULL && !compile_ident_w( C, func, node->child->next, optvalreg ) ) goto fail;
				
				LOOP_BEGIN;
				if( !compile_node( C, func, node->child->next->next->next ) ) goto fail; /* block */
				comp_reg_unwind( C, regstate );
				LOOP_END;
				
				if( !compile_breaks( C, func, node, 1 ) )
					goto fail;
				
				jp2 = func->code.size;
				/* WP: jump limit */
				off = (ptrdiff_t) ( codesize - jp2 ) / SGS_INSTR_SIZE - 1;
				if( over_limit( off, 32767 ) )
				{
					QPRINT( "Max. jump limit exceeded @ foreach; reduce size of loop" );
					goto fail;
				}
				INSTR_WRITE_EX( SGS_SI_JUMP, off, 0 );
				{
					sgs_instr_t instr;
					instr = SGS_INSTR_MAKE_EX( SGS_SI_FORJUMP, ( func->code.size - jp1 ) / SGS_INSTR_SIZE, iter );
					memcpy( func->code.ptr + jp1 - 4, &instr, sizeof(instr) );
				}
			}
			
			if( !compile_breaks( C, func, node, 0 ) )
				goto fail;
			C->fctx->loops--;
			comp_reg_unwind( C, regstate2 );
		}
		break;

	case SGS_SFT_BREAK:
		{
			sgs_TokenList tl = sgsT_Next( node->token );
			int32_t blev = 1;
			if( *tl == SGS_ST_NUMINT )
			{
				sgs_Int tint;
				SGS_AS_INTEGER( tint, tl + 1 );
				if( tint < 1 || tint > 0xffff )
				{
					QPRINT( "Invalid break level" );
					goto fail;
				}
				blev = (int32_t) tint;
			}
			if( blev > C->fctx->loops )
			{
				if( C->fctx->loops )
					QPRINT( "Break level too high" );
				else
					QPRINT( "Attempted to break while not in a loop" );
				goto fail;
			}
			sgs_LoopInfo* loop = C->fctx->loopinfo;
			{
				int lev = blev;
				while( --lev > 0 )
					loop = loop->parent;
			}
			compile_defers( C, func, loop->block );
			/* WP: instruction limit, max loop depth */
			fctx_binfo_add( C, C->fctx, (uint32_t) func->code.size, (uint16_t)( C->fctx->loops + 1 - blev ), SGS_FALSE );
			INSTR_WRITE_PCH();
		}
		break;

	case SGS_SFT_CONT:
		{
			sgs_TokenList tl = sgsT_Next( node->token );
			int32_t blev = 1;
			if( *tl == SGS_ST_NUMINT )
			{
				sgs_Int tint;
				SGS_AS_INTEGER( tint, tl + 1 );
				if( tint < 1 || tint > 0xffff )
				{
					QPRINT( "Invalid continue level" );
					goto fail;
				}
				blev = (int32_t) tint;
			}
			if( blev > C->fctx->loops )
			{
				if( C->fctx->loops )
					QPRINT( "Continue level too high" );
				else
					QPRINT( "Attempted to continue while not in a loop" );
				goto fail;
			}
			sgs_LoopInfo* loop = C->fctx->loopinfo;
			{
				int lev = blev;
				while( --lev > 0 )
					loop = loop->parent;
			}
			compile_defers( C, func, loop->block );
			/* WP: instruction limit, max loop depth */
			fctx_binfo_add( C, C->fctx, (uint32_t) func->code.size, (uint16_t)( C->fctx->loops + 1 - blev ), SGS_TRUE );
			INSTR_WRITE_PCH();
		}
		break;
	
	case SGS_SFT_DEFER:
		fctx_defer_add( C, node->child );
		break;
	
	case SGS_SFT_FUNC:
		{
			sgs_FTNode* n_name;
			rcpos_t pos;
			if( !compile_func( C, func, node, &pos ) ) goto fail;
			n_name = node->child->next->next->next;

			if( n_name )
			{
				if( !compile_node_w( C, func, n_name, pos ) ) goto fail;
				
				// symbol registration
				if( C->fctx->func == SGS_FALSE || n_name->type == SGS_SFT_CLSPFX )
				{
					rcpos_t r_name;
					sgs_MemBuf ffn = sgs_membuf_create();
					rpts( &ffn, C, n_name );
					r_name = add_const_s( C, func, (uint32_t) ffn.size, ffn.ptr );
					sgs_membuf_destroy( &ffn, C );
					INSTR_WRITE( SGS_SI_RSYM, 0, BC_CONSTENC( r_name ), pos );
				}
			}
		}
		break;
		
	case SGS_SFT_CLASS:
		{
			sgs_FTNode* name = node->child;
			sgs_FTNode* it = name->next;
			rcpos_t regstate = C->fctx->regs;
			
			/* create class */
			rcpos_t clsvar, r_name, r_inhname;
			clsvar = comp_reg_alloc( C );
			compile_ident( C, func, name, &r_name );
			r_inhname = clsvar; /* r_inhname is 'empty' if it equals clsvar (class cannot inherit itself) */
			if( it && it->type == SGS_SFT_CLSINH )
			{
				compile_ident( C, func, it, &r_inhname );
				it = it->next;
			}
			INSTR_WRITE( SGS_SI_CLASS, clsvar, r_name, r_inhname );
			compile_ident_w( C, func, name, clsvar );
			
			while( it )
			{
				if( it->type == SGS_SFT_CLSGLOB )
				{
					/* class globals -- convert to 'setindex' */
					sgs_FTNode* vn = it->child;
					while( vn )
					{
						rcpos_t vname, vsrc = SGS_RCPOS_UNSPEC;
						compile_ident( C, func, vn, &vname );
						if( vn->child )
						{
							if( !compile_node_r( C, func, vn->child, &vsrc ) )
								goto fail;
						}
						else
						{
							vsrc = add_const_null( C, func );
						}
						INSTR_WRITE( SGS_SI_SETINDEX, clsvar, vname, vsrc );
						
						vn = vn->next;
					}
				}
				else if( it->type == SGS_SFT_FUNC )
				{
					compile_node( C, func, it );
				}
				else
				{
					QINTERR( 1005 );
					goto fail;
				}
				it = it->next;
			}
			
			comp_reg_unwind( C, regstate );
		}
		break;
		
	case SGS_SFT_VARLIST:
	case SGS_SFT_GVLIST:
		{
			rcpos_t regstate = C->fctx->regs;
			sgs_FTNode* pp = node->child;
			while( pp )
			{
				if( pp->child )
				{
					rcpos_t arg = SGS_RCPOS_UNSPEC;
					if( !compile_node_r( C, func, pp->child, &arg ) ) goto fail;
					if( !pp->token || *pp->token != SGS_ST_IDENT ) goto fail;
					compile_ident_w( C, func, pp, arg );
					comp_reg_unwind( C, regstate );
				}
				pp = pp->next;
			}
		}
		break;
		
	default:
		QINTERR( 1001 );
		goto fail;
	}
	
	comp_reg_unwind( C, bkup );
	return 1;

fail:
	return 0;
}


sgs_iFunc* sgsBC_Generate( SGS_CTX, sgs_FTNode* tree )
{
	sgs_FuncCtx* fctx = fctx_create( C );
	fctx->func = SGS_FALSE;
	C->fctx = fctx;
	
	if( !compile_fn_base( C, &fctx->cfunc, tree, 0 ) || ( C->state & SGS_HAS_ERRORS ) )
		goto fail;
	
	C->fctx = NULL;
	{
		sgs_iFunc* outfn = sgsBC_ConvertFunc( C, fctx, "<main>", 6, 0 );
		fctx_destroy( C, fctx );
		return outfn;
	}

fail:
	C->fctx = NULL;
	fctx_destroy( C, fctx );
	C->state |= SGS_HAS_ERRORS;
	return NULL;
}

static void dump_varinfo( SGS_CTX, const char* varinfo )
{
	const char* varinfoend;
	uint32_t dbgvarsize;
	
	memcpy( &dbgvarsize, varinfo, sizeof(dbgvarsize) );
	varinfoend = varinfo + dbgvarsize;
	varinfo += sizeof(dbgvarsize);
	while( varinfo < varinfoend )
	{
		uint32_t from, to;
		int16_t pos;
		uint8_t len;
		
		memcpy( &from, varinfo, sizeof(from) );
		varinfo += sizeof(from);
		memcpy( &to, varinfo, sizeof(to) );
		varinfo += sizeof(to);
		memcpy( &pos, varinfo, sizeof(pos) );
		varinfo += sizeof(pos);
		memcpy( &len, varinfo, sizeof(len) );
		varinfo += sizeof(len);
		
		if( pos == 0 ) sgs_ErrWritef( C, "[global] " );
		else if( pos < 0 ) sgs_ErrWritef( C, "[closure %d] ", -1 - pos );
		else sgs_ErrWritef( C, "[local %d] ", pos - 1 );
		
		sgs_ErrWrite( C, varinfo, len );
		sgs_ErrWritef( C, " (%d-%d)\n", (int) from, (int) to );
		
		varinfo += len;
	}
}

void sgsBC_DumpEx( SGS_CTX, const char* constptr, size_t constsize,
	const char* codeptr, size_t codesize, const sgs_LineNum* lines, const char* varinfo )
{
	const sgs_Variable* vbeg = SGS_ASSUME_ALIGNED_CONST( constptr, sgs_Variable );
	const sgs_Variable* vend = SGS_ASSUME_ALIGNED_CONST( constptr + constsize, sgs_Variable );
	const sgs_Variable* var = vbeg;

	sgs_ErrWritef( C, "{\n" );
	sgs_ErrWritef( C, "> constants:\n" );
	while( var < vend )
	{
		sgs_ErrWritef( C, "%4d = ", (int) ( var - vbeg ) );
		sgsVM_VarDump( C, var );
		sgs_ErrWritef( C, "\n" );
		var++;
	}
	sgs_ErrWritef( C, "> code:\n" );
	sgsBC_DumpOpcode( C, SGS_ASSUME_ALIGNED_CONST( codeptr, sgs_instr_t ),
		codesize / sizeof( sgs_instr_t ), SGS_ASSUME_ALIGNED_CONST( codeptr, sgs_instr_t ), vbeg, lines );
	if( varinfo )
	{
		sgs_ErrWritef( C, "> variables:\n" );
		dump_varinfo( C, varinfo );
	}
	sgs_ErrWritef( C, "}\n" );
}


#undef rcpos_t
#undef SGS_FNTCMP_ARGS
#undef INSTR_N
#undef INSTR
#undef INSTR_WRITE
#undef INSTR_WRITE_EX
#undef INSTR_WRITE_PCH
#undef QPRINT

