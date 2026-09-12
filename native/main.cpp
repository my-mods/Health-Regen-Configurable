// Health Regeneration. MIT. RE-UE4SS 97b7e501c / UEPseudo eb40a05f; Framecore 2b.
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
struct State final: FUObjectDeleteListener {
    std::mutex identityMutex;
    std::atomic_bool active{};
    std::atomic<uint64_t> deletionInterest{};
    Identity blood{},unlock{},heal{},functionIdentity{};
    std::array<Getter,3> getters{};
    UFunction* function{};
    std::array<CallbackId,2> hooks{InvalidCallbackId,InvalidCallbackId};
    bool listening{},debug{};
    float rate{};
    uint64_t calls{},zeros{},errors{},nanos{};
    static uint64_t interest(uintptr_t address){return uint64_t{1}<<((address^(address>>11))&63);}
    void NotifyUObjectDeleted(const UObjectBase* object,int32 index) override {
        const auto address=reinterpret_cast<uintptr_t>(object);
        if(!(deletionInterest.load(std::memory_order_relaxed)&interest(address)))return;
        std::lock_guard lock(identityMutex);
        const auto match=[&](const Identity& id){return id.index==index&&id.address==address;};
        if(match(blood)||match(unlock)||match(heal)||match(functionIdentity)) active=false;
        for(const auto& g:getters) if(match(g.identity)) active=false;
    }
    void OnUObjectArrayShutdown() override { active=false;function=nullptr;hooks.fill(InvalidCallbackId);listening=false; }
    void stop() {
        active=false;
        deletionInterest=0;
        if(function && resolve(functionIdentity)==function) for(auto id:hooks) if(id!=InvalidCallbackId) function->UnregisterHook(id);
        hooks.fill(InvalidCallbackId);function=nullptr;functionIdentity={};
        if(listening) { FUObjectArray::RemoveUObjectDeleteListener(this);listening=false; }
        std::lock_guard lock(identityMutex);
        blood={};unlock={};heal={};getters={};
    }
    void calculate(UnrealScriptFunctionCallableContext& context,bool unlocking) noexcept {
        // Instance filtering occurs in the host. No Lua, discovery, metadata
        // traversal, settings I/O or scheduler is involved in a regen tick.
        if(!context.RESULT_DECL) return;
        context.SetReturnValue(0.0f); // The inherited MMC is never the fallback.
        if(!active.load(std::memory_order_acquire) || !IsInGameThreadRaw()) return;
        const auto started=debug ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        try {
            auto owner=resolve(blood);
            if(!owner || resolve(unlocking?unlock:heal)!=context.Context) { active=false;return; }
            const auto tick=HealthRegeneration::segmentTick(read(owner,getters[0]),read(owner,getters[1]),read(owner,getters[2]),rate);
            const float amount=unlocking ? tick.unlock : tick.heal;
            context.SetReturnValue(amount);
            if(debug) {++calls;if(amount==0)++zeros;nanos+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count();}
        } catch(...) {active=false;if(debug)++errors;}
    }
    void bind(UObject* owner,UObject* unlockCDO,UObject* healCDO,float requested,bool logging) {
        if(!IsInGameThread()) throw std::runtime_error("Health Regeneration setup requires the game thread");
        stop();
        blood=identify(owner);unlock=identify(unlockCDO);heal=identify(healCDO);
        if(!blood.address||!unlock.address||!heal.address||unlock.address==heal.address || requested<=0||requested>0.05f)
            throw std::runtime_error("Invalid segment regeneration context");
        auto bloodClass=UObjectGlobals::StaticFindObject<UClass*>(nullptr,nullptr,L"/Script/DogwoodStats.BloodBarComponent");
        if(!bloodClass || !owner->IsA(bloodClass))throw std::runtime_error("Unexpected blood component class");
        getters={getter(L"/Script/DogwoodStats.BloodBarComponent:GetBlood"),getter(L"/Script/DogwoodStats.BloodBarComponent:GetBloodBarLength"),getter(L"/Script/DogwoodStats.BloodBarComponent:GetBloodPermDamage")};
        function=UObjectGlobals::StaticFindObject<UFunction*>(nullptr,nullptr,L"/Script/GameplayAbilities.GameplayModMagnitudeCalculation:CalculateBaseMagnitude");
        if(!function || !function->HasAnyFunctionFlags(EFunctionFlags::FUNC_Native)) throw std::runtime_error("Native magnitude function unavailable");
        int returns=0;
        for(auto p:TFieldRange<FProperty>(function,EFieldIterationFlags::IncludeDeprecated)) if(p->HasAnyPropertyFlags(EPropertyFlags::CPF_ReturnParm)) {
            if(p->GetClass().GetFName().ToString()!=STR("FloatProperty")) throw std::runtime_error("Unexpected magnitude return type");
            ++returns;
        }
        if(returns!=1) throw std::runtime_error("Missing magnitude return value");
        functionIdentity=identify(function);
        if(!functionIdentity.address)throw std::runtime_error("Invalid magnitude function identity");
        debug=logging;rate=requested;calls=zeros=errors=nanos=0;
        uint64_t mask=interest(blood.address)|interest(unlock.address)|interest(heal.address)|interest(functionIdentity.address);
        for(const auto& g:getters)mask|=interest(g.identity.address);
        deletionInterest=mask;
        FUObjectArray::AddUObjectDeleteListener(this);listening=true;
        try {
            hooks[0]=function->RegisterPostHookForInstance([](UnrealScriptFunctionCallableContext& c,void* p){static_cast<State*>(p)->calculate(c,true);},this,unlockCDO);
            hooks[1]=function->RegisterPostHookForInstance([](UnrealScriptFunctionCallableContext& c,void* p){static_cast<State*>(p)->calculate(c,false);},this,healCDO);
            if(hooks[0]==InvalidCallbackId||hooks[1]==InvalidCallbackId) throw std::runtime_error("Segment magnitude registration failed");
            active=true;
        } catch(...) {stop();throw;}
    }
};
std::shared_ptr<State> current;
bool supportedRuntime() {
    wchar_t path[32768];auto module=GetModuleHandleW(L"UE4SS.dll");auto length=GetModuleFileNameW(module,path,32768);
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
    constexpr std::array<unsigned char,32> expected{0xfb,0x18,0x39,0xee,0x91,0xf7,0x1f,0x83,0xd5,0x08,0xd4,0x4a,0x27,0x63,0xa1,0x5a,0xc1,0xbb,0x0c,0x5f,0xb4,0xe5,0x04,0xac,0x0f,0xcf,0xca,0x64,0x37,0x6a,0x05,0x4a};
    return ok&&digest==expected;
}
class HealthRegenerationMod final:public CppUserModBase {
    std::shared_ptr<State> state=std::make_shared<State>();
public:
    HealthRegenerationMod(){ModName=STR("Health Regeneration");ModVersion=STR("0.0.0");ModAuthors=STR("oOCamilleOo");}
    void on_lua_start(StringViewType name,Lua& lua,Lua&,Lua&,Lua*)override {
        if(name!=STR("HealthRegeneration")||!supportedRuntime())return;
        current=state;
        lua.register_function("_HRNativeBind",[](const Lua& l){
            auto blood=reinterpret_cast<UObject*>(static_cast<uintptr_t>(l.get_integer()));
            auto unlock=reinterpret_cast<UObject*>(static_cast<uintptr_t>(l.get_integer()));
            auto heal=reinterpret_cast<UObject*>(static_cast<uintptr_t>(l.get_integer()));
            const float rate=static_cast<float>(l.get_number());const bool logging=l.get_bool();
            current->bind(blood,unlock,heal,rate,logging);l.set_bool(true);return 1;
        });
        lua.register_function("_HRNativeStop",[](const Lua& l){
            if(!IsInGameThread())throw std::runtime_error("Health Regeneration stop requires the game thread");
            auto s=current;s->stop();l.set_integer(s->calls);l.set_integer(s->zeros);l.set_integer(s->errors);l.set_number(s->nanos/1e6);return 4;
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
