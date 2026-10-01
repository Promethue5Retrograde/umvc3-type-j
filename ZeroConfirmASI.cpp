#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>

#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>

namespace {

constexpr std::uintptr_t kOriginalImageBase = 0x140000000ull;
constexpr std::uintptr_t kSelectedIdRootVa = 0x140C746E0ull;
constexpr std::uintptr_t kCharacterSelectInputHelperVa = 0x1403B2020ull;
constexpr std::uintptr_t kMeterSetterVa = 0x140022690ull;
constexpr std::uintptr_t kActionTransitionWriteVa = 0x14003C02Bull;
constexpr std::uintptr_t kSCharacterVa = 0x140D44A70ull;
constexpr std::uintptr_t kSActionVa = 0x140D47E68ull;
constexpr std::uintptr_t kShotListVa = 0x140D47F98ull;
constexpr std::uintptr_t kSBattleSettingVa = 0x140D50E58ull;
constexpr std::uintptr_t kGSoundVa = 0x140D533E0ull;
constexpr std::uintptr_t kDoomMissileShotVtableVa = 0x140AAA5E0ull;
constexpr std::uintptr_t kDoomMissileEmplacementVtableVa = 0x140AAAA60ull;
constexpr std::ptrdiff_t kSelectedPhaseOffset = 0x58D8;
constexpr std::ptrdiff_t kSelectedIdOffset = 0x58EC;
constexpr int kZeroEngineId = 13;
constexpr int kMorriganEngineId = 15;
constexpr int kVergilEngineId = 22;
constexpr int kDoomEngineId = 35;
constexpr int kCharacterHoverPhase = 2;
constexpr std::size_t kInputHelperPatchSize = 17;
constexpr std::size_t kMeterSetterPatchSize = 14;
constexpr std::size_t kActionTransitionWritePatchSize = 28;
constexpr std::ptrdiff_t kInputFlagsOffset = 4;
constexpr unsigned char kConfirmInputBit = 0x08;
constexpr int kSecondPromptConfirmId = 1001;
constexpr int kSecondPromptCancelId = 1002;
constexpr std::ptrdiff_t kBattleCharacterIdBaseOffset = 0x44;
constexpr std::ptrdiff_t kBattleCharacterIdStride = 0x58;
constexpr std::ptrdiff_t kTeam1Offset = 0x350;
constexpr std::ptrdiff_t kTeam2Offset = 0x610;
constexpr std::ptrdiff_t kTeamActiveCharacterOffset = 0x48;
constexpr std::ptrdiff_t kTeamMeterOffset = 0x78;
constexpr std::ptrdiff_t kCharacterCurrentActionOffset = 0x1310;
constexpr std::ptrdiff_t kCharacterNodeP1Offset = 0x58;
constexpr std::ptrdiff_t kCharacterNodeP2Offset = 0x328;
constexpr std::ptrdiff_t kCharacterMaxHpOffset = 0x154C;
constexpr std::ptrdiff_t kCharacterCurrentHpOffset = 0x1550;
constexpr std::ptrdiff_t kGroundRemainingHitstunOffset = 0x4154;
constexpr std::ptrdiff_t kRemainingBlockstunOffset = 0x4158;
constexpr std::ptrdiff_t kAirRemainingHitstunOffset = 0x415C;
constexpr std::ptrdiff_t kPushblockTimerOffset = 0x41A8;
constexpr std::ptrdiff_t kGroundHitstunTimerOffset = 0x63EC;
constexpr std::ptrdiff_t kAirHitstunTimerOffset = 0x63F0;
constexpr std::ptrdiff_t kShotPositionXOffset = 0x50;
constexpr std::ptrdiff_t kShotPositionYOffset = 0x54;
constexpr std::ptrdiff_t kShotPositionZOffset = 0x58;
constexpr std::ptrdiff_t kShotScaleXOffset = 0x70;
constexpr std::ptrdiff_t kShotScaleYOffset = 0x74;
constexpr std::ptrdiff_t kShotScaleZOffset = 0x78;
constexpr DWORD kDoomCleanupGraceMs = 1000;
constexpr DWORD kPendingInstallConfirmMs = 3000;
constexpr float kOneMeter = 10000.0f;
constexpr float kTwoMeters = 20000.0f;
constexpr float kMaxMeter = 50000.0f;
constexpr float kMeterEpsilon = 0.5f;
constexpr float kEarlyMeterSpendMin = 8000.0f;
constexpr float kEarlyMeterSpendMax = 12000.0f;
constexpr int kVergilSpiralSwordsAction = 220;
constexpr int kMorriganAstralVisionAction = 225;
constexpr bool kEnableEarlyMeterDebit = false;
constexpr bool kEnableInstallExtraMeterDebit = false;
constexpr std::ptrdiff_t kActionFieldScanStart = 0x6000;
constexpr std::ptrdiff_t kActionFieldScanEnd = 0x6600;
constexpr std::array<std::ptrdiff_t, 5> kInstallSlotOffsets = {
    0x15F0, 0x1670, 0x16F0, 0x1770, 0x17F0,
};

HWND g_gameWindow = nullptr;
HINSTANCE g_module = nullptr;
HHOOK g_keyboardHook = nullptr;
using InputHelperFn = void (*)(void*, int, void*);
InputHelperFn g_originalInputHelper = nullptr;
std::atomic<DWORD> g_allowConfirmUntil = 0;
std::atomic_bool g_promptOpen = false;
std::atomic_bool g_forceZeroNativeConfirm = false;
std::atomic_bool g_forceDebugP1Confirm = false;
std::atomic_int g_debugInputOffset = -1;
std::atomic_int g_debugInputMask = 0;
std::atomic_int g_debugInputFrames = 0;
std::atomic_int g_debugDumpFrames = 0;
std::atomic_bool g_debugInputLogged = false;
std::atomic_bool g_monitorThreadsRunning = true;
std::atomic<std::uintptr_t> g_blockedMeterTeam = 0;
std::atomic<DWORD> g_blockedMeterUntil = 0;

struct CharacterSlotState {
    int id = -1;
    std::uintptr_t data = 0;
};

struct DoomHitState {
    std::uintptr_t lastData = 0;
    bool wasHit = false;
    float previousHp = 0.0f;
    DWORD cleanupUntil = 0;
};

std::array<DoomHitState, 6> g_doomHitStates{};

struct MeterInstallTarget {
    int characterId = -1;
    int installId = -1;
    int installType = -1;
    bool matchInstallType = false;
    float requiredMeter = kTwoMeters;
    const char* label = "";
};

constexpr std::array<MeterInstallTarget, 2> kMeterInstallTargets = {{
    {kMorriganEngineId, 2065, 13, true, kTwoMeters, "Morrigan Astral Vision"},
    {kVergilEngineId, 17, 16, true, kTwoMeters, "Vergil Spiral Swords"},
}};

struct MeterInstallRuntimeState {
    bool active = false;
    std::uintptr_t activeSlot = 0;
    float lastMeter = -1.0f;
    bool pendingEarlyDebit = false;
    std::uintptr_t pendingCharacter = 0;
    DWORD pendingUntil = 0;
};

std::array<std::array<MeterInstallRuntimeState, kMeterInstallTargets.size()>, 2>
    g_meterInstallStates{};

struct InstallDebugState {
    bool active = false;
    int id = 0;
    int type = 0;
};

std::array<std::array<InstallDebugState, kInstallSlotOffsets.size()>, 2> g_installDebugStates{};

std::uintptr_t Rebase(std::uintptr_t va) {
    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    return module + (va - kOriginalImageBase);
}

void LogLine(const char* message) {
    char modulePath[MAX_PATH]{};
    GetModuleFileNameA(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
    char* slash = std::strrchr(modulePath, '\\');
    if (slash) {
        slash[1] = '\0';
    } else {
        modulePath[0] = '\0';
    }

    char logPath[MAX_PATH]{};
    std::snprintf(logPath, std::size(logPath), "%sZeroConfirm.log", modulePath);

    SYSTEMTIME now{};
    GetLocalTime(&now);
    std::ofstream log(logPath, std::ios::app);
    if (log) {
        log << '['
            << now.wYear << '-'
            << now.wMonth << '-'
            << now.wDay << ' '
            << now.wHour << ':'
            << now.wMinute << ':'
            << now.wSecond << "] "
            << message << '\n';
    }
}

void LogFormat(const char* prefix, int value) {
    char buffer[256]{};
    std::snprintf(buffer, std::size(buffer), "%s%d", prefix, value);
    LogLine(buffer);
}

std::wstring GameRootPath() {
    wchar_t exePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, exePath, static_cast<DWORD>(std::size(exePath)));
    if (length == 0 || length >= std::size(exePath)) {
        return L".";
    }
    std::wstring path(exePath, length);
    const auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return L".";
    }
    return path.substr(0, slash);
}

std::wstring DebugInputCommandPath() {
    return GameRootPath() + L"\\ZeroConfirm.debug_input";
}

bool CanRead(const void* pointer, std::size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(pointer, &info, sizeof(info))) {
        return false;
    }
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        return false;
    }

    const auto start = reinterpret_cast<std::uintptr_t>(pointer);
    const auto end = start + size;
    const auto regionStart = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    const auto regionEnd = regionStart + info.RegionSize;
    return start >= regionStart && end <= regionEnd;
}

bool CanWrite(void* pointer, std::size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(pointer, &info, sizeof(info))) {
        return false;
    }
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        return false;
    }

    const DWORD writable =
        PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if ((info.Protect & writable) == 0) {
        return false;
    }

    const auto start = reinterpret_cast<std::uintptr_t>(pointer);
    const auto end = start + size;
    const auto regionStart = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    const auto regionEnd = regionStart + info.RegionSize;
    return start >= regionStart && end <= regionEnd;
}

template <typename T>
bool ReadMemory(std::uintptr_t address, T& value) {
    const auto* pointer = reinterpret_cast<const void*>(address);
    if (!CanRead(pointer, sizeof(T))) {
        return false;
    }
    std::memcpy(&value, pointer, sizeof(T));
    return true;
}

template <typename T>
bool WriteMemory(std::uintptr_t address, const T& value) {
    auto* pointer = reinterpret_cast<void*>(address);
    if (!CanWrite(pointer, sizeof(T))) {
        return false;
    }
    std::memcpy(pointer, &value, sizeof(T));
    return true;
}

std::uintptr_t ReadPointer(std::uintptr_t address) {
    std::uintptr_t value = 0;
    if (!ReadMemory(address, value)) {
        return 0;
    }
    return value;
}

bool IsReasonableFloat(float value) {
    return std::isfinite(value) && value > -1000000.0f && value < 10000000.0f;
}

bool IsReadablePointer(std::uintptr_t pointer) {
    return pointer != 0 && pointer != static_cast<std::uintptr_t>(-1) &&
           CanRead(reinterpret_cast<const void*>(pointer), sizeof(std::uintptr_t));
}

bool ReadCharacterIds(std::array<CharacterSlotState, 6>& slots) {
    const auto battleSettings = ReadPointer(Rebase(kSBattleSettingVa));
    if (!battleSettings) {
        return false;
    }

    bool foundAny = false;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        int id = -1;
        const auto address = battleSettings + kBattleCharacterIdBaseOffset +
                             static_cast<std::ptrdiff_t>(i) * kBattleCharacterIdStride;
        if (ReadMemory(address, id) && id >= 0 && id < 256) {
            slots[i].id = id;
            foundAny = true;
        } else {
            slots[i].id = -1;
        }
    }
    return foundAny;
}

void ReadCharacterDataFromSoundTable(std::array<CharacterSlotState, 6>& slots) {
    const auto soundTable = ReadPointer(Rebase(kGSoundVa));
    if (!soundTable) {
        return;
    }

    for (std::size_t i = 0; i < slots.size(); ++i) {
        const auto pointer = ReadPointer(soundTable + 0xAA0 + (0x438 * i));
        if (IsReadablePointer(pointer)) {
            slots[i].data = pointer;
        }
    }
}

void ReadCharacterDataFromNodeTrees(std::array<CharacterSlotState, 6>& slots) {
    const auto characterManager = ReadPointer(Rebase(kSCharacterVa));
    if (!characterManager) {
        return;
    }

    const auto p1Tree = ReadPointer(characterManager + kCharacterNodeP1Offset);
    const auto p2Tree = ReadPointer(characterManager + kCharacterNodeP2Offset);

    if (IsReadablePointer(p1Tree)) {
        const auto p1Next = ReadPointer(p1Tree + 0x10);
        const auto p1c1 = ReadPointer(p1Tree + 0x8);
        if (IsReadablePointer(p1c1)) {
            slots[0].data = p1c1;
        }
        if (IsReadablePointer(p1Next)) {
            const auto p1c2 = ReadPointer(p1Next + 0x8);
            if (IsReadablePointer(p1c2)) {
                slots[1].data = p1c2;
                const auto p1c3 = ReadPointer(p1c2 + 0x18);
                if (IsReadablePointer(p1c3)) {
                    slots[2].data = p1c3;
                }
            }
        }
    }

    if (IsReadablePointer(p2Tree)) {
        const auto p2Next = ReadPointer(p2Tree + 0x10);
        const auto p2c1 = ReadPointer(p2Tree + 0x8);
        if (IsReadablePointer(p2c1)) {
            slots[3].data = p2c1;
        }
        if (IsReadablePointer(p2Next)) {
            const auto p2c2 = ReadPointer(p2Next + 0x8);
            if (IsReadablePointer(p2c2)) {
                slots[4].data = p2c2;
                const auto p2c3 = ReadPointer(p2c2 + 0x18);
                if (IsReadablePointer(p2c3)) {
                    slots[5].data = p2c3;
                }
            }
        }
    }
}

bool ReadCharacterSlots(std::array<CharacterSlotState, 6>& slots) {
    for (auto& slot : slots) {
        slot = {};
    }

    if (!ReadCharacterIds(slots)) {
        return false;
    }

    ReadCharacterDataFromSoundTable(slots);
    ReadCharacterDataFromNodeTrees(slots);

    bool foundData = false;
    for (const auto& slot : slots) {
        foundData = foundData || IsReadablePointer(slot.data);
    }
    return foundData;
}

int ResolveCharacterId(std::uintptr_t characterData) {
    std::array<CharacterSlotState, 6> slots{};
    if (!ReadCharacterSlots(slots)) {
        return -1;
    }
    for (const auto& slot : slots) {
        if (slot.data == characterData) {
            return slot.id;
        }
    }
    return -1;
}

std::uintptr_t ResolveActiveTeam(std::uintptr_t characterData) {
    const auto action = ReadPointer(Rebase(kSActionVa));
    if (!action || !IsReadablePointer(characterData)) {
        return 0;
    }

    for (const auto teamOffset : {kTeam1Offset, kTeam2Offset}) {
        const auto team = action + teamOffset;
        if (ReadPointer(team + kTeamActiveCharacterOffset) == characterData) {
            return team;
        }
    }
    return 0;
}

bool ReadCharacterHitState(std::uintptr_t characterData, bool& isHit, bool& isBlocking, float& hp) {
    hp = 0.0f;
    isHit = false;
    isBlocking = false;

    float currentHp = 0.0f;
    float groundTimer = 0.0f;
    float airTimer = 0.0f;
    float groundRemaining = 0.0f;
    float airRemaining = 0.0f;
    float blockRemaining = 0.0f;
    float pushblockTimer = 0.0f;

    if (!ReadMemory(characterData + kCharacterCurrentHpOffset, currentHp) ||
        !IsReasonableFloat(currentHp)) {
        return false;
    }

    ReadMemory(characterData + kGroundHitstunTimerOffset, groundTimer);
    ReadMemory(characterData + kAirHitstunTimerOffset, airTimer);
    ReadMemory(characterData + kGroundRemainingHitstunOffset, groundRemaining);
    ReadMemory(characterData + kAirRemainingHitstunOffset, airRemaining);
    ReadMemory(characterData + kRemainingBlockstunOffset, blockRemaining);
    ReadMemory(characterData + kPushblockTimerOffset, pushblockTimer);

    hp = currentHp;
    isHit = (IsReasonableFloat(groundTimer) && groundTimer > 0.5f) ||
            (IsReasonableFloat(airTimer) && airTimer > 0.5f) ||
            (IsReasonableFloat(groundRemaining) && groundRemaining > 0.5f) ||
            (IsReasonableFloat(airRemaining) && airRemaining > 0.5f);
    isBlocking = (IsReasonableFloat(blockRemaining) && blockRemaining > 0.5f) ||
                 (IsReasonableFloat(pushblockTimer) && pushblockTimer > 0.5f);
    return true;
}

bool IsDoomMissileVtable(std::uintptr_t vtable) {
    return vtable == Rebase(kDoomMissileShotVtableVa) ||
           vtable == Rebase(kDoomMissileEmplacementVtableVa);
}

bool FindDoomMissileObjectNearEntry(std::uintptr_t listEntry, std::uintptr_t& objectBase) {
    constexpr std::array<std::ptrdiff_t, 8> preferredOffsets = {
        0x1450, 0x1428, 0x1420, 0x13F8, 0x1480, 0x1400, 0x1300, 0x0,
    };

    for (const auto offset : preferredOffsets) {
        const auto candidate = listEntry - static_cast<std::uintptr_t>(offset);
        std::uintptr_t vtable = 0;
        if (candidate < listEntry && ReadMemory(candidate, vtable) && IsDoomMissileVtable(vtable)) {
            objectBase = candidate;
            return true;
        }
        if (offset == 0 && ReadMemory(listEntry, vtable) && IsDoomMissileVtable(vtable)) {
            objectBase = listEntry;
            return true;
        }
    }

    for (std::ptrdiff_t offset = 0x1200; offset <= 0x1580; offset += 0x8) {
        const auto candidate = listEntry - static_cast<std::uintptr_t>(offset);
        std::uintptr_t vtable = 0;
        if (candidate < listEntry && ReadMemory(candidate, vtable) && IsDoomMissileVtable(vtable)) {
            objectBase = candidate;
            return true;
        }
    }

    return false;
}

void SuppressShotObject(std::uintptr_t objectBase) {
    constexpr float kOffscreenX = -32000.0f;
    constexpr float kOffscreenY = -32000.0f;
    constexpr float kOffscreenZ = 0.0f;
    constexpr float kZeroScale = 0.0f;

    WriteMemory(objectBase + kShotPositionXOffset, kOffscreenX);
    WriteMemory(objectBase + kShotPositionYOffset, kOffscreenY);
    WriteMemory(objectBase + kShotPositionZOffset, kOffscreenZ);
    WriteMemory(objectBase + kShotScaleXOffset, kZeroScale);
    WriteMemory(objectBase + kShotScaleYOffset, kZeroScale);
    WriteMemory(objectBase + kShotScaleZOffset, kZeroScale);
}

int SuppressDoomMissilesInList(int side) {
    const auto shotListBase = Rebase(kShotListVa);
    const auto sentinel = side == 0 ? shotListBase : shotListBase + 0x28;
    const auto startCursor = side == 0 ? shotListBase : shotListBase;
    const auto nextOffset = side == 0 ? 0x8 : 0x30;

    int suppressed = 0;
    auto cursor = startCursor;
    for (int guard = 0; guard < 128; ++guard) {
        const auto entry = ReadPointer(cursor + nextOffset);
        if (!entry || entry == static_cast<std::uintptr_t>(-1) || entry == sentinel) {
            break;
        }

        std::uintptr_t objectBase = 0;
        if (FindDoomMissileObjectNearEntry(entry, objectBase)) {
            SuppressShotObject(objectBase);
            ++suppressed;
        }

        if (entry == cursor) {
            break;
        }
        cursor = entry;
    }
    return suppressed;
}

void TriggerDoomMissileCleanupForSide(int side, const char* reason) {
    const int count = SuppressDoomMissilesInList(side);
    if (count > 0) {
        static DWORD lastLogTime[2] = {};
        const DWORD now = GetTickCount();
        if (side < 0 || side > 1 || static_cast<LONG>(now - lastLogTime[side]) > 250) {
            if (side >= 0 && side <= 1) {
                lastLogTime[side] = now;
            }
            char message[256]{};
            std::snprintf(message, std::size(message),
                          "Doom missile cleanup side=%d count=%d reason=%s.",
                          side + 1, count, reason);
            LogLine(message);
        }
    }
}

void ResetDoomMonitorState() {
    for (auto& state : g_doomHitStates) {
        state = {};
    }
}

void TickDoomMissileMonitor() {
    std::array<CharacterSlotState, 6> slots{};
    if (!ReadCharacterSlots(slots)) {
        ResetDoomMonitorState();
        return;
    }

    const DWORD now = GetTickCount();
    for (std::size_t i = 0; i < slots.size(); ++i) {
        auto& state = g_doomHitStates[i];
        const auto& slot = slots[i];
        if (slot.id != kDoomEngineId || !IsReadablePointer(slot.data)) {
            state = {};
            continue;
        }

        if (state.lastData != slot.data) {
            state = {};
            state.lastData = slot.data;
        }

        bool hitNow = false;
        bool blockingNow = false;
        float hp = 0.0f;
        if (!ReadCharacterHitState(slot.data, hitNow, blockingNow, hp)) {
            continue;
        }

        const bool hpDropped =
            state.previousHp > 1.0f && hp > 0.0f && hp < (state.previousHp - 0.5f);
        if (hpDropped && !blockingNow) {
            state.cleanupUntil = now + kDoomCleanupGraceMs;
            TriggerDoomMissileCleanupForSide(i < 3 ? 0 : 1, "hp_drop");
        } else if (state.cleanupUntil && static_cast<LONG>(state.cleanupUntil - now) > 0) {
            TriggerDoomMissileCleanupForSide(i < 3 ? 0 : 1, "cleanup_grace");
        }

        state.wasHit = hitNow;
        state.previousHp = hp;
        if (state.cleanupUntil && static_cast<LONG>(state.cleanupUntil - now) <= 0) {
            state.cleanupUntil = 0;
        }
    }
}

bool InstallSlotMatchesTarget(
    std::uintptr_t slot,
    const MeterInstallTarget& target,
    int& installId,
    int& installType,
    float& timer) {
    installId = -1;
    installType = -1;
    timer = 0.0f;

    if (!ReadMemory(slot + 0x08, installId) ||
        !ReadMemory(slot + 0x0C, installType) ||
        !ReadMemory(slot + 0x10, timer)) {
        return false;
    }
    if (installId != target.installId) {
        return false;
    }
    if (target.matchInstallType && installType != target.installType) {
        return false;
    }
    return IsReasonableFloat(timer) && timer > 0.5f;
}

void LogMeterInstallEvent(
    const MeterInstallTarget& target,
    int side,
    const char* action,
    float beforeMeter,
    float currentMeter,
    float newMeter,
    int installId,
    int installType) {
    char message[320]{};
    std::snprintf(
        message,
        std::size(message),
        "%s side=%d %s meter_before=%.1f meter_current=%.1f meter_new=%.1f install_id=%d install_type=%d.",
        target.label,
        side + 1,
        action,
        beforeMeter,
        currentMeter,
        newMeter,
        installId,
        installType);
    LogLine(message);
}

bool IsLikelyActionFieldValue(int value) {
    if (value == -1) {
        return true;
    }
    return value > 0 && value <= 4096;
}

void LogActivationActionSnapshot(
    const MeterInstallTarget& target,
    int side,
    std::uintptr_t activeCharacter,
    std::uintptr_t slot,
    float previousMeter,
    float currentMeter,
    int installId,
    int installType) {
    char header[384]{};
    std::snprintf(
        header,
        std::size(header),
        "%s activation_snapshot side=%d active=0x%llx slot=0x%llx slot_delta=0x%tx "
        "previous_meter=%.1f current_meter=%.1f install_id=%d install_type=%d.",
        target.label,
        side + 1,
        static_cast<unsigned long long>(activeCharacter),
        static_cast<unsigned long long>(slot),
        static_cast<std::ptrdiff_t>(slot - activeCharacter),
        previousMeter,
        currentMeter,
        installId,
        installType);
    LogLine(header);

    std::string chunk;
    int fieldsInChunk = 0;
    int totalFields = 0;
    auto flushChunk = [&]() {
        if (chunk.empty()) {
            return;
        }
        char message[1024]{};
        std::snprintf(
            message,
            std::size(message),
            "%s activation_fields side=%d %s",
            target.label,
            side + 1,
            chunk.c_str());
        LogLine(message);
        chunk.clear();
        fieldsInChunk = 0;
    };

    for (std::ptrdiff_t offset = kActionFieldScanStart; offset < kActionFieldScanEnd; offset += 4) {
        int value = 0;
        if (!ReadMemory(activeCharacter + offset, value) || !IsLikelyActionFieldValue(value)) {
            continue;
        }

        char field[32]{};
        std::snprintf(field, std::size(field), "0x%tx=%d ", offset, value);
        if (chunk.size() + std::strlen(field) > 850 || fieldsInChunk >= 36) {
            flushChunk();
        }
        chunk += field;
        ++fieldsInChunk;
        ++totalFields;
    }
    flushChunk();

    char footer[192]{};
    std::snprintf(
        footer,
        std::size(footer),
        "%s activation_snapshot_done side=%d scanned=0x%tx-0x%tx matched_fields=%d.",
        target.label,
        side + 1,
        kActionFieldScanStart,
        kActionFieldScanEnd,
        totalFields);
    LogLine(footer);
}

float ClampMeter(float meter) {
    if (meter < 0.0f) {
        return 0.0f;
    }
    if (meter > kMaxMeter) {
        return kMaxMeter;
    }
    return meter;
}

bool ReadActiveCharacterMaxHp(std::uintptr_t activeCharacter, int& maxHp) {
    maxHp = 0;
    return ReadMemory(activeCharacter + kCharacterMaxHpOffset, maxHp) &&
           maxHp > 0 &&
           maxHp <= 20000;
}

bool ActiveCharacterLooksLikeMeterTarget(
    std::uintptr_t activeCharacter,
    const MeterInstallTarget& target,
    int& maxHp) {
    if (!ReadActiveCharacterMaxHp(activeCharacter, maxHp)) {
        return false;
    }

    if (target.characterId == kMorriganEngineId) {
        return maxHp == 9500;
    }
    if (target.characterId == kVergilEngineId) {
        return maxHp == 8500;
    }
    return false;
}

void ClearPendingEarlyDebit(MeterInstallRuntimeState& state) {
    state.pendingEarlyDebit = false;
    state.pendingCharacter = 0;
    state.pendingUntil = 0;
}

void RefundUnconfirmedEarlyDebit(
    const MeterInstallTarget& target,
    MeterInstallRuntimeState& state,
    int side,
    std::uintptr_t meterAddress) {
    if (!state.pendingEarlyDebit) {
        return;
    }

    const DWORD now = GetTickCount();
    if (state.pendingUntil && static_cast<LONG>(state.pendingUntil - now) > 0) {
        return;
    }

    float currentMeter = 0.0f;
    if (!ReadMemory(meterAddress, currentMeter) || !IsReasonableFloat(currentMeter)) {
        ClearPendingEarlyDebit(state);
        return;
    }

    const float newMeter = ClampMeter(currentMeter + kOneMeter);
    if (WriteMemory(meterAddress, newMeter)) {
        LogMeterInstallEvent(
            target,
            side,
            "early_debit_refunded_no_install",
            state.lastMeter,
            currentMeter,
            newMeter,
            -1,
            -1);
    } else {
        LogMeterInstallEvent(
            target,
            side,
            "early_debit_refund_failed",
            state.lastMeter,
            currentMeter,
            currentMeter,
            -1,
            -1);
    }
    ClearPendingEarlyDebit(state);
}

void MaybeApplyEarlyMeterDebit(
    const MeterInstallTarget& target,
    MeterInstallRuntimeState& state,
    int side,
    std::uintptr_t meterAddress,
    std::uintptr_t activeCharacter,
    float& currentMeter) {
    if (!kEnableEarlyMeterDebit) {
        return;
    }
    if (state.pendingEarlyDebit || state.active || state.lastMeter < 0.0f) {
        return;
    }

    if (state.lastMeter < target.requiredMeter - kMeterEpsilon ||
        currentMeter < kOneMeter - kMeterEpsilon) {
        return;
    }

    const float meterSpent = state.lastMeter - currentMeter;
    if (meterSpent < kEarlyMeterSpendMin || meterSpent > kEarlyMeterSpendMax) {
        return;
    }

    int maxHp = 0;
    if (!ActiveCharacterLooksLikeMeterTarget(activeCharacter, target, maxHp)) {
        return;
    }

    const float newMeter = ClampMeter(currentMeter - kOneMeter);
    if (!WriteMemory(meterAddress, newMeter)) {
        LogMeterInstallEvent(
            target,
            side,
            "early_extra_debit_failed",
            state.lastMeter,
            currentMeter,
            currentMeter,
            -1,
            -1);
        return;
    }

    state.pendingEarlyDebit = true;
    state.pendingCharacter = activeCharacter;
    state.pendingUntil = GetTickCount() + kPendingInstallConfirmMs;
    LogMeterInstallEvent(
        target,
        side,
        "early_extra_debit_applied",
        state.lastMeter,
        currentMeter,
        newMeter,
        -1,
        maxHp);
    currentMeter = newMeter;
}

void HandleMeterInstallActivation(
    const MeterInstallTarget& target,
    MeterInstallRuntimeState& state,
    int side,
    std::uintptr_t meterAddress,
    std::uintptr_t activeCharacter,
    std::uintptr_t slot,
    float currentMeter,
    int installId,
    int installType) {
    const float previousMeter = state.lastMeter;
    LogActivationActionSnapshot(
        target,
        side,
        activeCharacter,
        slot,
        previousMeter,
        currentMeter,
        installId,
        installType);

    if (!kEnableInstallExtraMeterDebit) {
        LogMeterInstallEvent(
            target,
            side,
            "extra_debit_disabled",
            previousMeter,
            currentMeter,
            currentMeter,
            installId,
            installType);
        ClearPendingEarlyDebit(state);
        return;
    }

    if (state.pendingEarlyDebit) {
        LogMeterInstallEvent(
            target,
            side,
            "early_debit_confirmed",
            previousMeter,
            currentMeter,
            currentMeter,
            installId,
            installType);
        ClearPendingEarlyDebit(state);
        return;
    }

    const bool alreadySpentTwo =
        previousMeter >= 0.0f && previousMeter > currentMeter + (kOneMeter + kOneMeter * 0.5f);
    if (alreadySpentTwo || currentMeter < kOneMeter - kMeterEpsilon) {
        LogMeterInstallEvent(
            target,
            side,
            "extra_debit_skipped",
            previousMeter,
            currentMeter,
            currentMeter,
            installId,
            installType);
        return;
    }

    const float newMeter = currentMeter - kOneMeter;
    WriteMemory(meterAddress, newMeter < 0.0f ? 0.0f : newMeter);
    LogMeterInstallEvent(
        target,
        side,
        "extra_debit_applied",
        previousMeter,
        currentMeter,
        newMeter < 0.0f ? 0.0f : newMeter,
        installId,
        installType);
}

void ResetMeterInstallMonitorState() {
    for (auto& sideStates : g_meterInstallStates) {
        for (auto& state : sideStates) {
            state = {};
            state.lastMeter = -1.0f;
        }
    }
    for (auto& sideStates : g_installDebugStates) {
        for (auto& state : sideStates) {
            state = {};
        }
    }
}

void LogActiveInstallSlot(
    int side,
    std::size_t slotIndex,
    int installId,
    int installType,
    float timer,
    float meter) {
    char message[256]{};
    std::snprintf(
        message,
        std::size(message),
        "Active install side=%d slot_index=%zu slot_offset=0x%tx id=%d type=%d timer=%.1f meter=%.1f.",
        side + 1,
        slotIndex,
        kInstallSlotOffsets[slotIndex],
        installId,
        installType,
        timer,
        meter);
    LogLine(message);
}

void TickInstallSlotDebug(
    int side,
    std::uintptr_t activeCharacter,
    float currentMeter) {
    for (std::size_t slotIndex = 0; slotIndex < kInstallSlotOffsets.size(); ++slotIndex) {
        const auto slot = activeCharacter + kInstallSlotOffsets[slotIndex];
        int installId = 0;
        int installType = 0;
        float timer = 0.0f;
        const bool readable =
            ReadMemory(slot + 0x08, installId) &&
            ReadMemory(slot + 0x0C, installType) &&
            ReadMemory(slot + 0x10, timer);
        const bool active = readable && installId != 0 && IsReasonableFloat(timer) && timer > 0.5f;
        auto& state = g_installDebugStates[side][slotIndex];
        if (active && (!state.active || state.id != installId || state.type != installType)) {
            LogActiveInstallSlot(side, slotIndex, installId, installType, timer, currentMeter);
        }
        state.active = active;
        state.id = active ? installId : 0;
        state.type = active ? installType : 0;
    }
}

void TickMeterInstallMonitor() {
    const auto action = ReadPointer(Rebase(kSActionVa));
    if (!IsReadablePointer(action)) {
        ResetMeterInstallMonitorState();
        return;
    }

    for (int side = 0; side < 2; ++side) {
        const auto team = action + (side == 0 ? kTeam1Offset : kTeam2Offset);
        const auto activeCharacter = ReadPointer(team + kTeamActiveCharacterOffset);
        const auto meterAddress = team + kTeamMeterOffset;

        float currentMeter = 0.0f;
        if (!ReadMemory(meterAddress, currentMeter) ||
            !IsReasonableFloat(currentMeter) ||
            !IsReadablePointer(activeCharacter)) {
            for (auto& state : g_meterInstallStates[side]) {
                state = {};
                state.lastMeter = -1.0f;
            }
            for (auto& state : g_installDebugStates[side]) {
                state = {};
            }
            continue;
        }

        TickInstallSlotDebug(side, activeCharacter, currentMeter);

        for (std::size_t targetIndex = 0; targetIndex < kMeterInstallTargets.size(); ++targetIndex) {
            const auto& target = kMeterInstallTargets[targetIndex];
            auto& state = g_meterInstallStates[side][targetIndex];
            RefundUnconfirmedEarlyDebit(target, state, side, meterAddress);
            MaybeApplyEarlyMeterDebit(target, state, side, meterAddress, activeCharacter, currentMeter);

            bool foundActiveInstall = false;
            std::uintptr_t foundSlot = 0;
            int foundInstallId = -1;
            int foundInstallType = -1;
            float foundTimer = 0.0f;

            for (const auto slotOffset : kInstallSlotOffsets) {
                const auto slot = activeCharacter + slotOffset;
                int installId = -1;
                int installType = -1;
                float timer = 0.0f;
                if (InstallSlotMatchesTarget(slot, target, installId, installType, timer)) {
                    foundActiveInstall = true;
                    foundSlot = slot;
                    foundInstallId = installId;
                    foundInstallType = installType;
                    foundTimer = timer;
                    break;
                }
            }

            if (foundActiveInstall && (!state.active || state.activeSlot != foundSlot)) {
                (void)foundTimer;
                HandleMeterInstallActivation(
                    target,
                    state,
                    side,
                    meterAddress,
                    activeCharacter,
                    foundSlot,
                    currentMeter,
                    foundInstallId,
                    foundInstallType);
            }

            state.active = foundActiveInstall;
            state.activeSlot = foundActiveInstall ? foundSlot : 0;
            state.lastMeter = currentMeter;
        }
    }
}

DWORD WINAPI DoomMissileMonitorThread(LPVOID) {
    LogLine("Doom Hidden Missiles monitor started.");
    while (g_monitorThreadsRunning.load()) {
        TickDoomMissileMonitor();
        Sleep(16);
    }
    LogLine("Doom Hidden Missiles monitor stopped.");
    return 0;
}

DWORD WINAPI MeterInstallMonitorThread(LPVOID) {
    LogLine("Meter install monitor started.");
    ResetMeterInstallMonitorState();
    while (g_monitorThreadsRunning.load()) {
        TickMeterInstallMonitor();
        Sleep(16);
    }
    LogLine("Meter install monitor stopped.");
    return 0;
}

bool IsSupportedGame() {
    char modulePath[MAX_PATH]{};
    GetModuleFileNameA(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
    const char* fileName = std::strrchr(modulePath, '\\');
    fileName = fileName ? fileName + 1 : modulePath;
    return lstrcmpiA(fileName, "umvc3.exe") == 0;
}

std::uintptr_t ReadSelectedObject() {
    const auto root = reinterpret_cast<std::uintptr_t*>(Rebase(kSelectedIdRootVa));
    if (!CanRead(root, sizeof(*root))) {
        return 0;
    }

    const auto selectedObject = *root;
    if (!selectedObject) {
        return 0;
    }
    return selectedObject;
}

int ReadSelectedCharacterId() {
    const auto selectedObject = ReadSelectedObject();
    if (!selectedObject) {
        return -1;
    }

    const auto selectedId = reinterpret_cast<int*>(selectedObject + kSelectedIdOffset);
    if (!CanRead(selectedId, sizeof(*selectedId))) {
        return -1;
    }
    return *selectedId;
}

int ReadSelectedPhase() {
    const auto selectedObject = ReadSelectedObject();
    if (!selectedObject) {
        return -1;
    }

    const auto phase = reinterpret_cast<int*>(selectedObject + kSelectedPhaseOffset);
    if (!CanRead(phase, sizeof(*phase))) {
        return -1;
    }
    return *phase;
}

bool ShouldAllowTwoMeterAction(
    void* characterPointer,
    int requestedAction) {
    const auto character =
        reinterpret_cast<std::uintptr_t>(characterPointer);
    if (!IsReadablePointer(character)) {
        return true;
    }

    const int characterId = ResolveCharacterId(character);
    const bool isSpiralSwords =
        characterId == kVergilEngineId &&
        requestedAction == kVergilSpiralSwordsAction;
    const bool isAstralVision =
        characterId == kMorriganEngineId &&
        requestedAction == kMorriganAstralVisionAction;
    if (!isSpiralSwords && !isAstralVision) {
        return true;
    }

    const auto team = ResolveActiveTeam(character);
    float currentMeter = 0.0f;
    if (!team ||
        !ReadMemory(team + kTeamMeterOffset, currentMeter) ||
        !IsReasonableFloat(currentMeter)) {
        return true;
    }

    const bool blocked =
        currentMeter + kMeterEpsilon < kTwoMeters;
    char message[256]{};
    std::snprintf(
        message,
        std::size(message),
        "Native traced-action gate character=%d action=%d meter=%.1f result=%s.",
        characterId,
        requestedAction,
        currentMeter,
        blocked ? "blocked" : "allowed");
    LogLine(message);

    if (blocked) {
        g_blockedMeterTeam.store(team);
        g_blockedMeterUntil.store(GetTickCount() + 250);
    }
    return !blocked;
}

void HookedMeterSetter(void* teamPointer, float requestedMeter) {
    const auto team = reinterpret_cast<std::uintptr_t>(teamPointer);
    float currentMeter = 0.0f;
    const auto activeCharacter = ReadPointer(team + kTeamActiveCharacterOffset);
    int currentAction = 0;

    if (ReadMemory(team + kTeamMeterOffset, currentMeter) &&
        IsReasonableFloat(currentMeter)) {
        const float nativeSpend = currentMeter - requestedMeter;
        const DWORD blockedUntil = g_blockedMeterUntil.load();
        const bool cancelBlockedSpend =
            g_blockedMeterTeam.load() == team &&
            static_cast<LONG>(blockedUntil - GetTickCount()) > 0 &&
            std::fabs(nativeSpend - kOneMeter) <= kMeterEpsilon;
        if (cancelBlockedSpend) {
            requestedMeter = currentMeter;
            g_blockedMeterTeam.store(0);
            g_blockedMeterUntil.store(0);
            LogLine("Native blocked two-meter action debit canceled.");
        } else if (
            std::fabs(nativeSpend - kOneMeter) <= kMeterEpsilon &&
            IsReadablePointer(activeCharacter) &&
            ReadMemory(
                activeCharacter + kCharacterCurrentActionOffset,
                currentAction)) {
            const int characterId = ResolveCharacterId(activeCharacter);
            const bool isSpiralSwords =
                characterId == kVergilEngineId &&
                currentAction == kVergilSpiralSwordsAction;
            const bool isAstralVision =
                characterId == kMorriganEngineId &&
                currentAction == kMorriganAstralVisionAction;
            if (isSpiralSwords || isAstralVision) {
                requestedMeter -= kOneMeter;
                char message[256]{};
                std::snprintf(
                    message,
                    std::size(message),
                    "Native two-meter debit character=%d action=%d meter_before=%.1f meter_requested=%.1f meter_final=%.1f.",
                    characterId,
                    currentAction,
                    currentMeter,
                    currentMeter - nativeSpend,
                    requestedMeter);
                LogLine(message);
            }
        }
    }

    float clampedMeter = requestedMeter;
    if (!std::isfinite(clampedMeter) || clampedMeter < 0.0f) {
        clampedMeter = 0.0f;
    } else if (clampedMeter > kMaxMeter) {
        clampedMeter = kMaxMeter;
    }
    WriteMemory(team + kTeamMeterOffset, clampedMeter);
}

void WriteAbsoluteJump(void* source, void* destination) {
    auto* cursor = static_cast<unsigned char*>(source);
    cursor[0] = 0xFF;
    cursor[1] = 0x25;
    cursor[2] = 0x00;
    cursor[3] = 0x00;
    cursor[4] = 0x00;
    cursor[5] = 0x00;
    *reinterpret_cast<void**>(cursor + 6) = destination;
}

void* AllocateRelayNear(void* target, std::size_t size) {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity =
        static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    const auto targetAddress = reinterpret_cast<std::uintptr_t>(target);
    const auto alignedTarget = targetAddress & ~(granularity - 1);
    constexpr std::uintptr_t kMaxRelayDistance = 0x70000000ull;

    for (std::uintptr_t distance = granularity;
         distance <= kMaxRelayDistance;
         distance += granularity) {
        const std::array<std::uintptr_t, 2> candidates = {
            alignedTarget > distance ? alignedTarget - distance : 0,
            alignedTarget + distance,
        };
        for (const auto candidate : candidates) {
            if (!candidate) {
                continue;
            }
            auto* relay = VirtualAlloc(
                reinterpret_cast<void*>(candidate),
                size,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_EXECUTE_READWRITE);
            if (!relay) {
                continue;
            }
            const auto displacement =
                reinterpret_cast<std::intptr_t>(relay) -
                static_cast<std::intptr_t>(targetAddress + 5);
            if (displacement >= INT32_MIN && displacement <= INT32_MAX) {
                return relay;
            }
            VirtualFree(relay, 0, MEM_RELEASE);
        }
    }
    return nullptr;
}

bool InstallAbsoluteJumpReplacement(
    void* target,
    void* replacement,
    std::size_t patchSize,
    const unsigned char* expectedBytes,
    std::size_t expectedSize) {
    if (patchSize < 14 || expectedSize > patchSize) {
        LogLine("InstallAbsoluteJumpReplacement failed: invalid patch size.");
        return false;
    }
    if (!CanRead(target, patchSize) ||
        std::memcmp(target, expectedBytes, expectedSize) != 0) {
        LogLine("InstallAbsoluteJumpReplacement failed: target bytes do not match this game build.");
        return false;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(target, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LogLine("InstallAbsoluteJumpReplacement failed: VirtualProtect target failed.");
        return false;
    }

    WriteAbsoluteJump(target, replacement);
    if (patchSize > 14) {
        std::memset(static_cast<unsigned char*>(target) + 14, 0x90, patchSize - 14);
    }

    DWORD ignored = 0;
    VirtualProtect(target, patchSize, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), target, patchSize);
    return true;
}

bool InstallAbsoluteJumpHook(void* target, void* replacement, std::size_t patchSize, void** original) {
    if (patchSize < 14) {
        LogLine("InstallAbsoluteJumpHook failed: patch size is too small.");
        return false;
    }

    auto* trampoline = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, patchSize + 14, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!trampoline) {
        LogLine("InstallAbsoluteJumpHook failed: trampoline allocation failed.");
        return false;
    }

    std::memcpy(trampoline, target, patchSize);
    WriteAbsoluteJump(trampoline + patchSize, static_cast<unsigned char*>(target) + patchSize);

    DWORD oldProtect = 0;
    if (!VirtualProtect(target, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LogLine("InstallAbsoluteJumpHook failed: VirtualProtect target failed.");
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    WriteAbsoluteJump(target, replacement);
    if (patchSize > 14) {
        std::memset(static_cast<unsigned char*>(target) + 14, 0x90, patchSize - 14);
    }

    DWORD ignored = 0;
    VirtualProtect(target, patchSize, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), target, patchSize);
    *original = trampoline;
    return true;
}

bool InstallActionTransitionWriteGate() {
    constexpr unsigned char
        kExpectedBytes[kActionTransitionWritePatchSize] = {
            0x8B, 0x87, 0x10, 0x13, 0x00, 0x00,
            0x81, 0x8F, 0xF0, 0x14, 0x00, 0x00,
            0x08, 0x00, 0x02, 0x00,
            0x89, 0xB7, 0x10, 0x13, 0x00, 0x00,
            0x89, 0x87, 0x14, 0x13, 0x00, 0x00,
        };
    auto* target =
        reinterpret_cast<unsigned char*>(Rebase(kActionTransitionWriteVa));
    if (!CanRead(target, kActionTransitionWritePatchSize) ||
        std::memcmp(
            target,
            kExpectedBytes,
            std::size(kExpectedBytes)) != 0) {
        LogLine(
            "InstallActionTransitionWriteGate failed: target bytes do not match this game build.");
        return false;
    }

    constexpr std::size_t kStubCapacity = 128;
    auto* stub = static_cast<unsigned char*>(
        AllocateRelayNear(target, kStubCapacity));
    if (!stub) {
        LogLine(
            "InstallActionTransitionWriteGate failed: nearby stub allocation failed.");
        return false;
    }

    std::array<unsigned char, kStubCapacity> code{};
    std::size_t cursor = 0;
    const auto emit = [&](std::initializer_list<unsigned char> bytes) {
        for (const auto byte : bytes) {
            code[cursor++] = byte;
        }
    };
    const auto emit64 = [&](std::uintptr_t value) {
        std::memcpy(code.data() + cursor, &value, sizeof(value));
        cursor += sizeof(value);
    };
    const auto patchRelativeByte = [&](
        std::size_t displacementOffset,
        std::size_t destinationOffset) {
        const auto displacement =
            static_cast<std::ptrdiff_t>(destinationOffset) -
            static_cast<std::ptrdiff_t>(displacementOffset + 1);
        if (displacement < INT8_MIN || displacement > INT8_MAX) {
            return false;
        }
        code[displacementOffset] =
            static_cast<unsigned char>(
                static_cast<std::int8_t>(displacement));
        return true;
    };

    emit({0x81, 0xFE, 0xDC, 0x00, 0x00, 0x00});  // cmp esi, 220
    emit({0x74, 0x00});
    const std::size_t spiralJump = cursor - 1;
    emit({0x81, 0xFE, 0xE1, 0x00, 0x00, 0x00});  // cmp esi, 225
    emit({0x75, 0x00});
    const std::size_t nonTargetJump = cursor - 1;

    const std::size_t gateOffset = cursor;
    emit({0x48, 0x83, 0xEC, 0x20});              // sub rsp, 0x20
    emit({0x48, 0x8B, 0xCF});                    // mov rcx, rdi
    emit({0x8B, 0xD6});                          // mov edx, esi
    emit({0x48, 0xB8});                          // mov rax, imm64
    emit64(reinterpret_cast<std::uintptr_t>(
        &ShouldAllowTwoMeterAction));
    emit({0xFF, 0xD0});                          // call rax
    emit({0x48, 0x83, 0xC4, 0x20});              // add rsp, 0x20
    emit({0x84, 0xC0});                          // test al, al
    emit({0x74, 0x00});
    const std::size_t blockedJump = cursor - 1;

    const std::size_t originalWriteOffset = cursor;
    emit({
        0x8B, 0x87, 0x10, 0x13, 0x00, 0x00,
        0x81, 0x8F, 0xF0, 0x14, 0x00, 0x00,
        0x08, 0x00, 0x02, 0x00,
        0x89, 0xB7, 0x10, 0x13, 0x00, 0x00,
        0x89, 0x87, 0x14, 0x13, 0x00, 0x00,
    });

    const std::size_t resumeOffset = cursor;
    emit({0x48, 0xB8});                          // mov rax, imm64
    emit64(
        reinterpret_cast<std::uintptr_t>(target) +
        kActionTransitionWritePatchSize);
    emit({0xFF, 0xE0});                          // jmp rax

    if (!patchRelativeByte(spiralJump, gateOffset) ||
        !patchRelativeByte(nonTargetJump, originalWriteOffset) ||
        !patchRelativeByte(blockedJump, resumeOffset)) {
        VirtualFree(stub, 0, MEM_RELEASE);
        LogLine(
            "InstallActionTransitionWriteGate failed: generated branch is out of range.");
        return false;
    }

    std::memcpy(stub, code.data(), cursor);
    FlushInstructionCache(GetCurrentProcess(), stub, cursor);

    const auto displacement64 =
        reinterpret_cast<std::intptr_t>(stub) -
        reinterpret_cast<std::intptr_t>(target + 5);
    if (displacement64 < INT32_MIN || displacement64 > INT32_MAX) {
        VirtualFree(stub, 0, MEM_RELEASE);
        LogLine(
            "InstallActionTransitionWriteGate failed: stub is out of relative-jump range.");
        return false;
    }
    const auto displacement =
        static_cast<std::int32_t>(displacement64);

    DWORD oldProtect = 0;
    if (!VirtualProtect(
            target,
            kActionTransitionWritePatchSize,
            PAGE_EXECUTE_READWRITE,
            &oldProtect)) {
        VirtualFree(stub, 0, MEM_RELEASE);
        LogLine(
            "InstallActionTransitionWriteGate failed: VirtualProtect target failed.");
        return false;
    }
    target[0] = 0xE9;
    std::memcpy(target + 1, &displacement, sizeof(displacement));
    std::memset(
        target + 5,
        0x90,
        kActionTransitionWritePatchSize - 5);

    DWORD ignored = 0;
    VirtualProtect(
        target,
        kActionTransitionWritePatchSize,
        oldProtect,
        &ignored);
    FlushInstructionCache(
        GetCurrentProcess(),
        target,
        kActionTransitionWritePatchSize);
    LogLine("Native traced action-transition gate installed.");
    return true;
}

bool InstallMeterSetterHook() {
    constexpr unsigned char kExpectedMeterSetterBytes[kMeterSetterPatchSize] = {
        0x0F, 0x2F, 0x0D, 0x7D, 0x7D, 0xA4, 0x00,
        0xF3, 0x0F, 0x11, 0x49, 0x78,
        0x73, 0x07,
    };
    auto* target = reinterpret_cast<void*>(Rebase(kMeterSetterVa));
    if (!InstallAbsoluteJumpReplacement(
            target,
            reinterpret_cast<void*>(&HookedMeterSetter),
            kMeterSetterPatchSize,
            kExpectedMeterSetterBytes,
            std::size(kExpectedMeterSetterBytes))) {
        return false;
    }
    LogLine("Native meter setter hook installed.");
    return true;
}

void HookedInputHelper(void* manager, int playerIndex, void* inputOut) {
    g_originalInputHelper(manager, playerIndex, inputOut);

    if (playerIndex != 0 || !inputOut) {
        return;
    }

    if (g_debugDumpFrames.exchange(0) > 0) {
        auto* bytes = static_cast<unsigned char*>(inputOut);
        if (CanRead(bytes, 32)) {
            char message[256]{};
            int written = std::snprintf(message, std::size(message), "Debug inputOut dump:");
            for (int i = 0; i < 32 && written > 0 && written < static_cast<int>(std::size(message)); ++i) {
                written += std::snprintf(
                    message + written,
                    std::size(message) - static_cast<std::size_t>(written),
                    " %02X",
                    bytes[i]);
            }
            LogLine(message);
        } else {
            LogLine("Debug inputOut dump failed: inputOut is not readable.");
        }
    }

    const int debugFrames = g_debugInputFrames.load();
    if (debugFrames > 0) {
        const int offset = g_debugInputOffset.load();
        const int mask = g_debugInputMask.load();
        if (offset >= 0 && offset < 64 && mask > 0 && mask <= 0xFF) {
            auto* flags = static_cast<unsigned char*>(inputOut) + offset;
            if (CanWrite(flags, sizeof(*flags))) {
                *flags = static_cast<unsigned char>(*flags | static_cast<unsigned char>(mask));
                if (!g_debugInputLogged.exchange(true)) {
                    char message[160]{};
                    std::snprintf(
                        message,
                        std::size(message),
                        "Debug input inject started offset=0x%x mask=0x%x frames=%d.",
                        offset,
                        mask,
                        debugFrames);
                    LogLine(message);
                }
            } else {
                LogLine("Debug input inject failed: target byte is not writable.");
            }
        }
        const int remaining = g_debugInputFrames.fetch_sub(1) - 1;
        if (remaining <= 0) {
            g_debugInputOffset.store(-1);
            g_debugInputMask.store(0);
            g_debugInputLogged.store(false);
            LogLine("Debug input inject completed.");
        }
    }

    if (g_forceDebugP1Confirm.exchange(false)) {
        auto* flags = static_cast<unsigned char*>(inputOut) + kInputFlagsOffset;
        if (CanWrite(flags, sizeof(*flags))) {
            *flags = static_cast<unsigned char>(*flags | kConfirmInputBit);
            LogLine("Debug P1 confirm bit injected into native input helper.");
        } else {
            LogLine("Debug P1 confirm failed: input flags are not writable.");
        }
        return;
    }

    if (!g_forceZeroNativeConfirm.exchange(false)) {
        return;
    }

    if (ReadSelectedCharacterId() != kZeroEngineId || ReadSelectedPhase() != kCharacterHoverPhase) {
        LogLine("Forced Zero confirm skipped: selected state changed before helper frame.");
        return;
    }

    auto* flags = static_cast<unsigned char*>(inputOut) + kInputFlagsOffset;
    if (!CanWrite(flags, sizeof(*flags))) {
        LogLine("Forced Zero confirm failed: input flags are not writable.");
        return;
    }

    *flags = static_cast<unsigned char>(*flags | kConfirmInputBit);
    LogLine("Forced Zero confirm bit injected into native input helper.");
}

bool InstallInputHelperHook() {
    if (g_originalInputHelper) {
        return true;
    }

    auto* target = reinterpret_cast<void*>(Rebase(kCharacterSelectInputHelperVa));
    void* original = nullptr;
    if (!InstallAbsoluteJumpHook(target, reinterpret_cast<void*>(&HookedInputHelper), kInputHelperPatchSize, &original)) {
        return false;
    }

    g_originalInputHelper = reinterpret_cast<InputHelperFn>(original);
    LogLine("Character-select input helper hook installed.");
    return true;
}

bool IsGameForeground() {
    HWND foreground = GetForegroundWindow();
    if (!foreground || foreground == g_gameWindow) {
        return foreground == g_gameWindow;
    }

    HWND rootOwner = GetAncestor(foreground, GA_ROOTOWNER);
    return rootOwner == g_gameWindow;
}

bool IsConfirmTemporarilyAllowed() {
    const DWORD allowUntil = g_allowConfirmUntil.load();
    return allowUntil != 0 && static_cast<LONG>(allowUntil - GetTickCount()) > 0;
}

struct ModalPromptState {
    int result = IDNO;
};

LRESULT CALLBACK SecondPromptWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<ModalPromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        state = static_cast<ModalPromptState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }

    switch (message) {
        case WM_COMMAND:
            if (LOWORD(wparam) == kSecondPromptConfirmId) {
                state->result = IDYES;
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wparam) == kSecondPromptCancelId) {
                state->result = IDNO;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            if (state) {
                state->result = IDNO;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            return 0;
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void ApplyDefaultFont(HWND control, HFONT font) {
    if (control && font) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

int ShowSecondZeroPrompt(HWND owner) {
    static const wchar_t kClassName[] = L"UMVC3ZeroSecondPrompt";
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = SecondPromptWndProc;
    windowClass.hInstance = g_module;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32514));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = kClassName;
    RegisterClassW(&windowClass);

    ModalPromptState state{};
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int width = 430;
    const int height = 170;
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

    HWND dialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        kClassName,
        L"Are you really sure?",
        WS_CAPTION | WS_SYSMENU | WS_POPUP,
        x,
        y,
        width,
        height,
        owner,
        nullptr,
        g_module,
        &state);
    if (!dialog) {
        LogLine("Second prompt creation failed.");
        return IDNO;
    }

    HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    ApplyDefaultFont(
        CreateWindowExW(
            0,
            L"STATIC",
            L"Are you really sure?",
            WS_CHILD | WS_VISIBLE,
            24,
            24,
            width - 48,
            24,
            dialog,
            nullptr,
            g_module,
            nullptr),
        font);
    ApplyDefaultFont(
        CreateWindowExW(
            0,
            L"BUTTON",
            L"I suck to pick Zero",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            34,
            86,
            180,
            30,
            dialog,
            reinterpret_cast<HMENU>(kSecondPromptConfirmId),
            g_module,
            nullptr),
        font);
    ApplyDefaultFont(
        CreateWindowExW(
            0,
            L"BUTTON",
            L"On second thought...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            230,
            86,
            160,
            30,
            dialog,
            reinterpret_cast<HMENU>(kSecondPromptCancelId),
            g_module,
            nullptr),
        font);

    HWND previousFocus = GetFocus();
    EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    SetForegroundWindow(dialog);

    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (previousFocus && IsWindow(previousFocus)) {
        SetFocus(previousFocus);
    }
    return state.result;
}

DWORD WINAPI PromptThread(LPVOID) {
    LogLine("Prompt opened.");
    const int firstResult = MessageBoxA(
        g_gameWindow,
        "Are you sure?",
        "Zero Confirmation",
        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_TASKMODAL | MB_SETFOREGROUND);

    if (firstResult == IDYES) {
        LogLine("First prompt answered Yes; opening second prompt.");
        const int secondResult = ShowSecondZeroPrompt(g_gameWindow);
        if (secondResult == IDYES) {
            if (g_originalInputHelper && ReadSelectedCharacterId() == kZeroEngineId &&
                ReadSelectedPhase() == kCharacterHoverPhase) {
                g_forceZeroNativeConfirm.store(true);
                LogLine("Second prompt confirmed; queued native confirm bit.");
            } else {
                LogLine("Second prompt confirmed; input helper unavailable, next confirm allowed for five seconds.");
                g_allowConfirmUntil.store(GetTickCount() + 5000);
            }
        } else {
            LogLine("Second prompt canceled; confirm blocked.");
        }
    } else {
        LogLine("First prompt answered No; confirm blocked.");
    }

    g_promptOpen.store(false);
    return 0;
}

LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION) {
        const auto* key = reinterpret_cast<KBDLLHOOKSTRUCT*>(lparam);
        const bool isReturn = key && key->vkCode == VK_RETURN;
        const bool isDebugConfirm = key && key->vkCode == VK_F6;
        const bool isKeyDown = wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN;
        const bool isKeyUp = wparam == WM_KEYUP || wparam == WM_SYSKEYUP;

        if (isDebugConfirm && isKeyDown && IsGameForeground()) {
            g_forceDebugP1Confirm.store(true);
            LogLine("Debug P1 confirm queued from F6.");
            return 1;
        }

        if (isReturn && (isKeyDown || isKeyUp) && IsGameForeground() &&
            ReadSelectedCharacterId() == kZeroEngineId && ReadSelectedPhase() == kCharacterHoverPhase) {
            if (IsConfirmTemporarilyAllowed()) {
                if (isKeyDown) {
                    LogLine("Allowed Zero confirm after Yes.");
                }
                return CallNextHookEx(g_keyboardHook, code, wparam, lparam);
            }

            if (isKeyDown && !g_promptOpen.exchange(true)) {
                HANDLE thread = CreateThread(nullptr, 0, PromptThread, nullptr, 0, nullptr);
                if (thread) {
                    CloseHandle(thread);
                } else {
                    LogLine("Failed to create prompt thread.");
                    g_promptOpen.store(false);
                }
            }
            return 1;
        }
    }

    return CallNextHookEx(g_keyboardHook, code, wparam, lparam);
}

bool TryReadDebugInputCommand(char* buffer, DWORD bufferSize) {
    if (!buffer || bufferSize < 2) {
        return false;
    }

    const auto path = DebugInputCommandPath();
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD read = 0;
    const BOOL ok = ReadFile(file, buffer, bufferSize - 1, &read, nullptr);
    CloseHandle(file);
    DeleteFileW(path.c_str());
    if (!ok || read == 0) {
        return false;
    }

    buffer[read] = '\0';
    return true;
}

void ApplyDebugInputCommand(const char* command) {
    if (!command) {
        return;
    }

    if (std::strstr(command, "dump")) {
        g_debugDumpFrames.store(1);
        LogLine("Debug input dump queued.");
        return;
    }

    int offset = -1;
    int mask = 0;
    int frames = 0;
    if (std::sscanf(command, "inject %i %i %i", &offset, &mask, &frames) == 3 &&
        offset >= 0 && offset < 64 && mask > 0 && mask <= 0xFF && frames > 0 && frames <= 600) {
        g_debugInputOffset.store(offset);
        g_debugInputMask.store(mask);
        g_debugInputFrames.store(frames);
        g_debugInputLogged.store(false);
        char message[160]{};
        std::snprintf(
            message,
            std::size(message),
            "Debug input command queued offset=0x%x mask=0x%x frames=%d.",
            offset,
            mask,
            frames);
        LogLine(message);
        return;
    }

    LogLine("Debug input command ignored: expected `dump` or `inject <offset> <mask> <frames>`.");
}

DWORD WINAPI DebugInputCommandThread(LPVOID) {
    LogLine("Debug input command monitor started.");
    char command[256]{};
    while (g_monitorThreadsRunning.load()) {
        if (TryReadDebugInputCommand(command, static_cast<DWORD>(std::size(command)))) {
            ApplyDebugInputCommand(command);
        }
        Sleep(50);
    }
    return 0;
}

BOOL CALLBACK EnumWindowsForCurrentProcess(HWND hwnd, LPARAM lparam) {
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid != GetCurrentProcessId() || !IsWindowVisible(hwnd)) {
        return TRUE;
    }

    wchar_t title[256]{};
    GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    if (std::wcsstr(title, L"ULTIMATE MARVEL VS. CAPCOM 3")) {
        *reinterpret_cast<HWND*>(lparam) = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND FindGameWindow() {
    HWND hwnd = nullptr;
    EnumWindows(EnumWindowsForCurrentProcess, reinterpret_cast<LPARAM>(&hwnd));
    return hwnd;
}

DWORD WINAPI InitializeThread(LPVOID) {
    LogLine("InitializeThread started.");

    if (!IsSupportedGame()) {
        LogLine("Unsupported executable name.");
        return 0;
    }
    LogLine("Executable name validated.");

    for (int attempt = 0; attempt < 300 && !g_gameWindow; ++attempt) {
        g_gameWindow = FindGameWindow();
        if (!g_gameWindow) {
            Sleep(100);
        }
    }

    if (!g_gameWindow) {
        LogLine("Could not find the UMVC3 game window.");
        return 0;
    }
    LogLine("UMVC3 game window found.");

    if (!InstallInputHelperHook()) {
        LogLine("Could not install character-select input helper hook.");
    }
    if (!InstallActionTransitionWriteGate()) {
        LogLine("Could not install native traced action-transition gate.");
    }
    if (!InstallMeterSetterHook()) {
        LogLine("Could not install native meter setter hook.");
    }

    HANDLE doomMonitorThread = CreateThread(nullptr, 0, DoomMissileMonitorThread, nullptr, 0, nullptr);
    if (doomMonitorThread) {
        CloseHandle(doomMonitorThread);
        LogLine("Doom Hidden Missiles monitor thread created.");
    } else {
        LogLine("Could not create Doom Hidden Missiles monitor thread.");
    }

    HANDLE meterMonitorThread = CreateThread(nullptr, 0, MeterInstallMonitorThread, nullptr, 0, nullptr);
    if (meterMonitorThread) {
        CloseHandle(meterMonitorThread);
        LogLine("Meter install monitor thread created.");
    } else {
        LogLine("Could not create meter install monitor thread.");
    }

    HANDLE debugInputThread = CreateThread(nullptr, 0, DebugInputCommandThread, nullptr, 0, nullptr);
    if (debugInputThread) {
        CloseHandle(debugInputThread);
        LogLine("Debug input command monitor thread created.");
    } else {
        LogLine("Could not create debug input command monitor thread.");
    }

    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, g_module, 0);
    if (!g_keyboardHook) {
        LogLine("Could not install low-level keyboard hook.");
    } else {
        LogLine("Low-level keyboard hook installed.");
        LogFormat("Initial selected character ID: ", ReadSelectedCharacterId());
        LogFormat("Initial selected phase: ", ReadSelectedPhase());
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}

}  // namespace

extern "C" __declspec(dllexport) void InitializeASI() {
    LogLine("InitializeASI called.");
    HANDLE thread = CreateThread(nullptr, 0, InitializeThread, nullptr, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    } else {
        LogLine("CreateThread failed.");
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        g_monitorThreadsRunning.store(true);
    } else if (reason == DLL_PROCESS_DETACH) {
        g_monitorThreadsRunning.store(false);
        if (g_keyboardHook) {
            LogLine("DllMain detach: unhooking low-level keyboard hook.");
            UnhookWindowsHookEx(g_keyboardHook);
            g_keyboardHook = nullptr;
        }
    }
    return TRUE;
}
