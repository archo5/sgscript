

#include <errno.h>
#include <math.h>
#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  ifndef _MSC_VER
#    define EADDRINUSE      100
#    define EADDRNOTAVAIL   101
#    define EAFNOSUPPORT    102
#    define EALREADY        103
#    define EBADMSG         104
#    define ECANCELED       105
#    define ECONNABORTED    106
#    define ECONNREFUSED    107
#    define ECONNRESET      108
#    define EDESTADDRREQ    109
#    define EHOSTUNREACH    110
#    define EIDRM           111
#    define EINPROGRESS     112
#    define EISCONN         113
#    define ELOOP           114
#    define EMSGSIZE        115
#    define ENETDOWN        116
#    define ENETRESET       117
#    define ENETUNREACH     118
#    define ENOBUFS         119
#    define ENODATA         120
#    define ENOLINK         121
#    define ENOMSG          122
#    define ENOPROTOOPT     123
#    define ENOSR           124
#    define ENOSTR          125
#    define ENOTCONN        126
#    define ENOTRECOVERABLE 127
#    define ENOTSOCK        128
#    define ENOTSUP         129
#    define EOPNOTSUPP      130
#    define EOTHER          131
#    define EOVERFLOW       132
#    define EOWNERDEAD      133
#    define EPROTO          134
#    define EPROTONOSUPPORT 135
#    define EPROTOTYPE      136
#    define ETIME           137
#    define ETIMEDOUT       138
#    define ETXTBSY         139
#    define EWOULDBLOCK     140
#  endif
#else
#  include <unistd.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  define closesocket close
#  define ioctlsocket ioctl
#endif


#ifndef MSG_CONFIRM
#define MSG_CONFIRM 0
#endif
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif
#ifndef MSG_EOR
#define MSG_EOR 0
#endif
#ifndef MSG_MORE
#define MSG_MORE 0
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif


#define SGS_INTERNAL

#include <sgs_int.h>

#define DF( x ) { #x, x }
#define STREQ( a, b ) (0==strcmp(a,b))
#define IFN( x ) { sgs_PushCFunction( C, x ); return SGS_SUCCESS; }
#define STDLIB_WARN( warn ) return sgs_Printf( C, SGS_WARNING, warn );


#ifdef _WIN32
#  define sgs_sockerror WSAGetLastError()
#  define SOCKADDR_SIZE int
#  define IOCTL_VALUE u_long
#else
#  define sgs_sockerror errno
#  define SOCKADDR_SIZE unsigned int
#  define IOCTL_VALUE int
#endif


/*
	Socket error handling
*/

#define SCKERRVN "__socket_error"

int sockassert( SGS_CTX, int test )
{
	int err = test ? 0 : sgs_sockerror;
	sgs_PushInt( C, err );
	sgs_StoreGlobal( C, SCKERRVN );
	return test;
}

int socket_error( SGS_CTX )
{
	int astext = 0, e = 0;
	SGSFN( "socket_error" );
	if( !sgs_LoadArgs( C, "|b", &astext ) )
		return 0;
	
	if( sgs_PushGlobal( C, SCKERRVN ) == SGS_SUCCESS )
		e = (int) sgs_GetInt( C, -1 );
	else if( !astext )
		sgs_PushInt( C, 0 );
	
	if( !astext )
		return 1;
	
#ifdef _WIN32
	{
		char buf[ 1024 ];
		int numwr = FormatMessageA
		(
			FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e,
			MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
			buf, 1024, NULL
		);
		if( !numwr )
			STDLIB_WARN( "failed to retrieve error message" )
		sgs_PushStringBuf( C, buf, numwr );
	}
#else
	sgs_PushString( C, strerror( e ) );
#endif
	return 1;
}

#define SOCKERR sockassert( C, 1 )
#define SOCKCLEARERR sockassert( C, 0 )


/*
	Socket address object
*/

static void* sockaddr_iface[];
#define SOCKADDR_IHDR( name ) \
	sgs_VarObj* data; \
	int method_call = sgs_Method( C ); \
	SGSFN( "socket_address." #name ); \
	if( !sgs_IsObject( C, 0, sockaddr_iface ) ) \
		return sgs_ArgErrorExt( C, 0, method_call, "socket_address", "" ); \
	data = sgs_GetObjectData( C, 0 );

#define GET_SAF ((struct sockaddr_storage*)data->data)->ss_family
#define GET_SAI ((struct sockaddr_in*)data->data)
#define GET_SAI6 ((struct sockaddr_in6*)data->data)

static int sockaddr_getindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	char* name;
	if( !sgs_ParseString( C, 0, &name, NULL ) )
		return SGS_ENOTSUP;
	
	if( STREQ( name, "family" ) ){ sgs_PushInt( C, GET_SAF ); return SGS_SUCCESS; }
	if( STREQ( name, "port" ) )
	{
		if( GET_SAF == AF_INET ){ sgs_PushInt( C, ntohs( GET_SAI->sin_port ) ); }
		else if( GET_SAF == AF_INET6 ){ sgs_PushInt( C, ntohs( GET_SAI6->sin6_port ) ); }
		else { sgs_PushNull( C ); sgs_Printf( C, SGS_WARNING, "port supported only for AF_INET[6]" ); }
		return SGS_SUCCESS;
	}
	if( STREQ( name, "addr_u32" ) )
	{
		if( GET_SAF == AF_INET ){ sgs_PushInt( C, ntohl( GET_SAI->sin_addr.s_addr ) ); }
		else { sgs_PushNull( C ); sgs_Printf( C, SGS_WARNING, "addr_u32 supported only for AF_INET" ); }
		return SGS_SUCCESS;
	}
	if( STREQ( name, "addr_buf" ) )
	{
		if( GET_SAF == AF_INET ){ sgs_PushStringBuf( C, (char*) &GET_SAI->sin_addr.s_addr, 4 ); }
		else if( GET_SAF == AF_INET6 ){ sgs_PushStringBuf( C, (char*) &GET_SAI6->sin6_addr.s6_addr, 16 ); }
		else { sgs_PushNull( C ); sgs_Printf( C, SGS_WARNING, "addr_buf supported only for AF_INET[6]" ); }
		return SGS_SUCCESS;
	}
	if( STREQ( name, "addr_bytes" ) )
	{
		char* buf = NULL;
		int i, sz = 0;
		if( GET_SAF == AF_INET )
		{
			buf = (char*) &GET_SAI->sin_addr.s_addr;
			sz = 4;
		}
		else if( GET_SAF == AF_INET6 )
		{
			buf = (char*) &GET_SAI6->sin6_addr.s6_addr;
			sz = 16;
		}
		if( buf )
		{
			for( i = 0; i < sz; ++i )
				sgs_PushInt( C, buf[ i ] );
			sgs_PushArray( C, sz );
		}
		else { sgs_PushNull( C ); sgs_Printf( C, SGS_WARNING, "addr_bytes supported only for AF_INET[6]" ); }
		return SGS_SUCCESS;
	}
	if( STREQ( name, "addr_string" ) )
	{
		char addr[ 64 ] = {0};
#ifdef _WIN32
		DWORD ioasz = 64;
		WSAAddressToString( data->data, sizeof(struct sockaddr_storage), NULL, addr, &ioasz );
		*strrchr( addr, ':' ) = 0;
#else
		if( GET_SAF == AF_INET )
			inet_ntop( GET_SAF, &GET_SAI->sin_addr, addr, 64 );
		else if( GET_SAF == AF_INET6 )
			inet_ntop( GET_SAF, &GET_SAI6->sin6_addr, addr, 64 );
#endif
		addr[ 64-1 ] = 0;
		if( *addr )
			sgs_PushString( C, addr );
		else
			sgs_PushString( C, "-" );
		return SGS_SUCCESS;
	}
	if( STREQ( name, "full_addr_string" ) )
	{
		char addr[ 64 ] = {0};
#ifdef _WIN32
		DWORD ioasz = 64;
		WSAAddressToString( data->data, sizeof(struct sockaddr_storage), NULL, addr, &ioasz );
#else
		if( GET_SAF == AF_INET || GET_SAF == AF_INET6 )
		{
			char pb[ 8 ];
			inet_ntop( GET_SAF, GET_SAF == AF_INET ? &GET_SAI->sin_addr : &GET_SAI6->sin6_addr, addr, 64 );
			sprintf( pb, ":%hu", GET_SAF == AF_INET ? &GET_SAI->sin_port : &GET_SAI6->sin6_port );
			strcat( addr, pb );
		}
#endif
		addr[ 64-1 ] = 0;
		if( *addr )
			sgs_PushString( C, addr );
		else
			sgs_PushString( C, "-" );
		return SGS_SUCCESS;
	}
	
	return SGS_ENOTFND;
}

static int sockaddr_setindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	char* name;
	if( !sgs_ParseString( C, 0, &name, NULL ) )
		return SGS_ENOTSUP;
	
	if( STREQ( name, "port" ) )
	{
		sgs_Int port;
		if( !sgs_ParseInt( C, -1, &port ) )
			return SGS_EINVAL;
		if( GET_SAF == AF_INET ) GET_SAI->sin_port = htons( port );
		else if( GET_SAF == AF_INET6 ) GET_SAI6->sin6_port = htons( port );
		return SGS_SUCCESS;
	}
	
	return SGS_ENOTFND;
}

static int sockaddr_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	if( type == SGS_CONVOP_TOTYPE )
	{
		sgs_PushString( C, "socket_address" );
		return SGS_SUCCESS;
	}
	else if( type == SVT_STRING )
	{
		sgs_PushProperty( C, "full_addr_string" );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int sockaddr_dump( SGS_CTX, sgs_VarObj* data, int depth )
{
	char buf[ 32 ];
	sgs_Variable var;
	sprintf( buf, "socket_address [family=%hu] ", GET_SAF );
	var.type = VTC_OBJECT;
	var.data.O = data;
	sgs_PushString( C, buf );
	sgs_PushVariable( C, &var );
	sgs_StringConcat( C );
	return SGS_SUCCESS;
}

static void* sockaddr_iface[] =
{
	SOP_GETINDEX, sockaddr_getindex,
	SOP_SETINDEX, sockaddr_setindex,
	SOP_CONVERT, sockaddr_convert,
	SOP_DUMP, sockaddr_dump,
	SOP_END,
};

static void push_sockaddr( SGS_CTX, struct sockaddr_storage* sa, int size )
{
	void* ss = sgs_PushObjectIPA( C, sizeof( struct sockaddr_storage ), sockaddr_iface );
	memset( ss, 0, sizeof(ss) );
	memcpy( ss, sa, size );
}


static int sgs_socket_address( SGS_CTX )
{
	struct sockaddr_storage ss;
	char* buf;
	sgs_SizeVal bufsize;
	sgs_Int af;
	uint16_t port = 0;
	
	SGSFN( "socket_address" );
	if( !sgs_LoadArgs( C, "im|+w", &af, &buf, &bufsize, &port ) )
		return 0;
	
	if( af != AF_INET && af != AF_INET6 )
		STDLIB_WARN( "argument 1 (address family)"
			" must be either AF_INET or AF_INET6" )
	
	ss.ss_family = (int16_t) af;
	port = htons( port );
	{
#ifdef _WIN32
		INT len = sizeof( ss );
		int res = sockassert( C, WSAStringToAddressA( buf, (int16_t) af,
			NULL, (struct sockaddr*) &ss, &len ) == 0 );
#else
		int res = sockassert( C, inet_pton( (int16_t) af, buf, &ss ) == 1 );
#endif
		if( !res )
			STDLIB_WARN( "failed to generate address from string" )
	}
	
	if( af == AF_INET )
	{
		struct sockaddr_in* sai = (struct sockaddr_in*) &ss;
		sai->sin_port = port;
	}
	else /* AF_INET6 */
	{
		struct sockaddr_in6* sai = (struct sockaddr_in6*) &ss;
		sai->sin6_port = port;
	}
	
	push_sockaddr( C, &ss, sizeof(ss) );
	return 1;
}

static int sgs_socket_address_frombytes( SGS_CTX )
{
	struct sockaddr_storage ss = {0};
	char* buf;
	sgs_SizeVal bufsize;
	sgs_Int af;
	uint16_t port = 0;
	
	SGSFN( "socket_address_frombytes" );
	if( !sgs_LoadArgs( C, "im|+w", &af, &buf, &bufsize, &port ) )
		return 0;
	
	if( af != AF_INET && af != AF_INET6 )
		STDLIB_WARN( "argument 1 (address family)"
			" must be either AF_INET or AF_INET6" )
	
	ss.ss_family = (int16_t) af;
	port = htons( port );
	if( af == AF_INET )
	{
		struct sockaddr_in* sai = (struct sockaddr_in*) &ss;
		if( bufsize != 4 )
			STDLIB_WARN( "argument 2 (buffer)"
				" must be 4 bytes long for an AF_INET address" )
		sai->sin_port = port;
		memcpy( &sai->sin_addr.s_addr, buf, 4 );
	}
	else /* AF_INET6 */
	{
		struct sockaddr_in6* sai = (struct sockaddr_in6*) &ss;
		if( bufsize != 16 )
			STDLIB_WARN( "argument 2 (buffer)"
				" must be 16 bytes long for an AF_INET address" )
		sai->sin6_port = port;
		memcpy( sai->sin6_addr.s6_addr, buf, 16 );
	}
	
	push_sockaddr( C, &ss, sizeof(ss) );
	return 1;
}


static int sgs_socket_gethostname( SGS_CTX )
{
	char buf[ 256 ];
	SGSFN( "socket_gethostname" );
	if( !sgs_LoadArgs( C, "." ) )
		return 0;
	if( !sockassert( C, gethostname( buf, 256 ) == 0 ) )
		STDLIB_WARN( "failed to get host name" )
	buf[ 256-1 ] = 0;
	sgs_PushString( C, buf );
	return 1;
}


/*
	Socket object
*/

static void* socket_iface[];
#define SOCK_IHDR( name ) \
	sgs_VarObj* data; \
	int method_call = sgs_Method( C ); \
	SGSFN( "socket." #name ); \
	if( !sgs_IsObject( C, 0, socket_iface ) ) \
		return sgs_ArgErrorExt( C, 0, method_call, "socket", "" ); \
	data = sgs_GetObjectData( C, 0 );

#define GET_SCK ((int)(size_t)data->data)

static int socketI_bind( SGS_CTX )
{
	sgs_Int port;
	struct sockaddr_in sa;
	int ret = 0;
	
	SOCK_IHDR( bind );
	
	if( !sgs_LoadArgs( C, "@>i", &port ) )
		return 0;
	
	memset( &sa, 0, sizeof(sa) );
	sa.sin_family = AF_INET;
	sa.sin_port = htons( port );
	sa.sin_addr.s_addr = htonl( INADDR_ANY );
	ret = bind( GET_SCK, (struct sockaddr*) &sa, sizeof(sa) );
	
	sgs_PushBool( C, sockassert( C, ret == 0 ) );
	return 1;
}

static int socketI_listen( SGS_CTX )
{
	sgs_Int queuesize;
	
	SOCK_IHDR( listen );
	
	if( !sgs_LoadArgs( C, "@>i", &queuesize ) )
		return 0;
	
	sgs_PushBool( C, sockassert( C, listen( GET_SCK, (int) queuesize ) == 0 ) );
	return 1;
}

static int socketI_accept( SGS_CTX )
{
	int S;
	struct sockaddr_storage sa = {0};
	SOCKADDR_SIZE sa_size = sizeof( sa );
	
	SOCK_IHDR( accept );
	
	if( !sgs_LoadArgs( C, "@>." ) )
		return 0;
	
	S = accept( GET_SCK, (struct sockaddr*) &sa, &sa_size );
	if( S == -1 )
	{
		SOCKERR;
		STDLIB_WARN( "failed to accept connection" )
	}
	SOCKCLEARERR;
	sgs_PushObject( C, (void*) (size_t) S, socket_iface );
	push_sockaddr( C, &sa, sa_size );
	return 2;
}

static int socketI_connect( SGS_CTX )
{
	sgs_VarObj* odt;
	
	SOCK_IHDR( connect );
	
	if( !sgs_LoadArgs( C, "@>o", sockaddr_iface, &odt ) )
		return 0;
	
	sgs_PushBool( C, sockassert( C, connect( GET_SCK,
		(struct sockaddr*) odt->data, sizeof(struct sockaddr_storage) ) != -1 ) );
	return 1;
}

static int socketI_send( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	sgs_Int flags = 0;
	int ret;
	
	SOCK_IHDR( send );
	
	if( !sgs_LoadArgs( C, "@>m|i", &str, &size, &flags ) )
		return 0;
	
	ret = send( GET_SCK, str, size, (int) flags );
	sockassert( C, ret >= 0 );
	if( ret < 0 )
		sgs_PushBool( C, 0 );
	else
		sgs_PushInt( C, ret );
	return 1;
}

static int socketI_sendto( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	sgs_Int flags = 0;
	int ret;
	sgs_VarObj* odt;
	
	SOCK_IHDR( sendto );
	
	if( !sgs_LoadArgs( C, "@>mo|i", &str, &size, sockaddr_iface, &odt, &flags ) )
		return 0;
	
	ret = sendto( GET_SCK, str, size, (int) flags,
		(struct sockaddr*) odt->data, sizeof(struct sockaddr_storage) );
	sockassert( C, ret >= 0 );
	if( ret < 0 )
		sgs_PushBool( C, 0 );
	else
		sgs_PushInt( C, ret );
	return 1;
}

static int socketI_recv( SGS_CTX )
{
	sgs_Int size, flags = 0;
	int ret;

	SOCK_IHDR( recv );
	
	if( !sgs_LoadArgs( C, "@>i|i", &size, &flags ) )
		return 0;

	sgs_PushStringBuf( C, NULL, size );
	ret = recv( GET_SCK, sgs_GetStringPtr( C, -1 ), size, flags );
	sockassert( C, ret );
	if( ret < 0 )
		sgs_PushBool( C, 0 );
	else
	{
		(C->stack_top-1)->data.S->size = ret;
		sgs_GetStringPtr( C, -1 )[ ret ] = 0;
	}
	return 1;
}

static int socketI_recvfrom( SGS_CTX )
{
	struct sockaddr_storage sa = {0};
	SOCKADDR_SIZE sa_size = sizeof( sa );
	
	sgs_Int size, flags = 0;
	int ret;

	SOCK_IHDR( recvfrom );
	
	if( !sgs_LoadArgs( C, "@>i|i", &size, &flags ) )
		return 0;

	sgs_PushStringBuf( C, NULL, size );
	ret = recvfrom( GET_SCK, sgs_GetStringPtr( C, -1 ), size, flags,
		(struct sockaddr*) &sa, &sa_size );
	sockassert( C, ret );
	if( ret < 0 )
	{
		sgs_PushBool( C, 0 );
		return 1;
	}
	else
	{
		(C->stack_top-1)->data.S->size = ret;
		sgs_GetStringPtr( C, -1 )[ ret ] = 0;
		push_sockaddr( C, &sa, sa_size );
		return 2;
	}
}

static int socketI_shutdown( SGS_CTX )
{
	sgs_Int flags;
	
	SOCK_IHDR( shutdown );
	
	if( !sgs_LoadArgs( C, "@>i", &flags ) )
		return 0;
	
	sgs_PushBool( C, sockassert( C, shutdown( GET_SCK, (int) flags ) == 0 ) );
	return 1;
}

static int socketI_close( SGS_CTX )
{
	SOCK_IHDR( close );
	
	if( !sgs_LoadArgs( C, "@>." ) )
		return 0;
	
	sgs_PushBool( C, GET_SCK != -1 && closesocket( GET_SCK ) == 0 );
	data->data = (void*) -1;
	return 1;
}

static int socketI_getpeername( SGS_CTX )
{
	struct sockaddr_storage sa = {0};
	SOCKADDR_SIZE sa_size = sizeof( sa );
	
	SOCK_IHDR( getpeername );
	
	if( !sgs_LoadArgs( C, "@>." ) )
		return 0;
	
	if( getpeername( GET_SCK, (struct sockaddr*) &sa, &sa_size ) != -1 )
	{
		SOCKCLEARERR;
		push_sockaddr( C, &sa, sa_size );
		return 1;
	}
	
	SOCKERR;
	STDLIB_WARN( "failed to get peer name" )
}

static int socket_getindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	char* name;
	if( !sgs_ParseString( C, 0, &name, NULL ) )
		return SGS_ENOTSUP;
	
	if( STREQ( name, "bind" ) ) IFN( socketI_bind )
	if( STREQ( name, "listen" ) ) IFN( socketI_listen )
	if( STREQ( name, "accept" ) ) IFN( socketI_accept )
	if( STREQ( name, "connect" ) ) IFN( socketI_connect )
	if( STREQ( name, "send" ) ) IFN( socketI_send )
	if( STREQ( name, "sendto" ) ) IFN( socketI_sendto )
	if( STREQ( name, "recv" ) ) IFN( socketI_recv )
	if( STREQ( name, "recvfrom" ) ) IFN( socketI_recvfrom )
	if( STREQ( name, "shutdown" ) ) IFN( socketI_shutdown )
	if( STREQ( name, "close" ) ) IFN( socketI_close )
	if( STREQ( name, "getpeername" ) ) IFN( socketI_getpeername )
	
	if( STREQ( name, "broadcast" ) )
	{
		int bv, bvl;
		if( !sockassert( C, getsockopt( GET_SCK, SOL_SOCKET, SO_BROADCAST, (char*) &bv, &bvl ) != -1 ) )
		{
			sgs_Printf( C, SGS_WARNING, "failed to retrieve the 'broadcast' property of a socket" );
			sgs_PushNull( C );
		}
		else
			sgs_PushBool( C, bv );
		return SGS_SUCCESS;
	}
	if( STREQ( name, "reuse_addr" ) )
	{
		int bv, bvl;
		if( !sockassert( C, getsockopt( GET_SCK, SOL_SOCKET, SO_REUSEADDR, (char*) &bv, &bvl ) != -1 ) )
		{
			sgs_Printf( C, SGS_WARNING, "failed to retrieve the 'reuse_addr' property of a socket" );
			sgs_PushNull( C );
		}
		else
			sgs_PushBool( C, bv );
		return SGS_SUCCESS;
	}
	
	return SGS_ENOTFND;
}

static int socket_setindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	char* name;
	if( !sgs_ParseString( C, 0, &name, NULL ) )
		return SGS_ENOTSUP;
	
	if( STREQ( name, "blocking" ) )
	{
		int bv;
		IOCTL_VALUE inbv;
		if( !sgs_ParseBool( C, -1, &bv ) )
			return SGS_EINVAL;
		inbv = !bv;
		if( !sockassert( C, ioctlsocket( GET_SCK, FIONBIO, &inbv ) != -1 ) )
			sgs_Printf( C, SGS_WARNING, "failed to set the 'blocking' property of a socket" );
		return SGS_SUCCESS;
	}
	if( STREQ( name, "broadcast" ) )
	{
		int bv;
		if( !sgs_ParseBool( C, -1, &bv ) )
			return SGS_EINVAL;
		if( !sockassert( C, setsockopt( GET_SCK, SOL_SOCKET, SO_BROADCAST, (char*) &bv, sizeof(bv) ) != -1 ) )
			sgs_Printf( C, SGS_WARNING, "failed to set the 'broadcast' property of a socket" );
		return SGS_SUCCESS;
	}
	if( STREQ( name, "reuse_addr" ) )
	{
		int bv;
		if( !sgs_ParseBool( C, -1, &bv ) )
			return SGS_EINVAL;
		if( !sockassert( C, setsockopt( GET_SCK, SOL_SOCKET, SO_REUSEADDR, (char*) &bv, sizeof(bv) ) != -1 ) )
			sgs_Printf( C, SGS_WARNING, "failed to set the 'reuse_addr' property of a socket" );
		return SGS_SUCCESS;
	}
	
	return SGS_ENOTFND;
}

static int socket_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	if( type == SGS_CONVOP_TOTYPE || type == SVT_STRING )
	{
		sgs_PushString( C, "socket" );
		return SGS_SUCCESS;
	}
	else if( type == SVT_BOOL )
	{
		sgs_PushBool( C, GET_SCK != -1 );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int socket_destruct( SGS_CTX, sgs_VarObj* data, int dco )
{
	UNUSED( dco );
	if( GET_SCK != -1 )
		closesocket( GET_SCK );
	return SGS_SUCCESS;
}

static void* socket_iface[] =
{
	SOP_GETINDEX, socket_getindex,
	SOP_SETINDEX, socket_setindex,
	SOP_CONVERT, socket_convert,
	SOP_DESTRUCT, socket_destruct,
	SOP_END,
};

static int sgs_socket( SGS_CTX )
{
	int S;
	sgs_Int domain, type, protocol;
	
	SGSFN( "socket" );
	
	if( !sgs_LoadArgs( C, "iii", &domain, &type, &protocol ) )
		return 0;
	
	S = socket( domain, type, protocol );
	if( S < 0 )
	{
		SOCKERR;
		STDLIB_WARN( "failed to create socket" )
	}
	SOCKCLEARERR;
	
	sgs_PushObject( C, (void*) (size_t) S, socket_iface );
	return 1;
}

static int sgs_socket_select( SGS_CTX )
{
	struct timeval tv;
	sgs_Real timeout = 0;
	sgs_SizeVal szR, szW, szE, i;
	fd_set setR, setW, setE;
	sgs_VarObj* data;
	int maxsock = 0, ret;
	
	SGSFN( "socket_select" );
	
	if( !sgs_LoadArgs( C, "aaa|r", &szR, &szW, &szE, &timeout ) )
		return 0;
	
	if( timeout < 0 )
		STDLIB_WARN( "argument 4 (timeout) cannot be negative" )
	
	FD_ZERO( &setR );
	FD_ZERO( &setW );
	FD_ZERO( &setE );
	
	for( i = 0; i < szR; ++i )
	{
		sgs_PushNumIndex( C, 0, i );
		if( !sgs_IsObject( C, -1, socket_iface ) )
			return sgs_Printf( C, SGS_WARNING, "item #%d of 'read' array is not a socket", i + 1 );
		data = sgs_GetObjectData( C, -1 );
		if( GET_SCK == -1 )
			return sgs_Printf( C, SGS_WARNING, "item #%d of 'read' array is not an open socket", i + 1 );
		FD_SET( GET_SCK, &setR );
		if( GET_SCK > maxsock )
			maxsock = GET_SCK;
		sgs_Pop( C, 1 );
	}
	
	for( i = 0; i < szW; ++i )
	{
		sgs_PushNumIndex( C, 1, i );
		if( !sgs_IsObject( C, -1, socket_iface ) )
			return sgs_Printf( C, SGS_WARNING, "item #%d of 'write' array is not a socket", i + 1 );
		data = sgs_GetObjectData( C, -1 );
		if( GET_SCK == -1 )
			return sgs_Printf( C, SGS_WARNING, "item #%d of 'write' array is not an open socket", i + 1 );
		FD_SET( GET_SCK, &setW );
		if( GET_SCK > maxsock )
			maxsock = GET_SCK;
		sgs_Pop( C, 1 );
	}
	
	for( i = 0; i < szE; ++i )
	{
		sgs_PushNumIndex( C, 2, i );
		if( !sgs_IsObject( C, -1, socket_iface ) )
			return sgs_Printf( C, SGS_WARNING, "item #%d of 'error' array is not a socket", i + 1 );
		data = sgs_GetObjectData( C, -1 );
		if( GET_SCK == -1 )
			return sgs_Printf( C, SGS_WARNING, "item #%d of 'error' array is not an open socket", i + 1 );
		FD_SET( GET_SCK, &setE );
		if( GET_SCK > maxsock )
			maxsock = GET_SCK;
		sgs_Pop( C, 1 );
	}
	
	tv.tv_sec = floor( timeout );
	tv.tv_usec = ( timeout - (sgs_Real) tv.tv_sec ) * 1000000;
	ret = select( maxsock + 1, &setR, &setW, &setE, sgs_StackSize( C ) >= 4 ? &tv : NULL );
	sockassert( C, ret != -1 );
	
	for( i = 0; i < szR; ++i )
	{
		sgs_PushNumIndex( C, 0, i );
		data = sgs_GetObjectData( C, -1 );
		if( !FD_ISSET( GET_SCK, &setR ) )
		{
			sgs_PushProperty( C, "erase" );
			sgs_PushInt( C, i );
			sgs_ThisCall( C, 1, 0 );
			i--; szR--;
		}
		sgs_Pop( C, 1 );
	}
	
	for( i = 0; i < szW; ++i )
	{
		sgs_PushNumIndex( C, 1, i );
		data = sgs_GetObjectData( C, -1 );
		if( !FD_ISSET( GET_SCK, &setW ) )
		{
			sgs_PushProperty( C, "erase" );
			sgs_PushInt( C, i );
			sgs_ThisCall( C, 1, 0 );
			i--; szW--;
		}
		sgs_Pop( C, 1 );
	}
	
	for( i = 0; i < szE; ++i )
	{
		sgs_PushNumIndex( C, 2, i );
		data = sgs_GetObjectData( C, -1 );
		if( !FD_ISSET( GET_SCK, &setE ) )
		{
			sgs_PushProperty( C, "erase" );
			sgs_PushInt( C, i );
			sgs_ThisCall( C, 1, 0 );
			i--; szE--;
		}
		sgs_Pop( C, 1 );
	}
	
	sgs_PushInt( C, ret );
	return 1;
}


static sgs_RegFuncConst f_sock[] =
{
	{ "socket_error", socket_error },
	{ "socket_address", sgs_socket_address },
	{ "socket_address_frombytes", sgs_socket_address_frombytes },
	{ "socket_gethostname", sgs_socket_gethostname },
	{ "socket", sgs_socket },
	{ "socket_select", sgs_socket_select },
};

static sgs_RegIntConst i_sock[] =
{
	DF( PF_INET ), DF( PF_INET6 ), DF( PF_UNIX ), DF( PF_IPX ),
	DF( AF_INET ), DF( AF_INET6 ), DF( AF_UNIX ), DF( AF_IPX ),
	DF( SOCK_STREAM ), DF( SOCK_DGRAM ), DF( SOCK_SEQPACKET ), DF( SOCK_RAW ),
	DF( IPPROTO_TCP ), DF( IPPROTO_UDP ),
	
	DF( MSG_CONFIRM ), DF( MSG_DONTROUTE ), DF( MSG_DONTWAIT ), DF( MSG_EOR ),
	DF( MSG_MORE ), DF( MSG_NOSIGNAL ), DF( MSG_OOB ),
	
	{ "SHUT_RD", 0 }, { "SHUT_WR", 1 }, { "SHUT_RDWR", 2 },
	{ "SD_RECEIVE", 0 }, { "SD_SEND", 1 }, { "SD_BOTH", 2 },
};


#ifdef SGS_COMPILE_MODULE
#  define sockets_module_entry_point sgscript_main
#endif


#ifdef _WIN32
__declspec(dllexport)
#endif
int sockets_module_entry_point( SGS_CTX )
{
	int ret;
#ifdef _WIN32
	WSADATA wsadata;
#endif
	
	sgs_PushInt( C, 0 );
	sgs_StoreGlobal( C, SCKERRVN );
	
	sgs_RegisterType( C, "socket", socket_iface );
	sgs_RegisterType( C, "socket_address", sockaddr_iface );
	
	ret = sgs_RegFuncConsts( C, f_sock, sizeof(f_sock) / sizeof(f_sock[0]) );
	if( ret != SGS_SUCCESS ) return ret;
	
	ret = sgs_RegIntConsts( C, i_sock, sizeof(i_sock) / sizeof(i_sock[0]) );
	if( ret != SGS_SUCCESS ) return ret;
	
#ifdef _WIN32
	ret = WSAStartup( MAKEWORD( 2, 0 ), &wsadata );
	if( ret != 0 )
		return SGS_EINPROC;
#endif
	
	return SGS_SUCCESS;
}

/*
	in case someone wants to compile it like a header file...
*/
#undef GET_SCK
#undef GET_SAF
#undef GET_SAI
#undef GET_SAI6
#undef DF
#undef STREQ
#undef IFN
#undef STDLIB_WARN
#undef SOCKADDR_SIZE
#undef SCKERRVN

