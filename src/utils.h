#pragma once

#include <winerror.h>
#include <wtypes.h>
#include <vector>
#include <memory>
#include <d3d12.h>

namespace Utils
{

// Assuming rgba channels
struct TexRawData
{
    float* m_data;
    int m_width;
    int m_height;

    TexRawData(float* data, int width, int height) : m_data(data), m_width(width), m_height(height){}
    ~TexRawData()
    {
        free(m_data);
    }
    TexRawData(const TexRawData&) = delete;
    TexRawData(TexRawData&&) = delete;
    TexRawData& operator=(const TexRawData&) = delete;
    TexRawData& operator=(TexRawData&&) = delete;
};
using TexRawDataPtr = std::unique_ptr<TexRawData>;

void AssertIfFailed(HRESULT hr);

void AssertIfFailed(DWORD d, DWORD failValue);

size_t AlignToPowerof2(size_t value, size_t alignmentPower2);

bool IsAlignedToPowerof2(size_t value, size_t alignmentPower2);

std::vector<char> ReadFullFile(const std::wstring& fileName, bool readAsBinary = false);

TexRawDataPtr ReadTexRawDataFromFile(const std::wstring& fileName);

bool WriteTexRawDataToFile(const std::wstring& fileName, const TexRawData* texRawData);

bool CheckFormatSupport(ID3D12Device* device, DXGI_FORMAT format, D3D12_FORMAT_SUPPORT1 inputFormatSupport1);

bool CheckFormatSupport(ID3D12Device* device, DXGI_FORMAT format, D3D12_FORMAT_SUPPORT2 inputFormatSupport);

}
