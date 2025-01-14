#ifndef CUSTOMPOPEN_H
#define CUSTOMPOPEN_H

#include <qglobal.h>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#include <cstdio>

FILE* CustomPopen(const char* command, const char* mode) {
    SECURITY_ATTRIBUTES sa;
    HANDLE hChildStdinRd, hChildStdinWr;
    HANDLE hChildStdoutRd, hChildStdoutWr;

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    // Erstelle eine Pipe für den Stdin
    if (!CreatePipe(&hChildStdinRd, &hChildStdinWr, &sa, 0)) {
        qDebug() << "Error: Failed to create input pipe!";
        return nullptr;
    }

    // Erstelle Ausgabe- und Fehlerpipeline
    if (!CreatePipe(&hChildStdoutRd, &hChildStdoutWr, &sa, 0)) {
        CloseHandle(hChildStdinRd);
        CloseHandle(hChildStdinWr);
        qDebug() << "Error: Failed to create output pipe!";
        return nullptr;
    }

    // Sicherstellen, dass der Schreibhandle nicht vererbt wird
    if (!SetHandleInformation(hChildStdinWr, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(hChildStdoutRd, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(hChildStdinRd);
        CloseHandle(hChildStdinWr);
        CloseHandle(hChildStdoutRd);
        CloseHandle(hChildStdoutWr);
        qDebug() << "Error: Failed to set handle information!";
        return nullptr;
    }

    PROCESS_INFORMATION pi;
    STARTUPINFO si;
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
    ZeroMemory(&si, sizeof(STARTUPINFO));
    si.cb = sizeof(STARTUPINFO);
    si.hStdInput = hChildStdinRd;
    si.hStdOutput = hChildStdoutWr;
    si.hStdError = hChildStdoutWr;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    // Befehl vorbereiten
    size_t commandLength = strlen(command) + 1;
    wchar_t* wideCommandLine = new wchar_t[commandLength];
    mbstowcs(wideCommandLine, command, commandLength);

    // Erstelle den Prozess
    if (!CreateProcess(
            NULL, wideCommandLine, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(hChildStdinRd);
        CloseHandle(hChildStdinWr);
        CloseHandle(hChildStdoutRd);
        CloseHandle(hChildStdoutWr);
        delete[] wideCommandLine;
        qDebug() << "Error: Failed to create process!";
        return nullptr;
    }

    // Schließe ungenutzte Handles
    CloseHandle(hChildStdinRd);
    CloseHandle(hChildStdoutWr);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Konvertiere den Schreibhandle in einen FILE*-Pointer
    int fd = _open_osfhandle((intptr_t)hChildStdinWr, _O_WRONLY);
    delete[] wideCommandLine;

    if(fd == -1)
    {
        CloseHandle(hChildStdinWr);
        qDebug() << "Error: Failed to open file descriptor for pipe!";
        return nullptr;
    }

    // Ausgabe von ffmpeg in einer seperaten Pipe lesen
    std::thread([hChildStdoutRd, pi](){
        char buffer[4096];
        DWORD bytesRead;
        while (true) {
            if (!ReadFile(hChildStdoutRd, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
                DWORD error = GetLastError();
                if (error == ERROR_SUCCESS) {
                } else if (error == ERROR_BROKEN_PIPE) {
                    break;
                } else {
                    qDebug() << "ReadFileError";
                }
            }
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                qDebug() << "FFmpeg: " << buffer;
            }
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(hChildStdoutRd);
    }).detach();

    return _fdopen(fd, mode);
}
#endif

#endif // CUSTOMPOPEN_H
