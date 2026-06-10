#include "engine/detectors/Detectors.h"

#include <windows.h>

#include <comdef.h>
#include <wbemidl.h>

#include <format>
#include <map>
#include <vector>

#include "core/Text.h"
#include "engine/Settings.h"

namespace engine::detectors
{

namespace
{

std::wstring VariantToString(const VARIANT& v)
{
    if (v.vt == VT_NULL || v.vt == VT_EMPTY)
        return L"";
    if (v.vt == VT_BSTR)
        return v.bstrVal ? v.bstrVal : L"";
    VARIANT copy;
    VariantInit(&copy);
    if (SUCCEEDED(VariantChangeType(&copy, const_cast<VARIANT*>(&v), 0, VT_BSTR)))
    {
        std::wstring s = copy.bstrVal ? copy.bstrVal : L"";
        VariantClear(&copy);
        return s;
    }
    return L"";
}

class WmiNamespace
{
public:
    ~WmiNamespace()
    {
        if (services_)
            services_->Release();
        if (locator_)
            locator_->Release();
    }

    bool Connect(const wchar_t* ns)
    {
        if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_IWbemLocator, reinterpret_cast<void**>(&locator_))))
            return false;
        if (FAILED(locator_->ConnectServer(_bstr_t(ns), nullptr, nullptr, nullptr, 0, nullptr,
                                           nullptr, &services_)))
            return false;
        CoSetProxyBlanket(services_, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                          RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        return true;
    }

    // Runs a WQL query; calls cb once per object with a property getter.
    template <typename Cb>
    bool Query(const wchar_t* wql, Cb cb)
    {
        if (!services_)
            return false;
        IEnumWbemClassObject* en = nullptr;
        if (FAILED(services_->ExecQuery(_bstr_t(L"WQL"), _bstr_t(wql),
                                        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                        nullptr, &en)))
            return false;
        for (;;)
        {
            IWbemClassObject* obj = nullptr;
            ULONG ret = 0;
            if (en->Next(static_cast<long>(WBEM_INFINITE), 1, &obj, &ret) != S_OK || ret == 0)
                break;
            auto prop = [obj](const wchar_t* name) -> std::wstring
            {
                VARIANT v;
                VariantInit(&v);
                std::wstring s;
                if (SUCCEEDED(obj->Get(name, 0, &v, nullptr, nullptr)))
                {
                    s = VariantToString(v);
                    VariantClear(&v);
                }
                return s;
            };
            cb(prop);
            obj->Release();
        }
        en->Release();
        return true;
    }

private:
    IWbemLocator* locator_ = nullptr;
    IWbemServices* services_ = nullptr;
};

std::wstring RegistryString(HKEY root, const wchar_t* key, const wchar_t* value)
{
    wchar_t buf[512]{};
    DWORD size = sizeof(buf);
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS)
        return buf;
    return L"";
}

std::wstring OsVersionString()
{
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW info{sizeof(info)};
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
    {
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")))
        {
            if (fn(&info) == 0)
            {
                std::wstring product = RegistryString(
                    HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                    L"ProductName");
                std::wstring display = RegistryString(
                    HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                    L"DisplayVersion");
                return std::format(L"{} {} (build {}.{}.{})", product, display,
                                   info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber);
            }
        }
    }
    return L"unknown";
}

std::wstring PartitionStyleName(const std::wstring& v)
{
    if (v == L"1")
        return L"MBR";
    if (v == L"2")
        return L"GPT";
    return L"RAW/unknown (" + v + L")";
}

}

void WriteSystemInfo(const std::wstring& filePath, EventSink& sink)
{
    sink.OnLog(LogSeverity::Info, L"Collecting system info...");
    std::wstring out;
    out.reserve(16 * 1024);
    auto line = [&](std::wstring s) { out += s + L"\r\n"; };

    // --- machine passport ---
    wchar_t computer[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD clen = MAX_COMPUTERNAME_LENGTH;
    GetComputerNameW(computer, &clen);
    wchar_t user[256]{};
    DWORD ulen = 256;
    GetUserNameW(user, &ulen);

    MEMORYSTATUSEX mem{sizeof(mem)};
    GlobalMemoryStatusEx(&mem);

    TIME_ZONE_INFORMATION tz{};
    GetTimeZoneInformation(&tz);

    SYSTEMTIME st;
    GetLocalTime(&st);

    line(L"=== RarBackuper system info (time capsule) ===");
    line(std::format(L"Generated: {:04}-{:02}-{:02} {:02}:{:02}:{:02}", st.wYear, st.wMonth,
                     st.wDay, st.wHour, st.wMinute, st.wSecond));
    line(L"Computer name: " + std::wstring(computer));
    line(L"User: " + std::wstring(user));
    line(L"OS: " + OsVersionString());
    line(L"CPU: " + RegistryString(HKEY_LOCAL_MACHINE,
                                   L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                                   L"ProcessorNameString"));
    line(std::format(L"RAM: {} MB physical", mem.ullTotalPhys / (1024 * 1024)));
    ULONGLONG up = GetTickCount64() / 1000;
    line(std::format(L"Uptime: {}d {}h {}m", up / 86400, (up % 86400) / 3600, (up % 3600) / 60));
    line(std::format(L"Timezone: {} (UTC offset {} min)", tz.StandardName, -tz.Bias));

    // --- BIOS / motherboard (WMI root\CIMV2) ---
    WmiNamespace cimv2;
    if (cimv2.Connect(L"ROOT\\CIMV2"))
    {
        cimv2.Query(L"SELECT SerialNumber, SMBIOSBIOSVersion, Manufacturer FROM Win32_BIOS",
                    [&](auto prop)
                    {
                        line(L"BIOS: " + prop(L"Manufacturer") + L" " + prop(L"SMBIOSBIOSVersion") +
                             L", serial: " + prop(L"SerialNumber"));
                    });
        cimv2.Query(L"SELECT Manufacturer, Product, SerialNumber FROM Win32_BaseBoard",
                    [&](auto prop)
                    {
                        line(L"Motherboard: " + prop(L"Manufacturer") + L" " + prop(L"Product") +
                             L", serial: " + prop(L"SerialNumber"));
                    });
    }
    else
    {
        line(L"BIOS/motherboard: WMI unavailable");
    }

    // --- drives map with partition table detail (root\Microsoft\Windows\Storage) ---
    line(L"");
    line(L"=== Drives map (for recovery tooling) ===");
    WmiNamespace storage;
    if (storage.Connect(L"ROOT\\Microsoft\\Windows\\Storage"))
    {
        struct Part
        {
            std::wstring text;
        };
        std::map<int, std::vector<std::wstring>> partsByDisk;
        storage.Query(
            L"SELECT DiskNumber, PartitionNumber, DriveLetter, Offset, Size, GptType, Guid, "
            L"MbrType FROM MSFT_Partition",
            [&](auto prop)
            {
                int disk = _wtoi(prop(L"DiskNumber").c_str());
                // WMI returns DriveLetter as a char16 -> numeric string ("67" == 'C')
                std::wstring rawLetter = prop(L"DriveLetter");
                wchar_t letterCh = 0;
                int code = _wtoi(rawLetter.c_str());
                if (code >= L'A' && code <= L'Z')
                    letterCh = static_cast<wchar_t>(code);
                else if (rawLetter.size() == 1 && iswalpha(rawLetter[0]))
                    letterCh = rawLetter[0];
                std::wstring letter = letterCh ? std::wstring(1, letterCh) + L":" : L"";
                std::wstring fs, label, serial, cluster;
                if (letterCh)
                {
                    std::wstring root = std::wstring(1, letterCh) + L":\\";
                    wchar_t volName[MAX_PATH]{}, fsName[64]{};
                    DWORD volSerial = 0;
                    if (GetVolumeInformationW(root.c_str(), volName, MAX_PATH, &volSerial, nullptr,
                                              nullptr, fsName, 64))
                    {
                        fs = fsName;
                        label = volName;
                        serial = std::format(L"{:04X}-{:04X}", volSerial >> 16, volSerial & 0xFFFF);
                    }
                    DWORD spc = 0, bps = 0, fc = 0, tc = 0;
                    if (GetDiskFreeSpaceW(root.c_str(), &spc, &bps, &fc, &tc))
                        cluster = std::format(L"{}", spc * bps);
                }
                std::wstring gpt = prop(L"GptType");
                std::wstring typeInfo =
                    !gpt.empty() ? std::format(L"type GUID {}, unique GUID {}", gpt, prop(L"Guid"))
                                 : std::format(L"MBR type {}", prop(L"MbrType"));
                partsByDisk[disk].push_back(std::format(
                    L"  Partition {}: {}, offset {} bytes, size {} bytes, letter {}, fs {}, "
                    L"cluster {}, volume serial {}, label \"{}\"",
                    prop(L"PartitionNumber"), typeInfo, prop(L"Offset"), prop(L"Size"),
                    letter.empty() ? L"-" : letter, fs.empty() ? L"-" : fs,
                    cluster.empty() ? L"-" : cluster, serial.empty() ? L"-" : serial, label));
            });
        bool anyDisk = false;
        storage.Query(
            L"SELECT Number, Model, SerialNumber, Size, LogicalSectorSize, PhysicalSectorSize, "
            L"PartitionStyle FROM MSFT_Disk",
            [&](auto prop)
            {
                anyDisk = true;
                int n = _wtoi(prop(L"Number").c_str());
                line(std::format(L"Disk {}: model \"{}\", serial \"{}\", size {} bytes, "
                                 L"sector {}/{} (logical/physical), partition style {}",
                                 n, prop(L"Model"), prop(L"SerialNumber"), prop(L"Size"),
                                 prop(L"LogicalSectorSize"), prop(L"PhysicalSectorSize"),
                                 PartitionStyleName(prop(L"PartitionStyle"))));
                for (const auto& p : partsByDisk[n])
                    line(p);
            });
        if (!anyDisk)
            line(L"No disks reported by the Storage WMI namespace");
    }
    else
    {
        line(L"Drives map: Storage WMI namespace unavailable");
    }

    // --- BitLocker status (usually needs elevation; degrade gracefully) ---
    line(L"");
    WmiNamespace bitlocker;
    bool blOk = false;
    if (bitlocker.Connect(L"ROOT\\CIMV2\\Security\\MicrosoftVolumeEncryption"))
    {
        blOk = bitlocker.Query(L"SELECT DriveLetter, ProtectionStatus FROM Win32_EncryptableVolume",
                               [&](auto prop)
                               {
                                   std::wstring status = prop(L"ProtectionStatus");
                                   line(L"BitLocker " + prop(L"DriveLetter") + L": " +
                                        (status == L"1"   ? L"ON"
                                         : status == L"0" ? L"off"
                                                          : L"unknown(" + status + L")"));
                               });
    }
    if (!blOk)
        line(L"BitLocker status: unavailable without elevation (app runs unelevated by design)");

    if (engine::WriteFileUtf8(filePath, core::WideToUtf8(out)))
        sink.OnLog(LogSeverity::Info, L"System info written to " + filePath);
    else
        sink.OnLog(LogSeverity::Warn, L"Could not write " + filePath);
}

}
