// Health Regen - Configurable. MIT. RE-UE4SS 97b7e501c / UEPseudo eb40a05f; Framecore 2b.
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
#include <bcrypt.h>
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
#include "GameCode.hpp"

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
// Build 25232147: GameplayModMagnitudeCalculation has 92 virtual entries;
// CalculateBaseMagnitude_Implementation is slot 90. Only our two CDOs receive
// a private table; the game's shared table and stock calculations stay intact.
struct State final: FUObjectDeleteListener {
    std::mutex identityMutex;
    std::atomic_bool active{};
    std::atomic<uint64_t> deletionInterest{};
    Identity blood{},unlock{},heal{};
    std::array<Getter,3> getters{};
    std::array<void*,92> magnitudeTable{};
    void** originalTable{};
    inline static std::atomic<State*> dispatchState{};
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
        // Do not use stop(): object-table restoration is unsafe during array shutdown.
        if(listening) { FUObjectArray::RemoveUObjectDeleteListener(this);listening=false; }
        deletionInterest=0;
        std::lock_guard lock(identityMutex);
        blood={};unlock={};heal={};getters={};
    }
    static float __fastcall dispatch(UObject* context,const void*) noexcept {
        if(!IsInGameThreadRaw())return 0.0f;
        auto state=dispatchState.load(std::memory_order_acquire);
        return state ? state->calculate(context) : 0.0f;
    }
    static void** table(UObject* object){return *reinterpret_cast<void***>(object);}
    void restore(const Identity& id) noexcept {
        if(auto object=resolve(id))
            _InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(object),originalTable,magnitudeTable.data());
    }
    void stop() {
        active=false;
        // Keep the immutable private table alive until DLL shutdown, even if a
        // deleted CDO is still unwinding. Never dereference an invalid identity.
        restore(unlock);restore(heal);
        deletionInterest=0;
        if(listening) { FUObjectArray::RemoveUObjectDeleteListener(this);listening=false; }
        std::lock_guard lock(identityMutex);
        blood={};unlock={};heal={};getters={};
    }
    ~State(){stop();auto expected=this;dispatchState.compare_exchange_strong(expected,nullptr);}
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
        if(!blood.address||!unlock.address||!heal.address||unlock.address==heal.address||requested<=0||requested>0.05f)
            throw std::runtime_error("Invalid segment regeneration context");
        auto bloodClass=UObjectGlobals::StaticFindObject<UClass*>(nullptr,nullptr,L"/Script/DogwoodStats.BloodBarComponent");
        auto calculationClass=UObjectGlobals::StaticFindObject<UClass*>(nullptr,nullptr,L"/Script/GameplayAbilities.GameplayModMagnitudeCalculation");
        if(!bloodClass || !owner->IsA(bloodClass))throw std::runtime_error("Unexpected blood component class");
        if(!calculationClass||!unlockCDO->IsA(calculationClass)||!healCDO->IsA(calculationClass))
            throw std::runtime_error("Unexpected segment calculation class");
        const auto image=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        auto expectedTable=reinterpret_cast<void**>(image+0x76c77f0);
        if(table(unlockCDO)!=expectedTable||table(healCDO)!=expectedTable
            ||expectedTable[90]!=reinterpret_cast<void*>(image+0x1271ab0))
            throw std::runtime_error("Segment calculation layout mismatch; install the matching assets and helper");
        if(!originalTable){
            originalTable=expectedTable;
            std::memcpy(magnitudeTable.data(),originalTable,sizeof(magnitudeTable));
            magnitudeTable[90]=reinterpret_cast<void*>(&dispatch);
        }
        getters={getter(L"/Script/DogwoodStats.BloodBarComponent:GetBlood"),getter(L"/Script/DogwoodStats.BloodBarComponent:GetBloodBarLength"),getter(L"/Script/DogwoodStats.BloodBarComponent:GetBloodPermDamage")};
        debug=logging;rate=requested;calls=zeros=errors=nanos=0;
        uint64_t mask=interest(blood.address)|interest(unlock.address)|interest(heal.address);
        for(const auto& g:getters)mask|=interest(g.identity.address);
        deletionInterest=mask;
        FUObjectArray::AddUObjectDeleteListener(this);listening=true;
        dispatchState.store(this,std::memory_order_release);
        try {
            for(auto object:{unlockCDO,healCDO})
                if(_InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(object),magnitudeTable.data(),originalTable)!=originalTable)
                    throw std::runtime_error("Segment calculation binding was replaced");
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
        if(!std::isfinite(requested)||requested<=0||requested>0.05f)
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
bool moduleMatches(HMODULE module,const std::array<unsigned char,32>& expected) {
    wchar_t path[32768];auto length=GetModuleFileNameW(module,path,32768);
    if(!module||!length||length==32768)return false;
    std::ifstream file(std::filesystem::path(path),std::ios::binary);if(!file)return false;
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
    std::array<unsigned char,32> digest{};bool ok=false;
    if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0) {
        std::array<char,65536> data{};bool good=true;
        while(file.read(data.data(),data.size())||file.gcount())if(BCryptHashData(hash,reinterpret_cast<PUCHAR>(data.data()),static_cast<ULONG>(file.gcount()),0)<0){good=false;break;}
        ok=good&&file.eof()&&BCryptFinishHash(hash,digest.data(),digest.size(),0)>=0;BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm,0);

    return ok&&digest==expected;
}
bool supportedRuntime() {
    return moduleMatches(GetModuleHandleW(L"UE4SS.dll"),{0xfb,0x18,0x39,0xee,0x91,0xf7,0x1f,0x83,0xd5,0x08,0xd4,0x4a,0x27,0x63,0xa1,0x5a,0xc1,0xbb,0x0c,0x5f,0xb4,0xe5,0x04,0xac,0x0f,0xcf,0xca,0x64,0x37,0x6a,0x05,0x4a});
}
class HealthRegenerationMod final:public CppUserModBase {
    std::shared_ptr<State> state=std::make_shared<State>();
public:
    HealthRegenerationMod(){ModName=STR("Health Regen - Configurable");ModVersion=STR("1.0.0");ModAuthors=STR("oOCamilleOo");}
    void on_lua_start(StringViewType name,Lua& lua,Lua&,Lua&,Lua*)override {
        if(name!=STR("HealthRegeneration"))return;
        if(!supportedRuntime()) {
            Output::send(STR("[HealthRegeneration] Native helper unavailable: unsupported UE4SS library; requires Framecore 2b.\n"));
            return;
        }
        try {
            NativeCompatibility::validateContract(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),HealthRegeneration::Build::code,HealthRegeneration::Build::pointers);
        } catch(const std::exception& error) {
            const std::string message=error.what();
            Output::send(std::wstring(L"[HealthRegeneration] Native helper unavailable: ")+std::wstring(message.begin(),message.end())+L"\n");
            return;
        }
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
