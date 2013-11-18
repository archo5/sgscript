

#include <stdio.h>

#include "sgsxgmath.h"


#define XGM_VT XGM_VECTOR_TYPE
#define XGM_WARNING( err ) sgs_Printf( C, SGS_WARNING, err );
#define XGM_OHDR XGM_VT* hdr = (XGM_VT*) data->data;
#define XGM_P2HDR xgm_poly2* poly = (xgm_poly2*) data->data;

#define XGM_VMUL_INNER2(a,b) ((a)[0]*(b)[0]+(a)[1]*(b)[1])
#define XGM_VMUL_INNER3(a,b) ((a)[0]*(b)[0]+(a)[1]*(b)[1]+(a)[2]*(b)[2])
#define XGM_VMUL_INNER4(a,b) ((a)[0]*(b)[0]+(a)[1]*(b)[1]+(a)[2]*(b)[2]+(a)[3]*(b)[3])

#define XGM_BB2_EXPAND_V2(b,v) \
	if( (b)[0] > (v)[0] ) (b)[0] = (v)[0]; \
	if( (b)[1] > (v)[1] ) (b)[1] = (v)[1]; \
	if( (b)[2] < (v)[0] ) (b)[2] = (v)[0]; \
	if( (b)[3] < (v)[1] ) (b)[3] = (v)[1];


static SGSBOOL xgm_ParseVT( SGS_CTX, int item, XGM_VT* out )
{
	sgs_Real val;
	if( sgs_ParseReal( C, item, &val ) )
	{
		*out = val;
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
	
	c = cos( angle );
	s = sin( angle );
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
	else if( type == SGS_CONVOP_TOTYPE )
	{
		sgs_PushString( C, "vec2" );
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

static int xgm_v2_getindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	char* str;
	sgs_SizeVal size;
	XGM_OHDR;
	
	if( !sgs_ParseString( C, 0, &str, &size ) )
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
			lensq = 1.0 / sqrt( lensq );
			sgs_PushVec2( C, hdr[0] * lensq, hdr[1] * lensq );
		}
		else
			sgs_PushVec2( C, 0, 0 );
		return SGS_SUCCESS;
	}
	if( !strcmp( str, "angle" ) ){ sgs_PushReal( C, atan2( hdr[0], hdr[1] ) ); return SGS_SUCCESS; }
	if( !strcmp( str, "perp" ) ){ sgs_PushVec2( C, -hdr[1], hdr[0] ); return SGS_SUCCESS; }
	if( !strcmp( str, "perp2" ) ){ sgs_PushVec2( C, hdr[1], -hdr[0] ); return SGS_SUCCESS; }
	if( !strcmp( str, "rotate" ) ){ sgs_PushCFunction( C, xgm_v2m_rotate ); return SGS_SUCCESS; }
	return SGS_ENOTFND;
}

static int xgm_v2_setindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	char* str;
	sgs_SizeVal size;
	sgs_Real val;
	XGM_OHDR;
	
	if( !sgs_ParseString( C, 0, &str, &size ) )
		return SGS_EINVAL;
	if( !sgs_ParseReal( C, 1, &val ) )
		return SGS_EINVAL;
	if( !strcmp( str, "x" ) ){ hdr[ 0 ] = val; return SGS_SUCCESS; }
	if( !strcmp( str, "y" ) ){ hdr[ 1 ] = val; return SGS_SUCCESS; }
	return SGS_ENOTFND;
}

static int xgm_v2_expr( SGS_CTX, sgs_VarObj* data, int type )
{
	if( type == SGS_EOP_ADD ||
		type == SGS_EOP_SUB ||
		type == SGS_EOP_MUL ||
		type == SGS_EOP_DIV ||
		type == SGS_EOP_MOD )
	{
		XGM_VT r[ 2 ], v1[ 2 ], v2[ 2 ];
		if( !sgs_ParseVec2( C, 0, v1, 0 ) || !sgs_ParseVec2( C, 1, v2, 0 ) )
			return SGS_EINVAL;
		
		if( ( type == SGS_EOP_DIV || type == SGS_EOP_MOD ) &&
			( v2[0] == 0 || v2[1] == 0 ) )
		{
			const char* errstr = type == SGS_EOP_DIV ?
				"vec2 operator '/' - division by zero" :
				"vec2 operator '%' - modulo by zero";
			sgs_Printf( C, SGS_ERROR, errstr );
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
			{ r[0] = fmod( v1[0], v2[0] ); r[1] = fmod( v1[1], v2[1] ); }
		
		sgs_PushVec2p( C, r );
		return SGS_SUCCESS;
	}
	else if( type == SGS_EOP_COMPARE )
	{
		XGM_VT *v1, *v2;
		if( !sgs_IsObject( C, 0, xgm_vec2_iface ) ||
			!sgs_IsObject( C, 1, xgm_vec2_iface ) )
			return SGS_EINVAL;
		
		v1 = (XGM_VT*) sgs_GetObjectData( C, 0 );
		v2 = (XGM_VT*) sgs_GetObjectData( C, 1 );
		
		if( v1[0] != v2[0] )
			return v1[0] - v2[0];
		return v1[1] - v2[1];
	}
	else if( type == SGS_EOP_NEGATE )
	{
		XGM_OHDR;
		sgs_PushVec2( C, -hdr[0], -hdr[1] );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int xgm_v2_serialize( SGS_CTX, sgs_VarObj* data, int unused )
{
	XGM_OHDR;
	sgs_PushReal( C, hdr[0] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	sgs_PushReal( C, hdr[1] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	return sgs_SerializeObject( C, 2, "vec2" );
}

static int xgm_v2_dump( SGS_CTX, sgs_VarObj* data, int unused )
{
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
	else if( type == SGS_CONVOP_TOTYPE )
	{
		sgs_PushString( C, "vec3" );
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

static int xgm_v3_getindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	char* str;
	sgs_SizeVal size;
	XGM_OHDR;
	
	if( !sgs_ParseString( C, 0, &str, &size ) )
		return SGS_EINVAL;
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
			lensq = 1.0 / sqrt( lensq );
			sgs_PushVec3( C, hdr[0] * lensq, hdr[1] * lensq, hdr[2] * lensq );
		}
		else
			sgs_PushVec3( C, 0, 0, 0 );
		return SGS_SUCCESS;
	}
	return SGS_ENOTFND;
}

static int xgm_v3_setindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	char* str;
	sgs_SizeVal size;
	sgs_Real val;
	XGM_OHDR;
	
	if( !sgs_ParseString( C, 0, &str, &size ) )
		return SGS_EINVAL;
	if( !sgs_ParseReal( C, 1, &val ) )
		return SGS_EINVAL;
	if( !strcmp( str, "x" ) ){ hdr[0] = val; return SGS_SUCCESS; }
	if( !strcmp( str, "y" ) ){ hdr[1] = val; return SGS_SUCCESS; }
	if( !strcmp( str, "z" ) ){ hdr[2] = val; return SGS_SUCCESS; }
	return SGS_ENOTFND;
}

static int xgm_v3_expr( SGS_CTX, sgs_VarObj* data, int type )
{
	if( type == SGS_EOP_ADD ||
		type == SGS_EOP_SUB ||
		type == SGS_EOP_MUL ||
		type == SGS_EOP_DIV ||
		type == SGS_EOP_MOD )
	{
		XGM_VT r[3], v1[3], v2[3];
		if( !sgs_ParseVec3( C, 0, v1, 0 ) || !sgs_ParseVec3( C, 1, v2, 0 ) )
			return SGS_EINVAL;
		
		if( ( type == SGS_EOP_DIV || type == SGS_EOP_MOD ) &&
			( v2[0] == 0 || v2[1] == 0 || v2[2] == 0 ) )
		{
			const char* errstr = type == SGS_EOP_DIV ?
				"vec2 operator '/' - division by zero" :
				"vec2 operator '%' - modulo by zero";
			sgs_Printf( C, SGS_ERROR, errstr );
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
			r[0] = fmod( v1[0], v2[0] );
			r[1] = fmod( v1[1], v2[1] );
			r[2] = fmod( v1[2], v2[2] );
		}
		
		sgs_PushVec3p( C, r );
		return SGS_SUCCESS;
	}
	else if( type == SGS_EOP_COMPARE )
	{
		XGM_VT *v1, *v2;
		if( !sgs_IsObject( C, 0, xgm_vec3_iface ) ||
			!sgs_IsObject( C, 1, xgm_vec3_iface ) )
			return SGS_EINVAL;
		
		v1 = (XGM_VT*) sgs_GetObjectData( C, 0 );
		v2 = (XGM_VT*) sgs_GetObjectData( C, 1 );
		
		if( v1[0] != v2[0] )
			return v1[0] - v2[0];
		if( v1[1] != v2[1] )
			return v1[1] - v2[1];
		return v1[2] - v2[2];
	}
	else if( type == SGS_EOP_NEGATE )
	{
		XGM_OHDR;
		sgs_PushVec3( C, -hdr[0], -hdr[1], -hdr[2] );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int xgm_v3_serialize( SGS_CTX, sgs_VarObj* data, int unused )
{
	XGM_OHDR;
	sgs_PushReal( C, hdr[0] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	sgs_PushReal( C, hdr[1] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	sgs_PushReal( C, hdr[2] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	return sgs_SerializeObject( C, 2, "vec3" );
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
	else if( type == SGS_CONVOP_TOTYPE )
	{
		sgs_PushString( C, "vec4" );
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

static int xgm_v4_getindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	char* str;
	sgs_SizeVal size;
	XGM_OHDR;
	
	if( !sgs_ParseString( C, 0, &str, &size ) )
		return SGS_EINVAL;
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
			lensq = 1.0 / sqrt( lensq );
			sgs_PushVec4( C, hdr[0] * lensq, hdr[1] * lensq, hdr[2] * lensq, hdr[3] * lensq );
		}
		else
			sgs_PushVec4( C, 0, 0, 0, 0 );
		return SGS_SUCCESS;
	}
	return SGS_ENOTFND;
}

static int xgm_v4_setindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	char* str;
	sgs_SizeVal size;
	sgs_Real val;
	XGM_OHDR;
	
	if( !sgs_ParseString( C, 0, &str, &size ) )
		return SGS_EINVAL;
	if( !sgs_ParseReal( C, 1, &val ) )
		return SGS_EINVAL;
	if( !strcmp( str, "x" ) ){ hdr[0] = val; return SGS_SUCCESS; }
	if( !strcmp( str, "y" ) ){ hdr[1] = val; return SGS_SUCCESS; }
	if( !strcmp( str, "z" ) ){ hdr[2] = val; return SGS_SUCCESS; }
	if( !strcmp( str, "w" ) ){ hdr[3] = val; return SGS_SUCCESS; }
	return SGS_ENOTFND;
}

static int xgm_v4_expr( SGS_CTX, sgs_VarObj* data, int type )
{
	if( type == SGS_EOP_ADD ||
		type == SGS_EOP_SUB ||
		type == SGS_EOP_MUL ||
		type == SGS_EOP_DIV ||
		type == SGS_EOP_MOD )
	{
		XGM_VT r[4], v1[4], v2[4];
		if( !sgs_ParseVec4( C, 0, v1, 0 ) || !sgs_ParseVec4( C, 1, v2, 0 ) )
			return SGS_EINVAL;
		
		if( ( type == SGS_EOP_DIV || type == SGS_EOP_MOD ) &&
			( v2[0] == 0 || v2[1] == 0 || v2[2] == 0 || v2[3] == 0 ) )
		{
			const char* errstr = type == SGS_EOP_DIV ?
				"vec2 operator '/' - division by zero" :
				"vec2 operator '%' - modulo by zero";
			sgs_Printf( C, SGS_ERROR, errstr );
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
			r[0] = fmod( v1[0], v2[0] );
			r[1] = fmod( v1[1], v2[1] );
			r[2] = fmod( v1[2], v2[2] );
			r[3] = fmod( v1[3], v2[3] );
		}
		
		sgs_PushVec4p( C, r );
		return SGS_SUCCESS;
	}
	else if( type == SGS_EOP_COMPARE )
	{
		XGM_VT *v1, *v2;
		if( !sgs_IsObject( C, 0, xgm_vec4_iface ) ||
			!sgs_IsObject( C, 1, xgm_vec4_iface ) )
			return SGS_EINVAL;
		
		v1 = (XGM_VT*) sgs_GetObjectData( C, 0 );
		v2 = (XGM_VT*) sgs_GetObjectData( C, 1 );
		
		if( v1[0] != v2[0] ) return v1[0] - v2[0];
		if( v1[1] != v2[1] ) return v1[1] - v2[1];
		if( v1[2] != v2[2] ) return v1[2] - v2[2];
		return v1[3] - v2[3];
	}
	else if( type == SGS_EOP_NEGATE )
	{
		XGM_OHDR;
		sgs_PushVec4( C, -hdr[0], -hdr[1], -hdr[2], -hdr[3] );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int xgm_v4_serialize( SGS_CTX, sgs_VarObj* data, int unused )
{
	XGM_OHDR;
	sgs_PushReal( C, hdr[0] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	sgs_PushReal( C, hdr[1] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	sgs_PushReal( C, hdr[2] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	sgs_PushReal( C, hdr[3] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	return sgs_SerializeObject( C, 2, "vec4" );
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
	else if( type == SGS_CONVOP_TOTYPE )
	{
		sgs_PushString( C, "aabb2" );
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

static int xgm_b2_getindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	char* str;
	sgs_SizeVal size;
	XGM_OHDR;
	
	if( !sgs_ParseString( C, 0, &str, &size ) )
		return SGS_EINVAL;
	if( !strcmp( str, "x1" ) ){ sgs_PushReal( C, hdr[0] ); return SGS_SUCCESS; }
	if( !strcmp( str, "y1" ) ){ sgs_PushReal( C, hdr[1] ); return SGS_SUCCESS; }
	if( !strcmp( str, "x2" ) ){ sgs_PushReal( C, hdr[2] ); return SGS_SUCCESS; }
	if( !strcmp( str, "y2" ) ){ sgs_PushReal( C, hdr[3] ); return SGS_SUCCESS; }
	if( !strcmp( str, "p1" ) ){ sgs_PushVec2p( C, hdr ); return SGS_SUCCESS; }
	if( !strcmp( str, "p2" ) ){ sgs_PushVec2p( C, hdr + 2 ); return SGS_SUCCESS; }
	if( !strcmp( str, "width" ) ){ sgs_PushReal( C, hdr[2] - hdr[0] ); return SGS_SUCCESS; }
	if( !strcmp( str, "height" ) ){ sgs_PushReal( C, hdr[3] - hdr[1] ); return SGS_SUCCESS; }
	if( !strcmp( str, "center" ) )
	{
		sgs_PushVec2( C, (hdr[0]+hdr[2])*0.5, (hdr[1]+hdr[3])*0.5 );
		return SGS_SUCCESS;
	}
	if( !strcmp( str, "area" ) )
	{
		sgs_PushReal( C, (hdr[2] - hdr[0]) * (hdr[3] - hdr[1]) );
		return SGS_SUCCESS;
	}
	if( !strcmp( str, "valid" ) )
	{
		sgs_PushBool( C, hdr[2] >= hdr[0] && hdr[3] >= hdr[1] );
		return SGS_SUCCESS;
	}
	if( !strcmp( str, "expand" ) )
	{
		sgs_PushCFunction( C, xgm_aabb2_expand );
		return SGS_SUCCESS;
	}
	return SGS_ENOTFND;
}

static int xgm_b2_setindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	char* str;
	sgs_SizeVal size;
	XGM_OHDR;
	
	if( !sgs_ParseString( C, 0, &str, &size ) )
		return SGS_EINVAL;
	if( !strcmp( str, "x1" ) ) return xgm_ParseVT( C, 1, &hdr[0] ) ? SGS_SUCCESS : SGS_EINVAL;
	if( !strcmp( str, "y1" ) ) return xgm_ParseVT( C, 1, &hdr[1] ) ? SGS_SUCCESS : SGS_EINVAL;
	if( !strcmp( str, "x2" ) ) return xgm_ParseVT( C, 1, &hdr[2] ) ? SGS_SUCCESS : SGS_EINVAL;
	if( !strcmp( str, "y2" ) ) return xgm_ParseVT( C, 1, &hdr[3] ) ? SGS_SUCCESS : SGS_EINVAL;
	if( !strcmp( str, "p1" ) ) return sgs_ParseVec2( C, 1, hdr, 1 ) ? SGS_SUCCESS : SGS_EINVAL;
	if( !strcmp( str, "p2" ) ) return sgs_ParseVec2( C, 1, hdr + 2, 1 ) ? SGS_SUCCESS : SGS_EINVAL;
	return SGS_ENOTFND;
}

static int xgm_b2_serialize( SGS_CTX, sgs_VarObj* data, int unused )
{
	XGM_OHDR;
	sgs_PushReal( C, hdr[0] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	sgs_PushReal( C, hdr[1] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	sgs_PushReal( C, hdr[2] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	sgs_PushReal( C, hdr[3] ); if( sgs_Serialize( C ) ) return SGS_EINPROC;
	return sgs_SerializeObject( C, 2, "aabb2" );
}

static int xgm_b2_dump( SGS_CTX, sgs_VarObj* data, int unused )
{
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



/*  2 D   P O L Y  */

static int xgm_p2_destruct( SGS_CTX, sgs_VarObj* data, int dco )
{
	XGM_P2HDR;
	if( poly->data )
		sgs_Dealloc( poly->data );
	return SGS_SUCCESS;
}

static int xgm_p2_getindex_aabb( SGS_CTX, xgm_poly2* poly )
{
	if( !poly->size )
	{
		XGM_WARNING( "cannot get AABB of empty poly2" );
		return SGS_EINPROC;
	}
	else
	{
		sgs_SizeVal i;
		XGM_VT bb[4] = {
			poly->data[0], poly->data[1], poly->data[0], poly->data[1]
		};
		for( i = 2; i < poly->size; i += 2 )
		{
			XGM_VT* pp = poly->data + i;
			if( bb[0] > pp[0] ) bb[0] = pp[0];
			if( bb[1] > pp[1] ) bb[1] = pp[1];
			if( bb[2] < pp[0] ) bb[2] = pp[0];
			if( bb[3] < pp[1] ) bb[3] = pp[1];
		}
		sgs_PushAABB2p( C, bb );
		return SGS_SUCCESS;
	}
}

static int xgm_p2_getindex( SGS_CTX, sgs_VarObj* data, int isprop )
{
	sgs_Int idx;
	XGM_P2HDR;
	
	if( !isprop && sgs_ParseInt( C, 0, &idx ) )
	{
		if( idx < 0 || idx >= poly->size )
			return SGS_EBOUNDS;
		sgs_PushVec2p( C, poly->data + idx * 2 );
		return SGS_SUCCESS;
	}
	
	if( isprop )
	{
		char* str;
		sgs_SizeVal size;
		if( !sgs_ParseString( C, 0, &str, &size ) )
			return SGS_EINVAL;
		
		if( !strcmp( str, "aabb" ) ) return xgm_p2_getindex_aabb( C, poly );
	}
	
	return SGS_ENOTFND;
}



sgs_ObjCallback xgm_vec2_iface[] =
{
	SGS_OP_GETINDEX, xgm_v2_getindex,
	SGS_OP_SETINDEX, xgm_v2_setindex,
	SGS_OP_EXPR, xgm_v2_expr,
	SGS_OP_CONVERT, xgm_v2_convert,
	SGS_OP_SERIALIZE, xgm_v2_serialize,
	SGS_OP_DUMP, xgm_v2_dump,
	SGS_OP_END
};

sgs_ObjCallback xgm_vec3_iface[] =
{
	SGS_OP_GETINDEX, xgm_v3_getindex,
	SGS_OP_SETINDEX, xgm_v3_setindex,
	SGS_OP_EXPR, xgm_v3_expr,
	SGS_OP_CONVERT, xgm_v3_convert,
	SGS_OP_SERIALIZE, xgm_v3_serialize,
	SGS_OP_DUMP, xgm_v3_dump,
	SGS_OP_END
};

sgs_ObjCallback xgm_vec4_iface[] =
{
	SGS_OP_GETINDEX, xgm_v4_getindex,
	SGS_OP_SETINDEX, xgm_v4_setindex,
	SGS_OP_EXPR, xgm_v4_expr,
	SGS_OP_CONVERT, xgm_v4_convert,
	SGS_OP_SERIALIZE, xgm_v4_serialize,
	SGS_OP_DUMP, xgm_v4_dump,
	SGS_OP_END
};

sgs_ObjCallback xgm_aabb2_iface[] =
{
	SGS_OP_GETINDEX, xgm_b2_getindex,
	SGS_OP_SETINDEX, xgm_b2_setindex,
	SGS_OP_CONVERT, xgm_b2_convert,
	SGS_OP_SERIALIZE, xgm_b2_serialize,
	SGS_OP_DUMP, xgm_b2_dump,
	SGS_OP_END
};

sgs_ObjCallback xgm_poly2_iface[] =
{
	SGS_OP_DESTRUCT, xgm_p2_destruct,
	SGS_OP_GETINDEX, xgm_p2_getindex,
	SGS_OP_END
};


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

void sgs_PushPoly2( SGS_CTX, XGM_VT* v2fn, int numverts )
{
	xgm_poly2* np = (xgm_poly2*) sgs_PushObjectIPA( C, sizeof(xgm_poly2), xgm_poly2_iface );
	np->size = numverts;
	np->mem = numverts;
	np->data = numverts ? sgs_Alloc_n( XGM_VT, np->mem * 2 ) : NULL;
	memcpy( np->data, v2fn, sizeof( XGM_VT ) * np->mem * 2 );
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


int sgs_ParseVec2( SGS_CTX, int pos, XGM_VT* v2f, int strict )
{
	int ty = sgs_ItemType( C, pos );
	if( !strict && ( ty == SGS_VT_INT || ty == SGS_VT_REAL ) )
	{
		v2f[0] = v2f[1] = sgs_GetReal( C, pos );
		return 1;
	}
	if( sgs_ItemType( C, pos ) != SGS_VT_OBJECT )
		return 0;
	
	if( sgs_IsObject( C, pos, xgm_vec2_iface ) )
	{
		XGM_VT* hdr = (XGM_VT*) sgs_GetObjectData( C, pos );
		v2f[0] = hdr[0];
		v2f[1] = hdr[1];
		return 1;
	}
	return 0;
}

int sgs_ParseVec3( SGS_CTX, int pos, XGM_VT* v3f, int strict )
{
	int ty = sgs_ItemType( C, pos );
	if( !strict && ( ty == SGS_VT_INT || ty == SGS_VT_REAL ) )
	{
		v3f[0] = v3f[1] = v3f[2] = sgs_GetReal( C, pos );
		return 1;
	}
	if( sgs_ItemType( C, pos ) != SGS_VT_OBJECT )
		return 0;
	
	if( sgs_IsObject( C, pos, xgm_vec3_iface ) )
	{
		XGM_VT* hdr = (XGM_VT*) sgs_GetObjectData( C, pos );
		v3f[0] = hdr[0];
		v3f[1] = hdr[1];
		v3f[2] = hdr[2];
		return 1;
	}
	return 0;
}

int sgs_ParseVec4( SGS_CTX, int pos, XGM_VT* v4f, int strict )
{
	int ty = sgs_ItemType( C, pos );
	if( !strict && ( ty == SGS_VT_INT || ty == SGS_VT_REAL ) )
	{
		v4f[0] = v4f[1] = v4f[2] = v4f[3] = sgs_GetReal( C, pos );
		return 1;
	}
	if( sgs_ItemType( C, pos ) != SGS_VT_OBJECT )
		return 0;
	
	if( sgs_IsObject( C, pos, xgm_vec4_iface ) )
	{
		XGM_VT* hdr = (XGM_VT*) sgs_GetObjectData( C, pos );
		v4f[0] = hdr[0];
		v4f[1] = hdr[1];
		v4f[2] = hdr[2];
		v4f[3] = hdr[3];
		return 1;
	}
	return 0;
}

int sgs_ParseAABB2( SGS_CTX, int pos, XGM_VT* v4f )
{
	if( sgs_IsObject( C, pos, xgm_aabb2_iface ) )
	{
		XGM_VT* hdr = (XGM_VT*) sgs_GetObjectData( C, pos );
		v4f[0] = hdr[0];
		v4f[1] = hdr[1];
		v4f[2] = hdr[2];
		v4f[3] = hdr[3];
		return 1;
	}
	return 0;
}


int sgs_ArgCheck_Vec2( SGS_CTX, int argid, va_list* args, int flags )
{
	XGM_VT* out = NULL;
	XGM_VT v[2];
	if( !( flags & SGS_LOADARG_NOWRITE ) )
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
	return sgs_ArgErrorExt( C, argid, 0, "vec2", "" );
}

int sgs_ArgCheck_Vec3( SGS_CTX, int argid, va_list* args, int flags )
{
	XGM_VT* out = NULL;
	XGM_VT v[3];
	if( !( flags & SGS_LOADARG_NOWRITE ) )
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
	return sgs_ArgErrorExt( C, argid, 0, "vec3", "" );
}

int sgs_ArgCheck_Vec4( SGS_CTX, int argid, va_list* args, int flags )
{
	XGM_VT* out = NULL;
	XGM_VT v[4];
	if( !( flags & SGS_LOADARG_NOWRITE ) )
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
	return sgs_ArgErrorExt( C, argid, 0, "vec4", "" );
}

int sgs_ArgCheck_AABB2( SGS_CTX, int argid, va_list* args, int flags )
{
	XGM_VT* out = NULL;
	XGM_VT v[4];
	if( !( flags & SGS_LOADARG_NOWRITE ) )
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
	return sgs_ArgErrorExt( C, argid, 0, "aabb2", "" );
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
};


SGS_APIFUNC int xgm_module_entry_point( SGS_CTX )
{
	sgs_RegFuncConsts( C, xgm_fconsts, sizeof(xgm_fconsts) / sizeof(xgm_fconsts[0]) );
	sgs_RegisterType( C, "vec2", xgm_vec2_iface );
	sgs_RegisterType( C, "vec3", xgm_vec3_iface );
	sgs_RegisterType( C, "vec4", xgm_vec4_iface );
	sgs_RegisterType( C, "aabb2", xgm_aabb2_iface );
	return SGS_SUCCESS;
}


#ifdef SGS_COMPILE_MODULE
SGS_APIFUNC int sgscript_main( SGS_CTX )
{
	return xgm_module_entry_point( C );
}
#endif

