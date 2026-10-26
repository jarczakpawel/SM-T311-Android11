#include <stdbool.h>
#include <aaudio/AAudio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* A bounded, silent playback-path test. No audio is captured or saved. */
int main(int argc, char **argv) {
    int rate = argc > 1 ? atoi(argv[1]) : 48000;
    int channels = argc > 2 ? atoi(argv[2]) : 2;
    if ((rate != 44100 && rate != 48000) || (channels != 1 && channels != 2)) {
        fprintf(stderr, "USAGE: t311-aaudio-output-probe [44100|48000] [1|2]\n");
        return 4;
    }

    AAudioStreamBuilder *builder = NULL;
    AAudioStream *stream = NULL;
    aaudio_result_t result = AAudio_createStreamBuilder(&builder);
    if (result != AAUDIO_OK) {
        fprintf(stderr, "BUILD_ERROR=%s\n", AAudio_convertResultToText(result));
        return 1;
    }
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSampleRate(builder, rate);
    AAudioStreamBuilder_setChannelCount(builder, channels);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    result = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (result != AAUDIO_OK) {
        fprintf(stderr, "OPEN_ERROR=%s\n", AAudio_convertResultToText(result));
        return 2;
    }
    printf("OUTPUT_OPEN requestedRate=%d actualRate=%d requestedChannels=%d actualChannels=%d format=%d burst=%d\n",
           rate, AAudioStream_getSampleRate(stream), channels,
           AAudioStream_getChannelCount(stream), AAudioStream_getFormat(stream),
           AAudioStream_getFramesPerBurst(stream));

    result = AAudioStream_requestStart(stream);
    if (result != AAUDIO_OK) {
        fprintf(stderr, "START_ERROR=%s\n", AAudio_convertResultToText(result));
        AAudioStream_close(stream);
        return 3;
    }

    int16_t silence[1024 * 2] = {0};
    int sent = 0;
    for (int i = 0; i < 8; ++i) {
        result = AAudioStream_write(stream, silence, 1024, 2000000000LL);
        if (result != 1024) {
            fprintf(stderr, "WRITE_ERROR index=%d result=%d (%s)\n",
                    i, result, AAudio_convertResultToText(result));
            break;
        }
        sent += result;
    }
    printf("OUTPUT_RESULT framesSent=%d framesWritten=%lld xruns=%d\n",
           sent, (long long)AAudioStream_getFramesWritten(stream),
           AAudioStream_getXRunCount(stream));
    usleep(100000);
    AAudioStream_requestStop(stream);
    AAudioStream_close(stream);
    return sent == 8192 ? 0 : 5;
}
