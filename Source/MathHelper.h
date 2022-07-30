#pragma once

namespace MathHelper
{
    extern const DirectX::XMFLOAT4X4 s_IdentityMatrix;

    DirectX::XMFLOAT3 MatrixRotationToRollPitchYall( const DirectX::XMFLOAT3X3& matrix );

    DirectX::XMFLOAT3 MatrixRotationToRollPitchYall( const DirectX::XMFLOAT4X4& matrix );

    void MatrixDecompose( const DirectX::XMFLOAT4X4& matrix, DirectX::XMFLOAT3* outPosition, DirectX::XMFLOAT3* outRollPitchYall, DirectX::XMFLOAT3* scale );
}