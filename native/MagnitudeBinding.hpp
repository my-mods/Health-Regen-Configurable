// MIT. One-slot native binding, preserving the complete original table.
#pragma once
#include <Windows.h>
#include <intrin.h>
#include <atomic>
#include <mutex>
#include <vector>
#include <stdexcept>
#include <cstring>
#include "OperationRoute.hpp"
namespace MagnitudeBinding {
using Original=float(__fastcall*)(void*,const void*);
using Override=bool(*)(void*,void*,const void*,float&);
struct Binding {
    void** slot{};
    Original original{};
    void* thunk{};
    std::recursive_mutex invocation;
    void* owner{};
    void* first{};
    void* second{};
    Override callback{};
    // Cheap unrelated-object filtering; exact identities are checked by State.
    std::atomic<void*> firstFilter{},secondFilter{};
    static float __fastcall route(Binding* b,void* context,const void* spec) {
        if(context==b->firstFilter.load(std::memory_order_acquire) || context==b->secondFilter.load(std::memory_order_acquire)) {
            std::lock_guard lock(b->invocation);
            float result{};
            if(b->owner && (context==b->first || context==b->second) && b->callback(b->owner,context,spec,result))return result;
        }
        return b->original(context,spec);
    }
    bool exchange(void* expected,void* desired) noexcept {
        if(!NativeRoute::accessible(slot,sizeof(void*)))return false;
        MEMORY_BASIC_INFORMATION memory{};
        if(!VirtualQuery(slot,&memory,sizeof(memory)))return false;
        const auto oldAccess=memory.Protect&0xff;
        const bool executable=oldAccess==PAGE_EXECUTE || oldAccess==PAGE_EXECUTE_READ || oldAccess==PAGE_EXECUTE_READWRITE || oldAccess==PAGE_EXECUTE_WRITECOPY;
        DWORD protection{};
        if(!VirtualProtect(slot,sizeof(void*),executable?PAGE_EXECUTE_READWRITE:PAGE_READWRITE,&protection))return false;
        const auto previous=_InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(slot),desired,expected);
        DWORD ignored{};
        const bool restored=VirtualProtect(slot,sizeof(void*),protection,&ignored)!=0;
        // If restoring protection failed, preserve safe forwarding and attempt the
        // original protection once more. No UObject or code bytes are overwritten.
        if(!restored)VirtualProtect(slot,sizeof(void*),protection,&ignored);
        return previous==expected;
    }
    void release() noexcept {
        {
            std::lock_guard lock(invocation);
            owner=nullptr;first=second=nullptr;
            firstFilter.store(nullptr,std::memory_order_release);
            secondFilter.store(nullptr,std::memory_order_release);
        }
        exchange(thunk,reinterpret_cast<void*>(original));
    }
};
inline std::vector<Binding*>& retained(){static auto* list=new std::vector<Binding*>;return *list;}
inline Binding* install(void** slot,void* owner,void* first,void* second,Override callback) {
    if(!slot || (reinterpret_cast<uintptr_t>(slot)%alignof(void*)) || !NativeRoute::accessible(slot,sizeof(void*)))
        throw std::runtime_error("Segment calculation slot is unreadable or unaligned");
    void* target{};std::memcpy(&target,slot,sizeof(target));
    if(!NativeRoute::accessible(target,1,true))throw std::runtime_error("Segment original calculation cannot be forwarded: target is not executable");
    Binding* b{};
    for(auto prior:retained())if(prior->slot==slot && (target==reinterpret_cast<void*>(prior->original) || target==prior->thunk)){b=prior;break;}
    if(!b) {
        // Retain code and routing records for any call that fetched the slot just
        // before cleanup. The DLL is pinned for the same reason. One page per
        // distinct original slot/target; unchanged save-loads reuse it.
        HMODULE pinned{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&Binding::route),&pinned))throw std::runtime_error("Cannot retain segment dispatcher lifetime");
        auto fresh=new Binding;fresh->slot=slot;fresh->original=reinterpret_cast<Original>(target);
        auto code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        if(!code){delete fresh;throw std::runtime_error("Cannot allocate segment dispatcher");}
        // Windows x64: (RCX=context,RDX=spec) -> (RCX=b,RDX=context,R8=spec).
        // Tail jump preserves the caller's return address, shadow space and XMM0.
        const unsigned char prefix[]={0xf3,0x0f,0x1e,0xfa,0x49,0x89,0xd0,0x48,0x89,0xca,0x48,0xb9};
        std::memcpy(code,prefix,sizeof(prefix));
        std::memcpy(code+12,&fresh,8);code[20]=0x48;code[21]=0xb8;
        auto route=&Binding::route;std::memcpy(code+22,&route,8);code[30]=0xff;code[31]=0xe0;
        DWORD old{};
        if(!VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old) || !FlushInstructionCache(GetCurrentProcess(),code,32)) {
            VirtualFree(code,0,MEM_RELEASE);delete fresh;throw std::runtime_error("Cannot publish executable segment dispatcher");
        }
        fresh->thunk=code;b=fresh;retained().push_back(b);
    }
    {
        std::lock_guard lock(b->invocation);
        if(b->owner && b->owner!=owner)throw std::runtime_error("Segment calculation slot already has an active owner");
        b->owner=owner;b->first=first;b->second=second;b->callback=callback;
        b->firstFilter.store(first,std::memory_order_release);b->secondFilter.store(second,std::memory_order_release);
    }
    if(target!=b->thunk && !b->exchange(target,b->thunk)) {
        b->release();throw std::runtime_error("Segment calculation slot changed before binding");
    }
    return b;
}
}
