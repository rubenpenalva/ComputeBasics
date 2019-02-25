#pragma once

#include <winerror.h>
#include <wtypes.h>

namespace Utils
{

void AssertIfFailed(HRESULT hr);

void AssertIfFailed(DWORD d, DWORD failValue);

}
