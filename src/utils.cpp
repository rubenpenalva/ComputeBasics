#include "utils.h"

#include <cassert>
#include <fstream>
#include <algorithm>

#ifdef max
#undef max
#endif

void Utils::AssertIfFailed(HRESULT hr)
{
#if NDEBUG
    hr;
#endif
    assert(SUCCEEDED(hr));
}

void Utils::AssertIfFailed(DWORD d, DWORD failValue)
{
#if NDEBUG
    d;
    failValue;
#endif
    assert(d != failValue);
}

size_t Utils::AlignToPowerof2(size_t value, size_t alignmentPower2)
{
    return (value + (alignmentPower2 - 1)) & ~(alignmentPower2 - 1);
}

bool Utils::IsAlignedToPowerof2(size_t value, size_t alignmentPower2)
{
    return (value & (alignmentPower2 - 1)) == 0;
}

// TODO check that it actually does a copy elission optimization
std::vector<char> Utils::ReadFullFile(const std::wstring& fileName, bool readAsBinary)
{
    int mode = readAsBinary ? std::ios::binary : 0;
    std::fstream file(fileName.c_str(), std::ios::in | mode);
    if (!file.is_open())
        return {};

    const auto fileStart = file.tellg();
    file.ignore(std::numeric_limits<std::streamsize>::max());
    const auto fileSize = file.gcount();
    file.seekg(fileStart);
    std::vector<char> buffer(readAsBinary ? fileSize : fileSize + 1);
    file.read(&buffer[0], fileSize);
    if (!readAsBinary)
        buffer[fileSize] = '\0';
    return buffer;
}