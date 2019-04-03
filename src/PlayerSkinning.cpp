#include <xbyak/xbyak.h>

#include "RE/InventoryChanges.h"
#include "RE/InventoryEntryData.h"
#include "RE/PlayerCharacter.h"
#include "RE/TESObjectARMO.h"
#include "RE/TESObjectREFR.h"

#include "ArmorAddonOverrideService.h"
#include "skse64_common/BranchTrampoline.h"
#include "skse64/GameRTTI.h"
#include "RE/EquipManager.h"

namespace OutfitSystem
{
    bool ShouldOverrideSkinning(RE::TESObjectREFR * target)
    {
        if (!ArmorAddonOverrideService::GetInstance().shouldOverride())
            return false;
        return target == RE::PlayerCharacter::GetSingleton();
    }

    const std::set<RE::TESObjectARMO*>& GetOverrideArmors()
    {
        auto& svc = ArmorAddonOverrideService::GetInstance();
        return svc.currentOutfit().armors;
    }

    // E8 ? ? ? ? FF C3 41 3B 5E 10 72 95 
    RelocAddr<void *> TESObjectARMO_ApplyArmorAddon(0x00228AD0);  // 1_5_73

    namespace DontVanillaSkinPlayer
    {
        bool _stdcall ShouldOverride(RE::TESObjectARMO* armor, RE::TESObjectREFR* target) {
            if (!ShouldOverrideSkinning(target))
                return false;
            if ((armor->flags & RE::TESObjectARMO::RecordFlags::kShield) != 0) {
                auto& svc = ArmorAddonOverrideService::GetInstance();
                auto& outfit = svc.currentOutfit();
                if (!outfit.hasShield()) {
                    return false;
                }
            }
            return true;
        }

        // 48 8B CD E8 ? ? ? ? 40 B7 01 40 0F B6 C7 
        RelocAddr<uintptr_t> DontVanillaSkinPlayer_Hook(0x00364652);  // 1_5_73

        void Apply()
        {
            _MESSAGE("Patching vanilla player skinning");
            {
                struct DontVanillaSkinPlayer_Code : Xbyak::CodeGenerator
                {
                    DontVanillaSkinPlayer_Code(void * buf) : CodeGenerator(4096, buf)
                    {
                        Xbyak::Label j_Out;
                        Xbyak::Label f_ApplyArmorAddon;
                        Xbyak::Label f_ShouldOverride;

                        // armor in rcx, target in r13
                        push(rcx);
                        push(rdx);
                        push(r8);
                        mov(rdx, r13);
                        sub(rsp, 0x20);
                        call(ptr[rip+f_ShouldOverride]);
                        add(rsp, 0x20);
                        pop(r8);
                        pop(rdx);
                        pop(rcx);
                        test(al, al);
                        jnz(j_Out);
                        call(ptr[rip+f_ApplyArmorAddon]);
                        L(j_Out);
                        jmp(ptr[rip]);
                        dq(DontVanillaSkinPlayer_Hook.GetUIntPtr() + 0x5);

                        L(f_ApplyArmorAddon);
                        dq(TESObjectARMO_ApplyArmorAddon.GetUIntPtr());

                        L(f_ShouldOverride);
                        dq(uintptr_t(ShouldOverride));
                    }
                };

                void* codeBuf = g_localTrampoline.StartAlloc();
                DontVanillaSkinPlayer_Code code(codeBuf);
                g_localTrampoline.EndAlloc(code.getCurr());

                g_branchTrampoline.Write5Branch(DontVanillaSkinPlayer_Hook.GetUIntPtr(),
                    uintptr_t(code.getCode()));
            }
            _MESSAGE("Done");
        }
    }

    namespace ShimWornFlags
    {
        class _ShieldVisitor : public RE::InventoryChanges::IItemChangeVisitor {
            //
            // If the player has a shield equipped, and if we're not overriding that 
            // shield, then we need to grab the equipped shield's worn-flags.
            //
        public:
            virtual ReturnType Visit(RE::InventoryEntryData* data) override {
                auto form = data->type;
                if (form && form->formType == RE::FormType::Armor) {
                    auto armor = reinterpret_cast<RE::TESObjectARMO*>(form);
                    if ((armor->flags & RE::TESObjectARMO::RecordFlags::kShield) != 0) {
                        this->mask |= static_cast<UInt32>(armor->GetSlotMask());
                        this->hasShield = true;
                    }
                }
                return ReturnType::kContinue;
            };

            UInt32 mask = 0;
            bool   hasShield = false;
        };

        UInt32 OverrideWornFlags(RE::InventoryChanges * inventory) {
            UInt32 mask = 0;
            //
            auto& svc = ArmorAddonOverrideService::GetInstance();
            auto& outfit = svc.currentOutfit();
            auto& armors = outfit.armors;
            bool  shield = false;
            if (!outfit.hasShield()) {
                _ShieldVisitor visitor;
                inventory->ExecuteVisitorOnWorn(&visitor);
                mask |= visitor.mask;
                shield = visitor.hasShield;
            }
            for (auto armor : armors)
            {
                if (armor) {
                    if ((armor->flags & RE::TESObjectARMO::RecordFlags::kShield) != 0) {
                        if (!shield)
                            continue;
                    }
                    mask |= static_cast<UInt32>(armor->GetSlotMask());
                }
            }
            //
            return mask;
        }

        // (+0x7C) E8 ? ? ? ? 45 33 C0 0F 28 CE 
        RelocAddr<uintptr_t> ShimWornFlags_Hook(0x00362F0C);  // 1_5_73
        // E8 ? ? ? ? 8B 8D ? ? ? ? 33 F6 
        RelocAddr<void *> InventoryChanges_GetWornMask(0x001D9040);  // 1_5_73

        void Apply()
        {
            _MESSAGE("Patching shim worn flags");
            {
                struct ShimWornFlags_Code : Xbyak::CodeGenerator
                {
                    ShimWornFlags_Code(void * buf) : CodeGenerator(4096, buf)
                    {
                        Xbyak::Label j_SuppressVanilla;
                        Xbyak::Label j_Out;
                        Xbyak::Label f_ShouldOverrideSkinning;
                        Xbyak::Label f_GetWornMask;
                        Xbyak::Label f_OverrideWornFlags;

                        // target in rsi
                        push(rcx);
                        mov(rcx, rsi);
                        sub(rsp, 0x20);
                        call(ptr[rip + f_ShouldOverrideSkinning]);
                        add(rsp, 0x20);
                        pop(rcx);
                        test(al, al);
                        jnz(j_SuppressVanilla);
                        call(ptr[rip + f_GetWornMask]);
                        jmp(j_Out);

                        L(j_SuppressVanilla);
                        call(ptr[rip + f_OverrideWornFlags]);

                        L(j_Out);
                        jmp(ptr[rip]);
                        dq(ShimWornFlags_Hook.GetUIntPtr() + 0x5);

                        L(f_ShouldOverrideSkinning);
                        dq(uintptr_t(ShouldOverrideSkinning));

                        L(f_GetWornMask);
                        dq(InventoryChanges_GetWornMask.GetUIntPtr());

                        L(f_OverrideWornFlags);
                        dq(uintptr_t(OverrideWornFlags));
                    }
                };

                void* codeBuf = g_localTrampoline.StartAlloc();
                ShimWornFlags_Code code(codeBuf);
                g_localTrampoline.EndAlloc(code.getCurr());

                g_branchTrampoline.Write5Branch(ShimWornFlags_Hook.GetUIntPtr(),
                    uintptr_t(code.getCode()));
            }
            _MESSAGE("Done");
        }
    }

    namespace CustomSkinPlayer
    {
        class _ShieldVisitor : public RE::InventoryChanges::IItemChangeVisitor {
            //
            // If the player has a shield equipped, and if we're not overriding that 
            // shield, then we need to grab the equipped shield's worn-flags.
            //
        public:
            virtual ReturnType Visit(RE::InventoryEntryData* data) override {
                auto form = data->type;
                if (form && form->formType == RE::FormType::Armor) {
                    auto armor = reinterpret_cast<RE::TESObjectARMO*>(form);
                    if ((armor->flags & RE::TESObjectARMO::RecordFlags::kShield) != 0) {
                        this->result = true;
                        return ReturnType::kBreak; // halt visitor early
                    }
                }
                return ReturnType::kContinue;
            };
            bool result = false;
        };

        void Custom(RE::Actor* target, RE::ActorWeightModel * actorWeightModel) {
            if (!actorWeightModel)
                return;
            auto base = reinterpret_cast<RE::TESNPC*>(DYNAMIC_CAST(target->baseForm, TESForm, TESNPC));
            if (!base)
                return;
            auto race = base->race;
            bool isFemale = base->IsFemale();
            //
            auto& armors = GetOverrideArmors();
            for (auto it = armors.cbegin(); it != armors.cend(); ++it) {
                RE::TESObjectARMO* armor = *it;
                if (armor) {
                    if ((armor->flags & RE::TESObjectARMO::RecordFlags::kShield) != 0) {
                        //
                        // We should only apply a shield's armor-addons if the player has 
                        // a shield equipped.
                        //
                        auto inventory = target->GetInventoryChanges();
                        if (inventory) {
                            _ShieldVisitor visitor;
                            inventory->ExecuteVisitorOnWorn(&visitor);
                            if (!visitor.result)
                                continue;
                        }
                        else {
                            _MESSAGE("OverridePlayerSkinning: Outfit has a shield; unable to check whether the player has a shield equipped.");
                        }
                    }
                    armor->ApplyArmorAddon(race, actorWeightModel, isFemale);
                }
            }
        }

        // (+0x81) E8 ? ? ? ? 48 8B 0D ? ? ? ? 48 3B F9 75 1B 
        RelocAddr<uintptr_t> CustomSkinPlayer_Hook(0x00364301);
        // E8 ? ? ? ? 0F B6 6C 24 ? 40 84 ED 
        RelocAddr<void *> InventoryChanges_ExecuteVisitorOnWorn(0x001E51D0);

        void Apply()
        {
            _MESSAGE("Patching custom skin player");
            {
                struct CustomSkinPlayer_Code : Xbyak::CodeGenerator
                {
                    CustomSkinPlayer_Code(void * buf) : CodeGenerator(4096, buf)
                    {
                        Xbyak::Label j_Out;
                        Xbyak::Label f_Custom;
                        Xbyak::Label f_ExecuteVisitorOnWorn;
                        Xbyak::Label f_ShouldOverrideSkinning;

                        // call original function
                        call(ptr[rip + f_ExecuteVisitorOnWorn]);

                        push(rcx);
                        mov(rcx, rbx);
                        sub(rsp, 0x20);
                        call(ptr[rip + f_ShouldOverrideSkinning]);
                        add(rsp, 0x20);
                        pop(rcx);

                        test(al, al);
                        jz(j_Out);

                        push(rdx);
                        push(rcx);
                        mov(rcx, rbx);
                        mov(rdx, rdi);
                        sub(rsp, 0x20);
                        call(ptr[rip + f_Custom]);
                        add(rsp, 0x20);
                        pop(rcx);
                        pop(rdx);             

                        L(j_Out);
                        jmp(ptr[rip]);
                        dq(CustomSkinPlayer_Hook.GetUIntPtr() + 0x5);

                        L(f_Custom);
                        dq(uintptr_t(Custom));

                        L(f_ExecuteVisitorOnWorn);
                        dq(InventoryChanges_ExecuteVisitorOnWorn.GetUIntPtr());

                        L(f_ShouldOverrideSkinning);
                        dq(uintptr_t(ShouldOverrideSkinning));
                    }
                };

                void* codeBuf = g_localTrampoline.StartAlloc();
                CustomSkinPlayer_Code code(codeBuf);
                g_localTrampoline.EndAlloc(code.getCurr());

                g_branchTrampoline.Write5Branch(CustomSkinPlayer_Hook.GetUIntPtr(),
                    uintptr_t(code.getCode()));
            }
            _MESSAGE("Done");
        }
    }

    // (+0x97) E8 ? ? ? ? 84 C0 74 2D 4C 8B 06 
    RelocAddr<uintptr_t> FixEquipConflictCheck_Hook(0x0060CAC7);
    // its the above function
    RelocAddr<void *> BGSBipedObjectForm_TestBodyPartByIndex(0x001820A0);

    namespace FixEquipConflictCheck
    {
        //
// When you try to equip an item, the game loops over the armors in your ActorWeightModel 
// rather than your other worn items. Because we're tampering with what goes into the AWM, 
// this means that conflict checks are run against your outfit instead of your equipment, 
// unless we patch in a fix. (For example, if your outfit doesn't include a helmet, then 
// you'd be able to stack helmets endlessly without this patch.)
//
// The loop in question is performed in Actor::Unk_120, which is also generally responsible 
// for equipping items at all.
//
        class _Visitor : public RE::InventoryChanges::IItemChangeVisitor {
            //
            // Bethesda used a visitor to add armor-addons to the ActorWeightModel in the first 
            // place (see call stack for DontVanillaSkinPlayer patch), so why not use a similar 
            // visitor to check for conflicts?
            //
        public:
            virtual ReturnType Visit(RE::InventoryEntryData* data) override {
                auto form = data->type;
                if (form && form->formType == RE::FormType::Armor) {
                    auto armor = reinterpret_cast<RE::TESObjectARMO*>(form);
                    if (armor->TestBodyPartByIndex(this->conflictIndex)) {
                        auto em = RE::EquipManager::GetSingleton();
                        //
                        // TODO: The third argument to this call is meant to be a BaseExtraList*, 
                        // and Bethesda supplies one when calling from Unk_120. Can we get away 
                        // with a nullptr here, or do we have to find the BaseExtraList that 
                        // contains an ExtraWorn?
                        //
                        // I'm not sure how to investigate this, but I did run one test, and that 
                        // works properly: I gave myself ten Falmer Helmets and applied different 
                        // enchantments to two of them (leaving the others unenchanted). In tests, 
                        // I was unable to stack the helmets with each other or with other helmets, 
                        // suggesting that the BaseExtraList may not be strictly necessary.
                        //
                        em->UnEquipItem(this->target, form, nullptr, 1, nullptr, false, false, true, false, nullptr);
                    }
                }
                return ReturnType::kContinue;
            };

            RE::Actor* target;
            UInt32     conflictIndex = 0;
        };
        void Inner(UInt32 bodyPartForNewItem, RE::Actor* target) {
            auto inventory = target->GetInventoryChanges();
            if (inventory) {
                _Visitor visitor;
                visitor.conflictIndex = bodyPartForNewItem;
                visitor.target = target;
                inventory->ExecuteVisitorOnWorn(&visitor);
            }
            else {
                _MESSAGE("OverridePlayerSkinning: Conflict check failed: no inventory!");
            }
        }
        bool ShouldOverride(RE::TESForm* item) {
            //
            // We only hijack equipping for armors, so I'd like for this patch to only 
            // apply to armors as well. It shouldn't really matter -- before I added 
            // this check, weapons and the like tested in-game with no issues, likely 
            // because they're handled very differently -- but I just wanna be sure. 
            // We should use vanilla code whenever we don't need to NOT use it.
            //
            return (item->formType == RE::FormType::Armor);
        }
        void Apply()
        {
            _MESSAGE("Patching fix for equip conflict check");
            {
                struct FixEquipConflictCheck_Code : Xbyak::CodeGenerator
                {
                    FixEquipConflictCheck_Code(void* buf) : CodeGenerator(4096, buf)
                    {
                        Xbyak::Label j_Out;
                        Xbyak::Label j_Exit;
                        Xbyak::Label f_Inner;
                        Xbyak::Label f_TestBodyPartByIndex;
                        Xbyak::Label f_ShouldOverride;

                        call(ptr[rip + f_TestBodyPartByIndex]);
                        test(al, al);
                        jz(j_Exit);

                        // rsp+0x10: item
                        // rdi: Actor
                        // rbx: Body Slot
                        push(rcx);
                        mov(rcx, ptr[rsp + 0x18]);
                        sub(rsp, 0x20);
                        call(ptr[rip + f_ShouldOverride]);
                        add(rsp, 0x20);
                        pop(rcx);
                        test(al, al);
                        mov(rax, 1);
                        jz(j_Out);
                        push(rcx);
                        push(rdx);
                        mov(rcx, rbx);
                        mov(rdx, rdi);
                        sub(rsp, 0x20);
                        call(ptr[rip + f_Inner]);
                        add(rsp, 0x20);
                        pop(rdx);
                        pop(rcx);

                        L(j_Exit);
                        xor(al, al);

                        L(j_Out);
                        jmp(ptr[rip]);
                        dq(FixEquipConflictCheck_Hook.GetUIntPtr() + 0x5);

                        L(f_TestBodyPartByIndex);
                        dq(BGSBipedObjectForm_TestBodyPartByIndex.GetUIntPtr());

                        L(f_ShouldOverride);
                        dq(uintptr_t(ShouldOverride));

                        L(f_Inner);
                        dq(uintptr_t(Inner));
                    }
                };

                void* codeBuf = g_localTrampoline.StartAlloc();
                FixEquipConflictCheck_Code code(codeBuf);
                g_localTrampoline.EndAlloc(code.getCurr());

                g_branchTrampoline.Write5Branch(FixEquipConflictCheck_Hook.GetUIntPtr(),
                                                uintptr_t(code.getCode()));
            }
            _MESSAGE("Done");
        }
    }

    void ApplyPlayerSkinningHooks()
    {
        DontVanillaSkinPlayer::Apply();
        ShimWornFlags::Apply();
        CustomSkinPlayer::Apply();
        FixEquipConflictCheck::Apply();
    }
}
