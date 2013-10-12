

#include <math.h>
#include <stdarg.h>

#include <sgscript.h>


#ifndef XGM_VECTOR_TYPE
#define XGM_VECTOR_TYPE float
#endif


sgs_ObjCallback xgm_vec2_iface[];
sgs_ObjCallback xgm_vec3_iface[];
sgs_ObjCallback xgm_vec4_iface[];

void sgs_PushVec2( SGS_CTX, XGM_VECTOR_TYPE x, XGM_VECTOR_TYPE y );
void sgs_PushVec3( SGS_CTX, XGM_VECTOR_TYPE x, XGM_VECTOR_TYPE y, XGM_VECTOR_TYPE z );
void sgs_PushVec4( SGS_CTX, XGM_VECTOR_TYPE x, XGM_VECTOR_TYPE y, XGM_VECTOR_TYPE z, XGM_VECTOR_TYPE w );

SGSBOOL sgs_ParseVec2( SGS_CTX, int pos, XGM_VECTOR_TYPE* v2f, int strict );
SGSBOOL sgs_ParseVec3( SGS_CTX, int pos, XGM_VECTOR_TYPE* v3f, int strict );
SGSBOOL sgs_ParseVec4( SGS_CTX, int pos, XGM_VECTOR_TYPE* v4f, int strict );

int sgs_ArgCheck_Vec2( SGS_CTX, int argid, va_list args, int flags );
int sgs_ArgCheck_Vec3( SGS_CTX, int argid, va_list args, int flags );
int sgs_ArgCheck_Vec4( SGS_CTX, int argid, va_list args, int flags );

