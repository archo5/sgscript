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
			/* TODO acquire */
		}

		public virtual void Dispose()
		{
			Release();
		}

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

		public void SetPrintFunc( IPrinter pr )
		{
			NI.SetPrintFunc( ctx, new NI.PrintFunc( IPrinter._PrintFunc ), pr );
		}

		public void Acquire()
		{
			/* TODO */
		}
		public void Release()
		{
			if( ctx != IntPtr.Zero )
			{
				NI.ReleaseState( ctx );
				ctx = IntPtr.Zero;
			}
		}

		public void PushNull(){ NI.PushNull( ctx ); }
		public void Push( bool b ){ NI.PushBool( ctx, b ? 1 : 0 ); }
		public void Push( Int16 i ){ NI.PushInt( ctx, i ); }
		public void Push( Int32 i ){ NI.PushInt( ctx, i ); }
		public void Push( Int64 i ){ NI.PushInt( ctx, i ); }
		public void Push( UInt16 i ){ NI.PushInt( ctx, i ); }
		public void Push( UInt32 i ){ NI.PushInt( ctx, i ); }
		public void Push( UInt64 i ){ NI.PushInt( ctx, (Int64) i ); }
		public void Push( string str ){ if( str == null ) PushNull(); else NI.PushStringBuf( ctx, str, str.Length ); }

		public void Pop( int count ){ NI.Pop( ctx, count ); }
		public void PopSkip( int count, int skip ){ NI.PopSkip( ctx, count, skip ); }
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

		public void Destroy()
		{
			if( ctx != IntPtr.Zero )
			{
				NI.DestroyEngine( ctx );
				ctx = IntPtr.Zero;
			}
		}

		public override void Dispose()
		{
			Destroy();
		}
	}
}
