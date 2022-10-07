#pragma once

#define _ONLY_ONE
//#define _MANY_MODEL

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.Input.Spatial.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Perception.People.h>
#include <winrt/Windows.Perception.Spatial.h>
#include <winrt/Windows.Perception.Spatial.Preview.h>
#include <winrt/Windows.Perception.Spatial.Surfaces.h>
//#include <winrt/Microsoft.MixedReality.QR.h>
//#include <winrt/Microsoft.MixedReality.h>

#include <winrt/Windows.Graphics.Holographic.h>

#include <wrl/client.h>

#include <stack>
#include <mutex>
#include <iostream>
#include <set>

#include <robuffer.h>

#include <windows.graphics.directx.direct3d11.interop.h>

#include <d3d11.h>
#include <d3d11_2.h>
#include <d3d11_4.h>
#include <D3Dcompiler.h>
#include <d3d11shader.h>
#include <d2d1_2.h>
#include <dwrite_2.h>
#include <dxgi1_3.h>
#include <dxgi1_4.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")

#include <algorithm>
#include <set>
#include <mutex>
#include <string>
#include <map>
#include <memory>
#include <iostream>
#include <thread>
#include <vector>