#ifndef _UINT64_H_
#define _UINT64_H_

uint2 UInt64_Add( uint2 a, uint2 b )
{
    uint sumLo = a.x + b.x;
    uint sumHi = a.y + b.y;
    uint xor = a.x ^ b.x;
    uint carry = ( ( xor & ( ~sumLo ) ) | ( a.x & b.x ) ) >> 31;
    sumHi += carry;
    return uint2( sumLo, sumHi );
}

uint2 UInt64_ShiftRight( uint2 v, uint n )
{
    uint hi = v.y >> n;
    uint lo = ( v.x >> n ) | ( v.y << ( 32 - n ) );
    return uint2( lo, hi );
}

uint2 UInt64_ShiftLeft( uint2 v, uint n )
{
    uint hi = ( v.y << n ) | ( v.x >> ( 32 - n ) );
    uint lo = v.x << n;
    return uint2( lo, hi );
}

uint2 UInt_Multiply32To64( uint a, uint b )
{
    uint a0 = a & 0xFFFF, a1 = a >> 16;
    uint b0 = b & 0xFFFF, b1 = b >> 16;
    uint p11 = a1 * b1, p01 = a0 * b1;
    uint p10 = a1 * b0, p00 = a0 * b0;

    uint middle = p10 + ( p00 >> 16 ) + ( p01 & 0xFFFF );
    uint hi = p11 + ( middle >> 16 ) + ( p01 >> 16 );
    uint lo = ( middle << 16 ) | ( p00 & 0xFFFF );
    return uint2( lo, hi );
}

uint2 UInt64_Multiply( uint2 a, uint2 b )
{
    uint2 mul = UInt_Multiply32To64( a.x, b.x );
    mul.y += a.y * b.x + a.x * b.y;
    return mul;
}

#endif