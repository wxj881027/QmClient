#include <game/client/component.h>

#include <stdexcept>

IStorage *CComponentInterfaces::Storage() const
{
	throw std::logic_error("storage access is outside the player-points test boundary");
}
