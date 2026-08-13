#include "pch.h"
#include "SharedVolume.h"

#include "GCommandList.h"
#include "GCommandQueue.h"
#include "GDevice.h"
#include "GModel.h"


void VolumeResources::InitializeVolumeRS()
{
    volumeRootSignature =
        std::make_shared<GRootSignature>();


    // t0, space2
    CD3DX12_DESCRIPTOR_RANGE depthTable;

    depthTable.Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
        1,
        0,
        2);


    // root[0] -> b0 ObjectData
    volumeRootSignature->AddConstantBufferParameter(
        0);

    // root[1] -> b1 PassConstants / WorldData
    volumeRootSignature->AddConstantBufferParameter(
        1);

    // root[2] -> b2 VolumeData
    volumeRootSignature->AddConstantBufferParameter(
        2);

    // root[3] -> t0, space2
    volumeRootSignature->AddDescriptorParameter(
        &depthTable,
        1,
        D3D12_SHADER_VISIBILITY_PIXEL);


    // Common.hlsl:
    // gsamPointClamp : register(s1)
    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        1,
        D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);


    volumeRootSignature->AddStaticSampler(
        pointClamp);


    volumeRootSignature->Initialize(
        device);
}


void VolumeResources::InitializeCompositeRS()
{
    compositeRootSignature =
        std::make_shared<GRootSignature>();


    // t0, space3
    // Resulting RGBA volume texture.
    CD3DX12_DESCRIPTOR_RANGE volumeTable;

    volumeTable.Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
        1,
        0,
        3);


    compositeRootSignature->AddDescriptorParameter(
        &volumeTable,
        1,
        D3D12_SHADER_VISIBILITY_PIXEL);


    // Volume composite shader uses
    // gsamLinearClamp : register(s3)
    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        3,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);


    compositeRootSignature->AddStaticSampler(
        linearClamp);


    compositeRootSignature->Initialize(
        device);
}


void VolumeResources::Initialize(
    const std::shared_ptr<GDevice>& Device,
    const D3D12_INPUT_LAYOUT_DESC& layout,
    const std::shared_ptr<GModel>& model)
{
    device = Device;
    volumeModel = model;


    depthMapSRV =
        device->AllocateDescriptors(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            1);


    volumeMapSRV =
        device->AllocateDescriptors(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            1);


    volumeMapRTV =
        device->AllocateDescriptors(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
            1);


    InitializeVolumeRS();
    InitializeCompositeRS();

    BuildPSO(layout);
}


void VolumeResources::OnResize(
    const UINT width,
    const UINT height)
{
    D3D12_RESOURCE_DESC texDesc = {};

    texDesc.Dimension =
        D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    texDesc.Alignment = 0;

    texDesc.Width = width;
    texDesc.Height = height;

    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;

    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;

    texDesc.Layout =
        D3D12_TEXTURE_LAYOUT_UNKNOWN;


    // ========================================================
    // Depth
    // ========================================================

    if (!depthMap.IsValid())
    {
        texDesc.Format =
            DepthMapFormat;

        texDesc.Flags =
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;


        D3D12_CLEAR_VALUE depthClear = {};

        depthClear.Format =
            DXGI_FORMAT_D32_FLOAT;

        depthClear.DepthStencil.Depth =
            1.0f;

        depthClear.DepthStencil.Stencil =
            0;


        depthMap =
            GTexture(
                device,
                texDesc,
                L"Volume Depth Map " +
                device->GetName(),
                TextureUsage::Depth,
                &depthClear);
    }
    else
    {
        GTexture::Resize(
            depthMap,
            width,
            height,
            1);
    }


    // ========================================================
    // RGBA Volume result
    // ========================================================

    if (!volumeMap.IsValid())
    {
        texDesc.Format =
            VolumeMapFormat;

        texDesc.Flags =
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;


        const float clearValue[] =
        {
            0.0f,
            0.0f,
            0.0f,
            0.0f
        };


        const auto volumeClear =
            CD3DX12_CLEAR_VALUE(
                VolumeMapFormat,
                clearValue);


        volumeMap =
            GTexture(
                device,
                texDesc,
                L"Volume Map " +
                device->GetName(),
                TextureUsage::RenderTarget,
                &volumeClear);
    }
    else
    {
        GTexture::Resize(
            volumeMap,
            width,
            height,
            1);
    }


    RebuildDescriptors();
}


void VolumeResources::BuildPSO(
    const D3D12_INPUT_LAYOUT_DESC& layout)
{
    // ========================================================
    // Volume shaders
    // ========================================================

    auto volumeVS =
        std::make_unique<GShader>(
            L"Shaders\\Volume.hlsl",
            VertexShader,
            nullptr,
            "VS",
            "vs_5_1");

    volumeVS->LoadAndCompile();


    auto volumePS =
        std::make_unique<GShader>(
            L"Shaders\\Volume.hlsl",
            PixelShader,
            nullptr,
            "PS",
            "ps_5_1");

    volumePS->LoadAndCompile();


    D3D12_GRAPHICS_PIPELINE_STATE_DESC
        volumeDesc = {};

    volumeDesc.InputLayout =
        layout;

    volumeDesc.pRootSignature =
        volumeRootSignature
        ->GetNativeSignature()
        .Get();


    volumeDesc.RasterizerState =
        CD3DX12_RASTERIZER_DESC(
            D3D12_DEFAULT);

    // Draw exit/back faces.
    volumeDesc.RasterizerState.CullMode =
        D3D12_CULL_MODE_FRONT;


    volumeDesc.BlendState =
        CD3DX12_BLEND_DESC(
            D3D12_DEFAULT);


    // Premultiplied alpha.
    auto& volumeBlend =
        volumeDesc.BlendState.RenderTarget[0];

    volumeBlend.BlendEnable = true;

    volumeBlend.SrcBlend =
        D3D12_BLEND_ONE;

    volumeBlend.DestBlend =
        D3D12_BLEND_INV_SRC_ALPHA;

    volumeBlend.BlendOp =
        D3D12_BLEND_OP_ADD;

    volumeBlend.SrcBlendAlpha =
        D3D12_BLEND_ONE;

    volumeBlend.DestBlendAlpha =
        D3D12_BLEND_INV_SRC_ALPHA;

    volumeBlend.BlendOpAlpha =
        D3D12_BLEND_OP_ADD;

    volumeBlend.RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;


    volumeDesc.DepthStencilState =
        CD3DX12_DEPTH_STENCIL_DESC(
            D3D12_DEFAULT);

    volumeDesc.DepthStencilState.DepthEnable =
        false;

    volumeDesc.DepthStencilState.DepthWriteMask =
        D3D12_DEPTH_WRITE_MASK_ZERO;


    volumeDesc.SampleMask =
        UINT_MAX;

    volumeDesc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    volumeDesc.NumRenderTargets = 1;

    volumeDesc.RTVFormats[0] =
        VolumeMapFormat;

    volumeDesc.DSVFormat =
        DXGI_FORMAT_UNKNOWN;

    volumeDesc.SampleDesc.Count = 1;
    volumeDesc.SampleDesc.Quality = 0;


    volumePSO =
        std::make_shared<GraphicPSO>(
            RenderMode::Volume);

    volumePSO->SetPsoDesc(
        volumeDesc);

    volumePSO->SetRootSignature(
        *volumeRootSignature);

    volumePSO->SetShader(
        volumeVS.get());

    volumePSO->SetShader(
        volumePS.get());

    volumePSO->Initialize(
        device);


    // ========================================================
    // Composite shaders
    // ========================================================

    auto compositeVS =
        std::make_unique<GShader>(
            L"Shaders\\Volume.hlsl",
            VertexShader,
            nullptr,
            "VSComposite",
            "vs_5_1");

    compositeVS->LoadAndCompile();


    auto compositePS =
        std::make_unique<GShader>(
            L"Shaders\\Volume.hlsl",
            PixelShader,
            nullptr,
            "PSComposite",
            "ps_5_1");

    compositePS->LoadAndCompile();


    D3D12_GRAPHICS_PIPELINE_STATE_DESC
        compositeDesc = {};

    compositeDesc.InputLayout =
    {
        nullptr,
        0
    };

    compositeDesc.pRootSignature =
        compositeRootSignature
        ->GetNativeSignature()
        .Get();


    compositeDesc.RasterizerState =
        CD3DX12_RASTERIZER_DESC(
            D3D12_DEFAULT);


    compositeDesc.BlendState =
        CD3DX12_BLEND_DESC(
            D3D12_DEFAULT);


    auto& compositeBlend =
        compositeDesc.BlendState.RenderTarget[0];

    compositeBlend.BlendEnable = true;

    // VolumeMap already contains premultiplied RGB.
    compositeBlend.SrcBlend =
        D3D12_BLEND_ONE;

    compositeBlend.DestBlend =
        D3D12_BLEND_INV_SRC_ALPHA;

    compositeBlend.BlendOp =
        D3D12_BLEND_OP_ADD;

    compositeBlend.SrcBlendAlpha =
        D3D12_BLEND_ONE;

    compositeBlend.DestBlendAlpha =
        D3D12_BLEND_INV_SRC_ALPHA;

    compositeBlend.BlendOpAlpha =
        D3D12_BLEND_OP_ADD;

    compositeBlend.RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;


    compositeDesc.DepthStencilState =
        CD3DX12_DEPTH_STENCIL_DESC(
            D3D12_DEFAULT);

    compositeDesc.DepthStencilState.DepthEnable =
        false;

    compositeDesc.DepthStencilState.DepthWriteMask =
        D3D12_DEPTH_WRITE_MASK_ZERO;


    compositeDesc.SampleMask =
        UINT_MAX;

    compositeDesc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    compositeDesc.NumRenderTargets = 1;

    compositeDesc.RTVFormats[0] =
        GetSRGBFormat(
            BackBufferFormat);

    compositeDesc.DSVFormat =
        DXGI_FORMAT_UNKNOWN;

    compositeDesc.SampleDesc.Count = 1;
    compositeDesc.SampleDesc.Quality = 0;


    compositePSO =
        std::make_shared<GraphicPSO>(
            RenderMode::Debug);

    compositePSO->SetPsoDesc(
        compositeDesc);

    compositePSO->SetRootSignature(
        *compositeRootSignature);

    compositePSO->SetShader(
        compositeVS.get());

    compositePSO->SetShader(
        compositePS.get());

    compositePSO->Initialize(
        device);
}


void VolumeResources::RebuildDescriptors() const
{
    D3D12_SHADER_RESOURCE_VIEW_DESC
        srvDesc = {};

    srvDesc.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    srvDesc.ViewDimension =
        D3D12_SRV_DIMENSION_TEXTURE2D;

    srvDesc.Texture2D.MostDetailedMip =
        0;

    srvDesc.Texture2D.MipLevels =
        1;


    // Depth SRV.
    srvDesc.Format =
        DXGI_FORMAT_R32_FLOAT;

    depthMap.CreateShaderResourceView(
        &srvDesc,
        &depthMapSRV);


    // Volume RGBA SRV.
    srvDesc.Format =
        VolumeMapFormat;

    volumeMap.CreateShaderResourceView(
        &srvDesc,
        &volumeMapSRV);


    // Volume RTV.
    D3D12_RENDER_TARGET_VIEW_DESC
        rtvDesc = {};

    rtvDesc.ViewDimension =
        D3D12_RTV_DIMENSION_TEXTURE2D;

    rtvDesc.Format =
        VolumeMapFormat;

    rtvDesc.Texture2D.MipSlice =
        0;

    rtvDesc.Texture2D.PlaneSlice =
        0;


    volumeMap.CreateRenderTargetView(
        &rtvDesc,
        &volumeMapRTV);
}


// ============================================================
// Cross adapter resources
// ============================================================

void VolumeCrossResources::Initialize(
    const VolumeResources& resources,
    const std::shared_ptr<GDevice>& primeDevice,
    const std::shared_ptr<GDevice>& secondDevice)
{
    sharedDepthMap =
        std::make_shared<GCrossAdapterResource>(
            resources
            .GetDepthMap()
            .GetD3D12ResourceDesc(),

            primeDevice,
            secondDevice,

            L"Cross Adapter Volume Depth Map");


    sharedVolumeMap =
        std::make_shared<GCrossAdapterResource>(
            resources
            .GetVolumeMap()
            .GetD3D12ResourceDesc(),

            primeDevice,
            secondDevice,

            L"Cross Adapter Volume Map");
}


void VolumeCrossResources::OnResize(
    const UINT width,
    const UINT height) const
{
    sharedDepthMap->Resize(
        width,
        height);

    sharedVolumeMap->Resize(
        width,
        height);
}


// ============================================================
// SharedVolume
// ============================================================

void SharedVolume::Initialize(
    const std::shared_ptr<GDevice>& primeDevice,
    const std::shared_ptr<GDevice>& secondDevice,
    const D3D12_INPUT_LAYOUT_DESC& layout,
    const std::shared_ptr<GModel>& primeVolumeModel,
    const UINT width,
    const UINT height)
{
    // Primary resources only need the received VolumeMap
    // for final compositing.
    primeResources.Initialize(
        primeDevice,
        layout);

    primeResources.OnResize(
        width,
        height);


    // Duplicate the proxy cube onto GPU1.
    auto secondQueue =
        secondDevice->GetCommandQueue();

    auto uploadCmdList =
        secondQueue->GetCommandList();


    auto secondVolumeModel =
        primeVolumeModel->Dublicate(
            uploadCmdList);


    const UINT64 uploadFence =
        secondQueue->ExecuteCommandList(
            uploadCmdList);

    secondQueue->WaitForFenceValue(
        uploadFence);


    secondResources.Initialize(
        secondDevice,
        layout,
        secondVolumeModel);

    secondResources.OnResize(
        width,
        height);


    crossResources.Initialize(
        primeResources,
        primeDevice,
        secondDevice);


    renderTargetWidth =
        width;

    renderTargetHeight =
        height;
}


void SharedVolume::OnResize(
    const UINT width,
    const UINT height)
{
    if (
        renderTargetWidth == width &&
        renderTargetHeight == height)
    {
        return;
    }


    renderTargetWidth =
        width;

    renderTargetHeight =
        height;


    primeResources.OnResize(
        width,
        height);

    secondResources.OnResize(
        width,
        height);

    crossResources.OnResize(
        width,
        height);
}


// ============================================================
// Render volume on selected GPU.
// For MGPU this will be secondResources.
// ============================================================

void SharedVolume::Render(
    const std::shared_ptr<GCommandList>& cmdList,
    const std::shared_ptr<ConstantUploadBuffer<ObjectConstants>>& objectConstants,
    const std::shared_ptr<ConstantUploadBuffer<PassConstants>>& passConstants,
    const std::shared_ptr<ConstantUploadBuffer<VolumeConstants>>& volumeConstants,
    const VolumeResources& resources) const
{
    cmdList->StartMark(
        L"Volume");


    // Volume output.
    cmdList->TransitionBarrier(
        resources.GetVolumeMap(),
        D3D12_RESOURCE_STATE_RENDER_TARGET);


    // Copied scene depth.
    cmdList->TransitionBarrier(
        resources.GetDepthMap(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);


    cmdList->FlushResourceBarriers();


    const float clearValue[] =
    {
        0.0f,
        0.0f,
        0.0f,
        0.0f
    };


    cmdList->ClearRenderTarget(
        resources.GetVolumeMapRTV(),
        0,
        clearValue);


    D3D12_VIEWPORT viewport = {};

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;

    viewport.Width =
        static_cast<float>(
            renderTargetWidth);

    viewport.Height =
        static_cast<float>(
            renderTargetHeight);

    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;


    const D3D12_RECT scissorRect =
    {
        0,
        0,

        static_cast<LONG>(
            renderTargetWidth),

        static_cast<LONG>(
            renderTargetHeight)
    };


    cmdList->SetViewports(
        &viewport,
        1);

    cmdList->SetScissorRects(
        &scissorRect,
        1);


    cmdList->SetRenderTargets(
        1,
        resources.GetVolumeMapRTV());


    cmdList->SetGraphicsRootSignature(
        resources.GetVolumeRootSignature());


    cmdList->SetPipelineState(
        resources.GetVolumePSO());


    // The depth SRV is the descriptor heap used by this pass.
    cmdList->SetDescriptorsHeap(
        resources.GetDepthMapSRV());


    // root[0] -> b0 ObjectData
    cmdList->SetGraphicsRootConstantBufferView(
        0,
        *objectConstants);


    // root[1] -> b1 WorldData
    cmdList->SetGraphicsRootConstantBufferView(
        1,
        *passConstants);


    // root[2] -> b2 VolumeData
    cmdList->SetGraphicsRootConstantBufferView(
        2,
        *volumeConstants);


    // root[3] -> t0, space2
    cmdList->SetGraphicsRootDescriptorTable(
        3,
        resources.GetDepthMapSRV());


    const auto& model =
        resources.GetVolumeModel();


    if (model != nullptr)
    {
        model->Draw(
            cmdList);
    }


    cmdList->TransitionBarrier(
        resources.GetVolumeMap(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    cmdList->FlushResourceBarriers();


    cmdList->EndMark();
}


// ============================================================
// Blend the returned GPU1 VolumeMap over GPU0 scene.
// ============================================================

void SharedVolume::Composite(
    const std::shared_ptr<GCommandList>& cmdList,
    const GTexture& outputRenderTarget,
    const GDescriptor* outputRTV,
    const UINT outputRTVOffset) const
{
    cmdList->StartMark(
        L"Volume Composite");


    cmdList->TransitionBarrier(
        primeResources.GetVolumeMap(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    cmdList->TransitionBarrier(
        outputRenderTarget,
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    cmdList->FlushResourceBarriers();


    D3D12_VIEWPORT viewport = {};

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;

    viewport.Width =
        static_cast<float>(
            renderTargetWidth);

    viewport.Height =
        static_cast<float>(
            renderTargetHeight);

    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;


    const D3D12_RECT scissorRect =
    {
        0,
        0,

        static_cast<LONG>(
            renderTargetWidth),

        static_cast<LONG>(
            renderTargetHeight)
    };


    cmdList->SetViewports(
        &viewport,
        1);

    cmdList->SetScissorRects(
        &scissorRect,
        1);


    cmdList->SetRenderTargets(
        1,
        outputRTV,
        outputRTVOffset);


    cmdList->SetGraphicsRootSignature(
        primeResources
        .GetCompositeRootSignature());


    cmdList->SetPipelineState(
        primeResources
        .GetCompositePSO());


    cmdList->SetDescriptorsHeap(
        primeResources
        .GetVolumeMapSRV());


    // root[0] -> t0, space3
    cmdList->SetGraphicsRootDescriptorTable(
        0,
        primeResources
        .GetVolumeMapSRV());


    // Full-screen triangle.
    cmdList->SetVBuffer(
        0,
        0,
        nullptr);

    cmdList->SetIBuffer(
        nullptr);

    cmdList->SetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    cmdList->Draw(
        3,
        1,
        0,
        0);


    cmdList->EndMark();
}