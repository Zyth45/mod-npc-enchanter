-- mod-npc-enchanter (SquidBots 2.8.5): enchanter NPC template, greeting and per-spell tier overrides.
-- Entry 9785001 sits in a range no CoA, Jealous or SquidBots data uses (checked 31/10 on the 2.8.4 sources).
-- The NPC stays invisible while EnchanterNpc.Enable = 0.

SET @ENTRY := 9785001;

DELETE FROM `creature_template` WHERE `entry` = @ENTRY;
INSERT INTO `creature_template` (`entry`, `name`, `subname`, `IconName`, `gossip_menu_id`, `minlevel`, `maxlevel`, `faction`, `npcflag`, `speed_walk`, `speed_run`, `unit_class`, `unit_flags`, `type`, `type_flags`, `RegenHealth`, `flags_extra`, `AIName`, `ScriptName`) VALUES
(@ENTRY, 'Ilyra Spellweave', 'Enchantments', 'Speak', 0, 60, 60, 35, 1, 1, 1.14286, 1, 2, 7, 0, 1, 2, '', 'npc_enchanter_squidbots');

DELETE FROM `creature_template_model` WHERE `CreatureID` = @ENTRY;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(@ENTRY, 0, 9353, 1, 1, 0);

DELETE FROM `npc_text` WHERE `ID` = @ENTRY;
INSERT INTO `npc_text` (`ID`, `text0_0`) VALUES
(@ENTRY, 'Greetings, $N. Choose the piece of gear you want me to enchant.');

CREATE TABLE IF NOT EXISTS `mod_npc_enchanter_override` (
  `spell_id` INT UNSIGNED NOT NULL,
  `tier` TINYINT UNSIGNED NOT NULL DEFAULT 60,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `comment` VARCHAR(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`spell_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
