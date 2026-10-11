#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <switch/result.h>

// HOS 22.5.0 hardware observation: the first scan StopBleScan can return this
// while the next scan receives the target and its final StopBleScan succeeds.
// Its exact firmware meaning is not assumed; only that verified sequence may
// recover it. Other errors stay fatal.
#define DGLAB_BLE_INITIAL_SCAN_STOP_RESULT 0x0002A871u

typedef struct {
    uint32_t critical_result;
    uint32_t initial_stop_result;
    bool final_scan_stopped;
    bool target_seen;
} DglabBlePreparation;

static inline uint32_t dglabBlePreparationResult(const DglabBlePreparation* preparation,
    bool require_target)
{
    if (preparation->critical_result)
        return preparation->critical_result;
    if (preparation->initial_stop_result &&
        (preparation->initial_stop_result != DGLAB_BLE_INITIAL_SCAN_STOP_RESULT ||
         !preparation->target_seen || !preparation->final_scan_stopped))
        return preparation->initial_stop_result;
    if (!preparation->final_scan_stopped)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    if (require_target && !preparation->target_seen)
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);
    return 0;
}
