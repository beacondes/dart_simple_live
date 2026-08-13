#include "flv_demuxer.h"

#include <android/log.h>

#define LOG_TAG "FlvDemuxer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static uint32_t ReadBE24(const uint8_t* p) {
    return (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | uint32_t(p[2]);
}
static uint32_t ReadBE32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

void FlvDemuxer::Reset() {
    state_ = kHeader;
    buf_.clear();
    skip_ = 0;
}

void FlvDemuxer::Feed(const uint8_t* data, size_t size) {
    if (size == 0) return;
    buf_.insert(buf_.end(), data, data + size);
    Process();
}

void FlvDemuxer::Process() {
    while (true) {
        switch (state_) {
            case kHeader: {
                if (buf_.size() < 9) return;
                bool ok = buf_[0] == 'F' && buf_[1] == 'L' && buf_[2] == 'V';
                if (!ok) { LOGE("not FLV"); buf_.erase(buf_.begin(), buf_.begin() + 4); state_ = kHeader; continue; }
                uint32_t hdrSize = ReadBE32(buf_.data() + 5);
                buf_.erase(buf_.begin(), buf_.begin() + 9);
                if (hdrSize > 9) { skip_ = hdrSize - 9; state_ = kSkip; }
                else { state_ = kPrevTagSize; }
                break;
            }
            case kSkip: {
                if (buf_.size() < skip_) return;
                buf_.erase(buf_.begin(), buf_.begin() + skip_);
                state_ = kPrevTagSize;
                break;
            }
            case kPrevTagSize: {
                if (buf_.size() < 4) return;
                buf_.erase(buf_.begin(), buf_.begin() + 4);
                state_ = kTagHeader;
                break;
            }
            case kTagHeader: {
                if (buf_.size() < 11) return;
                tag_type_ = buf_[0];
                tag_size_ = ReadBE24(buf_.data() + 1);
                uint32_t ts = ReadBE24(buf_.data() + 4) | (uint32_t(buf_[7]) << 24);
                tag_pts_ = int64_t(ts) * 1000;  // ms -> us
                buf_.erase(buf_.begin(), buf_.begin() + 11);
                state_ = kTagData;
                break;
            }
            case kTagData: {
                if (buf_.size() < tag_size_) return;
                if (tag_type_ == 9) HandleVideoTag(buf_.data(), tag_size_, tag_pts_);
                buf_.erase(buf_.begin(), buf_.begin() + tag_size_);
                state_ = kPrevTagSize;
                break;
            }
        }
    }
}

void FlvDemuxer::HandleVideoTag(const uint8_t* data, size_t size, int64_t pts) {
    if (size < 5) return;
    uint8_t codecId = data[0] & 0x0F;
    if (codecId != 7) return;  // AVC only
    uint8_t avcPacketType = data[1];
    int32_t cts = (int32_t(data[2]) << 16) | (int32_t(data[3]) << 8) | int32_t(data[4]);
    if (cts & 0x800000) cts -= 0x1000000;  // sign-extend 24-bit
    int64_t ptsUs = pts + int64_t(cts) * 1000;

    const uint8_t* payload = data + 5;
    size_t payloadSize = size - 5;
    if (avcPacketType == 0) EmitSeqHeader(payload, payloadSize);
    else if (avcPacketType == 1) EmitAvccNalus(payload, payloadSize, ptsUs);
}

void FlvDemuxer::EmitSeqHeader(const uint8_t* data, size_t size) {
    if (size < 7) return;
    nal_length_size_ = (data[4] & 0x03) + 1;
    int numSPS = data[5] & 0x1F;
    size_t off = 6;
    std::vector<uint8_t> sps, pps;
    for (int i = 0; i < numSPS && off + 2 <= size; i++) {
        uint16_t len = uint16_t((data[off] << 8) | data[off + 1]); off += 2;
        if (off + len > size) return;
        sps.assign(data + off, data + off + len); off += len;
    }
    if (off + 1 > size) return;
    int numPPS = data[off++];
    for (int i = 0; i < numPPS && off + 2 <= size; i++) {
        uint16_t len = uint16_t((data[off] << 8) | data[off + 1]); off += 2;
        if (off + len > size) return;
        pps.assign(data + off, data + off + len); off += len;
    }
    if (!sps.empty()) EmitAnnexB(sps.data(), sps.size(), 0);
    if (!pps.empty()) EmitAnnexB(pps.data(), pps.size(), 0);
}

void FlvDemuxer::EmitAvccNalus(const uint8_t* data, size_t size, int64_t pts) {
    size_t off = 0;
    while (off + (size_t)nal_length_size_ <= size) {
        uint32_t len = 0;
        for (int i = 0; i < nal_length_size_; i++) len = (len << 8) | data[off + i];
        off += nal_length_size_;
        if (len == 0 || off + len > size) break;
        EmitAnnexB(data + off, len, pts);
        off += len;
    }
}

void FlvDemuxer::EmitAnnexB(const uint8_t* nalu, size_t len, int64_t pts) {
    if (!on_nalu_ || len == 0) return;
    static const uint8_t kStart[4] = {0x00, 0x00, 0x00, 0x01};
    std::vector<uint8_t> out;
    out.reserve(len + 4);
    out.insert(out.end(), kStart, kStart + 4);
    out.insert(out.end(), nalu, nalu + len);
    on_nalu_(out.data(), out.size(), pts);
}
