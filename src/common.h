#pragma once

#include <wrl.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <cstdint>
#include <string>
#include <cassert>

#define ENABLE_D3D12_DEBUG_LAYER            ( 1 )
#define ENABLE_D3D12_DEBUG_GPU_VALIDATION   ( 1 )
#define ENABLE_PIX_CAPTURE                  ( 1 )
#define ENABLE_RGA_COMPATIBILITY            ( 1 )

#if ENABLE_PIX_CAPTURE
#include <DXProgrammableCapture.h>
#endif

// Note actually comptr is not a smart tr but a raii class using
// IUnknown AddRef and Release functions
using IDXGIAdapter1ComPtr = Microsoft::WRL::ComPtr<IDXGIAdapter1>;
using IDXGIFactory6ComPtr = Microsoft::WRL::ComPtr<IDXGIFactory6>;
using ID3D12DeviceComPtr = Microsoft::WRL::ComPtr<ID3D12Device>;
using ID3D12CommandQueueComPtr = Microsoft::WRL::ComPtr<ID3D12CommandQueue>;
using ID3D12ResourceComPtr = Microsoft::WRL::ComPtr<ID3D12Resource>;
using ID3D12GraphicsCommandListComPtr = Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>;
using ID3D12CommandAllocatorComPtr = Microsoft::WRL::ComPtr<ID3D12CommandAllocator>;
using ID3D12DescriptorHeapComPtr = Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>;
using ID3D12PipelineStateComPtr = Microsoft::WRL::ComPtr<ID3D12PipelineState>;
using ID3DBlobComPtr = Microsoft::WRL::ComPtr<ID3DBlob>;
using ID3D12RootSignatureComPtr = Microsoft::WRL::ComPtr<ID3D12RootSignature>;
#if ENABLE_PIX_CAPTURE
using IDXGraphicsAnalysisComPtr = Microsoft::WRL::ComPtr<IDXGraphicsAnalysis>;
#endif
using ID3D12QueryHeapComPtr = Microsoft::WRL::ComPtr<ID3D12QueryHeap>;