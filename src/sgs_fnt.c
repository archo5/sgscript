

#include "sgs_fnt.h"
#include "sgs_ctx.h"


static int isoneofN( char ch, const char* what, int size )
{
	const char* end = what + size;
	while( what < end )
	{
		if( ch == *what++ )
			return TRUE;
	}
	return FALSE;
}

static int is_keyword( TokenList tok, const char* text )
{
	return *tok == ST_KEYWORD && tok[ 1 ] == strlen( text ) && strncmp( (const char*) tok + 2, text, tok[ 1 ] ) == 0;
}
/*
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
*/

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
	case SFT_FOREACH: printf( "FOR_EACH" ); break;
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


typedef struct _ftcomp
{
	SGS_CTX;
	TokenList at;
}
FTComp;

#define SFTC FTComp* F
#define SFTRET static FTNode*
#define SFTC_VALIDATE sgs_BreakIf( !*F->at )
#define SFTC_AT F->at
#define SFTC_NEXT F->at = sgsT_Next( F->at )
#define SFTC_IS( type ) (*F->at == (type))
#define SFTC_IN( buf, sz ) isoneofN( *F->at, buf, sz )
#define SFTC_HASERR ( F->C->state & SGS_HAS_ERRORS )
#define SFTC_SETERR F->C->state |= SGS_HAS_ERRORS
#define SFTC_ISKEY( name ) is_keyword( F->at, name )
#define SFTC_LINENUM sgsT_LineNum( F->at )
#define SFTC_PRINTERR( what ) sgs_Printf( F->C, SGS_ERROR, SFTC_LINENUM, what )
#define SFTC_UNEXP SFTC_PRINTERR( "Unexpected end of code" )





SFTRET parse_exp( SFTC, char* endtoklist, int etlsize );
SFTRET parse_stmt( SFTC );
SFTRET parse_stmtlist( SFTC, char end );
SFTRET parse_function( SFTC, int inexp );





/*
	FUNC / finds the logical expected end of an expression
	ARGS / context, token stream, list of ending characters
	ERRS / brace mismatch, end of expression
*/
/*
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
*/


SFTRET parse_arg( SFTC, int argid, char end )
{
	FTNode* node = NULL;
	char toks[ 3 ] = { ',', end, 0 };

	FUNC_BEGIN;

	SFTC_VALIDATE;

	if( SFTC_IS( ST_KEYWORD ) )
	{
		SFTC_PRINTERR( "Argument name cannot be a reserved keyword" );
		goto fail;
	}

	if( !SFTC_IS( ST_IDENT ) && !SFTC_IS( ST_KEYWORD ) )
	{
		sgs_Printf( F->C, SGS_ERROR, SFTC_LINENUM, "Unexpected token while parsing argument %d", argid );
		goto fail;
	}

	node = make_node( SFT_ARGMT, SFTC_AT, NULL, NULL );
	SFTC_NEXT;

	if( SFTC_IS( ST_OP_SET ) )
	{
		SFTC_NEXT;
		if( SFTC_IS( end ) || SFTC_IS( ',' ) )
		{
			SFTC_PRINTERR( "Expected initializing expression" );
			goto fail;
		}
		node->child = parse_exp( F, toks, 2 );
		if( !node->child )
			goto fail;
	}

	FUNC_END;
	return node;

fail:
	if( node ) sgsFT_Destroy( node );
	SFTC_SETERR;
	FUNC_END;
	return 0;
}

/*
	FUNC / parses a list of arguments
	ARGS / context, function tree, token stream
	ERRS / unexpected token, @parse_arg
*/
SFTRET parse_arglist( SFTC, char end )
{
	FTNode* arglist = make_node( SFT_ARGLIST, NULL, NULL, NULL );
	FTNode* curnode = NULL;
	FTNode* argnode;
	int id = 1;

	FUNC_BEGIN;

	SFTC_VALIDATE;

	for(;;)
	{
		if( SFTC_IS( end ) )
		{
			break;
		}
		else if( SFTC_IS( 0 ) )
		{
			SFTC_UNEXP;
			goto fail;
		}
		else if( id == 1 || SFTC_IS( ',' ) )
		{
			if( id != 1 )
				SFTC_NEXT;
			argnode = parse_arg( F, id, end );
			if( !argnode && F->C->state & SGS_MUST_STOP )
			{
				goto fail;
			}
			if( curnode ) curnode->next = argnode;
			else arglist->child = argnode;
			curnode = argnode;

			id++;
		}
	}

	FUNC_END;
	return arglist;

fail:
	SFTC_SETERR;
	sgsFT_Destroy( arglist );
	return NULL;
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



static LineNum findlinenum( FTNode* node ) /* local, next, child */
{
	LineNum ln = -1;

	if( node->token )
		return sgsT_LineNum( node->token );

	ln = findlinenum( node->next );
	if( ln != -1 ) return ln;

	ln = findlinenum( node->child );
	if( ln != -1 ) return ln;

	return -1;
}

static LineNum predictlinenum( FTNode* node ) /* next, child, local */
{
	LineNum ln = -1;

	ln = findlinenum( node->next );
	if( ln != -1 ) return ln;

	ln = predictlinenum( node->child );
	if( ln != -1 ) return ln;

	if( node->token )
		return sgsT_LineNum( node->token );

	return -1;
}


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

	/* in case we failed unexpectedly, dump & debug */
	sgs_Printf( C, SGS_ERROR, predictlinenum( *tree ), "Missing operators or separators." );
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


SFTRET parse_exp( SFTC, char* endtoklist, int etlsize )
{
	FTNode* node, *cur;
	char prev = 0;

	FUNC_BEGIN;

	SFTC_VALIDATE;

	/* special cases */
	if( SFTC_ISKEY( "var" ) )
	{
		SFTC_NEXT;
		node = parse_arglist( F, endtoklist[ endtoklist[0] == ',' ] );
		if( node )
			node->type = SFT_VARLIST;
		FUNC_END;
		return node;
	}
	if( SFTC_ISKEY( "global" ) )
	{
		SFTC_NEXT;
		node = parse_arglist( F, endtoklist[ endtoklist[0] == ',' ] );
		if( node )
			node->type = SFT_GVLIST;
		FUNC_END;
		return node;
	}

	cur = node = make_node( 0, NULL, NULL, NULL );

	for(;;)
	{
		if( SFTC_IN( endtoklist, etlsize ) )
		{
			break;
		}
		else if( SFTC_IS( 0 ) )
		{
			SFTC_UNEXP;
			goto fail;
		}
		else if( SFTC_IS( ST_STRING )
			  || SFTC_IS( ST_NUMINT )
			  || SFTC_IS( ST_NUMREAL ) )
			cur = cur->next = make_node( SFT_CONST, SFTC_AT, NULL, NULL );
		else if( SFTC_IS( ST_IDENT ) )
			cur = cur->next = make_node( SFT_IDENT, SFTC_AT, NULL, NULL );
		else if( SFTC_IS( ST_KEYWORD ) )
		{
			if( SFTC_ISKEY( "function" ) )
			{
				cur->next = parse_function( F, 1 );
				if( !cur->next )
					goto fail;

				cur = cur->next;
				continue;
			}
			else
				cur = cur->next = make_node( SFT_KEYWORD, SFTC_AT, NULL, NULL );
		}
		else if( ST_ISOP( *SFTC_AT ) )
			cur = cur->next = make_node( SFT_OPER, SFTC_AT, NULL, NULL );
		else if( ST_ISSPEC( *SFTC_AT ) )
		{
			/* array accesor / argument list / subexpression */
			if( SFTC_IS( '(' ) || SFTC_IS( '[' ) )
			{
				int isidx = prev == ST_IDENT || prev == ')' || prev == ']';
				char cend = SFTC_IS( '(' ) ? ')' : ']';
				char endcstr[ 3 ] = { ',', cend, 0 };
				FTNode* exprlist = make_node( SFTC_IS( '(' ) ? SFT_EXPLIST : SFT_ARRLIST, SFTC_AT, NULL, NULL );
				FTNode* expr, * curexpr = NULL;

				SFTC_NEXT;
				/* if this is an empty expression (for a function call), do not process it further */
				if( !SFTC_IS( cend ) )
				{
					for(;;)
					{
						if( !isidx && SFTC_IS( cend ) )
						{
							SFTC_NEXT;
							break;
						}

						FUNC_ENTER;
						expr = parse_exp( F, endcstr, 2 );
						if( !expr )
							break;

						if( curexpr )
							curexpr->next = expr;
						else
							exprlist->child = expr;
						curexpr = expr;

						if( SFTC_IS( cend ) )
							break;

						SFTC_NEXT;
					}
				}
				else
					SFTC_NEXT;

				cur = cur->next = exprlist;
				continue;
			}
			/* dictionary */
			else if( SFTC_IS( '{' ) )
			{
				TokenList startok = SFTC_AT;
				FTNode* expr = NULL, *fexp = NULL;
				/* dictionary expression */
				SFTC_NEXT;
				while( !SFTC_IS( '}' ) )
				{
					if( !SFTC_IS( ST_IDENT ) && !SFTC_IS( ST_STRING ) )
					{
						SFTC_PRINTERR( "Expected key identifier in dictionary expression" );
						break;
					}

					if( !fexp )
						expr = fexp = make_node( SFT_IDENT, SFTC_AT, NULL, NULL );
					else
					{
						expr->next = make_node( SFT_IDENT, SFTC_AT, NULL, NULL );
						expr = expr->next;
					}
					SFTC_NEXT;

					if( !SFTC_IS( ST_OP_SET ) )
					{
						SFTC_PRINTERR( "Expected '=' in dictionary expression" );
						break;
					}
					SFTC_NEXT;

					expr->next = parse_exp( F, ",}", 2 );
					if( !expr->next )
						break;
					else
						expr = expr->next;

					if( SFTC_IS( ',' ) )
						SFTC_NEXT;
				}
				if( !SFTC_IS( '}' ) )
				{
					if( fexp )
						sgsFT_Destroy( fexp );
					SFTC_SETERR;
				}
				else
				{
					cur->next = make_node( SFT_MAPLIST, startok, NULL, fexp );
					cur = cur->next;
				}
			}
			else
			{
				sgs_Printf( F->C, SGS_ERROR, SFTC_LINENUM, "Unexpected token '%c' found!", *SFTC_AT );
				F->C->state |= SGS_MUST_STOP;
			}
		}
		else
		{
			SFTC_PRINTERR( "INTERNAL ERROR in parse_exp: unknown token found!" );
			F->C->state |= SGS_MUST_STOP;
		}

		if( F->C->state & SGS_MUST_STOP )
		{
			sgsFT_Destroy( node );
			FUNC_END;
			return NULL;
		}

		prev = *SFTC_AT;
		SFTC_NEXT;
	}

	cur = node->next;
	sgs_Free( node );
	node = cur;
	if( !level_exp( F->C, &node ) )
		goto fail;

	FUNC_END;
	return node;

fail:
	SFTC_SETERR;
	if( node ) sgsFT_Destroy( node );
	FUNC_END;
	return NULL;
}





SFTRET parse_explist( SFTC, char endtok )
{
	FTNode* explist = make_node( SFT_EXPLIST, SFTC_AT, NULL, NULL );
	FTNode* curexp = NULL, *node;
	char endtoklist[] = { ',', endtok, 0 };

	for(;;)
	{
		if( SFTC_IS( endtok ) )
		{
			break;
		}
		else if( SFTC_IS( 0 ) )
		{
			SFTC_UNEXP;
			SFTC_SETERR;
			sgsFT_Destroy( explist );
			return NULL;
		}
		else if( SFTC_IS( ',' ) )
		{
			node = parse_exp( F, endtoklist, 2 );
			if( curexp )
				curexp->next = node;
			else
				explist->child = node;
			curexp = node;
			SFTC_NEXT;
		}
		else
		{
			sgs_Printf( F->C, SGS_ERROR, SFTC_LINENUM, "Expected ',' or '%c'", endtok );
			SFTC_SETERR;
			sgsFT_Destroy( explist );
			return NULL;
		}
	}

	return explist;
}

SFTRET parse_if( SFTC )
{
	FTNode *node = NULL, *nexp = NULL, *nif = NULL, *nelse = NULL;
	TokenList begin = SFTC_AT;

	FUNC_BEGIN;

	SFTC_NEXT;
	if( !SFTC_IS( '(' ) )
	{
		SFTC_PRINTERR( "Expected '(' after 'if'" );
		goto fail;
	}

	SFTC_NEXT;

	nexp = parse_exp( F, ")", 1 );
	if( !nexp ) goto fail;
	SFTC_NEXT;

	nif = parse_stmt( F );
	if( !nif ) goto fail;

	if( SFTC_ISKEY( "else" ) )
	{
		SFTC_NEXT;
		nelse = parse_stmt( F );
		if( !nelse ) goto fail;
	}

	nexp->next = nif;
	nif->next = nelse;
	node = make_node( SFT_IFELSE, begin, NULL, nexp );

	FUNC_END;
	return node;

fail:
	if( nexp ) sgsFT_Destroy( nexp );
	if( nif ) sgsFT_Destroy( nif );
	if( nelse ) sgsFT_Destroy( nelse );
	SFTC_SETERR;
	FUNC_END;
	return NULL;
}

SFTRET parse_while( SFTC )
{
	FTNode *node, *nexp = NULL, *nwhile = NULL;
	TokenList begin = SFTC_AT;

	FUNC_BEGIN;

	SFTC_NEXT;
	if( !SFTC_IS( '(' ) )
	{
		SFTC_PRINTERR( "Expected '(' after 'while'" );
		goto fail;
	}

	SFTC_NEXT;

	nexp = parse_exp( F, ")", 1 );
	if( !nexp ) goto fail;
	SFTC_NEXT;

	nwhile = parse_stmt( F );
	if( !nwhile ) goto fail;

	nexp->next = nwhile;
	node = make_node( SFT_WHILE, begin, NULL, nexp );

	FUNC_END;
	return node;

fail:
	if( nexp ) sgsFT_Destroy( nexp );
	if( nwhile ) sgsFT_Destroy( nwhile );
	SFTC_SETERR;
	FUNC_END;
	return NULL;
}

SFTRET parse_dowhile( SFTC )
{
	FTNode *node, *nexp = NULL, *nwhile = NULL;
	TokenList begin = SFTC_AT;

	FUNC_BEGIN;

	SFTC_NEXT;
	nwhile = parse_stmt( F );
	if( !nwhile ) goto fail;

	if( !SFTC_ISKEY( "while" ) )
	{
		SFTC_PRINTERR( "Expected 'while' after statement in do/while" );
		goto fail;
	}

	SFTC_NEXT;
	if( !SFTC_IS( '(' ) )
	{
		SFTC_PRINTERR( "Expected '(' after 'while'" );
		goto fail;
	}

	SFTC_NEXT;

	nexp = parse_exp( F, ")", 1 );
	if( !nexp ) goto fail;

	nexp->next = nwhile;
	node = make_node( SFT_DOWHILE, begin, NULL, nexp );

	FUNC_END;
	return node;

fail:
	if( nexp ) sgsFT_Destroy( nexp );
	if( nwhile ) sgsFT_Destroy( nwhile );
	SFTC_SETERR;
	FUNC_END;
	return NULL;
}

SFTRET parse_for( SFTC )
{
	FTNode *node, *ninit = NULL, *nexp = NULL, *nincr = NULL, *nwhile = NULL;
	TokenList begin = SFTC_AT;

	FUNC_BEGIN;

	SFTC_NEXT;
	if( !SFTC_IS( '(' ) )
	{
		SFTC_PRINTERR( "Expected '(' after 'for'" );
		goto fail;
	}

	SFTC_NEXT;

	ninit = parse_explist( F, ';' );
	if( !ninit ) goto fail;
	SFTC_NEXT;

	nexp = parse_explist( F, ';' );
	if( !nexp ) goto fail;
	SFTC_NEXT;

	nincr = parse_explist( F, ')' );
	if( !nincr ) goto fail;
	SFTC_NEXT;

	nwhile = parse_stmt( F );
	if( !nwhile ) goto fail;

	ninit->next = nexp;
	nexp->next = nincr;
	nincr->next = nwhile;
	node = make_node( SFT_FOR, begin, NULL, ninit );

	FUNC_END;
	return node;

fail:
	if( ninit ) sgsFT_Destroy( ninit );
	if( nexp ) sgsFT_Destroy( nexp );
	if( nincr ) sgsFT_Destroy( nincr );
	if( nwhile ) sgsFT_Destroy( nwhile );
	SFTC_SETERR;
	FUNC_END;
	return NULL;
}

SFTRET parse_foreach( SFTC )
{
	FTNode *node, *nvar = NULL, *nexp = NULL, *nwhile = NULL;
	TokenList begin = SFTC_AT;

	FUNC_BEGIN;

	SFTC_NEXT;
	if( !SFTC_IS( '(' ) )
	{
		SFTC_PRINTERR( "Expected '(' after 'foreach'" );
		goto fail;
	}

	SFTC_NEXT;
	if( !SFTC_IS( ST_IDENT ) )
	{
		SFTC_PRINTERR( "Expected identifier after '(' in 'foreach'" );
		goto fail;
	}
	nvar = make_node( SFT_IDENT, SFTC_AT, NULL, NULL );

	SFTC_NEXT;
	if( !SFTC_IS( ':' ) )
	{
		SFTC_PRINTERR( "Expected ':' after identifier in 'foreach'" );
		goto fail;
	}

	SFTC_NEXT;

	nexp = parse_explist( F, ')' );
	if( !nexp ) goto fail;

	SFTC_NEXT;
	nwhile = parse_stmt( F );
	if( !nwhile ) goto fail;

	nvar->next = nexp;
	nexp->next = nwhile;
	node = make_node( SFT_FOREACH, begin, NULL, nvar );

	FUNC_END;
	return node;

fail:
	if( nvar ) sgsFT_Destroy( nvar );
	if( nexp ) sgsFT_Destroy( nexp );
	if( nwhile ) sgsFT_Destroy( nwhile );
	FUNC_END;
	return NULL;
}

SFTRET parse_function( SFTC, int inexp )
{
	FTNode *node, *nname = NULL, *nargs = NULL, *nbody = NULL;
	TokenList begin = SFTC_AT;

	FUNC_BEGIN;

	SFTC_NEXT;
	if( !inexp )
	{
		if( !SFTC_IS( ST_IDENT ) )
		{
			SFTC_PRINTERR( "Expected identifier after 'function'" );
			goto fail;
		}
		nname = make_node( SFT_IDENT, SFTC_AT, NULL, NULL );
		SFTC_NEXT;
		if( SFTC_IS( ST_OP_MMBR ) )
		{
			nname = make_node( SFT_OPER, SFTC_AT, NULL, nname );
			SFTC_NEXT;
			if( !SFTC_IS( ST_IDENT ) )
			{
				SFTC_PRINTERR( "Expected identifier after 'function', identifier and '.'" );
				goto fail;
			}
			else
			{
				nname->child->next = make_node( SFT_IDENT, SFTC_AT, NULL, NULL );
				SFTC_NEXT;
			}
		}
	}

	if( !SFTC_IS( '(' ) )
	{
		SFTC_PRINTERR( inexp ? "Expected '(' after 'function'"
			: "Expected '(' after 'function' and its name" );
		goto fail;
	}

	SFTC_NEXT;

	nargs = parse_arglist( F, ')' );
	if( !nargs ) goto fail;
	SFTC_NEXT;

	nbody = parse_stmt( F );
	if( !nbody ) goto fail;

	nargs->next = nbody;
	nbody->next = nname;
	node = make_node( SFT_FUNC, begin, NULL, nargs );

	FUNC_END;
	return node;

fail:
	if( nname ) sgsFT_Destroy( nname );
	if( nargs ) sgsFT_Destroy( nargs );
	if( nbody ) sgsFT_Destroy( nbody );
	SFTC_SETERR;
	FUNC_END;
	return NULL;
}

SFTRET parse_stmt( SFTC )
{
	FTNode* node;

	FUNC_BEGIN;

	SFTC_VALIDATE;

	/* IF / ELSE */
	if( SFTC_ISKEY( "if" ) ) { node = parse_if( F ); FUNC_END; return node; }
	else if( SFTC_ISKEY( "else" ) )
	{
		SFTC_PRINTERR( "Found 'else' without matching 'if'." );
		goto fail;
	}
	/* WHILE */
	else if( SFTC_ISKEY( "while" ) ) { node = parse_while( F ); FUNC_END; return node; }
	/* DO / WHILE */
	else if( SFTC_ISKEY( "do" ) ) { node = parse_dowhile( F ); FUNC_END; return node; }
	/* FOR */
	else if( SFTC_ISKEY( "for" ) ) { node = parse_for( F ); FUNC_END; return node; }
	/* FOREACH */
	else if( SFTC_ISKEY( "foreach" ) ) { node = parse_foreach( F ); FUNC_END; return node; }
	/* BREAK */
	else if( SFTC_ISKEY( "break" ) )
	{
		TokenList orig = SFTC_AT;
		SFTC_NEXT;

		if( SFTC_IS( ST_NUMINT ) )
		{
			sgs_Integer blev = *(sgs_Integer*)( SFTC_AT + 1 );
			if( blev < 1 || blev > 255 )
			{
				SFTC_PRINTERR( "Invalid break level (can be between 1 and 255)" );
				goto fail;
			}
			SFTC_NEXT;
		}

		node = make_node( SFT_BREAK, orig, NULL, NULL );

		FUNC_END;
		return node;
	}
	/* CONTINUE */
	else if( SFTC_ISKEY( "continue" ) )
	{
		TokenList orig = SFTC_AT;
		SFTC_NEXT;

		if( SFTC_IS( ST_NUMINT ) )
		{
			sgs_Integer blev = *(sgs_Integer*)( SFTC_AT + 1 );
			if( blev < 1 || blev > 255 )
			{
				SFTC_PRINTERR( "Invalid continue level (can be between 1 and 255)" );
				goto fail;
			}
			SFTC_NEXT;
		}

		node = make_node( SFT_CONT, orig, NULL, NULL );

		FUNC_END;
		return node;
	}
	/* FUNCTION */
	else if( SFTC_ISKEY( "function" ) ) { node = parse_function( F, 0 ); FUNC_END; return node; }
	/* RETURN */
	else if( SFTC_ISKEY( "return" ) )
	{
		SFTC_NEXT;
		node = parse_explist( F, ';' );

		if( node )
			node->type = SFT_RETURN;

		SFTC_NEXT;
		FUNC_END;
		return node;
	}
	/* VAR / GLOBAL - reuse code in parse_exp */
	else if( SFTC_ISKEY( "var" ) || SFTC_ISKEY( "global" ) )
	{
		node = parse_exp( F, ";", 1 );
		if( !node )
			goto fail;
		if( !SFTC_IS( ';' ) )
		{
			SFTC_UNEXP;
			sgsFT_Destroy( node );
			goto fail;
		}

		SFTC_NEXT;
		FUNC_END;
		return node;
	}
	/* BLOCK OF STATEMENTS */
	else if( SFTC_IS( ST_CBRKL ) )
	{
		node = parse_stmtlist( F, '}' );
		if( !node ) goto fail;

		SFTC_NEXT;
		FUNC_END;
		return node;
	}
	/* SEPARATED STATEMENTS */
	else
	{
		node = parse_explist( F, ';' );
		if( node )
		{
			SFTC_NEXT;
			FUNC_END;
			return node;
		}
		else
			goto fail;
	}

fail:
	SFTC_SETERR;
	FUNC_END;
	return NULL;
}

SFTRET parse_stmtlist( SFTC, char end )
{
	FTNode* stmtlist = make_node( SFT_BLOCK, NULL, NULL, NULL );
	FTNode* curstmt = NULL;

	FUNC_BEGIN;

	for(;;)
	{
		if( SFTC_IS( end ) )
		{
			break;
		}
		else if( SFTC_IS( 0 ) )
		{
			SFTC_UNEXP;
			SFTC_SETERR;
		}
		else
		{
			FTNode* stmt = parse_stmt( F );
			if( curstmt )
				curstmt->next = stmt;
			else
				stmtlist->child = stmt;
			curstmt = stmt;
		}

		if( F->C->state & SGS_MUST_STOP )
			break;
	}

	FUNC_END;
	return stmtlist;
}

FTNode* sgsFT_Compile( SGS_CTX, TokenList tlist )
{
	FTNode* ret;
	FTComp F = { C, tlist };
	ret = parse_stmtlist( &F, 0 );
	return ret;
}


