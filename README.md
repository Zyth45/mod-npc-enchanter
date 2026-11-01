# mod-npc-enchanter (SquidBots)

An NPC (entry 9785001, script `npc_enchanter_squidbots`) that applies permanent enchantments to
the gear a player wears. On by default (`EnchanterNpc.Enable = 1`, SquidBots 2.9); set it to 0 to hide the NPC.

- The enchantment list comes from the client data: every Enchanting recipe (SkillLineAbility,
  skill 333) with an "enchant item" effect. CoA's own recipes are included.
- Each enchantment has a tier: 60 (classic and CoA content), 70 (The Burning Crusade),
  80 (Wrath of the Lich King). `EnchanterNpc.AllowedTiers` chooses the tiers offered (default "60").
- One NPC in each of the eight capitals (enchanting shops), invisible while the module is off.
- A per-spell tier fix or ban goes in the world table `mod_npc_enchanter_override`.
- Settings: see `conf/npc_enchanter.conf.dist`.

Install as `modules/mod-npc-enchanter` (the loader function is `Addmod_npc_enchanterScripts`).

Idea taken from azerothcore/mod-npc-enchanter (AGPL v3); this code is a rewrite.
License: GNU AGPL v3, like AzerothCore.
