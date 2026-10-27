// Health Regen - Configurable. MIT. RE-UE4SS 97b7e501c / UEPseudo eb40a05f.
// Pure magnitude calculations only: the engine applies the two native modifiers.
#include <Mod/CppUserModBase.hpp>
#include <LuaMadeSimple/LuaMadeSimple.hpp>
#include <DynamicOutput/Output.hpp>
#include <Unreal/UnrealInitializer.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/Core/Windows/AllowWindowsPlatformTypes.hpp>
#include <Windows.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <cstring>
#include "RegenerationMath.hpp"
#include "OperationRoute.hpp"
#include "MagnitudeBinding.hpp"

namespace {
using namespace RC;
using namespace RC::Unreal;
using Lua=LuaMadeSimple::Lua;
struct Identity { uintptr_t address{}; int32 index{-1},serial{}; };
Identity identify(UObject* object) {
    if(!object) return {};
    const int32 index=object->GetInternalIndex();
    auto item=FUObjectArray::IndexToObject(index);
    if(!item || item->GetUObject()!=object || !FUObjectArray::IsValid(item,false)) return {};
    return {reinterpret_cast<uintptr_t>(object),index,item->GetSerialNumber()};
}
UObject* resolve(const Identity& id) {
    if(!id.address) return nullptr;
    auto item=FUObjectArray::IndexToObject(id.index);
    if(!item || !FUObjectArray::IsValid(item,false)) return nullptr;
    auto object=item->GetUObject();
    return reinterpret_cast<uintptr_t>(object)==id.address && (!id.serial || item->GetSerialNumber()==id.serial) ? object : nullptr;
}
struct Getter { UFunction* function{}; Identity identity{}; int offset{},size{}; };
Getter getter(const wchar_t* path) {
    auto f=UObjectGlobals::StaticFindObject<UFunction*>(nullptr,nullptr,path);
    if(!f || !f->HasAnyFunctionFlags(EFunctionFlags::FUNC_Native)) throw std::runtime_error("Native blood getter unavailable");
    int count=0,offset=-1;
    for(auto p:TFieldRange<FProperty>(f,EFieldIterationFlags::IncludeDeprecated)) {
        if(!p->HasAnyPropertyFlags(EPropertyFlags::CPF_Parm)) continue;
        ++count;
        if(!p->HasAnyPropertyFlags(EPropertyFlags::CPF_ReturnParm) || p->GetClass().GetFName().ToString()!=STR("FloatProperty"))
            throw std::runtime_error("Unexpected blood getter signature");
        offset=p->GetOffset_Internal();
    }
    if(count!=1 || offset<0 || offset+4>f->GetParmsSize() || f->GetParmsSize()>16) throw std::runtime_error("Invalid blood getter layout");
    const auto identity=identify(f);
    if(!identity.address)throw std::runtime_error("Invalid blood getter identity");
    return {f,identity,offset,f->GetParmsSize()};
}
float read(UObject* owner,const Getter& g) {
    if(resolve(g.identity)!=g.function) throw std::runtime_error("Blood getter was replaced");
    alignas(16) std::array<unsigned char,16> params{};
    owner->ProcessEvent(g.function,params.data());
    float value;std::memcpy(&value,params.data()+g.offset,sizeof(value));return value;
}
// Override one verified operation; forward all unrelated native calculations.
struct State final: FUObjectDeleteListener {
    std::mutex identityMutex;
    std::atomic_bool active{};
    std::atomic<uint64_t> deletionInterest{};
    Identity blood{},unlock{},heal{};
    std::array<Getter,3> getters{};
    MagnitudeBinding::Binding* binding{};
    bool listening{},debug{},probing{};
    int probeHits{};
    float rate{};
    float lastBlood{},lastCapacity{},lastDamage{},lastUnlock{},lastHeal{};
    uint64_t calls{},zeros{},errors{},nanos{};
    static uint64_t interest(uintptr_t address){return uint64_t{1}<<((address^(address>>11))&63);}
    void NotifyUObjectDeleted(const UObjectBase* object,int32 index) override {
        const auto address=reinterpret_cast<uintptr_t>(object);
        if(!(deletionInterest.load(std::memory_order_relaxed)&interest(address)))return;
        std::lock_guard lock(identityMutex);
        const auto match=[&](const Identity& id){return id.index==index&&id.address==address;};
        if(match(blood)||match(unlock)||match(heal)) active=false;
        for(const auto& g:getters) if(match(g.identity)) active=false;
    }
    void OnUObjectArrayShutdown() override {
        active=false;
        // The engine requires listeners to remove themselves before this callback returns.
        // Releasing the static table slot never dereferences dying UObjects.
        if(binding){binding->release();binding=nullptr;}
        if(listening) { FUObjectArray::RemoveUObjectDeleteListener(this);listening=false; }
        deletionInterest=0;
        std::lock_guard lock(identityMutex);
        blood={};unlock={};heal={};getters={};
    }
    static bool dispatch(void* user,void* context,const void*,float& result) {
        if(!IsInGameThreadRaw())return false;
        auto self=static_cast<State*>(user);
        if(!self->active.load(std::memory_order_acquire))return false;
        if(resolve(self->unlock)!=context && resolve(self->heal)!=context)return false;
        result=self->calculate(static_cast<UObject*>(context));return true;
    }
    static void** table(UObject* object){return *reinterpret_cast<void***>(object);}
    void stop() {
        active=false;
        if(binding){binding->release();binding=nullptr;}
        deletionInterest=0;
        if(listening) { FUObjectArray::RemoveUObjectDeleteListener(this);listening=false; }
        std::lock_guard lock(identityMutex);
        blood={};unlock={};heal={};getters={};
    }
    ~State(){stop();}
    float calculate(UObject* context) noexcept {
        if(!active.load(std::memory_order_acquire))return 0.0f;
        const bool unlocking=reinterpret_cast<uintptr_t>(context)==unlock.address;
        if(!unlocking && reinterpret_cast<uintptr_t>(context)!=heal.address)return 0.0f;
        if(probing)++probeHits;
        const auto started=debug ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        try {
            auto owner=resolve(blood);
            if(!owner || resolve(unlocking?unlock:heal)!=context) { active=false;return 0.0f; }
            const float bloodValue=read(owner,getters[0]),capacity=read(owner,getters[1]),damage=read(owner,getters[2]);
            const auto tick=HealthRegeneration::segmentTick(bloodValue,capacity,damage,rate);
            const float amount=unlocking ? tick.unlock : tick.heal;
            if(debug) {
                lastBlood=bloodValue;lastCapacity=capacity;lastDamage=damage;lastUnlock=tick.unlock;lastHeal=tick.heal;
                ++calls;if(amount==0)++zeros;nanos+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count();
            }
            return amount;
        } catch(...) {active=false;if(debug)++errors;return 0.0f;}
    }
    void bind(UObject* owner,UObject* unlockCDO,UObject* healCDO,float requested,bool logging) {
        if(!IsInGameThread()) throw std::runtime_error("Health Regen - Configurable setup requires the game thread");
        stop();
        blood=identify(owner);unlock=identify(unlockCDO);heal=identify(healCDO);
        if(!blood.address||!unlock.address||!heal.address||unlock.address==heal.address||!std::isfinite(requested)||requested<=0||requested>0.05f)
            throw std::runtime_error("Invalid segment regeneration context");
        auto bloodClass=UObjectGlobals::StaticFindObject<UClass*>(nullptr,nullptr,L"/Script/DogwoodStats.BloodBarComponent");
        auto calculationClass=UObjectGlobals::StaticFindObject<UClass*>(nullptr,nullptr,L"/Script/GameplayAbilities.GameplayModMagnitudeCalculation");
        if(!bloodClass || !owner->IsA(bloodClass))throw std::runtime_error("Unexpected blood component class");
        if(!calculationClass||!unlockCDO->IsA(calculationClass)||!healCDO->IsA(calculationClass))
            throw std::runtime_error("Unexpected segment calculation class");
        // The reflected native base CDO establishes table ownership, rather than
        // a development-image RVA or identities of entries we merely preserve.
        auto base=UObjectGlobals::StaticFindObject<UObject*>(nullptr,nullptr,L"/Script/GameplayAbilities.Default__GameplayModMagnitudeCalculation");
        if(!identify(base).address || !base->IsA(calculationClass))throw std::runtime_error("Neutral calculation base default is unavailable");
        auto expectedTable=table(base);
        if(table(unlockCDO)!=expectedTable||table(healCDO)!=expectedTable)
            throw std::runtime_error("Owned segment calculations do not share the neutral native base table");
        auto fn=UObjectGlobals::StaticFindObject<UFunction*>(nullptr,nullptr,L"/Script/GameplayAbilities.GameplayModMagnitudeCalculation:CalculateBaseMagnitude");
        if(!identify(fn).address || !fn->HasAnyFunctionFlags(EFunctionFlags::FUNC_Native))throw std::runtime_error("Native CalculateBaseMagnitude wrapper is missing");
        unsigned resultCount=0;
        for(auto p:TFieldRange<FProperty>(fn,EFieldIterationFlags::IncludeDeprecated)) {
            if(!p->HasAnyPropertyFlags(EPropertyFlags::CPF_ReturnParm))continue;
            if(p->GetClass().GetFName().ToString()!=STR("FloatProperty") || p->GetElementSize()!=4 || p->GetArrayDim()!=1)
                throw std::runtime_error("CalculateBaseMagnitude no longer returns a scalar float");
            ++resultCount;
        }
        if(resultCount!=1)throw std::runtime_error("CalculateBaseMagnitude float result is missing");
        NativeRoute::validate(reinterpret_cast<uintptr_t>(fn->GetFuncPtr()),true);
        getters={getter(L"/Script/DogwoodStats.BloodBarComponent:GetBlood"),getter(L"/Script/DogwoodStats.BloodBarComponent:GetBloodBarLength"),getter(L"/Script/DogwoodStats.BloodBarComponent:GetBloodPermDamage")};
        debug=logging;rate=requested;calls=zeros=errors=nanos=0;
        uint64_t mask=interest(blood.address)|interest(unlock.address)|interest(heal.address);
        for(const auto& g:getters)mask|=interest(g.identity.address);
        deletionInterest=mask;
        FUObjectArray::AddUObjectDeleteListener(this);listening=true;
        try {
            binding=MagnitudeBinding::install(expectedTable+90,this,unlockCDO,healCDO,&dispatch);
            active=true;
            // Probe the actual virtual call route before enabling either effect
            // modifier. This takes only read-only getters and never applies health.
            using Magnitude=float(__fastcall*)(UObject*,const void*);
            probing=true;probeHits=0;
            const float u=reinterpret_cast<Magnitude>(table(unlockCDO)[90])(unlockCDO,nullptr);
            const float h=reinterpret_cast<Magnitude>(table(healCDO)[90])(healCDO,nullptr);
            probing=false;
            if(probeHits!=2||!active||!std::isfinite(u)||!std::isfinite(h)||u>0||h<0)
                throw std::runtime_error("Segment calculation dispatch verification failed");
            calls=zeros=errors=nanos=0; // Subsequent counters describe game execution.
        } catch(...) {probing=false;stop();throw;}
    }
    bool configure(uintptr_t expectedBlood,float requested,bool logging) {
        if(!IsInGameThread()) throw std::runtime_error("Segment settings require the game thread");
        if(!std::isfinite(requested)||!std::isfinite(requested)||requested<=0||requested>0.05f)
            throw std::runtime_error("Invalid segment regeneration rate");
        if(!active.load(std::memory_order_acquire)||blood.address!=expectedBlood||!resolve(blood)) return false;
        // The calculation route and configuration are game-thread-only. Never
        // rebind, discover objects, apply effects, or reactivate a paused owner.
        rate=requested;
        if(debug!=logging) { calls=zeros=errors=nanos=0;debug=logging; }
        return true;
    }
};
std::shared_ptr<State> current;
class HealthRegenerationMod final:public CppUserModBase {
    std::shared_ptr<State> state=std::make_shared<State>();
public:
    HealthRegenerationMod(){ModName=STR("Health Regen - Configurable");ModVersion=STR("1.2.1-dev");ModAuthors=STR("oOCamilleOo");}
    void on_lua_start(StringViewType name,Lua& lua,Lua&,Lua&,Lua*)override {
        if(name!=STR("HealthRegeneration"))return;
        // Only segment binding depends on the optional native operation.
        current=state;
        lua.register_function("_HRNativeBind",[](const Lua& l){
            auto blood=reinterpret_cast<UObject*>(static_cast<uintptr_t>(l.get_integer()));
            auto unlock=reinterpret_cast<UObject*>(static_cast<uintptr_t>(l.get_integer()));
            auto heal=reinterpret_cast<UObject*>(static_cast<uintptr_t>(l.get_integer()));
            const float rate=static_cast<float>(l.get_number());const bool logging=l.get_bool();
            current->bind(blood,unlock,heal,rate,logging);l.set_bool(true);return 1;
        });
        lua.register_function("_HRNativeConfigure",[](const Lua& l){
            const auto owner=static_cast<uintptr_t>(l.get_integer());
            const auto rate=static_cast<float>(l.get_number());const bool logging=l.get_bool();
            l.set_bool(current->configure(owner,rate,logging));return 1;
        });
        lua.register_function("_HRNativeStop",[](const Lua& l){
            if(!IsInGameThread())throw std::runtime_error("Health Regen - Configurable stop requires the game thread");
            auto s=current;s->stop();l.set_integer(s->calls);l.set_integer(s->zeros);l.set_integer(s->errors);l.set_number(s->nanos/1e6);return 4;
        });
        lua.register_function("_HRNativeStats",[](const Lua& l){
            if(!IsInGameThread())throw std::runtime_error("Health Regen - Configurable diagnostics require the game thread");
            auto s=current;l.set_integer(s->calls);l.set_integer(s->zeros);l.set_integer(s->errors);l.set_number(s->nanos/1e6);
            l.set_number(s->lastBlood);l.set_number(s->lastCapacity);l.set_number(s->lastDamage);
            l.set_number(s->lastUnlock);l.set_number(s->lastHeal);return 9;
        });
        lua.register_function("_HRNativePause",[](const Lua&){current->active=false;return 0;});
    }
    void on_lua_stop(StringViewType name,Lua&,Lua&,Lua&,Lua*)override {if(name==STR("HealthRegeneration"))state->stop();}
    ~HealthRegenerationMod()override {state->stop();if(current==state)current.reset();}
};
}
#define HR_EXPORT extern "C" __declspec(dllexport)
HR_EXPORT RC::CppUserModBase* start_mod(){return new HealthRegenerationMod();}
HR_EXPORT void uninstall_mod(RC::CppUserModBase* mod){delete mod;}
