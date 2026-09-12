#include "platform/complete.h"

#include <stdlib.h>
#include <string.h>

typedef struct PlatformCompletion {
    PlatformCompletionFn fn;
    void* arg;
} PlatformCompletion;

static PlatformCompletion* queue;
static size_t queue_count;
static size_t queue_capacity;
static int pumping;

void platform_complete_init(void)
{
    free(queue);
    queue = NULL;
    queue_count = 0;
    queue_capacity = 0;
    pumping = 0;
}

void platform_post_completion(PlatformCompletionFn fn, void* arg)
{
    if (queue_count == queue_capacity) {
        size_t capacity = queue_capacity ? queue_capacity * 2 : 64;
        PlatformCompletion* grown =
            realloc(queue, capacity * sizeof(*grown));
        if (grown == NULL) {
            return; /* drop the completion rather than corrupt the queue */
        }
        queue = grown;
        queue_capacity = capacity;
    }
    queue[queue_count].fn = fn;
    queue[queue_count].arg = arg;
    queue_count++;
}

void platform_pump_completions(void)
{
    size_t i;

    if (pumping) {
        return;
    }
    pumping = 1;
    /*
     * queue_count grows while callbacks post follow-up completions; re-read it
     * every iteration and re-fetch the base pointer because a callback may
     * realloc the queue.
     */
    for (i = 0; i < queue_count; i++) {
        PlatformCompletion done = queue[i];
        done.fn(done.arg);
    }
    queue_count = 0;
    pumping = 0;
}
