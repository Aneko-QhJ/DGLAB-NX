#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define DGLAB_LOG_QUEUE_CAPACITY 65536u

// Caller serializes access. Reject complete lines on overflow, preserving the
// queued text and its line boundaries; the writer reports the dropped bytes.
typedef struct {
    char data[DGLAB_LOG_QUEUE_CAPACITY];
    size_t head;
    size_t size;
    uint64_t dropped;
} DglabLogQueue;

static inline bool dglabLogQueuePush(DglabLogQueue* q, const char* line, size_t len)
{
    if (len >= DGLAB_LOG_QUEUE_CAPACITY || len + 1u > DGLAB_LOG_QUEUE_CAPACITY - q->size) {
        q->dropped += len + 1u;
        return false;
    }
    size_t tail = (q->head + q->size) % DGLAB_LOG_QUEUE_CAPACITY;
    size_t first = DGLAB_LOG_QUEUE_CAPACITY - tail;
    if (first > len)
        first = len;
    memcpy(q->data + tail, line, first);
    memcpy(q->data, line + first, len - first);
    q->data[(tail + len) % DGLAB_LOG_QUEUE_CAPACITY] = '\n';
    q->size += len + 1u;
    return true;
}

static inline size_t dglabLogQueueTake(DglabLogQueue* q, char* out, size_t capacity)
{
    size_t count = q->size < capacity ? q->size : capacity;
    size_t first = DGLAB_LOG_QUEUE_CAPACITY - q->head;
    if (first > count)
        first = count;
    memcpy(out, q->data + q->head, first);
    memcpy(out + first, q->data, count - first);
    q->head = (q->head + count) % DGLAB_LOG_QUEUE_CAPACITY;
    q->size -= count;
    return count;
}
