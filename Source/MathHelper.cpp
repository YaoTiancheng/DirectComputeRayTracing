#include "stdafx.h"
#include "MathHelper.h"

using namespace DirectX;

const XMFLOAT4X4 MathHelper::s_IdentityMatrix4x4 = XMFLOAT4X4( 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f );
const XMFLOAT4X3 MathHelper::s_IdentityMatrix4x3 = XMFLOAT4X3( 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f );

XMFLOAT3 MathHelper::MatrixRotationToRollPitchYall( const XMFLOAT3X3& matrix )
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

XMFLOAT3 MathHelper::MatrixRotationToRollPitchYall( const XMFLOAT4X4& matrix )
{
    return MatrixRotationToRollPitchYall( XMFLOAT3X3( matrix._11, matrix._12, matrix._13, matrix._21, matrix._22, matrix._23, matrix._31, matrix._32, matrix._33 ) );
}

void MathHelper::MatrixDecompose( const DirectX::XMFLOAT4X4& matrix, DirectX::XMFLOAT3* outPosition, DirectX::XMFLOAT3* outRollPitchYall, DirectX::XMFLOAT3* scale )
{
    *outPosition = XMFLOAT3( matrix._41, matrix._42, matrix._43 );

    float sx = std::sqrt( matrix._11 * matrix._11 + matrix._12 * matrix._12 + matrix._13 * matrix._13 );
    float sy = std::sqrt( matrix._21 * matrix._21 + matrix._22 * matrix._22 + matrix._23 * matrix._23 );
    float sz = std::sqrt( matrix._31 * matrix._31 + matrix._32 * matrix._32 + matrix._33 * matrix._33 );
    *scale = XMFLOAT3( sx, sy, sz );

    sx = 1.0f / sx;
    sy = 1.0f / sy;
    sz = 1.0f / sz;

    XMFLOAT3X3 rotationMatrix(
        matrix._11 * sx, matrix._12 * sx, matrix._13 * sx,
        matrix._21 * sy, matrix._22 * sy, matrix._23 * sy,
        matrix._31 * sz, matrix._32 * sz, matrix._33 * sz
    );
    *outRollPitchYall = MatrixRotationToRollPitchYall( rotationMatrix );
}
