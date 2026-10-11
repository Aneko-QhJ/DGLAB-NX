#include <dglab/util/log_queue.h>
#include <assert.h>
#include <stdio.h>

static DglabLogQueue queue;
static char expected[DGLAB_LOG_QUEUE_CAPACITY];
static char output[DGLAB_LOG_QUEUE_CAPACITY];
static char line[DGLAB_LOG_QUEUE_CAPACITY];

int main(void)
{
    // A full queue rejects a complete line without corrupting pending text.
    memset(line, 'a', sizeof(line));
    assert(dglabLogQueuePush(&queue, line, sizeof(line) - 1));
    assert(!dglabLogQueuePush(&queue, "overflow", 8));
    assert(queue.dropped == 9);
    assert(dglabLogQueueTake(&queue, output, 17) == 17);
    assert(dglabLogQueuePush(&queue, "next", 4));
    size_t count = dglabLogQueueTake(&queue, output, sizeof(output));
    assert(count == sizeof(line) - 17 + 5);
    assert(output[sizeof(line) - 18] == '\n');
    assert(memcmp(output + sizeof(line) - 17, "next\n", 5) == 0);
    for (size_t i = 0; i < sizeof(line) - 18; i++)
        assert(output[i] == 'a');
    assert(!dglabLogQueuePush(&queue, line, sizeof(line)));

    // Compare mixed producer/consumer bursts across many wraparounds against
    // a linear FIFO; mimic a slow SD writer and then drain all remaining text.
    size_t pending = 0;
    uint32_t random = 123;
    for (unsigned step = 0; step < 10000; step++) {
        random = random * 1664525u + 1013904223u;
        if (random & 1) {
            size_t len = (random >> 8) % 3000;
            memset(line, 'A' + step % 26, len);
            bool fits = len + 1 <= sizeof(expected) - pending;
            uint64_t dropped = queue.dropped;
            assert(dglabLogQueuePush(&queue, line, len) == fits);
            if (fits) {
                memcpy(expected + pending, line, len);
                expected[pending + len] = '\n';
                pending += len + 1;
            } else {
                assert(queue.dropped == dropped + len + 1);
            }
        } else {
            size_t capacity = (random >> 8) % 2048;
            size_t wanted = pending < capacity ? pending : capacity;
            assert(dglabLogQueueTake(&queue, output, capacity) == wanted);
            assert(memcmp(expected, output, wanted) == 0);
            memmove(expected, expected + wanted, pending - wanted);
            pending -= wanted;
        }
        assert(queue.size == pending);
    }
    assert(dglabLogQueueTake(&queue, output, sizeof(output)) == pending);
    assert(memcmp(expected, output, pending) == 0);
    assert(queue.size == 0);
    puts("log queue: full/overflow/wraparound and 10000 mixed bursts passed");
    return 0;
}
