

#include "sgs_fnt.h"
#include "sgs_ctx.h"


static FTNode* make_node( int type, TokenList token, FTNode* next, FTNode* child )
{
	FTNode* node = sgs_Alloc( FTNode );
	node->type = type;
	node->token = token;
	node->next = next;
	node->child = child;
	return node;
}

void sgsFT_Destroy( FTNode* tree )
{
	if( tree->next ) sgsFT_Destroy( tree->next );
	if( tree->child ) sgsFT_Destroy( tree->child );
	sgs_Free( tree );
}


/* debugging */


static void dumpnode( FTNode* N )
{
	switch( N->type )
	{
	case SFT_FCALL: printf( "FCALL" ); break;
	case SFT_INDEX: printf( "INDEX" ); break;
	case SFT_ARGMT: printf( "ARG " ); sgsT_DumpToken( N->token ); break;
	case SFT_ARGLIST: printf( "ARG_LIST" ); break;
	case SFT_VARLIST: printf( "VAR_LIST" ); break;
	case SFT_GVLIST: printf( "GLOBAL_VAR_LIST" ); break;
	case SFT_EXPLIST: printf( "EXPR_LIST" ); break;
	case SFT_ARRLIST: printf( "ARRAY_LIST" ); break;
	case SFT_MAPLIST: printf( "MAP_LIST" ); break;
	case SFT_RETURN: printf( "RETURN" ); break;
	case SFT_BLOCK: printf( "BLOCK" ); break;
	case SFT_IFELSE: printf( "IF/ELSE" ); break;
	case SFT_WHILE: printf( "WHILE" ); break;
	case SFT_DOWHILE: printf( "DO/WHILE" ); break;
	case SFT_FOR: printf( "FOR" ); break;
	case SFT_BREAK: printf( "BREAK" ); if( *sgsT_Next( N->token ) == ST_NUMINT ) printf( " %" PRId64, *(sgs_Integer*)( sgsT_Next( N->token ) + 1 ) ); break;
	case SFT_CONT: printf( "CONTINUE" ); if( *sgsT_Next( N->token ) == ST_NUMINT ) printf( " %" PRId64, *(sgs_Integer*)( sgsT_Next( N->token ) + 1 ) ); break;
	case SFT_FUNC: printf( "FUNC" ); break;
	default:
		if( N->token ) sgsT_DumpToken( N->token );
		if( N->type == SFT_OPER_P ) printf( " [post]" );
		break;
	}
}

static void ft_dump( FTNode* node, int level )
{
	int i;
	FTNode* N = node;
	if( !node ) return;

	for( i = 0; i < level; ++i ) printf( "  " );
	dumpnode( N );
	printf( "\n" );

	if( node->child )
	{
		for( i = 0; i < level; ++i ) printf( "  " );
		printf( "{\n" );

		ft_dump( node->child, level + 1 );

		for( i = 0; i < level; ++i ) printf( "  " );
		printf( "}\n" );
	}

	ft_dump( node->next, level );
}

void sgsFT_Dump( FTNode* tree )
{
	ft_dump( tree, 0 );
}


/*
// C O M P I L E R
*/


/* Utilities
*/

#define PTR_MAX ((void*)-1)

static int is_ident( TokenList tok, const char* text )
{
	return *tok == ST_IDENT && tok[ 1 ] == strlen( text ) && strncmp( (const char*) tok + 2, text, tok[ 1 ] ) == 0;
}

static int is_keyword( TokenList tok, const char* text )
{
	return *tok == ST_KEYWORD && tok[ 1 ] == strlen( text ) && strncmp( (const char*) tok + 2, text, tok[ 1 ] ) == 0;
}

static char brace_opposite( char c )
{
	switch( c )
	{
	case '(':	return ')';
	case '[':	return ']';
	case '{':	return '}';
	default:	return 0;
	}
}

static FTNode* parse_exp( SGS_CTX, TokenList begin, TokenList end );
static FTNode* parse_stmt( SGS_CTX, TokenList* begin, TokenList end );
static FTNode* parse_stmtlist( SGS_CTX, TokenList begin, TokenList end );
static FTNode* parse_function( SGS_CTX, TokenList* begin, TokenList end, int inexp );

/* Expression parsing
*/

/*
	FUNC / finds the logical expected end of an expression
	ARGS / context, token stream, list of ending characters
	ERRS / brace mismatch, end of expression
*/
static TokenList detect_exp( SGS_CTX, TokenList at, TokenList end, const char* ends, int superr )
{
	StrBuf stack = strbuf_create();
	LineNum line = -1, fline;

	FUNC_BEGIN;
	FUNC_INFO( "Looking for %s\n", ends );

	sgs_BreakIf( !C );
	sgs_BreakIf( !at );

	if( !*at )
	{
		if( !superr )
		{
			sgs_Printf( C, SGS_ERROR, -1, "End of expression '%s' not found", ends );
			C->state |= SGS_HAS_ERRORS;
		}
		goto fail2;
	}

	fline = sgsT_LineNum( at );
	while( at < end && *at && ( stack.size > 0 || !isoneof( *at, ends ) ) )
	{
		line = sgsT_LineNum( at );
		if( isoneof( *at, "([{" ) )
		{
			strbuf_appbuf( &stack, &line, sizeof( LineNum ) );
			strbuf_appchr( &stack, brace_opposite( *at ) );
		}
		if( isoneof( *at, ")]}" ) )
		{
			if( stack.size == 0 || *at != stack.ptr[ stack.size - 1 ] )
			{
				sgs_Printf( C, SGS_ERROR, stack.size > 0 ? ST_READLN( stack.ptr + stack.size - 1 - sizeof( LineNum ) ) : -1,
							"Brace mismatch detected at line %d!", line );
				C->state |= SGS_HAS_ERRORS;
				goto fail;
			}
			else
				strbuf_resize( &stack, stack.size - 1 - sizeof( LineNum ) );
		}
		at = sgsT_Next( at );
	}

	if( !*at || at >= end )
	{
		if( !superr )
		{
			sgs_Printf( C, SGS_ERROR, fline, "End of expression '%s' not found", ends );
			C->state |= SGS_HAS_ERRORS;
		}
		goto fail;
	}

	strbuf_destroy( &stack );
	FUNC_END;
	return at;

fail:
	strbuf_destroy( &stack );
fail2:
	FUNC_END;
	return NULL;
}

/*
	FUNC / parses an argument
	ARGS / context, function tree, argument id, token stream
	ERRS / unexpected token, @parse_exp
	TODO: default values
*/
static FTNode* parse_arg( SGS_CTX, int argid, TokenList at, TokenList end )
{
	FTNode* node = NULL;
	char toks[ 3 ] = { ',', *end, 0 };
	int isthis = FALSE;

	FUNC_BEGIN;

	sgs_BreakIf( !C );
	sgs_BreakIf( !at );
	sgs_BreakIf( !*at );

	if( *at == ST_KEYWORD )
	{
		if( is_keyword( at, "this" ) )
		{
			if( argid != 1 )
			{
				sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "'this' must be the first argument" );
				goto fail;
			}
			else
				isthis = TRUE;
		}
		else
		{
			sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "Argument name cannot be a reserved keyword (except 'this')" );
			goto fail;
		}
	}

	if( *at != ST_IDENT && *at != ST_KEYWORD )
	{
		sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "Unexpected token while parsing argument %d", argid );
		goto fail;
	}

	node = make_node( SFT_ARGMT, at, NULL, NULL );
	at = sgsT_Next( at );

	if( *at == ST_OP_SET )
	{
		if( isthis )
		{
			sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "Cannot have default value for a 'this' argument" );
			goto fail;
		}
		TokenList expend;
		at = sgsT_Next( at );
		expend = detect_exp( C, at, sgsT_Next( end ), toks, 0 );
		if( !expend ) goto fail;
		node->child = parse_exp( C, at, expend );
		if( !node->child ) goto fail;
	}

	FUNC_END;
	return node;

fail:
	if( node ) sgsFT_Destroy( node );
	C->state |= SGS_HAS_ERRORS;
	FUNC_END;
	return 0;
}

/*
	FUNC / parses a list of arguments
	ARGS / context, function tree, token stream
	ERRS / unexpected token, @parse_arg
*/
static FTNode* parse_arglist( SGS_CTX, TokenList at, TokenList end )
{
	FTNode* arglist = make_node( SFT_ARGLIST, NULL, NULL, NULL );
	FTNode* curnode = NULL;
	FTNode* argnode;
	int id = 1;

	FUNC_BEGIN;

	sgs_BreakIf( !C );
	sgs_BreakIf( !at );
	sgs_BreakIf( !*at );

	while( at < end )
	{
		TokenList cend = detect_exp( C, at, end, ",", 1 );
		if( !cend ) cend = end;

		argnode = parse_arg( C, id, at, end );
		if( !argnode && C->state & SGS_MUST_STOP )
		{
			sgsFT_Destroy( arglist );
			arglist = NULL;
			break;
		}
		if( curnode ) curnode->next = argnode;
		else arglist->child = argnode;
		curnode = argnode;

		id++;
		at = cend;
		if( at < end )
			at = sgsT_Next( at );
	}

	FUNC_END;
	return arglist;
}

/*
	FUNC / calculates the weight of the part
	ARGS / context, part of function tree
	ERRS / none
*/
static int part_weight( FTNode* part, int isfcall, int binary )
{
	sgs_BreakIf( !part );

	if( part->type == SFT_OPER && ST_OP_ASSIGN( *part->token ) )
		return 40;

	if( isfcall )
		return 7;

	if( part->type == SFT_OPER )
	{
		TokenType type = *part->token;
		if( binary )
		{
			if( ST_OP_BOOL( type ) )	return 30;
			if( ST_OP_COMP( type ) )	return 28;
			if( ST_OP_BINOPS( type ) )	return 26;
			if( ST_OP_BINADD( type ) )	return 25;
			if( ST_OP_BINMUL( type ) )	return 24;
			if( type == ST_OP_CAT )		return 22;
			if( type == ST_OP_MMBR )	return 7;
			return 11;
		}

		/* unary operators */
		return 10;
	}

	/* everything else */
	return -1;
}


/*
	FUNC / adds depth to the tree
	ARGS / context, function tree, operational region
	ERRS / many
*/
static int level_exp( SGS_CTX, FTNode** tree )
{
	FTNode* node = *tree, *prev = NULL, *mpp = NULL;
	int weight = 0, curwt, isfcall, binary, count = 0;

	FUNC_BEGIN;
	sgs_BreakIf( !C || !tree );

	if( !*tree )
	{
		FUNC_END;
		return 1;
	}

	/* find the most powerful part (mpp) */
	while( node )
	{
		count++;

		/* only interested in operators and subexpressions */
		if( node->type != SFT_OPER && node->type != SFT_EXPLIST && node->type != SFT_ARRLIST )
			goto _continue;

		/* function tree test */
		isfcall = node->type == SFT_EXPLIST || node->type == SFT_ARRLIST;
		if( isfcall )	isfcall = !!prev;
		if( isfcall )	isfcall = prev->type != SFT_OPER || !ST_OP_BINARY( *prev->token );

		/* op tests */
		binary = node->type == SFT_OPER;
		if( binary )	binary = prev && node->next;
		if( binary )	binary = prev->type != SFT_OPER || *prev->token == ST_OP_INC || *prev->token == ST_OP_DEC;

		/* weighting */
		curwt = part_weight( node, isfcall, binary );
		if( ( curwt == 40 && curwt > weight ) || ( curwt != 40 && curwt >= weight ) )
		{
			weight = curwt;
			mpp = node;
		}

		/* move to next */
_continue:
		prev = node;
		node = node->next;
	}

	if( mpp )
	{
		/* function call */
		if( mpp->type == SFT_EXPLIST || mpp->type == SFT_ARRLIST )
		{
			int ret1, ret2;
			TokenList mpp_token = mpp->token;
			FTNode* se1 = *tree, *se2 = mpp, *se1i = *tree;
			while( se1i )
			{
				if( se1i->next == mpp )
					se1i->next = NULL;
				se1i = se1i->next;
			}

			FUNC_ENTER;
			ret1 = level_exp( C, &se1 );
			FUNC_ENTER;
			ret2 = level_exp( C, &se2 );
			FUNC_END;
			if( !ret1 || !ret2 )
			{
				*tree = NULL;
				if( se1 ) sgsFT_Destroy( se1 );
				if( se2 ) sgsFT_Destroy( se2 );
				FUNC_END;
				return 0;
			}
			if( *mpp_token == ST_SBRKL )
			{
				/* array */
				if( se2->child->next )
				{
					sgs_Printf( C, SGS_ERROR, sgsT_LineNum( mpp_token ), "Invalid number of arguments in an array accessor" );
					*tree = NULL;
					if( se1 ) sgsFT_Destroy( se1 );
					if( se2 ) sgsFT_Destroy( se2 );
					FUNC_END;
					return 0;
				}
				se1->next = se2->child;
				se2->child = NULL;
				sgsFT_Destroy( se2 );
				*tree = make_node( SFT_INDEX, mpp_token, NULL, se1 );
				return 1;
			}
			se1->next = se2;
			*tree = make_node( SFT_FCALL, mpp_token, NULL, se1 );
			return 1;
		}

		/* binary ops */
		if( mpp->type == SFT_OPER )
		{
			if( mpp == *tree )
				prev = NULL;
			else
			{
				prev = *tree;
				while( prev->next != mpp )
					prev = prev->next;
			}

			/* binary operators */
			if( mpp != *tree && mpp->next && ( prev->type != SFT_OPER || *prev->token == ST_OP_INC || *prev->token == ST_OP_DEC ) )
			{
				TokenList mpptoken = mpp->token;
				int ret1, ret2;
				FTNode* se1 = *tree, *se2 = mpp->next, *se1i = *tree;
				while( se1i )
				{
					if( se1i->next == mpp )
						se1i->next = NULL;
					se1i = se1i->next;
				}
				mpp->next = NULL;
				sgsFT_Destroy( mpp );

				FUNC_ENTER;
				ret1 = level_exp( C, &se1 );
				FUNC_ENTER;
				ret2 = level_exp( C, &se2 );
				if( !ret1 || !ret2 )
				{
					*tree = NULL;
					if( se1 ) sgsFT_Destroy( se1 );
					if( se2 ) sgsFT_Destroy( se2 );
					FUNC_END;
					return 0;
				}
				se1->next = se2;
				*tree = make_node( SFT_OPER, mpptoken, NULL, se1 );
				FUNC_END;
				return 1;
			}
			/* unary operators */
			else if( ST_OP_UNARY( *mpp->token ) )
			{
				int ret1;
				if( !mpp->next )
				{
					FTNode* pp = *tree;
					if( pp == mpp ) goto fail;

					while( pp->next != mpp )
						pp = pp->next;
					pp->next = NULL;
					mpp->child = *tree;
					*tree = mpp;

					mpp->type = SFT_OPER_P;
				}
				else
				{
					mpp->child = mpp->next;
					mpp->next = NULL;
				}
				FUNC_ENTER;
				ret1 = level_exp( C, &mpp->child );
				if( !ret1 )
				{
					FUNC_END;
					return 0;
				}
				FUNC_END;
				return 1;
			}
			/* problems */
			else goto fail;
		}
	}

	if( count <= 1 )
	{
		FUNC_END;
		return 1;
	}

	/* failed unexpectedly, dump & debug */
	sgs_Printf( C, SGS_ERROR, -1, "Failed to level the expression, missing operators or separators." );
	C->state |= SGS_HAS_ERRORS;
#if SGS_DEBUG && SGS_DEBUG_DATA
	sgsFT_Dump( *tree );
#endif
	FUNC_END;
	return 0;

fail:
	sgs_Printf( C, SGS_ERROR, sgsT_LineNum( mpp->token ), "Invalid expression" );
	FUNC_END;
	return 0;
}

/*
	FUNC / parses an expression
	ARGS / context, function tree, token stream
	ERRS / internal, @level_exp
*/
static FTNode* parse_exp( SGS_CTX, TokenList begin, TokenList end )
{
	FTNode* node, *cur;
	TokenList at = begin;

	FUNC_BEGIN;

	sgs_BreakIf( !C );
	sgs_BreakIf( !at );
	sgs_BreakIf( !*at );

	/* special cases */
	if( is_keyword( begin, "var" ) )
	{
		node = parse_arglist( C, sgsT_Next( begin ), end );
		if( node )
			node->type = SFT_VARLIST;
		FUNC_END;
		return node;
	}
	if( is_keyword( begin, "global" ) )
	{
		node = parse_arglist( C, sgsT_Next( begin ), end );
		if( node )
			node->type = SFT_GVLIST;
		FUNC_END;
		return node;
	}

	cur = node = make_node( 0, NULL, NULL, NULL );

	while( at < end )
	{
		if( *at == ST_STRING || *at == ST_NUMINT || *at == ST_NUMREAL )
		{
			cur->next = make_node( SFT_CONST, at, NULL, NULL );
			cur = cur->next;
		}
		else if( *at == ST_IDENT )
		{
			cur->next = make_node( SFT_IDENT, at, NULL, NULL );
			cur = cur->next;
		}
		else if( *at == ST_KEYWORD )
		{
			if( is_keyword( at, "function" ) )
			{
				cur->next = parse_function( C, &at, end, 1 );
				if( !cur->next )
					break;

				cur = cur->next;
				continue;
			}
			else
			{
				cur->next = make_node( SFT_KEYWORD, at, NULL, NULL );
				cur = cur->next;
			}
		}
		else if( ST_ISOP( *at ) )
		{
			cur->next = make_node( SFT_OPER, at, NULL, NULL );
			cur = cur->next;
		}
		else if( ST_ISSPEC( *at ) )
		{
			if( *at == '(' || *at == '[' )
			{
				char cend = *at == '(' ? ')' : ']';
				char endcstr[ 3 ] = { cend, ',', 0 };
				TokenList pat, expend;
				FTNode* exprlist = make_node( *at == '(' ? SFT_EXPLIST : SFT_ARRLIST, at, NULL, NULL );
				FTNode* expr, * curexpr = NULL;

				pat = at;
				at = sgsT_Next( at );
				/* if this is an empty expression (for a function call), do not process it further */
				if( *at != cend )
				{
					do
					{
						expend = detect_exp( C, at, end, endcstr, 0 );
						if( !expend )
						{
						/*	sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "End of expression not found" );	*/
							C->state |= SGS_HAS_ERRORS;
							break;
						}
						if( expend == at )
						{
							sgs_Printf( C, SGS_ERROR, sgsT_LineNum( pat ), "Empty expression found" );
							C->state |= SGS_HAS_ERRORS;
							break;
						}

						FUNC_ENTER;
						expr = parse_exp( C, at, expend );
						if( !expr )
							break;

						if( curexpr ) curexpr->next = expr;
						else exprlist->child = expr;
						curexpr = expr;

						pat = expend;
						at = sgsT_Next( expend );
					}
					while( *expend == ',' );
				}
				else
					at = sgsT_Next( at );

				cur->next = exprlist;
				cur = cur->next;
				continue;
			}
			else if( *at == '{' )
			{
				TokenList expend;
				FTNode* expr = NULL, *fexp = NULL;
				/* dictionary expression */
				at = sgsT_Next( at );
				while( *at != '}' )
				{
					if( *at != ST_IDENT && *at != ST_STRING )
					{
						sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "Expected key identifier in dictionary expression" );
						break;
					}

					if( !fexp )
						expr = fexp = make_node( SFT_IDENT, at, NULL, NULL );
					else
					{
						expr->next = make_node( SFT_IDENT, at, NULL, NULL );
						expr = expr->next;
					}
					at = sgsT_Next( at );

					if( *at != ST_OP_SET )
					{
						sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "Expected '=' in dictionary expression" );
						break;
					}
					at = sgsT_Next( at );

					expend = detect_exp( C, at, end, ",}", 1 );
					if( !expend )
					{
						sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "Could not find end of expression" );
						break;
					}

					expr->next = parse_exp( C, at, expend );
					at = expend;
					if( !expr->next )
						break;
					else
						expr = expr->next;

					if( *at == ',' )
						at = sgsT_Next( at );
				}
				if( *at != '}' )
				{
					if( fexp )
						sgsFT_Destroy( fexp );
					C->state |= SGS_HAS_ERRORS;
				}
				else
				{
					cur->next = make_node( SFT_MAPLIST, NULL, NULL, fexp );
					cur = cur->next;
				}
			}
			else
			{
				sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "Unexpected token '%c' found!", *at );
				C->state |= SGS_MUST_STOP;
			}
		}
		else
		{
			sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "INTERNAL ERROR in parse_exp: unknown token found!" );
			C->state |= SGS_MUST_STOP;
		}

		if( C->state & SGS_MUST_STOP )
		{
			sgsFT_Destroy( node );
			FUNC_END;
			return NULL;
		}

		at = sgsT_Next( at );
	}

	cur = node->next;
	sgs_Free( node );
	node = cur;
	if( !level_exp( C, &node ) )
	{
		C->state |= SGS_HAS_ERRORS;
		if( node ) sgsFT_Destroy( node );
		FUNC_END;
		return NULL;
	}

	FUNC_END;
	return node;
}


/* Statement parsing
*/

static FTNode* parse_explist( SGS_CTX, TokenList begin, TokenList end, char endtok )
{
	TokenList at = begin;
	FTNode* explist = make_node( SFT_EXPLIST, at, NULL, NULL );
	FTNode* curexp = NULL, *node;
	char endtoklist[] = { ',', endtok, 0 };

	while( at < end )
	{
		TokenList epe = detect_exp( C, at, end, endtoklist, 0 );
		if( !epe )
		{
			sgsFT_Destroy( explist );
			C->state |= SGS_HAS_ERRORS;
			return NULL;
		}
		node = parse_exp( C, at, epe );
		if( curexp ) curexp->next = node;
		else explist->child = node;
		curexp = node;
		at = sgsT_Next( epe );
	}

	return explist;
}

static FTNode* parse_if( SGS_CTX, TokenList* begin, TokenList end )
{
	FTNode *node = NULL, *nexp = NULL, *nif = NULL, *nelse = NULL;
	TokenList at = *begin;
	TokenList expend;

	FUNC_BEGIN;

	at = sgsT_Next( at );
	if( *at != '(' )
	{
		sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "Expected '(' after 'if'" );
		goto fail;
	}

	at = sgsT_Next( at );
	expend = detect_exp( C, at, end, ")", 0 );
	if( !expend ) goto fail;

	nexp = parse_exp( C, at, expend );
	at = sgsT_Next( expend );
	if( !nexp || ( C->state & SGS_HAS_ERRORS ) ) goto fail;

	nif = parse_stmt( C, &at, end );
	if( !nif || ( C->state & SGS_HAS_ERRORS ) ) goto fail;

	if( is_keyword( at, "else" ) )
	{
		at = sgsT_Next( at );
		nelse = parse_stmt( C, &at, end );
		if( !nelse || ( C->state & SGS_HAS_ERRORS ) ) goto fail;
	}

	nexp->next = nif;
	nif->next = nelse;
	node = make_node( SFT_IFELSE, *begin, NULL, nexp );
	*begin = at;

	FUNC_END;
	return node;

fail:
	if( nexp ) sgsFT_Destroy( nexp );
	if( nif ) sgsFT_Destroy( nif );
	if( nelse ) sgsFT_Destroy( nelse );
	C->state |= SGS_HAS_ERRORS;
	FUNC_END;
	return NULL;
}

static FTNode* parse_while( SGS_CTX, TokenList* begin, TokenList end )
{
	FTNode *node, *nexp = NULL, *nwhile = NULL;
	TokenList at = *begin;
	TokenList expend;

	FUNC_BEGIN;

	at = sgsT_Next( at );
	if( *at != '(' )
	{
		sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "Expected '(' after 'while'" );
		goto fail;
	}
	at = sgsT_Next( at );
	expend = detect_exp( C, at, end, ")", 0 );
	if( !expend ) goto fail;

	nexp = parse_exp( C, at, expend );
	at = sgsT_Next( expend );
	if( !nexp ) goto fail;

	nwhile = parse_stmt( C, &at, end );
	if( !nwhile ) goto fail;

	nexp->next = nwhile;
	node = make_node( SFT_WHILE, *begin, NULL, nexp );
	*begin = at;

	FUNC_END;
	return node;

fail:
	if( nexp ) sgsFT_Destroy( nexp );
	if( nwhile ) sgsFT_Destroy( nwhile );
	C->state |= SGS_HAS_ERRORS;
	FUNC_END;
	return NULL;
}

static FTNode* parse_dowhile( SGS_CTX, TokenList* begin, TokenList end )
{
	FTNode *node, *nexp = NULL, *nwhile = NULL;
	TokenList at = *begin;
	TokenList expend;

	FUNC_BEGIN;

	at = sgsT_Next( at );
	nwhile = parse_stmt( C, &at, end );
	if( !nwhile ) goto fail;

	if( !is_keyword( at, "while" ) )
	{
		sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "Expected 'while' after statement in do/while" );
		goto fail;
	}

	at = sgsT_Next( at );
	if( *at != '(' )
	{
		sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "Expected '(' after 'while'" );
		goto fail;
	}
	at = sgsT_Next( at );
	expend = detect_exp( C, at, end, ")", 0 );
	if( !expend ) goto fail;

	nexp = parse_exp( C, at, expend );
	at = sgsT_Next( expend );
	if( !nexp ) goto fail;

	nexp->next = nwhile;
	node = make_node( SFT_DOWHILE, *begin, NULL, nexp );
	*begin = at;

	FUNC_END;
	return node;

fail:
	if( nexp ) sgsFT_Destroy( nexp );
	if( nwhile ) sgsFT_Destroy( nwhile );
	C->state |= SGS_HAS_ERRORS;
	FUNC_END;
	return NULL;
}

static FTNode* parse_for( SGS_CTX, TokenList* begin, TokenList end )
{
	FTNode *node, *ninit = NULL, *nexp = NULL, *nincr = NULL, *nwhile = NULL;
	TokenList at = *begin;
	TokenList expend;

	FUNC_BEGIN;

	at = sgsT_Next( at );
	if( *at != '(' )
	{
		sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "Expected '(' after 'for'" );
		goto fail;
	}
	at = sgsT_Next( at );

	expend = detect_exp( C, at, end, ";", 0 );
	if( !expend ) goto fail;
	expend = sgsT_Next( expend );
	ninit = parse_explist( C, at, expend, ';' );
	at = expend;
	if( !ninit ) goto fail;

	expend = detect_exp( C, at, end, ";", 0 );
	if( !expend ) goto fail;
	expend = sgsT_Next( expend );
	nexp = parse_explist( C, at, expend, ';' );
	at = expend;
	if( !nexp ) goto fail;

	expend = detect_exp( C, at, end, ")", 0 );
	if( !expend ) goto fail;
	expend = sgsT_Next( expend );
	nincr = parse_explist( C, at, expend, ')' );
	at = expend;
	if( !nincr ) goto fail;

	nwhile = parse_stmt( C, &at, end );
	if( !nwhile ) goto fail;

	ninit->next = nexp;
	nexp->next = nincr;
	nincr->next = nwhile;
	node = make_node( SFT_FOR, *begin, NULL, ninit );
	*begin = at;

	FUNC_END;
	return node;

fail:
	if( ninit ) sgsFT_Destroy( ninit );
	if( nexp ) sgsFT_Destroy( nexp );
	if( nincr ) sgsFT_Destroy( nincr );
	if( nwhile ) sgsFT_Destroy( nwhile );
	C->state |= SGS_HAS_ERRORS;
	FUNC_END;
	return NULL;
}

static FTNode* parse_foreach( SGS_CTX, TokenList* begin, TokenList end )
{
	FTNode *node, *nvar = NULL, *nexp = NULL, *nwhile = NULL;
	TokenList at = *begin;
	TokenList expend;

	FUNC_BEGIN;

	at = sgsT_Next( at );
	if( *at != '(' )
	{
		sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "Expected '(' after 'foreach'" );
		goto fail;
	}

	at = sgsT_Next( at );
	if( *at != ST_IDENT )
	{
		sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "Expected identifier after '(' in 'foreach'" );
		goto fail;
	}
	nvar = make_node( SFT_IDENT, at, NULL, NULL );

	at = sgsT_Next( at );
	if( *at != ':' )
	{
		sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "Expected ':' after identifier in 'foreach'" );
		goto fail;
	}

	at = sgsT_Next( at );
	expend = detect_exp( C, at, end, ")", 0 );
	if( !expend ) goto fail;
	expend = sgsT_Next( expend );
	nexp = parse_explist( C, at, expend, ')' );
	at = expend;
	if( !nexp ) goto fail;

	nwhile = parse_stmt( C, &at, end );
	if( !nwhile ) goto fail;

	nvar->next = nexp;
	nexp->next = nwhile;
	node = make_node( SFT_FOREACH, *begin, NULL, nvar );
	*begin = at;

	FUNC_END;
	return node;

fail:
	if( nvar ) sgsFT_Destroy( nvar );
	if( nexp ) sgsFT_Destroy( nexp );
	if( nwhile ) sgsFT_Destroy( nwhile );
	FUNC_END;
	return NULL;
}

static FTNode* parse_function( SGS_CTX, TokenList* begin, TokenList end, int inexp )
{
	FTNode *node, *nname = NULL, *nargs = NULL, *nbody = NULL;
	TokenList at = *begin;
	TokenList expend;

	FUNC_BEGIN;

	at = sgsT_Next( at );
	if( !inexp )
	{
		if( *at != ST_IDENT )
		{
			sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "Expected identifier after 'function'" );
			goto fail;
		}
		nname = make_node( SFT_IDENT, at, NULL, NULL );
		at = sgsT_Next( at );
	}

	if( *at != '(' )
	{
		sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), inexp ? "Expected '(' after 'function'"
					: "Expected '(' after 'function' and identifier" );
		goto fail;
	}
	at = sgsT_Next( at );
	expend = detect_exp( C, at, end, ")", 0 );
	if( !expend ) goto fail;
	nargs = parse_arglist( C, at, expend );
	at = sgsT_Next( expend );
	if( !nargs ) goto fail;

	nbody = parse_stmt( C, &at, end );
	if( !nbody ) goto fail;

	nargs->next = nbody;
	nbody->next = nname;
	node = make_node( SFT_FUNC, *begin, NULL, nargs );
	*begin = at;

	FUNC_END;
	return node;

fail:
	if( nname ) sgsFT_Destroy( nname );
	if( nargs ) sgsFT_Destroy( nargs );
	if( nbody ) sgsFT_Destroy( nbody );
	C->state |= SGS_HAS_ERRORS;
	FUNC_END;
	return NULL;
}

static FTNode* parse_stmt( SGS_CTX, TokenList* begin, TokenList end )
{
	FTNode* node;
	TokenList at = *begin;

	FUNC_BEGIN;

	sgs_BreakIf( !C );
	sgs_BreakIf( !at );
	sgs_BreakIf( !*at );

	/* IF / ELSE */
	if( is_keyword( at, "if" ) )
	{
		node = parse_if( C, begin, end );
		FUNC_END;
		return node;
	}
	else if( is_keyword( at, "else" ) )
	{
		sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "Found 'else' without matching 'if'." );
		C->state |= SGS_HAS_ERRORS;
		*begin = sgsT_Next( at );
		FUNC_END;
		return NULL;
	}
	/* WHILE */
	else if( is_keyword( at, "while" ) )
	{
		node = parse_while( C, begin, end );
		FUNC_END;
		return node;
	}
	/* DO / WHILE */
	else if( is_keyword( at, "do" ) )
	{
		node = parse_dowhile( C, begin, end );
		FUNC_END;
		return node;
	}
	/* FOR */
	else if( is_keyword( at, "for" ) )
	{
		node = parse_for( C, begin, end );
		FUNC_END;
		return node;
	}
	/* FOREACH */
	else if( is_keyword( at, "foreach" ) )
	{
		node = parse_foreach( C, begin, end );
		FUNC_END;
		return node;
	}
	/* BREAK */
	else if( is_keyword( at, "break" ) )
	{
		TokenList expend, orig = at;
		at = sgsT_Next( at );
		expend = detect_exp( C, at, end, ";", 0 );
		if( expend && ( ( expend == at ) || ( expend == sgsT_Next( at ) && *at == ST_NUMINT ) ) )
		{
			if( *at == ST_NUMINT )
			{
				sgs_Integer blev = *(sgs_Integer*)( at + 1 );
				if( blev < 1 || blev > 255 )
				{
					sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "Invalid break level (can be between 1 and 255)" );
					goto fail;
				}
			}
			expend = sgsT_Next( expend );
			node = make_node( SFT_BREAK, orig, NULL, NULL );
		}
		else
		{
			sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "Invalid 'break' syntax, needs 'break;' or 'break <int>;'." );
			goto fail;
		}

		*begin = expend;
		FUNC_END;
		return node;
	}
	/* CONTINUE */
	else if( is_keyword( at, "continue" ) )
	{
		TokenList expend, orig = at;
		at = sgsT_Next( at );
		expend = detect_exp( C, at, end, ";", 0 );
		if( expend && ( ( expend == at ) || ( expend == sgsT_Next( at ) && *at == ST_NUMINT ) ) )
		{
			if( *at == ST_NUMINT )
			{
				sgs_Integer blev = *(sgs_Integer*)( at + 1 );
				if( blev < 1 || blev > 255 )
				{
					sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "Invalid continue level (can be between 1 and 255)" );
					goto fail;
				}
			}
			expend = sgsT_Next( expend );
			node = make_node( SFT_CONT, orig, NULL, NULL );
		}
		else
		{
			sgs_Printf( C, SGS_ERROR, sgsT_LineNum( at ), "Invalid 'continue' syntax, needs 'continue;' or 'continue <int>;'." );
			goto fail;
		}

		*begin = expend;
		FUNC_END;
		return node;
	}
	/* FUNCTION */
	else if( is_keyword( at, "function" ) )
	{
		node = parse_function( C, begin, end, 0 );
		FUNC_END;
		return node;
	}
	/* RETURN */
	else if( is_keyword( at, "return" ) )
	{
		TokenList expend;
		at = sgsT_Next( at );
		expend = detect_exp( C, at, end, ";", 0 );
		if( expend )
		{
			expend = sgsT_Next( expend );
			node = parse_explist( C, at, expend, ';' );
		}
		else goto fail;

		if( node )
			node->type = SFT_RETURN;
		*begin = expend;
		FUNC_END;
		return node;
	}
	/* VAR */
	else if( is_keyword( at, "var" ) )
	{
		TokenList expend;
		at = sgsT_Next( at );
		expend = detect_exp( C, at, end, ";", 0 );
		if( !expend ) goto fail;

		node = parse_arglist( C, at, expend );
		if( !node ) goto fail;

		node->type = SFT_VARLIST;
		*begin = sgsT_Next( expend );
		FUNC_END;
		return node;
	}
	/* GLOBAL */
	else if( is_keyword( at, "global" ) )
	{
		TokenList expend;
		at = sgsT_Next( at );
		expend = detect_exp( C, at, end, ";", 0 );
		if( !expend ) goto fail;

		node = parse_arglist( C, at, expend );
		if( !node ) goto fail;

		node->type = SFT_GVLIST;
		*begin = sgsT_Next( expend );
		FUNC_END;
		return node;
	}
	/* BLOCK OF STATEMENTS */
	else if( *at == ST_CBRKL )
	{
		TokenList expend;
		at = sgsT_Next( at );
		expend = detect_exp( C, at, end, "}", 0 );
		if( !expend ) goto fail;

		node = parse_stmtlist( C, at, expend );
		if( !node ) goto fail;

		*begin = sgsT_Next( expend );
		FUNC_END;
		return node;
	}
	/* SEPARATED STATEMENTS */
	else
	{
		TokenList expend = detect_exp( C, at, end, ";", 0 );
		if( expend )
		{
			expend = sgsT_Next( expend );
			node = parse_explist( C, at, expend, ';' );
			*begin = expend;
			FUNC_END;
			return node;
		}
		else
		{
			C->state |= SGS_HAS_ERRORS;
			FUNC_END;
			return NULL;
		}
	}

fail:
	C->state |= SGS_HAS_ERRORS;
	FUNC_END;
	return NULL;
}

static FTNode* parse_stmtlist( SGS_CTX, TokenList begin, TokenList end )
{
	FTNode* stmtlist = make_node( SFT_BLOCK, NULL, NULL, NULL );
	FTNode* curstmt = NULL;

	FUNC_BEGIN;

	while( begin < end && *begin != ST_NULL )
	{
		FTNode* stmt = parse_stmt( C, &begin, end );
		if( curstmt ) curstmt->next = stmt;
		else stmtlist->child = stmt;
		curstmt = stmt;

		if( C->state & SGS_MUST_STOP )
			break;
	}

	FUNC_END;
	return stmtlist;
}

FTNode* sgsFT_Compile( SGS_CTX, TokenList tlist )
{
	return parse_stmtlist( C, tlist, (TokenList) PTR_MAX );
}

/* E N D */

