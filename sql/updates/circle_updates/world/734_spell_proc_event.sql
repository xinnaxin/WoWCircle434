-- Frozen Power

DELETE FROM `spell_proc_event` WHERE `entry` IN (63373, 63374);
INSERT INTO `spell_proc_event` VALUES (63373, 0x00, 11, 0x80000000, 0x00000000, 0x00000000, 0x00010000, 0x00000000, 0.000000, 100, 0);
INSERT INTO `spell_proc_event` VALUES (63374, 0x00, 11, 0x80000000, 0x00000000, 0x00000000, 0x00010000, 0x00000000, 0.000000, 100, 0);