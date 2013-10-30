

#ifndef __SGS_CPPBC_H__
#define __SGS_CPPBC_H__

#include <sgscript.h>
#include <new>

#ifndef SGS_CPPBC_NO_STD_STRING
#  include <string>
#endif


#ifndef SGS_CPPBC_PROCESS
# define SGS_OBJECT \
	static int _sgs_convert( SGS_CTX, sgs_VarObj* obj, int param ); \
	static int _sgs_getindex( SGS_CTX, sgs_VarObj* obj, int param ); \
	static int _sgs_setindex( SGS_CTX, sgs_VarObj* obj, int param ); \
	static int _sgs_create( SGS_CTX ); \
	static sgs_ObjCallback _sgs_interface[];
# define SGS_METHOD
# define SGS_PROPERTY
# define READ
# define WRITE
#endif


// TODO: integrate these two into main API header
inline void sgs_ObjAddRef( SGS_CTX, sgs_VarObj* obj )
{
	sgs_Variable var;
	var.type = SGS_VTC_OBJECT;
	var.data.O = obj;
	sgs_Acquire( C, &var );
}

inline void sgs_ObjRelease( SGS_CTX, sgs_VarObj* obj )
{
	sgs_Variable var;
	var.type = SGS_VTC_OBJECT;
	var.data.O = obj;
	sgs_Release( C, &var );
}

template< class T >
class sgsHandle
{
public:
	
	sgsHandle(){};
	sgsHandle( sgs_VarObj* obj, sgs_Context* c )
	{
		if( obj->iface == T::_sgs_interface )
		{
			object = obj;
			C = c;
			_acquire();
		}
	}
	~sgsHandle(){ _release(); }
	
	const sgsHandle& operator = ( const sgsHandle& h )
	{
		_release();
		if( h.object->iface == T::_sgs_interface )
		{
			object = h.object;
			C = h.C;
			_acquire();
		}
	}
	operator T*(){ return static_cast<T*>( object->data ); }
	T* operator -> (){ return static_cast<T*>( object->data ); }
	
	bool operator < ( const sgsHandle& h ) const { return object < h.object; }
	
	
	sgs_VarObj* object;
	
protected:
	void _acquire(){ if( object ) sgs_ObjAddRef( C, object ); }
	void _release(){ if( object ){ sgs_ObjRelease( C, object ); object = NULL; } }
	SGS_CTX;
	
};

template< class T >
inline void sgs_PushHandle( SGS_CTX, const sgsHandle<T>& val )
{
	sgs_Variable var;
	var.type = SGS_VTC_OBJECT;
	var.data.O = val.object;
	sgs_PushVariable( C, &var );
}


template< class T > void sgs_PushVar( SGS_CTX, const T& );
template< class T > inline void sgs_PushVar( SGS_CTX, T* v ){ sgs_PushClass( C, v ); }
template< class T > inline void sgs_PushVar( SGS_CTX, sgsHandle<T> v ){ sgs_PushHandle( C, v ); }
#define SGS_DECL_PUSHVAR( type, parsefn ) template<> inline void sgs_PushVar<type>( SGS_CTX, const type& v ){ parsefn( C, v ); }
SGS_DECL_PUSHVAR( bool, sgs_PushBool );
#define SGS_DECL_PUSHVAR_INT( type ) SGS_DECL_PUSHVAR( type, sgs_PushInt )
SGS_DECL_PUSHVAR_INT( signed char );
SGS_DECL_PUSHVAR_INT( unsigned char );
SGS_DECL_PUSHVAR_INT( signed short );
SGS_DECL_PUSHVAR_INT( unsigned short );
SGS_DECL_PUSHVAR_INT( signed int );
SGS_DECL_PUSHVAR_INT( unsigned int );
SGS_DECL_PUSHVAR_INT( signed long );
SGS_DECL_PUSHVAR_INT( unsigned long );
SGS_DECL_PUSHVAR_INT( signed long long );
SGS_DECL_PUSHVAR( float, sgs_PushReal );
SGS_DECL_PUSHVAR( double, sgs_PushReal );
//SGS_DECL_PUSHVAR( char*, sgs_PushString );
SGS_DECL_PUSHVAR( sgs_CFunc, sgs_PushCFunction );
#ifndef SGS_CPPBC_NO_STD_STRING
template<> inline void sgs_PushVar<std::string>( SGS_CTX, const std::string& v ){ sgs_PushStringBuf( C, v.c_str(), v.size() ); }
#endif


template< class T > struct sgs_GetVar {
	T operator () ( SGS_CTX, int item );
};
template< class T > struct sgs_GetVarObj { T* operator () ( SGS_CTX, int item )
{
	if( sgs_IsObject( C, item, T::_sgs_interface ) )
		return static_cast<T*>( sgs_GetObjectData( C, item ) );
	return NULL;
}};
template<> struct sgs_GetVar<bool> { bool operator () ( SGS_CTX, int item )
{
	sgs_Bool v;
	if( sgs_ParseBool( C, item, &v ) )
		return v;
	return false;
}};
#define SGS_DECL_LOADVAR_INT( type ) \
	template<> struct sgs_GetVar<type> { type operator () ( SGS_CTX, int item ){ \
		sgs_Int v; if( sgs_ParseInt( C, item, &v ) ) return v; return 0; }};
SGS_DECL_LOADVAR_INT( signed char );
SGS_DECL_LOADVAR_INT( unsigned char );
SGS_DECL_LOADVAR_INT( signed short );
SGS_DECL_LOADVAR_INT( unsigned short );
SGS_DECL_LOADVAR_INT( signed int );
SGS_DECL_LOADVAR_INT( unsigned int );
SGS_DECL_LOADVAR_INT( signed long );
SGS_DECL_LOADVAR_INT( unsigned long );
SGS_DECL_LOADVAR_INT( signed long long );
template<> struct sgs_GetVar<float> { float operator () ( SGS_CTX, int item ){
	sgs_Real v; if( sgs_ParseReal( C, item, &v ) ) return v; return 0; }};
template<> struct sgs_GetVar<double> { double operator () ( SGS_CTX, int item ){
	sgs_Real v; if( sgs_ParseReal( C, item, &v ) ) return v; return 0; }};
template<> struct sgs_GetVar<char*> { char* operator () ( SGS_CTX, int item ){
	char* str = NULL; sgs_ParseString( C, item, &str, NULL ); return str; }};
template<> struct sgs_GetVar<std::string> { std::string operator () ( SGS_CTX, int item ){
	char* str; sgs_SizeVal size; if( sgs_ParseString( C, item, &str, &size ) )
		return std::string( str, size ); return std::string(); }};
template< class O >
struct sgs_GetVar< sgsHandle<O> >
{
	sgsHandle<O> operator () ( SGS_CTX, int item ) const
	{
		if( sgs_IsObject( C, item, O::_sgs_interface ) )
			return sgsHandle<O>( sgs_GetObjectStruct( C, item ), C );
		return sgsHandle<O>( NULL, C );
	}
};


template< class T > void sgs_PushClass( SGS_CTX, T* inst )
{
	sgs_PushObject( C, inst, T::_sgs_interface );
}

template< class T > T* sgs_PushClassIPA( SGS_CTX )
{
	return static_cast<T*>( sgs_PushObjectIPA( C, sizeof(T), T::_sgs_interface ) );
}

#define SGS_PUSHCLASS( C, name, args ) \
	(new (sgs_PushClassIPA< name >( C )) name args )


#endif // __SGS_CPPBC_H__

