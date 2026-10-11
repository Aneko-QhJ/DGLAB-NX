#pragma once

#include <stdio.h>
#include <switch/types.h>

// Takes ownership of file on success. Only the writer performs SD I/O.
Result dglabLogFileStart(FILE* file);
void dglabLogFilePush(const char* line);
// Drains pending text and closes the file when NRO exits.
Result dglabLogFileStop(void);
