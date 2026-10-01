typedef struct _SYSTEM_MODULE_ENTRY {
    HANDLE Section;
    PVOID MappedBase;
    PVOID ImageBase;
    ULONG ImageSize;
    ULONG Flags;
    USHORT LoadOrderIndex;
    USHORT InitOrderIndex;
    USHORT LoadCount;
    USHORT OffsetToFileName;
    CHAR ImageName[256];
} SYSTEM_MODULE_ENTRY, * PSYSTEM_MODULE_ENTRY;

typedef struct _SYSTEM_MODULE_INFORMATION {
    ULONG Count;
    SYSTEM_MODULE_ENTRY Module[1];
} SYSTEM_MODULE_INFORMATION, * PSYSTEM_MODULE_INFORMATION;


#define SystemModuleInformation (SYSTEM_INFORMATION_CLASS)11


extern "C" NTSTATUS NTAPI NtQuerySystemInformation(
    SYSTEM_INFORMATION_CLASS SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
);

bool is_driver_mapped() {
    //Query kernel modules directly via ntdll
    ULONG size = 0;
    NtQuerySystemInformation(SystemModuleInformation, NULL, 0, &size);

    std::vector<BYTE> buffer(size);
    NtQuerySystemInformation(SystemModuleInformation, buffer.data(), size, &size);

    auto* modules = (SYSTEM_MODULE_INFORMATION*)buffer.data();
    for (ULONG i = 0; i < modules->Count; i++) {
        if (strstr(modules->Module[i].ImageName, "driver.sys"))
            return true;
    }
    return false;
}

bool get_resource_data(int resource_id, LPVOID* data, DWORD* size) {
    HRSRC resource = FindResource(NULL, MAKEINTRESOURCE(resource_id), RT_RCDATA);

    if (!resource)
        return false;

    HGLOBAL loaded_resources = LoadResource(NULL, resource);

    if (!loaded_resources)
        return false;

    *data = LockResource(loaded_resources);

    if (!*data)
        return false;

    *size = SizeofResource(NULL, resource);

    if (*size == 0)
        return false;

    return true;
}

int main() {

    bool driver_mapped = is_driver_mapped();

    if (!driver_mapped) {
    LPVOID mapper_data = nullptr;
    LPVOID driver_data = nullptr;

    DWORD mapper_size = 0;
    DWORD driver_size = 0;

    if (!get_resource_data(IDR_MAPPER_EXE, &mapper_data, &mapper_size)) {
        printf("FAIL", "failed to load mapper resource");
        return -1;
    }
    printf("OK", "mapper resource loaded  size=%lu", mapper_size);

    if (!get_resource_data(IDR_DRIVER_SYS, &driver_data, &driver_size)) {
        printf("FAIL", "failed to load driver resource");
        return -1;
    }
    printf("OK", "driver resource loaded  size=%lu", driver_size);

    //Steam installation path from registry
    char steam_path[MAX_PATH] = { 0 };
    DWORD path_size = sizeof(steam_path);
    HKEY hkey;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Valve\\Steam", 0, KEY_READ, &hkey) == ERROR_SUCCESS ||
        RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam", 0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        RegQueryValueExA(hkey, "InstallPath", NULL, NULL, (LPBYTE)steam_path, &path_size);
        RegCloseKey(hkey);
    }

    //Fallback to default if registry read failed
    std::string steam_temp_dir;
    if (strlen(steam_path) > 0) {
        steam_temp_dir = std::string(steam_path) + "\\steamapps\\temp";
    }
    else {
        steam_temp_dir = "C:\\Program Files (x86)\\Steam\\steamapps\\temp";
    }

    //Create dir if it doesn't exist
    CreateDirectoryA(steam_temp_dir.c_str(), NULL);

    std::string mapper_temp_path = steam_temp_dir + "\\~m" + std::to_string(GetTickCount64()) + ".exe";
    std::string driver_temp_path = steam_temp_dir + "\\~d" + std::to_string(GetTickCount64()) + ".sys";

    HANDLE mapper_file = CreateFileA(mapper_temp_path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (mapper_file == INVALID_HANDLE_VALUE) {
        dbg::log("FAIL", "failed to create temp mapper file", mapper_temp_path.c_str());
        return -1;
    }
    printf("INFO", "temp mapper path: %s", mapper_temp_path.c_str());

    DWORD written = 0;

    if (!WriteFile(mapper_file, mapper_data, mapper_size, &written, NULL) || written != mapper_size) {
        CloseHandle(mapper_file);
        DeleteFileA(mapper_temp_path.c_str());
        dbg::log("FAIL", "failed to write mapper file");
        return -1;
    }
    printf("OK", "mapper written to disk  bytes=%lu", written);

    CloseHandle(mapper_file);

    HANDLE verify = CreateFileA(mapper_temp_path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

    if (verify == INVALID_HANDLE_VALUE) {
        DeleteFileA(mapper_temp_path.c_str());
        printf("FAIL", "failed to verify mapper file");
        return -1;
    }

    DWORD file_size = GetFileSize(verify, NULL);

    CloseHandle(verify);

    if (file_size != mapper_size) {
        DeleteFileA(mapper_temp_path.c_str());
        printf("FAIL", "mapper size mismatch  on_disk=%lu  expected=%lu", file_size, mapper_size);
        return -1;
    }
    printf("OK", "mapper file verified");

    HANDLE driver_file = CreateFileA(driver_temp_path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (driver_file == INVALID_HANDLE_VALUE) {
        DeleteFileA(mapper_temp_path.c_str());
        printf("FAIL", "failed to create temp driver file", driver_temp_path.c_str());
        return -1;
    }
    printf("INFO", "temp driver path: %s", driver_temp_path.c_str());

    written = 0;

    if (!WriteFile(driver_file, driver_data, driver_size, &written, NULL) || written != driver_size) {
        CloseHandle(driver_file);
        DeleteFileA(mapper_temp_path.c_str());
        DeleteFileA(driver_temp_path.c_str());
        printf("FAIL", "failed to write driver file");
        return -1;
    }
    printf("OK", "driver written to disk  bytes=%lu", written);

    CloseHandle(driver_file);

    verify = CreateFileA(driver_temp_path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

    if (verify == INVALID_HANDLE_VALUE) {
        DeleteFileA(mapper_temp_path.c_str());
        DeleteFileA(driver_temp_path.c_str());
        printf("FAIL", "failed to verify driver file");
        return -1;
    }

    file_size = GetFileSize(verify, NULL);

    CloseHandle(verify);

    if (file_size != driver_size) {
        DeleteFileA(mapper_temp_path.c_str());
        DeleteFileA(driver_temp_path.c_str());
        printf("FAIL", "driver size mismatch  on_disk=%lu  expected=%lu", file_size, driver_size);
        return -1;
    }
    printf("OK", "driver file verified");

    Sleep(1000);

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    std::string cmd_line = "\"" + mapper_temp_path + "\" \"" + driver_temp_path + "\"";

    printf("INFO", "launching mapper: %s", cmd_line.c_str());
    if (!CreateProcessA(NULL, (LPSTR)cmd_line.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        DeleteFileA(mapper_temp_path.c_str());
        DeleteFileA(driver_temp_path.c_str());
        printf("FAIL", "failed to execute mapper  error=%lu", GetLastError());
        return -1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code;

    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    DeleteFileA(mapper_temp_path.c_str());
    DeleteFileA(driver_temp_path.c_str());

    
}
else
    printf("OK", "driver already mapped");
}
