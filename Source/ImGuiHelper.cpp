#include "stdafx.h"
#include "ImGuiHelper.h"
#include <DirectXMath.h>

using namespace DirectX;

bool ImGuiHelper::DragFloat3RadianInDegree( const char* label, float v[3], float v_speed, float v_min, float v_max, const char* format, ImGuiSliderFlags flags )
{
    XMFLOAT3 eulerAnglesDeg;
    eulerAnglesDeg.x = XMConvertToDegrees( v[ 0 ] );
    eulerAnglesDeg.y = XMConvertToDegrees( v[ 1 ] );
    eulerAnglesDeg.z = XMConvertToDegrees( v[ 2 ] );
    if ( ImGui::DragFloat3( label, (float*)&eulerAnglesDeg, v_speed, v_min, v_max, format, flags ) )
    { 
        v[ 0 ] = XMConvertToRadians( eulerAnglesDeg.x );
        v[ 1 ] = XMConvertToRadians( eulerAnglesDeg.y );
        v[ 2 ] = XMConvertToRadians( eulerAnglesDeg.z );
        return true;
    }
    return false;
}
