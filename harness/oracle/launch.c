/*
 * oracle_launch.exe — start terranx.exe suspended, inject oracle.dll, resume.
 * Usage (under wine, from the game run directory):
 *   oracle_launch.exe [path\to\oracle.dll] [extra game args...]
 * Exit code is the game's exit code so the harness can wait on it.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *what) {
    fprintf(stderr, "oracle_launch: %s failed (err %lu)\n", what, GetLastError());
    return 1;
}

int main(int argc, char **argv) {
    char dll[MAX_PATH];
    char cmdline[1024] = "terranx.exe";
    GetFullPathNameA(argc > 1 ? argv[1] : "oracle.dll", sizeof(dll), dll, NULL);
    for (int i = 2; i < argc; i++) {
        strncat(cmdline, " ", sizeof(cmdline) - strlen(cmdline) - 1);
        strncat(cmdline, argv[i], sizeof(cmdline) - strlen(cmdline) - 1);
    }
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessA("terranx.exe", cmdline, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi))
        return fail("CreateProcess");
    LPVOID load = (LPVOID)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    LPVOID arg = VirtualAllocEx(pi.hProcess, NULL, sizeof(dll), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!arg || !WriteProcessMemory(pi.hProcess, arg, dll, sizeof(dll), NULL))
        return fail("WriteProcessMemory");
    HANDLE th = CreateRemoteThread(pi.hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)load, arg, 0, NULL);
    if (!th)
        return fail("CreateRemoteThread");
    WaitForSingleObject(th, 20000);
    DWORD mod = 0;
    GetExitCodeThread(th, &mod);
    if (!mod) {
        TerminateProcess(pi.hProcess, 1);
        fprintf(stderr, "oracle_launch: LoadLibrary(%s) failed in target\n", dll);
        return 1;
    }
    ResumeThread(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    return (int)code;
}
