#ifndef __REQUEST_H__
enum SchedulingAlgo {BLOCK, DT, DH, BF, DYNAMIC, RANDOM };

typedef struct requestStats {
    int fd;
    struct timeval t_arrive;
    struct timeval t_dispatch;
} RequestStats;

typedef struct threadStats {
    int thread_id;
    int requests;
    int staticRequests;
    int dynamicRequests;
} ThreadStats;

void requestHandle(int fd, RequestStats request, ThreadStats* stats);
#endif
