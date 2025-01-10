#ifndef CUSTOMPOPEN_H
#define CUSTOMPOPEN_H

#ifdef Q_OS_WIN
#include <windows.h>
#include <cstdio>

FILE* myPopen(const char* command, const char* mode) {
    SECURITY_ATTRIBUTES sa;
    HANDLE hChildStdinRd, hChildStdinWr;

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    // Erstelle eine Pipe für den Stdin
    if (!CreatePipe(&hChildStdinRd, &hChildStdinWr, &sa, 0)) {
        return nullptr;
    }

    // Sicherstellen, dass der Schreibhandle nicht vererbt wird
    if (!SetHandleInformation(hChildStdinWr, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(hChildStdinRd);
        CloseHandle(hChildStdinWr);
        return nullptr;
    }

    PROCESS_INFORMATION pi;
    STARTUPINFO si;
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
    ZeroMemory(&si, sizeof(STARTUPINFO));
    si.cb = sizeof(STARTUPINFO);
    si.hStdInput = hChildStdinRd;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    // Befehl vorbereiten
    char commandLine[1024];
    snprintf(commandLine, sizeof(commandLine), "cmd.exe /C %s", command);

    // Erstelle den Prozess
    if (!CreateProcess(
            NULL, commandLine, NULL, NULL, TRUE, DETACHED_PROCESS, NULL, NULL, &si, &pi)) {
        CloseHandle(hChildStdinRd);
        CloseHandle(hChildStdinWr);
        return nullptr;
    }

    // Schließe ungenutzte Handles
    CloseHandle(hChildStdinRd);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Konvertiere den Schreibhandle in einen FILE*-Pointer
    int fd = _open_osfhandle((intptr_t)hChildStdinWr, _O_WRONLY);
    return _fdopen(fd, mode);
}
#endif

#endif // CUSTOMPOPEN_H
