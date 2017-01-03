using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Reflection;

namespace SGScript
{
	[AttributeUsage(AttributeTargets.Method)]
	public class HideMethod : System.Attribute {}

	[AttributeUsage(AttributeTargets.Property | AttributeTargets.Field)]
	public class HideProperty : System.Attribute
	{
		public bool CanRead = false;
		public bool CanWrite = false;
	}
	
	[AttributeUsage(AttributeTargets.Parameter)]
	public class CallingThread : System.Attribute {}

	public abstract class ISGSBase : IDisposable, IGCMarkable
	{
		public Engine _sgsEngine;
		public WeakReference _sgsWeakRef;
		public enum PartiallyConstructed { Value };

		public ISGSBase( Context c ) : this( c.GetEngine() ){}
		public ISGSBase( Engine e )
		{
			e.ProcessVarRemoveQueue();
			_sgsEngine = e;
			_sgsWeakRef = new WeakReference( this, true );
			e._RegisterObj( this );
		}
		public ISGSBase( PartiallyConstructed pc )
		{
			_sgsWeakRef = new WeakReference( this, true );
		}
		~ISGSBase(){ Dispose(); }
		public abstract void Release();
		public virtual void GCMark(){}
		public void Dispose()
		{
			lock( _sgsEngine._objRefs )
			{
				if( _sgsWeakRef != null )
				{
					_sgsEngine._UnregisterObj( this );
					Release();
					_sgsWeakRef = null;
				}
			}
		}
	}
	
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
		// delegate references
		public NI.OC_Self d_destruct;
		public NI.OC_Self d_gcmark;
		public NI.OC_Self d_getindex;
		public NI.OC_Self d_setindex;
		public NI.OC_SlPr d_convert;
		public NI.OC_Self d_serialize;
		public NI.OC_SlPr d_dump;
		public NI.OC_Self d_call;
	}

	// SGScript object implementation core
	public abstract class IObjectBase : ISGSBase
	{
		public static IntPtr _sgsNullObjectInterface = AllocInterface( new NI.ObjInterface(), "<nullObject>" );


		public static IntPtr AllocInterface( NI.ObjInterface iftemplate, string name )
		{
			byte[] nameBytes = System.Text.Encoding.UTF8.GetBytes( name );

			IntPtr iface = MDL.Alloc( NI.ObjInterfaceSize + nameBytes.Length + 1 );

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
#if FINDPARSER_V1
			if( typeType != null && methodName != null )
				throw new SGSException( RC.ENOTFND, string.Format( "Could not find a Context.ParseVar method for '{2}' {1} type={0}", type, typeType, methodName ) );
			return null;
#else
			return typeof(Context).GetMethod( "_ParseVar_Generic" );
#endif
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
			if( ctx.GetEngine()._sgsClassInfo.TryGetValue( type, out cinfo ) )
				return cinfo;

			NI.OC_Self d_destruct, d_gcmark, d_getindex, d_setindex, d_serialize, d_call, d_expr;
			NI.OC_SlPr d_convert, d_dump, d_getnext;
			NI.ObjInterface oi = new NI.ObjInterface()
			{
				destruct = d_destruct = new NI.OC_Self( _sgsDestruct ),
				gcmark = d_gcmark = new NI.OC_Self( _sgsGCMark ),

				getindex = d_getindex = new NI.OC_Self( _sgsGetIndex ),
				setindex = d_setindex = new NI.OC_Self( _sgsSetIndex ),

				convert = d_convert = new NI.OC_SlPr( _sgsConvert ),
				serialize = d_serialize = new NI.OC_Self( _sgsSerialize ),
				dump = d_dump = new NI.OC_SlPr( _sgsDump ),
				getnext = d_getnext = new NI.OC_SlPr( _sgsGetNext ),

				call = d_call = new NI.OC_Self( _sgsCall ),
				expr = d_expr = new NI.OC_Self( _sgsExpr ),
			};

			cinfo = new SGSClassInfo()
			{
				iface = AllocInterface( oi, type.Name ),
				props = _ReadClassProps( ctx, type, false ),
				d_destruct = d_destruct,
				d_gcmark = d_gcmark,
				d_getindex = d_getindex,
				d_setindex = d_setindex,
				d_convert = d_convert,
				d_serialize = d_serialize,
				d_dump = d_dump,
				d_call = d_call,
			};
			ctx.GetEngine()._sgsClassInfo.Add( type, cinfo );
			return cinfo;
		}

		public static SGSClassInfo GetStaticClassInfo( Context ctx, Type type )
		{
			SGSClassInfo cinfo;
			if( ctx.GetEngine()._sgsStaticClassInfo.TryGetValue( type, out cinfo ) )
				return cinfo;
			
			NI.OC_Self d_destruct, d_getindex, d_setindex;
			NI.ObjInterface oi = new NI.ObjInterface()
            {
                destruct = d_destruct = new NI.OC_Self( _sgsDestruct ),
                getindex = d_getindex = new NI.OC_Self( _sgsGetIndex ),
				setindex = d_setindex = new NI.OC_Self( _sgsSetIndex ),
			};

			cinfo = new SGSClassInfo()
			{
				iface = AllocInterface( oi, type.Name + "[static]" ),
				props = _ReadClassProps( ctx, type, true ),
                d_destruct = d_destruct,
				d_getindex = d_getindex,
				d_setindex = d_setindex,
			};
			ctx.GetEngine()._sgsStaticClassInfo.Add( type, cinfo );
			return cinfo;
		}
		
		public static string[] _ovrEasyTypes = new string[]
		{
			"System.String",
		};
		public static string[] _ovrBasicTypes = new string[]
		{
			"System.SByte",
			"System.Int16",
			"System.Int32",
			"System.Int64",
			"System.Byte",
			"System.UInt16",
			"System.UInt32",
			"System.UInt64",
			"System.Float",
			"System.Double",
			"System.Char",
		};
		public static int _ovrEasyPrio = 1;
		public static int _ovrBasicPrio = 10;
		public static int _ovrOtherPrio = 100;
		public static int _ovrEmptyPrio = 100000;
		
		// smaller is better
		public static int CalcOverloadPriority( MethodInfo mi )
		{
			// prefer functions with at least one parameter to parameterless ones
			int prio = mi.GetParameters().Length == 0 ? _ovrEmptyPrio : 1;

			foreach( ParameterInfo pi in mi.GetParameters() )
			{
				// easy types - they accept almost anything and almost always make sense to use
				if( Array.IndexOf( _ovrEasyTypes, pi.ParameterType.FullName ) >= 0 )
					prio += _ovrEasyPrio;
				// basic types - easily convertible but not as useful
				else if( Array.IndexOf( _ovrBasicTypes, pi.ParameterType.FullName ) >= 0 )
					prio += _ovrBasicPrio;
				// other types - need hard-to-obtain values to function properly
				else
					prio += _ovrOtherPrio;
			}
			return prio;
		}
		
		public static string[] _monBasicNamespaces = new string[]
		{
			"System",
		};
		public static string GetMethodOverloadName( MethodInfo mi )
		{
			System.Text.StringBuilder sb = new System.Text.StringBuilder( mi.Name, 256 );
			sb.Append( "(" );
			foreach( ParameterInfo pi in mi.GetParameters() )
			{
				if( pi != mi.GetParameters()[0] )
					sb.Append( "," );
				if( Array.IndexOf( _monBasicNamespaces, pi.ParameterType.Namespace ) >= 0 )
				{
					sb.Append( pi.ParameterType.Name );
				}
				else
				{
					sb.Append( pi.ParameterType.FullName );
				}
			}
			sb.Append( ")" );
			return sb.ToString();
		}
		struct CSD_Overload
		{
			public DNMethod method;
			public int priority;
		};

		public static Variable CreateStaticDict( Context ctx, Type type )
		{
			int items = 0;

			MethodInfo[] methods = type.GetMethods( BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly );
			Dictionary<string, CSD_Overload> overloads = new Dictionary<string, CSD_Overload>();

			// map every function to its decorated name
			foreach( MethodInfo mi in methods )
			{
				if( mi.GetCustomAttributes( typeof(HideMethod), true ).Length != 0 )
					continue;

				DNMethod dnm = new DNMethod( ctx, mi );

				ctx.Push( GetMethodOverloadName( mi ) );
				ctx.Push( dnm );
				items += 2;

				int priority = CalcOverloadPriority( mi );
				CSD_Overload curovr;
				if( overloads.TryGetValue( mi.Name, out curovr ) == false || curovr.priority > priority )
					overloads[ mi.Name ] = new CSD_Overload(){ method = dnm, priority = priority };
			}

			// map best overloads to plain name
			foreach( KeyValuePair<string, CSD_Overload> kvp in overloads )
			{
				ctx.Push( kvp.Key );
				ctx.Push( kvp.Value.method );
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
		public override void GCMark()
		{
			if( _sgsObject != IntPtr.Zero )
				NI.ObjGCMark( _sgsEngine.ctx, _sgsObject );
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
			return (IObjectBase) HDL.GetObj( objdata );
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
				throw new SGSException( RC.EINPROC, "AllocClassObject - object is already allocated" );
			IntPtr iface = GetClassInterface();
			NI.Variable var;
			NI.CreateObject( _sgsEngine.ctx, HDL.Alloc( this ), iface, out var );
			_sgsObject = var.data.O;

			_InitMetaObject();
		}
		public void FreeClassObject()
		{
		//	Console.WriteLine("[freed "+ToString()+"]");
			if( _sgsObject == IntPtr.Zero )
				throw new SGSException( RC.EINPROC, "FreeClassObject - object is not allocated" );
			NI.ObjRelease( _sgsEngine.ctx, _sgsObject );
			_sgsObject = IntPtr.Zero;
		}
		public void DisownClassObject()
		{
			if( _sgsObject == IntPtr.Zero )
				throw new SGSException( RC.EINPROC, "FreeClassObject - object is not allocated" );
			Marshal.WriteIntPtr( _sgsObject, NI.VarObj.offsetOfIface, _sgsNullObjectInterface );
            HDL.Free( Marshal.ReadIntPtr( _sgsObject, NI.VarObj.offsetOfData ) );
            Marshal.WriteIntPtr( _sgsObject, NI.VarObj.offsetOfData, IntPtr.Zero );
			FreeClassObject();
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
				throw new SGSException( RC.ENOTFND, string.Format(
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
			IObjectBase obj = (IObjectBase) HDL.GetObj( handleP );
			if( freehandle )
				HDL.Free( handleP );
			return obj;
		}
		public static int _sgsDestruct( IntPtr ctx, IntPtr varobj ){ IObjectBase obj = _IP2Obj( varobj, true ); if( obj != null ) return obj._intOnDestroy(); else return RC.SUCCESS; }
		public static int _sgsGCMark( IntPtr ctx, IntPtr varobj ){ IObjectBase obj = _IP2Obj( varobj ); return obj._intOnGCMark(); }
		public static int _sgsGetIndex( IntPtr ctx, IntPtr varobj ){ IObjectBase obj = _IP2Obj( varobj ); return obj._intOnGetIndex( Engine.GetCtx( ctx ), NI.ObjectArg( ctx ) != 0 ); }
		public static int _sgsSetIndex( IntPtr ctx, IntPtr varobj ){ IObjectBase obj = _IP2Obj( varobj ); return obj._intOnSetIndex( Engine.GetCtx( ctx ), NI.ObjectArg( ctx ) != 0 ); }
		public static int _sgsConvert( IntPtr ctx, IntPtr varobj, int type ){ IObjectBase obj = _IP2Obj( varobj ); return obj._intOnConvert( Engine.GetCtx( ctx ), (ConvOp) type ); }
		public static int _sgsSerialize( IntPtr ctx, IntPtr varobj ){ IObjectBase obj = _IP2Obj( varobj ); return obj._intOnSerialize( Engine.GetCtx( ctx ) ); }
		public static int _sgsDump( IntPtr ctx, IntPtr varobj, int maxdepth ){ IObjectBase obj = _IP2Obj( varobj ); return obj._intOnDump( Engine.GetCtx( ctx ), maxdepth ); }
		public static int _sgsGetNext( IntPtr ctx, IntPtr varobj, int type ){ IObjectBase obj = _IP2Obj( varobj ); return obj._intGetNext( Engine.GetCtx( ctx ), type ); }
		public static int _sgsCall( IntPtr ctx, IntPtr varobj ){ IObjectBase obj = _IP2Obj( varobj ); return obj.OnCall( Engine.GetCtx( ctx ) ); }
		public static int _sgsExpr( IntPtr ctx, IntPtr varobj ){ IObjectBase obj = _IP2Obj( varobj ); return obj._intOnExpr( Engine.GetCtx( ctx ), (ExprOp) NI.ObjectArg( ctx ) ); }

		// core feature layer (don't override these unless there is some overhead to cut)
		public virtual int _intOnDestroy()
		{
			OnDestroy();
			return RC.SUCCESS;
		}
		public virtual int _intOnGCMark()
		{
			OnGCMark();
			return RC.SUCCESS;
		}
		public virtual int _intOnGetIndex( Context ctx, bool isprop )
		{
			Variable v = OnGetIndex( ctx, ctx.StackItem( 0 ), isprop );
			if( v != null )
			{
				ctx.Push( v );
				return RC.SUCCESS;
			}
			return RC.ENOTFND;
		}
		public virtual int _intOnSetIndex( Context ctx, bool isprop )
		{
			return OnSetIndex( ctx, ctx.StackItem( 0 ), ctx.StackItem( 1 ), isprop ) ? RC.SUCCESS : RC.ENOTFND;
		}
		public virtual int _intOnConvert( Context ctx, ConvOp type )
		{
			switch( type )
			{
				case ConvOp.ToBool: ctx.Push( ConvertToBool() ); return RC.SUCCESS;
				case ConvOp.ToString: ctx.Push( ConvertToString() ); return RC.SUCCESS;
				case ConvOp.Clone: Variable clone = OnClone( ctx ); if( clone != null ) ctx.Push( clone ); return clone != null ? RC.SUCCESS : RC.ENOTSUP;
				case ConvOp.ToIter: Variable iter = OnGetIterator( ctx ); if( iter != null ) ctx.Push( iter ); return iter != null ? RC.SUCCESS : RC.ENOTSUP;
			}
			return RC.ENOTSUP;
		}
		public virtual int _intOnSerialize( Context ctx )
		{
			return OnSerialize( ctx ) ? RC.SUCCESS : RC.ENOTSUP;
		}
		public virtual int _intOnDump( Context ctx, int maxdepth )
		{
			string dump = OnDump( ctx, maxdepth );
			if( dump != null )
			{
				ctx.Push( dump );
				return RC.SUCCESS;
			}
			return RC.ENOTSUP;
		}
		public virtual int _intGetNext( Context ctx, int type )
		{
			if( type == GetNextType.Advance )
				return OnIterAdvance( ctx ) ? RC.SUCCESS : RC.EINPROC;
			else
				return OnIterGetValues( ctx, type ) ? RC.SUCCESS : RC.EINPROC;
		}
		public virtual int _intOnExpr( Context ctx, ExprOp op )
		{
			Variable rv = null;
			switch( op )
			{
				case ExprOp.Add: rv = OnAdd( ctx.StackItem( 0 ), ctx.StackItem( 1 ) ); break;
				case ExprOp.Sub: rv = OnSub( ctx.StackItem( 0 ), ctx.StackItem( 1 ) ); break;
				case ExprOp.Mul: rv = OnMul( ctx.StackItem( 0 ), ctx.StackItem( 1 ) ); break;
				case ExprOp.Div: rv = OnDiv( ctx.StackItem( 0 ), ctx.StackItem( 1 ) ); break;
				case ExprOp.Mod: rv = OnMod( ctx.StackItem( 0 ), ctx.StackItem( 1 ) ); break;
				case ExprOp.Compare: rv = OnCompare( ctx.StackItem( 0 ), ctx.StackItem( 1 ) ); break;
				case ExprOp.Negate: rv = OnNegate(); break;
			}
			if( rv != null )
			{
				ctx.Push( rv );
				return RC.SUCCESS;
			}
			return RC.ENOTSUP;
		}

		// user override callbacks
		[HideMethod] public virtual void OnDestroy(){}
		[HideMethod] public virtual void OnGCMark(){} // only needed for unmanaged IntPtr-type object handles
		[HideMethod] public virtual Variable OnGetIndex( Context ctx, Variable key, bool isprop ){ return sgsGetPropertyByName( key, isprop ); }
		[HideMethod] public virtual bool OnSetIndex( Context ctx, Variable key, Variable val, bool isprop ){ return sgsSetPropertyByName( key, ctx, isprop, 1 ); }
		[HideMethod] public virtual bool ConvertToBool(){ return true; }
		[HideMethod] public virtual string ConvertToString(){ return ToString(); }
		[HideMethod] public virtual Variable OnClone( Context ctx ){ return null; }
		[HideMethod] public virtual Variable OnGetIterator( Context ctx ){ return null; }
		[HideMethod] public virtual bool OnSerialize( Context ctx ){ return false; }
		[HideMethod] public virtual string OnDump( Context ctx, int maxdepth ){ return null; }
		[HideMethod] public virtual bool OnIterAdvance( Context ctx ){ return false; }
		[HideMethod] public virtual bool OnIterGetValues( Context ctx, int type ){ return false; }
		[HideMethod] public virtual int OnCall( Context ctx ){ return RC.ENOTSUP; }
		[HideMethod] public virtual Variable OnAdd( Variable a, Variable b ){ return null; }
		[HideMethod] public virtual Variable OnSub( Variable a, Variable b ){ return null; }
		[HideMethod] public virtual Variable OnMul( Variable a, Variable b ){ return null; }
		[HideMethod] public virtual Variable OnDiv( Variable a, Variable b ){ return null; }
		[HideMethod] public virtual Variable OnMod( Variable a, Variable b ){ return null; }
		[HideMethod] public virtual Variable OnCompare( Variable a, Variable b ){ return null; }
		[HideMethod] public virtual Variable OnNegate(){ return null; }
	}

	// Base class for custom SGScript objects
	public abstract class IObject : IObjectBase
	{
		public IObject( Context c, bool skipInit = false ) : base( c, skipInit ){}
	}
	
	// .NET object usage marker object for SGScript GC (for internal use only)
	public class _DNGarbageCollector : IObjectBase
	{
		public _DNGarbageCollector( Context c ) : base( c ){}
		public override void OnGCMark()
		{
			_sgsEngine._MarkAllObjects();
		}
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

		public override string ToString(){ return "SGScript.DNMetaObject(" + targetType.Name + ")"; }
	}

	// .NET Method wrapper
	public class DNMethod : IObjectBase
	{
		public MethodInfo thisMethodInfo;

		public string name;
		public MethodInfo parseVarThis = null;
		public MethodInfo[] parseVarArgs;
		object[] callArgs;
		
		static object[] parseVarParams = new object[2];

		public DNMethod( Context c, MethodInfo mi ) : base( c )
		{
			thisMethodInfo = mi;
			name = thisMethodInfo.DeclaringType.FullName + "." + thisMethodInfo.Name;

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
				bool isCallingThread = false;
				foreach( object attr in param.GetCustomAttributes( true ) )
				{
					if( attr is CallingThread )
					{
						isCallingThread = true;
						break;
					}
				}
				if( isCallingThread )
				{
					parseVarArgs[ i ] = typeof(Context).GetMethod( "_ParseVar_CallingThread" );
				}
				else
				{
					parseVarArgs[ i ] = FindParserForType( param.ParameterType, "argument " + i, thisMethodInfo.ToString() );
				}
			}
		}
		
		public override void _InitMetaObject(){} // no meta object for functions

		public override int OnCall( Context ctx )
		{
			NI.FuncName( ctx.ctx, name );
			bool gotthis = ctx.Method();
			if( !gotthis && !thisMethodInfo.IsStatic )
			{
				// if 'this' was not passed but the method is not static, error
				ctx.Msg( MsgLevel.ERROR, "Expected 'this' for non-static method" );
				return RC.EINVAL;
			}

			object thisvar = null;
			if( gotthis && thisMethodInfo.IsStatic == false )
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
				if( pva.Name == "_ParseVar_CallingThread" )
				{
					callArgs[ outArg ] = ctx;
				}
				else
				{
					parseVarParams[ 0 ] = null;
					parseVarParams[ 1 ] = ctx.StackItem( inArg++ );
					pva.Invoke( ctx, parseVarParams );
					callArgs[ outArg ] = parseVarParams[ 0 ];
				}
				outArg++;
			}

			ctx.PushObj( thisMethodInfo.Invoke( thisvar, callArgs ) );

			// pooled resource cleanup
			parseVarParams[0] = null;
			parseVarParams[1] = null;
			for( int i = 0; i < parseVarArgs.Length; ++i )
				callArgs[ i ] = null;

			return 1;
		}

		public override string ToString(){ return "SGScript.DNMethod(" + name + ")"; }
	}

	// .NET Generic object handle
	public class DNHandle : IObjectBase
	{
		public object objectRef;
		public DNHandle( Context c, object o ) : base( c, true )
		{
			objectRef = o;
			AllocClassObject();
		}
		public override void _InitMetaObject()
		{
			DNMetaObject dnmo = _sgsEngine._GetMetaObject( objectRef.GetType() );
			NI.ObjSetMetaObj( _sgsEngine.ctx, _sgsObject, dnmo._sgsObject );
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
		public Variable( Context c, NI.Variable v, bool acquire = true ) : base( c )
		{
			var = v;
			if( acquire )
				Acquire();
		}

		public void Acquire(){ NI.Acquire( ctx.ctx, ref var ); }
		public override void Release()
		{
			_sgsEngine._ReleaseVar( ref var );
		}
		public override void GCMark()
		{
			NI.GCMark( _sgsEngine.ctx, ref var );
		}

		public VarType type { get { return var.type; } }
		public bool isNull { get { return var.type == VarType.Null; } }
		public bool notNull { get { return var.type != VarType.Null; } }
		public bool GetBool(){ return NI.GetBoolP( ctx.ctx, ref var ) != 0; }
		public Int64 GetInt(){ return NI.GetIntP( ctx.ctx, ref var ); }
		public double GetReal(){ return NI.GetRealP( ctx.ctx, ref var ); }
		public string ConvertToString()
		{
			Variable v2 = new Variable( ctx, var );
			NI.ToStringBufP( ctx.ctx, ref v2.var );
			string s = v2.GetString();
			v2.Release();
			return s;
		}
		public string GetString(){ return NI.GetString( var ); }
		public byte[] GetByteArray(){ return NI.GetByteArray( var ); }
		public Context GetThread(){ if( var.type != VarType.Thread ) throw new SGSException( RC.EINVAL, "Variable is not a thread" ); return Engine.GetCtx( var.data.T ); }
		public string str { get { return GetString(); } }
		public string typename { get { return _sgsEngine.TypeOf( this ); } }
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

		public Variable GetIterator()
		{
			NI.Variable iter;
			if( NI.CreateIterator( _sgsEngine.ctx, out iter, var ) == 0 )
				throw new SGSException( RC.ENOTSUP, "Object does not support iterators" );
			return _sgsEngine.Var( iter, false );
		}
		public bool IterAdvance()
		{
			return NI.IterAdvance( _sgsEngine.ctx, var ) != 0;
		}
		public Variable IterGetKey()
		{
			NI.Variable outvar;
			NI.IterGetKey( _sgsEngine.ctx, var, out outvar );
			return _sgsEngine.Var( outvar, false );
		}
		public Variable IterGetValue()
		{
			NI.Variable outvar;
			NI.IterGetValue( _sgsEngine.ctx, var, out outvar );
			return _sgsEngine.Var( outvar, false );
		}
		public void IterGetKeyValue( out Variable key, out Variable val )
		{
			NI.Variable outkey, outval;
			NI.IterGetData( _sgsEngine.ctx, var, out outkey, out outval );
			key = _sgsEngine.Var( outkey, false );
			val = _sgsEngine.Var( outval, false );
		}

		public string TypeOf(){ return _sgsEngine.TypeOf( this ); }
		public Variable Clone(){ return _sgsEngine.CloneItem( this ); }
		public string Dump( int maxdepth = 5 ){ return _sgsEngine.DumpVar( this ); }

		public bool IsArray(){ return NI.IsArray( var ) != 0; }
		public bool IsDict(){ return NI.IsDict( var ) != 0; }
		public bool IsMap(){ return NI.IsMap( var ) != 0; }
		public Int32 ArraySize(){ return NI.ArraySize( var ); }
		public void ArrayPushFromStack( Context ctx, Int32 count ){ NI.ArrayPush( ctx.ctx, var, count ); }
		public void ArrayPush( params object[] args ){ foreach( object arg in args ) _sgsEngine.PushObj( arg ); ArrayPushFromStack( _sgsEngine, args.Length ); }
		public void ArrayPop( Int32 count ){ NI.ArrayPop( _sgsEngine.ctx, var, count, 0 ); }
		public void ArrayPopToStack( Context ctx, Int32 count ){ NI.ArrayPop( ctx.ctx, var, count, 1 ); }
		public Variable[] ArrayPopRetrieve( Int32 count )
		{
			Int32 arrsize = ArraySize();
			if( arrsize < count )
				throw new SGSException( RC.EINPROC, string.Format( "Not enough items in array {0}, expected at least {1}", arrsize, count ) );
			ArrayPopToStack( _sgsEngine, count );
			return _sgsEngine.TakeTopmostVars( count );
		}
		public Int32 ArrayFind( Variable val ){ return NI.ArrayFind( _sgsEngine.ctx, var, val.var ); }
		public Int32 ArrayRemove( Variable val, bool all ){ return NI.ArrayRemove( _sgsEngine.ctx, var, val.var, all ? 1 : 0 ); }
		public bool Unset( Variable key ){ return NI.Unset( _sgsEngine.ctx, var, key.var ) != 0; }
		public bool EventState( EventStateType est ){ return NI.EventState( _sgsEngine.ctx, var, est ) != 0; }

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
				default: throw new SGSException( RC.EINVAL, string.Format( "invalid variable type ({0})", type ) );
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
