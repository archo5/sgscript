

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
	if( out > 0x7fff )
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


static void dump_rcpos( char* ptr )
{
	int a = AS_INT16( ptr );
	char rc = a >= 0 ? 'R' : 'C';
	if( a < 0 ) a = CONSTDEC( a );
	printf( "%c%d", rc, a );
}
static void dump_opcode_a( const char* name, char* ptr )
{
	int a1 = AS_INT16( ptr );
	printf( "%s R%d <= ", name, a1 );
	dump_rcpos( ptr + 2 );
	printf( ", " );
	dump_rcpos( ptr + 4 );
}
static void dump_opcode_a1( const char* name, char* ptr )
{
	printf( "%s ", name );
	dump_rcpos( ptr );
	printf( " <= " );
	dump_rcpos( ptr + 2 );
	printf( ", " );
	dump_rcpos( ptr + 4 );
}
static void dump_opcode_b( const char* name, char* ptr )
{
	int a1 = AS_INT16( ptr );
	printf( "%s R%d <= ", name, a1 );
	dump_rcpos( ptr + 2 );
}
static void dump_opcode_b1( const char* name, char* ptr )
{
	printf( "%s ", name );
	dump_rcpos( ptr );
	printf( " <= " );
	dump_rcpos( ptr + 2 );
}
static void dump_opcode( char* ptr, int32_t size )
{
	char* pend = ptr + size;
	char* pbeg = ptr;
	while( ptr < pend )
	{
		unsigned char instr = *ptr++;
		printf( "\t%04d |  ", ptr - pbeg - 1 );
		switch( instr )
		{
#define DOP_A( wat ) case SI_##wat: dump_opcode_a( #wat, ptr ); ptr += 6; break;
#define DOP_A1( wat ) case SI_##wat: dump_opcode_a1( #wat, ptr ); ptr += 6; break;
#define DOP_B( wat ) case SI_##wat: dump_opcode_b( #wat, ptr ); ptr += 4; break;
#define DOP_B1( wat ) case SI_##wat: dump_opcode_b1( #wat, ptr ); ptr += 4; break;
#define DOP_C( wat ) case SI_##wat: dump_opcode_c( #wat, ptr ); ptr += 2; break;
		case SI_NOP: printf( "NOP   " ); break;

		case SI_PUSH: printf( "PUSH " ); dump_rcpos( ptr ); ptr += 2; break;
		case SI_PUSHN: printf( "PUSH_NULLS %d", (int) AS_UINT8( ptr++ ) ); break;
		case SI_POPN: printf( "POP_N %d", (int) AS_UINT8( ptr++ ) ); break;
		case SI_POPR: printf( "POP_REG R%d", (int) AS_INT16( ptr ) ); ptr += 2; break;

		case SI_RETN: printf( "RETURN %d", (int) AS_UINT8( ptr++ ) ); break;
		case SI_JUMP: printf( "JUMP %d", (int) AS_INT16( ptr ) ); ptr += 2; break;
		case SI_JMPT: printf( "JMP_T " ); dump_rcpos( ptr ); printf( ", %d", (int) AS_INT16( ptr + 2 ) ); ptr += 4; break;
		case SI_JMPF: printf( "JMP_F " ); dump_rcpos( ptr ); printf( ", %d", (int) AS_INT16( ptr + 2 ) ); ptr += 4; break;
		case SI_CALL: printf( "CALL args:%d%s expect:%d func:", (int) AS_UINT8( ptr ) & 0x7f, AS_UINT8( ptr ) & 0x80 ? ",method" : "",
							(int) AS_UINT8( ptr + 1 ) ); dump_rcpos( ptr + 2 ); ptr += 4; break;

		case SI_FORPREP: printf( "FOR_PREP " ); dump_rcpos( ptr ); printf( " <= " ); dump_rcpos( ptr + 2 ); ptr += 4; break;
		case SI_FORNEXT: printf( "FOR_NEXT " ); dump_rcpos( ptr ); printf( ", " ); dump_rcpos( ptr + 2 );
							printf( " <= " ); dump_rcpos( ptr + 4 ); ptr += 6; break;

		DOP_B( GETVAR );
		DOP_B1( SETVAR );
		DOP_A( GETPROP );
		DOP_A1( SETPROP );
		DOP_A( GETINDEX );
		DOP_A1( SETINDEX );

		DOP_B( SET );
		DOP_B( CLONE );
		DOP_A( CONCAT );
		DOP_A( BOOL_AND );
		DOP_A( BOOL_OR );
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
#undef DOP_C

		case SI_ARRAY: printf( "ARRAY args:%d output:", (int) AS_UINT8( ptr++ ) ); dump_rcpos( ptr ); ptr += 2; break;
		case SI_DICT: printf( "DICT args:%d output:", (int) AS_UINT8( ptr++ ) ); dump_rcpos( ptr ); ptr += 2; break;

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


static void add_linenum( sgs_CompFunc* func, FTNode* node )
{
	uint16_t ln[ 2 ] = { func->code.size, sgsT_LineNum( node->token ) };
	membuf_appbuf( &func->lnbuf, ln, sizeof( uint16_t ) * 2 );
}

/* simplifies writing code */
#define LINENUM_N( n )  add_linenum( func, n )
#define LINENUM()       LINENUM_N( node )
#define BYTE( c )       membuf_appchr( &func->code, (c) )
#define DATA( ptr, sz ) membuf_appbuf( &func->code, (ptr), (sz) )


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

	lht_init_all( &F->lineinfo, (uint16_t*) nf->lnbuf.ptr, nf->lnbuf.size / sizeof( uint16_t ) );
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
	case ST_OP_BLAND: case ST_OP_BLAEQ: return SI_BOOL_AND;
	case ST_OP_BLOR: case ST_OP_BLOEQ: return SI_BOOL_OR;

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
		LINENUM();
		BYTE( SI_GETVAR );
		DATA( out, 2 );
		DATA( &pos, 2 );
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
			LINENUM();
			BYTE( SI_SET );
			DATA( &pos, 2 );
			DATA( &src, 2 );
		}
	}
	else
	{
		compile_ident( C, func, node, &pos );
		LINENUM();
		BYTE( SI_SETVAR );
		DATA( &pos, 2 );
		DATA( &src, 2 );
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
			LINENUM();
			BYTE( SI_PUSH );
			DATA( &argpos, 2 );
			gotthis = TRUE;
		}

		FTNode* n = node->child->next->child;
		while( n )
		{
			argpos = -1;
			FUNC_ENTER;
			if( !compile_node_r( C, func, n, &argpos ) ) return 0;
			LINENUM();
			BYTE( SI_PUSH );
			DATA( &argpos, 2 );
			i++;
			n = n->next;
		}
	}

	if( gotthis )
		i |= 0x80;

	/* compile call */
	LINENUM();
	BYTE( SI_CALL );
	BYTE( i );
	BYTE( expect );
	DATA( &funcpos, 2 );

	/* compile writeback */
	while( expect )
	{
		int16_t cra;
		expect--;
		cra = retpos + expect;
		out[ expect ] = cra;
		LINENUM();
		BYTE( SI_POPR );
		DATA( &cra, 2 );
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
	LINENUM();
	BYTE( SI_GETINDEX );
	DATA( &opos, 2 );
	DATA( &var, 2 );
	DATA( &name, 2 );
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
	LINENUM();
	BYTE( SI_SETINDEX );
	DATA( &var, 2 );
	DATA( &name, 2 );
	DATA( &src, 2 );
	comp_reg_unwind( C, regpos );
	return 1;
}


/*
	This, all of this, is way more complicated than it has to be.
	If I'm ever switching to equally sized instructions, this will be one of the reasons.
*/

static int instr_size( uint8_t pos )
{
	switch( pos )
	{
	case SI_NOP: return 1;
	case SI_PUSHN: case SI_POPN: case SI_RETN: return 2;
	case SI_PUSH: case SI_POPR: case SI_JUMP: return 3;
	case SI_ARRAY: case SI_DICT: return 4;
	case SI_JMPT: case SI_JMPF: case SI_CALL: case SI_GETVAR: case SI_SETVAR:
	case SI_SET: case SI_CLONE: case SI_NEGATE: case SI_BOOL_INV: case SI_INVERT:
	case SI_INC: case SI_DEC: return 5;
	case SI_GETPROP: case SI_SETPROP: case SI_GETINDEX: case SI_SETINDEX:
	case SI_CONCAT: case SI_BOOL_AND: case SI_BOOL_OR: case SI_ADD: case SI_SUB:
	case SI_MUL: case SI_DIV: case SI_MOD: case SI_AND: case SI_OR: case SI_XOR:
	case SI_LSH: case SI_RSH: case SI_SEQ: case SI_EQ: case SI_LT: case SI_LTE:
	case SI_SNEQ: case SI_NEQ: case SI_GT: case SI_GTE:
		return 7;
	}
	sgs_BreakIf( "invalid instruction passed to instr_size" );
	return 0;
}

static int try_optimize_last_instr_out( SGS_CTX, sgs_CompFunc* func, FTNode* node, int32_t ioff )
{
	int16_t pos = -1;
	int32_t ibeg = ioff;

	FUNC_BEGIN;
	UNUSED( C );

	if( node->type != SFT_IDENT || *node->token != ST_IDENT )
		goto cannot;

	/* find last instruction */
	while( ioff < func->code.size )
	{
		ibeg = ioff;
		ioff += instr_size( func->code.ptr[ ioff ] );
	}
	if( ibeg == ioff )
		goto cannot; /* no instructions to process */
	/* last one's after "ibeg" */

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

	switch( (uint8_t) func->code.ptr[ ibeg ] )
	{
	case SI_POPR: case SI_GETVAR: case SI_GETPROP: case SI_GETINDEX:
	case SI_SET: case SI_CONCAT: case SI_BOOL_AND: case SI_BOOL_OR:
	case SI_NEGATE: case SI_BOOL_INV: case SI_INVERT: case SI_INC: case SI_DEC:
	case SI_ADD: case SI_SUB: case SI_MUL: case SI_DIV: case SI_MOD:
	case SI_AND: case SI_OR: case SI_XOR: case SI_LSH: case SI_RSH:
	case SI_SEQ: case SI_EQ: case SI_LT: case SI_LTE:
	case SI_SNEQ: case SI_NEQ: case SI_GT: case SI_GTE:
		AS_INT16( func->code.ptr + ibeg + 1 ) = pos;
		break;
	case SI_ARRAY: case SI_DICT:
		AS_INT16( func->code.ptr + ibeg + 2 ) = pos;
		break;
	default:
		goto cannot;
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
			LINENUM();
			BYTE( SI_SET );
			DATA( &oreg, 2 );
			DATA( &ireg, 2 );
		}

		/* check for errors if this operator generates output */
		if( expect )
		{
			if( expect != 1 )
			{
				sgs_Printf( C, SGS_ERROR, sgsT_LineNum( node->token ), "Too many expected outputs for operator." );
				goto fail;
			}
		}

		/* write bytecode */
		LINENUM();
		BYTE( *node->token == ST_OP_INC ? SI_INC : SI_DEC );
		DATA( &ireg, 2 );
		DATA( &ireg, 2 );

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
				if( !try_optimize_last_instr_out( C, func, node->child, isb ) )
				{
					/* just set the contents */
					FUNC_ENTER;
					if( !compile_node_w( C, func, node->child, ireg ) ) goto fail;
				}
			}
			else
			{
				/* TODO: remove, currently emits invalid instruction */
				/* load the original variable and copy data to it */
				int16_t oreg;

				FUNC_ENTER;
				if( !compile_node_r( C, func, node->child, &oreg ) ) goto fail;

				LINENUM();
				BYTE( 255 );
				DATA( &oreg, 2 );
				DATA( &ireg, 2 );
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
			LINENUM();
			BYTE( op );
			DATA( &oreg, 2 );
			DATA( &ireg1, 2 );
			DATA( &ireg2, 2 );

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
			sgs_Printf( C, SGS_ERROR, sgsT_LineNum( node->token ), "Too many expected outputs for operator." );
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
			LINENUM();
			BYTE( out ? SI_GETPROP : SI_SETPROP );
			if( out ) DATA( &oreg, 2 );
			DATA( &ireg1, 2 );
			DATA( &ireg2, 2 );
			if( !out ) DATA( &oreg, 2 );

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
			LINENUM();
			BYTE( op );
			DATA( &oreg, 2 );
			DATA( &ireg1, 2 );
			if( node->child->next )
				DATA( &ireg2, 2 );
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
			*(int16_t*)( func->code.ptr + binfo->jdoff ) = func->code.size - 2 - binfo->jdoff;
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
		uint8_t lpn[ 2 ] = { SI_PUSHN, C->fctx->lastreg - args };
		uint16_t* lndata = (uint16_t*) nf->lnbuf.ptr;
		uint16_t i, lncnt = nf->lnbuf.size / sizeof( uint16_t );

		membuf_insbuf( &nf->code, 0, lpn, 2 );
		for( i = 0; i < lncnt; i += 2 )
			lndata[ i ] += 2;
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
			strncpy( ffn, node->child->next->next->token + 2, node->child->next->next->token[1] );
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
				LINENUM_N( n );
				BYTE( SI_PUSH );
				DATA( &pos, 2 );
				comp_reg_unwind( C, bkup );
				args++;
				n = n->next;
			}
			LINENUM();
			BYTE( SI_ARRAY );
			BYTE( args );
			pos = comp_reg_alloc( C );
			DATA( &pos, 2 );
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
				LINENUM_N( n );
				BYTE( SI_PUSH );
				DATA( &pos, 2 );
				comp_reg_unwind( C, bkup );
				args++;
				n = n->next;
			}
			LINENUM();
			BYTE( SI_DICT );
			BYTE( args );
			pos = comp_reg_alloc( C );
			DATA( &pos, 2 );
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
				LINENUM();
				BYTE( SI_PUSH );
				DATA( &arg, 2 );
				n = n->next;
				num++;
			}
			LINENUM();
			BYTE( SI_RETN );
			BYTE( num );
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
			LINENUM();
			BYTE( SI_JMPF );
			DATA( &arg, 2 );
			{
				int32_t jp1, jp2 = 0, jp3 = 0;
				uint16_t pos = 0;
				DATA( &pos, 2 );
				jp1 = func->code.size;

				regstate = C->fctx->regs;
				FUNC_ENTER;
				if( !compile_node( C, func, node->child->next ) ) goto fail;
				comp_reg_unwind( C, regstate );

				if( node->child->next->next )
				{
					LINENUM();
					BYTE( SI_JUMP );
					DATA( &pos, 2 );
					jp2 = func->code.size;
					AS_INT16( func->code.ptr + jp1 - 2 ) = jp2 - jp1;

					regstate = C->fctx->regs;
					FUNC_ENTER;
					if( !compile_node( C, func, node->child->next->next ) ) goto fail;
					jp3 = func->code.size;
					AS_INT16( func->code.ptr + jp2 - 2 ) = jp3 - jp2;
					comp_reg_unwind( C, regstate );
				}
				else
				{
					AS_INT16( func->code.ptr + jp1 - 2 ) = func->code.size - jp1;
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
			LINENUM();
			BYTE( SI_JMPF );
			DATA( &arg, 2 );
			{
				int16_t off;
				int32_t jp1, jp2 = 0;
				uint16_t pos = 0;
				DATA( &pos, 2 );
				jp1 = func->code.size;

				regstate = C->fctx->regs;
				FUNC_ENTER;
				if( !compile_node( C, func, node->child->next ) ) goto fail; /* while */
				comp_reg_unwind( C, regstate );

				if( !compile_breaks( C, func, 1 ) )
					goto fail;

				LINENUM();
				BYTE( SI_JUMP );
				jp2 = func->code.size + 2;
				off = i - jp2;
				DATA( &off, 2 );
				AS_INT16( func->code.ptr + jp1 - 2 ) = jp2 - jp1;
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
			LINENUM();
			BYTE( SI_JMPT );
			DATA( &arg, 2 );
			joff = i - func->code.size - 2;
			DATA( &joff, 2 );
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
			LINENUM();
			BYTE( SI_JMPF );
			DATA( &arg, 2 );
			{
				int16_t off;
				int32_t jp1, jp2 = 0;
				uint16_t pos = 0;
				DATA( &pos, 2 );
				jp1 = func->code.size;

				FUNC_ENTER;
				if( !compile_node( C, func, node->child->next->next->next ) ) goto fail; /* block */
				comp_reg_unwind( C, regstate );

				if( !compile_breaks( C, func, 1 ) )
					goto fail;
				FUNC_ENTER;
				if( !compile_node( C, func, node->child->next->next ) ) goto fail; /* incr */
				comp_reg_unwind( C, regstate );

				LINENUM();
				BYTE( SI_JUMP );
				jp2 = func->code.size + 2;
				off = i - jp2;
				DATA( &off, 2 );
				AS_INT16( func->code.ptr + jp1 - 2 ) = jp2 - jp1;
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

			LINENUM();
			BYTE( SI_FORPREP );
			DATA( &iter, 2 );
			DATA( &var, 2 );
			comp_reg_unwind( C, regstate );

			/* iterate */
			i = func->code.size;
			LINENUM();
			BYTE( SI_FORNEXT );
			DATA( &key, 2 );
			DATA( &state, 2 );
			DATA( &iter, 2 );

			LINENUM();
			BYTE( SI_JMPF );
			DATA( &state, 2 );
			{
				int32_t jp1, jp2 = 0;
				int16_t off = 0;
				DATA( &off, 2 );
				jp1 = func->code.size;

				if( !compile_ident_w( C, func, node->child, key ) ) goto fail;

				FUNC_ENTER;
				if( !compile_node( C, func, node->child->next->next ) ) goto fail; /* block */
				comp_reg_unwind( C, regstate );

				if( !compile_breaks( C, func, 1 ) )
					goto fail;

				LINENUM();
				BYTE( SI_JUMP );
				jp2 = func->code.size + 2;
				off = i - jp2;
				DATA( &off, 2 );
				AS_INT16( func->code.ptr + jp1 - 2 ) = jp2 - jp1;
			}

			if( !compile_breaks( C, func, 0 ) )
				goto fail;
			comp_reg_unwind( C, regstate2 );
		}
		break;

	case SFT_BREAK:
		FUNC_HIT( "BREAK" );
		{
			int16_t off = 0;
			TokenList tl = sgsT_Next( node->token );
			int32_t blev = 1;
			if( *tl == ST_NUMINT )
				blev = (uint32_t)*(sgs_Integer*)( tl + 1 );
			if( blev > C->fctx->loops )
			{
				sgs_Printf( C, SGS_ERROR, sgsT_LineNum( node->token ), C->fctx->loops ? "Break level too high." : "Attempted to break while not in a loop." );
				goto fail;
			}
			LINENUM();
			BYTE( SI_JUMP );
			fctx_binfo_add( C->fctx, func->code.size, C->fctx->loops + 1 - blev, FALSE );
			DATA( &off, 2 );
		}
		break;

	case SFT_CONT:
		FUNC_HIT( "CONTINUE" );
		{
			int16_t off = 0;
			TokenList tl = sgsT_Next( node->token );
			int32_t blev = 1;
			if( *tl == ST_NUMINT )
				blev = (uint32_t)*(sgs_Integer*)( tl + 1 );
			if( blev > C->fctx->loops )
			{
				sgs_Printf( C, SGS_ERROR, sgsT_LineNum( node->token ), C->fctx->loops ? "Continue level too high." : "Attempted to continue while not in a loop." );
				goto fail;
			}
			LINENUM();
			BYTE( SI_JUMP );
			fctx_binfo_add( C->fctx, func->code.size, C->fctx->loops + 1 - blev, TRUE );
			DATA( &off, 2 );
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
		uint8_t lpn[ 2 ] = { SI_PUSHN, C->fctx->lastreg };
		uint16_t* lndata = (uint16_t*) func->lnbuf.ptr;
		uint16_t i, lncnt = func->lnbuf.size / sizeof( uint16_t );

		membuf_insbuf( &func->code, 0, lpn, 2 );
		for( i = 0; i < lncnt; i += 2 )
			lndata[ i ] += 2;
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
	dump_opcode( func->code.ptr, func->code.size );
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


