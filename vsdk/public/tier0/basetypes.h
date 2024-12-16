//========= Copyright 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BASETYPES_H
#define BASETYPES_H
#pragma once

//SDR_PUBLIC #include "commonmacros.h"
//SDR_PUBLIC #include "wchartypes.h"

#include "tier0/platform.h"
#include <cmath>
#include <cstdlib>

typedef unsigned char byte;
typedef unsigned short word;
typedef float vec_t;

#define ALIGN4_POST DECL_ALIGN(4)
#define FLOAT32_NAN_BITS     (uint32)0x7FC00000	// not a number!
#define FLOAT32_NAN          BitsToFloat( FLOAT32_NAN_BITS )
#define DECL_ALIGN(x)			__declspec( align( x ) )
#define ALIGN4 DECL_ALIGN(4)
#define ALIGN8 DECL_ALIGN(8)
#define ALIGN16 DECL_ALIGN(16)
#define ALIGN32 DECL_ALIGN(32)
#define ALIGN128 DECL_ALIGN(128)
#define VEC_T_NAN FLOAT32_NAN

#define fsel(c,x,y) ( (c) >= 0 ? (x) : (y) )

// integer conditional move
// if a >= 0, return x, else y
#define isel(a,x,y) ( ((a) >= 0) ? (x) : (y) )

// if x = y, return a, else b
#define ieqsel(x,y,a,b) (( (x) == (y) ) ? (a) : (b))

// if the nth bit of a is set (counting with 0 = LSB),
// return x, else y
// this is fast if nbit is a compile-time immediate 
#define ibitsel(a, nbit, x, y) ( ( ((a) & (1 << (nbit))) != 0 ) ? (x) : (y) )

// Pad a number so it lies on an N byte boundary.
// So PAD_NUMBER(0,4) is 0 and PAD_NUMBER(1,4) is 4
#define PAD_NUMBER(number, boundary) \
	( ((number) + ((boundary)-1)) / (boundary) ) * (boundary)


FORCEINLINE double fpmin(double a, double b)
{
	return a > b ? b : a;
}

FORCEINLINE double fpmax(double a, double b)
{
	return a >= b ? a : b;
}

// clamp x to lie inside [a,b]. Assumes b>a
FORCEINLINE float fclamp(float x, float a, float b)
{
	return fpmin(fpmax(x, a), b);
}
// clamp x to lie inside [a,b]. Assumes b>a
FORCEINLINE double fclamp(double x, double a, double b)
{
	return fpmin(fpmax(x, a), b);
}
inline float FloatMakePositive(vec_t f)
{
	return fabsf(f);
}
inline unsigned long& FloatBits(vec_t& f)
{
	return *reinterpret_cast<unsigned long*>(&f);
}

inline unsigned long const& FloatBits(vec_t const& f)
{
	return *reinterpret_cast<unsigned long const*>(&f);
}

inline vec_t BitsToFloat(unsigned long i)
{
	return *reinterpret_cast<vec_t*>(&i);
}
inline bool IsFinite(const vec_t& f)
{
#ifdef _GAMECONSOLE
	return f == f && fabs(f) <= FLT_MAX;
#else
	return ((FloatBits(f) & 0x7F800000) != 0x7F800000);
#endif
}
// This is the preferred Min operator. Using the MIN macro can lead to unexpected
// side-effects or more expensive code.
template< class T >
static FORCEINLINE T const & Min( T const &val1, T const &val2 )
{
	return val1 < val2 ? val1 : val2;
}

// This is the preferred Max operator. Using the MAX macro can lead to unexpected
// side-effects or more expensive code.
template< class T >
static FORCEINLINE T const & Max( T const &val1, T const &val2 )
{
	return val1 > val2 ? val1 : val2;
}

template< class T >
static FORCEINLINE T const & Clamp( T const &val, T const &minVal, T const &maxVal )
{
	if ( val < minVal )
		return minVal;
	else if ( val > maxVal )
		return maxVal;
	else
		return val;
}

#undef min
#undef max
#undef MIN
#undef MAX
#define MIN( a, b )				( ( ( a ) < ( b ) ) ? ( a ) : ( b ) )
#define MAX( a, b )				( ( ( a ) > ( b ) ) ? ( a ) : ( b ) )


namespace basetypes
{
	template <class T>
	inline bool IsPowerOf2( T n )
	{
		return n > 0 && (n & (n - 1)) == 0;
	}

	template <class T1, class T2>
	inline T2 ModPowerOf2( T1 a, T2 b )
	{
		return T2( a ) & (b - 1);
	}

	template <class T>
	inline T RoundDownToMultipleOf( T n, T m )
	{
		return n - (IsPowerOf2( m ) ? ModPowerOf2( n, m ) : (n%m));
	}

	template <class T>
	inline T RoundUpToMultipleOf( T n, T m )
	{
		if ( !n )
		{
			return m;
		}
		else
		{
			return RoundDownToMultipleOf( n + m - 1, m );
		}
	}
}
//SDR_PUBLIC using namespace basetypes;

//-----------------------------------------------------------------------------
// integer bitscan operations
//-----------------------------------------------------------------------------
#if defined(_MSC_VER) && ( defined(_M_IX86) || defined(_M_X64) )
extern "C" unsigned char _BitScanReverse(unsigned long* Index, unsigned long Mask);
extern "C" unsigned char _BitScanForward(unsigned long* Index, unsigned long Mask);
#pragma intrinsic(_BitScanReverse)
#pragma intrinsic(_BitScanForward)
#if defined(_M_X64)
extern "C" unsigned char _BitScanReverse64(unsigned long* Index, unsigned __int64 Mask);
extern "C" unsigned char _BitScanForward64(unsigned long* Index, unsigned __int64 Mask);
#pragma intrinsic(_BitScanReverse64)
#pragma intrinsic(_BitScanForward64)
#endif
#endif

inline int FindMostSignificantBit( uint32 n )
{
#if defined(_MSC_VER) && ( defined(_M_IX86) || defined(_M_X64) )
	unsigned long b;
	if ( !_BitScanReverse( &b, n ) )
		return -1;
	return (int)b;
#elif defined(__GNUC__)
	if ( !n ) return -1;
	return 31 - (int) __builtin_clz( n );
#else
	int b = -1;
	for ( ; n; ++b, n >>= 1 ) {}
	return b;
#endif
}

inline int FindMostSignificantBit64( uint64 n )
{
#if defined(_MSC_VER) && defined(_M_X64)
	unsigned long b;
	if ( !_BitScanReverse64( &b, n ) )
		return -1;
	return (int)b;
#elif defined(_MSC_VER) && defined(_M_IX86)
	if ( n >> 32 )
		return 32 + FindMostSignificantBit( n >> 32 );
	return FindMostSignificantBit( (uint32) n );
#elif defined(__GNUC__)
	if ( !n ) return -1;
	return 63 - (int) __builtin_clzll( n );
#else
	int b = -1;
	for ( ; n; ++b, n >>= 1 ) {}
	return b;
#endif
}

inline int FindLeastSignificantBit( uint32 n )
{
#if defined(_MSC_VER) && ( defined(_M_IX86) || defined(_M_X64) )
	unsigned long b;
	if ( !_BitScanForward( &b, n ) )
		return -1;
	return (int)b;
#elif defined(__GNUC__)
	if ( !n ) return -1;
	return __builtin_ctz( n );
#else
	// isolate low bit and call FindMSB
	return FindMostSignificantBit( n & (uint32)(-(int32)n) );
#endif
}

inline int FindLeastSignificantBit64( uint64 n )
{
#if defined(_MSC_VER) && defined(_M_X64)
	unsigned long b;
	if ( !_BitScanForward64( &b, n ) )
		return -1;
	return (int)b;
#elif defined(_MSC_VER) && defined(_M_IX86)
	if ( (uint32)n )
		return FindLeastSignificantBit( (uint32)n );
	if ( n >> 32 )
		return 32 + FindLeastSignificantBit( (uint32)(n >> 32) );
	return -1;
#elif defined(__GNUC__)
	if ( !n ) return -1;
	return __builtin_ctzll( n );
#else
	// isolate low bit and call FindMSB
	return FindMostSignificantBit64( n & (uint64)(-(int64)n) );
#endif
}
#endif // BASETYPES_H
