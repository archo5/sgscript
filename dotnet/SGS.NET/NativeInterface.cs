﻿using System;
using System.Runtime.InteropServices;
using System.Text;

namespace SGScript
{
	public class UTF8Marshaler : ICustomMarshaler
	{
		static UTF8Marshaler inst = null;

		public static ICustomMarshaler GetInstance( string name )
		{
			if( inst == null )
				inst = new UTF8Marshaler();
			return inst;
		}

		public void CleanUpManagedData( object obj )
		{
		}
		public void CleanUpNativeData( IntPtr mem )
		{
			Marshal.FreeHGlobal( mem );
		}
		public int GetNativeDataSize()
		{
			return -1;
		}
		public IntPtr MarshalManagedToNative( object obj )
		{
			if( obj == null )
				return IntPtr.Zero;
			byte[] bytes = Encoding.UTF8.GetBytes( (string) obj );
			IntPtr mem = Marshal.AllocHGlobal( bytes.Length + 1 );
			Marshal.Copy( bytes, 0, mem, bytes.Length );
			Marshal.WriteByte( mem, bytes.Length, 0 );
			return mem;
		}
		public object MarshalNativeToManaged( IntPtr mem )
		{
			int size = 0;
			while( Marshal.ReadByte( mem, size ) != 0 )
				size++;
			byte[] bytes = new byte[ size ];
			Marshal.Copy( mem, bytes, 0, size );
			return Encoding.UTF8.GetString( bytes );
		}
	}

	public class SGSException : Exception
	{
		public static string MakeExceptionMessage( int code, string additionalInfo )
		{
			string codeMsg = NI.ResultToString( code );
			if( additionalInfo != null && additionalInfo != "" )
				return string.Format( "{0} ({1})", additionalInfo, codeMsg );
			else
				return codeMsg;
		}

		public SGSException( int code, string additionalInfo = "" ) : base( MakeExceptionMessage( code, additionalInfo ) )
		{
			resultCode = code;
		}

		public int resultCode;
	}

	public enum VarType : uint
	{
		Null =   0,
		Bool =   1,
		Int =    2,
		Real =   3,
		String = 4,
		Func =   5,
		CFunc =  6,
		Object = 7,
		Ptr =    8,
		Thread = 9,
		COUNT  = 10,
	}

	public enum Stat : int
	{
		Version      = 0,
		StateCount   = 1,
		ObjCount     = 2,
		MemSize      = 3,
		NumAllocs    = 4,
		NumFrees     = 5,
		NumBlocks    = 6,
		DumpStack    = 10,
		DumpGlobals  = 11,
		DumpObjects  = 12,
		DumpFrames   = 13,
		DumpStats    = 14,
		DumpSymbols  = 15,
		DumpRsrc     = 16,
		XDumpStack   = 20,
	}

	public enum Cntl : int
	{
		State      = 1,
		GetState   = 2,
		MinLev     = 3,
		GetMinLev  = 4,
		ApiLev     = 5,
		GetApiLev  = 6,
		Errno      = 7,
		SetErrno   = 8,
		GetErrno   = 9,
		ErrSup     = 10,
		GetErrSup  = 11,
		NumRetVals = 13,
		GetPaused  = 14,
		GetAbort   = 15,
	}

	public enum ConvOp
	{
		ToBool = (int) VarType.Bool,
		ToString = (int) VarType.String,
		Clone = 0x10000,
		ToIter = 0x30000,
	}

	// native interface
	public class NI
	{
		// message level presets
		public const int INFO    = 100;
		public const int WARNING = 200;
		public const int ERROR   = 300;
		public const int APIERR  = 330;
		public const int INTERR  = 360;

		// result codes
		public const int SUCCESS  = 0;
		public const int ENOTFND = -1;
		public const int ECOMP   = -2;
		public const int ENOTSUP = -4;
		public const int EBOUNDS = -5;
		public const int EINVAL  = -6;
		public const int EINPROC = -7;

		// CodeString codes
		public const int CODE_ER = 0; /* error codes */
		public const int CODE_VT = 1; /* variable type */
		public const int CODE_OP = 2; /* VM instruction */

		// version info
		public const string Version = "1.4.0";
		public const int VersionMajor = 1;
		public const int VersionMinor = 4;
		public const int VersionIncr = 0;


		// helper functions
		public static string ResultToString( int result )
		{
			if( result >= 0 )
			{
				return string.Format( "[SUCCESS] Operation was successful (value={0})", result );
			}
			switch( result )
			{
				case ENOTFND: return "[ENOTFND] Item was not found";
				case ECOMP:   return "[ECOMP] Compiler error";
				case ENOTSUP: return "[ENOTSUP] Operation not supported";
				case EBOUNDS: return "[EBOUNDS] Index out of bounds";
				case EINVAL:  return "[EINVAL] Invalid value";
				case EINPROC: return "[EINPROC] Procedure failed";
				default: return string.Format( "Unknown result ({0})", result );
			}
		}
		public static int ResultToException( int result, string additionalInfo = "" )
		{
			if( result < 0 )
				throw new SGSException( result, additionalInfo );
			return result;
		}


		[UnmanagedFunctionPointer( CallingConvention.Cdecl )]
		public delegate int OC_Self( IntPtr ctx, IntPtr obj );
		
		[UnmanagedFunctionPointer( CallingConvention.Cdecl )]
		public delegate int OC_SlPr( IntPtr ctx, IntPtr obj, int param );

		public struct ObjInterface
		{
			public IntPtr name;

			public OC_Self destruct;
			public OC_Self gcmark;

			public OC_Self getindex;
			public OC_Self setindex;

			public OC_SlPr convert;
			public OC_Self serialize;
			public OC_SlPr dump;
			public OC_SlPr getnext;

			public OC_Self call;
			public OC_Self expr;
		};
		public static int ObjInterfaceSize { get { return Marshal.SizeOf( typeof( ObjInterface ) ); } }

		public struct VarObj
		{
			public Int32 refcount;
			public UInt32 appsize;
			public byte redblue;
			public byte mm_enable;
			public byte is_iface;
			public byte in_setindex;
			public IntPtr data;
			public IntPtr iface;
			public IntPtr prev;
			public IntPtr next;
			public IntPtr metaobj;
			
			public static int offsetOfData = Marshal.OffsetOf( typeof(NI.VarObj), "data" ).ToInt32();
		};

		[StructLayout(LayoutKind.Explicit, Size=8)]
		public struct VarData
		{
			[FieldOffset(0)] public Int32 B;
			[FieldOffset(0)] public Int64 I;
			[FieldOffset(0)] public Double R;
			[FieldOffset(0)] public IntPtr S;
			[FieldOffset(0)] public IntPtr F;
			[FieldOffset(0)] public IntPtr C;
			[FieldOffset(0)] public IntPtr O;
			[FieldOffset(0)] public IntPtr P;
			[FieldOffset(0)] public IntPtr T;
		};
		public struct Variable
		{
			public VarType type;
			public VarData data;
		};


		public interface IUserData {}

		[UnmanagedFunctionPointer( CallingConvention.Cdecl )]
		public delegate IntPtr MemFunc( IUserData ud, IntPtr ptr, IntPtr size );

		[UnmanagedFunctionPointer( CallingConvention.Cdecl )]
		public delegate void PrintFunc( IUserData ud, IntPtr ctx, int type, [MarshalAs( UnmanagedType.CustomMarshaler, MarshalTypeRef = typeof(UTF8Marshaler) )] string message );


		public static IntPtr DefaultMemFunc( IUserData ud, IntPtr ptr, IntPtr size )
		{
			if( ptr != IntPtr.Zero && size != IntPtr.Zero ) return Marshal.ReAllocHGlobal( ptr, size );
			else if( size != IntPtr.Zero ) return Marshal.AllocHGlobal( size );
			if( ptr != IntPtr.Zero ) Marshal.FreeHGlobal( ptr );
			return IntPtr.Zero;
		}


		[DllImport( "sgscript.dll", EntryPoint = "sgs_CreateEngineExt", CallingConvention = CallingConvention.Cdecl )]
		public static extern IntPtr CreateEngineExt( MemFunc mf, IUserData mfuserdata );

		public static IntPtr CreateEngine()
		{
			return CreateEngineExt( new MemFunc( DefaultMemFunc ), null );
		}

		[DllImport( "sgscript.dll", EntryPoint = "sgs_DestroyEngine", CallingConvention = CallingConvention.Cdecl )]
		public static extern void DestroyEngine( IntPtr ctx );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_RootContext", CallingConvention = CallingConvention.Cdecl )]
		public static extern IntPtr RootContext( IntPtr ctx );
		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_ForkState", CallingConvention = CallingConvention.Cdecl )]
		public static extern IntPtr ForkState( IntPtr ctx, int copystate );
		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_ReleaseState", CallingConvention = CallingConvention.Cdecl )]
		public static extern void ReleaseState( IntPtr ctx );
		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_PauseState", CallingConvention = CallingConvention.Cdecl )]
		public static extern int PauseState( IntPtr ctx );
		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_ResumeStateRet", CallingConvention = CallingConvention.Cdecl )]
		public static unsafe extern int _ResumeStateRet( IntPtr ctx, int args, int* outrvc );
		public static unsafe int ResumeStateRet( IntPtr ctx, int args, out int outrvc )
		{
			int rvc;
			int ret = _ResumeStateRet( ctx, args, &rvc );
			outrvc = rvc;
			return ret;
		}
		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_ResumeStateExp", CallingConvention = CallingConvention.Cdecl )]
		public static extern int ResumeStateExp( IntPtr ctx, int args, int expect );
		public static int ResumeState( IntPtr ctx ){ return ResumeStateExp( ctx, 0, 0 ); }
		
		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_CodeString", CallingConvention = CallingConvention.Cdecl )]
		[return: MarshalAs( UnmanagedType.CustomMarshaler, MarshalTypeRef = typeof(UTF8Marshaler) )]
		public static extern string CodeString( int type, int val );


		[DllImport( "sgscript.dll", EntryPoint = "sgs_SetPrintFunc", CallingConvention = CallingConvention.Cdecl )]
		public static extern void SetPrintFunc( IntPtr ctx, PrintFunc pfn, IUserData printer );


		[DllImport( "sgscript.dll", EntryPoint = "sgs_EvalBuffer", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi )]
		public static extern int EvalBuffer( IntPtr ctx, [MarshalAs( UnmanagedType.CustomMarshaler, MarshalTypeRef = typeof(UTF8Marshaler) )] string buf, Int32 bufsz );
		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_EvalFile", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi )]
		public static extern int EvalFile( IntPtr ctx, [MarshalAs( UnmanagedType.CustomMarshaler, MarshalTypeRef = typeof(UTF8Marshaler) )] string buf );
		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_AdjustStack", CallingConvention = CallingConvention.Cdecl )]
		public static extern int AdjustStack( IntPtr ctx, int expected, int ret );
		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_IncludeExt", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi )]
		public static extern int Include( IntPtr ctx,
			[MarshalAs( UnmanagedType.CustomMarshaler, MarshalTypeRef = typeof(UTF8Marshaler) )] string name,
			[MarshalAs( UnmanagedType.CustomMarshaler, MarshalTypeRef = typeof(UTF8Marshaler) )] string searchpath = null );
		
		public static int Eval( IntPtr ctx, string str ) { return EvalBuffer( ctx, str, str.Length ); }
		public static int Exec( IntPtr ctx, string str ) { return AdjustStack( ctx, 0, Eval( ctx, str ) ); }
		public static int ExecFile( IntPtr ctx, string str ) { return AdjustStack( ctx, 0, EvalFile( ctx, str ) ); }

		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_Abort", CallingConvention = CallingConvention.Cdecl )]
		public static extern int Abort( IntPtr ctx );
		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_Stat", CallingConvention = CallingConvention.Cdecl )]
		public static extern IntPtr Stat( IntPtr ctx, Stat type );
		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_Cntl", CallingConvention = CallingConvention.Cdecl )]
		public static extern Int32 Cntl( IntPtr ctx, Cntl what, Int32 val );


		[DllImport( "sgscript.dll", EntryPoint = "sgs_LoadLib_Fmt", CallingConvention = CallingConvention.Cdecl )]
		public static extern void LoadLib_Fmt( IntPtr ctx );
		[DllImport( "sgscript.dll", EntryPoint = "sgs_LoadLib_IO", CallingConvention = CallingConvention.Cdecl )]
		public static extern void LoadLib_IO( IntPtr ctx );
		[DllImport( "sgscript.dll", EntryPoint = "sgs_LoadLib_Math", CallingConvention = CallingConvention.Cdecl )]
		public static extern void LoadLib_Math( IntPtr ctx );
		[DllImport( "sgscript.dll", EntryPoint = "sgs_LoadLib_OS", CallingConvention = CallingConvention.Cdecl )]
		public static extern void LoadLib_OS( IntPtr ctx );
		[DllImport( "sgscript.dll", EntryPoint = "sgs_LoadLib_RE", CallingConvention = CallingConvention.Cdecl )]
		public static extern void LoadLib_RE( IntPtr ctx );
		[DllImport( "sgscript.dll", EntryPoint = "sgs_LoadLib_String", CallingConvention = CallingConvention.Cdecl )]
		public static extern void LoadLib_String( IntPtr ctx );


		public static Variable MakeNull(){ Variable var = new Variable(); var.type = VarType.Null; return var; }
		public static Variable MakeBool( bool v ){ Variable var = new Variable(); var.type = VarType.Bool; var.data.B = v ? 1 : 0; return var; }
		public static Variable MakeInt( Int64 v ){ Variable var = new Variable(); var.type = VarType.Int; var.data.I = v; return var; }
		public static Variable MakeReal( double v ){ Variable var = new Variable(); var.type = VarType.Real; var.data.R = v; return var; }
		public static Variable MakeCFunc( IntPtr v ){ Variable var = new Variable(); var.type = VarType.CFunc; var.data.C = v; return var; }
		public static Variable MakeObjPtrNoRef( IntPtr v ){ Variable var = new Variable(); var.type = VarType.Object; var.data.O = v; return var; }
		public static Variable MakePtr( IntPtr v ){ Variable var = new Variable(); var.type = VarType.Ptr; var.data.P = v; return var; }


		[DllImport( "sgscript.dll", EntryPoint = "sgs_InitStringBuf", CallingConvention = CallingConvention.Cdecl )]
		public static unsafe extern void _InitStringBuf( IntPtr ctx, Variable* outvar, [MarshalAs( UnmanagedType.CustomMarshaler, MarshalTypeRef = typeof(UTF8Marshaler) )] string buf, Int32 bufsz );
		public static unsafe void InitStringBuf( IntPtr ctx, out Variable outvar, string buf ){ Variable item; _InitStringBuf( ctx, &item, buf, buf.Length ); outvar = item; }

		[DllImport( "sgscript.dll", EntryPoint = "sgs_InitString", CallingConvention = CallingConvention.Cdecl )]
		public static unsafe extern void _InitNullTerminatedString( IntPtr ctx, Variable* outvar, [MarshalAs( UnmanagedType.CustomMarshaler, MarshalTypeRef = typeof(UTF8Marshaler) )] string str );
		public static unsafe void InitNullTerminatedString( IntPtr ctx, out Variable outvar, string str ){ Variable item; _InitNullTerminatedString( ctx, &item, str ); outvar = item; }

		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_CreateObject", CallingConvention = CallingConvention.Cdecl )]
		public static unsafe extern int _CreateObject( IntPtr ctx, Variable* outvar, IntPtr data, IntPtr iface );
		public static unsafe void CreateObject( IntPtr ctx, IntPtr data, IntPtr iface ){ _CreateObject( ctx, null, data, iface ); }
		public static unsafe void CreateObject( IntPtr ctx, IntPtr data, IntPtr iface, out Variable outvar ){ Variable v; _CreateObject( ctx, &v, data, iface ); outvar = v; }
		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_CreateObjectIPA", CallingConvention = CallingConvention.Cdecl )]
		public static unsafe extern IntPtr _CreateObjectIPA( IntPtr ctx, Variable* outvar, UInt32 mem, IntPtr iface );
		public static unsafe IntPtr CreateObjectIPA( IntPtr ctx, UInt32 mem, IntPtr iface ){ return _CreateObjectIPA( ctx, null, mem, iface ); }
		public static unsafe IntPtr CreateObjectIPA( IntPtr ctx, UInt32 mem, IntPtr iface, out Variable outvar ){ Variable v; IntPtr p = _CreateObjectIPA( ctx, &v, mem, iface ); outvar = v; return p; }

		[DllImport( "sgscript.dll", EntryPoint = "sgs_CreateArray", CallingConvention = CallingConvention.Cdecl )]
		public static unsafe extern int _CreateArray( IntPtr ctx, Variable* outvar, Int32 numitems );
		public static unsafe void CreateArray( IntPtr ctx, Int32 numitems ){ _CreateArray( ctx, null, numitems ); }
		public static unsafe void CreateArray( IntPtr ctx, Int32 numitems, out Variable outvar ){ Variable v; _CreateArray( ctx, &v, numitems ); outvar = v; }

		[DllImport( "sgscript.dll", EntryPoint = "sgs_CreateDict", CallingConvention = CallingConvention.Cdecl )]
		public static unsafe extern int _CreateDict( IntPtr ctx, Variable* outvar, Int32 numitems );
		public static unsafe void CreateDict( IntPtr ctx, Int32 numitems ){ _CreateDict( ctx, null, numitems ); }
		public static unsafe void CreateDict( IntPtr ctx, Int32 numitems, out Variable outvar ){ Variable v; _CreateDict( ctx, &v, numitems ); outvar = v; }

		[DllImport( "sgscript.dll", EntryPoint = "sgs_CreateMap", CallingConvention = CallingConvention.Cdecl )]
		public static unsafe extern int _CreateMap( IntPtr ctx, Variable* outvar, Int32 numitems );
		public static unsafe void CreateMap( IntPtr ctx, Int32 numitems ){ _CreateMap( ctx, null, numitems ); }
		public static unsafe void CreateMap( IntPtr ctx, Int32 numitems, out Variable outvar ){ Variable v; _CreateMap( ctx, &v, numitems ); outvar = v; }


		[DllImport( "sgscript.dll", EntryPoint = "sgs_PushNulls", CallingConvention = CallingConvention.Cdecl )]
		public static extern int PushNulls( IntPtr ctx, int count );
		public static int PushNull( IntPtr ctx ){ return PushNulls( ctx, 1 ); }

		[DllImport( "sgscript.dll", EntryPoint = "sgs_PushBool", CallingConvention = CallingConvention.Cdecl )]
		public static extern int PushBool( IntPtr ctx, Int32 v );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_PushInt", CallingConvention = CallingConvention.Cdecl )]
		public static extern int PushInt( IntPtr ctx, Int64 v );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_PushReal", CallingConvention = CallingConvention.Cdecl )]
		public static extern int PushReal( IntPtr ctx, double v );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_PushStringBuf", CallingConvention = CallingConvention.Cdecl )]
		public static extern int PushStringBuf( IntPtr ctx, [MarshalAs( UnmanagedType.CustomMarshaler, MarshalTypeRef = typeof(UTF8Marshaler) )] string buf, Int32 bufsz );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_PushString", CallingConvention = CallingConvention.Cdecl )]
		public static extern int PushNullTerminatedString( IntPtr ctx, [MarshalAs( UnmanagedType.CustomMarshaler, MarshalTypeRef = typeof(UTF8Marshaler) )] string str );
		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_PushObjectPtr", CallingConvention = CallingConvention.Cdecl )]
		public static extern int PushObjectPtr( IntPtr ctx, IntPtr v );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_PushThreadPtr", CallingConvention = CallingConvention.Cdecl )]
		public static extern int PushThreadPtr( IntPtr ctx, IntPtr v );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_PushPtr", CallingConvention = CallingConvention.Cdecl )]
		public static extern int PushPtr( IntPtr ctx, IntPtr v );


		[DllImport( "sgscript.dll", EntryPoint = "sgs_PushVariable", CallingConvention = CallingConvention.Cdecl )]
		public static extern int PushVariable( IntPtr ctx, Variable v );
		
		public static int PushItem( IntPtr ctx, Int32 item ){ return PushVariable( ctx, StackItem( ctx, item ) ); }

		[DllImport( "sgscript.dll", EntryPoint = "sgs_InsertVariable", CallingConvention = CallingConvention.Cdecl )]
		public static extern void InsertVariable( IntPtr ctx, Int32 pos, Variable v );


		[DllImport( "sgscript.dll", EntryPoint = "sgs_GetIndex", CallingConvention = CallingConvention.Cdecl )]
		public static unsafe extern int _GetIndex( IntPtr ctx, Variable obj, Variable idx, Variable* outvar, int isprop );
		public static unsafe bool GetIndex( IntPtr ctx, Variable obj, Variable idx, out Variable outvar, bool isprop )
		{ Variable v; bool ret =_GetIndex( ctx, obj, idx, &v, isprop ? 1 : 0 ) != 0; outvar = v; return ret; }

		[DllImport( "sgscript.dll", EntryPoint = "sgs_SetIndex", CallingConvention = CallingConvention.Cdecl )]
		public static extern int SetIndex( IntPtr ctx, Variable obj, Variable idx, Variable val, int isprop );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_PushIndex", CallingConvention = CallingConvention.Cdecl )]
		public static extern int PushIndex( IntPtr ctx, Variable obj, Variable idx, int isprop );


		[DllImport( "sgscript.dll", EntryPoint = "sgs_PushProperty", CallingConvention = CallingConvention.Cdecl )]
		public static extern int PushProperty( IntPtr ctx, Variable obj, [MarshalAs( UnmanagedType.CustomMarshaler, MarshalTypeRef = typeof(UTF8Marshaler) )] string prop );

		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_GetGlobal", CallingConvention = CallingConvention.Cdecl )]
		public static unsafe extern int _GetGlobal( IntPtr ctx, Variable key, Variable* outvar );
		public static unsafe bool GetGlobal( IntPtr ctx, Variable key, out Variable outvar ){ Variable v; bool ret = _GetGlobal( ctx, key, &v ) != 0; outvar = v; return ret; }

		[DllImport( "sgscript.dll", EntryPoint = "sgs_SetGlobal", CallingConvention = CallingConvention.Cdecl )]
		public static extern int SetGlobal( IntPtr ctx, Variable key, Variable value );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_PushGlobalByName", CallingConvention = CallingConvention.Cdecl )]
		public static extern int PushGlobalByName( IntPtr ctx, [MarshalAs( UnmanagedType.CustomMarshaler, MarshalTypeRef = typeof(UTF8Marshaler) )] string key );
		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_GetGlobalByName", CallingConvention = CallingConvention.Cdecl )]
		public static unsafe extern int _GetGlobalByName( IntPtr ctx, [MarshalAs( UnmanagedType.CustomMarshaler, MarshalTypeRef = typeof(UTF8Marshaler) )] string key, Variable* outvar );
		public static unsafe bool GetGlobalByName( IntPtr ctx, string key, out Variable outvar ){ Variable v; bool ret = _GetGlobalByName( ctx, key, &v ) != 0; outvar = v; return ret; }

		[DllImport( "sgscript.dll", EntryPoint = "sgs_SetGlobalByName", CallingConvention = CallingConvention.Cdecl )]
		public static extern void SetGlobalByName( IntPtr ctx, [MarshalAs( UnmanagedType.CustomMarshaler, MarshalTypeRef = typeof(UTF8Marshaler) )] string key, Variable value );


		[DllImport( "sgscript.dll", EntryPoint = "sgs_Pop", CallingConvention = CallingConvention.Cdecl )]
		public static extern void Pop( IntPtr ctx, Int32 count );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_PopSkip", CallingConvention = CallingConvention.Cdecl )]
		public static extern void PopSkip( IntPtr ctx, Int32 count, Int32 skip );


		[DllImport( "sgscript.dll", EntryPoint = "sgs_StackSize", CallingConvention = CallingConvention.Cdecl )]
		public static extern Int32 StackSize( IntPtr ctx );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_SetStackSize", CallingConvention = CallingConvention.Cdecl )]
		public static extern void SetStackSize( IntPtr ctx, Int32 size );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_SetDeltaSize", CallingConvention = CallingConvention.Cdecl )]
		public static extern void SetDeltaSize( IntPtr ctx, Int32 diff );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_AbsIndex", CallingConvention = CallingConvention.Cdecl )]
		public static extern Int32 AbsIndex( IntPtr ctx, Int32 item );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_IsValidIndex", CallingConvention = CallingConvention.Cdecl )]
		public static extern int IsValidIndex( IntPtr ctx, Int32 item );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_OptStackItem", CallingConvention = CallingConvention.Cdecl )]
		public static extern Variable OptStackItem( IntPtr ctx, Int32 item );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_StackItem", CallingConvention = CallingConvention.Cdecl )]
		public static extern Variable StackItem( IntPtr ctx, Int32 item );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_ItemType", CallingConvention = CallingConvention.Cdecl )]
		public static extern VarType ItemType( IntPtr ctx, Int32 item );


		[DllImport( "sgscript.dll", EntryPoint = "sgs_XFCall", CallingConvention = CallingConvention.Cdecl )]
		public static extern int XFCall( IntPtr ctx, int args, int gotthis );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_GCExecute", CallingConvention = CallingConvention.Cdecl )]
		public static extern void GCExecute( IntPtr ctx );

		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_Method", CallingConvention = CallingConvention.Cdecl )]
		public static extern int Method( IntPtr ctx );
		[DllImport( "sgscript.dll", EntryPoint = "sgs_HideThis", CallingConvention = CallingConvention.Cdecl )]
		public static extern int HideThis( IntPtr ctx );
		[DllImport( "sgscript.dll", EntryPoint = "sgs_ForceHideThis", CallingConvention = CallingConvention.Cdecl )]
		public static extern int ForceHideThis( IntPtr ctx );
		[DllImport( "sgscript.dll", EntryPoint = "sgs_ObjectArg", CallingConvention = CallingConvention.Cdecl )]
		public static extern int ObjectArg( IntPtr ctx );


		[DllImport( "sgscript.dll", EntryPoint = "sgs_Acquire", CallingConvention = CallingConvention.Cdecl )]
		public static unsafe extern void _Acquire( IntPtr ctx, Variable* item );
		public static unsafe void Acquire( IntPtr ctx, Variable item ){ _Acquire( ctx, &item ); }

		[DllImport( "sgscript.dll", EntryPoint = "sgs_Release", CallingConvention = CallingConvention.Cdecl )]
		public static unsafe extern void _Release( IntPtr ctx, Variable* item );
		public static unsafe void Release( IntPtr ctx, ref Variable item )
		{
			Variable v = item;
			_Release( ctx, &v );
			item = v;
		}

		[DllImport( "sgscript.dll", EntryPoint = "sgs_GCMark", CallingConvention = CallingConvention.Cdecl )]
		public static unsafe extern void _GCMark( IntPtr ctx, Variable* item );
		public static unsafe void GCMark( IntPtr ctx, Variable item ){ _GCMark( ctx, &item ); }

		[DllImport( "sgscript.dll", EntryPoint = "sgs_ObjAcquire", CallingConvention = CallingConvention.Cdecl )]
		public static extern void ObjAcquire( IntPtr ctx, IntPtr obj );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_ObjRelease", CallingConvention = CallingConvention.Cdecl )]
		public static extern void ObjRelease( IntPtr ctx, IntPtr obj );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_ObjGCMark", CallingConvention = CallingConvention.Cdecl )]
		public static extern void ObjGCMark( IntPtr ctx, IntPtr obj );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_ObjSetMetaObj", CallingConvention = CallingConvention.Cdecl )]
		public static extern void ObjSetMetaObj( IntPtr ctx, IntPtr obj, IntPtr metaobj );

		[DllImport( "sgscript.dll", EntryPoint = "sgs_ObjGetMetaObj", CallingConvention = CallingConvention.Cdecl )]
		public static extern IntPtr ObjGetMetaObj( IntPtr obj );

		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_GetBoolP", CallingConvention = CallingConvention.Cdecl )]
		public static unsafe extern int _GetBoolP( IntPtr ctx, Variable* item );
		public static unsafe bool GetBoolP( IntPtr ctx, Variable item ){ return _GetBoolP( ctx, &item ) != 0; }
		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_GetIntP", CallingConvention = CallingConvention.Cdecl )]
		public static unsafe extern Int64 _GetIntP( IntPtr ctx, Variable* item );
		public static unsafe Int64 GetIntP( IntPtr ctx, Variable item ){ return _GetIntP( ctx, &item ); }
		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_GetRealP", CallingConvention = CallingConvention.Cdecl )]
		public static unsafe extern double _GetRealP( IntPtr ctx, Variable* item );
		public static unsafe double GetRealP( IntPtr ctx, Variable item ){ return _GetRealP( ctx, &item ); }
		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_GetPtrP", CallingConvention = CallingConvention.Cdecl )]
		public static unsafe extern IntPtr _GetPtrP( IntPtr ctx, Variable* item );
		public static unsafe IntPtr GetPtrP( IntPtr ctx, Variable item ){ return _GetPtrP( ctx, &item ); }
		
		[DllImport( "sgscript.dll", EntryPoint = "sgs_ToStringBufP", CallingConvention = CallingConvention.Cdecl )]
		public static unsafe extern IntPtr _ToStringBufP( IntPtr ctx, Variable* item, Int32* outsize );
		public static unsafe IntPtr ToStringBufP( IntPtr ctx, ref Variable item )
		{
			Int32 sz = 0;
			Variable v = item;
			IntPtr p = _ToStringBufP( ctx, &v, &sz );
			item = v;
			return p;
		}
		public static unsafe IntPtr ToStringBufP( IntPtr ctx, ref Variable item, out Int32 size )
		{
			Int32 sz = 0;
			Variable v = item;
			IntPtr p = _ToStringBufP( ctx, &v, &sz );
			item = v;
			size = sz;
			return p;
		}
		
		public static string GetString( Variable var )
		{
			if( var.type == VarType.Null )
				return null;
			else if( var.type == VarType.String )
			{
				IntPtr s = var.data.S;
				Int32 size = Marshal.ReadInt32( s, 4 );
				byte[] bytes = new byte[ size ];
				Marshal.Copy( (IntPtr) ( s.ToInt64() + 12 ), bytes, 0, size );
				return Encoding.UTF8.GetString( bytes );
			}
			else throw new SGSException( EINVAL, string.Format( "GetString expected 'string' or 'null' type, got '{0}'", var.type ) );
		}
	}
}
