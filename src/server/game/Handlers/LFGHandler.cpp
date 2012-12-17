/*
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#include "WorldSession.h"
#include "WorldPacket.h"
#include "DBCStores.h"
#include "Player.h"
#include "Group.h"
#include "LFGMgr.h"
#include "ObjectMgr.h"
#include "GroupMgr.h"

void BuildPlayerLockDungeonBlock(WorldPacket& data, const LfgLockMap& lock)
{
    data << uint32(lock.size());                           // Size of lock dungeons
    for (LfgLockMap::const_iterator it = lock.begin(); it != lock.end(); ++it)
    {
        data << uint32(it->first);                         // Dungeon entry (id + type)
        data << uint32(it->second);                        // Lock status
        data << uint32(0);                                 // Required player ILvL
        data << uint32(0);                                 // Player ILvL
    }
}

void BuildPartyLockDungeonBlock(WorldPacket& data, const LfgLockPartyMap& lockMap)
{
    data << uint8(lockMap.size());
    for (LfgLockPartyMap::const_iterator it = lockMap.begin(); it != lockMap.end(); ++it)
    {
        data << uint64(it->first);                         // Player guid
        BuildPlayerLockDungeonBlock(data, it->second);
    }
}

void WorldSession::HandleLfgJoinOpcode(WorldPacket& recvData)
{
    if (!sLFGMgr->isOptionEnabled(LFG_OPTION_ENABLE_DUNGEON_FINDER | LFG_OPTION_ENABLE_RAID_BROWSER) ||
        (GetPlayer()->GetGroup() && GetPlayer()->GetGroup()->GetLeaderGUID() != GetPlayer()->GetGUID() &&
        (GetPlayer()->GetGroup()->GetMembersCount() == MAXGROUPSIZE || !GetPlayer()->GetGroup()->isLFGGroup())))
    {
        recvData.rfinish();
        return;
    }

    std::string comment;
    uint32 commentSize;
    uint32 numDungeons;
    uint32 dungeon;
    uint32 roles;

    recvData >> roles;

    for (int i = 0; i < 3; ++i)
    {
        recvData.read_skip<uint32>();
    }

    commentSize = recvData.ReadBits(9);
    numDungeons = recvData.ReadBits(24);
    if (!numDungeons)
    {
        sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_JOIN %s no dungeons selected", GetPlayer()->GetGUID());
        recvData.rfinish();
        return;
    }

    recvData.read(comment, commentSize);

    LfgDungeonSet newDungeons;
    for (uint8 i = 0; i < numDungeons; ++i)
    {
        recvData >> dungeon;
        newDungeons.insert((dungeon & 0x00FFFFFF));        // remove the type from the dungeon entry
    }

    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_JOIN %s roles: %u, Dungeons: %u, Comment: %s",
        GetPlayer()->GetGUID(), roles, uint8(newDungeons.size()), comment.c_str());
    sLFGMgr->JoinLfg(GetPlayer(), uint8(roles), newDungeons, comment);
}

void WorldSession::HandleLfgLeaveOpcode(WorldPacket&  /*recvData*/)
{
    Group* grp = GetPlayer()->GetGroup();
    uint64 guid = GetPlayer()->GetGUID();
    uint64 gguid = grp ? grp->GetGUID() : guid;

    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_LEAVE %s in group: %u",
        GetPlayer()->GetGUID(), grp ? 1 : 0);

    // Check cheating - only leader can leave the queue
    if (!grp || grp->GetLeaderGUID() == GetPlayer()->GetGUID())
        sLFGMgr->LeaveLfg(gguid);
}

void WorldSession::HandleLfgProposalResultOpcode(WorldPacket& recvData)
{
    uint32 lfgGroupID;                                      // Internal lfgGroupID
    bool accept;                                           // Accept to join?
    recvData >> lfgGroupID;
    recvData >> accept;

    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_PROPOSAL_RESULT %s proposal: %u accept: %u",
        GetPlayer()->GetGUID(), lfgGroupID, accept ? 1 : 0);
    sLFGMgr->UpdateProposal(lfgGroupID, GetPlayer()->GetGUID(), accept);
}

void WorldSession::HandleLfgSetRolesOpcode(WorldPacket& recvData)
{
    uint8 roles;
    recvData >> roles;                                     // Player Group Roles
    uint64 guid = GetPlayer()->GetGUID();
    Group* grp = GetPlayer()->GetGroup();
    if (!grp)
    {
        sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_SET_ROLES %s Not in group",
            GetPlayer()->GetGUID());
        return;
    }
    uint64 gguid = grp->GetGUID();
    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_SET_ROLES: Group %u, Player %s, Roles: %u",
        GUID_LOPART(gguid), GetPlayer()->GetGUID(), roles);
    sLFGMgr->UpdateRoleCheck(gguid, guid, roles);
}

void WorldSession::HandleLfgSetCommentOpcode(WorldPacket&  recvData)
{
    uint32 commentSize;
    std::string comment;

    commentSize = recvData.ReadBits(9);
    recvData.ReadBit();
    recvData.read(comment, commentSize);

    uint64 guid = GetPlayer()->GetGUID();
    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_SET_COMMENT %s comment: %s",
        GetPlayer()->GetGUID(), comment.c_str());

    sLFGMgr->SetComment(guid, comment);
}

void WorldSession::HandleLfgSetBootVoteOpcode(WorldPacket& recvData)
{
    bool agree = recvData.ReadBit();

    uint64 guid = GetPlayer()->GetGUID();
    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_SET_BOOT_VOTE %s agree: %u",
        GetPlayer()->GetGUID(), agree ? 1 : 0);
    sLFGMgr->UpdateBoot(guid, agree);
}

void WorldSession::HandleLfgTeleportOpcode(WorldPacket& recvData)
{
    bool out = recvData.ReadBit();

    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_TELEPORT %s out: %u",
        GetPlayer()->GetGUID(), out ? 1 : 0);
    sLFGMgr->TeleportPlayer(GetPlayer(), out, true);
}

void WorldSession::HandleLfgPlayerLockInfoRequestOpcode(WorldPacket& /*recvData*/)
{
    uint64 guid = GetPlayer()->GetGUID();
    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_PLAYER_LOCK_INFO_REQUEST %s",
        GetPlayer()->GetGUID());

    // Get Random dungeons that can be done at a certain level and expansion
    LfgDungeonSet randomDungeons;
    uint8 level = GetPlayer()->getLevel();
    uint8 expansion = GetPlayer()->GetSession()->Expansion();

    LFGDungeonContainer& LfgDungeons = sLFGMgr->GetLFGDungeonMap();
    for (LFGDungeonContainer::const_iterator itr = LfgDungeons.begin(); itr != LfgDungeons.end(); ++itr)
    {
        LFGDungeonData const& dungeon = itr->second;
        if ((dungeon.type == LFG_TYPE_RANDOM || (dungeon.seasonal && sLFGMgr->IsSeasonActive(dungeon.id)))
            && dungeon.expansion <= expansion && dungeon.minlevel <= level && level <= dungeon.maxlevel)
            randomDungeons.insert(dungeon.Entry());
    }

    // Get player locked Dungeons
    LfgLockMap const& lock = sLFGMgr->GetLockedDungeons(guid);
    uint32 rsize = uint32(randomDungeons.size());
    uint32 lsize = uint32(lock.size());

    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_PLAYER_INFO %s", GetPlayer()->GetGUID());
    WorldPacket data(SMSG_LFG_PLAYER_INFO, 1 + rsize * (4 + 1 + 4 + 4 + 4 + 4 + 1 + 4 + 4 + 4) + 4 + lsize * (1 + 4 + 4 + 4 + 4 + 1 + 4 + 4 + 4));

    data << uint8(randomDungeons.size());                  // Random Dungeon count
    for (LfgDungeonSet::const_iterator it = randomDungeons.begin(); it != randomDungeons.end(); ++it)
    {
        data << uint32(*it);                               // Dungeon Entry (id + type)
        LfgReward const* reward = sLFGMgr->GetRandomDungeonReward(*it, level);
        Quest const* quest = NULL;
        uint8 done = 0;
        if (reward)
        {
            quest = sObjectMgr->GetQuestTemplate(reward->reward[0].questId);
            if (quest)
            {
                done = !GetPlayer()->CanRewardQuest(quest, false);
                if (done)
                    quest = sObjectMgr->GetQuestTemplate(reward->reward[1].questId);
            }
        }

        if (quest)
        {
            data << uint8(done);
            data << uint32(1); // Times precision
            data << uint32(7); // Available times per week

            data << uint32(0); // Unknown 4.3.4
            data << uint32(0); // Unknown 4.3.4

            data << uint32(0); // Unknown 4.3.4
            data << uint32(0); // Unknown 4.3.4
            data << uint32(0); // Unknown 4.3.4
            data << uint32(0); // Unknown 4.3.4
            data << uint32(0); // Unknown 4.3.4
            data << uint32(0); // Unknown 4.3.4
            data << uint32(0); // Unknown 4.3.4

            data << uint32(1); // Unknown 4.3.4
            data << uint32(0); // isComplited

            data << uint8(0); // seasonal ?
            {
                for (uint8 i = 0; i < 3; ++i) // 3 - Max roles ?
                {
                    uint8 callToArmsRoleMask = 0; // TODO Call to arms role check (LfgRoles) Not implemented
                    data << uint32(callToArmsRoleMask);
                    if (callToArmsRoleMask > 0)
                    {
                        /* Call to Arms bonus*/

                        data << uint32(0); // Call to arms Money
                        data << uint32(0); // Call to arms XP

                        uint8 totalRewardCount = uint8(quest->GetRewCurrencyCount() + quest->GetRewItemsCount());
                        if (totalRewardCount > 16)
                            totalRewardCount = 16;

                        data << uint8(totalRewardCount);
                        if (totalRewardCount)
                        {
                            for (uint8 j = 0; j < QUEST_REWARD_CURRENCY_COUNT; ++j)
                            {
                                if (!quest->RewardCurrencyId[j])
                                    continue;

                                data << uint32(quest->RewardCurrencyId[j]);
                                data << uint32(0);
                                data << uint32(quest->RewardCurrencyCount[j]);
                                data << uint8(true); // Is currency
                            }

                            ItemTemplate const* iProto = NULL;
                            for (uint8 j = 0; j < QUEST_REWARDS_COUNT; ++j)
                            {
                                if (!quest->RewardItemId[j])
                                    continue;

                                iProto = sObjectMgr->GetItemTemplate(quest->RewardItemId[j]);

                                data << uint32(quest->RewardItemId[j]);
                                data << uint32(iProto ? iProto->DisplayInfoID : 0);
                                data << uint32(quest->RewardItemIdCount[j]);
                                data << uint8(false); // Is currency
                            }
                        }
                    }
                }
            }

            data << uint32(quest->GetRewOrReqMoney());
            data << uint32(quest->XPValue(GetPlayer()));

            uint8 totalRewardCount = uint8(quest->GetRewCurrencyCount() + quest->GetRewItemsCount());
            if (totalRewardCount > 16)
                totalRewardCount = 16;

            data << uint8(totalRewardCount);
            if (totalRewardCount)
            {
                for (uint8 i = 0; i < QUEST_REWARD_CURRENCY_COUNT; ++i)
                {
                    if (!quest->RewardCurrencyId[i])
                        continue;

                    data << uint32(quest->RewardCurrencyId[i]);
                    data << uint32(0);
                    data << uint32(quest->RewardCurrencyCount[i]);
                    data << uint8(true); // Is currency
                }

                ItemTemplate const* iProto = NULL;
                for (uint8 i = 0; i < QUEST_REWARDS_COUNT; ++i)
                {
                    if (!quest->RewardItemId[i])
                        continue;

                    iProto = sObjectMgr->GetItemTemplate(quest->RewardItemId[i]);

                    data << uint32(quest->RewardItemId[i]);
                    data << uint32(iProto ? iProto->DisplayInfoID : 0);
                    data << uint32(quest->RewardItemIdCount[i]);
                    data << uint8(false); // Is currency
                }
            }
        }
        else
        {
            data << uint8(0);
            data << uint32(0);
            data << uint32(0);
            data << uint32(0);
            data << uint32(0);

            for (int8 i = 0; i < 9; ++i)
                data << uint32(0); // Unknown 4.3.4

            data << uint8(1);
            for (int8 i = 0; i < 3; ++i)
                data << uint32(0); // Unknown 4.3.4

            for (int8 i = 0; i < 2; ++i)
                data << uint32(0); // Unknown 4.3.4

            data << uint8(0);
        }
    }
    BuildPlayerLockDungeonBlock(data, lock);
    SendPacket(&data);
}

void WorldSession::HandleLfgPartyLockInfoRequestOpcode(WorldPacket&  /*recvData*/)
{
    uint64 guid = GetPlayer()->GetGUID();
    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_PARTY_LOCK_INFO_REQUEST %s", GetPlayer()->GetGUID());

    Group* grp = GetPlayer()->GetGroup();
    if (!grp)
        return;

    // Get the locked dungeons of the other party members
    LfgLockPartyMap lockMap;
    for (GroupReference* itr = grp->GetFirstMember(); itr != NULL; itr = itr->next())
    {
        Player* plrg = itr->getSource();
        if (!plrg)
            continue;

        uint64 pguid = plrg->GetGUID();
        if (pguid == guid)
            continue;

        lockMap[pguid] = sLFGMgr->GetLockedDungeons(pguid);
    }

    uint32 size = 0;
    for (LfgLockPartyMap::const_iterator it = lockMap.begin(); it != lockMap.end(); ++it)
        size += 8 + 4 + uint32(it->second.size()) * (4 + 4 + 4 + 4);

    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_PARTY_INFO %s", GetPlayer()->GetGUID());
    WorldPacket data(SMSG_LFG_PARTY_INFO, 1 + size);
    BuildPartyLockDungeonBlock(data, lockMap);
    SendPacket(&data);
}

void WorldSession::HandleLfrJoinOpcode(WorldPacket& recvData)
{
    uint32 entry;                                          // Raid id to search
    recvData >> entry;
    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_LFR_JOIN %s dungeon entry: %u",
        GetPlayer()->GetGUID(), entry);
    //SendLfrUpdateListOpcode(entry);
}

void WorldSession::HandleLfrLeaveOpcode(WorldPacket& recvData)
{
    uint32 dungeonId;                                      // Raid id queue to leave
    recvData >> dungeonId;
    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_LFR_LEAVE %s dungeonId: %u",
        GetPlayer()->GetGUID(), dungeonId);
    //sLFGMgr->LeaveLfr(GetPlayer(), dungeonId);
}

void WorldSession::HandleLfgGetStatus(WorldPacket& /*recvData*/)
{
    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_GET_STATUS %s", GetPlayer()->GetGUID());

    uint64 guid = GetPlayer()->GetGUID();
    LfgUpdateData updateData = sLFGMgr->GetLfgStatus(guid);

    if (GetPlayer()->GetGroup())
    {
        SendLfgUpdateParty(updateData);
        updateData.dungeons.clear();
        SendLfgUpdatePlayer(updateData);
    }
    else
    {
        SendLfgUpdatePlayer(updateData);
        updateData.dungeons.clear();
        SendLfgUpdateParty(updateData);
    }
}

void WorldSession::SendLfgUpdatePlayer(const LfgUpdateData& updateData)
{
#pragma warning "Found opcode"

    bool queued = false;
    uint8 size = uint8(updateData.dungeons.size());

    switch (updateData.updateType)
    {
        case LFG_UPDATETYPE_JOIN_QUEUE:
        case LFG_UPDATETYPE_ADDED_TO_QUEUE:
            queued = true;
            break;
        case LFG_UPDATETYPE_UPDATE_STATUS:
            queued = updateData.state == LFG_STATE_QUEUED;
            break;
        default:
            break;
    }

    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_UPDATE_PLAYER %s updatetype: %u",
        GetPlayer()->GetGUID(), updateData.updateType);
//    WorldPacket data(SMSG_LFG_UPDATE_PLAYER, 1 + 1 + (size > 0 ? 1 : 0) * (1 + 1 + 1 + 1 + size * 4 + updateData.comment.length()));
//    data << uint8(updateData.updateType);                  // Lfg Update type
//    data << uint8(size > 0);                               // Extra info
//    if (size)
//    {
//        data << uint8(queued);                             // Join the queue
//        data << uint8(0);                                  // unk - Always 0
//        data << uint8(0);                                  // unk - Always 0

//        data << uint8(size);
//        for (LfgDungeonSet::const_iterator it = updateData.dungeons.begin(); it != updateData.dungeons.end(); ++it)
//            data << uint32(*it);
//        data << updateData.comment;
//    }
//    SendPacket(&data);
}

void WorldSession::SendLfgUpdateParty(const LfgUpdateData& updateData)
{
    bool join = false;
    bool queued = false;
    uint8 size = uint8(updateData.dungeons.size());

    switch (updateData.updateType)
    {
        case LFG_UPDATETYPE_ADDED_TO_QUEUE:                // Rolecheck Success
            queued = true;
            // no break on purpose
        case LFG_UPDATETYPE_PROPOSAL_BEGIN:
            join = true;
            break;
        case LFG_UPDATETYPE_UPDATE_STATUS:
            join = updateData.state != LFG_STATE_ROLECHECK && updateData.state != LFG_STATE_NONE;
            queued = updateData.state == LFG_STATE_QUEUED;
            break;
        default:
            break;
    }

    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_UPDATE_PARTY %s updatetype: %u",
        GetPlayer()->GetGUID(), updateData.updateType);

    // Calls trace at every login that hits server performance hard
//    WorldPacket data(SMSG_LFG_UPDATE_PARTY, 1 + 1 + (size > 0 ? 1 : 0) * (1 + 1 + 1 + 1 + 1 + size * 4 + updateData.comment.length()));
//    data << uint8(updateData.updateType);                  // Lfg Update type
//    data << uint8(size > 0);                               // Extra info
//    if (size)
//    {
//        data << uint8(join);                               // LFG Join
//        data << uint8(queued);                             // Join the queue
//        data << uint8(0);                                  // unk - Always 0
//        data << uint8(0);                                  // unk - Always 0
//        for (uint8 i = 0; i < 3; ++i)
//            data << uint8(0);                              // unk - Always 0

//        data << uint8(size);
//        for (LfgDungeonSet::const_iterator it = updateData.dungeons.begin(); it != updateData.dungeons.end(); ++it)
//            data << uint32(*it);
//        data << updateData.comment;
//    }
//    SendPacket(&data);
}

void WorldSession::SendLfgRoleChosen(uint64 guid, uint8 roles)
{
    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_ROLE_CHOSEN %s guid: %u roles: %u",
        GetPlayer()->GetGUID(), GUID_LOPART(guid), roles);

    WorldPacket data(SMSG_LFG_ROLE_CHOSEN, 8 + 1 + 4);
    data << uint64(guid);                                  // Guid
    data << uint8(roles > 0);                              // Ready
    data << uint32(roles);                                 // Roles
    SendPacket(&data);
}

void WorldSession::SendLfgRoleCheckUpdate(const LfgRoleCheck& roleCheck)
{
    LfgDungeonSet dungeons;
    if (roleCheck.rDungeonId)
        dungeons.insert(roleCheck.rDungeonId);
    else
        dungeons = roleCheck.dungeons;

    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_ROLE_CHECK_UPDATE %s", GetPlayer()->GetGUID());
    WorldPacket data(SMSG_LFG_ROLE_CHECK_UPDATE, 4 + 1 + 1 + dungeons.size() * 4 + 1 + roleCheck.roles.size() * (8 + 1 + 4 + 1));

    data << uint32(roleCheck.state);                       // Check result
    data << uint8(roleCheck.state == LFG_ROLECHECK_INITIALITING);
    data << uint8(dungeons.size());                        // Number of dungeons
    if (!dungeons.empty())
    {
        for (LfgDungeonSet::iterator it = dungeons.begin(); it != dungeons.end(); ++it)
        {
            LFGDungeonData const* dungeon = sLFGMgr->GetLFGDungeon(*it);
            data << uint32(dungeon ? dungeon->Entry() : 0); // Dungeon
        }
    }

    data << uint8(roleCheck.roles.size());                 // Players in group
    if (!roleCheck.roles.empty())
    {
        // Leader info MUST be sent 1st :S
        uint64 guid = roleCheck.leader;
        uint8 roles = roleCheck.roles.find(guid)->second;
        data << uint64(guid);                              // Guid
        data << uint8(roles > 0);                          // Ready
        data << uint32(roles);                             // Roles
        Player* player = ObjectAccessor::FindPlayer(guid);
        data << uint8(player ? player->getLevel() : 0);    // Level

        for (LfgRolesMap::const_iterator it = roleCheck.roles.begin(); it != roleCheck.roles.end(); ++it)
        {
            if (it->first == roleCheck.leader)
                continue;

            guid = it->first;
            roles = it->second;
            data << uint64(guid);                          // Guid
            data << uint8(roles > 0);                      // Ready
            data << uint32(roles);                         // Roles
            player = ObjectAccessor::FindPlayer(guid);
            data << uint8(player ? player->getLevel() : 0);// Level
        }
    }
    SendPacket(&data);
}

void WorldSession::SendLfgJoinResult(uint64 jguid, uint32 queueID, const LfgJoinResultData& joinData)
{
    uint32 size = 0;
    for (LfgLockPartyMap::const_iterator it = joinData.lockmap.begin(); it != joinData.lockmap.end(); ++it)
        size += 8 + 4 + uint32(it->second.size()) * (4 + 4 + 4 + 4);

    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_JOIN_RESULT %s checkResult: %u checkValue: %u",
        GetPlayer()->GetGUID(), joinData.result, joinData.state);

    WorldPacket data(SMSG_LFG_JOIN_RESULT, 4 + 4 + size);

    ObjectGuid joinGuid(jguid);

    data << uint32(3);                                     // Unk
    data << uint8(joinData.result);                        // Check Result
    data << uint32(queueID);                               // Queue id
    data << uint8(joinData.state);                         // Check Value

    data
        .WriteByteMask(joinGuid[2])
        .WriteByteMask(joinGuid[7])
        .WriteByteMask(joinGuid[3])
        .WriteByteMask(joinGuid[0])
        .WriteUnaligned<24>(joinData.lockmap.size());

    for (LfgLockPartyMap::const_iterator it = joinData.lockmap.begin(); it != joinData.lockmap.end(); ++it)
    {
        ObjectGuid guid(it->first);

        data
            .WriteByteMask(guid[7])
            .WriteByteMask(guid[5])
            .WriteByteMask(guid[3])
            .WriteByteMask(guid[6])
            .WriteByteMask(guid[0])
            .WriteByteMask(guid[2])
            .WriteByteMask(guid[4])
            .WriteByteMask(guid[1])
            .WriteUnaligned<22>(it->second.size());
    }

    data
        .WriteByteMask(joinGuid[4])
        .WriteByteMask(joinGuid[5])
        .WriteByteMask(joinGuid[1])
        .WriteByteMask(joinGuid[6]);

    for (LfgLockPartyMap::const_iterator itr = joinData.lockmap.begin(); itr != joinData.lockmap.end(); ++itr)
    {
        LfgLockMap second = itr->second;
        for (LfgLockMap::const_iterator it = second.begin(); it != second.end(); ++it)
        {
            data << uint32(it->first);
            data << uint32(it->second);
            data << uint32(0); // needed ILvL
            data << uint32(0); // player ILvL
        }

        ObjectGuid guid(itr->first);

        data
            .WriteByteSeq(guid[2])
            .WriteByteSeq(guid[5])
            .WriteByteSeq(guid[1])
            .WriteByteSeq(guid[0])
            .WriteByteSeq(guid[4])
            .WriteByteSeq(guid[3])
            .WriteByteSeq(guid[6])
            .WriteByteSeq(guid[7]);
    }

    data
        .WriteByteSeq(joinGuid[1])
        .WriteByteSeq(joinGuid[4])
        .WriteByteSeq(joinGuid[3])
        .WriteByteSeq(joinGuid[5])
        .WriteByteSeq(joinGuid[0])
        .WriteByteSeq(joinGuid[7])
        .WriteByteSeq(joinGuid[2])
        .WriteByteSeq(joinGuid[6]);

    SendPacket(&data);
}

void WorldSession::SendLfgQueueStatus(const LfgQueueStatusData& queueData)
{
    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_QUEUE_STATUS %s dungeon: %u - waitTime: %d - avgWaitTime: %d - waitTimeTanks: %d - waitTimeHealer: %d - waitTimeDps: %d - queuedTime: %u - tanks: %u - healers: %u - dps: %u",
        GetPlayer()->GetGUID(), queueData.dungeonId, queueData.waitTime, queueData.waitTimeAvg, queueData.waitTimeTank, queueData.waitTimeHealer, queueData.waitTimeDps, queueData.queuedTime, queueData.tanks, queueData.healers, queueData.dps);

    ObjectGuid guid(GetPlayer()->GetGUID());

    WorldPacket data(SMSG_LFG_QUEUE_STATUS, 4 + 4 + 4 + 4 + 4 +4 + 1 + 1 + 1 + 4);

    data
        .WriteByteMask(guid[3])
        .WriteByteMask(guid[2])
        .WriteByteMask(guid[0])
        .WriteByteMask(guid[6])
        .WriteByteMask(guid[5])
        .WriteByteMask(guid[7])
        .WriteByteMask(guid[1])
        .WriteByteMask(guid[4])

        .WriteByteSeq(guid[0]);

    data << uint8(queueData.tanks);                        // Tanks needed
    data << int32(queueData.waitTimeTank);                 // Wait Tanks
    data << uint8(queueData.healers);                      // Healers needed
    data << int32(queueData.waitTimeHealer);               // Wait Healers
    data << uint8(queueData.dps);                          // Dps needed
    data << int32(queueData.waitTimeDps);                  // Wait Dps

    data
        .WriteByteSeq(guid[4])
        .WriteByteSeq(guid[6]);

    data << int32(queueData.waitTimeAvg);                  // Average Wait time
    data << int32(queueData.joinTime);                     // Join Time
    data << uint32(queueData.dungeonId);                   // Dungeon
    data << uint32(queueData.queuedTime);                  // Player wait time in queue

    data
        .WriteByteSeq(guid[5])
        .WriteByteSeq(guid[7])
        .WriteByteSeq(guid[3]);

    data << uint32(0);                                     // Queue Id

    data
        .WriteByteSeq(guid[1])
        .WriteByteSeq(guid[2]);

    data << int32(queueData.waitTime);                     // Wait Time
    data << int32(3);                                      // Same value than "Unk_UInt32_1" in SMSG_LFG_JOIN_RESULT - Only seen 3

    SendPacket(&data);
}

void WorldSession::SendLfgPlayerReward(uint32 rdungeonEntry, uint32 sdungeonEntry, uint8 done, const LfgReward* reward, const Quest* quest)
{
    if (!rdungeonEntry || !sdungeonEntry || !quest)
        return;

    uint8 totalRewardCount = uint8(quest->GetRewCurrencyCount() + quest->GetRewItemsCount());
    if (totalRewardCount > 16)
        totalRewardCount = 16;

    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_PLAYER_REWARD %s rdungeonEntry: %u - sdungeonEntry: %u - done: %u",
        GetPlayer()->GetGUID(), rdungeonEntry, sdungeonEntry, done);

    WorldPacket data(SMSG_LFG_PLAYER_REWARD, 4 + 4 + 1 + 4 + 4 + 4 + 4 + 4 + 1 + totalRewardCount * (4 + 4 + 4 + 1));
    data << uint32(rdungeonEntry);                         // Random Dungeon Finished
    data << uint32(sdungeonEntry);                         // Dungeon Finished
    data << uint32(quest->GetRewOrReqMoney());
    data << uint32(quest->XPValue(GetPlayer()));
    data << uint8(totalRewardCount);
    if (totalRewardCount)
    {
        for (uint8 i = 0; i < QUEST_REWARDS_COUNT; ++i)
        {
            if (uint32 itemId = quest->RewardItemId[i])
            {
                ItemTemplate const* item = sObjectMgr->GetItemTemplate(itemId);
                data << uint32(itemId);
                data << uint32(item ? item->DisplayInfoID : 0);
                data << uint32(quest->RewardItemIdCount[i]);
                data << uint8(false); // Is currency
            }
        }

        for (uint8 i = 0; i < QUEST_REWARD_CURRENCY_COUNT; ++i)
        {
            if (uint32 currencyId = quest->RewardCurrencyId[i])
            {
                data << uint32(currencyId);
                data << uint32(0);
                data << uint32(quest->RewardCurrencyCount[i]);
                data << uint8(true); // Is currency
            }
        }
    }
    SendPacket(&data);
}

void WorldSession::SendLfgBootProposalUpdate(const LfgPlayerBoot& boot)
{
    uint64 guid = GetPlayer()->GetGUID();
    LfgAnswer playerVote = boot.votes.find(guid)->second;
    uint8 votesNum = 0;
    uint8 agreeNum = 0;
    uint32 secsleft = uint8((boot.cancelTime - time(NULL)) / 1000);
    for (LfgAnswerContainer::const_iterator it = boot.votes.begin(); it != boot.votes.end(); ++it)
    {
        if (it->second != LFG_ANSWER_PENDING)
        {
            ++votesNum;
            if (it->second == LFG_ANSWER_AGREE)
                ++agreeNum;
        }
    }
    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_BOOT_PROPOSAL_UPDATE %s inProgress: %u - "
        "didVote: %u - agree: %u - victim: %u votes: %u - agrees: %u - left: %u - "
        "needed: %u - reason %s",
        GetPlayer()->GetGUID(), uint8(boot.inProgress), uint8(playerVote != LFG_ANSWER_PENDING),
        uint8(playerVote == LFG_ANSWER_AGREE), GUID_LOPART(boot.victim), votesNum, agreeNum,
        secsleft, LFG_GROUP_KICK_VOTES_NEEDED, boot.reason.c_str());
    WorldPacket data(SMSG_LFG_BOOT_PROPOSAL_UPDATE, 1 + 1 + 1 + 8 + 4 + 4 + 4 + 4 + boot.reason.length());
    data << uint8(boot.inProgress);                        // Vote in progress
    data << uint8(0);                                      // Succesed ? if inProgress = false then value in this field will lead to UI Error: false - ERR_PARTY_LFG_BOOT_VOTE_FAILED and true - ERR_PARTY_LFG_BOOT_VOTE_SUCCEEDE and 1
    data << uint8(playerVote != LFG_ANSWER_PENDING);       // I Voted
    data << uint8(playerVote == LFG_ANSWER_AGREE);         // My vote
    data << uint64(boot.victim);                           // Victim GUID
    data << uint32(votesNum);                              // Total Votes
    data << uint32(agreeNum);                              // Agree Count
    data << uint32(secsleft);                              // Time Left
    data << uint32(LFG_GROUP_KICK_VOTES_NEEDED);           // Needed Votes
    data << boot.reason.c_str();                           // Kick reason
    SendPacket(&data);
}

void WorldSession::SendLfgUpdateProposal(uint32 proposalId, LfgProposal const& proposal)
{
    ObjectGuid guid(GetPlayer()->GetGUID());
    ObjectGuid gguid(proposal.players.find(guid)->second.group);

    bool silent = !proposal.isNew && gguid == proposal.group;
    uint32 dungeonEntry = proposal.dungeonId;

    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_PROPOSAL_UPDATE %s state: %u",
        GetPlayer()->GetGUID(), proposal.state);
    WorldPacket data(SMSG_LFG_PROPOSAL_UPDATE, 4 + 1 + 4 + 4 + 1 + 1 + proposal.players.size() * (4 + 1 + 1 + 1 + 1 +1));

    // show random dungeon if player selected random dungeon and it's not lfg group
    if (!silent)
    {
        LfgDungeonSet const& playerDungeons = sLFGMgr->GetSelectedDungeons(guid);
        if (playerDungeons.find(proposal.dungeonId) == playerDungeons.end())
            dungeonEntry = (*playerDungeons.begin());
    }

    if (LFGDungeonData const* dungeon = sLFGMgr->GetLFGDungeon(dungeonEntry))
        dungeonEntry = dungeon->Entry();

    data << uint32(0);                                     // Date
    data << uint32(proposal.encounters);                   // Bosses killed mask - encounters done?
    data << uint32(3);                                     // Unk1
    data << uint32(0);                                     // Unk2
    data << uint32(dungeonEntry);                          // Dungeon
    data << uint32(0);                                     // Unk3 - proposalId ?
    data << uint8(proposal.state);                         // Proposal state

    data
        .WriteByteMask(gguid[4])
        .WriteByteMask(guid[3])
        .WriteByteMask(guid[7])
        .WriteByteMask(guid[0])
        .WriteByteMask(gguid[1])

        .WriteBit(silent)

        .WriteByteMask(guid[4])
        .WriteByteMask(guid[5])
        .WriteByteMask(gguid[3])

        .WriteUnaligned<23>(proposal.players.size())

        .WriteByteMask(gguid[7]);

    for (LfgProposalPlayerContainer::const_iterator it = proposal.players.begin(); it != proposal.players.end(); ++it)
    {
        LfgProposalPlayer const& player = it->second;

        data
            .WriteBit(player.group == proposal.group)      // In Dungeon
            .WriteBit(player.group == gguid)               // Same Group?
            .WriteBit(player.accept == LFG_ANSWER_AGREE)   // Accepted
            .WriteBit(player.accept != LFG_ANSWER_PENDING) // Answered
            .WriteBit(it->first == guid);                  // Self player
    }

    data
        .WriteByteMask(gguid[5])
        .WriteByteMask(guid[6])
        .WriteByteMask(gguid[2])
        .WriteByteMask(gguid[6])
        .WriteByteMask(guid[2])
        .WriteByteMask(guid[1])
        .WriteByteMask(gguid[0]);

    data
        .WriteByteSeq(guid[5])
        .WriteByteSeq(gguid[3])
        .WriteByteSeq(gguid[6])
        .WriteByteSeq(guid[6])
        .WriteByteSeq(guid[0])
        .WriteByteSeq(gguid[5])
        .WriteByteSeq(guid[1]);

    for (LfgProposalPlayerContainer::const_iterator it = proposal.players.begin(); it != proposal.players.end(); ++it)
    {
        data << uint32(it->second.role);                   // Role
    }

    data
        .WriteByteSeq(gguid[7])
        .WriteByteSeq(guid[4])
        .WriteByteSeq(gguid[0])
        .WriteByteSeq(gguid[1])
        .WriteByteSeq(guid[2])
        .WriteByteSeq(guid[7])
        .WriteByteSeq(gguid[2])
        .WriteByteSeq(guid[3])
        .WriteByteSeq(gguid[4]);

    SendPacket(&data);
}

void WorldSession::SendLfgLfrList(bool update)
{
    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_LFR_LIST %s update: %u",
        GetPlayer()->GetGUID(), update ? 1 : 0);
    WorldPacket data(SMSG_LFG_UPDATE_SEARCH, 1);
    data << uint8(update);                                 // In Lfg Queue?
    SendPacket(&data);
}

void WorldSession::SendLfgDisabled()
{
    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_DISABLED %s", GetPlayer()->GetGUID());
    WorldPacket data(SMSG_LFG_DISABLED, 0);
    SendPacket(&data);
}

void WorldSession::SendLfgOfferContinue(uint32 dungeonEntry)
{
    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_OFFER_CONTINUE %s dungeon entry: %u",
        GetPlayer()->GetGUID(), dungeonEntry);
    WorldPacket data(SMSG_LFG_OFFER_CONTINUE, 4);
    data << uint32(dungeonEntry);
    SendPacket(&data);
}

void WorldSession::SendLfgTeleportError(uint8 err)
{
    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_TELEPORT_DENIED %s reason: %u",
        GetPlayer()->GetGUID(), err);
    WorldPacket data(SMSG_LFG_TELEPORT_DENIED, 4);
    data << uint32(err);                                   // Error
    SendPacket(&data);
}

/*
void WorldSession::SendLfrUpdateListOpcode(uint32 dungeonEntry)
{
    sLog->outDebug(LOG_FILTER_PACKETIO, "SMSG_LFG_UPDATE_LIST %s dungeon entry: %u",
        GetPlayer()->GetGUID(), dungeonEntry);
    WorldPacket data(SMSG_LFG_UPDATE_LIST);
    SendPacket(&data);
}
*/
