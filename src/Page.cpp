
#include "Page.h"
#include "filters/FilterRegistry.h"
#include "filters/FilterChain.h"

static int page_next_id(const std::map<int, Entity> &ents)
{
    int maxId = -1;
    for (const auto &kv : ents)
    {
        if (kv.first > maxId) maxId = kv.first;
    }
    return maxId + 1;
}

int PageModel::addPathSet(const PathSet &ps)
{
    int id = page_next_id(entities);
    Entity e;
    e.id = id;
    e.name = "Entity " + std::to_string(id);
    e.payload = ps;
    e.localToPage = Mat3();
    e.refreshFilterBase();
    entities[id] = std::move(e);
    return id;
}

void PageModel::addBitmap(const Bitmap &bm)
{
    int id = page_next_id(entities);
    Entity e;
    e.id = id;
    e.name = "Entity " + std::to_string(id);
    e.payload = bm;
    e.localToPage = Mat3();
    e.refreshFilterBase();
    entities[id] = std::move(e);
}

void PageModel::addColorImage(const ColorImage &ci)
{
    int id = page_next_id(entities);
    Entity e;
    e.id = id;
    e.name = "Entity " + std::to_string(id);
    e.payload = ci;
    e.localToPage = Mat3();
    e.refreshFilterBase();
    entities[id] = std::move(e);
}

int PageModel::addGeneratedEntity(std::unique_ptr<GeneratorBase> gen, Vec2 positionMm)
{
    int id = page_next_id(entities);
    Entity e;
    e.id = id;
    e.name = std::string(gen->name()) + " " + std::to_string(id);
    e.localToPage = Mat3::translation(positionMm);
    // Initialise payload with a default value matching the generator's output kind.
    switch (gen->outputKind())
    {
    case LayerKind::Bitmap:     e.payload = Bitmap{};     break;
    case LayerKind::FloatImage: e.payload = FloatImage{}; break;
    case LayerKind::ColorImage: e.payload = ColorImage{}; break;
    default:                    e.payload = PathSet{};    break;
    }
    e.generator = std::move(gen);
    e.tickGenerator();   // populate payload immediately
    entities[id] = std::move(e);
    return id;
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

    // Clone generator if present
    if (src.generator)
        dst.generator = src.generator->clone();

    dst.tickGenerator();  // regenerate payload if generator present
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
