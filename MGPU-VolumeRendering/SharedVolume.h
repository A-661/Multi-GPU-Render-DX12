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

class GModel;


class VolumeResources final
{
    static constexpr DXGI_FORMAT DepthMapFormat =
        DXGI_FORMAT_R32_TYPELESS;

    static constexpr DXGI_FORMAT VolumeMapFormat =
        DXGI_FORMAT_R16G16B16A16_FLOAT;

    std::shared_ptr<GDevice> device;

    std::shared_ptr<GRootSignature> volumeRootSignature;
    std::shared_ptr<GRootSignature> compositeRootSignature;

    GTexture depthMap;
    GDescriptor depthMapSRV;

    GTexture volumeMap;
    GDescriptor volumeMapSRV;
    GDescriptor volumeMapRTV;

    std::shared_ptr<GraphicPSO> volumePSO;
    std::shared_ptr<GraphicPSO> compositePSO;

    std::shared_ptr<GModel> volumeModel;

public:
    void Initialize(
        const std::shared_ptr<GDevice>& device,
        const D3D12_INPUT_LAYOUT_DESC& layout,
        const std::shared_ptr<GModel>& model = nullptr);

    void OnResize(
        UINT width,
        UINT height);

    const std::shared_ptr<GDevice>& GetDevice() const
    {
        return device;
    }

    const GTexture& GetDepthMap() const
    {
        return depthMap;
    }

    const GDescriptor* GetDepthMapSRV() const
    {
        return &depthMapSRV;
    }

    const GTexture& GetVolumeMap() const
    {
        return volumeMap;
    }

    const GDescriptor* GetVolumeMapSRV() const
    {
        return &volumeMapSRV;
    }

    const GDescriptor* GetVolumeMapRTV() const
    {
        return &volumeMapRTV;
    }

    const GRootSignature& GetVolumeRootSignature() const
    {
        return *volumeRootSignature;
    }

    const GRootSignature& GetCompositeRootSignature() const
    {
        return *compositeRootSignature;
    }

    const GraphicPSO& GetVolumePSO() const
    {
        return *volumePSO;
    }

    const GraphicPSO& GetCompositePSO() const
    {
        return *compositePSO;
    }

    const std::shared_ptr<GModel>& GetVolumeModel() const
    {
        return volumeModel;
    }

private:
    void InitializeVolumeRS();
    void InitializeCompositeRS();

    void BuildPSO(
        const D3D12_INPUT_LAYOUT_DESC& layout);

    void RebuildDescriptors() const;
};


class VolumeCrossResources final
{
    std::shared_ptr<GCrossAdapterResource> sharedDepthMap;
    std::shared_ptr<GCrossAdapterResource> sharedVolumeMap;

public:
    void Initialize(
        const VolumeResources& resources,
        const std::shared_ptr<GDevice>& primeDevice,
        const std::shared_ptr<GDevice>& secondDevice);

    void OnResize(
        UINT width,
        UINT height) const;

    const GCrossAdapterResource& GetDepthMap() const
    {
        return *sharedDepthMap;
    }

    const GCrossAdapterResource& GetVolumeMap() const
    {
        return *sharedVolumeMap;
    }
};


class SharedVolume final
{
    VolumeResources primeResources;
    VolumeResources secondResources;

    VolumeCrossResources crossResources;

    UINT renderTargetWidth = 0;
    UINT renderTargetHeight = 0;

public:
    SharedVolume() = default;

    SharedVolume(const SharedVolume&) = delete;
    SharedVolume& operator=(const SharedVolume&) = delete;

    ~SharedVolume() = default;


    void Initialize(
        const std::shared_ptr<GDevice>& primeDevice,
        const std::shared_ptr<GDevice>& secondDevice,
        const D3D12_INPUT_LAYOUT_DESC& layout,
        const std::shared_ptr<GModel>& primeVolumeModel,
        UINT width,
        UINT height);


    void OnResize(
        UINT width,
        UINT height);


    const VolumeResources& GetPrimeResources() const
    {
        return primeResources;
    }

    const VolumeResources& GetSecondResources() const
    {
        return secondResources;
    }

    const VolumeCrossResources& GetCrossResources() const
    {
        return crossResources;
    }


    void Render(
        const std::shared_ptr<GCommandList>& cmdList,
        const std::shared_ptr<ConstantUploadBuffer<ObjectConstants>>& objectConstants,
        const std::shared_ptr<ConstantUploadBuffer<PassConstants>>& passConstants,
        const std::shared_ptr<ConstantUploadBuffer<VolumeConstants>>& volumeConstants,
        const VolumeResources& resources) const;


    void Composite(
        const std::shared_ptr<GCommandList>& cmdList,
        const GTexture& outputRenderTarget,
        const GDescriptor* outputRTV,
        UINT outputRTVOffset = 0) const;
};