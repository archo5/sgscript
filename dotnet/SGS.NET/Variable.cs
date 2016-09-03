﻿using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace SGScript
{
	public abstract class IObject : NI.IUserData
	{
		public static Dictionary<IObject, bool> _sgsOwnedObjects;
		public static Dictionary<Type, IntPtr> _sgsInterfaces;
		public IntPtr _sgsObject;
		public Engine _sgsEngine;

		public IObject( Context c )
		{
			_sgsEngine = c.GetEngine();
			IntPtr iface = GetClassInterface();
		}

		/// INTERFACE SYSTEM ///
		// returns the cached interface for the current class
		public IntPtr GetClassInterface()
		{
			return GetClassInterface( this.GetType() );
		}
		// returns the cached interface for any supporting (IObject-based) class
		public static IntPtr GetClassInterface( Type type )
		{
			IntPtr iface;
			if( _sgsInterfaces.TryGetValue( type, out iface ) )
				return iface;

			byte[] nameBytes = System.Text.Encoding.UTF8.GetBytes( type.Name );
			iface = Marshal.AllocHGlobal( NI.ObjInterfaceSize + nameBytes.Length + 1 );
			IntPtr nameOffset = (IntPtr) ( iface.ToInt64() + NI.ObjInterfaceSize );
			NI.ObjInterface oi = new NI.ObjInterface()
			{
				name = nameOffset,
				destruct = new NI.OC_Self( _sgsDestruct ),
				gcmark = new NI.OC_Self( _sgsGCMark ),
				// TODO
			};
			Marshal.StructureToPtr( oi, iface, false );
			Marshal.Copy( nameBytes, 0, nameOffset, nameBytes.Length );
			Marshal.WriteByte( nameOffset, nameBytes.Length, 0 );

			_sgsInterfaces.Add( type, iface );
			return iface;
		}
		/// END OF INTERFACE SYSTEM ///

		// callback wrappers
		public static int _sgsDestruct( IntPtr ctx, NI.IUserData obj )
		{
			((IObject) obj)._intOnDestroy();
			return NI.SUCCESS;
		}
		public static int _sgsGCMark( IntPtr ctx, NI.IUserData obj )
		{
			((IObject) obj).OnGCMark();
			return NI.SUCCESS;
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
	}

	// the interface to set when destroying C# object
	public class NullObject : IObject
	{
		public NullObject( Context c ) : base( c ){}
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
		public bool GetBool(){ return NI.GetBoolP( ctx.ctx, var ); }
		public Int64 GetInt(){ return NI.GetIntP( ctx.ctx, var ); }
		public double GetReal(){ return NI.GetRealP( ctx.ctx, var ); }
		public string GetString(){ return NI.GetString( var ); }
		public string str { get { return GetString(); } }
	}
}
