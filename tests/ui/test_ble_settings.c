#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include <dglab/nro/ble_settings.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

static bool fsdev_rename, fail_install, fail_rollback;
static int simulatedRename(const char* from, const char* to)
{
    struct stat info;
    if (fsdev_rename && stat(to, &info) == 0) { errno = EEXIST; return -1; }
    if ((fail_install && strstr(from, ".tmp")) ||
        (fail_rollback && strstr(from, ".bak"))) { errno = EIO; return -1; }
    return rename(from, to);
}
#define rename simulatedRename
#include "../../nro/source/config/ble_settings.c"
#undef rename

static const uint8_t ips32[] = {
    'I','P','S','3','2',
    0,0x0C,0xD9,0x20,0,4,0x1F,0x20,0x03,0xD5,
    0,0x0C,0xF8,0xD4,0,4,0x1F,0x20,0x03,0xD5,
    'E','E','O','F'
};
static const uint8_t ips[] = {
    'P','A','T','C','H',
    0x0C,0xD9,0x20,0,4,0x1F,0x20,0x03,0xD5,
    0x0C,0xF8,0xD4,0,4,0x1F,0x20,0x03,0xD5,
    'E','O','F'
};
static void writeFile(const char* path, const void* bytes, size_t size)
{
    FILE* file = fopen(path, "wb"); assert(file);
    assert(fwrite(bytes, 1, size, file) == size);
    assert(fclose(file) == 0);
}
static void test_format_and_files(const char* root)
{
    uint8_t address[6], unchanged[6] = {1,2,3,4,5,6};
    char text[18], path[512], temporary[520];
    assert(dglabBleAddressParse("aA:bb:0c:DD:eE:fF", address));
    dglabBleAddressFormat(address, text); assert(!strcmp(text, "AA:BB:0C:DD:EE:FF"));
    const char* invalid[] = {"", "A:BB:CC:DD:EE:FF", "AAA:BB:CC:DD:EE:FF",
        "GG:BB:CC:DD:EE:FF", "AA-BB-CC-DD-EE-FF", "AABBCCDDEEFF",
        " AA:BB:CC:DD:EE:FF", "AA:BB:CC:DD:EE:FF ", "AA:BB:CC:DD:EE:FFx",
        "AA:BB:CC:DD:EE:FF\n", "AA:BB:CC:DD:EE", "ＡＡ:BB:CC:DD:EE:FF"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        memcpy(address, unchanged, 6);
        assert(!dglabBleAddressParse(invalid[i], address));
        assert(!memcmp(address, unchanged, 6));
    }
    snprintf(path, sizeof(path), "%s/address.txt", root);
    assert(!dglabBleAddressLoad(path, address));
    const char* valid[] = {"AA:BB:CC:DD:EE:FF", "aa:bb:cc:dd:ee:ff\n", "AA:BB:CC:DD:EE:FF\r\n"};
    for (unsigned i = 0; i < 3; ++i) {
        writeFile(path, valid[i], strlen(valid[i]));
        assert(dglabBleAddressLoad(path, address) && address[0] == 0xAA);
    }
    const char bad[] = "AA:BB:CC:DD:EE:FF\nextra";
    writeFile(path, bad, sizeof(bad) - 1); assert(!dglabBleAddressLoad(path, address));
    const char nul[] = "AA:BB:CC:DD:EE:FF\0";
    writeFile(path, nul, sizeof(nul) - 1); assert(!dglabBleAddressLoad(path, address));
    memcpy(address, unchanged, 6); assert(dglabBleAddressSave(path, address));
    assert(dglabBleAddressLoad(path, address) && !memcmp(address, unchanged, 6));
    snprintf(temporary, sizeof(temporary), "%s.tmp", path); assert(mkdir(temporary, 0700) == 0);
    memset(address, 0xAA, 6); assert(!dglabBleAddressSave(path, address));
    assert(dglabBleAddressLoad(path, address) && !memcmp(address, unchanged, 6));
    assert(rmdir(temporary) == 0);
    // Failed rename must clean temporary output and retain the existing target.
    // Emulate Switch fsdev refusal to replace an existing destination.
    fsdev_rename = true;
    memset(address, 0xAA, 6); assert(dglabBleAddressSave(path, address));
    assert(dglabBleAddressLoad(path, address) && address[0] == 0xAA);
    fail_install = true;
    memcpy(address, unchanged, 6); assert(!dglabBleAddressSave(path, address));
    assert(dglabBleAddressLoad(path, address) && address[0] == 0xAA);
    fail_rollback = true;
    assert(!dglabBleAddressSave(path, address));
    assert(dglabBleAddressLoad(path, address) && address[0] == 0xAA);
    fail_install = fail_rollback = false;
    assert(dglabBleAddressSave(path, unchanged));
    assert(dglabBleAddressLoad(path, address) && !memcmp(address, unchanged, 6));
    fsdev_rename = false;
    assert(unlink(path) == 0 && mkdir(path, 0700) == 0);
    assert(!dglabBleAddressSave(path, address));
    assert(access(temporary, F_OK) != 0); assert(rmdir(path) == 0);
}
static void test_patch_parser(void)
{
    assert(dglabBlePatchValidate(ips32, sizeof(ips32)));
    assert(dglabBlePatchValidate(ips, sizeof(ips)));
    for (size_t size = 0; size < sizeof(ips32); ++size)
        assert(!dglabBlePatchValidate(ips32, size));
    uint8_t broken[sizeof(ips32) + 10];
    memcpy(broken, ips32, sizeof(ips32)); broken[11] = 0xD5;
    assert(!dglabBlePatchValidate(broken, sizeof(ips32)));
    memcpy(broken, ips32, sizeof(ips32)); broken[18] = 0x21;
    assert(!dglabBlePatchValidate(broken, sizeof(ips32)));
    memcpy(broken, ips32, sizeof(ips32)); broken[sizeof(ips32)] = 0;
    assert(!dglabBlePatchValidate(broken, sizeof(ips32) + 1));
    // Diagnostic marker at another offset, even with the required edits present.
    memcpy(broken, ips32, sizeof(ips32) - 4);
    const uint8_t marker[] = {0,6,0,0,0,4,0xE2,3,0x1F,0x2A,'E','E','O','F'};
    memcpy(broken + sizeof(ips32) - 4, marker, sizeof(marker));
    assert(!dglabBlePatchValidate(broken, sizeof(broken)));
    // Exercise byte-sized RLE records and reverse record order.
    uint8_t split[85] = {'I','P','S','3','2'}; size_t pos = 5;
    for (unsigned edit = 0; edit < 2; ++edit) for (unsigned byte = 0; byte < 4; ++byte) {
        uint32_t offset = (edit ? 0xCD920 : 0xCF8D4) + byte;
        split[pos++] = 0; split[pos++] = (uint8_t)(offset >> 16);
        split[pos++] = (uint8_t)(offset >> 8); split[pos++] = (uint8_t)offset;
        split[pos++] = 0; split[pos++] = 0;
        split[pos++] = 0; split[pos++] = 1;
        split[pos++] = ips32[11 + byte];
    }
    memcpy(split + pos, "EEOF", 4); pos += 4;
    assert(dglabBlePatchValidate(split, pos));
    split[5 + 7] = 0; assert(!dglabBlePatchValidate(split, pos));
}
static void test_patch_directory(const char* root)
{
    char subdir[512], path[768], padded[768];
    assert(dglabBlePatchCheck(root, false, 22,5,0) == DglabBlePatch_FirmwareUnknown);
    assert(dglabBlePatchCheck(root, true, 22,4,0) == DglabBlePatch_UnsupportedFirmware);
    assert(dglabBlePatchCheck(root, true, 22,5,1) == DglabBlePatch_UnsupportedFirmware);
    assert(dglabBlePatchCheck(root, true, 22,5,0) == DglabBlePatch_Missing);
    snprintf(subdir, sizeof(subdir), "%s/any-directory", root); assert(mkdir(subdir, 0700) == 0);
    snprintf(path, sizeof(path), "%s/c91c6fc8aa4c39222d6ccfe0fff468543105e59b.ips", subdir);
    writeFile(path, ips32, sizeof(ips32));
    assert(dglabBlePatchCheck(root, true, 22,5,0) == DglabBlePatch_Ok);
    if (geteuid() != 0) {
        assert(chmod(path, 0) == 0);
        assert(dglabBlePatchCheck(root, true, 22,5,0) == DglabBlePatch_Unreadable);
        assert(chmod(path, 0600) == 0);
    }
    snprintf(padded, sizeof(padded), "%s/C91C6FC8AA4C39222D6CCFE0FFF468543105E59B000000000000000000000000.ips", subdir);
    assert(rename(path, padded) == 0);
    assert(dglabBlePatchCheck(root, true, 22,5,0) == DglabBlePatch_Ok);
    writeFile(padded, ips32, sizeof(ips32) - 1);
    assert(dglabBlePatchCheck(root, true, 22,5,0) == DglabBlePatch_Invalid);
    writeFile(padded, ips, sizeof(ips));
    writeFile(path, "invalid", 7);
    assert(dglabBlePatchCheck(root, true, 22,5,0) == DglabBlePatch_Invalid);
    assert(unlink(path) == 0 && unlink(padded) == 0 && rmdir(subdir) == 0);
    // A regular file used as directory is unreadable, rather than missing.
    writeFile(subdir, "x", 1);
    assert(dglabBlePatchCheck(subdir, true, 22,5,0) == DglabBlePatch_Unreadable);
    assert(unlink(subdir) == 0);
}
int main(void)
{
    char root[] = "/tmp/dglab-ble-settings-XXXXXX"; assert(mkdtemp(root));
    test_format_and_files(root); test_patch_parser(); test_patch_directory(root);
    assert(rmdir(root) == 0);
    puts("BLE patch validation, MAC format, replacement and rollback tests passed");
    return 0;
}
