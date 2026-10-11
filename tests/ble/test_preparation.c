#include <dglab/transport/ble_prepare.h>
#include <assert.h>
#include <stdio.h>

int main(void)
{
    // Replay the first v39 hardware round: an initial stop rejection followed
    // by target advertisements and a successfully stopped final scan.
    DglabBlePreparation p = {
        .initial_stop_result = DGLAB_BLE_INITIAL_SCAN_STOP_RESULT,
        .target_seen = true,
        .final_scan_stopped = true,
    };
    assert(dglabBlePreparationResult(&p, true) == 0);
    // No final stop confirmation must keep the original rejection fatal.
    p.final_scan_stopped = false;
    assert(dglabBlePreparationResult(&p, true) == DGLAB_BLE_INITIAL_SCAN_STOP_RESULT);
    p.final_scan_stopped = true;
    p.target_seen = false;
    assert(dglabBlePreparationResult(&p, true) == DGLAB_BLE_INITIAL_SCAN_STOP_RESULT);
    // Replay v39's second round: successful calls but no target. Do not queue
    // a connect that will remain in flight and require a reboot.
    p.initial_stop_result = 0;
    assert(dglabBlePreparationResult(&p, true) == MAKERESULT(Module_Libnx, LibnxError_NotFound));
    // The diagnostic console may finish its scan without a configured target.
    assert(dglabBlePreparationResult(&p, false) == 0);
    // The recovery never erases failed initialization/scan setup/final stop.
    p.target_seen = true;
    p.initial_stop_result = DGLAB_BLE_INITIAL_SCAN_STOP_RESULT;
    p.critical_result = 0x12345678u;
    assert(dglabBlePreparationResult(&p, true) == p.critical_result);
    p.critical_result = 0;
    p.initial_stop_result = 0x87654321u;
    assert(dglabBlePreparationResult(&p, true) == p.initial_stop_result);
    p.initial_stop_result = 0;
    assert(dglabBlePreparationResult(&p, true) == 0);
    p.final_scan_stopped = false;
    assert(dglabBlePreparationResult(&p, true) != 0);
    puts("BLE preparation: 9 success/recovery/failure cases passed");
    return 0;
}
