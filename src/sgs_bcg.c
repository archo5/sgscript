

#define SGS_INTERNAL
#define SGS_REALLY_INTERNAL

#include "sgs_int.h"



static int is_keyword( TokenList tok, const char* text )
{
	return *tok == ST_KEYWORD &&
		tok[ 1 ] == strlen( text ) &&
		strncmp( (const char*) tok + 2, text, tok[ 1 ] ) == 0;
}

#define over_limit( x, lim ) ((x)>(lim)||(x)<(-lim))


/* register allocation */

static SGS_INLINE rcpos_t comp_reg_alloc( SGS_CTX )
{
	rcpos_t out = C->fctx->regs++;
	if( out > 0xff )
	{
		C->state |= SGS_HAS_ERRORS | SGS_MUST_STOP;
		sgs_Printf( C, SGS_ERROR, "Max. register count exceeded" );
	}
	return out;
}

static SGS_INLINE void comp_reg_unwind( SGS_CTX, rcpos_t pos )
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
	func->numtmp = 0;
	func->numclsr = 0;
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
	printf( "%s R%"PRId32" <= ", name, INSTR_GET_A( I ) );
	dump_rcpos( INSTR_GET_B( I ) );
	printf( ", " );
	dump_rcpos( INSTR_GET_C( I ) );
}
static void dump_opcode_b( const char* name, instr_t I )
{
	printf( "%s R%"PRId32" <= ", name, INSTR_GET_A( I ) );
	dump_rcpos( INSTR_GET_B( I ) );
}
static void dump_opcode_b1( const char* name, instr_t I )
{
	printf( "%s ", name );
	dump_rcpos( INSTR_GET_B( I ) );
	printf( " <= " );
	dump_rcpos( INSTR_GET_C( I ) );
}
static void dump_opcode( const instr_t* ptr, size_t count )
{
	const instr_t* pend = ptr + count;
	const instr_t* pbeg = ptr;
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
		case SI_MCONCAT: printf( "MCONCAT " ); dump_rcpos( argA );
			printf( " [%d]", argB ); break;
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


static int find_var( MemBuf* S, char* str, unsigned len )
{
	char* ptr = S->ptr;
	char* pend = ptr + S->size;
	const char* cstr = str;
	int difs = 0, at = 0;
	unsigned left = len;

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

static int find_nth_var( MemBuf* S, int which, char** outstr, unsigned* outlen )
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

static int add_var( MemBuf* S, SGS_CTX, char* str, unsigned len )
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
	sgs_LineNum ln = sgsT_LineNum( node->token );
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


#define add_const_HDR \
	sgs_Variable* vbeg = (sgs_Variable*) ASSUME_ALIGNED( func->consts.ptr, 16 ); \
	sgs_Variable* vend = (sgs_Variable*) ASSUME_ALIGNED( func->consts.ptr + func->consts.size, 16 ); \
	sgs_Variable* var = vbeg; \
	sgs_Variable nvar;

static rcpos_t add_const_null( SGS_CTX, sgs_CompFunc* func )
{
	add_const_HDR;
	while( var < vend )
	{
		if( var->type == SGS_VTC_NULL )
			return (rcpos_t) ( var - vbeg ); /* WP: const limit */
		var++;
	}

	nvar.type = SGS_VTC_NULL;
	membuf_appbuf( &func->consts, C, &nvar, sizeof( nvar ) );
	return (rcpos_t) ( vend - vbeg ); /* WP: const limit */
}

static rcpos_t add_const_b( SGS_CTX, sgs_CompFunc* func, sgs_Bool bval )
{
	add_const_HDR;
	while( var < vend )
	{
		if( var->type == SGS_VTC_BOOL && var->data.B == bval )
			return (rcpos_t) ( var - vbeg ); /* WP: const limit */
		var++;
	}

	nvar.type = SGS_VTC_BOOL;
	nvar.data.B = bval;
	membuf_appbuf( &func->consts, C, &nvar, sizeof( nvar ) );
	return (rcpos_t) ( vend - vbeg ); /* WP: const limit */
}

static rcpos_t add_const_i( SGS_CTX, sgs_CompFunc* func, sgs_Int ival )
{
	add_const_HDR;
	while( var < vend )
	{
		if( var->type == SGS_VTC_INT && var->data.I == ival )
			return (rcpos_t) ( var - vbeg ); /* WP: const limit */
		var++;
	}

	nvar.type = SGS_VTC_INT;
	nvar.data.I = ival;
	membuf_appbuf( &func->consts, C, &nvar, sizeof( nvar ) );
	return (rcpos_t) ( vend - vbeg ); /* WP: const limit */
}

static rcpos_t add_const_r( SGS_CTX, sgs_CompFunc* func, sgs_Real rval )
{
	add_const_HDR;
	while( var < vend )
	{
		if( var->type == SGS_VTC_REAL && var->data.R == rval )
			return (rcpos_t) ( var - vbeg ); /* WP: const limit */
		var++;
	}

	nvar.type = SGS_VTC_REAL;
	nvar.data.R = rval;
	membuf_appbuf( &func->consts, C, &nvar, sizeof( nvar ) );
	return (rcpos_t) ( vend - vbeg ); /* WP: const limit */
}

static rcpos_t add_const_s( SGS_CTX, sgs_CompFunc* func, uint32_t len, const char* str )
{
	add_const_HDR;
	while( var < vend )
	{
		if( var->type == SGS_VTC_STRING && var->data.S->size == len
			&& memcmp( var_cstr( var ), str, len ) == 0 )
			return (rcpos_t) ( var - vbeg ); /* WP: const limit */
		var++;
	}
	
	sgs_BreakIf( len > 0x7fffffff );

	sgsVM_VarCreateString( C, &nvar, str, (int32_t) len );
	membuf_appbuf( &func->consts, C, &nvar, sizeof( nvar ) );
	return (rcpos_t) ( vend - vbeg ); /* WP: const limit */
}

static rcpos_t add_const_f( SGS_CTX, sgs_CompFunc* func, sgs_CompFunc* nf,
	const char* funcname, size_t fnsize, LineNum lnum )
{
	sgs_Variable nvar;
	rcpos_t pos;
	func_t* F = sgs_Alloc_a( func_t, nf->consts.size + nf->code.size );

	F->refcount = 1;
	/* WP: const/instruction limits */
	F->size = (uint32_t) ( nf->consts.size + nf->code.size );
	F->instr_off = (uint32_t) nf->consts.size;
	F->gotthis = nf->gotthis;
	F->numargs = nf->numargs;
	F->numtmp = nf->numtmp;
	F->numclsr = nf->numclsr;

	{
		size_t lnc = nf->lnbuf.size / sizeof( sgs_LineNum );
		F->lineinfo = sgs_Alloc_n( sgs_LineNum, lnc );
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
	
	/* WP: const limit */
	pos = (rcpos_t) ( func->consts.size / sizeof( nvar ) );
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


static SGSBOOL compile_node( SGS_CTX, sgs_CompFunc* func, FTNode* node );
static SGSBOOL compile_node_r( SGS_CTX, sgs_CompFunc* func, FTNode* node, rcpos_t* out );
static SGSBOOL compile_node_w( SGS_CTX, sgs_CompFunc* func, FTNode* node, rcpos_t src );
static SGSBOOL compile_oper( SGS_CTX, sgs_CompFunc* func, FTNode* node, rcpos_t* arg, int out, int expect );




static rcpos_t const_maybeload( SGS_CTX, sgs_CompFunc* func, FTNode* node, rcpos_t cid )
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
		rcpos_t out = comp_reg_alloc( C );
		INSTR_WRITE_EX( SI_LOADCONST, cid, out );
		return out;
	}
}

#define BC_CONSTENC( cid ) const_maybeload( C, func, node, cid )


static void compile_ident( SGS_CTX, sgs_CompFunc* func, FTNode* node, rcpos_t* out )
{
	rcpos_t pos = add_const_s( C, func, node->token[ 1 ], (const char*) node->token + 2 );
	*out = BC_CONSTENC( pos );
}


static SGSBOOL compile_ident_r( SGS_CTX, sgs_CompFunc* func, FTNode* node, rcpos_t* out )
{
	rcpos_t pos;
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
		INSTR_WRITE( SI_GETVAR, *out, pos, 0 );
	}
	return 1;
}

static SGSBOOL compile_ident_w( SGS_CTX, sgs_CompFunc* func, FTNode* node, rcpos_t src )
{
	rcpos_t pos;
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

static SGSBOOL compile_const( SGS_CTX, sgs_CompFunc* func, FTNode* node, rcpos_t* opos )
{
	if( *node->token == ST_NUMINT )
	{
		sgs_Int val;
		AS_INTEGER( val, node->token + 1 );
		*opos = BC_CONSTENC( add_const_i( C, func, val ) );
	}
	else if( *node->token == ST_NUMREAL )
	{
		sgs_Real val;
		AS_REAL( val, node->token + 1 );
		*opos = BC_CONSTENC( add_const_r( C, func, val ) );
	}
	else if( *node->token == ST_STRING )
	{
		uint32_t val;
		AS_UINT32( val, node->token + 1 );
		*opos = BC_CONSTENC( add_const_s( C, func, val, (const char*) node->token + 5 ) );
	}
	else
	{
		QPRINT( "INTERNAL ERROR: constant doesn't have a token of type int/real/string attached" );
		return 0;
	}
	return 1;
}


static SGSBOOL compile_regcopy( SGS_CTX, sgs_CompFunc* func, FTNode* node, size_t from, rcpos_t srcpos, rcpos_t dstpos )
{
	INSTR_WRITE( SI_SET, dstpos, srcpos, 0 );
	return 1;
}

static SGSBOOL compile_fcall( SGS_CTX, sgs_CompFunc* func, FTNode* node, rcpos_t* out, int expect )
{
	int i = 0, gotthis = FALSE;
	rcpos_t funcpos = -1, objpos = -1, retpos = -1;
	
	/* IF (ternary-like) */
	if( is_keyword( node->child->token, "if" ) )
	{
		FTNode* n = node->child->next->child;
		int argc = 0;
		size_t csz1, csz2, csz3;
		rcpos_t exprpos = -1, srcpos = -1;
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
		{
			uint32_t instr1, instr2;
			instr1 = INSTR_MAKE_EX( SI_JMPF, ( csz2 - csz1 ) / INSTR_SIZE, exprpos );
			instr2 = INSTR_MAKE_EX( SI_JUMP, ( csz3 - csz2 ) / INSTR_SIZE, 0 );
			memcpy( func->code.ptr + csz1 - 4, &instr1, sizeof(instr1) );
			memcpy( func->code.ptr + csz2 - 4, &instr2, sizeof(instr2) );
		}
		
		*out = retpos;
		return 1;
	}

	/* load function (for properties, object too) */
	if( node->child->type == SFT_OPER && *node->child->token == ST_OP_MMBR )
	{
		FTNode* ncc = node->child->child;
		rcpos_t proppos = -1;
		funcpos = comp_reg_alloc( C );
		FUNC_ENTER;
		if( !compile_node_r( C, func, ncc, &objpos ) ) return 0;
		if( ncc->next->type == SFT_IDENT )
			compile_ident( C, func, ncc->next, &proppos );
		else
		{
			FUNC_ENTER;
			if( !compile_node_r( C, func, ncc->next, &proppos ) ) return 0;
		}
		INSTR_WRITE( SI_GETPROP, funcpos, objpos, proppos );
	}
	else
	{
		FUNC_ENTER;
		if( !compile_node_r( C, func, node->child, &funcpos ) ) return 0;
	}

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
		rcpos_t argpos = -1;
		if( node->child->type == SFT_OPER && *node->child->token == ST_OP_MMBR )
		{
			INSTR_WRITE( SI_PUSH, 0, objpos, 0 );
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
			rcpos_t cra;
			expect--;
			cra = retpos + expect;
			out[ expect ] = cra;
			INSTR_WRITE( SI_POPR, cra, 0, 0 );
		}
	}

	return 1;
}

static SGSBOOL compile_index_r( SGS_CTX, sgs_CompFunc* func, FTNode* node, rcpos_t* out )
{
	rcpos_t var, name, opos = comp_reg_alloc( C );
	rcpos_t regpos = C->fctx->regs;
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

static SGSBOOL compile_index_w( SGS_CTX, sgs_CompFunc* func, FTNode* node, rcpos_t src )
{
	rcpos_t var, name;
	rcpos_t regpos = C->fctx->regs;
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


static SGSBOOL try_optimize_last_instr_out( SGS_CTX, sgs_CompFunc* func, FTNode* node, size_t ioff, rcpos_t* out )
{
	rcpos_t pos = -1;

	FUNC_BEGIN;
	UNUSED( C );
	
	if( ( node->type != SFT_IDENT && node->type != SFT_ARGMT ) || *node->token != ST_IDENT )
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
		instr_t I;
		AS_( I, func->code.ptr + ioff, instr_t );
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
			{
				char* dummy0 = NULL;
				unsigned dummy1 = 0;
				if( find_nth_var( &C->fctx->vars, INSTR_GET_A( I ), &dummy0, &dummy1 ) )
					goto cannot;
			}
			I = INSTR_MAKE( op, pos, argB, argC );
			memcpy( func->code.ptr + ioff, &I, sizeof(I) );
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


static SGSBOOL compile_oper( SGS_CTX, sgs_CompFunc* func, FTNode* node, rcpos_t* arg, int out, int expect )
{
	int assign = ST_OP_ASSIGN( *node->token );
	FUNC_BEGIN;

	/* Boolean ops */
	if( ST_OP_BOOL( *node->token ) )
	{
		int jin;
		rcpos_t ireg1, ireg2, oreg = 0;
		size_t csz, csz2;

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
		{
			instr_t instr;
			/* WP: instruction limit */
			ptrdiff_t jmp_off = (ptrdiff_t) ( func->code.size - csz ) / INSTR_SIZE;
			instr = INSTR_MAKE_EX( jin, jmp_off, ireg1 );
			memcpy( func->code.ptr + csz - 4, &instr, sizeof(instr) );
		}

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
		{
			instr_t instr;
			/* WP: instruction limit */
			ptrdiff_t jmp_off = (ptrdiff_t) ( func->code.size - csz2 ) / INSTR_SIZE;
			instr = INSTR_MAKE_EX( SI_JUMP, jmp_off, 0 );
			memcpy( func->code.ptr + csz2 - 4, &instr, sizeof(instr) );
		}

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
		rcpos_t ireg, oreg;

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
			rcpos_t ireg;
			size_t isb = func->code.size;
			
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
				
				FUNC_ENTER;
				if( !try_optimize_last_instr_out( C, func, node->child, isb, arg ) )
				{
					/* just set the contents */
					FUNC_ENTER;
					if( !compile_node_w( C, func, node->child, ireg ) ) goto fail;
				}
				
				if( arg )
				{
					FUNC_ENTER;
					if( !compile_node_r( C, func, node->child, arg ) ) goto fail;
				}
			}
		}
		/* 3+ operands (MCONCAT only) */
		else if( *node->token == ST_OP_CATEQ && node->child &&
			node->child->next && node->child->next->next )
		{
			int numch = 0;
			FTNode* cur = node->child;
			rcpos_t ireg, oreg = comp_reg_alloc( C );
			if( C->state & SGS_MUST_STOP )
				goto fail;

			/* get source data registers */
			while( cur )
			{
				int32_t bkup = C->fctx->regs;
				
				FUNC_ENTER;
				if( !compile_node_r( C, func, cur, &ireg ) ) goto fail;
				INSTR_WRITE( SI_PUSH, 0, ireg, 0 );
				numch++;
				cur = cur->next;
				
				comp_reg_unwind( C, bkup );
			}
			
			INSTR_WRITE( SI_MCONCAT, oreg, numch, 0 );

			if( arg )
				*arg = oreg;

			/* compile write */
			FUNC_ENTER;
			if( !compile_node_w( C, func, node->child, oreg ) ) goto fail;
		}
		/* 2 operands */
		else
		{
			int op;
			rcpos_t ireg1, ireg2, oreg = comp_reg_alloc( C );
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
		rcpos_t ireg1, ireg2, oreg;

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
		/* 3+ operands (MCONCAT only) */
		else if( *node->token == ST_OP_CAT && node->child &&
			node->child->next && node->child->next->next )
		{
			int numch = 0;
			FTNode* cur = node->child;
			rcpos_t ireg;
			oreg = comp_reg_alloc( C );
			if( C->state & SGS_MUST_STOP )
				goto fail;

			/* get source data registers */
			while( cur )
			{
				int32_t bkup = C->fctx->regs;
				
				FUNC_ENTER;
				if( !compile_node_r( C, func, cur, &ireg ) ) goto fail;
				INSTR_WRITE( SI_PUSH, 0, ireg, 0 );
				numch++;
				cur = cur->next;
				
				comp_reg_unwind( C, bkup );
			}
			
			if( arg )
				*arg = oreg;
			
			/* compile op */
			INSTR_WRITE( SI_MCONCAT, oreg, numch, 0 );
		}
		else
		{
			int op;

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


static SGSBOOL compile_breaks( SGS_CTX, sgs_CompFunc* func, FTNode* node, uint8_t iscont )
{
	sgs_BreakInfo* binfo = C->fctx->binfo, *prev = NULL;
	while( binfo )
	{
		if( binfo->numlp == C->fctx->loops && binfo->iscont == iscont )
		{
			/* WP: jump limit */
			ptrdiff_t off = (ptrdiff_t) ( func->code.size - binfo->jdoff ) / INSTR_SIZE - 1;
			if( over_limit( off, 32767 ) )
			{
				QPRINT( "Max. jump limit exceeded (32767 instructions) @ break/continue; reduce size of loops" );
				return 0;
			}
			instr_t instr = INSTR_MAKE_EX( SI_JUMP, off, 0 );
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
	
	if( C->fctx->outclsr > C->fctx->inclsr )
	{
		int i;
		instr_t I = INSTR_MAKE( SI_GENCLSR, C->fctx->outclsr - C->fctx->inclsr, 0, 0 );
		uint16_t ln = 0;
		membuf_appbuf( &ncode, C, &I, sizeof( I ) );
		membuf_appbuf( &nlnbuf, C, &ln, sizeof( ln ) );

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
			I = INSTR_MAKE( SI_SETCLSR, 0, which, i );
			membuf_appbuf( &ncode, C, &I, sizeof( I ) );
			membuf_appbuf( &nlnbuf, C, &ln, sizeof( ln ) );
		}
	}

	membuf_appbuf( &ncode, C, func->code.ptr, func->code.size );
	membuf_appbuf( &nlnbuf, C, func->lnbuf.ptr, func->lnbuf.size );

	membuf_destroy( &func->code, C );
	membuf_destroy( &func->lnbuf, C );
	
	/* WP: both lastreg and args cannot exceed 255, lastreg includes args */
	func->numtmp = (uint8_t) ( C->fctx->lastreg - args );
	func->code = ncode;
	func->lnbuf = nlnbuf;
}


static SGSBOOL compile_func( SGS_CTX, sgs_CompFunc* func, FTNode* node, rcpos_t* out )
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
		QPRINT( "Max. register count exceeded" );
		goto fail;
	}
	if( C->fctx->inclsr > 0xff )
	{
		QPRINT( "Max. closure count exceeded" );
		goto fail;
	}
	
	prefix_bytecode( C, nf, args );
	/* WP: closure limit */
	nf->numclsr = (uint8_t) ( clsrcnt = C->fctx->inclsr );

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
			rcpos_t ro = comp_reg_alloc( C );
			FTNode* uli = n_uselist->child;
			for( i = 0; i < clsrcnt; ++i )
			{
				INSTR_WRITE( SI_PUSHCLSR, find_varT( &bkfctx->clsr, uli->token ), 0, 0 );
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


static SGSBOOL compile_node_w( SGS_CTX, sgs_CompFunc* func, FTNode* node, rcpos_t src )
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
static SGSBOOL compile_node_r( SGS_CTX, sgs_CompFunc* func, FTNode* node, rcpos_t* out )
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
			rcpos_t pos = 0;
			int args = 0;
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
			rcpos_t pos = 0;
			int args = 0;
			FTNode* n = node->child;
			while( n )
			{
				rcpos_t bkup = C->fctx->regs;
				pos = 0;
				if( args % 2 == 0 )
				{
					if( *n->token == ST_STRING )
					{
						uint32_t string_len;
						AS_UINT32( string_len, n->token + 1 );
						pos = BC_CONSTENC( add_const_s( C, func, string_len, (const char*) n->token + 5 ) );
					}
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

static SGSBOOL compile_for_explist( SGS_CTX, sgs_CompFunc* func, FTNode* node, rcpos_t* out )
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

static SGSBOOL compile_node( SGS_CTX, sgs_CompFunc* func, FTNode* node )
{
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
			rcpos_t regstate = C->fctx->regs;
			int num = 0;
			FTNode* n = node->child;
			while( n )
			{
				rcpos_t arg = 0;
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
			rcpos_t regstate = C->fctx->regs;
			FUNC_ENTER;
			if( !compile_node( C, func, node ) ) goto fail;
			node = node->next;
			comp_reg_unwind( C, regstate );
		}
		break;

	case SFT_IFELSE:
		FUNC_HIT( "IF/ELSE" );
		{
			rcpos_t arg = 0;
			rcpos_t regstate = C->fctx->regs;
			FUNC_ENTER;
			if( !compile_node_r( C, func, node->child, &arg ) ) goto fail;
			comp_reg_unwind( C, regstate );
			INSTR_WRITE_PCH();
			{
				size_t jp1, jp2 = 0, jp3 = 0;
				jp1 = func->code.size;

				regstate = C->fctx->regs;
				FUNC_ENTER;
				if( !compile_node( C, func, node->child->next ) ) goto fail;
				comp_reg_unwind( C, regstate );

				if( node->child->next->next )
				{
					INSTR_WRITE_PCH();
					jp2 = func->code.size;
					{
						instr_t instr;
						/* WP: jump limit */
						ptrdiff_t jmp_off = (ptrdiff_t) ( jp2 - jp1 ) / INSTR_SIZE;
						if( over_limit( jmp_off, 32767 ) )
						{
							QPRINT( "Max. jump limit exceeded (32767 instructions) @ if/else; reduce size of construct" );
							goto fail;
						}
						instr = INSTR_MAKE_EX( SI_JMPF, jmp_off, arg );
						memcpy( func->code.ptr + jp1 - 4, &instr, sizeof(instr) );
					}

					regstate = C->fctx->regs;
					FUNC_ENTER;
					if( !compile_node( C, func, node->child->next->next ) ) goto fail;
					jp3 = func->code.size;
					{
						instr_t instr;
						/* WP: jump limit */
						ptrdiff_t jmp_off = (ptrdiff_t) ( jp3 - jp2 ) / INSTR_SIZE;
						if( over_limit( jmp_off, 32767 ) )
						{
							QPRINT( "Max. jump limit exceeded (32767 instructions) @ if/else; reduce size of construct" );
							goto fail;
						}
						instr = INSTR_MAKE_EX( SI_JUMP, jmp_off, 0 );
						memcpy( func->code.ptr + jp2 - 4, &instr, sizeof(instr) );
					}
					comp_reg_unwind( C, regstate );
				}
				else
				{
					instr_t instr;
					/* WP: jump limit */
					ptrdiff_t jmp_off = (ptrdiff_t) ( func->code.size - jp1 ) / INSTR_SIZE;
					if( over_limit( jmp_off, 32767 ) )
					{
						QPRINT( "Max. jump limit exceeded (32767 instructions) @ if/else; reduce size of construct" );
						goto fail;
					}
					instr = INSTR_MAKE_EX( SI_JMPF, jmp_off, arg );
					memcpy( func->code.ptr + jp1 - 4, &instr, sizeof(instr) );
				}
			}
		}
		break;

	case SFT_WHILE:
		FUNC_HIT( "WHILE" );
		{
			size_t codesize;
			rcpos_t arg = -1;
			rcpos_t regstate = C->fctx->regs;
			C->fctx->loops++;
			codesize = func->code.size;
			FUNC_ENTER;
			if( !compile_node_r( C, func, node->child, &arg ) ) goto fail; /* test */
			comp_reg_unwind( C, regstate );
			INSTR_WRITE_PCH();
			{
				ptrdiff_t off;
				size_t jp1, jp2 = 0;
				jp1 = func->code.size;

				regstate = C->fctx->regs;
				FUNC_ENTER;
				if( !compile_node( C, func, node->child->next ) ) goto fail; /* while */
				comp_reg_unwind( C, regstate );

				if( !compile_breaks( C, func, node, 1 ) )
					goto fail;

				jp2 = func->code.size;
				/* WP: jump limit */
				off = (ptrdiff_t) ( codesize - jp2 ) / INSTR_SIZE - 1;
				if( over_limit( off, 32767 ) )
				{
					QPRINT( "Max. jump limit exceeded (32767 instructions) @ while; reduce size of loop" );
					goto fail;
				}
				INSTR_WRITE_EX( SI_JUMP, off, 0 );
				{
					instr_t instr;
					instr = INSTR_MAKE_EX( SI_JMPF, ( jp2 - jp1 ) / INSTR_SIZE + 1, arg );
					memcpy( func->code.ptr + jp1 - 4, &instr, sizeof(instr) );
				}
			}
			if( !compile_breaks( C, func, node, 0 ) )
				goto fail;
			C->fctx->loops--;
		}
		break;

	case SFT_DOWHILE:
		FUNC_HIT( "DO/WHILE" );
		{
			size_t codesize;
			rcpos_t regstate = C->fctx->regs;
			rcpos_t arg = -1;
			ptrdiff_t off;
			C->fctx->loops++;
			codesize = func->code.size;
			{
				FUNC_ENTER;
				if( !compile_node( C, func, node->child->next ) ) goto fail; /* while */
				comp_reg_unwind( C, regstate );

				if( !compile_breaks( C, func, node, 1 ) )
					goto fail;
			}
			FUNC_ENTER;
			if( !compile_node_r( C, func, node->child, &arg ) ) goto fail; /* test */
			comp_reg_unwind( C, regstate );
			/* WP: jump limit */
			off = (ptrdiff_t) ( codesize - func->code.size ) / INSTR_SIZE - 1;
			if( over_limit( off, 32767 ) )
			{
				QPRINT( "Max. jump limit exceeded (32767 instructions) @ do/while; reduce size of loop" );
				goto fail;
			}
			INSTR_WRITE_EX( SI_JMPT, off, arg );
			if( !compile_breaks( C, func, node, 0 ) )
				goto fail;
			C->fctx->loops--;
		}
		break;

	case SFT_FOR:
		FUNC_HIT( "FOR" );
		{
			size_t codesize;
			rcpos_t regstate = C->fctx->regs;
			rcpos_t arg = -1;
			C->fctx->loops++;
			FUNC_ENTER;
			if( !compile_node( C, func, node->child ) ) goto fail; /* init */
			comp_reg_unwind( C, regstate );
			codesize = func->code.size;
			FUNC_ENTER;
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

				FUNC_ENTER;
				if( !compile_node( C, func, node->child->next->next->next ) ) goto fail; /* block */
				comp_reg_unwind( C, regstate );

				if( !compile_breaks( C, func, node, 1 ) )
					goto fail;
				FUNC_ENTER;
				if( !compile_node( C, func, node->child->next->next ) ) goto fail; /* incr */
				comp_reg_unwind( C, regstate );

				jp2 = func->code.size;
				/* WP: jump limit */
				off = (ptrdiff_t) ( codesize - jp2 ) / INSTR_SIZE - 1;
				if( over_limit( off, 32767 ) )
				{
					QPRINT( "Max. jump limit exceeded (32767 instructions) @ for; reduce size of loop" );
					goto fail;
				}
				INSTR_WRITE_EX( SI_JUMP, off, 0 );
				if( arg != -1 )
				{
					instr_t instr;
					instr = INSTR_MAKE_EX( SI_JMPF, ( jp2 - jp1 ) / INSTR_SIZE + 1, arg );
					memcpy( func->code.ptr + jp1 - 4, &instr, sizeof(instr) );
				}
			}
			if( !compile_breaks( C, func, node, 0 ) )
				goto fail;
			C->fctx->loops--;
		}
		break;
		
	case SFT_FOREACH:
		FUNC_HIT( "FOREACH" );
		{
			size_t codesize, jp1, jp2;
			rcpos_t var, iter, key = -1, val = -1;
			rcpos_t regstate, regstate2;
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
			codesize = func->code.size;
			INSTR_WRITE_PCH();
			jp1 = func->code.size;
			INSTR_WRITE( SI_FORLOAD, iter, key, val );
			
			{
				ptrdiff_t off = 0;
				
				/* write to key variable */
				if( node->child->type != SFT_NULL && !compile_ident_w( C, func, node->child, key ) ) goto fail;
				
				/* write to value variable */
				if( node->child->next->type != SFT_NULL && !compile_ident_w( C, func, node->child->next, val ) ) goto fail;
				
				FUNC_ENTER;
				if( !compile_node( C, func, node->child->next->next->next ) ) goto fail; /* block */
				comp_reg_unwind( C, regstate );
				
				if( !compile_breaks( C, func, node, 1 ) )
					goto fail;
				
				jp2 = func->code.size;
				/* WP: jump limit */
				off = (ptrdiff_t) ( codesize - jp2 ) / INSTR_SIZE - 1;
				if( over_limit( off, 32767 ) )
				{
					QPRINT( "Max. jump limit exceeded (32767 instructions) @ foreach; reduce size of loop" );
					goto fail;
				}
				INSTR_WRITE_EX( SI_JUMP, off, 0 );
				{
					instr_t instr;
					instr = INSTR_MAKE_EX( SI_FORJUMP, ( func->code.size - jp1 ) / INSTR_SIZE, iter );
					memcpy( func->code.ptr + jp1 - 4, &instr, sizeof(instr) );
				}
			}
			
			if( !compile_breaks( C, func, node, 0 ) )
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
			{
				sgs_Int tint;
				AS_INTEGER( tint, tl + 1 );
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
			/* WP: instruction limit, max loop depth */
			fctx_binfo_add( C, C->fctx, (uint32_t) func->code.size, (uint16_t)( C->fctx->loops + 1 - blev ), FALSE );
			INSTR_WRITE_PCH();
		}
		break;

	case SFT_CONT:
		FUNC_HIT( "CONTINUE" );
		{
			TokenList tl = sgsT_Next( node->token );
			int32_t blev = 1;
			if( *tl == ST_NUMINT )
			{
				sgs_Int tint;
				AS_INTEGER( tint, tl + 1 );
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
			/* WP: instruction limit, max loop depth */
			fctx_binfo_add( C, C->fctx, (uint32_t) func->code.size, (uint16_t)( C->fctx->loops + 1 - blev ), TRUE );
			INSTR_WRITE_PCH();
		}
		break;

	case SFT_FUNC:
		FUNC_HIT( "FUNC" );
		{
			rcpos_t pos;
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
			rcpos_t regstate = C->fctx->regs;
			FTNode* pp = node->child;
			while( pp )
			{
				if( pp->child )
				{
					rcpos_t arg = -1;
					size_t lastsize = func->code.size;
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

void sgsBC_DumpEx( const char* constptr, size_t constsize,
	const char* codeptr, size_t codesize )
{
	const sgs_Variable* vbeg = (const sgs_Variable*) ASSUME_ALIGNED( constptr, 16 );
	const sgs_Variable* vend = (const sgs_Variable*) ASSUME_ALIGNED( constptr + constsize, 16 );
	const sgs_Variable* var = vbeg;

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
	dump_opcode( (const instr_t*) ASSUME_ALIGNED( codeptr, 4 ), codesize / sizeof( instr_t ) );
	printf( "}\n" );
}

void sgsBC_Free( SGS_CTX, sgs_CompFunc* func )
{
	sgs_Variable* vbeg = (sgs_Variable*) ASSUME_ALIGNED( func->consts.ptr, 16 );
	sgs_Variable* vend = (sgs_Variable*) ASSUME_ALIGNED( func->consts.ptr + func->consts.size, 16 );
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
	size_t filename_len;
}
decoder_t;

static void esi32_array( uint32_t* data, unsigned cnt )
{
	unsigned i;
	for( i = 0; i < cnt; ++i )
	{
		data[ i ] = (uint32_t) esi32( data[ i ] );
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
	int32_t len;
	AS_INT32( len, buf );
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
	uint8_t vt = BASETYPE( var->type );
	/* WP: don't care about the sign when serializing bitfield */
	membuf_appchr( outbuf, C, (char) vt );
	switch( vt )
	{
	case SVT_NULL: break;
	/* WP: var->data.B can only store 0/1 */
	case SVT_BOOL: membuf_appchr( outbuf, C, (char) var->data.B ); break;
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
	case SVT_INT: var->type = VTC_INT; AS_INTEGER( var->data.I, D->buf ); D->buf += sizeof( sgs_Int ); break;
	case SVT_REAL: var->type = VTC_REAL; AS_REAL( var->data.R, D->buf ); D->buf += sizeof( sgs_Real ); break;
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
	byte numtmp
	byte numclsr
	i16 linenum
	i16[instrcount] lineinfo
	i32 funcname_size
	byte[funcname_size] funcname
	varlist consts
	instr[instrcount] instrs
*/
static int bc_write_sgsfunc( func_t* F, SGS_CTX, MemBuf* outbuf )
{
	size_t size = F->funcname.size;
	uint16_t cc, ic;
	uint8_t gntc[4] = { F->gotthis, F->numargs, F->numtmp, F->numclsr };
	
	/* WP: const/instruction limits */
	cc = (uint16_t)( F->instr_off / sizeof( sgs_Variable ) );
	ic = (uint16_t)( ( F->size - F->instr_off ) / sizeof( instr_t ) );

	membuf_appbuf( outbuf, C, &cc, sizeof( cc ) );
	membuf_appbuf( outbuf, C, &ic, sizeof( ic ) );
	membuf_appbuf( outbuf, C, gntc, 4 );
	membuf_appbuf( outbuf, C, &F->linenum, sizeof( LineNum ) );
	membuf_appbuf( outbuf, C, F->lineinfo, sizeof( uint16_t ) * ic );
	membuf_appbuf( outbuf, C, &size, sizeof( size ) );
	membuf_appbuf( outbuf, C, F->funcname.ptr, F->funcname.size );

	if( !bc_write_varlist( func_consts( F ), C, cc, outbuf ) )
		return 0;

	membuf_appbuf( outbuf, C, func_bytecode( F ), sizeof( instr_t ) * ic );
	return 1;
}

static void bc_read_membuf( decoder_t* D, MemBuf* out )
{
	const char* buf = D->buf;
	uint32_t len;
	AS_UINT32( len, buf );
	if( D->convend )
		len = (uint32_t) esi32( len );
	buf += 4;
	membuf_setstrbuf( out, D->C, buf, len );
	D->buf = buf + len;
}
static const char* bc_read_sgsfunc( decoder_t* D, sgs_Variable* var )
{
	func_t* F;
	uint32_t coff, ioff, size;
	uint16_t cc, ic;
	const char* ret;
	SGS_CTX = D->C;

	AS_UINT16( cc, D->buf );
	AS_UINT16( ic, D->buf + 2 );

	if( D->convend )
	{
		/* WP: int promotion will not affect the result */
		cc = (uint16_t) esi16( cc );
		ic = (uint16_t) esi16( ic );
	}
	/* WP: const/instruction limits */
	ioff = (uint32_t) sizeof( sgs_Variable ) * cc;
	coff = (uint32_t) sizeof( instr_t ) * ic;
	size = ioff + coff;

	F = sgs_Alloc_a( func_t, size );
	F->refcount = 1;
	F->size = size;
	F->instr_off = ioff;
	AS_UINT8( F->gotthis, D->buf + 4 );
	AS_UINT8( F->numargs, D->buf + 5 );
	AS_UINT8( F->numtmp, D->buf + 6 );
	AS_UINT8( F->numclsr, D->buf + 7 );
	AS_INT16( F->linenum, D->buf + 8 );
	if( D->convend )
		F->linenum = (sgs_LineNum) esi16( F->linenum );
	F->lineinfo = sgs_Alloc_n( sgs_LineNum, ic );
	D->buf += 10;
	memcpy( F->lineinfo, D->buf, sizeof( sgs_LineNum ) * ic );
	D->buf += sizeof( sgs_LineNum ) * ic;
	F->funcname = membuf_create();
	bc_read_membuf( D, &F->funcname );
	F->filename = membuf_create();
	membuf_setstrbuf( &F->filename, C, D->filename, D->filename_len );

	/* the main data */
	ret = bc_read_varlist( D, func_consts( F ), cc );
	if( ret )
		goto fail;

	memcpy( func_bytecode( F ), D->buf, coff );
	if( D->convend )
		esi32_array( func_bytecode( F ), coff / sizeof( instr_t ) );
	D->buf += coff;

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
	u32 filesize
	-- header end --
	i16 constcount
	i16 instrcount
	byte gotthis
	byte numargs
	byte numtmp
	byte numclsr
	-- -- -- -- -- 8 bytes in the previous section
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
		uint16_t cc, ic;
		uint8_t gntc[4] = { func->gotthis, func->numargs, func->numtmp, func->numclsr };
		
		/* max. count: 65535, max. variable size: 16 bytes */
		cc = (uint16_t) func->consts.size / sizeof( sgs_Variable );
		ic = (uint16_t) func->code.size / sizeof( instr_t );
		
		membuf_appbuf( outbuf, C, &cc, sizeof( cc ) );
		membuf_appbuf( outbuf, C, &ic, sizeof( ic ) );
		membuf_appbuf( outbuf, C, gntc, 4 );
		
		sgs_BreakIf( outbuf->size != 22 );
		
		if( !bc_write_varlist( (sgs_Variable*) ASSUME_ALIGNED( func->consts.ptr, 16 ), C,
			cc, outbuf ) )
			return 0;
		
		membuf_appbuf( outbuf, C, func->code.ptr, sizeof( instr_t ) * ic );
		membuf_appbuf( outbuf, C, func->lnbuf.ptr, sizeof( LineNum ) * ic );
		
		{
			/* WP: bytecode size limit */
			uint32_t outbufsize = (uint32_t) outbuf->size;
			memcpy( outbuf->ptr + 10, &outbufsize, sizeof(uint32_t) );
		}
		
		return 1;
	}
}

const char* sgsBC_Buf2Func( SGS_CTX, const char* fn, const char* buf, size_t size, sgs_CompFunc** outfunc )
{
	char flags = buf[ 9 ];
	uint32_t sz;
	
	AS_UINT32( sz, buf + 10 );
	
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
	if( (size_t) sz != size )
		return "incomplete file";
	{
		const char* ret;
		uint16_t cc, ic;
		sgs_CompFunc* func = make_compfunc( C );
		AS_UINT16( cc, buf + 14 );
		AS_UINT16( ic, buf + 16 );
		AS_UINT8( func->gotthis, buf + 18 );
		AS_UINT8( func->numargs, buf + 19 );
		AS_UINT8( func->numtmp, buf + 20 );
		AS_UINT8( func->numclsr, buf + 21 );
		D.buf = buf + 22;
		
		if( D.convend )
		{
			/* WP: int promotion will not affect the result */
			cc = (uint16_t) esi16( cc );
			ic = (uint16_t) esi16( ic );
		}
		
		membuf_resize( &func->consts, C, sizeof( sgs_Variable ) * cc );
		membuf_resize( &func->code, C, sizeof( instr_t ) * ic );
		membuf_resize( &func->lnbuf, C, sizeof( LineNum ) * ic );
		
		ret = bc_read_varlist( &D, (sgs_Variable*) ASSUME_ALIGNED( func->consts.ptr, 16 ), cc );
		if( ret )
		{
			sgsBC_Free( C, func );
			return ret;
		}
		memcpy( func->code.ptr, D.buf, sizeof( instr_t ) * ic );
		if( D.convend )
			esi32_array( (instr_t*) ASSUME_ALIGNED( func->code.ptr, 4 ), ic );
		D.buf += sizeof( instr_t ) * ic;
		memcpy( func->lnbuf.ptr, D.buf, sizeof( LineNum ) * ic );

		*outfunc = func;
		return NULL;
	}
}

int sgsBC_ValidateHeader( const char* buf, size_t size )
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


