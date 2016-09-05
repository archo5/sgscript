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

	public interface IGetVariable
	{
		Variable GetVariable( Context ctx );
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

		public void LoadLib_Fmt(){ NI.LoadLib_Fmt( ctx ); }
		public void LoadLib_IO(){ NI.LoadLib_IO( ctx ); }
		public void LoadLib_Math(){ NI.LoadLib_Math( ctx ); }
		public void LoadLib_OS(){ NI.LoadLib_OS( ctx ); }
		public void LoadLib_RE(){ NI.LoadLib_RE( ctx ); }
		public void LoadLib_String(){ NI.LoadLib_String( ctx ); }

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
		
		void _IndexCheck( int item, string funcname )
		{
			int size = StackSize();
			if( item >= size || item < -size )
				throw new SGSException( NI.EBOUNDS, string.Format( "{0}({1}) failed with stack size = {2}", funcname, item, size ) );
		}
		void _SizeCheck( int numitems, string funcname )
		{
			int size = StackSize();
			if( numitems > size )
				throw new SGSException( NI.EBOUNDS, string.Format( "{0}({1}) failed with stack size = {2} - not enough items exist in the stack", funcname, numitems, size ) );
		}
		void _SizePairCheck( int numitems, string funcname )
		{
			_SizeCheck( numitems, funcname );
			if( numitems % 2 != 0 )
				throw new SGSException( NI.EINVAL, string.Format( "{0}({1}) failed - item count cannot be an odd number", funcname, numitems ) );
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
		public Variable Var( NI.Variable v ){ return new Variable( this, v ); }
		public Variable Var( string v ){ if( v == null ) return NullVar(); NI.Variable var; NI.InitStringBuf( ctx, out var, v ); return new Variable( this, var ); }
		public Variable Var( IObject v )
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
			else if( o is IntPtr ) return Var( (IntPtr) o );
			else if( o is IObject ) return Var( (IObject) o );
			else if( o is Context ) return Var( (Context) o );
			else if( o is IGetVariable ) return Var( (IGetVariable) o );
			else if( o is Variable ) return (Variable) o;
			else
				throw new SGSException( NI.ENOTSUP, string.Format( "Unsupported value was passed to ObjVar (type={0})", o.GetType().FullName ) );
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
		public void Push( string str ){ if( str == null ) PushNull(); else NI.PushStringBuf( ctx, str, str.Length ); }
		public void Push( IObject o ){ if( o == null ) PushNull(); else NI.PushObjectPtr( ctx, o._sgsObject ); }
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
				throw new SGSException( NI.ENOTSUP, string.Format( "Unsupported value was passed to PushObj (type={0})", o.GetType().FullName ) );
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
				case VarType.Object: return v; // TODO IObject CHECK
				case VarType.Ptr: return v.var.data.P;
				case VarType.Thread: return new Context( v.var.data.T );
				default: throw new SGSException( NI.EINVAL, string.Format( "Bad type ID detected while parsing item {0}", item ) );
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

		public void Push( Variable v ){ NI.PushVariable( ctx, v.var ); }
		public void PushItem( int item ){ _IndexCheck( item, "PushItem" ); NI.PushItem( ctx, item ); }
		public void InsertVar( int pos, Variable v )
		{
			int size = StackSize();
			if( pos > size || pos < -size - 1 )
				throw new SGSException( NI.EBOUNDS, string.Format( "InsertVar({0}) failed with stack size = {1}", pos, StackSize() ) );
			NI.InsertVariable( ctx, pos, v.var );
		}

		public void Pop( int count )
		{
			if( count < 0 || count > StackSize() )
				throw new SGSException( NI.EBOUNDS, string.Format( "Pop({0}) failed with stack size = {1}", count, StackSize() ) );
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

		public int XFCall( int args = 0, bool gotthis = false ){ _SizeCheck( args + ( gotthis ? 2 : 1 ), "[X]FCall" ); return NI.XFCall( ctx, args, gotthis ? 1 : 0 ); }
		public int FCall( int args = 0, int expect = 0, bool gotthis = false ){ return NI.AdjustStack( ctx, expect, XFCall( args, gotthis ) ); }

		public object[] RetrieveReturnValues( int count )
		{
			if( count < 0 || count > StackSize() )
				throw new SGSException( NI.EBOUNDS, string.Format( "RetrieveReturnValues({0}) failed with stack size = {1}", count, StackSize() ) );
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
				throw new SGSException( NI.EBOUNDS, string.Format( "RetrieveOneReturnValue({0}) failed with stack size = {1}", count, StackSize() ) );
			object retval = ParseObj( -count );
			Pop( count );
			return retval;
		}

		// various simple call functions
		// - X[This]Call: arguments are left on stack, count is returned
		public int XThisCall( string func, object thisvar, params object[] args ){ return XThisCall( GetGlobal( func ), thisvar, args ); }
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
		public object[] AThisCall( string func, object thisvar, params object[] args ){ return RetrieveReturnValues( XThisCall( func, thisvar, args ) ); }
		public object[] AThisCall( Variable func, object thisvar, params object[] args ){ return RetrieveReturnValues( XThisCall( func, thisvar, args ) ); }
		public object[] ACall( string func, params object[] args ){ return RetrieveReturnValues( XCall( func, args ) ); }
		public object[] ACall( Variable func, params object[] args ){ return RetrieveReturnValues( XCall( func, args ) ); }
		// - O[This]Call: first argument (object) or null is returned
		public object OThisCall( string func, object thisvar, params object[] args ){ return RetrieveOneReturnValue( XThisCall( func, thisvar, args ) ); }
		public object OThisCall( Variable func, object thisvar, params object[] args ){ return RetrieveOneReturnValue( XThisCall( func, thisvar, args ) ); }
		public object OCall( string func, params object[] args ){ return RetrieveOneReturnValue( XCall( func, args ) ); }
		public object OCall( Variable func, params object[] args ){ return RetrieveOneReturnValue( XCall( func, args ) ); }
		// - [This]Call: first argument (converted to specified type) or null is returned
		public T ThisCall<T>( string func, object thisvar, params object[] args ){ return (T) RetrieveOneReturnValue( XThisCall( func, thisvar, args ) ); }
		public T ThisCall<T>( Variable func, object thisvar, params object[] args ){ return (T) RetrieveOneReturnValue( XThisCall( func, thisvar, args ) ); }
		public T Call<T>( string func, params object[] args ){ return (T) RetrieveOneReturnValue( XCall( func, args ) ); }
		public T Call<T>( Variable func, params object[] args ){ return (T) RetrieveOneReturnValue( XCall( func, args ) ); }

		public void GCExecute(){ NI.GCExecute( ctx ); }
		
		public Variable GetIndex( Variable obj, Variable idx, bool isprop = false )
		{
			NI.Variable var;
			if( !NI.GetIndex( ctx, obj.var, idx.var, out var, isprop ) )
				return null;
			return Var( var );
		}
		public bool SetIndex( Variable obj, Variable idx, Variable val, bool isprop = false ){ return NI.SetIndex( ctx, obj.var, idx.var, val.var, isprop ? 1 : 0 ) != 0; }
		public bool PushIndex( Variable obj, Variable idx, bool isprop = false ){ return NI.PushIndex( ctx, obj.var, idx.var, isprop ? 1 : 0 ) != 0; }

		public bool PushProperty( Variable obj, string prop ){ return NI.PushProperty( ctx, obj.var, prop ) != 0; }
		
		public Variable GetGlobal( string key )
		{
			NI.Variable var;
			if( !NI.GetGlobalByName( ctx, key, out var ) )
				return null;
			return Var( var );
		}
		public Variable GetGlobal( Variable key )
		{
			NI.Variable var;
			if( !NI.GetGlobal( ctx, key.var, out var ) )
				return null;
			return Var( var );
		}
		public bool SetGlobal( Variable key, Variable value ){ return NI.SetGlobal( ctx, key.var, value.var ) != 0; }
		public bool PushGlobal( string key ){ return NI.PushGlobalByName( ctx, key ) != 0; }
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
