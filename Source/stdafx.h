#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN  
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <Shlwapi.h>
#include <commdlg.h>

#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#include <vector>
#include <stack>

#include "d3d12.h"
#include "d3dx12.h"
#include <dxgi1_5.h>
#include <dxgidebug.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <wrl/client.h>

#include <random>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <locale>
#include <codecvt>
#include <filesystem>

#include <fstream>
#include <sstream>

#define _USE_MATH_DEFINES
#include <math.h>

#include "PointerTypes.h"

#define ARRAY_LENGTH( arr ) std::size( arr )

#define DCRT_STRINGIFY_MACRO_VALUE( s ) DCRT_STRINGIFY_MACRO( s )
#define DCRT_STRINGIFY_MACRO( s ) #s

using Microsoft::WRL::ComPtr;

#include "pix3"