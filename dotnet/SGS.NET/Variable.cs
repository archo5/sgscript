using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace SGScript
{
	public abstract class IObject : NI.IUserData
	{
		public static IntPtr _sgsNullObjectInterface;
		public static Dictionary<IObject, bool> _sgsOwnedObjects = new Dictionary<IObject,bool>();
		public static Dictionary<Type, IntPtr> _sgsInterfaces = new Dictionary<Type,IntPtr>();
		static int _offsetOfData = Marshal.OffsetOf( typeof(NI.VarObj), "data" ).ToInt32();

		static IObject()
		{
			_sgsNullObjectInterface = AllocInterface( new NI.ObjInterface(), "<nullObject>" );
		}

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
		
		// returns the cached interface for any supporting (IObject-based) class
		public static IntPtr GetClassInterface( Type type )
		{
			IntPtr iface;
			if( _sgsInterfaces.TryGetValue( type, out iface ) )
				return iface;

			NI.ObjInterface oi = new NI.ObjInterface()
			{
				destruct = new NI.OC_Self( _sgsDestruct ),
				gcmark = new NI.OC_Self( _sgsGCMark ),

				dump = new NI.OC_SlPr( _sgsDump ),
				// TODO
			};

			iface = AllocInterface( oi, type.Name );
			_sgsInterfaces.Add( type, iface );
			return iface;
		}


		public IntPtr _sgsObject = IntPtr.Zero;
		public Engine _sgsEngine;

		public IObject( Context c, bool skipInit = false )
		{
			_sgsEngine = c.GetEngine();
			if( !skipInit )
				AllocClassObject();
		}

		// returns the cached interface for the current class
		public IntPtr GetClassInterface()
		{
			return GetClassInterface( this.GetType() );
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
		}
		public void FreeClassObject()
		{
			if( _sgsObject == IntPtr.Zero )
				throw new SGSException( NI.EINPROC, "FreeClassObject - object is not allocated" );
			IntPtr handleP = Marshal.ReadIntPtr( _sgsObject, _offsetOfData );
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

		// callback wrappers
		public static IObject _IP2Obj( IntPtr varobj, bool freehandle = false )
		{
			IntPtr handleP = Marshal.ReadIntPtr( varobj, _offsetOfData );
			GCHandle h = GCHandle.FromIntPtr( handleP );
			IObject obj = (IObject) h.Target;
			if( freehandle )
				h.Free();
			return obj;
		}
		public static int _sgsDestruct( IntPtr ctx, IntPtr varobj )
		{
			IObject obj = _IP2Obj( varobj, true );
			obj._intOnDestroy();
			return NI.SUCCESS;
		}
		public static int _sgsGCMark( IntPtr ctx, IntPtr varobj )
		{
			IObject obj = _IP2Obj( varobj );
			obj.OnGCMark();
			return NI.SUCCESS;
		}
		public static int _sgsDump( IntPtr ctx, IntPtr varobj, int maxdepth )
		{
			IObject obj = _IP2Obj( varobj );
			return obj.OnDump( new Context( ctx ), maxdepth ) ? NI.SUCCESS : NI.EINPROC;
		}

		// core feature layer
		public virtual void _intOnDestroy()
		{
			RetakeOwnership();
			OnDestroy();
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

		// user override callbacks
		public virtual void OnDestroy(){}
		public virtual void OnGCMark(){}
		public virtual bool OnDump( Context ctx, int maxdepth ){ return false; }
	}


	public class Variable : IDisposable
	{
		public NI.Variable var;
		public Engine ctx;
		
		public Variable( Context c )
		{
			ctx = c.GetEngine();
			var = NI.MakeNull();
		}
		public Variable( Context c, NI.Variable v )
		{
			var = v;
			ctx = c.GetEngine();
			Acquire();
		}
		
		public virtual void Dispose()
		{
			Release();
		}

		public void Acquire(){ NI.Acquire( ctx.ctx, var ); }
		public void Release(){ NI.Release( ctx.ctx, ref var ); }

		public VarType type { get { return var.type; } }
		public bool isNull { get { return var.type == VarType.Null; } }
		public bool notNull { get { return var.type != VarType.Null; } }
		public bool GetBool(){ return NI.GetBoolP( ctx.ctx, var ); }
		public Int64 GetInt(){ return NI.GetIntP( ctx.ctx, var ); }
		public double GetReal(){ return NI.GetRealP( ctx.ctx, var ); }
		public string GetString(){ return NI.GetString( var ); }
		public string str { get { return GetString(); } }

		public override bool Equals( object obj )
		{
			if( obj == null )
				return false;
			Variable v = (Variable) obj;
			if( v == null )
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
		public static bool operator == ( Variable a, Variable b ){ return a.Equals( b ); }
		public static bool operator != ( Variable a, Variable b ){ return !a.Equals( b ); }
	}
}
