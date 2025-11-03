
#include "Page.h"

void PageModel::addPathSet(const PathSet &ps)
{
    int id = static_cast<int>(entities.size());
    Entity e;
    e.id = id;
    e.name = "Entity " + std::to_string(id);
    e.payload = ps;
    e.localToPage = Mat3();
    e.refreshFilterBase();
    entities[id] = e;
}

void PageModel::addBitmap(const Bitmap &bm)
{
    int id = static_cast<int>(entities.size());
    Entity e;
    e.id = id;
    e.name = "Entity " + std::to_string(id);
    e.payload = bm;
    e.localToPage = Mat3();
    e.refreshFilterBase();
    entities[id] = e;
}