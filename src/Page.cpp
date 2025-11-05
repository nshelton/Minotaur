
#include "Page.h"
#include "filters/FilterRegistry.h"
#include "filters/FilterChain.h"

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

static int page_next_id(const std::map<int, Entity>& ents)
{
    int maxId = -1;
    for (const auto &kv : ents)
    {
        if (kv.first > maxId) maxId = kv.first;
    }
    return maxId + 1;
}

int PageModel::duplicateEntity(int sourceId)
{
    auto it = entities.find(sourceId);
    if (it == entities.end()) return -1;

    const Entity &src = it->second;

    Entity dst;
    dst.id = page_next_id(entities);
    dst.name = src.name + " Copy";
    dst.payload = src.payload;        // deep copy PathSet/Bitmap
    dst.payloadVersion = src.payloadVersion;
    dst.localToPage = src.localToPage;
    dst.visible = src.visible;
    dst.color = src.color;
    dst.refreshFilterBase();

    // Rebuild filter chain using registry and copy parameters/enabled flags
    size_t n = src.filterChain.filterCount();
    if (n > 0)
    {
        const auto &all = FilterRegistry::instance().all();
        for (size_t i = 0; i < n; ++i)
        {
            const FilterBase *fb = src.filterChain.filterAt(i);
            if (!fb) continue;

            std::unique_ptr<FilterBase> fNew;
            for (const auto &info : all)
            {
                if (info.name == fb->name())
                {
                    fNew = info.factory();
                    break;
                }
            }
            if (!fNew) continue;

            // Copy parameter values
            for (const auto &kv : fb->m_parameters)
            {
                fNew->setParameter(kv.first, kv.second.value);
            }

            size_t idx = dst.filterChain.addFilter(std::move(fNew));
            // Maintain enabled/disabled state
            bool enabled = src.filterChain.isFilterEnabled(i);
            dst.filterChain.setFilterEnabled(idx, enabled);
        }
    }

    entities[dst.id] = std::move(dst);
    return entities.rbegin()->first; // return new id
}