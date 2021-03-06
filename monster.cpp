////////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
////////////////////////////////////////////////////////////////////////
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
////////////////////////////////////////////////////////////////////////
#include "otpch.h"

#include "monster.h"
#include "spawn.h"
#include "monsters.h"

#include "spells.h"
#include "combat.h"

#include "configmanager.h"
#include "game.h"
#include "creatureevent.h"

#include <sstream>
#include <string>

extern Game g_game;
extern ConfigManager g_config;
extern Monsters g_monsters;
extern CreatureEvents* g_creatureEvents;

AutoList<Monster>Monster::autoList;
#ifdef __ENABLE_SERVER_DIAGNOSTIC__
uint32_t Monster::monsterCount = 0;
#endif

Monster* Monster::createMonster(MonsterType* mType)
{
	return new Monster(mType);
}

Monster* Monster::createMonster(const std::string& name)
{
	MonsterType* mType = g_monsters.getMonsterType(name);
	if(!mType)
		return NULL;

	return createMonster(mType);
}

Monster::Monster(MonsterType* _mType):
	Creature()
{
#ifdef __ENABLE_SERVER_DIAGNOSTIC__
	monsterCount++;
#endif
	mType = _mType;
			
	if(!mType->nameVector.empty())
	{
		const nameBlock_t& nb = mType->nameVector[random_range(0, mType->nameVector.size() - 1)];
		{
			mType->name = nb.name;
			mType->nameDescription = nb.nameDescription;
			_mType->name = nb.name;
			_mType->nameDescription = nb.nameDescription;
			name = _mType->name;
			nameDescription = _mType->nameDescription;
		}
	}
	else
	{
		name = _mType->name;
		nameDescription = _mType->nameDescription;
	}
			
			
	isIdle = true;
	isMasterInRange = false;
	teleportToMaster = false;
	setStorage("baseaggro", mType->baseAggro);
	setStorage("distaggro", mType->distAggro);
	setStorage("tileaggro", mType->tileAggro);
	setStorage("threatmult", mType->threatNormal);
	setStorage("threathealthmult", mType->threatHealth);
	setStorage("threatmanamult", mType->threatMana);
	setStorage("threatbarriermult", mType->threatBarrier);
	setStorage("threat", "0");
	setStorage("group", mType->group);
	setStorage("view", mType->viewRange);
	spawn = NULL;
	raid = NULL;
	defaultOutfit = mType->outfit;
	currentOutfit = mType->outfit;

	double multiplier = g_config.getDouble(ConfigManager::RATE_MONSTER_HEALTH);
	
	health = (int32_t)(mType->health * multiplier);
	healthMin = mType->healthMin, healthMax = mType->healthMax;
	if(healthMin > 0)
		healthMax = random_range(healthMin, healthMax);
	healthMax = (int32_t)(healthMax * multiplier);
	healthRegen = mType->healthRegen;
		
	mana = (int32_t)(mType->mana);
	manaMin = mType->manaMin, manaMax = mType->manaMax;
	if(manaMin > 0)
		manaMax = random_range(manaMin, manaMax);
	manaMax = (int32_t)(manaMax);
	manaRegen = mType->manaRegen;
	
	shield = (int32_t)(mType->shield * multiplier);
	shieldMin = mType->shieldMin, shieldMax = mType->shieldMax;
	if(shieldMin > 0)
		shieldMax = random_range(shieldMin, shieldMax);

	shieldMax = (int32_t)(shieldMax * multiplier);
	shieldRegen = mType->shieldRegen;
	
	barrier = (int32_t)(mType->barrier * multiplier);
	barrierMin = mType->barrierMin, barrierMax = mType->barrierMax;
	if(barrierMin > 0)
		barrierMax = random_range(barrierMin, barrierMax);

	barrierMax = (int32_t)(barrierMax * multiplier);
	
	baseSpeed = mType->baseSpeed;
	internalLight.level = mType->lightLevel;
	internalLight.color = mType->lightColor;
	setSkull(mType->skull);
	setPShield(mType->partyShield);
	setEmblem(mType->guildEmblem);

	hideName = mType->hideName, hideHealth = mType->hideHealth;

	minCombatValue = 0;
	maxCombatValue = 0;

	lastDamage = 0;
	targetTicks = 0;
	targetChangeTicks = 0;
	targetChangeCooldown = 0;
	attackTicks = 0;
	defenseTicks = 0;
	yellTicks = 0;
	extraMeleeAttack = false;

	// register creature events
	for(StringVec::iterator it = mType->scriptList.begin(); it != mType->scriptList.end(); ++it)
	{
		if(!registerCreatureEvent(*it))
			std::clog << "[Warning - Monster::Monster] Unknown event name - " << *it << std::endl;
	}
}

Monster::~Monster()
{
	clearTargetList();
	clearFriendList();
#ifdef __ENABLE_SERVER_DIAGNOSTIC__

	monsterCount--;
#endif
	if(raid)
	{
		raid->unRef();
		raid = NULL;
	}
}

void Monster::onTarget(Creature* target)
{
	Creature::onTarget(target);
	if(isSummon())
		master->onSummonTarget(this, target);
}

void Monster::onTargetDisappear(bool)
{
#ifdef __DEBUG__
	std::clog << "Attacked creature disappeared." << std::endl;
#endif
	attackTicks = 0;
	extraMeleeAttack = true;
	if(g_config.getBool(ConfigManager::MONSTER_SPAWN_WALKBACK))
		g_game.steerCreature(this, masterPosition, 5000);
}

void Monster::onTargetDrain(Creature* target, int32_t points)
{
	Creature::onTargetDrain(target, points);
	if(isSummon())
		master->onSummonTargetDrain(this, target, points);
}

void Monster::onCreatureAppear(const Creature* creature)
{
	Creature::onCreatureAppear(creature);
	if(creature == this)
	{
		CreatureEventList spawnEvents = getCreatureEvents(CREATURE_EVENT_SPAWN_SINGLE);
		for(CreatureEventList::iterator it = spawnEvents.begin(); it != spawnEvents.end(); ++it)
			(*it)->executeSpawn(this);

		g_creatureEvents->monsterSpawn(this);
		//We just spawned lets look around to see who is there.
		if(isSummon())
			isMasterInRange = canSee(master->getPosition());

		
		updateTargetList();
		updateIdleStatus();
	}
	else
		onCreatureEnter(const_cast<Creature*>(creature));
}

void Monster::onCreatureDisappear(const Creature* creature, bool isLogout)
{
	Creature::onCreatureDisappear(creature, isLogout);
	if(creature == this)
	{
		if(spawn)
			spawn->startEvent();

		setIdle(true);
	}
	else
		onCreatureLeave(const_cast<Creature*>(creature));
}

void Monster::onCreatureMove(const Creature* creature, const Tile* newTile, const Position& newPos,
	const Tile* oldTile, const Position& oldPos, bool teleport)
{
	Creature::onCreatureMove(creature, newTile, newPos, oldTile, oldPos, teleport);
	if(creature == this)
	{
		if(isSummon())
			isMasterInRange = canSee(master->getPosition());

		updateTargetList();
		updateIdleStatus();
	}
	else
	{
		bool canSeeNewPos = canSee(newPos), canSeeOldPos = canSee(oldPos);
		if(canSeeNewPos && !canSeeOldPos)
			onCreatureEnter(const_cast<Creature*>(creature));
		else if(!canSeeNewPos && canSeeOldPos)
			onCreatureLeave(const_cast<Creature*>(creature));

		if(isSummon() && master == creature && canSeeNewPos) //Turn the summon on again
			isMasterInRange = true;

		updateIdleStatus();
		if(!followCreature && !isSummon() && isOpponent(creature)) //we have no target lets try pick this one
			selectTarget(const_cast<Creature*>(creature));
	}
}

void Monster::updateTargetList()
{
	CreatureList::iterator it;
	for(it = friendList.begin(); it != friendList.end();)
	{
		if((*it)->getHealth() <= 0 || !canSee((*it)->getPosition()))
		{
			(*it)->unRef();
			it = friendList.erase(it);
		}
		else
			++it;
	}

	for(it = targetList.begin(); it != targetList.end();)
	{

		std::string changer; 
		getStorage(std::to_string((*it)->getID()), changer);
		int32_t value = atoi(changer.c_str());
		getStorage("baseaggro", changer);
		int32_t aggro = atoi(changer.c_str());
		int32_t threat;
		getThreat(*it, threat);
		if (((value+threat) < aggro) && (!g_config.getBool(ConfigManager::MONSTER_TARGET_DEFAULT)))
		{
			(*it)->unRef();
			it = targetList.erase(it);
		}
		else if((*it)->getHealth() <= 0 || !canSee((*it)->getPosition()))
		{
			(*it)->unRef();
			it = targetList.erase(it);
		}
		else
			++it;
	}
	SpectatorVec list;
	int32_t monstView = Map::maxViewportX;
	std::string changer;
	getStorage("view", changer);
	int32_t value = atoi(changer.c_str());
	if (value > monstView)
		monstView = value;

	g_game.getSpectators(list, getPosition(), false, false, monstView, monstView, monstView, monstView);
	for(SpectatorVec::const_iterator it = list.begin(); it != list.end(); ++it)
	{
		if((*it) != this && canSee((*it)->getPosition()))
			onCreatureFound(*it);
	}
}

void Monster::clearTargetList()
{
	for(CreatureList::iterator it = targetList.begin(); it != targetList.end(); ++it)
		(*it)->unRef();

	targetList.clear();
}

void Monster::clearFriendList()
{
	for(CreatureList::iterator it = friendList.begin(); it != friendList.end(); ++it)
		(*it)->unRef();

	friendList.clear();
}

void Monster::onCreatureFound(Creature* creature, bool pushFront /*= false*/)
{
	if(isFriend(creature))
	{
		assert(creature != this);
		if(std::find(friendList.begin(), friendList.end(), creature) == friendList.end())
		{
			creature->addRef();
			friendList.push_back(creature);
		}
	}

	if(isOpponent(creature))
	{
		assert(creature != this);
		if(std::find(targetList.begin(), targetList.end(), creature) == targetList.end())
		{
			if(g_config.getBool(ConfigManager::MONSTER_TARGET_DEFAULT))
			{
				creature->addRef();
				if(pushFront)
					targetList.push_front(creature);
				else
					targetList.push_back(creature);
			}
			else
			{
				std::string changer;
				getStorage(std::to_string((creature)->getID()), changer);
				int aggro = atoi(changer.c_str());
				getStorage("baseaggro", changer);
				int baseAggro = atoi(changer.c_str());
				int32_t threat;
				getThreat(creature, threat);
				if((aggro+threat) > baseAggro)
				{
					creature->addRef();
					if(pushFront)
						targetList.push_front(creature);
					else
						targetList.push_back(creature);
				}
			}
		}
	}

	updateIdleStatus();
}

void Monster::onCreatureEnter(Creature* creature)
{
	if(master == creature) //Turn the summon on again
	{
		isMasterInRange = true;
		updateIdleStatus();
	}

	onCreatureFound(creature, true);
}

bool Monster::isFriend(const Creature* creature)
{
	if(g_config.getBool(ConfigManager::MONSTER_TARGET_DEFAULT))
	{
		if(!isSummon() || !master->getPlayer())
			return creature->getMonster() && !creature->isSummon();

		const Player* tmpPlayer = NULL;
		if(creature->getPlayer())
			tmpPlayer = creature->getPlayer();
		else if(creature->getPlayerMaster())
			tmpPlayer = creature->getPlayerMaster();

		const Player* masterPlayer = master->getPlayer();
		return tmpPlayer && (tmpPlayer == masterPlayer || masterPlayer->isPartner(tmpPlayer) || masterPlayer->isAlly(tmpPlayer));
	}
	else
	{
		bool friendly = false;
		std::string mygroup;
		creature->getStorage("group", mygroup);
		if(creature->getPlayer() && mType->groupPlayer == 1)
			friendly = true;
		else if(!creature->getPlayer())
		{
			if(mType->groupType == 1 || (mType->group == mygroup))
				friendly = true;

			for(GroupList::iterator it = mType->groupList.begin(); it != mType->groupList.end(); ++it)
			{
				if(it->groupname == mygroup && it->status == 2)
					friendly = false;
					
				if(it->groupname == mygroup && it->status == 1)
					friendly = true;
					
				if(it->groupname == mygroup && it->status == 0)
					friendly = false;
			}

		}
		if(friendly)
			return true;

		return false;
	}
}

bool Monster::isOpponent(const Creature* creature)
{
	if(g_config.getBool(ConfigManager::MONSTER_TARGET_DEFAULT))
	{
		return (isSummon() && master->getPlayer() && creature != master) || ((creature->getPlayer()
			&& !creature->getPlayer()->hasFlag(PlayerFlag_IgnoredByMonsters)) || creature->getPlayerMaster());
	}
	else
	{
		bool enemy = false;
		std::string mygroup;
		creature->getStorage("group", mygroup);
		if(creature->getPlayer() && creature->getPlayer()->hasFlag(PlayerFlag_IgnoredByMonsters))
			enemy = false;
		else if(creature->getPlayer() && mType->groupPlayer == 2)
			enemy = true;
		else if(!creature->getPlayer())
		{
			if(mType->groupType == 2 && (!(mType->group == mygroup)))
				enemy = true;

			for(GroupList::iterator it = mType->groupList.begin(); it != mType->groupList.end(); ++it)
			{
				if(it->groupname == mygroup && it->status == 2)
					enemy = true;
					
				if(it->groupname == mygroup && it->status == 1)
					enemy = false;
					
				if(it->groupname == mygroup && it->status == 0)
					enemy = false;
			}

		}
		if(enemy)
		{

			//std::string changer;
			//getStorage(std::to_string(creature->getID()), changer);
			//int32_t value = atoi(changer.c_str());
			//getStorage("baseaggro", changer);
			//int32_t aggro = atoi(changer.c_str());
			//return (value > aggro);
			return true;
		}
		else
			return false;
	}
}

bool Monster::doTeleportToMaster()
{
	const Position& tmp = getPosition();
	if(g_game.internalTeleport(this, g_game.getClosestFreeTile(this,
		master->getPosition(), true), true) != RET_NOERROR)
		return false;

	g_game.addMagicEffect(tmp, MAGIC_EFFECT_POFF);
	g_game.addMagicEffect(getPosition(), MAGIC_EFFECT_TELEPORT);
	return true;
}

void Monster::onCreatureLeave(Creature* creature)
{
#ifdef __DEBUG__
	std::clog << "onCreatureLeave - " << creature->getName() << std::endl;
#endif
	if(isSummon() && master == creature)
	{
		if(!g_config.getBool(ConfigManager::TELEPORT_SUMMONS) && (!master->getPlayer()
			|| !g_config.getBool(ConfigManager::TELEPORT_PLAYER_SUMMONS)))
		{
			//Turn the monster off until its master comes back
			isMasterInRange = false;
			updateIdleStatus();
		}
		else if(!doTeleportToMaster())
			teleportToMaster = true;
	}

	//update friendList
	if(isFriend(creature))
	{
		CreatureList::iterator it = std::find(friendList.begin(), friendList.end(), creature);
		if(it != friendList.end())
		{
			(*it)->unRef();
			friendList.erase(it);
		}
#ifdef __DEBUG__
		else
			std::clog << "Monster: " << creature->getName() << " not found in the friendList." << std::endl;
#endif
	}

	//update targetList
	if(isOpponent(creature))
	{
		CreatureList::iterator it = std::find(targetList.begin(), targetList.end(), creature);
		if(it != targetList.end())
		{
			(*it)->unRef();
			targetList.erase(it);
			if(targetList.empty())
				updateIdleStatus();
		}
#ifdef __DEBUG__
		else
			std::clog << "Player: " << creature->getName() << " not found in the targetList." << std::endl;
#endif
	}
}

bool Monster::searchTarget(TargetSearchType_t searchType /*= TARGETSEARCH_DEFAULT*/)
{
#ifdef __DEBUG__
	std::clog << "Searching target... " << std::endl;
#endif
	std::list<Creature*> resultList;
	const Position& myPos = getPosition();
	for(CreatureList::iterator it = targetList.begin(); it != targetList.end(); ++it)
	{
		if(followCreature != (*it) && isTarget(*it) && (searchType == TARGETSEARCH_RANDOM
			|| canUseAttack(myPos, *it)))
			resultList.push_back(*it);
	}

	switch(searchType)
	{
		case TARGETSEARCH_NEAREST:
		{
			Creature* target = NULL;
			int32_t range = -1;
			for(CreatureList::iterator it = resultList.begin(); it != resultList.end(); ++it)
			{
				int32_t tmp = std::max(std::abs(myPos.x - (*it)->getPosition().x),
					std::abs(myPos.y - (*it)->getPosition().y));
				if(range >= 0 && tmp >= range)
					continue;

				target = *it;
				range = tmp;
			}

			if(target && selectTarget(target))
				return true;

			break;
		}
		case TARGETSEARCH_AGGRO:
		{
			int32_t taggro = atoi((mType->baseAggro).c_str());
			std::string changer;
			if(followCreature) {
				getStorage(std::to_string((followCreature)->getID()), changer);
				taggro = atoi(changer.c_str()); }
				
			int32_t value = 0;
			int32_t threat = 0;
			Creature* target = NULL;
			for(CreatureList::iterator it = targetList.begin(); it != targetList.end(); ++it)
			{
				getStorage(std::to_string((*it)->getID()), changer);
				value = atoi(changer.c_str());
				getThreat(*it, threat);
				if((value+threat) > taggro && (*it) != followCreature) {
					target = *it;
					taggro = value;
				}
			}
			if(target) {
				setStorage(std::to_string((target)->getID()), std::to_string(taggro*10));
				selectTarget(target);
			}
			
			return true;
		}
		default:
		{
			if(!resultList.empty())
			{
				CreatureList::iterator it = resultList.begin();
				std::advance(it, random_range(0, resultList.size() - 1));
#ifdef __DEBUG__

				std::clog << "Selecting target " << (*it)->getName() << std::endl;
#endif
				return selectTarget(*it);
			}

			if(searchType == TARGETSEARCH_ATTACKRANGE)
				return false;

			break;
		}
	}


	//lets just pick the first target in the list
	for(CreatureList::iterator it = targetList.begin(); it != targetList.end(); ++it)
	{
		if(followCreature == (*it) || !selectTarget(*it))
			continue;

#ifdef __DEBUG__
		/*std::clog << "Selecting target " << (*it)->getName() << std::endl;*/ // Caused a strange crash, will look at it later
#endif
		return true;
	}

	return false;
}

void Monster::onFollowCreatureComplete(const Creature* creature)
{
	if(!creature)
		return;

	CreatureList::iterator it = std::find(targetList.begin(), targetList.end(), creature);
	if(it != targetList.end())
	{
		Creature* target = (*it);
		targetList.erase(it);

		if(hasFollowPath) //push target we have found a path to the front
			targetList.push_front(target);
		else if(!isSummon()) //push target we have not found a path to the back
			targetList.push_back(target);
		else //Since we removed the creature from the targetList (and not put it back) we have to release it too
			target->unRef();
	}
}

BlockType_t Monster::blockHit(Creature* attacker, CombatType_t combatType, int32_t& damage,
	bool checkDefense/* = false*/, bool checkArmor/* = false*/, bool/* reflect = true*/, bool/* field = false*/, bool/* element = false*/)
{
	int32_t aggroGain = (int32_t)std::ceil(damage * mType->damageAggro);
	std::string changer;
	getStorage(std::to_string((attacker)->getID()), changer);
	int32_t taggro = atoi(changer.c_str());
	setStorage(std::to_string((attacker)->getID()), std::to_string((aggroGain+taggro)));
	
	BlockType_t blockType = Creature::blockHit(attacker, combatType, damage, checkDefense, checkArmor);
	if(!damage)
		return blockType;

	int32_t elementMod = 0;
	ElementMap::iterator it = mType->elementMap.find(combatType);
	if(it != mType->elementMap.end())
		elementMod = it->second;

	if(!elementMod)
		return blockType;

	damage = (int32_t)std::ceil(damage * ((float)(100 - elementMod) / 100));
	if(damage > 0)
		return blockType;

	damage = 0;
	blockType = BLOCK_DEFENSE;
	return blockType;
}

bool Monster::isTarget(Creature* creature)
{
	return (!creature->isRemoved() && creature->isAttackable() && creature->getZone() != ZONE_PROTECTION
		&& canSeeCreature(creature) && creature->getPosition().z == getPosition().z);
}

bool Monster::selectTarget(Creature* creature)
{
#ifdef __DEBUG__
	std::clog << "Selecting target... " << std::endl;
#endif
	if(!isTarget(creature) || std::find(targetList.begin(),
		targetList.end(), creature) == targetList.end())
	{
		//Target not found in our target list.
#ifdef __DEBUG__
		std::clog << "Target not found in targetList." << std::endl;
#endif
		return false;
	}

	if((isHostile() || isSummon()) && setAttackedCreature(creature) && !isSummon())
		Dispatcher::getInstance().addTask(createTask(
			boost::bind(&Game::checkCreatureAttack, &g_game, getID())));
	
	return setFollowCreature(creature, true);
}

void Monster::setIdle(bool _idle)
{
	if(isRemoved() || getHealth() <= 0)
		return;

	isIdle = _idle;
	if(isIdle)
	{
		onIdleStatus();
		clearTargetList();
		clearFriendList();
		g_game.removeCreatureCheck(this);
	}
	else
		g_game.addCreatureCheck(this);
}

void Monster::updateIdleStatus()
{
	bool idle = true;
	std::string changer;
	getStorage("activethreat", changer);
	int32_t activethreat = atoi(changer.c_str());
	if(mType->alwaysactive == 1)
		idle = false;
	else
	{
		//if(isSummon())
		//{
		//	if((isMasterInRange && teleportToMaster) || (!master->getMonster() && !master->getMonster()->getIdleStatus())) //This Line Crashes Server
		//		idle = false;
		//}
		SpectatorVec list;
		int32_t monstView = Map::maxViewportX;
		getStorage("view", changer);
		int32_t value = atoi(changer.c_str());
		if (value > monstView)
			monstView = value;

		g_game.getSpectators(list, getPosition(), false, false, monstView, monstView, monstView, monstView);
		
		for(SpectatorVec::const_iterator it = list.begin(); it != list.end(); ++it)
		{
			if((*it) != this && canSee((*it)->getPosition()) && (((*it)->getPlayer() && !(*it)->getPlayer()->hasFlag(PlayerFlag_IgnoredByMonsters))))
				idle=false;
		
		}
	}
	if(idle == true && activethreat > 0)
	{
		activethreat = activethreat-1;
		setStorage("activethreat", std::to_string(activethreat));
		idle = false; 
	}
	setIdle(idle);
}

void Monster::onAddCondition(ConditionType_t type, bool hadCondition)
{
	Creature::onAddCondition(type, hadCondition);
	//the walkCache need to be updated if the monster becomes "resistent" to the damage, see Tile::__queryAdd()
	updateMapCache();
	updateIdleStatus();
}

void Monster::onEndCondition(ConditionType_t type)
{
	Creature::onEndCondition(type);
	//the walkCache need to be updated if the monster loose the "resistent" to the damage, see Tile::__queryAdd()
	updateMapCache();
	updateIdleStatus();
}

void Monster::onThink(uint32_t interval)
{
	Creature::onThink(interval);
	if(despawn())
	{
		g_game.removeCreature(this, true);
		setIdle(true);
		return;
	}
		
	//Regeneration System
	Condition* condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_REGENERATION, 1000);
	if(healthRegen > 0) {
	condition->setParam(CONDITIONPARAM_HEALTHGAIN, healthRegen);
	condition->setParam(CONDITIONPARAM_HEALTHTICKS, 1000); }
	if(manaRegen > 0) {
	condition->setParam(CONDITIONPARAM_MANAGAIN, manaRegen);
	condition->setParam(CONDITIONPARAM_MANATICKS, 1000); }
	if(shieldRegen > 0) {
	condition->setParam(CONDITIONPARAM_SHIELDGAIN, shieldRegen);
	condition->setParam(CONDITIONPARAM_SHIELDTICKS, 1000); }
	addCondition(condition);

	updateIdleStatus();
	if(!g_config.getBool(ConfigManager::MONSTER_TARGET_DEFAULT))
		updateTargetList();

	if(isIdle)
		return;
		
	if(!g_config.getBool(ConfigManager::MONSTER_TARGET_DEFAULT)) {
		std::string changer;
		getStorage("activethreat", changer);
		int32_t activethreat = atoi(changer.c_str());
		if(activethreat < 60)
			activethreat = (activethreat + 1);
		setStorage("activethreat", std::to_string(activethreat));
		//Aggro System
		SpectatorVec list;
		int32_t monstView = Map::maxViewportX;
		getStorage("view", changer);
		int32_t value = atoi((changer).c_str());
		if (value > monstView)
			monstView = value;

		getStorage("distaggro", changer);
		int32_t distAggro = atoi((changer).c_str());
		getStorage("tileaggro", changer);
		int32_t tileAggro = atoi((changer).c_str());
		
		g_game.getSpectators(list, getPosition(), false, false, monstView, monstView, monstView, monstView);

		for(SpectatorVec::const_iterator it = list.begin(); it != list.end(); ++it) {
			if(isOpponent(*it)) {
				std::string changer;
				
				getStorage(std::to_string((*it)->getID()), changer);
				int32_t taggro = atoi((changer).c_str());
				taggro = std::ceil(taggro*0.75);
				int32_t distBetween = std::max(std::abs(((*it)->getPosition().x) - getPosition().x), std::abs(((*it)->getPosition().y) - getPosition().y));
				distBetween = distAggro-distBetween;
				if(distBetween != 0) {
					int32_t addAggro = std::ceil((distBetween*tileAggro)+taggro);
					if(addAggro < 0)
						addAggro = 0;
					setStorage(std::to_string((*it)->getID()), std::to_string(addAggro));
				}
			}
		}
	}
		
	if(teleportToMaster && doTeleportToMaster())
		teleportToMaster = false;

	addEventWalk();

	if(isSummon())
	{
		if(!attackedCreature)
		{
			if(master && master->getAttackedCreature()) //This happens if the monster is summoned during combat
				selectTarget(master->getAttackedCreature());
			else if(master != followCreature) //Our master has not ordered us to attack anything, lets follow him around instead.
				setFollowCreature(master);
		}
		else if(attackedCreature == this)
			setFollowCreature(NULL);
		else if(followCreature != attackedCreature) //This happens just after a master orders an attack, so lets follow it aswell.
			setFollowCreature(attackedCreature);
	}
	else if(!targetList.empty())
	{
		if(!g_config.getBool(ConfigManager::MONSTER_TARGET_DEFAULT))
			searchTarget(TARGETSEARCH_AGGRO);
		else if(!followCreature || !hasFollowPath)
			searchTarget();
		else if(isFleeing() && attackedCreature && !canUseAttack(getPosition(), attackedCreature))
			searchTarget(TARGETSEARCH_ATTACKRANGE);
	}
	onThinkTarget(interval);
	onThinkYell(interval);
}

void Monster::onAttacking(uint32_t interval)
{
	Creature::onAttacking(interval);
	doHealing(interval);
}

void Monster::doAttacking(uint32_t interval)
{
	if(!attackedCreature || (isSummon() && attackedCreature == this))
		return;

	bool updateLook = true;
	resetTicks = (interval != 0);
	attackTicks += interval;

	const Position& myPos = getPosition();
	for(SpellList::iterator it = mType->spellAttackList.begin(); it != mType->spellAttackList.end(); ++it)
	{
		if(!attackedCreature || attackedCreature->isRemoved())
			break;

		const Position& targetPos = attackedCreature->getPosition();
		if(it->isMelee && isFleeing())
			continue;

		bool inRange = false;
		if(canUseSpell(myPos, targetPos, *it, interval, inRange))
		{
			if(it->chance >= (uint32_t)random_range(1, 100))
			{

				double multiplier;
				if(maxCombatValue > 0) //defense
					multiplier = g_config.getDouble(ConfigManager::RATE_MONSTER_DEFENSE);
				else //attack
					multiplier = g_config.getDouble(ConfigManager::RATE_MONSTER_ATTACK);

				minCombatValue = (int32_t)(it->minCombatValue * multiplier);
				maxCombatValue = (int32_t)(it->maxCombatValue * multiplier);

				it->spell->castSpell(this, attackedCreature);
				if(it->isMelee)
					extraMeleeAttack = false;
#ifdef __DEBUG__

				static uint64_t prevTicks = OTSYS_TIME();
				std::clog << "doAttacking ticks: " << OTSYS_TIME() - prevTicks << std::endl;
				prevTicks = OTSYS_TIME();
#endif
			}
		}

		if(!inRange && it->isMelee) //melee swing out of reach
			extraMeleeAttack = true;
	}


	if(resetTicks)
		attackTicks = 0;
}

bool Monster::canUseAttack(const Position& pos, const Creature* target) const
{
	if(!isHostile())
		return true;
		
	const Position& targetPos = target->getPosition();
	for(SpellList::iterator it = mType->spellAttackList.begin(); it != mType->spellAttackList.end(); ++it)
	{
		if((*it).range != 0 && std::max(std::abs(pos.x - targetPos.x), std::abs(pos.y - targetPos.y)) <= (int32_t)(*it).range)
			return g_game.isSightClear(pos, targetPos, true);
	}

	return false;
}

bool Monster::canUseSpell(const Position& pos, const Position& targetPos,
	const spellBlock_t& sb, uint32_t interval, bool& inRange)
{
	inRange = true;

	if(!sb.isMelee || !extraMeleeAttack)
	{
		if(sb.speed > attackTicks)
		{
			resetTicks = false;
			return false;
		}

		if(attackTicks % sb.speed >= interval) //already used this spell for this round
			return false;
	}

	
		
	
	if(!sb.range || std::max(std::abs(pos.x - targetPos.x), std::abs(pos.y - targetPos.y)) <= (int32_t)sb.range)
	{
		if(sb.health)
			changeHealth(-sb.health);
			
		if(!sb.mana)
			return true;
		else if(sb.mana < this->getMana())
		{	
			changeMana(-sb.mana);
			return true;
		}
	}

	inRange = false;
	return false;
}

void Monster::onThinkTarget(uint32_t interval)
{
	if(isSummon() || mType->changeTargetSpeed <= 0 || !g_config.getBool(ConfigManager::MONSTER_TARGET_DEFAULT))
		return;

	bool canChangeTarget = true;
	if(targetChangeCooldown > 0)
	{
		targetChangeCooldown -= interval;
		if(targetChangeCooldown <= 0)
		{
			targetChangeCooldown = 0;
			targetChangeTicks = (uint32_t)mType->changeTargetSpeed;
		}
		else
			canChangeTarget = false;
	}

	if(!canChangeTarget)
		return;

	targetChangeTicks += interval;
	if(targetChangeTicks < (uint32_t)mType->changeTargetSpeed)
		return;

	targetChangeTicks = 0;
	targetChangeCooldown = (uint32_t)mType->changeTargetSpeed;
	if(mType->changeTargetChance < random_range(1, 100))
		return;
	
	std::string changer;
	int32_t value = 0;
	if(!getStorage("targetdistance", changer))
		value = mType->targetDistance;
	else
		value = atoi((changer).c_str());
		
	if(value <= 1)
		searchTarget(TARGETSEARCH_RANDOM);
	else
		searchTarget(TARGETSEARCH_NEAREST); 
}

void Monster::doHealing(uint32_t interval)
{
	resetTicks = true;
	defenseTicks += interval;
	for(SpellList::iterator it = mType->spellDefenseList.begin(); it != mType->spellDefenseList.end(); ++it)
	{
		if(it->speed > defenseTicks)
		{
			if(resetTicks)
				resetTicks = false;

			continue;
		}

		if(defenseTicks % it->speed >= interval) //already used this spell for this round
			continue;

		if((it->chance >= (uint32_t)random_range(1, 100)))
		{
			minCombatValue = it->minCombatValue;
			maxCombatValue = it->maxCombatValue;
			it->spell->castSpell(this, this);
		}
	}

	if(!isSummon())
	{
		if(mType->maxSummons < 0 || (int32_t)summons.size() < mType->maxSummons)
		{
			for(SummonList::iterator it = mType->summonList.begin(); it != mType->summonList.end(); ++it)
			{
				if((int32_t)summons.size() >= mType->maxSummons)
					break;

				if(it->interval > defenseTicks)
				{
					if(resetTicks)
						resetTicks = false;

					continue;
				}

				if(defenseTicks % it->interval >= interval)
					continue;

				uint32_t typeCount = 0;
				for(CreatureList::iterator cit = summons.begin(); cit != summons.end(); ++cit)
				{
					if(!(*cit)->isRemoved() && (*cit)->getName() == it->name)
						++typeCount;
				}

				if(typeCount >= it->amount)
					continue;

				if((it->chance >= (uint32_t)random_range(1, 100)))
				{
					if(Monster* summon = Monster::createMonster(it->name))
					{
						addSummon(summon);
						if(g_game.placeCreature(summon, getPosition()))
						{
							g_game.addMagicEffect(getPosition(), MAGIC_EFFECT_WRAPS_BLUE);
							g_game.addMagicEffect(summon->getPosition(), MAGIC_EFFECT_TELEPORT);
						}
						else
							removeSummon(summon);
					}
				}
			}
		}
	}

	if(resetTicks)
		defenseTicks = 0;
}

void Monster::onThinkYell(uint32_t interval)
{
	if(mType->yellSpeedTicks <= 0)
		return;

	yellTicks += interval;
	if(yellTicks < mType->yellSpeedTicks)
		return;

	yellTicks = 0;
	if(mType->voiceVector.empty() || (mType->yellChance < (uint32_t)random_range(1, 100)))
		return;

	const voiceBlock_t& vb = mType->voiceVector[random_range(0, mType->voiceVector.size() - 1)];
	if(vb.yellText)
		g_game.internalCreatureSay(this, MSG_SPEAK_MONSTER_YELL, vb.text, false);
	else
		g_game.internalCreatureSay(this, MSG_SPEAK_MONSTER_SAY, vb.text, false);
}

bool Monster::pushItem(Item* item, int32_t radius)
{
	const Position& centerPos = item->getPosition();
	PairVector pairVector;

	pairVector.push_back(PositionPair(-1, -1));
	pairVector.push_back(PositionPair(-1, 0));
	pairVector.push_back(PositionPair(-1, 1));
	pairVector.push_back(PositionPair(0, -1));
	pairVector.push_back(PositionPair(0, 1));
	pairVector.push_back(PositionPair(1, -1));
	pairVector.push_back(PositionPair(1, 0));
	pairVector.push_back(PositionPair(1, 1));

	std::random_shuffle(pairVector.begin(), pairVector.end());
	Position tryPos;
	for(int32_t n = 1; n <= radius; ++n)
	{
		for(PairVector::iterator it = pairVector.begin(); it != pairVector.end(); ++it)
		{
			int32_t dx = it->first * n, dy = it->second * n;
			tryPos = centerPos;

			tryPos.x = tryPos.x + dx;
			tryPos.y = tryPos.y + dy;

			Tile* tile = g_game.getTile(tryPos);
			if(tile && g_game.canThrowObjectTo(centerPos, tryPos) && g_game.internalMoveItem(this, item->getParent(),
				tile, INDEX_WHEREEVER, item, item->getItemCount(), NULL) == RET_NOERROR)
				return true;
		}
	}

	return false;
}

void Monster::pushItems(Tile* tile)
{
	TileItemVector* items = tile->getItemList();
	if(!items)
		return;

	//We cannot use iterators here since we can push the item to another tile
	//which will invalidate the iterator.
	//start from the end to minimize the amount of traffic
	int32_t moveCount = 0, removeCount = 0, downItemsSize = items->getDownItemCount();
	Item* item = NULL;
	for(int32_t i = downItemsSize - 1; i >= 0; --i)
	{
		assert(i >= 0 && i < downItemsSize);
		if((item = items->at(i)) && item->hasProperty(MOVABLE) &&
			(item->hasProperty(BLOCKPATH) || item->hasProperty(BLOCKSOLID)))
		{
			if(moveCount < 20 && pushItem(item, 1))
				moveCount++;
			else if(g_game.internalRemoveItem(this, item) == RET_NOERROR)
				++removeCount;
		}
	}

	if(removeCount > 0)
		g_game.addMagicEffect(tile->getPosition(), MAGIC_EFFECT_POFF);
}

bool Monster::pushCreature(Creature* creature)
{
	DirVector dirVector;
	dirVector.push_back(NORTH);
	dirVector.push_back(SOUTH);
	dirVector.push_back(WEST);
	dirVector.push_back(EAST);

	std::random_shuffle(dirVector.begin(), dirVector.end());
	Position monsterPos = creature->getPosition();

	Tile* tile = NULL;
	for(DirVector::iterator it = dirVector.begin(); it != dirVector.end(); ++it)
	{
		if((tile = g_game.getTile(Spells::getCasterPosition(creature, (*it)))) && !tile->hasProperty(
			BLOCKPATH) && g_game.internalMoveCreature(creature, (*it)) == RET_NOERROR)
			return true;
	}

	return false;
}

void Monster::pushCreatures(Tile* tile)
{
	if(CreatureVector* creatures = tile->getCreatures())
	{
		uint32_t removeCount = 0;
		for(uint32_t i = 0; i < creatures->size();)
		{
			Monster* monster = creatures->at(i)->getMonster();
			if(monster && monster->isPushable())
			{
				monster->changeHealth(-monster->getHealth());
				removeCount++;
			}
			++i;
		}

		if(removeCount > 0)
			g_game.addMagicEffect(tile->getPosition(), MAGIC_EFFECT_POFF);
	}
}

bool Monster::getNextStep(Direction& dir, uint32_t& flags)
{
	if(getHealth() <= 0 || cannotMove)
	{
		//we dont have anyone watching might aswell stop walking
		eventWalk = 0;
		return false;
	}

	bool result = false;
	if((!followCreature || !hasFollowPath) && !isSummon())
	{
		if(followCreature || getTimeSinceLastMove() > 1000) //choose a random direction
			result = getRandomStep(getPosition(), dir);
	}
	else if(isSummon() || followCreature)
	{
		result = Creature::getNextStep(dir, flags);
		if(!result)
		{
			//target dancing
			if(attackedCreature && attackedCreature == followCreature)
			{
				if(isFleeing())
					result = getDanceStep(getPosition(), dir, false, false);
				else 
				{
					updateLookDirection();
					if(mType->staticAttackChance < (uint32_t)random_range(1, 100))
						result = getDanceStep(getPosition(), dir);
				}
			}
		}
		else
			flags |= FLAG_PATHFINDING;
	}

	if(result && (canPushItems() || canPushCreatures()))
	{
		if(Tile* tile = g_game.getTile(Spells::getCasterPosition(this, dir)))
		{
			if(canPushItems())
				pushItems(tile);

			if(canPushCreatures())
				pushCreatures(tile);
		}
#ifdef __DEBUG__
		else
			std::clog << "[Warning - Monster::getNextStep] no tile found." << std::endl;
#endif
	}

	return result;
}

bool Monster::getRandomStep(const Position& creaturePos, Direction& dir)
{
	DirVector dirVector;
	dirVector.push_back(NORTH);
	dirVector.push_back(SOUTH);
	dirVector.push_back(WEST);
	dirVector.push_back(EAST);

	std::random_shuffle(dirVector.begin(), dirVector.end());
	for(DirVector::iterator it = dirVector.begin(); it != dirVector.end(); ++it)
	{
		if(!canWalkTo(creaturePos, *it))
			continue;

		dir = *it;
		return true;
	}

	return false;
}

bool Monster::getDanceStep(const Position& creaturePos, Direction& dir,
	bool keepAttack /*= true*/, bool keepDistance /*= true*/)
{
	bool canDoAttackNow = canUseAttack(creaturePos, attackedCreature);

	assert(attackedCreature != NULL);
	const Position& centerPos = attackedCreature->getPosition();
	uint32_t centerToDist = std::max(std::abs(creaturePos.x - centerPos.x), std::abs(creaturePos.y - centerPos.y));
	uint32_t tmpDist;

	DirVector dirVector;

	if(!keepDistance || creaturePos.y - centerPos.y >= 0)
	{
		tmpDist = std::max(std::abs((creaturePos.x) - centerPos.x), std::abs((creaturePos.y - 1) - centerPos.y));
		if(tmpDist == centerToDist && canWalkTo(creaturePos, NORTH))
		{
			bool result = true;
			if(keepAttack)
				result = (!canDoAttackNow || canUseAttack(Position(creaturePos.x, creaturePos.y - 1, creaturePos.z), attackedCreature));

			if(result)
				dirVector.push_back(NORTH);
		}
	}

	if(!keepDistance || creaturePos.y - centerPos.y <= 0)
	{
		tmpDist = std::max(std::abs((creaturePos.x) - centerPos.x), std::abs((creaturePos.y + 1) - centerPos.y));
		if(tmpDist == centerToDist && canWalkTo(creaturePos, SOUTH))
		{
			bool result = true;
			if(keepAttack)
				result = (!canDoAttackNow || canUseAttack(Position(creaturePos.x, creaturePos.y + 1, creaturePos.z), attackedCreature));

			if(result)
				dirVector.push_back(SOUTH);
		}
	}

	if(!keepDistance || creaturePos.x - centerPos.x <= 0)
	{
		tmpDist = std::max(std::abs((creaturePos.x + 1) - centerPos.x), std::abs(creaturePos.y - centerPos.y));
		if(tmpDist == centerToDist && canWalkTo(creaturePos, EAST))
		{
			bool result = true;
			if(keepAttack)
				result = (!canDoAttackNow || canUseAttack(Position(creaturePos.x + 1, creaturePos.y, creaturePos.z), attackedCreature));

			if(result)
				dirVector.push_back(EAST);
		}
	}

	if(!keepDistance || creaturePos.x - centerPos.x >= 0)
	{
		tmpDist = std::max(std::abs((creaturePos.x - 1) - centerPos.x), std::abs(creaturePos.y - centerPos.y));
		if(tmpDist == centerToDist && canWalkTo(creaturePos, WEST))
		{
			bool result = true;
			if(keepAttack)
				result = (!canDoAttackNow || canUseAttack(Position(creaturePos.x - 1, creaturePos.y, creaturePos.z), attackedCreature));

			if(result)
				dirVector.push_back(WEST);
		}
	}

	if(dirVector.empty())
		return false;

	std::random_shuffle(dirVector.begin(), dirVector.end());
	dir = dirVector[random_range(0, dirVector.size() - 1)];
	return true;
}

bool Monster::isInSpawnRange(const Position& toPos)
{
	return masterRadius == -1 || !inDespawnRange(toPos);
}

bool Monster::canWalkTo(Position pos, Direction dir)
{
	switch(dir)
	{
		case NORTH:
			pos.y += -1;
			break;
		case WEST:
			pos.x += -1;
			break;
		case EAST:
			pos.x += 1;
			break;
		case SOUTH:
			pos.y += 1;
			break;
		default:
			break;
	}

	if(!isInSpawnRange(pos) || !getWalkCache(pos))
		return false;

	Tile* tile = g_game.getTile(pos);
	if(!tile || g_game.isSwimmingPool(NULL, getTile(), false) != g_game.isSwimmingPool(NULL, tile, false)) // prevent monsters entering/exiting to swimming pool
		return false;

	// If we don't follow, or attack, and we can't handle the damage, then we can't move on this field
	MagicField* field = NULL;
	if(!followCreature && !attackedCreature && (field = tile->getFieldItem()) && !isImmune(field->getCombatType()))
		return false;

	return !tile->getTopVisibleCreature(this) && tile->__queryAdd(
		0, this, 1, FLAG_PATHFINDING) == RET_NOERROR;
}

bool Monster::onDeath()
{
	if(!Creature::onDeath())
		return false;

	//destroySummons();
	clearTargetList();
	clearFriendList();

	setAttackedCreature(NULL);
	onIdleStatus();
	if(raid)
	{
		raid->unRef();
		raid = NULL;
	}

	g_game.removeCreature(this, false);
	return true;
}

Item* Monster::createCorpse(DeathList deathList)
{
	Item* corpse = Creature::createCorpse(deathList);
	if(!corpse)
		return NULL;

	if(master)
	{
		corpse->setAttribute("summon", true);
		return corpse;
	}

	if(mType->corpseUnique)
		corpse->setUniqueId(mType->corpseUnique);

	if(mType->corpseAction)
		corpse->setActionId(mType->corpseAction, false);

	if(deathList[0].isNameKill())
		return corpse;

	Creature* _owner = deathList[0].getKillerCreature();
	if(deathList.size() > 1 && deathList[1].getDamage() > deathList[0].getDamage())
		_owner = deathList[1].getKillerCreature();

	if(!_owner)
		return corpse;

	Player* owner = NULL;
	if(_owner->getPlayer())
		owner = _owner->getPlayer();
	else if(_owner->isPlayerSummon())
		owner = _owner->getPlayerMaster();

	if(!owner)
		return corpse;

	uint64_t stamina = g_config.getNumber(ConfigManager::STAMINA_DESTROY_LOOT);
	if(stamina && owner->getStamina() <= (stamina * STAMINA_MULTIPLIER))
		lootDrop = LOOT_DROP_NONE;

	corpse->setCorpseOwner(owner->getGUID());
	return corpse;
}

bool Monster::inDespawnRange(const Position& pos)
{
	if(!spawn || mType->isLureable)
		return false;

	int32_t radius = g_config.getNumber(ConfigManager::DEFAULT_DESPAWNRADIUS);
	if(!radius)
		return false;

	if(!Spawns::getInstance()->isInZone(masterPosition, radius, pos))
		return true;

	int32_t range = g_config.getNumber(ConfigManager::DEFAULT_DESPAWNRANGE);
	if(!range)
		return false;

	return std::abs(pos.z - masterPosition.z) > range;
}

bool Monster::despawn()
{
	return inDespawnRange(getPosition());
}

bool Monster::getCombatValues(int32_t& min, int32_t& max)
{
	if(!minCombatValue && !maxCombatValue)
		return false;

	double multiplier;
	if(maxCombatValue > 0) //defense
		multiplier = g_config.getDouble(ConfigManager::RATE_MONSTER_DEFENSE);
	else //attack
		multiplier = g_config.getDouble(ConfigManager::RATE_MONSTER_ATTACK);

	min = (int32_t)(minCombatValue * multiplier);
	max = (int32_t)(maxCombatValue * multiplier);
	return true;
}

bool Monster::getThreat(Creature* creature, int32_t& value)
{
	if(!creature)
		return false;
		
	std::string changer;

	(creature)->getStorage("threatmult", changer);
	int32_t threatmult = atoi(changer.c_str());
	(creature)->getStorage("threathealthmult", changer);
	int32_t threathealthmult = atoi(changer.c_str());
	(creature)->getStorage("threatmanamult", changer);
	int32_t threatmanamult = atoi(changer.c_str());
	(creature)->getStorage("threatbarriermult", changer);
	int32_t threatbarriermult = atoi(changer.c_str());
	
	int32_t threat = 0;
	(creature)->getStorage("threat", changer);
	threat = threat + (atoi(changer.c_str())*threatmult);
	(creature)->getStorage("threathealth", changer);
	threat = threat + (atoi(changer.c_str())*threathealthmult);
	(creature)->getStorage("threatmana", changer);
	threat = threat + (atoi(changer.c_str())*threatmanamult);
	(creature)->getStorage("threatbarrier", changer);
	threat = threat + (atoi(changer.c_str())*threatbarriermult);
	
	value = threat;
	return true;
}

void Monster::updateLookDirection()
{
	Direction newDir = getDirection();
	if(attackedCreature)
	{
		const Position& pos = getPosition();
		const Position& attackedCreaturePos = attackedCreature->getPosition();

		int32_t dx = attackedCreaturePos.x - pos.x, dy = attackedCreaturePos.y - pos.y;
		if(std::abs(dx) > std::abs(dy))
		{
			//look EAST/WEST
			if(dx < 0)
				newDir = WEST;
			else
				newDir = EAST;
		}
		else if(std::abs(dx) < std::abs(dy))
		{
			//look NORTH/SOUTH
			if(dy < 0)
				newDir = NORTH;
			else
				newDir = SOUTH;
		}
		else if(dx < 0 && dy < 0)
		{
			if(getDirection() == SOUTH)
				newDir = WEST;
			else if(getDirection() == EAST)
				newDir = NORTH;
		}
		else if(dx < 0 && dy > 0)
		{
			if(getDirection() == NORTH)
				newDir = WEST;
			else if(getDirection() == EAST)
				newDir = SOUTH;
		}
		else if(dx > 0 && dy < 0)
		{
			if(getDirection() == SOUTH)
				newDir = EAST;
			else if(getDirection() == WEST)
				newDir = NORTH;
		}
		else if(getDirection() == NORTH)
			newDir = EAST;
		else if(getDirection() == WEST)
			newDir = SOUTH;
	}

	g_game.internalCreatureTurn(this, newDir);
}

void Monster::dropLoot(Container* corpse)
{
	if(corpse && lootDrop == LOOT_DROP_FULL)
		mType->dropLoot(corpse);
}

bool Monster::isAttackable() const
{
	std::string value;
	if(!getStorage("attackable", value))
		return mType->isAttackable;

	return booleanString(value);
}

bool Monster::isHostile() const
{
	std::string value;
	if(!getStorage("hostile", value))
		return mType->isHostile;

	return booleanString(value);
}

bool Monster::isPushable() const
{
	if(baseSpeed < 1)
		return false;

	std::string value;
	if(!getStorage("pushable", value))
		return mType->pushable;

	return booleanString(value);
}

bool Monster::isWalkable() const
{
	std::string value;
	if(!getStorage("walkable", value))
		return mType->isWalkable;

	return booleanString(value);
}

bool Monster::isFleeing() const
{
	std::string value;
	if(!getStorage("fleeing", value))
		return getHealth() <= mType->runAwayHealth;

	return booleanString(value);
}

bool Monster::isImmune(CombatType_t type) const
{
	ElementMap::const_iterator it = mType->elementMap.find(type);
	if(it == mType->elementMap.end())
		return Creature::isImmune(type);

	return it->second >= 100;
}

void Monster::resetLight()
{
	internalLight.level = mType->lightLevel;
	internalLight.color = mType->lightColor;
}

void Monster::drainHealth(Creature* attacker, CombatType_t combatType, int32_t damage)
{
	Creature::drainHealth(attacker, combatType, damage);
	if(isInvisible())
		removeCondition(CONDITION_INVISIBLE);
}

void Monster::changeHealth(int32_t healthChange)
{
	//In case a player with ignore flag set attacks the monster
	setIdle(false);
	if(!hasRecentBattle())
	{
		lastDamage = OTSYS_TIME();
		updateMapCache();
	}

	Creature::changeHealth(healthChange);
}

void Monster::changeMana(int32_t manaChange)
{
	//In case a player with ignore flag set attacks the monster
	setIdle(false);
	if(!hasRecentBattle())
	{
		lastDamage = OTSYS_TIME();
		updateMapCache();
	}

	Creature::changeMana(manaChange);
}

void Monster::changeShield(int32_t shieldChange)
{
	//In case a player with ignore flag set attacks the monster
	setIdle(false);
	if(!hasRecentBattle())
	{
		lastDamage = OTSYS_TIME();
		updateMapCache();
	}

	Creature::changeShield(shieldChange);
}

void Monster::changeBarrier(int32_t barrierChange)
{
	//In case a player with ignore flag set attacks the monster
	setIdle(false);
	if(!hasRecentBattle())
	{
		lastDamage = OTSYS_TIME();
		updateMapCache();
	}

	Creature::changeBarrier(barrierChange);
}

bool Monster::challengeCreature(Creature* creature)
{
	if(isSummon() || !selectTarget(creature))
		return false;

	targetChangeCooldown = 8000;
	targetChangeTicks = 0;
	return true;
}

bool Monster::convinceCreature(Creature* creature)
{
	Player* player = creature->getPlayer();
	if(player && !player->hasFlag(PlayerFlag_CanConvinceAll) && !mType->isConvinceable)
		return false;

	if(master)
	{
		if(master == creature)
			return false;

		master->removeSummon(this);
	}

	setFollowCreature(NULL);
	setAttackedCreature(NULL);
	destroySummons();

	creature->addSummon(this);
	updateTargetList();
	updateIdleStatus();

	//Notify surrounding about the change
	SpectatorVec list;
	g_game.getSpectators(list, getPosition(), false, true);
	g_game.getSpectators(list, creature->getPosition(), true, true);

	isMasterInRange = true;
	for(SpectatorVec::iterator it = list.begin(); it != list.end(); ++it)
		(*it)->onCreatureConvinced(creature, this);

	if(spawn)
	{
		spawn->removeMonster(this);
		spawn = NULL;
		masterRadius = -1;
	}

	if(raid)
	{
		raid->unRef();
		raid = NULL;
	}

	return true;
}

void Monster::onCreatureConvinced(const Creature* convincer, const Creature* creature)
{
	if(convincer == this || (!isFriend(creature) && !isOpponent(creature)))
		return;

	updateTargetList();
	updateIdleStatus();
}

void Monster::getPathSearchParams(const Creature* creature, FindPathParams& fpp) const
{
	Creature::getPathSearchParams(creature, fpp);
	fpp.minTargetDist = 1;

	
	std::string changer;
	if(!getStorage("targetdistance", changer))
		fpp.maxTargetDist = mType->targetDistance;
	else
		fpp.maxTargetDist = atoi((changer).c_str());
	
	if(isSummon())
	{
		if(master == creature)
		{
			fpp.maxTargetDist = 2;
			fpp.fullPathSearch = true;
		}
		else if(mType->targetDistance <= 1)
			fpp.fullPathSearch = true;
		else
			fpp.fullPathSearch = !canUseAttack(getPosition(), creature);
	}
	else if(isFleeing())
	{
		//Distance should be higher than the client view range (Map::maxClientViewportX/Map::maxClientViewportY)
		std::string changer;
		if(!getStorage("view", changer))
			fpp.maxTargetDist = 11;
		else
			fpp.maxTargetDist = atoi((changer).c_str());

		fpp.clearSight = fpp.fullPathSearch = false;
		fpp.keepDistance = true;
	}
	else if(mType->targetDistance <= 1)
		fpp.fullPathSearch = true;
	else
		fpp.fullPathSearch = !canUseAttack(getPosition(), creature);
}
