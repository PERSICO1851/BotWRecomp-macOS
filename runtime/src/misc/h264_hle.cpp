// Minimal H.264 helpers needed by BotW's video player.
#include <cstdint>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <vector>

#include "../hle.h"
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>

namespace {
constexpr uint32_t kH264Success = 0;
constexpr uint32_t kH264BadStream = 0x01000000;
constexpr uint32_t kH264InvalidParam = 0x01010000;

struct H264HostState {
    std::vector<uint8_t> sps;
    std::vector<uint8_t> pps;
    CMVideoFormatDescriptionRef format{};
    VTDecompressionSessionRef session{};
    uint32_t width{}, height{};
    uint64_t frameNumber{};
};
H264HostState g_decoder;
std::mutex g_decoderMutex;

struct DecodedFrame {
    std::mutex mutex;
    std::condition_variable ready;
    CVPixelBufferRef pixelBuffer{};
    OSStatus status{ kVTInvalidSessionErr };
    bool done{};
};

void decoded_frame_callback(void*, void* sourceFrameRefCon, OSStatus status, VTDecodeInfoFlags,
                            CVImageBufferRef imageBuffer, CMTime, CMTime) {
    auto* result = static_cast<DecodedFrame*>(sourceFrameRefCon);
    if (!result) {
        LOG("h264: VT callback received null source frame context");
        return;
    }
    LOG("h264: VT callback context=%p status=%d image=%p", result, (int)status, imageBuffer);
    std::lock_guard lock(result->mutex);
    result->status = status;
    if (imageBuffer) {
        result->pixelBuffer = (CVPixelBufferRef)imageBuffer;
        CVPixelBufferRetain(result->pixelBuffer);
    }
    result->done = true;
    result->ready.notify_one();
}

size_t find_start_code(const uint8_t* data, size_t size, size_t from, size_t& prefix) {
    for (size_t i = from; i + 3 < size; ++i) {
        if (data[i] || data[i + 1]) continue;
        if (data[i + 2] == 1) { prefix = 3; return i; }
        if (i + 4 <= size && data[i + 2] == 0 && data[i + 3] == 1) { prefix = 4; return i; }
    }
    return size;
}

void capture_parameter_sets(const uint8_t* data, size_t size) {
    size_t prefix = 0;
    size_t start = find_start_code(data, size, 0, prefix);
    while (start < size) {
        const size_t nalStart = start + prefix;
        size_t nextPrefix = 0;
        const size_t next = find_start_code(data, size, nalStart, nextPrefix);
        const size_t nalEnd = next < size ? next : size;
        if (nalStart < nalEnd) {
            const uint8_t type = data[nalStart] & 0x1f;
            if (type == 7) g_decoder.sps.assign(data + nalStart, data + nalEnd);
            else if (type == 8) g_decoder.pps.assign(data + nalStart, data + nalEnd);
        }
        start = next;
        prefix = nextPrefix;
    }
}

bool ensure_decoder() {
    if (g_decoder.session) return true;
    if (g_decoder.sps.empty() || g_decoder.pps.empty()) {
        LOG("h264: missing SPS/PPS (%zu / %zu bytes)", g_decoder.sps.size(), g_decoder.pps.size());
        return false;
    }
    const uint8_t* parameterSets[] = { g_decoder.sps.data(), g_decoder.pps.data() };
    const size_t parameterSizes[] = { g_decoder.sps.size(), g_decoder.pps.size() };
    OSStatus status = CMVideoFormatDescriptionCreateFromH264ParameterSets(
        kCFAllocatorDefault, 2, parameterSets, parameterSizes, 4, &g_decoder.format);
    if (status != noErr) { LOG("h264: format description failed (%d)", (int)status); return false; }
    CMVideoDimensions dimensions = CMVideoFormatDescriptionGetDimensions(g_decoder.format);
    g_decoder.width = static_cast<uint32_t>(dimensions.width);
    g_decoder.height = static_cast<uint32_t>(dimensions.height);
    if (!g_decoder.width || !g_decoder.height || g_decoder.width > 2800 || g_decoder.height > 1408) {
        LOG("h264: invalid decoded dimensions %ux%u", g_decoder.width, g_decoder.height);
        return false;
    }

    VTDecompressionOutputCallbackRecord callback{ decoded_frame_callback, nullptr };
    CFMutableDictionaryRef attributes = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    int pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    CFNumberRef pixelFormatValue = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pixelFormat);
    CFDictionarySetValue(attributes, kCVPixelBufferPixelFormatTypeKey, pixelFormatValue);
    int pixelWidth = dimensions.width, pixelHeight = dimensions.height;
    CFNumberRef widthValue = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pixelWidth);
    CFNumberRef heightValue = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pixelHeight);
    CFDictionarySetValue(attributes, kCVPixelBufferWidthKey, widthValue);
    CFDictionarySetValue(attributes, kCVPixelBufferHeightKey, heightValue);
    status = VTDecompressionSessionCreate(kCFAllocatorDefault, g_decoder.format, nullptr, attributes,
                                         &callback, &g_decoder.session);
    CFRelease(pixelFormatValue);
    CFRelease(widthValue);
    CFRelease(heightValue);
    CFRelease(attributes);
    LOG("h264: VideoToolbox session create=%d size=%ux%u", (int)status, g_decoder.width, g_decoder.height);
    return status == noErr;
}

bool decode_access_unit(const uint8_t* data, size_t size, CVPixelBufferRef& output) {
    if (!ensure_decoder()) return false;
    std::vector<uint8_t> avcc;
    size_t prefix = 0;
    size_t start = find_start_code(data, size, 0, prefix);
    while (start < size) {
        const size_t nalStart = start + prefix;
        size_t nextPrefix = 0;
        const size_t next = find_start_code(data, size, nalStart, nextPrefix);
        const size_t nalEnd = next < size ? next : size;
        if (nalStart < nalEnd) {
            const uint8_t type = data[nalStart] & 0x1f;
            if (type != 7 && type != 8 && type != 9) {
                const uint32_t length = static_cast<uint32_t>(nalEnd - nalStart);
                avcc.push_back(length >> 24); avcc.push_back(length >> 16);
                avcc.push_back(length >> 8); avcc.push_back(length);
                avcc.insert(avcc.end(), data + nalStart, data + nalEnd);
            }
        }
        start = next;
        prefix = nextPrefix;
    }
    if (avcc.empty()) return false;

    CMBlockBufferRef block = nullptr;
    OSStatus status = CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, avcc.data(), avcc.size(),
        kCFAllocatorNull, nullptr, 0, avcc.size(), 0, &block);
    if (status != noErr) { LOG("h264: block buffer create failed (%d)", (int)status); return false; }
    const size_t sampleSize = avcc.size();
    CMSampleBufferRef sample = nullptr;
    CMSampleTimingInfo timing = { CMTimeMake(1, 30), CMTimeMake(static_cast<int64_t>(g_decoder.frameNumber), 30), kCMTimeInvalid };
    status = CMSampleBufferCreateReady(kCFAllocatorDefault, block, g_decoder.format, 1, 1, &timing, 1,
                                       &sampleSize, &sample);
    CFRelease(block);
    if (status != noErr) { LOG("h264: sample buffer create failed (%d)", (int)status); return false; }
    LOG("h264: sample ready sample=%p bytes=%zu", sample, avcc.size());

    DecodedFrame result;
    VTDecodeInfoFlags info = 0;
    ++g_decoder.frameNumber;
    LOG("h264: submit decode session=%p sample=%p context=%p", g_decoder.session, sample, &result);
    status = VTDecompressionSessionDecodeFrame(g_decoder.session, sample,
        kVTDecodeFrame_EnableAsynchronousDecompression, &result, &info);
    CFRelease(sample);
    if (status != noErr) { LOG("h264: decode submit failed (%d)", (int)status); return false; }
    LOG("h264: decode submitted info=%u", (unsigned)info);
    status = VTDecompressionSessionWaitForAsynchronousFrames(g_decoder.session);
    LOG("h264: async decode wait status=%d", (int)status);
    {
        std::unique_lock lock(result.mutex);
        result.ready.wait_for(lock, std::chrono::seconds(2), [&] { return result.done; });
    }
    if (result.status == noErr && result.pixelBuffer) output = result.pixelBuffer;
    if (!output) LOG("h264: decode completed without pixel buffer status=%d", (int)result.status);
    return output != nullptr;
}

bool copy_frame_to_guest(CVPixelBufferRef pixelBuffer, uint32_t output) {
    if (!pixelBuffer || !output) return false;
    CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    const size_t width = CVPixelBufferGetWidth(pixelBuffer);
    const size_t height = CVPixelBufferGetHeight(pixelBuffer);
    uint8_t* dst = mem::host(output);
    if (CVPixelBufferIsPlanar(pixelBuffer) && CVPixelBufferGetPlaneCount(pixelBuffer) >= 2) {
        const uint8_t* y = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0));
        const uint8_t* uv = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 1));
        const size_t yStride = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 0);
        const size_t uvStride = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 1);
        if (!y || !uv || yStride < width || uvStride < ((width + 1) / 2) * 2) {
            CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
            LOG("h264: decoded pixel planes unavailable");
            return false;
        }
        const size_t chromaWidth = (width + 1) / 2, chromaHeight = (height + 1) / 2;
        for (size_t row = 0; row < height; ++row) memcpy(dst + row * width, y + row * yStride, width);
        uint8_t* u = dst + width * height;
        uint8_t* v = u + chromaWidth * chromaHeight;
        for (size_t row = 0; row < chromaHeight; ++row) {
            for (size_t col = 0; col < chromaWidth; ++col) {
                u[row * chromaWidth + col] = uv[row * uvStride + col * 2];
                v[row * chromaWidth + col] = uv[row * uvStride + col * 2 + 1];
            }
        }
    } else {
        CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
        return false;
    }
    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    return true;
}

void report_decoded_frame(PPCContext* context, uint32_t workMemory, uint32_t imageOutput) {
    const uint32_t callback = mem::r32(workMemory + 16);
    if (!callback) return;
    const uint32_t frame = mem::sys_alloc(120, 16);
    const uint32_t resultArray = mem::sys_alloc(4, 4);
    const uint32_t outputArgs = mem::sys_alloc(12, 4);
    if (!frame || !resultArray || !outputArgs) return;
    memset(mem::host(frame), 0, 120);
    mem::w32(frame + 0, 100);
    mem::w32(frame + 16, g_decoder.width);
    mem::w32(frame + 20, g_decoder.height);
    mem::w32(frame + 24, g_decoder.width);
    mem::w32(frame + 68, imageOutput);
    mem::w32(resultArray, frame);
    mem::w32(outputArgs + 0, 1);
    mem::w32(outputArgs + 4, resultArray);
    mem::w32(outputArgs + 8, mem::r32(workMemory + 24));
    LOG("h264: invoke output callback %08x, frame %08x, output %08x", callback, frame, imageOutput);
    guest_call(context, callback, {outputArgs});
    LOG("h264: output callback returned");
    mem::sys_free(outputArgs);
    mem::sys_free(resultArray);
    mem::sys_free(frame);
}

class BitReader {
public:
    explicit BitReader(const std::vector<uint8_t>& bytes) : bytes_(bytes) {}

    bool bits(unsigned count, uint32_t& value) {
        if (count > 32 || bit_ + count > bytes_.size() * 8)
            return false;
        value = 0;
        for (unsigned i = 0; i < count; ++i) {
            value = (value << 1) | ((bytes_[bit_ / 8] >> (7 - bit_ % 8)) & 1);
            ++bit_;
        }
        return true;
    }

    bool ue(uint32_t& value) {
        unsigned zeros = 0;
        uint32_t bit = 0;
        while (true) {
            if (!bits(1, bit)) return false;
            if (bit) break;
            if (++zeros > 31) return false;
        }
        uint32_t suffix = 0;
        if (zeros && !bits(zeros, suffix)) return false;
        value = ((uint32_t{1} << zeros) - 1) + suffix;
        return true;
    }

    bool se(int32_t& value) {
        uint32_t code = 0;
        if (!ue(code)) return false;
        value = (code & 1) ? static_cast<int32_t>((code + 1) / 2)
                           : -static_cast<int32_t>(code / 2);
        return true;
    }

private:
    const std::vector<uint8_t>& bytes_;
    size_t bit_{};
};

bool skip_scaling_list(BitReader& reader, unsigned count) {
    int32_t last = 8, next = 8;
    for (unsigned i = 0; i < count; ++i) {
        if (next != 0) {
            int32_t delta = 0;
            if (!reader.se(delta)) return false;
            next = (last + delta + 256) % 256;
        }
        if (next != 0) last = next;
    }
    return true;
}

bool parse_sps(const uint8_t* nal, size_t length, uint32_t& width, uint32_t& height) {
    if (length < 4 || (nal[0] & 0x1f) != 7) return false;
    std::vector<uint8_t> rbsp;
    rbsp.reserve(length);
    unsigned zeros = 0;
    for (size_t i = 1; i < length; ++i) {
        uint8_t byte = nal[i];
        if (zeros >= 2 && byte == 3) { zeros = 0; continue; }
        rbsp.push_back(byte);
        zeros = byte == 0 ? zeros + 1 : 0;
    }
    BitReader r(rbsp);
    uint32_t profile, tmp, spsId;
    if (!r.bits(8, profile) || !r.bits(16, tmp) || !r.ue(spsId) || spsId >= 32)
        return false;

    if (profile == 100) {
        uint32_t chroma, lumaDepth, chromaDepth, scaling;
        if (!r.ue(chroma) || chroma > 1 || !r.ue(lumaDepth) || lumaDepth > 6 ||
            !r.ue(chromaDepth) || chromaDepth > 6 || !r.bits(1, tmp) || !r.bits(1, scaling))
            return false;
        if (scaling) {
            for (unsigned i = 0; i < 8; ++i) {
                uint32_t present;
                if (!r.bits(1, present)) return false;
                if (present && !skip_scaling_list(r, i < 6 ? 16 : 64)) return false;
            }
        }
    } else if (profile != 66 && profile != 77) {
        return false;
    }

    uint32_t log2Frame, pocType;
    if (!r.ue(log2Frame) || log2Frame > 12 || !r.ue(pocType) || pocType > 2)
        return false;
    if (pocType == 0) {
        uint32_t v;
        if (!r.ue(v) || v > 12) return false;
    } else if (pocType == 1) {
        uint32_t flag, cycle;
        int32_t sv;
        if (!r.bits(1, flag) || !r.se(sv) || !r.se(sv) || !r.ue(cycle) || cycle > 255)
            return false;
        for (uint32_t i = 0; i < cycle; ++i) if (!r.se(sv)) return false;
    }
    uint32_t refs, gaps, widthMbs, heightMap, frameOnly;
    if (!r.ue(refs) || refs > 16 || !r.bits(1, gaps) || !r.ue(widthMbs) || widthMbs > 255 ||
        !r.ue(heightMap) || heightMap > 255 || !r.bits(1, frameOnly))
        return false;
    if (!frameOnly && !r.bits(1, tmp)) return false;
    if (!r.bits(1, tmp)) return false;
    uint32_t cropping;
    if (!r.bits(1, cropping)) return false;
    if (cropping) {
        uint32_t crop;
        for (int i = 0; i < 4; ++i) if (!r.ue(crop)) return false;
    }
    width = (widthMbs + 1) * 16;
    height = (heightMap + 1) * 16 * (frameOnly ? 1 : 2);
    if (width == 1920 && height == 1088) height = 1080;
    return width && height;
}
}  // namespace

HLE_EXPORT(h264, H264DECGetImageSize) {
    const uint32_t stream = ARG(0);
    const int32_t size = static_cast<int32_t>(ARG(1));
    const int32_t offset = static_cast<int32_t>(ARG(2));
    const uint32_t widthOut = ARG(3);
    const uint32_t heightOut = ARG(4);
    if (!stream || size < 4 || offset < 0 || offset >= size || !widthOut || !heightOut) {
        RET(kH264InvalidParam);
        return;
    }

    const auto* data = mem::host(stream);
    {
        std::lock_guard lock(g_decoderMutex);
        capture_parameter_sets(data, static_cast<size_t>(size));
    }
    const size_t begin = static_cast<size_t>(offset);
    for (size_t i = begin; i + 5 <= static_cast<size_t>(size); ++i) {
        if (data[i] != 0 || data[i + 1] != 0 || data[i + 2] != 1 || (data[i + 3] & 0x1f) != 7)
            continue;
        uint32_t width = 0, height = 0;
        if (parse_sps(data + i + 3, static_cast<size_t>(size) - (i + 3), width, height)) {
            mem::w32(widthOut, width);
            mem::w32(heightOut, height);
            RET(kH264Success);
            return;
        }
    }
    RET(kH264BadStream);
}

// BotW requests an H.264 work buffer before opening a decoder. Cemu uses a
// 256-byte context for BotW because the game only needs the API's small state
// block here; decoding is handled separately by a backend.
HLE_EXPORT(h264, H264DECMemoryRequirement) {
    const uint32_t output = ARG(4);
    if (!output) {
        RET(kH264InvalidParam);
        return;
    }
    mem::w32(output, 256);
    RET(kH264Success);
}

HLE_EXPORT(h264, H264DECCheckMemSegmentation) { RET(0); }

HLE_EXPORT(h264, H264DECInitParam) {
    const uint32_t workMemory = ARG(1);
    if (workMemory) memset(mem::host(workMemory), 0, 256);
    RET(0);
}

// BotW's decoder context layout follows the Cafe H.264 API. This lightweight
// context implementation lets the game handle absent video frames gracefully;
// it does not decode H.264 image data.
HLE_EXPORT(h264, H264DECOpen) {
    if (ARG(0)) mem::w32(ARG(0) + 28, 1);
    RET(0);
}
HLE_EXPORT(h264, H264DECClose) {
    std::lock_guard lock(g_decoderMutex);
    if (g_decoder.session) {
        VTDecompressionSessionInvalidate(g_decoder.session);
        CFRelease(g_decoder.session);
        g_decoder.session = nullptr;
    }
    if (g_decoder.format) {
        CFRelease(g_decoder.format);
        g_decoder.format = nullptr;
    }
    g_decoder.sps.clear();
    g_decoder.pps.clear();
    RET(0);
}
HLE_EXPORT(h264, H264DECBegin) { RET(0); }
HLE_EXPORT(h264, H264DECEnd) { RET(kH264Success); }
HLE_EXPORT(h264, H264DECSetParam_FPTR_OUTPUT) {
    if (ARG(0)) mem::w32(ARG(0) + 16, ARG(1));
    RET(kH264Success);
}
HLE_EXPORT(h264, H264DECSetParam_OUTPUT_PER_FRAME) {
    if (ARG(0)) mem::w8(ARG(0) + 20, ARG(1) != 0);
    RET(kH264Success);
}
HLE_EXPORT(h264, H264DECSetParam_USER_MEMORY) {
    if (ARG(0) && ARG(1)) mem::w32(ARG(0) + 24, mem::r32(ARG(1)));
    RET(kH264Success);
}
HLE_EXPORT(h264, H264DECSetBitstream) {
    if (ARG(0)) {
        mem::w32(ARG(0), ARG(1));
        mem::w32(ARG(0) + 4, ARG(2));
        if (ARG(1) && ARG(2)) {
            std::lock_guard lock(g_decoderMutex);
            capture_parameter_sets(mem::host(ARG(1)), std::min<uint32_t>(ARG(2), 8 * 1024 * 1024));
        }
    }
    RET(0);
}
HLE_EXPORT(h264, H264DECExecute) {
    const uint32_t workMemory = ARG(0);
    const uint32_t imageOutput = ARG(1);
    if (!workMemory) { RET(kH264InvalidParam); return; }
    const uint32_t stream = mem::r32(workMemory);
    const uint32_t size = mem::r32(workMemory + 4);
    if (!stream || size < 4 || size > 8 * 1024 * 1024) { RET(0x400); return; }

    CVPixelBufferRef pixelBuffer = nullptr;
    bool decoded = false;
    {
        std::lock_guard lock(g_decoderMutex);
        capture_parameter_sets(mem::host(stream), size);
        decoded = decode_access_unit(mem::host(stream), size, pixelBuffer);
    }
    if (!decoded || !copy_frame_to_guest(pixelBuffer, imageOutput)) {
        LOG("h264: frame decode/output failed (stream=%08x, size=%u, image=%08x)", stream, size, imageOutput);
        if (pixelBuffer) CVPixelBufferRelease(pixelBuffer);
        RET(0x400);
        return;
    }
    report_decoded_frame(c, workMemory, imageOutput);
    CVPixelBufferRelease(pixelBuffer);
    RET(0x80 | 100);
}
