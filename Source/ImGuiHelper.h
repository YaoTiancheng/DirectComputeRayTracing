#pragma once

#include "imgui/imgui.h"

namespace ImGuiHelper
{
    bool DragFloat3RadianInDegree( const char* label, float v[3], float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0 );
}