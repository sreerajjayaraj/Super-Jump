#include "BoostConfig.h"
#include "GameImage.h"
#include "HookGroup.h"
#include "JumpFrameQueue.h"
#include "LayoutValidation.h"
#include "NativeDoubleJump.h"
#include "PalmPublishPolicy.h"
#include "SessionLog.h"

#include <PalmBinding.h>
#include <SuperJumpHostPolicy.h>

#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <immintrin.h>
#include <mutex>
#include <new>
#include <string>

namespace superjump {
namespace {

constexpr unsigned boostFrames = 5;
constexpr std::uint64_t startDeadlineMs = 250;
constexpr boss::Reader reader{nullptr, ReadGameMemory};

struct NativeContext {
    const DWORD error = GetLastError();
    const unsigned mxcsr = _mm_getcsr();
    ~NativeContext() { _mm_setcsr(mxcsr); SetLastError(error); }
};

struct Handle {
    HANDLE value{};
    ~Handle() { if (value) CloseHandle(value); }
};

std::filesystem::path ModulePath(HMODULE module) {
    std::wstring path(32768, L'\0');
    const auto size = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!size || size >= path.size()) return {};
    path.resize(size);
    return path;
}

bool GameHasFocus() noexcept {
    const auto window = GetForegroundWindow();
    DWORD owner{};
    return window && GetWindowThreadProcessId(window, &owner) && owner == GetCurrentProcessId() &&
           GetForegroundWindow() == window;
}

bool SamePalmOwner(const motiontrace::Snapshot& a, const traversal::Snapshot& b) noexcept {
    return a.valid && b.valid && a.ground_known && a.root == b.root && a.context == b.context &&
        a.actor == b.actor && a.uid == b.uid && a.parts == b.parts && a.controller == b.controller &&
        a.movement_state == b.movement && a.physics == b.physics && a.contact == b.contact &&
        a.ground == b.ground && a.grounded == b.grounded && b.lane0 == 7 && b.lane1 == 7;
}

palm::Action CurrentAction(const motiontrace::Snapshot& snapshot) noexcept {
    const auto action = palm::binding::Classify(reader, snapshot);
    if (action.valid) return action.action;
    using A = palm::Action;
    const auto& slot = snapshot.upper.present && snapshot.upper.effective ? snapshot.upper : snapshot.lower;
    if (slot.present && slot.effective && !slot.suppressed) {
        switch (slot.skill_key) {
        case 15033: case 15155: case 15158: return A::GroundPalm;
        case 15035: case 10372: return A::OtherAir;
        case 10018: return A::Fall;
        case 10052: case 10053: return A::Freeclimb;
        case 15028: case 15031: case 15036: return A::Flight;
        case 15032: case 15037: return A::Swimming;
        default: break;
        }
    }
    return snapshot.grounded ? A::Ground : A::Unknown;
}

struct Decision {
    traversal::Snapshot actor;
    std::uint32_t hero{};
    bool allowed{};
};

struct Pending {
    traversal::Epoch owner;
    std::uintptr_t metadata{};
    std::uint64_t firstDeadline{};
    std::uint64_t triggerTick{};
    std::uint32_t hero{};
    unsigned remaining{};
    float boostY{};
};

enum class Eligibility { Waiting, Unfocused, MovementBlocked, MenuOrEncounter, ActorUnknown,
                         TraversalBlocked, Gameplay };

struct JumpStats {
    unsigned frames{}, neutral{}, first{}, second{}, early{}, cooldown{}, missed{}, queued{}, cancelled{};
    unsigned nativeGround{}, nativeAir{}, nativeDoubleJump{}, nativeQueued{}, verifiedDamianeLaunch{};
    float maxRise{};
};

struct JumpReport {
    JumpStats counts{};
    Eligibility eligibility{Eligibility::Waiting};
    std::uint32_t hero{};
    bool available{};
};

class Runtime;
std::atomic<Runtime*> activeRuntime{};

class Runtime {
public:
    explicit Runtime(HMODULE game) : game_(game),
        base_(reinterpret_cast<std::uintptr_t>(game)) {}

    bool Start(const std::filesystem::path& modulePath, const std::filesystem::path& gamePath) {
        log_.Start(modulePath, gamePath);
        config_.SetPath(modulePath.parent_path() / L"SuperJump_ThirdPalm.ini");
        PollBoostConfig(GetTickCount64());
        if (!AcquireLeases()) return Fail("startup", "VARIANT_REFUSED", "LeaseUnavailable");
        if (GetModuleHandleW(L"SJNativeMotionTrace.asi"))
            return Fail("startup", "VARIANT_REFUSED", "ConflictingProvider");

        const auto resolution = ResolveGameImage(game_);
        if (resolution.error != ResolveError::None)
            return Fail("scan", "RESOLUTION_FAILED", ResolveReason(resolution.error));
        sites_ = resolution.live;
        log_.Emit(LogLevel::Info, "scan", "RESOLUTION_COMPLETED", sites_.sites.HasJumpCommit() ?
                  "result=AVAILABLE targets=2 jump_commit=VERIFIED" :
                  "result=AVAILABLE targets=2 jump_commit=UNVERIFIED");

        profile_ = boss::ValidateProfile(reader, base_);
        if (!profile_.valid) return Fail("scan", "VALIDATION_FAILED", "ProfileMismatch");
        provider_ = palm::menu_mode::RetainExactMenu(reader);
        if (!AwaitLayout())
            return Fail("scan", "VALIDATION_FAILED", "NativeContractMismatch");

        const auto allocator = host::HookAllocator(game_);
        if (!allocator) return Fail("hook", "HOOK_INSTALL_FAILED", "AllocatorUnavailable");
        const auto outcome = hooks_.Install(sites_, base_, allocator, {MotionCallback, PublisherCallback});
        if (outcome != HookOutcome::Installed) {
            const auto* reason = outcome == HookOutcome::Changed ? "OwnershipChanged" :
                outcome == HookOutcome::EnableFailed ? "EnableFailed" :
                outcome == HookOutcome::RetainedInert ? "RollbackIncomplete" : "PreparationFailed";
            return Fail("hook", "HOOK_INSTALL_FAILED", reason);
        }
        log_.Emit(LogLevel::Info, "hook", "HOOK_INSTALL_COMPLETED", "resource=Group_A targets=2 result=ACTIVE");
        nextAudit_ = GetTickCount64() + 1000;
        ready_.store(true, std::memory_order_release);
        activeRuntime.store(this, std::memory_order_release);
        log_.Emit(LogLevel::Info, "startup", "INITIALIZATION_COMPLETED", "mode=AUTOMATIC result=ACTIVE");
        return true;
    }

    void Run() noexcept {
        while (ready_.load(std::memory_order_acquire)) {
            Sleep(16);
            const auto now = GetTickCount64();
            if (now >= nextAudit_) {
                if (!hooks_.StillOwned() || !sites_.DependenciesMatch()) {
                    ready_.store(false, std::memory_order_release);
                    Fail("hook", "HOOK_VALIDATION_FAILED", "OwnershipChanged");
                    break;
                }
                nextAudit_ = now + 1000;
            }
            PollBoostConfig(now);
            JumpReport report;
            {
                std::unique_lock lock(mutex_, std::try_to_lock);
                if (lock) {
                    const auto current = Evaluate();
                    if (current.allowed && current.hero == 1) UpdatePalmContext(current, now);
                    if (current.allowed && palm::Route(current.hero) == palm::Mode::DoubleJump)
                        ProcessJumpFrames(current.hero);
                    report = CollectJumpReport(GetTickCount64());
                }
            }
            ReportJump(report);
            ReportMovementPath();
            ReportBoosts();
        }
        ReportBoosts();
        log_.Emit(LogLevel::Info, "runtime", "FEATURE_DEACTIVATED", "resource=Group_A result=INERT");
        log_.Emit(LogLevel::Info, "shutdown", "SESSION_ENDED", "result=INERT");
        log_.Close();
    }

    void RecordFailure() noexcept {
        ready_.store(false, std::memory_order_release);
        Fail("startup", "INITIALIZATION_FAILED", "RuntimeException");
        log_.Close();
    }

    void EndRefusedStartup() noexcept {
        log_.Emit(LogLevel::Info, "shutdown", "SESSION_ENDED", "result=DISABLED");
        log_.Close();
    }

private:
    static void MotionCallback(safetyhook::Context& context) noexcept {
        auto* const runtime = activeRuntime.load(std::memory_order_acquire);
        if (runtime && runtime->ready_.load(std::memory_order_acquire)) runtime->OnMotion(context);
    }

    static void PublisherCallback(safetyhook::Context& context) noexcept {
        auto* const runtime = activeRuntime.load(std::memory_order_acquire);
        if (runtime && runtime->ready_.load(std::memory_order_acquire)) runtime->OnPublisher(context);
    }

    bool Fail(std::string_view component, std::string_view event, const char* reason) noexcept {
        char fields[240]{};
        std::snprintf(fields, sizeof(fields),
            "stage=%.*s resource=Group_A reason=%s code=none action=STOP_GROUP result=DISABLED",
            int(component.size()), component.data(), reason);
        log_.Emit(LogLevel::Error, component, event, fields);
        return false;
    }

    static const char* ResolveReason(ResolveError error) noexcept {
        switch (error) {
        case ResolveError::InvalidImage: return "ImageUnavailable";
        case ResolveError::MotionAmbiguous: case ResolveError::PalmAmbiguous: return "Ambiguous";
        case ResolveError::MotionMissing: case ResolveError::PalmMissing: return "NoValidatedTarget";
        case ResolveError::None: break;
        }
        return "Unknown";
    }

    bool AcquireLeases() {
        constexpr std::array<const wchar_t*, 2> names{
            L"Local\\CrimsonDesert.SuperJump.NormalVariants.", L"Local\\CrimsonDesert.SuperJump.PalmCollector."};
        for (std::size_t i = 0; i < leases_.size(); ++i) {
            const auto name = names[i] + std::to_wstring(GetCurrentProcessId());
            const auto lease = CreateMutexW(nullptr, FALSE, name.c_str());
            if (!lease) return false;
            if (GetLastError() == ERROR_ALREADY_EXISTS) {
                CloseHandle(lease);
                return false;
            }
            leases_[i].value = lease;
        }
        return true;
    }

    static uintptr_t FreedomFlyerModule() {
        const auto asi = GetModuleHandleW(L"FreedomFlyer.asi");
        const auto dll = GetModuleHandleW(L"FreedomFlyer.dll");
        // Two independently loaded providers cannot share one proof.
        if (asi && dll && asi != dll) return 1;
        return reinterpret_cast<uintptr_t>(asi ? asi : dll);
    }

    bool AwaitLayout() {
        auto layout = ValidateGameLayout(reader, profile_, provider_, sites_.sites.palm, FreedomFlyerModule());
        if (layout != LayoutResult::MovementMismatch || (!provider_ && !FreedomFlyerModule()))
            return layout == LayoutResult::Valid;
        log_.Emit(LogLevel::Info, "scan", "VALIDATION_RETRY",
                  "stage=startup resource=Group_A reason=ProviderNotReady action=RETRY result=PENDING");
        const auto deadline = GetTickCount64() + 15000;
        do {
            Sleep(25);
            layout = ValidateGameLayout(reader, profile_, provider_, sites_.sites.palm, FreedomFlyerModule());
        } while (layout == LayoutResult::MovementMismatch && GetTickCount64() < deadline);
        return layout == LayoutResult::Valid;
    }

    std::uint32_t Hero(const traversal::Snapshot& actor) const {
        const auto read = [&] {
            std::uint16_t index{};
            std::uint32_t key{};
            boss::InfoEntry info{};
            return reader.Get(actor.status + 0x30, index) && boss::ReadInfoEntry(reader, profile_, index, info) &&
                reader.Get(info.info + 8, key) ? key : 0U;
        };
        const auto first = read();
        return palm::Route(first) != palm::Mode::None && first == read() ? first : 0;
    }

    // Only the runtime mutex protects game samples and activation state. The
    // callbacks use try_lock; game code is never called while it is held.
    Decision Evaluate() {
        const auto now = GetTickCount64();
        if (invalidate_.exchange(false, std::memory_order_acq_rel)) Reset(now);
        if (!GameHasFocus()) {
            eligibility_ = Eligibility::Unfocused;
            LoseContext(now);
            return {};
        }
        const auto path = ResolveMovementPath(reader, profile_, provider_.Movement(), true, FreedomFlyerModule());
        movementPath_.store(path, std::memory_order_relaxed);
        if (path == MovementPath::Unavailable) {
            eligibility_ = Eligibility::MovementBlocked;
            LoseContext(now);
            return {};
        }
        const auto encounter = boss::Sample(reader, profile_);
        const auto ui = context::SampleUi(reader, profile_);
        if (context::BlocksBoost(ui, encounter, true, false)) {
            eligibility_ = Eligibility::MenuOrEncounter;
            LoseContext(now);
            return {};
        }
        const auto actor = traversal::Sample(reader, profile_, true);
        const bool coherent = actor.valid && actor.root == encounter.world_root &&
            actor.context == encounter.context && actor.actor == encounter.player && actor.uid == encounter.player_uid;
        const auto hero = coherent ? Hero(actor) : 0;
        if (!hero) {
            eligibility_ = Eligibility::ActorUnknown;
            LoseContext(now);
            return {};
        }
        if (!traversal::SameIdentity(epoch_, actor) || hero != hero_) Reset(now);
        const bool allowed = traversal::Allows(epoch_, actor, true);
        hero_ = hero;
        eligibility_ = allowed ? Eligibility::Gameplay : Eligibility::TraversalBlocked;
        if (!allowed) ResetActivation(now);
        const auto doubleTarget = allowed && palm::Route(hero) == palm::Mode::DoubleJump ? actor.frame_body : 0;
        if (jumpFrames_.Target() != doubleTarget) jumpFrames_.SetTarget(doubleTarget, now);
        jumpMovement_.store(doubleTarget ? actor.movement : 0, std::memory_order_release);
        body_.store(allowed ? actor.frame_body : 0, std::memory_order_release);
        return {actor, hero, allowed};
    }

    void ResetActivation(std::uint64_t now) {
        palm_.Reset(now);
        doubles_.ResetContext();
        jumpMovement_.store(0, std::memory_order_release);
        if (jumpFrames_.Target()) jumpFrames_.SetTarget(0, now);
        if (pending_.hero && pending_.hero != 1 && pending_.firstDeadline) ++jumpStats_.cancelled;
        ClearPending();
    }

    void ClearPending() {
        pending_ = {};
        pendingHero_.store(0, std::memory_order_release);
    }

    void QueueBoost(std::uint32_t hero, std::uintptr_t metadata, std::uint64_t now) {
        pending_ = {epoch_, metadata, now + startDeadlineMs, now, hero, boostFrames, config_.Value()};
        pendingHero_.store(hero, std::memory_order_release);
    }

    void Reset(std::uint64_t now) {
        ResetActivation(now);
        epoch_ = {};
        hero_ = 0;
        ++generation_;
    }

    void LoseContext(std::uint64_t now) {
        if (hero_ || epoch_.actor) Reset(now);
        body_.store(0, std::memory_order_release);
    }

    palm::Identity PalmIdentity(const motiontrace::Snapshot& snapshot, const traversal::Snapshot& actor) const {
        return {snapshot.root, snapshot.context, snapshot.actor, snapshot.controller, snapshot.movement_state,
            actor.component, actor.frame_body, snapshot.uid, snapshot.hero_key, generation_};
    }

    void UpdatePalmContext(const Decision& current, std::uint64_t now) {
        const auto sample = motiontrace::ReadOnce(reader, profile_, 0);
        const auto next = motiontrace::ReadOnce(reader, profile_, 0);
        if (!SamePalmOwner(next, current.actor) || !sample.valid ||
            (motiontrace::ChangedFields(sample, next) & ~64U) || next.hero_key != 1) {
            palm_.Reset(now);
            ClearPending();
            return;
        }
        palm_.ObserveContext({PalmIdentity(next, current.actor), now, true, true, next.grounded, CurrentAction(next)});
        if (palm_.CurrentPhase() == palm::Phase::Idle) ClearPending();
    }

    void ObserveDoubleJumpCommit(const Decision& current, const motiontrace::Snapshot& sample,
                                 std::uint64_t now) {
        if (!SamePalmOwner(sample, current.actor) || sample.hero_key != current.hero) return;

        // The native state belongs to the current actor. The commit has to launch upwards.
        if (sample.grounded) ++jumpStats_.nativeGround;
        else ++jumpStats_.nativeAir;
        const auto isDoubleJump = [hero = current.hero](const motiontrace::Slot& slot) {
            return slot.present && slot.effective && !slot.suppressed &&
                   IsNativeDoubleJumpSkill(hero, slot.skill_key);
        };
        const auto velocity = std::bit_cast<float>(sample.jump_velocity_bits);
        if (pending_.remaining || !std::isfinite(velocity) || velocity <= 0.0f) return;
        if (!sample.grounded) {
            const bool skillMatches = isDoubleJump(sample.upper) || isDoubleJump(sample.lower);
            const bool consumerVerified = sites_.sites.HasJumpCommit();
            if (!QualifiesNativeAirLaunch(current.hero, skillMatches, consumerVerified)) return;
            ++jumpStats_.nativeDoubleJump;
            if (current.hero == 4 && !skillMatches) ++jumpStats_.verifiedDamianeLaunch;
        }

        const auto commit = sample.grounded ? NativeJumpCommit::GroundLaunch :
                                              NativeJumpCommit::AirborneDoubleJump;
        if (doubles_.ObserveCommit(now, commit) == JumpObservation::DoubleJump) {
            QueueBoost(current.hero, 0, now);
            ++jumpStats_.second;
            ++jumpStats_.queued;
            ++jumpStats_.nativeQueued;
        }
    }

    void OnPublisher(const safetyhook::Context& ctx) noexcept {
        const auto jumpMovement = jumpMovement_.load(std::memory_order_acquire);
        if (jumpMovement && ctx.rcx != jumpMovement) return;
        NativeContext preserve;
        std::uintptr_t caller{};
        if (!ReadGameMemory(nullptr, ctx.rsp, &caller, sizeof(caller))) return;
        const auto publication = ClassifyPalmPublication(sites_.sites, base_, caller, ctx.rcx,
                                                         static_cast<std::uint32_t>(ctx.r9));
        if (!publication || !publication->accepted) return;
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock) {
            if (!jumpMovement) invalidate_.store(true, std::memory_order_release);
            return;
        }
        const auto current = Evaluate();
        if (!current.allowed) return;
        const auto now = GetTickCount64();
        const auto sample = motiontrace::Sample(reader, profile_, publication->nativeState);
        if (palm::Route(current.hero) == palm::Mode::DoubleJump) {
            ObserveDoubleJumpCommit(current, sample, now);
            return;
        }
        if (current.hero != 1) return;
        const auto action = palm::binding::Classify(reader, sample);
        if (!SamePalmOwner(sample, current.actor) || sample.hero_key != 1 || !action.valid) {
            ResetActivation(now);
            return;
        }
        const auto identity = PalmIdentity(sample, current.actor);
        if (palm_.OnNative({identity, now, ++sequence_, true, true, action.action, sample.grounded}, now) &&
            !sample.grounded && sample.upper.metadata) {
            QueueBoost(1, sample.upper.metadata, now);
        }
    }

    bool PalmWriteAllowed(const Decision& current) {
        const auto first = motiontrace::ReadOnce(reader, profile_, 0);
        const auto last = motiontrace::ReadOnce(reader, profile_, 0);
        if (!first.valid || !SamePalmOwner(last, current.actor) ||
            (motiontrace::ChangedFields(first, last) & ~64U) || last.grounded || last.hero_key != 1 ||
            last.upper.metadata != pending_.metadata) return false;
        const auto action = palm::binding::Classify(reader, last);
        return action.valid && action.action == palm::Action::Palm3;
    }

    void OnMotion(safetyhook::Context& ctx) noexcept {
        const auto body = body_.load(std::memory_order_acquire);
        if (!body || ctx.rbx != body || ctx.r13 != body + 0x90) return;
        NativeContext preserve;
        const auto now = GetTickCount64();
        if (jumpFrames_.Target() == body) {
            if (!GameHasFocus()) {
                invalidate_.store(true, std::memory_order_release);
                return;
            }
            // Sample the game's original motion even if the worker is checking
            // layouts or reading actor state. Never feed the amplified frame back.
            if (!pendingHero_.load(std::memory_order_acquire)) {
                jumpFrames_.Capture(body, {now, ctx.xmm6.f32[1]});
                return;
            }
        }
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock) {
            if (pendingHero_.load(std::memory_order_acquire) == 1)
                invalidate_.store(true, std::memory_order_release);
            return;
        }
        if (invalidate_.exchange(false, std::memory_order_acq_rel) || !GameHasFocus()) {
            LoseContext(now);
            return;
        }
        if (!pending_.remaining) return;
        if (pending_.hero != 1 && now <= pending_.triggerTick) return;
        const auto accepted = Evaluate();
        if (!pending_.remaining || !accepted.allowed || accepted.actor.frame_body != body ||
            accepted.hero != pending_.hero || !traversal::SameIdentity(pending_.owner, accepted.actor) ||
            (pending_.firstDeadline && now > pending_.firstDeadline) ||
            (pending_.hero == 1 && !PalmWriteAllowed(accepted)) || !hooks_.StillOwned() ||
            !sites_.DependenciesMatch() || invalidate_.load(std::memory_order_acquire)) {
            ResetActivation(now);
            return;
        }
        if (!ready_.load(std::memory_order_acquire) || !GameHasFocus()) return;
        if (pending_.remaining == boostFrames) {
            appliedBoostY_.store(pending_.boostY, std::memory_order_relaxed);
            (pending_.hero == 1 ? palmBoosts_ : doubleBoosts_).fetch_add(1, std::memory_order_release);
        }
        ctx.xmm6.f32[1] = pending_.boostY;
        pending_.firstDeadline = 0;
        if (!--pending_.remaining) ClearPending();
    }

    void ProcessJumpFrames(std::uint32_t hero) {
        const auto batch = jumpFrames_.Drain();
        const auto now = GetTickCount64();
        jumpStats_.frames += static_cast<unsigned>(batch.count);
        if (batch.missed) {
            jumpStats_.missed += batch.missed;
            doubles_.ResetMotion();
            return;
        }
        if (pending_.remaining) {
            if (pending_.firstDeadline && now > pending_.firstDeadline) ResetActivation(now);
            return;
        }
        for (const auto frame : batch.Samples()) {
            if (frame.tick > now || now - frame.tick > startDeadlineMs) {
                doubles_.ResetMotion();
                ++jumpStats_.missed;
                continue;
            }
            if (std::isfinite(frame.deltaY) && frame.deltaY > jumpStats_.maxRise)
                jumpStats_.maxRise = frame.deltaY;
            switch (doubles_.Observe(frame.tick, frame.deltaY, true)) {
            case JumpObservation::None: break;
            case JumpObservation::Neutral: ++jumpStats_.neutral; break;
            case JumpObservation::FirstImpulse: ++jumpStats_.first; break;
            case JumpObservation::TooSoon: ++jumpStats_.early; break;
            case JumpObservation::Cooldown: ++jumpStats_.cooldown; break;
            case JumpObservation::DoubleJump:
                ++jumpStats_.second;
                QueueBoost(hero, 0, frame.tick);
                ++jumpStats_.queued;
                return;
            }
        }
    }

    JumpReport CollectJumpReport(std::uint64_t now) {
        if (now < nextJumpReport_) return {};
        const auto stateChanged = eligibility_ != lastReportedEligibility_ || hero_ != lastReportedHero_;
        const auto& s = jumpStats_;
        const bool meaningful = s.first || s.second || s.early || s.cooldown || s.missed ||
                                s.queued || s.cancelled || s.nativeGround || s.nativeAir;
        if (!stateChanged && !meaningful && (!s.frames || framesReported_)) return {};
        if (stateChanged && !s.frames) framesReported_ = false;
        if (s.frames) framesReported_ = true;
        const JumpReport report{s, eligibility_, hero_, true};
        lastReportedEligibility_ = eligibility_;
        lastReportedHero_ = hero_;
        nextJumpReport_ = now + 2500;
        jumpStats_ = {};
        return report;
    }

    void ReportJump(const JumpReport& report) noexcept {
        if (!report.available) return;
        constexpr std::array state{"WAITING", "UNFOCUSED", "MOVEMENT_BLOCKED", "MENU_OR_ENCOUNTER",
            "ACTOR_UNKNOWN", "TRAVERSAL_BLOCKED", "GAMEPLAY"};
        const auto* hero = report.hero == 1 ? "Kliff" : report.hero == 4 ? "Damiane" :
                           report.hero == 6 ? "Oongka" : "Unknown";
        const auto& s = report.counts;
        char fields[384]{};
        std::snprintf(fields, sizeof(fields),
            "hero=%s context=%s frames=%u neutral=%u first=%u recognized=%u early=%u cooldown=%u missed=%u queued=%u cancelled=%u max_rise=%.4f native_ground=%u native_air=%u native_double_jump=%u native_queued=%u verified_damiane_launch=%u",
            hero, state[static_cast<std::size_t>(report.eligibility)], s.frames, s.neutral, s.first,
            s.second, s.early, s.cooldown, s.missed, s.queued, s.cancelled, s.maxRise,
            s.nativeGround, s.nativeAir, s.nativeDoubleJump, s.nativeQueued, s.verifiedDamianeLaunch);
        log_.Emit(LogLevel::Info, "runtime", "DOUBLE_JUMP_STATUS", fields);
    }

    void PollBoostConfig(std::uint64_t now) noexcept {
        const auto status = config_.Poll(now);
        if (!status) return;
        const char* result = *status == BoostConfigStatus::Loaded ? "LOADED" :
            *status == BoostConfigStatus::Invalid ? "INVALID_RETAINED" : "UNAVAILABLE_RETAINED";
        char fields[96]{};
        std::snprintf(fields, sizeof(fields), "result=%s boost_y=%.3f", result, config_.Value());
        log_.Emit(LogLevel::Info, "config", "BOOST_CONFIG", fields);
    }

    void ReportBoosts() noexcept {
        const auto palms = palmBoosts_.exchange(0, std::memory_order_acquire);
        const auto doubles = doubleBoosts_.exchange(0, std::memory_order_acquire);
        if (!palms && !doubles) return;
        char fields[112]{};
        std::snprintf(fields, sizeof(fields), "palm=%u double_jump=%u result=APPLIED last_boost_y=%.3f",
                      palms, doubles, appliedBoostY_.load(std::memory_order_relaxed));
        log_.Emit(LogLevel::Info, "runtime", "BOOST_APPLIED", fields);
    }

    void ReportMovementPath() noexcept {
        const auto path = movementPath_.load(std::memory_order_relaxed);
        if (path == reportedMovementPath_) return;
        reportedMovementPath_ = path;
        if (path == MovementPath::NativeContinuation)
            log_.Emit(LogLevel::Info, "compat", "MOVEMENT_PATH", "mode=NATIVE_CONTINUATION result=AVAILABLE");
    }

    HMODULE game_{};
    const std::uintptr_t base_;
    std::array<Handle, 2> leases_;
    SessionLog log_;
    BoostConfig config_;
    LoadedSites sites_;
    HookGroup hooks_;
    boss::Profile profile_;
    palm::menu_mode::Provider provider_;
    std::atomic<bool> ready_{}, invalidate_{};
    std::atomic<std::uint32_t> pendingHero_{};
    std::mutex mutex_;
    traversal::Epoch epoch_;
    std::atomic<std::uintptr_t> body_{}, jumpMovement_{};
    std::uint32_t hero_{};
    std::int32_t generation_{};
    std::uint64_t sequence_{}, nextAudit_{};
    palm::Policy palm_;
    JumpFrameQueue jumpFrames_;
    NativeDoubleJump doubles_;
    Pending pending_;
    JumpStats jumpStats_;
    Eligibility eligibility_{}, lastReportedEligibility_{};
    std::uint32_t lastReportedHero_{};
    std::uint64_t nextJumpReport_{};
    bool framesReported_{};
    std::atomic<unsigned> palmBoosts_{}, doubleBoosts_{};
    std::atomic<float> appliedBoostY_{};
    std::atomic<MovementPath> movementPath_{};
    MovementPath reportedMovementPath_{};
};

DWORD WINAPI StartRuntime(void* context) noexcept {
    const auto module = static_cast<HMODULE>(context);
    Runtime* runtime = nullptr;
    try {
        const auto gamePath = ModulePath(nullptr);
        if (!host::IsGameName(gamePath.c_str())) return 0;
        const auto modulePath = ModulePath(module);
        HMODULE self{};
        if (modulePath.empty() ||
            !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                reinterpret_cast<LPCWSTR>(&StartRuntime), &self)) return 0;
        const auto game = GetModuleHandleW(nullptr);
        if (!game) return 0;
        // Owned callbacks and trampolines are process-lifetime. A lost patch
        // stays inert and its storage remains available until process exit.
        runtime = new (std::nothrow) Runtime(game);
        if (runtime && runtime->Start(modulePath, gamePath)) runtime->Run();
        else if (runtime) runtime->EndRefusedStartup();
    } catch (...) {
        if (runtime) runtime->RecordFailure();
    }
    return 0;
}

} // namespace
} // namespace superjump

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        const auto thread = CreateThread(nullptr, 0, superjump::StartRuntime, module, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
