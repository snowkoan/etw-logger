# etw-logger

A single-header C++23 logger for Windows 10 and 11 user-mode code that writes to ETW (Event Tracing
for Windows) using TraceLogging. You can watch the events live without WPP, manifests or a
registration step.

```cpp
#include "EtwLogger.h"

int wmain()
{
    etwlog::Init();

    LOG_INFO(L"starting version {}", 3);
    LOG_ERROR(L"failed to open {} -- error is {}", path, etwlog::Hex(hr));
    LOG_WARNING_C(L"Net", L"retry {} of {}", attempt, maxAttempts);

    etwlog::Shutdown();
}
```

- Five levels: Critical, Error, Warning, Info and Verbose.
- `std::format`-style messages whose format strings the compiler checks against the arguments.
- An optional component on each event, with a per-file default.
- The source file, line and function are recorded automatically.
- Levels can be compiled out, and messages can also go to the debugger (`OutputDebugString`).
- Messages are formatted only when a trace session is listening or debugger output is on.

## Requirements

- Windows 10 or 11.
- Visual Studio 2022 with the C++ workload, compiling with `/std:c++latest`. Tested with VS 2022
  17.14 (MSVC 14.44).
- `tools\watch.cmd` uses `tracelog.exe` and `tracefmt.exe` from the Windows SDK.

## Using it in your project

1. Copy `include\EtwLogger.h` into your project, and `tools\watch.cmd` if you want the live viewer.
2. Give your app its own provider name. Use the form `Company.Product[.Component]`.
   `tools\setup.ps1` sets the name and its GUID in your header, and sets the name in your
   `watch.cmd`:

   ```
   powershell -ExecutionPolicy Bypass -File tools\setup.ps1 -ProviderName Contoso.MyApp -Header D:\src\MyApp\EtwLogger.h -Watch D:\src\MyApp\watch.cmd
   ```

   The GUID is the standard hash of the name, the same one .NET `EventSource` uses. ETW tools
   compute it when you enable a provider as `*Contoso.MyApp`.
3. Call `etwlog::Init()` once at startup and `etwlog::Shutdown()` once at exit. Every EXE and DLL
   that logs needs its own calls, and a DLL must call `Shutdown()` before it unloads, for example
   in `DLL_PROCESS_DETACH`.

## Logging

| Default component | Explicit component |
|---|---|
| `LOG_CRITICAL(fmt, ...)` | `LOG_CRITICAL_C(component, fmt, ...)` |
| `LOG_ERROR(fmt, ...)` | `LOG_ERROR_C(component, fmt, ...)` |
| `LOG_WARNING(fmt, ...)` | `LOG_WARNING_C(component, fmt, ...)` |
| `LOG_INFO(fmt, ...)` | `LOG_INFO_C(component, fmt, ...)` |
| `LOG_VERBOSE(fmt, ...)` | `LOG_VERBOSE_C(component, fmt, ...)` |

Format strings use `std::format` syntax and must be wide (`L"..."`). Use `{{` and `}}` for literal
braces. Messages longer than 1023 characters are truncated.

- `std::wstring`, `std::wstring_view` and `wchar_t` pointers work directly. Wrap narrow (UTF-8)
  strings in `etwlog::Narrow`, for example `etwlog::Narrow(e.what())`.
- HRESULT and NTSTATUS values are signed, so `{}` prints them as negative numbers.
  `etwlog::Hex(hr)` prints `0x80070005`.
- Containers format too: `{}` on a `std::vector<int>` prints `[1, 2, 3]`.
- MSVC can't format `std::filesystem::path` yet, so pass `path.native()`.

Each event is named after its level and has `Component`, `Message`, `File`, `Line` and `Function`
fields. ETW records the timestamp, process ID and thread ID itself.

## Settings

Per-app settings are at the top of `EtwLogger.h`:

- `ETWLOG_PROVIDER_NAME` and `ETWLOG_PROVIDER_GUID`. Set both with `tools\setup.ps1`.
- `ETWLOG_DEFAULT_COMPONENT`, the component for `LOG_xxx` calls that don't pass one. The default
  is `L"App"`. A `.cpp` can use a different default by defining it before the `#include`.

Optional settings can be set there or project-wide with `/D`:

- `ETWLOG_MAX_LEVEL`, the most verbose level compiled in. The default is 5 (Verbose), and 0
  compiles out all logging. Calls above this level generate no code, but their format strings are
  still checked.
- `ETWLOG_DEBUG_OUTPUT_LEVEL`, the most verbose level also sent to `OutputDebugString`, where it
  shows in the Visual Studio Output window and DebugView. The default is every level in `_DEBUG`
  builds and none otherwise.

A call that isn't compiled out still evaluates its arguments when nobody is listening; only the
formatting is skipped. Keep expensive work out of log arguments, or put it behind a level that you
compile out.

## Viewing events

From an elevated prompt:

```
tools\watch.cmd [ProviderName] [Level]
```

It prints each event as it arrives, for example:

```
11/21/2026-19:50:58.274 BD98.9D2C {"Component":"App","Message":"error message","File":"EtwLoggerTest.cpp","Line":51,"Function":"wmain","meta":{"event":"Error"}}
```

The line starts with the time and the process and thread IDs in hex. The event name at the end is
the level.

Any ETW tool can collect the events if you enable the provider as `*Contoso.MyApp`. For example, in
PerfView add it under Additional Providers. To record to a file and format it later:

```
tracelog -start MyTrace -f trace.etl -guid *Contoso.MyApp -level 5
tracelog -stop MyTrace
tracefmt trace.etl -displayonly -nosummary
```

## Building the test program

`tests\EtwLoggerTest.cpp` exercises every feature, then logs a heartbeat once a second so there is
something to watch.

```
build.cmd              Debug build; messages also go to the debugger
build.cmd release      Release build
out\EtwLoggerTest.exe  [heartbeat seconds, default 10]
```

`build.cmd` sets up the Visual Studio environment itself, which can be slow. Run it from a
Developer Command Prompt to skip that step.

## Repository layout

```
include\EtwLogger.h      the logger
tools\watch.cmd          live viewer
tools\setup.ps1          sets the provider name and GUID
tests\EtwLoggerTest.cpp  test program
build.cmd                builds the test program into out\
```

## License

MIT. See [LICENSE](LICENSE).
