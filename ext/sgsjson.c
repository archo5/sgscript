

#define SGS_INTERNAL

#include <math.h>

#include <sgscript.h>
#include <sgs_int.h>


/* disables trailing commas and other possibly useful things */
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

		if( STK_TOP == 0 && sgs_StackSize( C ) > stk )
			return pos;

		if( *pos == '{' )
		{
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
			MemBuf str = membuf_create();
			while( pos < end && *pos != '"' )
			{
				uint8_t cc = *pos;
#ifdef STRICT_JSON
				if( cc <= 0x1f )
				{
					membuf_destroy( &str, C );
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
						membuf_appchr( &str, C, *pos );
						break;
					case 'b': membuf_appchr( &str, C, '\b' ); break;
					case 'f': membuf_appchr( &str, C, '\f' ); break;
					case 'n': membuf_appchr( &str, C, '\n' ); break;
					case 'r': membuf_appchr( &str, C, '\r' ); break;
					case 't': membuf_appchr( &str, C, '\t' ); break;
					case 'u':
						{
							char hex[ 4 ];
							uint8_t buf[ 2 ];
							uint16_t uchar;
							pos++;
							if( hexchar( *pos ) ) pos++; else return pos;
							if( hexchar( *pos ) ) pos++; else return pos;
							if( hexchar( *pos ) ) pos++; else return pos;
							if( hexchar( *pos ) ) pos++; else return pos;
							pos--;
							hex[ 0 ] = gethex( *(pos-3) );
							hex[ 1 ] = gethex( *(pos-2) );
							hex[ 2 ] = gethex( *(pos-1) );
							hex[ 3 ] = gethex( *pos );
							if( hex[0] == 0xff || hex[1] == 0xff ||
								hex[2] == 0xff || hex[3] == 0xff )
								return pos;
							buf[ 0 ] = ( hex[0] << 4 ) | hex[1];
							buf[ 1 ] = ( hex[2] << 4 ) | hex[3];
							uchar = ( buf[0]<<8 ) | buf[1];
							if( uchar <= 0x7f )
							{
								membuf_appchr( &str, C, buf[1] );
								break;
							}
							if( uchar <= 0x7ff )
							{
								char obuf[ 2 ] =
								{
									0xC0 | ((buf[1] & 0xC0) >> 6) | ((buf[0] & 0x7) << 2),
									0x80 | (buf[1] & 0x3F)
								};
								membuf_appbuf( &str, C, obuf, 2 );
								break;
							}

							{
								char obuf[ 3 ] =
								{
									0xE0 | ((buf[0] & 0xF0) >> 4),
									0x80 | ((buf[0] & 0xF) << 2) | ((buf[1] & 0xC0) >> 6),
									0x80 | (buf[1] & 0x3F)
								};
								membuf_appbuf( &str, C, obuf, 3 );
							}
						}
						break;
					default:
#ifdef STRICT_JSON
						return pos;
#else
						membuf_appbuf( &str, C, pos - 1, 2 ); break;
#endif
					}
				}
				else
					membuf_appchr( &str, C, *pos );
				pos++;
			}
			if( pos >= end )
			{
				membuf_destroy( &str, C );
				return beg;
			}
			sgs_PushStringBuf( C, str.ptr, str.size );
			membuf_destroy( &str, C );
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
		else if( decchar( *pos ) || *pos == '-' )
		{
			sgs_Real val = 0;
			int neg = *pos == '-', num;
			if( neg )
			{
				pos++;
				if( pos >= end ) goto endnumparse;
				if( !decchar( *pos ) )
					return pos;
			}
			num = getdec( *pos++ );
			val = num;
			if( pos >= end ) goto endnumparse;
			if( num )
			{
				while( pos < end && decchar( *pos ) )
				{
					val *= 10;
					val += getdec( *pos++ );
				}
			}
			if( *pos == '.' )
			{
				sgs_Real dp = 1;
				pos++;
				while( pos < end && decchar( *pos ) )
				{
					dp /= 10;
					val += dp * getdec( *pos++ );
				}
			}
			if( *pos == 'E' || *pos == 'e' )
			{
				sgs_Real expnt = 0, sign = 1;
				pos++;
				if( pos >= end ) goto endnumparse;
				if( *pos == '-' )
				{
					sign = -1;
					pos++;
				}
				else if( *pos == '+' )
					pos++;
				else if( !decchar( *pos ) )
					return pos;
				if( pos >= end ) goto endnumparse;
				while( pos < end && decchar( *pos ) )
				{
					expnt *= 10;
					expnt += getdec( *pos++ );
				}
				val *= powf( 10, expnt * sign );
			}
endnumparse:
			if( neg )
				val = -val;
			if( val == (sgs_Integer) val )
				sgs_PushInt( C, (sgs_Integer) val );
			else
				sgs_PushReal( C, val );
			push = 1;
			pos--;
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


int encode_var( SGS_CTX, MemBuf* buf )
{
	sgs_Variable var;
	sgs_GetStackItem( C, -1, &var );
	switch( var.type )
	{
	case SVT_NULL:
		membuf_appbuf( buf, C, "null", 4 );
		return 1;
	case SVT_BOOL:
		membuf_appbuf( buf, C, var.data.B ? "true" : "false", 5 - !!var.data.B );
		return 1;
	case SVT_INT:
		{
			char tmp[ 64 ];
			sprintf( tmp, "%" PRId64, var.data.I );
			membuf_appbuf( buf, C, tmp, strlen( tmp ) );
			return 1;
		}
	case SVT_REAL:
		{
			char tmp[ 64 ];
			sprintf( tmp, "%g", var.data.R );
			membuf_appbuf( buf, C, tmp, strlen( tmp ) );
			return 1;
		}
	case SVT_STRING:
		{
			membuf_appchr( buf, C, '"' );
			{
				char* str = var_cstr( &var );
				char* frm = str, *end = str + sgs_GetStringSize( C, -1 );
				while( str < end )
				{
					if( *str == '"' || *str == '\\' )
					{
						char pp[] = { '\\', *str };
						if( str != frm )
							membuf_appbuf( buf, C, frm, str - frm );
						membuf_appbuf( buf, C, pp, 2 );
						frm = str + 1;
					}
					str++;
				}
				if( str != frm )
					membuf_appbuf( buf, C, frm, str - frm );
			}
			membuf_appchr( buf, C, '"' );
			return 1;
		}
	case SVT_FUNC:
	case SVT_CFUNC:
		sgs_Printf( C, SGS_WARNING, -1, "json_encode: cannot encode functions" );
		return 0;
	case SVT_OBJECT:
		{
			/* stack: Obj */
			sgs_Variable idx;
			int isarr = sgs_IsArray( C, &var ), first = 1;
			membuf_appchr( buf, C, isarr ? '[' : '{' );
			if( sgs_PushIterator( C, -1 ) != SGS_SUCCESS )
				return 0;
			/* stack: Obj, Iter */
			while( sgs_PushNextKey( C, -1 ) > 0 )
			{
				/* stack: Obj, Iter, Key */
				if( first ) first = 0;
				else membuf_appchr( buf, C, ',' );

				if( !isarr )
				{
					int nsk = sgs_ItemType( C, -1 ) != SVT_STRING;
					if( nsk )
					{
						sgs_PushItem( C, -1 );
						sgs_ToString( C, -1 );
					}
					if( !encode_var( C, buf ) )
						return 0;
					if( nsk )
						sgs_Pop( C, 1 );
					membuf_appchr( buf, C, ':' );
				}

				if( !sgs_GetStackItem( C, -1, &idx )
					|| sgs_PushIndex( C, &var, &idx ) != SGS_SUCCESS )
					return 0;
				/* stack: Obj, Iter, Key, Value */
				sgs_PopSkip( C, 1, 1 );
				/* stack: Obj, Iter, Value */
				if( !encode_var( C, buf ) )
					return 0;
				/* stack: -- (?) */
				sgs_Pop( C, 1 );
				/* stack: Obj, Iter */
			}
			sgs_Pop( C, 1 );
			/* stack: Obj */
			membuf_appchr( buf, C, isarr ? ']' : '}' );
			return 1;
		}
	}
	return 0;
}

int json_encode( SGS_CTX )
{
	MemBuf buf = membuf_create();
	int argc = sgs_StackSize( C ), ret;

	if( argc != 1 )
	{
		sgs_Printf( C, SGS_WARNING, -1, "json_encode: function expects 1 argument" );
		return 0;
	}

	ret = encode_var( C, &buf );
	if( ret )
		sgs_PushStringBuf( C, buf.ptr, buf.size );
	membuf_destroy( &buf, C );
	return ret;
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

