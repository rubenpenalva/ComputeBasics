#include "utils.h"

#include <cassert>

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
