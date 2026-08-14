#include "pch.h"
#include "GCrossAdapterResource.h"

#include "d3dUtil.h"
#include "GDevice.h"
#include "GResource.h"


namespace PEPEngine::Graphics
{
    namespace
    {
        void CrossAdapterDebugCheckpoint(
            const std::wstring& message)
        {
            OutputDebugStringW(
                (L"[SharedVolumeInit] PEPEngine::Graphics::GCrossAdapterResource " + message + L"\n")
                .c_str());
        }

        void CrossAdapterDebugResourceDesc(
            const std::wstring& message,
            const D3D12_RESOURCE_DESC& desc)
        {
            CrossAdapterDebugCheckpoint(
                message +
                L": Width=" + std::to_wstring(desc.Width) +
                L", Height=" + std::to_wstring(desc.Height) +
                L", Format=" + std::to_wstring(static_cast<UINT>(desc.Format)) +
                L", Flags=" + std::to_wstring(static_cast<UINT>(desc.Flags)) +
                L", Layout=" + std::to_wstring(static_cast<UINT>(desc.Layout)) +
                L", MipLevels=" + std::to_wstring(desc.MipLevels) +
                L", SampleDesc.Count=" + std::to_wstring(desc.SampleDesc.Count));
        }
    }

    bool GCrossAdapterResource::IsInit() const
    {
        return isInit;
    }

    GCrossAdapterResource::GCrossAdapterResource(D3D12_RESOURCE_DESC& desc, const std::shared_ptr<GDevice>& primeDevice,
                                                 const std::shared_ptr<GDevice>& sharedDevice, const std::wstring& name,
                                                 const D3D12_RESOURCE_STATES initialState,
                                                 const D3D12_CLEAR_VALUE* clearValue)
    {
        CrossAdapterDebugCheckpoint(
            L"constructor begin name=" + name);

        CrossAdapterDebugResourceDesc(
            L"constructor input desc",
            desc);

        CrossAdapterDebugCheckpoint(
            L"before setting cross-adapter flags");

        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;

        CrossAdapterDebugResourceDesc(
            L"after setting cross-adapter flags",
            desc);

        CrossAdapterDebugCheckpoint(
            L"before setting row-major layout");

        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        CrossAdapterDebugResourceDesc(
            L"after setting row-major layout",
            desc);

        CrossAdapterDebugCheckpoint(
            L"before footprint declaration");

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;

        CrossAdapterDebugCheckpoint(
            L"after footprint declaration");

        CrossAdapterDebugResourceDesc(
            L"before GetCopyableFootprints",
            desc);

        primeDevice->GetDXDevice()->GetCopyableFootprints(&desc, 0, 1, 0, &layout, nullptr, nullptr, nullptr);

        CrossAdapterDebugCheckpoint(
            L"after GetCopyableFootprints RowPitch=" +
            std::to_wstring(layout.Footprint.RowPitch) +
            L", Footprint.Height=" +
            std::to_wstring(layout.Footprint.Height));

        CrossAdapterDebugCheckpoint(
            L"before calculating textureSize");

        auto textureSize = Align(layout.Footprint.RowPitch * layout.Footprint.Height);

        CrossAdapterDebugCheckpoint(
            L"after calculating textureSize=" +
            std::to_wstring(textureSize) +
            L", RowPitch=" +
            std::to_wstring(layout.Footprint.RowPitch) +
            L", Footprint.Height=" +
            std::to_wstring(layout.Footprint.Height));

        // Create a heap that will be shared by both adapters.
        CrossAdapterDebugCheckpoint(
            L"before heapDesc creation");

        CD3DX12_HEAP_DESC heapDesc(
            textureSize,
            D3D12_HEAP_TYPE_DEFAULT,
            0,
            D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER);

        CrossAdapterDebugCheckpoint(
            L"after heapDesc creation textureSize=" +
            std::to_wstring(textureSize));

        CrossAdapterDebugCheckpoint(
            L"before CreateHeap");

        ThrowIfFailed(primeDevice->GetDXDevice()->CreateHeap(&heapDesc, IID_PPV_ARGS(&crossAdapterResourceHeap[0])));

        CrossAdapterDebugCheckpoint(
            L"after CreateHeap");

        CrossAdapterDebugCheckpoint(
            L"before heapHandle initialization");

        HANDLE heapHandle = nullptr;

        CrossAdapterDebugCheckpoint(
            L"after heapHandle initialization");

        CrossAdapterDebugCheckpoint(
            L"before CreateSharedHandle");

        ThrowIfFailed(primeDevice->GetDXDevice()->CreateSharedHandle(
            crossAdapterResourceHeap[0].Get(),
            nullptr,
            GENERIC_ALL,
            nullptr,
            &heapHandle));

        CrossAdapterDebugCheckpoint(
            L"after CreateSharedHandle");

        CrossAdapterDebugCheckpoint(
            L"before OpenSharedHandle");

        HRESULT openSharedHandleResult = sharedDevice->GetDXDevice()->OpenSharedHandle(
            heapHandle, IID_PPV_ARGS(&crossAdapterResourceHeap[1]));

        CrossAdapterDebugCheckpoint(
            L"after OpenSharedHandle HRESULT=" +
            std::to_wstring(static_cast<long>(openSharedHandleResult)));

        // We can close the handle after opening the cross-adapter shared resource.
        CrossAdapterDebugCheckpoint(
            L"before CloseHandle");

        CloseHandle(heapHandle);

        CrossAdapterDebugCheckpoint(
            L"after CloseHandle");

        CrossAdapterDebugCheckpoint(
            L"before ThrowIfFailed OpenSharedHandle result");

        ThrowIfFailed(openSharedHandleResult);

        CrossAdapterDebugCheckpoint(
            L"after ThrowIfFailed OpenSharedHandle result");

        CrossAdapterDebugResourceDesc(
            L"before creating primeResource",
            desc);

        primeResource = std::make_shared<GResource>(primeDevice, desc, crossAdapterResourceHeap[0], name, clearValue,
                                                    initialState);

        CrossAdapterDebugCheckpoint(
            L"after creating primeResource");

        CrossAdapterDebugResourceDesc(
            L"before creating sharedResource",
            desc);

        sharedResource = std::make_shared<GResource
        >(sharedDevice, desc, crossAdapterResourceHeap[1], name + L" Shared");

        CrossAdapterDebugCheckpoint(
            L"after creating sharedResource");

        CrossAdapterDebugCheckpoint(
            L"before setting isInit");

        isInit = true;

        CrossAdapterDebugCheckpoint(
            L"after setting isInit");

        CrossAdapterDebugCheckpoint(
            L"constructor end name=" + name);
    }

    const GResource& GCrossAdapterResource::GetPrimeResource() const
    {
        return *primeResource.get();
    }

    const GResource& GCrossAdapterResource::GetSharedResource() const
    {
        return *sharedResource.get();
    }

    void GCrossAdapterResource::Reset()
    {
        if (!isInit) return;

        if (primeResource)
        {
            primeResource->Reset();
            primeResource.reset();
        }

        if (sharedResource)
        {
            sharedResource->Reset();
            sharedResource.reset();
        }

        isInit = false;
    }

    void GCrossAdapterResource::Resize(const UINT newWidth, const UINT newHeight)
    {
        auto desc = primeResource->GetD3D12ResourceDesc();
        desc.Width = newWidth;
        desc.Height = newHeight;


        primeResource = std::make_shared<GResource>(primeResource->GetDevice(), desc, crossAdapterResourceHeap[0],
                                                    primeResource->GetName());

        sharedResource = std::make_shared<GResource>(sharedResource->GetDevice(), desc, crossAdapterResourceHeap[1],
                                                     sharedResource->GetName());
    }
}
