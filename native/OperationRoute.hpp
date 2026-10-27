// MIT. Operation-specific Windows x64 dispatch validation; no binary identity gates.
#pragma once
#include <Windows.h>
#include <Zydis/Zydis.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <deque>
#include <stdexcept>
namespace NativeRoute {
inline bool accessible(const void* ptr,size_t size,bool code=false) {
    auto cursor=reinterpret_cast<uintptr_t>(ptr);
    if(!cursor || !size || size>std::numeric_limits<uintptr_t>::max()-cursor)return false;
    const auto end=cursor+size;
    while(cursor<end) {
        MEMORY_BASIC_INFORMATION m{};
        if(!VirtualQuery(reinterpret_cast<void*>(cursor),&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const auto p=m.Protect&0xff;
        const bool execute=p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
        if(!execute && (code || (p!=PAGE_READONLY&&p!=PAGE_READWRITE&&p!=PAGE_WRITECOPY)))return false;
        const auto base=reinterpret_cast<uintptr_t>(m.BaseAddress);
        if(m.RegionSize>std::numeric_limits<uintptr_t>::max()-base || base+m.RegionSize<=cursor)return false;
        cursor=base+m.RegionSize;
    }
    return true;
}
struct Instruction {ZydisDecodedInstruction ins{};std::array<ZydisDecodedOperand,ZYDIS_MAX_OPERAND_COUNT> op{};};
inline Instruction decode(uintptr_t address) {
    // Copy only individually verified bytes: a function need not occupy one region.
    std::array<uint8_t,ZYDIS_MAX_INSTRUCTION_LENGTH> bytes{};size_t n=0;
    for(;n<bytes.size() && address<=UINTPTR_MAX-n && accessible(reinterpret_cast<void*>(address+n),1,true);++n)
        std::memcpy(&bytes[n],reinterpret_cast<void*>(address+n),1);
    ZydisDecoder decoder{};ZydisDecoderInit(&decoder,ZYDIS_MACHINE_MODE_LONG_64,ZYDIS_STACK_WIDTH_64);
    Instruction d;
    if(!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder,bytes.data(),n,&d.ins,d.op.data())))throw std::runtime_error("native route instruction is unreadable or undecodable");
    return d;
}
inline int reg(ZydisRegister r) {
    r=ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64,r);
    if(r>=ZYDIS_REGISTER_RAX && r<=ZYDIS_REGISTER_R15)return int(r-ZYDIS_REGISTER_RAX);
    return -1;
}
inline bool nonvolatile(int r) {return r==reg(ZYDIS_REGISTER_RBX)||r==reg(ZYDIS_REGISTER_RBP)||r==reg(ZYDIS_REGISTER_RSI)||r==reg(ZYDIS_REGISTER_RDI)||r>=reg(ZYDIS_REGISTER_R12);}
inline uintptr_t relative(uintptr_t at,const Instruction& d,unsigned operand=0) {
    ZyanU64 address{};
    if(!ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&d.ins,&d.op[operand],at,&address)))throw std::runtime_error("native route target cannot be resolved");
    return uintptr_t(address);
}
enum class Kind {Unknown,Owner,Result,Frame,Table,StackAddress};
struct Value {Kind kind{};int64_t detail{};bool operator==(const Value&)const=default;};
struct Flow {
    std::array<Value,16> r{};int64_t stack{};uintptr_t call{},stored{};bool floatResult{};
    bool operator==(const Flow&)const=default;
};
struct Route {uintptr_t implementation{};size_t slot{};};
inline Route validate(uintptr_t entry,bool magnitude) {
    if(!magnitude)throw std::runtime_error("this validator supports only magnitude dispatch");
    // This is a bounded abstract interpreter of the reflected wrapper's reachable
    // control flow, not a function signature. Only full-width owner, vptr,
    // result destination and calling convention are dependencies. Cleanup and
    // unrelated helper instructions are decoded but never compared to a baseline.
    Flow initial;initial.r[reg(ZYDIS_REGISTER_RCX)]={Kind::Owner};
    initial.r[reg(ZYDIS_REGISTER_RDX)]={Kind::Frame};initial.r[reg(ZYDIS_REGISTER_R8)]={Kind::Result};
    std::map<uintptr_t,Flow> incoming;std::deque<uintptr_t> work;incoming[entry]=initial;work.push_back(entry);
    uintptr_t selected{};size_t returns=0,iterations=0;
    auto enqueue=[&](uintptr_t address,const Flow& state) {
        if(address<entry || address-entry>=4096)throw std::runtime_error("native wrapper route leaves the verified analysis window");
        auto [it,added]=incoming.emplace(address,state);
        if(added){work.push_back(address);return;}
        Flow merged=it->second;
        if(merged.stack!=state.stack)throw std::runtime_error("native wrapper stack layout is ambiguous");
        for(size_t i=0;i<merged.r.size();++i)if(merged.r[i]!=state.r[i])merged.r[i]={};
        if(merged.call!=state.call)merged.call=0;
        if(merged.stored!=state.stored)merged.stored=0;
        merged.floatResult=merged.floatResult&&state.floatResult;
        if(merged!=it->second){it->second=merged;work.push_back(address);}
    };
    while(!work.empty()) {
        if(++iterations>8192)throw std::runtime_error("native wrapper route analysis did not converge");
        const auto at=work.front();work.pop_front();auto state=incoming.at(at);const auto d=decode(at);const auto& ins=d.ins;const auto& a=d.op[0];const auto& b=d.op[1];
        const auto next=at+ins.length;
        auto value=[&](const ZydisDecodedOperand& o)->Value {
            if(o.type==ZYDIS_OPERAND_TYPE_REGISTER){const auto i=reg(o.reg.value);return i>=0 && o.size==64 ? state.r[i]:Value{};}
            if(o.type==ZYDIS_OPERAND_TYPE_MEMORY && ins.address_width==64 && o.mem.index==ZYDIS_REGISTER_NONE) {
                const auto base=reg(o.mem.base);
                if(base>=0 && state.r[base].kind==Kind::Owner && o.mem.disp.value==0 && o.size==64)return {Kind::Table};
            }
            return {};
        };
        if(ins.mnemonic==ZYDIS_MNEMONIC_RET) {
            if(ins.operand_count_visible!=0)throw std::runtime_error("native wrapper uses an incompatible callee stack cleanup convention");
            if(!state.stored || (selected && state.stored!=selected))throw std::runtime_error("native wrapper does not deliver the required result on every return route");
            selected=state.stored;++returns;continue;
        }
        if(ins.mnemonic==ZYDIS_MNEMONIC_CALL) {
            uintptr_t candidate{};
            if(state.r[reg(ZYDIS_REGISTER_RCX)].kind==Kind::Owner) {
                if(magnitude && ins.address_width==64 && a.type==ZYDIS_OPERAND_TYPE_MEMORY && a.mem.index==ZYDIS_REGISTER_NONE && a.size==64 && a.mem.disp.value==90*8) {
                    const auto i=reg(a.mem.base);if(i>=0 && state.r[i].kind==Kind::Table)candidate=at;
                }
            }
            for(int i=0;i<16;++i)if(!nonvolatile(i))state.r[i]={};
            state.floatResult=false;
            if(candidate) {
                if(state.call && state.call!=candidate)throw std::runtime_error("native wrapper has multiple operation targets");
                state.call=candidate;
                state.floatResult=true;
            }
        } else {
            Value assigned{};bool assign=false;
            if((ins.mnemonic==ZYDIS_MNEMONIC_MOV||ins.mnemonic==ZYDIS_MNEMONIC_MOVZX) && a.type==ZYDIS_OPERAND_TYPE_REGISTER){assigned=value(b);assign=true;if(a.size!=64 || b.size!=64)assigned={};}
            if(ins.mnemonic==ZYDIS_MNEMONIC_LEA && a.type==ZYDIS_OPERAND_TYPE_REGISTER && b.type==ZYDIS_OPERAND_TYPE_MEMORY && b.mem.base==ZYDIS_REGISTER_RSP && b.mem.index==ZYDIS_REGISTER_NONE){assigned={Kind::StackAddress,state.stack+b.mem.disp.value};assign=true;}
            if(a.type==ZYDIS_OPERAND_TYPE_MEMORY && a.mem.index==ZYDIS_REGISTER_NONE) {
                const auto base=reg(a.mem.base);
                if(base>=0 && state.r[base].kind==Kind::Result && (a.actions&ZYDIS_OPERAND_ACTION_MASK_WRITE)) {
                    if(a.mem.disp.value < (magnitude?4:1) && a.mem.disp.value+int64_t((a.size+7)/8)>0)state.stored=0;
                    const bool scalar=ins.address_width==64 && (ins.mnemonic==ZYDIS_MNEMONIC_MOVSS||ins.mnemonic==ZYDIS_MNEMONIC_VMOVSS) && a.size==32 && b.type==ZYDIS_OPERAND_TYPE_REGISTER && b.reg.value==ZYDIS_REGISTER_XMM0 && state.floatResult;
                    if(a.mem.disp.value==0 && state.call && scalar)state.stored=state.call;
                }
            }
            // Register writes that do not carry a proven value invalidate it.
            for(unsigned i=0;i<ins.operand_count;++i) {
                const auto& o=d.op[i];if(o.type!=ZYDIS_OPERAND_TYPE_REGISTER || !(o.actions&ZYDIS_OPERAND_ACTION_MASK_WRITE))continue;
                const auto r=reg(o.reg.value);if(r>=0 && r!=reg(ZYDIS_REGISTER_RSP))state.r[r]={};
                if(o.reg.value==ZYDIS_REGISTER_XMM0 || o.reg.value==ZYDIS_REGISTER_YMM0)state.floatResult=false;
            }
            if(assign){const auto r=reg(a.reg.value);if(r>=0)state.r[r]=assigned;}
            if(ins.mnemonic==ZYDIS_MNEMONIC_PUSH)state.stack-=8;
            if(ins.mnemonic==ZYDIS_MNEMONIC_POP)state.stack+=8;
            if((ins.mnemonic==ZYDIS_MNEMONIC_SUB||ins.mnemonic==ZYDIS_MNEMONIC_ADD) && a.type==ZYDIS_OPERAND_TYPE_REGISTER && a.reg.value==ZYDIS_REGISTER_RSP && b.type==ZYDIS_OPERAND_TYPE_IMMEDIATE)
                state.stack+=(ins.mnemonic==ZYDIS_MNEMONIC_ADD?1:-1)*b.imm.value.s;
            else if(a.type==ZYDIS_OPERAND_TYPE_REGISTER && a.reg.value==ZYDIS_REGISTER_RSP && (a.actions&ZYDIS_OPERAND_ACTION_MASK_WRITE) && ins.mnemonic!=ZYDIS_MNEMONIC_POP)
                throw std::runtime_error("native wrapper stack convention cannot be verified");
        }
        if(ins.meta.category==ZYDIS_CATEGORY_COND_BR || ins.mnemonic==ZYDIS_MNEMONIC_JMP) {
            if(a.type!=ZYDIS_OPERAND_TYPE_IMMEDIATE || !a.imm.is_relative)throw std::runtime_error("native wrapper has an unresolved branch");
            enqueue(relative(at,d),state);
            if(ins.mnemonic==ZYDIS_MNEMONIC_JMP)continue;
        }
        enqueue(next,state);
    }
    if(!returns || !selected)throw std::runtime_error("native wrapper required dispatch/result route is missing");
    return {0,90};
}
}
