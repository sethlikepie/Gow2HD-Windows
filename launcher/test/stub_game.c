/* Stub "game" used by the launcher tests: writes what it was started with to stub_game_report.txt and exits. */
#include <windows.h>
#include <stdio.h>

int main(void) {
    char cwd[MAX_PATH], buf[2048];
    GetCurrentDirectoryA(MAX_PATH, cwd);
    FILE* f = fopen("stub_game_report.txt", "w");
    if (!f) return 1;
    fprintf(f, "cwd=%s\n", cwd);
    /* the launcher must pass none of these: the real game configures itself (gow2_port.ini, compiled-in defaults) */
    const char* vars[] = { "PS3_VFS_ROOT", "WINPORT_PORT_SETTINGS", "PYTHONIOENCODING", "GOW2_LAUNCHER_VERSION" };
    for (int i = 0; i < 4; ++i) {
        DWORD n = GetEnvironmentVariableA(vars[i], buf, sizeof(buf));
        fprintf(f, "%s=%s\n", vars[i], n ? buf : "");
    }
    FILE* ini = fopen("port_settings.ini", "r");
    if (ini) {
        fprintf(f, "--- port_settings.ini\n");
        while (fgets(buf, sizeof(buf), ini)) fputs(buf, f);
        fclose(ini);
    }
    fclose(f);
    return 0;
}
