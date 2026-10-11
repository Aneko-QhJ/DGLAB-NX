#include <dglab/platform/log_file.h>
#include <dglab/util/log_queue.h>
#include <switch.h>

static DglabLogQueue g_queue;
static Mutex g_mutex;
static Thread g_thread;
static FILE* g_file;
static bool g_started;
static bool g_stop;
static bool g_io_failed;
static Result g_result;
static char g_batch[4096]; // Writer-owned, kept off the thread stack.
static u8 g_stack[0x8000] __attribute__((aligned(0x1000)));

static void logWriter(void* arg)
{
    (void)arg;
    while (true) {
        mutexLock(&g_mutex);
        size_t count = dglabLogQueueTake(&g_queue, g_batch, sizeof(g_batch));
        u64 dropped = g_queue.dropped;
        g_queue.dropped = 0;
        bool done = g_stop && g_queue.size == 0;
        mutexUnlock(&g_mutex);

        if (count || dropped) {
            u64 started = armGetSystemTick();
            if (dropped)
                fprintf(g_file, "nro log writer: dropped %llu bytes (queue full)\n",
                    (unsigned long long)dropped);
            if (count && fwrite(g_batch, 1, count, g_file) != count)
                g_io_failed = true;
            if (fflush(g_file) != 0)
                g_io_failed = true;
            u64 elapsed_ms = armTicksToNs(armGetSystemTick() - started) / 1000000ull;
            if (elapsed_ms >= 100u)
                fprintf(g_file, "nro log writer: SD batch took %llu ms\n",
                    (unsigned long long)elapsed_ms);
        }
        if (done)
            break;
        svcSleepThread(100000000ull);
    }
    if (fclose(g_file) != 0)
        g_io_failed = true;
    g_result = g_io_failed ? MAKERESULT(Module_Libnx, LibnxError_IoError) : 0;
}

Result dglabLogFileStart(FILE* file)
{
    if (!file || g_started)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    memset(&g_queue, 0, sizeof(g_queue));
    memset(&g_thread, 0, sizeof(g_thread));
    mutexInit(&g_mutex);
    g_stop = false;
    g_io_failed = false;
    g_result = 0;
    g_file = file;
    Result rc = threadCreate(&g_thread, logWriter, NULL, g_stack, sizeof(g_stack), 0x2D, -2);
    if (R_FAILED(rc))
        return rc;
    rc = threadStart(&g_thread);
    if (R_FAILED(rc)) {
        Result close_rc = threadClose(&g_thread);
        if (R_FAILED(close_rc))
            return close_rc;
        return rc;
    }
    g_started = true;
    return 0;
}

void dglabLogFilePush(const char* line)
{
    if (!g_started)
        return;
    size_t len = strlen(line);
    mutexLock(&g_mutex);
    dglabLogQueuePush(&g_queue, line, len);
    mutexUnlock(&g_mutex);
}

Result dglabLogFileStop(void)
{
    if (!g_started)
        return 0;
    mutexLock(&g_mutex);
    g_stop = true;
    mutexUnlock(&g_mutex);
    Result wait_rc = threadWaitForExit(&g_thread);
    if (R_FAILED(wait_rc))
        return wait_rc;
    Result close_rc = threadClose(&g_thread);
    if (R_FAILED(close_rc))
        return close_rc;
    g_started = false;
    return g_result;
}
