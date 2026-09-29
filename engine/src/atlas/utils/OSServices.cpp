#include "utils/OSServices.hpp"

#include <stdexcept>
#include <string>

#include "core/Log.hpp"

#if defined(ATLAS_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Atlas {
    void OSServices::startProcessAsync(const std::filesystem::path &name, const std::string &args) {
#if defined(ATLAS_PLATFORM_WINDOWS)
        STARTUPINFOA si{};
        si.cb = sizeof(si);

        PROCESS_INFORMATION pi{};
        std::string cmdLine = "\"" + name.string() + "\" " + args;

        if (CreateProcessA( // https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessa
            nullptr, // [in, optional]      LPCSTR                lpApplicationName,
            cmdLine.data(), // [in, out, optional] LPSTR                 lpCommandLine,
            nullptr, // [in, optional]      LPSECURITY_ATTRIBUTES lpProcessAttributes,
            nullptr, // [in, optional]      LPSECURITY_ATTRIBUTES lpThreadAttributes,
            FALSE, // [in]                BOOL                  bInheritHandles,
            0, // [in]                DWORD                 dwCreationFlags,
            nullptr, // [in, optional]      LPVOID                lpEnvironment,
            nullptr, // [in, optional]      LPCSTR                lpCurrentDirectory,
            &si, // [in]                LPSTARTUPINFOA        lpStartupInfo,
            &pi // [out]               LPPROCESS_INFORMATION lpProcessInformation
        )) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        } else {
            throw std::runtime_error(std::format("Process {} with arguments: {} failed: Error message: {}", name.string(), args, GetLastError()));
        }
#endif
    }
}
