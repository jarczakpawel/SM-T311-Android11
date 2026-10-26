#ifndef T311_RIL_AUDIO_H
#define T311_RIL_AUDIO_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

typedef void (*t311_wb_callback)(void *data, int enabled);

struct t311_ril_audio {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pthread_t thread;
    int fd;
    uint32_t generation;
    bool stop;
    bool thread_started;
    t311_wb_callback wb_callback;
    void *wb_data;
};

int t311_ril_audio_init(struct t311_ril_audio *ril, t311_wb_callback callback, void *data);
void t311_ril_audio_close(struct t311_ril_audio *ril);
int t311_ril_audio_set_path(struct t311_ril_audio *ril, int path);
int t311_ril_audio_set_clock(struct t311_ril_audio *ril, bool start);
int t311_ril_audio_set_volume(struct t311_ril_audio *ril, int type, int level);
int t311_ril_audio_set_mute(struct t311_ril_audio *ril, bool mute);

#endif
