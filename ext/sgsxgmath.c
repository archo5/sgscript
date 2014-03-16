

#include <stdio.h>

#include "sgsxgmath.h"


#define XGM_WARNING( err ) sgs_Msg( C, SGS_WARNING, err )
#define XGM_OHDR XGM_VT* hdr = (XGM_VT*) data->data;
#define XGM_V2AHDR xgm_vtarray* v2arr = (xgm_vtarray*) data->data;

#define XGM_VMUL_INNER2(a,b) ((a)[0]*(b)[0]+(a)[1]*(b)[1])
#define XGM_VMUL_INNER3(a,b) ((a)[0]*(b)[0]+(a)[1]*(b)[1]+(a)[2]*(b)[2])
#define XGM_VMUL_INNER4(a,b) ((a)[0]*(b)[0]+(a)[1]*(b)[1]+(a)[2]*(b)[2]+(a)[3]*(b)[3])

#define XGM_BB2_EXPAND_V2(b,v) \
	if( (b)[0] > (v)[0] ) (b)[0] = (v)[0]; \
	if( (b)[1] > (v)[1] ) (b)[1] = (v)[1]; \
	if( (b)[2] < (v)[0] ) (b)[2] = (v)[0]; \
	if( (b)[3] < (v)[1] ) (b)[3] = (v)[1];


SGSBOOL sgs_ParseVTP( SGS_CTX, sgs_Variable* var, XGM_VT* out )
{
	sgs_Real val;
	if( sgs_ParseRealP( C, var, &val ) )
	{
		*out = (XGM_VT) val;
		return 1;
	}
	return 0;
}


/*  2 D   V E C T O R  */

#define VEC2_IFN( fn ) \
	int is_method = sgs_Method( C ); \
	sgs_FuncName( C, is_method ? "vec2." #fn : "vec2_" #fn ); \
	if( !sgs_IsObject( C, 0, xgm_vec2_iface ) ) \
		return sgs_ArgErrorExt( C, 0, is_method, "vec2", "" ); \
	XGM_VT* data = (XGM_VT*) sgs_GetObjectData( C, 0 );

static int xgm_v2m_rotate( SGS_CTX )
{
	XGM_VT angle, s, c;
	
	VEC2_IFN( fn );
	
	if( !sgs_LoadArgs( C, "@>f", &angle ) )
		return 0;
	
	c = (XGM_VT) cos( angle );
	s = (XGM_VT) sin( angle );
	sgs_PushVec2( C, data[0] * c - data[1] * s, data[0] * s + data[1] * c );
	return 1;
}

static int xgm_v2_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	XGM_OHDR;
	if( type == SGS_CONVOP_CLONE )
	{
		sgs_PushVec2p( C, hdr );
		return SGS_SUCCESS;
	}
	else if( type == SGS_VT_STRING )
	{
		char buf[ 128 ];
		sprintf( buf, "vec2(%g;%g)", hdr[0], hdr[1] );
		sgs_PushString( C, buf );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int xgm_v2_getindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, int prop )
{
	char* str;
	XGM_OHDR;
	
	if( key->type == SGS_VT_INT )
	{
		sgs_Int pos = sgs_GetIntP( C, key );
		if( pos != 0 && pos != 1 )
			return SGS_ENOTFND;
		sgs_PushReal( C, hdr[ pos ] );
		return SGS_SUCCESS;
	}
	
	if( !sgs_ParseStringP( C, key, &str, NULL ) )
		return SGS_EINVAL;
	if( !strcmp( str, "x" ) ){ sgs_PushReal( C, hdr[ 0 ] ); return SGS_SUCCESS; }
	if( !strcmp( str, "y" ) ){ sgs_PushReal( C, hdr[ 1 ] ); return SGS_SUCCESS; }
	if( !strcmp( str, "length" ) )
	{
		sgs_PushReal( C, sqrt( XGM_VMUL_INNER2( hdr, hdr ) ) );
		return SGS_SUCCESS;
	}
	if( !strcmp( str, "length_squared" ) )
	{
		sgs_PushReal( C, XGM_VMUL_INNER2( hdr, hdr ) );
		return SGS_SUCCESS;
	}
	if( !strcmp( str, "normalized" ) )
	{
		XGM_VT lensq = XGM_VMUL_INNER2( hdr, hdr );
		if( lensq )
		{
			lensq = (XGM_VT) 1.0 / (XGM_VT) sqrt( lensq );
			sgs_PushVec2( C, hdr[0] * lensq, hdr[1] * lensq );
		}
		else
			sgs_PushVec2( C, 0, 0 );
		return SGS_SUCCESS;
	}
	if( !strcmp( str, "angle" ) ){ sgs_PushReal( C, atan2( hdr[1], hdr[0] ) ); return SGS_SUCCESS; }
	if( !strcmp( str, "perp" ) ){ sgs_PushVec2( C, -hdr[1], hdr[0] ); return SGS_SUCCESS; }
	if( !strcmp( str, "perp2" ) ){ sgs_PushVec2( C, hdr[1], -hdr[0] ); return SGS_SUCCESS; }
	if( !strcmp( str, "rotate" ) ){ sgs_PushCFunction( C, xgm_v2m_rotate ); return SGS_SUCCESS; }
	if( !strcmp( str, "size" ) ){ sgs_PushInt( C, 2 ); return SGS_SUCCESS; }
	return SGS_ENOTFND;
}

static int xgm_v2_setindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, sgs_Variable* vv, int prop )
{
	sgs_Real val;
	XGM_OHDR;
	
	if( !sgs_ParseRealP( C, vv, &val ) )
		return SGS_EINVAL;
	if( key->type == SGS_VT_INT )
	{
		sgs_Int pos = sgs_GetIntP( C, key );
		if( pos != 0 && pos != 1 )
			return SGS_ENOTFND;
		hdr[ pos ] = (XGM_VT) val;
		return SGS_SUCCESS;
	}
	else
	{
		char* str;
		if( !sgs_ParseStringP( C, key, &str, NULL ) )
			return SGS_EINVAL;
		if( !strcmp( str, "x" ) ){ hdr[0] = (XGM_VT) val; return SGS_SUCCESS; }
		if( !strcmp( str, "y" ) ){ hdr[1] = (XGM_VT) val; return SGS_SUCCESS; }
	}
	return SGS_ENOTFND;
}

static int xgm_v2_expr( SGS_CTX, sgs_VarObj* data, sgs_Variable* A, sgs_Variable* B, int type )
{
	if( type == SGS_EOP_ADD ||
		type == SGS_EOP_SUB ||
		type == SGS_EOP_MUL ||
		type == SGS_EOP_DIV ||
		type == SGS_EOP_MOD )
	{
		XGM_VT r[ 2 ], v1[ 2 ], v2[ 2 ];
		if( !sgs_ParseVec2P( C, A, v1, 0 ) || !sgs_ParseVec2P( C, B, v2, 0 ) )
			return SGS_EINVAL;
		
		if( ( type == SGS_EOP_DIV || type == SGS_EOP_MOD ) &&
			( v2[0] == 0 || v2[1] == 0 ) )
		{
			const char* errstr = type == SGS_EOP_DIV ?
				"vec2 operator '/' - division by zero" :
				"vec2 operator '%' - modulo by zero";
			sgs_Msg( C, SGS_ERROR, errstr );
			return SGS_EINPROC;
		}
		
		if( type == SGS_EOP_ADD )
			{ r[0] = v1[0] + v2[0]; r[1] = v1[1] + v2[1]; }
		else if( type == SGS_EOP_SUB )
			{ r[0] = v1[0] - v2[0]; r[1] = v1[1] - v2[1]; }
		else if( type == SGS_EOP_MUL )
			{ r[0] = v1[0] * v2[0]; r[1] = v1[1] * v2[1]; }
		else if( type == SGS_EOP_DIV )
			{ r[0] = v1[0] / v2[0]; r[1] = v1[1] / v2[1]; }
		else
			{ r[0] = (XGM_VT) fmod( v1[0], v2[0] ); r[1] = (XGM_VT) fmod( v1[1], v2[1] ); }
		
		sgs_PushVec2p( C, r );
		return SGS_SUCCESS;
	}
	else if( type == SGS_EOP_COMPARE )
	{
		XGM_VT *v1, *v2;
		if( !sgs_IsObjectP( A, xgm_vec2_iface ) ||
			!sgs_IsObjectP( B, xgm_vec2_iface ) )
			return SGS_EINVAL;
		
		v1 = (XGM_VT*) sgs_GetObjectDataP( A );
		v2 = (XGM_VT*) sgs_GetObjectDataP( B );
		
		if( v1[0] != v2[0] )
			sgs_PushReal( C, v1[0] - v2[0] );
		else
			sgs_PushReal( C, v1[1] - v2[1] );
		return SGS_SUCCESS;
	}
	else if( type == SGS_EOP_NEGATE )
	{
		XGM_OHDR;
		sgs_PushVec2( C, -hdr[0], -hdr[1] );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int xgm_v2_serialize( SGS_CTX, sgs_VarObj* data )
{
	XGM_OHDR;
	sgs_PushReal( C, hdr[0] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	sgs_PushReal( C, hdr[1] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	return sgs_SerializeObject( C, 2, "vec2" );
}

static int xgm_v2_dump( SGS_CTX, sgs_VarObj* data, int maxdepth )
{
	UNUSED( maxdepth );
	return xgm_v2_convert( C, data, SGS_VT_STRING );
}

static int xgm_vec2( SGS_CTX )
{
	int argc = sgs_StackSize( C );
	XGM_VT v[ 2 ] = { 0, 0 };
	
	SGSFN( "vec2" );
	
	if( !sgs_LoadArgs( C, "f|f.", v, v + 1 ) )
		return 0;
	
	if( argc == 1 )
		v[1] = v[0];
	
	sgs_PushVec2p( C, v );
	return 1;
}

static int xgm_vec2_dot( SGS_CTX )
{
	XGM_VT v1[2], v2[2];
	
	SGSFN( "vec2_dot" );
	
	if( !sgs_LoadArgs( C, "!x!x", sgs_ArgCheck_Vec2, v1, sgs_ArgCheck_Vec2, v2 ) )
		return 0;
	
	sgs_PushReal( C, XGM_VMUL_INNER2( v1, v2 ) );
	return 1;
}



/*  3 D   V E C T O R  */

static int xgm_v3_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	XGM_OHDR;
	if( type == SGS_CONVOP_CLONE )
	{
		sgs_PushVec3p( C, hdr );
		return SGS_SUCCESS;
	}
	else if( type == SGS_VT_STRING )
	{
		char buf[ 192 ];
		sprintf( buf, "vec3(%g;%g;%g)", hdr[0], hdr[1], hdr[2] );
		sgs_PushString( C, buf );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int xgm_v3_getindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, int prop )
{
	char* str;
	sgs_SizeVal size;
	XGM_OHDR;
	
	if( key->type == SGS_VT_INT )
	{
		sgs_Int pos = sgs_GetIntP( C, key );
		if( pos != 0 && pos != 1 && pos != 2 )
			return SGS_ENOTFND;
		sgs_PushReal( C, hdr[ pos ] );
		return SGS_SUCCESS;
	}
	
	if( sgs_ParseStringP( C, key, &str, &size ) )
	{
		if( !strcmp( str, "x" ) ){ sgs_PushReal( C, hdr[ 0 ] ); return SGS_SUCCESS; }
		if( !strcmp( str, "y" ) ){ sgs_PushReal( C, hdr[ 1 ] ); return SGS_SUCCESS; }
		if( !strcmp( str, "z" ) ){ sgs_PushReal( C, hdr[ 2 ] ); return SGS_SUCCESS; }
		if( !strcmp( str, "length" ) )
		{
			sgs_PushReal( C, sqrt( XGM_VMUL_INNER3( hdr, hdr ) ) );
			return SGS_SUCCESS;
		}
		if( !strcmp( str, "length_squared" ) )
		{
			sgs_PushReal( C, XGM_VMUL_INNER3( hdr, hdr ) );
			return SGS_SUCCESS;
		}
		if( !strcmp( str, "normalized" ) )
		{
			XGM_VT lensq = XGM_VMUL_INNER3( hdr, hdr );
			if( lensq )
			{
				lensq = (XGM_VT) 1.0 / (XGM_VT) sqrt( lensq );
				sgs_PushVec3( C, hdr[0] * lensq, hdr[1] * lensq, hdr[2] * lensq );
			}
			else
				sgs_PushVec3( C, 0, 0, 0 );
			return SGS_SUCCESS;
		}
		if( !strcmp( str, "size" ) ){ sgs_PushInt( C, 3 ); return SGS_SUCCESS; }
	}
	return SGS_ENOTFND;
}

static int xgm_v3_setindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, sgs_Variable* vv, int prop )
{
	sgs_Real val;
	XGM_OHDR;
	
	if( !sgs_ParseRealP( C, vv, &val ) )
		return SGS_EINVAL;
	if( key->type == SGS_VT_INT )
	{
		sgs_Int pos = sgs_GetIntP( C, key );
		if( pos != 0 && pos != 1 && pos != 2 )
			return SGS_ENOTFND;
		hdr[ pos ] = (XGM_VT) val;
		return SGS_SUCCESS;
	}
	else
	{
		char* str;
		sgs_SizeVal size;
		if( sgs_ParseStringP( C, key, &str, &size ) )
		{
			if( !strcmp( str, "x" ) ){ hdr[0] = (XGM_VT) val; return SGS_SUCCESS; }
			if( !strcmp( str, "y" ) ){ hdr[1] = (XGM_VT) val; return SGS_SUCCESS; }
			if( !strcmp( str, "z" ) ){ hdr[2] = (XGM_VT) val; return SGS_SUCCESS; }
		}
	}
	return SGS_ENOTFND;
}

static int xgm_v3_expr( SGS_CTX, sgs_VarObj* data, sgs_Variable* A, sgs_Variable* B, int type )
{
	if( type == SGS_EOP_ADD ||
		type == SGS_EOP_SUB ||
		type == SGS_EOP_MUL ||
		type == SGS_EOP_DIV ||
		type == SGS_EOP_MOD )
	{
		XGM_VT r[3], v1[3], v2[3];
		if( !sgs_ParseVec3P( C, A, v1, 0 ) || !sgs_ParseVec3P( C, B, v2, 0 ) )
			return SGS_EINVAL;
		
		if( ( type == SGS_EOP_DIV || type == SGS_EOP_MOD ) &&
			( v2[0] == 0 || v2[1] == 0 || v2[2] == 0 ) )
		{
			const char* errstr = type == SGS_EOP_DIV ?
				"vec3 operator '/' - division by zero" :
				"vec3 operator '%' - modulo by zero";
			sgs_Msg( C, SGS_ERROR, errstr );
			return SGS_EINPROC;
		}
		
		if( type == SGS_EOP_ADD )
			{ r[0] = v1[0] + v2[0]; r[1] = v1[1] + v2[1]; r[2] = v1[2] + v2[2]; }
		else if( type == SGS_EOP_SUB )
			{ r[0] = v1[0] - v2[0]; r[1] = v1[1] - v2[1]; r[2] = v1[2] - v2[2]; }
		else if( type == SGS_EOP_MUL )
			{ r[0] = v1[0] * v2[0]; r[1] = v1[1] * v2[1]; r[2] = v1[2] * v2[2]; }
		else if( type == SGS_EOP_DIV )
			{ r[0] = v1[0] / v2[0]; r[1] = v1[1] / v2[1]; r[2] = v1[2] / v2[2]; }
		else
		{
			r[0] = (XGM_VT) fmod( v1[0], v2[0] );
			r[1] = (XGM_VT) fmod( v1[1], v2[1] );
			r[2] = (XGM_VT) fmod( v1[2], v2[2] );
		}
		
		sgs_PushVec3p( C, r );
		return SGS_SUCCESS;
	}
	else if( type == SGS_EOP_COMPARE )
	{
		XGM_VT *v1, *v2;
		if( !sgs_IsObjectP( A, xgm_vec3_iface ) ||
			!sgs_IsObjectP( B, xgm_vec3_iface ) )
			return SGS_EINVAL;
		
		v1 = (XGM_VT*) sgs_GetObjectDataP( A );
		v2 = (XGM_VT*) sgs_GetObjectDataP( B );
		
		if( v1[0] != v2[0] )
			sgs_PushReal( C, v1[0] - v2[0] );
		else if( v1[1] != v2[1] )
			sgs_PushReal( C, v1[1] - v2[1] );
		else
			sgs_PushReal( C, v1[2] - v2[2] );
		return SGS_SUCCESS;
	}
	else if( type == SGS_EOP_NEGATE )
	{
		XGM_OHDR;
		sgs_PushVec3( C, -hdr[0], -hdr[1], -hdr[2] );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int xgm_v3_serialize( SGS_CTX, sgs_VarObj* data )
{
	XGM_OHDR;
	sgs_PushReal( C, hdr[0] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	sgs_PushReal( C, hdr[1] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	sgs_PushReal( C, hdr[2] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	return sgs_SerializeObject( C, 3, "vec3" );
}

static int xgm_v3_dump( SGS_CTX, sgs_VarObj* data, int unused )
{
	return xgm_v3_convert( C, data, SGS_VT_STRING );
}

static int xgm_vec3( SGS_CTX )
{
	int argc = sgs_StackSize( C );
	XGM_VT v[ 3 ] = { 0, 0, 0 };
	
	SGSFN( "vec3" );
	
	if( !sgs_LoadArgs( C, "f|ff.", v, v + 1, v + 2 ) )
		return 0;
	
	if( argc == 2 )
		return XGM_WARNING( "expected 1 or 3 real values" );
	
	if( argc == 1 )
		v[2] = v[1] = v[0];
	
	sgs_PushVec3p( C, v );
	return 1;
}

static int xgm_vec3_dot( SGS_CTX )
{
	XGM_VT v1[3], v2[3];
	
	SGSFN( "vec3_dot" );
	
	if( !sgs_LoadArgs( C, "!x!x", sgs_ArgCheck_Vec3, v1, sgs_ArgCheck_Vec3, v2 ) )
		return 0;
	
	sgs_PushReal( C, XGM_VMUL_INNER3( v1, v2 ) );
	return 1;
}

static int xgm_vec3_cross( SGS_CTX )
{
	XGM_VT v1[3], v2[3];
	
	SGSFN( "vec3_cross" );
	
	if( !sgs_LoadArgs( C, "!x!x", sgs_ArgCheck_Vec3, v1, sgs_ArgCheck_Vec3, v2 ) )
		return 0;
	
	sgs_PushVec3( C,
		v1[1] * v2[2] - v1[2] * v2[1],
		v1[2] * v2[0] - v1[0] * v2[2],
		v1[0] * v2[1] - v1[1] * v2[0]
	);
	return 1;
}



/*  4 D   V E C T O R  */

static int xgm_v4_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	XGM_OHDR;
	if( type == SGS_CONVOP_CLONE )
	{
		sgs_PushVec4p( C, hdr );
		return SGS_SUCCESS;
	}
	else if( type == SGS_VT_STRING )
	{
		char buf[ 256 ];
		sprintf( buf, "vec4(%g;%g;%g;%g)", hdr[0], hdr[1], hdr[2], hdr[3] );
		sgs_PushString( C, buf );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int xgm_v4_getindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, int prop )
{
	char* str;
	XGM_OHDR;
	
	if( key->type == SGS_VT_INT )
	{
		sgs_Int pos = sgs_GetIntP( C, key );
		if( pos != 0 && pos != 1 && pos != 2 && pos != 3 )
			return SGS_ENOTFND;
		sgs_PushReal( C, hdr[ pos ] );
		return SGS_SUCCESS;
	}
	
	if( sgs_ParseStringP( C, key, &str, NULL ) )
	{
		if( !strcmp( str, "x" ) ){ sgs_PushReal( C, hdr[ 0 ] ); return SGS_SUCCESS; }
		if( !strcmp( str, "y" ) ){ sgs_PushReal( C, hdr[ 1 ] ); return SGS_SUCCESS; }
		if( !strcmp( str, "z" ) ){ sgs_PushReal( C, hdr[ 2 ] ); return SGS_SUCCESS; }
		if( !strcmp( str, "w" ) ){ sgs_PushReal( C, hdr[ 3 ] ); return SGS_SUCCESS; }
		if( !strcmp( str, "length" ) )
		{
			sgs_PushReal( C, sqrt( XGM_VMUL_INNER4( hdr, hdr ) ) );
			return SGS_SUCCESS;
		}
		if( !strcmp( str, "length_squared" ) )
		{
			sgs_PushReal( C, XGM_VMUL_INNER4( hdr, hdr ) );
			return SGS_SUCCESS;
		}
		if( !strcmp( str, "normalized" ) )
		{
			XGM_VT lensq = XGM_VMUL_INNER4( hdr, hdr );
			if( lensq )
			{
				lensq = (XGM_VT) 1.0 / (XGM_VT) sqrt( lensq );
				sgs_PushVec4( C, hdr[0] * lensq, hdr[1] * lensq, hdr[2] * lensq, hdr[3] * lensq );
			}
			else
				sgs_PushVec4( C, 0, 0, 0, 0 );
			return SGS_SUCCESS;
		}
		if( !strcmp( str, "size" ) ){ sgs_PushInt( C, 4 ); return SGS_SUCCESS; }
	}
	return SGS_ENOTFND;
}

static int xgm_v4_setindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, sgs_Variable* vv, int prop )
{
	sgs_Real val;
	XGM_OHDR;
	
	if( !sgs_ParseRealP( C, vv, &val ) )
		return SGS_EINVAL;
	if( key->type == SGS_VT_INT )
	{
		sgs_Int pos = sgs_GetIntP( C, key );
		if( pos != 0 && pos != 1 && pos != 2 && pos != 3 )
			return SGS_ENOTFND;
		hdr[ pos ] = (XGM_VT) val;
		return SGS_SUCCESS;
	}
	else
	{
		char* str;
		if( sgs_ParseStringP( C, key, &str, NULL ) )
		{
			if( !strcmp( str, "x" ) ){ hdr[0] = (XGM_VT) val; return SGS_SUCCESS; }
			if( !strcmp( str, "y" ) ){ hdr[1] = (XGM_VT) val; return SGS_SUCCESS; }
			if( !strcmp( str, "z" ) ){ hdr[2] = (XGM_VT) val; return SGS_SUCCESS; }
			if( !strcmp( str, "w" ) ){ hdr[3] = (XGM_VT) val; return SGS_SUCCESS; }
		}
	}
	return SGS_ENOTFND;
}

static int xgm_v4_expr( SGS_CTX, sgs_VarObj* data, sgs_Variable* A, sgs_Variable* B, int type )
{
	if( type == SGS_EOP_ADD ||
		type == SGS_EOP_SUB ||
		type == SGS_EOP_MUL ||
		type == SGS_EOP_DIV ||
		type == SGS_EOP_MOD )
	{
		XGM_VT r[4], v1[4], v2[4];
		if( !sgs_ParseVec4P( C, A, v1, 0 ) || !sgs_ParseVec4P( C, B, v2, 0 ) )
			return SGS_EINVAL;
		
		if( ( type == SGS_EOP_DIV || type == SGS_EOP_MOD ) &&
			( v2[0] == 0 || v2[1] == 0 || v2[2] == 0 || v2[3] == 0 ) )
		{
			const char* errstr = type == SGS_EOP_DIV ?
				"vec4 operator '/' - division by zero" :
				"vec4 operator '%' - modulo by zero";
			sgs_Msg( C, SGS_ERROR, errstr );
			return SGS_EINPROC;
		}
		
		if( type == SGS_EOP_ADD )
		{
			r[0] = v1[0] + v2[0]; r[1] = v1[1] + v2[1];
			r[2] = v1[2] + v2[2]; r[3] = v1[3] + v2[3];
		}
		else if( type == SGS_EOP_SUB )
		{
			r[0] = v1[0] - v2[0]; r[1] = v1[1] - v2[1];
			r[2] = v1[2] - v2[2]; r[3] = v1[3] - v2[3];
		}
		else if( type == SGS_EOP_MUL )
		{
			r[0] = v1[0] * v2[0]; r[1] = v1[1] * v2[1];
			r[2] = v1[2] * v2[2]; r[3] = v1[3] * v2[3];
		}
		else if( type == SGS_EOP_DIV )
		{
			r[0] = v1[0] / v2[0]; r[1] = v1[1] / v2[1];
			r[2] = v1[2] / v2[2]; r[3] = v1[3] / v2[3];
		}
		else
		{
			r[0] = (XGM_VT) fmod( v1[0], v2[0] );
			r[1] = (XGM_VT) fmod( v1[1], v2[1] );
			r[2] = (XGM_VT) fmod( v1[2], v2[2] );
			r[3] = (XGM_VT) fmod( v1[3], v2[3] );
		}
		
		sgs_PushVec4p( C, r );
		return SGS_SUCCESS;
	}
	else if( type == SGS_EOP_COMPARE )
	{
		XGM_VT *v1, *v2;
		if( !sgs_IsObjectP( A, xgm_vec4_iface ) ||
			!sgs_IsObjectP( B, xgm_vec4_iface ) )
			return SGS_EINVAL;
		
		v1 = (XGM_VT*) sgs_GetObjectDataP( A );
		v2 = (XGM_VT*) sgs_GetObjectDataP( B );
		
		if( v1[0] != v2[0] ) sgs_PushReal( C, v1[0] - v2[0] );
		else if( v1[1] != v2[1] ) sgs_PushReal( C, v1[1] - v2[1] );
		else if( v1[2] != v2[2] ) sgs_PushReal( C, v1[2] - v2[2] );
		else sgs_PushReal( C, v1[3] - v2[3] );
		return SGS_SUCCESS;
	}
	else if( type == SGS_EOP_NEGATE )
	{
		XGM_OHDR;
		sgs_PushVec4( C, -hdr[0], -hdr[1], -hdr[2], -hdr[3] );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int xgm_v4_serialize( SGS_CTX, sgs_VarObj* data )
{
	XGM_OHDR;
	sgs_PushReal( C, hdr[0] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	sgs_PushReal( C, hdr[1] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	sgs_PushReal( C, hdr[2] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	sgs_PushReal( C, hdr[3] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	return sgs_SerializeObject( C, 4, "vec4" );
}

static int xgm_v4_dump( SGS_CTX, sgs_VarObj* data, int unused )
{
	return xgm_v4_convert( C, data, SGS_VT_STRING );
}

static int xgm_vec4( SGS_CTX )
{
	int argc = sgs_StackSize( C );
	XGM_VT v[ 4 ] = { 0, 0, 0, 0 };
	
	SGSFN( "vec4" );
	
	if( !sgs_LoadArgs( C, "f|fff.", v, v + 1, v + 2, v + 3 ) )
		return 0;
	
	if( argc == 1 )
		v[3] = v[2] = v[1] = v[0];
	else if( argc == 2 )
	{
		v[3] = v[1];
		v[2] = v[1] = v[0];
	}
	
	sgs_PushVec4( C, v[0], v[1], v[2], v[3] );
	return 1;
}

static int xgm_vec4_dot( SGS_CTX )
{
	XGM_VT v1[4], v2[4];
	
	SGSFN( "vec4_dot" );
	
	if( !sgs_LoadArgs( C, "!x!x", sgs_ArgCheck_Vec4, v1, sgs_ArgCheck_Vec4, v2 ) )
		return 0;
	
	sgs_PushReal( C, XGM_VMUL_INNER4( v1, v2 ) );
	return 1;
}



/*  2 D   A A B B  */

static int xgm_aabb2_expand( SGS_CTX );

static int xgm_b2_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	XGM_OHDR;
	if( type == SGS_CONVOP_CLONE )
	{
		sgs_PushAABB2( C, hdr[0], hdr[1], hdr[2], hdr[3] );
		return SGS_SUCCESS;
	}
	else if( type == SGS_VT_STRING )
	{
		char buf[ 256 ];
		sprintf( buf, "aabb2(%g;%g - %g;%g)", hdr[0], hdr[1], hdr[2], hdr[3] );
		sgs_PushString( C, buf );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int xgm_b2_getindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, int prop )
{
	char* str;
	XGM_OHDR;
	if( sgs_ParseStringP( C, key, &str, NULL ) )
	{
		SGS_CASE( "x1" )     SGS_RETURN_REAL( hdr[0] )
		SGS_CASE( "y1" )     SGS_RETURN_REAL( hdr[1] )
		SGS_CASE( "x2" )     SGS_RETURN_REAL( hdr[2] )
		SGS_CASE( "y2" )     SGS_RETURN_REAL( hdr[3] )
		SGS_CASE( "p1" )     SGS_RETURN_VEC2P( hdr )
		SGS_CASE( "p2" )     SGS_RETURN_VEC2P( hdr + 2 )
		SGS_CASE( "width" )  SGS_RETURN_REAL( hdr[2] - hdr[0] )
		SGS_CASE( "height" ) SGS_RETURN_REAL( hdr[3] - hdr[1] )
		SGS_CASE( "center" ) SGS_RETURN_VEC2( (hdr[0]+hdr[2])*0.5f, (hdr[1]+hdr[3])*0.5f )
		SGS_CASE( "area" )   SGS_RETURN_REAL( (hdr[2] - hdr[0]) * (hdr[3] - hdr[1]) )
		SGS_CASE( "valid" )  SGS_RETURN_BOOL( hdr[2] >= hdr[0] && hdr[3] >= hdr[1] )
		SGS_CASE( "expand" ) SGS_RETURN_CFUNC( xgm_aabb2_expand )
	}
	return SGS_ENOTFND;
}

static int xgm_b2_setindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, sgs_Variable* vv, int prop )
{
	char* str;
	XGM_OHDR;
	if( sgs_ParseStringP( C, key, &str, NULL ) )
	{
		if( !strcmp( str, "x1" ) ) return sgs_ParseVTP( C, vv, &hdr[0] ) ? SGS_SUCCESS : SGS_EINVAL;
		if( !strcmp( str, "y1" ) ) return sgs_ParseVTP( C, vv, &hdr[1] ) ? SGS_SUCCESS : SGS_EINVAL;
		if( !strcmp( str, "x2" ) ) return sgs_ParseVTP( C, vv, &hdr[2] ) ? SGS_SUCCESS : SGS_EINVAL;
		if( !strcmp( str, "y2" ) ) return sgs_ParseVTP( C, vv, &hdr[3] ) ? SGS_SUCCESS : SGS_EINVAL;
		if( !strcmp( str, "p1" ) ) return sgs_ParseVec2P( C, vv, hdr, 1 ) ? SGS_SUCCESS : SGS_EINVAL;
		if( !strcmp( str, "p2" ) ) return sgs_ParseVec2P( C, vv, hdr + 2, 1 ) ? SGS_SUCCESS : SGS_EINVAL;
	}
	return SGS_ENOTFND;
}

static int xgm_b2_serialize( SGS_CTX, sgs_VarObj* data )
{
	XGM_OHDR;
	sgs_PushReal( C, hdr[0] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	sgs_PushReal( C, hdr[1] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	sgs_PushReal( C, hdr[2] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	sgs_PushReal( C, hdr[3] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	return sgs_SerializeObject( C, 4, "aabb2" );
}

static int xgm_b2_dump( SGS_CTX, sgs_VarObj* data, int maxdepth )
{
	UNUSED( maxdepth );
	return xgm_b2_convert( C, data, SGS_VT_STRING );
}

static int xgm_aabb2( SGS_CTX )
{
	XGM_VT b[4] = { 0, 0, 0, 0 };
	
	SGSFN( "aabb2" );
	
	if( !sgs_LoadArgs( C, "ffff", b, b + 1, b + 2, b + 3 ) )
		return 0;
	
	sgs_PushAABB2p( C, b );
	return 1;
}

static int xgm_aabb2v( SGS_CTX )
{
	XGM_VT b[4] = { 0, 0, 0, 0 };
	
	SGSFN( "aabb2v" );
	
	if( !sgs_LoadArgs( C, "!x!x", xgm_vec2_iface, b, xgm_vec2_iface, b + 2 ) )
		return 0;
	
	sgs_PushAABB2p( C, b );
	return 1;
}

static int xgm_aabb2_intersect( SGS_CTX )
{
	XGM_VT b1[4], b2[4];
	
	SGSFN( "aabb2_intersect" );
	
	if( !sgs_LoadArgs( C, "xx", xgm_aabb2_iface, b1, xgm_aabb2_iface, b2 ) )
		return 0;
	
	sgs_PushBool( C, b1[0] < b2[2] && b2[0] < b1[2] && b1[1] < b2[3] && b2[1] < b1[3] );
	return 1;
}

static int xgm_aabb2_expand( SGS_CTX )
{
	XGM_VT* bb, tmp[4];
	
	int i, ssz = sgs_StackSize( C );
	int method_call = sgs_Method( C );
	SGSFN( method_call ? "aabb2.expand" : "aabb2_expand" );
	if( !sgs_IsObject( C, 0, xgm_aabb2_iface ) )
		return sgs_ArgErrorExt( C, 0, method_call, "aabb2", "" );
	bb = (XGM_VT*) sgs_GetObjectData( C, 0 );
	
	for( i = 1; i < ssz; ++i )
	{
		if( sgs_ParseVec2( C, i, tmp, 0 ) )
		{
			XGM_BB2_EXPAND_V2( bb, tmp );
		}
		else if( sgs_ParseAABB2( C, i, tmp ) )
		{
			XGM_BB2_EXPAND_V2( bb, tmp );
			XGM_BB2_EXPAND_V2( bb, tmp + 2 );
		}
		else
			return sgs_ArgErrorExt( C, i, 0, "aabb2 or vec2", "" );
	}
	return 0;
}



/*  C O L O R  */

static int xgm_col_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	XGM_OHDR;
	if( type == SGS_CONVOP_CLONE )
	{
		sgs_PushColorp( C, hdr );
		return SGS_SUCCESS;
	}
	else if( type == SGS_VT_STRING )
	{
		char buf[ 256 ];
		sprintf( buf, "color(%g;%g;%g;%g)", hdr[0], hdr[1], hdr[2], hdr[3] );
		sgs_PushString( C, buf );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int xgm_col_getindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, int prop )
{
	char* str;
	XGM_OHDR;
	if( key->type == SGS_VT_INT )
	{
		sgs_Int pos = sgs_GetInt( C, 0 );
		if( pos != 0 && pos != 1 && pos != 2 && pos != 3 )
			return SGS_ENOTFND;
		sgs_PushReal( C, hdr[ pos ] );
		return SGS_SUCCESS;
	}
	
	if( sgs_ParseStringP( C, key, &str, NULL ) )
	{
		if( !strcmp( str, "r" ) ){ sgs_PushReal( C, hdr[ 0 ] ); return SGS_SUCCESS; }
		if( !strcmp( str, "g" ) ){ sgs_PushReal( C, hdr[ 1 ] ); return SGS_SUCCESS; }
		if( !strcmp( str, "b" ) ){ sgs_PushReal( C, hdr[ 2 ] ); return SGS_SUCCESS; }
		if( !strcmp( str, "a" ) ){ sgs_PushReal( C, hdr[ 3 ] ); return SGS_SUCCESS; }
		if( !strcmp( str, "size" ) ){ sgs_PushInt( C, 4 ); return SGS_SUCCESS; }
	}
	return SGS_ENOTFND;
}

static int xgm_col_setindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, sgs_Variable* vv, int prop )
{
	sgs_Real val;
	XGM_OHDR;
	if( !sgs_ParseRealP( C, vv, &val ) )
		return SGS_EINVAL;
	if( key->type == SGS_VT_INT )
	{
		sgs_Int pos = sgs_GetIntP( C, key );
		if( pos != 0 && pos != 1 && pos != 2 && pos != 3 )
			return SGS_ENOTFND;
		hdr[ pos ] = (XGM_VT) val;
		return SGS_SUCCESS;
	}
	else
	{
		char* str;
		if( !sgs_ParseStringP( C, key, &str, NULL ) )
		{
			if( !strcmp( str, "r" ) ){ hdr[0] = (XGM_VT) val; return SGS_SUCCESS; }
			if( !strcmp( str, "g" ) ){ hdr[1] = (XGM_VT) val; return SGS_SUCCESS; }
			if( !strcmp( str, "b" ) ){ hdr[2] = (XGM_VT) val; return SGS_SUCCESS; }
			if( !strcmp( str, "a" ) ){ hdr[3] = (XGM_VT) val; return SGS_SUCCESS; }
		}
	}
	return SGS_ENOTFND;
}

static int xgm_col_expr( SGS_CTX, sgs_VarObj* data, sgs_Variable* A, sgs_Variable* B, int type )
{
	if( type == SGS_EOP_ADD ||
		type == SGS_EOP_SUB ||
		type == SGS_EOP_MUL ||
		type == SGS_EOP_DIV ||
		type == SGS_EOP_MOD )
	{
		XGM_VT r[4], v1[4], v2[4];
		if( !sgs_ParseColorP( C, A, v1, 0 ) || !sgs_ParseColorP( C, B, v2, 0 ) )
			return SGS_EINVAL;
		
		if( ( type == SGS_EOP_DIV || type == SGS_EOP_MOD ) &&
			( v2[0] == 0 || v2[1] == 0 || v2[2] == 0 || v2[3] == 0 ) )
		{
			const char* errstr = type == SGS_EOP_DIV ?
				"color operator '/' - division by zero" :
				"color operator '%' - modulo by zero";
			sgs_Msg( C, SGS_ERROR, errstr );
			return SGS_EINPROC;
		}
		
		if( type == SGS_EOP_ADD )
		{
			r[0] = v1[0] + v2[0]; r[1] = v1[1] + v2[1];
			r[2] = v1[2] + v2[2]; r[3] = v1[3] + v2[3];
		}
		else if( type == SGS_EOP_SUB )
		{
			r[0] = v1[0] - v2[0]; r[1] = v1[1] - v2[1];
			r[2] = v1[2] - v2[2]; r[3] = v1[3] - v2[3];
		}
		else if( type == SGS_EOP_MUL )
		{
			r[0] = v1[0] * v2[0]; r[1] = v1[1] * v2[1];
			r[2] = v1[2] * v2[2]; r[3] = v1[3] * v2[3];
		}
		else if( type == SGS_EOP_DIV )
		{
			r[0] = v1[0] / v2[0]; r[1] = v1[1] / v2[1];
			r[2] = v1[2] / v2[2]; r[3] = v1[3] / v2[3];
		}
		else
		{
			r[0] = (XGM_VT) fmod( v1[0], v2[0] );
			r[1] = (XGM_VT) fmod( v1[1], v2[1] );
			r[2] = (XGM_VT) fmod( v1[2], v2[2] );
			r[3] = (XGM_VT) fmod( v1[3], v2[3] );
		}
		
		sgs_PushColorp( C, r );
		return SGS_SUCCESS;
	}
	else if( type == SGS_EOP_COMPARE )
	{
		XGM_VT *v1, *v2;
		if( !sgs_IsObjectP( A, xgm_vec4_iface ) ||
			!sgs_IsObjectP( B, xgm_vec4_iface ) )
			return SGS_EINVAL;
		
		v1 = (XGM_VT*) sgs_GetObjectDataP( A );
		v2 = (XGM_VT*) sgs_GetObjectDataP( B );
		
		if( v1[0] != v2[0] ) sgs_PushReal( C, v1[0] - v2[0] );
		else if( v1[1] != v2[1] ) sgs_PushReal( C, v1[1] - v2[1] );
		else if( v1[2] != v2[2] ) sgs_PushReal( C, v1[2] - v2[2] );
		else sgs_PushReal( C, v1[3] - v2[3] );
		return SGS_SUCCESS;
	}
	else if( type == SGS_EOP_NEGATE )
	{
		XGM_OHDR;
		sgs_PushColor( C, -hdr[0], -hdr[1], -hdr[2], -hdr[3] );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int xgm_col_serialize( SGS_CTX, sgs_VarObj* data )
{
	int i;
	XGM_OHDR;
	for( i = 0; i < 4; ++i )
	{
		sgs_PushReal( C, hdr[0] );
		if( sgs_Serialize( C ) )
			return SGS_EINPROC;
	}
	return sgs_SerializeObject( C, 4, "color" );
}

static int xgm_col_dump( SGS_CTX, sgs_VarObj* data, int unused )
{
	return xgm_col_convert( C, data, SGS_VT_STRING );
}

static int xgm_color( SGS_CTX )
{
	int argc = sgs_StackSize( C );
	XGM_VT v[ 4 ] = { 0, 0, 0, 0 };
	
	SGSFN( "color" );
	
	if( !sgs_LoadArgs( C, "f|fff.", v, v + 1, v + 2, v + 3 ) )
		return 0;
	
	sgs_PushColorvp( C, v, argc );
	return 1;
}



/*  4 x 4   M A T R I X  */

static int xgm_m4_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	XGM_OHDR;
	if( type == SGS_CONVOP_CLONE )
	{
		sgs_PushMat4( C, hdr, 0 );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int xgm_m4_getindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, int prop )
{
	char* str;
	XGM_OHDR;
	if( key->type == SGS_VT_INT )
	{
		sgs_Int pos = sgs_GetIntP( C, key );
		if( pos < 0 || pos > 15 )
			return SGS_ENOTFND;
		sgs_PushReal( C, hdr[ pos ] );
		return SGS_SUCCESS;
	}
	
	if( sgs_ParseStringP( C, key, &str, NULL ) )
	{
		if( *str == 'm' && str[1] && str[2] && !str[3] )
		{
			int nx = str[1] - '0';
			int ny = str[2] - '0';
			if( nx >= 0 && nx < 4 && ny >= 0 && ny < 4 )
			{
				sgs_PushReal( C, hdr[ nx + ny * 4 ] );
				return SGS_SUCCESS;
			}
		}
		if( !strcmp( str, "size" ) ){ sgs_PushInt( C, 16 ); return SGS_SUCCESS; }
	}
	return SGS_ENOTFND;
}

static int xgm_m4_setindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, sgs_Variable* vv, int prop )
{
	char* str;
	XGM_OHDR;
	sgs_Real val;
	if( key->type == SGS_VT_INT )
	{
		sgs_Int pos = sgs_GetIntP( C, key );
		if( pos < 0 || pos > 15 )
			return SGS_ENOTFND;
		if( sgs_ParseRealP( C, vv, &val ) )
		{
			hdr[ pos ] = (XGM_VT) val;
			return SGS_SUCCESS;
		}
		else
			return SGS_EINVAL;
	}
	
	if( sgs_ParseStringP( C, key, &str, NULL ) )
	{
		if( *str == 'm' && str[1] && str[2] && !str[3] )
		{
			int nx = str[1] - '0';
			int ny = str[2] - '0';
			if( nx >= 0 && nx < 4 && ny >= 0 && ny < 4 )
			{
				if( sgs_ParseRealP( C, vv, &val ) )
				{
					hdr[ nx + ny * 4 ] = (XGM_VT) val;
					return SGS_SUCCESS;
				}
				else
					return SGS_EINVAL;
			}
		}
	}
	return SGS_ENOTFND;
}

static int xgm_m4_serialize( SGS_CTX, sgs_VarObj* data )
{
	int i;
	XGM_OHDR;
	for( i = 0; i < 16; ++i )
	{
		sgs_PushReal( C, hdr[0] );
		if( sgs_Serialize( C ) )
			return SGS_EINPROC;
	}
	return sgs_SerializeObject( C, 16, "mat4" );
}

static int xgm_m4_dump( SGS_CTX, sgs_VarObj* data, int maxdepth )
{
	char bfr[ 1024 ];
	XGM_OHDR;
	UNUSED( maxdepth );
	snprintf( bfr, 1024,
		"\n%10.6g %10.6g %10.6g %10.6g"
		"\n%10.6g %10.6g %10.6g %10.6g"
		"\n%10.6g %10.6g %10.6g %10.6g"
		"\n%10.6g %10.6g %10.6g %10.6g",
		hdr[0], hdr[1], hdr[2], hdr[3],
		hdr[4], hdr[5], hdr[6], hdr[7],
		hdr[8], hdr[9], hdr[10], hdr[11],
		hdr[12], hdr[13], hdr[14], hdr[15] );
	bfr[ 1023 ] = 0;
	sgs_PushString( C, "mat4\n(" );
	sgs_PushString( C, bfr );
	if( sgs_PadString( C ) ) return SGS_EINPROC;
	sgs_PushString( C, "\n)" );
	if( sgs_StringConcat( C, 3 ) ) return SGS_EINPROC;
	return SGS_SUCCESS;
}

static int xgm_mat4( SGS_CTX )
{
	XGM_VT v[ 16 ];
	int argc = sgs_StackSize( C );
	
	SGSFN( "mat4" );
	
	if( !argc )
	{
		int i;
		for( i = 0; i < 16; ++i )
			v[ i ] = 0;
		v[0] = v[5] = v[10] = v[15] = 1;
		sgs_PushMat4( C, v, 0 );
		return 1;
	}
	else if( argc == 1 && sgs_ParseMat4( C, 0, v ) )
	{
		sgs_PushMat4( C, v, 0 );
		return 1;
	}
	else if( argc >= 3 && argc <= 4 )
	{
		if( sgs_ParseVec4( C, 0, v, 0 ) &&
			sgs_ParseVec4( C, 1, v+4, 0 ) &&
			sgs_ParseVec4( C, 2, v+8, 0 ) )
		{
			if( !sgs_ParseVec4( C, 3, v+12, 0 ) )
			{
				v[12] = v[13] = v[14] = 0;
				v[15] = 1;
			}
			sgs_PushMat4( C, v, 0 );
			return 1;
		}
	}
	else if( argc == 16 )
	{
		int i;
		for( i = 0; i < 16; ++i )
		{
			sgs_Real val;
			if( !sgs_ParseReal( C, i, &val ) )
				break;
			v[ i ] = (XGM_VT) val;
		}
		if( i == 16 )
		{
			sgs_PushMat4( C, v, 0 );
			return 1;
		}
	}
	return sgs_Msg( C, SGS_WARNING, "expected 0 arguments or "
		"1 mat4 argument or 3-4 vec4 arguments or 16 real arguments" );
}



/*  V E C 2 A R R A Y  */

static int xgm_v2a_destruct( SGS_CTX, sgs_VarObj* data )
{
	XGM_V2AHDR;
	if( v2arr->data )
		sgs_Dealloc( v2arr->data );
	return SGS_SUCCESS;
}

static int xgm_v2a_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	XGM_V2AHDR;
	if( type == SGS_CONVOP_CLONE )
	{
		sgs_PushVec2Array( C, v2arr->data, v2arr->size );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int xgm_v2a_serialize( SGS_CTX, sgs_VarObj* data )
{
	sgs_SizeVal i;
	XGM_V2AHDR;
	for( i = 0; i < v2arr->size * 2; ++i )
	{
		sgs_PushReal( C, v2arr->data[ i ] );
		if( sgs_Serialize( C ) )
			return SGS_EINPROC;
	}
	return sgs_SerializeObject( C, v2arr->size * 2, "vec2array" );
}

static int xgm_v2a_dump( SGS_CTX, sgs_VarObj* data, int maxdepth )
{
	XGM_V2AHDR;
	sgs_SizeVal i, vc = v2arr->size > 64 ? 64 : v2arr->size;
	sgs_PushString( C, "\n{" );
	for( i = 0; i < vc; ++i )
	{
		char bfr[ 128 ];
		snprintf( bfr, 128, "\n%10.6g %10.6g" );
		bfr[ 127 ] = 0;
		sgs_PushString( C, bfr );
	}
	if( vc < v2arr->size )
	{
		sgs_PushString( C, "\n..." );
		vc++;
	}
	if( vc > 1 ) /* concatenate all numbers and "..." if it exists" */
		sgs_StringConcat( C, vc );
	sgs_PadString( C );
	sgs_PushString( C, "\n}" );
	sgs_StringConcat( C, 3 );
	return SGS_SUCCESS;
}

static int xgm_v2a_getindex_aabb( SGS_CTX, xgm_vtarray* v2arr )
{
	if( !v2arr->size )
	{
		XGM_WARNING( "cannot get AABB of empty vec2array" );
		return SGS_EINPROC;
	}
	else
	{
		sgs_SizeVal i;
		XGM_VT bb[4] = {
			v2arr->data[0], v2arr->data[1], v2arr->data[0], v2arr->data[1]
		};
		for( i = 2; i < v2arr->size; i += 2 )
		{
			XGM_VT* pp = v2arr->data + i;
			if( bb[0] > pp[0] ) bb[0] = pp[0];
			if( bb[1] > pp[1] ) bb[1] = pp[1];
			if( bb[2] < pp[0] ) bb[2] = pp[0];
			if( bb[3] < pp[1] ) bb[3] = pp[1];
		}
		sgs_PushAABB2p( C, bb );
		return SGS_SUCCESS;
	}
}

static int xgm_v2a_setindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, sgs_Variable* vv, int prop )
{
	XGM_V2AHDR;
	XGM_VT val[2];
	if( key->type == SGS_VT_INT )
	{
		sgs_Int pos = sgs_GetIntP( C, key );
		if( pos < 0 || pos >= v2arr->size )
			return XGM_WARNING( "index out of bounds" );
		if( sgs_ParseVec2P( C, vv, val, 0 ) )
		{
			v2arr->data[ pos * 2 + 0 ] = val[0];
			v2arr->data[ pos * 2 + 1 ] = val[1];
			return SGS_SUCCESS;
		}
		else
			return SGS_EINVAL;
	}
	
	return SGS_ENOTFND;
}

/* methods */
#define XGM_V2A_IHDR( funcname ) xgm_vtarray* v2arr; \
	if( !SGS_PARSE_METHOD( C, xgm_vec2arr_iface, v2arr, vec2array, funcname ) ) return 0;

#define XGM_V2A_BINOPMETHOD( opname, assignop ) \
static int xgm_v2a_##opname( SGS_CTX ) \
{ \
	XGM_VT v2f[ 2 ], *v2fa; \
	sgs_SizeVal i, sz; \
	 \
	XGM_V2A_IHDR( opname ) \
	 \
	if( sgs_ParseVec2( C, 0, v2f, 0 ) ) \
	{ \
		sz = v2arr->size * 2; \
		for( i = 0; i < sz; i += 2 ) \
		{ \
			v2arr->data[ i+0 ] assignop v2f[ 0 ]; \
			v2arr->data[ i+1 ] assignop v2f[ 1 ]; \
		} \
		return 0; \
	} \
	 \
	if( sgs_ParseVec2Array( C, 0, &v2fa, &sz ) ) \
	{ \
		if( sz != v2arr->size ) \
			return XGM_WARNING( "array sizes don't match" ); \
		for( i = 0; i < sz; ++i ) \
		{ \
			v2arr->data[ i * 2 + 0 ] assignop v2fa[ i * 2 + 0 ]; \
			v2arr->data[ i * 2 + 1 ] assignop v2fa[ i * 2 + 1 ]; \
		} \
		return 0; \
	} \
	 \
	return XGM_WARNING( "expected real, vec2 or vec2array" ); \
}

XGM_V2A_BINOPMETHOD( add, += );
XGM_V2A_BINOPMETHOD( sub, -= );
XGM_V2A_BINOPMETHOD( mul, *= );
XGM_V2A_BINOPMETHOD( div, /= );

static int xgm_v2a_getindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, int isprop )
{
	sgs_Int idx;
	XGM_V2AHDR;
	
	if( !isprop && sgs_ParseIntP( C, key, &idx ) )
	{
		if( idx < 0 || idx >= v2arr->size )
			return SGS_EBOUNDS;
		sgs_PushVec2p( C, v2arr->data + idx * 2 );
		return SGS_SUCCESS;
	}
	
	if( isprop )
	{
		char* str;
		if( sgs_ParseStringP( C, key, &str, NULL ) )
		{
			if( !strcmp( str, "aabb" ) ) return xgm_v2a_getindex_aabb( C, v2arr );
			if( !strcmp( str, "size" ) ){ sgs_PushInt( C, v2arr->size / 2 ); return SGS_SUCCESS; }
			
			SGS_CASE( "add" ) SGS_RETURN_CFUNC( xgm_v2a_add )
			SGS_CASE( "sub" ) SGS_RETURN_CFUNC( xgm_v2a_sub )
			SGS_CASE( "mul" ) SGS_RETURN_CFUNC( xgm_v2a_mul )
			SGS_CASE( "div" ) SGS_RETURN_CFUNC( xgm_v2a_div )
		}
	}
	
	return SGS_ENOTFND;
}

static XGM_VT* _xgm_pushv2a( SGS_CTX, sgs_SizeVal size )
{
	xgm_vtarray* np = (xgm_vtarray*) sgs_PushObjectIPA( C, sizeof(xgm_vtarray), xgm_vec2arr_iface );
	np->size = size;
	np->mem = size;
	np->data = size ? sgs_Alloc_n( XGM_VT, (size_t) np->mem * 2 ) : NULL;
	return np->data;
}

static int xgm_vec2array( SGS_CTX )
{
	sgs_SizeVal asize;
	
	/* create vec2array from array */
	if( ( asize = sgs_ArraySize( C, 0 ) ) >= 0 )
	{
		XGM_VT* fdata = _xgm_pushv2a( C, asize );
		sgs_PushIterator( C, 0 );
		while( sgs_IterAdvance( C, -1 ) > 0 )
		{
			sgs_IterPushData( C, -1, 0, 1 );
			if( !sgs_ParseVec2( C, -1, fdata, 0 ) )
				return XGM_WARNING( "failed to parse array" );
			fdata += 2;
			sgs_Pop( C, 1 );
		}
		sgs_Pop( C, 1 );
		return 1;
	}
	
	/* create vec2array from list of vec2 */
	if( sgs_IsObject( C, 0, xgm_vec2_iface ) )
	{
		sgs_StkIdx i, ssz = sgs_StackSize( C );
		XGM_VT* fdata = _xgm_pushv2a( C, ssz );
		for( i = 0; i < ssz; ++i )
		{
			if( !sgs_ParseVec2( C, i, fdata, 1 ) )
				return sgs_Msg( C, SGS_WARNING, "failed to parse argument %d as vec2", i + 1 );
			fdata += 2;
		}
		return 1;
	}
	
	if( sgs_ItemType( C, 0 ) == SGS_VT_INT || sgs_ItemType( C, 0 ) == SGS_VT_REAL )
	{
		sgs_StkIdx i, ssz = sgs_StackSize( C );
		XGM_VT* fdata;
		if( ssz % 2 != 0 )
			return XGM_WARNING( "scalar argument count not multiple of 2" );
		fdata = _xgm_pushv2a( C, ssz );
		for( i = 0; i < ssz; i += 2 )
		{
			fdata[ 0 ] = (XGM_VT) sgs_GetReal( C, i+0 );
			fdata[ 1 ] = (XGM_VT) sgs_GetReal( C, i+1 );
			fdata += 2;
		}
		return 1;
	}
	
	return XGM_WARNING( "expected array of vec2, array of arrays, vec2 list or float list" );
}



sgs_ObjInterface xgm_vec2_iface[1] =
{{
	"vec2",
	NULL, NULL,
	xgm_v2_getindex, xgm_v2_setindex,
	xgm_v2_convert, xgm_v2_serialize, xgm_v2_dump, NULL,
	NULL, xgm_v2_expr
}};

sgs_ObjInterface xgm_vec3_iface[1] =
{{
	"vec3",
	NULL, NULL,
	xgm_v3_getindex, xgm_v3_setindex,
	xgm_v3_convert, xgm_v3_serialize, xgm_v3_dump, NULL,
	NULL, xgm_v3_expr
}};

sgs_ObjInterface xgm_vec4_iface[1] =
{{
	"vec4",
	NULL, NULL,
	xgm_v4_getindex, xgm_v4_setindex,
	xgm_v4_convert, xgm_v4_serialize, xgm_v4_dump, NULL,
	NULL, xgm_v4_expr
}};

sgs_ObjInterface xgm_aabb2_iface[1] =
{{
	"aabb2",
	NULL, NULL,
	xgm_b2_getindex, xgm_b2_setindex,
	xgm_b2_convert, xgm_b2_serialize, xgm_b2_dump, NULL,
	NULL, NULL
}};

sgs_ObjInterface xgm_color_iface[1] =
{{
	"color",
	NULL, NULL,
	xgm_col_getindex, xgm_col_setindex,
	xgm_col_convert, xgm_col_serialize, xgm_col_dump, NULL,
	NULL, xgm_col_expr
}};

sgs_ObjInterface xgm_mat4_iface[1] =
{{
	"mat4",
	NULL, NULL,
	xgm_m4_getindex, xgm_m4_setindex,
	xgm_m4_convert, xgm_m4_serialize, xgm_m4_dump, NULL,
	NULL, NULL
}};

sgs_ObjInterface xgm_vec2arr_iface[1] =
{{
	"vec2array",
	xgm_v2a_destruct, NULL,
	xgm_v2a_getindex, xgm_v2a_setindex,
	xgm_v2a_convert, xgm_v2a_serialize, xgm_v2a_dump, NULL,
	NULL, NULL
}};


void sgs_InitVec2( SGS_CTX, sgs_Variable* var, XGM_VT x, XGM_VT y )
{
	XGM_VT* nv = (XGM_VT*) sgs_InitObjectIPA( C, var, sizeof(XGM_VT) * 2, xgm_vec2_iface );
	nv[ 0 ] = x;
	nv[ 1 ] = y;
}

void sgs_InitVec3( SGS_CTX, sgs_Variable* var, XGM_VT x, XGM_VT y, XGM_VT z )
{
	XGM_VT* nv = (XGM_VT*) sgs_InitObjectIPA( C, var, sizeof(XGM_VT) * 3, xgm_vec3_iface );
	nv[ 0 ] = x;
	nv[ 1 ] = y;
	nv[ 2 ] = z;
}

void sgs_InitVec4( SGS_CTX, sgs_Variable* var, XGM_VT x, XGM_VT y, XGM_VT z, XGM_VT w )
{
	XGM_VT* nv = (XGM_VT*) sgs_InitObjectIPA( C, var, sizeof(XGM_VT) * 4, xgm_vec4_iface );
	nv[ 0 ] = x;
	nv[ 1 ] = y;
	nv[ 2 ] = z;
	nv[ 3 ] = w;
}

void sgs_InitAABB2( SGS_CTX, sgs_Variable* var, XGM_VT x1, XGM_VT y1, XGM_VT x2, XGM_VT y2 )
{
	XGM_VT* nv = (XGM_VT*) sgs_InitObjectIPA( C, var, sizeof(XGM_VT) * 4, xgm_aabb2_iface );
	nv[ 0 ] = x1;
	nv[ 1 ] = y1;
	nv[ 2 ] = x2;
	nv[ 3 ] = y2;
}

void sgs_InitColor( SGS_CTX, sgs_Variable* var, XGM_VT x, XGM_VT y, XGM_VT z, XGM_VT w )
{
	XGM_VT* nv = (XGM_VT*) sgs_InitObjectIPA( C, var, sizeof(XGM_VT) * 4, xgm_color_iface );
	nv[ 0 ] = x;
	nv[ 1 ] = y;
	nv[ 2 ] = z;
	nv[ 3 ] = w;
}

void sgs_InitMat4( SGS_CTX, sgs_Variable* var, XGM_VT* v16f, int transpose )
{
	XGM_VT* nv = (XGM_VT*) sgs_InitObjectIPA( C, var, sizeof(XGM_VT) * 16, xgm_mat4_iface );
	if( transpose )
	{
		nv[ 0 ] = v16f[ 0 ]; nv[ 1 ] = v16f[ 4 ]; nv[ 2 ] = v16f[ 8 ]; nv[ 3 ] = v16f[ 12 ];
		nv[ 4 ] = v16f[ 1 ]; nv[ 5 ] = v16f[ 5 ]; nv[ 6 ] = v16f[ 9 ]; nv[ 7 ] = v16f[ 13 ];
		nv[ 8 ] = v16f[ 2 ]; nv[ 9 ] = v16f[ 6 ]; nv[ 10 ] = v16f[ 10 ]; nv[ 11 ] = v16f[ 14 ];
		nv[ 12 ] = v16f[ 3 ]; nv[ 13 ] = v16f[ 7 ]; nv[ 14 ] = v16f[ 11 ]; nv[ 15 ] = v16f[ 15 ];
	}
	else
		memcpy( nv, v16f, sizeof(XGM_VT) * 16 );
}

void sgs_InitVec2Array( SGS_CTX, sgs_Variable* var, XGM_VT* v2fn, sgs_SizeVal size )
{
	xgm_vtarray* np = (xgm_vtarray*) sgs_InitObjectIPA( C, var, sizeof(xgm_vtarray), xgm_vec2arr_iface );
	np->size = size;
	np->mem = size;
	np->data = size ? sgs_Alloc_n( XGM_VT, (size_t) np->mem * 2 ) : NULL;
	memcpy( np->data, v2fn, sizeof( XGM_VT ) * (size_t) np->mem * 2 );
}


void sgs_InitVec2p( SGS_CTX, sgs_Variable* var, XGM_VT* v2f )
{
	XGM_VT* nv = (XGM_VT*) sgs_InitObjectIPA( C, var, sizeof(XGM_VT) * 2, xgm_vec2_iface );
	nv[ 0 ] = v2f[ 0 ];
	nv[ 1 ] = v2f[ 1 ];
}

void sgs_InitVec3p( SGS_CTX, sgs_Variable* var, XGM_VT* v3f )
{
	XGM_VT* nv = (XGM_VT*) sgs_InitObjectIPA( C, var, sizeof(XGM_VT) * 3, xgm_vec3_iface );
	nv[ 0 ] = v3f[ 0 ];
	nv[ 1 ] = v3f[ 1 ];
	nv[ 2 ] = v3f[ 2 ];
}

void sgs_InitVec4p( SGS_CTX, sgs_Variable* var, XGM_VT* v4f )
{
	XGM_VT* nv = (XGM_VT*) sgs_InitObjectIPA( C, var, sizeof(XGM_VT) * 4, xgm_vec4_iface );
	nv[ 0 ] = v4f[ 0 ];
	nv[ 1 ] = v4f[ 1 ];
	nv[ 2 ] = v4f[ 2 ];
	nv[ 3 ] = v4f[ 3 ];
}

void sgs_InitAABB2p( SGS_CTX, sgs_Variable* var, XGM_VT* v4f )
{
	XGM_VT* nv = (XGM_VT*) sgs_InitObjectIPA( C, var, sizeof(XGM_VT) * 4, xgm_aabb2_iface );
	nv[ 0 ] = v4f[ 0 ];
	nv[ 1 ] = v4f[ 1 ];
	nv[ 2 ] = v4f[ 2 ];
	nv[ 3 ] = v4f[ 3 ];
}

void sgs_InitColorp( SGS_CTX, sgs_Variable* var, XGM_VT* v4f )
{
	XGM_VT* nv = (XGM_VT*) sgs_InitObjectIPA( C, var, sizeof(XGM_VT) * 4, xgm_color_iface );
	nv[ 0 ] = v4f[ 0 ];
	nv[ 1 ] = v4f[ 1 ];
	nv[ 2 ] = v4f[ 2 ];
	nv[ 3 ] = v4f[ 3 ];
}

void sgs_InitColorvp( SGS_CTX, sgs_Variable* var, XGM_VT* vf, int numfloats )
{
	XGM_VT* nv = (XGM_VT*) sgs_InitObjectIPA( C, var, sizeof(XGM_VT) * 4, xgm_color_iface );
	if( numfloats == 0 ) nv[0] = nv[1] = nv[2] = nv[3] = 0;
	else if( numfloats == 1 ) nv[0] = nv[1] = nv[2] = nv[3] = vf[0];
	else if( numfloats == 2 ){ nv[0] = nv[1] = nv[2] = vf[0]; nv[3] = vf[1]; }
	else if( numfloats == 3 ){ nv[0] = vf[0]; nv[1] = vf[1]; nv[2] = vf[2]; nv[3] = 1; }
	else { nv[0] = vf[0]; nv[1] = vf[1]; nv[2] = vf[2]; nv[3] = vf[3]; }
}


void sgs_PushVec2( SGS_CTX, XGM_VT x, XGM_VT y )
{
	XGM_VT* nv = (XGM_VT*) sgs_PushObjectIPA( C, sizeof(XGM_VT) * 2, xgm_vec2_iface );
	nv[ 0 ] = x;
	nv[ 1 ] = y;
}

void sgs_PushVec3( SGS_CTX, XGM_VT x, XGM_VT y, XGM_VT z )
{
	XGM_VT* nv = (XGM_VT*) sgs_PushObjectIPA( C, sizeof(XGM_VT) * 3, xgm_vec3_iface );
	nv[ 0 ] = x;
	nv[ 1 ] = y;
	nv[ 2 ] = z;
}

void sgs_PushVec4( SGS_CTX, XGM_VT x, XGM_VT y, XGM_VT z, XGM_VT w )
{
	XGM_VT* nv = (XGM_VT*) sgs_PushObjectIPA( C, sizeof(XGM_VT) * 4, xgm_vec4_iface );
	nv[ 0 ] = x;
	nv[ 1 ] = y;
	nv[ 2 ] = z;
	nv[ 3 ] = w;
}

void sgs_PushAABB2( SGS_CTX, XGM_VT x1, XGM_VT y1, XGM_VT x2, XGM_VT y2 )
{
	XGM_VT* nv = (XGM_VT*) sgs_PushObjectIPA( C, sizeof(XGM_VT) * 4, xgm_aabb2_iface );
	nv[ 0 ] = x1;
	nv[ 1 ] = y1;
	nv[ 2 ] = x2;
	nv[ 3 ] = y2;
}

void sgs_PushColor( SGS_CTX, XGM_VT x, XGM_VT y, XGM_VT z, XGM_VT w )
{
	XGM_VT* nv = (XGM_VT*) sgs_PushObjectIPA( C, sizeof(XGM_VT) * 4, xgm_color_iface );
	nv[ 0 ] = x;
	nv[ 1 ] = y;
	nv[ 2 ] = z;
	nv[ 3 ] = w;
}

void sgs_PushMat4( SGS_CTX, XGM_VT* v16f, int transpose )
{
	XGM_VT* nv = (XGM_VT*) sgs_PushObjectIPA( C, sizeof(XGM_VT) * 16, xgm_mat4_iface );
	if( transpose )
	{
		nv[ 0 ] = v16f[ 0 ]; nv[ 1 ] = v16f[ 4 ]; nv[ 2 ] = v16f[ 8 ]; nv[ 3 ] = v16f[ 12 ];
		nv[ 4 ] = v16f[ 1 ]; nv[ 5 ] = v16f[ 5 ]; nv[ 6 ] = v16f[ 9 ]; nv[ 7 ] = v16f[ 13 ];
		nv[ 8 ] = v16f[ 2 ]; nv[ 9 ] = v16f[ 6 ]; nv[ 10 ] = v16f[ 10 ]; nv[ 11 ] = v16f[ 14 ];
		nv[ 12 ] = v16f[ 3 ]; nv[ 13 ] = v16f[ 7 ]; nv[ 14 ] = v16f[ 11 ]; nv[ 15 ] = v16f[ 15 ];
	}
	else
		memcpy( nv, v16f, sizeof(XGM_VT) * 16 );
}

void sgs_PushVec2Array( SGS_CTX, XGM_VT* v2fn, sgs_SizeVal size )
{
	xgm_vtarray* np = (xgm_vtarray*) sgs_PushObjectIPA( C, sizeof(xgm_vtarray), xgm_vec2arr_iface );
	np->size = size;
	np->mem = size;
	np->data = size ? sgs_Alloc_n( XGM_VT, (size_t) np->mem * 2 ) : NULL;
	memcpy( np->data, v2fn, sizeof( XGM_VT ) * (size_t) np->mem * 2 );
}


void sgs_PushVec2p( SGS_CTX, XGM_VT* v2f )
{
	XGM_VT* nv = (XGM_VT*) sgs_PushObjectIPA( C, sizeof(XGM_VT) * 2, xgm_vec2_iface );
	nv[ 0 ] = v2f[ 0 ];
	nv[ 1 ] = v2f[ 1 ];
}

void sgs_PushVec3p( SGS_CTX, XGM_VT* v3f )
{
	XGM_VT* nv = (XGM_VT*) sgs_PushObjectIPA( C, sizeof(XGM_VT) * 3, xgm_vec3_iface );
	nv[ 0 ] = v3f[ 0 ];
	nv[ 1 ] = v3f[ 1 ];
	nv[ 2 ] = v3f[ 2 ];
}

void sgs_PushVec4p( SGS_CTX, XGM_VT* v4f )
{
	XGM_VT* nv = (XGM_VT*) sgs_PushObjectIPA( C, sizeof(XGM_VT) * 4, xgm_vec4_iface );
	nv[ 0 ] = v4f[ 0 ];
	nv[ 1 ] = v4f[ 1 ];
	nv[ 2 ] = v4f[ 2 ];
	nv[ 3 ] = v4f[ 3 ];
}

void sgs_PushAABB2p( SGS_CTX, XGM_VT* v4f )
{
	XGM_VT* nv = (XGM_VT*) sgs_PushObjectIPA( C, sizeof(XGM_VT) * 4, xgm_aabb2_iface );
	nv[ 0 ] = v4f[ 0 ];
	nv[ 1 ] = v4f[ 1 ];
	nv[ 2 ] = v4f[ 2 ];
	nv[ 3 ] = v4f[ 3 ];
}

void sgs_PushColorp( SGS_CTX, XGM_VT* v4f )
{
	XGM_VT* nv = (XGM_VT*) sgs_PushObjectIPA( C, sizeof(XGM_VT) * 4, xgm_color_iface );
	nv[ 0 ] = v4f[ 0 ];
	nv[ 1 ] = v4f[ 1 ];
	nv[ 2 ] = v4f[ 2 ];
	nv[ 3 ] = v4f[ 3 ];
}

void sgs_PushColorvp( SGS_CTX, XGM_VT* vf, int numfloats )
{
	XGM_VT* nv = (XGM_VT*) sgs_PushObjectIPA( C, sizeof(XGM_VT) * 4, xgm_color_iface );
	if( numfloats == 0 ) nv[0] = nv[1] = nv[2] = nv[3] = 0;
	else if( numfloats == 1 ) nv[0] = nv[1] = nv[2] = nv[3] = vf[0];
	else if( numfloats == 2 ){ nv[0] = nv[1] = nv[2] = vf[0]; nv[3] = vf[1]; }
	else if( numfloats == 3 ){ nv[0] = vf[0]; nv[1] = vf[1]; nv[2] = vf[2]; nv[3] = 1; }
	else { nv[0] = vf[0]; nv[1] = vf[1]; nv[2] = vf[2]; nv[3] = vf[3]; }
}


SGSBOOL sgs_ParseVec2P( SGS_CTX, sgs_Variable* var, XGM_VT* v2f, int strict )
{
	if( !strict && ( var->type == SGS_VT_INT || var->type == SGS_VT_REAL ) )
	{
		v2f[0] = v2f[1] = (XGM_VT) sgs_GetRealP( C, var );
		return 1;
	}
	if( var->type != SGS_VT_OBJECT )
		return 0;
	
	if( sgs_IsObjectP( var, xgm_vec2_iface ) )
	{
		XGM_VT* hdr = (XGM_VT*) sgs_GetObjectDataP( var );
		v2f[0] = hdr[0];
		v2f[1] = hdr[1];
		return 1;
	}
	return 0;
}

SGSBOOL sgs_ParseVec3P( SGS_CTX, sgs_Variable* var, XGM_VT* v3f, int strict )
{
	if( !strict && ( var->type == SGS_VT_INT || var->type == SGS_VT_REAL ) )
	{
		v3f[0] = v3f[1] = v3f[2] = (XGM_VT) sgs_GetRealP( C, var );
		return 1;
	}
	if( var->type != SGS_VT_OBJECT )
		return 0;
	
	if( sgs_IsObjectP( var, xgm_vec3_iface ) )
	{
		XGM_VT* hdr = (XGM_VT*) sgs_GetObjectDataP( var );
		v3f[0] = hdr[0];
		v3f[1] = hdr[1];
		v3f[2] = hdr[2];
		return 1;
	}
	return 0;
}

SGSBOOL sgs_ParseVec4P( SGS_CTX, sgs_Variable* var, XGM_VT* v4f, int strict )
{
	if( !strict && ( var->type == SGS_VT_INT || var->type == SGS_VT_REAL ) )
	{
		v4f[0] = v4f[1] = v4f[2] = v4f[3] = (XGM_VT) sgs_GetRealP( C, var );
		return 1;
	}
	if( var->type != SGS_VT_OBJECT )
		return 0;
	
	if( sgs_IsObjectP( var, xgm_vec4_iface ) ||
		sgs_IsObjectP( var, xgm_color_iface ) )
	{
		XGM_VT* hdr = (XGM_VT*) sgs_GetObjectDataP( var );
		v4f[0] = hdr[0];
		v4f[1] = hdr[1];
		v4f[2] = hdr[2];
		v4f[3] = hdr[3];
		return 1;
	}
	return 0;
}

SGSBOOL sgs_ParseAABB2P( SGS_CTX, sgs_Variable* var, XGM_VT* v4f )
{
	if( sgs_IsObjectP( var, xgm_aabb2_iface ) )
	{
		XGM_VT* hdr = (XGM_VT*) sgs_GetObjectDataP( var );
		v4f[0] = hdr[0];
		v4f[1] = hdr[1];
		v4f[2] = hdr[2];
		v4f[3] = hdr[3];
		return 1;
	}
	return 0;
}

SGSBOOL sgs_ParseColorP( SGS_CTX, sgs_Variable* var, XGM_VT* v4f, int strict )
{
	return sgs_ParseVec4P( C, var, v4f, strict );
}

SGSBOOL sgs_ParseMat4P( SGS_CTX, sgs_Variable* var, XGM_VT* v16f )
{
	if( sgs_IsObjectP( var, xgm_mat4_iface ) )
	{
		XGM_VT* hdr = (XGM_VT*) sgs_GetObjectDataP( var );
		memcpy( v16f, hdr, sizeof(XGM_VT) * 16 );
		return 1;
	}
	return 0;
}

SGSBOOL sgs_ParseVec2ArrayP( SGS_CTX, sgs_Variable* var, XGM_VT** v2fa, sgs_SizeVal* osz )
{
	if( sgs_IsObjectP( var, xgm_vec2arr_iface ) )
	{
		xgm_vtarray* data = (xgm_vtarray*) sgs_GetObjectDataP( var );
		if( v2fa ) *v2fa = data->data;
		if( osz ) *osz = data->size;
		return 1;
	}
	return 0;
}


SGSBOOL sgs_ParseVec2( SGS_CTX, sgs_StkIdx item, XGM_VT* v2f, int strict )
{
	sgs_Variable tmp;
	return sgs_PeekStackItem( C, item, &tmp ) && sgs_ParseVec2P( C, &tmp, v2f, strict );
}

SGSBOOL sgs_ParseVec3( SGS_CTX, sgs_StkIdx item, XGM_VT* v3f, int strict )
{
	sgs_Variable tmp;
	return sgs_PeekStackItem( C, item, &tmp ) && sgs_ParseVec3P( C, &tmp, v3f, strict );
}

SGSBOOL sgs_ParseVec4( SGS_CTX, sgs_StkIdx item, XGM_VT* v4f, int strict )
{
	sgs_Variable tmp;
	return sgs_PeekStackItem( C, item, &tmp ) && sgs_ParseVec4P( C, &tmp, v4f, strict );
}

SGSBOOL sgs_ParseAABB2( SGS_CTX, sgs_StkIdx item, XGM_VT* v4f )
{
	sgs_Variable tmp;
	return sgs_PeekStackItem( C, item, &tmp ) && sgs_ParseAABB2P( C, &tmp, v4f );
}

SGSBOOL sgs_ParseColor( SGS_CTX, sgs_StkIdx item, XGM_VT* v4f, int strict )
{
	return sgs_ParseVec4( C, item, v4f, strict );
}

SGSBOOL sgs_ParseMat4( SGS_CTX, sgs_StkIdx item, XGM_VT* v16f )
{
	sgs_Variable tmp;
	return sgs_PeekStackItem( C, item, &tmp ) && sgs_ParseMat4P( C, &tmp, v16f );
}

SGSBOOL sgs_ParseVec2Array( SGS_CTX, sgs_StkIdx item, XGM_VT** v2fa, sgs_SizeVal* osz )
{
	sgs_Variable tmp;
	return sgs_PeekStackItem( C, item, &tmp ) && sgs_ParseVec2ArrayP( C, &tmp, v2fa, osz );
}


int sgs_ArgCheck_Vec2( SGS_CTX, int argid, va_list* args, int flags )
{
	XGM_VT* out = NULL;
	XGM_VT v[2];
	if( flags & SGS_LOADARG_WRITE )
		out = va_arg( *args, XGM_VT* );
	
	if( sgs_ParseVec2( C, argid, v, flags & SGS_LOADARG_STRICT ? 1 : 0 ) )
	{
		if( out )
		{
			out[ 0 ] = v[ 0 ];
			out[ 1 ] = v[ 1 ];
		}
		return 1;
	}
	if( flags & SGS_LOADARG_OPTIONAL )
		return 1;
	return sgs_ArgErrorExt( C, argid, 0, "vec2", flags & SGS_LOADARG_STRICT ? "strict " : "" );
}

int sgs_ArgCheck_Vec3( SGS_CTX, int argid, va_list* args, int flags )
{
	XGM_VT* out = NULL;
	XGM_VT v[3];
	if( flags & SGS_LOADARG_WRITE )
		out = va_arg( *args, XGM_VT* );
	
	if( sgs_ParseVec3( C, argid, v, flags & SGS_LOADARG_STRICT ? 1 : 0 ) )
	{
		if( out )
		{
			out[ 0 ] = v[ 0 ];
			out[ 1 ] = v[ 1 ];
			out[ 2 ] = v[ 2 ];
		}
		return 1;
	}
	if( flags & SGS_LOADARG_OPTIONAL )
		return 1;
	return sgs_ArgErrorExt( C, argid, 0, "vec3", flags & SGS_LOADARG_STRICT ? "strict " : "" );
}

static int sgs_ArgCheck_4F( SGS_CTX, int argid, va_list* args, int flags, const char* name )
{
	XGM_VT* out = NULL;
	XGM_VT v[4];
	if( flags & SGS_LOADARG_WRITE )
		out = va_arg( *args, XGM_VT* );
	
	if( sgs_ParseVec4( C, argid, v, flags & SGS_LOADARG_STRICT ? 1 : 0 ) )
	{
		if( out )
		{
			out[ 0 ] = v[ 0 ];
			out[ 1 ] = v[ 1 ];
			out[ 2 ] = v[ 2 ];
			out[ 3 ] = v[ 3 ];
		}
		return 1;
	}
	if( flags & SGS_LOADARG_OPTIONAL )
		return 1;
	return sgs_ArgErrorExt( C, argid, 0, name, flags & SGS_LOADARG_STRICT ? "strict " : "" );
}

int sgs_ArgCheck_Vec4( SGS_CTX, int argid, va_list* args, int flags )
{
	return sgs_ArgCheck_4F( C, argid, args, flags, "vec4" );
}

int sgs_ArgCheck_AABB2( SGS_CTX, int argid, va_list* args, int flags )
{
	XGM_VT* out = NULL;
	XGM_VT v[4];
	if( flags & SGS_LOADARG_WRITE )
		out = va_arg( *args, XGM_VT* );
	
	if( sgs_ParseAABB2( C, argid, v ) )
	{
		if( out )
		{
			out[ 0 ] = v[ 0 ];
			out[ 1 ] = v[ 1 ];
			out[ 2 ] = v[ 2 ];
			out[ 3 ] = v[ 3 ];
		}
		return 1;
	}
	if( flags & SGS_LOADARG_OPTIONAL )
		return 1;
	return sgs_ArgErrorExt( C, argid, 0, "aabb2", flags & SGS_LOADARG_STRICT ? "strict " : "" );
}

int sgs_ArgCheck_Color( SGS_CTX, int argid, va_list* args, int flags )
{
	return sgs_ArgCheck_4F( C, argid, args, flags, "color" );
}

int sgs_ArgCheck_Mat4( SGS_CTX, int argid, va_list* args, int flags )
{
	XGM_VT v[16];
	if( sgs_ParseMat4( C, argid, v ) )
	{
		if( flags & SGS_LOADARG_WRITE )
		{
			XGM_VT* out = va_arg( *args, XGM_VT* );
			memcpy( out, v, sizeof(v) );
		}
		return 1;
	}
	if( flags & SGS_LOADARG_OPTIONAL )
		return 1;
	return sgs_ArgErrorExt( C, argid, 0, "mat4", "" );
}

int sgs_ArgCheck_Vec2Array( SGS_CTX, int argid, va_list* args, int flags )
{
	if( sgs_ParseVec2Array( C, argid, NULL, NULL ) )
	{
		if( flags & SGS_LOADARG_WRITE )
		{
			xgm_vtarray** out = va_arg( *args, xgm_vtarray** );
			*out = (xgm_vtarray*) sgs_GetObjectData( C, argid );
		}
		return 1;
	}
	if( flags & SGS_LOADARG_OPTIONAL )
		return 1;
	return sgs_ArgErrorExt( C, argid, 0, "vec2array", "" );
}


static sgs_RegFuncConst xgm_fconsts[] =
{
	{ "vec2", xgm_vec2 },
	{ "vec2_dot", xgm_vec2_dot },
	
	{ "vec3", xgm_vec3 },
	{ "vec3_dot", xgm_vec3_dot },
	{ "vec3_cross", xgm_vec3_cross },
	
	{ "vec4", xgm_vec4 },
	{ "vec4_dot", xgm_vec4_dot },
	
	{ "aabb2", xgm_aabb2 },
	{ "aabb2v", xgm_aabb2v },
	{ "aabb2_intersect", xgm_aabb2_intersect },
	
	{ "color", xgm_color },
	
	{ "mat4", xgm_mat4 },
	
	{ "vec2array", xgm_vec2array },
};


SGS_APIFUNC int xgm_module_entry_point( SGS_CTX )
{
	sgs_RegFuncConsts( C, xgm_fconsts, sizeof(xgm_fconsts) / sizeof(xgm_fconsts[0]) );
	sgs_RegisterType( C, "vec2", xgm_vec2_iface );
	sgs_RegisterType( C, "vec3", xgm_vec3_iface );
	sgs_RegisterType( C, "vec4", xgm_vec4_iface );
	sgs_RegisterType( C, "aabb2", xgm_aabb2_iface );
	sgs_RegisterType( C, "color", xgm_color_iface );
	sgs_RegisterType( C, "vec4", xgm_vec4_iface );
	sgs_RegisterType( C, "vec2array", xgm_vec2arr_iface );
	return SGS_SUCCESS;
}


#ifdef SGS_COMPILE_MODULE
SGS_APIFUNC int sgscript_main( SGS_CTX )
{
	return xgm_module_entry_point( C );
}
#endif

