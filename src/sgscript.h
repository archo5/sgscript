
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
#define SGS_SUCCESS  0  /* success (no errors) */
#define SGS_ESTKOF  -1  /* stack overflow */
#define SGS_ESTKUF  -2  /* stack underflow */
#define SGS_ENOTFND -3  /* item was not found */
#define SGS_ECOMP   -4  /* compile error */
#define SGS_ENOTOBJ -5  /* argument was not an object */
#define SGS_ENOTSUP -6  /* not supported */
#define SGS_EBOUNDS -7  /* index out of bounds */
#define SGS_EINVAL  -8  /* invalid value was passed */
#define SGS_EINPROC -9  /* process was interrupted */
#define SGS_ENOTIMP -31 /* - not implemented - */


/* Accessible / transferable data */
typedef int64_t sgs_Integer;
typedef double  sgs_Real;
typedef struct _LNTable sgs_LNTable;
typedef struct _sgs_Context sgs_Context;
typedef struct _sgs_Variable sgs_Variable;
typedef struct _sgs_StackFrame sgs_StackFrame;
typedef int (*sgs_CFunc) ( sgs_Context* );


struct _sgs_StackFrame
{
	sgs_Variable*   func;
	uint16_t*       lntable;
	const uint32_t* code;
	const uint32_t* iptr;
	const uint32_t* iend;
	sgs_StackFrame* prev;
	sgs_StackFrame* next;
};


/* Memory management */
extern void* (*sgs_MemFunc) ( void*, size_t );


/* Debug output */
#define SGS_INFO	0
#define SGS_WARNING	1
#define SGS_ERROR	2

typedef void (*sgs_PrintFunc) ( void* /* data */, sgs_Context* /* ctx / SGS_CTX */, int /* type */, int /* line */, const char* /* message */ );


/* Statistics / debugging */
#define SGS_STAT_VARCOUNT     1
#define SGS_STAT_DUMP_STACK   10
#define SGS_STAT_DUMP_GLOBALS 11
#define SGS_STAT_DUMP_OBJECTS 12
#define SGS_STAT_DUMP_FRAMES  13


/* Context internals */
/* - variable types */
#define SVT_NULL    0  /* null (/no) data */
#define SVT_BOOL    1  /* bool(u8) data */
#define SVT_INT     2  /* i64 data */
#define SVT_REAL    3  /* f64 data */
#define SVT_STRING  4  /* variable-length string data */
#define SVT_FUNC    5  /* function data */
#define SVT_CFUNC   6  /* C function */
#define SVT_OBJECT  7  /* variable-length binary data */
#define SVT__COUNT  8  /* number of available types */

/* - object data */
typedef struct sgs_ObjData sgs_VarObj;
struct sgs_ObjData
{
	int32_t refcount;
	void* data;
	void** iface;
	sgs_VarObj* prev; /* pointer to previous GC object */
	sgs_VarObj* next; /* pointer to next GC object */
	uint8_t redblue; /* red or blue? mark & sweep */
	uint8_t destroying; /* whether the variable is already in a process of destruction. for container circ. ref. handling. */
};

/* - object interface */
typedef int (*sgs_ObjCallback) ( sgs_Context*, sgs_VarObj* data );

#define SOP( idx ) ((void*)idx)
#define SOP_END        SOP( 0 )
#define SOP_DESTRUCT   SOP( 1 )
#define SOP_CLONE      SOP( 2 )
#define SOP_GETTYPE    SOP( 3 )
#define SOP_GETPROP    SOP( 4 )
#define SOP_SETPROP    SOP( 5 )
#define SOP_GETINDEX   SOP( 6 )
#define SOP_SETINDEX   SOP( 7 )
#define SOP_TOBOOL     SOP( 8 )
#define SOP_TOINT      SOP( 9 )
#define SOP_TOREAL     SOP( 10 )
#define SOP_TOSTRING   SOP( 11 )
#define SOP_DUMP       SOP( 12 )
#define SOP_GCMARK     SOP( 13 )
#define SOP_GETITER    SOP( 14 )
#define SOP_NEXTKEY    SOP( 15 )
#define SOP_CALL       SOP( 16 )
#define SOP_COMPARE    SOP( 17 )
#define SOP_OP_ADD     SOP( 18 )
#define SOP_OP_SUB     SOP( 19 )
#define SOP_OP_MUL     SOP( 20 )
#define SOP_OP_DIV     SOP( 21 )
#define SOP_OP_MOD     SOP( 22 )
#define SOP_OP_NEGATE  SOP( 23 )


/* Engine context */
#define SGS_CTX sgs_Context* C

sgs_Context*    sgs_CreateEngine();
void            sgs_DestroyEngine( SGS_CTX );
void            sgs_SetPrintFunc( SGS_CTX, sgs_PrintFunc func, void* ctx );

void sgs_Printf( SGS_CTX, int type, int line, const char* what, ... );

int             sgs_ExecBuffer( SGS_CTX, const char* buf, int size );
static SGS_INLINE int sgs_ExecString( SGS_CTX, const char* str ){ return sgs_ExecBuffer( C, str, strlen( str ) ); }
int             sgs_EvalBuffer( SGS_CTX, const char* buf, int size, int* rvc );
static SGS_INLINE int sgs_EvalString( SGS_CTX, const char* str, int* rvc ){ return sgs_EvalBuffer( C, str, strlen( str ), rvc ); }
int             sgs_EvalFile( SGS_CTX, const char* file, int* rvc );
static SGS_INLINE int sgs_ExecFile( SGS_CTX, const char* file ){ return sgs_EvalFile( C, file, NULL ); }
int             sgs_Stat( SGS_CTX, int type );
void            sgs_StackFrameInfo( SGS_CTX, sgs_StackFrame* frame, char** name, char** file, int* line );
sgs_StackFrame* sgs_GetFramePtr( SGS_CTX, int end );


/* Additional libraries */

int sgs_LoadLib_Math( SGS_CTX );
#if 0
int sgs_LoadLib_Native( SGS_CTX );
#endif
int sgs_LoadLib_String( SGS_CTX );
int sgs_LoadLib_Type( SGS_CTX );

typedef struct _sgs_RegFuncConst
{
	char* name;
	sgs_CFunc value;
}
sgs_RegFuncConst;
int sgs_RegFuncConsts( SGS_CTX, const sgs_RegFuncConst* list, int size );

typedef struct _sgs_RegIntConst
{
	char* name;
	sgs_Integer value;
}
sgs_RegIntConst;
int sgs_RegIntConsts( SGS_CTX, const sgs_RegIntConst* list, int size );

typedef struct _sgs_RegRealConst
{
	char* name;
	sgs_Real value;
}
sgs_RegRealConst;
int sgs_RegRealConsts( SGS_CTX, const sgs_RegRealConst* list, int size );


/* The core interface */

void sgs_PushNull( SGS_CTX );
void sgs_PushBool( SGS_CTX, int value );
void sgs_PushInt( SGS_CTX, sgs_Integer value );
void sgs_PushReal( SGS_CTX, sgs_Real value );
void sgs_PushStringBuf( SGS_CTX, const char* str, int32_t size );
void sgs_PushString( SGS_CTX, const char* str );
void sgs_PushCFunction( SGS_CTX, sgs_CFunc func );
void sgs_PushObject( SGS_CTX, void* data, void** iface );
void sgs_PushVariable( SGS_CTX, sgs_Variable* var );

int sgs_PushItem( SGS_CTX, int pos );
int sgs_PushProperty( SGS_CTX, const char* name );
int sgs_PushIndex( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx );
int sgs_PushGlobal( SGS_CTX, const char* name );
int sgs_StoreGlobal( SGS_CTX, const char* name );

int sgs_GetIndex( SGS_CTX, sgs_Variable* out, sgs_Variable* obj, sgs_Variable* idx ); /* must release "out" */
int sgs_SetIndex( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx, sgs_Variable* val );
int sgs_GetNumIndex( SGS_CTX, sgs_Variable* out, sgs_Variable* obj, sgs_Integer idx ); /* must release "out" */
int sgs_SetNumIndex( SGS_CTX, sgs_Variable* obj, sgs_Integer idx, sgs_Variable* val );

int sgs_Pop( SGS_CTX, int count );
int sgs_PopSkip( SGS_CTX, int count, int skip );

int sgs_Call( SGS_CTX, int args, int expect );
int sgs_GlobalCall( SGS_CTX, const char* name, int args, int expect );
int sgs_Method( SGS_CTX );
int sgs_TypeOf( SGS_CTX );
int sgs_DumpVar( SGS_CTX, int maxdepth );
int sgs_PadString( SGS_CTX );
int sgs_StringConcat( SGS_CTX );
int sgs_StringMultiConcat( SGS_CTX, int args );

int sgs_GetBool( SGS_CTX, int item );
sgs_Integer sgs_GetInt( SGS_CTX, int item );
sgs_Real sgs_GetReal( SGS_CTX, int item );

int sgs_ToBool( SGS_CTX, int item );
sgs_Integer sgs_ToInt( SGS_CTX, int item );
sgs_Real sgs_ToReal( SGS_CTX, int item );
char* sgs_ToStringBuf( SGS_CTX, int item, sgs_Integer* outsize );
#define sgs_ToString( ctx, item ) sgs_ToStringBuf( ctx, item, NULL )

int sgs_StackSize( SGS_CTX );
sgs_Variable* sgs_StackItem( SGS_CTX, int item );
int sgs_ItemType( SGS_CTX, int item );
void sgs_Acquire( SGS_CTX, sgs_Variable* var );
void sgs_Release( SGS_CTX, sgs_Variable* var );
int sgs_GCExecute( SGS_CTX );
int sgs_GCMark( SGS_CTX, sgs_Variable* var );

const char* sgs_GetStringPtr( SGS_CTX, int item );
int32_t sgs_GetStringSize( SGS_CTX, int item );
sgs_VarObj* sgs_GetObjectData( SGS_CTX, int item );


#ifdef __cplusplus
}
#endif

#endif /* SGSCRIPT_H_INCLUDED */
