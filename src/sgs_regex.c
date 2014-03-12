
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define MAX(a,b) ((a)>(b)?(a):(b))


#define RX_NEED_DEFAULT_MEMFUNC
#include "sgs_regex.h"


#define RX_MAX_CAPTURES 10


#define RX_MALLOC( bytes ) R->memfn( R->memctx, NULL, bytes )
#define RX_ALLOC_N( what, N ) (what*) R->memfn( R->memctx, NULL, sizeof( what ) * ((size_t)(N)) )
#define RX_ALLOC( what ) RX_ALLOC_N( what, 1 )
#define RX_FREE( ptr ) R->memfn( R->memctx, ptr, 0 )

#define RX_IS_ALPHA( x ) rx_isalpha( x )
#define RX_EQUALIZE( x ) rx_tolower( x )


#define RIT_MATCH  1 /* matching */
#define RIT_RANGE  2
#define RIT_SPCBEG 3
#define RIT_SPCEND 4
#define RIT_BKREF  5
#define RIT_EITHER 11 /* control */
#define RIT_SUBEXP 12

#define RIF_LAZY   0x01
#define RIF_INVERT 0x02

#define RCF_MULTILINE 0x01 /* ^/$ matches beginning/end of line too */
#define RCF_CASELESS  0x02 /* pre-equalized case for match/range */
#define RCF_DOTALL    0x04 /* "." is compiled as "[^]" instead of "[^\r\n]" */

#ifndef RXLOG
#define RXLOG 0
#endif

#if RXLOG
#define RXLOGINFO( x ) x
#else
#define RXLOGINFO( x )
#endif


static int rx_isalpha( RX_Char c )
{
	return ( c >= 'a' && c <= 'z' )
	    || ( c >= 'A' && c <= 'Z' );
}

static RX_Char rx_tolower( RX_Char c )
{
	if( c >= 'A' && c <= 'Z' )
		return (RX_Char)( c - 'A' + 'a' );
	return c;
}


typedef struct _regex_item regex_item;
struct _regex_item
{
	/* structure */
	regex_item* prev;
	regex_item* next;
	regex_item* ch, *ch2;
	regex_item* pos;
	
	RX_Char* range;
	int count;
	
	int type, flags;
	RX_Char a;
	int min, max;
	
	/* match state */
	const RX_Char *matchbeg, *matchend;
	int counter;
};

struct _srx_Context
{
	/* structure */
	regex_item*    root;
	int            flags;
	
	/* memory */
	srx_MemFunc    memfn;
	void*          memctx;
	
	/* captures */
	regex_item*    caps[ RX_MAX_CAPTURES ];
	int            numcaps;
	
	/* temporary data */
	const RX_Char* string;
};

typedef struct _match_ctx
{
	const RX_Char* string;
	regex_item*    item;
	srx_Context*   R;
}
match_ctx;


static int regex_test( const RX_Char* str, match_ctx* ctx );


static int regex_match_once( match_ctx* ctx )
{
	int i;
	regex_item* item = ctx->item;
	const RX_Char* str = item->matchend;
	RXLOGINFO( printf( "type %d char %d action at %p (%.5s)\n", item->type, (int) item->a, str, str ) );
	switch( item->type )
	{
	case RIT_MATCH:
		{
			RX_Char ch = *str;
			if( ctx->R->flags & RCF_CASELESS )
				ch = RX_EQUALIZE( *str );
			if( ch == item->a )
			{
				item->matchend++;
				return 1;
			}
		}
		break;
	case RIT_RANGE:
		{
			RX_Char ch = *str;
			int inv = ( item->flags & RIF_INVERT ) != 0, inrange = 0;
			if( ctx->R->flags & RCF_CASELESS )
				ch = RX_EQUALIZE( *str );
			for( i = 0; i < item->count*2; i += 2 )
			{
				if( ch >= item->range[i] && ch <= item->range[i+1] )
				{
					inrange = 1;
					break;
				}
			}
			if( inrange ^ inv )
			{
				item->matchend++;
				return 1;
			}
		}
		break;
	case RIT_SPCBEG:
		if( ctx->R->flags & RCF_MULTILINE && ( *item->matchend == '\n' || *item->matchend == '\r' ) )
		{
			if( *item->matchend == '\r' && item->matchend[1] == '\n' )
				item->matchend++;
			item->matchend++;
			item->matchbeg = item->matchend;
			return 1;
		}
		return ctx->string == item->matchend;
	case RIT_SPCEND:
		if( ctx->R->flags & RCF_MULTILINE && ( *item->matchend == '\n' || *item->matchend == '\r' ) )
		{
			return 1;
		}
		return !*str;
	case RIT_BKREF:
		{
			regex_item* cap = ctx->R->caps[ (int) item->a ];
			ptrdiff_t len = cap->matchend - cap->matchbeg;
			ptrdiff_t len2 = (ptrdiff_t) strlen( str );
			if( len2 >= len && strncmp( cap->matchbeg, str, (size_t) len ) == 0 )
			{
				item->matchend += len;
				return 1;
			}
		}
		break;
	case RIT_SUBEXP:
		{
			match_ctx cc;
			{
				cc.string = ctx->string;
				cc.item = item->pos;
				cc.R = ctx->R;
			}
			if( regex_test( str, &cc ) )
			{
				regex_item* p = item->ch;
				while( p->next )
					p = p->next;
				item->pos = p;
				item->matchend = p->matchend;
				return 1;
			}
		}
		break;
	}
	return 0;
}

static int regex_match_many( match_ctx* ctx )
{
	regex_item* item = ctx->item;
	item->matchend = item->matchbeg;
	if( item->type == RIT_EITHER )
	{
		regex_item* chi = item->counter ? item->ch2 : item->ch;
		match_ctx cc;
		{
			cc.string = ctx->string;
			cc.item = chi;
			cc.R = ctx->R;
		}
		if( regex_test( item->matchbeg, &cc ) )
		{
			regex_item* p = chi;
			while( p->next )
				p = p->next;
			item->matchend = p->matchend;
			return 1;
		}
		return 0;
	}
	else
	{
		int i;
		for( i = 0; i < item->counter; ++i )
		{
			if( !*item->matchend && item->type != RIT_SPCEND && item->type != RIT_EITHER && item->type != RIT_SUBEXP )
			{
				item->counter = item->flags & RIF_LAZY ? item->max : i;
				RXLOGINFO( printf( "stopped while matching, counter = %d, %d between %d and %d?\n", item->counter, i, item->min, item->max ) );
				return i >= item->min && i <= item->max;
			}
			if( !regex_match_once( ctx ) )
			{
				item->counter = item->flags & RIF_LAZY ? item->max : i;
				RXLOGINFO( printf( "did not match, counter reset to %d\n", item->counter ) );
				return i >= item->min && i <= item->max;
			}
			RXLOGINFO( else printf( "matched\n" ) );
		}
		return 1;
	}
}

static void regex_full_reset( regex_item* p );
static void regex_reset_one( regex_item* p )
{
	if( p->ch ) regex_full_reset( p->ch );
	if( p->ch2 ) regex_full_reset( p->ch2 );
	p->pos = p->ch;
	p->counter = p->flags & RIF_LAZY ? p->min : p->max;
}
static void regex_full_reset( regex_item* p )
{
	while( p )
	{
		regex_reset_one( p );
		p = p->next;
	}
}

static int regex_subexp_backtrack( regex_item* item )
{
	int chgh = 0;
	regex_item* p = item->pos;
	
	while( p )
	{
		RXLOGINFO( printf( "backtracker at type %d char %d\n", p->type, (int) p->a ) );
		if( chgh && p->type == RIT_SUBEXP && regex_subexp_backtrack( p ) )
			break;
		else if( p->flags & RIF_LAZY )
		{
			p->counter++;
			if( p->counter <= p->max )
				break;
		}
		else
		{
			p->counter--;
			if( p->counter >= p->min )
				break;
		}
		RXLOGINFO( printf( "subexp backtrack - reset current, move back\n" ) );
		regex_reset_one( p );
		p = p->prev;
		chgh = 1;
	}
	
	RXLOGINFO( printf( "subexp backtrack - %s\n", p ? "success" : "failure" ) );
	RXLOGINFO( if( p ) printf( "subexp-backtracked to type %d ctr=%d min=%d max=%d\n", p->type, p->counter, p->min, p->max ) );
	
	return !!p;
}

static int regex_test( const RX_Char* str, match_ctx* ctx )
{
	regex_item* p = ctx->item;
	p->matchbeg = str;
	
	for(;;)
	{
		int res;
		match_ctx cc;
		{
			cc.string = ctx->string;
			cc.item = p;
			cc.R = ctx->R;
		}
		RXLOGINFO( printf( "match_many: item %p type %d at position %p (%.5s)\n", (void*) p, p->type, p->matchbeg, p->matchbeg ) );
		res = regex_match_many( &cc );
		if( res < 0 )
		{
			RXLOGINFO( printf( "test of subexp %p FAILED\n", (void*) ctx->item ) );
			return -1;
		}
		else if( res > 0 )
		{
			p = p->next;
			if( !p )
			{
				RXLOGINFO( printf( "test of subexp %p SUCCEEDED\n", (void*) ctx->item ) );
				return 1;
			}
			RXLOGINFO( printf( "moving on to type %d action\n", p->type ) );
			p->matchbeg = p->prev->matchend;
		}
		else
		{
			int chgh = 0;
			while( p )
			{
				if( chgh && p->type == RIT_SUBEXP && regex_subexp_backtrack( p ) )
					break;
				else if( p->flags & RIF_LAZY )
				{
					p->counter++;
					if( p->counter <= p->max )
						break;
				}
				else
				{
					p->counter--;
					if( p->counter >= p->min )
						break;
				}
				RXLOGINFO( printf( "backtrack, reset current\n" ) );
				regex_reset_one( p );
				p = p->prev;
				chgh = 1;
			}
			if( !p )
			{
				RXLOGINFO( printf( "test of subexp %p BT-ENDED\n", (void*) ctx->item ) );
				return 0;
			}
		}
	}
}

static int regex_test_start( const RX_Char* str, match_ctx* ctx )
{
	regex_item* p = ctx->item;
	RXLOGINFO( printf( "test start - counter reset\n" ) );
	regex_reset_one( p );
	return regex_test( str, ctx );
}


/*
	mapping:
	- [^a-zA-Z] ... RIT_RANGE, optional RIF_INVERT
	- "." ... empty RIT_RANGE + RIF_INVERT
	- "\s" and others ... predefined RIT_RANGE with optional RIF_INVERT
	- "|" ... RIT_EITHER
	- "(..)" ... RIT_SUBEXP
	- "?" ... range = [0,1]
	- "*" ... range = [0,INT_MAX]
	- "+" ... range = [1,INT_MAX]
	- "{1,5}" ... range = [1,5] (other ranges mapped similarly)
	- "^" ... RIT_SPCBEG
	- "$" ... RIT_SPCEND
	- "\1" ... RIT_BKREF
*/

static void regex_free_item( srx_Context* R, regex_item* item );
static void regex_dealloc_item( srx_Context* R, regex_item* item )
{
	if( item->range )
		RX_FREE( item->range );
	if( item->ch ) regex_free_item( R, item->ch );
	if( item->ch2 ) regex_free_item( R, item->ch2 );
	RX_FREE( item );
}

static void regex_free_item( srx_Context* R, regex_item* item )
{
	regex_item *p, *c;
	if( !item )
		return;
	p = item->prev;
	while( p )
	{
		c = p;
		p = p->prev;
		regex_dealloc_item( R, c );
	}
	p = item->next;
	while( p )
	{
		c = p;
		p = p->next;
		regex_dealloc_item( R, c );
	}
	regex_dealloc_item( R, item );
}

static void regex_level( regex_item** pitem )
{
	/* TODO: balanced/non-(pseudo-)binary leveling */
	regex_item* item = *pitem;
	while( item )
	{
		if( item->type == RIT_EITHER )
		{
			regex_item* next = item->next;
			regex_level( &next );
			
			if( item->prev )
			{
				item->prev->next = NULL;
				item->prev = NULL;
			}
			if( item->next )
			{
				item->next->prev = NULL;
				item->next = NULL;
			}
			
			item->ch = *pitem;
			item->ch2 = next;
			
			*pitem = item;
			return;
		}
		item = item->next;
	}
}

static int regex_real_compile( srx_Context* R, int* cel, const RX_Char** pstr, int sub, regex_item** out )
{
#define _RX_ALLOC_NODE( ty ) \
	item = RX_ALLOC( regex_item ); \
	memset( item, 0, sizeof(*item) ); \
	if( citem ) \
	{ \
		citem->next = item; \
		item->prev = citem; \
	} \
	item->type = ty; \
	item->min = 1; \
	item->max = 1;

#define _RXE( err ) for(;;){ error = err; goto fail; }
	
	const RX_Char* s = *pstr;
	regex_item* item = NULL, *citem = NULL;
	int error = 0;
	while( *s )
	{
		if( sub && *s == ')' )
			break;
		switch( *s )
		{
		case '[':
			{
				const RX_Char* sc;
				int inv = 0, cnt = 0;
				RX_Char* ri;
				s++;
				if( *s == '^' )
				{
					inv = 1;
					s++;
				}
				sc = s;
				if( *sc == ']' )
				{
					sc++;
					cnt++;
				}
				while( *sc && *sc != ']' )
				{
					if( *sc == '-' && sc > s && sc[1] != 0 && sc[1] != ']' )
						sc++;
					else
						cnt++;
					sc++;
				}
				if( !*sc )
					_RXE( RXEPART );
				_RX_ALLOC_NODE( RIT_RANGE );
				if( inv )
					item->flags |= RIF_INVERT;
				item->range = ri = RX_ALLOC_N( RX_Char, cnt * 2 );
				item->count = cnt;
				sc = s;
				if( *sc == ']' )
				{
					sc++;
					ri[0] = ri[1] = *sc;
					ri += 2;
				}
				while( *sc && *sc != ']' )
				{
					if( *sc == '-' && sc > s && sc[1] != 0 && sc[1] != ']' )
					{
						if( ri > item->range )
							*(ri-1) = sc[1];
						sc++;
					}
					else
					{
						ri[0] = ri[1] = *sc;
						ri += 2;
					}
					sc++;
				}
				s = sc;
				if( *s == ']' )
					s++;
				if( R->flags & RCF_CASELESS )
				{
					int i;
					ri = item->range;
					for( i = 0; i < cnt * 2; i += 2 )
					{
						RX_Char A = ri[ i ], B = ri[ i + 1 ];
						if( RX_IS_ALPHA( A ) && RX_IS_ALPHA( B ) )
						{
							ri[ i ] = RX_EQUALIZE( A );
							ri[ i + 1 ] = RX_EQUALIZE( B );
						}
					}
				}
			}
			break;
		case ']':
			_RXE( RXEUNEXP );
		case '(':
			{
				int r, cap = R->numcaps < RX_MAX_CAPTURES ? 1 : -1;
				_RX_ALLOC_NODE( RIT_SUBEXP );
				if( cap >= 0 )
				{
					cap = R->numcaps++;
					R->caps[ cap ] = item;
				}
				s++;
				r = regex_real_compile( R, cel, &s, 1, &item->ch );
				if( r )
					_RXE( r );
				item->pos = item->ch;
				if( *s != ')' )
					_RXE( RXEUNEXP );
				if( cap >= 0 )
					cel[ cap ] = 1;
				s++;
			}
			break;
		case ')':
			_RXE( RXEUNEXP );
		case '{':
		case '?':
		case '*':
		case '+':
			if( s > *pstr && ( *(s-1) == '}' || *(s-1) == '?' || *(s-1) == '*' || *(s-1) == '+' ) )
			{
				if( *s == '?' )
					item->flags |= RIF_LAZY;
				else
					_RXE( RXEUNEXP );
			}
			else if( item && ( item->type == RIT_MATCH || item->type == RIT_RANGE || item->type == RIT_BKREF || item->type == RIT_SUBEXP ) )
			{
				int min = 1, max = 1;
				if( *s == '{' )
				{
					int ctr;
					s++;
					if( !isdigit( *s ) )
						_RXE( RXEUNEXP );
					min = 0;
					ctr = 8;
					while( isdigit( *s ) && ctr > 0 )
					{
						min = min * 10 + *s++ - '0';
						ctr--;
					}
					if( isdigit( *s ) && ctr == 0 )
						_RXE( RXELIMIT );
					if( *s == ',' )
					{
						if( !isdigit(s[1]) )
							_RXE( RXEUNEXP );
						s++;
						max = 0;
						ctr = 8;
						while( isdigit( *s ) && ctr > 0 )
						{
							max = max * 10 + *s++ - '0';
							ctr--;
						}
						if( isdigit( *s ) && ctr == 0 )
							_RXE( RXELIMIT );
						if( min > max )
							_RXE( RXERANGE );
					}
					else
						max = min;
					if( *s != '}' )
						_RXE( RXEUNEXP );
				}
				else if( *s == '?' ){ min = 0; max = 1; }
				else if( *s == '*' ){ min = 0; max = INT_MAX - 1; }
				else if( *s == '+' ){ min = 1; max = INT_MAX - 1; }
				item->min = min;
				item->max = max;
			}
			else
				_RXE( RXEUNEXP );
			s++;
			break;
		case '}':
			_RXE( RXEUNEXP );
		case '|':
			if( !citem )
				_RXE( RXEUNEXP );
			_RX_ALLOC_NODE( RIT_EITHER );
			item->min = 0;
			item->max = 1;
			item->flags |= RIF_LAZY;
			s++;
			break;
		case '^':
			_RX_ALLOC_NODE( RIT_SPCBEG );
			s++;
			break;
		case '$':
			_RX_ALLOC_NODE( RIT_SPCEND );
			s++;
			break;
		case '\\':
			if( s[1] )
			{
				s++;
				if( *s == '.' )
				{
					_RX_ALLOC_NODE( RIT_MATCH );
					item->a = *s++;
					break;
				}
				else if( isdigit( *s ) )
				{
					int dig = *s++ - '0';
					if( dig == 0 || dig >= RX_MAX_CAPTURES || !cel[ dig ] )
						_RXE( RXENOREF );
					_RX_ALLOC_NODE( RIT_BKREF );
					item->a = (RX_Char) dig;
					break;
				}
				/* TODO: character classes */
			}
			else
				_RXE( RXEPART );
		default:
			if( *s == '.' )
			{
				_RX_ALLOC_NODE( RIT_RANGE );
				if( !( R->flags & RCF_DOTALL ) )
				{
					item->range = RX_ALLOC_N( RX_Char, 2 * 2 );
					item->range[0] = item->range[1] = '\n';
					item->range[2] = item->range[3] = '\r';
					item->count = 2;
				}
				item->flags |= RIF_INVERT;
			}
			else
			{
				_RX_ALLOC_NODE( RIT_MATCH );
				item->a = *s;
				if( R->flags & RCF_CASELESS && RX_IS_ALPHA( item->a ) )
					item->a = RX_EQUALIZE( item->a );
			}
			s++;
			break;
		}
		citem = item;
	}
	if( !item )
		_RXE( RXEEMPTY );
	if( item->type == RIT_EITHER )
		_RXE( RXEPART );
	*pstr = s;
	while( item->prev )
		item = item->prev;
	regex_level( &item );
	*out = item;
	return RXSUCCESS;
fail:
	regex_free_item( R, item );
	return (int)( ( error & 0xf ) | ( ( s - R->string ) << 4 ) );
}

/*
	#### srx_CreateExt ####
*/
srx_Context* srx_CreateExt( const RX_Char* str, const RX_Char* mods, int* errnpos, srx_MemFunc memfn, void* memctx )
{
	int flags = 0, err, cel[ RX_MAX_CAPTURES ];
	srx_Context* R = NULL;
	if( mods )
	{
		while( *mods )
		{
			switch( *mods )
			{
			case 'm': flags |= RCF_MULTILINE; break;
			case 'i': flags |= RCF_CASELESS; break;
			case 's': flags |= RCF_DOTALL; break;
			default:
				err = RXEINMOD;
				goto fail;
			}
			mods++;
		}
	}
	
	if( !memfn )
		memfn = srx_DefaultMemFunc;
	
	R = (srx_Context*) memfn( memctx, NULL, sizeof(srx_Context) );
	memset( R, 0, sizeof(*R) );
	memset( cel, 0, sizeof(cel) );
	R->memfn = memfn;
	R->memctx = memctx;
	R->string = str;
	R->flags = flags;
	R->numcaps = 1;
	
	err = regex_real_compile( R, cel, &str, 0, &R->root );
	
	if( err )
	{
		memfn( memctx, R, 0 );
		R = NULL;
	}
	else
	{
		regex_item* item = RX_ALLOC( regex_item );
		memset( item, 0, sizeof(*item) );
		item->type = RIT_SUBEXP;
		item->min = 1;
		item->max = 1;
		item->pos = item->ch = R->root;
		R->caps[ 0 ] = R->root = item;
	}
fail:
	if( errnpos )
	{
		unsigned uerr = (unsigned) err;
		errnpos[0] = (int)( uerr ? ( uerr & 0xf ) | 0xfffffff0 : 0 );
		errnpos[1] = (int)( ( uerr & 0xfffffff0 ) >> 4 );
	}
	RXLOGINFO( if( R ) srx_DumpToStdout(R) );
	return R;
}

/*
	#### srx_Destroy ####
*/
int srx_Destroy( srx_Context* R )
{
	if( R )
	{
		srx_MemFunc memfn = R->memfn;
		void* memctx = R->memctx;
		if( R->root )
			regex_free_item( R, R->root );
		memfn( memctx, R, 0 );
	}
	return !!R;
}


static void regex_dump_list( regex_item* items, int lev );
static void regex_dump_item( regex_item* item, int lev )
{
	const char* types[] =
	{
		"-", "MATCH", "RANGE", "SPCBEG", "SPCEND", "BKREF", "-", "-", "-", "-",
		"-", "EITHER", "SUBEXP", "-"
	};
	
	int l = lev;
	while( l --> 0 )
		printf( "- " );
	printf( "%s", types[ item->type ] );
	if( item->flags & RIF_INVERT ) printf( " INV" );
	if( item->flags & RIF_LAZY ) printf( " LAZY" );
	switch( item->type )
	{
	case RIT_MATCH: printf( " char %d", (int) item->a ); break;
	case RIT_RANGE:
		for( l = 0; l < item->count; ++l )
		{
			if( l > 0 )
				printf( "," );
			printf( " %d - %d", (int) item->range[l*2], (int) item->range[l*2+1] );
		}
		break;
	case RIT_BKREF: printf( " #%d", (int) item->a ); break;
	}
	printf( " (%d to %d) (0x%p => 0x%p)\n", item->min, item->max, item->matchbeg, item->matchend );
	
	if( item->ch )
	{
		regex_dump_list( item->ch, lev + 1 );
		if( item->ch2 )
		{
			int l2 = lev;
			while( l2 --> 0 )
				printf( "- " );
			printf( "--|\n" );
			regex_dump_list( item->ch2, lev + 1 );
		}
	}
}
static void regex_dump_list( regex_item* items, int lev )
{
	while( items )
	{
		regex_dump_item( items, lev );
		items = items->next;
	}
}

/*
	#### srx_DumpToStdout ####
*/
void srx_DumpToStdout( srx_Context* R )
{
	regex_dump_list( R->root, 0 );
}

/*
	#### srx_Match ####
*/
int srx_Match( srx_Context* R, const RX_Char* str, int offset )
{
	int ret;
	match_ctx ctx;
	{
		ctx.string = str;
		ctx.item = R->root;
		ctx.R = R;
	}
	R->string = str;
	str += offset;
	while( *str )
	{
		ret = regex_test_start( str, &ctx );
		if( ret < 0 )
			return 0;
		if( ret > 0 )
			return 1;
		str++;
	}
	return 0;
}

/*
	#### srx_GetCaptureCount ####
*/
int srx_GetCaptureCount( srx_Context* R )
{
	return R->numcaps;
}

/*
	#### srx_GetCaptured ####
*/
int srx_GetCaptured( srx_Context* R, int which, int* pbeg, int* pend )
{
	const RX_Char* a, *b;
	if( srx_GetCapturedPtrs( R, which, &a, &b ) )
	{
		if( pbeg ) *pbeg = (int)( a - R->string );
		if( pend ) *pend = (int)( b - R->string );
		return 1;
	}
	return 0;
}

/*
	#### srx_GetCapturedPtrs ####
*/
int srx_GetCapturedPtrs( srx_Context* R, int which, const RX_Char** pbeg, const RX_Char** pend )
{
	if( which < 0 || which >= R->numcaps )
		return 0;
	if( R->caps[ which ] == NULL )
		return 0;
	if( pbeg ) *pbeg = R->caps[ which ]->matchbeg;
	if( pend ) *pend = R->caps[ which ]->matchend;
	return 1;
}

/*
	#### srx_Replace ####
*/
RX_Char* srx_Replace( srx_Context* R, const RX_Char* str, const RX_Char* rep )
{
	RX_Char* out = "";
	const RX_Char *from = str, *fromend = str + strlen( str );
	size_t size = 0, mem = 0;
	
#define SR_CHKSZ( szext ) \
	if( (ptrdiff_t)( mem - size ) < (ptrdiff_t)(szext) ) \
	{ \
		size_t nsz = MAX( mem * 2, size + (size_t)(szext) ) + 1; \
		RX_Char* nmem = RX_ALLOC_N( RX_Char, nsz ); \
		if( mem ) \
		{ \
			memcpy( nmem, out, size + 1 ); \
			RX_FREE( out ); \
		} \
		out = nmem; \
		mem = nsz; \
	}
#define SR_ADDBUF( from, to ) \
	SR_CHKSZ( to - from ) \
	memcpy( out + size, from, (size_t)( to - from ) ); \
	size += (size_t)( to - from );
	
	while( *from )
	{
		const RX_Char* ofp = NULL, *ep = NULL, *rp;
		if( !srx_Match( R, from, 0 ) )
			break;
		srx_GetCapturedPtrs( R, 0, &ofp, &ep );
		SR_ADDBUF( from, ofp );
		
		rp = rep;
		while( *rp )
		{
			RX_Char rc = *rp;
			if( ( rc == '\\' || rc == '$' ) && rp[1] )
			{
				if( isdigit( rp[1] ) )
				{
					int dig = rp[1] - '0';
					const RX_Char *brp, *erp;
					if( srx_GetCapturedPtrs( R, dig, &brp, &erp ) )
					{
						SR_ADDBUF( brp, erp );
					}
					rp += 2;
					continue;
				}
				else if( rp[1] == rc )
				{
					rp++;
					continue;
				}
			}
			SR_ADDBUF( rp, rp + 1 );
			rp++;
		}
		
		if( from == ep )
			from++;
		else
			from = ep;
	}
	
	SR_ADDBUF( from, fromend );
	{
		char nul[1] = {0};
		SR_ADDBUF( nul, &nul[1] );
	}
	return out;
}

/*
	#### srx_FreeReplaced ####
*/
void srx_FreeReplaced( srx_Context* R, RX_Char* repstr )
{
	RX_FREE( repstr );
}

