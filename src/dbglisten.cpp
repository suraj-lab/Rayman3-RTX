// dbglisten — capture OutputDebugString from any process (DBWIN_BUFFER protocol).
//
// Rayman 3 ships as a debug MFC build (MFC42D/MSVCRTD), so its abort() is most
// likely a failed ASSERT, and debug CRT/MFC report those through
// OutputDebugStringA. Reading them turns "mysterious abort" into a file/line.
//
// Must be the only listener running: the buffer is a single global channel.

#include <windows.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    const char *out = (argc > 1) ? argv[1] : "dbglisten.log";
    const char *secs = (argc > 2) ? argv[2] : "25";
    int seconds = atoi(secs);
    if (seconds <= 0) seconds = 25;

    FILE *log = fopen(out, "w");
    if (!log) { printf("cannot open %s\n", out); return 1; }

    HANDLE bufReady = CreateEventA(NULL, FALSE, FALSE, "DBWIN_BUFFER_READY");
    HANDLE dataReady = CreateEventA(NULL, FALSE, FALSE, "DBWIN_DATA_READY");
    HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, "DBWIN_BUFFER");
    if (!bufReady || !dataReady || !mapping) {
        fprintf(log, "setup failed err=%lu\n", GetLastError());
        fclose(log);
        return 2;
    }
    unsigned char *buf = (unsigned char *)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 4096);
    if (!buf) { fprintf(log, "MapViewOfFile failed err=%lu\n", GetLastError()); return 3; }

    fprintf(log, "listening for %d s ...\n", seconds);
    fflush(log);

    DWORD deadline = GetTickCount() + (DWORD)seconds * 1000;
    int lines = 0;
    while (GetTickCount() < deadline) {
        SetEvent(bufReady);
        DWORD wait = WaitForSingleObject(dataReady, 500);
        if (wait != WAIT_OBJECT_0) continue;
        DWORD pid = *(DWORD *)buf;
        const char *msg = (const char *)(buf + 4);
        size_t len = strnlen(msg, 4091);
        // keep the game's noise down: skip nothing, but tag the pid
        fprintf(log, "[%lu] %.*s", pid, (int)len, msg);
        if (len == 0 || msg[len - 1] != '\n') fputc('\n', log);
        fflush(log);
        lines++;
        if (lines > 4000) break;
    }
    fprintf(log, "--- done, %d messages ---\n", lines);
    fclose(log);
    UnmapViewOfFile(buf);
    CloseHandle(mapping);
    CloseHandle(bufReady);
    CloseHandle(dataReady);
    return 0;
}
