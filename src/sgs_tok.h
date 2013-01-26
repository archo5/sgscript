
#ifndef SGS_TOK_H_INCLUDED
#define SGS_TOK_H_INCLUDED

#include "sgs_cfg.h"
#include "sgs_util.h"
#include "sgscript.h"

/*
	Token stream
	<1 byte: type> <additional data> <2 bytes: line number>
*/
/* special          id */
#define ST_NULL	    '\0'
#define ST_RBRKL    '('
#define ST_RBRKR    ')'
#define ST_SBRKL    '['
#define ST_SBRKR    ']'
#define ST_CBRKL    '{'
#define ST_CBRKR    '}'
#define ST_ARGSEP   ','
#define ST_STSEP    ';'
#define ST_PICKSEP  ':'
/* other            id    additional data */
#define ST_IDENT    'N'	/* 1 byte (string size), N bytes (string), not null-terminated */
#define ST_KEYWORD  'K'	/* same as IDENT */
#define ST_NUMREAL  'R'	/* 8 bytes (double) */
#define ST_NUMINT   'I'	/* 8 bytes (int64) */
#define ST_STRING   'S'	/* 4 bytes (string size), N bytes (string), not null-terminated */
/* operators        id    type  */
#define ST_OP_SEQ   200	/* ===  */
#define ST_OP_SNEQ  201	/* !==  */
#define ST_OP_EQ    202	/* ==   */
#define ST_OP_NEQ   203	/* !=   */
#define ST_OP_LEQ   204	/* <=   */
#define ST_OP_GEQ   205	/* >=   */
#define ST_OP_LESS  206	/* <    */
#define ST_OP_GRTR  207	/* >    */
#define ST_OP_ADDEQ 208	/* +=   */
#define ST_OP_SUBEQ 209	/* -=   */
#define ST_OP_MULEQ 210	/* *=   */
#define ST_OP_DIVEQ 211	/* /=   */
#define ST_OP_MODEQ 212	/* %=   */
#define ST_OP_ANDEQ 213	/* &=   */
#define ST_OP_OREQ  214	/* |=   */
#define ST_OP_XOREQ 215	/* ^=   */
#define ST_OP_LSHEQ 216	/* <<=  */
#define ST_OP_RSHEQ 217	/* >>=  */
#define ST_OP_BLAEQ 218 /* &&=  */
#define ST_OP_BLOEQ 219	/* ||=  */
#define ST_OP_CATEQ 220	/* $=   */
#define ST_OP_SET   221	/* =    */
#define ST_OP_COPY  222	/* =&   */
#define ST_OP_BLAND 223	/* &&   */
#define ST_OP_BLOR  224	/* ||   */
#define ST_OP_ADD   225	/* +    */
#define ST_OP_SUB   226	/* -    */
#define ST_OP_MUL   227	/* *    */
#define ST_OP_DIV   228	/* /    */
#define ST_OP_MOD   229	/* %    */
#define ST_OP_AND   230	/* &    */
#define ST_OP_OR    231	/* |    */
#define ST_OP_XOR   232	/* ^    */
#define ST_OP_LSH   233 /* <<   */
#define ST_OP_RSH   234	/* >>   */
#define ST_OP_MMBR  235	/* .    */
#define ST_OP_CAT   236	/* $    */
#define ST_OP_NOT   237	/* !    */
#define ST_OP_INV   238	/* ~    */
#define ST_OP_INC   239	/* ++   */
#define ST_OP_DEC   240	/* --   */

#define ST_ISOP( chr )      ( (chr) >= 200 && (chr) <= 240 )
#define ST_OP_UNARY( chr )  ( (chr) == ST_OP_ADD || (chr) == ST_OP_SUB || ( (chr) >= ST_OP_NOT && (chr) <= ST_OP_DEC ) )
#define ST_OP_BINARY( chr ) ( (chr) >= 200 && (chr) <= 236 )
#define ST_OP_ASSIGN( chr ) ( (chr) >= 208 && (chr) <= 222 )
#define ST_OP_BINMUL( chr ) ( (chr) == ST_OP_MUL || (chr) == ST_OP_DIV || (chr) == ST_OP_MOD )
#define ST_OP_BINADD( chr ) ( (chr) == ST_OP_ADD || (chr) == ST_OP_SUB )
#define ST_OP_BINOPS( chr ) ( (chr) >= ST_OP_AND && (chr) <= ST_OP_RSH )
#define ST_OP_COMP( chr )   ( (chr) >= 200 && (chr) <= 207 )
#define ST_OP_BOOL( chr )   ( (chr) == ST_OP_BLAEQ || (chr) == ST_OP_BLOEQ || (chr) == ST_OP_BLAND || (chr) == ST_OP_BLOR )

#define ST_ISSPEC( chr )    isoneof( (chr), "()[]{},;:" )

#define ST_READINT( pos )   (*(int32_t*)( pos ))
#define ST_READLN( pos )    (*(LineNum*)( pos ))


typedef unsigned char TokenType;
typedef unsigned char* TokenList;

TokenList   sgsT_Gen( SGS_CTX, const char* code, int32_t length );
void        sgsT_Free( TokenList tlist );
TokenList   sgsT_Next( TokenList tok );
LineNum     sgsT_LineNum( TokenList tok );

int32_t     sgsT_ListSize( TokenList tlist );
int32_t     sgsT_ListMemSize( TokenList tlist );

#ifdef SGS_DEBUG
void        sgsT_DumpToken( TokenList tok );
void        sgsT_DumpList( TokenList tlist, TokenList tend );
#endif


#endif /* SGS_TOK_H_INCLUDED */
