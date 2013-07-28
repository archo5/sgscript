
#ifndef SGS_INT_H_INCLUDED
#define SGS_INT_H_INCLUDED


#ifndef _sgs_iStr
#  define _sgs_iStr _sgs_string_t
#  define _sgs_iFunc _sgs_func_t
#endif

#include "sgscript.h"
#include "sgs_util.h"


#ifdef SGS_INTERNAL
#  define ST_NULL SGS_ST_NULL
#  define ST_RBRKL SGS_ST_RBRKL
#  define ST_RBRKR SGS_ST_RBRKR
#  define ST_SBRKL SGS_ST_SBRKL
#  define ST_SBRKR SGS_ST_SBRKR
#  define ST_CBRKL SGS_ST_CBRKL
#  define ST_CBRKR SGS_ST_CBRKR
#  define ST_ARGSEP SGS_ST_ARGSEP
#  define ST_STSEP SGS_ST_STSEP
#  define ST_PICKSEP SGS_ST_PICKSEP
#  define ST_IDENT SGS_ST_IDENT
#  define ST_KEYWORD SGS_ST_KEYWORD
#  define ST_NUMREAL SGS_ST_NUMREAL
#  define ST_NUMINT SGS_ST_NUMINT
#  define ST_STRING SGS_ST_STRING
#  define ST_OP_SEQ SGS_ST_OP_SEQ
#  define ST_OP_SNEQ SGS_ST_OP_SNEQ
#  define ST_OP_EQ SGS_ST_OP_EQ
#  define ST_OP_NEQ SGS_ST_OP_NEQ
#  define ST_OP_LEQ SGS_ST_OP_LEQ
#  define ST_OP_GEQ SGS_ST_OP_GEQ
#  define ST_OP_LESS SGS_ST_OP_LESS
#  define ST_OP_GRTR SGS_ST_OP_GRTR
#  define ST_OP_ADDEQ SGS_ST_OP_ADDEQ
#  define ST_OP_SUBEQ SGS_ST_OP_SUBEQ
#  define ST_OP_MULEQ SGS_ST_OP_MULEQ
#  define ST_OP_DIVEQ SGS_ST_OP_DIVEQ
#  define ST_OP_MODEQ SGS_ST_OP_MODEQ
#  define ST_OP_ANDEQ SGS_ST_OP_ANDEQ
#  define ST_OP_OREQ SGS_ST_OP_OREQ
#  define ST_OP_XOREQ SGS_ST_OP_XOREQ
#  define ST_OP_LSHEQ SGS_ST_OP_LSHEQ
#  define ST_OP_RSHEQ SGS_ST_OP_RSHEQ
#  define ST_OP_BLAEQ SGS_ST_OP_BLAEQ
#  define ST_OP_BLOEQ SGS_ST_OP_BLOEQ
#  define ST_OP_CATEQ SGS_ST_OP_CATEQ
#  define ST_OP_SET SGS_ST_OP_SET
#  define ST_OP_MSET SGS_ST_OP_MSET
#  define ST_OP_BLAND SGS_ST_OP_BLAND
#  define ST_OP_BLOR SGS_ST_OP_BLOR
#  define ST_OP_ADD SGS_ST_OP_ADD
#  define ST_OP_SUB SGS_ST_OP_SUB
#  define ST_OP_MUL SGS_ST_OP_MUL
#  define ST_OP_DIV SGS_ST_OP_DIV
#  define ST_OP_MOD SGS_ST_OP_MOD
#  define ST_OP_AND SGS_ST_OP_AND
#  define ST_OP_OR SGS_ST_OP_OR
#  define ST_OP_XOR SGS_ST_OP_XOR
#  define ST_OP_LSH SGS_ST_OP_LSH
#  define ST_OP_RSH SGS_ST_OP_RSH
#  define ST_OP_MMBR SGS_ST_OP_MMBR
#  define ST_OP_CAT SGS_ST_OP_CAT
#  define ST_OP_NOT SGS_ST_OP_NOT
#  define ST_OP_INV SGS_ST_OP_INV
#  define ST_OP_INC SGS_ST_OP_INC
#  define ST_OP_DEC SGS_ST_OP_DEC
#  define ST_ISOP SGS_ST_ISOP
#  define ST_OP_UNARY SGS_ST_OP_UNARY
#  define ST_OP_BINARY SGS_ST_OP_BINARY
#  define ST_OP_ASSIGN SGS_ST_OP_ASSIGN
#  define ST_OP_BINMUL SGS_ST_OP_BINMUL
#  define ST_OP_BINADD SGS_ST_OP_BINADD
#  define ST_OP_BINOPS SGS_ST_OP_BINOPS
#  define ST_OP_COMP SGS_ST_OP_COMP
#  define ST_OP_BOOL SGS_ST_OP_BOOL
#  define ST_ISSPEC SGS_ST_ISSPEC
#  define ST_READINT SGS_ST_READINT
#  define ST_READLN SGS_ST_READLN

/* #  define TokenType sgs_TokenType */
#  define TokenList sgs_TokenList

#  define SFT_NULL SGS_SFT_NULL
#  define SFT_IDENT SGS_SFT_IDENT
#  define SFT_KEYWORD SGS_SFT_KEYWORD
#  define SFT_CONST SGS_SFT_CONST
#  define SFT_OPER SGS_SFT_OPER
#  define SFT_OPER_P SGS_SFT_OPER_P
#  define SFT_FCALL SGS_SFT_FCALL
#  define SFT_INDEX SGS_SFT_INDEX
#  define SFT_ARGMT SGS_SFT_ARGMT
#  define SFT_ARGLIST SGS_SFT_ARGLIST
#  define SFT_VARLIST SGS_SFT_VARLIST
#  define SFT_GVLIST SGS_SFT_GVLIST
#  define SFT_EXPLIST SGS_SFT_EXPLIST
#  define SFT_ARRLIST SGS_SFT_ARRLIST
#  define SFT_MAPLIST SGS_SFT_MAPLIST
#  define SFT_RETURN SGS_SFT_RETURN
#  define SFT_BLOCK SGS_SFT_BLOCK
#  define SFT_IFELSE SGS_SFT_IFELSE
#  define SFT_WHILE SGS_SFT_WHILE
#  define SFT_DOWHILE SGS_SFT_DOWHILE
#  define SFT_FOR SGS_SFT_FOR
#  define SFT_FOREACH SGS_SFT_FOREACH
#  define SFT_BREAK SGS_SFT_BREAK
#  define SFT_CONT SGS_SFT_CONT
#  define SFT_FUNC SGS_SFT_FUNC
#  define FTNode sgs_FTNode

#  define CompFunc sgs_CompFunc

/* VM */
#  define CONSTVAR SGS_CONSTVAR
#  define CONSTENC SGS_CONSTENC
#  define CONSTDEC SGS_CONSTDEC

#  define SI_NOP SGS_SI_NOP
#  define SI_PUSH SGS_SI_PUSH
#  define SI_PUSHN SGS_SI_PUSHN
#  define SI_POPN SGS_SI_POPN
#  define SI_POPR SGS_SI_POPR
#  define SI_RETN SGS_SI_RETN
#  define SI_JUMP SGS_SI_JUMP
#  define SI_JMPT SGS_SI_JMPT
#  define SI_JMPF SGS_SI_JMPF
#  define SI_CALL SGS_SI_CALL
#  define SI_FORPREP SGS_SI_FORPREP
#  define SI_FORLOAD SGS_SI_FORLOAD
#  define SI_FORJUMP SGS_SI_FORJUMP
#  define SI_LOADCONST SGS_SI_LOADCONST
#  define SI_GETVAR SGS_SI_GETVAR
#  define SI_SETVAR SGS_SI_SETVAR
#  define SI_GETPROP SGS_SI_GETPROP
#  define SI_SETPROP SGS_SI_SETPROP
#  define SI_GETINDEX SGS_SI_GETINDEX
#  define SI_SETINDEX SGS_SI_SETINDEX
#  define SI_SET SGS_SI_SET
#  define SI_CLONE SGS_SI_CLONE
#  define SI_CONCAT SGS_SI_CONCAT
#  define SI_NEGATE SGS_SI_NEGATE
#  define SI_BOOL_INV SGS_SI_BOOL_INV
#  define SI_INVERT SGS_SI_INVERT
#  define SI_INC SGS_SI_INC
#  define SI_DEC SGS_SI_DEC
#  define SI_ADD SGS_SI_ADD
#  define SI_SUB SGS_SI_SUB
#  define SI_MUL SGS_SI_MUL
#  define SI_DIV SGS_SI_DIV
#  define SI_MOD SGS_SI_MOD
#  define SI_AND SGS_SI_AND
#  define SI_OR SGS_SI_OR
#  define SI_XOR SGS_SI_XOR
#  define SI_LSH SGS_SI_LSH
#  define SI_RSH SGS_SI_RSH
#  define SI_SEQ SGS_SI_SEQ
#  define SI_SNEQ SGS_SI_SNEQ
#  define SI_EQ SGS_SI_EQ
#  define SI_NEQ SGS_SI_NEQ
#  define SI_LT SGS_SI_LT
#  define SI_GTE SGS_SI_GTE
#  define SI_GT SGS_SI_GT
#  define SI_LTE SGS_SI_LTE
#  define SI_ARRAY SGS_SI_ARRAY
#  define SI_DICT SGS_SI_DICT

#  define instr_t sgs_instr_t

#  define INSTR_SIZE SGS_INSTR_SIZE
#  define INSTR_OFF_OP SGS_INSTR_OFF_OP
#  define INSTR_OFF_A SGS_INSTR_OFF_A
#  define INSTR_OFF_B SGS_INSTR_OFF_B
#  define INSTR_OFF_C SGS_INSTR_OFF_C
#  define INSTR_OFF_E SGS_INSTR_OFF_E
#  define INSTR_MASK_OP SGS_INSTR_MASK_OP
#  define INSTR_MASK_A SGS_INSTR_MASK_A
#  define INSTR_MASK_B SGS_INSTR_MASK_B
#  define INSTR_MASK_C SGS_INSTR_MASK_C
#  define INSTR_MASK_E SGS_INSTR_MASK_E
#  define INSTR_GET_OP SGS_INSTR_GET_OP
#  define INSTR_GET_A SGS_INSTR_GET_A
#  define INSTR_GET_B SGS_INSTR_GET_B
#  define INSTR_GET_C SGS_INSTR_GET_C
#  define INSTR_GET_E SGS_INSTR_GET_E
#  define INSTR_MAKE_OP SGS_INSTR_MAKE_OP
#  define INSTR_MAKE_A SGS_INSTR_MAKE_A
#  define INSTR_MAKE_B SGS_INSTR_MAKE_B
#  define INSTR_MAKE_C SGS_INSTR_MAKE_C
#  define INSTR_MAKE_E SGS_INSTR_MAKE_E
#  define INSTR_MAKE SGS_INSTR_MAKE
#  define INSTR_MAKE_EX SGS_INSTR_MAKE_EX
#  define INSTR_RECOMB_E SGS_INSTR_RECOMB_E

#  define func_t sgs_func_t
#  define func_consts sgs_func_consts
#  define func_bytecode sgs_func_bytecode
#  define string_t sgs_string_t
#  define str_cstr sgs_str_cstr
#  define var_cstr sgs_var_cstr
#  define object_t sgs_object_t
#  define VHTable sgs_VHTable
#  define VHTableVar sgs_VHTableVar
#  define vht_init sgs_vht_init
#  define vht_free sgs_vht_free
#  define vht_getS sgs_vht_getS
#  define vht_setS sgs_vht_setS
#  define vht_unsetS sgs_vht_unsetS
#  define vht_size sgs_vht_size
#  define var_destroy_object sgsVM_VarDestroyObject

#  define STACKFRAMESIZE SGS_STACKFRAMESIZE
#endif



/*
	Token stream
	<1 byte: type> <additional data> <2 bytes: line number>
*/
/*     special          id */
#define SGS_ST_NULL	    '\0'
#define SGS_ST_RBRKL    '('
#define SGS_ST_RBRKR    ')'
#define SGS_ST_SBRKL    '['
#define SGS_ST_SBRKR    ']'
#define SGS_ST_CBRKL    '{'
#define SGS_ST_CBRKR    '}'
#define SGS_ST_ARGSEP   ','
#define SGS_ST_STSEP    ';'
#define SGS_ST_PICKSEP  ':'
/*     other            id    additional data */
#define SGS_ST_IDENT    'N' /* 1 byte (string size), N bytes (string), not null-terminated */
#define SGS_ST_KEYWORD  'K' /* same as IDENT */
#define SGS_ST_NUMREAL  'R' /* 8 bytes (double) */
#define SGS_ST_NUMINT   'I' /* 8 bytes (int64) */
#define SGS_ST_STRING   'S' /* 4 bytes (string size), N bytes (string), not null-terminated */
/*     operators        id    type  */
#define SGS_ST_OP_SEQ   200 /* ===  */
#define SGS_ST_OP_SNEQ  201 /* !==  */
#define SGS_ST_OP_EQ    202 /* ==   */
#define SGS_ST_OP_NEQ   203 /* !=   */
#define SGS_ST_OP_LEQ   204 /* <=   */
#define SGS_ST_OP_GEQ   205 /* >=   */
#define SGS_ST_OP_LESS  206 /* <    */
#define SGS_ST_OP_GRTR  207 /* >    */
#define SGS_ST_OP_ADDEQ 208 /* +=   */
#define SGS_ST_OP_SUBEQ 209 /* -=   */
#define SGS_ST_OP_MULEQ 210 /* *=   */
#define SGS_ST_OP_DIVEQ 211 /* /=   */
#define SGS_ST_OP_MODEQ 212 /* %=   */
#define SGS_ST_OP_ANDEQ 213 /* &=   */
#define SGS_ST_OP_OREQ  214 /* |=   */
#define SGS_ST_OP_XOREQ 215 /* ^=   */
#define SGS_ST_OP_LSHEQ 216 /* <<=  */
#define SGS_ST_OP_RSHEQ 217 /* >>=  */
#define SGS_ST_OP_BLAEQ 218 /* &&=  */
#define SGS_ST_OP_BLOEQ 219 /* ||=  */
#define SGS_ST_OP_CATEQ 220 /* $=   */
#define SGS_ST_OP_SET   221 /* =    */
/* ------- RESERVED --- 222 ------- */
#define SGS_ST_OP_BLAND 223 /* &&   */
#define SGS_ST_OP_BLOR  224 /* ||   */
#define SGS_ST_OP_ADD   225 /* +    */
#define SGS_ST_OP_SUB   226 /* -    */
#define SGS_ST_OP_MUL   227 /* *    */
#define SGS_ST_OP_DIV   228 /* /    */
#define SGS_ST_OP_MOD   229 /* %    */
#define SGS_ST_OP_AND   230 /* &    */
#define SGS_ST_OP_OR    231 /* |    */
#define SGS_ST_OP_XOR   232 /* ^    */
#define SGS_ST_OP_LSH   233 /* <<   */
#define SGS_ST_OP_RSH   234 /* >>   */
#define SGS_ST_OP_MMBR  235 /* .    */
#define SGS_ST_OP_CAT   236 /* $    */
#define SGS_ST_OP_NOT   237 /* !    */
#define SGS_ST_OP_INV   238 /* ~    */
#define SGS_ST_OP_INC   239 /* ++   */
#define SGS_ST_OP_DEC   240 /* --   */

#define SGS_ST_ISOP( chr )      ( (chr) >= 200 && (chr) <= 240 )
#define SGS_ST_OP_UNARY( chr )  ( (chr) == SGS_ST_OP_ADD || (chr) == SGS_ST_OP_SUB || ( (chr) >= SGS_ST_OP_NOT && (chr) <= SGS_ST_OP_DEC ) )
#define SGS_ST_OP_BINARY( chr ) ( (chr) >= 200 && (chr) <= 236 )
#define SGS_ST_OP_ASSIGN( chr ) ( (chr) >= 208 && (chr) <= 222 )
#define SGS_ST_OP_BINMUL( chr ) ( (chr) == SGS_ST_OP_MUL || (chr) == SGS_ST_OP_DIV || (chr) == SGS_ST_OP_MOD )
#define SGS_ST_OP_BINADD( chr ) ( (chr) == SGS_ST_OP_ADD || (chr) == SGS_ST_OP_SUB )
#define SGS_ST_OP_BINOPS( chr ) ( (chr) >= SGS_ST_OP_AND && (chr) <= SGS_ST_OP_RSH )
#define SGS_ST_OP_COMP( chr )   ( (chr) >= 200 && (chr) <= 207 )
#define SGS_ST_OP_BOOL( chr )   ( (chr) == SGS_ST_OP_BLAEQ || (chr) == SGS_ST_OP_BLOEQ || (chr) == SGS_ST_OP_BLAND || (chr) == SGS_ST_OP_BLOR )

#define SGS_ST_ISSPEC( chr )    isoneof( (chr), "()[]{},;:" )

#define SGS_ST_READINT( pos )   (*(int32_t*)( pos ))
#define SGS_ST_READLN( pos )    (*(sgs_LineNum*)( pos ))


typedef unsigned char sgs_TokenType;
typedef unsigned char* sgs_TokenList;

sgs_TokenList sgsT_Gen( SGS_CTX, const char* code, int32_t length );
void          sgsT_Free( SGS_CTX, sgs_TokenList tlist );
sgs_TokenList sgsT_Next( sgs_TokenList tok );
sgs_LineNum   sgsT_LineNum( sgs_TokenList tok );

int32_t       sgsT_ListSize( sgs_TokenList tlist );
int32_t       sgsT_ListMemSize( sgs_TokenList tlist );

#ifdef SGS_DEBUG
void          sgsT_DumpToken( sgs_TokenList tok );
void          sgsT_DumpList( sgs_TokenList tlist, sgs_TokenList tend );
#endif


/*
	Function tree
*/

#define SGS_SFT_NULL    0
/* data */
#define SGS_SFT_IDENT   1
#define SGS_SFT_KEYWORD 2
#define SGS_SFT_CONST   3
/* expression parts */
#define SGS_SFT_OPER    4
#define SGS_SFT_OPER_P  5 /* post-op (inc/dec) */
#define SGS_SFT_FCALL   6
#define SGS_SFT_INDEX   7
/* statement data */
#define SGS_SFT_ARGMT   10
#define SGS_SFT_ARGLIST 11
#define SGS_SFT_VARLIST 12
#define SGS_SFT_GVLIST  13
#define SGS_SFT_EXPLIST 15
#define SGS_SFT_ARRLIST 16
#define SGS_SFT_MAPLIST 17
#define SGS_SFT_RETURN  18
/* statement types */
#define SGS_SFT_BLOCK   21
#define SGS_SFT_IFELSE  22
#define SGS_SFT_WHILE   23
#define SGS_SFT_DOWHILE 24
#define SGS_SFT_FOR     25
#define SGS_SFT_FOREACH 26
#define SGS_SFT_BREAK   27
#define SGS_SFT_CONT    28
#define SGS_SFT_FUNC    30

typedef struct _sgs_FTNode sgs_FTNode;
struct _sgs_FTNode
{
	sgs_TokenList token;
	sgs_FTNode*   next;
	sgs_FTNode*   child;
	short         type;
};

void sgsFT_Destroy( SGS_CTX, sgs_FTNode* tree );

sgs_FTNode* sgsFT_Compile( SGS_CTX, sgs_TokenList tlist );
void sgsFT_Dump( sgs_FTNode* tree );



/*
	Intermediate function
	- keeps the function data between compiler stages
*/

typedef
struct _sgs_CompFunc
{
	sgs_MemBuf consts;
	sgs_MemBuf code;
	sgs_MemBuf lnbuf;
	int	   gotthis;
	int	   numargs;
}
sgs_CompFunc;


/* - bytecode generator */
sgs_CompFunc* sgsBC_Generate( SGS_CTX, sgs_FTNode* tree );
void sgsBC_Dump( sgs_CompFunc* func );
void sgsBC_DumpEx( const char* constptr, sgs_SizeVal constsize,
	const char* codeptr, sgs_SizeVal codesize );
void sgsBC_Free( SGS_CTX, sgs_CompFunc* func );


/*
	Serialized bytecode
*/
SGS_APIFUNC int sgsBC_Func2Buf( SGS_CTX, sgs_CompFunc* func, sgs_MemBuf* outbuf );

/* assumes headers have already been validated (except size) but are still in the buffer */
SGS_APIFUNC const char* sgsBC_Buf2Func( SGS_CTX, const char* fn,
	const char* buf, sgs_SizeVal size, sgs_CompFunc** outfunc );

/* validates header size and bytes one by one (except last flag byte)
-- will return header_size on success and failed byte position on failure */
int sgsBC_ValidateHeader( const char* buf, sgs_SizeVal size );
#define SGS_HEADER_SIZE 14
#define SGS_MIN_BC_SIZE 14 + SGS_HEADER_SIZE
#define SGSBC_FLAG_LITTLE_ENDIAN 0x01


/*
	Virtual Machine
*/

#define SGS_CONSTVAR( x ) ((x)&0x100)
#define SGS_CONSTENC( x ) ((x)|0x100)
#define SGS_CONSTDEC( x ) ((x)&0xff)

typedef enum sgs_Instruction_e
{
	SGS_SI_NOP = 0,

	SGS_SI_PUSH,     /* (B:src)                 push register/constant */
	SGS_SI_PUSHN,    /* (A:N)                   push N nulls */
	SGS_SI_POPN,     /* (A:N)                   pop N items */
	SGS_SI_POPR,     /* (A:out)                 pop item to register */

	SGS_SI_RETN,     /* (A:N)                   exit current frame of execution, preserve N output arguments */
	SGS_SI_JUMP,     /* (E:off)                 add to instruction pointer */
	SGS_SI_JMPT,     /* (C:src, E:off)          jump (add to instr.ptr.) if true */
	SGS_SI_JMPF,     /* (C:src, E:off)          jump (add to instr.ptr.) if false */
	SGS_SI_CALL,     /* (A:exp, B:args, C:src)  call a variable */

	SGS_SI_FORPREP,  /* (A:out, B:src)          retrieves the iterator to work the object */
	SGS_SI_FORLOAD,  /* (A:iter, B:oky, C:ovl)  retrieves current key & value from iterator */
	SGS_SI_FORJUMP,  /* (C:iter, E:off)         advances and jumps if next key exists */

	SGS_SI_LOADCONST,/* (C:out, E:off)          load a constant to a register */
	SGS_SI_GETVAR,   /* (A:out, B:name)         <varname> => <value> */
	SGS_SI_SETVAR,   /* (B:name, C:src)         <varname> <value> => set <value> to <varname> */
	SGS_SI_GETPROP,  /* (A:out, B:var, C:name)  <var> <prop> => <var> */
	SGS_SI_SETPROP,  /* (A:var, B:name, C:src)  <var> <prop> <value> => set a <prop> of <var> to <value> */
	SGS_SI_GETINDEX, /* -- || -- */
	SGS_SI_SETINDEX, /* -- || -- */

	/* operators */
	/*
		A: (A:out, B:s1, C:s2)
		B: (A:out, B:s1)
	*/
	SGS_SI_SET,      /* B */
	SGS_SI_CLONE,
	SGS_SI_CONCAT,   /* A */
	SGS_SI_NEGATE,   /* B */
	SGS_SI_BOOL_INV,
	SGS_SI_INVERT,

	SGS_SI_INC,      /* B */
	SGS_SI_DEC,
	SGS_SI_ADD,      /* A */
	SGS_SI_SUB,
	SGS_SI_MUL,
	SGS_SI_DIV,
	SGS_SI_MOD,

	SGS_SI_AND,      /* A */
	SGS_SI_OR,
	SGS_SI_XOR,
	SGS_SI_LSH,
	SGS_SI_RSH,

	SGS_SI_SEQ,      /* A */
	SGS_SI_SNEQ,
	SGS_SI_EQ,
	SGS_SI_NEQ,
	SGS_SI_LT,
	SGS_SI_GTE,
	SGS_SI_GT,
	SGS_SI_LTE,

	/* specials */
	SGS_SI_ARRAY,    /* (A:out, B:args) */
	SGS_SI_DICT,     /* -- || -- */

	SGS_SI_COUNT,
}
sgs_Instruction;


typedef uint32_t sgs_instr_t;

/*
	instruction data: 32 bits
	OOOOOOAAAAAAAABBBBBBBBBCCCCCCCCC
	OOOOOOEEEEEEEEEEEEEEEEECCCCCCCCC
*/

#define SGS_INSTR_SIZE    4

#define SGS_INSTR_OFF_OP  0
#define SGS_INSTR_OFF_A   6
#define SGS_INSTR_OFF_B   14
#define SGS_INSTR_OFF_C   23
#define SGS_INSTR_OFF_E   6

#define SGS_INSTR_MASK_OP 0x003f /* OP: 6 bits */
#define SGS_INSTR_MASK_A  0x00ff /* A:  8 bits */
#define SGS_INSTR_MASK_B  0x01ff /* B:  9 bits */
#define SGS_INSTR_MASK_C  0x01ff /* C:  9 bits */
#define SGS_INSTR_MASK_E  0x0001ffff

#define SGS_INSTR_GET_OP( x )  (((x) >> SGS_INSTR_OFF_OP) & SGS_INSTR_MASK_OP)
#define SGS_INSTR_GET_A( x )   (((x) >> SGS_INSTR_OFF_A ) & SGS_INSTR_MASK_A )
#define SGS_INSTR_GET_B( x )   (((x) >> SGS_INSTR_OFF_B ) & SGS_INSTR_MASK_B )
#define SGS_INSTR_GET_C( x )   (((x) >> SGS_INSTR_OFF_C ) & SGS_INSTR_MASK_C )
#define SGS_INSTR_GET_E( x )   (((x) >> SGS_INSTR_OFF_E ) & SGS_INSTR_MASK_E )

#define SGS_INSTR_MAKE_OP( x ) (((x) & SGS_INSTR_MASK_OP) << SGS_INSTR_OFF_OP)
#define SGS_INSTR_MAKE_A( x )  (((x) & SGS_INSTR_MASK_A ) << SGS_INSTR_OFF_A )
#define SGS_INSTR_MAKE_B( x )  (((x) & SGS_INSTR_MASK_B ) << SGS_INSTR_OFF_B )
#define SGS_INSTR_MAKE_C( x )  (((x) & SGS_INSTR_MASK_C ) << SGS_INSTR_OFF_C )
#define SGS_INSTR_MAKE_E( x )  (((x) & SGS_INSTR_MASK_E ) << SGS_INSTR_OFF_E )

#define SGS_INSTR_MAKE( op, a, b, c ) \
	( SGS_INSTR_MAKE_OP( op ) | SGS_INSTR_MAKE_A( a ) | SGS_INSTR_MAKE_B( b ) | SGS_INSTR_MAKE_C( c ) )
#define SGS_INSTR_MAKE_EX( op, ex, c ) \
	( SGS_INSTR_MAKE_OP( op ) | SGS_INSTR_MAKE_E( ex ) | SGS_INSTR_MAKE_C( c ) )
#define SGS_INSTR_RECOMB_E( a, b ) ( ( ( b ) << 8 ) | ( a ) )


typedef struct _sgs_func_t
{
	int32_t refcount;
	int32_t size;
	int16_t instr_off;
	int8_t  gotthis;
	int8_t  numargs;
	sgs_LineNum linenum;
	uint16_t* lineinfo;
	sgs_MemBuf funcname;
	sgs_MemBuf filename;
}
sgs_func_t;
#define sgs_func_consts( pfn )   ((sgs_Variable*)(((char*)(pfn))+sizeof(sgs_func_t)))
#define sgs_func_bytecode( pfn ) ((sgs_instr_t*)(((char*)(pfn))+sizeof(sgs_func_t)+pfn->instr_off))

typedef struct _sgs_string_t
{
	int32_t refcount;
	int32_t size;
	sgs_Hash hash;
	int isconst;
}
sgs_string_t;
#define sgs_str_cstr( pstr ) (((char*)(pstr))+sizeof(sgs_string_t))
#define sgs_var_cstr( var ) sgs_str_cstr( (var)->data.S )

#define sgs_object_t sgs_VarObj


/* hash table for variables */
typedef struct _sgs_VHTableVar
{
	sgs_Variable var;
	string_t*    str;
}
sgs_VHTableVar;
typedef struct _sgs_VHTable
{
	sgs_HashTable   ht;
	sgs_VHTableVar* vars;
	int32_t     mem;
}
sgs_VHTable;

void sgs_vht_init( sgs_VHTable* vht, SGS_CTX );
void sgs_vht_free( sgs_VHTable* vht, SGS_CTX, int dco );
sgs_VHTableVar* sgs_vht_getS( sgs_VHTable* vht, sgs_string_t* S );
void sgs_vht_setS( sgs_VHTable* vht, sgs_string_t* S, sgs_Variable* var, SGS_CTX );
int sgs_vht_unsetS( sgs_VHTable* vht, sgs_string_t* S, SGS_CTX );
#define sgs_vht_size( T ) ((T)->ht.load)


/* VM interface */
void sgsVM_VarCreateString( SGS_CTX, sgs_Variable* out, const char* str, int32_t len );
void sgsVM_VarDestroyObject( SGS_CTX, sgs_object_t* O );

int sgsVM_VarSize( sgs_Variable* var );
void sgsVM_VarDump( sgs_Variable* var );

void sgsVM_StackDump( SGS_CTX );

int sgsVM_ExecFn( SGS_CTX, void* code, int32_t codesize,
	void* data, int32_t datasize, int clean, uint16_t* T );
int sgsVM_VarCall( SGS_CTX, sgs_Variable* var, int args, int expect, int gotthis );


sgs_Variable* sgsVM_VarMake_Dict();

int sgsVM_RegStdLibs( SGS_CTX );


/*
	Context handling
*/

typedef struct _sgs_BreakInfo sgs_BreakInfo;
struct _sgs_BreakInfo
{
	sgs_BreakInfo* next;
	uint32_t jdoff;  /* jump data offset */
	uint16_t numlp;  /* which loop */
	uint8_t  iscont; /* is a "continue"? */
};

typedef
struct _sgs_FuncCtx
{
	int32_t func;
	int32_t regs, lastreg;
	sgs_MemBuf vars;
	sgs_MemBuf gvars;
	int32_t loops;
	sgs_BreakInfo* binfo;
}
sgs_FuncCtx;


typedef sgs_Variable* sgs_VarPtr;

struct _sgs_Context
{
	uint32_t      version;
	uint32_t      apiversion;

	/* output */
	sgs_OutputFunc output_fn; /* output function */
	void*         output_ctx; /* output context */

	/* info output */
	int           minlev;
	sgs_PrintFunc print_fn;  /* printing function */
	void*         print_ctx; /* printing context */
	int           last_errno;

	/* hook */
	sgs_HookFunc  hook_fn;
	void*         hook_ctx;

	/* memory */
	sgs_MemFunc   memfunc;
	void*         mfuserdata;
	uint32_t      memsize;
	uint32_t      numallocs;
	uint32_t      numfrees;
	uint32_t      numblocks;

	/* compilation */
	uint32_t      state;
	sgs_FuncCtx*  fctx;      /* ByteCodeGen */
	const char*   filename;  /* filename of currently compiled code */
	sgs_HashTable stringtable; /* string constant caching hash table */

	/* virtual machine */
	sgs_VarPtr    stack_base;
	int           stack_mem;
	sgs_VarPtr    stack_off;
	sgs_VarPtr    stack_top;

	int           call_args;
	int           call_expect;
	int           call_this;
	sgs_StackFrame* sf_first;
	sgs_StackFrame* sf_last;
	int           sf_count;

	void*         data;

	sgs_object_t* objs;
	int32_t       objcount;
	uint8_t       redblue;
	sgs_VarPtr    gclist;
	int           gclist_size;
};

#define SGS_STACKFRAMESIZE  (C->stack_top - C->stack_off)


#ifdef SGS_INTERNAL_STRINGTABLES

static const char* sgs_ErrNames[] =
{
	"SUCCESS", "ENOTFND", "ECOMP", "ENOTOBJ",
	"ENOTSUP", "EBOUNDS", "EINVAL", "EINPROC"
};

static const char* sgs_VarNames[] =
{
	"null",
	"bool",
	"int",
	"real",
	"string",
	"function",
	"C function",
	"object",
};

static const char* sgs_OpNames[] =
{
	"nop",  "push", "push_nulls", "pop_n", "pop_reg",
	"return", "jump", "jump_if_true", "jump_if_false", "call",
	"for_prep", "for_load", "for_jump", "loadconst", "getvar", "setvar",
	"getprop", "setprop", "getindex", "setindex", "set",
	"clone", "concat", "negate", "bool_inv", "invert",
	"inc", "dec", "add", "sub", "mul", "div", "mod",
	"and", "or", "xor", "lsh", "rsh",
	"seq", "sneq", "eq", "neq", "lt", "gte", "gt", "lte",
	"array", "dict"
};

static const char* sgs_IfaceNames[] =
{
	"end", "destruct", "getindex", "setindex", "convert",
	"dump", "gcmark", "getnext", "call", "expr", "flags"
};

#endif


/* apicall = error codes only; printfs otherwise (VM call) */
int sgsSTD_PostInit( SGS_CTX );
int sgsSTD_MakeArray( SGS_CTX, int cnt );
int sgsSTD_MakeDict( SGS_CTX, int cnt );
int sgsSTD_GlobalInit( SGS_CTX );
int sgsSTD_GlobalFree( SGS_CTX );
int sgsSTD_GlobalGet( SGS_CTX, sgs_Variable* out, sgs_Variable* idx, int apicall );
int sgsSTD_GlobalSet( SGS_CTX, sgs_Variable* idx, sgs_Variable* val, int apicall );
int sgsSTD_GlobalGC( SGS_CTX );
int sgsSTD_GlobalIter( SGS_CTX, sgs_VHTableVar** outp, sgs_VHTableVar** outpend );



/* STRUCTS */

typedef struct sgsstd_array_header_s
{
	sgs_SizeVal size;
	sgs_SizeVal mem;
	sgs_Variable* data;
}
sgsstd_array_header_t;

typedef struct sgsstd_array_iter_s
{
	sgs_Variable ref;
	sgs_SizeVal size;
	sgs_SizeVal off;
}
sgsstd_array_iter_t;



#endif /* SGS_INT_H_INCLUDED */
