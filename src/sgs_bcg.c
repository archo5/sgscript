

#include "sgs_bcg.h"
#include "sgs_ctx.h"
#include "sgs_proc.h"



static int is_keyword( TokenList tok, const char* text )
{
	return *tok == ST_KEYWORD && tok[ 1 ] == strlen( text ) && strncmp( (const char*) tok + 2, text, tok[ 1 ] ) == 0;
}


/* register allocation */

static SGS_INLINE int16_t comp_reg_alloc( SGS_CTX )
{
	int out = C->fctx->regs++;
	if( out > 0xff )
	{
		C->state |= SGS_HAS_ERRORS;
		sgs_Printf( C, SGS_ERROR, -1, "Max. register count exceeded" );
	}
	return out;
}

static SGS_INLINE void comp_reg_unwind( SGS_CTX, int32_t pos )
{
	if( C->fctx->regs > C->fctx->lastreg )
		C->fctx->lastreg = C->fctx->regs;
	C->fctx->regs = pos;
}


static sgs_CompFunc* make_compfunc()
{
	sgs_CompFunc* func = sgs_Alloc( sgs_CompFunc );
	func->consts = membuf_create();
	func->code = membuf_create();
	func->lnbuf = membuf_create();
	func->gotthis = FALSE;
	func->numargs = 0;
	return func;
}


static void fctx_binfo_add( sgs_FuncCtx* fctx, uint32_t ioff, uint16_t loop, uint8_t iscont )
{
	sgs_BreakInfo* binfo = sgs_Alloc( sgs_BreakInfo );
	binfo->jdoff = ioff;
	binfo->numlp = loop;
	binfo->iscont = iscont;
	binfo->next = fctx->binfo;
	fctx->binfo = binfo;
}

static void fctx_binfo_rem( sgs_FuncCtx* fctx, sgs_BreakInfo* prev )
{
	sgs_BreakInfo* pn;
	if( prev )
	{
		pn = prev->next;
		prev->next = prev->next->next;
		sgs_Free( pn );
	}
	else
	{
		pn = fctx->binfo;
		fctx->binfo = fctx->binfo->next;
		sgs_Free( pn );
	}
}

static sgs_FuncCtx* fctx_create()
{
	sgs_FuncCtx* fctx = sgs_Alloc( sgs_FuncCtx );
	fctx->func = TRUE;
	fctx->regs = 0;
	fctx->lastreg = -1;
	fctx->vars = strbuf_create();
	fctx->gvars = strbuf_create();
	fctx->loops = 0;
	fctx->binfo = NULL;
	return fctx;
}

static void fctx_destroy( sgs_FuncCtx* fctx )
{
	while( fctx->binfo )
		fctx_binfo_rem( fctx, NULL );
	strbuf_destroy( &fctx->vars );
	strbuf_destroy( &fctx->gvars );
	sgs_Free( fctx );
}

static void fctx_dump( sgs_FuncCtx* fctx )
{
	printf( "Type: %s\nGlobals: %s\nVariables: %s\n", fctx->func ? "Function" : "Main code", fctx->gvars.ptr, fctx->vars.ptr );
}


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
		int op = INSTR_GET_OP( I ), argA = INSTR_GET_A( I ), argB = INSTR_GET_B( I ),
			argC = INSTR_GET_C( I ), argE = INSTR_GET_E( I );

		printf( "\t%04d |  ", ptr - pbeg - 1 );

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
		case SI_JMPT: printf( "JMP_T " ); dump_rcpos( argC ); printf( ", %d", (int) (int16_t) argE ); break;
		case SI_JMPF: printf( "JMP_F " ); dump_rcpos( argC ); printf( ", %d", (int) (int16_t) argE ); break;
		case SI_CALL: printf( "CALL args:%d%s expect:%d func:", argB & 0xff, ( argB & 0x100 ) ? ",method" : "",
							argA ); dump_rcpos( argC ); break;

		case SI_FORPREP: printf( "FOR_PREP " ); dump_rcpos( argA ); printf( " <= " ); dump_rcpos( argB ); break;
		case SI_FORNEXT: printf( "FOR_NEXT " ); dump_rcpos( argA ); printf( ", " ); dump_rcpos( argB );
							printf( " <= " ); dump_rcpos( argC ); break;

		DOP_B( GETVAR );
		case SI_SETVAR: dump_opcode_b1( "SETVAR", I ); break;
		DOP_A( GETPROP );
		DOP_A( SETPROP );
		DOP_A( GETINDEX );
		DOP_A( SETINDEX );

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

		case SI_ARRAY: printf( "ARRAY args:%d output:", argB ); dump_rcpos( argA ); break;
		case SI_DICT: printf( "DICT args:%d output:", argB ); dump_rcpos( argA ); break;

		default: printf( "<error>" ); break;
		}
		printf( "\n" );
	}
}


static int find_var( StrBuf* S, char* str, int len )
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

static int add_var( StrBuf* S, char* str, int len )
{
	int pos = find_var( S, str, len );
	if( pos < 0 )
	{
		strbuf_appbuf( S, str, len );
		strbuf_appchr( S, '=' );
		return TRUE;
	}
	return FALSE;
}


/* simplifies writing code */
static void add_instr( sgs_CompFunc* func, FTNode* node, instr_t I )
{
	uint16_t ln = sgsT_LineNum( node->token );
	membuf_appbuf( &func->lnbuf, &ln, sizeof( ln ) );
	membuf_appbuf( &func->code, &I, sizeof( I ) );
}
#define INSTR_N( i, n ) add_instr( func, n, i )
#define INSTR( i )      INSTR_N( i, node )
#define INSTR_WRITE( op, a, b, c ) INSTR( INSTR_MAKE( op, a, b, c ) )
#define INSTR_WRITE_EX( op, ex, c ) INSTR( INSTR_MAKE_EX( op, ex, c ) )
#define INSTR_WRITE_PCH() INSTR_WRITE( 63, 0, 0, 0 )


static int preparse_varlist( SGS_CTX, FTNode* node )
{
	node = node->child;
	while( node )
	{
		if( find_var( &C->fctx->gvars, (char*) node->token + 2, node->token[ 1 ] ) >= 0 )
		{
			sgs_Printf( C, SGS_ERROR, sgsT_LineNum( node->token ), "Variable storage redefined: global -> local" );
			return FALSE;
		}
		if( add_var( &C->fctx->vars, (char*) node->token + 2, node->token[ 1 ] ) )
			comp_reg_alloc( C );
		node = node->next;
	}
	return TRUE;
}

static int preparse_gvlist( SGS_CTX, FTNode* node )
{
	node = node->child;
	while( node )
	{
		if( find_var( &C->fctx->vars, (char*) node->token + 2, node->token[ 1 ] ) >= 0 )
		{
			sgs_Printf( C, SGS_ERROR, sgsT_LineNum( node->token ), "Variable storage redefined: local -> global" );
			return FALSE;
		}
		add_var( &C->fctx->gvars, (char*) node->token + 2, node->token[ 1 ] );
		node = node->next;
	}
	return TRUE;
}

static int preparse_varlists( SGS_CTX, sgs_CompFunc* func, FTNode* node )
{
	int ret = 1;
	if( node->type == SFT_VARLIST )
		ret &= preparse_varlist( C, node );
	else if( node->type == SFT_GVLIST )
		ret &= preparse_gvlist( C, node );
	else if( node->type == SFT_OPER )
	{
		if( ST_OP_ASSIGN( *node->token ) && node->child && node->child->type == SFT_IDENT )
		{
			if( find_var( &C->fctx->gvars, (char*) node->child->token + 2, node->child->token[ 1 ] ) == -1 &&
				add_var( &C->fctx->vars, (char*) node->child->token + 2, node->child->token[ 1 ] ) )
				comp_reg_alloc( C );
		}
	}
	else if( node->type == SFT_FOREACH )
	{
		if( find_var( &C->fctx->gvars, (char*) node->token + 2, node->token[ 1 ] ) >= 0 )
		{
			sgs_Printf( C, SGS_ERROR, sgsT_LineNum( node->token ), "Variable storage redefined (foreach key variable cannot be global): global -> local" );
			ret = FALSE;
		}
		else if( add_var( &C->fctx->vars, (char*) node->child->token + 2, node->child->token[ 1 ] ) )
			comp_reg_alloc( C );

		ret &= preparse_varlists( C, func, node->child->next );
	}
	else if( node->child && node->type != SFT_FUNC )
		ret &= preparse_varlists( C, func, node->child );
	if( node->next )
		ret &= preparse_varlists( C, func, node->next );
	return ret;
}

static int preparse_arglist( SGS_CTX, sgs_CompFunc* func, FTNode* node )
{
	node = node->child;
	if( node && is_keyword( node->token, "this" ) )
	{
		func->gotthis = TRUE;
		func->numargs--;
	}
	while( node )
	{
		if( !add_var( &C->fctx->vars, (char*) node->token + 2, node->token[ 1 ] ) )
		{
			sgs_Printf( C, SGS_ERROR, sgsT_LineNum( node->token ), "Cannot redeclare arguments with the same name." );
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
		if( var->type == SVT_NULL )
			return var - vbeg;
		var++;
	}
	UNUSED( C );

	nvar.type = SVT_NULL;
	membuf_appbuf( &func->consts, &nvar, sizeof( nvar ) );
	return vend - vbeg;
}

static int add_const_b( SGS_CTX, sgs_CompFunc* func, int32_t bval )
{
	add_const_HDR;
	while( var < vend )
	{
		if( var->type == SVT_BOOL && var->data.B == bval )
			return var - vbeg;
		var++;
	}
	UNUSED( C );

	nvar.type = SVT_BOOL;
	nvar.data.B = bval;
	membuf_appbuf( &func->consts, &nvar, sizeof( nvar ) );
	return vend - vbeg;
}

static int add_const_i( SGS_CTX, sgs_CompFunc* func, sgs_Integer ival )
{
	add_const_HDR;
	while( var < vend )
	{
		if( var->type == SVT_INT && var->data.I == ival )
			return var - vbeg;
		var++;
	}
	UNUSED( C );

	nvar.type = SVT_INT;
	nvar.data.I = ival;
	membuf_appbuf( &func->consts, &nvar, sizeof( nvar ) );
	return vend - vbeg;
}

static int add_const_r( SGS_CTX, sgs_CompFunc* func, sgs_Real rval )
{
	add_const_HDR;
	while( var < vend )
	{
		if( var->type == SVT_REAL && var->data.R == rval )
			return var - vbeg;
		var++;
	}
	UNUSED( C );

	nvar.type = SVT_REAL;
	nvar.data.R = rval;
	membuf_appbuf( &func->consts, &nvar, sizeof( nvar ) );
	return vend - vbeg;
}

static int add_const_s( SGS_CTX, sgs_CompFunc* func, int32_t len, const char* str )
{
	add_const_HDR;
	while( var < vend )
	{
		if( var->type == SVT_STRING && var->data.S->size == len
			&& memcmp( var_cstr( var ), str, len ) == 0 )
			return var - vbeg;
		var++;
	}
	UNUSED( C );

	sgsVM_VarCreateString( C, &nvar, str, len );
	membuf_appbuf( &func->consts, &nvar, sizeof( nvar ) );
	return vend - vbeg;
}

static int add_const_f( SGS_CTX, sgs_CompFunc* func, sgs_CompFunc* nf, const char* funcname, LineNum lnum )
{
	sgs_Variable nvar;
	int pos;
	func_t* F = sgs_Alloc_a( func_t, nf->consts.size + nf->code.size );
	UNUSED( C );

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
	F->funcname = strbuf_create();
	if( funcname )
		strbuf_appstr( &F->funcname, funcname );
	F->linenum = lnum;

	memcpy( func_consts( F ), nf->consts.ptr, nf->consts.size );
	memcpy( func_bytecode( F ), nf->code.ptr, nf->code.size );

	membuf_destroy( &nf->consts );
	membuf_destroy( &nf->code );
	membuf_destroy( &nf->lnbuf );
	sgs_Free( nf );

	pos = func->consts.size / sizeof( nvar );
	nvar.type = SVT_FUNC;
	nvar.data.F = F;
	membuf_appbuf( &func->consts, &nvar, sizeof( nvar ) );
	return pos;
}

#define INTERNAL_ERROR( loff ) sgs_printf( C, SGS_ERROR, -1, "INTERNAL ERROR occured in file %s [%d]", __FILE__, __LINE__ - (loff) )

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



static void compile_ident( SGS_CTX, sgs_CompFunc* func, FTNode* node, int16_t* out )
{
	int16_t pos = add_const_s( C, func, node->token[ 1 ], (const char*) node->token + 2 );
	*out = CONSTENC( pos );
}


static int compile_ident_r( SGS_CTX, sgs_CompFunc* func, FTNode* node, int16_t* out )
{
	int16_t pos;
	if( is_keyword( node->token, "null" ) )
	{
		pos = add_const_null( C, func );
		*out = CONSTENC( pos );
		return 1;
	}
	if( is_keyword( node->token, "true" ) )
	{
		pos = add_const_b( C, func, TRUE );
		*out = CONSTENC( pos );
		return 1;
	}
	if( is_keyword( node->token, "false" ) )
	{
		pos = add_const_b( C, func, FALSE );
		*out = CONSTENC( pos );
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
				sgs_Printf( C, SGS_ERROR, sgsT_LineNum( node->token ), "This function is not a method, cannot use 'this'" );
				return 0;
			}
		}
		sgs_Printf( C, SGS_ERROR, sgsT_LineNum( node->token ), "Cannot read from this keyword" );
		return 0;
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
		sgs_Printf( C, SGS_ERROR, sgsT_LineNum( node->token ), "Cannot write to reserved keywords" );
		return 0;
	}

	if( C->fctx->func )
	{
		int16_t gpos = find_var( &C->fctx->gvars, (char*) node->token + 2, node->token[ 1 ] );
		if( gpos >= 0 )
			pos = -1;
		else
		{
			add_var( &C->fctx->vars, (char*) node->token + 2, node->token[ 1 ] );
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
		*opos = CONSTENC( add_const_i( C, func, AS_INTEGER( node->token + 1 ) ) );
	}
	else if( *node->token == ST_NUMREAL )
	{
		*opos = CONSTENC( add_const_r( C, func, AS_REAL( node->token + 1 ) ) );
	}
	else if( *node->token == ST_STRING )
	{
		*opos = CONSTENC( add_const_s( C, func, AS_INT32( node->token + 1 ), (const char*) node->token + 5 ) );
	}
	else
	{
		sgs_Printf( C, SGS_ERROR, -1, "INTERNAL ERROR: constant doesn't have a token of type int/real/string attached" );
		return 0;
	}
	return 1;
}


static int compile_fcall( SGS_CTX, sgs_CompFunc* func, FTNode* node, int16_t* out, int expect )
{
	int i = 0;
	int16_t funcpos = -1, retpos = -1, gotthis = FALSE;

	/* load function */
	FUNC_ENTER;
	if( !compile_node_r( C, func, node->child, &funcpos ) ) return 0;

	if( expect )
		retpos = comp_reg_alloc( C );
	for( i = 1; i < expect; ++i )
		comp_reg_alloc( C );

	/* load arguments */
	i = 0;
	{
		/* passing objects where the call is formed appropriately */
		/* TODO: implement client side after function expressions */
		int16_t argpos = -1;
		if( node->child->type == SFT_OPER && *node->child->token == ST_OP_MMBR )
		{
			FUNC_ENTER;
			if( !compile_node_r( C, func, node->child->child, &argpos ) ) return 0;
			INSTR_WRITE( SI_PUSH, 0, argpos, 0 );
			gotthis = TRUE;
		}

		FTNode* n = node->child->next->child;
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
	while( expect )
	{
		int16_t cra;
		expect--;
		cra = retpos + expect;
		out[ expect ] = cra;
		INSTR_WRITE( SI_POPR, cra, 0, 0 );
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
		sgs_Printf( C, SGS_ERROR, sgsT_LineNum( node->token ), "Cannot set indexed value of a constant" );
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

	if( node->type != SFT_IDENT || *node->token != ST_IDENT )
		goto cannot;

	if( ioff >= func->code.size - 4 )
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
			add_var( &C->fctx->vars, (char*) node->token + 2, node->token[ 1 ] );
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
		case SI_NEGATE: case SI_BOOL_INV: case SI_INVERT: case SI_INC: case SI_DEC:
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
		if( assign || expect )
		{
			int jin;
			int16_t ireg1, ireg2, oreg = 0, jmp_off = 0;
			int32_t csz, csz2;

			if( !assign )
				oreg = comp_reg_alloc( C );

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
		if( oreg != ireg )
		{
			INSTR_WRITE( SI_SET, oreg, ireg, 0 );
		}

		/* check for errors if this operator generates output */
		if( expect )
		{
			if( expect != 1 )
			{
				sgs_Printf( C, SGS_ERROR, sgsT_LineNum( node->token ), "Too many expected outputs for operator" );
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
		if( *node->token == ST_OP_SET || *node->token == ST_OP_COPY )
		{
			int16_t ireg, isb = func->code.size;

			/* get source data register */
			FUNC_ENTER;
			if( !compile_node_r( C, func, node->child->next, &ireg ) ) goto fail;

			if( arg )
				*arg = ireg;

			if( *node->token == ST_OP_SET )
			{
				FUNC_ENTER;
				if( !try_optimize_last_instr_out( C, func, node->child, isb, arg ) )
				{
					/* just set the contents */
					FUNC_ENTER;
					if( !compile_node_w( C, func, node->child, ireg ) ) goto fail;
				}
			}
			else
			{
				/* TODO: remove, currently emits NOP */
				/* load the original variable and copy data to it */
				int16_t oreg;

				FUNC_ENTER;
				if( !compile_node_r( C, func, node->child, &oreg ) ) goto fail;

				INSTR_WRITE_PCH();
			}
		}
		/* 2 operands */
		else
		{
			uint8_t op;
			int16_t ireg1, ireg2, oreg = comp_reg_alloc( C );

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
	/* Any other, needs expected output to be compiled (optimization) */
	else if( expect )
	{
		int16_t ireg1, ireg2, oreg;

		if( expect != 1 )
		{
			sgs_Printf( C, SGS_ERROR, sgsT_LineNum( node->token ), "Too many expected outputs for operator" );
			goto fail;
		}

		if( *node->token == ST_OP_MMBR )
		{
			/* oreg points to output register if "out", source register otherwise */
			if( out )
			{
				oreg = comp_reg_alloc( C );
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
					sgs_Printf( C, SGS_ERROR, sgsT_LineNum( node->token ), "Cannot set property of a constant" );
					goto fail;
				}
				INSTR_WRITE( SI_SETPROP, ireg1, ireg2, oreg );
			}

		}
		else
		{
			uint8_t op;

			oreg = comp_reg_alloc( C );

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
			fctx_binfo_rem( C->fctx, prev );
		}
		else
		{
			prev = binfo;
			binfo = binfo->next;
		}
	}
	return 1;
}


static int compile_func( SGS_CTX, sgs_CompFunc* func, FTNode* node, int16_t* out )
{
	sgs_FuncCtx* fctx = fctx_create(), *bkfctx = C->fctx;
	sgs_CompFunc* nf = make_compfunc();
	int args = 0;

	C->fctx = fctx;
	FUNC_ENTER;
	if( !preparse_arglist( C, nf, node->child ) ) { goto fail; }
	args = fctx->regs;
	if( !preparse_varlists( C, nf, node->child->next ) ) { goto fail; }
	FUNC_ENTER;
	if( !compile_node( C, nf, node->child->next ) ) { goto fail; }
	comp_reg_unwind( C, 0 );

	{
		instr_t I = INSTR_MAKE( SI_PUSHN, C->fctx->lastreg - args, 0, 0 );
		uint16_t ln = 0;
		membuf_insbuf( &nf->code, 0, &I, sizeof( I ) );
		membuf_insbuf( &nf->lnbuf, 0, &ln, sizeof( ln ) );
	}

#if SGS_PROFILE_BYTECODE || ( SGS_DEBUG && SGS_DEBUG_DATA )
	fctx_dump( fctx );
	sgsBC_Dump( nf );
#endif
	fctx_destroy( fctx );
	C->fctx = bkfctx;

	{
		char ffn[ 256 ] = {0};
		if( node->child->next->next )
			strncpy( ffn, (const char*) node->child->next->next->token + 2, node->child->next->next->token[1] );
		*out = CONSTENC( add_const_f( C, func, nf, *ffn ? ffn : NULL, sgsT_LineNum( node->token ) ) );
	}
	return 1;

fail:
	sgsBC_Free( C, nf );
	C->fctx = bkfctx;
	fctx_destroy( fctx );
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
		sgs_Printf( C, SGS_ERROR, sgsT_LineNum( node->token ), "Cannot write to constants." );
		goto fail;
	case SFT_FUNC:
		FUNC_HIT( "W_FUNC" );
		sgs_Printf( C, SGS_ERROR, sgsT_LineNum( node->token ), "Cannot write to constants." );
		goto fail;
	case SFT_ARRLIST:
		FUNC_HIT( "W_ARRLIST" );
		sgs_Printf( C, SGS_ERROR, sgsT_LineNum( node->token ), "Cannot write to constants." );
		goto fail;
	case SFT_MAPLIST:
		FUNC_HIT( "W_MAPLIST" );
		sgs_Printf( C, SGS_ERROR, sgsT_LineNum( node->token ), "Cannot write to constants." );
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

	default:
		sgs_Printf( C, SGS_ERROR, -1, "Unexpected tree node [uncaught/internal BcG/w error]." );
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
						pos = CONSTENC( add_const_s( C, func, AS_INT32( n->token + 1 ), (const char*) n->token + 5 ) );
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
		sgs_Printf( C, SGS_ERROR, -1, "Unexpected tree node [uncaught/internal BcG/r error]." );
		goto fail;
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
	case SFT_INDEX:
		break;

	case SFT_OPER:
	case SFT_OPER_P:
		FUNC_HIT( "OPERATOR" );
		if( !compile_oper( C, func, node, NULL, 0, 0 ) ) goto fail;
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
			if( !compile_node_r( C, func, node->child->next, &arg ) ) goto fail; /* test */
			comp_reg_unwind( C, regstate );
			INSTR_WRITE_PCH();
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
				AS_UINT32( func->code.ptr + jp1 - 4 ) = INSTR_MAKE_EX( SI_JMPF, ( jp2 - jp1 ) / INSTR_SIZE + 1, arg );
			}
			if( !compile_breaks( C, func, 0 ) )
				goto fail;
			C->fctx->loops--;
		}
		break;

	case SFT_FOREACH:
		FUNC_HIT( "FOREACH" );
		{
			int regstate2 = C->fctx->regs;
			int16_t var = -1;
			int16_t iter = comp_reg_alloc( C );
			int16_t key = comp_reg_alloc( C );
			int16_t state = comp_reg_alloc( C );
			int regstate = C->fctx->regs;
			C->fctx->loops++;

			/* init */
			FUNC_ENTER;
			if( !compile_node_r( C, func, node->child->next, &var ) ) goto fail; /* get variable */

			INSTR_WRITE( SI_FORPREP, iter, var, 0 );
			comp_reg_unwind( C, regstate );

			/* iterate */
			i = func->code.size;
			INSTR_WRITE( SI_FORNEXT, key, state, iter );

			INSTR_WRITE_PCH();
			{
				int32_t jp1, jp2 = 0;
				int16_t off = 0;
				jp1 = func->code.size;

				if( !compile_ident_w( C, func, node->child, key ) ) goto fail;

				FUNC_ENTER;
				if( !compile_node( C, func, node->child->next->next ) ) goto fail; /* block */
				comp_reg_unwind( C, regstate );

				if( !compile_breaks( C, func, 1 ) )
					goto fail;

				jp2 = func->code.size;
				off = i - jp2;
				INSTR_WRITE_EX( SI_JUMP, off / INSTR_SIZE - 1, 0 );
				AS_UINT32( func->code.ptr + jp1 - 4 ) = INSTR_MAKE_EX( SI_JMPF, ( func->code.size - jp1 ) / INSTR_SIZE, state );
			}

			if( !compile_breaks( C, func, 0 ) )
				goto fail;
			comp_reg_unwind( C, regstate2 );
		}
		break;

	case SFT_BREAK:
		FUNC_HIT( "BREAK" );
		{
			TokenList tl = sgsT_Next( node->token );
			int32_t blev = 1;
			if( *tl == ST_NUMINT )
				blev = (uint32_t)*(sgs_Integer*)( tl + 1 );
			if( blev > C->fctx->loops )
			{
				sgs_Printf( C, SGS_ERROR, sgsT_LineNum( node->token ), C->fctx->loops ? "Break level too high." : "Attempted to break while not in a loop." );
				goto fail;
			}
			fctx_binfo_add( C->fctx, func->code.size, C->fctx->loops + 1 - blev, FALSE );
			INSTR_WRITE_PCH();
		}
		break;

	case SFT_CONT:
		FUNC_HIT( "CONTINUE" );
		{
			TokenList tl = sgsT_Next( node->token );
			int32_t blev = 1;
			if( *tl == ST_NUMINT )
				blev = (uint32_t)*(sgs_Integer*)( tl + 1 );
			if( blev > C->fctx->loops )
			{
				sgs_Printf( C, SGS_ERROR, sgsT_LineNum( node->token ), C->fctx->loops ? "Continue level too high." : "Attempted to continue while not in a loop." );
				goto fail;
			}
			fctx_binfo_add( C->fctx, func->code.size, C->fctx->loops + 1 - blev, TRUE );
			INSTR_WRITE_PCH();
		}
		break;

	case SFT_FUNC:
		FUNC_HIT( "FUNC" );
		{
			int16_t pos;
			FUNC_ENTER;
			if( !compile_func( C, func, node, &pos ) ) goto fail;

			if( node->child->next->next )
			{
				FUNC_ENTER;
				if( !compile_node_w( C, func, node->child->next->next, pos ) ) goto fail;
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
					if( !compile_node_r( C, func, pp->child, &arg ) ) goto fail;
					if( !pp->token || *pp->token != ST_IDENT ) goto fail;
					compile_ident_w( C, func, pp, arg );
					comp_reg_unwind( C, regstate );
				}
				pp = pp->next;
			}
		}
		break;

	default:
		sgs_Printf( C, SGS_ERROR, -1, "Unexpected tree node [uncaught/internal BcG error]." );
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
	sgs_CompFunc* func = make_compfunc();
	sgs_FuncCtx* fctx = fctx_create();
	fctx->func = FALSE;
	C->fctx = fctx;
	if( !preparse_varlists( C, func, tree ) )
		goto fail;
	if( !compile_node( C, func, tree ) )
		goto fail;
	comp_reg_unwind( C, 0 );

	{
		instr_t I = INSTR_MAKE( SI_PUSHN, C->fctx->lastreg, 0, 0 );
		uint16_t ln = 0;
		membuf_insbuf( &func->code, 0, &I, sizeof( I ) );
		membuf_insbuf( &func->lnbuf, 0, &ln, sizeof( ln ) );
	}

	C->fctx = NULL;
#if SGS_PROFILE_BYTECODE || ( SGS_DEBUG && SGS_DEBUG_DATA )
	fctx_dump( fctx );
#endif
	fctx_destroy( fctx );
	return func;

fail:
	sgsBC_Free( C, func );
	C->fctx = NULL;
	fctx_destroy( fctx );
	C->state |= SGS_HAS_ERRORS;
	return NULL;
}

void sgsBC_Dump( sgs_CompFunc* func )
{
	sgs_Variable* vbeg = (sgs_Variable*) func->consts.ptr;
	sgs_Variable* vend = (sgs_Variable*) ( func->consts.ptr + func->consts.size );
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
	dump_opcode( (instr_t*) func->code.ptr, func->code.size / sizeof( instr_t ) );
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

	membuf_destroy( &func->code );
	membuf_destroy( &func->consts );
	membuf_destroy( &func->lnbuf );
	sgs_Free( func );
}


