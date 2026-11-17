#include <cstring>

#include "KsControls.h"
#include "Test.h"

using namespace tourelle::core;

TEST(ks_operation_decoding) {
    EXPECT(OperationOf(KSPROPERTY_TYPE_GET) == KsOperation::Get);
    EXPECT(OperationOf(KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_TOPOLOGY) == KsOperation::Set);
    EXPECT(OperationOf(KSPROPERTY_TYPE_BASICSUPPORT) == KsOperation::BasicSupport);
    EXPECT(OperationOf(KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET) == KsOperation::Unsupported);
}

TEST(ks_basic_support_probe_sequence) {
    const LongRange range{-70, 70, 1, 0};
    ULONG returned = 0;
    EXPECT_EQ(WriteBasicSupport(range, nullptr, 0, &returned), HRESULT_FROM_WIN32(ERROR_MORE_DATA));
    EXPECT_EQ(returned, static_cast<ULONG>(sizeof(LongRangeDescription)));

    ULONG access = 0;
    EXPECT_EQ(WriteBasicSupport(range, &access, sizeof(access), &returned), S_OK);
    EXPECT_EQ(access, static_cast<ULONG>(KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET));

    KSPROPERTY_DESCRIPTION description{};
    EXPECT_EQ(WriteBasicSupport(range, &description, sizeof(description), &returned), S_OK);
    EXPECT_EQ(description.DescriptionSize, static_cast<ULONG>(sizeof(LongRangeDescription)));

    LongRangeDescription full{};
    EXPECT_EQ(WriteBasicSupport(range, &full, sizeof(full), &returned), S_OK);
    EXPECT_EQ(full.range.Bounds.SignedMinimum, -70);
    EXPECT_EQ(full.range.Bounds.SignedMaximum, 70);
    EXPECT_EQ(full.defaultValue, 0);

    BYTE tiny[2]{};
    EXPECT_EQ(WriteBasicSupport(range, tiny, sizeof(tiny), &returned), HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
    EXPECT_EQ(WriteBasicSupport(range, tiny, sizeof(tiny), nullptr), E_POINTER);
}

TEST(ks_control_value_round_trip) {
    KSPROPERTY_CAMERACONTROL_S control{};
    control.Property.Id = KSPROPERTY_CAMERACONTROL_PAN;
    control.Value = -12;
    control.Flags = KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL;
    LONG value = 0;
    ULONG flags = 0;
    EXPECT(ReadControlValue(&control, sizeof(control), &value, &flags));
    EXPECT_EQ(value, -12);
    EXPECT(!ReadControlValue(&control, sizeof(control) - 1, &value, &flags));
    EXPECT(!ReadControlValue(nullptr, sizeof(control), &value, &flags));

    ULONG returned = 0;
    EXPECT_EQ(WriteControlValue(&control, sizeof(control), 42, 2, 2, &returned), S_OK);
    EXPECT_EQ(control.Value, 42);
    EXPECT_EQ(control.Property.Id, static_cast<ULONG>(KSPROPERTY_CAMERACONTROL_PAN));  // header kept
    EXPECT_EQ(WriteControlValue(&control, 4, 1, 2, 2, &returned), HRESULT_FROM_WIN32(ERROR_MORE_DATA));
}
