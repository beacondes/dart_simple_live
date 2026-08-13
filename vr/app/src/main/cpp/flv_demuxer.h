#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>

// Parses an HTTP-FLV live stream and extracts H.264 NAL units in Annex-B format
// (each prefixed with a 0x00000001 start code), ready to feed MediaCodec.
class FlvDemuxer {
public:
    using NaluCallback = std::function<void(const uint8_t*, size_t, int64_t)>;

    void SetNaluCallback(NaluCallback cb) { on_nalu_ = std::move(cb); }
    void Feed(const uint8_t* data, size_t size);
    void Reset();

private:
    enum State { kHeader, kSkip, kPrevTagSize, kTagHeader, kTagData };
    State state_ = kHeader;
    std::vector<uint8_t> buf_;
    size_t skip_ = 0;
    uint8_t tag_type_ = 0;
    uint32_t tag_size_ = 0;
    int64_t tag_pts_ = 0;
    int nal_length_size_ = 4;  // default, updated from sequence header

    NaluCallback on_nalu_;

    void Process();
    void HandleVideoTag(const uint8_t* data, size_t size, int64_t pts);
    void EmitSeqHeader(const uint8_t* data, size_t size);
    void EmitAvccNalus(const uint8_t* data, size_t size, int64_t pts);
    void EmitAnnexB(const uint8_t* nalu, size_t len, int64_t pts);
};
