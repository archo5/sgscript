
#ifndef SGSCRIPT_H_INCLUDED
#define SGSCRIPT_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <malloc.h>

#include "sgs_cfg.h"
#include "sgs_xpc.h"


/* Output codes */
#define SGS_SUCCESS	0	/* success (no errors) */
#define SGS_ESTKOF	-1	/* stack overflow */
#define SGS_ESTKUF	-2	/* stack underflow */
#define SGS_ENOTFND	-3	/* item was not found */
#define SGS_ECOMP	-4	/* compile error */
#define SGS_ENOTOBJ	-5	/* argument was not an object */
#define SGS_ENOTSUP	-6	/* not supported */
#define SGS_EBOUNDS	-7	/* index out of bounds */
#define SGS_EINVAL	-8	/* invalid value was passed */
#define SGS_ENOTIMP	-31	/* - not implemented - */


/* Accessible / transferable data */
typedef int64_t	sgs_Integer;
typedef double	sgs_Real;
typedef struct _sgs_Context sgs_Context;
typedef struct _sgs_Variable sgs_Variable;
typedef int (*sgs_CFunc) ( sgs_Context* );


/* Memory management */
extern void* (*sgs_MemFunc) ( void*, size_t );


/* Debug output */
#define SGS_INFO	0
#define SGS_WARNING	1
#define SGS_ERROR	2

typedef void (*sgs_PrintFunc) ( void* /* ctx */, int /* type */, int /* line */, const char* /* message */ );


/* Statistics */
#define SGS_STAT_VARCOUNT	1


/* Context internals */
/* - variable types */
#define SVT_NULL	0	/* null (/no) data */
#define SVT_BOOL	1	/* bool(u8) data */
#define SVT_INT		2	/* i64 data */
#define SVT_REAL	3	/* f64 data */
#define SVT_STRING	4	/* variable-length string data */
#define SVT_FUNC	5	/* function data */
#define SVT_CFUNC	6	/* C function */
#define SVT_OBJECT	7	/* variable-length binary data */

/* - object interface */
typedef int (*sgs_ObjCallback) ( sgs_Context*, void* data );

#define SOP( idx ) ((void*)idx)
#define SOP_END			SOP( 0 )
#define SOP_DESTRUCT	SOP( 1 )
#define SOP_COPY		SOP( 2 )
#define SOP_GETTYPE		SOP( 3 )
#define SOP_GETPROP		SOP( 4 )
#define SOP_SETPROP		SOP( 5 )
#define SOP_GETINDEX	SOP( 6 )
#define SOP_SETINDEX	SOP( 7 )
#define SOP_TOBOOL		SOP( 8 )
#define SOP_TOINT		SOP( 9 )
#define SOP_TOREAL		SOP( 10 )
#define SOP_TOSTRING	SOP( 11 )
#define SOP_GCMARK		SOP( 12 )


/* Engine context */
#define SGS_CTX sgs_Context* C

sgs_Context*	sgs_CreateEngine();
void			sgs_DestroyEngine( SGS_CTX );
sgs_PrintFunc	sgs_SetPrintFunc( SGS_CTX, sgs_PrintFunc func );

void sgs_Printf( SGS_CTX, int type, int line, const char* what, ... );

int				sgs_ExecBuffer( SGS_CTX, const char* buf, int size );
static SGS_INLINE int	sgs_ExecString( SGS_CTX, const char* str ){ return sgs_ExecBuffer( C, str, strlen( str ) ); }
int				sgs_Stat( SGS_CTX, int type );


/* The core interface */

int sgs_SetNull( SGS_CTX, int item );
int sgs_SetBool( SGS_CTX, int item, int value );
int sgs_SetInt( SGS_CTX, int item, sgs_Integer value );
int sgs_SetReal( SGS_CTX, int item, sgs_Real value );

int sgs_PushNull( SGS_CTX );
int sgs_PushBool( SGS_CTX, int value );
int sgs_PushInt( SGS_CTX, sgs_Integer value );
int sgs_PushReal( SGS_CTX, sgs_Real value );
int sgs_PushStringBuf( SGS_CTX, const char* str, int32_t size );
int sgs_PushString( SGS_CTX, const char* str );
int sgs_PushCFunction( SGS_CTX, sgs_CFunc func );
int sgs_PushObject( SGS_CTX, void* data, void** iface );
int sgs_PushVariable( SGS_CTX, sgs_Variable* var );

int sgs_PushProperty( SGS_CTX, const char* name );
int sgs_StringConcat( SGS_CTX );
int sgs_StringMultiConcat( SGS_CTX, int args );

int sgs_Pop( SGS_CTX, int count );
int sgs_Call( SGS_CTX, int args, int expect );
int sgs_Method( SGS_CTX );
int sgs_TypeOf( SGS_CTX );

int sgs_GetGlobal( SGS_CTX, const char* name );
int sgs_SetGlobal( SGS_CTX, const char* name );

int sgs_GetBool( SGS_CTX, int item );
sgs_Integer sgs_GetInt( SGS_CTX, int item );
sgs_Real sgs_GetReal( SGS_CTX, int item );

int sgs_ToBool( SGS_CTX, int item );
sgs_Integer sgs_ToInt( SGS_CTX, int item );
sgs_Real sgs_ToReal( SGS_CTX, int item );
const char* sgs_ToString( SGS_CTX, int item );

int sgs_StackSize( SGS_CTX );
sgs_Variable* sgs_StackItem( SGS_CTX, int item );
int sgs_ItemType( SGS_CTX, int item );
void sgs_Acquire( SGS_CTX, sgs_Variable* var );
void sgs_Release( SGS_CTX, sgs_Variable* var );
int sgs_GCExecute( SGS_CTX );
int sgs_GCMark( SGS_CTX, sgs_Variable* var );
int sgs_CheckArgs( SGS_CTX, const char* str );

const char* sgs_GetStringPtr( SGS_CTX, int item );
int32_t sgs_GetStringSize( SGS_CTX, int item );
void* sgs_GetObjectData( SGS_CTX, int item );


#ifdef __cplusplus
}
#endif

#endif /* SGSCRIPT_H_INCLUDED */
