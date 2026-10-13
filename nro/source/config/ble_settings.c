#include <dglab/nro/ble_settings.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static int hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool dglabBleAddressParse(const char* text, uint8_t out[6])
{
    uint8_t address[6];
    if (!text || strlen(text) != 17) return false;
    for (unsigned i = 0; i < 6; ++i) {
        int a = hexDigit(text[i * 3]), b = hexDigit(text[i * 3 + 1]);
        if (a < 0 || b < 0 || (i < 5 && text[i * 3 + 2] != ':')) return false;
        address[i] = (uint8_t)((a << 4) | b);
    }
    memcpy(out, address, sizeof(address));
    return true;
}

void dglabBleAddressFormat(const uint8_t address[6], char out[18])
{
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", address[0], address[1],
        address[2], address[3], address[4], address[5]);
}

bool dglabBleAddressLoad(const char* path, uint8_t out[6])
{
    FILE* file = fopen(path, "rb");
    // An interrupted fsdev replacement may have moved the old file to .bak.
    if (!file && errno == ENOENT) {
        char backup[1024];
        int n = snprintf(backup, sizeof(backup), "%s.bak", path);
        if (n >= 0 && n < (int)sizeof(backup)) file = fopen(backup, "rb");
    }
    if (!file) return false;
    char text[21];
    size_t size = fread(text, 1, sizeof(text) - 1, file);
    bool ok = !ferror(file) && feof(file);
    if (fclose(file) != 0) ok = false;
    if (!ok || memchr(text, 0, size)) return false;
    if (size && text[size - 1] == '\n') {
        --size;
        if (size && text[size - 1] == '\r') --size;
    }
    text[size] = 0;
    return dglabBleAddressParse(text, out);
}

bool dglabBleAddressSave(const char* path, const uint8_t address[6])
{
    char temporary[1024], backup[1024], text[18];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary))
        return false;
    if (snprintf(backup, sizeof(backup), "%s.bak", path) >= (int)sizeof(backup)) return false;
    struct stat info;
    // Resolve a previous interrupted replacement before attempting another.
    if (stat(backup, &info) == 0) {
        struct stat current;
        if (stat(path, &current) != 0) {
            if (errno != ENOENT || rename(backup, path) != 0) return false;
        } else {
            uint8_t verified[6];
            if (!dglabBleAddressLoad(path, verified) || remove(backup) != 0) return false;
        }
    } else if (errno != ENOENT) return false;
    FILE* file = fopen(temporary, "wb");
    if (!file) return false;
    dglabBleAddressFormat(address, text);
    bool ok = fprintf(file, "%s\n", text) == 18;
    if (fflush(file) != 0) ok = false;
    if (fclose(file) != 0) ok = false;
    if (ok && rename(temporary, path) == 0) return true;
    // libnx fsdev forwards RenameFile directly and does not replace an existing
    // destination. Preserve the old file instead of unlinking it to make room.
    if (ok && errno == EEXIST && stat(path, &info) == 0 && S_ISREG(info.st_mode) &&
        rename(path, backup) == 0) {
        if (rename(temporary, path) == 0) {
            remove(backup); // If cleanup fails, next save verifies the new file first.
            return true;
        }
        // Even if SD failure prevents rollback, Load can still read the backup.
        rename(backup, path);
    }
    remove(temporary);
    return false;
}

static uint32_t bigEndian(const uint8_t* data, unsigned count)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < count; ++i) value = (value << 8) | data[i];
    return value;
}

bool dglabBlePatchValidate(const uint8_t* data, size_t size)
{
    const uint32_t offsets[] = {0xCD920, 0xCF8D4};
    const uint8_t nop[] = {0x1F, 0x20, 0x03, 0xD5};
    uint8_t seen[2] = {0};
    if (!data || size < 8) return false;
    bool ips32 = memcmp(data, "IPS32", 5) == 0;
    if (!ips32 && memcmp(data, "PATCH", 5) != 0) return false;
    unsigned width = ips32 ? 4 : 3;
    size_t pos = 5;
    while (pos + width <= size) {
        if (memcmp(data + pos, ips32 ? "EEOF" : "EOF", width) == 0)
            return pos + width == size && seen[0] == 15 && seen[1] == 15;
        if (size - pos < width + 2) return false;
        uint32_t offset = bigEndian(data + pos, width);
        unsigned length = bigEndian(data + pos + width, 2);
        pos += width + 2;
        bool rle = length == 0;
        if (rle) {
            if (size - pos < 3) return false;
            length = bigEndian(data + pos, 2);
            pos += 2;
        }
        if (!length || size - pos < (rle ? 1u : length)) return false;
        // Only the two known edits are allowed, including split/RLE records.
        for (unsigned i = 0; i < length; ++i) {
            bool matched = false;
            for (unsigned edit = 0; edit < 2; ++edit) {
                if (offset + i < offsets[edit] || offset + i >= offsets[edit] + 4) continue;
                unsigned byte = offset + i - offsets[edit];
                if ((seen[edit] & (1u << byte)) || data[pos + (rle ? 0 : i)] != nop[byte])
                    return false;
                seen[edit] |= 1u << byte;
                matched = true;
            }
            if (!matched) return false;
        }
        pos += rle ? 1 : length;
    }
    return false;
}

static bool patchName(const char* name)
{
    const char* id = "C91C6FC8AA4C39222D6CCFE0FFF468543105E59B";
    size_t length = strlen(name), prefix = strlen(id);
    if (length < prefix + 4 || length > 64 + 4 ||
        strcasecmp(name + length - 4, ".ips") || strncasecmp(name, id, prefix)) return false;
    for (size_t i = prefix; i < length - 4; ++i) if (name[i] != '0') return false;
    return (length - 4) % 2 == 0;
}

DglabBlePatchResult dglabBlePatchCheck(const char* root, bool firmware_known,
    unsigned major, unsigned minor, unsigned micro)
{
    if (!firmware_known) return DglabBlePatch_FirmwareUnknown;
    if (major != 22 || minor != 5 || micro != 0) return DglabBlePatch_UnsupportedFirmware;
    DIR* directory = opendir(root);
    if (!directory) return errno == ENOENT ? DglabBlePatch_Missing : DglabBlePatch_Unreadable;
    DglabBlePatchResult result = DglabBlePatch_Missing;
    bool found = false;
    struct dirent* entry;
    errno = 0;
    while ((entry = readdir(directory))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char path[1024];
        struct stat info;
        int n = snprintf(path, sizeof(path), "%s/%s", root, entry->d_name);
        if (n < 0 || n >= (int)sizeof(path) || stat(path, &info) != 0) {
            result = DglabBlePatch_Unreadable; break;
        }
        if (!S_ISDIR(info.st_mode)) continue;
        DIR* child = opendir(path);
        if (!child) { result = DglabBlePatch_Unreadable; break; }
        struct dirent* patch;
        errno = 0;
        while ((patch = readdir(child))) {
            if (!patchName(patch->d_name)) continue;
            char file_path[1024];
            n = snprintf(file_path, sizeof(file_path), "%s/%s", path, patch->d_name);
            FILE* file = n >= 0 && n < (int)sizeof(file_path) ? fopen(file_path, "rb") : NULL;
            if (!file) { result = DglabBlePatch_Unreadable; break; }
            uint8_t data[256];
            size_t size = fread(data, 1, sizeof(data), file);
            bool readable = !ferror(file);
            bool complete = feof(file);
            if (fclose(file) != 0) readable = false;
            if (!readable) result = DglabBlePatch_Unreadable;
            else if (!complete || !dglabBlePatchValidate(data, size)) result = DglabBlePatch_Invalid;
            else found = true;
            if (result != DglabBlePatch_Missing) break;
            errno = 0;
        }
        if (errno && result == DglabBlePatch_Missing) result = DglabBlePatch_Unreadable;
        closedir(child);
        if (result != DglabBlePatch_Missing) break;
        errno = 0;
    }
    if (errno && result == DglabBlePatch_Missing) result = DglabBlePatch_Unreadable;
    closedir(directory);
    return result == DglabBlePatch_Missing && found ? DglabBlePatch_Ok : result;
}
