

#ifndef SGSXGMATH_H
#define SGSXGMATH_H


#ifdef __cplusplus
extern "C" {
#endif


#include <math.h>
#include <stdarg.h>

#ifndef HEADER_SGSCRIPT_H
# define HEADER_SGSCRIPT_H <sgscript.h>
#endif
#include HEADER_SGSCRIPT_H


#ifndef XGM_VT
#define XGM_VT float
#endif

#define XGM_SMALL_VT 0.0001f


typedef struct _xgm_vtarray
{
	XGM_VT* data;
	sgs_SizeVal size;
	sgs_SizeVal mem;
}
xgm_vtarray;


extern sgs_ObjInterface xgm_vec2_iface[1];
extern sgs_ObjInterface xgm_vec3_iface[1];
extern sgs_ObjInterface xgm_vec4_iface[1];
extern sgs_ObjInterface xgm_aabb2_iface[1];
extern sgs_ObjInterface xgm_aabb3_iface[1];
extern sgs_ObjInterface xgm_color_iface[1];
extern sgs_ObjInterface xgm_quat_iface[1];
extern sgs_ObjInterface xgm_mat3_iface[1];
extern sgs_ObjInterface xgm_mat4_iface[1];
extern sgs_ObjInterface xgm_floatarr_iface[1];

SGS_APIFUNC SGSONE sgs_CreateVec2( SGS_CTX, sgs_Variable* var, XGM_VT x, XGM_VT y );
SGS_APIFUNC SGSONE sgs_CreateVec3( SGS_CTX, sgs_Variable* var, XGM_VT x, XGM_VT y, XGM_VT z );
SGS_APIFUNC SGSONE sgs_CreateVec4( SGS_CTX, sgs_Variable* var, XGM_VT x, XGM_VT y, XGM_VT z, XGM_VT w );
SGS_APIFUNC SGSONE sgs_CreateAABB2( SGS_CTX, sgs_Variable* var, XGM_VT x1, XGM_VT y1, XGM_VT x2, XGM_VT y2 );
SGS_APIFUNC SGSONE sgs_CreateAABB3( SGS_CTX, sgs_Variable* var, const XGM_VT* v3a, const XGM_VT* v3b );
SGS_APIFUNC SGSONE sgs_CreateColor( SGS_CTX, sgs_Variable* var, XGM_VT r, XGM_VT g, XGM_VT b, XGM_VT a );
SGS_APIFUNC SGSONE sgs_CreateQuat( SGS_CTX, sgs_Variable* var, XGM_VT x, XGM_VT y, XGM_VT z, XGM_VT w );
SGS_APIFUNC SGSONE sgs_CreateMat3( SGS_CTX, sgs_Variable* var, const XGM_VT* v9f, int transpose );
SGS_APIFUNC SGSONE sgs_CreateMat4( SGS_CTX, sgs_Variable* var, const XGM_VT* v16f, int transpose );
SGS_APIFUNC SGSONE sgs_CreateFloatArray( SGS_CTX, sgs_Variable* var, const XGM_VT* vfn, sgs_SizeVal size );

SGS_APIFUNC SGSONE sgs_CreateVec2p( SGS_CTX, sgs_Variable* var, const XGM_VT* v2f );
SGS_APIFUNC SGSONE sgs_CreateVec3p( SGS_CTX, sgs_Variable* var, const XGM_VT* v3f );
SGS_APIFUNC SGSONE sgs_CreateVec4p( SGS_CTX, sgs_Variable* var, const XGM_VT* v4f );
SGS_APIFUNC SGSONE sgs_CreateAABB2p( SGS_CTX, sgs_Variable* var, const XGM_VT* v4f );
SGS_APIFUNC SGSONE sgs_CreateAABB3p( SGS_CTX, sgs_Variable* var, const XGM_VT* v6f );
SGS_APIFUNC SGSONE sgs_CreateColorp( SGS_CTX, sgs_Variable* var, const XGM_VT* v4f );
SGS_APIFUNC SGSONE sgs_CreateColorvp( SGS_CTX, sgs_Variable* var, const XGM_VT* vf, int numfloats );
SGS_APIFUNC SGSONE sgs_CreateQuatp( SGS_CTX, sgs_Variable* var, const XGM_VT* v4f );

SGS_APIFUNC SGSBOOL sgs_ParseVT( SGS_CTX, sgs_StkIdx item, XGM_VT* out );
SGS_APIFUNC SGSBOOL sgs_ParseVTP( SGS_CTX, sgs_Variable* var, XGM_VT* out );

SGS_APIFUNC SGSBOOL sgs_ParseVec2P( SGS_CTX, sgs_Variable* var, XGM_VT* v2f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseVec3P( SGS_CTX, sgs_Variable* var, XGM_VT* v3f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseVec4P( SGS_CTX, sgs_Variable* var, XGM_VT* v4f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseAABB2P( SGS_CTX, sgs_Variable* var, XGM_VT* v4f );
SGS_APIFUNC SGSBOOL sgs_ParseAABB3P( SGS_CTX, sgs_Variable* var, XGM_VT* v6f );
SGS_APIFUNC SGSBOOL sgs_ParseColorP( SGS_CTX, sgs_Variable* var, XGM_VT* v4f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseQuatP( SGS_CTX, sgs_Variable* var, XGM_VT* v4f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseMat3P( SGS_CTX, sgs_Variable* var, XGM_VT* v9f );
SGS_APIFUNC SGSBOOL sgs_ParseMat4P( SGS_CTX, sgs_Variable* var, XGM_VT* v16f );
SGS_APIFUNC SGSBOOL sgs_ParseFloatArrayP( SGS_CTX, sgs_Variable* var, XGM_VT** vfa, sgs_SizeVal* osz );

SGS_APIFUNC SGSBOOL sgs_ParseVec2( SGS_CTX, sgs_StkIdx item, XGM_VT* v2f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseVec3( SGS_CTX, sgs_StkIdx item, XGM_VT* v3f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseVec4( SGS_CTX, sgs_StkIdx item, XGM_VT* v4f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseAABB2( SGS_CTX, sgs_StkIdx item, XGM_VT* v4f );
SGS_APIFUNC SGSBOOL sgs_ParseAABB3( SGS_CTX, sgs_StkIdx item, XGM_VT* v6f );
SGS_APIFUNC SGSBOOL sgs_ParseColor( SGS_CTX, sgs_StkIdx item, XGM_VT* v4f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseQuat( SGS_CTX, sgs_StkIdx item, XGM_VT* v4f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseMat3( SGS_CTX, sgs_StkIdx item, XGM_VT* v9f );
SGS_APIFUNC SGSBOOL sgs_ParseMat4( SGS_CTX, sgs_StkIdx item, XGM_VT* v16f );
SGS_APIFUNC SGSBOOL sgs_ParseFloatArray( SGS_CTX, sgs_StkIdx item, XGM_VT** vfa, sgs_SizeVal* osz );

SGS_APIFUNC int sgs_ArgCheck_Vec2( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_Vec3( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_Vec4( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_AABB2( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_AABB3( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_Color( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_Quat( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_Mat3( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_Mat4( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_FloatArray( SGS_CTX, int argid, va_list* args, int flags );


SGS_APIFUNC int xgm_module_entry_point( SGS_CTX );


/* utility macros */
#define SGS_RETURN_VEC2( x, y ) return sgs_CreateVec2( C, NULL, (XGM_VT)(x), (XGM_VT)(y) )
#define SGS_RETURN_VEC3( x, y, z ) return sgs_CreateVec3( C, NULL, (XGM_VT)(x), (XGM_VT)(y), (XGM_VT)(z) )
#define SGS_RETURN_VEC4( x, y, z, w ) return sgs_CreateVec4( C, NULL, (XGM_VT)(x), (XGM_VT)(y), (XGM_VT)(z), (XGM_VT)(w) )
#define SGS_RETURN_AABB2( x1, y1, x2, y2 ) return sgs_CreateAABB2( C, NULL, (XGM_VT)(x1), (XGM_VT)(y1), (XGM_VT)(x2), (XGM_VT)(y2) )
#define SGS_RETURN_AABB3( v1, v2 ) return sgs_CreateAABB2( C, NULL, v1, v2 )
#define SGS_RETURN_COLOR( r, g, b, a ) return sgs_CreateColor( C, NULL, (XGM_VT)(r), (XGM_VT)(g), (XGM_VT)(b), (XGM_VT)(a) )
#define SGS_RETURN_QUAT( x, y, z, w ) return sgs_CreateQuat( C, NULL, (XGM_VT)(x), (XGM_VT)(y), (XGM_VT)(z), (XGM_VT)(w) )
#define SGS_RETURN_VEC2P( value ) return sgs_CreateVec2p( C, NULL, value )
#define SGS_RETURN_VEC3P( value ) return sgs_CreateVec3p( C, NULL, value )
#define SGS_RETURN_VEC4P( value ) return sgs_CreateVec4p( C, NULL, value )
#define SGS_RETURN_AABB2P( value ) return sgs_CreateAABB2p( C, NULL, value )
#define SGS_RETURN_AABB3P( value ) return sgs_CreateAABB3p( C, NULL, value )
#define SGS_RETURN_COLORP( value ) return sgs_CreateColorp( C, NULL, value )
#define SGS_RETURN_QUATP( value ) return sgs_CreateQuatp( C, NULL, value )
#define SGS_RETURN_MAT3( value ) return sgs_CreateMat3( C, NULL, value, 0 )
#define SGS_RETURN_MAT3_TRANSPOSED( value ) return sgs_CreateMat3( C, NULL, value, 1 )
#define SGS_RETURN_MAT4( value ) return sgs_CreateMat4( C, NULL, value, 0 )
#define SGS_RETURN_MAT4_TRANSPOSED( value ) return sgs_CreateMat4( C, NULL, value, 1 )

#define SGS_PARSE_VEC2( outptr, strict ) { return sgs_ParseVec2P( C, val, outptr, strict ) ? SGS_SUCCESS : SGS_EINVAL; }
#define SGS_PARSE_VEC3( outptr, strict ) { return sgs_ParseVec3P( C, val, outptr, strict ) ? SGS_SUCCESS : SGS_EINVAL; }
#define SGS_PARSE_VEC4( outptr, strict ) { return sgs_ParseVec4P( C, val, outptr, strict ) ? SGS_SUCCESS : SGS_EINVAL; }
#define SGS_PARSE_AABB2( outptr ) { return sgs_ParseAABB2P( C, val, outptr ) ? SGS_SUCCESS : SGS_EINVAL; }
#define SGS_PARSE_AABB3( outptr ) { return sgs_ParseAABB3P( C, val, outptr ) ? SGS_SUCCESS : SGS_EINVAL; }
#define SGS_PARSE_COLOR( outptr, strict ) { return sgs_ParseColorP( C, val, outptr, strict ) ? SGS_SUCCESS : SGS_EINVAL; }
#define SGS_PARSE_QUAT( outptr, strict ) { return sgs_ParseQuatP( C, val, outptr, strict ) ? SGS_SUCCESS : SGS_EINVAL; }
#define SGS_PARSE_MAT3( outptr ) { return sgs_ParseMat3P( C, val, outptr ) ? SGS_SUCCESS : SGS_EINVAL; }
#define SGS_PARSE_MAT4( outptr ) { return sgs_ParseMat4P( C, val, outptr ) ? SGS_SUCCESS : SGS_EINVAL; }


#ifdef __cplusplus
}
#endif

#endif /* SGSXGMATH_H */

