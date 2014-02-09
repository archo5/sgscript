

#define SGS_INTERNAL
#define SGS_REALLY_INTERNAL

#include "sgs_int.h"


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
	return *tok == ST_KEYWORD && tok[ 1 ] == strlen( text ) &&
		strncmp( (const char*) tok + 2, text, tok[ 1 ] ) == 0;
}

static int is_ident( TokenList tok, const char* text )
{
	return *tok == ST_IDENT && tok[ 1 ] == strlen( text ) &&
		strncmp( (const char*) tok + 2, text, tok[ 1 ] ) == 0;
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
	case SFT_USELIST: printf( "USE_LIST" ); break;
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
	case SFT_BREAK: printf( "BREAK" );
		if( *sgsT_Next( N->token ) == ST_NUMINT )
		{
			sgs_Int val;
			AS_INTEGER( val, sgsT_Next( N->token ) + 1 );
			printf( " %" PRId64, val );
		}
		break;
	case SFT_CONT: printf( "CONTINUE" );
		if( *sgsT_Next( N->token ) == ST_NUMINT )
		{
			sgs_Int val;
			AS_INTEGER( val, sgsT_Next( N->token ) + 1 );
			printf( " %" PRId64, val );
		}
		break;
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
#define SFTC_AT F->at
#define SFTC_NEXT F->at = sgsT_Next( F->at )
#define SFTC_IS( type ) (*F->at == (type))
/* WP: char/uchar conversion */
#define SFTC_IN( buf, sz ) isoneofN( (char) *F->at, buf, sz )
#define SFTC_HASERR ( F->C->state & SGS_HAS_ERRORS )
#define SFTC_SETERR F->C->state |= SGS_HAS_ERRORS
#define SFTC_ISKEY( name ) is_keyword( F->at, name )
#define SFTC_IS_ID( name ) is_ident( F->at, name )
#define SFTC_LINENUM sgsT_LineNum( F->at )
#define SFTC_PRINTERR( what ) sgs_Msg( F->C, SGS_ERROR, "[line %d] " what, SFTC_LINENUM )
#define SFTC_UNEXP sgs_Msg( F->C, SGS_ERROR, "Unexpected end of code", SFTC_LINENUM )


static FTNode* _make_node( SGS_CTX, int type, TokenList token, FTNode* next, FTNode* child )
{
	FTNode* node = sgs_Alloc( FTNode );
	node->type = type;
	node->token = token;
	node->next = next;
	node->child = child;
	return node;
}
#define make_node( ty, tok, next, ch ) _make_node( F->C, ty, tok, next, ch )

void sgsFT_Destroy( SGS_CTX, FTNode* tree )
{
	if( tree->next ) sgsFT_Destroy( C, tree->next );
	if( tree->child ) sgsFT_Destroy( C, tree->child );
	sgs_Dealloc( tree );
}
#define SFTC_DESTROY( node ) sgsFT_Destroy( F->C, node )





SFTRET parse_exp( SFTC, char* endtoklist, int etlsize );
SFTRET parse_stmt( SFTC );
SFTRET parse_stmtlist( SFTC, char end );
SFTRET parse_function( SFTC, int inexp );





SFTRET parse_arg( SFTC, int argid, char end )
{
	FTNode* node = NULL;
	char toks[ 3 ] = { ',', 0, 0 };
	toks[1] = end;

	FUNC_BEGIN;

	if( SFTC_IS(0) )
	{
		SFTC_UNEXP;
		goto fail;
	}

	if( SFTC_IS( ST_KEYWORD ) )
	{
		SFTC_PRINTERR( "Argument name cannot be a reserved keyword" );
		goto fail;
	}

	if( !SFTC_IS( ST_IDENT ) && !SFTC_IS( ST_KEYWORD ) )
	{
		sgs_Msg( F->C, SGS_ERROR, "[line %d] Unexpected token while parsing argument %d", SFTC_LINENUM, argid );
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
	if( node ) SFTC_DESTROY( node );
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
		else
		{
			sgs_Msg( F->C, SGS_ERROR, "[line %d] Expected ',' or '%c'", SFTC_LINENUM, end );
			goto fail;
		}
	}

	FUNC_END;
	return arglist;

fail:
	SFTC_SETERR;
	SFTC_DESTROY( arglist );
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
		sgs_TokenType type = *part->token;
		if( binary )
		{
			if( ST_OP_BOOL( type ) )	return 30;
			if( ST_OP_COMP( type ) )	return 28;
			if( type == ST_OP_CAT )		return 27;
			if( ST_OP_BINOPS( type ) )	return 26;
			if( ST_OP_BINADD( type ) )	return 25;
			if( ST_OP_BINMUL( type ) )	return 24;
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
		return 0;
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

		/* HACK: discard unary operators following unary operators */
		if( !binary && !isfcall && mpp && mpp->next == node && ST_OP_UNARY( *mpp->token ) )
			goto _continue;

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
				if( se1 ) sgsFT_Destroy( C, se1 );
				if( se2 ) sgsFT_Destroy( C, se2 );
				FUNC_END;
				return 0;
			}
			if( *mpp_token == ST_SBRKL )
			{
				/* array */
				if( !se2->child || se2->child->next )
				{
					sgs_Msg( C, SGS_ERROR, "[line %d] Invalid number of arguments "
						"in an array accessor", sgsT_LineNum( mpp_token ) );
					*tree = NULL;
					if( se1 ) sgsFT_Destroy( C, se1 );
					if( se2 ) sgsFT_Destroy( C, se2 );
					FUNC_END;
					return 0;
				}
				se1->next = se2->child;
				se2->child = NULL;
				sgsFT_Destroy( C, se2 );
				*tree = _make_node( C, SFT_INDEX, mpp_token, NULL, se1 );
				return 1;
			}
			se1->next = se2;
			*tree = _make_node( C, SFT_FCALL, mpp_token, NULL, se1 );
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
				sgsFT_Destroy( C, mpp );

				FUNC_ENTER;
				ret1 = level_exp( C, &se1 );
				FUNC_ENTER;
				ret2 = level_exp( C, &se2 );
				if( !ret1 || !ret2 )
				{
					*tree = NULL;
					if( se1 ) sgsFT_Destroy( C, se1 );
					if( se2 ) sgsFT_Destroy( C, se2 );
					FUNC_END;
					return 0;
				}
				
				se1->next = se2;
				*tree = _make_node( C, SFT_OPER, mpptoken, NULL, se1 );
				FUNC_END;
				
				if( *mpptoken == ST_OP_CAT || *mpptoken == ST_OP_CATEQ )
				{
					/* merge in CAT on first operand (works only with non-assignment op) */
					if( *mpptoken == ST_OP_CAT && *se1->token == ST_OP_CAT )
					{
						/* target tree: tree { se1:children, se2 } */
						FTNode* tmp = se1->child;
						while( tmp->next )
							tmp = tmp->next;
						tmp->next = se2;
						(*tree)->child = se1->child;
						
						se1->child = NULL;
						se1->next = NULL;
						sgsFT_Destroy( C, se1 );
					}
					/* merge in CAT on second operand */
					if( *se2->token == ST_OP_CAT )
					{
						/* target tree: tree { children without se2, se2:children } */
						FTNode* tmp = (*tree)->child;
						while( tmp->next && tmp->next != se2 )
							tmp = tmp->next;
						sgs_BreakIf( tmp->next == NULL );
						tmp->next = se2->child;
						
						se2->child = NULL;
						sgsFT_Destroy( C, se2 );
					}
				}
				
				return 1;
			}
			/* unary operators
				must be one of these cases:
				- no tokens before operator
				- is inc|dec and there are tokens either before or after the operator
					- can't have both, can't have neither
			*/
			else if( ST_OP_UNARY( *mpp->token ) && ( mpp == *tree || 
				( ( *mpp->token == ST_OP_INC || *mpp->token == ST_OP_DEC ) &&
				( mpp != *tree ) != ( !!mpp->next ) ) ) )
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
	sgs_Msg( C, SGS_ERROR, "[line %d] Missing operators or separators", predictlinenum( *tree ) );
	C->state |= SGS_HAS_ERRORS;
#if SGS_DEBUG && SGS_DEBUG_DATA
	sgsFT_Dump( *tree );
#endif
	FUNC_END;
	return 0;

fail:
	sgs_Msg( C, SGS_ERROR, "[line %d] Invalid expression", sgsT_LineNum( mpp->token ) );
#if SGS_DEBUG && SGS_DEBUG_DATA
	sgsFT_Dump( *tree );
#endif
	FUNC_END;
	return 0;
}


SFTRET parse_exp( SFTC, char* endtoklist, int etlsize )
{
	FTNode* node, *cur;
	char prev = 0;

	FUNC_BEGIN;

	if( SFTC_IS( 0 ) )
	{
		SFTC_UNEXP;
		SFTC_SETERR;
		FUNC_END;
		return NULL;
	}

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
				FTNode* exprlist = make_node( SFTC_IS( '(' ) ? SFT_EXPLIST : SFT_ARRLIST, SFTC_AT, NULL, NULL );
				FTNode* expr, * curexpr = NULL;
				char endcstr[ 3 ] = { ',', 0, 0 };
				endcstr[1] = cend;

				SFTC_NEXT;
				/* if this is an empty expression (for a function call), do not process it further */
				if( !SFTC_IS( cend ) )
				{
					for(;;)
					{
						/* if not index, extra ',' is allowed */
						if( !isidx && SFTC_IS( cend ) )
						{
							SFTC_NEXT;
							break;
						}

						FUNC_ENTER;
						expr = parse_exp( F, endcstr, 2 );
						if( !expr )
						{
							SFTC_DESTROY( exprlist );
							goto fail;
						}

						if( curexpr )
							curexpr->next = expr;
						else
							exprlist->child = expr;
						curexpr = expr;

						if( SFTC_IS( cend ) )
						{
							SFTC_NEXT;
							break;
						}

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
						SFTC_PRINTERR( "Expected '=' in dictionary expression "
							"/ missing closing bracket before '{'" );
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
						SFTC_DESTROY( fexp );
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
				sgs_Msg( F->C, SGS_ERROR, "[line %d] Unexpected token '%c' found!", SFTC_LINENUM, *SFTC_AT );
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
			SFTC_DESTROY( node );
			FUNC_END;
			return NULL;
		}
		
		/* WP: char/uchar conversion */
		prev = (char) *SFTC_AT;
		SFTC_NEXT;
	}

	cur = node->next;
	sgs_Free( F->C, node );
	node = cur;
	if( !node )
	{
		SFTC_PRINTERR( "Empty expression found" );
		goto fail;
	}

	if( !level_exp( F->C, &node ) )
		goto fail;

	FUNC_END;
	return node;

fail:
	SFTC_SETERR;
	if( node ) SFTC_DESTROY( node );
	FUNC_END;
	return NULL;
}





SFTRET parse_explist( SFTC, char endtok )
{
	FTNode* explist = make_node( SFT_EXPLIST, SFTC_AT, NULL, NULL );
	FTNode* curexp = NULL, *node;
	char endtoklist[] = { ',', 0, 0 };
	endtoklist[1] = endtok;

	FUNC_BEGIN;

	for(;;)
	{
		if( SFTC_IS( endtok ) )
		{
			break;
		}
		else if( SFTC_IS( 0 ) )
		{
			SFTC_UNEXP;
			goto fail;
		}
		else if( SFTC_IS( ',' ) || SFTC_AT == explist->token )
		{
			if( SFTC_AT != explist->token )
				SFTC_NEXT;
			node = parse_exp( F, endtoklist, 2 );
			if( !node )
				goto fail;
			if( curexp )
				curexp->next = node;
			else
				explist->child = node;
			curexp = node;
		}
		else
		{
			sgs_Msg( F->C, SGS_ERROR, "[line %d] Expected ',' or '%c'", SFTC_LINENUM, endtok );
			goto fail;
		}
	}

	FUNC_END;
	return explist;

fail:
	SFTC_SETERR;
	SFTC_DESTROY( explist );
	FUNC_END;
	return NULL;
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
	if( nexp ) SFTC_DESTROY( nexp );
	if( nif ) SFTC_DESTROY( nif );
	if( nelse ) SFTC_DESTROY( nelse );
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
	if( nexp ) SFTC_DESTROY( nexp );
	if( nwhile ) SFTC_DESTROY( nwhile );
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
	SFTC_NEXT;

	nexp->next = nwhile;
	node = make_node( SFT_DOWHILE, begin, NULL, nexp );

	FUNC_END;
	return node;

fail:
	if( nexp ) SFTC_DESTROY( nexp );
	if( nwhile ) SFTC_DESTROY( nwhile );
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
	if( ninit ) SFTC_DESTROY( ninit );
	if( nexp ) SFTC_DESTROY( nexp );
	if( nincr ) SFTC_DESTROY( nincr );
	if( nwhile ) SFTC_DESTROY( nwhile );
	SFTC_SETERR;
	FUNC_END;
	return NULL;
}

SFTRET parse_foreach( SFTC )
{
	/*
		(x: => null=>ident
		(x,x: => ident=>ident
		(x,: => ident=>null
	*/
	FTNode *node, *nvar = NULL, *nkey = NULL, *nexp = NULL, *nwhile = NULL;
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
	/* (x:e) */
	nkey = make_node( SFT_NULL, SFTC_AT, NULL, NULL );
	nvar = make_node( SFT_IDENT, SFTC_AT, NULL, NULL );
	SFTC_NEXT;

	if( !SFTC_IS( ':' ) && !SFTC_IS( ',' ) )
	{
		SFTC_PRINTERR( "Expected ':' or ',' after identifier in 'foreach'" );
		goto fail;
	}

	if( SFTC_IS( ',' ) )
	{
		SFTC_NEXT;
		if( !SFTC_IS( ST_IDENT ) && !SFTC_IS( ':' ) )
		{
			SFTC_PRINTERR( "Expected identifier or ':' after ',' in 'foreach'" );
			goto fail;
		}

		if( SFTC_IS( ST_IDENT ) )
		{
			/* (x,x:e) */
			nkey->type = SFT_IDENT;
			nvar->token = SFTC_AT;
			SFTC_NEXT;
		}
		else
		{
			/* (x,:e) */
			nkey->type = SFT_IDENT;
			nvar->type = SFT_NULL;
		}

		if( !SFTC_IS( ':' ) )
		{
			SFTC_PRINTERR( "Expected ':' after identifier #2 or ',' in 'foreach'" );
			goto fail;
		}
	}

	SFTC_NEXT;

	nexp = parse_explist( F, ')' );
	if( !nexp ) goto fail;

	SFTC_NEXT;
	nwhile = parse_stmt( F );
	if( !nwhile ) goto fail;

	nkey->next = nvar;
	nvar->next = nexp;
	nexp->next = nwhile;
	node = make_node( SFT_FOREACH, begin, NULL, nkey );

	FUNC_END;
	return node;

fail:
	if( nvar ) SFTC_DESTROY( nvar );
	if( nkey ) SFTC_DESTROY( nkey );
	if( nexp ) SFTC_DESTROY( nexp );
	if( nwhile ) SFTC_DESTROY( nwhile );
	FUNC_END;
	return NULL;
}

SFTRET parse_function( SFTC, int inexp )
{
	FTNode *node, *nname = NULL, *nargs = NULL, *nbody = NULL, *nclos = NULL;
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
		if( inexp )
			SFTC_PRINTERR( "Expected '(' after 'function'" );
		else
			SFTC_PRINTERR( "Expected '(' after 'function' and its name" );
		goto fail;
	}

	SFTC_NEXT;

	nargs = parse_arglist( F, ')' );
	if( !nargs ) goto fail;
	SFTC_NEXT;

	if( SFTC_ISKEY( "use" ) )
	{
		/* closure */
		SFTC_NEXT;
		if( !SFTC_IS( '(' ) )
		{
			SFTC_PRINTERR( "Expected '(' after 'use' in 'function'" );
			goto fail;
		}
		SFTC_NEXT;
		nclos = parse_arglist( F, ')' );
		if( !nclos ) goto fail;
		nclos->type = SFT_USELIST;
		SFTC_NEXT;
	}
	else
		nclos = make_node( SFT_USELIST, SFTC_AT, NULL, NULL );

	if( !SFTC_IS( '{' ) )
	{
		SFTC_PRINTERR( "Expected '{' or 'use'" );
		goto fail;
	}

	nbody = parse_stmt( F );
	if( !nbody ) goto fail;

	nargs->next = nclos;
	nclos->next = nbody;
	nbody->next = nname;
	node = make_node( SFT_FUNC, begin, NULL, nargs );

	FUNC_END;
	return node;

fail:
	if( nname ) SFTC_DESTROY( nname );
	if( nargs ) SFTC_DESTROY( nargs );
	if( nclos ) SFTC_DESTROY( nclos );
	if( nbody ) SFTC_DESTROY( nbody );
	SFTC_SETERR;
	FUNC_END;
	return NULL;
}

SFTRET parse_command( SFTC, int multi )
{
	FTNode *nargs = NULL;
	TokenList begin = SFTC_AT;

	FUNC_BEGIN;
	SFTC_NEXT;
	
	nargs = parse_explist( F, ';' );
	if( !nargs ) goto fail;
	SFTC_NEXT;
	
	if( multi )
	{
		/* one argument to one function call */
		FTNode *r = NULL, *n = NULL, *p = nargs->child;
		
		if( !p )
		{
			if( nargs ) SFTC_DESTROY( nargs );
			FUNC_END;
			return make_node( SFT_BLOCK, begin, NULL, NULL );
		}
		
		nargs->child = NULL;
		SFTC_DESTROY( nargs );
		
		while( p )
		{
			FTNode* nn = make_node( SFT_FCALL, begin, NULL,
				make_node( SFT_IDENT, begin, 
					make_node( SFT_EXPLIST, p->token, NULL, p ),
						NULL ) );
			FTNode* pp = p;
			p = p->next;
			pp->next = NULL;
			if( !r )
				r = n = nn;
			else
				n = n->next = nn;
		}
		
		return make_node( SFT_BLOCK, begin, NULL, r );
	}
	else
	{
		/* one function call */
		FTNode* nname = make_node( SFT_IDENT, begin, nargs, NULL );
		FUNC_END;
		return make_node( SFT_FCALL, begin, NULL, nname );
	}
	
fail:
	if( nargs ) SFTC_DESTROY( nargs );
	SFTC_SETERR;
	FUNC_END;
	return NULL;
}

SFTRET parse_stmt( SFTC )
{
	FTNode* node;

	FUNC_BEGIN;

	if( SFTC_IS(0) )
	{
		SFTC_UNEXP;
		goto fail;
	}

	/* IF / ELSE */
	if( SFTC_ISKEY( "if" ) ) { node = parse_if( F ); FUNC_END; return node; }
	else if( SFTC_ISKEY( "else" ) )
	{
		SFTC_PRINTERR( "Found 'else' without matching 'if'" );
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
			sgs_Int blev;
			AS_INTEGER( blev, SFTC_AT + 1 );
			if( blev < 1 || blev > 255 )
			{
				SFTC_PRINTERR( "Invalid break level (can be between 1 and 255)" );
				goto fail;
			}
			SFTC_NEXT;
		}
		
		if( !SFTC_IS( ';' ) )
		{
			SFTC_UNEXP;
			goto fail;
		}
		SFTC_NEXT;

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
			sgs_Int blev;
			AS_INTEGER( blev, SFTC_AT + 1 );
			if( blev < 1 || blev > 255 )
			{
				SFTC_PRINTERR( "Invalid continue level (can be between 1 and 255)" );
				goto fail;
			}
			SFTC_NEXT;
		}
		
		if( !SFTC_IS( ';' ) )
		{
			SFTC_UNEXP;
			goto fail;
		}
		SFTC_NEXT;

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
		if( !node )
			goto fail;

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
			SFTC_DESTROY( node );
			goto fail;
		}

		SFTC_NEXT;
		FUNC_END;
		return node;
	}
	/* COMMAND HELPERS */
#define NOT_FCALL ( !sgsT_Next( F->at ) || '(' != *sgsT_Next( F->at ) )
	/* SIMPLE COMMANDS */
	else if( SFTC_IS_ID( "print" ) && NOT_FCALL )
	{
		node = parse_command( F, 0 );
		FUNC_END;
		return node;
	}
	/* MULTIPLIED COMMANDS */
	else if( SFTC_IS_ID( "include" ) && NOT_FCALL )
	{
		node = parse_command( F, 1 );
		FUNC_END;
		return node;
	}
	/* BLOCK OF STATEMENTS */
	else if( SFTC_IS( ST_CBRKL ) )
	{
		SFTC_NEXT;
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
			goto fail;
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
			goto fail;
	}

	FUNC_END;
	return stmtlist;

fail:
	SFTC_DESTROY( stmtlist );
	SFTC_SETERR;
	FUNC_END;
	return NULL;
}

FTNode* sgsFT_Compile( SGS_CTX, TokenList tlist )
{
	FTNode* ret;
	FTComp F;
	{
		F.C = C;
		F.at = tlist;
	}
	ret = parse_stmtlist( &F, 0 );
	return ret;
}


