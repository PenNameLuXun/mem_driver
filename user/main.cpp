#include <Windows.h>
#include <DbgHelp.h>
#include <TlHelp32.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../shared/memattrib_shared.h"

namespace {

struct ProcessEntry {
    DWORD pid;
    std::wstring exeName;
};

struct RegionBatch {
    std::vector<MEMATTRIB_REGION_INFO> regions;
    bool hasMore = false;
    std::uint64_t nextAddress = 0;
};

std::wstring ToHex(std::uint64_t value)
{
    std::wstringstream stream;
    stream << L"0x" << std::hex << std::uppercase << value;
    return stream.str();
}

std::wstring DescribeType(ULONG type)
{
    switch (type) {
    case MEM_IMAGE:
        return L"MEM_IMAGE";
    case MEM_MAPPED:
        return L"MEM_MAPPED";
    case MEM_PRIVATE:
        return L"MEM_PRIVATE";
    default:
        return L"UNKNOWN";
    }
}

std::wstring DescribeState(ULONG state)
{
    switch (state) {
    case MEM_COMMIT:
        return L"MEM_COMMIT";
    case MEM_RESERVE:
        return L"MEM_RESERVE";
    case MEM_FREE:
        return L"MEM_FREE";
    default:
        return L"UNKNOWN";
    }
}

std::wstring DescribeProtect(ULONG protect)
{
    const ULONG normalized = protect & 0xFF;

    switch (normalized) {
    case PAGE_EXECUTE:
        return L"EXECUTE";
    case PAGE_EXECUTE_READ:
        return L"EXECUTE_READ";
    case PAGE_EXECUTE_READWRITE:
        return L"EXECUTE_READWRITE";
    case PAGE_EXECUTE_WRITECOPY:
        return L"EXECUTE_WRITECOPY";
    case PAGE_NOACCESS:
        return L"NOACCESS";
    case PAGE_READONLY:
        return L"READONLY";
    case PAGE_READWRITE:
        return L"READWRITE";
    case PAGE_WRITECOPY:
        return L"WRITECOPY";
    default:
        return ToHex(normalized);
    }
}

std::wstring AnsiToWide(const char* text)
{
    if (text == nullptr || text[0] == '\0') {
        return L"";
    }

    const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (length <= 0) {
        return L"(symbol-conversion-failed)";
    }

    std::wstring wide(static_cast<std::size_t>(length) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), length);
    return wide;
}

bool ParseUlong(const std::wstring& text, DWORD& value)
{
    try {
        value = static_cast<DWORD>(std::stoul(text, nullptr, 0));
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseUlong64(const std::wstring& text, std::uint64_t& value)
{
    try {
        value = std::stoull(text, nullptr, 0);
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<ProcessEntry> EnumerateProcesses()
{
    std::vector<ProcessEntry> result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return result;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            result.push_back({ entry.th32ProcessID, entry.szExeFile });
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    std::sort(result.begin(), result.end(), [](const ProcessEntry& left, const ProcessEntry& right) {
        return left.pid < right.pid;
    });
    return result;
}

HANDLE OpenDriver()
{
    return CreateFileW(
        MEMATTRIB_DOS_DEVICE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
}

bool QueryRegion(HANDLE driver, DWORD pid, std::uint64_t address, MEMATTRIB_REGION_INFO& info)
{
    MEMATTRIB_REGION_REQUEST request{};
    DWORD bytesReturned = 0;

    request.ProcessId = pid;
    request.Address = address;

    return DeviceIoControl(
        driver,
        IOCTL_MEMATTRIB_QUERY_REGION,
        &request,
        sizeof(request),
        &info,
        sizeof(info),
        &bytesReturned,
        nullptr
    ) != FALSE;
}

bool SnapshotRegions(HANDLE driver, DWORD pid, std::uint64_t startAddress, ULONG batchCount, RegionBatch& batch)
{
    MEMATTRIB_SNAPSHOT_REQUEST request{};
    const DWORD outputSize = static_cast<DWORD>(
        FIELD_OFFSET(MEMATTRIB_SNAPSHOT_RESPONSE, Regions) +
        sizeof(MEMATTRIB_REGION_INFO) * batchCount
    );
    std::vector<std::byte> buffer(outputSize);
    DWORD bytesReturned = 0;

    request.ProcessId = pid;
    request.StartAddress = startAddress;
    request.MaxRegionCount = batchCount;

    if (!DeviceIoControl(
            driver,
            IOCTL_MEMATTRIB_SNAPSHOT_REGIONS,
            &request,
            sizeof(request),
            buffer.data(),
            outputSize,
            &bytesReturned,
            nullptr)) {
        return false;
    }

    const auto* response = reinterpret_cast<const MEMATTRIB_SNAPSHOT_RESPONSE*>(buffer.data());
    batch.hasMore = response->MoreData != 0;
    batch.nextAddress = response->NextAddress;
    batch.regions.assign(response->Regions, response->Regions + response->RegionCount);
    return true;
}

bool LoadProcessModulesForSymbols(HANDLE process)
{
    DWORD processId = GetProcessId(process);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    if (!Module32FirstW(snapshot, &module)) {
        CloseHandle(snapshot);
        return false;
    }

    do {
        SymLoadModuleExW(
            process,
            nullptr,
            module.szExePath,
            module.szModule,
            reinterpret_cast<DWORD64>(module.modBaseAddr),
            module.modBaseSize,
            nullptr,
            0
        );
    } while (Module32NextW(snapshot, &module));

    CloseHandle(snapshot);
    return true;
}

std::wstring DescribeNearestSymbol(DWORD pid, std::uint64_t address)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) {
        return L"(failed to open process for symbols)";
    }

    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    if (!SymInitialize(process, nullptr, FALSE)) {
        CloseHandle(process);
        return L"(SymInitialize failed)";
    }

    LoadProcessModulesForSymbols(process);

    std::vector<std::byte> symbolBuffer(sizeof(SYMBOL_INFO) + MAX_SYM_NAME);
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer.data());
    DWORD64 displacement = 0;
    std::wstring text = L"(no symbol)";

    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;

    if (SymFromAddr(process, address, &displacement, symbol)) {
        std::wstringstream stream;
        stream << AnsiToWide(symbol->Name);
        if (displacement != 0) {
            stream << L" + 0x" << std::hex << displacement;
        }
        text = stream.str();
    }

    SymCleanup(process);
    CloseHandle(process);
    return text;
}

void PrintUsage()
{
    std::wcout
        << L"MemAttribCli --list\n"
        << L"MemAttribCli --pid <pid> --summary\n"
        << L"MemAttribCli --pid <pid> --regions\n"
        << L"MemAttribCli --pid <pid> --addr <hex-address>\n";
}

void PrintRegion(const MEMATTRIB_REGION_INFO& info)
{
    std::wcout
        << L"Base=" << ToHex(info.BaseAddress)
        << L" AllocationBase=" << ToHex(info.AllocationBase)
        << L" Size=" << ToHex(info.RegionSize)
        << L" State=" << DescribeState(info.State)
        << L" Protect=" << DescribeProtect(info.Protect)
        << L" Type=" << DescribeType(info.Type);

    if (info.MappedFile[0] != L'\0') {
        std::wcout << L" File=" << info.MappedFile;
    }
    std::wcout << L"\n";
}

int RunList()
{
    for (const auto& process : EnumerateProcesses()) {
        std::wcout << std::setw(6) << process.pid << L"  " << process.exeName << L"\n";
    }
    return 0;
}

int RunAddressQuery(HANDLE driver, DWORD pid, std::uint64_t address)
{
    MEMATTRIB_REGION_INFO info{};
    if (!QueryRegion(driver, pid, address, info)) {
        std::wcerr << L"driver query failed, GetLastError=" << GetLastError() << L"\n";
        return 1;
    }

    PrintRegion(info);
    std::wcout << L"NearestSymbol=" << DescribeNearestSymbol(pid, address) << L"\n";
    return 0;
}

std::vector<MEMATTRIB_REGION_INFO> ReadAllRegions(HANDLE driver, DWORD pid)
{
    std::vector<MEMATTRIB_REGION_INFO> result;
    std::uint64_t cursor = 0;

    while (true) {
        RegionBatch batch;
        if (!SnapshotRegions(driver, pid, cursor, 64, batch)) {
            std::wcerr << L"snapshot failed, GetLastError=" << GetLastError() << L"\n";
            break;
        }

        if (batch.regions.empty()) {
            break;
        }

        result.insert(result.end(), batch.regions.begin(), batch.regions.end());
        if (!batch.hasMore || batch.nextAddress <= cursor) {
            break;
        }

        cursor = batch.nextAddress;
    }

    return result;
}

int RunRegions(HANDLE driver, DWORD pid)
{
    const auto regions = ReadAllRegions(driver, pid);
    for (const auto& region : regions) {
        PrintRegion(region);
    }
    std::wcout << L"TotalRegions=" << regions.size() << L"\n";
    return 0;
}

int RunSummary(HANDLE driver, DWORD pid)
{
    const auto regions = ReadAllRegions(driver, pid);
    std::map<std::wstring, std::uint64_t> byOwner;
    std::map<std::wstring, std::uint64_t> byType;
    std::map<std::wstring, std::uint64_t> byProtect;

    for (const auto& region : regions) {
        const std::wstring owner =
            region.MappedFile[0] != L'\0' ? region.MappedFile : DescribeType(region.Type);

        byOwner[owner] += region.RegionSize;
        byType[DescribeType(region.Type)] += region.RegionSize;
        byProtect[DescribeProtect(region.Protect)] += region.RegionSize;
    }

    std::wcout << L"[By Type]\n";
    for (const auto& [key, value] : byType) {
        std::wcout << std::setw(18) << key << L"  " << ToHex(value) << L"\n";
    }

    std::wcout << L"\n[By Protect]\n";
    for (const auto& [key, value] : byProtect) {
        std::wcout << std::setw(18) << key << L"  " << ToHex(value) << L"\n";
    }

    std::vector<std::pair<std::wstring, std::uint64_t>> ownerRows(byOwner.begin(), byOwner.end());
    std::sort(ownerRows.begin(), ownerRows.end(), [](const auto& left, const auto& right) {
        return left.second > right.second;
    });

    std::wcout << L"\n[Top Owners]\n";
    const std::size_t limit = std::min<std::size_t>(ownerRows.size(), 20);
    for (std::size_t index = 0; index < limit; ++index) {
        std::wcout << ToHex(ownerRows[index].second) << L"  " << ownerRows[index].first << L"\n";
    }

    std::wcout << L"\nTotalRegions=" << regions.size() << L"\n";
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    const std::vector<std::wstring> args(argv + 1, argv + argc);
    if (args.size() == 1 && args[0] == L"--list") {
        return RunList();
    }

    DWORD pid = 0;
    bool hasPid = false;
    bool summary = false;
    bool regions = false;
    bool hasAddress = false;
    std::uint64_t address = 0;

    for (std::size_t index = 0; index < args.size(); ++index) {
        if (args[index] == L"--pid" && index + 1 < args.size()) {
            hasPid = ParseUlong(args[++index], pid);
        } else if (args[index] == L"--summary") {
            summary = true;
        } else if (args[index] == L"--regions") {
            regions = true;
        } else if (args[index] == L"--addr" && index + 1 < args.size()) {
            hasAddress = ParseUlong64(args[++index], address);
        }
    }

    if (!hasPid || (!summary && !regions && !hasAddress)) {
        PrintUsage();
        return 1;
    }

    HANDLE driver = OpenDriver();
    if (driver == INVALID_HANDLE_VALUE) {
        std::wcerr << L"failed to open " << MEMATTRIB_DOS_DEVICE_NAME
                   << L", GetLastError=" << GetLastError() << L"\n";
        return 1;
    }

    int exitCode = 0;
    if (summary) {
        exitCode = RunSummary(driver, pid);
    } else if (regions) {
        exitCode = RunRegions(driver, pid);
    } else if (hasAddress) {
        exitCode = RunAddressQuery(driver, pid, address);
    }

    CloseHandle(driver);
    return exitCode;
}
