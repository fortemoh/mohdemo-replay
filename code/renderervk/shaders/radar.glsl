// radar.glsl -- a Competitive HUD radar floor's alphas for a height (radar.comp), as the UI's own picture has them
// (RM_Paint in cl_uicomp.cpp), byte for byte. Every operation the CPU's: each rounded on its own (x86-64's SSE has no
// fused multiply-add, and the builds don't ask for one; precise keeps the compiler from fusing or reordering), the
// division by 56 rounded to nearest as the CPU's, where Vulkan's may be 2.5 ulp off (RM_Div56). Tested over every
// float height distance in [60, 125] by every shade 0-255 against the CPU's code

// x / 56 rounded to nearest, from any division within a few ulp: its quotient's last 3 bits cleared (q then has 21
// bits, q * 56 = q * 7 * 8 at most 24: exact), the remainder x - q * 56 (exact: within a factor 2 of x, Sterbenz),
// its own quotient (some ulp of x / 56 at most, any error in it ~1e-6 ulp of the result) added, rounded once. The
// exact quotient is never nearer than 1/14 ulp to a halfway point between two floats (56 = 7 * 8: x * 2^k / 7 - an
// odd number / 2 is a nonzero multiple of 1/14), so the sum rounds as it would. Needs neither a division nor an fma
// better than the spec's
float RM_Div56( float x )
{
	precise float q0 = x / 56.0;
	precise float q = uintBitsToFloat( floatBitsToUint( q0 ) & 0xFFFFFFF8u );
	precise float r = x - q * 56.0;
	precise float quotient = q + r / 56.0;

	return quotient;
}

// how near a floor at height fz is to height z, 0..1 (1 within 64 units, 0 from 120)
float RM_Focus( float fz, float z )
{
	precise float d = abs( fz - z );
	precise float x = 120.0 - d;

	return clamp( RM_Div56( x ), 0.0, 1.0 );
}

// its stairs' stripe, between its shade (fill: alpha / 255) and 0.22 + 0.3 * focus
uint RM_Stair( float focus, float fill )
{
	precise float v = ( fill + ( 0.22 + 0.3 * focus - fill ) * 0.8 ) * 255.0;

	return uint( v );
}
