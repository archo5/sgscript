

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
};

