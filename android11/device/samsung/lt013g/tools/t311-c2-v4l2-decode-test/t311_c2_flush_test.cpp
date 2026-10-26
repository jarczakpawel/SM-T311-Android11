#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <openssl/sha.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <string>
#include <vector>

struct Packet { size_t off = 0; size_t size = 0; uint32_t flags = 0; uint64_t pts = 0; };

static bool loadFile(const char* path, std::vector<uint8_t>* out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return false; }
    long n = ftell(f);
    if (n <= 0 || n > 32 * 1024 * 1024) { fclose(f); return false; }
    rewind(f);
    out->resize((size_t)n);
    bool ok = fread(out->data(), 1, out->size(), f) == out->size();
    fclose(f);
    return ok;
}

static bool loadIndex(const std::string& path, std::vector<Packet>* out) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    Packet p;
    while (fscanf(f, "%zu %zu %u %llu", &p.off, &p.size, &p.flags, (unsigned long long*)&p.pts) == 4) {
        out->push_back(p);
    }
    fclose(f);
    return !out->empty();
}

static bool loadHashes(const std::string& path, std::vector<std::string>* out) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (strlen(line) < 64) { fclose(f); return false; }
        out->emplace_back(line, 64);
    }
    fclose(f);
    return !out->empty();
}

static std::string hashFrame(const uint8_t* b, size_t n) {
    uint8_t digest[SHA256_DIGEST_LENGTH];
    SHA256(b, n, digest);
    char result[SHA256_DIGEST_LENGTH * 2 + 1];
    for (unsigned i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        snprintf(result + i * 2, 3, "%02x", digest[i]);
    return result;
}

struct Stage { const char* name; size_t first; size_t feed; size_t target; bool eos; };

static bool runStage(AMediaCodec* codec, const std::vector<uint8_t>& data,
                     const std::vector<Packet>& packets,
                     const std::vector<std::string>& hashes, Stage stage,
                     size_t frameBytes) {
    printf("T311_FLUSH_STAGE_BEGIN=%s start=%zu feed=%zu target=%zu eos=%d\n",
           stage.name, stage.first, stage.feed, stage.target, (int)stage.eos);
    size_t queued = 0, completed = 0;
    bool inputEOS = false, outputEOS = false;
    size_t changes = 0;
    auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(90);

    while (std::chrono::steady_clock::now() < limit) {
        if (!inputEOS && queued <= stage.feed) {
            ssize_t in = AMediaCodec_dequeueInputBuffer(codec, 10000);
            if (in >= 0) {
                size_t cap = 0;
                uint8_t* buf = AMediaCodec_getInputBuffer(codec, in, &cap);
                if (queued < stage.feed) {
                    const Packet& p = packets[stage.first + queued];
                    if (!buf || p.off > data.size() || p.size > data.size()-p.off || p.size > cap) {
                        printf("T311_FLUSH_PACKET_ERROR=%s at=%zu cap=%zu\n", stage.name, queued, cap);
                        return false;
                    }
                    memcpy(buf, data.data()+p.off, p.size);
                    uint64_t pts = (uint64_t)queued * 33333;
                    media_status_t s = AMediaCodec_queueInputBuffer(codec, in, 0, p.size, pts, 0);
                    if (s != AMEDIA_OK) {
                        printf("T311_FLUSH_INPUT_ERROR=%s status=%d\n", stage.name, s);
                        return false;
                    }
                    ++queued;
                } else if (stage.eos) {
                    media_status_t s = AMediaCodec_queueInputBuffer(codec, in, 0, 0,
                                    (uint64_t)queued * 33333, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    if (s != AMEDIA_OK) return false;
                    inputEOS = true;
                }
            }
        }
        AMediaCodecBufferInfo info{};
        ssize_t oi = AMediaCodec_dequeueOutputBuffer(codec, &info, 20000);
        if (oi >= 0) {
            if (info.size > 0) {
                size_t cap = 0;
                uint8_t* b = AMediaCodec_getOutputBuffer(codec, oi, &cap);
                if (!b || info.offset < 0 || (size_t)info.offset > cap ||
                    (size_t)info.size > cap-(size_t)info.offset ||
                    (size_t)info.size != frameBytes || completed >= stage.feed) {
                    printf("T311_FLUSH_FRAME_LAYOUT_FAIL=%s index=%zu size=%d cap=%zu\n",
                           stage.name, completed, info.size, cap);
                    AMediaCodec_releaseOutputBuffer(codec, oi, false);
                    return false;
                }
                const std::string digest = hashFrame(b+info.offset, (size_t)info.size);
                const std::string& expected = hashes[stage.first+completed];
                if (digest != expected) {
                    printf("T311_FLUSH_MISMATCH=%s frame=%zu actual=%s expected=%s\n",
                           stage.name, stage.first+completed, digest.c_str(), expected.c_str());
                    AMediaCodec_releaseOutputBuffer(codec, oi, false);
                    return false;
                }
                ++completed;
            }
            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) outputEOS = true;
            media_status_t release = AMediaCodec_releaseOutputBuffer(codec, oi, false);
            if (release != AMEDIA_OK) {
                printf("T311_FLUSH_RELEASE_FAIL=%s status=%d\n", stage.name, release);
                return false;
            }
        } else if (oi == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            ++changes;
            AMediaFormat* f = AMediaCodec_getOutputFormat(codec);
            printf("T311_FLUSH_FORMAT=%s %s\n", stage.name,
                   f ? AMediaFormat_toString(f) : "(null)");
            if (f) AMediaFormat_delete(f);
        } else if (oi != AMEDIACODEC_INFO_TRY_AGAIN_LATER &&
                   oi != AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
            printf("T311_FLUSH_UNEXPECTED_OUTPUT=%zd\n", oi);
            return false;
        }
        if (completed >= stage.target && (!stage.eos || outputEOS)) {
            if (stage.eos && (completed != stage.feed || !inputEOS)) {
                printf("T311_FLUSH_FRAMECOUNT_FAIL=%s completed=%zu queued=%zu\n",
                       stage.name, completed, queued);
                return false;
            }
            printf("T311_FLUSH_STAGE_PASS=%s verified=%zu queued=%zu format_changes=%zu output_eos=%d\n",
                   stage.name, completed, queued, changes, (int)outputEOS);
            return true;
        }
    }
    printf("T311_FLUSH_STAGE_TIMEOUT=%s verified=%zu queued=%zu\n",
           stage.name, completed, queued);
    return false;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 5) {
        printf("USAGE: %s width height /data/path/file.h264 /data/path/file.frames.sha256\n", argv[0]);
        return 2;
    }
    int w = atoi(argv[1]), h = atoi(argv[2]);
    if (w <= 0 || h <= 0 || w > 1920 || h > 1080) return 3;
    size_t frameBytes = (size_t)w * (size_t)h * 3 / 2;
    std::vector<uint8_t> data;
    std::vector<Packet> packets;
    std::vector<std::string> hashes;
    if (!loadFile(argv[3], &data) || !loadIndex(std::string(argv[3])+".idx", &packets) ||
        !loadHashes(argv[4], &hashes) || packets.size() != 180 || hashes.size() != 180) {
        printf("T311_FLUSH_INPUT=FAIL\n");
        return 4;
    }
    AMediaCodec* c = AMediaCodec_createCodecByName("c2.v4l2.avc.decoder");
    if (!c) return 5;
    AMediaFormat* f = AMediaFormat_new();
    AMediaFormat_setString(f, "mime", "video/avc");
    AMediaFormat_setInt32(f, "width", w);
    AMediaFormat_setInt32(f, "height", h);
    AMediaFormat_setInt32(f, "color-format", 19);
    media_status_t st = AMediaCodec_configure(c, f, nullptr, nullptr, 0);
    AMediaFormat_delete(f);
    if (st != AMEDIA_OK) { AMediaCodec_delete(c); return 6; }
    st = AMediaCodec_start(c);
    if (st != AMEDIA_OK) { AMediaCodec_delete(c); return 7; }
    printf("T311_FLUSH_CODEC=START_PASS component=c2.v4l2.avc.decoder\n");

    const Stage stages[] = {
        {"preflush_non_eos", 0, 45, 30, false},
        {"seek_idr_60_after_flush", 60, 30, 30, true},
        {"seek_idr_30_after_eos_flush", 30, 60, 60, true},
    };
    bool success = true;
    for (size_t i = 0; i < sizeof(stages)/sizeof(stages[0]); ++i) {
        if (i != 0) {
            st = AMediaCodec_flush(c);
            printf("T311_FLUSH_CALL=index_%zu status_%d\n", i, st);
            if (st != AMEDIA_OK) { success = false; break; }
        }
        if (!runStage(c, data, packets, hashes, stages[i], frameBytes)) {
            success = false;
            break;
        }
    }
    st = AMediaCodec_stop(c);
    printf("T311_FLUSH_CODEC_STOP=%d\n", st);
    AMediaCodec_delete(c);
    printf("T311_FLUSH_FINAL=%s\n", success ? "PASS" : "FAIL");
    return success ? 0 : 20;
}
