// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/goal.h"

#include <string_view>

namespace coney::ai {

// The name of each goal type, as the code names it (the run log and the debug pages show it).
// @coney-only a label for the run log and the debug pages
std::string_view goalTypeName(GoalType type) {
    using enum GoalType;
    switch (type) {
    case Idle:
        return "Idle";
    case MoveToFlag:
        return "MoveToFlag";
    case MoveToExitFlag:
        return "MoveToExitFlag";
    case MoveToUseFlag:
        return "MoveToUseFlag";
    case MoveToHuman:
        return "MoveToHuman";
    case Melee:
        return "Melee";
    case EngageEnemy:
        return "EngageEnemy";
    case Fight:
        return "Fight";
    case Spectate:
        return "Spectate";
    case ReactGrabbing:
        return "ReactGrabbing";
    case ReactTackling:
        return "ReactTackling";
    case ReactGrabbed:
        return "ReactGrabbed";
    case ReactTackled:
        return "ReactTackled";
    case ReactKnockedDown:
        return "ReactKnockedDown";
    case ReactStunned:
        return "ReactStunned";
    case Block:
        return "Block";
    case HelpRespond:
        return "HelpRespond";
    case GrabTarget:
        return "GrabTarget";
    case PlayAnimation:
        return "PlayAnimation";
    case PlayDynAnimation:
        return "PlayDynAnimation";
    case PlayDynIdle:
        return "PlayDynIdle";
    case PlayGenAnim:
        return "PlayGenAnim";
    case GuardFlag:
        return "GuardFlag";
    case GetItem:
        return "GetItem";
    case DestroyItem:
        return "DestroyItem";
    case TrackHuman:
        return "TrackHuman";
    case FollowPlayer:
        return "FollowPlayer";
    case FollowFormation:
        return "FollowFormation";
    case FollowAndAttack:
        return "FollowAndAttack";
    case FollowAndDefend:
        return "FollowAndDefend";
    case HoldPosition:
        return "HoldPosition";
    case TravelPath:
        return "TravelPath";
    case LeftTurf:
        return "LeftTurf";
    case FindEnemy:
        return "FindEnemy";
    case AreaWalker:
        return "AreaWalker";
    case LeadChase:
        return "LeadChase";
    case RunCarrot:
        return "RunCarrot";
    case BumLogic:
        return "BumLogic";
    case Peddler:
        return "Peddler";
    case Riot:
        return "Riot";
    case AddressPerson:
        return "AddressPerson";
    case Tag:
        return "Tag";
    case Destroy:
        return "Destroy";
    case ThrowObject:
        return "ThrowObject";
    case Pedestrian:
        return "Pedestrian";
    case PedestrianReaction:
        return "PedestrianReaction";
    case CallGang:
        return "CallGang";
    case Scout:
        return "Scout";
    case Dealer:
        return "Dealer";
    case DealerReaction:
        return "DealerReaction";
    case Shopkeeper:
        return "Shopkeeper";
    case WarriorVandalSteal:
        return "WarriorVandalSteal";
    case BigBrawler:
        return "BigBrawler";
    case BigThrower:
        return "BigThrower";
    case BigLedgeThrower:
        return "BigLedgeThrower";
    case StationaryThrower:
        return "StationaryThrower";
    case Tired:
        return "Tired";
    case DevilRun:
        return "DevilRun";
    case Backoff:
        return "Backoff";
    case Boxer:
        return "Boxer";
    }
    return "?";
}

} // namespace coney::ai
