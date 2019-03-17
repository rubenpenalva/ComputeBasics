#pragma once

#include <winerror.h>
#include <wtypes.h>
#include <vector>

namespace Utils
{

void AssertIfFailed(HRESULT hr);

void AssertIfFailed(DWORD d, DWORD failValue);

size_t AlignToPowerof2(size_t value, size_t alignmentPower2);

bool IsAlignedToPowerof2(size_t value, size_t alignmentPower2);

std::vector<char> ReadFullFile(const std::wstring& fileName, bool readAsBinary = false);

}
