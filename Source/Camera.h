#pragma once

class Camera
{
public:
    Camera();

    bool OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam );

    void Update( float deltaTime );

    bool IsDirty() const { return m_IsDirty; }

    void SetDirty() { m_IsDirty = true; }

    void ClearDirty() { m_IsDirty = false; }

    void GetTransformMatrix( DirectX::XMFLOAT4X4* m ) const;

    void GetTransformMatrixAndClearDirty( DirectX::XMFLOAT4X4* m );

    void OnImGUI();

    void SetPositionAndEulerAngles( const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& eulerAngles )
    {
        m_Position = { position.x, position.y, position.z, 1.0f };
        m_EulerAngles.x = eulerAngles.x;
        m_EulerAngles.y = eulerAngles.y;
        m_EulerAngles.z = eulerAngles.z;
    }

private:
    DirectX::XMFLOAT4 m_Position;
    DirectX::XMFLOAT4 m_EulerAngles;
    DirectX::XMFLOAT4 m_Velocity;
    DirectX::XMFLOAT2 m_LastPointerPosition;
    float             m_Speed;

    bool m_IsDirty;
};