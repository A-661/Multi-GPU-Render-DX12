#pragma once

#include "d3dUtil.h"
#include "GCrossAdapterResource.h"
#include "GraphicPSO.h"
#include "GDescriptor.h"
#include "GTexture.h"
#include "ShaderBuffersData.h"

using namespace DirectX::SimpleMath;
using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;
using namespace Utils;

struct alignas(sizeof(Vector4)) FogConstants
{
    Matrix InvView;
    Matrix Proj;
    Matrix InvProj;
    Vector4 Resolution;   // x = width, y = height, z = 1/width, w = 1/height

    Vector4 FogColor;

    Vector3 CameraPosW;
    float Density;

    float HeightFalloff;
    float FogBaseHeight;
    float StartDistance;
    float MaxOpacity;

    float NearZ;
    float FarZ;
    Vector2 Padding;
};

class FogResources final
{
    static constexpr DXGI_FORMAT DepthMapFormat = DXGI_FORMAT_R32_TYPELESS;
    static constexpr DXGI_FORMAT FogMapFormat = DXGI_FORMAT_R16_UNORM;

    std::shared_ptr<GDevice> device;

    std::shared_ptr<GRootSignature> computeRootSignature;
    std::shared_ptr<GRootSignature> compositeRootSignature;

    GTexture depthMap;
    GDescriptor computeDescriptorTable;
    GDescriptor depthMapSRV;

    GTexture fogMap;
    GDescriptor fogMapSRV;
    GDescriptor fogMapRTV;
    GDescriptor fogMapUAV;
    GDescriptor compositeSRVTable;

    ComputePSO fogPSO;
    std::shared_ptr<GraphicPSO> compositePSO;

public:
    void Initialize(const std::shared_ptr<GDevice>& device, const D3D12_INPUT_LAYOUT_DESC& layout);
    void OnResize(UINT width, UINT height);
    const std::shared_ptr<GDevice>& GetDevice() const { return device; }

    const GTexture& GetDepthMap() const { return depthMap; }
    const GTexture& GetFogMap() const { return fogMap; }

    const GDescriptor* GetDepthMapSRV() const { return &depthMapSRV; }
    const GDescriptor* GetFogMapSRV() const { return &fogMapSRV; }
    const GDescriptor* GetFogMapRTV() const { return &fogMapRTV; }
    const GDescriptor* GetFogMapUAV() const { return &fogMapUAV; }
    const GDescriptor* GetComputeDescriptorTable() const { return &computeDescriptorTable; }
    const GDescriptor* GetCompositeSRVTable() const { return &compositeSRVTable; }

    const GRootSignature& GetComputeRootSignature() const { return *computeRootSignature; }
    const GRootSignature& GetCompositeRootSignature() const { return *compositeRootSignature; }

    const ComputePSO& GetFogPSO() const { return fogPSO; }
    const GraphicPSO& GetCompositePSO() const { return *compositePSO; }

private:
    void InitializeComputeRS();
    void InitializeCompositeRS();
    void BuildPSO(const D3D12_INPUT_LAYOUT_DESC& layout);
    void RebuildDescriptors() const;
};

class FogCrossResources final
{
    std::shared_ptr<GCrossAdapterResource> sharedDepthMap;
    std::shared_ptr<GCrossAdapterResource> sharedFogMap;

public:
    void Initialize(const FogResources& resources, const std::shared_ptr<GDevice>& primeDevice,
                    const std::shared_ptr<GDevice>& secondDevice);

    void OnResize(UINT width, UINT height) const;

    const GCrossAdapterResource& GetDepthMap() const { return *sharedDepthMap; }
    const GCrossAdapterResource& GetFogMap() const { return *sharedFogMap; }
};

class SharedFog final
{
    FogResources primeResources;
    FogResources secondResources;
    FogCrossResources crossResources;

    UINT renderTargetWidth = 0;
    UINT renderTargetHeight = 0;

public:
    SharedFog() = default;
    SharedFog(const SharedFog& rhs) = delete;
    SharedFog& operator=(const SharedFog& rhs) = delete;
    ~SharedFog() = default;

    void Initialize(const std::shared_ptr<GDevice>& primeDevice, const std::shared_ptr<GDevice>& secondDevice,
                    const D3D12_INPUT_LAYOUT_DESC& layout, UINT width, UINT height);

    void OnResize(UINT width, UINT height);

    const FogResources& GetPrimeResources() const { return primeResources; }
    const FogResources& GetSecondResources() const { return secondResources; }
    const FogCrossResources& GetCrossResources() const { return crossResources; }

    void Compute(const std::shared_ptr<GCommandList>& cmdList,
                 const std::shared_ptr<ConstantUploadBuffer<FogConstants>>& constants,
                 const FogResources& resources) const;

    void Composite(const std::shared_ptr<GCommandList>& cmdList,
                   const std::shared_ptr<ConstantUploadBuffer<FogConstants>>& constants,
                   const GTexture& sceneColorTexture,
                   const GTexture& outputRenderTarget,
                   const GDescriptor* outputRTV, UINT outputRTVOffset) const;
};
