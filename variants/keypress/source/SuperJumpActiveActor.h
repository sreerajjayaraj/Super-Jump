// Included inside superjump::boss after the native registry reader.
inline constexpr uintptr_t kClientActorRootRva=0x72451C0;

struct ControlRoster {
    uintptr_t entries=0;
    uint32_t count=0,capacity=0;
};

inline bool ReadControlRoster(const Reader& r,uintptr_t context,ControlRoster& roster) {
    // 9BF7CC..9BF835 copies count pointer entries, and 9CF424 uses them
    // as actor addresses. Both witnesses are checked by ValidateProfile.
    return r.Get(context+0x148,roster) && roster.count<=roster.capacity &&
        roster.count<=8192 && (!roster.count || roster.entries>=0x10000);
}

inline bool ReadPlayableActor(const Reader& r,const Profile& p,uintptr_t actor) {
    uintptr_t table=0,parts=0,status=0;
    uint16_t index=0;
    uint32_t key=0;
    InfoEntry info{};
    return r.Get(actor,table) && KnownPrimary(p,table) &&
        r.Get(actor+0x68,parts) && r.Get(parts+0x20,status) &&
        r.Get(status+0x30,index) && ReadInfoEntry(r,p,index,info) &&
        r.Get(info.info+8,key) && (key==1 || key==4 || key==6);
}

inline bool HasLocalControl(const Reader& r,uintptr_t actor,uintptr_t token) {
    uintptr_t actorToken=0;
    uint8_t local=0;
    return r.Get(actor+0x94,local) && local &&
        r.Get(actor+0xA0,actorToken) && actorToken==token;
}

// The native scheduler's work list empties between frames. Retain its last
// canonical actor as a discovery hint shared by the worker and frame reader.
inline std::atomic<uintptr_t> g_activeActorHint{0};

inline uintptr_t ReadActiveActor(const Reader& r,const Profile& p,uintptr_t context) {
    uintptr_t active=0,defaultActor=0;
    if (p.actor_root_slot!=p.base+kClientActorRootRva) {
        // Legacy synthetic/profile layouts without the Game 3663 roster.
        return r.Get(context+0x50,defaultActor)?defaultActor:0;
    }
    uintptr_t token=0,registry=0;
    Registry map{};
    if (!r.Get(context+0x58,token) || !token || !r.Get(context+8,registry) ||
        !LoadRegistry(r,registry,map) || !r.Get(context+0x50,defaultActor)) return 0;

    uint32_t activeUid=0;
    const auto consider=[&](uintptr_t actor) {
        if (!actor || actor==active || !HasLocalControl(r,actor,token)) return true;
        uintptr_t registered=0;
        uint32_t uid=0;
        if (!r.Get(actor+0x60,uid) || FindActor(r,map,uid,registered)!=Lookup::Found ||
            registered!=actor || !ReadPlayableActor(r,p,actor)) return true;
        if (active && active!=actor) return false;
        active=actor;
        activeUid=uid;
        return true;
    };
    if (!consider(defaultActor) || !consider(g_activeActorHint.load(std::memory_order_acquire))) return 0;
    ControlRoster roster{};
    if (ReadControlRoster(r,context,roster)) {
        for (uint32_t i=0;i<roster.count;++i) {
            uintptr_t actor=0;
            if (r.Get(roster.entries+8ULL*i,actor) && !consider(actor)) return 0;
        }
    }
    uintptr_t liveToken=0,liveRegistry=0,registered=0;
    Registry after{};
    if (!active || !r.Get(context+0x58,liveToken) || liveToken!=token ||
        !r.Get(context+8,liveRegistry) || liveRegistry!=registry ||
        !LoadRegistry(r,liveRegistry,after) || FindActor(r,after,activeUid,registered)!=Lookup::Found ||
        registered!=active || !HasLocalControl(r,active,token)) return 0;
    g_activeActorHint.store(active,std::memory_order_release);
    return active;
}

inline bool MatchesActiveActor(const Reader& r,const Profile& p,uintptr_t root,
                               uintptr_t context,uintptr_t actor) {
    uintptr_t current=0,liveContext=0;
    return actor && r.Get(p.actor_root_slot,current) && current==root &&
        r.Get(root+0x30,liveContext) && liveContext==context &&
        ReadActiveActor(r,p,context)==actor;
}
