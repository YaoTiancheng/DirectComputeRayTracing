#include "stdafx.h"
#include "MathHelper.h"

using namespace DirectX;

const XMFLOAT4X4 MathHelper::s_IdentityMatrix = XMFLOAT4X4( 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f );

XMFLOAT3 MathHelper::MatrixRotationToRollPitchYall( const XMFLOAT4X4& matrix )
{
    float cy = sqrtf( matrix._33 * matrix._33 + matrix._31 * matrix._31 );
    XMFLOAT3 result;
    result.x = atan2f( -matrix._32, cy );
    if ( cy > 16.f * FLT_EPSILON )
    {
        result.y = atan2f( matrix._31, matrix._33 );
        result.z = atan2f( matrix._12, matrix._22 );
    }
    else
    {
        result.y = 0.f;
        result.z = atan2f( -matrix._21, matrix._11 );
    }
    return result;
}
