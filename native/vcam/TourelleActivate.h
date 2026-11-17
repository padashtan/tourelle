#pragma once

#include <mfapi.h>
#include <mfidl.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <mutex>

namespace tourelle {

class TourelleSource;

// COM class the Frame Server creates from the virtual camera's CLSID.
// Activates the media source lazily, so no hardware is touched at creation.
class __declspec(uuid("7268DC8D-D8F5-47C9-B981-EC9E9199EAEE")) TourelleActivate
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          Microsoft::WRL::ChainInterfaces<IMFActivate, IMFAttributes>> {
public:
    HRESULT RuntimeClassInitialize();

    // IMFActivate
    IFACEMETHODIMP ActivateObject(REFIID riid, void** object) override;
    IFACEMETHODIMP ShutdownObject() override;
    IFACEMETHODIMP DetachObject() override;

    // IMFAttributes: forwarded to an inner store.
    IFACEMETHODIMP GetItem(REFGUID key, PROPVARIANT* v) override { return a_->GetItem(key, v); }
    IFACEMETHODIMP GetItemType(REFGUID key, MF_ATTRIBUTE_TYPE* type) override { return a_->GetItemType(key, type); }
    IFACEMETHODIMP CompareItem(REFGUID key, REFPROPVARIANT v, BOOL* result) override {
        return a_->CompareItem(key, v, result);
    }
    IFACEMETHODIMP Compare(IMFAttributes* theirs, MF_ATTRIBUTES_MATCH_TYPE type, BOOL* result) override {
        return a_->Compare(theirs, type, result);
    }
    IFACEMETHODIMP GetUINT32(REFGUID key, UINT32* v) override { return a_->GetUINT32(key, v); }
    IFACEMETHODIMP GetUINT64(REFGUID key, UINT64* v) override { return a_->GetUINT64(key, v); }
    IFACEMETHODIMP GetDouble(REFGUID key, double* v) override { return a_->GetDouble(key, v); }
    IFACEMETHODIMP GetGUID(REFGUID key, GUID* v) override { return a_->GetGUID(key, v); }
    IFACEMETHODIMP GetStringLength(REFGUID key, UINT32* length) override { return a_->GetStringLength(key, length); }
    IFACEMETHODIMP GetString(REFGUID key, LPWSTR v, UINT32 size, UINT32* length) override {
        return a_->GetString(key, v, size, length);
    }
    IFACEMETHODIMP GetAllocatedString(REFGUID key, LPWSTR* v, UINT32* length) override {
        return a_->GetAllocatedString(key, v, length);
    }
    IFACEMETHODIMP GetBlobSize(REFGUID key, UINT32* size) override { return a_->GetBlobSize(key, size); }
    IFACEMETHODIMP GetBlob(REFGUID key, UINT8* buffer, UINT32 size, UINT32* blobSize) override {
        return a_->GetBlob(key, buffer, size, blobSize);
    }
    IFACEMETHODIMP GetAllocatedBlob(REFGUID key, UINT8** buffer, UINT32* size) override {
        return a_->GetAllocatedBlob(key, buffer, size);
    }
    IFACEMETHODIMP GetUnknown(REFGUID key, REFIID riid, LPVOID* object) override {
        return a_->GetUnknown(key, riid, object);
    }
    IFACEMETHODIMP SetItem(REFGUID key, REFPROPVARIANT v) override { return a_->SetItem(key, v); }
    IFACEMETHODIMP DeleteItem(REFGUID key) override { return a_->DeleteItem(key); }
    IFACEMETHODIMP DeleteAllItems() override { return a_->DeleteAllItems(); }
    IFACEMETHODIMP SetUINT32(REFGUID key, UINT32 v) override { return a_->SetUINT32(key, v); }
    IFACEMETHODIMP SetUINT64(REFGUID key, UINT64 v) override { return a_->SetUINT64(key, v); }
    IFACEMETHODIMP SetDouble(REFGUID key, double v) override { return a_->SetDouble(key, v); }
    IFACEMETHODIMP SetGUID(REFGUID key, REFGUID v) override { return a_->SetGUID(key, v); }
    IFACEMETHODIMP SetString(REFGUID key, LPCWSTR v) override { return a_->SetString(key, v); }
    IFACEMETHODIMP SetBlob(REFGUID key, const UINT8* buffer, UINT32 size) override {
        return a_->SetBlob(key, buffer, size);
    }
    IFACEMETHODIMP SetUnknown(REFGUID key, IUnknown* v) override { return a_->SetUnknown(key, v); }
    IFACEMETHODIMP LockStore() override { return a_->LockStore(); }
    IFACEMETHODIMP UnlockStore() override { return a_->UnlockStore(); }
    IFACEMETHODIMP GetCount(UINT32* count) override { return a_->GetCount(count); }
    IFACEMETHODIMP GetItemByIndex(UINT32 index, GUID* key, PROPVARIANT* v) override {
        return a_->GetItemByIndex(index, key, v);
    }
    IFACEMETHODIMP CopyAllItems(IMFAttributes* destination) override { return a_->CopyAllItems(destination); }

private:
    std::mutex lock_;
    Microsoft::WRL::ComPtr<IMFAttributes> a_;
    Microsoft::WRL::ComPtr<IMFMediaSource> source_;
};

}  // namespace tourelle
