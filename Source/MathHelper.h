#pragma once

namespace MathHelper
{
    extern const DirectX::XMFLOAT4X4 s_IdentityMatrix;

    DirectX::XMFLOAT3 MatrixRotationToRollPitchYall( const DirectX::XMFLOAT4X4& matrix );
}