

#include <sgs_int.h>


static sgs_RegIntConst int_consts[] =
{
#define _META_RGS( instr ) { "SI_" #instr, SGS_SI_##instr },
	_META_RGS( NOP )
	_META_RGS( PUSH )
	_META_RGS( INT )
	_META_RGS( RET1 )
	_META_RGS( RETN )
	_META_RGS( JUMP )
	_META_RGS( JMPT )
	_META_RGS( JMPF )
	_META_RGS( JMPN )
	_META_RGS( CALL )
	_META_RGS( FORPREP )
	_META_RGS( FORLOAD )
	_META_RGS( FORJUMP )
	_META_RGS( LOADCONST )
	_META_RGS( GETVAR )
	_META_RGS( SETVAR )
	_META_RGS( GETPROP )
	_META_RGS( SETPROP )
	_META_RGS( GETINDEX )
	_META_RGS( SETINDEX )
	_META_RGS( ARRPUSH )
	_META_RGS( GENCLSR )
	_META_RGS( PUSHCLSR )
	_META_RGS( MAKECLSR )
	_META_RGS( GETCLSR )
	_META_RGS( SETCLSR )
	_META_RGS( SET )
	_META_RGS( MCONCAT )
	_META_RGS( CONCAT )
	_META_RGS( NEGATE )
	_META_RGS( BOOL_INV )
	_META_RGS( INVERT )
	_META_RGS( INC )
	_META_RGS( DEC )
	_META_RGS( ADD )
	_META_RGS( SUB )
	_META_RGS( MUL )
	_META_RGS( DIV )
	_META_RGS( MOD )
	_META_RGS( AND )
	_META_RGS( OR )
	_META_RGS( XOR )
	_META_RGS( LSH )
	_META_RGS( RSH )
	_META_RGS( SEQ )
	_META_RGS( SNEQ )
	_META_RGS( EQ )
	_META_RGS( NEQ )
	_META_RGS( LT )
	_META_RGS( GTE )
	_META_RGS( GT )
	_META_RGS( LTE )
	_META_RGS( RAWCMP )
	_META_RGS( ARRAY )
	_META_RGS( DICT )
	_META_RGS( MAP )
	_META_RGS( CLASS )
	_META_RGS( NEW )
	_META_RGS( RSYM )
	_META_RGS( COTRT )
	_META_RGS( COTRF )
	_META_RGS( COABORT )
	_META_RGS( YLDJMP )
#undef _META_RGS
	{ NULL, 0 }
};

int sgs_meta_globals( SGS_CTX )
{
	sgs_RegIntConsts( C, int_consts, -1 );
	return 0;
}


static int _sgs_meta_dumpfn( SGS_CTX, sgs_iFunc* func );

static int _sgs_meta_dumpconstlist( SGS_CTX, sgs_Variable* var, size_t numvars )
{
	sgs_Variable* vend = var + numvars;
	
	sgs_CreateArray( C, NULL, 0 );
	
	while( var < vend )
	{
		sgs_PushString( C, "type" );
		sgs_PushInt( C, var->type );
		
		sgs_PushString( C, "data" );
		switch( var->type )
		{
		case SGS_VT_NULL:
		case SGS_VT_BOOL:
		case SGS_VT_INT:
		case SGS_VT_REAL:
		case SGS_VT_STRING:
			sgs_PushVariable( C, *var );
			break;
		case SGS_VT_FUNC:
			if( !_sgs_meta_dumpfn( C, var->data.F ) )
				return 0;
			break;
		default:
			return 0;
		}
		
		sgs_CreateDict( C, NULL, 4 );
		sgs_ArrayPush( C, sgs_StackItem( C, -2 ), 1 );
		
		var++;
	}
	
	return 1;
}

static int _sgs_meta_dumpbclist( SGS_CTX, sgs_instr_t* data, size_t numinstr )
{
	sgs_instr_t* dend = data + numinstr;
	
	sgs_CreateArray( C, NULL, 0 );
	
	while( data < dend )
	{
		sgs_instr_t i = *data++;
		
		sgs_PushString( C, "op" );
		sgs_PushInt( C, SGS_INSTR_GET_OP( i ) );
		sgs_PushString( C, "a" );
		sgs_PushInt( C, SGS_INSTR_GET_A( i ) );
		sgs_PushString( C, "b" );
		sgs_PushInt( C, SGS_INSTR_GET_B( i ) );
		sgs_PushString( C, "c" );
		sgs_PushInt( C, SGS_INSTR_GET_C( i ) );
		sgs_PushString( C, "e" );
		sgs_PushInt( C, SGS_INSTR_GET_E( i ) );
		
		sgs_CreateDict( C, NULL, 10 );
		sgs_ArrayPush( C, sgs_StackItem( C, -2 ), 1 );
	}
	
	return 1;
}

static int _sgs_meta_dumplnlist( SGS_CTX, sgs_LineNum* data, size_t numinstr )
{
	sgs_LineNum* dend = data + numinstr;
	
	sgs_CreateArray( C, NULL, 0 );
	
	while( data < dend )
	{
		sgs_PushInt( C, *data++ );
		sgs_ArrayPush( C, sgs_StackItem( C, -2 ), 1 );
	}
	
	return 1;
}

static int _sgs_meta_dumpfn( SGS_CTX, sgs_iFunc* func )
{
	sgs_Variable strvar;
	int ssz = sgs_StackSize( C );
	
	sgs_PushString( C, "consts" );
	if( !_sgs_meta_dumpconstlist( C, sgs_func_consts( func ),
			(size_t) sgs_func_const_count( func ) ) )
		return 0;
	
	sgs_PushString( C, "code" );
	if( !_sgs_meta_dumpbclist( C, sgs_func_bytecode( func ),
			(size_t) sgs_func_instr_count( func ) ) )
		return 0;
	
	sgs_PushString( C, "lines" );
	if( !_sgs_meta_dumplnlist( C, func->lineinfo,
			(size_t) sgs_func_instr_count( func ) ) )
		return 0;
	
	sgs_PushString( C, "gotthis" );
	sgs_PushBool( C, func->gotthis );
	
	sgs_PushString( C, "numargs" );
	sgs_PushInt( C, func->numargs );
	
	sgs_PushString( C, "numtmp" );
	sgs_PushInt( C, func->numtmp );
	
	sgs_PushString( C, "numclsr" );
	sgs_PushInt( C, func->numclsr );
	
	strvar.type = SGS_VT_STRING;
	
	sgs_PushString( C, "name" );
	strvar.data.S = func->sfuncname;
	sgs_PushVariable( C, strvar );
	
	sgs_PushString( C, "filename" );
	strvar.data.S = func->sfilename;
	sgs_PushVariable( C, strvar );
	
	sgs_PushString( C, "line" );
	sgs_PushInt( C, func->linenum );
	
	sgs_CreateDict( C, NULL, sgs_StackSize( C ) - ssz );
	
	return 1;
}

int sgs_meta_unpack( SGS_CTX )
{
	int ret;
	char* buf;
	const char* bfret;
	sgs_SizeVal size;
	sgs_Variable funcvar;
	
	SGSFN( "meta_unpack" );
	
	if( !sgs_LoadArgs( C, "m", &buf, &size ) )
		return 0;
	
	ret = sgsBC_ValidateHeader( buf, (size_t) size );
	if( ret < SGS_HEADER_SIZE )
		return sgs_Msg( C, SGS_WARNING, "compiled code header error "
			"detected at position %d", ret );
	
	bfret = sgsBC_Buf2Func( C, "", buf, (size_t) size, &funcvar );
	if( bfret )
		return sgs_Msg( C, SGS_WARNING, bfret );
	
	ret = _sgs_meta_dumpfn( C, funcvar.data.F );
	sgs_Release( C, &funcvar );
	
	if( !ret )
		return sgs_Msg( C, SGS_WARNING, "internal error while converting data" );
	
	return 1;
}


int sgs_meta_opname( SGS_CTX )
{
	const char* str;
	sgs_Int op;
	
	if( !sgs_LoadArgs( C, "i", &op ) )
		return 0;
	
	str = sgs_CodeString( SGS_CODE_OP, (int) op );
	if( str )
	{
		sgs_PushString( C, str );
		return 1;
	}
	return 0;
}


static sgs_RegFuncConst meta_funcs[] =
{
	{ "meta_globals", sgs_meta_globals },
	{ "meta_unpack", sgs_meta_unpack },
	{ "meta_opname", sgs_meta_opname },
};


#ifdef SGS_COMPILE_MODULE
#  define meta_module_entry_point sgscript_main
#endif


#ifdef __cplusplus
extern "C"
#endif
#ifdef WIN32
__declspec(dllexport)
#endif
int meta_module_entry_point( SGS_CTX )
{
	SGS_MODULE_CHECK_VERSION( C );
	sgs_RegFuncConsts( C, meta_funcs, SGS_ARRAY_SIZE( meta_funcs ) );
	return SGS_SUCCESS;
}

