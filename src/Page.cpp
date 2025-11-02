
#include "Page.h"

void PageModel::addPathSet(const PathSet &ps)
{
    int id = static_cast<int>(entities.size());
    entities[id] = Entity{static_cast<int>(entities.size()),
                          "Entity " + std::to_string(entities.size()),
                          ps};
}