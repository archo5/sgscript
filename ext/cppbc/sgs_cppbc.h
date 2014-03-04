

#ifndef __SGS_CPPBC_H__
#define __SGS_CPPBC_H__

#include <sgscript.h>
#include <new>
#include <assert.h>

#ifndef SGS_CPPBC_NO_STD_STRING
#  include <string>
#endif


#ifndef SGS_CPPBC_PROCESS
# define SGS_OBJECT \
	static int _sgs_destruct( SGS_CTX, sgs_VarObj* obj ); \
	static int _sgs_convert( SGS_CTX, sgs_VarObj* obj, int type ); \
	static int _sgs_getindex( SGS_CTX, sgs_VarObj* obj, sgs_Variable* key, int isprop ); \
	static int _sgs_setindex( SGS_CTX, sgs_VarObj* obj, sgs_Variable* key, sgs_Variable* val, int isprop ); \
	static sgs_ObjInterface _sgs_interface[1]; \
	sgs_VarObj* m_sgsObject; \
	SGS_CTX;
# define SGS_METHOD
# define SGS_PROPERTY
# define SGS_PROPERTY_FUNC( funcs )
# define READ
# define WRITE
# define READ_CALLBACK
# define WRITE_CALLBACK
# define SGS_IFUNC( type ) static
# define SGS_ALIAS( func )
#endif


template< class T >
class sgsHandle
{
public:
	
	sgsHandle() : object(NULL), C(NULL) {};
	sgsHandle( const sgsHandle& h ) : object(h.object), C(h.C)
	{
		if( object )
			_acquire();
	}
	sgsHandle( sgs_VarObj* obj, sgs_Context* c ) : object(NULL), C(NULL)
	{
		if( obj && obj->iface == T::_sgs_interface )
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
		if( h.object && h.object->iface == T::_sgs_interface )
		{
			object = h.object;
			C = h.C;
			_acquire();
		}
		return *this;
	}
	
	operator T*(){ return object ? static_cast<T*>( object->data ) : NULL; }
	operator const T*() const { return object ? static_cast<T*>( object->data ) : NULL; }
	
	T* operator -> (){ return object ? static_cast<T*>( object->data ) : NULL; }
	const T* operator -> () const { return object ? static_cast<T*>( object->data ) : NULL; }
	
	bool operator < ( const sgsHandle& h ) const { return object < h.object; }
	bool operator == ( const sgsHandle& h ) const { return object == h.object; }
	
	SGSRESULT gcmark() const { if( !object ){ return SGS_SUCCESS; } return sgs_ObjGCMark( C, object ); }
	
	sgs_VarObj* object;
	
protected:
	void _acquire(){ if( object ) sgs_ObjAcquire( C, object ); }
	void _release(){ if( object ){ sgs_ObjRelease( C, object ); object = NULL; } }
	SGS_CTX;
	
};

template< class T >
inline void sgs_PushHandle( SGS_CTX, const sgsHandle<T>& val )
{
	if( !val.object )
		sgs_PushNull( C );
	else
		sgs_PushObjectPtr( C, val.object );
}


class sgsVariable
{
public:
	
	sgsVariable() : C(NULL) { var.type = SGS_VT_NULL; };
	sgsVariable( const sgsVariable& h ) : var(h.var), C(h.C)
	{
		if( h.var.type != SGS_VT_NULL )
		{
			var = h.var;
			C = h.C;
			_acquire();
		}
	}
	sgsVariable( sgs_Context* c ) : C(c) { var.type = SGS_VT_NULL; }
	sgsVariable( const sgs_Variable& v, sgs_Context* c )
	{
		if( v.type != SGS_VT_NULL )
		{
			var = v;
			C = c;
			_acquire();
		}
	}
	sgsVariable( sgs_Context* c, int item ) : C(c)
	{
		var.type = SGS_VT_NULL;
		sgs_PeekStackItem( C, item, &var );
		_acquire();
	}
	sgsVariable( sgs_Context* c, sgs_Variable* v ) : C(c)
	{
		var = *v;
		_acquire();
	}
	~sgsVariable(){ _release(); }
	
	const sgsVariable& operator = ( const sgsVariable& h )
	{
		_release();
		if( h.var.type != SGS_VT_NULL )
		{
			var = h.var;
			C = h.C;
			_acquire();
		}
		return *this;
	}
	
	bool operator < ( const sgsVariable& h ) const { return var.type < h.var.type || var.data.I < h.var.data.I; }
	bool operator == ( const sgsVariable& h ) const { return var.type == h.var.type && var.data.I == h.var.data.I; }
	
	void push( sgs_Context* c = NULL ) const { if( C ){ c = C; assert( C ); } else { assert( c ); } sgs_PushVariable( c, const_cast<sgs_Variable*>( &var ) ); }
	SGSRESULT gcmark() { if( !C ) return SGS_SUCCESS; return sgs_GCMark( C, &var ); }
	bool not_null(){ return var.type != SGS_VT_NULL; }
	
	sgs_Variable var;
	SGS_CTX;
	
	void _acquire(){ if( C ){ sgs_Acquire( C, &var ); } }
	void _release(){ if( C ){ sgs_Release( C, &var ); var.type = SGS_VT_NULL; } }
	
};


/* PushVar [stack] */
template< class T > void sgs_PushVar( SGS_CTX, const T& );
template< class T > inline void sgs_PushVar( SGS_CTX, T* v ){ sgs_PushClass( C, v ); }
template< class T > inline void sgs_PushVar( SGS_CTX, sgsHandle<T> v ){ sgs_PushHandle( C, v ); }
template<> inline void sgs_PushVar<sgsVariable>( SGS_CTX, const sgsVariable& v ){ v.push( C ); }
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


/* GETVAR [stack] */
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
#define SGS_DECL_GETVAR_INT( type ) \
	template<> struct sgs_GetVar<type> { type operator () ( SGS_CTX, int item ){ \
		sgs_Int v; if( sgs_ParseInt( C, item, &v ) ) return (type) v; return 0; }};
SGS_DECL_GETVAR_INT( signed char );
SGS_DECL_GETVAR_INT( unsigned char );
SGS_DECL_GETVAR_INT( signed short );
SGS_DECL_GETVAR_INT( unsigned short );
SGS_DECL_GETVAR_INT( signed int );
SGS_DECL_GETVAR_INT( unsigned int );
SGS_DECL_GETVAR_INT( signed long );
SGS_DECL_GETVAR_INT( unsigned long );
SGS_DECL_GETVAR_INT( signed long long );
template<> struct sgs_GetVar<float> { float operator () ( SGS_CTX, int item ){
	sgs_Real v; if( sgs_ParseReal( C, item, &v ) ) return (float) v; return 0; }};
template<> struct sgs_GetVar<double> { double operator () ( SGS_CTX, int item ){
	sgs_Real v; if( sgs_ParseReal( C, item, &v ) ) return (double) v; return 0; }};
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
template<> struct sgs_GetVar< sgsVariable > { sgsVariable operator () ( SGS_CTX, int item ) const {
	return sgsVariable( C, item ); } };


/* GETVARP [pointer] */
template< class T > struct sgs_GetVarP {
	T operator () ( SGS_CTX, sgs_Variable* var );
};
template< class T > struct sgs_GetVarObjP { T* operator () ( SGS_CTX, sgs_Variable* var )
{
	if( sgs_IsObjectP( var, T::_sgs_interface ) )
		return static_cast<T*>( sgs_GetObjectDataP( var ) );
	return NULL;
}};
template<> struct sgs_GetVarP<bool> { bool operator () ( SGS_CTX, sgs_Variable* var )
{
	sgs_Bool v;
	if( sgs_ParseBoolP( C, var, &v ) )
		return v;
	return false;
}};
#define SGS_DECL_GETVARP_INT( type ) \
	template<> struct sgs_GetVarP<type> { type operator () ( SGS_CTX, sgs_Variable* var ){ \
		sgs_Int v; if( sgs_ParseIntP( C, var, &v ) ) return (type) v; return 0; }};
SGS_DECL_GETVARP_INT( signed char );
SGS_DECL_GETVARP_INT( unsigned char );
SGS_DECL_GETVARP_INT( signed short );
SGS_DECL_GETVARP_INT( unsigned short );
SGS_DECL_GETVARP_INT( signed int );
SGS_DECL_GETVARP_INT( unsigned int );
SGS_DECL_GETVARP_INT( signed long );
SGS_DECL_GETVARP_INT( unsigned long );
SGS_DECL_GETVARP_INT( signed long long );
template<> struct sgs_GetVarP<float> { float operator () ( SGS_CTX, sgs_Variable* var ){
	sgs_Real v; if( sgs_ParseRealP( C, var, &v ) ) return (float) v; return 0; }};
template<> struct sgs_GetVarP<double> { double operator () ( SGS_CTX, sgs_Variable* var ){
	sgs_Real v; if( sgs_ParseRealP( C, var, &v ) ) return (double) v; return 0; }};
template<> struct sgs_GetVarP<char*> { char* operator () ( SGS_CTX, sgs_Variable* var ){
	char* str = NULL; sgs_ParseStringP( C, var, &str, NULL ); return str; }};
template<> struct sgs_GetVarP<std::string> { std::string operator () ( SGS_CTX, sgs_Variable* var ){
	char* str; sgs_SizeVal size; if( sgs_ParseStringP( C, var, &str, &size ) )
		return std::string( str, size ); return std::string(); }};
template< class O >
struct sgs_GetVarP< sgsHandle<O> >
{
	sgsHandle<O> operator () ( SGS_CTX, sgs_Variable* var ) const
	{
		if( sgs_IsObjectP( var, O::_sgs_interface ) )
			return sgsHandle<O>( sgs_GetObjectStructP( var ), C );
		return sgsHandle<O>( NULL, C );
	}
};
template<> struct sgs_GetVarP< sgsVariable > { sgsVariable operator () ( SGS_CTX, sgs_Variable* var ) const {
	return sgsVariable( C, var ); } };


template< class T > void sgs_PushClass( SGS_CTX, T* inst )
{
	sgs_PushObject( C, inst, T::_sgs_interface );
	inst->m_sgsObject = sgs_GetObjectStruct( C, -1 );
	inst->C = C;
}

template< class T > T* sgs_PushClassIPA( SGS_CTX )
{
	T* data = static_cast<T*>( sgs_PushObjectIPA( C, sizeof(T), T::_sgs_interface ) );
	data->m_sgsObject = sgs_GetObjectStruct( C, -1 );
	data->C = C;
	return data;
}

template< class T > T* sgs_PushClassFrom( SGS_CTX, T* inst )
{
	T* data = static_cast<T*>( sgs_PushObjectIPA( C, sizeof(T), T::_sgs_interface ) );
	inst->m_sgsObject = sgs_GetObjectStruct( C, -1 );
	inst->C = C;
	*data = *inst;
	return data;
}

template< class T> T* sgs_InitPushedClass( T* inst, SGS_CTX )
{
	inst->C = C;
	inst->m_sgsObject = sgs_GetObjectStruct( C, -1 );
	return inst;
}

#define SGS_PUSHCLASS( C, name, args ) \
	sgs_InitPushedClass(new (sgs_PushClassIPA< name >( C )) name args, C )


#endif // __SGS_CPPBC_H__

