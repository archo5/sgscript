
#ifndef SGS_DBGSERVER_H_INCLUDED
#define SGS_DBGSERVER_H_INCLUDED


#ifdef __cplusplus
extern "C" {
#endif


#ifndef HEADER_SGSCRIPT_H
# define HEADER_SGSCRIPT_H <sgscript.h>
#endif
#include HEADER_SGSCRIPT_H
#ifndef HEADER_SGS_UTIL_H
# define HEADER_SGS_UTIL_H <sgs_util.h>
#endif
#include HEADER_SGS_UTIL_H

typedef struct sgs_DebugServer sgs_DebugServer;

#define SGS_DBGSRV_PORT_DISABLE 0 /* run without network, in console mode */
#define SGS_DBGSRV_PORT_DEFAULT 7473

sgs_DebugServer* sgs_CreateDebugServer( SGS_CTX, int port );
void sgs_CloseDebugServer( sgs_DebugServer* D );
void sgs_DebugServerCmd( sgs_DebugServer* D, const char* cmd );


#ifdef __cplusplus
}
#endif

#endif
