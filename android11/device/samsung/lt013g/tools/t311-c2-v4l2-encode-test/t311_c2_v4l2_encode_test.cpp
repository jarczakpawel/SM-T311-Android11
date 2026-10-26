#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <string>
#include <vector>

static void fill_i420(uint8_t* dst, int w, int h, int frame) {
    const int ysz = w * h;
    const int csz = ysz / 4;
    uint8_t* y = dst;
    uint8_t* u = dst + ysz;
    uint8_t* v = u + csz;
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            y[row * w + col] = static_cast<uint8_t>(16 + ((col + row + frame * 3) % 220));
        }
    }
    for (int row = 0; row < h / 2; ++row) {
        for (int col = 0; col < w / 2; ++col) {
            const int i = row * (w / 2) + col;
            u[i] = static_cast<uint8_t>(96 + ((col + frame * 2) % 64));
            v[i] = static_cast<uint8_t>(96 + ((row + frame * 5) % 64));
        }
    }
}

// H27_PACKET_INDEX: sidecar .idx preserves output access units for direct hardware decode validation.
static int encode(const char* name, int w, int h, int frames, int fps, int bitrate,
                  const char* out_path) {
    std::printf("T311_C2_BEGIN codec=%s size=%dx%d frames=%d fps=%d bitrate=%d\n",
                name, w, h, frames, fps, bitrate);
    AMediaCodec* codec = AMediaCodec_createCodecByName(name);
    if (!codec) {
        std::printf("T311_C2_CREATE=FAIL codec=%s\n", name);
        return 10;
    }
    std::printf("T311_C2_CREATE=PASS codec=%s\n", name);

    AMediaFormat* fmt = AMediaFormat_new();
    AMediaFormat_setString(fmt, "mime", "video/avc");
    AMediaFormat_setInt32(fmt, "width", w);
    AMediaFormat_setInt32(fmt, "height", h);
    AMediaFormat_setInt32(fmt, "bitrate", bitrate);
    AMediaFormat_setInt32(fmt, "frame-rate", fps);
    AMediaFormat_setInt32(fmt, "i-frame-interval", 1);
    AMediaFormat_setInt32(fmt, "color-format", 19);  // COLOR_FormatYUV420Planar
    AMediaFormat_setInt32(fmt, "bitrate-mode", 2);   // CBR
    AMediaFormat_setInt32(fmt, "profile", 1);        // AVC Baseline

    const media_status_t cr = AMediaCodec_configure(
            codec, fmt, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    if (cr != AMEDIA_OK) {
        std::printf("T311_C2_CONFIGURE=FAIL status=%d format=%s\n", cr,
                    AMediaFormat_toString(fmt));
        AMediaFormat_delete(fmt);
        AMediaCodec_delete(codec);
        return 11;
    }
    std::printf("T311_C2_CONFIGURE=PASS format=%s\n", AMediaFormat_toString(fmt));
    AMediaFormat_delete(fmt);

    const media_status_t sr = AMediaCodec_start(codec);
    if (sr != AMEDIA_OK) {
        std::printf("T311_C2_START=FAIL status=%d\n", sr);
        AMediaCodec_delete(codec);
        return 12;
    }
    std::printf("T311_C2_START=PASS\n");

    FILE* out = std::fopen(out_path, "wb");
    std::string idx_path = std::string(out_path) + ".idx";
    FILE* idx = std::fopen(idx_path.c_str(), "w");
    if (!out || !idx) {
        std::printf("T311_C2_OUT_OPEN=FAIL path=%s idx=%s\n", out_path, idx_path.c_str());
        if (out) std::fclose(out);
        if (idx) std::fclose(idx);
        AMediaCodec_stop(codec);
        AMediaCodec_delete(codec);
        return 13;
    }
    size_t stream_offset = 0;

    const size_t frame_size = static_cast<size_t>(w) * h * 3 / 2;
    int input_frame = 0;
    bool input_eos = false;
    bool output_eos = false;
    int output_buffers = 0;
    int output_payload_buffers = 0;
    size_t output_bytes = 0;
    size_t codec_config_bytes = 0;
    const auto begin = std::chrono::steady_clock::now();
    const auto deadline = begin + std::chrono::seconds(45);

    while (!output_eos && std::chrono::steady_clock::now() < deadline) {
        if (!input_eos) {
            const ssize_t in_index = AMediaCodec_dequeueInputBuffer(codec, 10000);
            if (in_index >= 0) {
                size_t cap = 0;
                uint8_t* buf = AMediaCodec_getInputBuffer(codec, static_cast<size_t>(in_index), &cap);
                if (!buf) {
                    std::printf("T311_C2_INPUT_BUFFER=FAIL index=%zd null=1\n", in_index);
                    std::fclose(out);
                    std::fclose(idx);
                    AMediaCodec_stop(codec);
                    AMediaCodec_delete(codec);
                    return 14;
                }
                if (input_frame < frames) {
                    if (cap < frame_size) {
                        std::printf("T311_C2_INPUT_CAPACITY=FAIL cap=%zu need=%zu\n", cap, frame_size);
                        std::fclose(out);
                        AMediaCodec_stop(codec);
                        AMediaCodec_delete(codec);
                        return 15;
                    }
                    fill_i420(buf, w, h, input_frame);
                    const uint64_t pts = static_cast<uint64_t>(input_frame) * 1000000ULL / fps;
                    const media_status_t qr = AMediaCodec_queueInputBuffer(
                            codec, static_cast<size_t>(in_index), 0, frame_size, pts, 0);
                    if (qr != AMEDIA_OK) {
                        std::printf("T311_C2_QUEUE_INPUT=FAIL frame=%d status=%d\n", input_frame, qr);
                        std::fclose(out);
                        AMediaCodec_stop(codec);
                        AMediaCodec_delete(codec);
                        return 16;
                    }
                    ++input_frame;
                    // Keep the encoder active long enough for the host script to inspect /proc/<pid>/fd.
                    std::this_thread::sleep_for(std::chrono::microseconds(1000000 / fps));
                } else {
                    const uint64_t pts = static_cast<uint64_t>(input_frame) * 1000000ULL / fps;
                    const media_status_t qr = AMediaCodec_queueInputBuffer(
                            codec, static_cast<size_t>(in_index), 0, 0, pts,
                            AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    if (qr != AMEDIA_OK) {
                        std::printf("T311_C2_QUEUE_EOS=FAIL status=%d\n", qr);
                        std::fclose(out);
                        AMediaCodec_stop(codec);
                        AMediaCodec_delete(codec);
                        return 17;
                    }
                    input_eos = true;
                    std::printf("T311_C2_INPUT_EOS=PASS frames=%d\n", input_frame);
                }
            }
        }

        AMediaCodecBufferInfo info{};
        const ssize_t out_index = AMediaCodec_dequeueOutputBuffer(codec, &info, 10000);
        if (out_index >= 0) {
            ++output_buffers;
            size_t cap = 0;
            uint8_t* data = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(out_index), &cap);
            if (data && info.size > 0 && info.offset >= 0 &&
                static_cast<size_t>(info.offset) + static_cast<size_t>(info.size) <= cap) {
                const size_t chunk = static_cast<size_t>(info.size);
                std::fwrite(data + info.offset, 1, chunk, out);
                std::fprintf(idx, "%zu %zu %u %" PRIu64 "\n", stream_offset, chunk,
                             static_cast<unsigned>(info.flags), static_cast<uint64_t>(info.presentationTimeUs));
                stream_offset += chunk;
                output_bytes += chunk;
                if (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) {
                    codec_config_bytes += static_cast<size_t>(info.size);
                } else {
                    ++output_payload_buffers;
                }
            }
            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                output_eos = true;
            }
            AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(out_index), false);
        } else if (out_index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat* of = AMediaCodec_getOutputFormat(codec);
            std::printf("T311_C2_OUTPUT_FORMAT=%s\n", of ? AMediaFormat_toString(of) : "(null)");
            if (of) AMediaFormat_delete(of);
        }
    }

    std::fflush(out);
    std::fflush(idx);
    std::fclose(out);
    std::fclose(idx);
    const media_status_t stop = AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);

    std::printf("T311_C2_STATS size=%dx%d input_frames=%d output_buffers=%d payload_buffers=%d bytes=%zu config_bytes=%zu eos=%d stop=%d\n",
                w, h, input_frame, output_buffers, output_payload_buffers, output_bytes,
                codec_config_bytes, output_eos ? 1 : 0, stop);

    if (!output_eos || input_frame != frames || output_payload_buffers < 1 || output_bytes < 128) {
        std::printf("T311_C2_RESULT=FAIL size=%dx%d\n", w, h);
        return 20;
    }
    std::printf("T311_C2_RESULT=PASS size=%dx%d path=%s bytes=%zu\n",
                w, h, out_path, output_bytes);
    return 0;
}

int main(int argc, char** argv) {
    // H26J_UNBUFFERED_STDIO
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::printf("T311_C2_MAIN=ENTER argc=%d\n", argc);
    if (argc != 7) {
        std::fprintf(stderr, "usage: %s WIDTH HEIGHT FRAMES FPS BITRATE OUT.h264\n", argv[0]);
        return 2;
    }
    const int w = std::atoi(argv[1]);
    const int h = std::atoi(argv[2]);
    const int frames = std::atoi(argv[3]);
    const int fps = std::atoi(argv[4]);
    const int bitrate = std::atoi(argv[5]);
    if (w <= 0 || h <= 0 || frames <= 0 || fps <= 0 || bitrate <= 0 || (w & 1) || (h & 1)) {
        return 3;
    }
    return encode("c2.v4l2.avc.encoder", w, h, frames, fps, bitrate, argv[6]);
}
