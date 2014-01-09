

#ifndef SGSXGMATH_H
#define SGSXGMATH_H


#include <math.h>
#include <stdarg.h>

#include <sgscript.h>


#ifndef XGM_VECTOR_TYPE
#define XGM_VECTOR_TYPE float
#endif


typedef struct _xgm_poly2
{
	XGM_VECTOR_TYPE* data;
	sgs_SizeVal size;
	sgs_SizeVal mem;
}
xgm_poly2;


extern sgs_ObjCallback xgm_vec2_iface[];
extern sgs_ObjCallback xgm_vec3_iface[];
extern sgs_ObjCallback xgm_vec4_iface[];
extern sgs_ObjCallback xgm_aabb2_iface[];
extern sgs_ObjCallback xgm_poly2_iface[];
extern sgs_ObjCallback xgm_color_iface[];

SGS_APIFUNC void sgs_PushVec2( SGS_CTX, XGM_VECTOR_TYPE x, XGM_VECTOR_TYPE y );
SGS_APIFUNC void sgs_PushVec3( SGS_CTX, XGM_VECTOR_TYPE x, XGM_VECTOR_TYPE y, XGM_VECTOR_TYPE z );
SGS_APIFUNC void sgs_PushVec4( SGS_CTX, XGM_VECTOR_TYPE x, XGM_VECTOR_TYPE y, XGM_VECTOR_TYPE z, XGM_VECTOR_TYPE w );
SGS_APIFUNC void sgs_PushAABB2( SGS_CTX, XGM_VECTOR_TYPE x1, XGM_VECTOR_TYPE y1, XGM_VECTOR_TYPE x2, XGM_VECTOR_TYPE y2 );
SGS_APIFUNC void sgs_PushPoly2( SGS_CTX, XGM_VECTOR_TYPE* v2fn, int numverts );
SGS_APIFUNC void sgs_PushColor( SGS_CTX, XGM_VECTOR_TYPE r, XGM_VECTOR_TYPE g, XGM_VECTOR_TYPE b, XGM_VECTOR_TYPE a );

SGS_APIFUNC void sgs_PushVec2p( SGS_CTX, XGM_VECTOR_TYPE* v2f );
SGS_APIFUNC void sgs_PushVec3p( SGS_CTX, XGM_VECTOR_TYPE* v3f );
SGS_APIFUNC void sgs_PushVec4p( SGS_CTX, XGM_VECTOR_TYPE* v4f );
SGS_APIFUNC void sgs_PushAABB2p( SGS_CTX, XGM_VECTOR_TYPE* v4f );
SGS_APIFUNC void sgs_PushColorp( SGS_CTX, XGM_VECTOR_TYPE* v4f );
SGS_APIFUNC void sgs_PushColorvp( SGS_CTX, XGM_VECTOR_TYPE* vf, int numfloats );

SGS_APIFUNC SGSBOOL sgs_ParseVec2( SGS_CTX, int pos, XGM_VECTOR_TYPE* v2f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseVec3( SGS_CTX, int pos, XGM_VECTOR_TYPE* v3f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseVec4( SGS_CTX, int pos, XGM_VECTOR_TYPE* v4f, int strict );
SGS_APIFUNC SGSBOOL sgs_ParseAABB2( SGS_CTX, int pos, XGM_VECTOR_TYPE* v4f );
SGS_APIFUNC SGSBOOL sgs_ParseColor( SGS_CTX, int pos, XGM_VECTOR_TYPE* v4f, int strict );

SGS_APIFUNC int sgs_ArgCheck_Vec2( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_Vec3( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_Vec4( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_AABB2( SGS_CTX, int argid, va_list* args, int flags );
SGS_APIFUNC int sgs_ArgCheck_Color( SGS_CTX, int argid, va_list* args, int flags );


SGS_APIFUNC int xgm_module_entry_point( SGS_CTX );


#endif /* SGSXGMATH_H */

