using System;

namespace SGScript
{
	public abstract class IMemory : NI.IUserData
	{
		public static IntPtr _MemFunc( NI.IUserData ud, IntPtr ptr, IntPtr size )
		{
			return ((IMemory) ud).MemoryAllocFree( ptr, size );
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
			else if( size != IntPtr.Zero ) return Alloc( size );
			if( ptr != IntPtr.Zero ) Free( ptr );
			return IntPtr.Zero;
		}
	}

	public abstract class IPrinter : NI.IUserData
	{
		public static void _PrintFunc( NI.IUserData ud, IntPtr ctx, int type, string message )
		{
			((IPrinter) ud).PrintMessage( new Context( ctx ), type, message );
		}

		public abstract void PrintMessage( Context ctx, int type, string message );
	}

	public class Context : IDisposable
	{
		public IntPtr ctx = IntPtr.Zero;
		
		public Context()
		{
		}
		public Context( IntPtr c )
		{
			ctx = c;
			Acquire();
		}

		public virtual void Dispose()
		{
			Release();
		}

		// = root context
		public Engine GetEngine(){ return new Engine( NI.RootContext( ctx ) ); }

		public int TryExec( string str ){ return NI.Exec( ctx, str ); }
		public int TryEval( string str ){ return NI.Eval( ctx, str ); }
		public int TryExecFile( string name ){ return NI.ExecFile( ctx, name ); }
		public int TryEvalFile( string name ){ return NI.EvalFile( ctx, name ); }
		public bool TryInclude( string name, string searchPath = null ){ return NI.Include( ctx, name, searchPath ) != 0; }

		public int Exec( string str ){ return NI.ResultToException( TryExec( str ) ); }
		public int Eval( string str ){ return NI.ResultToException( TryEval( str ) ); }
		public int ExecFile( string str ){ return NI.ResultToException( TryExecFile( str ) ); }
		public int EvalFile( string str ){ return NI.ResultToException( TryEvalFile( str ) ); }
		public void Include( string name, string searchPath = null ){ if( !TryInclude( name, searchPath ) ) NI.ResultToException( NI.EINPROC ); }

		public bool Abort(){ return NI.Abort( ctx ) != 0; }
		public IntPtr Stat( Stat type ){ return NI.Stat( ctx, type ); }
		public Int32 Cntl( Cntl what, Int32 val ){ return NI.Cntl( ctx, what, val ); }

		public void SetPrintFunc( IPrinter pr )
		{
			NI.SetPrintFunc( ctx, new NI.PrintFunc( IPrinter._PrintFunc ), pr );
		}

		public void Acquire()
		{
			NI.Variable var = new NI.Variable();
			var.type = VarType.Thread;
			var.data.T = ctx;
			NI.Acquire( ctx, var );
		}
		public void Release()
		{
			if( ctx != IntPtr.Zero )
			{
				NI.ReleaseState( ctx );
				ctx = IntPtr.Zero;
			}
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
		public Variable ArrayVar( Int32 numitems ){ NI.Variable arr; NI.CreateArray( ctx, numitems, out arr ); return new Variable( this, arr ); }

		public void PushNull(){ NI.PushNull( ctx ); }
		public void Push( bool b ){ NI.PushBool( ctx, b ? 1 : 0 ); }
		public void Push( Int16 i ){ NI.PushInt( ctx, i ); }
		public void Push( Int32 i ){ NI.PushInt( ctx, i ); }
		public void Push( Int64 i ){ NI.PushInt( ctx, i ); }
		public void Push( UInt16 i ){ NI.PushInt( ctx, i ); }
		public void Push( UInt32 i ){ NI.PushInt( ctx, i ); }
		public void Push( UInt64 i ){ NI.PushInt( ctx, (Int64) i ); }
		public void Push( string str ){ if( str == null ) PushNull(); else NI.PushStringBuf( ctx, str, str.Length ); }
		public void PushArray( Int32 numitems ){ NI.CreateArray( ctx, numitems ); }

		public void Pop( int count ){ NI.Pop( ctx, count ); }
		public void PopSkip( int count, int skip ){ NI.PopSkip( ctx, count, skip ); }

		public int StackSize(){ return NI.StackSize( ctx ); }
		public void SetStackSize( int size ){ NI.SetStackSize( ctx, size ); }
		public void SetDeltaSize( int diff ){ NI.SetDeltaSize( ctx, diff ); }
		public int AbsIndex( int item ){ return NI.AbsIndex( ctx, item ); }
		public bool IsValidIndex( int item ){ return NI.IsValidIndex( ctx, item ) != 0; }
		public Variable OptStackItem( int item ){ return new Variable( this, NI.OptStackItem( ctx, item ) ); }
		public Variable StackItem( int item ){ return new Variable( this, NI.StackItem( ctx, item ) ); }
		
		public bool SetGlobal( Variable key, Variable value ){ return NI.SetGlobal( ctx, key.var, value.var ) != 0; }
		public void SetGlobal( string key, Variable value ){ NI.SetGlobalByName( ctx, key, value.var ); }
	}

	public class Engine : Context
	{
		public Engine()
		{
			ctx = NI.CreateEngine();
		}
		public Engine( IMemory mem )
		{
			ctx = NI.CreateEngineExt( new NI.MemFunc( IMemory._MemFunc ), mem );
		}
		public Engine( IntPtr ctx ) : base( ctx ){}
	}
}
