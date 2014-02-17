

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


typedef struct _xgm_poly2
{
	XGM_VT* data;
	sgs_SizeVal size;
	sgs_SizeVal mem;
}
xgm_poly2;


extern sgs_ObjInterface xgm_vec2_iface[1];
extern sgs_ObjInterface xgm_vec3_iface[1];
extern sgs_ObjInterface xgm_vec4_iface[1];
extern sgs_ObjInterface xgm_aabb2_iface[1];
extern sgs_ObjInterface xgm_poly2_iface[1];
extern sgs_ObjInterface xgm_color_iface[1];
extern sgs_ObjInterface xgm_mat4_iface[1];

SGS_APIFUNC void sgs_InitVec2( SGS_CTX, sgs_Variable* var, XGM_VT x, XGM_VT y );
SGS_APIFUNC void sgs_InitVec3( SGS_CTX, sgs_Variable* var, XGM_VT x, XGM_VT y, XGM_VT z );
SGS_APIFUNC void sgs_InitVec4( SGS_CTX, sgs_Variable* var, XGM_VT x, XGM_VT y, XGM_VT z, XGM_VT w );
SGS_APIFUNC void sgs_InitAABB2( SGS_CTX, sgs_Variable* var, XGM_VT x1, XGM_VT y1, XGM_VT x2, XGM_VT y2 );
SGS_APIFUNC void sgs_InitPoly2( SGS_CTX, sgs_Variable* var, XGM_VT* v2fn, int numverts );
SGS_APIFUNC void sgs_InitColor( SGS_CTX, sgs_Variable* var, XGM_VT r, XGM_VT g, XGM_VT b, XGM_VT a );
SGS_APIFUNC void sgs_InitMat4( SGS_CTX, sgs_Variable* var, XGM_VT* v16f, int transpose );

SGS_APIFUNC void sgs_InitVec2p( SGS_CTX, sgs_Variable* var, XGM_VT* v2f );
SGS_APIFUNC void sgs_InitVec3p( SGS_CTX, sgs_Variable* var, XGM_VT* v3f );
SGS_APIFUNC void sgs_InitVec4p( SGS_CTX, sgs_Variable* var, XGM_VT* v4f );
SGS_APIFUNC void sgs_InitAABB2p( SGS_CTX, sgs_Variable* var, XGM_VT* v4f );
SGS_APIFUNC void sgs_InitColorp( SGS_CTX, sgs_Variable* var, XGM_VT* v4f );
SGS_APIFUNC void sgs_InitColorvp( SGS_CTX, sgs_Variable* var, XGM_VT* vf, int numfloats );

SGS_APIFUNC void sgs_PushVec2( SGS_CTX, XGM_VT x, XGM_VT y );
SGS_APIFUNC void sgs_PushVec3( SGS_CTX, XGM_VT x, XGM_VT y, XGM_VT z );
SGS_APIFUNC void sgs_PushVec4( SGS_CTX, XGM_VT x, XGM_VT y, XGM_VT z, XGM_VT w );
SGS_APIFUNC void sgs_PushAABB2( SGS_CTX, XGM_VT x1, XGM_VT y1, XGM_VT x2, XGM_VT y2 );
SGS_APIFUNC void sgs_PushPoly2( SGS_CTX, XGM_VT* v2fn, int numverts );
SGS_APIFUNC void sgs_PushColor( SGS_CTX, XGM_VT r, XGM_VT g, XGM_VT b, XGM_VT a );
SGS_APIFUNC void sgs_PushMat4( SGS_CTX, XGM_VT* v16f, int transpose );

SGS_APIFUNC void sgs_PushVec2p( SGS_CTX, XGM_VT* v2f );
SGS_APIFUNC void sgs_PushVec3p( SGS_CTX, XGM_VT* v3f );
SGS_APIFUNC void sgs_PushVec4p( SGS_CTX, XGM_VT* v4f );
SGS_APIFUNC void sgs_PushAABB2p( SGS_CTX, XGM_VT* v4f );
SGS_APIFUNC void sgs_PushColorp( SGS_CTX, XGM_VT* v4f );
SGS_APIFUNC void sgs_PushColorvp( SGS_CTX, XGM_VT* vf, int numfloats );

SGS_APIFUNC SGSBOOL sgs_ParseVec2P( SGS_CTX, sgs_Variable* var, XGM_VT* v2f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseVec3P( SGS_CTX, sgs_Variable* var, XGM_VT* v3f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseVec4P( SGS_CTX, sgs_Variable* var, XGM_VT* v4f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseAABB2P( SGS_CTX, sgs_Variable* var, XGM_VT* v4f );
SGS_APIFUNC SGSBOOL sgs_ParseColorP( SGS_CTX, sgs_Variable* var, XGM_VT* v4f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseMat4P( SGS_CTX, sgs_Variable* var, XGM_VT* v16f );

SGS_APIFUNC SGSBOOL sgs_ParseVec2( SGS_CTX, int pos, XGM_VT* v2f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseVec3( SGS_CTX, int pos, XGM_VT* v3f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseVec4( SGS_CTX, int pos, XGM_VT* v4f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseAABB2( SGS_CTX, int pos, XGM_VT* v4f );
SGS_APIFUNC SGSBOOL sgs_ParseColor( SGS_CTX, int pos, XGM_VT* v4f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseMat4( SGS_CTX, int pos, XGM_VT* v16f );

SGS_APIFUNC int sgs_ArgCheck_Vec2( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_Vec3( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_Vec4( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_AABB2( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_Color( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_Mat4( SGS_CTX, int argid, va_list* args, int flags );


SGS_APIFUNC int xgm_module_entry_point( SGS_CTX );


#ifdef __cplusplus
}
#endif

#endif /* SGSXGMATH_H */

