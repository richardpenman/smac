// Engine globals, bound to their original addresses (see memmap.h).
// Initially drafted from the Thinker mod's address table (MIT License); extended during
// reconstruction. Names are aliases for addresses: two names for the same address are harmless.
#pragma once
#include "engine/memmap.h"
#include "engine/types/all.h"

GLOBAL(int32_t, CurrentBaseID, 0x689370);
GLOBAL(int32_t, ComputeBaseID, 0x689374);  // changed whenever base_compute values are refreshed
GLOBAL(int32_t, CostRatios, 0x689378);
GLOBAL(int32_t, VehDrawAttackID, 0x689F28);
GLOBAL(int32_t, VehDrawDefendID, 0x689F2C);
GLOBAL(int32_t, CouncilNetA, 0x68BF38);
GLOBAL(int32_t, CouncilNetB, 0x68BF3C);
GLOBAL(int32_t, CouncilNetC, 0x68BF40);
GLOBAL(int32_t, dword_68F0F0, 0x68F0F0);
GLOBAL(int32_t, GameHalted, 0x68F21C);
GLOBAL(int32_t, MapHalfX, 0x68FAF0);
GLOBAL_NATIVE(CTerraform[20], Terraform, 0x691878);  // [20]
GLOBAL(int32_t, GameDialogFlags, 0x6970DC);
GLOBAL(int32_t, PopupDialogState, 0x6A721C);  // non-zero when most popups or scrollable lists are visible
GLOBAL(int32_t, BaseUpkeepDrawID, 0x6E8134);
GLOBAL(int32_t, ControlWaitLoop, 0x703DE0);
GLOBAL(int32_t, CouncilSessionPending, 0x703DE8);
GLOBAL(int32_t[8], CouncilProposal, 0x703DEC);  // [8]
GLOBAL(int32_t[8], CouncilVoteState, 0x703E0C);  // [8]
GLOBAL(int32_t, dword_73396C, 0x73396C);
GLOBAL(int32_t[8][8][36], DiploStateC, 0x7388D8);  // [8][8][36]
GLOBAL(int32_t, dword_7492CC, 0x7492CC);
GLOBAL(int32_t, FactionCurrentTotalMaint, 0x7492DC);
GLOBAL(int32_t, FactionCurrentBreakthroughTurns, 0x749AE4);
GLOBAL(int32_t, FactionCurrentGrossIncome, 0x749AE8);
GLOBAL(int32_t, FactionCurrentTotalLoan, 0x749C04);
GLOBAL(int32_t, FactionCurrentNetIncome, 0x749C0C);
GLOBAL(int32_t, dword_74DA9C, 0x74DA9C);  // ambience sound related
GLOBAL(int32_t, dword_7591A4, 0x7591A4);
GLOBAL(int32_t, dword_7591A8, 0x7591A8);
GLOBAL(int32_t[30], InfcList, 0x7AE778);  // [30]
GLOBAL(int32_t, InfcUsed, 0x7D392C);  // InfcPtrs[InfcList[InfcUsed]]
GLOBAL(char[256], GameCmdLine, 0x7D3970);
GLOBAL(int32_t, GameDrawState, 0x90D91C);
GLOBAL(int32_t, dword_90DB84, 0x90DB84);  // action_terraform
GLOBAL(int32_t, NetSetupType, 0x90E778);
GLOBAL(int32_t, BaseForcesSupported, 0x90E8FC);
GLOBAL(int32_t[4], BaseResourceConvoyFrom, 0x90E904);  // [4] for each resource type
GLOBAL(int32_t, BaseTerraformEnergy, 0x90E914);
GLOBAL(int32_t, BaseGrowthRate, 0x90E918);
GLOBAL(int32_t, BaseForcesMaintCost, 0x90E91C);
GLOBAL(int32_t[5], BasePsychNDrones, 0x90E920);  // [5] base psych rows
GLOBAL(int32_t, BaseUpkeepFlag, 0x90E934);
GLOBAL(int32_t[4], BaseResourceConvoyTo, 0x90E93C);  // [4] for each resource type
GLOBAL(int32_t[5], BasePsychSDrones, 0x90E94C);  // [5] base psych rows
GLOBAL(int32_t[8], BaseCommerceExport, 0x90E960);  // [8] for each faction trading with this base
GLOBAL(int32_t, BaseVehPacifismCount, 0x90E980);
GLOBAL(int32_t[5], BasePsychTalents, 0x90E984);  // [5] base psych rows
GLOBAL(int32_t, BaseTerraformReduce, 0x90E998);
GLOBAL(int32_t[25], BaseTileFlags, 0x90E99C);  // [25] for base radius but only first 21 used
GLOBAL(int32_t, BaseReservedTiles, 0x90EA00);
GLOBAL(int32_t, BaseFindDist, 0x90EA04);
GLOBAL(int32_t, BaseForcesMaintCount, 0x90EA08);
GLOBAL(int32_t[8], BaseCommerceImport, 0x90EA0C);  // [8] for each faction trading with this base
GLOBAL(int32_t, BaseUpkeepState, 0x90EA34);
GLOBAL(int32_t, dword_90EA3C, 0x90EA3C);
GLOBAL(int32_t, dword_90EA40, 0x90EA40);
GLOBAL(char[2][4][80], VehBattleDisplay, 0x90F554);  // [2][4][80]
GLOBAL(int32_t[2], VehBasicBattleMorale, 0x912420);  // [2] ; [0] offense, [1] defense
GLOBAL(int32_t[2][4], VehBattleModifier, 0x9155F0);  // [2][4]
GLOBAL(int32_t[2], VehBattleModCount, 0x915614);  // [2] ; [0] offense, [1] defense
GLOBAL(int32_t[2], VehBattleState, 0x91561C);  // [2] ; planet_busting() + boom() + timers
GLOBAL(int32_t, CurrentPlayerFaction, 0x939284);  // MapWin->cOwner
GLOBAL(int32_t, CanvasHeight, 0x939E58);
GLOBAL(int32_t, CanvasWidth, 0x939E5C);
GLOBAL(GameChecksum, MasterChecksum, 0x93A008);
GLOBAL(GameChecksum, RemoteChecksum, 0x93A510);
GLOBAL(int32_t, ControlUpkeepA, 0x93A934);
GLOBAL(int32_t, ControlTurnC, 0x93A938);
GLOBAL(int32_t, NetUpkeepCount, 0x93A93C);
GLOBAL(int32_t, dword_93A940, 0x93A940);
GLOBAL(int32_t, ControlTurnB, 0x93A948);
GLOBAL(int32_t, MultiDebugActive, 0x93A94C);
GLOBAL(int32_t, dword_93A950, 0x93A950);
GLOBAL(int32_t, dword_93A954, 0x93A954);
GLOBAL(int32_t, EnergyCredits, 0x93A958);
GLOBAL(int32_t, PbemActive, 0x93A95C);  // HotSeat / PBEM
GLOBAL(int32_t, dword_93A960, 0x93A960);
GLOBAL(int32_t, dword_93A968, 0x93A968);
GLOBAL(int32_t[8], FactionCombatWin, 0x93A96C);  // [8]
GLOBAL(int32_t[8], FactionCombatLoss, 0x93A98C);  // [8]
GLOBAL(int32_t, dword_93A9AC, 0x93A9AC);
GLOBAL(int32_t, dword_93A9B0, 0x93A9B0);
GLOBAL(int32_t, dword_93A9B4, 0x93A9B4);
GLOBAL(int32_t, dword_93A9B8, 0x93A9B8);
GLOBAL(int32_t, dword_93A9D8, 0x93A9D8);
GLOBAL(int32_t, VehAttackFlags, 0x93E904);
GLOBAL(int32_t, dword_93E960, 0x93E960);
GLOBAL(int32_t[8][103], DiploPlrList, 0x93E978);  // [8][103]
GLOBAL(int32_t, MultiplayerActive, 0x93F660);  // DirectPlay, Internet (TCP/IP)
GLOBAL(int32_t[8], DiploStateA, 0x93F678);  // [8]
GLOBAL(int32_t[8][8], DiploStateB, 0x93F698);  // [8][8]
GLOBAL(int32_t, DiploSocConflict, 0x93F7B4);  // social model conflict, SOCIAL dialog messages
GLOBAL(int32_t, DiploEnemyAlly, 0x93F7B8);  // player's ally that the rival has a vendetta with
GLOBAL(int32_t, DiploBattlePlayer, 0x93F7BC);  // player in the battle plans dialog
GLOBAL(int32_t, DiploTradeWantValue4, 0x93F7C0);  // tech_val of DiploTradeWantTech4 to the rival
GLOBAL(int32_t, DiploTradeDeclined, 0x93F7C4);  // player dismissed the tech trade popup
GLOBAL(int32_t, DiploMutualFaction, 0x93F7C8);  // known to both sides, rival has no pact or vendetta with it
GLOBAL(int32_t, DiploSecondFaction, 0x93F7CC);  // rival faction of the current dialog
GLOBAL(int32_t, DiploAlterFear, 0x93F7D0);  // rival is more defiant to fear when value is higher, possibly unused
GLOBAL(int32_t, DiploUnmetEnemy, 0x93F7D4);  // rival's enemy the player hasn't met, offered with a commlink
GLOBAL(int32_t, DiploPlayerEnemy, 0x93F7D8);  // player's enemy that the rival isn't at vendetta with
GLOBAL(int32_t, DiploPeaceFaction, 0x93F7DC);  // rival's enemy the player has a truce, treaty or pact with
GLOBAL(int32_t, DiploRivalPactCount, 0x93F7E0);  // number of the rival's pact partners
GLOBAL(int32_t, DiploSpokeTurn, 0x93F7E4);  // turn the player last spoke to the rival
GLOBAL(int32_t, DiploHelpFaction, 0x93F7E8);  // rival's enemy that the rival asks the player to attack
GLOBAL(int32_t, DiploTradeOfferValue2, 0x93F7EC);  // tech_val of DiploTradeOfferTech2 to the player
GLOBAL(int32_t, DiploRunawayLeader, 0x93F7F0);  // top faction's score is over 2x the player's, rival isn't top
GLOBAL(int32_t, DiploTradeTechProject, 0x93F7F4);  // secret project checks in tech_analysis
GLOBAL(int32_t, DiploRivalPactScore, 0x93F7F8);  // sum of ranking scores of the rival's pact partners
GLOBAL(int32_t, DiploRivalStrongPacts, 0x93F7FC);  // rival's pact partners ranked 5+ while it is below 5
GLOBAL(int32_t, DiploTradeOfferTech1, 0x93F800);  // best tech the rival could give the player
GLOBAL(int32_t, DiploBestUnitID, 0x93F804);  // rival's best prototype, -1 if none
GLOBAL(int32_t, DiploRivalWarCount, 0x93F808);  // contacted factions the rival has a vendetta with
GLOBAL(int32_t, DiploTradeOfferTech2, 0x93F80C);  // second-best tech the rival could give the player
GLOBAL(int32_t, DiploBattleRival, 0x93F810);  // rival in the battle plans dialog
GLOBAL(int32_t, DiploTradeWantValue1, 0x93F814);  // tech_val of DiploTradeWantTech1 to the rival
GLOBAL(int32_t[128], DiploRegionFactions, 0x93F818);  // [128] faction bitmask per region
GLOBAL(int32_t, DiploTradeWantTech2, 0x93FA18);  // second-best tech the player could give the rival
GLOBAL(int32_t, DiploTradeWantTech3, 0x93FA1C);  // third-best tech the player could give the rival
GLOBAL(int32_t, DiploTradeWantValue3, 0x93FA20);  // tech_val of DiploTradeWantTech3 to the rival
GLOBAL(int32_t, DiploCanSurrender, 0x93FA24);  // player defeated the rival, enables surrender pact
GLOBAL(int32_t, DiploTradeWantTech4, 0x93FA28);  // fourth-best tech the player could give the rival
GLOBAL(int32_t, DiploCapturedBases, 0x93FA2C);  // player's bases captured from the rival
GLOBAL(int32_t, DiploBidBaseSwapID, 0x93FA30);
GLOBAL(int32_t, DiploCurrentProposal, 0x93FA34);
GLOBAL(int32_t, DiploUnmetFaction, 0x93FA38);  // faction the player hasn't met, another contact or vendetta target
GLOBAL(int32_t, DiploGreatSatan, 0x93FA3C);  // player is a great_satan target, rival refuses pacts
GLOBAL(int32_t, DiploMentionUnitID, 0x93FA40);  // prototype the rival mentions, -1 if none
GLOBAL(int32_t, DiploTradeWantValue2, 0x93FA44);  // tech_val of DiploTradeWantTech2 to the rival
GLOBAL(int32_t, DiploMapsTraded, 0x93FA48);  // maps already traded, blocks further map offers
GLOBAL(int32_t, DiploEnemyAllyCount, 0x93FA4C);  // number of the player's allies the rival is fighting
GLOBAL(int32_t, DiploExposedBaseCount, 0x93FA50);  // player's empty bases with rival units adjacent
GLOBAL(int32_t, DiploPlayerPactCount, 0x93FA54);  // number of the player's pact partners
GLOBAL(int32_t, DiploOpenBaseID, 0x93FA58);  // rival's empty base next to the player's units
GLOBAL(int32_t, DiploProposalFaction, 0x93FA5C);  // faction for commlink trade or vendetta request proposal
GLOBAL(int32_t, DiploHostility, 0x93FA60);  // rival's hostility toward the player, 0 to 4
GLOBAL(int32_t, DiploNearEndgame, 0x93FA64);  // at_climax() result for the player
GLOBAL(int32_t, DiploPrevHostility, 0x93FA68);  // copy of DiploHostility before later overrides
GLOBAL(int32_t, DiploTradeOfferProject1, 0x93FA6C);  // secret project blocking DiploTradeOfferTech1, -1 if none
GLOBAL(int32_t, DiploCanBetray, 0x93FA70);
GLOBAL(int32_t, DiploFriction, 0x93FA74);
GLOBAL(int32_t, DiploReputation, 0x93FA78);
GLOBAL(int32_t, DiploAskBaseSwapID, 0x93FA7C);
GLOBAL(int32_t, DiploConquestFear, 0x93FA80);  // fear value kept only if the player took rival bases and has no pact
GLOBAL(int32_t, DiploTradeOfferProject2, 0x93FA84);  // secret project blocking DiploTradeOfferTech2, -1 if none
GLOBAL(int32_t, DiploTributeDemand, 0x93FA88);  // energy credits the rival demands from the player
GLOBAL(int32_t, DiploAcceptedFaction, 0x93FA8C);  // target of a rival request the player accepted
GLOBAL(int32_t, DiploRivalOutranks, 0x93FA90);  // rival's ranking is higher than the player's
GLOBAL(int32_t, DiploCommonEnemy, 0x93FA94);  // rival's enemy the player also has a vendetta with
GLOBAL(int32_t, DiploFearScale, 0x93FA98);  // rival's fear of the player, scales threaten demands
GLOBAL(int32_t, DiploGoodwill, 0x93FA9C);  // rival's goodwill toward the player, also used by propose_pact
GLOBAL(int32_t, DiploAttackChance, 0x93FAA0);  // rival's openings to attack the player
GLOBAL(int32_t, DiploTradeOfferValue1, 0x93FAA4);  // tech_val of DiploTradeOfferTech1 to the player
GLOBAL(int32_t, DiploTradeWantTech1, 0x93FAA8);  // best tech the player could give the rival
GLOBAL(int32_t, DiploCounterProposal, 0x93FAB0);
GLOBAL(int32_t, DiploWinState, 0x93FAB4);  // non-zero when diplomacy communication active
GLOBAL(int32_t, DiploFrictionFactionIDWith, 0x93FABC);  // player, the target of the cached friction
GLOBAL(int32_t, DiploFrictionFactionID, 0x93FAC0);  // rival, the owner of the cached friction
GLOBAL(int32_t, DiploCaptionOff, 0x93FAC8);
GLOBAL(int32_t, DiploCaptionMode, 0x93FACC);
GLOBAL(int32_t, DiploMenuFlagA, 0x93FAD0);
GLOBAL(int32_t, DiploMenuFlagB, 0x93FAD4);
GLOBAL(int32_t, DiploMapMsg, 0x93FAD8);
GLOBAL_NATIVE(Path, Paths, 0x945B00);
GLOBAL(int32_t, ProbeHasAlgoEnhancement, 0x945B30);
GLOBAL(int32_t, ProbeTargetFactionID, 0x945B34);
GLOBAL(int32_t, ProbeTargetHasHSA, 0x945B38);
GLOBAL(int32_t, SaveFileWrite, 0x945D6C);
GLOBAL(int32_t, SaveFileEncrypt, 0x945D74);
GLOBAL(int32_t, SaveFileMenu, 0x945D78);
GLOBAL(int32_t, SaveFileStatus, 0x945D7C);
GLOBAL(int32_t[8], FactionRankingsUnk, 0x945DD8);  // [8]
GLOBAL(int32_t, SkipTechScreenA, 0x945F40);  // non-zero skips popups, used in tech_achieved and tech_advance
GLOBAL(int32_t, SkipTechScreenB, 0x945F44);  // non-zero skips popups, used in base_change and tech_advance
GLOBAL(int32_t, NetUpkeepState, 0x945F48);
GLOBAL(CResourceInfo, ResInfo, 0x945F50);
GLOBAL_NATIVE(CCitizen[10], Citizen, 0x946020);  // [10]
GLOBAL(int32_t[8], TectonicDetonationCount, 0x946138);  // [8]
GLOBAL_NATIVE(CResourceName[4], ResName, 0x946158);  // [4]
GLOBAL_NATIVE(CCombatMode[3], OffenseMode, 0x946178);  // [3]
GLOBAL(CBonusName[41], BonusName, 0x9461A8);  // [41]
GLOBAL(CSocialParam[11], SocialParam, 0x946580);  // [11]
GLOBAL(int32_t, ReplayEventSize, 0x9469F8);
GLOBAL_NATIVE(CCombatMode[3], DefenseMode, 0x946A00);  // [3]
GLOBAL(MFaction[8], MFactions, 0x946A50);
GLOBAL(int32_t, TechValidCount, 0x949730);
GLOBAL(int32_t, TechCommerceCount, 0x949734);
GLOBAL(CRules, Rules, 0x949738);
GLOBAL(int32_t, MapAreaX, 0x949870);
GLOBAL(int32_t, MapAreaY, 0x949874);
GLOBAL(int32_t, MapRandomSeed, 0x949878);
GLOBAL(int32_t, MapSeaLevel, 0x94987C);
GLOBAL(int32_t, MapSeaLevelCouncil, 0x949880);
GLOBAL(int32_t, MapAreaTiles, 0x949884);
GLOBAL(int32_t, MapAreaSqRoot, 0x949888);
GLOBAL(int32_t, MapToggleFlat, 0x94988C);
GLOBAL(int32_t, MapLandmarkCount, 0x949890);
GLOBAL(Landmark[64], Landmarks, 0x949894);  // [64]
GLOBAL(int32_t, MapAbstractX, 0x94A294);
GLOBAL(int32_t, MapAbstractY, 0x94A298);
GLOBAL(int32_t, MapAbstractCount, 0x94A29C);
GLOBAL(int32_t, MapSizePlanet, 0x94A2A0);
GLOBAL(int32_t, MapOceanCoverage, 0x94A2A4);
GLOBAL(int32_t, MapLandCoverage, 0x94A2A8);  // 2 - MapOceanCoverage
GLOBAL(int32_t, MapErosiveForces, 0x94A2AC);
GLOBAL(int32_t, MapPlanetaryOrbit, 0x94A2B0);  // affects temperature
GLOBAL(int32_t, MapCloudCover, 0x94A2B4);  // affects rainfall, rivers
GLOBAL(int32_t, MapNativeLifeForms, 0x94A2B8);
GLOBAL(char[256], MapFilePath, 0x94A2BC);
GLOBAL_NATIVE(CEnergy[3], Energy, 0x94A318);  // [3]
GLOBAL_NATIVE(CChassis[9], Chassis, 0x94A330);  // [9]
GLOBAL_NATIVE(CNatural[16], Natural, 0x94ADE0);  // [16]
GLOBAL_NATIVE(CWeapon[26], Weapon, 0x94AE60);  // [26]
GLOBAL_NATIVE(CSocialField[4], SocialField, 0x94B000);  // [4]
GLOBAL_NATIVE(CMandate[4], Mandate, 0x94B4A0);  // [4]
GLOBAL(int32_t, ObjectiveReqVictory, 0x94B4C0);
GLOBAL(int32_t, ObjectivesSuddenDeathVictory, 0x94B4C4);
GLOBAL(int32_t, ObjectiveAchievePts, 0x94B4C8);
GLOBAL(int32_t, VictoryAchieveBonusPts, 0x94B4CC);
GLOBAL(int32_t, StartingMissionYear, 0x94B4D0);  // Normal starting year
GLOBAL(int32_t, EndingMissionYear, 0x94B4D4);  // Normal ending year for the difficulty level
GLOBAL_NATIVE(CMight[7], Might, 0x94C558);  // [7]
GLOBAL(Monument[8], Monuments, 0x94CA08);  // [8]
GLOBAL_NATIVE(CTimeControl[6], TimeControl, 0x94F1B8);  // [6]
GLOBAL_NATIVE(CArmor[14], Armor, 0x94F278);  // [14]
GLOBAL_NATIVE(CTech[89], Tech, 0x94F358);  // [89]
GLOBAL(CWorldbuilder, WorldBuilder, 0x9502A8);
GLOBAL(ReplayEvent[1024], ReplayEvents, 0x950324);  // [1024]
GLOBAL_NATIVE(CMorale[7], Morale, 0x952328);  // [7]
GLOBAL_NATIVE(CReactor[4], Reactor, 0x9527F8);  // [4]
GLOBAL(VEH[2049], VehsDef, 0x952828);  // [2049]
GLOBAL_NATIVE(COrder[30], Order, 0x96C878);  // [30]
GLOBAL(Faction[8], Factions, 0x96C9E0);
GLOBAL(BASE[512], Bases, 0x97D040);
GLOBAL_NATIVE(CFacility[134], Facility, 0x9A4B68);  // [134]
GLOBAL(int32_t, ExpansionEnabled, 0x9A6488);
GLOBAL(GamePrefs, cs, 0x9A6490);
GLOBAL(int32_t, GamePreferences, 0x9A6490);
GLOBAL(int32_t, GameMorePreferences, 0x9A6494);
GLOBAL(int32_t, GameWarnings, 0x9A6498);
GLOBAL(int32_t, GameRules, 0x9A649C);
GLOBAL(int32_t, GameInterludeState, 0x9A64A4);
GLOBAL(int32_t, TutWinMapState, 0x9A64B4);
GLOBAL(int32_t, GameState, 0x9A64C0);
GLOBAL(int32_t, DiffLevel, 0x9A64C4);
GLOBAL(int32_t, VehCount, 0x9A64C8);
GLOBAL(int32_t, BaseCount, 0x9A64CC);
GLOBAL(int32_t, CurrentTurn, 0x9A64D4);
GLOBAL(int32_t, CurrentMissionYear, 0x9A64D8);
GLOBAL(int32_t, CurrentVehID, 0x9A64DC);
GLOBAL(int32_t, GameTimeControl, 0x9A64E0);
GLOBAL(uint8_t[4], FactionStatus, 0x9A64E8);  // [0] human players bitmask, [1] alive factions bitmask
GLOBAL(int32_t[8], FactionRankings, 0x9A64EC);  // [rank] faction id (0: lowest)
GLOBAL(int32_t, RankingFactionIDUnk1, 0x9A650C);
GLOBAL(int32_t, RankingFactionIDUnk2, 0x9A6510);
GLOBAL(int32_t[64], SecretProjects, 0x9A6514);  // [64] base id of each secret project (-1: not built)
GLOBAL(int32_t, GovernorFaction, 0x9A6614);
GLOBAL(int32_t[11], ProposalPassCount, 0x9A6618);  // [11]
GLOBAL(int32_t[11], ProposalCallTurn, 0x9A6644);  // [11]
GLOBAL(uint8_t[89], TechOwners, 0x9A6670);  // [89] faction bitmask per tech
GLOBAL(char[256], LastSavePath, 0x9A66C9);
GLOBAL(int32_t, ClimateValueA, 0x9A67CC);
GLOBAL(int32_t, ClimateValueB, 0x9A67D0);
GLOBAL(int32_t, ClimateValueC, 0x9A67D4);
GLOBAL(int32_t, ClimateFutureChange, 0x9A67D8);
GLOBAL(int32_t, ScoreBonusPts, 0x9A67DC);
GLOBAL(int32_t[8], ScorePopTotal, 0x9A67E0);  // [8]
GLOBAL(int32_t, SunspotDuration, 0x9A6800);
GLOBAL(int32_t, DustCloudDuration, 0x9A680C);
GLOBAL(int32_t, SaveFileVersion, 0x9A6810);
GLOBAL(int32_t, ScnVictFacilityObj, 0x9A6814);
GLOBAL(int32_t, SolarFlaresEvent, 0x9A6818);
GLOBAL(int32_t, GameMoreRules, 0x9A681C);
GLOBAL(int32_t, CurrentFaction, 0x9A6820);  // active faction for turn processing
GLOBAL(int32_t, ClimateLevel, 0x9A6824);
GLOBAL_NATIVE(CProposal[11], Proposal, 0x9A6828);  // [11]
GLOBAL(int16_t[1000][8], FactionTurnMight, 0x9A68AC);  // [turn][faction] ranking score history
GLOBAL(Continent[128], Continents, 0x9AA730);  // [128]
GLOBAL_NATIVE(CAbility[29], Ability, 0x9AB538);  // [29]
GLOBAL(UNIT[512], Units, 0x9AB868);
GLOBAL(int32_t, ControlTurnA, 0x9B2068);
GLOBAL(int32_t, GameVictoryType, 0x9B206C);
GLOBAL(int32_t, ControlTurnMove, 0x9B2070);
GLOBAL(int32_t, VehLiftX, 0x9B2278);
GLOBAL(int32_t, VehFindDist, 0x9B227C);
GLOBAL(int32_t, VehDropLiftVehID, 0x9B2280);
GLOBAL(int32_t, VehLiftY, 0x9B2284);
GLOBAL(int32_t, VehBitError, 0x9B228C);
GLOBAL(int32_t[8], MapBaseSubmergedCount, 0x9B2290);  // [8]; reset each time global alt changes
GLOBAL(int32_t, BrushVal1, 0x9B22B0);
GLOBAL(int32_t, WorldBuildVal1, 0x9B22B4);
GLOBAL(int32_t, BrushVal2, 0x9B22B8);
GLOBAL(int32_t[8], MapBaseIdClosestSubmergedVeh, 0x9B22BC);  // [8]; reset each time global alt changes
GLOBAL(int32_t, dword_9B22E0, 0x9B22E0);  // action_terraform
GLOBAL(int32_t, WorldAddTemperature, 0x9B22E8);
GLOBAL(int32_t, WorldSkipTerritory, 0x9B22EC);
GLOBAL(int32_t, WinModalState, 0x9B7AE4);
GLOBAL(int32_t, ScreenWidth, 0x9B7B1C);
GLOBAL(int32_t, ScreenHeight, 0x9B7B20);
GLOBAL(char[256], StrBuffer, 0x9B86A0);
GLOBAL(int32_t[10], ParseStrPlurality, 0x9BB570);  // [10] parse_say plural flags
GLOBAL(int32_t[10], ParseNumTable, 0x9BB598);  // [10] parse_num values ($NUMn)
GLOBAL(int32_t[10], ParseStrGender, 0x9BB5C0);  // [10] parse_say genders
GLOBAL(char[10][256], ParseStrBuffer, 0x9BB5E8);  // [10] parse_say strings ($STRn)
GLOBAL(int32_t, GenderDefault, 0x9BBFEC);
GLOBAL(int32_t, PluralDefault, 0x9BBFF0);
GLOBAL(int32_t, GameLanguage, 0x9BC054);
GLOBAL(int32_t, DialogChoices, 0x9BC06C);
GLOBAL(int32_t, DialogToggle, 0x9BC070);

// Heap-allocated map arrays (pointers: native storage).
GLOBAL_NATIVE(MAP*, MapTiles, 0x94A30C);
GLOBAL_NATIVE(uint8_t*, MapAbstract, 0x94A310);
GLOBAL(VEH[2049], Vehs, 0x952828);  // [2048] + 1 scratch slot used by veh_fake

// Setup / preferences
GLOBAL(int32_t, MapGenerate, 0x945820);  // set by size_of_planet: world_build must generate a map
GLOBAL(AlphaIniPref, AlphaIniPrefs, 0x94B464);
GLOBAL(DefaultPref, DefaultPrefs, 0x94B350);
