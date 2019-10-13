#pragma once

class Camera
{
public:
    Camera();

    bool OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam );

    void Update();

    bool IsDirty() const { return m_IsDirty; }

    void GetTransformMatrix( DirectX::XMFLOAT4X4* m ) const;

    void GetTransformMatrixAndClearDirty( DirectX::XMFLOAT4X4* m );

private:
    DirectX::XMFLOAT4 m_Position;
    DirectX::XMFLOAT4 m_EulerAngles;
    DirectX::XMFLOAT4 m_Velocity;
    DirectX::XMFLOAT2 m_LastPointerPosition;

    bool m_IsDirty;
};