//////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
//////////////////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////////////////
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//////////////////////////////////////////////////////////////////////
#include "otpch.h"

#include "depotchest.h"
#include "tools.h"
#include "player.h"
#include "configmanager.h"

extern ConfigManager g_config;
extern Game g_game;

DepotChest::DepotChest(uint16_t _type) :
	Container(_type)
{
	depotLimit = 1500;
	depotCap = 1000000;
}

DepotChest::~DepotChest()
{
	//
}

ReturnValue DepotChest::__queryAdd(int32_t index, const Thing* thing, uint32_t count,
	uint32_t flags, Creature* actor/* = NULL*/) const
{
	const Item* item = thing->getItem();
	if(!item)
		return RET_NOTPOSSIBLE;

	if((flags & FLAG_NOLIMIT) == FLAG_NOLIMIT)
		return Container::__queryAdd(index, thing, count, flags, actor);

	int32_t addCount = 0;
	if((item->isStackable() && item->getItemCount() != count))
		addCount = 1;

	if(item->getTopParent() != this)
	{
		if(const Container* container = item->getContainer())
			addCount = container->getItemHoldingCount() + 1;
		else
			addCount = 1;
	}

	if(getItemHoldingCount() + addCount > depotLimit)
		return RET_DEPOTISFULL;

	float totalWeights = 0.0;
	float itemWeight = item->getWeight();
	
	if(item->isStackable())
		itemWeight = ((itemWeight/100) * count);
		
	for(ContainerIterator it = this->begin(); it != this->end(); ++it)
		totalWeights += (*it)->getWeight();
		
	std::stringstream iw;
	iw << std::fixed << std::setprecision(2) << itemWeight;
	std::stringstream tw;
	tw << std::fixed << std::setprecision(2) << totalWeights;
	
	if(actor) 
	{
		Player* player = actor->getPlayer();
		char buffer[90];
		sprintf(buffer, "Added %s weight to depot. Current/Max: %s/%s.", iw.str(), tw.str(), std::to_string(depotCap));
		player->sendCancel(buffer);
	}
		
	if((totalWeights + item->getWeight()) > depotCap) 
		return RET_DEPOTISHEAVY;
	
	return Container::__queryAdd(index, thing, count, flags, actor);
}

void DepotChest::postAddNotification(Creature* actor, Thing* thing, const Cylinder* oldParent, int32_t index, CylinderLink_t /*link LINK_OWNER*/)
{
	if(getParent())
		getParent()->postAddNotification(actor, thing, oldParent, index, LINK_PARENT);
}

void DepotChest::postRemoveNotification(Creature* actor, Thing* thing, const Cylinder* newParent, int32_t index, bool isCompleteRemoval, CylinderLink_t /*link = LINK_OWNER*/)
{
	if(getParent())
		getParent()->postRemoveNotification(actor, thing, newParent, index, isCompleteRemoval, LINK_PARENT);
}
