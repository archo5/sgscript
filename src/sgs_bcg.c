

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
	sgs_BreakIf( n < 1 );
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
	sgs_membuf_appbuf( &fctx->gvars, C, "_G=", 3 );
	
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
static void fctx_dumpvarlist( SGS_CTX, sgs_MemBuf* mb )
{
	char* p = mb->ptr, *pend = mb->ptr + mb->size;
	while( p < pend )
	{
		if( *p == '=' )
		{
			if( p < pend - 1 )
				sgs_ErrWritef( C, ", " );
		}
		else
			sgs_ErrWritef( C, "%c", *p );
		p++;
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
static void dump_opcode_a( SGS_CTX, const char* name, sgs_instr_t I )
{
	sgs_ErrWritef( C, "%s R%" PRId32" <= ", name, SGS_INSTR_GET_A( I ) );
	dump_rcpos( C, SGS_INSTR_GET_B( I ) );
	sgs_ErrWritef( C, ", " );
	dump_rcpos( C, SGS_INSTR_GET_C( I ) );
}
static void dump_opcode_b( SGS_CTX, const char* name, sgs_instr_t I )
{
	sgs_ErrWritef( C, "%s R%" PRId32" <= ", name, SGS_INSTR_GET_A( I ) );
	dump_rcpos( C, SGS_INSTR_GET_B( I ) );
}
static void dump_opcode_b1( SGS_CTX, const char* name, sgs_instr_t I )
{
	sgs_ErrWritef( C, "%s ", name );
	dump_rcpos( C, SGS_INSTR_GET_B( I ) );
	sgs_ErrWritef( C, " <= " );
	dump_rcpos( C, SGS_INSTR_GET_C( I ) );
}

void sgsBC_DumpOpcode( SGS_CTX, const sgs_instr_t* ptr, size_t count,
	const sgs_instr_t* numstart, const sgs_LineNum* lines )
{
	const sgs_instr_t* pend = ptr + count;
	for( ; ptr < pend; ptr++ )
	{
		sgs_instr_t I = *ptr;
		int op = SGS_INSTR_GET_OP( I ), argA = SGS_INSTR_GET_A( I ),
			argB = SGS_INSTR_GET_B( I ), argC = SGS_INSTR_GET_C( I ),
			argE = SGS_INSTR_GET_E( I );

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

		switch( op )
		{
#define DOP_A( wat ) case SGS_SI_##wat: dump_opcode_a( C, #wat, I ); break;
#define DOP_B( wat ) case SGS_SI_##wat: dump_opcode_b( C, #wat, I ); break;
		case SGS_SI_NOP: sgs_ErrWritef( C, "NOP   " ); break;

		case SGS_SI_PUSH: sgs_ErrWritef( C, "PUSH " ); dump_rcpos( C, argB ); break;
		case SGS_SI_INT: sgs_ErrWritef( C, "INT %d", argC ); break;

		case SGS_SI_RET1: sgs_ErrWritef( C, "RET_1 <= " ); dump_rcpos( C, argC ); break;
		case SGS_SI_RETN: sgs_ErrWritef( C, "RETURN %d", argA ); break;
		case SGS_SI_JUMP: sgs_ErrWritef( C, "JUMP %d", (int) (int16_t) argE ); break;
		case SGS_SI_JMPT: sgs_ErrWritef( C, "JMP_T " ); dump_rcpos( C, argC );
			sgs_ErrWritef( C, ", %d", (int) (int16_t) argE ); break;
		case SGS_SI_JMPF: sgs_ErrWritef( C, "JMP_F " ); dump_rcpos( C, argC );
			sgs_ErrWritef( C, ", %d", (int) (int16_t) argE ); break;
		case SGS_SI_JMPN: sgs_ErrWritef( C, "JMP_N " ); dump_rcpos( C, argC );
			sgs_ErrWritef( C, ", %d", (int) (int16_t) argE ); break;
		case SGS_SI_CALL: sgs_ErrWritef( C, "CALL args: %d - %d expect: %d%s",
			argB & 0xff, argC, argA, ( argB & 0x100 ) ? ", method" : "" );
			break;

		case SGS_SI_FORPREP: sgs_ErrWritef( C, "FOR_PREP " ); dump_rcpos( C, argA );
			sgs_ErrWritef( C, " <= " ); dump_rcpos( C, argB ); break;
		case SGS_SI_FORLOAD: sgs_ErrWritef( C, "FOR_LOAD " ); dump_rcpos( C, argA );
			sgs_ErrWritef( C, " => " ); dump_rcpos( C, argB );
			sgs_ErrWritef( C, ", " ); dump_rcpos( C, argC ); break;
		case SGS_SI_FORJUMP: sgs_ErrWritef( C, "FOR_JUMP " ); dump_rcpos( C, argC );
			sgs_ErrWritef( C, ", %d", (int) (int16_t) argE ); break;

		case SGS_SI_LOADCONST: sgs_ErrWritef( C, "LOADCONST " ); dump_rcpos( C, argC );
			sgs_ErrWritef( C, " <= C%d", argE ); break;

		DOP_B( GETVAR );
		case SGS_SI_SETVAR: dump_opcode_b1( C, "SETVAR", I ); break;
		DOP_A( GETPROP );
		DOP_A( SETPROP );
		DOP_A( GETINDEX );
		DOP_A( SETINDEX );
		case SGS_SI_ARRPUSH: sgs_ErrWritef( C, "ARR_PUSH R%d <= ", argA );
			dump_rcpos( C, argC ); break;

		case SGS_SI_CLSRINFO: sgs_ErrWritef( C, "CLSR_INFO %d %d %d", argA, argB, argC ); break;
		case SGS_SI_MAKECLSR: sgs_ErrWritef( C, "MAKE_CLSR " ); dump_rcpos( C, argA );
			sgs_ErrWritef( C, " <= " ); dump_rcpos( C, argB );
			sgs_ErrWritef( C, " [%d]", argC ); break;
		case SGS_SI_GETCLSR: sgs_ErrWritef( C, "GET_CLSR " ); dump_rcpos( C, argA );
			sgs_ErrWritef( C, " <= CL%d", argB ); break;
		case SGS_SI_SETCLSR: sgs_ErrWritef( C, "SET_CLSR CL%d <= ", argB );
			dump_rcpos( C, argC ); break;

		DOP_B( SET );
		case SGS_SI_MCONCAT: sgs_ErrWritef( C, "MCONCAT " ); dump_rcpos( C, argA );
			sgs_ErrWritef( C, " [%d]", argB ); break;
		DOP_A( CONCAT );
		DOP_B( NEGATE );
		DOP_B( BOOL_INV );
		DOP_B( INVERT );

		DOP_B( INC );
		DOP_B( DEC );
		DOP_A( ADD );
		DOP_A( SUB );
		DOP_A( MUL );
		DOP_A( DIV );
		DOP_A( MOD );

		DOP_A( AND );
		DOP_A( OR );
		DOP_A( XOR );
		DOP_A( LSH );
		DOP_A( RSH );

		DOP_A( SEQ );
		DOP_A( EQ );
		DOP_A( LT );
		DOP_A( LTE );

		DOP_A( SNEQ );
		DOP_A( NEQ );
		DOP_A( GT );
		DOP_A( GTE );
		
		DOP_A( RAWCMP );

		case SGS_SI_ARRAY:
			sgs_ErrWritef( C, "ARRAY args:%d output:", argE );
			dump_rcpos( C, argC ); break;
		case SGS_SI_DICT:
			sgs_ErrWritef( C, "DICT output:" );
			dump_rcpos( C, argC ); break;
		case SGS_SI_CLASS:
			sgs_ErrWritef( C, "CLASS output:%d", argA );
			sgs_ErrWritef( C, ", name:" ); dump_rcpos( C, argB );
			sgs_ErrWritef( C, ", inhname:%s", argC == argA ? "<none>" : "" );
			if( argC != argA ) dump_rcpos( C, argC );
			break;
		case SGS_SI_NEW:
			sgs_ErrWritef( C, "NEW output:%d", argA );
			sgs_ErrWritef( C, ", class:" ); dump_rcpos( C, argB );
			break;
		case SGS_SI_RSYM:
			sgs_ErrWritef( C, "RSYM name:" ); dump_rcpos( C, argB );
			sgs_ErrWritef( C, " value:" ); dump_rcpos( C, argC ); break;
			
		DOP_B( COTRT );
		DOP_B( COTRF );
		case SGS_SI_COABORT: sgs_ErrWritef( C, "CO_ABORT %d", (int) (int16_t) argE ); break;
		case SGS_SI_YLDJMP: sgs_ErrWritef( C, "YIELD_JUMP %d", (int) (int16_t) argE ); break;
			
#undef DOP_A
#undef DOP_B
		default:
			sgs_ErrWritef( C, "<error> (op=%d A=%d B=%d C=%d E=%d)",
				op, argA, argB, argC, argE ); break;
		}
		sgs_ErrWritef( C, "\n" );
	}
}


static int find_var( sgs_MemBuf* S, const char* str, unsigned len )
{
	int at = 0;
	const char* ptr = S->ptr;
	const char* pend = ptr + S->size;
	const char* estr = str + len;
	
	while( ptr < pend )
	{
		/* compare ptr .. '=' with str .. str + len */
		const char* cstr = str;
		while( ptr < pend && *ptr != '=' && cstr < estr )
		{
			if( *ptr != *cstr )
				break;
			ptr++;
			cstr++;
		}
		if( ptr < pend && *ptr == '=' && cstr == estr )
			return at;
		
		/* advance to next variable */
		at++;
		while( ptr < pend && *ptr != '=' )
			ptr++;
		ptr++; /* skip '=' */
	}
	return -1;
}

static int find_nth_var( sgs_MemBuf* S, int which, char** outstr, unsigned* outlen )
{
	char* ptr = S->ptr;
	char* pend = ptr + S->size;
	while( which > 0 )
	{
		while( ptr < pend && *ptr != '=' )
			ptr++;
		ptr++;
		which--;
	}
	if( ptr >= pend )
		return 0;
	*outstr = ptr;
	while( ptr < pend && *ptr != '=' )
		ptr++;
	/* WP: ptr always bigger or equal to *outstr, difference cannot exceed 255 */
	*outlen = (unsigned) ( ptr - *outstr );
	return 1;
}

static int add_var( sgs_MemBuf* S, SGS_CTX, char* str, unsigned len )
{
	int pos = find_var( S, str, len );
	if( pos < 0 )
	{
		sgs_membuf_appbuf( S, C, str, len );
		sgs_membuf_appchr( S, C, '=' );
		return SGS_TRUE;
	}
	return SGS_FALSE;
}

#define find_varT( S, tok ) \
	find_var( S, (char*) (tok) + 2, tok[1] )
#define add_varT( S, C, tok ) \
	add_var( S, C, (char*) (tok) + 2, tok[1] )

static int preadd_thisvar( sgs_MemBuf* S, SGS_CTX )
{
	int pos = find_var( S, "this", 4 );
	if( pos < 0 )
	{
		sgs_membuf_insbuf( S, C, 0, "this=", 5 );
		return SGS_TRUE;
	}
	return SGS_FALSE;
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
		if( add_varT( &C->fctx->vars, C, node->token ) )
			comp_reg_alloc( C );
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
			func->gotthis = SGS_TRUE;
			if( preadd_thisvar( &C->fctx->vars, C ) )
				comp_reg_alloc( C );
		}
		else if( node->type == SGS_SFT_OPER )
		{
			if( SGS_ST_OP_ASSIGN( *node->token ) && node->child )
			{
				if( node->child->type == SGS_SFT_IDENT )
				{
					/* add_var calls find_var internally but - GVARS vs VARS - note the difference */
					if( find_varT( &C->fctx->gvars, node->child->token ) == -1 &&
						find_varT( &C->fctx->clsr, node->child->token ) == -1 &&
						add_varT( &C->fctx->vars, C, node->child->token ) )
						comp_reg_alloc( C );
				}
				if( node->child->type == SGS_SFT_EXPLIST )
					ret &= preparse_varlist( C, func, node->child );
			}
			ret &= preparse_varlists( C, func, node->child );
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
				if( node->child->type != SGS_SFT_NULL && 
					add_varT( &C->fctx->vars, C, node->child->token ) )
					comp_reg_alloc( C );
				if( node->child->next->type != SGS_SFT_NULL && 
					add_varT( &C->fctx->vars, C, node->child->next->token ) )
					comp_reg_alloc( C );
			}

			ret &= preparse_varlists( C, func, node->child->next );
		}
		else if( node->type == SGS_SFT_FUNC )
		{
			sgs_FTNode* N = node->child->next->next->next;
			if( N && N->type == SGS_SFT_IDENT )
			{
				if( find_varT( &C->fctx->gvars, N->token ) == -1 && /* if the variable hasn't been .. */
					find_varT( &C->fctx->clsr, N->token ) == -1 && /* .. created before */
					/* if it was successfully added */
					add_varT( C->fctx->func ? &C->fctx->vars : &C->fctx->gvars, C, N->token ) &&
					C->fctx->func ) /* and if it was added as a local variable */
					comp_reg_alloc( C ); /* add a register for it */
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
		if( !add_var( &C->fctx->vars, C, (char*) node->token + 2, node->token[ 1 ] ) )
		{
			QPRINT( "Cannot redeclare arguments with the same name" );
			return 0;
		}
		comp_reg_alloc( C );
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

static rcpos_t add_const_i( SGS_CTX, sgs_CompFunc* func, sgs_Int ival )
{
	add_const_HDR;
	while( var < vend )
	{
		if( var->type == SGS_VT_INT && var->data.I == ival )
			return (rcpos_t) ( var - vbeg ); /* WP: const limit */
		var++;
	}

	nvar.type = SGS_VT_INT;
	nvar.data.I = ival;
	sgs_membuf_appbuf( &func->consts, C, &nvar, sizeof( nvar ) );
	return (rcpos_t) ( vend - vbeg ); /* WP: const limit */
}

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

sgs_iFunc* sgsBC_ConvertFunc( SGS_CTX, sgs_CompFunc* nf,
	const char* funcname, size_t fnsize, sgs_LineNum lnum )
{
	sgs_Variable strvar;
	sgs_iFunc* F = sgs_Alloc_a( sgs_iFunc, nf->consts.size + nf->code.size );

#if 0	
	sgs_SetErrOutputFunc( C, sgs_StdOutputFunc, stderr );
	sgsBC_DumpOpcode( C, (sgs_instr_t*) nf->code.ptr, nf->code.size / sizeof(sgs_instr_t),
		(sgs_instr_t*) nf->code.ptr, (sgs_LineNum*) nf->lnbuf.ptr );
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
	
	return F;
}

static rcpos_t add_const_f( SGS_CTX, sgs_CompFunc* func, sgs_CompFunc* nf,
	const char* funcname, size_t fnsize, sgs_LineNum lnum )
{
	sgs_Variable nvar;
	rcpos_t pos;
	sgs_iFunc* F = sgsBC_ConvertFunc( C, nf, funcname, fnsize, lnum );
	
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

	case SGS_ST_OP_CAT: case SGS_ST_OP_CATEQ: return SGS_SI_CONCAT;

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
				QPRINT( "This function is not a method, cannot use 'this'" );
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
	if( ( pos = find_var( &C->fctx->clsr, (char*) node->token + 2, node->token[1] ) ) >= 0 )
	{
		*out = comp_reg_alloc( C );
		INSTR_WRITE( SGS_SI_GETCLSR, *out, pos, 0 );
		return 1;
	}

	if( C->fctx->func )
	{
		rcpos_t gpos = find_var( &C->fctx->gvars, (char*) node->token + 2, node->token[ 1 ] );
		if( gpos >= 0 )
			pos = -1;
		else
		{
			pos = find_var( &C->fctx->vars, (char*) node->token + 2, node->token[ 1 ] );
			if( pos < 0 )
				pos = -1; /* read from globals by default */
		}
	}
	else
	{
		pos = find_var( &C->fctx->vars, (char*) node->token + 2, node->token[ 1 ] );
	}

	if( pos >= 0 )
	{
		*out = pos;
	}
	else
	{
		*out = comp_reg_alloc( C );
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

	if( ( pos = find_var( &C->fctx->clsr, (char*) node->token + 2, node->token[1] ) ) >= 0 )
	{
		INSTR_WRITE( SGS_SI_SETCLSR, 0, pos, src );
		return 1;
	}

	if( C->fctx->func )
	{
		rcpos_t gpos = find_var( &C->fctx->gvars, (char*) node->token + 2, node->token[ 1 ] );
		if( gpos >= 0 )
			pos = -1;
		else
		{
			add_var( &C->fctx->vars, C, (char*) node->token + 2, node->token[ 1 ] );
			pos = find_var( &C->fctx->vars, (char*) node->token + 2, node->token[ 1 ] );
		}
	}
	else
	{
		pos = find_var( &C->fctx->vars, (char*) node->token + 2, node->token[ 1 ] );
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
	else if( *node->token == SGS_ST_STRING )
	{
		uint32_t val;
		SGS_AS_UINT32( val, node->token + 1 );
		*opos = BC_CONSTENC( add_const_s( C, func, val, (const char*) node->token + 5 ) );
	}
	else
	{
		QINTERR( 1011 );
		return 0;
	}
	return 1;
}


static SGSBOOL compile_regcopy( SGS_FNTCMP_ARGS, size_t from, rcpos_t srcpos, rcpos_t dstpos )
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
		rcpos_t exprpos = -1, srcpos = -1, retpos = -1;
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
		SGS_FN_ENTER;
		if( !compile_node_r( C, func, n, &exprpos ) || exprpos < 0 ) return 0;
		
		n = n->next;
		INSTR_WRITE_PCH();
		csz1 = func->code.size;
		if( !compile_node_r( C, func, n, &srcpos ) ||
			!compile_regcopy( C, func, n, csz1, srcpos, retpos ) ) return 0;
		
		n = n->next;
		INSTR_WRITE_PCH();
		csz2 = func->code.size;
		if( !compile_node_r( C, func, n, &srcpos ) ||
			!compile_regcopy( C, func, n, csz2, srcpos, retpos ) ) return 0;
		
		csz3 = func->code.size;
		
		INSTR_WRITE_EX( SGS_SI_NOP, 0, 0 ); /* harmful optimization prevention hack */
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
		
		if( out )
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
		if( out )
			*out = argpos;
		
		/* load function (for properties, object too) */
		if( node->child->type == SGS_SFT_OPER &&
			( *node->child->token == SGS_ST_OP_MMBR || *node->child->token == SGS_ST_OP_NOT ) )
		{
			sgs_FTNode* ncc = node->child->child;
			rcpos_t proppos = -1;
			SGS_FN_ENTER;
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
					SGS_FN_ENTER;
					/* load property key */
					if( !compile_node_r( C, func, ncc->next, &proppos ) ) return 0;
				}
				/* function as property of object */
				INSTR_WRITE( SGS_SI_GETPROP, funcpos, objpos, proppos );
			}
			else
			{
				SGS_FN_ENTER;
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
			SGS_FN_ENTER;
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
				SGS_FN_ENTER;
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
	rcpos_t var, name, opos = comp_reg_alloc( C );
	rcpos_t regpos = C->fctx->regs;
	SGS_FN_ENTER;
	if( !compile_node_r( C, func, node->child, &var ) ) return 0;
	SGS_FN_ENTER;
	if( !compile_node_r( C, func, node->child->next, &name ) ) return 0;
	INSTR_WRITE( SGS_SI_GETINDEX, opos, var, name );
	comp_reg_unwind( C, regpos );
	if( out )
		*out = opos;
	return 1;
}

static SGSBOOL compile_index_w( SGS_FNTCMP_ARGS, rcpos_t src )
{
	rcpos_t var, name;
	rcpos_t regpos = C->fctx->regs;
	SGS_FN_ENTER;
	if( !compile_node_r( C, func, node->child, &var ) ) return 0;
	SGS_FN_ENTER;
	if( !compile_node_r( C, func, node->child->next, &name ) ) return 0;
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
	rcpos_t var, name;
	rcpos_t regpos = C->fctx->regs;
	SGS_FN_ENTER;
	if( !compile_node_r( C, func, node, &var ) ) return 0;
	SGS_FN_ENTER;
	compile_ident( C, func, node->child, &name );
	INSTR_WRITE( SGS_SI_SETINDEX, var, name, src );
	comp_reg_unwind( C, regpos );
	return 1;
}

static SGSBOOL compile_midxset( SGS_FNTCMP_ARGS, rcpos_t* out, int isprop )
{
	sgs_FTNode* mapi;
	rcpos_t var, name, src;
	rcpos_t regpos = C->fctx->regs, regpos2;
	SGS_FN_ENTER;
	if( !compile_node_r( C, func, node->child, &var ) ) return 0;
	regpos2 = C->fctx->regs;
	mapi = node->child->next->child;
	while( mapi )
	{
		if( *mapi->token == SGS_ST_STRING )
		{
			compile_const( C, func, mapi, &name );
		}
		else
		{
			compile_ident( C, func, mapi, &name );
		}
		mapi = mapi->next;
		
		SGS_FN_ENTER;
		if( !compile_node_r( C, func, mapi, &src ) ) return 0;
		mapi = mapi->next;
		
		INSTR_WRITE( isprop ? SGS_SI_SETPROP : SGS_SI_SETINDEX, var, name, src );
		comp_reg_unwind( C, regpos2 );
	}
	if( out )
		*out = var;
	else
		comp_reg_unwind( C, regpos );
	return 1;
}


static SGSBOOL try_optimize_last_instr_out( SGS_FNTCMP_ARGS, size_t ioff, rcpos_t* out )
{
	rcpos_t pos = -1;
	
	SGS_FN_BEGIN;
	SGS_UNUSED( C );
	
	if( ( node->type != SGS_SFT_IDENT && node->type != SGS_SFT_ARGMT ) || *node->token != SGS_ST_IDENT )
		goto cannot;
	
	/* moved offset 4 to other side of equation to prevent unsigned underflow */
	if( ioff + 4 > func->code.size )
		goto cannot;
	
	ioff = func->code.size - 4;
	
	/* check if closure variable */
	pos = find_varT( &C->fctx->clsr, node->token );
	if( pos >= 0 )
		goto cannot;
	
	/* find the variable output register */
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
	
	/* global variable */
	if( pos < 0 )
		goto cannot;
	
	{
		sgs_instr_t I;
		SGS_AS_( I, func->code.ptr + ioff, sgs_instr_t );
		int op = SGS_INSTR_GET_OP( I ), argB = SGS_INSTR_GET_B( I ), argC = SGS_INSTR_GET_C( I );
		switch( op )
		{
		case SGS_SI_GETVAR: case SGS_SI_GETPROP: case SGS_SI_GETINDEX:
		case SGS_SI_MAKECLSR: case SGS_SI_GETCLSR:
		case SGS_SI_SET: case SGS_SI_CONCAT:
		case SGS_SI_NEGATE: case SGS_SI_BOOL_INV: case SGS_SI_INVERT:
		case SGS_SI_ADD: case SGS_SI_SUB: case SGS_SI_MUL: case SGS_SI_DIV: case SGS_SI_MOD:
		case SGS_SI_AND: case SGS_SI_OR: case SGS_SI_XOR: case SGS_SI_LSH: case SGS_SI_RSH:
		case SGS_SI_SEQ: case SGS_SI_EQ: case SGS_SI_LT: case SGS_SI_LTE:
		case SGS_SI_SNEQ: case SGS_SI_NEQ: case SGS_SI_GT: case SGS_SI_GTE: case SGS_SI_RAWCMP:
			{
				char* dummy0 = NULL;
				unsigned dummy1 = 0;
				if( find_nth_var( &C->fctx->vars, SGS_INSTR_GET_A( I ), &dummy0, &dummy1 ) )
					goto cannot;
			}
			I = SGS_INSTR_MAKE( op, pos, argB, argC );
			memcpy( func->code.ptr + ioff, &I, sizeof(I) );
			if( out )
				*out = pos;
			break;
		case SGS_SI_ARRAY: case SGS_SI_DICT: case SGS_SI_MAP:
			{
				int argE = SGS_INSTR_GET_E( I );
				char* dummy0 = NULL;
				unsigned dummy1 = 0;
				if( find_nth_var( &C->fctx->vars, SGS_INSTR_GET_A( I ), &dummy0, &dummy1 ) )
					goto cannot;
				I = SGS_INSTR_MAKE_EX( op, argE, pos );
				memcpy( func->code.ptr + ioff, &I, sizeof(I) );
				if( out )
					*out = pos;
			}
			break;
		default:
			goto cannot;
		}
	}
	
	SGS_FN_END;
	return 1;
	
cannot:
	SGS_FN_END;
	return 0;
}

static SGSBOOL try_optimize_set_op( SGS_CTX, sgs_CompFunc* func, size_t ioff, rcpos_t ireg )
{
	SGS_FN_BEGIN;
	SGS_UNUSED( C );
	
	/* moved offset 4 to other side of equation to prevent unsigned underflow */
	if( ioff + 4 > func->code.size )
		goto cannot;
	
	ioff = func->code.size - 4;
	
	{
		sgs_instr_t I;
		SGS_AS_( I, func->code.ptr + ioff, sgs_instr_t );
		int op = SGS_INSTR_GET_OP( I ), argB = SGS_INSTR_GET_B( I ), argC = SGS_INSTR_GET_C( I );
		switch( op )
		{
		case SGS_SI_GETVAR: case SGS_SI_GETPROP: case SGS_SI_GETINDEX:
		case SGS_SI_MAKECLSR: case SGS_SI_GETCLSR:
		case SGS_SI_SET: case SGS_SI_CONCAT:
		case SGS_SI_NEGATE: case SGS_SI_BOOL_INV: case SGS_SI_INVERT:
		case SGS_SI_ADD: case SGS_SI_SUB: case SGS_SI_MUL: case SGS_SI_DIV: case SGS_SI_MOD:
		case SGS_SI_AND: case SGS_SI_OR: case SGS_SI_XOR: case SGS_SI_LSH: case SGS_SI_RSH:
		case SGS_SI_SEQ: case SGS_SI_EQ: case SGS_SI_LT: case SGS_SI_LTE:
		case SGS_SI_SNEQ: case SGS_SI_NEQ: case SGS_SI_GT: case SGS_SI_GTE: case SGS_SI_RAWCMP:
			{
				char* dummy0 = NULL;
				unsigned dummy1 = 0;
				if( find_nth_var( &C->fctx->vars, SGS_INSTR_GET_A( I ), &dummy0, &dummy1 ) )
					goto cannot;
			}
			I = SGS_INSTR_MAKE( op, ireg, argB, argC );
			memcpy( func->code.ptr + ioff, &I, sizeof(I) );
			break;
		case SGS_SI_ARRAY: case SGS_SI_DICT: case SGS_SI_MAP:
			{
				int argE = SGS_INSTR_GET_E( I );
				char* dummy0 = NULL;
				unsigned dummy1 = 0;
				if( find_nth_var( &C->fctx->vars, SGS_INSTR_GET_C( I ), &dummy0, &dummy1 ) )
					goto cannot;
				I = SGS_INSTR_MAKE_EX( op, argE, ireg );
				memcpy( func->code.ptr + ioff, &I, sizeof(I) );
			}
			break;
		default:
			goto cannot;
		}
	}
	
	SGS_FN_END;
	return 1;
	
cannot:
	SGS_FN_END;
	return 0;
}

static SGSBOOL compile_node_rrw( SGS_FNTCMP_ARGS, rcpos_t dst )
{
	rcpos_t ireg = -1, bkup = C->fctx->regs;
	size_t newcodestart = func->code.size;
	
	SGS_FN_ENTER;
	if( !compile_node_r( C, func, node, &ireg ) ) return 0;
	
	SGS_FN_ENTER;
	if( !try_optimize_set_op( C, func, newcodestart, dst ) )
	{
		/* just set the contents */
		SGS_FN_ENTER;
		INSTR_WRITE( SGS_SI_SET, dst, ireg, 0 );
	}
	
	comp_reg_unwind( C, bkup );
	
	return 1;
}

static int compile_mconcat( SGS_FNTCMP_ARGS, rcpos_t* arg )
{
	int numch = 0;
	sgs_FTNode* cur = node->child;
	rcpos_t ireg, oreg = comp_reg_alloc( C );
	if( C->state & SGS_MUST_STOP )
		return 0;
	
	/* get source data registers */
	while( cur )
	{
		int32_t bkup = C->fctx->regs;
		
		SGS_FN_ENTER;
		if( !compile_node_r( C, func, cur, &ireg ) )
			return 0;
		INSTR_WRITE( SGS_SI_PUSH, 0, ireg, 0 );
		numch++;
		cur = cur->next;
		
		comp_reg_unwind( C, bkup );
	}
	
	INSTR_WRITE( SGS_SI_MCONCAT, oreg, numch, 0 );
	
	if( arg )
		*arg = oreg;
	return 1;
}

static SGSBOOL compile_oper( SGS_FNTCMP_ARGS, rcpos_t* arg, int out, int expect )
{
	int assign = SGS_ST_OP_ASSIGN( *node->token );
	SGS_FN_BEGIN;
	
	/* Error suppression op */
	if( *node->token == SGS_ST_OP_ERSUP )
	{
		size_t csz;
		INSTR_WRITE( SGS_SI_INT, 0, 0, SGS_INT_ERRSUP_INC );
		csz = func->code.size;
		
		if( out && expect )
		{
			SGS_FN_ENTER;
			if( !compile_node_r( C, func, node->child, arg ) ) goto fail;
		}
		else
		{
			SGS_FN_ENTER;
			if( !compile_node( C, func, node->child ) ) goto fail;
		}
		
		if( func->code.size > csz )
		{
			/* write INT ERRSUP_DEC if any code was written */
			INSTR_WRITE( SGS_SI_INT, 0, 0, SGS_INT_ERRSUP_DEC );
		}
		else
		{
			/* otherwise, remove the already written IN ERRSUP_INC */
			func->code.size -= 4;
			func->lnbuf.size -= 2;
		}
		return 1;
	}
	/* Boolean ops */
	else if( SGS_ST_OP_BOOL( *node->token ) || SGS_ST_OP_FNN( *node->token ) )
	{
		int jin;
		rcpos_t ireg1, ireg2, oreg = 0;
		size_t csz, csz2;
		
		if( !assign )
			oreg = comp_reg_alloc( C );
		if( C->state & SGS_MUST_STOP )
			goto fail;
		
		/* get source data register */
		SGS_FN_ENTER;
		if( !compile_node_r( C, func, node->child, &ireg1 ) ) goto fail;
		
		/* write cond. jump */
		jin = ( *node->token == SGS_ST_OP_BLAND || *node->token == SGS_ST_OP_BLAEQ ) ? SGS_SI_JMPT :
			( ( *node->token == SGS_ST_OP_BLOR || *node->token == SGS_ST_OP_BLOEQ ) ? SGS_SI_JMPF : SGS_SI_JMPN );
		INSTR_WRITE_PCH();
		csz = func->code.size;
		
		/* compile write of value 1 */
		if( assign )
		{
			SGS_FN_ENTER;
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
		SGS_FN_ENTER;
		if( !compile_node_r( C, func, node->child->next, &ireg2 ) ) goto fail;
		
		/* compile write of value 2 */
		if( assign )
		{
			SGS_FN_ENTER;
			if( !compile_node_w( C, func, node->child, ireg2 ) ) goto fail;
		}
		else
		{
			INSTR_WRITE( SGS_SI_SET, oreg, ireg2, 0 );
		}
		
		INSTR_WRITE( SGS_SI_NOP, 0, 0, 0 );
		
		/* fix-up jump 2 */
		{
			sgs_instr_t instr;
			/* WP: instruction limit */
			ptrdiff_t jmp_off = (ptrdiff_t) ( func->code.size - csz2 ) / SGS_INSTR_SIZE;
			instr = SGS_INSTR_MAKE_EX( SGS_SI_JUMP, jmp_off, 0 );
			memcpy( func->code.ptr + csz2 - 4, &instr, sizeof(instr) );
		}
		
		/* re-read from assignments */
		if( arg )
		{
			if( assign )
			{
				SGS_FN_ENTER;
				if( !compile_node_r( C, func, node->child, arg ) ) goto fail;
			}
			else
				*arg = oreg;
		}
	}
	else
	/* Increment / decrement */
	if( *node->token == SGS_ST_OP_INC || *node->token == SGS_ST_OP_DEC )
	{
		rcpos_t ireg, oreg;
		
		/* register with input data */
		SGS_FN_ENTER;
		if( !compile_node_r( C, func, node->child, &ireg ) ) goto fail;
		
		/* output register selection */
		oreg = expect && node->type == SGS_SFT_OPER_P ? comp_reg_alloc( C ) : ireg;
		if( C->state & SGS_MUST_STOP )
			goto fail;
		if( oreg != ireg )
		{
			INSTR_WRITE( SGS_SI_SET, oreg, ireg, 0 );
		}
		
		/* check for errors if this operator generates output */
		if( expect )
		{
			if( expect != 1 )
			{
				QPRINT( "Too many expected outputs for operator" );
				goto fail;
			}
		}
		
		/* write bytecode */
		INSTR_WRITE( *node->token == SGS_ST_OP_INC ? SGS_SI_INC : SGS_SI_DEC, ireg, ireg, 0 );
		
		if( arg )
			*arg = oreg;
		
		/* compile writeback */
		SGS_FN_ENTER;
		if( !compile_node_w( C, func, node->child, ireg ) ) goto fail;
	}
	/* Assignment */
	else if( assign )
	{
		/* 1 operand */
		if( *node->token == SGS_ST_OP_SET )
		{
			rcpos_t ireg;
			size_t isb = func->code.size;
			
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
					SGS_FN_ENTER;
					if( !compile_node_w( C, func, n, freg + i ) ) goto fail;
					
					comp_reg_unwind( C, bkup );
					
					n = n->next;
				}
			}
			else
			{
				/* get source data register */
				SGS_FN_ENTER;
				if( !compile_node_r( C, func, node->child->next, &ireg ) ) goto fail;
				
				SGS_FN_ENTER;
				if( !try_optimize_last_instr_out( C, func, node->child, isb, arg ) )
				{
					/* just set the contents */
					SGS_FN_ENTER;
					if( !compile_node_w( C, func, node->child, ireg ) ) goto fail;
				}
				
				if( arg )
				{
					SGS_FN_ENTER;
					if( !compile_node_r( C, func, node->child, arg ) ) goto fail;
				}
			}
		}
		/* 3+ operands (MCONCAT only) */
		else if( *node->token == SGS_ST_OP_CATEQ && node->child &&
			node->child->next && node->child->next->next )
		{
			rcpos_t oreg;
			SGS_FN_ENTER;
			if( !compile_mconcat( C, func, node, &oreg ) )
				goto fail;
			if( arg )
				*arg = oreg;
			
			/* compile write */
			SGS_FN_ENTER;
			if( !compile_node_w( C, func, node->child, oreg ) ) goto fail;
		}
		/* 2 operands */
		else
		{
			int op;
			size_t isb = func->code.size;
			rcpos_t ireg1, ireg2, oreg = comp_reg_alloc( C );
			if( C->state & SGS_MUST_STOP )
				goto fail;
			
			if( !node->child || !node->child->next )
			{
				QINTERR( 1012 );
				goto fail;
			}
			
			/* get source data registers */
			SGS_FN_ENTER;
			if( !compile_node_r( C, func, node->child, &ireg1 ) ) goto fail;
			SGS_FN_ENTER;
			if( !compile_node_r( C, func, node->child->next, &ireg2 ) ) goto fail;
			
			/* compile op */
			op = op_pick_opcode( *node->token, 1 );
			INSTR_WRITE( op, oreg, ireg1, ireg2 );
			
			SGS_FN_ENTER;
			if( !try_optimize_last_instr_out( C, func, node->child, isb, arg ) )
			{
				/* just set the contents */
				SGS_FN_ENTER;
				if( !compile_node_w( C, func, node->child, oreg ) ) goto fail;
			}
			
			if( arg )
			{
				SGS_FN_ENTER;
				if( !compile_node_r( C, func, node->child, arg ) ) goto fail;
			}
		}
	}
	/* Any other */
	else
	{
		rcpos_t ireg1, ireg2, oreg;
		
		if( expect > 1 )
		{
			QPRINT( "Too many expected outputs for operator" );
			goto fail;
		}
		
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
				if( arg )
					*arg = oreg;
			}
			else
				oreg = *arg;
			
			/* get source data registers */
			SGS_FN_ENTER;
			if( !compile_node_r( C, func, node->child, &ireg1 ) ) goto fail;
			
			if( node->child->next->type == SGS_SFT_IDENT )
				compile_ident( C, func, node->child->next, &ireg2 );
			else
			{
				SGS_FN_ENTER;
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
		/* 3+ operands (MCONCAT only) */
		else if( *node->token == SGS_ST_OP_CAT && node->child &&
			node->child->next && node->child->next->next )
		{
			SGS_FN_ENTER;
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
			SGS_FN_ENTER;
			if( !compile_node_r( C, func, node->child, &ireg1 ) ) goto fail;
			if( node->child->next )
			{
				SGS_FN_ENTER;
				if( !compile_node_r( C, func, node->child->next, &ireg2 ) ) goto fail;
			}
			
			if( arg )
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
	
	SGS_FN_END;
	return 1;
	
fail:
	C->state |= SGS_HAS_ERRORS;
	SGS_FN_END;
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
				QPRINT( "Max. jump limit exceeded (32767 instructions) @ break/continue; reduce size of loops" );
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
			char* varstr = NULL;
			unsigned varstrlen;
			int result, which;
			result = find_nth_var( &C->fctx->vars, i, &varstr, &varstrlen );
			sgs_BreakIf( !result );
			if( !result )
				continue;
			which = find_var( &C->fctx->clsr, varstr, varstrlen );
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
	
	SGS_FN_ENTER;
	if( !preparse_clsrlists( C, func, node ) ) return 0;
	
	SGS_FN_ENTER;
	if( !preparse_varlists( C, func, node ) ) return 0;
	args += func->gotthis;
	
	SGS_FN_ENTER;
	if( !preparse_funcorder( C, func, node ) ) return 0;
	
	SGS_FN_ENTER;
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
	fctx_dump( C, C->fctx );
	sgs_ErrWritef( C, "function (this=%s args=%d tmp=%d clsr=%d inclsr=%d)\n",
		func->gotthis ? "Y" : "n", func->numargs, func->numtmp, func->numclsr, func->inclsr );
	sgsBC_DumpEx( C, func->consts.ptr, func->consts.size, func->code.ptr, func->code.size,
		(sgs_LineNum*) func->lnbuf.ptr );
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
	SGS_FN_ENTER;
	if( !preparse_closures( C, func, n_uselist, 0 ) ) { goto fail; }

	C->fctx = fctx;

	SGS_FN_ENTER;
	if( !preparse_closures( C, nf, n_uselist, 1 ) ) { goto fail; }

	SGS_FN_ENTER;
	if( !preparse_arglist( C, nf, n_arglist ) ) { goto fail; }
	
	if( !compile_fn_base( C, nf, n_body, fctx->regs ) ) goto fail;
	
	C->fctx = bkfctx;

	{
		sgs_MemBuf ffn = sgs_membuf_create();
		if( n_name )
			rpts( &ffn, C, n_name );
		*out = BC_CONSTENC( add_const_f( C, func, nf, ffn.ptr, ffn.size, sgsT_LineNum( node->token ) ) );
		sgs_membuf_destroy( &ffn, C );
		
		if( fctx->outclsr > 0 )
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
	SGS_FN_BEGIN;
	switch( node->type )
	{
	case SGS_SFT_IDENT:
	case SGS_SFT_KEYWORD:
		SGS_FN_HIT( "W_IDENT" );
		if( !compile_ident_w( C, func, node, src ) ) goto fail;
		break;

	case SGS_SFT_CONST:
		SGS_FN_HIT( "W_CONST" );
		QPRINT( "Cannot write to constants" );
		goto fail;
	case SGS_SFT_FUNC:
		SGS_FN_HIT( "W_FUNC" );
		QPRINT( "Cannot write to constants" );
		goto fail;
	case SGS_SFT_ARRLIST:
		SGS_FN_HIT( "W_ARRLIST" );
		QPRINT( "Cannot write to constants" );
		goto fail;
	case SGS_SFT_DCTLIST:
		SGS_FN_HIT( "W_DCTLIST" );
		QPRINT( "Cannot write to constants" );
		goto fail;
	case SGS_SFT_MAPLIST:
		SGS_FN_HIT( "W_MAPLIST" );
		QPRINT( "Cannot write to constants" );
		goto fail;

	case SGS_SFT_OPER:
	case SGS_SFT_OPER_P:
		SGS_FN_HIT( "W_OPER" );
		if( !compile_oper( C, func, node, &src, 0, 1 ) ) goto fail;
		break;

	case SGS_SFT_FCALL:
	case SGS_SFT_THRCALL:
	case SGS_SFT_STHCALL:
	case SGS_SFT_NEWCALL:
		SGS_FN_HIT( "W_FCALL" );
		if( !compile_fcall( C, func, node, NULL, 0 ) ) goto fail;
		break;

	case SGS_SFT_INDEX:
		SGS_FN_HIT( "W_INDEX" );
		if( !compile_index_w( C, func, node, src ) ) goto fail;
		break;
		
	case SGS_SFT_MIDXSET:
		SGS_FN_HIT( "MIDXSET" );
		QPRINT( "Cannot write to multi-index-set expression" );
		break;
		
	case SGS_SFT_MPROPSET:
		SGS_FN_HIT( "MPROPSET" );
		QPRINT( "Cannot write to multi-property-set expression" );
		break;
		
	case SGS_SFT_EXPLIST:
		SGS_FN_HIT( "W_EXPLIST" );
		QPRINT( "Expression writes only allowed with function call reads" );
		goto fail;
		
	case SGS_SFT_CLSPFX:
		SGS_FN_HIT( "W_CLSPFX" );
		if( !compile_clspfx_w( C, func, node, src ) ) goto fail;
		break;

	default:
		QINTERR( 1003 );
		goto fail;
	}
	SGS_FN_END;
	return 1;

fail:
	C->state |= SGS_HAS_ERRORS;
	SGS_FN_END;
	return 0;
}
static SGSBOOL compile_node_r( SGS_FNTCMP_ARGS, rcpos_t* out )
{
	SGS_FN_BEGIN;
	switch( node->type )
	{
	case SGS_SFT_IDENT:
	case SGS_SFT_KEYWORD:
	case SGS_SFT_CLSPFX:
		SGS_FN_HIT( "R_IDENT" );
		if( !compile_ident_r( C, func, node, out ) ) goto fail;
		break;

	case SGS_SFT_CONST:
		SGS_FN_HIT( "R_CONST" );
		if( !compile_const( C, func, node, out ) ) goto fail;
		break;
	case SGS_SFT_FUNC:
		SGS_FN_HIT( "R_FUNC" );
		if( !compile_func( C, func, node, out ) ) goto fail;
		break;
	case SGS_SFT_ARRLIST:
		SGS_FN_HIT( "R_ARRLIST" );
		{
			rcpos_t pos = 0;
			int args = 0, off = 0;
			sgs_FTNode* n = node->child;
			while( n )
			{
				args++;
				n = n->next;
			}
			pos = comp_reg_alloc( C );
			INSTR_WRITE_EX( SGS_SI_ARRAY, args, pos );
			n = node->child;
			while( n )
			{
				rcpos_t bkup = C->fctx->regs, vpos = 0;
				SGS_FN_ENTER;
				if( !compile_node_r( C, func, n, &vpos ) )
					goto fail;
				INSTR_WRITE( SGS_SI_ARRPUSH, pos, 0, vpos );
				comp_reg_unwind( C, bkup );
				off++;
				n = n->next;
			}
			*out = pos;
		}
		break;
	case SGS_SFT_DCTLIST:
	case SGS_SFT_MAPLIST:
		SGS_FN_HIT( "R_(DCT|MAP)LIST" );
		{
			int args = 0;
			sgs_FTNode* n = node->child;
			rcpos_t pos = comp_reg_alloc( C ), kpos, vpos, bkup;
			bkup = C->fctx->regs;
			INSTR_WRITE_EX( node->type == SGS_SFT_DCTLIST ? SGS_SI_DICT : SGS_SI_MAP, 0, pos );
			while( n )
			{
				if( args % 2 == 0 )
				{
					kpos = 0;
					if( n->type != SGS_SFT_ARGMT )
					{
						SGS_FN_ENTER;
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
					vpos = 0;
					SGS_FN_ENTER;
					if( !compile_node_r( C, func, n, &vpos ) )
						goto fail;
				}
				if( args % 2 == 1 )
				{
					INSTR_WRITE( SGS_SI_SETINDEX, pos, kpos, vpos );
					comp_reg_unwind( C, bkup );
				}
				args++;
				n = n->next;
			}
			sgs_BreakIf( args % 2 != 0 );
			*out = pos;
		}
		break;

	case SGS_SFT_OPER:
	case SGS_SFT_OPER_P:
		SGS_FN_HIT( "R_OPER" );
		if( !compile_oper( C, func, node, out, 1, 1 ) ) goto fail;
		break;

	case SGS_SFT_FCALL:
	case SGS_SFT_THRCALL:
	case SGS_SFT_STHCALL:
	case SGS_SFT_NEWCALL:
		SGS_FN_HIT( "R_FCALL" );
		if( !compile_fcall( C, func, node, out, 1 ) ) goto fail;
		break;

	case SGS_SFT_INDEX:
		SGS_FN_HIT( "R_INDEX" );
		if( !compile_index_r( C, func, node, out ) ) goto fail;
		break;
		
	case SGS_SFT_MIDXSET:
		SGS_FN_HIT( "MIDXSET" );
		if( !compile_midxset( C, func, node, out, 0 ) ) goto fail;
		break;
		
	case SGS_SFT_MPROPSET:
		SGS_FN_HIT( "MPROPSET" );
		if( !compile_midxset( C, func, node, out, 1 ) ) goto fail;
		break;
		
	case SGS_SFT_EXPLIST:
		SGS_FN_HIT( "R_EXPLIST" );
		{
			sgs_FTNode* n = node->child;
			if( !n )
			{
				QPRINT( "Empty expression found" );
				goto fail;
			}
			while( n )
			{
				SGS_FN_ENTER;
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
	SGS_FN_END;
	return 1;

fail:
	SGS_FN_END;
	return 0;
}

static SGSBOOL compile_for_explist( SGS_FNTCMP_ARGS, rcpos_t* out )
{
	sgs_FTNode* n;

	SGS_FN_BEGIN;

	if( node->type != SGS_SFT_EXPLIST )
	{
		QINTERR( 1004 );
		goto fail;
	}

	SGS_FN_HIT( "Rs_EXPLIST" );

	n = node->child;
	while( n )
	{
		SGS_FN_ENTER;
		if( !compile_node_r( C, func, n, out ) )
			goto fail;
		n = n->next;
	}

	SGS_FN_END;
	return 1;

fail:
	SGS_FN_END;
	return 0;
}

static SGSBOOL compile_node( SGS_FNTCMP_ARGS )
{
	SGS_FN_BEGIN;

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
		SGS_FN_HIT( "OPERATOR" );
		if( !compile_oper( C, func, node, NULL, 1, 0 ) ) goto fail;
		break;

	case SGS_SFT_INDEX:
		SGS_FN_HIT( "INDEX" );
		if( !compile_index_r( C, func, node, NULL ) ) goto fail;
		break;
		
	case SGS_SFT_MIDXSET:
		SGS_FN_HIT( "MIDXSET" );
		if( !compile_midxset( C, func, node, NULL, 0 ) ) goto fail;
		break;
		
	case SGS_SFT_MPROPSET:
		SGS_FN_HIT( "MPROPSET" );
		if( !compile_midxset( C, func, node, NULL, 1 ) ) goto fail;
		break;
		
	case SGS_SFT_FCALL:
	case SGS_SFT_THRCALL:
	case SGS_SFT_STHCALL:
	case SGS_SFT_NEWCALL:
		SGS_FN_HIT( "FCALL" );
		if( !compile_fcall( C, func, node, NULL, 0 ) ) goto fail;
		break;

	case SGS_SFT_EXPLIST:
		SGS_FN_HIT( "EXPLIST" );
		{
			sgs_FTNode* n = node->child;
			while( n )
			{
				SGS_FN_ENTER;
				if( !compile_node( C, func, n ) )
					goto fail;
				n = n->next;
			}
		}
		break;

	case SGS_SFT_RETURN:
		SGS_FN_HIT( "RETURN" );
		{
			rcpos_t regstate = C->fctx->regs;
			sgs_FTNode* n = node->child;
			if( n && n->next == NULL )
			{
				/* one value */
				rcpos_t arg = 0;
				SGS_FN_ENTER;
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
					rcpos_t arg = 0;
					SGS_FN_ENTER;
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
		SGS_FN_HIT( "BLOCK" );
		node = node->child;
		BLOCK_BEGIN;
		while( node )
		{
			rcpos_t regstate = C->fctx->regs;
			SGS_FN_ENTER;
			if( !compile_node( C, func, node ) ) goto fail;
			node = node->next;
			comp_reg_unwind( C, regstate );
		}
		BLOCK_END;
		break;

	case SGS_SFT_IFELSE:
		SGS_FN_HIT( "IF/ELSE" );
		{
			rcpos_t arg = 0;
			rcpos_t regstate = C->fctx->regs;
			SGS_FN_ENTER;
			if( !compile_node_r( C, func, node->child, &arg ) ) goto fail;
			comp_reg_unwind( C, regstate );
			INSTR_WRITE_PCH();
			{
				size_t jp1, jp2 = 0, jp3 = 0;
				jp1 = func->code.size;

				regstate = C->fctx->regs;
				SGS_FN_ENTER;
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
							QPRINT( "Max. jump limit exceeded (32767 instructions) @ if/else; reduce size of construct" );
							goto fail;
						}
						instr = SGS_INSTR_MAKE_EX( SGS_SI_JMPF, jmp_off, arg );
						memcpy( func->code.ptr + jp1 - 4, &instr, sizeof(instr) );
					}

					regstate = C->fctx->regs;
					SGS_FN_ENTER;
					if( !compile_node( C, func, node->child->next->next ) ) goto fail;
					jp3 = func->code.size;
					{
						sgs_instr_t instr;
						/* WP: jump limit */
						ptrdiff_t jmp_off = (ptrdiff_t) ( jp3 - jp2 ) / SGS_INSTR_SIZE;
						if( over_limit( jmp_off, 32767 ) )
						{
							QPRINT( "Max. jump limit exceeded (32767 instructions) @ if/else; reduce size of construct" );
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
						QPRINT( "Max. jump limit exceeded (32767 instructions) @ if/else; reduce size of construct" );
						goto fail;
					}
					instr = SGS_INSTR_MAKE_EX( SGS_SI_JMPF, jmp_off, arg );
					memcpy( func->code.ptr + jp1 - 4, &instr, sizeof(instr) );
				}
			}
		}
		break;

	case SGS_SFT_WHILE:
		SGS_FN_HIT( "WHILE" );
		{
			size_t codesize;
			rcpos_t arg = -1;
			rcpos_t regstate = C->fctx->regs;
			C->fctx->loops++;
			codesize = func->code.size;
			SGS_FN_ENTER;
			if( !compile_node_r( C, func, node->child, &arg ) ) goto fail; /* test */
			comp_reg_unwind( C, regstate );
			INSTR_WRITE_PCH();
			{
				ptrdiff_t off;
				size_t jp1, jp2 = 0;
				jp1 = func->code.size;

				LOOP_BEGIN;
				regstate = C->fctx->regs;
				SGS_FN_ENTER;
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
					QPRINT( "Max. jump limit exceeded (32767 instructions) @ while; reduce size of loop" );
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
		SGS_FN_HIT( "DO/WHILE" );
		{
			size_t codesize;
			rcpos_t regstate = C->fctx->regs;
			rcpos_t arg = -1;
			ptrdiff_t off;
			C->fctx->loops++;
			codesize = func->code.size;
			{
				LOOP_BEGIN;
				SGS_FN_ENTER;
				if( !compile_node( C, func, node->child->next ) ) goto fail; /* while */
				comp_reg_unwind( C, regstate );
				LOOP_END;

				if( !compile_breaks( C, func, node, 1 ) )
					goto fail;
			}
			SGS_FN_ENTER;
			if( !compile_node_r( C, func, node->child, &arg ) ) goto fail; /* test */
			comp_reg_unwind( C, regstate );
			/* WP: jump limit */
			off = (ptrdiff_t) ( codesize - func->code.size ) / SGS_INSTR_SIZE - 1;
			if( over_limit( off, 32767 ) )
			{
				QPRINT( "Max. jump limit exceeded (32767 instructions) @ do/while; reduce size of loop" );
				goto fail;
			}
			INSTR_WRITE_EX( SGS_SI_JMPT, off, arg );
			if( !compile_breaks( C, func, node, 0 ) )
				goto fail;
			C->fctx->loops--;
		}
		break;

	case SGS_SFT_FOR:
		SGS_FN_HIT( "FOR" );
		{
			size_t codesize;
			rcpos_t regstate = C->fctx->regs;
			rcpos_t arg = -1;
			C->fctx->loops++;
			SGS_FN_ENTER;
			if( !compile_node( C, func, node->child ) ) goto fail; /* init */
			comp_reg_unwind( C, regstate );
			codesize = func->code.size;
			SGS_FN_ENTER;
			if( !compile_for_explist( C, func, node->child->next, &arg ) ) goto fail; /* test */
			comp_reg_unwind( C, regstate );
			if( arg != -1 )
			{
				INSTR_WRITE_PCH();
			}
			{
				ptrdiff_t off;
				size_t jp1, jp2 = 0;
				jp1 = func->code.size;

				LOOP_BEGIN;
				SGS_FN_ENTER;
				if( !compile_node( C, func, node->child->next->next->next ) ) goto fail; /* block */
				comp_reg_unwind( C, regstate );
				LOOP_END;

				if( !compile_breaks( C, func, node, 1 ) )
					goto fail;
				SGS_FN_ENTER;
				if( !compile_node( C, func, node->child->next->next ) ) goto fail; /* incr */
				comp_reg_unwind( C, regstate );

				jp2 = func->code.size;
				/* WP: jump limit */
				off = (ptrdiff_t) ( codesize - jp2 ) / SGS_INSTR_SIZE - 1;
				if( over_limit( off, 32767 ) )
				{
					QPRINT( "Max. jump limit exceeded (32767 instructions) @ for; reduce size of loop" );
					goto fail;
				}
				INSTR_WRITE_EX( SGS_SI_JUMP, off, 0 );
				if( arg != -1 )
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
		
	case SGS_SFT_FOREACH:
		SGS_FN_HIT( "FOREACH" );
		{
			size_t codesize, jp1, jp2;
			rcpos_t var, iter, key = -1, val = -1;
			rcpos_t regstate, regstate2;
			regstate2 = C->fctx->regs;
			
			/* init */
			var = -1;
			SGS_FN_ENTER;
			if( !compile_node_r( C, func, node->child->next->next, &var ) ) goto fail; /* get variable */
			
			iter = comp_reg_alloc( C );
			if( node->child->type != SGS_SFT_NULL ) key = comp_reg_alloc( C );
			if( node->child->next->type != SGS_SFT_NULL ) val = comp_reg_alloc( C );
			regstate = C->fctx->regs;
			C->fctx->loops++;
			
			INSTR_WRITE( SGS_SI_FORPREP, iter, var, 0 );
			comp_reg_unwind( C, regstate );
			
			/* iterate */
			codesize = func->code.size;
			INSTR_WRITE_PCH();
			jp1 = func->code.size;
			INSTR_WRITE( SGS_SI_FORLOAD, iter, key, val );
			
			{
				ptrdiff_t off = 0;
				
				/* write to key variable */
				if( node->child->type != SGS_SFT_NULL && !compile_ident_w( C, func, node->child, key ) ) goto fail;
				
				/* write to value variable */
				if( node->child->next->type != SGS_SFT_NULL && !compile_ident_w( C, func, node->child->next, val ) ) goto fail;
				
				LOOP_BEGIN;
				SGS_FN_ENTER;
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
					QPRINT( "Max. jump limit exceeded (32767 instructions) @ foreach; reduce size of loop" );
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
		SGS_FN_HIT( "BREAK" );
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
		SGS_FN_HIT( "CONTINUE" );
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
		SGS_FN_HIT( "DEFER" );
		fctx_defer_add( C, node->child );
		break;
	
	case SGS_SFT_FUNC:
		SGS_FN_HIT( "FUNC" );
		{
			sgs_FTNode* n_name;
			rcpos_t pos;
			SGS_FN_ENTER;
			if( !compile_func( C, func, node, &pos ) ) goto fail;
			n_name = node->child->next->next->next;

			if( n_name )
			{
				SGS_FN_ENTER;
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
		SGS_FN_HIT( "CLASS" );
		{
			sgs_FTNode* name = node->child;
			sgs_FTNode* it = name->next;
			rcpos_t regstate = C->fctx->regs;
			
			/* create class */
			rcpos_t clsvar, r_name, r_inhname, vname, vsrc;
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
						compile_ident( C, func, vn, &vname );
						if( vn->child )
						{
							SGS_FN_ENTER;
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
		SGS_FN_HIT( node->type == SGS_SFT_VARLIST ? "VARLIST" : "GLOBALVARLIST" );
		{
			rcpos_t regstate = C->fctx->regs;
			sgs_FTNode* pp = node->child;
			while( pp )
			{
				if( pp->child )
				{
					rcpos_t arg = -1;
					size_t lastsize = func->code.size;
					if( !compile_node_r( C, func, pp->child, &arg ) ) goto fail;
					if( !pp->token || *pp->token != SGS_ST_IDENT ) goto fail;
					if( node->type != SGS_SFT_VARLIST || !try_optimize_last_instr_out( C, func, pp, lastsize, NULL ) )
					{
						compile_ident_w( C, func, pp, arg );
					}
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

	SGS_FN_END;
	return 1;

fail:
	SGS_FN_END;
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
		sgs_iFunc* outfn = sgsBC_ConvertFunc( C, &fctx->cfunc, "<main>", 6, 0 );
		fctx_destroy( C, fctx );
		return outfn;
	}

fail:
	C->fctx = NULL;
	fctx_destroy( C, fctx );
	C->state |= SGS_HAS_ERRORS;
	return NULL;
}

void sgsBC_DumpEx( SGS_CTX, const char* constptr, size_t constsize,
	const char* codeptr, size_t codesize, const sgs_LineNum* lines )
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
		codesize / sizeof( sgs_instr_t ), SGS_ASSUME_ALIGNED_CONST( codeptr, sgs_instr_t ), lines );
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

