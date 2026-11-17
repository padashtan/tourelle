#include "KsControls.h"

#include <cstddef>
#include <cstring>

namespace tourelle::core {
namespace {

static_assert(sizeof(KSPROPERTY_CAMERACONTROL_S) == sizeof(KSPROPERTY_VIDEOPROCAMP_S));
static_assert(offsetof(KSPROPERTY_CAMERACONTROL_S, Value) == offsetof(KSPROPERTY_VIDEOPROCAMP_S, Value));

LongRangeDescription Describe(const LongRange& range) {
    LongRangeDescription full{};
    full.description.AccessFlags = KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET;
    full.description.DescriptionSize = sizeof(LongRangeDescription);
    full.description.PropTypeSet.Set = KSPROPTYPESETID_General;
    full.description.PropTypeSet.Id = VT_I4;
    full.description.MembersListCount = 2;
    full.rangeHeader.MembersFlags = KSPROPERTY_MEMBER_STEPPEDRANGES;
    full.rangeHeader.MembersSize = sizeof(KSPROPERTY_STEPPING_LONG);
    full.rangeHeader.MembersCount = 1;
    full.range.SteppingDelta = static_cast<ULONG>(range.step > 0 ? range.step : 1);
    full.range.Bounds.SignedMinimum = range.minimum;
    full.range.Bounds.SignedMaximum = range.maximum;
    full.defaultHeader.MembersFlags = KSPROPERTY_MEMBER_VALUES;
    full.defaultHeader.MembersSize = sizeof(LONG);
    full.defaultHeader.MembersCount = 1;
    full.defaultHeader.Flags = KSPROPERTY_MEMBER_FLAG_DEFAULT;
    full.defaultValue = range.defaultValue;
    return full;
}

}  // namespace

KsOperation OperationOf(ULONG flags) {
    flags &= ~KSPROPERTY_TYPE_TOPOLOGY;
    if (flags == KSPROPERTY_TYPE_GET) return KsOperation::Get;
    if (flags == KSPROPERTY_TYPE_SET) return KsOperation::Set;
    if (flags == KSPROPERTY_TYPE_BASICSUPPORT) return KsOperation::BasicSupport;
    return KsOperation::Unsupported;
}

HRESULT WriteBasicSupport(const LongRange& range, void* data, ULONG length, ULONG* returned) {
    if (!returned) return E_POINTER;
    *returned = 0;
    const LongRangeDescription full = Describe(range);
    if (length == 0 || !data) {
        *returned = sizeof(full);
        return HRESULT_FROM_WIN32(ERROR_MORE_DATA);
    }
    if (length >= sizeof(full)) {
        std::memcpy(data, &full, sizeof(full));
        *returned = sizeof(full);
        return S_OK;
    }
    if (length >= sizeof(KSPROPERTY_DESCRIPTION)) {
        std::memcpy(data, &full.description, sizeof(KSPROPERTY_DESCRIPTION));
        *returned = sizeof(KSPROPERTY_DESCRIPTION);
        return S_OK;
    }
    if (length >= sizeof(ULONG)) {
        std::memcpy(data, &full.description.AccessFlags, sizeof(ULONG));
        *returned = sizeof(ULONG);
        return S_OK;
    }
    *returned = sizeof(full);
    return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
}

bool ReadControlValue(const void* data, ULONG length, LONG* value, ULONG* flags) {
    if (!data || length < sizeof(KSPROPERTY_CAMERACONTROL_S) || !value || !flags) return false;
    KSPROPERTY_CAMERACONTROL_S control;
    std::memcpy(&control, data, sizeof(control));  // the caller's buffer may be unaligned
    *value = control.Value;
    *flags = control.Flags;
    return true;
}

HRESULT WriteControlValue(void* data, ULONG length, LONG value, ULONG flags, ULONG capabilities, ULONG* returned) {
    if (!returned) return E_POINTER;
    *returned = 0;
    if (!data || length < sizeof(KSPROPERTY_CAMERACONTROL_S)) {
        *returned = sizeof(KSPROPERTY_CAMERACONTROL_S);
        return HRESULT_FROM_WIN32(ERROR_MORE_DATA);
    }
    KSPROPERTY_CAMERACONTROL_S control;
    std::memcpy(&control, data, sizeof(control));  // keep the caller's KSPROPERTY header
    control.Value = value;
    control.Flags = flags;
    control.Capabilities = capabilities;
    std::memcpy(data, &control, sizeof(control));
    *returned = sizeof(control);
    return S_OK;
}

}  // namespace tourelle::core
