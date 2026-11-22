// EtwLogger.h - single-header ETW logger (TraceLogging) for user-mode C++23 on Windows 10/11.
// Copyright (c) 2026 Alnoor Allidina. SPDX-License-Identifier: MIT
//
// Usage:
//     etwlog::Init();                                    // once per EXE/DLL, at startup
//     LOG_ERROR(L"failed to open {} -- error is {}", path, etwlog::Hex(status));
//     LOG_WARNING_C(L"Net", L"retry {} of {}", i, n);    // explicit component
//     etwlog::Shutdown();                                // once per EXE/DLL, at exit
//
//   Macros: LOG_CRITICAL, LOG_ERROR, LOG_WARNING, LOG_INFO, LOG_VERBOSE, plus a _C variant of
//   each that takes a component first.
//
//   Format strings use std::format syntax and must be wide (L"..."). The compiler checks them
//   against the arguments. Use {{ and }} for literal braces.
//   - std::wstring, std::wstring_view and wchar_t pointers work directly. Wrap narrow (UTF-8)
//     strings in etwlog::Narrow, e.g. etwlog::Narrow(e.what()).
//   - HRESULT and NTSTATUS are signed, so {} prints them as negative numbers; etwlog::Hex(hr)
//     prints 0x80070005.
//   - Containers format too: {} on a std::vector<int> prints [1, 2, 3].
//
//   Each event is named after its level and has Component, Message, File, Line and Function
//   fields; the timestamp, process ID and thread ID come from ETW itself. Messages are only
//   formatted when a trace session is listening at that level (or debugger output is on), and
//   are truncated at 1023 characters.
//
// Viewing: run watch.cmd from an elevated prompt, or enable "*<ETWLOG_PROVIDER_NAME>" in any
// ETW tool (tracelog, WPR, PerfView, ...).

#pragma once

// ============================================================================
// PER-APP SETTINGS: change these for each app (or DLL) that uses this header.
// ============================================================================

// Provider name and GUID. Set both, and the name in your copy of watch.cmd, with setup.ps1 from
// the etw-logger repository:
//     powershell -ExecutionPolicy Bypass -File tools\setup.ps1 -ProviderName Contoso.MyApp
//         -Header <this file> -Watch <your watch.cmd>
//
// Name convention: "Company.Product[.Component]". The GUID must be the standard ETW hash of the
// name (the same one .NET EventSource uses) so tools can enable the provider by name. To compute
// it by hand in PowerShell: [System.Diagnostics.Tracing.EventSource]::new('<name>').Guid
#define ETWLOG_PROVIDER_NAME "MyCompany.MyApp"
// {2fd875c5-68ac-5b77-5bc7-bace5bac28f4}
#define ETWLOG_PROVIDER_GUID (0x2fd875c5, 0x68ac, 0x5b77, 0x5b, 0xc7, 0xba, 0xce, 0x5b, 0xac, 0x28, 0xf4)

// Component for LOG_xxx calls that don't pass one. A .cpp can use a different default by
// defining ETWLOG_DEFAULT_COMPONENT before including this header.
#ifndef ETWLOG_DEFAULT_COMPONENT
#define ETWLOG_DEFAULT_COMPONENT L"App"
#endif

// ============================================================================
// OPTIONAL SETTINGS: override here, or project-wide with /D on the compiler command line.
// Levels: 1=Critical 2=Error 3=Warning 4=Info 5=Verbose.
// ============================================================================

// Most verbose level compiled into the binary. Calls above it are compiled out: their format
// strings are still checked, but their arguments are never evaluated. 0 compiles out all logging.
#ifndef ETWLOG_MAX_LEVEL
#define ETWLOG_MAX_LEVEL 5
#endif

// Most verbose level also sent to OutputDebugString (VS Output window, DebugView). 0 is off.
#ifndef ETWLOG_DEBUG_OUTPUT_LEVEL
#ifdef _DEBUG
#define ETWLOG_DEBUG_OUTPUT_LEVEL 5
#else
#define ETWLOG_DEBUG_OUTPUT_LEVEL 0
#endif
#endif

// ============================================================================

#include <windows.h>
#include <TraceLoggingProvider.h>

#include <algorithm>
#include <atomic>
#include <concepts>
#include <format>
#include <string_view>
#include <type_traits>
#include <utility>

#pragma comment(lib, "advapi32.lib")

namespace etwlog
{
    enum class Level : UCHAR
    {
        Critical = 1,
        Error = 2,
        Warning = 3,
        Info = 4,
        Verbose = 5,
    };

    // Lets a narrow (UTF-8) string be used in a wide format string: etwlog::Narrow(e.what()).
    struct Narrow
    {
        explicit Narrow(std::string_view utf8) noexcept : text(utf8) {}
        std::string_view text;
    };

    // Formats an integer as zero-padded hex of its full width, treating signed values (HRESULT,
    // NTSTATUS) as unsigned: etwlog::Hex(hr) prints 0x80070005.
    template <std::integral T>
    struct Hex
    {
        explicit constexpr Hex(T number) noexcept : value(number) {}
        T value;
    };

    namespace detail
    {
        inline constexpr size_t MaxMessageChars = 1024;
    }
} // namespace etwlog

template <>
struct std::formatter<etwlog::Narrow, wchar_t> : std::formatter<std::wstring_view, wchar_t>
{
    template <typename FormatContext>
    auto format(etwlog::Narrow narrow, FormatContext& context) const
    {
        // UTF-16 never needs more code units than UTF-8 has bytes, so the buffer can't overflow.
        wchar_t buffer[etwlog::detail::MaxMessageChars];
        const int bytes = static_cast<int>((std::min)(narrow.text.size(), std::size(buffer)));
        const int chars = MultiByteToWideChar(CP_UTF8, 0, narrow.text.data(), bytes, buffer, bytes);
        return std::formatter<std::wstring_view, wchar_t>::format(std::wstring_view(buffer, chars), context);
    }
};

template <typename T>
struct std::formatter<etwlog::Hex<T>, wchar_t>
{
    constexpr auto parse(std::wformat_parse_context& context) { return context.begin(); }

    template <typename FormatContext>
    auto format(etwlog::Hex<T> hex, FormatContext& context) const
    {
        return std::format_to(context.out(), L"0x{:0{}X}", static_cast<std::make_unsigned_t<T>>(hex.value),
                              sizeof(T) * 2);
    }
};

namespace etwlog
{
    namespace detail
    {
        // Defined inside an inline function so every .cpp in an EXE/DLL shares one provider
        // without needing a separate definition file.
        inline TraceLoggingHProvider Provider() noexcept
        {
            TRACELOGGING_DEFINE_PROVIDER_STORAGE(storage, ETWLOG_PROVIDER_NAME, ETWLOG_PROVIDER_GUID);
            return &storage;
        }

        // Registering an already-registered provider can crash, so Init checks this first.
        inline std::atomic<bool> registered{false};

        constexpr const char* FileName(const char* path) noexcept
        {
            const char* name = path;
            for (const char* p = path; *p != '\0'; ++p)
            {
                if (*p == '\\' || *p == '/')
                {
                    name = p + 1;
                }
            }
            return name;
        }

        constexpr const wchar_t* LevelName(Level level) noexcept
        {
            switch (level)
            {
            case Level::Critical: return L"Critical";
            case Level::Error: return L"Error";
            case Level::Warning: return L"Warning";
            case Level::Info: return L"Info";
            case Level::Verbose: return L"Verbose";
            }
            return L"?";
        }

        inline void Emit(Level level, const wchar_t* component, const wchar_t* message, const char* file,
                         unsigned line, const char* function) noexcept
        {
            // TraceLoggingWrite needs the event name and level as literals, hence one call per level.
#define ETWLOG_EMIT_(name, value)                                    \
    TraceLoggingWrite(Provider(), name, TraceLoggingLevel(value),    \
                      TraceLoggingWideString(component, "Component"), \
                      TraceLoggingWideString(message, "Message"),     \
                      TraceLoggingString(file, "File"),               \
                      TraceLoggingUInt32(line, "Line"),               \
                      TraceLoggingString(function, "Function"))

            switch (level)
            {
            case Level::Critical: ETWLOG_EMIT_("Critical", 1); break;
            case Level::Error: ETWLOG_EMIT_("Error", 2); break;
            case Level::Warning: ETWLOG_EMIT_("Warning", 3); break;
            case Level::Info: ETWLOG_EMIT_("Info", 4); break;
            case Level::Verbose: ETWLOG_EMIT_("Verbose", 5); break;
            }
#undef ETWLOG_EMIT_
        }

        template <typename... Args>
        void Write(Level level, bool toDebugger, const wchar_t* component, const char* file, unsigned line,
                   const char* function, std::wformat_string<Args...> format, Args&&... args) noexcept
        {
            const bool toEtw = TraceLoggingProviderEnabled(Provider(), std::to_underlying(level), 0);
            if (!toEtw && !toDebugger)
            {
                return;
            }

            wchar_t message[MaxMessageChars];
            try
            {
                *std::format_to_n(message, MaxMessageChars - 1, format, std::forward<Args>(args)...).out = L'\0';
            }
            catch (...)
            {
                // Only arguments that are invalid at run time (such as a negative dynamic width) or a
                // throwing custom formatter get here; log the unformatted string instead.
                *std::format_to_n(message, MaxMessageChars - 1, L"{}", format.get()).out = L'\0';
            }

            if (toEtw)
            {
                Emit(level, component, message, FileName(file), line, function);
            }

            if (toDebugger)
            {
                // "file(line): " makes the line double-clickable in the VS Output window.
                wchar_t text[MaxMessageChars + 512];
                *std::format_to_n(text, std::size(text) - 1, L"{}({}): [{}] {}: {}\n", Narrow(file), line,
                                  LevelName(level), component, std::wstring_view(message))
                     .out = L'\0';
                OutputDebugStringW(text);
            }
        }
    } // namespace detail

    // Registers the provider. Call once per EXE/DLL before logging; ETW events logged before
    // Init or after Shutdown are dropped. Returns S_FALSE if already initialized. A failure
    // HRESULT can usually be ignored: logging just does nothing.
    inline HRESULT Init() noexcept
    {
        if (detail::registered.exchange(true))
        {
            return S_FALSE;
        }

        const HRESULT hr = TraceLoggingRegister(detail::Provider());
        if (FAILED(hr))
        {
            detail::registered = false;
        }
        return hr;
    }

    // Unregisters the provider. A DLL must call this before it unloads (for example in
    // DLL_PROCESS_DETACH), or ETW may call into unloaded code. Don't call Init and Shutdown
    // concurrently.
    inline void Shutdown() noexcept
    {
        if (detail::registered.exchange(false))
        {
            TraceLoggingUnregister(detail::Provider());
        }
    }
} // namespace etwlog

#define ETWLOG_LOG_(level, component, ...)                                                           \
    do                                                                                               \
    {                                                                                                \
        if constexpr (::std::to_underlying(level) <= ETWLOG_MAX_LEVEL)                               \
        {                                                                                            \
            ::etwlog::detail::Write(level, ::std::to_underlying(level) <= ETWLOG_DEBUG_OUTPUT_LEVEL, \
                                    component, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__);       \
        }                                                                                            \
    } while (0)

#define LOG_CRITICAL(...) ETWLOG_LOG_(::etwlog::Level::Critical, ETWLOG_DEFAULT_COMPONENT, __VA_ARGS__)
#define LOG_ERROR(...)    ETWLOG_LOG_(::etwlog::Level::Error, ETWLOG_DEFAULT_COMPONENT, __VA_ARGS__)
#define LOG_WARNING(...)  ETWLOG_LOG_(::etwlog::Level::Warning, ETWLOG_DEFAULT_COMPONENT, __VA_ARGS__)
#define LOG_INFO(...)     ETWLOG_LOG_(::etwlog::Level::Info, ETWLOG_DEFAULT_COMPONENT, __VA_ARGS__)
#define LOG_VERBOSE(...)  ETWLOG_LOG_(::etwlog::Level::Verbose, ETWLOG_DEFAULT_COMPONENT, __VA_ARGS__)

#define LOG_CRITICAL_C(component, ...) ETWLOG_LOG_(::etwlog::Level::Critical, component, __VA_ARGS__)
#define LOG_ERROR_C(component, ...)    ETWLOG_LOG_(::etwlog::Level::Error, component, __VA_ARGS__)
#define LOG_WARNING_C(component, ...)  ETWLOG_LOG_(::etwlog::Level::Warning, component, __VA_ARGS__)
#define LOG_INFO_C(component, ...)     ETWLOG_LOG_(::etwlog::Level::Info, component, __VA_ARGS__)
#define LOG_VERBOSE_C(component, ...)  ETWLOG_LOG_(::etwlog::Level::Verbose, component, __VA_ARGS__)
