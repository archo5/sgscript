

#include <sgscript.h>
#include <sgs_util.h>
#include <sgs_ctx.h>


/* disables trailing commas */
#define STRICT_JSON


void skipws( const char** p, const char* end )
{
	const char* pos = *p;
	while( pos < end )
	{
		if( *pos != ' ' && *pos != '\t' &&
			*pos != '\n' && *pos != '\r' )
			break;
		pos++;
	}
	*p = pos;
}


#define STK_TOP stack->ptr[ stack->size - 1 ]
#define STK_POP membuf_resize( stack, C, stack->size - 1 )
#define STK_PUSH( what ) membuf_appchr( stack, C, what )

const char* json_parse( SGS_CTX, MemBuf* stack, const char* str, sgs_SizeVal size, int proto )
{
	int stk = sgs_StackSize( C );
	const char* pos = str, *end = str + size;
	for(;;)
	{
		int push = 0;
		skipws( &pos, end );
		if( pos >= end )
			break;

		if( STK_TOP == '{' && *pos != '"' && *pos != '}' )
			return pos;

		if( *pos == '{' )
		{
			if( STK_TOP == 0 && sgs_StackSize( C ) > stk )
				return pos;
			STK_PUSH( '{' );
			if( proto )
			{
				sgs_PushItem( C, 1 );
				sgs_CloneItem( C, -1 );
			}
			else
			{
				sgs_PushCFunction( C, C->dict_func );
				sgs_Call( C, 0, 1 );
			}
		}
		else if( *pos == '}' )
		{
			if( STK_TOP != '{' && STK_TOP != ':' )
				return pos;
			STK_POP;
			push = 1;
		}
		else if( *pos == '[' )
		{
			if( STK_TOP == 0 && sgs_StackSize( C ) > stk )
				return pos;
			STK_PUSH( '[' );
			sgs_PushCFunction( C, C->array_func );
			sgs_Call( C, 0, 1 );
		}
		else if( *pos == ']' )
		{
			if( STK_TOP != '[' )
				return pos;
			STK_POP;
			push = 1;
		}
		else if( *pos == '"' )
		{
			const char* beg = ++pos;
			StrBuf str = strbuf_create();
			while( pos < end && *pos != '"' )
			{
#ifdef STRICT_JSON
				if( *pos < 32 || *pos == 127 )
				{
					strbuf_destroy( &str, C );
					return pos;
				}
#endif
				if( *pos == '\\' )
				{
					pos++;
					switch( *pos )
					{
					case '"':
					case '\\':
					case '/':
						strbuf_appchr( &str, C, *pos );
						break;
					case 'b': strbuf_appchr( &str, C, '\b' ); break;
					case 'f': strbuf_appchr( &str, C, '\f' ); break;
					case 'n': strbuf_appchr( &str, C, '\n' ); break;
					case 'r': strbuf_appchr( &str, C, '\r' ); break;
					case 't': strbuf_appchr( &str, C, '\t' ); break;
					case 'u':
						{
							char buf[ 2 ];
							pos++;
							if( hexchar( *pos ) ) pos++; else return pos;
							if( hexchar( *pos ) ) pos++; else return pos;
							if( hexchar( *pos ) ) pos++; else return pos;
							if( hexchar( *pos ) ) pos++; else return pos;
							pos--;
							buf[ 0 ] = ( gethex( *(pos-3) ) << 4 ) | gethex( *(pos-2) );
							buf[ 1 ] = ( gethex( *(pos-1) ) << 4 ) | gethex( *pos );
							strbuf_appbuf( &str, C, buf, 2 );
						}
						break;
					default:
#ifdef STRICT_JSON
						return pos;
#else
						strbuf_appbuf( &str, C, pos - 1, 2 ); break;
#endif
					}
				}
				else
					strbuf_appchr( &str, C, *pos );
				pos++;
			}
			if( pos >= end )
			{
				strbuf_destroy( &str, C );
				return beg;
			}
			sgs_PushStringBuf( C, str.ptr, str.size );
			strbuf_destroy( &str, C );
			if( STK_TOP == '{' )
			{
				STK_TOP = ':';
				pos++;
				skipws( &pos, end );
				if( *pos != ':' )
					return pos;
			}
			else
			{
				push = 1;
			}
		}
		/* special constants */
		else if( *pos == 'n' )
		{
			pos++;
			if( *pos == 'u' ) pos++; else return pos;
			if( *pos == 'l' ) pos++; else return pos;
			if( *pos == 'l' ) pos++; else return pos;
			sgs_PushNull( C );
			pos--;
			push = 1;
		}
		else if( *pos == 't' )
		{
			pos++;
			if( *pos == 'r' ) pos++; else return pos;
			if( *pos == 'u' ) pos++; else return pos;
			if( *pos == 'e' ) pos++; else return pos;
			sgs_PushBool( C, TRUE );
			pos--;
			push = 1;
		}
		else if( *pos == 'f' )
		{
			pos++;
			if( *pos == 'a' ) pos++; else return pos;
			if( *pos == 'l' ) pos++; else return pos;
			if( *pos == 's' ) pos++; else return pos;
			if( *pos == 'e' ) pos++; else return pos;
			sgs_PushBool( C, FALSE );
			pos--;
			push = 1;
		}
		else
			return pos;

		if( push )
		{
			if( STK_TOP == '[' || STK_TOP == ':' )
			{
				int revchr = STK_TOP == '[' ? ']' : '}';
				pos++;
				skipws( &pos, end );
				if( pos >= end )
					break;
				if( *pos != ',' && *pos != revchr ) return pos;
#ifdef STRICT_JSON
				if( *pos == ',' )
				{
					pos++;
					skipws( &pos, end );
					if( pos >= end )
						break;
					if( *pos == revchr )
						return pos;
					if( *pos == ',' )
						pos--;
				}
#endif
				if( *pos != ',' )
					pos--;
			}
			if( STK_TOP == '[' )
			{
				sgs_Variable obj;
				sgs_GetStackItem( C, -2, &obj );
				sgs_Acquire( C, &obj );
				sgs_PushItem( C, -2 );
				sgs_PushProperty( C, "push" );
				sgs_ThisCall( C, 1, 0 );
				sgs_PushVariable( C, &obj );
				sgs_Release( C, &obj );
			}
			if( STK_TOP == ':' )
			{
				sgs_Variable obj, idx, val;
				sgs_GetStackItem( C, -3, &obj );
				sgs_GetStackItem( C, -2, &idx );
				sgs_GetStackItem( C, -1, &val );
				sgs_SetIndex( C, &obj, &idx, &val );
				sgs_Pop( C, 2 );
				STK_TOP = '{';
			}
		}
		pos++;
	}
/*	printf( "%d, %.*s, %d\n", stack->size, stack->size, stack->ptr, sgs_StackSize(C)-stk ); */
	return sgs_StackSize( C ) > stk && stack->size == 1 ? NULL : str;
}

int json_decode( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	int argc = sgs_StackSize( C );

	if( argc < 1 || argc > 2 ||
		!sgs_ParseString( C, 0, &str, &size ) ||
		( argc == 2 && sgs_ItemType( C, 1 ) != SVT_OBJECT ) )
	{
		sgs_Printf( C, SGS_WARNING, -1, "json_decode: unexpected arguments; "
			"function expects 1-2 arguments: string[, object]" );
		return 0;
	}
	else
	{
		MemBuf stack = membuf_create();
		membuf_appchr( &stack, C, 0 );
		const char* ret = json_parse( C, &stack, str, size, argc == 2 );
		membuf_destroy( &stack, C );
		if( ret )
		{
		/*	printf( "pos %d, %.8s...\n", ret - str, ret ); */
			return 0;
		}
		return 1;
	}
}

int json_encode( SGS_CTX )
{
	return 0;
}


#ifdef WIN32
__declspec(dllexport)
#endif
int sgscript_main( SGS_CTX )
{
	sgs_PushCFunction( C, json_decode );
	sgs_StoreGlobal( C, "json_decode" );
	sgs_PushCFunction( C, json_encode );
	sgs_StoreGlobal( C, "json_encode" );
	sgs_PushBool( C, 1 );
	return 1;
}

