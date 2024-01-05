#pragma once

namespace MathHelper
{
    extern const DirectX::XMFLOAT4X4 s_IdentityMatrix4x4;
    extern const DirectX::XMFLOAT4X3 s_IdentityMatrix4x3;

    DirectX::XMFLOAT3 MatrixRotationToRollPitchYall( const DirectX::XMFLOAT3X3& matrix );

    DirectX::XMFLOAT3 MatrixRotationToRollPitchYall( const DirectX::XMFLOAT4X4& matrix );

    void MatrixDecompose( const DirectX::XMFLOAT4X4& matrix, DirectX::XMFLOAT3* outPosition, DirectX::XMFLOAT3* outRollPitchYall, DirectX::XMFLOAT3* scale );

    // Divides two integers and rounds up
    template <typename T>
    T DivideAndRoundUp( T Dividend, T Divisor )
    {
        return ( Dividend + Divisor - 1 ) / Divisor;
    }
}