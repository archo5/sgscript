

#define SGS_INTERNAL
#define SGS_REALLY_INTERNAL

#include "sgs_int.h"



static int is_keyword( TokenList tok, const char* text )
{
	return *tok == ST_KEYWORD &&
		tok[ 1 ] == strlen( text ) &&
		strncmp( (const char*) tok + 2, text, tok[ 1 ] ) == 0;
}


/* register allocation */

static SGS_INLINE int16_t comp_reg_alloc( SGS_CTX )
{
	int out = C->fctx->regs++;
	if( out > 0xff )
	{
		C->state |= SGS_HAS_ERRORS | SGS_MUST_STOP;
		sgs_Printf( C, SGS_ERROR, "Max. register count exceeded" );
	}
	return out;
}

static SGS_INLINE void comp_reg_unwind( SGS_CTX, int32_t pos )
{
	if( C->fctx->regs > C->fctx->lastreg )
		C->fctx->lastreg = C->fctx->regs;
	C->fctx->regs = pos;
}


static sgs_CompFunc* make_compfunc( SGS_CTX )
{
	sgs_CompFunc* func = sgs_Alloc( sgs_CompFunc );
	func->consts = membuf_create();
	func->code = membuf_create();
	func->lnbuf = membuf_create();
	func->gotthis = FALSE;
	func->numargs = 0;
	return func;
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

static sgs_FuncCtx* fctx_create( SGS_CTX )
{
	sgs_FuncCtx* fctx = sgs_Alloc( sgs_FuncCtx );
	fctx->func = TRUE;
	fctx->regs = 0;
	fctx->lastreg = -1;
	fctx->vars = membuf_create();
	fctx->gvars = membuf_create();
	fctx->clsr = membuf_create();
	fctx->inclsr = 0;
	fctx->outclsr = 0;
	fctx->loops = 0;
	fctx->binfo = NULL;
	membuf_appbuf( &fctx->gvars, C, "_G=", 3 );
	return fctx;
}

static void fctx_destroy( SGS_CTX, sgs_FuncCtx* fctx )
{
	while( fctx->binfo )
		fctx_binfo_rem( C, fctx, NULL );
	membuf_destroy( &fctx->vars, C );
	membuf_destroy( &fctx->gvars, C );
	membuf_destroy( &fctx->clsr, C );
	sgs_Dealloc( fctx );
}

#if SGS_DUMP_BYTECODE || ( SGS_DEBUG && SGS_DEBUG_DATA )
static void fctx_dumpvarlist( MemBuf* mb )
{
	char* p = mb->ptr, *pend = mb->ptr + mb->size;
	while( p < pend )
	{
		if( *p == '=' )
		{
			if( p < pend - 1 )
				printf( ", " );
		}
		else
			putchar( *p );
		p++;
	}
}
static void fctx_dump( sgs_FuncCtx* fctx )
{
	printf( "Type: %s\n", fctx->func ? "Function" : "Main code" );
	printf( "Globals: " ); fctx_dumpvarlist( &fctx->gvars ); printf( "\n" );
	printf( "Variables: " ); fctx_dumpvarlist( &fctx->vars ); printf( "\n" );
	printf( "Closures in=%d out=%d : ", fctx->inclsr, fctx->outclsr );
	fctx_dumpvarlist( &fctx->clsr ); printf( "\n" );
}
#endif


static void dump_rcpos( int arg )
{
	char rc = CONSTVAR( arg ) ? 'C' : 'R';
	arg = CONSTDEC( arg );
	printf( "%c%d", rc, arg );
}
static void dump_opcode_a( const char* name, instr_t I )
{
	printf( "%s R%d <= ", name, INSTR_GET_A( I ) );
	dump_rcpos( INSTR_GET_B( I ) );
	printf( ", " );
	dump_rcpos( INSTR_GET_C( I ) );
}
static void dump_opcode_b( const char* name, instr_t I )
{
	printf( "%s R%d <= ", name, INSTR_GET_A( I ) );
	dump_rcpos( INSTR_GET_B( I ) );
}
static void dump_opcode_b1( const char* name, instr_t I )
{
	printf( "%s ", name );
	dump_rcpos( INSTR_GET_B( I ) );
	printf( " <= " );
	dump_rcpos( INSTR_GET_C( I ) );
}
static void dump_opcode( instr_t* ptr, int32_t count )
{
	instr_t* pend = ptr + count;
	instr_t* pbeg = ptr;
	while( ptr < pend )
	{
		instr_t I = *ptr++;
		int op = INSTR_GET_OP( I ), argA = INSTR_GET_A( I ),
			argB = INSTR_GET_B( I ), argC = INSTR_GET_C( I ),
			argE = INSTR_GET_E( I );

		printf( "\t%04d |  ", (int)( ptr - pbeg - 1 ) );

		switch( op )
		{
#define DOP_A( wat ) case SI_##wat: dump_opcode_a( #wat, I ); break;
#define DOP_B( wat ) case SI_##wat: dump_opcode_b( #wat, I ); break;
		case SI_NOP: printf( "NOP   " ); break;

		case SI_PUSH: printf( "PUSH " ); dump_rcpos( argB ); break;
		case SI_PUSHN: printf( "PUSH_NULLS %d", argA ); break;
		case SI_POPN: printf( "POP_N %d", argA ); break;
		case SI_POPR: printf( "POP_REG R%d", argA ); break;

		case SI_RETN: printf( "RETURN %d", argA ); break;
		case SI_JUMP: printf( "JUMP %d", (int) (int16_t) argE ); break;
		case SI_JMPT: printf( "JMP_T " ); dump_rcpos( argC );
			printf( ", %d", (int) (int16_t) argE ); break;
		case SI_JMPF: printf( "JMP_F " ); dump_rcpos( argC );
			printf( ", %d", (int) (int16_t) argE ); break;
		case SI_CALL: printf( "CALL args:%d%s expect:%d func:",
			argB & 0xff, ( argB & 0x100 ) ? ",method" : "", argA );
			dump_rcpos( argC ); break;

		case SI_FORPREP: printf( "FOR_PREP " ); dump_rcpos( argA );
			printf( " <= " ); dump_rcpos( argB ); break;
		case SI_FORLOAD: printf( "FOR_LOAD " ); dump_rcpos( argA );
			printf( " => " ); dump_rcpos( argB );
			printf( ", " ); dump_rcpos( argC ); break;
		case SI_FORJUMP: printf( "FOR_JUMP " ); dump_rcpos( argC );
			printf( ", %d", (int) (int16_t) argE ); break;

		case SI_LOADCONST: printf( "LOADCONST " ); dump_rcpos( argC );
			printf( " <= C%d", argE ); break;

		DOP_B( GETVAR );
		case SI_SETVAR: dump_opcode_b1( "SETVAR", I ); break;
		DOP_A( GETPROP );
		DOP_A( SETPROP );
		DOP_A( GETINDEX );
		DOP_A( SETINDEX );

		case SI_GENCLSR: printf( "GEN_CLSR %d", argA ); break;
		case SI_PUSHCLSR: printf( "PUSH_CLSR %d", argA ); break;
		case SI_MAKECLSR: printf( "MAKE_CLSR " ); dump_rcpos( argA );
			printf( " <= " ); dump_rcpos( argB );
			printf( " [%d]", argC ); break;
		case SI_GETCLSR: printf( "GET_CLSR " ); dump_rcpos( argA );
			printf( " <= CL%d", argB ); break;
		case SI_SETCLSR: printf( "SET_CLSR CL%d <= ", argB );
			dump_rcpos( argC ); break;

		DOP_B( SET );
		DOP_B( CLONE );
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
#undef DOP_A
#undef DOP_B

		case SI_ARRAY:
			printf( "ARRAY args:%d output:", argB );
			dump_rcpos( argA ); break;
		case SI_DICT:
			printf( "DICT args:%d output:", argB );
			dump_rcpos( argA ); break;

		default:
			printf( "<error> \t\t(op=%d A=%d B=%d C=%d E=%d)",
				op, argA, argB, argC, argE ); break;
		}
		printf( "\n" );
	}
}


static int find_var( MemBuf* S, char* str, int len )
{
	char* ptr = S->ptr;
	char* pend = ptr + S->size;
	const char* cstr = str;
	int difs = 0, at = 0, left = len;

	while( ptr < pend )
	{
		if( *ptr == '=' )
		{
			if( difs == 0 && !left )
				return at;
			difs = 0;
			cstr = str;
			left = len;
			ptr++;
			at++;
		}
		else
		{
			difs += abs( *cstr - *ptr );
			ptr += *ptr != '=';
			cstr += ( left -= 1 ) > 0;
		}
	}
	return -1;
}

static int find_nth_var( MemBuf* S, int which, char** outstr, int* outlen )
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
	*outlen = ptr - *outstr;
	return 1;
}

static int add_var( MemBuf* S, SGS_CTX, char* str, int len )
{
	int pos = find_var( S, str, len );
	if( pos < 0 )
	{
		membuf_appbuf( S, C, str, len );
		membuf_appchr( S, C, '=' );
		return TRUE;
	}
	return FALSE;
}

#define find_varT( S, tok ) \
	find_var( S, (char*) (tok) + 2, tok[1] )
#define add_varT( S, C, tok ) \
	add_var( S, C, (char*) (tok) + 2, tok[1] )

static int preadd_thisvar( MemBuf* S, SGS_CTX )
{
	int pos = find_var( S, "this", 4 );
	if( pos < 0 )
	{
		membuf_insbuf( S, C, 0, "this=", 5 );
		return TRUE;
	}
	return FALSE;
}


/* simplifies writing code */
static void add_instr( sgs_CompFunc* func, SGS_CTX, FTNode* node, instr_t I )
{
	uint16_t ln = sgsT_LineNum( node->token );
	membuf_appbuf( &func->lnbuf, C, &ln, sizeof( ln ) );
	membuf_appbuf( &func->code, C, &I, sizeof( I ) );
}
#define INSTR_N( i, n ) add_instr( func, C, n, i )
#define INSTR( i )      INSTR_N( i, node )
#define INSTR_WRITE( op, a, b, c ) INSTR( INSTR_MAKE( op, a, b, c ) )
#define INSTR_WRITE_EX( op, ex, c ) INSTR( INSTR_MAKE_EX( op, ex, c ) )
#define INSTR_WRITE_PCH() INSTR_WRITE( 63, 0, 0, 0 )

#define QPRINT( str ) sgs_Printf( C, SGS_ERROR, "[line %d] " str, sgsT_LineNum( node->token ) )



static int preparse_varlists( SGS_CTX, sgs_CompFunc* func, FTNode* node );

static int preparse_varlist( SGS_CTX, sgs_CompFunc* func, FTNode* node )
{
	int ret = TRUE;
	node = node->child;
	while( node )
	{
		if( node->type != SFT_IDENT && node->type != SFT_KEYWORD && node->type != SFT_ARGMT )
			goto cont; /* compatibility with explists */
		if( find_varT( &C->fctx->clsr, node->token ) >= 0 )
			goto cont; /* closure */
		if( find_varT( &C->fctx->gvars, node->token ) >= 0 )
		{
			QPRINT( "Variable storage redefined: global -> local" );
			return FALSE;
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

static int preparse_gvlist( SGS_CTX, sgs_CompFunc* func, FTNode* node )
{
	int ret = TRUE;
	node = node->child;
	while( node )
	{
		if( find_varT( &C->fctx->clsr, node->token ) >= 0 )
		{
			QPRINT( "Variable storage redefined: closure -> global" );
			return FALSE;
		}
		if( find_varT( &C->fctx->vars, node->token ) >= 0 )
		{
			QPRINT( "Variable storage redefined: local -> global" );
			return FALSE;
		}
		add_varT( &C->fctx->gvars, C, node->token );
		if( node->child )
			ret &= preparse_varlists( C, func, node );
		node = node->next;
	}
	return ret;
}

static int preparse_varlists( SGS_CTX, sgs_CompFunc* func, FTNode* node )
{
	int ret = 1;
	if( node->type == SFT_VARLIST )
		ret &= preparse_varlist( C, func, node );
	else if( node->type == SFT_GVLIST )
		ret &= preparse_gvlist( C, func, node );
	else if( node->token && is_keyword( node->token, "this" ) )
	{
		func->gotthis = TRUE;
		if( preadd_thisvar( &C->fctx->vars, C ) )
			comp_reg_alloc( C );
	}
	else if( node->type == SFT_OPER )
	{
		if( ST_OP_ASSIGN( *node->token ) && node->child )
		{
			if( node->child->type == SFT_IDENT )
			{
				/* add_var calls find_var internally but - GVARS vs VARS - note the difference */
				if( find_varT( &C->fctx->gvars, node->child->token ) == -1 &&
					find_varT( &C->fctx->clsr, node->child->token ) == -1 &&
					add_varT( &C->fctx->vars, C, node->child->token ) )
					comp_reg_alloc( C );
			}
			if( node->child->type == SFT_EXPLIST )
				ret &= preparse_varlist( C, func, node->child );
		}
		ret &= preparse_varlists( C, func, node->child );
	}
	else if( node->type == SFT_FOREACH )
	{
		if( find_varT( &C->fctx->gvars, node->token ) >= 0 )
		{
			QPRINT( "Variable storage redefined (foreach key variable cannot be global): global -> local" );
			ret = FALSE;
		}
		else
		{
			if( node->child->type != SFT_NULL && 
				add_varT( &C->fctx->vars, C, node->child->token ) )
				comp_reg_alloc( C );
			if( node->child->next->type != SFT_NULL && 
				add_varT( &C->fctx->vars, C, node->child->next->token ) )
				comp_reg_alloc( C );
		}

		ret &= preparse_varlists( C, func, node->child->next );
	}
	else if( node->type == SFT_FUNC )
	{
		FTNode* N = node->child->next->next->next;
		if( N && N->type == SFT_IDENT )
		{
			if( find_varT( &C->fctx->gvars, N->token ) == -1 &&
				find_varT( &C->fctx->clsr, N->token ) == -1 &&
				add_varT( C->fctx->func ? &C->fctx->vars : &C->fctx->gvars, C, N->token ) )
				comp_reg_alloc( C );
		}
	}
	else if( node->child )
		ret &= preparse_varlists( C, func, node->child );
	if( node->next )
		ret &= preparse_varlists( C, func, node->next );
	return ret;
}

static int preparse_closures( SGS_CTX, sgs_CompFunc* func, FTNode* node, int decl )
{
	int ret;
	node = node->child;
	while( node )
	{
		ret = add_var( &C->fctx->clsr, C, (char*) node->token + 2, node->token[ 1 ] );
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

static int preparse_clsrlists( SGS_CTX, sgs_CompFunc* func, FTNode* node )
{
	int ret = 1;
	if( node->type == SFT_FUNC )
		ret &= preparse_closures( C, func, node->child->next, 0 );
	else if( node->child )
		ret &= preparse_clsrlists( C, func, node->child );
	if( node->next )
		ret &= preparse_clsrlists( C, func, node->next );
	return ret;
}

static int preparse_arglist( SGS_CTX, sgs_CompFunc* func, FTNode* node )
{
	node = node->child;
	while( node )
	{
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


#define add_const_HDR \
	sgs_Variable* vbeg = (sgs_Variable*) func->consts.ptr; \
	sgs_Variable* vend = (sgs_Variable*) ( func->consts.ptr + func->consts.size ); \
	sgs_Variable* var = vbeg; \
	sgs_Variable nvar;

static int add_const_null( SGS_CTX, sgs_CompFunc* func )
{
	add_const_HDR;
	while( var < vend )
	{
		if( var->type == SGS_VTC_NULL )
			return var - vbeg;
		var++;
	}

	nvar.type = SGS_VTC_NULL;
	membuf_appbuf( &func->consts, C, &nvar, sizeof( nvar ) );
	return vend - vbeg;
}

static int add_const_b( SGS_CTX, sgs_CompFunc* func, int32_t bval )
{
	add_const_HDR;
	while( var < vend )
	{
		if( var->type == SGS_VTC_BOOL && var->data.B == bval )
			return var - vbeg;
		var++;
	}

	nvar.type = SGS_VTC_BOOL;
	nvar.data.B = bval;
	membuf_appbuf( &func->consts, C, &nvar, sizeof( nvar ) );
	return vend - vbeg;
}

static int add_const_i( SGS_CTX, sgs_CompFunc* func, sgs_Int ival )
{
	add_const_HDR;
	while( var < vend )
	{
		if( var->type == SGS_VTC_INT && var->data.I == ival )
			return var - vbeg;
		var++;
	}

	nvar.type = SGS_VTC_INT;
	nvar.data.I = ival;
	membuf_appbuf( &func->consts, C, &nvar, sizeof( nvar ) );
	return vend - vbeg;
}

static int add_const_r( SGS_CTX, sgs_CompFunc* func, sgs_Real rval )
{
	add_const_HDR;
	while( var < vend )
	{
		if( var->type == SGS_VTC_REAL && var->data.R == rval )
			return var - vbeg;
		var++;
	}

	nvar.type = SGS_VTC_REAL;
	nvar.data.R = rval;
	membuf_appbuf( &func->consts, C, &nvar, sizeof( nvar ) );
	return vend - vbeg;
}

static int add_const_s( SGS_CTX, sgs_CompFunc* func, int32_t len, const char* str )
{
	add_const_HDR;
	while( var < vend )
	{
		if( var->type == SGS_VTC_STRING && var->data.S->size == len
			&& memcmp( var_cstr( var ), str, len ) == 0 )
			return var - vbeg;
		var++;
	}

	sgsVM_VarCreateString( C, &nvar, str, len );
	membuf_appbuf( &func->consts, C, &nvar, sizeof( nvar ) );
	return vend - vbeg;
}

static int add_const_f( SGS_CTX, sgs_CompFunc* func, sgs_CompFunc* nf,
	const char* funcname, sgs_SizeVal fnsize, LineNum lnum )
{
	sgs_Variable nvar;
	int pos;
	func_t* F = sgs_Alloc_a( func_t, nf->consts.size + nf->code.size );

	F->refcount = 1;
	F->size = nf->consts.size + nf->code.size;
	F->instr_off = nf->consts.size;
	F->gotthis = nf->gotthis;
	F->numargs = nf->numargs;

	{
		int lnc = nf->lnbuf.size / sizeof( uint16_t );
		F->lineinfo = sgs_Alloc_n( uint16_t, lnc );
		memcpy( F->lineinfo, nf->lnbuf.ptr, nf->lnbuf.size );
	}
	F->funcname = membuf_create();
	if( funcname )
		membuf_setstrbuf( &F->funcname, C, funcname, fnsize );
	F->linenum = lnum;
	
	F->filename = membuf_create();
	if( C->filename )
		membuf_setstr( &F->filename, C, C->filename );
	
	memcpy( func_consts( F ), nf->consts.ptr, nf->consts.size );
	memcpy( func_bytecode( F ), nf->code.ptr, nf->code.size );

	membuf_destroy( &nf->consts, C );
	membuf_destroy( &nf->code, C );
	membuf_destroy( &nf->lnbuf, C );
	sgs_Dealloc( nf );

	pos = func->consts.size / sizeof( nvar );
	nvar.type = SGS_VTC_FUNC;
	nvar.data.F = F;
	membuf_appbuf( &func->consts, C, &nvar, sizeof( nvar ) );
	return pos;
}

#define INTERNAL_ERROR( loff ) sgs_printf( C, SGS_ERROR, "INTERNAL ERROR occured in file %s [%d]", __FILE__, __LINE__ - (loff) )

static int op_pick_opcode( int oper, int binary )
{
	if( !binary )
	{
		if( oper == ST_OP_ADD ) return 0;
		if( oper == ST_OP_SUB )	return SI_NEGATE;
		if( oper == ST_OP_NOT ) return SI_BOOL_INV;
		if( oper == ST_OP_INV ) return SI_INVERT;
	}

	switch( oper )
	{
	case ST_OP_ADD: case ST_OP_ADDEQ: return SI_ADD;
	case ST_OP_SUB: case ST_OP_SUBEQ: return SI_SUB;
	case ST_OP_MUL: case ST_OP_MULEQ: return SI_MUL;
	case ST_OP_DIV: case ST_OP_DIVEQ: return SI_DIV;
	case ST_OP_MOD: case ST_OP_MODEQ: return SI_MOD;

	case ST_OP_AND: case ST_OP_ANDEQ: return SI_AND;
	case ST_OP_OR: case ST_OP_OREQ: return SI_OR;
	case ST_OP_XOR: case ST_OP_XOREQ: return SI_XOR;
	case ST_OP_LSH: case ST_OP_LSHEQ: return SI_LSH;
	case ST_OP_RSH: case ST_OP_RSHEQ: return SI_RSH;

	case ST_OP_CAT: case ST_OP_CATEQ: return SI_CONCAT;

	case ST_OP_SEQ: return SI_SEQ;
	case ST_OP_SNEQ: return SI_SNEQ;
	case ST_OP_EQ: return SI_EQ;
	case ST_OP_NEQ: return SI_NEQ;
	case ST_OP_LESS: return SI_LT;
	case ST_OP_LEQ: return SI_LTE;
	case ST_OP_GRTR: return SI_GT;
	case ST_OP_GEQ: return SI_GTE;

	default: return 0;
	}
}


static int compile_node( SGS_CTX, sgs_CompFunc* func, FTNode* node );
static int compile_node_r( SGS_CTX, sgs_CompFunc* func, FTNode* node, int16_t* out );
static int compile_node_w( SGS_CTX, sgs_CompFunc* func, FTNode* node, int16_t src );
static int compile_oper( SGS_CTX, sgs_CompFunc* func, FTNode* node, int16_t* arg, int out, int expect );




static int const_maybeload( SGS_CTX, sgs_CompFunc* func, FTNode* node, int cid )
{
	if( cid > 65535 )
	{
		QPRINT( "Maximum number of constants exceeded" );
		C->state |= SGS_MUST_STOP;
		return 0;
	}
	if( cid < 256 )
		return CONSTENC( cid );
	else
	{
		int out = comp_reg_alloc( C );
		INSTR_WRITE_EX( SI_LOADCONST, cid, out );
		return out;
	}
}

#define BC_CONSTENC( cid ) const_maybeload( C, func, node, cid )


static void compile_ident( SGS_CTX, sgs_CompFunc* func, FTNode* node, int16_t* out )
{
	int16_t pos = add_const_s( C, func, node->token[ 1 ], (const char*) node->token + 2 );
	*out = BC_CONSTENC( pos );
}


static int compile_ident_r( SGS_CTX, sgs_CompFunc* func, FTNode* node, int16_t* out )
{
	int16_t pos;
	if( is_keyword( node->token, "null" ) )
	{
		pos = add_const_null( C, func );
		*out = BC_CONSTENC( pos );
		return 1;
	}
	if( is_keyword( node->token, "true" ) )
	{
		pos = add_const_b( C, func, TRUE );
		*out = BC_CONSTENC( pos );
		return 1;
	}
	if( is_keyword( node->token, "false" ) )
	{
		pos = add_const_b( C, func, FALSE );
		*out = BC_CONSTENC( pos );
		return 1;
	}
	if( *node->token == ST_KEYWORD )
	{
		if( is_keyword( node->token, "this" ) )
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

	/* closures */
	if( ( pos = find_var( &C->fctx->clsr, (char*) node->token + 2, node->token[1] ) ) >= 0 )
	{
		*out = comp_reg_alloc( C );
		INSTR_WRITE( SI_GETCLSR, *out, pos, 0 );
		return 1;
	}

	if( C->fctx->func )
	{
		int16_t gpos = find_var( &C->fctx->gvars, (char*) node->token + 2, node->token[ 1 ] );
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
		INSTR_WRITE( SI_GETVAR, *out, pos, 0 );
	}
	return 1;
}

static int compile_ident_w( SGS_CTX, sgs_CompFunc* func, FTNode* node, int16_t src )
{
	int16_t pos;
	if( *node->token == ST_KEYWORD )
	{
		QPRINT( "Cannot write to reserved keywords" );
		return 0;
	}

	if( ( pos = find_var( &C->fctx->clsr, (char*) node->token + 2, node->token[1] ) ) >= 0 )
	{
		INSTR_WRITE( SI_SETCLSR, 0, pos, src );
		return 1;
	}

	if( C->fctx->func )
	{
		int16_t gpos = find_var( &C->fctx->gvars, (char*) node->token + 2, node->token[ 1 ] );
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
			INSTR_WRITE( SI_SET, pos, src, 0 );
		}
	}
	else
	{
		compile_ident( C, func, node, &pos );
		INSTR_WRITE( SI_SETVAR, 0, pos, src );
	}
	return 1;
}

static int compile_const( SGS_CTX, sgs_CompFunc* func, FTNode* node, int16_t* opos )
{
	if( *node->token == ST_NUMINT )
	{
		*opos = BC_CONSTENC( add_const_i( C, func, AS_INTEGER( node->token + 1 ) ) );
	}
	else if( *node->token == ST_NUMREAL )
	{
		*opos = BC_CONSTENC( add_const_r( C, func, AS_REAL( node->token + 1 ) ) );
	}
	else if( *node->token == ST_STRING )
	{
		*opos = BC_CONSTENC( add_const_s( C, func, AS_INT32( node->token + 1 ), (const char*) node->token + 5 ) );
	}
	else
	{
		QPRINT( "INTERNAL ERROR: constant doesn't have a token of type int/real/string attached" );
		return 0;
	}
	return 1;
}


static int compile_regcopy( SGS_CTX, sgs_CompFunc* func, FTNode* node, int from, int16_t srcpos, int16_t dstpos )
{
	INSTR_WRITE( SI_SET, dstpos, srcpos, 0 );
	return 1;
}

static int compile_fcall( SGS_CTX, sgs_CompFunc* func, FTNode* node, int16_t* out, int expect )
{
	int i = 0;
	int16_t funcpos = -1, retpos = -1, gotthis = FALSE;
	
	/* IF (ternary-like) */
	if( is_keyword( node->child->token, "if" ) )
	{
		FTNode* n = node->child->next->child;
		int argc = 0, csz1, csz2, csz3;
		int16_t exprpos = -1, srcpos = -1;
		if( !out )
		{
			QPRINT( "'if' pseudo-function cannot be used as input for expression writes" );
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
		FUNC_ENTER;
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
		
		INSTR_WRITE_EX( SI_NOP, 0, 0 ); /* harmful optimization prevention hack */
		AS_UINT32( func->code.ptr + csz1 - 4 ) = INSTR_MAKE_EX( SI_JMPF, ( csz2 - csz1 ) / INSTR_SIZE, exprpos );
		AS_UINT32( func->code.ptr + csz2 - 4 ) = INSTR_MAKE_EX( SI_JUMP, ( csz3 - csz2 ) / INSTR_SIZE, 0 );
		
		*out = retpos;
		return 1;
	}

	/* load function */
	FUNC_ENTER;
	if( !compile_node_r( C, func, node->child, &funcpos ) ) return 0;

	if( out )
	{
		if( expect )
			retpos = comp_reg_alloc( C );
		for( i = 1; i < expect; ++i )
			comp_reg_alloc( C );
	}

	/* load arguments */
	i = 0;
	{
		/* passing objects where the call is formed appropriately */
		FTNode* n;
		int16_t argpos = -1;
		if( node->child->type == SFT_OPER && *node->child->token == ST_OP_MMBR )
		{
			FUNC_ENTER;
			if( !compile_node_r( C, func, node->child->child, &argpos ) ) return 0;
			INSTR_WRITE( SI_PUSH, 0, argpos, 0 );
			gotthis = TRUE;
		}

		n = node->child->next->child;
		while( n )
		{
			argpos = -1;
			FUNC_ENTER;
			if( !compile_node_r( C, func, n, &argpos ) ) return 0;
			INSTR_WRITE( SI_PUSH, 0, argpos, 0 );
			i++;
			n = n->next;
		}
	}

	if( gotthis )
		i |= 0x100;

	/* compile call */
	INSTR_WRITE( SI_CALL, expect, i, funcpos );

	/* compile writeback */
	if( out )
	{
		while( expect )
		{
			int16_t cra;
			expect--;
			cra = retpos + expect;
			out[ expect ] = cra;
			INSTR_WRITE( SI_POPR, cra, 0, 0 );
		}
	}

	return 1;
}

static int compile_index_r( SGS_CTX, sgs_CompFunc* func, FTNode* node, int16_t* out )
{
	int16_t var, name, opos = comp_reg_alloc( C );
	int32_t regpos = C->fctx->regs;
	FUNC_ENTER;
	if( !compile_node_r( C, func, node->child, &var ) ) return 0;
	FUNC_ENTER;
	if( !compile_node_r( C, func, node->child->next, &name ) ) return 0;
	INSTR_WRITE( SI_GETINDEX, opos, var, name );
	comp_reg_unwind( C, regpos );
	if( out )
		*out = opos;
	return 1;
}

static int compile_index_w( SGS_CTX, sgs_CompFunc* func, FTNode* node, int16_t src )
{
	int16_t var, name;
	int32_t regpos = C->fctx->regs;
	FUNC_ENTER;
	if( !compile_node_r( C, func, node->child, &var ) ) return 0;
	FUNC_ENTER;
	if( !compile_node_r( C, func, node->child->next, &name ) ) return 0;
	if( CONSTVAR( var ) )
	{
		QPRINT( "Cannot set indexed value of a constant" );
		return 0;
	}
	INSTR_WRITE( SI_SETINDEX, var, name, src );
	comp_reg_unwind( C, regpos );
	return 1;
}


static int try_optimize_last_instr_out( SGS_CTX, sgs_CompFunc* func, FTNode* node, int32_t ioff, int16_t* out )
{
	int16_t pos = -1;

	FUNC_BEGIN;
	UNUSED( C );
	
	if( ( node->type != SFT_IDENT && node->type != SFT_ARGMT ) || *node->token != ST_IDENT )
		goto cannot;

	if( ioff > func->code.size - 4 )
		goto cannot;

	ioff = func->code.size - 4;

	/* find the variable output register */
	if( C->fctx->func )
	{
		int16_t gpos = find_var( &C->fctx->gvars, (char*) node->token + 2, node->token[ 1 ] );
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

	/* global variable */
	if( pos < 0 )
		goto cannot;

	{
		instr_t I = *(instr_t*)(func->code.ptr + ioff);
		int op = INSTR_GET_OP( I ), argB = INSTR_GET_B( I ), argC = INSTR_GET_C( I );
		switch( op )
		{
		case SI_POPR: case SI_GETVAR: case SI_GETPROP: case SI_GETINDEX:
		case SI_SET: case SI_CONCAT:
		case SI_NEGATE: case SI_BOOL_INV: case SI_INVERT:
		case SI_ADD: case SI_SUB: case SI_MUL: case SI_DIV: case SI_MOD:
		case SI_AND: case SI_OR: case SI_XOR: case SI_LSH: case SI_RSH:
		case SI_SEQ: case SI_EQ: case SI_LT: case SI_LTE:
		case SI_SNEQ: case SI_NEQ: case SI_GT: case SI_GTE:
		case SI_ARRAY: case SI_DICT:
			I = INSTR_MAKE( op, pos, argB, argC );
			AS_UINT32( func->code.ptr + ioff ) = I;
			if( out )
				*out = pos;
			break;
		default:
			goto cannot;
		}
	}

	FUNC_END;
	return 1;

cannot:
	FUNC_END;
	return 0;
}


static int compile_oper( SGS_CTX, sgs_CompFunc* func, FTNode* node, int16_t* arg, int out, int expect )
{
	int assign = ST_OP_ASSIGN( *node->token );
	FUNC_BEGIN;

	/* Boolean ops */
	if( ST_OP_BOOL( *node->token ) )
	{
		int jin;
		int16_t ireg1, ireg2, oreg = 0, jmp_off = 0;
		int32_t csz, csz2;

		if( !assign )
			oreg = comp_reg_alloc( C );
		if( C->state & SGS_MUST_STOP )
			goto fail;

		/* get source data register */
		FUNC_ENTER;
		if( !compile_node_r( C, func, node->child, &ireg1 ) ) goto fail;

		/* write cond. jump */
		jin = ( *node->token == ST_OP_BLAND || *node->token == ST_OP_BLAEQ ) ? SI_JMPT : SI_JMPF;
		INSTR_WRITE_PCH();
		csz = func->code.size;

		/* compile write of value 1 */
		if( assign )
		{
			FUNC_ENTER;
			if( !compile_node_w( C, func, node->child, ireg1 ) ) goto fail;
		}
		else
		{
			INSTR_WRITE( SI_SET, oreg, ireg1, 0 );
		}

		INSTR_WRITE_PCH();
		csz2 = func->code.size;

		/* fix-up jump 1 */
		jmp_off = func->code.size - csz;
		AS_UINT32( func->code.ptr + csz - 4 ) = INSTR_MAKE_EX( jin, jmp_off / INSTR_SIZE, ireg1 );

		/* get source data register 2 */
		FUNC_ENTER;
		if( !compile_node_r( C, func, node->child->next, &ireg2 ) ) goto fail;

		/* compile write of value 2 */
		if( assign )
		{
			FUNC_ENTER;
			if( !compile_node_w( C, func, node->child, ireg2 ) ) goto fail;
		}
		else
		{
			INSTR_WRITE( SI_SET, oreg, ireg2, 0 );
		}
		
		INSTR_WRITE( SI_NOP, 0, 0, 0 );

		/* fix-up jump 2 */
		jmp_off = func->code.size - csz2;
		AS_UINT32( func->code.ptr + csz2 - 4 ) = INSTR_MAKE_EX( SI_JUMP, jmp_off / INSTR_SIZE, 0 );

		/* re-read from assignments */
		if( arg )
		{
			if( assign )
			{
				FUNC_ENTER;
				if( !compile_node_r( C, func, node->child, arg ) ) goto fail;
			}
			else
				*arg = oreg;
		}
	}
	else
	/* Increment / decrement */
	if( *node->token == ST_OP_INC || *node->token == ST_OP_DEC )
	{
		int16_t ireg, oreg;

		/* register with input data */
		FUNC_ENTER;
		if( !compile_node_r( C, func, node->child, &ireg ) ) goto fail;

		/* output register selection */
		oreg = expect && node->type == SFT_OPER_P ? comp_reg_alloc( C ) : ireg;
		if( C->state & SGS_MUST_STOP )
			goto fail;
		if( oreg != ireg )
		{
			INSTR_WRITE( SI_SET, oreg, ireg, 0 );
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
		INSTR_WRITE( *node->token == ST_OP_INC ? SI_INC : SI_DEC, ireg, ireg, 0 );

		if( arg )
			*arg = oreg;

		/* compile writeback */
		FUNC_ENTER;
		if( !compile_node_w( C, func, node->child, ireg ) ) goto fail;
	}
	/* Assignment */
	else if( assign )
	{
		/* 1 operand */
		if( *node->token == ST_OP_SET )
		{
			int16_t ireg, isb = func->code.size;
			
			if( node->child->type == SFT_EXPLIST )
			{
				FTNode* n;
				int xpct = 0;
				int32_t bkup;
				if( node->child->next->type != SFT_FCALL )
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
				
				if( !compile_fcall( C, func, node->child->next, NULL, xpct ) ) goto fail;
				
				bkup = C->fctx->regs;
				while( xpct-- )
				{
					int i;
					ireg = comp_reg_alloc( C );
					if( C->state & SGS_MUST_STOP )
						goto fail;
					
					isb = func->code.size;
					INSTR_WRITE( SI_POPR, ireg, 0, 0 );
					if( arg )
						*arg = ireg;
					
					n = node->child->child;
					for( i = 0; i < xpct; ++i )
						n = n->next;
					
					FUNC_ENTER;
					if( !try_optimize_last_instr_out( C, func, n, isb, arg ) )
					{
						/* just set the contents */
						FUNC_ENTER;
						if( !compile_node_w( C, func, n, ireg ) ) goto fail;
					}
					
					comp_reg_unwind( C, bkup );
				}
			}
			else
			{
				/* get source data register */
				FUNC_ENTER;
				if( !compile_node_r( C, func, node->child->next, &ireg ) ) goto fail;

				if( arg )
					*arg = ireg;

				FUNC_ENTER;
				if( !try_optimize_last_instr_out( C, func, node->child, isb, arg ) )
				{
					/* just set the contents */
					FUNC_ENTER;
					if( !compile_node_w( C, func, node->child, ireg ) ) goto fail;
				}
			}
		}
		/* 2 operands */
		else
		{
			uint8_t op;
			int16_t ireg1, ireg2, oreg = comp_reg_alloc( C );
			if( C->state & SGS_MUST_STOP )
				goto fail;

			/* get source data registers */
			FUNC_ENTER;
			if( !compile_node_r( C, func, node->child, &ireg1 ) ) goto fail;
			FUNC_ENTER;
			if( !compile_node_r( C, func, node->child->next, &ireg2 ) ) goto fail;

			/* compile op */
			op = op_pick_opcode( *node->token, 1 );
			INSTR_WRITE( op, oreg, ireg1, ireg2 );

			if( arg )
				*arg = oreg;

			/* compile write */
			FUNC_ENTER;
			if( !compile_node_w( C, func, node->child, oreg ) ) goto fail;
		}
	}
	/* Any other */
	else
	{
		int16_t ireg1, ireg2, oreg;

		if( expect > 1 )
		{
			QPRINT( "Too many expected outputs for operator" );
			goto fail;
		}

		if( *node->token == ST_OP_MMBR )
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
			FUNC_ENTER;
			if( !compile_node_r( C, func, node->child, &ireg1 ) ) goto fail;

			if( node->child->next->type == SFT_IDENT )
				compile_ident( C, func, node->child->next, &ireg2 );
			else
			{
				FUNC_ENTER;
				if( !compile_node_r( C, func, node->child->next, &ireg2 ) ) goto fail;
			}

			/* compile op */
			if( out )
				INSTR_WRITE( SI_GETPROP, oreg, ireg1, ireg2 );
			else
			{
				if( CONSTVAR( ireg1 ) )
				{
					QPRINT( "Cannot set property of a constant" );
					goto fail;
				}
				INSTR_WRITE( SI_SETPROP, ireg1, ireg2, oreg );
			}

		}
		else
		{
			uint8_t op;

			oreg = comp_reg_alloc( C );
			if( C->state & SGS_MUST_STOP )
				goto fail;

			/* get source data registers */
			FUNC_ENTER;
			if( !compile_node_r( C, func, node->child, &ireg1 ) ) goto fail;
			if( node->child->next )
			{
				FUNC_ENTER;
				if( !compile_node_r( C, func, node->child->next, &ireg2 ) ) goto fail;
			}

			if( arg )
				*arg = oreg;

			/* compile op */
			op = op_pick_opcode( *node->token, !!node->child->next );
			if( node->child->next )
				INSTR_WRITE( op, oreg, ireg1, ireg2 );
			else
				INSTR_WRITE( op, oreg, ireg1, 0 );
		}
	}

	FUNC_END;
	return 1;

fail:
	C->state |= SGS_HAS_ERRORS;
	FUNC_END;
	return 0;
}


static int compile_breaks( SGS_CTX, sgs_CompFunc* func, uint8_t iscont )
{
	sgs_BreakInfo* binfo = C->fctx->binfo, *prev = NULL;
	while( binfo )
	{
		if( binfo->numlp == C->fctx->loops && binfo->iscont == iscont )
		{
			int16_t off = ( func->code.size - binfo->jdoff ) / INSTR_SIZE - 1;
			AS_UINT32( func->code.ptr + binfo->jdoff ) = INSTR_MAKE_EX( SI_JUMP, off, 0 );
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




static void rpts( MemBuf* out, SGS_CTX, FTNode* root )
{
	switch( root->type )
	{
	case SFT_IDENT:
		membuf_appbuf( out, C, root->token + 2, root->token[1] );
		break;
	case SFT_OPER:
		switch( *root->token )
		{
		case ST_OP_MMBR:
			rpts( out, C, root->child );
			membuf_appchr( out, C, '.' );
			rpts( out, C, root->child->next );
			break;
		}
		break;
	}
}


static void prefix_bytecode( SGS_CTX, sgs_CompFunc* func, int args )
{
	MemBuf ncode = membuf_create();
	MemBuf nlnbuf = membuf_create();

	instr_t I = INSTR_MAKE( SI_PUSHN, C->fctx->lastreg - args, 0, 0 );
	uint16_t ln = 0;

	membuf_appbuf( &ncode, C, &I, sizeof( I ) );
	membuf_appbuf( &nlnbuf, C, &ln, sizeof( ln ) );

	if( C->fctx->outclsr > C->fctx->inclsr )
	{
		int i;
		I = INSTR_MAKE( SI_GENCLSR, C->fctx->outclsr - C->fctx->inclsr, 0, 0 );
		membuf_appbuf( &ncode, C, &I, sizeof( I ) );
		membuf_appbuf( &nlnbuf, C, &ln, sizeof( ln ) );

		for( i = 0; i < args; ++i )
		{
			char* varstr = NULL;
			int varstrlen, result, which;
			result = find_nth_var( &C->fctx->vars, i, &varstr, &varstrlen );
			sgs_BreakIf( !result );
			if( !result )
				continue;
			which = find_var( &C->fctx->clsr, varstr, varstrlen );
			if( which < 0 )
				continue;
			I = INSTR_MAKE( SI_SETCLSR, 0, which, i );
			membuf_appbuf( &ncode, C, &I, sizeof( I ) );
			membuf_appbuf( &nlnbuf, C, &ln, sizeof( ln ) );
		}
	}

	membuf_appbuf( &ncode, C, func->code.ptr, func->code.size );
	membuf_appbuf( &nlnbuf, C, func->lnbuf.ptr, func->lnbuf.size );

	membuf_destroy( &func->code, C );
	membuf_destroy( &func->lnbuf, C );

	func->code = ncode;
	func->lnbuf = nlnbuf;
}


static int compile_func( SGS_CTX, sgs_CompFunc* func, FTNode* node, int16_t* out )
{
	sgs_FuncCtx* fctx = fctx_create( C ), *bkfctx = C->fctx;
	sgs_CompFunc* nf = make_compfunc( C );
	int args = 0, clsrcnt = 0;

	FTNode* n_arglist = node->child;
	FTNode* n_uselist = n_arglist->next;
	FTNode* n_body = n_uselist->next;
	FTNode* n_name = n_body->next;

	/* pre-context-change closure-apply */
	FUNC_ENTER;
	if( !preparse_closures( C, func, n_uselist, 0 ) ) { goto fail; }

	C->fctx = fctx;

	FUNC_ENTER;
	if( !preparse_closures( C, nf, n_uselist, 1 ) ) { goto fail; }

	FUNC_ENTER;
	if( !preparse_arglist( C, nf, n_arglist ) ) { goto fail; }
	args = fctx->regs;

	FUNC_ENTER;
	if( !preparse_clsrlists( C, nf, n_body ) ) { goto fail; }

	FUNC_ENTER;
	if( !preparse_varlists( C, nf, n_body ) ) { goto fail; }
	args += nf->gotthis;

	FUNC_ENTER;
	if( !compile_node( C, nf, n_body ) ) { goto fail; }

	comp_reg_unwind( C, 0 );

	if( C->fctx->lastreg > 0xff )
	{
		QPRINT( "Maximum register count exceeded" );
		goto fail;
	}
	
	prefix_bytecode( C, nf, args );
	clsrcnt = C->fctx->inclsr;

#if SGS_DUMP_BYTECODE || ( SGS_DEBUG && SGS_DEBUG_DATA )
	fctx_dump( fctx );
	sgsBC_Dump( nf );
#endif
	C->fctx = bkfctx;

	{
		MemBuf ffn = membuf_create();
		if( n_name )
			rpts( &ffn, C, n_name );
		*out = BC_CONSTENC( add_const_f( C, func, nf, ffn.ptr, ffn.size, sgsT_LineNum( node->token ) ) );
		membuf_destroy( &ffn, C );
		
		if( clsrcnt > 0 )
		{
			int i;
			int16_t ro = comp_reg_alloc( C );
			FTNode* uli = n_uselist->child;
			for( i = 0; i < clsrcnt; ++i )
			{
				INSTR_WRITE( SI_PUSHCLSR, find_varT( &fctx->clsr, uli->token ), 0, 0 );
				uli = uli->next;
			}
			INSTR_WRITE( SI_MAKECLSR, ro, *out, clsrcnt );
			*out = ro;
		}
	}
	fctx_destroy( C, fctx );
	return 1;

fail:
	sgsBC_Free( C, nf );
	C->fctx = bkfctx;
	fctx_destroy( C, fctx );
	C->state |= SGS_HAS_ERRORS;
	return 0;
}


static int compile_node_w( SGS_CTX, sgs_CompFunc* func, FTNode* node, int16_t src )
{
	FUNC_BEGIN;
	switch( node->type )
	{
	case SFT_IDENT:
	case SFT_KEYWORD:
		FUNC_HIT( "W_IDENT" );
		if( !compile_ident_w( C, func, node, src ) ) goto fail;
		break;

	case SFT_CONST:
		FUNC_HIT( "W_CONST" );
		QPRINT( "Cannot write to constants" );
		goto fail;
	case SFT_FUNC:
		FUNC_HIT( "W_FUNC" );
		QPRINT( "Cannot write to constants" );
		goto fail;
	case SFT_ARRLIST:
		FUNC_HIT( "W_ARRLIST" );
		QPRINT( "Cannot write to constants" );
		goto fail;
	case SFT_MAPLIST:
		FUNC_HIT( "W_MAPLIST" );
		QPRINT( "Cannot write to constants" );
		goto fail;

	case SFT_OPER:
	case SFT_OPER_P:
		FUNC_HIT( "W_OPER" );
		if( !compile_oper( C, func, node, &src, 0, 1 ) ) goto fail;
		break;

	case SFT_FCALL:
		FUNC_HIT( "W_FCALL" );
		if( !compile_fcall( C, func, node, NULL, 0 ) ) goto fail;
		break;

	case SFT_INDEX:
		FUNC_HIT( "W_INDEX" );
		if( !compile_index_w( C, func, node, src ) ) goto fail;
		break;
	
	case SFT_EXPLIST:
		FUNC_HIT( "W_EXPLIST" );
		QPRINT( "Expression writes only allowed with function call reads" );
		goto fail;

	default:
		sgs_Printf( C, SGS_ERROR, "Unexpected tree node [uncaught/internal BcG/w error]" );
		goto fail;
	}
	FUNC_END;
	return 1;

fail:
	FUNC_END;
	return 0;
}
static int compile_node_r( SGS_CTX, sgs_CompFunc* func, FTNode* node, int16_t* out )
{
	FUNC_BEGIN;
	switch( node->type )
	{
	case SFT_IDENT:
	case SFT_KEYWORD:
		FUNC_HIT( "R_IDENT" );
		if( !compile_ident_r( C, func, node, out ) ) goto fail;
		break;

	case SFT_CONST:
		FUNC_HIT( "R_CONST" );
		if( !compile_const( C, func, node, out ) ) goto fail;
		break;
	case SFT_FUNC:
		FUNC_HIT( "R_FUNC" );
		if( !compile_func( C, func, node, out ) ) goto fail;
		break;
	case SFT_ARRLIST:
		FUNC_HIT( "R_ARRLIST" );
		{
			int16_t pos = 0, args = 0;
			FTNode* n = node->child;
			while( n )
			{
				int32_t bkup = C->fctx->regs;
				pos = 0;
				FUNC_ENTER;
				if( !compile_node_r( C, func, n, &pos ) )
					goto fail;
				INSTR_WRITE( SI_PUSH, 0, pos, 0 );
				comp_reg_unwind( C, bkup );
				args++;
				n = n->next;
			}
			pos = comp_reg_alloc( C );
			INSTR_WRITE( SI_ARRAY, pos, args, 0 );
			*out = pos;
		}
		break;
	case SFT_MAPLIST:
		FUNC_HIT( "R_MAPLIST" );
		{
			int16_t pos = 0, args = 0;
			FTNode* n = node->child;
			while( n )
			{
				int32_t bkup = C->fctx->regs;
				pos = 0;
				if( args % 2 == 0 )
				{
					if( *n->token == ST_STRING )
						pos = BC_CONSTENC( add_const_s( C, func, AS_INT32( n->token + 1 ), (const char*) n->token + 5 ) );
					else
						compile_ident( C, func, n, &pos );
				}
				else
				{
					FUNC_ENTER;
					if( !compile_node_r( C, func, n, &pos ) )
						goto fail;
				}
				INSTR_WRITE( SI_PUSH, 0, pos, 0 );
				comp_reg_unwind( C, bkup );
				args++;
				n = n->next;
			}
			pos = comp_reg_alloc( C );
			INSTR_WRITE( SI_DICT, pos, args, 0 );
			*out = pos;
		}
		break;

	case SFT_OPER:
	case SFT_OPER_P:
		FUNC_HIT( "R_OPER" );
		if( !compile_oper( C, func, node, out, 1, 1 ) ) goto fail;
		break;

	case SFT_FCALL:
		FUNC_HIT( "R_FCALL" );
		if( !compile_fcall( C, func, node, out, 1 ) ) goto fail;
		break;

	case SFT_INDEX:
		FUNC_HIT( "R_INDEX" );
		if( !compile_index_r( C, func, node, out ) ) goto fail;
		break;

	case SFT_EXPLIST:
		FUNC_HIT( "R_EXPLIST" );
		{
			FTNode* n = node->child;
			if( !n )
			{
				QPRINT( "Empty expression found" );
				goto fail;
			}
			while( n )
			{
				FUNC_ENTER;
				if( !compile_node_r( C, func, n, out ) )
					goto fail;
				n = n->next;
			}
		}
		break;

	default:
		sgs_Printf( C, SGS_ERROR, "Unexpected tree node [uncaught/internal BcG/r error]" );
		goto fail;
	}
	FUNC_END;
	return 1;

fail:
	FUNC_END;
	return 0;
}

static int compile_for_explist( SGS_CTX, sgs_CompFunc* func, FTNode* node, int16_t* out )
{
	FTNode* n;

	FUNC_BEGIN;

	if( node->type != SFT_EXPLIST )
	{
		sgs_Printf( C, SGS_ERROR, "Unexpected tree node [uncaught/internal BcG/r[fe] error]" );
		goto fail;
	}

	FUNC_HIT( "Rs_EXPLIST" );

	n = node->child;
	while( n )
	{
		FUNC_ENTER;
		if( !compile_node_r( C, func, n, out ) )
			goto fail;
		n = n->next;
	}

	FUNC_END;
	return 1;

fail:
	FUNC_END;
	return 0;
}

static int compile_node( SGS_CTX, sgs_CompFunc* func, FTNode* node )
{
	int32_t i = 0;
	FUNC_BEGIN;

	switch( node->type )
	{
	/* ignore these items if they're leading in statements */
	case SFT_IDENT:
	case SFT_KEYWORD:
	case SFT_CONST:
	case SFT_ARRLIST:
	case SFT_MAPLIST:
		break;

	case SFT_OPER:
	case SFT_OPER_P:
		FUNC_HIT( "OPERATOR" );
		if( !compile_oper( C, func, node, NULL, 1, 0 ) ) goto fail;
		break;

	case SFT_INDEX:
		FUNC_HIT( "INDEX" );
		if( !compile_index_r( C, func, node, NULL ) ) goto fail;
		break;

	case SFT_FCALL:
		FUNC_HIT( "FCALL" );
		if( !compile_fcall( C, func, node, NULL, 0 ) ) goto fail;
		break;

	case SFT_EXPLIST:
		FUNC_HIT( "EXPLIST" );
		{
			FTNode* n = node->child;
			while( n )
			{
				FUNC_ENTER;
				if( !compile_node( C, func, n ) )
					goto fail;
				n = n->next;
			}
		}
		break;

	case SFT_RETURN:
		FUNC_HIT( "RETURN" );
		{
			int regstate = C->fctx->regs;
			int num = 0;
			FTNode* n = node->child;
			while( n )
			{
				int16_t arg = 0;
				FUNC_ENTER;
				if( !compile_node_r( C, func, n, &arg ) ) goto fail;
				INSTR_WRITE( SI_PUSH, 0, arg, 0 );
				n = n->next;
				num++;
			}
			INSTR_WRITE( SI_RETN, num, 0, 0 );
			comp_reg_unwind( C, regstate );
		}
		break;

	case SFT_BLOCK:
		FUNC_HIT( "BLOCK" );
		node = node->child;
		while( node )
		{
			int regstate = C->fctx->regs;
			FUNC_ENTER;
			if( !compile_node( C, func, node ) ) goto fail;
			node = node->next;
			comp_reg_unwind( C, regstate );
		}
		break;

	case SFT_IFELSE:
		FUNC_HIT( "IF/ELSE" );
		{
			int16_t arg = 0;
			int regstate = C->fctx->regs;
			FUNC_ENTER;
			if( !compile_node_r( C, func, node->child, &arg ) ) goto fail;
			comp_reg_unwind( C, regstate );
			INSTR_WRITE_PCH();
			{
				int32_t jp1, jp2 = 0, jp3 = 0;
				jp1 = func->code.size;

				regstate = C->fctx->regs;
				FUNC_ENTER;
				if( !compile_node( C, func, node->child->next ) ) goto fail;
				comp_reg_unwind( C, regstate );

				if( node->child->next->next )
				{
					INSTR_WRITE_PCH();
					jp2 = func->code.size;
					AS_UINT32( func->code.ptr + jp1 - 4 ) = INSTR_MAKE_EX( SI_JMPF, ( jp2 - jp1 ) / INSTR_SIZE, arg );

					regstate = C->fctx->regs;
					FUNC_ENTER;
					if( !compile_node( C, func, node->child->next->next ) ) goto fail;
					jp3 = func->code.size;
					AS_UINT32( func->code.ptr + jp2 - 4 ) = INSTR_MAKE_EX( SI_JUMP, ( jp3 - jp2 ) / INSTR_SIZE, 0 );
					comp_reg_unwind( C, regstate );
				}
				else
				{
					AS_UINT32( func->code.ptr + jp1 - 4 ) = INSTR_MAKE_EX( SI_JMPF, ( func->code.size - jp1 ) / INSTR_SIZE, arg );
				}
			}
		}
		break;

	case SFT_WHILE:
		FUNC_HIT( "WHILE" );
		{
			int16_t arg = -1;
			int regstate = C->fctx->regs;
			C->fctx->loops++;
			i = func->code.size;
			FUNC_ENTER;
			if( !compile_node_r( C, func, node->child, &arg ) ) goto fail; /* test */
			comp_reg_unwind( C, regstate );
			INSTR_WRITE_PCH();
			{
				int16_t off;
				int32_t jp1, jp2 = 0;
				jp1 = func->code.size;

				regstate = C->fctx->regs;
				FUNC_ENTER;
				if( !compile_node( C, func, node->child->next ) ) goto fail; /* while */
				comp_reg_unwind( C, regstate );

				if( !compile_breaks( C, func, 1 ) )
					goto fail;

				jp2 = func->code.size;
				off = i - jp2;
				INSTR_WRITE_EX( SI_JUMP, off / INSTR_SIZE - 1, 0 );
				AS_UINT32( func->code.ptr + jp1 - 4 ) = INSTR_MAKE_EX( SI_JMPF, ( jp2 - jp1 ) / INSTR_SIZE + 1, arg );
			}
			if( !compile_breaks( C, func, 0 ) )
				goto fail;
			C->fctx->loops--;
		}
		break;

	case SFT_DOWHILE:
		FUNC_HIT( "DO/WHILE" );
		{
			int regstate = C->fctx->regs;
			int16_t arg = -1, joff;
			C->fctx->loops++;
			i = func->code.size;
			{
				FUNC_ENTER;
				if( !compile_node( C, func, node->child->next ) ) goto fail; /* while */
				comp_reg_unwind( C, regstate );

				if( !compile_breaks( C, func, 1 ) )
					goto fail;
			}
			FUNC_ENTER;
			if( !compile_node_r( C, func, node->child, &arg ) ) goto fail; /* test */
			comp_reg_unwind( C, regstate );
			joff = i - func->code.size;
			INSTR_WRITE_EX( SI_JMPT, joff / INSTR_SIZE - 1, arg );
			if( !compile_breaks( C, func, 0 ) )
				goto fail;
			C->fctx->loops--;
		}
		break;

	case SFT_FOR:
		FUNC_HIT( "FOR" );
		{
			int regstate = C->fctx->regs;
			int16_t arg = -1;
			C->fctx->loops++;
			FUNC_ENTER;
			if( !compile_node( C, func, node->child ) ) goto fail; /* init */
			comp_reg_unwind( C, regstate );
			i = func->code.size;
			FUNC_ENTER;
			if( !compile_for_explist( C, func, node->child->next, &arg ) ) goto fail; /* test */
			comp_reg_unwind( C, regstate );
			if( arg != -1 )
			{
				INSTR_WRITE_PCH();
			}
			{
				int16_t off;
				int32_t jp1, jp2 = 0;
				jp1 = func->code.size;

				FUNC_ENTER;
				if( !compile_node( C, func, node->child->next->next->next ) ) goto fail; /* block */
				comp_reg_unwind( C, regstate );

				if( !compile_breaks( C, func, 1 ) )
					goto fail;
				FUNC_ENTER;
				if( !compile_node( C, func, node->child->next->next ) ) goto fail; /* incr */
				comp_reg_unwind( C, regstate );

				jp2 = func->code.size;
				off = i - jp2;
				INSTR_WRITE_EX( SI_JUMP, off / INSTR_SIZE - 1, 0 );
				if( arg != -1 )
				{
					AS_UINT32( func->code.ptr + jp1 - 4 ) = INSTR_MAKE_EX( SI_JMPF, ( jp2 - jp1 ) / INSTR_SIZE + 1, arg );
				}
			}
			if( !compile_breaks( C, func, 0 ) )
				goto fail;
			C->fctx->loops--;
		}
		break;

	case SFT_FOREACH:
		FUNC_HIT( "FOREACH" );
		{
			int32_t jp1;
			int16_t var, iter, key = -1, val = -1;
			int regstate, regstate2;
			regstate2 = C->fctx->regs;

			/* init */
			var = -1;
			FUNC_ENTER;
			if( !compile_node_r( C, func, node->child->next->next, &var ) ) goto fail; /* get variable */
			
			iter = comp_reg_alloc( C );
			if( node->child->type != SFT_NULL ) key = comp_reg_alloc( C );
			if( node->child->next->type != SFT_NULL ) val = comp_reg_alloc( C );
			regstate = C->fctx->regs;
			C->fctx->loops++;

			INSTR_WRITE( SI_FORPREP, iter, var, 0 );
			comp_reg_unwind( C, regstate );

			/* iterate */
			i = func->code.size;
			INSTR_WRITE_PCH();
			jp1 = func->code.size;
			INSTR_WRITE( SI_FORLOAD, iter, key, val );

			{
				int32_t jp2 = 0;
				int16_t off = 0;

				/* write to key variable */
				if( node->child->type != SFT_NULL && !compile_ident_w( C, func, node->child, key ) ) goto fail;

				/* write to value variable */
				if( node->child->next->type != SFT_NULL && !compile_ident_w( C, func, node->child->next, val ) ) goto fail;

				FUNC_ENTER;
				if( !compile_node( C, func, node->child->next->next->next ) ) goto fail; /* block */
				comp_reg_unwind( C, regstate );

				if( !compile_breaks( C, func, 1 ) )
					goto fail;

				jp2 = func->code.size;
				off = i - jp2;
				INSTR_WRITE_EX( SI_JUMP, off / INSTR_SIZE - 1, 0 );
				AS_UINT32( func->code.ptr + jp1 - 4 ) = INSTR_MAKE_EX(
					SI_FORJUMP,
					( func->code.size - jp1 ) / INSTR_SIZE,
					iter
				);
			}

			if( !compile_breaks( C, func, 0 ) )
				goto fail;
			C->fctx->loops--;
			comp_reg_unwind( C, regstate2 );
		}
		break;

	case SFT_BREAK:
		FUNC_HIT( "BREAK" );
		{
			TokenList tl = sgsT_Next( node->token );
			int32_t blev = 1;
			if( *tl == ST_NUMINT )
				blev = (uint32_t)*(sgs_Int*)( tl + 1 );
			if( blev > C->fctx->loops )
			{
				if( C->fctx->loops )
					QPRINT( "Break level too high" );
				else
					QPRINT( "Attempted to break while not in a loop" );
				goto fail;
			}
			fctx_binfo_add( C, C->fctx, func->code.size, C->fctx->loops + 1 - blev, FALSE );
			INSTR_WRITE_PCH();
		}
		break;

	case SFT_CONT:
		FUNC_HIT( "CONTINUE" );
		{
			TokenList tl = sgsT_Next( node->token );
			int32_t blev = 1;
			if( *tl == ST_NUMINT )
				blev = (uint32_t)*(sgs_Int*)( tl + 1 );
			if( blev > C->fctx->loops )
			{
				if( C->fctx->loops )
					QPRINT( "Continue level too high" );
				else
					QPRINT( "Attempted to continue while not in a loop" );
				goto fail;
			}
			fctx_binfo_add( C, C->fctx, func->code.size, C->fctx->loops + 1 - blev, TRUE );
			INSTR_WRITE_PCH();
		}
		break;

	case SFT_FUNC:
		FUNC_HIT( "FUNC" );
		{
			int16_t pos;
			FUNC_ENTER;
			if( !compile_func( C, func, node, &pos ) ) goto fail;

			if( node->child->next->next->next )
			{
				FUNC_ENTER;
				if( !compile_node_w( C, func, node->child->next->next->next, pos ) ) goto fail;
			}
		}
		break;

	case SFT_VARLIST:
	case SFT_GVLIST:
		FUNC_HIT( node->type == SFT_VARLIST ? "VARLIST" : "GLOBALVARLIST" );
		{
			int regstate = C->fctx->regs;
			FTNode* pp = node->child;
			while( pp )
			{
				if( pp->child )
				{
					int16_t arg = -1;
					int32_t lastsize = func->code.size;
					if( !compile_node_r( C, func, pp->child, &arg ) ) goto fail;
					if( !pp->token || *pp->token != ST_IDENT ) goto fail;
					if( node->type != SFT_VARLIST || !try_optimize_last_instr_out( C, func, pp, lastsize, NULL ) )
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
		sgs_Printf( C, SGS_ERROR, "Unexpected tree node [uncaught/internal BcG error]" );
		goto fail;
	}

	FUNC_END;
	return 1;

fail:
	FUNC_END;
	return 0;
}


sgs_CompFunc* sgsBC_Generate( SGS_CTX, FTNode* tree )
{
	sgs_CompFunc* func = make_compfunc( C );
	sgs_FuncCtx* fctx = fctx_create( C );
	fctx->func = FALSE;
	C->fctx = fctx;
	if( !preparse_clsrlists( C, func, tree ) )
		goto fail;
	if( !preparse_varlists( C, func, tree ) )
		goto fail;
	if( !compile_node( C, func, tree ) )
		goto fail;
	comp_reg_unwind( C, 0 );

	if( C->fctx->lastreg > 0xff )
	{
		sgs_Printf( C, SGS_ERROR, "[line %d] Maximum register count exceeded",
			sgsT_LineNum( tree->token ) );
		goto fail;
	}

	prefix_bytecode( C, func, 0 );

	C->fctx = NULL;
#if SGS_DUMP_BYTECODE || ( SGS_DEBUG && SGS_DEBUG_DATA )
	fctx_dump( fctx );
#endif
	fctx_destroy( C, fctx );
	return func;

fail:
	sgsBC_Free( C, func );
	C->fctx = NULL;
	fctx_destroy( C, fctx );
	C->state |= SGS_HAS_ERRORS;
	return NULL;
}

void sgsBC_Dump( sgs_CompFunc* func )
{
	sgsBC_DumpEx( func->consts.ptr, func->consts.size, func->code.ptr, func->code.size );
}

void sgsBC_DumpEx( const char* constptr, sgs_SizeVal constsize,
	const char* codeptr, sgs_SizeVal codesize )
{
	sgs_Variable* vbeg = (sgs_Variable*) constptr;
	sgs_Variable* vend = (sgs_Variable*) ( constptr + constsize );
	sgs_Variable* var = vbeg;

	printf( "{\n" );
	printf( "> constants:\n" );
	while( var < vend )
	{
		printf( "%4d = ", (int) ( var - vbeg ) );
		sgsVM_VarDump( var );
		printf( "\n" );
		var++;
	}
	printf( "> code:\n" );
	dump_opcode( (instr_t*) codeptr, codesize / sizeof( instr_t ) );
	printf( "}\n" );
}

void sgsBC_Free( SGS_CTX, sgs_CompFunc* func )
{
	sgs_Variable* vbeg = (sgs_Variable*) func->consts.ptr;
	sgs_Variable* vend = (sgs_Variable*) ( func->consts.ptr + func->consts.size );
	sgs_Variable* var = vbeg;
	while( var < vend )
	{
		sgs_Release( C, var );
		var++;
	}

	membuf_destroy( &func->code, C );
	membuf_destroy( &func->consts, C );
	membuf_destroy( &func->lnbuf, C );
	sgs_Dealloc( func );
}



/* bytecode serialization */

#define esi16( x ) ( (((x)&0xff)<<8) | (((x)>>8)&0xff) )

#define esi32( x ) (\
	(((x)&0xff)<<24) | (((x)&0xff00)<<8) |\
	(((x)>>8)&0xff00) | (((x)>>24)&0xff) )

typedef struct decoder_s
{
	SGS_CTX;
	const char* buf, *start;
	char convend;
	const char* filename;
	int32_t filename_len;
}
decoder_t;

static void esi32_array( int32_t* data, int cnt )
{
	int i;
	for( i = 0; i < cnt; ++i )
	{
		data[ i ] = esi32( data[ i ] );
	}
}


/*
	i32 size
	byte[size] data
*/
static void bc_write_sgsstring( string_t* S, SGS_CTX, MemBuf* outbuf )
{
	membuf_appbuf( outbuf, C, &S->size, sizeof( int32_t ) );
	membuf_appbuf( outbuf, C, str_cstr( S ), S->size );
}

static void bc_read_sgsstring( decoder_t* D, sgs_Variable* var )
{
	const char* buf = D->buf;
	int32_t len = AS_INT32( buf );
	if( D->convend )
		len = esi32( len );
	buf += 4;
	sgsVM_VarCreateString( D->C, var, buf, len );
	D->buf = buf + len;
}


/*
	byte type
	--if type = NULL:
	--if type = BOOL:
	byte value
	--if type = INT:
	integer value
	--if type = REAL:
	real value
	--if type = STRING:
	stringdata data
	--if type = FUNC:
	funcdata data
*/
static int bc_write_sgsfunc( func_t* F, SGS_CTX, MemBuf* outbuf );
static int bc_write_var( sgs_Variable* var, SGS_CTX, MemBuf* outbuf )
{
	int vt = BASETYPE( var->type );
	membuf_appchr( outbuf, C, vt );
	switch( vt )
	{
	case SVT_NULL: break;
	case SVT_BOOL: membuf_appchr( outbuf, C, var->data.B ); break;
	case SVT_INT: membuf_appbuf( outbuf, C, &var->data.I, sizeof( sgs_Int ) ); break;
	case SVT_REAL: membuf_appbuf( outbuf, C, &var->data.R, sizeof( sgs_Real ) ); break;
	case SVT_STRING: bc_write_sgsstring( var->data.S, C, outbuf ); break;
	case SVT_FUNC: if( !bc_write_sgsfunc( var->data.F, C, outbuf ) ) return 0; break;
	default:
		return 0;
	}
	return 1;
}

static const char* bc_read_sgsfunc( decoder_t* D, sgs_Variable* var );
static const char* bc_read_var( decoder_t* D, sgs_Variable* var )
{
	int vt = *D->buf++;
	switch( vt )
	{
	case SVT_NULL: var->type = VTC_NULL; break;
	case SVT_BOOL: var->type = VTC_BOOL; var->data.B = *D->buf++; break;
	case SVT_INT: var->type = VTC_INT; var->data.I = AS_INTEGER( D->buf ); D->buf += sizeof( sgs_Int ); break;
	case SVT_REAL: var->type = VTC_REAL; var->data.R = AS_REAL( D->buf ); D->buf += sizeof( sgs_Real ); break;
	case SVT_STRING: var->type = VTC_STRING; bc_read_sgsstring( D, var ); break;
	case SVT_FUNC: var->type = VTC_FUNC; return bc_read_sgsfunc( D, var );
	default:
		return "invalid variable type found";
	}
	return NULL;
}


/*
	var[cnt] varlist
*/
static int bc_write_varlist( sgs_Variable* vlist, SGS_CTX, int cnt, MemBuf* outbuf )
{
	int i;
	for( i = 0; i < cnt; ++i )
	{
		if( !bc_write_var( vlist + i, C, outbuf ) )
			return 0;
	}
	return 1;
}

static const char* bc_read_varlist( decoder_t* D, sgs_Variable* vlist, int cnt )
{
	int i;
	for( i = 0; i < cnt; ++i )
	{
		const char* ret = bc_read_var( D, vlist + i );
		if( ret )
		{
			cnt = i;
			for( i = 0; i < cnt; ++i )
				sgs_Release( D->C, vlist + i );
			return ret;
		}
	}
	return NULL;
}


/*
	i16 constcount
	i16 instrcount
	byte gotthis
	byte numargs
	i16 linenum
	i16[instrcount] lineinfo
	i32 funcname_size
	byte[funcname_size] funcname
	varlist consts
	instr[instrcount] instrs
*/
static int bc_write_sgsfunc( func_t* F, SGS_CTX, MemBuf* outbuf )
{
	int16_t cc = F->instr_off / sizeof( sgs_Variable ),
	        ic = ( F->size - F->instr_off ) / sizeof( instr_t );
	char gt = F->gotthis, na = F->numargs;

	membuf_appbuf( outbuf, C, &cc, sizeof( int16_t ) );
	membuf_appbuf( outbuf, C, &ic, sizeof( int16_t ) );
	membuf_appchr( outbuf, C, gt );
	membuf_appchr( outbuf, C, na );
	membuf_appbuf( outbuf, C, &F->linenum, sizeof( LineNum ) );
	membuf_appbuf( outbuf, C, F->lineinfo, sizeof( uint16_t ) * ic );
	membuf_appbuf( outbuf, C, &F->funcname.size, sizeof( int32_t ) );
	membuf_appbuf( outbuf, C, F->funcname.ptr, F->funcname.size );

	if( !bc_write_varlist( func_consts( F ), C, cc, outbuf ) )
		return 0;

	membuf_appbuf( outbuf, C, func_bytecode( F ), sizeof( instr_t ) * ic );
	return 1;
}

static void bc_read_membuf( decoder_t* D, MemBuf* out )
{
	const char* buf = D->buf;
	int32_t len = AS_INT32( buf );
	if( D->convend )
		len = esi32( len );
	buf += 4;
	membuf_setstrbuf( out, D->C, buf, len );
	D->buf = buf + len;
}
static const char* bc_read_sgsfunc( decoder_t* D, sgs_Variable* var )
{
	func_t* F;
	int32_t ioff, size;
	int16_t cc, ic;
	const char* ret;
	SGS_CTX = D->C;

	cc = AS_INT16( D->buf );
	ic = AS_INT16( D->buf + 2 );

	if( D->convend )
	{
		cc = esi16( cc );
		ic = esi16( ic );
	}
	ioff = sizeof( sgs_Variable ) * (int32_t)cc;
	size = ioff + sizeof( instr_t ) * (int32_t)ic;

	F = sgs_Alloc_a( func_t, size );
	F->refcount = 1;
	F->size = size;
	F->instr_off = ioff;
	F->gotthis = AS_INT8( D->buf + 4 );
	F->numargs = AS_INT8( D->buf + 5 );
	F->linenum = AS_INT16( D->buf + 6 );
	if( D->convend )
		F->linenum = esi16( F->linenum );
	F->lineinfo = sgs_Alloc_n( uint16_t, (int32_t)ic );
	D->buf += 8;
	memcpy( F->lineinfo, D->buf, sizeof( uint16_t ) * (int32_t) ic );
	D->buf += sizeof( uint16_t ) * (int32_t) ic;
	F->funcname = membuf_create();
	bc_read_membuf( D, &F->funcname );
	F->filename = membuf_create();
	membuf_setstrbuf( &F->filename, C, D->filename, D->filename_len );

	/* the main data */
	ret = bc_read_varlist( D, func_consts( F ), cc );
	if( ret )
		goto fail;

	memcpy( func_bytecode( F ), D->buf, size - ioff );
	if( D->convend )
		esi32_array( (int32_t*) func_bytecode( F ), ( size - ioff ) / sizeof( instr_t ) );
	D->buf += size - ioff;

	var->data.F = F;
	return NULL;

fail:
	sgs_Dealloc( F->lineinfo );
	membuf_destroy( &F->funcname, C );
	membuf_destroy( &F->filename, C );
	sgs_Dealloc( F );
	return ret;
}


/*
	-- header -- 14 bytes --
	seq "SGS\0"
	byte version_major
	byte version_minor
	byte version_incr
	byte integer_size
	byte real_size
	byte flags
	i32 filesize
	-- header end --
	i16 constcount
	i16 instrcount
	byte gotthis
	byte numargs
	varlist consts
	i32[instrcount] instrs
	linenum[instrcount] lines
*/
int sgsBC_Func2Buf( SGS_CTX, sgs_CompFunc* func, MemBuf* outbuf )
{
	char header_bytes[ 14 ] =
	{
		'S', 'G', 'S', 0,
		SGS_VERSION_MAJOR,
		SGS_VERSION_MINOR,
		SGS_VERSION_INCR,
		sizeof( sgs_Int ),
		sizeof( sgs_Real ),
		0,
		0, 0, 0, 0
	};
	header_bytes[ 9 ] = ( O32_HOST_ORDER == O32_LITTLE_ENDIAN ) ? SGSBC_FLAG_LITTLE_ENDIAN : 0;
	membuf_resize( outbuf, C, 0 );
	membuf_reserve( outbuf, C, 1000 );
	membuf_appbuf( outbuf, C, header_bytes, 14 );

	{
		int16_t cc = func->consts.size / sizeof( sgs_Variable ),
		        ic = func->code.size / sizeof( instr_t );
		char gt = func->gotthis, na = func->numargs;

		membuf_appbuf( outbuf, C, &cc, sizeof( int16_t ) );
		membuf_appbuf( outbuf, C, &ic, sizeof( int16_t ) );
		membuf_appchr( outbuf, C, gt );
		membuf_appchr( outbuf, C, na );

		sgs_BreakIf( outbuf->size != 20 );

		if( !bc_write_varlist( (sgs_Variable*) func->consts.ptr, C,
			func->consts.size / sizeof( sgs_Variable ), outbuf ) )
			return 0;

		membuf_appbuf( outbuf, C, func->code.ptr, sizeof( instr_t ) * ic );
		membuf_appbuf( outbuf, C, func->lnbuf.ptr, sizeof( LineNum ) * ic );

		AS_UINT32( outbuf->ptr + 10 ) = outbuf->size;

		return 1;
	}
}

const char* sgsBC_Buf2Func( SGS_CTX, const char* fn, const char* buf, int32_t size, sgs_CompFunc** outfunc )
{
	char flags = buf[ 9 ];
	int32_t sz = AS_INT32( buf + 10 );
	
	decoder_t D;
	{
		D.C = C;
		D.buf = NULL;
		D.start = buf;
		D.convend = ( O32_HOST_ORDER == O32_LITTLE_ENDIAN ) !=
			( ( flags & SGSBC_FLAG_LITTLE_ENDIAN ) != 0 );
		D.filename = fn;
		D.filename_len = strlen( fn );
	}
	
	if( D.convend )
		sz = esi32( sz );
	if( sz != size )
		return "incomplete file";
	{
		const char* ret;
		int16_t cc, ic;
		char gt, na;
		sgs_CompFunc* func = make_compfunc( C );
		cc = AS_INT16( buf + 14 );
		ic = AS_INT16( buf + 16 );
		gt = AS_UINT8( buf + 18 );
		na = AS_UINT8( buf + 19 );
		D.buf = buf + 20;

		if( D.convend )
		{
			cc = esi16( cc );
			ic = esi16( ic );
		}

		func->gotthis = gt;
		func->numargs = na;
		membuf_resize( &func->consts, C, sizeof( sgs_Variable ) * cc );
		membuf_resize( &func->code, C, sizeof( instr_t ) * ic );
		membuf_resize( &func->lnbuf, C, sizeof( LineNum ) * ic );

		ret = bc_read_varlist( &D, (sgs_Variable*) func->consts.ptr, cc );
		if( ret )
		{
			sgsBC_Free( C, func );
			return ret;
		}
		memcpy( func->code.ptr, D.buf, sizeof( instr_t ) * ic );
		if( D.convend )
			esi32_array( (int32_t*) func->code.ptr, ic );
		D.buf += sizeof( instr_t ) * ic;
		memcpy( func->lnbuf.ptr, D.buf, sizeof( LineNum ) * ic );

		*outfunc = func;
		return NULL;
	}
}

int sgsBC_ValidateHeader( const char* buf, int32_t size )
{
	int i;
	char validate_bytes[ 9 ] =
	{
		'S', 'G', 'S', 0,
		SGS_VERSION_MAJOR,
		SGS_VERSION_MINOR,
		SGS_VERSION_INCR,
		sizeof( sgs_Int ),
		sizeof( sgs_Real )
	};

	if( size < SGS_MIN_BC_SIZE )
		return -1;
	for( i = 0; i < 9; ++i )
	{
		if( buf[ i ] != validate_bytes[ i ] )
			return i;
	}
	return SGS_HEADER_SIZE;
}


