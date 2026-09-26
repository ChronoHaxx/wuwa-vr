/* WuWa VR Launcher.exe - original project launcher stub. Licence scope: see LICENSE.md.
 *
 * Starts the package's private Python (python\pythonw.exe) with the launcher
 * script (app\dev\wuwa_player.py) and forwards any arguments. It does nothing
 * else: no network, no registry writes, no elevation (manifest: asInvoker).
 * If a required file is missing it explains how to extract the ZIP.
 * WUWA_VR_NO_DIALOG=1 reports errors on stderr instead of a message box, for
 * automated tests.
 */
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

#define PATH_CHARS 32768
#define EXIT_REPORTED 3
#define STARTUP_WAIT_MS 20000

static BOOL quiet(void) {
    wchar_t flag[8];
    return GetEnvironmentVariableW(L"WUWA_VR_NO_DIALOG", flag, 8) > 0;
}

static void report(const wchar_t *text) {
    if (quiet()) {
        HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
        if (err != NULL && err != INVALID_HANDLE_VALUE) {
            char buffer[8192];
            int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, buffer, sizeof(buffer) - 2, NULL, NULL);
            if (bytes > 1) {
                DWORD written;
                buffer[bytes - 1] = '\n';
                WriteFile(err, buffer, (DWORD)bytes, &written, NULL);
            }
        }
        return;
    }
    MessageBoxW(NULL, text, L"WuWa VR Launcher", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

static BOOL is_file(const wchar_t *path) {
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static int missing(const wchar_t *path) {
    static wchar_t text[PATH_CHARS + 512];
    swprintf(text, sizeof(text) / sizeof(text[0]),
             L"A required file is missing:\n%ls\n\nExtract the whole ZIP to a normal folder (for example C:\\Games\\WuWa VR), "
             L"then open WuWa VR Launcher.exe from there. It cannot run from inside the ZIP window.", path);
    report(text);
    return EXIT_REPORTED;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR arguments, int show) {
    static wchar_t folder[PATH_CHARS], python[PATH_CHARS], script[PATH_CHARS], app[PATH_CHARS];
    static wchar_t command[PATH_CHARS * 2 + 64];
    static wchar_t text[1024];
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    DWORD length, wait, code = 0;
    wchar_t *slash;
    BOOL inherit = quiet();
    (void)instance; (void)previous; (void)show;

    length = GetModuleFileNameW(NULL, folder, PATH_CHARS);
    if (length == 0 || length >= PATH_CHARS || (slash = wcsrchr(folder, L'\\')) == NULL) {
        report(L"Could not find the launcher's own folder.");
        return EXIT_REPORTED;
    }
    *slash = L'\0';
    if (swprintf(python, PATH_CHARS, L"%ls\\python\\pythonw.exe", folder) < 0 ||
        swprintf(script, PATH_CHARS, L"%ls\\app\\dev\\wuwa_player.py", folder) < 0 ||
        swprintf(app, PATH_CHARS, L"%ls\\app", folder) < 0) {
        report(L"The launcher folder path is too long. Move the WuWa VR folder somewhere shorter, such as C:\\Games\\WuWa VR.");
        return EXIT_REPORTED;
    }
    if (!is_file(python)) return missing(python);
    if (!is_file(script)) return missing(script);

    /* -I: ignore PYTHON* variables and user site-packages; -B: never write
       bytecode into the package; -X utf8: UTF-8 file and pipe text. */
    if (swprintf(command, sizeof(command) / sizeof(command[0]), L"\"%ls\" -I -B -X utf8 \"%ls\" %ls",
                 python, script, arguments ? arguments : L"") < 0) {
        report(L"The command line is too long.");
        return EXIT_REPORTED;
    }
    ZeroMemory(&startup, sizeof(startup));
    startup.cb = sizeof(startup);
    if (inherit) {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    }
    ZeroMemory(&process, sizeof(process));
    if (!CreateProcessW(python, command, NULL, NULL, inherit, 0, NULL, app, &startup, &process)) {
        swprintf(text, sizeof(text) / sizeof(text[0]),
                 L"Windows could not start the bundled Python (error %lu). Security software may have blocked or "
                 L"removed it. Extract the ZIP again; do not disable security software.", GetLastError());
        report(text);
        return EXIT_REPORTED;
    }
    CloseHandle(process.hThread);
    /* The launcher keeps running as a local page server. Report only an
       early failure that the Python side could not report itself. */
    wait = WaitForSingleObject(process.hProcess, STARTUP_WAIT_MS);
    if (wait == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &code) && code != 0 && code != EXIT_REPORTED &&
        !(inherit && code == 1)) {
        swprintf(text, sizeof(text) / sizeof(text[0]),
                 L"The launcher stopped unexpectedly (exit code %lu). Logs are in %%LOCALAPPDATA%%\\WuWa VR Launcher\\logs. "
                 L"Extract the ZIP again if this repeats.", code);
        report(text);
    }
    CloseHandle(process.hProcess);
    return wait == WAIT_OBJECT_0 ? (int)code : 0;
}
