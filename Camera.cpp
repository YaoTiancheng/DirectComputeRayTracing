#include "stdafx.h"
#include "Camera.h"

using namespace DirectX;

Camera::Camera()
    : m_Position( 0.0f, 1.0f, 0.0f, 1.0f )
    , m_EulerAngles( 0.0f, 0.0f, 0.0f, 0.0f )
    , m_Velocity( 0.0f, 0.0f, 0.0f, 0.0f )
    , m_LastPointerPosition( 0.0f, 0.0f )
    , m_IsDirty( true )
{
}

bool Camera::OnWndMessage( UINT message, WPARAM wParam, LPARAM lParam )
{
    switch ( message )
    {
    case WM_MOUSEMOVE:
    {
        XMFLOAT2 pointerPosition;
        pointerPosition.x = ( float ) GET_X_LPARAM( lParam );
        pointerPosition.y = ( float ) GET_Y_LPARAM( lParam );
        XMFLOAT2 deltaPointerPosition = XMFLOAT2( pointerPosition.x - m_LastPointerPosition.x, pointerPosition.y - m_LastPointerPosition.y );
        m_LastPointerPosition = pointerPosition;

        if ( wParam & MK_RBUTTON )
        {
            m_EulerAngles.x += deltaPointerPosition.y * 0.01f;
            m_EulerAngles.y += deltaPointerPosition.x * 0.01f;

            m_IsDirty = true;
        }

        break;
    }
    case WM_KEYDOWN:
    {
        if ( ( lParam & ( 1 << 30 ) ) == 0 )
        {
            if ( wParam == 'W' )
                m_Velocity.z = 0.01f;
            else if ( wParam == 'S' )
                m_Velocity.z = -0.01f;
            else if ( wParam == 'A' )
                m_Velocity.x = -0.01f;
            else if ( wParam == 'D' )
                m_Velocity.x = 0.01f;
        }

        break;
    }
    case WM_KEYUP:
    {
        if ( wParam == 'W' || wParam == 'S' )
            m_Velocity.z = 0.0f;
        else if ( wParam == 'A' || wParam == 'D' )
            m_Velocity.x = 0.0f;

        break;
    }
    default:
        return false;
    }

    return true;
}

void Camera::Update()
{
    if ( m_Velocity.x != 0.0f || m_Velocity.z != 0.0f )
    {
        XMVECTOR vEulerAngles = XMLoadFloat4( &m_EulerAngles );
        XMVECTOR vVelocity = XMLoadFloat4( &m_Velocity );
        XMMATRIX vM = XMMatrixRotationRollPitchYawFromVector( vEulerAngles );
        vVelocity = XMVector4Transform( vVelocity, vM );
        XMVECTOR vPosition = XMLoadFloat4( &m_Position );
        vPosition = XMVectorAdd( vVelocity, vPosition );
        XMStoreFloat4( &m_Position, vPosition );

        m_IsDirty = true;
    }
}

void Camera::GetTransformMatrix( DirectX::XMFLOAT4X4* m ) const
{
    XMVECTOR vEulerAngles = XMLoadFloat4( &m_EulerAngles );
    XMMATRIX vM = XMMatrixRotationRollPitchYawFromVector( vEulerAngles );
    XMStoreFloat4x4( m, vM );

    m->_41 = m_Position.x;
    m->_42 = m_Position.y;
    m->_43 = m_Position.z;
}

void Camera::GetTransformMatrixAndClearDirty( DirectX::XMFLOAT4X4* m )
{
    GetTransformMatrix( m );
    m_IsDirty = false;
}
