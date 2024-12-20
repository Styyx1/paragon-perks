#include "Events.h"
#include "UpdateManager.h"
#include "patches/ArmorRatingScaling.h"
#include "patches/BashBlockStaminaPatch.h"
#include "patches/MiscPatches.h"

namespace Hooks
{
    bool InstallHooks()
    {
        if (!UpdateManager::Install()) {
            return false;
        }
        if (!MiscPatches::InstallScalePatch()) {
            return false;
        }
        if (!MiscPatches::InstallFBlockPatch()) {
            return false;
        }
        if (!MiscPatches::InstallSpellCapPatch()) {
            return false;
        }
        WeaponFireHandler::InstallArrowReleaseHook();

        auto runtime = REL::Module::GetRuntime();
        if (Settings::GetSingleton()->armorScalingEnabled) {
            if (runtime == REL::Module::Runtime::AE) {
                logger::info("Installing ar hook AE");
                ArmorRatingScaling::InstallArmorRatingHookAE();
            }
            else {
                logger::info("Installing ar hook SE");
                ArmorRatingScaling::InstallArmorRatingHookSE();
            }
            logger::info("Installed ar hook");
        }
        if (!BashBlockStaminaPatch::InstallBlockMultHook()) {
            return false;
        }
        //ActorUpdateHook::InstallUpdateActor();
        CombatHit::Install(); // 
        BowHit::Install();
        AdjustActiveEffect::Install();
        ValueEffectStartHook::Install();
        ValueEffectFinishHook::Install();
        Hook_OnProjectileCollision::Install();
        
        return true;
    }
    bool InstallBashMultHook()
    {
        return BashBlockStaminaPatch::InstallBashMultHook();
    }
    bool shouldIgnoreHit(RE::Projectile* a_projectile, RE::hkpAllCdPointCollector* a_AllCdPointCollector)
    {
        if (a_AllCdPointCollector) {
            for (auto& hit : a_AllCdPointCollector->hits) {
                auto refrA = RE::TESHavokUtilities::FindCollidableRef(*hit.rootCollidableA);
                auto refrB = RE::TESHavokUtilities::FindCollidableRef(*hit.rootCollidableB);
                if (refrA && refrA->formType == RE::FormType::ActorCharacter) {
                    dlog("ref a true");
                    return TimedBlockHandler::BlockHandler::GetSingleton()->processProjectileBlock(refrA->As<RE::Actor>(), a_projectile, const_cast<RE::hkpCollidable*>(hit.rootCollidableB));
                }                
                if (refrB && refrB->formType == RE::FormType::ActorCharacter) {
                    dlog(" ref b true");
                    return TimedBlockHandler::BlockHandler::GetSingleton()->processProjectileBlock(refrB->As<RE::Actor>(), a_projectile, const_cast<RE::hkpCollidable*>(hit.rootCollidableA));
                }
            }
        }
        return false;
    }
    void ActorUpdateHook::InstallUpdateActor()
    {
        REL::Relocation<std::uintptr_t> ActorVTABLE{ RE::VTABLE_Character[0] };

        _ActorUpdate = ActorVTABLE.write_vfunc(0xAD, ActorUpdate);
        logger::info("hook:NPC Update");
    }
    void ActorUpdateHook::ActorUpdate(RE::Character* a_this, float a_delta)
    {
        _ActorUpdate(a_this, a_delta);
        /*if (a_this->Is3DLoaded() && a_this->IsInCombat() && !a_this->IsDead()) {
            Settings* settings = Settings::GetSingleton();
            if (Conditions::ActorHasActiveEffect(a_this, settings->StaminaPenEffectNPC) || Conditions::ActorHasActiveEffect(a_this, settings->StaminaPenaltyEffect)) {
                Conditions::greyoutAvMeter(a_this, RE::ActorValue::kStamina);
            }
            else {
                Conditions::revertAvMeter(a_this, RE::ActorValue::kStamina);
            }
        }*/
    }
    void CombatHit::Install()
    {
        auto& trampoline = SKSE::GetTrampoline();
        REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(42832, 44001), REL::VariantOffset(0x1a5, 0x1a4, 0x1a5) };
        _originalCall = trampoline.write_call<5>(target.address(), &PitFighter);
    }

    float CombatHit::PitFighter(void* _weap, RE::ActorValueOwner* a, float DamageMult, char isbow)
    {
        const Settings* settings = Settings::GetSingleton();
        RE::PlayerCharacter* player = Cache::GetPlayerSingleton();
        RE::Actor* actor = skyrim_cast<RE::Actor*>(a);

        auto dam = _originalCall(_weap, a, DamageMult, isbow);
        if (player->HasPerk(settings->PitFighterPerk)) {
            if (actor != player) {
                return dam;
            }

            if (Conditions::NumNearbyActors(player, 500.0f, false) > 0) {
                if (!isbow) {
                    std::int32_t enemyNum = Conditions::NumNearbyActors(player, 500.0f, false);
                    if (enemyNum <= 2)
                        dam *= Settings::dmgModifierMinEnemy;
                    if (enemyNum == 3)
                        dam *= Settings::dmgModifierMidEnemy;
                    if (enemyNum >= 4)
                        dam *= Settings::dmgModifierMaxEnemy;
                    return dam;
                }              
            }
        }
        return dam;
    }

    void BowHit::Install()
    {
        //modifies the bow draw modifier actually, will try to find a better hook location at some point to adjust the damage to make it usable for NPCs
        auto& trampoline = SKSE::GetTrampoline();
        REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(42928, 44108), REL::VariantOffset(0x604, 0x5d6, 0x604) };
        _originalCall = trampoline.write_call<5>(target.address(), &PitFighterBow);

    }

    float BowHit::PitFighterBow(float a1, float a2)
    {
        RE::PlayerCharacter* player = Cache::GetPlayerSingleton();
        Settings* settings = Settings::GetSingleton();
        auto dam = _originalCall(a1, a2);
        if (player->IsInCombat() && player->HasPerk(settings->PitFighterPerk) && player->IsAttacking()) {
            std::int32_t enemyNum = Conditions::NumNearbyActors(player, 500.0f, false);
            if (enemyNum >= 4) {                
                dam *= Settings::dmgModifierMinEnemy;
                dlog("return with more than 4 enemies, dam is {}", dam);
            }                
            if (enemyNum == 3){
                dam *= Settings::dmgModifierMidEnemy;
                dlog("return with 3 enemies, dam is {}", dam);
            }                
            if (enemyNum <= 2 ) {
                dam *= Settings::dmgModifierMaxEnemy;
                dlog("return with 2 or less enemies, dam is {}", dam);
            }                
            return dam;
        }
        return dam;
    }
    void AdjustActiveEffect::AdjustSpells(RE::ActiveEffect* a_this, float a_power, bool a_onlyHostile)
    {
        const auto attacker = a_this->GetCasterActor();
        const auto target = a_this->GetTargetActor();
        const auto effect = a_this->GetBaseObject();
        const auto spell = a_this->spell;
        dlog("detected {} on {}", spell->GetName(), attacker.get()->GetName());
        const Settings* settings = Settings::GetSingleton();
        if (attacker && attacker->HasPerk(settings->PitFighterPerk) && target && spell && effect && effect->IsHostile()) {
            dlog("{} adjusted for {}", spell->GetName(), attacker.get()->GetName());
            if (const auto projectile = effect->data.projectileBase; projectile) {
                auto dam = a_this->magnitude;
                std::int32_t enemyNum = Conditions::NumNearbyActors(attacker.get(), 500.0f, false);
                dlog("sanity check, enemy number is {}", enemyNum);
                if (enemyNum >= 4) {                
                    dam *= Settings::dmgModifierMinEnemy;
                    dlog("return with more than 4 enemies, dam is {}", dam);
                }                
                if (enemyNum == 3){
                    dam *= Settings::dmgModifierMidEnemy;
                    dlog("return with 3 enemies, dam is {}", dam);
                }                
                if (enemyNum <= 2 ) {
                    dam *= Settings::dmgModifierMaxEnemy;
                    dlog("return with 2 or less enemies, dam is {}", dam);
                }
                a_this->magnitude = dam;
            }
        }
        func(a_this, a_power, a_onlyHostile);
    }
    void AdjustActiveEffect::Install()
    {
        auto& trampoline = SKSE::GetTrampoline();
        REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(33763, 34547), REL::Relocate(0x4A3, 0x656, 0x427)  }; // MagicTarget::CheckAddEffect
        func = trampoline.write_call<5>(target.address(), &AdjustSpells);
    }

    void ValueEffectStartHook::Install()
    {
        func = REL::Relocation<uintptr_t>{ RE::PeakValueModifierEffect::VTABLE[0] }.write_vfunc(20, thunk);
        logger::info("ValueEffectStartHook complete...");
    }

    void ValueEffectStartHook::thunk(RE::PeakValueModifierEffect* a_this)
    {
        dlog("new hook active");
        func(a_this);
        auto effect = a_this->effect;
        float magnitude = effect->GetMagnitude();
        if (a_this->GetBaseObject() == Settings::StaminaPenaltyEffect || a_this->GetBaseObject() == Settings::StaminaPenEffectNPC) {
            RE::Actor* actor = skyrim_cast<RE::Actor*>(a_this->target);
            if (actor) {
                dlog("effect end hooked. effect is {} and target is {}", a_this->GetBaseObject()->GetName(), actor->GetName());
                Conditions::greyoutAvMeter(actor, RE::ActorValue::kStamina);                    
            }
        }
    }
    void ValueEffectFinishHook::Install()
    {
        func = REL::Relocation<uintptr_t>{ RE::PeakValueModifierEffect::VTABLE[0] }.write_vfunc(21, thunk);
        logger::info("ValueEffectFinishHook complete...");
    }

    void ValueEffectFinishHook::thunk(RE::PeakValueModifierEffect* a_this)
    {
        dlog("new end effect hook active");
        func(a_this);
        auto effect = a_this->effect;
        float magnitude = effect->GetMagnitude();
        if (a_this->GetBaseObject() == Settings::StaminaPenaltyEffect || a_this->GetBaseObject() == Settings::StaminaPenEffectNPC) {
            RE::Actor* actor = skyrim_cast<RE::Actor*>(a_this->target);
            if (actor) {
                dlog("effect end hooked. effect is {} and target is {}", a_this->GetBaseObject()->GetName(), actor->GetName());
                Conditions::revertAvMeter(actor, RE::ActorValue::kStamina);                    
            }
        }
    }
    void Hook_OnProjectileCollision::Install()
    {
        REL::Relocation<std::uintptr_t> arrowProjectileVtbl{ RE::VTABLE_ArrowProjectile[0] };
        REL::Relocation<std::uintptr_t> missileProjectileVtbl{ RE::VTABLE_MissileProjectile[0] };

        _arrowCollission = arrowProjectileVtbl.write_vfunc(0xBE, OnArrowCollision);
        _missileCollission = missileProjectileVtbl.write_vfunc(0xBE, OnMissileCollision);
        logger::info("hook:OnProjectileCollision");
    };
    void Hook_OnProjectileCollision::OnArrowCollision(RE::Projectile* a_this, RE::hkpAllCdPointCollector* a_AllCdPointCollector)
    {
        if (shouldIgnoreHit(a_this, a_AllCdPointCollector)) {
            dlog("arrow collision ignored");
            return;
        };
        dlog("arrow collision happened");
        _arrowCollission(a_this, a_AllCdPointCollector);
    }
    void Hook_OnProjectileCollision::OnMissileCollision(RE::Projectile* a_this, RE::hkpAllCdPointCollector* a_AllCdPointCollector)
    {
        if (shouldIgnoreHit(a_this, a_AllCdPointCollector)) {
            dlog("missile collision ignored");
            return;
        };
        _missileCollission(a_this, a_AllCdPointCollector);
    }
} // namespace Hooks
