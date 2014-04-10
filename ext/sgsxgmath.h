

#ifndef SGSXGMATH_H
#define SGSXGMATH_H


#ifdef __cplusplus
extern "C" {
#endif


#include <math.h>
#include <stdarg.h>

#include <sgscript.h>


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
extern sgs_ObjInterface xgm_color_iface[1];
extern sgs_ObjInterface xgm_mat4_iface[1];
extern sgs_ObjInterface xgm_floatarr_iface[1];

SGS_APIFUNC void sgs_InitVec2( SGS_CTX, sgs_Variable* var, XGM_VT x, XGM_VT y );
SGS_APIFUNC void sgs_InitVec3( SGS_CTX, sgs_Variable* var, XGM_VT x, XGM_VT y, XGM_VT z );
SGS_APIFUNC void sgs_InitVec4( SGS_CTX, sgs_Variable* var, XGM_VT x, XGM_VT y, XGM_VT z, XGM_VT w );
SGS_APIFUNC void sgs_InitAABB2( SGS_CTX, sgs_Variable* var, XGM_VT x1, XGM_VT y1, XGM_VT x2, XGM_VT y2 );
SGS_APIFUNC void sgs_InitColor( SGS_CTX, sgs_Variable* var, XGM_VT r, XGM_VT g, XGM_VT b, XGM_VT a );
SGS_APIFUNC void sgs_InitMat4( SGS_CTX, sgs_Variable* var, const XGM_VT* v16f, int transpose );
SGS_APIFUNC void sgs_InitFloatArray( SGS_CTX, sgs_Variable* var, const XGM_VT* vfn, sgs_SizeVal size );

SGS_APIFUNC void sgs_InitVec2p( SGS_CTX, sgs_Variable* var, const XGM_VT* v2f );
SGS_APIFUNC void sgs_InitVec3p( SGS_CTX, sgs_Variable* var, const XGM_VT* v3f );
SGS_APIFUNC void sgs_InitVec4p( SGS_CTX, sgs_Variable* var, const XGM_VT* v4f );
SGS_APIFUNC void sgs_InitAABB2p( SGS_CTX, sgs_Variable* var, const XGM_VT* v4f );
SGS_APIFUNC void sgs_InitColorp( SGS_CTX, sgs_Variable* var, const XGM_VT* v4f );
SGS_APIFUNC void sgs_InitColorvp( SGS_CTX, sgs_Variable* var, const XGM_VT* vf, int numfloats );

SGS_APIFUNC void sgs_PushVec2( SGS_CTX, XGM_VT x, XGM_VT y );
SGS_APIFUNC void sgs_PushVec3( SGS_CTX, XGM_VT x, XGM_VT y, XGM_VT z );
SGS_APIFUNC void sgs_PushVec4( SGS_CTX, XGM_VT x, XGM_VT y, XGM_VT z, XGM_VT w );
SGS_APIFUNC void sgs_PushAABB2( SGS_CTX, XGM_VT x1, XGM_VT y1, XGM_VT x2, XGM_VT y2 );
SGS_APIFUNC void sgs_PushColor( SGS_CTX, XGM_VT r, XGM_VT g, XGM_VT b, XGM_VT a );
SGS_APIFUNC void sgs_PushMat4( SGS_CTX, const XGM_VT* v16f, int transpose );
SGS_APIFUNC void sgs_PushFloatArray( SGS_CTX, const XGM_VT* vfn, sgs_SizeVal size );

SGS_APIFUNC void sgs_PushVec2p( SGS_CTX, const XGM_VT* v2f );
SGS_APIFUNC void sgs_PushVec3p( SGS_CTX, const XGM_VT* v3f );
SGS_APIFUNC void sgs_PushVec4p( SGS_CTX, const XGM_VT* v4f );
SGS_APIFUNC void sgs_PushAABB2p( SGS_CTX, const XGM_VT* v4f );
SGS_APIFUNC void sgs_PushColorp( SGS_CTX, const XGM_VT* v4f );
SGS_APIFUNC void sgs_PushColorvp( SGS_CTX, const XGM_VT* vf, int numfloats );

SGS_APIFUNC SGSBOOL sgs_ParseVT( SGS_CTX, sgs_StkIdx item, XGM_VT* out );
SGS_APIFUNC SGSBOOL sgs_ParseVTP( SGS_CTX, sgs_Variable* var, XGM_VT* out );

SGS_APIFUNC SGSBOOL sgs_ParseVec2P( SGS_CTX, sgs_Variable* var, XGM_VT* v2f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseVec3P( SGS_CTX, sgs_Variable* var, XGM_VT* v3f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseVec4P( SGS_CTX, sgs_Variable* var, XGM_VT* v4f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseAABB2P( SGS_CTX, sgs_Variable* var, XGM_VT* v4f );
SGS_APIFUNC SGSBOOL sgs_ParseColorP( SGS_CTX, sgs_Variable* var, XGM_VT* v4f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseMat4P( SGS_CTX, sgs_Variable* var, XGM_VT* v16f );
SGS_APIFUNC SGSBOOL sgs_ParseFloatArrayP( SGS_CTX, sgs_Variable* var, XGM_VT** vfa, sgs_SizeVal* osz );

SGS_APIFUNC SGSBOOL sgs_ParseVec2( SGS_CTX, sgs_StkIdx item, XGM_VT* v2f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseVec3( SGS_CTX, sgs_StkIdx item, XGM_VT* v3f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseVec4( SGS_CTX, sgs_StkIdx item, XGM_VT* v4f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseAABB2( SGS_CTX, sgs_StkIdx item, XGM_VT* v4f );
SGS_APIFUNC SGSBOOL sgs_ParseColor( SGS_CTX, sgs_StkIdx item, XGM_VT* v4f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseMat4( SGS_CTX, sgs_StkIdx item, XGM_VT* v16f );
SGS_APIFUNC SGSBOOL sgs_ParseFloatArray( SGS_CTX, sgs_StkIdx item, XGM_VT** vfa, sgs_SizeVal* osz );

SGS_APIFUNC int sgs_ArgCheck_Vec2( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_Vec3( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_Vec4( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_AABB2( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_Color( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_Mat4( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_FloatArray( SGS_CTX, int argid, va_list* args, int flags );


SGS_APIFUNC int xgm_module_entry_point( SGS_CTX );


/* utility macros */
#define SGS_RETURN_VEC2( x, y ) { sgs_PushVec2( C, (XGM_VT)(x), (XGM_VT)(y) ); return SGS_SUCCESS; }
#define SGS_RETURN_VEC3( x, y, z ) { sgs_PushVec3( C, (XGM_VT)(x), (XGM_VT)(y), (XGM_VT)(z) ); return SGS_SUCCESS; }
#define SGS_RETURN_VEC4( x, y, z, w ) { sgs_PushVec4( C, (XGM_VT)(x), (XGM_VT)(y), (XGM_VT)(z), (XGM_VT)(w) ); return SGS_SUCCESS; }
#define SGS_RETURN_VEC2P( value ) { sgs_PushVec2p( C, value ); return SGS_SUCCESS; }
#define SGS_RETURN_VEC3P( value ) { sgs_PushVec3p( C, value ); return SGS_SUCCESS; }
#define SGS_RETURN_VEC4P( value ) { sgs_PushVec4p( C, value ); return SGS_SUCCESS; }
#define SGS_RETURN_AABB2( value ) { sgs_PushAABB2p( C, value ); return SGS_SUCCESS; }
#define SGS_RETURN_COLOR( value ) { sgs_PushColorp( C, value ); return SGS_SUCCESS; }
#define SGS_RETURN_MAT4( value ) { sgs_PushMat4( C, value, 0 ); return SGS_SUCCESS; }

#define SGS_PARSE_VEC2( outptr, strict ) { return sgs_ParseVec2P( C, val, outptr, strict ) ? SGS_SUCCESS : SGS_EINVAL; }
#define SGS_PARSE_VEC3( outptr, strict ) { return sgs_ParseVec3P( C, val, outptr, strict ) ? SGS_SUCCESS : SGS_EINVAL; }
#define SGS_PARSE_VEC4( outptr, strict ) { return sgs_ParseVec4P( C, val, outptr, strict ) ? SGS_SUCCESS : SGS_EINVAL; }
#define SGS_PARSE_AABB2( outptr ) { return sgs_ParseAABB2P( C, val, outptr ) ? SGS_SUCCESS : SGS_EINVAL; }
#define SGS_PARSE_COLOR( outptr, strict ) { return sgs_ParseColorP( C, val, outptr, strict ) ? SGS_SUCCESS : SGS_EINVAL; }
#define SGS_PARSE_MAT4( outptr ) { return sgs_ParseMat4P( C, val, outptr ) ? SGS_SUCCESS : SGS_EINVAL; }


#ifdef __cplusplus
}
#endif

#endif /* SGSXGMATH_H */

