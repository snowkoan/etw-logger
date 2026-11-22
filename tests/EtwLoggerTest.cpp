// EtwLoggerTest.cpp - exercises EtwLogger.h.
//
// Build:  build.cmd            (Debug: also mirrors messages to the debugger)
//         build.cmd release
// Watch:  tools\watch.cmd      (from an elevated prompt, before or while this runs)
// Run:    out\EtwLoggerTest.exe [heartbeat seconds, default 10]

#include "EtwLogger.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <print>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    bool OpenFile(const std::wstring& path)
    {
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            const DWORD error = GetLastError();
            LOG_ERROR(L"failed to open {} -- error is {} ({})", path, error, etwlog::Hex(HRESULT_FROM_WIN32(error)));
            // LOG_ERROR(L"failed to open {:d}", path);  // compile error: {:d} doesn't apply to a string
            return false;
        }

        LOG_VERBOSE(L"opened {}", path);
        CloseHandle(file);
        return true;
    }
}

int wmain(int argc, wchar_t* argv[])
{
    const int seconds = argc > 1 ? _wtoi(argv[1]) : 10;

    const HRESULT hr = etwlog::Init();
    std::println("etwlog::Init() returned 0x{:08X}", static_cast<unsigned>(hr));
    std::println("etwlog::Init() again returned 0x{:08X} (S_FALSE: already initialized)",
                 static_cast<unsigned>(etwlog::Init()));

    // One event per level, default component.
    LOG_CRITICAL(L"critical message");
    LOG_ERROR(L"error message");
    LOG_WARNING(L"warning message");
    LOG_INFO(L"info message");
    LOG_VERBOSE(L"verbose message");

    // Explicit component.
    LOG_INFO_C(L"Net", L"connecting to {}:{}", L"example.com", 443);
    LOG_WARNING_C(L"Net", L"retry {} of {}", 2, 5);

    // Format specs, literal braces, pointers, and C++23 container formatting.
    LOG_INFO(L"hex {:#x}, padded [{:>6}], double {:.3f}, pointer {}, {{braces}}", 0xBEEF, 42, 3.14159,
             static_cast<const void*>(&hr));
    LOG_INFO(L"vector {}, map {}", std::vector{1, 2, 3}, std::map<std::wstring, int>{{L"a", 1}, {L"b", 2}});

    // HRESULT and NTSTATUS are signed: {} prints a negative number, etwlog::Hex the familiar form.
    const LONG status = static_cast<LONG>(0xC0000034); // STATUS_OBJECT_NAME_NOT_FOUND
    LOG_WARNING(L"status with {{}}: {}, with etwlog::Hex: {}", status, etwlog::Hex(status));

    // Narrow (UTF-8) strings go through etwlog::Narrow.
    LOG_INFO(L"narrow UTF-8 text: {}", etwlog::Narrow("caf\xC3\xA9"));
    try
    {
        (void)std::vector<int>{}.at(1);
    }
    catch (const std::exception& e)
    {
        LOG_ERROR(L"caught exception: {}", etwlog::Narrow(e.what()));
        // LOG_ERROR(L"caught exception: {}", e.what());  // compile error: wrap it in etwlog::Narrow
    }

    // Paths go through .native() until MSVC can format std::filesystem::path.
    const std::filesystem::path exe = argv[0];
    LOG_INFO(L"running {}", exe.filename().native());

    // A real failure, logged the way you'd log it in an app.
    OpenFile(L"C:\\does\\not\\exist\\config.json");
    OpenFile(argv[0]);

    // Messages longer than 1023 characters are truncated.
    LOG_VERBOSE(L"long message: {}", std::wstring(2000, L'x'));

    // Several threads at once; each event's thread ID comes from ETW.
    {
        std::vector<std::jthread> workers;
        for (int i = 0; i < 4; ++i)
        {
            workers.emplace_back([i] {
                for (int step = 1; step <= 3; ++step)
                {
                    LOG_INFO_C(L"Worker", L"worker {} step {}", i, step);
                }
            });
        }
    }

    // A steady stream of events to watch in real time.
    std::println("Logging a heartbeat every second for {} seconds...", seconds);
    for (int i = 1; i <= seconds; ++i)
    {
        LOG_INFO(L"heartbeat {} of {}", i, seconds);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    etwlog::Shutdown();
    std::println("Done.");
    return 0;
}
