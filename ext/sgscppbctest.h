

#include <math.h>

#include "cppbc/sgs_cppbc.h"

struct Vec3
{
	SGS_OBJECT;
	
	Vec3( float _x, float _y, float _z ) : x(_x), y(_y), z(_z){}
	
	float _get_length(){ return (float) sqrtf(x*x+y*y+z*z); }
	
	SGS_PROPERTY float x;
	SGS_PROPERTY float y;
	SGS_PROPERTY float z;
	
	SGS_PROPERTY_FUNC( READ _get_length ) SGS_ALIAS( float length );
	SGS_METHOD float getLength(){ return _get_length(); }
	SGS_METHOD void setLength( float len )
	{
		if( x == 0 && y == 0 && z == 0 )
			x = 1;
		else
		{
			float ol = _get_length();
			len /= ol;
		}
		x *= len;
		y *= len;
		z *= len;
	}
	SGS_IFUNC( CONVERT ) int _convert( SGS_CTX, sgs_VarObj* data, int type )
	{
		Vec3* V = (Vec3*) data->data;
		if( type == SGS_VT_STRING )
		{
			char bfr[ 128 ];
			sprintf( bfr, "Vec3(%g;%g;%g)", V->x, V->y, V->z );
			sgs_PushString( C, bfr );
			return SGS_SUCCESS;
		}
		return SGS_ENOTSUP;
	}
};

