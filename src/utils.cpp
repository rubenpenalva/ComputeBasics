#include "utils.h"

#include <cassert>
#include <fstream>
#include <algorithm>

#ifdef max
#undef max
#endif

#define TINYEXR_IMPLEMENTATION
#include "tinyexr/tinyexr.h"

namespace
{
std::string ConvertFromUTF16ToUTF8(const std::wstring& str)
{
    auto outStrLength = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, 0, 0, 0, 0);
    assert(outStrLength);
    std::string outStr(outStrLength, 0);
    auto result = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, &outStr[0], static_cast<int>(outStr.size()), 0, 0);
    result;
    assert(result == outStr.size());

    return outStr;
}
}

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

Utils::TexRawDataPtr Utils::ReadTexRawDataFromFile(const std::wstring& fileName)
{
    std::vector<char> inputData = ReadFullFile(fileName, true);
    if (inputData.empty())
        return nullptr;

    float* outData = nullptr;
    int outWidth = 0;
    int outHeight = 0;
    const char* outError = nullptr;

    int ret = LoadEXRFromMemory(&outData, &outWidth, &outHeight, 
                                reinterpret_cast<unsigned char*>(&inputData[0]), inputData.size(), &outError);
    if (ret != TINYEXR_SUCCESS)
    {
        // TODO do something with the error
        FreeEXRErrorMessage(outError);
        return nullptr;
    }

    return std::make_unique<TexRawData>(outData, outWidth, outHeight);
}

bool Utils::WriteTexRawDataToFile(const std::wstring& fileName, const TexRawData* texRawData)
{
    assert(texRawData);
    auto fileNameStr = ConvertFromUTF16ToUTF8(fileName);

    const char* outError = nullptr;
    const int components = 4; //fixed to rgba when using LoadEXRFromMemory
    const bool saveAsFloat16 = true;
    int ret = SaveEXR(texRawData->m_data, texRawData->m_width, texRawData->m_height, components, 
                      saveAsFloat16, fileNameStr.c_str(), &outError);
    if (ret != TINYEXR_SUCCESS)
    {
        // TODO do something with the error
        FreeEXRErrorMessage(outError);
        return false;
    }

    return true;
}

bool Utils::CheckFormatSupport(ID3D12Device* device, DXGI_FORMAT format, D3D12_FORMAT_SUPPORT1 inputFormatSupport1)
{
    assert(device);

    D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport = { format, D3D12_FORMAT_SUPPORT1_NONE, D3D12_FORMAT_SUPPORT2_NONE };
    Utils::AssertIfFailed(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &formatSupport, sizeof(formatSupport)));

    return formatSupport.Support1 & inputFormatSupport1;
}

// https://docs.microsoft.com/en-us/windows/desktop/direct3d12/typed-unordered-access-view-loads
bool Utils::CheckFormatSupport(ID3D12Device* device, DXGI_FORMAT format, D3D12_FORMAT_SUPPORT2 inputFormatSupport)
{
    assert(device);
    // TODO add check support for other features
    assert(inputFormatSupport == D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE || inputFormatSupport == D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD);
    if (inputFormatSupport != D3D12_FORMAT_SUPPORT2_NONE)
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS featureData;
        Utils::AssertIfFailed(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &featureData, sizeof(featureData)));
        if (!featureData.TypedUAVLoadAdditionalFormats)
            return false;
    }

    D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport = { format, D3D12_FORMAT_SUPPORT1_NONE, D3D12_FORMAT_SUPPORT2_NONE };
    Utils::AssertIfFailed(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &formatSupport, sizeof(formatSupport)));

    return formatSupport.Support2 & inputFormatSupport;
}