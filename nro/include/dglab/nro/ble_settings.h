#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    DglabBlePatch_Ok, DglabBlePatch_FirmwareUnknown,
    DglabBlePatch_UnsupportedFirmware, DglabBlePatch_Missing,
    DglabBlePatch_Unreadable, DglabBlePatch_Invalid,
} DglabBlePatchResult;

typedef enum {
    DglabBleEdit_None, DglabBleEdit_Saved, DglabBleEdit_KeyboardFailed,
    DglabBleEdit_Invalid, DglabBleEdit_SaveFailed,
} DglabBleEditResult;

// Pure format validation; loading permits only a single terminal LF or CRLF.
bool dglabBleAddressParse(const char* text, uint8_t out[6]);
void dglabBleAddressFormat(const uint8_t address[6], char out[18]);
bool dglabBleAddressLoad(const char* path, uint8_t out[6]);
bool dglabBleAddressSave(const char* path, const uint8_t address[6]);
bool dglabBlePatchValidate(const uint8_t* data, size_t size);
// HOS 22.5.0 only. Checks files, not the running Bluetooth module.
DglabBlePatchResult dglabBlePatchCheck(const char* root, bool firmware_known,
    unsigned major, unsigned minor, unsigned micro);
