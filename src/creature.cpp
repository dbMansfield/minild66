#include <JsonBox.h>
#include "entity_manager.hpp"
#include "creature.hpp"

void Creature::load(const JsonBox::Value& v, EntityManager* mgr)
{
    JsonBox::Object o = v.getObject();

    auto has = [&o](const std::string& s) { return o.find(s) != o.end(); };

    // Load main properties
    if(has("hp")) hp = o["hp"].tryGetFloat(0.0f);
    if(has("hp_max")) hp_max = o["hp_max"].tryGetFloat(0.0f);
    if(has("hp_regen")) hp_regen = o["hp_regen"].tryGetFloat(0.0f);
    if(has("mp")) mp = o["mp"].tryGetFloat(0.0f);
    if(has("mp_max")) mp_max = o["mp_max"].tryGetFloat(0.0f);
    if(has("mp_regen")) mp_regen = o["mp_regen"].tryGetFloat(0.0f);
    if(has("moveSpeed")) moveSpeed = o["moveSpeed"].tryGetFloat(0.0f);
    if(has("pd")) pd = o["pd"].tryGetFloat(0.0f);
    if(has("sd")) sd = o["sd"].tryGetFloat(0.0f);
    if(has("pa")) pa = o["pa"].tryGetFloat(0.0f);
    if(has("sa")) sa = o["sa"].tryGetFloat(0.0f);

    // Load stats
    if(has("stats"))
    {
        JsonBox::Object statsO = o["stats"].getObject();
        if(has("str")) stats.str = statsO["str"].tryGetFloat(0.0f);
        if(has("dex")) stats.dex = statsO["dex"].tryGetFloat(0.0f);
        if(has("con")) stats.con = statsO["con"].tryGetFloat(0.0f);
        if(has("mnd")) stats.mnd = statsO["mnd"].tryGetFloat(0.0f);
        if(has("wis")) stats.wis = statsO["wis"].tryGetFloat(0.0f);
        if(has("lck")) stats.lck = statsO["lck"].tryGetFloat(0.0f);
    }
}
