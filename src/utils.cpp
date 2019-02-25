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