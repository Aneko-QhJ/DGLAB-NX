#pragma once
// Host-only declarations for the controller's existing libnx calls.
#include <switch/types.h>
#include <stdbool.h>
#include <stddef.h>
typedef u32 Result;
typedef struct { unsigned id; } Service;
typedef struct { u64 buttons; } PadState;
#define R_SUCCEEDED(rc) ((rc) == 0)
#define R_FAILED(rc) ((rc) != 0)
#define R_VALUE(rc) (rc)
#define KERNELRESULT(name) 0xEA01u
#define Module_Libnx 345u
#define LibnxError_BadInput 1u
#define MAKERESULT(module, description) ((module) | ((description) << 9))
// Constants copied from installed libnx hid.h, used only by the mock host.
#define HidNpadButton_A (1ull << 0)
#define HidNpadButton_B (1ull << 1)
#define HidNpadButton_X (1ull << 2)
#define HidNpadButton_Y (1ull << 3)
#define HidNpadButton_L (1ull << 6)
#define HidNpadButton_ZL (1ull << 8)
#define HidNpadButton_ZR (1ull << 9)
#define HidNpadButton_Left (1ull << 12)
#define HidNpadButton_Up (1ull << 13)
#define HidNpadButton_Right (1ull << 14)
#define HidNpadButton_Down (1ull << 15)
#define HidNpadIdType_No1 0u
#define HidDeviceTypeBits_JoyLeft (1u << 4)
#define HidDeviceTypeBits_JoyRight (1u << 5)
#define HidNpadStyleTag_NpadJoyDual (1u << 2)
#define HidNpadStyleTag_NpadJoyLeft (1u << 3)
#define HidNpadStyleTag_NpadJoyRight (1u << 4)
#define HidNpadAttribute_IsConnected (1u << 0)
#define HidNpadAttribute_IsLeftConnected (1u << 2)
#define HidNpadAttribute_IsRightConnected (1u << 4)
static inline void padUpdate(PadState* p) { (void)p; }
static inline u64 padGetButtons(const PadState* p) { return p->buttons; }
u64 armGetSystemTick(void);
static inline u64 armTicksToNs(u64 ticks) { return ticks; }
u32 hidGetNpadDeviceType(unsigned id);
static inline u32 padGetStyleSet(const PadState* p) { (void)p; return HidNpadStyleTag_NpadJoyDual; }
static inline u32 padGetAttributes(const PadState* p) { (void)p; return HidNpadAttribute_IsConnected | HidNpadAttribute_IsLeftConnected | HidNpadAttribute_IsRightConnected; }
static inline bool padIsHandheld(const PadState* p) { (void)p; return false; }
static inline bool padIsConnected(const PadState* p) { (void)p; return true; }
Result testDispatch(Service* service, unsigned command, const void* input, size_t input_size, void* output, size_t output_size);
#define serviceDispatch(s, c) testDispatch(s, c, NULL, 0, NULL, 0)
#define serviceDispatchOut(s, c, o) testDispatch(s, c, NULL, 0, &(o), sizeof(o))
#define serviceDispatchIn(s, c, i) testDispatch(s, c, &(i), sizeof(i), NULL, 0)
