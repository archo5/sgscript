using System;

namespace SGScript
{
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
	}
}
