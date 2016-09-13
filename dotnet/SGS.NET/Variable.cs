using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Reflection;

namespace SGScript
{
	[AttributeUsage(AttributeTargets.Method)]
	public class HideMethod : System.Attribute
	{
	}

	[AttributeUsage(AttributeTargets.Property | AttributeTargets.Field)]
	public class HideProperty : System.Attribute
	{
		public bool CanRead = false;
		public bool CanWrite = false;
	}

	public abstract class ISGSBase : IDisposable
	{
		public Engine _sgsEngine;
		public enum PartiallyConstructed { Value };

		public ISGSBase( Context c ) : this( c.GetEngine() ){}
		public ISGSBase( Engine e )
		{
			_sgsEngine = e;
			e._RegisterObj( this );
		}
		public ISGSBase( PartiallyConstructed pc ){}
		~ISGSBase(){ Dispose(); }
		public abstract void Release();
		public void Dispose()
		{
			_sgsEngine._UnregisterObj( this );
			Release();
		}
	}

	// SGScript object implementation core
	public abstract class IObjectBase : ISGSBase
	{
		public class SGSPropInfo
		{
			public bool canRead = true;
			public bool canWrite = true;
			public Type propType;
			public MemberInfo info; // PropertyInfo or FieldInfo
			public MethodInfo parseVarMethod;
		}
		public class SGSClassInfo
		{
			public IntPtr iface;
			public Dictionary<Variable, SGSPropInfo> props;
		}

		public static IntPtr _sgsNullObjectInterface = AllocInterface( new NI.ObjInterface(), "<nullObject>" );
		public static Dictionary<IObjectBase, bool> _sgsOwnedObjects = new Dictionary<IObjectBase,bool>();
		public static Dictionary<Type, SGSClassInfo> _sgsClassInfo = new Dictionary<Type,SGSClassInfo>();
		public static Dictionary<Type, SGSClassInfo> _sgsStaticClassInfo = new Dictionary<Type,SGSClassInfo>();


		public static IntPtr AllocInterface( NI.ObjInterface iftemplate, string name )
		{
			byte[] nameBytes = System.Text.Encoding.UTF8.GetBytes( name );

			IntPtr iface = Marshal.AllocHGlobal( NI.ObjInterfaceSize + nameBytes.Length + 1 );

			IntPtr nameOffset = (IntPtr) ( iface.ToInt64() + NI.ObjInterfaceSize );
			iftemplate.name = nameOffset;

			Marshal.StructureToPtr( iftemplate, iface, false );
			Marshal.Copy( nameBytes, 0, nameOffset, nameBytes.Length );
			Marshal.WriteByte( nameOffset, nameBytes.Length, 0 );

			return iface;
		}

		public static MethodInfo FindParserForType( Type type, string typeType = null, string methodName = null )
		{
			foreach( MethodInfo method in typeof(Context).GetMethods() )
			{
				if( method.Name != "ParseVar" )
					continue;
				ParameterInfo[] prms = method.GetParameters();

				// expected signature: public void ParseVar( out T value, Variable var );
				if( prms.Length != 2 || prms[0].IsOut == false || prms[0].ParameterType.IsByRef == false || prms[1].ParameterType != typeof(Variable) )
					continue; // unrecognized signature

				// ParameterType is T&
				Type paramType = prms[0].ParameterType.GetElementType();
				if( paramType != type && !type.IsSubclassOf( paramType ) )
					continue; // type does not match
			//	Console.WriteLine(type +" > " + paramType);

				// a good conversion method has been found
				return method;
			}
			if( typeType != null && methodName != null )
				throw new SGSException( NI.ENOTFND, string.Format( "Could not find a Context.ParseVar method for '{2}' {1} type={0}", type, typeType, methodName ) );
			return null;
		}
		static SGSPropInfo _GetPropFieldInfo( MemberInfo minfo, Type type )
		{
			SGSPropInfo info = new SGSPropInfo(){ info = minfo, propType = type };
			foreach( object attr in minfo.GetCustomAttributes( false ) )
			{
				if( attr is HideProperty )
				{
					info.canRead = (attr as HideProperty).CanRead;
					info.canWrite = (attr as HideProperty).CanWrite;
					if( info.canRead == false && info.canWrite == false )
						return null; // no need to register the property, it is marked as inaccessible
				}
			}

			info.parseVarMethod = FindParserForType( type );

			return info;
		}
		static Dictionary<Variable, SGSPropInfo> _ReadClassProps( Context ctx, Type type, bool needStatic )
		{
			Dictionary<Variable, SGSPropInfo> outprops = new Dictionary<Variable, SGSPropInfo>();

			BindingFlags placementFlag = needStatic ? BindingFlags.Static : BindingFlags.Instance;
			FieldInfo[] fields = type.GetFields( BindingFlags.Public | BindingFlags.NonPublic | placementFlag );
			foreach( FieldInfo field in fields )
			{
				SGSPropInfo info = _GetPropFieldInfo( field, field.FieldType );
				if( info != null )
					outprops.Add( ctx.Var( field.Name ), info );
			}

			PropertyInfo[] properties = type.GetProperties( BindingFlags.Public | BindingFlags.NonPublic | placementFlag );
			foreach( PropertyInfo prop in properties )
			{
				SGSPropInfo info = _GetPropFieldInfo( prop, prop.PropertyType );
				if( info != null )
					outprops.Add( ctx.Var( prop.Name ), info );
			}

			return outprops;
		}
		
		// returns the cached interface for any supporting (IObject-based) class
		public static SGSClassInfo GetClassInfo( Context ctx, Type type )
		{
			SGSClassInfo cinfo;
			if( _sgsClassInfo.TryGetValue( type, out cinfo ) )
				return cinfo;

			NI.ObjInterface oi = new NI.ObjInterface()
			{
				destruct = new NI.OC_Self( _sgsDestruct ),
				gcmark = new NI.OC_Self( _sgsGCMark ),

				getindex = new NI.OC_Self( _sgsGetIndex ),
				setindex = new NI.OC_Self( _sgsSetIndex ),

				convert = new NI.OC_SlPr( _sgsConvert ),
				serialize = new NI.OC_Self( _sgsSerialize ),
				dump = new NI.OC_SlPr( _sgsDump ),
				// TODO getnext

				call = new NI.OC_Self( _sgsCall ),
				// TODO expr
			};

			cinfo = new SGSClassInfo()
			{
				iface = AllocInterface( oi, type.Name ),
				props = _ReadClassProps( ctx, type, false ),
			};
			_sgsClassInfo.Add( type, cinfo );
			return cinfo;
		}

		public static SGSClassInfo GetStaticClassInfo( Context ctx, Type type )
		{
			SGSClassInfo cinfo;
			if( _sgsStaticClassInfo.TryGetValue( type, out cinfo ) )
				return cinfo;

			NI.ObjInterface oi = new NI.ObjInterface()
			{
				getindex = new NI.OC_Self( _sgsGetIndex ),
				setindex = new NI.OC_Self( _sgsSetIndex ),
			};

			cinfo = new SGSClassInfo()
			{
				iface = AllocInterface( oi, type.Name + "[static]" ),
				props = _ReadClassProps( ctx, type, true ),
			};
			_sgsStaticClassInfo.Add( type, cinfo );
			return cinfo;
		}
		
		public static Variable CreateStaticDict( Context ctx, Type type )
		{
			int items = 0;

			MethodInfo[] methods = type.GetMethods( BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly );
			foreach( MethodInfo mi in methods )
			{
				if( mi.GetCustomAttributes( typeof(HideMethod), true ).Length != 0 )
					continue;

				DNMethod dnm = new DNMethod( ctx, mi );

				ctx.Push( mi.Name );
				ctx.Push( dnm );
				items += 2;
			}

			return ctx.DictVar( items );
		}


		public IntPtr _sgsObject = IntPtr.Zero;
		public Variable backingStore = null;

		public IObjectBase( Context c, bool skipInit = false ) : base( c )
		{
			if( !skipInit )
				AllocClassObject();
		}
		public override void Release()
		{
			if( _sgsObject != IntPtr.Zero )
			{
				FreeClassObject();
				_sgsObject = IntPtr.Zero;
			}
		}
		public Variable GetVariable()
		{
			return _sgsEngine.Var( this );
		}

		public static IObjectBase GetFromVarObj( IntPtr varobj )
		{
			return GetFromObjData( Marshal.ReadIntPtr( varobj, NI.VarObj.offsetOfData ) );
		}
		public static IObjectBase GetFromObjData( IntPtr objdata )
		{
			GCHandle h = GCHandle.FromIntPtr( objdata );
			return (IObjectBase) h.Target;
		}

		// returns the cached interface for the current class
		public virtual SGSClassInfo GetClassInfo()
		{
			return GetClassInfo( _sgsEngine, GetType() );
		}
		public IntPtr GetClassInterface()
		{
			return GetClassInfo().iface;
		}
		public virtual void _InitMetaObject()
		{
			DNMetaObject dnmo = _sgsEngine._GetMetaObject( GetType() );
			NI.ObjSetMetaObj( _sgsEngine.ctx, _sgsObject, dnmo._sgsObject );
		}
		public void AllocClassObject()
		{
			if( _sgsObject != IntPtr.Zero )
				throw new SGSException( NI.EINPROC, "AllocClassObject - object is already allocated" );
			IntPtr iface = GetClassInterface();
			GCHandle h = GCHandle.Alloc( this );
			NI.Variable var;
			NI.CreateObject( _sgsEngine.ctx, GCHandle.ToIntPtr( h ), iface, out var );
			_sgsObject = var.data.O;

			_InitMetaObject();
		}
		public void FreeClassObject()
		{
			if( _sgsObject == IntPtr.Zero )
				throw new SGSException( NI.EINPROC, "FreeClassObject - object is not allocated" );
			IntPtr handleP = Marshal.ReadIntPtr( _sgsObject, NI.VarObj.offsetOfData );
			GCHandle h = GCHandle.FromIntPtr( handleP );
			NI.ObjRelease( _sgsEngine.ctx, _sgsObject );
			_sgsObject = IntPtr.Zero;
			h.Free();
		}
		public void DisownClassObject()
		{
			if( _sgsObject == IntPtr.Zero )
				throw new SGSException( NI.EINPROC, "FreeClassObject - object is not allocated" );
			Marshal.WriteIntPtr( _sgsObject, Marshal.OffsetOf( typeof(NI.VarObj), "iface" ).ToInt32(), _sgsNullObjectInterface );
			FreeClassObject();
		}
		
		// Let SGScript keep the object even if there are no references to it from C# code
		public void DelegateOwnership()
		{
			_sgsOwnedObjects.Add( this, true );
		}
		// Remove the object
		public void RetakeOwnership()
		{
			_sgsOwnedObjects.Remove( this );
		}

		// callback implementation helpers
		public Variable sgsGetPropertyByName( Variable key, bool isprop )
		{
			SGSClassInfo cinfo = GetClassInfo();
			SGSPropInfo propinfo;
			if( !cinfo.props.TryGetValue( key, out propinfo ) || !propinfo.canRead )
			{
				if( backingStore != null )
					return backingStore.GetSubItem( key, isprop );
				return null;
			}

			object obj;
			if( propinfo.info is FieldInfo )
				obj = (propinfo.info as FieldInfo).GetValue( this );
			else // PropertyInfo
				obj = (propinfo.info as PropertyInfo).GetValue( this, null );
			return _sgsEngine.ObjVar( obj );
		}
		public bool sgsSetPropertyByName( Variable key, Context ctx, bool isprop, int valueOnStack )
		{
			SGSClassInfo cinfo = GetClassInfo();
			SGSPropInfo propinfo;
			if( !cinfo.props.TryGetValue( key, out propinfo ) || !propinfo.canWrite )
			{
				if( backingStore != null )
					return backingStore.SetSubItem( key, ctx.StackItem( valueOnStack ), isprop );
				return false;
			}
			if( propinfo.parseVarMethod == null )
				throw new SGSException( NI.ENOTFND, string.Format(
					"Property cannot be set - no Context.ParseVar method exists that supports this type ({0})", propinfo.propType ) );

			object[] args = new object[]{ null, ctx.StackItem( valueOnStack ) };
			propinfo.parseVarMethod.Invoke( ctx, args );
			if( propinfo.info is FieldInfo )
				(propinfo.info as FieldInfo).SetValue( this, args[0] );
			else // PropertyInfo
				(propinfo.info as PropertyInfo).SetValue( this, args[0], null );
			return true;
		}
		public bool sgsSetPropertyByName( Variable key, Variable val, bool isprop )
		{
			_sgsEngine.Push( val );
			bool ret = sgsSetPropertyByName( key, _sgsEngine, isprop, _sgsEngine.StackSize() - 1 );
			_sgsEngine.Pop( 1 );
			return ret;
		}

		// callback wrappers
		public static IObjectBase _IP2Obj( IntPtr varobj, bool freehandle = false )
		{
			IntPtr handleP = Marshal.ReadIntPtr( varobj, NI.VarObj.offsetOfData );
			GCHandle h = GCHandle.FromIntPtr( handleP );
			IObjectBase obj = (IObjectBase) h.Target;
			if( freehandle )
				h.Free();
			return obj;
		}
		public static int _sgsDestruct( IntPtr ctx, IntPtr varobj ){ IObjectBase obj = _IP2Obj( varobj, true ); if( obj != null ) return obj._intOnDestroy(); else return NI.SUCCESS; }
		public static int _sgsGCMark( IntPtr ctx, IntPtr varobj ){ IObjectBase obj = _IP2Obj( varobj ); return obj._intOnGCMark(); }
		public static int _sgsGetIndex( IntPtr ctx, IntPtr varobj ){ IObjectBase obj = _IP2Obj( varobj ); return obj._intOnGetIndex( new Context( ctx ), NI.ObjectArg( ctx ) != 0 ); }
		public static int _sgsSetIndex( IntPtr ctx, IntPtr varobj ){ IObjectBase obj = _IP2Obj( varobj ); return obj._intOnSetIndex( new Context( ctx ), NI.ObjectArg( ctx ) != 0 ); }
		public static int _sgsConvert( IntPtr ctx, IntPtr varobj, int type ){ IObjectBase obj = _IP2Obj( varobj ); return obj._intOnConvert( new Context( ctx ), type ); }
		public static int _sgsSerialize( IntPtr ctx, IntPtr varobj ){ IObjectBase obj = _IP2Obj( varobj ); return obj._intOnSerialize( new Context( ctx ) ); }
		public static int _sgsDump( IntPtr ctx, IntPtr varobj, int maxdepth ){ IObjectBase obj = _IP2Obj( varobj ); return obj._intOnDump( new Context( ctx ), maxdepth ); }
		public static int _sgsCall( IntPtr ctx, IntPtr varobj ){ IObjectBase obj = _IP2Obj( varobj ); return obj.OnCall( new Context( ctx ) ); }

		// core feature layer (don't override these unless there is some overhead to cut)
		public virtual int _intOnDestroy()
		{
			RetakeOwnership();
			OnDestroy();
			return NI.SUCCESS;
		}
		public virtual int _intOnGCMark()
		{
			OnGCMark();
			return NI.SUCCESS;
		}
		public virtual int _intOnGetIndex( Context ctx, bool isprop )
		{
			Variable v = OnGetIndex( ctx, ctx.StackItem( 0 ), isprop );
			if( v != null )
			{
				ctx.Push( v );
				return NI.SUCCESS;
			}
			return NI.ENOTFND;
		}
		public virtual int _intOnSetIndex( Context ctx, bool isprop )
		{
			return OnSetIndex( ctx, ctx.StackItem( 0 ), ctx.StackItem( 1 ), isprop ) ? NI.SUCCESS : NI.ENOTFND;
		}
		public virtual int _intOnConvert( Context ctx, int type )
		{
			switch( (ConvOp) type )
			{
				case ConvOp.ToBool: ctx.Push( ConvertToBool() ); return NI.SUCCESS;
				case ConvOp.ToString: ctx.Push( ConvertToString() ); return NI.SUCCESS;
				case ConvOp.Clone: Variable clone = OnClone( ctx ); if( clone != null ) ctx.Push( clone ); return clone != null ? NI.SUCCESS : NI.ENOTSUP;
				case ConvOp.ToIter: Variable iter = OnGetIterator( ctx ); if( iter != null ) ctx.Push( iter ); return iter != null ? NI.SUCCESS : NI.ENOTSUP;
			}
			return NI.ENOTSUP;
		}
		public virtual int _intOnSerialize( Context ctx )
		{
			return OnSerialize( ctx ) ? NI.SUCCESS : NI.ENOTSUP;
		}
		public virtual int _intOnDump( Context ctx, int maxdepth )
		{
			string dump = OnDump( ctx, maxdepth );
			if( dump != null )
			{
				ctx.Push( dump );
				return NI.SUCCESS;
			}
			return NI.ENOTSUP;
		}

		// user override callbacks
		[HideMethod]
		public virtual void OnDestroy(){}
		[HideMethod]
		public virtual void OnGCMark(){}
		[HideMethod]
		public virtual Variable OnGetIndex( Context ctx, Variable key, bool isprop ){ return sgsGetPropertyByName( key, isprop ); }
		[HideMethod]
		public virtual bool OnSetIndex( Context ctx, Variable key, Variable val, bool isprop ){ return sgsSetPropertyByName( key, ctx, isprop, 1 ); }
		[HideMethod]
		public virtual bool ConvertToBool(){ return true; }
		[HideMethod]
		public virtual string ConvertToString(){ return ToString(); }
		[HideMethod]
		public virtual Variable OnClone( Context ctx ){ return null; }
		[HideMethod]
		public virtual Variable OnGetIterator( Context ctx ){ return null; }
		[HideMethod]
		public virtual bool OnSerialize( Context ctx ){ return false; }
		[HideMethod]
		public virtual string OnDump( Context ctx, int maxdepth ){ return null; }
		[HideMethod]
		public virtual int OnCall( Context ctx ){ return NI.ENOTSUP; }
	}

	// Base class for custom SGScript objects
	public abstract class IObject : IObjectBase
	{
		public IObject( Context c, bool skipInit = false ) : base( c, skipInit ){}
	}

	// .NET Object interface wrapper
	public class DNMetaObject : IObjectBase
	{
		public SGSClassInfo staticInfo;
		public Type targetType;

		public DNMetaObject( Context c, Type t ) : base( c, true )
		{
			targetType = t;
			staticInfo = GetStaticClassInfo( c, t );
			backingStore = CreateStaticDict( c, t );
			AllocClassObject();
		}
		
		public override SGSClassInfo GetClassInfo()
		{
			return GetStaticClassInfo( _sgsEngine, targetType );
		}
		public override void _InitMetaObject(){} // no meta object for meta objects
	}

	// .NET Method wrapper
	public class DNMethod : IObjectBase
	{
		public MethodInfo thisMethodInfo;

		public MethodInfo parseVarThis = null;
		public MethodInfo[] parseVarArgs;
		object[] callArgs;
		
		static object[] parseVarParams = new object[2];

		public DNMethod( Context c, MethodInfo mi ) : base( c )
		{
			thisMethodInfo = mi;

			if( thisMethodInfo.IsStatic == false )
			{
				parseVarThis = FindParserForType( thisMethodInfo.DeclaringType, "object", thisMethodInfo.ToString() );
			}

			ParameterInfo[] prms = thisMethodInfo.GetParameters();
			callArgs = new object[ prms.Length ];
			parseVarArgs = new MethodInfo[ prms.Length ];

			for( int i = 0; i < prms.Length; ++i )
			{
				ParameterInfo param = prms[ i ];
				parseVarArgs[ i ] = FindParserForType( param.ParameterType, "argument " + i, thisMethodInfo.ToString() );
			}
		}
		
		public override void _InitMetaObject(){} // no meta object for functions

		public override int OnCall( Context ctx )
		{
			bool gotthis = ctx.Method();
			if( !gotthis && !thisMethodInfo.IsStatic )
			{
				// if 'this' was not passed but the method is not static, error
				return NI.EINVAL; // TODO sgs_Msg?
			}

			object thisvar = null;
			if( gotthis )
			{
				parseVarParams[ 0 ] = null;
				parseVarParams[ 1 ] = ctx.StackItem( 0 );
				parseVarThis.Invoke( ctx, parseVarParams );
				thisvar = parseVarParams[ 0 ];
			}
			int outArg = 0;
			int inArg = gotthis ? 1 : 0;
			foreach( MethodInfo pva in parseVarArgs )
			{
				parseVarParams[ 0 ] = null;
				parseVarParams[ 1 ] = ctx.StackItem( inArg );
				pva.Invoke( ctx, parseVarParams );
				callArgs[ outArg ] = parseVarParams[ 0 ];
 
				inArg++;
				outArg++;
			}

			ctx.PushObj( thisMethodInfo.Invoke( thisvar, callArgs ) );

			return 1;
		}
	}


	// SGScript variable handle
	public class Variable : ISGSBase
	{
		public NI.Variable var;
		public Engine ctx { get { return _sgsEngine; } }
		
		public Variable( Context c ) : base( c )
		{
			var = NI.MakeNull();
		}
		public Variable( Context c, NI.Variable v ) : base( c )
		{
			var = v;
			Acquire();
		}

		public void Acquire(){ NI.Acquire( ctx.ctx, var ); }
		public override void Release()
		{
			if( ctx.ctx != IntPtr.Zero && var.type != VarType.Null )
				NI.Release( ctx.ctx, ref var );
		}

		public VarType type { get { return var.type; } }
		public bool isNull { get { return var.type == VarType.Null; } }
		public bool notNull { get { return var.type != VarType.Null; } }
		public bool GetBool(){ return NI.GetBoolP( ctx.ctx, var ); }
		public Int64 GetInt(){ return NI.GetIntP( ctx.ctx, var ); }
		public double GetReal(){ return NI.GetRealP( ctx.ctx, var ); }
		public string ConvertToString()
		{
			Variable v2 = new Variable( ctx, var );
			NI.ToStringBufP( ctx.ctx, ref v2.var );
			string s = v2.GetString();
			v2.Release();
			return s;
		}
		public string GetString(){ return NI.GetString( var ); }
		public string str { get { return GetString(); } }
		public IObjectBase GetIObjectBase()
		{
			if( var.type == VarType.Object )
				return IObjectBase.GetFromVarObj( var.data.O );
			else
				return null;
		}
		public Variable GetMetaObj()
		{
			if( var.type == VarType.Object )
				return _sgsEngine.SGSObjectVar( NI.ObjGetMetaObj( var.data.O ) );
			else
				return _sgsEngine.NullVar();
		}

		public Variable GetSubItem( Variable key, bool isprop ){ return ctx.GetIndex( this, key, isprop ); }
		public Variable GetSubItem( string key, bool isprop ){ return ctx.GetIndex( this, ctx.Var( key ), isprop ); }
		public bool SetSubItem( Variable key, Variable val, bool isprop ){ return ctx.SetIndex( this, key, val, isprop ); }
		public bool SetSubItem( string key, Variable val, bool isprop ){ return ctx.SetIndex( this, ctx.Var( key ), val, isprop ); }
		public Variable GetIndex( Variable key ){ return ctx.GetIndex( this, key, false ); }
		public Variable GetIndex( string key ){ return ctx.GetIndex( this, ctx.Var( key ), false ); }
		public Variable GetIndex( Int64 key ){ return ctx.GetIndex( this, ctx.Var( key ), false ); }
		public bool SetIndex( Variable key, Variable val ){ return ctx.SetIndex( this, key, val, false ); }
		public bool SetIndex( string key, Variable val ){ return ctx.SetIndex( this, ctx.Var( key ), val, false ); }
		public bool SetIndex( Int64 key, Variable val ){ return ctx.SetIndex( this, ctx.Var( key ), val, false ); }
		public Variable GetProp( Variable key ){ return ctx.GetIndex( this, key, true ); }
		public Variable GetProp( string key ){ return ctx.GetIndex( this, ctx.Var( key ), true ); }
		public bool SetProp( Variable key, Variable val ){ return ctx.SetIndex( this, key, val, true ); }
		public bool SetProp( string key, Variable val ){ return ctx.SetIndex( this, ctx.Var( key ), val, true ); }

		public override string ToString()
		{
			switch( type )
			{
				case VarType.Null: return string.Format( "SGScript.Variable(null)", var.type );
				case VarType.Bool: return string.Format( "SGScript.Variable(bool [{0}])", var.data.B != 0 ? true : false );
				case VarType.Int: return string.Format( "SGScript.Variable(int [{0}])", var.data.I );
				case VarType.Real: return string.Format( "SGScript.Variable(real [{0}])", var.data.R );
				case VarType.String: string s = GetString(); return string.Format( "SGScript.Variable(string [{0}] \"{1}\")", s.Length, s );
				default: return string.Format( "SGScript.Variable(typeid={0} ptr={1})", (int) var.type, var.data.P );
			}
		}
		public override bool Equals( object obj )
		{
			if( obj == null )
				return false;
			Variable v = (Variable) obj;
			if( (object) v == null )
				return false;

			if( type != v.type )
				return false;
			switch( type )
			{
				case VarType.Null: return true;
				case VarType.Bool: return var.data.B == v.var.data.B;
				case VarType.Int: return var.data.I == v.var.data.I;
				case VarType.Real: return var.data.R == v.var.data.R;
				case VarType.String: return var.data.S == v.var.data.S; // all strings are interned
				case VarType.Func: // union pointer overlap
				case VarType.CFunc:
				case VarType.Object:
				case VarType.Ptr:
				case VarType.Thread: return var.data.T == v.var.data.T;
				default: return true;
			}
		}
		public override int GetHashCode()
		{
			int code = (int) var.type;
			switch( type )
			{
				case VarType.Null: break;
				case VarType.Bool: code ^= var.data.B << 8; break;
				case VarType.Int: code ^= var.data.I.GetHashCode(); break;
				case VarType.Real: code ^= var.data.R.GetHashCode(); break;
				case VarType.String: // union pointer overlap
				case VarType.Func:
				case VarType.CFunc:
				case VarType.Object:
				case VarType.Ptr:
				case VarType.Thread: code ^= var.data.P.GetHashCode(); break;
				default: throw new SGSException( NI.EINVAL, string.Format( "invalid variable type ({0})", type ) );
			}
			return code;
		}
		public static bool operator == ( Variable a, Variable b )
		{
			if( ReferenceEquals( a, b ) )
				return true;
			if( ( (object) a == null ) || ( (object) b == null ) )
				return false;
			return a.Equals( b );
		}
		public static bool operator != ( Variable a, Variable b ){ return !( a == b ); }
	}
}
