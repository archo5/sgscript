

#pragma once

#include <sgscript.h>


#define SGS_VDECL( var ) extern var
#define SGS_VDEF( var ) var

#define SGS_IFN( name ) sgsiface_##name
#define SGS_METHODNAME( obj, funcname ) SGS_IFN( obj##_##funcname )
#define SGS_GETINDEXNAME( obj ) SGS_IFN(obj##GetIndex)
#define SGS_SETINDEXNAME( obj ) SGS_IFN(obj##SetIndex)
#define SGS_CTORNAME( obj ) SGS_IFN(obj##Create)
#define SGS_DTORNAME( obj ) SGS_IFN(obj##Destroy)

#define SGS_DECLARE_IFACE_O( obj ) SGS_VDECL( void* SGS_IFN(obj)[] );
#define SGS_DEFINE_IFACE_O( obj ) SGS_VDEF( void* SGS_IFN(obj)[] ) = {
#define SGS_DEFINE_IFACE_END SGS_OP_END, };
#define SGS_IFACE_ENTRY( name, func ) SGS_OP_##name, (void*) func
#define SGS_IFACE_GETINDEX_O( obj ) \
	SGS_IFACE_ENTRY(GETINDEX,SGS_GETINDEXNAME(obj))
#define SGS_IFACE_SETINDEX_O( obj ) \
	SGS_IFACE_ENTRY(SETINDEX,SGS_SETINDEXNAME(obj))
#define SGS_IFACE_DESTRUCT_O( obj ) \
	SGS_IFACE_ENTRY(DESTRUCT,SGS_DTORNAME(obj))

#define SGS_IFNHDR( obj, funcname ) \
	sgs_VarObj* data; SGSFN( #funcname ); obj* item; \
	if( !sgs_Method( C ) || !sgs_IsObject( C, 0, SGS_IFN(obj) ) ) \
		return sgs_Printf( C, SGS_WARNING, "method not called on " #obj );\
	data = sgs_GetObjectStruct( C, 0 ); item = (obj*) data->data;
#define SGS_METHOD_WRAPPER_O( obj, funcname ) \
	int SGS_METHODNAME(obj,funcname)(SGS_CTX){SGS_IFNHDR(obj,funcname) \
	return item->funcname(C);}

#define SGS_DEFINE_GETINDEXFUNC_O( obj ) int SGS_GETINDEXNAME(obj) \
	(SGS_CTX,sgs_VarObj*data,int is_property)
#define SGS_BEGIN_GENERIC_GETINDEXFUNC_O( obj ) SGS_DEFINE_GETINDEXFUNC_O(obj){\
	char* property_name;obj*item;if(!sgs_ParseString(C,0,&property_name,NULL))\
	return SGS_EINVAL; item = (obj*) data->data;
#define SGS_END_GENERIC_GETINDEXFUNC return SGS_ENOTFND; }
#define SGS_GIF_CUSTOM( ename, code ) \
	if(strcmp(property_name,#ename)==0){code;return SGS_SUCCESS;}
#define SGS_GIF_METHOD_O( obj, funcname ) SGS_GIF_CUSTOM(funcname, \
	sgs_PushCFunction(C,SGS_METHODNAME(obj,funcname)))
#define SGS_GIF_GETTER( name ) SGS_GIF_CUSTOM(name, \
	return item->getter_##name(C); )

#define SGS_DEFINE_SETINDEXFUNC( obj ) int SGS_SETINDEXNAME(obj) \
	(SGS_CTX,sgs_VarObj*data,int is_property)
#define SGS_BEGIN_GENERIC_SETINDEXFUNC_O( obj ) SGS_DEFINE_SETINDEXFUNC(obj){\
	char* property_name;obj*item;if(!sgs_ParseString(C,0,&property_name,NULL))\
	return SGS_EINVAL; item = (obj*) data->data;
#define SGS_END_GENERIC_SETINDEXFUNC return SGS_ENOTFND; }
#define SGS_GIF_SETTER( name ) SGS_GIF_CUSTOM(name, \
	return item->setter_##name(C); )
	

#define SGS_GENERIC_DESTRUCTOR_O( obj ) \
	int SGS_DTORNAME(obj)(SGS_CTX,sgs_VarObj*data,int dco) \
	{ delete (obj*) data->data; return SGS_SUCCESS; }
#define SGS_GENERIC_IFPUSH( obj ) sgs_PushObject( C, new obj, SGS_IFN(obj) )
#define SGS_DEFINE_CTORFUNC_O( obj ) int SGS_CTORNAME(obj)(SGS_CTX)
#define SGS_DEFINE_EMPTY_CTORFUNC_O( obj ) SGS_DEFINE_CTORFUNC_O(obj) \
	{ SGS_GENERIC_IFPUSH( obj ); return 1; }

#define SGS_REGISTER_C( C, obj ) do{\
	sgs_PushCFunction(C,SGS_CTORNAME(obj));sgs_StoreGlobal(C,#obj);}while(0)
#define SGS_REGISTER( obj ) SGS_REGISTER_C( C, obj )


#define SGS_DECLARE_IFACE SGS_DECLARE_IFACE_O(SGS_CLASS)
#define SGS_DEFINE_IFACE SGS_DEFINE_IFACE_O(SGS_CLASS)
#define SGS_METHOD_WRAPPER( funcname ) \
	SGS_METHOD_WRAPPER_O(SGS_CLASS,funcname)
#define SGS_BEGIN_GENERIC_GETINDEXFUNC \
	SGS_BEGIN_GENERIC_GETINDEXFUNC_O(SGS_CLASS)
#define SGS_GIF_METHOD( funcname ) SGS_GIF_METHOD_O(SGS_CLASS,funcname)
#define SGS_BEGIN_GENERIC_SETINDEXFUNC \
	SGS_BEGIN_GENERIC_SETINDEXFUNC_O(SGS_CLASS)
#define SGS_GENERIC_DESTRUCTOR SGS_GENERIC_DESTRUCTOR_O(SGS_CLASS)
#define SGS_DEFINE_CTORFUNC SGS_DEFINE_CTORFUNC_O(SGS_CLASS)
#define SGS_DEFINE_EMPTY_CTORFUNC SGS_DEFINE_EMPTY_CTORFUNC_O(SGS_CLASS)

#define SGS_IFACE_GETINDEX SGS_IFACE_GETINDEX_O(SGS_CLASS)
#define SGS_IFACE_SETINDEX SGS_IFACE_SETINDEX_O(SGS_CLASS)
#define SGS_IFACE_DESTRUCT SGS_IFACE_DESTRUCT_O(SGS_CLASS)

