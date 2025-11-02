
#include "Page.h"

void PageModel::addPathSet(const PathSet& ps)
{
    entities.emplace_back(Entity{ static_cast<int>(entities.size()), "Entity " + std::to_string(entities.size()), ps });
}