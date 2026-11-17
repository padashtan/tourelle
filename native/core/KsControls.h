#pragma once
// Helpers for kernel-streaming property requests on LONG-valued controls
// (PROPSETID_VIDCAP_CAMERACONTROL, PROPSETID_VIDCAP_VIDEOPROCAMP). These
// requests come from any process that opens the camera: every size is checked.

#include <windows.h>
#include <wtypes.h>  // VT_I4
#include <mmsystem.h>
#include <ks.h>
#include <ksmedia.h>

namespace tourelle::core {

struct LongRange {
    LONG minimum = 0;
    LONG maximum = 0;
    LONG step = 1;
    LONG defaultValue = 0;
    bool supportsAuto = false;
};

// Full BASICSUPPORT answer for a LONG control with one range and one default.
struct LongRangeDescription {
    KSPROPERTY_DESCRIPTION description;
    KSPROPERTY_MEMBERSHEADER rangeHeader;
    KSPROPERTY_STEPPING_LONG range;
    KSPROPERTY_MEMBERSHEADER defaultHeader;
    LONG defaultValue;
};

enum class KsOperation { Get, Set, BasicSupport, Unsupported };

KsOperation OperationOf(ULONG flags);

// Answers KSPROPERTY_TYPE_BASICSUPPORT the way callers probe it: 4 bytes (access
// flags), sizeof(KSPROPERTY_DESCRIPTION), the full description, or a size query.
HRESULT WriteBasicSupport(const LongRange& range, void* data, ULONG length, ULONG* returned);

// Camera control and video procamp share the same layout (KSPROPERTY_CAMERACONTROL_S
// and KSPROPERTY_VIDEOPROCAMP_S). Returns false when the buffer is too small.
bool ReadControlValue(const void* data, ULONG length, LONG* value, ULONG* flags);
HRESULT WriteControlValue(void* data, ULONG length, LONG value, ULONG flags, ULONG capabilities, ULONG* returned);

}  // namespace tourelle::core
