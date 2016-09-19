using System;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;
using System.Text;

namespace SGScript
{
	public struct Nothing {};

	public abstract class IMemory
	{
		public static IntPtr _MemFunc( IntPtr ud, IntPtr ptr, IntPtr size )
		{
			return (HDL.GetObj( ud ) as IMemory).MemoryAllocFree( ptr, size );
		}

		public abstract IntPtr MemoryAllocFree( IntPtr ptr, IntPtr size );
	}

	public abstract class IMemoryExt : IMemory
	{
		public abstract IntPtr Alloc( IntPtr size );
		public abstract void Free( IntPtr ptr );
		public abstract IntPtr Realloc( IntPtr ptr, IntPtr size );

		public override IntPtr MemoryAllocFree( IntPtr ptr, IntPtr size )
		{
			if( ptr != IntPtr.Zero && size != IntPtr.Zero ) return Realloc( ptr, size );
			if( size != IntPtr.Zero ) return Alloc( size );
			if( ptr != IntPtr.Zero ) Free( ptr );
			return IntPtr.Zero;
		}
	}

	public class MemoryDefault : IMemory
	{
		public override IntPtr MemoryAllocFree( IntPtr ptr, IntPtr size )
		{
			if( ptr != IntPtr.Zero && size != IntPtr.Zero ) return MDL.Realloc( ptr, size );
			if( size != IntPtr.Zero ) return MDL.Alloc( size );
			if( ptr != IntPtr.Zero ) MDL.Free( ptr );
			return IntPtr.Zero;
		}
	}

	public class DbgMemoryHook : IMemory
	{
		public IMemory original;
		public static Dictionary<IntPtr, StackTrace> handles = new Dictionary<IntPtr, StackTrace>();

		public DbgMemoryHook( IMemory o ){ original = o; }
		public void CheckMemoryState()
		{
			foreach( KeyValuePair<IntPtr, StackTrace> handle in handles )
			{
				if( handle.Value == null )
				{
					throw new SGSException( RC.EINPROC, "DbgMemoryHook [CheckMemoryState]: handle " + handle.Key + " was not freed!" );
				}
			}
		}

		void DbgOnAlloc( IntPtr ptr )
		{
			if( handles.ContainsKey( ptr ) )
			{
				if( handles[ptr] != null )
					handles[ptr] = null; // previous handle was successfully freed
				else
					throw new SGSException( RC.EINPROC, "DbgMemoryHook [Alloc]: handle " + ptr + " was already allocated!" );
			}
			else
				handles.Add( ptr, null );
		}
		void DbgOnFree( IntPtr ptr )
		{
			if( handles.ContainsKey( ptr ) == false )
			{
				throw new SGSException( RC.EINPROC, "DbgMemoryHook [Free]: handle " + ptr + " was never allocated!" );
			}
			if( handles[ptr] != null )
			{
				Console.WriteLine( handles[ptr] );
				SGSException x = new SGSException( RC.EINPROC, "DbgMemoryHook [Free]: handle " + ptr + " was already freed!" );
				x.Data.Add( "Stack trace", handles[ptr] );
				throw x;
			}
			handles[ptr] = new StackTrace();
		}

		public override IntPtr MemoryAllocFree( IntPtr ptr, IntPtr size )
		{
			if( ptr != IntPtr.Zero && size != IntPtr.Zero )
			{
				DbgOnFree( ptr );
				ptr = original.MemoryAllocFree( ptr, size );
				DbgOnAlloc( ptr );
				return ptr;
			}
			if( size != IntPtr.Zero )
			{
				ptr = original.MemoryAllocFree( ptr, size );
				DbgOnAlloc( ptr );
				return ptr;
			}
			if( ptr != IntPtr.Zero )
			{
				DbgOnFree( ptr );
				original.MemoryAllocFree( ptr, size );
			}
			return IntPtr.Zero;
		}
	}

	public abstract class IMessenger
	{
		public static void _MsgFunc( IntPtr ud, IntPtr ctx, int type, string message )
		{
			(HDL.GetObj( ud ) as IMessenger).Message( new Context( ctx ), type, message );
		}

		public abstract void Message( Context ctx, int type, string message );
	}

	public abstract class IOutputWriter
	{
		public static void _OutFunc( IntPtr ud, IntPtr ctx, IntPtr data, IntPtr size )
		{
			byte[] buf = new byte[ size.ToInt32() ];
			Marshal.Copy( data, buf, 0, size.ToInt32() );
			(HDL.GetObj( ud ) as IOutputWriter).Write( new Context( ctx ), Encoding.UTF8.GetString( buf ) );
		}

		public abstract void Write( Context ctx, string text );
	}

	public class IStdOutWriter : IOutputWriter
	{
		public override void Write( Context ctx, string text ){ Console.Write( text ); }
	}
	public class IStdErrWriter : IOutputWriter
	{
		public override void Write( Context ctx, string text ){ Console.Error.Write( text ); }
	}

	public interface IGetVariable
	{
		Variable GetVariable( Context ctx );
	}

	public struct OutputFuncData
	{
		public IntPtr func;
		public IntPtr userdata;
	};
	public struct MsgFuncData
	{
		public IntPtr func;
		public IntPtr userdata;
	};

	public class Context : ISGSBase
	{
		public IntPtr ctx = IntPtr.Zero;
		
		public Context( IntPtr c, bool acquire = true ) : base( PartiallyConstructed.Value )
		{
			ctx = c;
			if( acquire )
				Acquire();
			if( this is Engine )
				_sgsEngine = (Engine) this;
			else
			{
				_sgsEngine = GetEngine();
				_sgsEngine.ProcessVarRemoveQueue();
				_sgsEngine._RegisterObj( this );
			}
		}

		// = root context
		public bool IsEngine(){ return NI.RootContext( ctx ) == ctx; }
		public Engine GetEngine(){ return Engine.GetFromCtx( NI.RootContext( ctx ) ); }

		public int TryExec( string str ){ return NI.Exec( ctx, str ); }
		public int TryEval( string str ){ return NI.Eval( ctx, str ); }
		public int TryExecFile( string name ){ return NI.ExecFile( ctx, name ); }
		public int TryEvalFile( string name ){ return NI.EvalFile( ctx, name ); }
		public bool TryInclude( string name, string searchPath = null ){ return NI.Include( ctx, name, searchPath ) != 0; }

		public int Exec( string str ){ return NI.ResultToException( TryExec( str ) ); }
		public int Eval( string str ){ return NI.ResultToException( TryEval( str ) ); }
		public int ExecFile( string str ){ return NI.ResultToException( TryExecFile( str ) ); }
		public int EvalFile( string str ){ return NI.ResultToException( TryEvalFile( str ) ); }
		public void Include( string name, string searchPath = null ){ if( !TryInclude( name, searchPath ) ) NI.ResultToException( RC.EINPROC ); }

		public bool Abort(){ return NI.Abort( ctx ) != 0; }
		public IntPtr Stat( Stat type ){ return NI.Stat( ctx, type ); }
		public Int32 Cntl( Cntl what, Int32 val ){ return NI.Cntl( ctx, what, val ); }

		public void LoadLib_Fmt(){ NI.LoadLib_Fmt( ctx ); }
		public void LoadLib_IO(){ NI.LoadLib_IO( ctx ); }
		public void LoadLib_Math(){ NI.LoadLib_Math( ctx ); }
		public void LoadLib_OS(){ NI.LoadLib_OS( ctx ); }
		public void LoadLib_RE(){ NI.LoadLib_RE( ctx ); }
		public void LoadLib_String(){ NI.LoadLib_String( ctx ); }

		IntPtr hOutFunc = IntPtr.Zero;
		NI.OutputFunc d_outfunc;
		public OutputFuncData GetOutputFunc()
		{
			OutputFuncData ofd;
			NI.GetOutputFunc( ctx, out ofd.func, out ofd.userdata );
			return ofd;
		}
		public IOutputWriter GetOutputWriter(){ return (IOutputWriter) HDL.GetObj( GetOutputFunc().userdata ); }
		public OutputFuncData SetOutputFunc( IOutputWriter pr )
		{
			OutputFuncData prev = GetOutputFunc();
			HDL.FreeIfAlloc( ref hOutFunc );
			hOutFunc = HDL.Alloc( pr );
			d_outfunc = new NI.OutputFunc( IOutputWriter._OutFunc );
			NI.SetOutputFunc( ctx, d_outfunc, hOutFunc );
			return prev;
		}
		public void SetOutputFunc( OutputFuncData ofd ){ NI.SetOutputFunc( ctx, ofd.func, ofd.userdata ); }
		public void Write( string message )
		{
			byte[] buf = Encoding.UTF8.GetBytes( message );
			NI.Write( ctx, buf, (IntPtr) buf.Length );
		}
		
		IntPtr hErrOutFunc = IntPtr.Zero;
		NI.OutputFunc d_erroutfunc;
		public OutputFuncData GetErrOutputFunc()
		{
			OutputFuncData ofd;
			NI.GetErrOutputFunc( ctx, out ofd.func, out ofd.userdata );
			return ofd;
		}
		public IOutputWriter GetErrOutputWriter(){ return (IOutputWriter) HDL.GetObj( GetErrOutputFunc().userdata ); }
		public OutputFuncData SetErrOutputFunc( IOutputWriter pr )
		{
			OutputFuncData prev = GetErrOutputFunc();
			HDL.FreeIfAlloc( ref hErrOutFunc );
			hErrOutFunc = HDL.Alloc( pr );
			d_erroutfunc = new NI.OutputFunc( IOutputWriter._OutFunc );
			NI.SetErrOutputFunc( ctx, d_erroutfunc, hErrOutFunc );
			return prev;
		}
		public void SetErrOutputFunc( OutputFuncData ofd ){ NI.SetErrOutputFunc( ctx, ofd.func, ofd.userdata ); }
		public void ErrWrite( string message )
		{
			byte[] buf = Encoding.UTF8.GetBytes( message );
			NI.ErrWrite( ctx, buf, (IntPtr) buf.Length );
		}

		IntPtr hMsgFunc = IntPtr.Zero;
		NI.MsgFunc d_msgfunc;
		public MsgFuncData GetMsgFunc()
		{
			MsgFuncData mfd;
			NI.GetMsgFunc( ctx, out mfd.func, out mfd.userdata );
			return mfd;
		}
		public IMessenger GetMessenger(){ return (IMessenger) HDL.GetObj( GetMsgFunc().userdata ); }
		public MsgFuncData SetMsgFunc( IMessenger pr )
		{
			MsgFuncData prev = GetMsgFunc();
			HDL.FreeIfAlloc( ref hMsgFunc );
			hMsgFunc = HDL.Alloc( pr );
			d_msgfunc = new NI.MsgFunc( IMessenger._MsgFunc );
			NI.SetMsgFunc( ctx, d_msgfunc, hMsgFunc );
			return prev;
		}
		public void SetMsgFunc( MsgFuncData mfd ){ NI.SetMsgFunc( ctx, mfd.func, mfd.userdata ); }
		public void Msg( int type, string message ){ NI.Msg( ctx, type, message ); }

		public void Acquire()
		{
			NI.Variable var = new NI.Variable();
			var.type = VarType.Thread;
			var.data.T = ctx;
			NI.Acquire( ctx, ref var );
		}
		public override void Release()
		{
			if( ctx != IntPtr.Zero )
			{
				_sgsEngine._ReleaseCtx( ref ctx );
				HDL.FreeIfAlloc( ref hMsgFunc );
			}
		}
		public Context Fork( bool copy = false )
		{
			return new Context( NI.ForkState( ctx, copy ? 1 : 0 ), false );
		}
		public void ResumeExt( int args = 0, int expect = 0 )
		{
			if( NI.ResumeStateExp( ctx, args, expect ) == 0 )
				throw new SGSException( RC.EINPROC, "Failed to resume coroutine" );
		}
		public int ResumeExtV( int args = 0 )
		{
			int outrvc;
			if( NI.ResumeStateRet( ctx, args, out outrvc ) == 0 )
				throw new SGSException( RC.EINPROC, "Failed to resume coroutine" );
			return outrvc;
		}
		public void Resume( params object[] args )
		{
			foreach( object arg in args )
				PushObj( args );
			ResumeExt( args.Length );
		}
		public object OResume( params object[] args )
		{
			foreach( object arg in args )
				PushObj( args );
			return RetrieveOneReturnValue( ResumeExtV( args.Length ) );
		}
		public T Resume<T>( params object[] args )
		{
			foreach( object arg in args )
				PushObj( args );
			return RetrieveOneReturnValueT<T>( ResumeExtV( args.Length ) );
		}
		public Variable CreateSubthreadExt( int args = 0, bool gotthis = false, Context argstack = null )
		{
			NI.Variable var;
			NI.CreateSubthread( ctx, argstack == null ? ctx : argstack.ctx, out var, args, gotthis ? 1 : 0 );
			return new Variable( this, var, false );
		}
		public Variable CreateSubthreadT( object thisvar, params object[] args )
		{
			if( thisvar != null )
				PushObj( thisvar );
			foreach( object arg in args )
				PushObj( args );
			return CreateSubthreadExt( args.Length, thisvar != null );
		}
		public Variable CreateSubthread( params object[] args ){ return CreateSubthreadT( null, args ); }
		public int ProcessSubthreads( double dt ){ return NI.ProcessSubthreads( ctx, dt ); }
		public void EndOn( Variable var, bool enable = true )
		{
			NI.EndOn( ctx, var.var, enable ? 1 : 0 );
		}
		
		void _IndexCheck( int item, string funcname )
		{
			int size = StackSize();
			if( item >= size || item < -size )
				throw new SGSException( RC.EBOUNDS, string.Format( "{0}({1}) failed with stack size = {2}", funcname, item, size ) );
		}
		void _SizeCheck( int numitems, string funcname )
		{
			int size = StackSize();
			if( numitems > size )
				throw new SGSException( RC.EBOUNDS, string.Format( "{0}({1}) failed with stack size = {2} - not enough items exist in the stack", funcname, numitems, size ) );
		}
		void _SizePairCheck( int numitems, string funcname )
		{
			_SizeCheck( numitems, funcname );
			if( numitems % 2 != 0 )
				throw new SGSException( RC.EINVAL, string.Format( "{0}({1}) failed - item count cannot be an odd number", funcname, numitems ) );
		}
		void _AnyCheck( string funcname )
		{
			int size = StackSize();
			if( size == 0 )
				throw new SGSException( RC.EBOUNDS, string.Format( "{0} failed with stack size = 0 - expected at least one variable on stack", funcname ) );
		}

		public Variable NullVar(){ return new Variable( this, NI.MakeNull() ); }
		public Variable Var( bool v ){ return new Variable( this, NI.MakeBool( v ) ); }
		public Variable Var( Int16 v ){ return new Variable( this, NI.MakeInt( v ) ); }
		public Variable Var( Int32 v ){ return new Variable( this, NI.MakeInt( v ) ); }
		public Variable Var( Int64 v ){ return new Variable( this, NI.MakeInt( v ) ); }
		public Variable Var( UInt16 v ){ return new Variable( this, NI.MakeInt( v ) ); }
		public Variable Var( UInt32 v ){ return new Variable( this, NI.MakeInt( v ) ); }
		public Variable Var( UInt64 v ){ return new Variable( this, NI.MakeInt( (Int64) v ) ); }
		public Variable Var( double v ){ return new Variable( this, NI.MakeReal( v ) ); }
		public Variable Var( IntPtr v ){ return new Variable( this, NI.MakePtr( v ) ); }
		public Variable Var( NI.Variable v, bool acquire = true ){ return new Variable( this, v, acquire ); }
		public Variable Var( string v ){ if( v == null ) return NullVar(); NI.Variable var; NI.InitStringBuf( ctx, out var, v ); return new Variable( this, var, false ); }
		public Variable Var( byte[] v ){ if( v == null ) return NullVar(); NI.Variable var; NI.InitStringBufB( ctx, out var, v, v.Length ); return new Variable( this, var, false ); }
		public Variable Var( IObjectBase v )
		{
			NI.Variable var = NI.MakeNull();
			if( v != null && v._sgsObject != IntPtr.Zero )
			{
				var.type = VarType.Object;
				var.data.T = v._sgsObject;
			}
			return new Variable( this, var );
		}
		public Variable Var( Context v )
		{
			NI.Variable var = NI.MakeNull();
			if( v != null )
			{
				var.type = VarType.Thread;
				var.data.T = v.ctx;
			}
			return new Variable( this, var );
		}
		public Variable SGSObjectVar( IntPtr v )
		{
			NI.Variable var = NI.MakeNull();
			if( v != IntPtr.Zero )
			{
				var.type = VarType.Object;
				var.data.O = v;
			}
			return new Variable( this, var );
		}
		public Variable Var( IGetVariable v ){ return v.GetVariable( this ); }
		public Variable ObjVar( object o )
		{
			if( o == null )        return NullVar();
			else if( o is bool )   return Var( (bool) o );
			else if( o is Byte )   return Var( (Byte) o );
			else if( o is SByte )  return Var( (SByte) o );
			else if( o is Int16 )  return Var( (Int16) o );
			else if( o is UInt16 ) return Var( (UInt16) o );
			else if( o is Int32 )  return Var( (Int32) o );
			else if( o is UInt32 ) return Var( (UInt32) o );
			else if( o is Int64 )  return Var( (Int64) o );
			else if( o is float )  return Var( (float) o );
			else if( o is double ) return Var( (double) o );
			else if( o is string ) return Var( (string) o );
			else if( o is byte[] ) return Var( (byte[]) o );
			else if( o is IntPtr ) return Var( (IntPtr) o );
			else if( o is IObject ) return Var( (IObject) o );
			else if( o is Context ) return Var( (Context) o );
			else if( o is IGetVariable ) return Var( (IGetVariable) o );
			else if( o is Variable ) return (Variable) o;
			else
				throw new SGSException( RC.ENOTSUP, string.Format( "Unsupported value was passed to ObjVar (type={0})", o.GetType().FullName ) );
		}

		public Variable ArrayVar( int numitems ){ _SizeCheck( numitems, "ArrayVar" ); NI.Variable arr; NI.CreateArray( ctx, numitems, out arr ); return new Variable( this, arr ); }
		public Variable DictVar( int numitems ){ _SizePairCheck( numitems, "DictVar" ); NI.Variable arr; NI.CreateDict( ctx, numitems, out arr ); return new Variable( this, arr ); }
		public Variable MapVar( int numitems ){ _SizePairCheck( numitems, "MapVar" ); NI.Variable arr; NI.CreateMap( ctx, numitems, out arr ); return new Variable( this, arr ); }
		public void PushArray( int numitems ){ _SizeCheck( numitems, "PushArray" ); NI.CreateArray( ctx, numitems ); }
		public void PushDict( int numitems ){ _SizePairCheck( numitems, "PushDict" ); NI.CreateDict( ctx, numitems ); }
		public void PushMap( int numitems ){ _SizePairCheck( numitems, "PushMap" ); NI.CreateMap( ctx, numitems ); }

		public void PushNull(){ NI.PushNull( ctx ); }
		public void Push( bool b ){ NI.PushBool( ctx, b ? 1 : 0 ); }
		public void Push( SByte i ){ NI.PushInt( ctx, i ); }
		public void Push( Int16 i ){ NI.PushInt( ctx, i ); }
		public void Push( Int32 i ){ NI.PushInt( ctx, i ); }
		public void Push( Int64 i ){ NI.PushInt( ctx, i ); }
		public void Push( Byte i ){ NI.PushInt( ctx, i ); }
		public void Push( UInt16 i ){ NI.PushInt( ctx, i ); }
		public void Push( UInt32 i ){ NI.PushInt( ctx, i ); }
		// manual casting would have been preferred but UInt64 resolves to double
		// (note the potential misinterpretation of data above Int64.MaxValue due to signedness cast)
		public void Push( UInt64 i ){ NI.PushInt( ctx, (Int64) i ); }
		public void Push( double v ){ NI.PushReal( ctx, v ); }
		public void Push( IntPtr p ){ NI.PushPtr( ctx, p ); }
		public void Push( bool? b ){ if( b.HasValue == false ) PushNull(); else NI.PushBool( ctx, b.Value ? 1 : 0 ); }
		public void Push( Byte? i ){ if( i.HasValue == false ) PushNull(); else NI.PushInt( ctx, i.Value ); }
		public void Push( SByte? i ){ if( i.HasValue == false ) PushNull(); else NI.PushInt( ctx, i.Value ); }
		public void Push( Int16? i ){ if( i.HasValue == false ) PushNull(); else NI.PushInt( ctx, i.Value ); }
		public void Push( Int32? i ){ if( i.HasValue == false ) PushNull(); else NI.PushInt( ctx, i.Value ); }
		public void Push( Int64? i ){ if( i.HasValue == false ) PushNull(); else NI.PushInt( ctx, i.Value ); }
		public void Push( UInt16? i ){ if( i.HasValue == false ) PushNull(); else NI.PushInt( ctx, i.Value ); }
		public void Push( UInt32? i ){ if( i.HasValue == false ) PushNull(); else NI.PushInt( ctx, i.Value ); }
		// manual casting would have been preferred but UInt64 resolves to double
		// (note the potential misinterpretation of data above Int64.MaxValue due to signedness cast)
		public void Push( UInt64? i ){ if( i.HasValue == false ) PushNull(); else NI.PushInt( ctx, (Int64) i.Value ); }
		public void Push( double? v ){ if( v.HasValue == false ) PushNull(); else NI.PushReal( ctx, v.Value ); }
		public void Push( IntPtr? p ){ if( p.HasValue == false ) PushNull(); else NI.PushPtr( ctx, p.Value ); }
		public void Push( string str ){ if( str == null ) PushNull(); else NI.PushStringBuf( ctx, str ); }
		public void Push( byte[] buf ){ if( buf == null ) PushNull(); else NI.PushStringBufB( ctx, buf, buf.Length ); }
		public void Push( IObjectBase o ){ if( o == null ) PushNull(); else NI.PushObjectPtr( ctx, o._sgsObject ); }
		public void Push( Context c ){ if( c == null ) PushNull(); else NI.PushThreadPtr( ctx, c.ctx ); }
		public void Push( IGetVariable p ){ if( p == null ) PushNull(); else Push( p.GetVariable( this ) ); }
		public void PushObj( object o )
		{
			if( o == null )        PushNull();
			else if( o is bool )   Push( (bool) o );
			else if( o is Byte )   Push( (Byte) o );
			else if( o is SByte )  Push( (SByte) o );
			else if( o is Int16 )  Push( (Int16) o );
			else if( o is UInt16 ) Push( (UInt16) o );
			else if( o is Int32 )  Push( (Int32) o );
			else if( o is UInt32 ) Push( (UInt32) o );
			else if( o is Int64 )  Push( (Int64) o );
			else if( o is float )  Push( (float) o );
			else if( o is double ) Push( (double) o );
			else if( o is string ) Push( (string) o );
			else if( o is IntPtr ) Push( (IntPtr) o );
			else if( o is IObject ) Push( (IObject) o );
			else if( o is Context ) Push( (Context) o );
			else if( o is IGetVariable ) Push( (IGetVariable) o );
			else if( o is Variable ) Push( (Variable) o );
			else
				throw new SGSException( RC.ENOTSUP, string.Format( "Unsupported value was passed to PushObj (type={0})", o.GetType().FullName ) );
		}
		public object ParseObj( int item )
		{
			_IndexCheck( item, "ParseObj" );
			Variable v = StackItem( item );
			switch( v.type )
			{
				case VarType.Null: return null;
				case VarType.Bool: return v.GetBool();
				case VarType.Int: return v.GetInt();
				case VarType.Real: return v.GetReal();
				case VarType.String: return v.GetString();
				case VarType.Func:
				case VarType.CFunc: return v;
				case VarType.Object:
					IObjectBase iob = IObjectBase.GetFromVarObj( v.var.data.O );
					if( iob != null )
						return iob;
					return v;
				case VarType.Ptr: return v.var.data.P;
				case VarType.Thread: return new Context( v.var.data.T );
				default: throw new SGSException( RC.EINVAL, string.Format( "Bad type ID detected while parsing item {0}", item ) );
			}
		}
		public void ParseVar( out bool b, Variable v ){ b = v.GetBool(); }
		public void ParseVar( out SByte i, Variable v ){ i = (SByte) v.GetInt(); }
		public void ParseVar( out Int16 i, Variable v ){ i = (Int16) v.GetInt(); }
		public void ParseVar( out Int32 i, Variable v ){ i = (Int32) v.GetInt(); }
		public void ParseVar( out Int64 i, Variable v ){ i = v.GetInt(); }
		public void ParseVar( out Byte i, Variable v ){ i = (Byte) v.GetInt(); }
		public void ParseVar( out UInt16 i, Variable v ){ i = (UInt16) v.GetInt(); }
		public void ParseVar( out UInt32 i, Variable v ){ i = (UInt32) v.GetInt(); }
		public void ParseVar( out UInt64 i, Variable v ){ i = (UInt64) v.GetInt(); }
		public void ParseVar( out float f, Variable v ){ f = (float) v.GetReal(); }
		public void ParseVar( out double f, Variable v ){ f = v.GetReal(); }
		public void ParseVar( out string s, Variable v ){ s = v.ConvertToString(); }
		public void ParseVar( out IObjectBase o, Variable v ){ o = v.GetIObjectBase(); }
		public void ParseVar( out Context c, Variable v ){ c = v.type == VarType.Thread ? new Context( v.var.data.T ) : null; }
		public void ParseVar( out Variable o, Variable v ){ o = v; }
		public void _ParseVar_CallingThread( out Context c, Variable v ){ c = this; }

		public void Push( Variable v ){ NI.PushVariable( ctx, v.var ); }
		public void PushItem( int item ){ _IndexCheck( item, "PushItem" ); NI.PushItem( ctx, item ); }
		public void InsertVar( int pos, Variable v )
		{
			int size = StackSize();
			if( pos > size || pos < -size - 1 )
				throw new SGSException( RC.EBOUNDS, string.Format( "InsertVar({0}) failed with stack size = {1}", pos, StackSize() ) );
			NI.InsertVariable( ctx, pos, v.var );
		}

		public void Pop( int count )
		{
			if( count < 0 || count > StackSize() )
				throw new SGSException( RC.EBOUNDS, string.Format( "Pop({0}) failed with stack size = {1}", count, StackSize() ) );
			NI.Pop( ctx, count );
		}
		public void PopSkip( int count, int skip ){ NI.PopSkip( ctx, count, skip ); }

		public int StackSize(){ return NI.StackSize( ctx ); }
		public void SetStackSize( int size ){ NI.SetStackSize( ctx, size ); }
		public void SetDeltaSize( int diff ){ NI.SetDeltaSize( ctx, diff ); }
		public int AbsIndex( int item ){ return NI.AbsIndex( ctx, item ); }
		public bool IsValidIndex( int item ){ return NI.IsValidIndex( ctx, item ) != 0; }
		public Variable OptStackItem( int item ){ return new Variable( this, NI.OptStackItem( ctx, item ) ); }
		public Variable StackItem( int item ){ return new Variable( this, NI.StackItem( ctx, item ) ); }
		public VarType ItemType( Int32 item ){ return NI.ItemType( ctx, item ); }
		public Variable[] TakeTopmostVars( Int32 count )
		{
			_SizeCheck( count, "TakeTopmostVars" );
			Variable[] vars = new Variable[ count ];
			for( Int32 i = 0; i < count; ++i )
			{
				vars[ i ] = StackItem( i - count );
			}
			Pop( count );
			return vars;
		}
		public Variable TakeTopmostVar()
		{
			_AnyCheck( "TakeTopmostVar" );
			Variable topmost = StackItem( -1 );
			Pop( 1 );
			return topmost;
		}

		public int XFCall( int args = 0, bool gotthis = false ){ _SizeCheck( args + ( gotthis ? 2 : 1 ), "[X]FCall" ); return NI.XFCall( ctx, args, gotthis ? 1 : 0 ); }
		public int FCall( int args = 0, int expect = 0, bool gotthis = false ){ return NI.AdjustStack( ctx, expect, XFCall( args, gotthis ) ); }

		public object[] RetrieveReturnValues( int count )
		{
			if( count < 0 || count > StackSize() )
				throw new SGSException( RC.EBOUNDS, string.Format( "RetrieveReturnValues({0}) failed with stack size = {1}", count, StackSize() ) );
			object[] retvals = new object[ count ];
			for( int i = 0; i < count; ++i )
			{
				retvals[ i ] = ParseObj( i - count );
			}
			Pop( count );
			return retvals;
		}
		public object RetrieveOneReturnValue( int count )
		{
			if( count == 0 )
				return null;
			if( count < 0 || count > StackSize() )
				throw new SGSException( RC.EBOUNDS, string.Format( "RetrieveOneReturnValue({0}) failed with stack size = {1}", count, StackSize() ) );
			object retval = ParseObj( -count );
			Pop( count );
			return retval;
		}
		public T RetrieveOneReturnValueT<T>( int count )
		{
			if( typeof(T) == typeof(Nothing) )
			{
				Pop( count );
				return (T) (object) new Nothing();
			}
			return (T) RetrieveOneReturnValue( count );
		}

		// various simple call functions
		// - X[This]Call: arguments are left on stack, count is returned
		public int XThisCall( string func, Variable thisvar, params object[] args ){ return XThisCall( thisvar.GetProp( func ), thisvar, args ); }
		public int XThisCall( Variable func, object thisvar, params object[] args )
		{
			Push( func );
			if( thisvar != null )
				PushObj( thisvar );
			foreach( object arg in args )
				PushObj( arg );
			return XFCall( args.Length, thisvar != null );
		}
		public int XCall( string func, params object[] args ){ return XCall( GetGlobal( func ), args ); }
		public int XCall( Variable func, params object[] args )
		{
			Push( func );
			foreach( object arg in args )
				PushObj( arg );
			return XFCall( args.Length );
		}
		// - A[This]Call: all arguments are returned
		public object[] AThisCall( string func, Variable thisvar, params object[] args ){ return RetrieveReturnValues( XThisCall( func, thisvar, args ) ); }
		public object[] AThisCall( Variable func, object thisvar, params object[] args ){ return RetrieveReturnValues( XThisCall( func, thisvar, args ) ); }
		public object[] ACall( string func, params object[] args ){ return RetrieveReturnValues( XCall( func, args ) ); }
		public object[] ACall( Variable func, params object[] args ){ return RetrieveReturnValues( XCall( func, args ) ); }
		// - O[This]Call: first argument (object) or null is returned
		public object OThisCall( string func, Variable thisvar, params object[] args ){ return RetrieveOneReturnValue( XThisCall( func, thisvar, args ) ); }
		public object OThisCall( Variable func, object thisvar, params object[] args ){ return RetrieveOneReturnValue( XThisCall( func, thisvar, args ) ); }
		public object OCall( string func, params object[] args ){ return RetrieveOneReturnValue( XCall( func, args ) ); }
		public object OCall( Variable func, params object[] args ){ return RetrieveOneReturnValue( XCall( func, args ) ); }
		// - [This]Call: first argument (converted to specified type) or null is returned
		public T ThisCall<T>( string func, Variable thisvar, params object[] args ){ return RetrieveOneReturnValueT<T>( XThisCall( func, thisvar, args ) ); }
		public T ThisCall<T>( Variable func, object thisvar, params object[] args ){ return RetrieveOneReturnValueT<T>( XThisCall( func, thisvar, args ) ); }
		public T Call<T>( string func, params object[] args ){ return RetrieveOneReturnValueT<T>( XCall( func, args ) ); }
		public T Call<T>( Variable func, params object[] args ){ return RetrieveOneReturnValueT<T>( XCall( func, args ) ); }

		// C call utility wrappers
		public bool Method(){ return NI.Method( ctx ) != 0; }
		public bool HideThis(){ return NI.HideThis( ctx ) != 0; }
		public bool ForceHideThis(){ return NI.ForceHideThis( ctx ) != 0; }
		public int ObjectArg(){ return NI.ObjectArg( ctx ); }

		// indexing
		public Variable GetIndex( Variable obj, Variable idx, bool isprop = false )
		{
			NI.Variable var;
			if( NI.GetIndex( ctx, obj.var, idx.var, out var, isprop ? 1 : 0 ) == 0 )
				return null;
			return Var( var, false );
		}
		public bool SetIndex( Variable obj, Variable idx, Variable val, bool isprop = false ){ return NI.SetIndex( ctx, obj.var, idx.var, val.var, isprop ? 1 : 0 ) != 0; }
		public bool PushIndex( Variable obj, Variable idx, bool isprop = false ){ return NI.PushIndex( ctx, obj.var, idx.var, isprop ? 1 : 0 ) != 0; }

		public bool PushProperty( Variable obj, string prop ){ return NI.PushProperty( ctx, obj.var, prop ) != 0; }
		
		public Variable GetGlobal( string key )
		{
			NI.Variable var;
			if( NI.GetGlobalByName( ctx, key, out var ) == 0 )
				return null;
			return Var( var, false );
		}
		public Variable GetGlobal( Variable key )
		{
			NI.Variable var;
			if( NI.GetGlobal( ctx, key.var, out var ) == 0 )
				return null;
			return Var( var, false );
		}
		public bool SetGlobal( Variable key, Variable value ){ return NI.SetGlobal( ctx, key.var, value.var ) != 0; }
		public bool PushGlobal( string key ){ return NI.PushGlobalByName( ctx, key ) != 0; }
		public void SetGlobal( string key, Variable value ){ NI.SetGlobalByName( ctx, key, value.var ); }

		// serialization interfaces
		public void SerializePush( Variable v, int mode = 0 )
		{
			if( mode < 0 || mode > 3 )
				throw new SGSException( RC.EINVAL, string.Format( "Invalid serialization mode ({0})", mode ) );
			NI.SerializeExt( ctx, v.var, mode );
		}
		public byte[] Serialize( Variable v, int mode = 0 ){ SerializePush( v, mode ); return TakeTopmostVar().GetByteArray(); }
		public void UnserializePushV( Variable v, int mode = 0 )
		{
			if( mode < 0 || mode > 3 )
				throw new SGSException( RC.EINVAL, string.Format( "Invalid serialization mode ({0})", mode ) );
			if( NI.UnserializeExt( ctx, v.var, mode ) == 0 )
				throw new SGSException( RC.EINPROC, "Failed to unserialize data" );
		}
		public void UnserializePush( byte[] data, int mode = 0 )
		{
			UnserializePushV( Var( data ), mode );
		}
		public Variable Unserialize( byte[] data, int mode = 0 ){ UnserializePush( data, mode ); return TakeTopmostVar(); }
		public void SerializeSGSONPush( Variable v, string tab = null ){ NI.SerializeSGSON( ctx, v.var, tab ); }
		public string SerializeSGSON( Variable v, string tab = null ){ NI.SerializeSGSON( ctx, v.var, tab ); return TakeTopmostVar().GetString(); }
		public void UnserializeSGSONPush( string text ){ NI.UnserializeSGSONExt( ctx, text, text.Length ); }
		public Variable UnserializeSGSON( string text ){ NI.UnserializeSGSONExt( ctx, text, text.Length ); return TakeTopmostVar(); }
		// - for implementing object serialization
		public void SerializeObject( Int32 args, string funcname ){ NI.SerializeObject( ctx, args, funcname ); }
		public void SerializeObjIndex( Variable key, Variable val, bool isprop ){ NI.SerializeObjIndex( ctx, key.var, val.var, isprop ? 1 : 0 ); }

		// utility function wrappers
		public void GCExecute(){ NI.GCExecute( ctx ); } // TODO implement backend
		// - typeof string
		public void TypeOfPush( Variable v ){ NI.TypeOf( ctx, v.var ); }
		public Variable TypeOfV( Variable v ){ TypeOfPush( v ); return TakeTopmostVar(); }
		public string TypeOf( Variable v ){ TypeOfPush( v ); return TakeTopmostVar().GetString(); }
		// - dumpvar string
		public void DumpVarPush( Variable v, int maxdepth = 5 ){ NI.DumpVar( ctx, v.var ); }
		public Variable DumpVarV( Variable v, int maxdepth = 5 ){ DumpVarPush( v, maxdepth ); return TakeTopmostVar(); }
		public string DumpVar( Variable v, int maxdepth = 5 ){ DumpVarPush( v, maxdepth ); return TakeTopmostVar().GetString(); }
		// - pad string
		public void PadStringOnStack(){ NI.PadString( ctx ); }
		public Variable PadString( Variable v ){ Push( v ); PadStringOnStack(); return TakeTopmostVar(); }
		public string PadString( string s ){ Push( s ); PadStringOnStack(); return TakeTopmostVar().GetString(); }
		// - safe-for-printing string
		public void ToPrintSafeStringOnStack(){ NI.ToPrintSafeString( ctx ); }
		public Variable ToPrintSafeString( Variable v ){ Push( v ); ToPrintSafeStringOnStack(); return TakeTopmostVar(); }
		public string ToPrintSafeString( string s ){ Push( s ); ToPrintSafeStringOnStack(); return TakeTopmostVar().GetString(); }
		// - string concatenation
		public void StringConcatOnStack( int count ){ NI.StringConcat( ctx, count ); }
		public Variable StringConcatV( params object[] args ){ foreach( object arg in args ){ PushObj( arg ); } StringConcatOnStack( args.Length ); return TakeTopmostVar(); }
		public string StringConcat( params object[] args ){ return StringConcatV( args ).GetString(); }
		// - cloning
		public void CloneItemAndPushOnStack( Variable v ){ NI.CloneItem( ctx, v.var ); }
		public Variable CloneItem( Variable v ){ CloneItemAndPushOnStack( v ); return TakeTopmostVar(); }
	}

	public class Engine : Context
	{
		public struct WeakRefWrap
		{
			public WeakReference wr;
		}

		public Dictionary<WeakReference, Nothing> _objRefs = new Dictionary<WeakReference, Nothing>(); // Key.Target = ISGSBase
		public Dictionary<Type, DNMetaObject> _metaObjects = new Dictionary<Type, DNMetaObject>();
		public Dictionary<Type, SGSClassInfo> _sgsClassInfo = new Dictionary<Type,SGSClassInfo>(); // < contains native memory, must be freed
		public Dictionary<Type, SGSClassInfo> _sgsStaticClassInfo = new Dictionary<Type,SGSClassInfo>(); // < contains native memory, must be freed
        public static Dictionary<IntPtr, WeakReference> _engines = new Dictionary<IntPtr, WeakReference>(); // Value.Target = Engine
		public Thread owningThread;
		public Queue<NI.Variable> _varReleaseQueue = new Queue<NI.Variable>();

		public static Engine GetFromCtx( IntPtr ctx )
		{
			WeakReference engine = null;
			_engines.TryGetValue( ctx, out engine );
			return engine != null ? (Engine) engine.Target : null;
		}

		NI.MemFunc d_memfunc;
		IntPtr hMem = IntPtr.Zero;
		public Engine() : base( IntPtr.Zero, false )
		{
			owningThread = Thread.CurrentThread;
			ctx = NI.CreateEngine( out d_memfunc );
			_engines.Add( ctx, _sgsWeakRef );
		}
		public Engine( IMemory mem ) : base( IntPtr.Zero, false )
		{
			owningThread = Thread.CurrentThread;
			hMem = HDL.Alloc( mem );
			d_memfunc = new NI.MemFunc( IMemory._MemFunc );
			ctx = NI.CreateEngineExt( d_memfunc, hMem );
			_engines.Add( ctx, _sgsWeakRef );
		}
		public override void Release()
		{
			if( ctx != IntPtr.Zero )
			{
                lock( _objRefs )
                {
                    lock( _varReleaseQueue )
                    {
                        WeakReference[] objrefs;
                        objrefs = new WeakReference[_objRefs.Count];
                        _objRefs.Keys.CopyTo( objrefs, 0 );
                        foreach( WeakReference wr in objrefs )
                        {
                            IDisposable d = ((IDisposable) wr.Target);
                            d.Dispose();
                        }

                        ProcessVarRemoveQueue();

                        if( _objRefs.Count != 0 )
                            throw new Exception( "[SGSINT] _objRefs.Count != 0 but is " + _objRefs.Count );
                        if( _varReleaseQueue.Count != 0 )
                            throw new Exception( "[SGSINT] _varReleaseQueue.Count != 0 but is " + _varReleaseQueue.Count );
                        
						_engines.Remove( ctx );
                        NI.DestroyEngine( ctx );
                        ctx = IntPtr.Zero;

                        // release interfaces
                        foreach( SGSClassInfo ci in _sgsClassInfo.Values )
                            MDL.Free( ci.iface );
                        foreach( SGSClassInfo ci in _sgsStaticClassInfo.Values )
                            MDL.Free( ci.iface );

                        HDL.FreeIfAlloc( ref hMem );
                    }
                }
			}
		}

		public void _ReleaseVar( ref NI.Variable v )
		{
			if( v.type == VarType.String || v.type == VarType.Func || v.type == VarType.Object || v.type == VarType.Thread )
			{
				if( ctx == IntPtr.Zero )
				{
					throw new Exception( "[SGSINT] Tried to release variable after engine has been freed!" );
				}
				if( Thread.CurrentThread == owningThread )
				{
					NI.Release( ctx, ref v );
				}
				else
				{
					lock( _varReleaseQueue )
					{
						_varReleaseQueue.Enqueue( v );
					}
				}
			}
			v = new NI.Variable(){ type = VarType.Null };
		}
		public void _ReleaseCtx( ref IntPtr c )
		{
			if( c == IntPtr.Zero )
				return;
			NI.Variable v = new NI.Variable(){ type = VarType.Thread };
			v.data.T = c;
			_ReleaseVar( ref v );
			c = IntPtr.Zero;
		}
		public void _ReleaseObj( ref IntPtr o )
		{
			if( o == IntPtr.Zero )
				return;
			NI.Variable v = new NI.Variable(){ type = VarType.Object };
			v.data.O = o;
			_ReleaseVar( ref v );
			o = IntPtr.Zero;
		}
		public void ProcessVarRemoveQueue()
		{
			if( Thread.CurrentThread != owningThread )
				throw new SGSException( RC.ENOTSUP, "ProcessVarRemoveQueue can be called only on the owning thread" );
			for (;;)
			{
				NI.Variable var;
				lock( _varReleaseQueue )
				{
					if( _varReleaseQueue.Count == 0 )
						break;
					var = _varReleaseQueue.Dequeue();
				}
				NI.Release( ctx, ref var );
			}
		}

		public DNMetaObject _GetMetaObject( Type t )
		{
			DNMetaObject dnmo;
			if( _metaObjects.TryGetValue( t, out dnmo ) )
				return dnmo;

			dnmo = new DNMetaObject( this, t );
			_metaObjects.Add( t, dnmo );
			return dnmo;
		}
		public void _RegisterObj( ISGSBase obj )
		{
			lock( _objRefs )
			{
				_objRefs.Add( obj._sgsWeakRef, new Nothing() );
			}
		}
		public void _UnregisterObj( ISGSBase obj )
		{
			if( obj is Engine )
				return;
			lock( _objRefs )
			{
				if( !_objRefs.Remove( obj._sgsWeakRef ) )
					throw new SGSException( RC.EINPROC, "Failed to unregister SGS object" );
			}
		}
	}
}
