-- mod-npc-enchanter: one enchanter in each of the eight capitals, inside the enchanting shop,
-- between the trainer and the apprentice/supplier. Invisible while EnchanterNpc.Enable = 0.
-- Guids 9785001-9785008: free in the 2.8.4 world database (checked 31/10).

SET @ENTRY := 9785001;
SET @GUID  := 9785001;

DELETE FROM `creature` WHERE `guid` BETWEEN @GUID AND @GUID + 7;
INSERT INTO `creature` (`guid`, `id`, `map`, `spawnMask`, `phaseMask`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecs`, `Comment`) VALUES
(@GUID,     @ENTRY,   0, 1, 1, -8854.09,   802.62,   96.47, 5.36, 300, 'Enchanter NPC - Stormwind'),
(@GUID + 1, @ENTRY,   0, 1, 1, -4804.99, -1193.14,  505.90, 2.90, 300, 'Enchanter NPC - Ironforge'),
(@GUID + 2, @ENTRY,   1, 1, 1, 10147.05,  2318.22, 1333.08, 0.99, 300, 'Enchanter NPC - Darnassus'),
(@GUID + 3, @ENTRY, 530, 1, 1, -3887.92,-11492.10, -136.10, 1.68, 300, 'Enchanter NPC - Exodar'),
(@GUID + 4, @ENTRY,   1, 1, 1,  1912.72, -4433.58,   24.90, 2.86, 300, 'Enchanter NPC - Orgrimmar'),
(@GUID + 5, @ENTRY,   0, 1, 1,  1484.90,   274.30,  -62.08, 3.19, 300, 'Enchanter NPC - Undercity'),
(@GUID + 6, @ENTRY,   1, 1, 1, -1114.40,    49.46,  140.85, 5.29, 300, 'Enchanter NPC - Thunder Bluff'),
(@GUID + 7, @ENTRY, 530, 1, 1,  9960.51, -7243.58,   32.16, 2.43, 300, 'Enchanter NPC - Silvermoon');
