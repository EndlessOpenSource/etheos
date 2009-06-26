
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "map.hpp"

#include <string>
#include <vector>
#include <cstdio>
#include <cmath>

#include "util.hpp"
#include "eoserver.hpp"
#include "packet.hpp"

Map::Map(int id, World *world)
{
	this->id = id;
	this->world = world;

	char namebuf[6];
	if (id < 0)
	{
		return;
	}
	std::string filename = world->config["MapDir"];
	std::sprintf(namebuf, "%05i", id);
	this->filename = filename;
	filename.append(namebuf);
	filename.append(".emf");

	std::FILE *fh = std::fopen(filename.c_str(), "rb");

	this->exists = true;
	if (!fh)
	{
		this->exists = false;
		std::fprintf(stderr, "Could not load file: %s\n", filename.c_str());
		return;
	}

	std::fseek(fh, 0x03, SEEK_SET);
	std::fread(this->rid, sizeof(char), 4, fh);

	char buf[8];
	int outersize;
	int innersize;

	std::fseek(fh, 0x1F, SEEK_SET);
	std::fread(buf, sizeof(char), 1, fh);
	this->pk = PacketProcessor::Number(buf[0]) == 3;

	std::fseek(fh, 0x25, SEEK_SET);
	std::fread(buf, sizeof(char), 2, fh);
	this->width = PacketProcessor::Number(buf[0]) + 1;
	this->height = PacketProcessor::Number(buf[1]) + 1;

	this->tiles.resize(height);
	for (int i = 0; i < height; ++i)
	{
		this->tiles[i].resize(width);
	}

	std::fseek(fh, 0x2E, SEEK_SET);
	std::fread(buf, sizeof(char), 1, fh);
	outersize = PacketProcessor::Number(buf[0]);
	if (outersize)
	{
		std::fseek(fh, 8 * outersize, SEEK_CUR);
	}

	std::fread(buf, sizeof(char), 1, fh);
	outersize = PacketProcessor::Number(buf[0]);
	if (outersize)
	{
		std::fseek(fh, 4 * outersize, SEEK_CUR);
	}

	std::fread(buf, sizeof(char), 1, fh);
	outersize = PacketProcessor::Number(buf[0]);
	if (outersize)
	{
		std::fseek(fh, 12 * outersize, SEEK_CUR);
	}

	std::fread(buf, sizeof(char), 1, fh);
	outersize = PacketProcessor::Number(buf[0]);
	for (int i = 0; i < outersize; ++i)
	{
		std::fread(buf, sizeof(char), 2, fh);
		unsigned char yloc = PacketProcessor::Number(buf[0]);
		innersize = PacketProcessor::Number(buf[1]);
		for (int ii = 0; ii < innersize; ++ii)
		{
			Map_Tile newtile;
			std::fread(buf, sizeof(char), 2, fh);
			unsigned char xloc = PacketProcessor::Number(buf[0]);
			unsigned char spec = PacketProcessor::Number(buf[1]);
			newtile.tilespec = static_cast<Map_Tile::TileSpec>(spec);
			this->tiles[yloc][xloc] = newtile;
		}
	}

	std::fread(buf, sizeof(char), 1, fh);
	outersize = PacketProcessor::Number(buf[0]);
	for (int i = 0; i < outersize; ++i)
	{
		std::fread(buf, sizeof(char), 2, fh);
		unsigned char yloc = PacketProcessor::Number(buf[0]);
		innersize = PacketProcessor::Number(buf[1]);
		for (int ii = 0; ii < innersize; ++ii)
		{
			Map_Warp *newwarp = new Map_Warp;
			std::fread(buf, sizeof(char), 8, fh);
			unsigned char xloc = PacketProcessor::Number(buf[0]);
			newwarp->map = PacketProcessor::Number(buf[1], buf[2]);
			newwarp->x = PacketProcessor::Number(buf[3]);
			newwarp->y = PacketProcessor::Number(buf[4]);
			newwarp->levelreq = PacketProcessor::Number(buf[5]);
			newwarp->spec = static_cast<Map_Warp::WarpSpec>(PacketProcessor::Number(buf[6], buf[7]));
			this->tiles[yloc][xloc].warp = newwarp;
		}
	}

	std::fseek(fh, 0x2E, SEEK_SET);
	std::fread(buf, sizeof(char), 1, fh);
	outersize = PacketProcessor::Number(buf[0]);
	int index = 0;
	for (int i = 0; i < outersize; ++i)
	{
		std::fread(buf, sizeof(char), 8, fh);
		unsigned char x = PacketProcessor::Number(buf[0]);
		unsigned char y = PacketProcessor::Number(buf[1]);
		short npc_id = PacketProcessor::Number(buf[2], buf[3]);
		unsigned char spawntype = PacketProcessor::Number(buf[4]);
		short spawntime = PacketProcessor::Number(buf[5], buf[6]);
		unsigned char amount = PacketProcessor::Number(buf[7]);

		for (int ii = 0; ii < amount; ++ii)
		{
			NPC *newnpc = new NPC(this, npc_id, x, y, spawntype, spawntime, index++);
			this->npcs.push_back(newnpc);

			newnpc->Spawn();
		}
	}

	std::fseek(fh, 0x00, SEEK_END);
	this->filesize = std::ftell(fh);

	std::fclose(fh);
}

int Map::GenerateItemID()
{
	int lowest_free_id = 1;
	restart_loop:
	UTIL_VECTOR_FOREACH_ALL(this->items, Map_Item, item)
	{
		if (item.uid == lowest_free_id)
		{
			lowest_free_id = item.uid + 1;
			goto restart_loop;
		}
	}
	return lowest_free_id;
}

void Map::Enter(Character *character, WarpAnimation animation)
{
	PacketBuilder builder;
	this->characters.push_back(character);
	character->map = this;

	builder.SetID(PACKET_PLAYERS, PACKET_AGREE);

	builder.AddByte(255);
	builder.AddBreakString(character->name);
	builder.AddShort(character->player->id);
	builder.AddShort(character->mapid);
	builder.AddShort(character->x);
	builder.AddShort(character->y);
	builder.AddChar(character->direction);
	builder.AddChar(6); // ?
	builder.AddString(character->PaddedGuildTag());
	builder.AddChar(character->level);
	builder.AddChar(character->gender);
	builder.AddChar(character->hairstyle);
	builder.AddChar(character->haircolor);
	builder.AddChar(character->race);
	builder.AddShort(character->maxhp);
	builder.AddShort(character->hp);
	builder.AddShort(character->maxtp);
	builder.AddShort(character->tp);
	// equipment
	builder.AddShort(this->world->eif->Get(character->paperdoll[Character::Boots])->dollgraphic);
	builder.AddShort(0); // ??
	builder.AddShort(0); // ??
	builder.AddShort(0); // ??
	builder.AddShort(this->world->eif->Get(character->paperdoll[Character::Armor])->dollgraphic);
	builder.AddShort(0); // ??
	builder.AddShort(this->world->eif->Get(character->paperdoll[Character::Hat])->dollgraphic);
	builder.AddShort(this->world->eif->Get(character->paperdoll[Character::Shield])->dollgraphic);
	builder.AddShort(this->world->eif->Get(character->paperdoll[Character::Weapon])->dollgraphic);
	builder.AddChar(character->sitting);
	builder.AddChar(0); // visible
	builder.AddChar(animation);
	builder.AddByte(255);
	builder.AddChar(1); // 0 = NPC, 1 = player

	UTIL_VECTOR_FOREACH_ALL(this->characters, Character *, checkcharacter)
	{
		if (checkcharacter == character || !character->InRange(checkcharacter))
		{
			continue;
		}

		checkcharacter->player->client->SendBuilder(builder);
	}
}

void Map::Leave(Character *character, WarpAnimation animation)
{
	PacketBuilder builder;

	if (animation == WARP_ANIMATION_NONE)
	{
		builder.SetID(PACKET_PLAYERS, PACKET_REMOVE);
	}
	else
	{
		builder.SetID(PACKET_CLOTHES, PACKET_REMOVE);
	}
	builder.AddShort(character->player->id);
	if (animation != WARP_ANIMATION_NONE)
	{
		builder.AddChar(animation);
	}

	UTIL_VECTOR_FOREACH_ALL(this->characters, Character *, checkcharacter)
	{
		if (checkcharacter == character || !character->InRange(checkcharacter))
		{
			continue;
		}

		checkcharacter->player->client->SendBuilder(builder);
	}

	UTIL_VECTOR_IFOREACH_ALL(this->characters, Character *, checkcharacter)
	{
		if (*(checkcharacter) == character)
		{
			this->characters.erase(checkcharacter);
			break;
		}
	}
	character->map = 0;
}

void Map::Msg(Character *from, std::string message)
{
	PacketBuilder builder;

	builder.SetID(PACKET_TALK, PACKET_PLAYER);
	builder.AddShort(from->player->id);
	builder.AddString(message);

	UTIL_VECTOR_FOREACH_ALL(this->characters, Character *, character)
	{
		if (character == from || !from->InRange(character))
		{
			continue;
		}

		character->player->client->SendBuilder(builder);
	}
}

bool Map::Walk(Character *from, Direction direction, bool admin)
{
	PacketBuilder builder;
	int seedistance = this->world->config["SeeDistance"];

	// TODO: Check for open/closed doors

	unsigned char target_x = from->x;
	unsigned char target_y = from->y;

	switch (direction)
	{
		case DIRECTION_UP:
			target_y -= 1;

			if (target_y > from->y)
			{
				return false;
			}

			break;

		case DIRECTION_RIGHT:
			target_x += 1;

			if (target_x < from->x)
			{
				return false;
			}

			break;

		case DIRECTION_DOWN:
			target_y += 1;

			if (target_x < from->x)
			{
				return false;
			}

			break;

		case DIRECTION_LEFT:
			target_x -= 1;

			if (target_x > from->x)
			{
				return false;
			}

			break;
	}

	if (!admin && !this->Walkable(target_x, target_y))
	{
		return false;
	}

	Map_Warp *warp;
	if (!admin && (warp = this->GetWarp(target_x, target_y)))
	{
		if (from->level >= warp->levelreq)
		{
			from->Warp(warp->map, warp->x, warp->y);
		}

		return false;
	}

	from->x = target_x;
	from->y = target_y;

	int newx;
	int newy;
	int oldx;
	int oldy;

	std::vector<std::pair<int, int> > newcoords;
	std::vector<std::pair<int, int> > oldcoords;

	std::vector<Character *> newchars;
	std::vector<Character *> oldchars;
	std::vector<NPC *> newnpcs;
	//std::vector<NPC *> oldnpcs;
	std::vector<Map_Item> newitems;

	switch (direction)
	{
		case DIRECTION_UP:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newy = from->y - seedistance + std::abs(i);
				newx = from->x + i;
				oldy = from->y + seedistance + 1 - std::abs(i);
				oldx = from->x + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

		case DIRECTION_RIGHT:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newx = from->x + seedistance - std::abs(i);
				newy = from->y + i;
				oldx = from->x - seedistance - 1 + std::abs(i);
				oldy = from->y + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

		case DIRECTION_DOWN:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newy = from->y + seedistance - std::abs(i);
				newx = from->x + i;
				oldy = from->y - seedistance - 1 + std::abs(i);
				oldx = from->x + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

		case DIRECTION_LEFT:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newx = from->x - seedistance + std::abs(i);
				newy = from->y + i;
				oldx = from->x + seedistance + 1 - std::abs(i);
				oldy = from->y + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

	}

	UTIL_VECTOR_FOREACH_ALL(this->characters, Character *, checkchar)
	{
		if (checkchar == from)
		{
			continue;
		}

		for (std::size_t i = 0; i < oldcoords.size(); ++i)
		{
			if (checkchar->x == oldcoords[i].first && checkchar->y == oldcoords[i].second)
			{
				oldchars.push_back(checkchar);
			}
			else if (checkchar->x == newcoords[i].first && checkchar->y == newcoords[i].second)
			{
				newchars.push_back(checkchar);
			}
		}
	}

	UTIL_VECTOR_FOREACH_ALL(this->npcs, NPC *, checknpc)
	{
		if (!checknpc->alive)
		{
			continue;
		}

		for (std::size_t i = 0; i < oldcoords.size(); ++i)
		{
			/*if (checknpc->x == oldcoords[i].first && checknpc->y == oldcoords[i].second)
			{
				oldnpcs.push_back(checknpc);
			}
			else */if (checknpc->x == newcoords[i].first && checknpc->y == newcoords[i].second)
			{
				newnpcs.push_back(checknpc);
			}
		}
	}

	UTIL_VECTOR_FOREACH_ALL(this->items, Map_Item, checkitem)
	{
		for (std::size_t i = 0; i < oldcoords.size(); ++i)
		{
			if (checkitem.x == newcoords[i].first && checkitem.y == newcoords[i].second)
			{
				newitems.push_back(checkitem);
			}
		}
	}

	from->direction = direction;

	builder.SetID(PACKET_PLAYERS, PACKET_REMOVE);
	builder.AddShort(from->player->id);

	UTIL_VECTOR_FOREACH_ALL(oldchars, Character *, character)
	{
		PacketBuilder rbuilder;
		rbuilder.SetID(PACKET_PLAYERS, PACKET_REMOVE);
		rbuilder.AddShort(character->player->id);

		character->player->client->SendBuilder(builder);
		from->player->client->SendBuilder(rbuilder);
	}

	builder.Reset();

	builder.SetID(PACKET_PLAYERS, PACKET_AGREE);
	builder.AddByte(255);
	builder.AddBreakString(from->name);
	builder.AddShort(from->player->id);
	builder.AddShort(from->mapid);
	builder.AddShort(from->x);
	builder.AddShort(from->y);
	builder.AddChar(from->direction);
	builder.AddChar(6); // ?
	builder.AddString(from->PaddedGuildTag());
	builder.AddChar(from->level);
	builder.AddChar(from->gender);
	builder.AddChar(from->hairstyle);
	builder.AddChar(from->haircolor);
	builder.AddChar(from->race);
	builder.AddShort(from->maxhp);
	builder.AddShort(from->hp);
	builder.AddShort(from->maxtp);
	builder.AddShort(from->tp);
	// equipment
	builder.AddShort(this->world->eif->Get(from->paperdoll[Character::Boots])->dollgraphic);
	builder.AddShort(0); // ??
	builder.AddShort(0); // ??
	builder.AddShort(0); // ??
	builder.AddShort(this->world->eif->Get(from->paperdoll[Character::Armor])->dollgraphic);
	builder.AddShort(0); // ??
	builder.AddShort(this->world->eif->Get(from->paperdoll[Character::Hat])->dollgraphic);
	builder.AddShort(this->world->eif->Get(from->paperdoll[Character::Shield])->dollgraphic);
	builder.AddShort(this->world->eif->Get(from->paperdoll[Character::Weapon])->dollgraphic);
	builder.AddChar(from->sitting);
	builder.AddChar(0); // visible
	builder.AddByte(255);
	builder.AddChar(1); // 0 = NPC, 1 = player

	UTIL_VECTOR_FOREACH_ALL(newchars, Character *, character)
	{
		PacketBuilder rbuilder;
		rbuilder.SetID(PACKET_PLAYERS, PACKET_AGREE);
		rbuilder.AddByte(255);
		rbuilder.AddBreakString(character->name);
		rbuilder.AddShort(character->player->id);
		rbuilder.AddShort(character->mapid);
		rbuilder.AddShort(character->x);
		rbuilder.AddShort(character->y);
		rbuilder.AddChar(character->direction);
		rbuilder.AddChar(6); // ?
		rbuilder.AddString(character->PaddedGuildTag());
		rbuilder.AddChar(character->level);
		rbuilder.AddChar(character->gender);
		rbuilder.AddChar(character->hairstyle);
		rbuilder.AddChar(character->haircolor);
		rbuilder.AddChar(character->race);
		rbuilder.AddShort(character->maxhp);
		rbuilder.AddShort(character->hp);
		rbuilder.AddShort(character->maxtp);
		rbuilder.AddShort(character->tp);
		// equipment
		rbuilder.AddShort(this->world->eif->Get(character->paperdoll[Character::Boots])->dollgraphic);
		rbuilder.AddShort(0); // ??
		rbuilder.AddShort(0); // ??
		rbuilder.AddShort(0); // ??
		rbuilder.AddShort(this->world->eif->Get(character->paperdoll[Character::Armor])->dollgraphic);
		rbuilder.AddShort(0); // ??
		rbuilder.AddShort(this->world->eif->Get(character->paperdoll[Character::Hat])->dollgraphic);
		rbuilder.AddShort(this->world->eif->Get(character->paperdoll[Character::Shield])->dollgraphic);
		rbuilder.AddShort(this->world->eif->Get(character->paperdoll[Character::Weapon])->dollgraphic);
		rbuilder.AddChar(character->sitting);
		rbuilder.AddChar(0); // visible
		rbuilder.AddByte(255);
		rbuilder.AddChar(1); // 0 = NPC, 1 = player

		character->player->client->SendBuilder(builder);
		from->player->client->SendBuilder(rbuilder);
	}

	builder.Reset();

	builder.SetID(PACKET_WALK, PACKET_PLAYER);
	builder.AddShort(from->player->id);
	builder.AddChar(direction);
	builder.AddChar(from->x);
	builder.AddChar(from->y);

	UTIL_VECTOR_FOREACH_ALL(this->characters, Character *, character)
	{
		if (character == from || !from->InRange(character))
		{
			continue;
		}

		character->player->client->SendBuilder(builder);
	}

	builder.Reset();

	builder.SetID(PACKET_WALK, PACKET_REPLY);
	builder.AddByte(255);
	builder.AddByte(255);
	UTIL_VECTOR_FOREACH_ALL(newitems, Map_Item, item)
	{
		builder.AddShort(item.uid);
		builder.AddShort(item.id);
		builder.AddChar(item.x);
		builder.AddChar(item.y);
		builder.AddThree(item.amount);
	}
	from->player->client->SendBuilder(builder);

	builder.SetID(PACKET_APPEAR, PACKET_REPLY);
	UTIL_VECTOR_FOREACH_ALL(newnpcs, NPC *, npc)
	{
		builder.Reset();
		builder.AddChar(0);
		builder.AddByte(255);
		builder.AddChar(npc->index);
		builder.AddShort(npc->id);
		builder.AddChar(npc->x);
		builder.AddChar(npc->y);
		builder.AddChar(npc->direction);

		from->player->client->SendBuilder(builder);
	}

	// TODO: Find some way to delete NPCs from the client view

	return true;
}

bool Map::Walk(NPC *from, Direction direction)
{
	PacketBuilder builder;
	int seedistance = this->world->config["SeeDistance"];

	unsigned char target_x = from->x;
	unsigned char target_y = from->y;

	switch (direction)
	{
		case DIRECTION_UP:
			target_y -= 1;

			if (target_y > from->y)
			{
				return false;
			}

			break;

		case DIRECTION_RIGHT:
			target_x += 1;

			if (target_x < from->x)
			{
				return false;
			}

			break;

		case DIRECTION_DOWN:
			target_y += 1;

			if (target_x < from->x)
			{
				return false;
			}

			break;

		case DIRECTION_LEFT:
			target_x -= 1;

			if (target_x > from->x)
			{
				return false;
			}

			break;
	}

	if (!this->Walkable(target_x, target_y, true) || this->Occupied(target_x, target_y, Map::PlayerAndNPC))
	{
		return false;
	}

	from->x = target_x;
	from->y = target_y;

	int newx;
	int newy;
	int oldx;
	int oldy;

	std::vector<std::pair<int, int> > newcoords;
	std::vector<std::pair<int, int> > oldcoords;

	std::vector<Character *> newchars;
	std::vector<Character *> oldchars;
	std::vector<NPC *> newnpcs;
	//std::vector<NPC *> oldnpcs;
	std::vector<Map_Item> newitems;

	switch (direction)
	{
		case DIRECTION_UP:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newy = from->y - seedistance + std::abs(i);
				newx = from->x + i;
				oldy = from->y + seedistance + 1 - std::abs(i);
				oldx = from->x + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

		case DIRECTION_RIGHT:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newx = from->x + seedistance - std::abs(i);
				newy = from->y + i;
				oldx = from->x - seedistance - 1 + std::abs(i);
				oldy = from->y + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

		case DIRECTION_DOWN:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newy = from->y + seedistance - std::abs(i);
				newx = from->x + i;
				oldy = from->y - seedistance - 1 + std::abs(i);
				oldx = from->x + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

		case DIRECTION_LEFT:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newx = from->x - seedistance + std::abs(i);
				newy = from->y + i;
				oldx = from->x + seedistance + 1 - std::abs(i);
				oldy = from->y + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

	}

	from->direction = direction;

	UTIL_VECTOR_FOREACH_ALL(this->characters, Character *, checkchar)
	{
		for (std::size_t i = 0; i < oldcoords.size(); ++i)
		{
			if (checkchar->x == oldcoords[i].first && checkchar->y == oldcoords[i].second)
			{
				oldchars.push_back(checkchar);
			}
			else if (checkchar->x == newcoords[i].first && checkchar->y == newcoords[i].second)
			{
				newchars.push_back(checkchar);
			}
		}
	}

	builder.SetID(PACKET_APPEAR, PACKET_REPLY);
	builder.AddChar(0);
	builder.AddByte(255);
	builder.AddChar(from->index);
	builder.AddShort(from->id);
	builder.AddChar(from->x);
	builder.AddChar(from->y);
	builder.AddChar(from->direction);

	UTIL_VECTOR_FOREACH_ALL(newchars, Character *, character)
	{
		character->player->client->SendBuilder(builder);
	}

	builder.Reset();

	builder.SetID(PACKET_NPC, PACKET_PLAYER);
	builder.AddChar(from->index);
	builder.AddChar(from->x);
	builder.AddChar(from->y);
	builder.AddChar(from->direction);
	builder.AddByte(255);
	builder.AddByte(255);
	builder.AddByte(255);

	UTIL_VECTOR_FOREACH_ALL(this->characters, Character *, character)
	{
		if (!character->InRange(from))
		{
			continue;
		}

		character->player->client->SendBuilder(builder);
	}

	return true;
}

void Map::Attack(Character *from, Direction direction)
{
	PacketBuilder builder;

	from->direction = direction;

	builder.SetID(PACKET_ATTACK, PACKET_PLAYER);
	builder.AddShort(from->player->id);
	builder.AddChar(direction);

	UTIL_VECTOR_FOREACH_ALL(this->characters, Character *, character)
	{
		if (character == from || !from->InRange(character))
		{
			continue;
		}

		character->player->client->SendBuilder(builder);
	}

	int target_x = from->x;
	int target_y = from->y;

	int range = 1;

	if (this->world->eif->Get(from->paperdoll[Character::Weapon])->subtype == EIF::Ranged)
	{
		range = static_cast<int>(this->world->config["RangedDistance"]);
	}

	for (int i = 0; i < range; ++i)
	{
		switch (from->direction)
		{
			case DIRECTION_UP:
				target_y -= 1;
				break;

			case DIRECTION_RIGHT:
				target_x += 1;
				break;

			case DIRECTION_DOWN:
				target_y += 1;
				break;

			case DIRECTION_LEFT:
				target_x -= 1;
				break;
		}

		if (!this->Walkable(target_x, target_y, true))
		{
			return;
		}

		double mobrate = static_cast<double>(this->world->config["MobRate"]) / 100.0;
		UTIL_VECTOR_FOREACH_ALL(this->npcs, NPC *, npc)
		{
			if ((npc->data->type == ENF::Passive || npc->data->type == ENF::Aggressive || from->admin > static_cast<int>(this->world->admin_config["killnpcs"]))
			 && npc->alive && npc->x == target_x && npc->y == target_y)
			{
				int amount = util::rand(from->mindam, from->maxdam);

				// TODO: Revise these stat effects

				int hit_rate = 120;
				bool critical = true;

				if ((npc->direction == DIRECTION_UP && from->direction == DIRECTION_DOWN)
				 || (npc->direction == DIRECTION_RIGHT && from->direction == DIRECTION_LEFT)
				 || (npc->direction == DIRECTION_DOWN && from->direction == DIRECTION_UP)
				 || (npc->direction == DIRECTION_LEFT && from->direction == DIRECTION_RIGHT))
				{
					critical = false;
					hit_rate -= 40;
				}

				hit_rate += int(from->accuracy / 2.0);
				hit_rate -= int(double(npc->data->evade) / 2.0 * mobrate);
				hit_rate = std::min(std::max(hit_rate, 20), 100);

				int origamount = amount;
				amount -= int(double(npc->data->armor) / 3.0 * mobrate);

				amount = std::max(amount, int(std::ceil(double(origamount) * 0.1)));
				amount = std::min(amount, npc->hp);

				int rand = util::rand(0, 100);

				if (rand > hit_rate)
				{
					amount = 0;
				}

				if (rand > 92)
				{
					critical = true;
				}

				if (critical)
				{
					amount = int(double(amount) * 1.5);
				}

				amount = std::max(amount, 0);
				amount = std::min(amount, npc->hp);

				npc->Damage(from, amount);

				return;
			}
		}
	}
}

void Map::Face(Character *from, Direction direction)
{
	PacketBuilder builder;

	from->direction = direction;

	builder.SetID(PACKET_FACE, PACKET_PLAYER);
	builder.AddShort(from->player->id);
	builder.AddChar(direction);

	UTIL_VECTOR_FOREACH_ALL(this->characters, Character *, character)
	{
		if (character == from || !from->InRange(character))
		{
			continue;
		}

		character->player->client->SendBuilder(builder);
	}
}

void Map::Sit(Character *from, SitAction sit_type)
{
	PacketBuilder builder;

	from->sitting = sit_type;

	builder.SetID((sit_type == SIT_CHAIR) ? PACKET_CHAIR : PACKET_SIT, PACKET_PLAYER);
	builder.AddShort(from->player->id);
	builder.AddChar(from->x);
	builder.AddChar(from->y);
	builder.AddChar(from->direction);
	builder.AddChar(0); // ?

	UTIL_VECTOR_FOREACH_ALL(this->characters, Character *, character)
	{
		if (character == from || !from->InRange(character))
		{
			continue;
		}

		character->player->client->SendBuilder(builder);
	}
}

void Map::Stand(Character *from)
{
	PacketBuilder builder;

	from->sitting = SIT_STAND;

	builder.SetID(PACKET_SIT, PACKET_REMOVE);
	builder.AddShort(from->player->id);
	builder.AddChar(from->x);
	builder.AddChar(from->y);

	UTIL_VECTOR_FOREACH_ALL(this->characters, Character *, character)
	{
		if (character == from || !from->InRange(character))
		{
			continue;
		}

		character->player->client->SendBuilder(builder);
	}
}

void Map::Emote(Character *from, enum Emote emote, bool relay)
{
	PacketBuilder builder;

	builder.SetID(PACKET_EMOTE, PACKET_PLAYER);
	builder.AddShort(from->player->id);
	builder.AddChar(emote);

	UTIL_VECTOR_FOREACH_ALL(this->characters, Character *, character)
	{
		if (!relay && (character == from || !from->InRange(character)))
		{
			continue;
		}

		character->player->client->SendBuilder(builder);
	}
}

bool Map::Occupied(unsigned char x, unsigned char y, Map::OccupiedTarget target)
{
	if (x >= this->width || y >= this->height)
	{
		return false;
	}

	if (target != Map::NPCOnly)
	{
		UTIL_VECTOR_FOREACH_ALL(this->characters, Character *, character)
		{
			if (character->x == x && character->y == y)
			{
				return true;
			}
		}
	}

	if (target != Map::PlayerOnly)
	{
		UTIL_VECTOR_FOREACH_ALL(this->npcs, NPC *, npc)
		{
			if (npc->alive && npc->x == x && npc->y == y)
			{
				return true;
			}
		}
	}

	return false;
}

Map::~Map()
{
	UTIL_VECTOR_FOREACH_ALL(this->npcs, NPC *, npc)
	{
		delete npc;
	}
}

bool Map::OpenDoor(Character *from, unsigned char x, unsigned char y)
{
	if (from && !from->InRange(x, y))
	{
		return false;
	}

	if (Map_Warp *warp = this->GetWarp(x, y))
	{
		if (warp->spec == Map_Warp::NoDoor/* || warp->open*/)
		{
			return false;
		}

		// TODO: Check for keys
		// TODO: Check for open/closed doors

		PacketBuilder builder;
		builder.SetID(PACKET_DOOR, PACKET_OPEN);
		builder.AddChar(x);
		builder.AddShort(y);

		UTIL_VECTOR_FOREACH_ALL(this->characters, Character *, character)
		{
			if (character->InRange(x, y))
			{
				character->player->client->SendBuilder(builder);
			}
		}

		/*warp->open = true;*/
		return true;
	}

	return false;
}

Map_Item *Map::AddItem(short id, int amount, unsigned char x, unsigned char y, Character *from)
{
	Map_Item newitem = {GenerateItemID(), id, amount, x, y, 0, 0};

	PacketBuilder builder;
	builder.SetID(PACKET_ITEM, PACKET_ADD);
	builder.AddShort(id);
	builder.AddShort(newitem.uid);
	builder.AddThree(amount);
	builder.AddChar(x);
	builder.AddChar(y);

	if (from || (from && from->admin <= ADMIN_GM))
	{
		int ontile = 0;
		int onmap = 0;

		UTIL_VECTOR_FOREACH_ALL(this->items, Map_Item, item)
		{
			++onmap;
			if (item.x == x && item.y == y)
			{
				++ontile;
			}
		}

		if (ontile >= static_cast<int>(this->world->config["MaxTile"]) || onmap >= static_cast<int>(this->world->config["MaxMap"]))
		{
			return 0;
		}
	}

	UTIL_VECTOR_FOREACH_ALL(this->characters, Character *, character)
	{
		if ((from && character == from) || !character->InRange(newitem))
		{
			continue;
		}
		character->player->client->SendBuilder(builder);
	}

	this->items.push_back(newitem);
	return &this->items.back();
}

void Map::DelItem(short uid, Character *from)
{
	UTIL_VECTOR_IFOREACH_ALL(this->items, Map_Item, item)
	{
		if (item->uid == uid)
		{
			this->items.erase(item);
			PacketBuilder builder;
			builder.SetID(PACKET_ITEM, PACKET_REMOVE);
			builder.AddShort(uid);
			UTIL_VECTOR_FOREACH_ALL(this->characters, Character *, character)
			{
				if ((from && character == from) || !character->InRange(*item))
				{
					continue;
				}
				character->player->client->SendBuilder(builder);
			}
			break;
		}
	}
}

bool Map::Walkable(unsigned char x, unsigned char y, bool npc)
{
	if (x >= this->width || y >= this->height)
	{
		return false;
	}

	return this->tiles[y][x].Walkable(npc);
}

Map_Tile::TileSpec Map::GetSpec(unsigned char x, unsigned char y)
{
	if (x >= this->width || y >= this->height)
	{
		return Map_Tile::None;
	}

	return this->tiles[y][x].tilespec;
}

Map_Warp *Map::GetWarp(unsigned char x, unsigned char y)
{
	if (x >= this->width || y >= this->height)
	{
		return 0;
	}

	return this->tiles[y][x].warp;
}

void Map::Effect(int effect, int param)
{
	PacketBuilder builder;
	builder.SetID(PACKET_EFFECT, PACKET_USE);
	builder.AddChar(effect);
	builder.AddChar(param);

	UTIL_VECTOR_FOREACH_ALL(this->characters, Character *, character)
	{
		character->player->client->SendBuilder(builder);
	}
}

Character *Map::GetCharacter(std::string name)
{
	Character *selected = 0;

	util::lowercase(name);

	UTIL_VECTOR_FOREACH_ALL(this->characters, Character *, character)
	{
		if (character->name.compare(name) == 0)
		{
			selected = character;
			break;
		}
	}

	return selected;
}

Character *Map::GetCharacterPID(unsigned int id)
{
	Character *selected = 0;

	UTIL_VECTOR_FOREACH_ALL(this->characters, Character *, character)
	{
		if (character->player->id == id)
		{
			selected = character;
			break;
		}
	}

	return selected;
}

Character *Map::GetCharacterCID(unsigned int id)
{
	Character *selected = 0;

	UTIL_VECTOR_FOREACH_ALL(this->characters, Character *, character)
	{
		if (character->id == id)
		{
			selected = character;
			break;
		}
	}

	return selected;
}