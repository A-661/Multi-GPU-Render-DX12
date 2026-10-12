#include "pch.h"
#include "SharedFog.h"

#include "GCommandList.h"
#include "GDevice.h"

namespace
{
    UINT IntDivRoundUp(const UINT a, const UINT b)
    {
        return (a + b - 1) / b;
    }
}

void FogResources::InitializeComputeRS()
{
    computeRootSignature = std::make_shared<GRootSignature>();

    CD3DX12_DESCRIPTOR_RANGE depthTable;
    depthTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);

    CD3DX12_DESCRIPTOR_RANGE fogTable;
    fogTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

    computeRootSignature->AddConstantBufferParameter(0);
    computeRootSignature->AddDescriptorParameter(&depthTable, 1);
    computeRootSignature->AddDescriptorParameter(&fogTable, 1);

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        0,
        D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        1,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    computeRootSignature->AddStaticSampler(pointClamp);
    computeRootSignature->AddStaticSampler(linearClamp);
    computeRootSignature->Initialize(device);
}

void FogResources::InitializeCompositeRS()
{
    compositeRootSignature = std::make_shared<GRootSignature>();

    CD3DX12_DESCRIPTOR_RANGE compositeTable;
    compositeTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

    compositeRootSignature->AddConstantBufferParameter(0);
    compositeRootSignature->AddDescriptorParameter(&compositeTable, 1, D3D12_SHADER_VISIBILITY_PIXEL);

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        0,
        D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        1,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    compositeRootSignature->AddStaticSampler(pointClamp);
    compositeRootSignature->AddStaticSampler(linearClamp);
    compositeRootSignature->Initialize(device);
}

void FogResources::Initialize(const std::shared_ptr<GDevice>& Device, const D3D12_INPUT_LAYOUT_DESC& layout)
{
    device = Device;

    computeDescriptorTable = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);
    depthMapSRV = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    fogMapUAV = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    fogMapSRV = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    fogMapRTV = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
    compositeSRVTable = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);

    InitializeComputeRS();
    InitializeCompositeRS();
    BuildPSO(layout);
}

void FogResources::OnResize(const UINT width, const UINT height)
{
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    if (!depthMap.IsValid())
    {
        texDesc.Format = DepthMapFormat;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE depthClear = {};
        depthClear.Format = DXGI_FORMAT_D32_FLOAT;
        depthClear.DepthStencil.Depth = 1.0f;
        depthClear.DepthStencil.Stencil = 0;

        depthMap = GTexture(device, texDesc, L"Fog Depth Map " + device->GetName(), TextureUsage::Depth, &depthClear);
    }
    else
    {
        GTexture::Resize(depthMap, width, height, 1);
    }

    if (!fogMap.IsValid())
    {
        texDesc.Format = FogMapFormat;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        const float clearValue[] = {0.0f, 0.0f, 0.0f, 0.0f};
        const auto fogClear = CD3DX12_CLEAR_VALUE(FogMapFormat, clearValue);

        fogMap = GTexture(device, texDesc, L"Fog Map " + device->GetName(), TextureUsage::RenderTarget, &fogClear);
    }
    else
    {
        GTexture::Resize(fogMap, width, height, 1);
    }

    RebuildDescriptors();
}

void FogResources::BuildPSO(const D3D12_INPUT_LAYOUT_DESC& layout)
{
    (void)layout;

    auto computeShader = std::make_unique<GShader>(
        L"Shaders\\ExponentialHeightFog.hlsl",
        ComputeShader,
        nullptr,
        "CSMain",
        "cs_5_1");
    computeShader->LoadAndCompile();

    fogPSO.SetRootSignature(GetComputeRootSignature());
    fogPSO.SetShader(computeShader.get());
    fogPSO.Initialize(device);

    auto compositeVS = std::make_unique<GShader>(
        L"Shaders\\ExponentialHeightFog.hlsl",
        VertexShader,
        nullptr,
        "VSComposite",
        "vs_5_1");
    compositeVS->LoadAndCompile();

    auto compositePS = std::make_unique<GShader>(
        L"Shaders\\ExponentialHeightFog.hlsl",
        PixelShader,
        nullptr,
        "PSComposite",
        "ps_5_1");
    compositePS->LoadAndCompile();

    D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc = {};
    basePsoDesc.InputLayout = {nullptr, 0};
    basePsoDesc.pRootSignature = compositeRootSignature->GetNativeSignature().Get();
    basePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    basePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    basePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    basePsoDesc.DepthStencilState.DepthEnable = false;
    basePsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    basePsoDesc.SampleMask = UINT_MAX;
    basePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    basePsoDesc.NumRenderTargets = 1;
    basePsoDesc.RTVFormats[0] = GetSRGBFormat(BackBufferFormat);
    basePsoDesc.SampleDesc.Count = 1;
    basePsoDesc.SampleDesc.Quality = 0;
    basePsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;

    compositePSO = std::make_shared<GraphicPSO>(RenderMode::Debug);
    compositePSO->SetPsoDesc(basePsoDesc);
    compositePSO->SetRootSignature(GetCompositeRootSignature());
    compositePSO->SetInputLayout({nullptr, 0});
    compositePSO->SetShader(compositeVS.get());
    compositePSO->SetShader(compositePS.get());
    compositePSO->SetRTVFormat(0, GetSRGBFormat(BackBufferFormat));
    compositePSO->SetDSVFormat(DXGI_FORMAT_UNKNOWN);
    compositePSO->SetSampleCount(1);
    compositePSO->SetSampleQuality(0);
    compositePSO->Initialize(device);
}

void FogResources::RebuildDescriptors() const
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    depthMap.CreateShaderResourceView(&srvDesc, &depthMapSRV);
    depthMap.CreateShaderResourceView(&srvDesc, &computeDescriptorTable, 0);

    srvDesc.Format = FogMapFormat;
    fogMap.CreateShaderResourceView(&srvDesc, &fogMapSRV);
    fogMap.CreateShaderResourceView(&srvDesc, &compositeSRVTable, 1);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Format = FogMapFormat;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;
    fogMap.CreateRenderTargetView(&rtvDesc, &fogMapRTV);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = FogMapFormat;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    uavDesc.Texture2D.PlaneSlice = 0;
    fogMap.CreateUnorderedAccessView(&uavDesc, &fogMapUAV);
    fogMap.CreateUnorderedAccessView(&uavDesc, &computeDescriptorTable, 1);
}

void FogCrossResources::Initialize(const FogResources& resources, const std::shared_ptr<GDevice>& primeDevice,
                                   const std::shared_ptr<GDevice>& secondDevice)
{
    sharedDepthMap = std::make_shared<GCrossAdapterResource>(
        resources.GetDepthMap().GetD3D12ResourceDesc(),
        primeDevice,
        secondDevice,
        L"Cross Adapter Fog Depth Map");

    sharedFogMap = std::make_shared<GCrossAdapterResource>(
        resources.GetFogMap().GetD3D12ResourceDesc(),
        primeDevice,
        secondDevice,
        L"Cross Adapter Fog Map");
}

void FogCrossResources::OnResize(const UINT width, const UINT height) const
{
    sharedDepthMap->Resize(width, height);
    sharedFogMap->Resize(width, height);
}

void SharedFog::Initialize(const std::shared_ptr<GDevice>& primeDevice, const std::shared_ptr<GDevice>& secondDevice,
                           const D3D12_INPUT_LAYOUT_DESC& layout, const UINT width, const UINT height)
{
    primeResources.Initialize(primeDevice, layout);
    primeResources.OnResize(width, height);

    secondResources.Initialize(secondDevice, layout);
    secondResources.OnResize(width, height);

    crossResources.Initialize(primeResources, primeDevice, secondDevice);
    crossResources.OnResize(width, height);

    OnResize(width, height);
}

void SharedFog::OnResize(const UINT width, const UINT height)
{
    if (renderTargetWidth == width && renderTargetHeight == height)
    {
        return;
    }

    renderTargetWidth = width;
    renderTargetHeight = height;

    primeResources.OnResize(width, height);
    secondResources.OnResize(width, height);
    crossResources.OnResize(width, height);
}

void SharedFog::Compute(const std::shared_ptr<GCommandList>& cmdList,
                        const std::shared_ptr<ConstantUploadBuffer<FogConstants>>& constants,
                        const FogResources& resources) const
{
    cmdList->StartMark(L"Fog");

    cmdList->TransitionBarrier(resources.GetFogMap(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->FlushResourceBarriers();

    const float clearValue[] = {0.0f, 0.0f, 0.0f, 0.0f};
    cmdList->ClearRenderTarget(resources.GetFogMapRTV(), 0, clearValue);

    cmdList->TransitionBarrier(resources.GetDepthMap(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(resources.GetFogMap(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->FlushResourceBarriers();

    cmdList->SetComputeRootSignature(resources.GetComputeRootSignature());
    cmdList->SetPipelineState(resources.GetFogPSO());
    cmdList->SetDescriptorsHeap(resources.GetComputeDescriptorTable());

    cmdList->SetComputeRootConstantBufferView(0, *constants.get());
    cmdList->SetComputeRootDescriptorTable(1, resources.GetComputeDescriptorTable(), 0);
    cmdList->SetComputeRootDescriptorTable(2, resources.GetComputeDescriptorTable(), 1);

    cmdList->Dispatch(IntDivRoundUp(renderTargetWidth, 32), IntDivRoundUp(renderTargetHeight, 32), 1);
    cmdList->UAVBarrier(resources.GetFogMap(), true);

    cmdList->TransitionBarrier(resources.GetFogMap(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();

    cmdList->EndMark();
}

void SharedFog::Composite(const std::shared_ptr<GCommandList>& cmdList,
                          const std::shared_ptr<ConstantUploadBuffer<FogConstants>>& constants,
                          const GTexture& sceneColorTexture,
                          const GTexture& outputRenderTarget,
                          const GDescriptor* outputRTV, const UINT outputRTVOffset) const
{
    auto compositeTable = primeResources.GetDevice()->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);
    const auto& fogMap = primeResources.GetFogMap();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    srvDesc.Format = sceneColorTexture.GetD3D12ResourceDesc().Format;
    sceneColorTexture.CreateShaderResourceView(&srvDesc, &compositeTable, 0);

    srvDesc.Format = fogMap.GetD3D12ResourceDesc().Format;
    fogMap.CreateShaderResourceView(&srvDesc, &compositeTable, 1);

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(renderTargetWidth);
    viewport.Height = static_cast<float>(renderTargetHeight);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissorRect = {0, 0, static_cast<LONG>(renderTargetWidth), static_cast<LONG>(renderTargetHeight)};

    cmdList->StartMark(L"Fog Composite");
    cmdList->TransitionBarrier(outputRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->FlushResourceBarriers();
    cmdList->SetViewports(&viewport, 1);
    cmdList->SetScissorRects(&scissorRect, 1);
    cmdList->SetRenderTargets(1, outputRTV, outputRTVOffset);
    cmdList->SetGraphicsRootSignature(primeResources.GetCompositeRootSignature());
    cmdList->SetPipelineState(primeResources.GetCompositePSO());
    cmdList->SetDescriptorsHeap(&compositeTable);

    cmdList->SetGraphicsRootConstantBufferView(0, *constants.get());
    cmdList->SetGraphicsRootDescriptorTable(1, &compositeTable);

    cmdList->SetVBuffer(0, 0, nullptr);
    cmdList->SetIBuffer(nullptr);
    cmdList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->Draw(3, 1, 0, 0);
    cmdList->EndMark();
}
