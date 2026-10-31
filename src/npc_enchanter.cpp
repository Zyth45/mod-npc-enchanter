/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 */

#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Tokenize.h"
#include "WorldSession.h"

#include <algorithm>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr uint32 NPC_TEXT_ENCHANTER = 9785001;

    constexpr uint32 SENDER_MAIN = 1;
    constexpr uint32 SENDER_SLOT_BASE = 100;
    constexpr uint32 SENDER_APPLY_BASE = 200;

    constexpr uint32 PAGE_SIZE = 25;

    constexpr uint32 LAST_TBC_SPELL_ID_BOUND = 44483;
    constexpr uint32 FIRST_TBC_SPELL_ID = 27899;
    constexpr uint32 LAST_WOTLK_SPELL_ID = 80864;
    constexpr uint32 CLASSIC_SKILL_CEILING = 310;

    struct EnchantOffer
    {
        uint32 SpellId = 0;
        uint32 EnchantId = 0;
        uint32 Tier = 60;
        SpellInfo const* Spell = nullptr;
        SpellItemEnchantmentEntry const* Enchant = nullptr;
    };

    struct EnchanterConfig
    {
        bool Enable = false;
        std::set<uint32> AllowedTiers{ 60 };
        uint32 CostGold60 = 1;
        uint32 CostGold70 = 5;
        uint32 CostGold80 = 10;
        bool RequireProfession = true;
        bool RespectRequiredLevel = true;
        std::set<uint32> ExcludeSpells;
        uint32 LogCatalog = 1;
    };

    EnchanterConfig sCfg;
    std::vector<EnchantOffer> sCatalog;

    std::set<uint32> ParseIdList(std::string const& text)
    {
        std::set<uint32> ids;
        for (std::string_view token : Acore::Tokenize(text, ',', false))
        {
            std::string value(token);
            value.erase(std::remove_if(value.begin(), value.end(), [](char c) { return c == ' ' || c == '"'; }), value.end());
            if (value.empty() || !std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; }))
                continue;
            ids.insert(uint32(std::stoul(value)));
        }
        return ids;
    }

    void LoadConfig()
    {
        sCfg.Enable = sConfigMgr->GetOption<bool>("EnchanterNpc.Enable", false);

        sCfg.AllowedTiers.clear();
        for (uint32 tier : ParseIdList(sConfigMgr->GetOption<std::string>("EnchanterNpc.AllowedTiers", "60")))
            if (tier == 60 || tier == 70 || tier == 80)
                sCfg.AllowedTiers.insert(tier);
        sCfg.AllowedTiers.insert(60);

        sCfg.CostGold60 = sConfigMgr->GetOption<uint32>("EnchanterNpc.CostGold60", 1);
        sCfg.CostGold70 = sConfigMgr->GetOption<uint32>("EnchanterNpc.CostGold70", 5);
        sCfg.CostGold80 = sConfigMgr->GetOption<uint32>("EnchanterNpc.CostGold80", 10);
        sCfg.RequireProfession = sConfigMgr->GetOption<bool>("EnchanterNpc.RequireProfession", true);
        sCfg.RespectRequiredLevel = sConfigMgr->GetOption<bool>("EnchanterNpc.RespectRequiredLevel", true);
        sCfg.ExcludeSpells = ParseIdList(sConfigMgr->GetOption<std::string>("EnchanterNpc.ExcludeSpells", ""));
        sCfg.LogCatalog = sConfigMgr->GetOption<uint32>("EnchanterNpc.LogCatalog", 1);
    }

    uint32 CostInCopper(uint32 tier)
    {
        uint32 gold = tier >= 80 ? sCfg.CostGold80 : (tier >= 70 ? sCfg.CostGold70 : sCfg.CostGold60);
        return gold * GOLD;
    }

    uint32 ClassifyTier(uint32 spellId, SkillLineAbilityEntry const* ability, SpellItemEnchantmentEntry const* enchant)
    {
        if (spellId > LAST_WOTLK_SPELL_ID)
            return 60;

        uint32 origin = spellId < FIRST_TBC_SPELL_ID ? 60 : (spellId < LAST_TBC_SPELL_ID_BOUND ? 70 : 80);
        if (origin > 60 && ability->TrivialSkillLineRankLow <= CLASSIC_SKILL_CEILING && enchant->requiredLevel <= 60)
            return 60;

        return origin;
    }

    std::unordered_map<uint32, std::pair<uint32, bool>> LoadOverrides()
    {
        std::unordered_map<uint32, std::pair<uint32, bool>> overrides;
        QueryResult result = WorldDatabase.Query("SELECT `spell_id`, `tier`, `enabled` FROM `mod_npc_enchanter_override`");
        if (!result)
            return overrides;

        do
        {
            Field* fields = result->Fetch();
            overrides[fields[0].Get<uint32>()] = { fields[1].Get<uint32>(), fields[2].Get<bool>() };
        } while (result->NextRow());

        return overrides;
    }

    void BuildCatalog()
    {
        sCatalog.clear();
        auto overrides = LoadOverrides();
        std::set<uint32> seenSpells;

        for (SkillLineAbilityEntry const* ability : sSkillLineAbilityStore)
        {
            if (!ability || ability->SkillLine != SKILL_ENCHANTING || !seenSpells.insert(ability->Spell).second)
                continue;

            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(ability->Spell);
            if (!spellInfo)
                continue;

            for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                if (spellInfo->Effects[i].Effect != SPELL_EFFECT_ENCHANT_ITEM || spellInfo->Effects[i].MiscValue <= 0)
                    continue;

                uint32 enchantId = uint32(spellInfo->Effects[i].MiscValue);
                SpellItemEnchantmentEntry const* enchant = sSpellItemEnchantmentStore.LookupEntry(enchantId);
                if (!enchant)
                    break;

                EnchantOffer offer;
                offer.SpellId = spellInfo->Id;
                offer.EnchantId = enchantId;
                offer.Spell = spellInfo;
                offer.Enchant = enchant;
                offer.Tier = ClassifyTier(spellInfo->Id, ability, enchant);

                auto itr = overrides.find(spellInfo->Id);
                if (itr != overrides.end())
                {
                    if (!itr->second.second)
                        break;
                    offer.Tier = itr->second.first;
                }

                sCatalog.push_back(offer);
                break;
            }
        }

        std::sort(sCatalog.begin(), sCatalog.end(), [](EnchantOffer const& a, EnchantOffer const& b)
        {
            if (a.Tier != b.Tier)
                return a.Tier < b.Tier;
            return a.SpellId < b.SpellId;
        });

        if (sCfg.LogCatalog)
        {
            uint32 counts[3] = { 0, 0, 0 };
            for (EnchantOffer const& offer : sCatalog)
            {
                counts[offer.Tier >= 80 ? 2 : (offer.Tier >= 70 ? 1 : 0)]++;
                if (sCfg.LogCatalog >= 2)
                    LOG_INFO("module", "mod-npc-enchanter: tier {} spell {} enchant {} '{}'", offer.Tier, offer.SpellId, offer.EnchantId, offer.Spell->SpellName[0]);
            }
            LOG_INFO("module", "mod-npc-enchanter: {} enchantments (tier 60: {}, tier 70: {}, tier 80: {}), {} overrides",
                sCatalog.size(), counts[0], counts[1], counts[2], overrides.size());
        }
    }

    bool IsOfferable(Player* player, Item* item, EnchantOffer const& offer)
    {
        if (!sCfg.AllowedTiers.count(offer.Tier) || sCfg.ExcludeSpells.count(offer.SpellId))
            return false;

        if (!item->IsFitToSpellRequirements(offer.Spell))
            return false;

        if (sCfg.RequireProfession && offer.Enchant->requiredSkill &&
            (!player->HasSkill(offer.Enchant->requiredSkill) || player->GetSkillValue(offer.Enchant->requiredSkill) < offer.Enchant->requiredSkillValue))
            return false;

        if (sCfg.RespectRequiredLevel && offer.Enchant->requiredLevel > player->GetLevel())
            return false;

        return true;
    }

    std::vector<EnchantOffer const*> OffersFor(Player* player, Item* item)
    {
        std::vector<EnchantOffer const*> offers;
        std::set<uint32> seenEnchants;
        for (EnchantOffer const& offer : sCatalog)
            if (IsOfferable(player, item, offer) && seenEnchants.insert(offer.EnchantId).second)
                offers.push_back(&offer);
        return offers;
    }

    std::string SlotName(uint8 slot)
    {
        switch (slot)
        {
            case EQUIPMENT_SLOT_HEAD:      return "Head";
            case EQUIPMENT_SLOT_NECK:      return "Neck";
            case EQUIPMENT_SLOT_SHOULDERS: return "Shoulders";
            case EQUIPMENT_SLOT_BODY:      return "Shirt";
            case EQUIPMENT_SLOT_CHEST:     return "Chest";
            case EQUIPMENT_SLOT_WAIST:     return "Waist";
            case EQUIPMENT_SLOT_LEGS:      return "Legs";
            case EQUIPMENT_SLOT_FEET:      return "Feet";
            case EQUIPMENT_SLOT_WRISTS:    return "Wrists";
            case EQUIPMENT_SLOT_HANDS:     return "Hands";
            case EQUIPMENT_SLOT_FINGER1:   return "Ring 1";
            case EQUIPMENT_SLOT_FINGER2:   return "Ring 2";
            case EQUIPMENT_SLOT_TRINKET1:  return "Trinket 1";
            case EQUIPMENT_SLOT_TRINKET2:  return "Trinket 2";
            case EQUIPMENT_SLOT_BACK:      return "Back";
            case EQUIPMENT_SLOT_MAINHAND:  return "Main hand";
            case EQUIPMENT_SLOT_OFFHAND:   return "Off hand";
            case EQUIPMENT_SLOT_RANGED:    return "Ranged";
            case EQUIPMENT_SLOT_TABARD:    return "Tabard";
            default:                       return "Item";
        }
    }

    std::string OfferLabel(Player* player, EnchantOffer const& offer, bool current)
    {
        LocaleConstant locale = player->GetSession()->GetSessionDbcLocale();
        char const* name = offer.Spell->SpellName[locale];
        if (!name || !*name)
            name = offer.Spell->SpellName[0];

        std::string label = name ? name : "?";
        char const* effect = offer.Enchant->description[locale];
        if (!effect || !*effect)
            effect = offer.Enchant->description[0];
        if (effect && *effect)
            label += std::string(" (") + effect + ")";
        if (offer.Tier > 60)
            label += " [" + std::to_string(offer.Tier) + "]";
        if (current)
            label += " - current";
        return label;
    }

    void SendMainMenu(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);
        bool any = false;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!item || OffersFor(player, item).empty())
                continue;

            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, SlotName(slot) + ": " + item->GetTemplate()->Name1, SENDER_SLOT_BASE + slot, 0);
            any = true;
        }

        if (!any)
            ChatHandler(player->GetSession()).SendNotification("Nothing you wear can take one of my enchantments.");

        SendGossipMenuFor(player, NPC_TEXT_ENCHANTER, creature->GetGUID());
    }

    void SendSlotMenu(Player* player, Creature* creature, uint8 slot, uint32 page)
    {
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
        {
            SendMainMenu(player, creature);
            return;
        }

        std::vector<EnchantOffer const*> offers = OffersFor(player, item);
        uint32 current = item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT);
        uint32 first = page * PAGE_SIZE;
        if (first >= offers.size())
            first = 0, page = 0;

        ClearGossipMenuFor(player);
        for (uint32 i = first; i < offers.size() && i < first + PAGE_SIZE; ++i)
        {
            EnchantOffer const& offer = *offers[i];
            std::string label = OfferLabel(player, offer, offer.EnchantId == current);
            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, label, SENDER_APPLY_BASE + slot, offer.SpellId,
                "Enchant " + item->GetTemplate()->Name1 + "? The current enchantment will be replaced.", CostInCopper(offer.Tier), false);
        }

        if (first + PAGE_SIZE < offers.size())
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Next page", SENDER_SLOT_BASE + slot, page + 1);
        if (page > 0)
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Previous page", SENDER_SLOT_BASE + slot, page - 1);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Back", SENDER_MAIN, 0);

        SendGossipMenuFor(player, NPC_TEXT_ENCHANTER, creature->GetGUID());
    }

    void ApplyOffer(Player* player, Creature* creature, uint8 slot, uint32 spellId)
    {
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        auto itr = std::find_if(sCatalog.begin(), sCatalog.end(), [spellId](EnchantOffer const& o) { return o.SpellId == spellId; });
        if (!item || itr == sCatalog.end() || !IsOfferable(player, item, *itr))
        {
            ChatHandler(player->GetSession()).SendNotification("That enchantment no longer fits this item.");
            CloseGossipMenuFor(player);
            return;
        }

        uint32 cost = CostInCopper(itr->Tier);
        if (!player->HasEnoughMoney(cost))
        {
            player->SendBuyError(BUY_ERR_NOT_ENOUGHT_MONEY, creature, 0, 0);
            CloseGossipMenuFor(player);
            return;
        }

        if (cost)
            player->ModifyMoney(-int32(cost));

        player->ApplyEnchantment(item, PERM_ENCHANTMENT_SLOT, false);
        item->SetEnchantment(PERM_ENCHANTMENT_SLOT, itr->EnchantId, 0, 0, player->GetGUID());
        player->ApplyEnchantment(item, PERM_ENCHANTMENT_SLOT, true);
        player->RemoveTradeableItem(item);
        item->ClearSoulboundTradeable(player);

        creature->CastSpell(player, 12512, true);
        CloseGossipMenuFor(player);
    }
}

class npc_enchanter_squidbots : public CreatureScript
{
public:
    npc_enchanter_squidbots() : CreatureScript("npc_enchanter_squidbots") { }

    struct npc_enchanter_squidbotsAI : public ScriptedAI
    {
        npc_enchanter_squidbotsAI(Creature* creature) : ScriptedAI(creature) { }

        void Reset() override
        {
            me->SetVisible(sCfg.Enable);
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_enchanter_squidbotsAI(creature);
    }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (!sCfg.Enable)
            return true;

        SendMainMenu(player, creature);
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        if (!sCfg.Enable)
        {
            CloseGossipMenuFor(player);
            return true;
        }

        if (sender >= SENDER_APPLY_BASE && sender < SENDER_APPLY_BASE + EQUIPMENT_SLOT_END)
            ApplyOffer(player, creature, uint8(sender - SENDER_APPLY_BASE), action);
        else if (sender >= SENDER_SLOT_BASE && sender < SENDER_SLOT_BASE + EQUIPMENT_SLOT_END)
            SendSlotMenu(player, creature, uint8(sender - SENDER_SLOT_BASE), action);
        else
            SendMainMenu(player, creature);

        return true;
    }
};

class npc_enchanter_world : public WorldScript
{
public:
    npc_enchanter_world() : WorldScript("npc_enchanter_world") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        LoadConfig();
    }

    void OnStartup() override
    {
        BuildCatalog();
    }
};

void AddSC_npc_enchanter()
{
    new npc_enchanter_squidbots();
    new npc_enchanter_world();
}
