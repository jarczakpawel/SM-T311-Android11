#include <stdbool.h>
#include <aaudio/AAudio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    int rate = argc > 1 ? atoi(argv[1]) : 48000;
    AAudioStreamBuilder *builder = NULL;
    AAudioStream *stream = NULL;
    aaudio_result_t result = AAudio_createStreamBuilder(&builder);
    if (result != AAUDIO_OK)
        return 1;
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
    AAudioStreamBuilder_setSampleRate(builder, rate);
    AAudioStreamBuilder_setChannelCount(builder, 1);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    result = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (result != AAUDIO_OK) {
        printf("OPEN_ERROR=%s\n", AAudio_convertResultToText(result));
        return 2;
    }
    printf("OPENED requested=%d actual=%d channels=%d format=%d\n",
           rate, AAudioStream_getSampleRate(stream),
           AAudioStream_getChannelCount(stream), AAudioStream_getFormat(stream));
    result = AAudioStream_requestStart(stream);
    if (result == AAUDIO_OK) {
        int16_t samples[1024];
        result = AAudioStream_read(stream, samples, 1024, 2000000000LL);
        printf("READ_RESULT=%d\n", result);
        AAudioStream_requestStop(stream);
    } else {
        printf("START_ERROR=%s\n", AAudio_convertResultToText(result));
    }
    AAudioStream_close(stream);
    return result == 1024 ? 0 : 3;
}
