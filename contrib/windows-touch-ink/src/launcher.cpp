// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <string>
#include <vector>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int)
{
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size())
        return 1;
    std::wstring folder(buffer.data(), length);
    folder.resize(folder.find_last_of(L"\\/"));
    const std::wstring program = folder + L"\\speedynote.exe";
    const std::wstring plugin = folder + L"\\generic\\speedynote_touchink.dll";
    const std::wstring update = folder + L"\\touch-ink-update.dll";
    // Install a prepared update only after all existing edition windows close.
    // Never terminate a running notebook process or interrupt unsaved writing.
    if (GetFileAttributesW(update.c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (!CopyFileW(update.c_str(), plugin.c_str(), FALSE)) {
            MessageBoxW(nullptr, L"更新已准备好。请先关闭所有“SpeedyNote 电容笔版”窗口，再重新启动。",
                        L"SpeedyNote 电容笔版", MB_OK | MB_ICONINFORMATION);
            return 4;
        }
        DeleteFileW(update.c_str());
    }
    if (GetFileAttributesW(program.c_str()) == INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW(plugin.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr, L"电容笔版运行文件不完整。请保留整个文件夹，不要只移动启动文件。",
                    L"SpeedyNote 电容笔版", MB_OK | MB_ICONERROR);
        return 2;
    }
    // This environment belongs only to this launcher and its child process.
    SetEnvironmentVariableW(L"QT_QPA_GENERIC_PLUGINS", L"speedynotetouchink");
    std::wstring command = L"\"" + program + L"\"";
    if (arguments && *arguments)
        command += L" " + std::wstring(arguments);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, folder.c_str(), &startup, &process)) {
        MessageBoxW(nullptr, L"无法启动 SpeedyNote 电容笔版。请检查文件是否完整。",
                    L"SpeedyNote 电容笔版", MB_OK | MB_ICONERROR);
        return 3;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
