#include "FrameAssembler.h"
#include "Test.h"

using namespace tourelle::core;

namespace {

std::vector<uint8_t> Payload(uint8_t info, std::initializer_list<uint8_t> data) {
    std::vector<uint8_t> p{2, info};
    p.insert(p.end(), data);
    return p;
}

struct Collector {
    std::vector<std::vector<uint8_t>> frames;
    FrameAssembler::Sink Sink() {
        return [this](const uint8_t* d, size_t n) { frames.emplace_back(d, d + n); };
    }
};

void Feed(FrameAssembler& assembler, const std::vector<uint8_t>& p) { assembler.OnPayload(p.data(), p.size()); }

}  // namespace

TEST(assembler_end_of_frame) {
    Collector out;
    FrameAssembler a;
    a.Reset(4, 1024, out.Sink());
    Feed(a, Payload(0x00, {1, 2}));
    Feed(a, Payload(0x02, {3, 4}));  // EOF
    EXPECT_EQ(out.frames.size(), 1u);
    if (!out.frames.empty()) EXPECT(out.frames[0] == std::vector<uint8_t>({1, 2, 3, 4}));
    EXPECT_EQ(a.Delivered(), 1u);
}

TEST(assembler_fid_toggle_without_eof) {
    Collector out;
    FrameAssembler a;
    a.Reset(0, 1024, out.Sink());
    Feed(a, Payload(0x00, {1}));
    Feed(a, Payload(0x00, {2}));
    Feed(a, Payload(0x01, {3}));  // FID toggles: previous frame ends
    EXPECT_EQ(out.frames.size(), 1u);
    if (!out.frames.empty()) EXPECT(out.frames[0] == std::vector<uint8_t>({1, 2}));
}

TEST(assembler_error_bit_drops_frame) {
    Collector out;
    FrameAssembler a;
    a.Reset(0, 1024, out.Sink());
    Feed(a, Payload(0x40, {1}));  // ERR
    Feed(a, Payload(0x02, {2}));
    Feed(a, Payload(0x03, {3}));  // next frame is fine
    EXPECT_EQ(out.frames.size(), 1u);
    EXPECT_EQ(a.Dropped(), 1u);
}

TEST(assembler_wrong_size_dropped) {
    Collector out;
    FrameAssembler a;
    a.Reset(4, 1024, out.Sink());
    Feed(a, Payload(0x02, {1, 2, 3}));  // 3 bytes, 4 expected
    EXPECT(out.frames.empty());
    EXPECT_EQ(a.Dropped(), 1u);
}

TEST(assembler_size_cap) {
    Collector out;
    FrameAssembler a;
    a.Reset(0, 4, out.Sink());
    Feed(a, Payload(0x00, {1, 2, 3}));
    Feed(a, Payload(0x02, {4, 5, 6}));  // would reach 6 > 4
    EXPECT(out.frames.empty());
    EXPECT_EQ(a.Dropped(), 1u);
}

TEST(assembler_rejects_bad_headers) {
    Collector out;
    FrameAssembler a;
    a.Reset(0, 1024, out.Sink());
    const uint8_t tooLong[] = {9, 0x02, 1};  // header longer than the packet
    a.OnPayload(tooLong, sizeof(tooLong));
    const uint8_t tooShort[] = {1};
    a.OnPayload(tooShort, sizeof(tooShort));
    a.OnPayload(nullptr, 10);
    EXPECT(out.frames.empty());
}

TEST(assembler_mark_error) {
    Collector out;
    FrameAssembler a;
    a.Reset(0, 1024, out.Sink());
    Feed(a, Payload(0x00, {1}));
    a.MarkError();
    Feed(a, Payload(0x02, {2}));
    EXPECT(out.frames.empty());
}
