#include "localization.h"
#include <windows.h>
#include <windowsx.h>
#include <bcrypt.h>
#include <array>
#include <commdlg.h>
#include <richedit.h>
#include <commctrl.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <regex>
#include <thread>
#include <unordered_map>
#include <utility>

using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Storage::Streams;

struct Settings {
    std::wstring rx = L"0000FFF1-0000-1000-8000-00805F9B34FB";
    std::wstring tx = L"0000FFF2-0000-1000-8000-00805F9B34FB";
    int encoding = 0, ending = 0;
    bool sound = false, token = true;
    std::wstring passwordVerifier;
} settings;
struct Device { uint64_t address; BluetoothAddressType type; std::wstring name; int rssi; };
enum class ScanPhase { Checking, Scanning, Failed };
struct ScanState {
    std::mutex mutex;
    std::condition_variable changed;
    bool canceled = false;
    bool received = false;
    ScanPhase phase = ScanPhase::Checking;
    std::wstring error;
    std::unordered_map<uint64_t, Device> devices;
};
struct Session {
    BluetoothLEDevice device{ nullptr };
    std::vector<GattDeviceService> services;
    GattCharacteristic rx{ nullptr }, tx{ nullptr };
    std::atomic_bool canceled = false;
    event_token data{}, state{};
    ~Session() {
        if (rx && data.value) rx.ValueChanged(data);
        if (device && state.value) device.ConnectionStatusChanged(state);
        for (auto const& service : services) try { service.Close(); } catch (...) {}
        if (device) try { device.Close(); } catch (...) {}
    }
};

enum Id { Connect = 101, Setup, Devices, Receive, Copy, Token, ClearRx, Input, ClearTx, Send };
HWND window, controls[Send + 1], statusText, progress, rxHeader, txHeader;
HFONT font;
UINT dpi = 96;
bool ready, busy, scanning, scanVisible;
std::wstring deviceName, logText, configPath, fileName;
std::vector<uint8_t> fileBytes, pendingBytes;
std::shared_ptr<Session> session;
std::shared_ptr<ScanState> scan;
ULONGLONG scanDeadline;
bool scanListDrawn;
std::unordered_map<uint64_t, Device> found;
std::unordered_map<uint64_t, std::wstring> names;
std::vector<Device> rows;
std::mutex queueLock;
std::vector<std::function<void()>> callbacks;
std::atomic_bool alive = true;
ULONGLONG lastSound;
constexpr UINT Dispatch = WM_APP + 1;
constexpr UINT_PTR ScanTimer = 3;
constexpr wchar_t WindowTitle[] = L"OBDcomm · BLE · 1.1.4";
wchar_t scanLogPath[MAX_PATH]{};

void post(std::function<void()> callback) {
    std::lock_guard lock(queueLock);
    if (!alive) return;
    callbacks.push_back(std::move(callback));
    PostMessageW(window, Dispatch, 0, 0);
}
void set(HWND control, const std::wstring& text) { SetWindowTextW(control, text.c_str()); }
std::wstring read(HWND control) {
    std::wstring text(GetWindowTextLengthW(control) + 1, L'\0');
    text.resize(GetWindowTextW(control, text.data(), static_cast<int>(text.size())));
    return text;
}
void alert(const std::wstring& text) { MessageBoxW(window, text.c_str(), L"OBDcomm", MB_OK | MB_ICONINFORMATION); }
void layout();
void refresh(const std::wstring& message) {
    set(statusText, message);
    set(controls[Connect], ready ? tr(Text::Disconnect) :
        session ? tr(Text::CancelConnection) :
        scanning ? tr(Text::StopScan) : tr(Text::Connect));
    EnableWindow(controls[Send], ready && !busy);
    layout();
}
void disconnect(const std::wstring& message) {
    if (session) session->canceled = true;
    session.reset();
    KillTimer(window, 2);
    ready = busy = false;
    pendingBytes.clear();
    lastSound = 0;
    refresh(message);
}
void receive(const std::vector<uint8_t>& bytes) {
    if (settings.sound && !bytes.empty() && (!lastSound || GetTickCount64() - lastSound >= 1500)) {
        MessageBeep(MB_OK);
        lastSound = GetTickCount64();
    }
    pendingBytes.insert(pendingBytes.end(), bytes.begin(), bytes.end());
    size_t complete = 0;
    while (complete < pendingBytes.size()) {
        auto first = pendingBytes[complete];
        size_t length = 1;
        if (settings.encoding) {
            if (first >= 0x81 && first <= 0xfe) length = 2;
        } else if (first >= 0xc2 && first <= 0xf4) {
            length = first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
        }
        size_t next = 1;
        while (next < length && complete + next < pendingBytes.size()) {
            auto byte = pendingBytes[complete + next];
            if (settings.encoding ? (byte < 0x40 || byte > 0xfe || byte == 0x7f) : (byte & 0xc0) != 0x80) break;
            ++next;
        }
        if (next < length && complete + next == pendingBytes.size()) break;
        complete += next;
    }
    if (!complete) return;
    UINT cp = settings.encoding ? 936 : CP_UTF8;
    auto data = reinterpret_cast<const char*>(pendingBytes.data());
    int count = MultiByteToWideChar(cp, 0, data, static_cast<int>(complete), nullptr, 0);
    std::wstring decoded(count, L'\0');
    MultiByteToWideChar(cp, 0, data, static_cast<int>(complete), decoded.data(), count);
    pendingBytes.erase(pendingBytes.begin(), pendingBytes.begin() + complete);
    logText += decoded;
    HWND box = controls[Receive];
    if (logText.size() > 131072) {
        size_t trim = logText.size() - 131072;
        if (logText[trim] >= 0xdc00 && logText[trim] <= 0xdfff) ++trim;
        logText.erase(0, trim);
        set(box, logText);
    } else {
        SendMessageW(box, EM_SETSEL, static_cast<WPARAM>(-1), -1);
        SendMessageW(box, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(decoded.c_str()));
    }
    SendMessageW(box, EM_SETSEL, static_cast<WPARAM>(-1), -1);
    SendMessageW(box, EM_SCROLLCARET, 0, 0);
}
void stopScan() {
    scanning = false;
    KillTimer(window, ScanTimer);
    if (!scan) return;
    auto current = std::exchange(scan, nullptr);
    {
        std::lock_guard lock(current->mutex);
        current->canceled = true;
    }
    current->changed.notify_one();
}
std::wstring addressText(uint64_t address) {
    constexpr wchar_t hex[] = L"0123456789ABCDEF";
    std::wstring result = L"00:00:00:00:00:00";
    for (int index = 5; index >= 0; --index) {
        result[index * 3] = hex[(address >> 4) & 15];
        result[index * 3 + 1] = hex[address & 15];
        address >>= 8;
    }
    return result;
}
void updateDevices(std::unordered_map<uint64_t, Device>& devices) {
    if (devices.empty()) return;
    for (auto& [address, item] : devices) {
        if (!item.name.empty()) names[address] = item.name;
        if (names.contains(address)) item.name = names[address];
        found.insert_or_assign(address, std::move(item));
    }
    HWND list = controls[Devices];
    const int selected = ListBox_GetCurSel(list);
    const int top = ListBox_GetTopIndex(list);
    const auto address = selected >= 0 && static_cast<size_t>(selected) < rows.size()
        ? rows[selected].address : UINT64_MAX;
    rows.clear();
    for (auto const& [key, item] : found) rows.push_back(item);
    std::sort(rows.begin(), rows.end(), [](auto const& a, auto const& b) {
        if (a.name.empty() != b.name.empty()) return !a.name.empty();
        if (a.name != b.name) return a.name < b.name;
        return a.address < b.address;
    });
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListBox_ResetContent(list);
    for (size_t index = 0; index < rows.size(); ++index) {
        auto const& item = rows[index];
        auto label = (item.name.empty() ? tr(Text::UnnamedDevice) : item.name) +
            L"    " + addressText(item.address) + L"    " + std::to_wstring(item.rssi) + L" dBm";
        ListBox_AddString(list, label.c_str());
        if (item.address == address) ListBox_SetCurSel(list, index);
    }
    ListBox_SetTopIndex(list, top);
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
    set(statusText, tr(Text::ScanningCount) + std::to_wstring(rows.size()));
}
void traceScan(const char* line) noexcept {
    if (!scanLogPath[0]) return;
    HANDLE file = CreateFileW(scanLogPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
    CloseHandle(file);
}
LONG WINAPI logUnhandledException(EXCEPTION_POINTERS* exception) {
    const auto& record = *exception->ExceptionRecord;
    char module[MAX_PATH]{};
    HMODULE image = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(record.ExceptionAddress), &image)) {
        GetModuleFileNameA(image, module, ARRAYSIZE(module));
    }
    module[ARRAYSIZE(module) - 1] = '\0';
    const char* filename = strrchr(module, '\\');
    const auto address = reinterpret_cast<uintptr_t>(record.ExceptionAddress);
    const auto base = reinterpret_cast<uintptr_t>(image);
    char line[MAX_PATH + 96];
    sprintf_s(line, "Unhandled exception 0x%08lX in %s at offset 0x%llX\r\n",
        record.ExceptionCode, filename ? filename + 1 : module, static_cast<unsigned long long>(address - base));
    traceScan(line);
    return EXCEPTION_CONTINUE_SEARCH;
}
void prepareScanLog() {
    static bool prepared = false;
    if (prepared) return;
    prepared = true;
    DWORD length = GetTempPathW(ARRAYSIZE(scanLogPath), scanLogPath);
    constexpr wchar_t filename[] = L"OBDcomm-scan.log";
    if (!length || length > ARRAYSIZE(scanLogPath) - ARRAYSIZE(filename)) {
        scanLogPath[0] = L'\0';
        return;
    }
    memcpy(scanLogPath + length, filename, sizeof(filename));
    HANDLE file = CreateFileW(scanLogPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    SetUnhandledExceptionFilter(logUnhandledException);
    traceScan("OBDcomm Windows 1.1.4\r\n");
}
std::wstring scanStoppedMessage(BluetoothError error) {
    switch (error) {
    case BluetoothError::DisabledByUser:
    case BluetoothError::RadioNotAvailable:
        return tr(Text::BluetoothOff);
    case BluetoothError::NotSupported:
    case BluetoothError::TransportNotSupported:
        return tr(Text::ScanUnsupported);
    case BluetoothError::DisabledByPolicy:
    case BluetoothError::ConsentRequired:
        return tr(Text::BluetoothRestricted);
    case BluetoothError::ResourceInUse:
        return tr(Text::BluetoothBusy);
    default:
        return tr(Text::ScanStoppedStatus) + std::to_wstring(static_cast<int>(error));
    }
}
void failScan(ScanState& state, std::wstring reason) {
    {
        std::lock_guard lock(state.mutex);
        if (state.canceled || state.phase == ScanPhase::Failed) return;
        state.error = std::move(reason);
        state.phase = ScanPhase::Failed;
    }
    state.changed.notify_one();
}
struct ScanApartment {
    ScanApartment() { init_apartment(apartment_type::multi_threaded); }
    ~ScanApartment() { uninit_apartment(); }
};
bool scanFinished(ScanState const& state) {
    return state.canceled || state.phase == ScanPhase::Failed;
}
Device readAdvertisement(BluetoothLEAdvertisementReceivedEventArgs const& args) {
    // The original event interface is sufficient for scanning on older Windows 10 builds.
    const auto details = args.try_as<IBluetoothLEAdvertisementReceivedEventArgs2>();
    const auto type = details ? details.BluetoothAddressType() : BluetoothAddressType::Unspecified;
    const auto advertisement = args.Advertisement();
    return { args.BluetoothAddress(), type,
        advertisement ? std::wstring(advertisement.LocalName()) : L"", args.RawSignalStrengthInDBm() };
}
void runScan(std::shared_ptr<ScanState> state) {
    try {
        ScanApartment apartment;
        traceScan("Checking Windows Bluetooth adapter\r\n");
        auto operation = BluetoothAdapter::GetDefaultAsync();
        const auto deadline = GetTickCount64() + 5000;
        while (operation.Status() == Windows::Foundation::AsyncStatus::Started) {
            std::unique_lock lock(state->mutex);
            const bool canceled = state->changed.wait_for(lock, std::chrono::milliseconds(100),
                [&] { return scanFinished(*state); });
            lock.unlock();
            if (canceled || GetTickCount64() >= deadline) {
                try { operation.Cancel(); } catch (hresult_error const&) {}
                if (!canceled) {
                    traceScan("Adapter check timed out\r\n");
                    failScan(*state, tr(Text::AdapterTimeout));
                }
                return;
            }
        }
        auto adapter = operation.GetResults();
        if (!adapter) {
            traceScan("Windows returned no Bluetooth adapter\r\n");
            failScan(*state, tr(Text::NoAdapter));
            return;
        }
        if (!adapter.IsLowEnergySupported() || !adapter.IsCentralRoleSupported()) {
            traceScan("BLE central role unavailable\r\n");
            failScan(*state, tr(Text::CentralUnsupported));
            return;
        }
        {
            std::lock_guard lock(state->mutex);
            if (scanFinished(*state)) return;
        }
        traceScan("Creating advertisement watcher\r\n");
        BluetoothLEAdvertisementWatcher watcher;
        watcher.ScanningMode(BluetoothLEScanningMode::Active);
        const std::weak_ptr<ScanState> weak = state;
        const auto received = watcher.Received([weak](auto const&, auto const& args) {
            try {
                auto current = weak.lock();
                if (!current || !args) return;
                auto item = readAdvertisement(args);
                bool first = false;
                {
                    std::lock_guard lock(current->mutex);
                    if (scanFinished(*current)) return;
                    auto previous = current->devices.find(item.address);
                    if (item.name.empty() && previous != current->devices.end()) item.name = previous->second.name;
                    current->devices.insert_or_assign(item.address, std::move(item));
                    first = !std::exchange(current->received, true);
                }
                if (first) traceScan("First BLE advertisement received\r\n");
            } catch (...) {}
        });
        const auto stopped = watcher.Stopped([weak](auto const&, auto const& args) {
            try {
                auto current = weak.lock();
                if (!current) return;
                const auto error = args ? args.Error() : BluetoothError::OtherError;
                char line[64];
                sprintf_s(line, "Windows stopped the watcher: %d\r\n", static_cast<int>(error));
                traceScan(line);
                failScan(*current, scanStoppedMessage(error));
            } catch (hresult_error const&) {
                if (auto current = weak.lock()) failScan(*current, scanStoppedMessage(BluetoothError::OtherError));
            }
        });
        traceScan("Calling advertisement watcher Start\r\n");
        watcher.Start();
        traceScan("Advertisement watcher Start returned\r\n");
        {
            std::unique_lock lock(state->mutex);
            if (!scanFinished(*state)) {
                state->phase = ScanPhase::Scanning;
                state->changed.wait(lock, [&] { return scanFinished(*state); });
            }
        }
        watcher.Received(received);
        watcher.Stopped(stopped);
        try { watcher.Stop(); } catch (hresult_error const&) {}
        traceScan("Advertisement watcher closed\r\n");
    } catch (hresult_error const& error) {
        const auto code = static_cast<unsigned>(error.code().value);
        char line[64];
        sprintf_s(line, "Scan failed with HRESULT 0x%08X\r\n", code);
        traceScan(line);
        wchar_t displayCode[11];
        swprintf_s(displayCode, L"0x%08X", code);
        failScan(*state, tr(Text::ScanFailed) + std::wstring(displayCode) + L" · " + std::wstring(error.message()));
    } catch (...) {
        traceScan("Unexpected C++ error during scanning\r\n");
        failScan(*state, tr(Text::ScanError));
    }
}
void pollScan() {
    if (!scan) return;
    ScanPhase phase;
    std::wstring error;
    std::unordered_map<uint64_t, Device> devices;
    {
        std::lock_guard lock(scan->mutex);
        phase = scan->phase;
        error = scan->error;
        devices.swap(scan->devices);
    }
    if (phase == ScanPhase::Scanning && !scanVisible) {
        scanVisible = true;
        scanDeadline = GetTickCount64() + 60000;
        refresh(tr(Text::Scanning));
    }
    if (!devices.empty()) {
        if (!scanListDrawn) traceScan("Updating the device list with its first results\r\n");
        updateDevices(devices);
        if (!scanListDrawn) traceScan("First device list update completed\r\n");
        scanListDrawn = true;
    }
    if (phase == ScanPhase::Failed) {
        stopScan();
        refresh(error);
    } else if (phase == ScanPhase::Checking && GetTickCount64() >= scanDeadline) {
        traceScan("Windows did not finish starting the scanner\r\n");
        stopScan();
        refresh(tr(Text::ScanStartTimeout));
    } else if (phase == ScanPhase::Scanning && GetTickCount64() >= scanDeadline) {
        stopScan();
        refresh(tr(Text::ScanFinished));
    }
}
void startScan() {
    prepareScanLog();
    traceScan("Scan requested\r\n");
    try {
        found.clear();
        rows.clear();
        ListBox_ResetContent(controls[Devices]);
        scanVisible = false;
        scanListDrawn = false;
        scanning = true;
        scanDeadline = GetTickCount64() + 10000;
        scan = std::make_shared<ScanState>();
        if (!SetTimer(window, ScanTimer, 150, nullptr)) {
            stopScan();
            refresh(tr(Text::ScanTimerError));
            return;
        }
        std::thread(runScan, scan).detach();
        refresh(tr(Text::CheckingAdapter));
    } catch (...) {
        traceScan("Unable to launch scan worker\r\n");
        stopScan();
        refresh(tr(Text::StartScanError));
    }
}
void connect(Device selected) {
    stopScan();
    scanVisible = false;
    auto current = std::make_shared<Session>();
    session = current;
    deviceName = selected.name.empty() ? addressText(selected.address) : selected.name;
    refresh(tr(Text::Connecting) + deviceName);
    SetTimer(window, 2, 20000, nullptr);
    auto rxId = guid{ std::wstring_view(settings.rx) };
    auto txId = guid{ std::wstring_view(settings.tx) };
    std::thread([current, selected, rxId, txId] {
        init_apartment();
        try {
            current->device = selected.type == BluetoothAddressType::Unspecified
                ? BluetoothLEDevice::FromBluetoothAddressAsync(selected.address).get()
                : BluetoothLEDevice::FromBluetoothAddressAsync(selected.address, selected.type).get();
            if (!current->device) throw hresult_error(E_FAIL, tr(Text::DeviceUnavailable));
            if (current->canceled) return;
            auto result = current->device.GetGattServicesAsync(BluetoothCacheMode::Uncached).get();
            if (result.Status() != GattCommunicationStatus::Success)
                throw hresult_error(E_FAIL, tr(Text::ServiceDiscoveryError));
            for (auto const& service : result.Services()) current->services.push_back(service);
            for (auto const& service : current->services) {
                if (current->canceled) return;
                auto chars = service.GetCharacteristicsAsync(BluetoothCacheMode::Uncached).get();
                if (chars.Status() != GattCommunicationStatus::Success) continue;
                for (auto const& item : chars.Characteristics()) {
                    if (item.Uuid() == rxId) current->rx = item;
                    if (item.Uuid() == txId) current->tx = item;
                }
                if (current->rx && current->tx) break;
            }
            if (!current->rx || !current->tx)
                throw hresult_error(E_FAIL, tr(Text::UuidMissing));
            auto properties = current->tx.CharacteristicProperties();
            if ((properties & (GattCharacteristicProperties::Write | GattCharacteristicProperties::WriteWithoutResponse)) == GattCharacteristicProperties::None)
                throw hresult_error(E_FAIL, tr(Text::TxNotWritable));
            auto rxProperties = current->rx.CharacteristicProperties();
            auto mode = GattClientCharacteristicConfigurationDescriptorValue::Indicate;
            if ((rxProperties & GattCharacteristicProperties::Notify) != GattCharacteristicProperties::None)
                mode = GattClientCharacteristicConfigurationDescriptorValue::Notify;
            else if ((rxProperties & GattCharacteristicProperties::Indicate) == GattCharacteristicProperties::None)
                throw hresult_error(E_FAIL, tr(Text::NoNotifications));
            std::weak_ptr<Session> weak = current;
            current->data = current->rx.ValueChanged([weak](auto const&, auto const& args) {
                try {
                    if (weak.expired()) return;
                    auto buffer = args.CharacteristicValue();
                    std::vector<uint8_t> bytes(buffer.Length());
                    DataReader::FromBuffer(buffer).ReadBytes(bytes);
                    post([weak, bytes = std::move(bytes)] { if (session && session == weak.lock()) receive(bytes); });
                } catch (...) {}
            });
            if (current->canceled) return;
            if (current->rx.WriteClientCharacteristicConfigurationDescriptorAsync(mode).get() != GattCommunicationStatus::Success)
                throw hresult_error(E_FAIL, tr(Text::SubscriptionError));
            current->state = current->device.ConnectionStatusChanged([weak](auto const& sender, auto const&) {
                try {
                    if (sender.ConnectionStatus() == BluetoothConnectionStatus::Disconnected)
                        post([weak] { if (session && session == weak.lock()) disconnect(tr(Text::DeviceDisconnected)); });
                } catch (...) {}
            });
            if (current->device.ConnectionStatus() != BluetoothConnectionStatus::Connected)
                throw hresult_error(E_FAIL, tr(Text::DeviceDisconnected));
            post([weak] {
                if (!session || session != weak.lock()) return;
                KillTimer(window, 2);
                ready = true;
                set(progress, L"");
                refresh(tr(Text::Connected) + deviceName);
            });
        } catch (hresult_error const& error) {
            post([weak = std::weak_ptr(current), message = std::wstring(error.message())] {
                if (session && session == weak.lock()) disconnect(message);
            });
        }
    }).detach();
}
void sendCurrent() {
    if (!ready || busy) return;
    std::vector<uint8_t> bytes;
    if (!fileName.empty()) {
        auto prompt = fileName + L" (" + std::to_wstring(fileBytes.size()) + L" bytes)\n\n" +
            tr(Text::ConfirmRawFile);
        if (MessageBoxW(window, prompt.c_str(), L"OBDcomm", MB_OKCANCEL | MB_ICONQUESTION) != IDOK || !ready || busy) return;
        bytes = fileBytes;
    } else {
        const wchar_t* endings[] = { L"\r\n", L"\r", L"\n" };
        auto input = read(controls[Input]) + endings[settings.ending];
        UINT cp = settings.encoding ? 936 : CP_UTF8;
        int size = WideCharToMultiByte(cp, 0, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
        bytes.resize(size);
        WideCharToMultiByte(cp, 0, input.data(), static_cast<int>(input.size()),
            reinterpret_cast<char*>(bytes.data()), size, nullptr, nullptr);
    }
    if (bytes.empty()) {
        set(progress, L"0 / 0 bytes");
        return;
    }
    busy = true;
    EnableWindow(controls[Send], FALSE);
    auto total = std::to_wstring(bytes.size()) + L" bytes";
    set(progress, L"0 / " + total);
    std::thread([current = session, bytes = std::move(bytes), total] {
        init_apartment();
        std::weak_ptr<Session> weak = current;
        try {
            auto option = (current->tx.CharacteristicProperties() & GattCharacteristicProperties::Write) != GattCharacteristicProperties::None
                ? GattWriteOption::WriteWithResponse : GattWriteOption::WriteWithoutResponse;
            auto lastUpdate = GetTickCount64();
            for (size_t offset = 0; offset < bytes.size();) {
                if (current->canceled) return;
                size_t count = std::min<size_t>(20, bytes.size() - offset);
                DataWriter writer;
                writer.WriteBytes({ bytes.data() + offset, bytes.data() + offset + count });
                auto operation = current->tx.WriteValueAsync(writer.DetachBuffer(), option);
                auto deadline = GetTickCount64() + 10000;
                while (operation.Status() == Windows::Foundation::AsyncStatus::Started) {
                    if (current->canceled || GetTickCount64() >= deadline) {
                        operation.Cancel();
                        if (!current->canceled)
                            post([weak] { if (session && session == weak.lock()) disconnect(tr(Text::WriteTimeout)); });
                        return;
                    }
                    Sleep(5);
                }
                auto result = operation.GetResults();
                if (result != GattCommunicationStatus::Success)
                    throw hresult_error(E_FAIL, tr(Text::WriteFailed));
                offset += count;
                if (offset != bytes.size() && GetTickCount64() - lastUpdate >= 100) {
                    lastUpdate = GetTickCount64();
                    post([weak, offset, total] {
                        if (session && session == weak.lock()) set(progress, std::to_wstring(offset) + L" / " + total);
                    });
                }
            }
            post([weak, size = bytes.size(), total] {
                if (!session || session != weak.lock()) return;
                busy = false;
                EnableWindow(controls[Send], TRUE);
                set(progress, std::to_wstring(size) + L" / " + total);
            });
        } catch (hresult_error const& error) {
            post([weak, message = std::wstring(error.message())] {
                if (!session || session != weak.lock()) return;
                busy = false;
                EnableWindow(controls[Send], TRUE);
                set(progress, message);
            });
        }
    }).detach();
}
void clipboard(std::wstring const& value) {
    if (value.empty()) return;
    auto size = (value.size() + 1) * sizeof(wchar_t);
    auto memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) return;
    auto target = GlobalLock(memory);
    if (target) {
        memcpy(target, value.c_str(), size);
        GlobalUnlock(memory);
        if (OpenClipboard(window)) {
            EmptyClipboard();
            if (SetClipboardData(CF_UNICODETEXT, memory)) memory = nullptr;
            CloseClipboard();
        }
    }
    if (memory) {
        GlobalFree(memory);
        alert(tr(Text::ClipboardError));
    }
}
void copyToken() {
    static const std::wregex pattern(LR"(SFD token 长度[ \t]+Length[ \t]*=[ \t]*([0-9]+)[ \t]*(?:\r\n|\r|\n)([0-9A-Fa-f\s]+?)(?:\r\n|\r|\n)[ \t]*CRC8[ \t]*=[ \t]*[0-9A-Fa-f]{2}\b)");
    std::wstring latest;
    for (std::wsregex_iterator match(logText.begin(), logText.end(), pattern), end; match != end; ++match) {
        auto payload = (*match)[2].str();
        std::erase_if(payload, [](wchar_t c) { return iswspace(c) != 0; });
        try {
            const auto size = std::stoull((*match)[1].str());
            if (size && payload.size() % 2 == 0 && size == payload.size() / 2) latest = std::move(payload);
        } catch (std::out_of_range const&) {}
    }
    if (latest.empty()) alert(tr(Text::TokenMissing));
    else clipboard(latest);
}
void loadFile() {
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFile = path;
    dialog.nMaxFile = ARRAYSIZE(path);
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return;
    std::ifstream input(std::filesystem::path(path), std::ios::binary | std::ios::ate);
    const auto size = input ? input.tellg() : std::streampos(-1);
    if (size < 0 || size > 8388608) {
        alert(tr(Text::FileLimit));
        return;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    input.seekg(0);
    if (size && !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
        alert(tr(Text::FileReadError));
        return;
    }
    fileBytes = std::move(bytes);
    fileName = std::filesystem::path(path).filename().wstring();
    set(controls[Input], fileName + L" (" + std::to_wstring(size) + L" bytes)");
    SendMessageW(controls[Input], EM_SETREADONLY, TRUE, 0);
}
constexpr wchar_t section[] = L"BLE";
bool validGuid(const std::wstring& text) {
    static const std::wregex full(LR"([0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12})");
    return std::regex_match(text, full);
}
std::wstring hex(BYTE const* bytes, size_t size) {
    constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring result;
    for (size_t i = 0; i < size; ++i) {
        result += digits[bytes[i] >> 4];
        result += digits[bytes[i] & 15];
    }
    return result;
}
std::wstring passwordVerifier(std::wstring const& password, std::wstring const& previous = {}) {
    std::array<BYTE, 16> salt{};
    if (previous.empty()) {
        if (BCryptGenRandom(nullptr, salt.data(), static_cast<ULONG>(salt.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return {};
    } else {
        if (previous.size() != 96 || previous.find_first_not_of(L"0123456789abcdef") != std::wstring::npos) return {};
        for (size_t i = 0; i < salt.size(); ++i)
            salt[i] = static_cast<BYTE>(std::stoul(previous.substr(i * 2, 2), nullptr, 16));
    }
    BCRYPT_ALG_HANDLE algorithm{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0) return {};
    std::array<BYTE, 32> key{};
    auto result = BCryptDeriveKeyPBKDF2(algorithm,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(password.data())),
        static_cast<ULONG>(password.size() * sizeof(wchar_t)), salt.data(),
        static_cast<ULONG>(salt.size()), 600000, key.data(), static_cast<ULONG>(key.size()), 0);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (result < 0) return {};
    return hex(salt.data(), salt.size()) + hex(key.data(), key.size());
}
INT_PTR CALLBACK unlockProc(HWND dialog, UINT message, WPARAM w, LPARAM) {
    if (message == WM_INITDIALOG) {
        SetWindowTextW(dialog, tr(Text::UnlockTitle));
        SetDlgItemTextW(dialog, 1002, tr(Text::EnterPassword));
        SetDlgItemTextW(dialog, IDOK, tr(Text::Unlock));
        SetDlgItemTextW(dialog, IDCANCEL, tr(Text::Exit));
        SendDlgItemMessageW(dialog, 1001, EM_SETLIMITTEXT, 128, 0);
        return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(w) == IDCANCEL)) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(w) == IDOK) {
        auto password = read(GetDlgItem(dialog, 1001));
        if (!password.empty() && passwordVerifier(password, settings.passwordVerifier) == settings.passwordVerifier)
            EndDialog(dialog, IDOK);
        else {
            MessageBoxW(dialog, tr(Text::IncorrectPassword), L"OBDcomm", MB_OK | MB_ICONWARNING);
            SetFocus(GetDlgItem(dialog, 1001));
            SendDlgItemMessageW(dialog, 1001, EM_SETSEL, 0, -1);
        }
        return TRUE;
    }
    return FALSE;
}
void loadSettings() {
    wchar_t path[32768];
    DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", path, ARRAYSIZE(path));
    if (!count || count >= ARRAYSIZE(path)) return;
    auto directory = std::filesystem::path(path) / L"OBDcomm";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    configPath = (directory / L"settings.ini").wstring();
    wchar_t value[128];
    GetPrivateProfileStringW(section, L"PasswordVerifier", L"", value, ARRAYSIZE(value), configPath.c_str());
    settings.passwordVerifier = value;
    GetPrivateProfileStringW(section, L"RX", settings.rx.c_str(), value, ARRAYSIZE(value), configPath.c_str());
    if (validGuid(value)) settings.rx = value;
    GetPrivateProfileStringW(section, L"TX", settings.tx.c_str(), value, ARRAYSIZE(value), configPath.c_str());
    if (validGuid(value)) settings.tx = value;
    auto number = [](const wchar_t* key, int defaultValue) {
        return GetPrivateProfileIntW(section, key, defaultValue, configPath.c_str());
    };
    settings.encoding = number(L"Encoding", 0) == 1;
    settings.ending = std::min(number(L"Ending", 0), 2u);
    settings.sound = number(L"Sound", 0) != 0;
    settings.token = number(L"Token", 1) != 0;
}
bool saveSettings(Settings const& value) {
    std::wstring data;
    auto entry = [&](const wchar_t* name, const std::wstring& content) {
        data += std::wstring(name) + L"=" + content + L'\0';
    };
    entry(L"RX", value.rx);
    entry(L"TX", value.tx);
    entry(L"Encoding", std::to_wstring(value.encoding));
    entry(L"Ending", std::to_wstring(value.ending));
    entry(L"Sound", std::to_wstring(value.sound));
    entry(L"Token", std::to_wstring(value.token));
    if (!value.passwordVerifier.empty()) entry(L"PasswordVerifier", value.passwordVerifier);
    return !configPath.empty() && WritePrivateProfileSectionW(section, data.c_str(), configPath.c_str());
}
HWND make(HWND owner, const wchar_t* type, const wchar_t* title, DWORD style, int id = 0, DWORD ex = 0) {
    return CreateWindowExW(ex, type, title, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, owner,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
}
HFONT createFont(UINT monitorDpi) {
    return CreateFontW(-MulDiv(10, monitorDpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, uiFont());
}
void place(HWND control, int x, int y, int width, int height, UINT scale) {
    MoveWindow(control, MulDiv(x, scale, 96), MulDiv(y, scale, 96),
        MulDiv(std::max(0, width), scale, 96), MulDiv(std::max(0, height), scale, 96), TRUE);
}
enum SettingId { Srx = 201, Stx, Sencoding, Sending, Ssound, Stoken, Sdefaults, Sfile, Ssave, Scancel, SusePassword, Spassword, Sconfirm };
struct SettingsWindow {
    HWND dialog{}, rxLabel{}, txLabel{}, encodingLabel{}, endingLabel{}, hint{};
    HFONT font{};
    bool chooseFile = false;
    void fill(Settings const& values) const {
        set(GetDlgItem(dialog, Srx), values.rx);
        set(GetDlgItem(dialog, Stx), values.tx);
        SendDlgItemMessageW(dialog, Sencoding, CB_SETCURSEL, values.encoding, 0);
        SendDlgItemMessageW(dialog, Sending, CB_SETCURSEL, values.ending, 0);
        CheckDlgButton(dialog, Ssound, values.sound ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dialog, Stoken, values.token ? BST_CHECKED : BST_UNCHECKED);
    }
    void updatePasswordFields() const {
        bool needsPassword = IsDlgButtonChecked(dialog, SusePassword) == BST_CHECKED && settings.passwordVerifier.empty();
        for (int id : { Spassword, Sconfirm }) EnableWindow(GetDlgItem(dialog, id), needsPassword);
    }
    void resize() {
        UINT scale = GetDpiForWindow(dialog);
        auto nextFont = createFont(scale);
        for (HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(nextFont), TRUE);
        if (font) DeleteObject(font);
        font = nextFont;
        auto move = [&](int id, int x, int y, int w, int h) { place(GetDlgItem(dialog, id), x, y, w, h, scale); };
        place(rxLabel, 20, 16, 430, 24, scale);
        move(Srx, 20, 42, 436, 29);
        place(txLabel, 20, 80, 430, 24, scale);
        move(Stx, 20, 106, 436, 29);
        place(encodingLabel, 20, 149, 190, 24, scale);
        place(endingLabel, 245, 149, 190, 24, scale);
        move(Sencoding, 20, 174, 211, 120);
        move(Sending, 245, 174, 211, 120);
        move(Ssound, 20, 215, 436, 28);
        move(Stoken, 20, 248, 436, 28);
        move(SusePassword, 20, 281, 436, 28);
        move(Spassword, 20, 316, 211, 29);
        move(Sconfirm, 245, 316, 211, 29);
        place(hint, 20, 359, 436, 30, scale);
        move(Sdefaults, 20, 404, 116, 34);
        move(Sfile, 144, 404, 120, 34);
        move(Ssave, 278, 404, 84, 34);
        move(Scancel, 371, 404, 85, 34);
    }
};
LRESULT CALLBACK settingsProc(HWND dialog, UINT message, WPARAM w, LPARAM l) {
    auto view = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
    switch (message) {
    case WM_CREATE: {
        view = static_cast<SettingsWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        view->dialog = dialog;
        SetWindowLongPtrW(dialog, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
        view->rxLabel = make(dialog, L"STATIC", tr(Text::ReceiveUuid), 0);
        make(dialog, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, Srx, WS_EX_CLIENTEDGE);
        view->txLabel = make(dialog, L"STATIC", tr(Text::SendUuid), 0);
        make(dialog, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, Stx, WS_EX_CLIENTEDGE);
        view->encodingLabel = make(dialog, L"STATIC", tr(Text::Encoding), 0);
        view->endingLabel = make(dialog, L"STATIC", tr(Text::SendLineEnding), 0);
        auto encoding = make(dialog, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, Sencoding);
        auto ending = make(dialog, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, Sending);
        for (auto label : { L"UTF-8", L"GBK" }) ComboBox_AddString(encoding, label);
        for (auto label : { L"CRLF", L"CR", L"LF" }) ComboBox_AddString(ending, label);
        make(dialog, L"BUTTON", tr(Text::ReceiveSound), WS_TABSTOP | BS_AUTOCHECKBOX, Ssound);
        make(dialog, L"BUTTON", tr(Text::ShowCopyToken), WS_TABSTOP | BS_AUTOCHECKBOX, Stoken);
        make(dialog, L"BUTTON", tr(Text::RequirePassword), WS_TABSTOP | BS_AUTOCHECKBOX, SusePassword);
        auto password = make(dialog, L"EDIT", L"", WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL, Spassword, WS_EX_CLIENTEDGE);
        auto confirmation = make(dialog, L"EDIT", L"", WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL, Sconfirm, WS_EX_CLIENTEDGE);
        Edit_SetCueBannerText(password, tr(Text::NewPassword));
        Edit_SetCueBannerText(confirmation, tr(Text::ConfirmPassword));
        Edit_LimitText(password, 128);
        Edit_LimitText(confirmation, 128);
        CheckDlgButton(dialog, SusePassword, settings.passwordVerifier.empty() ? BST_UNCHECKED : BST_CHECKED);
        view->updatePasswordFields();
        view->hint = make(dialog, L"STATIC", tr(Text::SaveDisconnects), 0);
        make(dialog, L"BUTTON", tr(Text::RestoreDefaults), WS_TABSTOP, Sdefaults);
        make(dialog, L"BUTTON", tr(Text::LoadFile), WS_TABSTOP, Sfile);
        make(dialog, L"BUTTON", tr(Text::Save), WS_TABSTOP, Ssave);
        make(dialog, L"BUTTON", tr(Text::Cancel), WS_TABSTOP, Scancel);
        view->fill(settings);
        view->resize();
        return 0;
    }
    case WM_DPICHANGED: {
        auto rectangle = reinterpret_cast<RECT*>(l);
        SetWindowPos(dialog, nullptr, rectangle->left, rectangle->top,
            rectangle->right - rectangle->left, rectangle->bottom - rectangle->top, SWP_NOZORDER | SWP_NOACTIVATE);
        view->resize();
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case SusePassword: view->updatePasswordFields(); return 0;
        case Sdefaults: view->fill(Settings{}); return 0;
        case Sfile: view->chooseFile = true; DestroyWindow(dialog); return 0;
        case Scancel:
        case IDCANCEL: DestroyWindow(dialog); return 0;
        case Ssave: {
            Settings next;
            next.rx = read(GetDlgItem(dialog, Srx));
            next.tx = read(GetDlgItem(dialog, Stx));
            if (!validGuid(next.rx) || !validGuid(next.tx)) {
                MessageBoxW(dialog, tr(Text::InvalidUuid), L"OBDcomm", MB_OK | MB_ICONWARNING);
                return 0;
            }
            next.encoding = ComboBox_GetCurSel(GetDlgItem(dialog, Sencoding));
            next.ending = ComboBox_GetCurSel(GetDlgItem(dialog, Sending));
            next.sound = IsDlgButtonChecked(dialog, Ssound) == BST_CHECKED;
            next.token = IsDlgButtonChecked(dialog, Stoken) == BST_CHECKED;
            if (IsDlgButtonChecked(dialog, SusePassword) == BST_CHECKED) {
                next.passwordVerifier = settings.passwordVerifier;
                if (next.passwordVerifier.empty()) {
                    auto password = read(GetDlgItem(dialog, Spassword));
                    if (password.empty() || password != read(GetDlgItem(dialog, Sconfirm))) {
                        MessageBoxW(dialog, tr(Text::PasswordMismatch), L"OBDcomm", MB_OK | MB_ICONWARNING);
                        return 0;
                    }
                    next.passwordVerifier = passwordVerifier(password);
                    if (next.passwordVerifier.empty()) {
                        MessageBoxW(dialog, tr(Text::PasswordError), L"OBDcomm", MB_OK | MB_ICONERROR);
                        return 0;
                    }
                }
            }
            if (!saveSettings(next)) {
                MessageBoxW(dialog, tr(Text::SaveError), L"OBDcomm", MB_OK | MB_ICONERROR);
                return 0;
            }
            settings = std::move(next);
            stopScan();
            scanVisible = false;
            disconnect(tr(Text::Disconnected));
            DestroyWindow(dialog);
            return 0;
        }
        }
        break;
    case WM_CLOSE: DestroyWindow(dialog); return 0;
    case WM_DESTROY: EnableWindow(window, TRUE); return 0;
    }
    return DefWindowProcW(dialog, message, w, l);
}
void showSettings() {
    SettingsWindow view;
    RECT parent{}, bounds{ 0, 0, MulDiv(476, dpi, 96), MulDiv(461, dpi, 96) };
    GetWindowRect(window, &parent);
    constexpr DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectExForDpi(&bounds, style, FALSE, WS_EX_DLGMODALFRAME, dpi);
    int w = bounds.right - bounds.left, h = bounds.bottom - bounds.top;
    auto dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, L"OBDcommSettings",
        tr(Text::Settings), style, (parent.left + parent.right - w) / 2,
        (parent.top + parent.bottom - h) / 2, w, h, window, nullptr, GetModuleHandleW(nullptr), &view);
    if (!dialog) return;
    EnableWindow(window, FALSE);
    ShowWindow(dialog, SW_SHOW);
    SetFocus(GetDlgItem(dialog, Srx));
    MSG msg;
    while (IsWindow(dialog)) {
        int result = GetMessageW(&msg, nullptr, 0, 0);
        if (result <= 0) {
            if (!result) PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && (msg.hwnd == dialog || IsChild(dialog, msg.hwnd))) {
            DestroyWindow(dialog);
        } else if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (IsWindow(dialog)) DestroyWindow(dialog);
    if (view.font) DeleteObject(view.font);
    if (view.chooseFile) loadFile();
}
void layout() {
    if (!controls[Send]) return;
    RECT client;
    GetClientRect(window, &client);
    int w = MulDiv(client.right, 96, dpi), h = MulDiv(client.bottom, 96, dpi);
    auto move = [&](int id, int x, int y, int width, int height) { place(controls[id], x, y, width, height, dpi); };
    move(Connect, 16, 14, 160, 36);
    move(Setup, w - 109, 14, 93, 36);
    place(statusText, 16, 58, w - 32, 24, dpi);
    ShowWindow(controls[Devices], scanVisible ? SW_SHOWNA : SW_HIDE);
    int y = 88;
    if (scanVisible) {
        int listHeight = std::clamp(h / 4, 72, 150);
        move(Devices, 16, y, w - 32, listHeight);
        y += listHeight + 10;
    }
    place(rxHeader, 16, y + 6, 125, 27, dpi);
    move(ClearRx, w - 104, y, 88, 32);
    move(Copy, w - 224, y, 112, 32);
    move(Token, w - 344, y, 112, 32);
    ShowWindow(controls[Token], settings.token ? SW_SHOWNA : SW_HIDE);
    move(Receive, 16, y + 38, w - 32, h - 180 - y);
    place(txHeader, 16, h - 135, w - 32, 23, dpi);
    move(Input, 16, h - 110, w - 32, 60);
    place(progress, 16, h - 40, w - 245, 30, dpi);
    move(ClearTx, w - 204, h - 44, 88, 33);
    move(Send, w - 108, h - 44, 92, 33);
}
void applyDpi() {
    auto nextFont = createFont(dpi);
    for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(nextFont), TRUE);
    if (font) DeleteObject(font);
    font = nextFont;
    layout();
}
LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_CREATE:
        window = hwnd;
        dpi = GetDpiForWindow(hwnd);
        controls[Connect] = make(hwnd, L"BUTTON", L"", WS_TABSTOP, Connect);
        controls[Setup] = make(hwnd, L"BUTTON", tr(Text::Settings), WS_TABSTOP, Setup);
        statusText = make(hwnd, L"STATIC", L"", SS_ENDELLIPSIS);
        controls[Devices] = make(hwnd, L"LISTBOX", L"", WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL, Devices, WS_EX_CLIENTEDGE);
        rxHeader = make(hwnd, L"STATIC", tr(Text::ReceivedData), 0);
        controls[Copy] = make(hwnd, L"BUTTON", tr(Text::CopyReceived), WS_TABSTOP, Copy);
        controls[Token] = make(hwnd, L"BUTTON", tr(Text::CopyToken), WS_TABSTOP, Token);
        controls[ClearRx] = make(hwnd, L"BUTTON", tr(Text::ClearRx), WS_TABSTOP, ClearRx);
        controls[Receive] = make(hwnd, MSFTEDIT_CLASS, L"", WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, Receive, WS_EX_CLIENTEDGE);
        SendMessageW(controls[Receive], EM_EXLIMITTEXT, 0, 262144);
        txHeader = make(hwnd, L"STATIC", tr(Text::SendData), 0);
        controls[Input] = make(hwnd, L"EDIT", L"", WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, Input, WS_EX_CLIENTEDGE);
        SendMessageW(controls[Input], EM_SETLIMITTEXT, 131072, 0);
        controls[ClearTx] = make(hwnd, L"BUTTON", tr(Text::ClearInput), WS_TABSTOP, ClearTx);
        controls[Send] = make(hwnd, L"BUTTON", tr(Text::Send), WS_TABSTOP, Send);
        progress = make(hwnd, L"STATIC", L"", SS_ENDELLIPSIS);
        applyDpi();
        refresh(tr(Text::Disconnected));
        return 0;
    case WM_SIZE: layout(); return 0;
    case WM_DPICHANGED: {
        dpi = HIWORD(w);
        const auto& bounds = *reinterpret_cast<RECT*>(l);
        SetWindowPos(hwnd, nullptr, bounds.left, bounds.top, bounds.right - bounds.left,
            bounds.bottom - bounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
        applyDpi();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto& info = *reinterpret_cast<MINMAXINFO*>(l);
        auto scale = GetDpiForWindow(hwnd);
        if (!scale) scale = dpi;
        info.ptMinTrackSize = { MulDiv(590, scale, 96), MulDiv(480, scale, 96) };
        return 0;
    }
    case WM_TIMER:
        if (w == ScanTimer && scanning) {
            try {
                pollScan();
            } catch (...) {
                traceScan("Unexpected C++ error updating the scan window\r\n");
                stopScan();
                refresh(tr(Text::UpdateScanError));
            }
        }
        if (w == 2 && session && !ready) disconnect(tr(Text::ConnectionTimeout));
        return 0;
    case Dispatch: {
        std::vector<std::function<void()>> tasks;
        {
            std::lock_guard lock(queueLock);
            tasks.swap(callbacks);
        }
        for (auto& task : tasks) task();
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case Connect:
            if (session) disconnect(tr(Text::Disconnected));
            else if (scanning) {
                const bool checkingAdapter = !scanVisible;
                stopScan();
                traceScan("Scan canceled by the user\r\n");
                refresh(checkingAdapter ? tr(Text::CheckCanceled) :
                    tr(Text::ScanStopped));
            } else startScan();
            return 0;
        case Devices:
            if (HIWORD(w) == LBN_DBLCLK && scanVisible && !session) {
                int index = ListBox_GetCurSel(controls[Devices]);
                if (index >= 0 && static_cast<size_t>(index) < rows.size()) connect(rows[index]);
            }
            return 0;
        case Setup: showSettings(); return 0;
        case Send: sendCurrent(); return 0;
        case Copy: clipboard(logText); return 0;
        case Token: copyToken(); return 0;
        case ClearRx:
            logText.clear();
            set(controls[Receive], L"");
            return 0;
        case ClearTx:
            fileName.clear();
            fileBytes.clear();
            SendMessageW(controls[Input], EM_SETREADONLY, FALSE, 0);
            set(controls[Input], L"");
            return 0;
        }
        break;
    case WM_DESTROY:
        {
            std::lock_guard lock(queueLock);
            alive = false;
            callbacks.clear();
        }
        stopScan();
        if (session) session->canceled = true;
        session.reset();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    init_apartment(apartment_type::single_threaded);

    auto richEdit = LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!richEdit) return 1;
    loadSettings();
    if (!settings.passwordVerifier.empty() &&
        DialogBoxParamW(instance, MAKEINTRESOURCEW(101), nullptr, unlockProc, 0) != IDOK) return 0;
    WNDCLASSW registration{};
    registration.hInstance = instance;
    registration.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    registration.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    registration.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    registration.lpfnWndProc = settingsProc;
    registration.lpszClassName = L"OBDcommSettings";
    if (!RegisterClassW(&registration)) return 1;
    registration.lpfnWndProc = windowProc;
    registration.lpszClassName = L"OBDcomm";
    if (!RegisterClassW(&registration)) return 1;
    const auto scale = GetDpiForSystem();
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    auto main = CreateWindowExW(WS_EX_CONTROLPARENT, L"OBDcomm", WindowTitle,
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        std::min<int>(MulDiv(820, scale, 96), work.right - work.left),
        std::min<int>(MulDiv(650, scale, 96), work.bottom - work.top),
        nullptr, nullptr, instance, nullptr);
    if (!main) return 1;
    ShowWindow(main, show);
    MSG msg{};
    int result;
    while ((result = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN && msg.hwnd == controls[Devices]) {
            SendMessageW(main, WM_COMMAND, MAKEWPARAM(Devices, LBN_DBLCLK), reinterpret_cast<LPARAM>(msg.hwnd));
        } else if (!IsDialogMessageW(main, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (font) DeleteObject(font);
    return result < 0 ? 1 : static_cast<int>(msg.wParam);
}
