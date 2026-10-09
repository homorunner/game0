#include "RogueRun.h"
#include "../GameConstants.h"
#include "Common.h"
#include <algorithm>
#include <bit>
#include <cassert>

namespace
{
constexpr int upgradeCount = static_cast<int>(RogueUpgrade::COUNT);
constexpr uint32_t allUpgrades = (uint32_t{1} << upgradeCount) - 1;
const std::array<RogueUpgradeDefinition, upgradeCount> upgrades{{
    {"LEFTPEATER_BURST", "Opening Volley", "Fire 4 peas when planted. Repeat picks double the volley at the same firing interval, up to level 6 (128 peas).",
        "开场齐射", "种植后连射 4 发豌豆。重复选择使数量翻倍，发射间隔不变，最多 6 级（128 发）。", SEED_LEFTPEATER, &ENABLE_LEFTPEATER_PLANTING_BURST},
    {"PLANTERN_HEALING", "Healing Light", "Planterns heal the 8 nearby tiles for 45 HP each second.",
        "治愈之光", "路灯花每秒为周围 8 格植物恢复 45 点生命。", SEED_PLANTERN, &ENABLE_PLANTERN_HEALING},
    {"PLANTERN_GREEN_VASE", "Guiding Light", "The Plantern is always inside a green vase.",
        "指路明灯", "装有路灯花的罐子必定是绿罐。", SEED_PLANTERN, &ENABLE_PLANTERN_GREEN_VASE},
    {"EMPOWERED_PEA", "Heavy Peas", "Pick up to 3 times: empower every third, second, then every pea for 50% extra damage. Gargantuars cannot be knocked back.",
        "重型豌豆", "最多选择 3 次：每第 3 发、每第 2 发、每发豌豆强化，伤害增加 50%。巨人僵尸无法被击退。", SEED_PEASHOOTER, &ENABLE_PEASHOOTER_EMPOWERED_PEA},
    {"THREEPEATER_HOMING", "Seeking Volley", "Threepeater peas from empty lanes seek enemies in other lanes.",
        "追踪齐射", "三线射手空行的豌豆会追踪其他行的敌人。", SEED_THREEPEATER, &ENABLE_THREEPEATER_HOMING},
    {"SQUASH_ENHANCEMENT", "Heavy Landing", "Squash hits a wider area and stuns all enemies for 0.5 seconds on landing.",
        "沉重落地", "窝瓜攻击范围更广，落地时眩晕所有敌人 0.5 秒。", SEED_SQUASH, &ENABLE_SQUASH_ENHANCEMENT},
    {"DOUBLE_WALLNUT_CARDS", "Two for One", "Each Wall-nut vase drops 2 usable cards instead of 1.",
        "一举两得", "每个坚果罐子掉落 2 张卡片而不是 1 张。", SEED_WALLNUT, &ENABLE_WALLNUT_DOUBLE_VASE_CARDS},
    {"WALLNUT_BOWLING", "Let's Roll", "Double-click a planted Wall-nut to send it bowling through zombies.",
        "滚起来吧", "双击已种植的坚果，让它滚过去碾压僵尸。", SEED_WALLNUT, &ENABLE_WALLNUT_DOUBLE_CLICK_BOWLING}
}};
}

const RogueUpgradeDefinition& GetRogueUpgrade(int id)
{
    assert(id >= 0 && id < upgradeCount);
    return upgrades[id];
}

bool RogueRun::IsUnlocked(RogueUpgrade id) const
{
    const int index = static_cast<int>(id);
    return index >= 0 && index < upgradeCount && (unlocked & (uint32_t{1} << index)) != 0;
}

int RogueRun::OfferCount() const
{
    return static_cast<int>(std::count_if(offers.begin(), offers.end(), [](int id) { return id >= 0; }));
}

int RogueRun::UpgradeLevel(RogueUpgrade id) const
{
    if (!IsUnlocked(id)) return 0;
    if (id == RogueUpgrade::LeftpeaterBurst) return static_cast<int>(std::max(1U, leftpeaterBurstLevel));
    if (id == RogueUpgrade::EmpoweredPea) return static_cast<int>(std::max(1U, empoweredPeaLevel));
    return 1;
}

bool RogueRun::CanChoose(RogueUpgrade id) const
{
    const int index = static_cast<int>(id);
    const int maximum = id == RogueUpgrade::LeftpeaterBurst ? 6 : id == RogueUpgrade::EmpoweredPea ? 3 : 1;
    return index >= 0 && index < upgradeCount && UpgradeLevel(id) < maximum;
}

void RogueRun::RollOffers()
{
    if (!active || phase != RoguePhase::Reward) return;
    std::array<int, upgradeCount> pool{};
    int count = 0;
    for (int id = 0; id < upgradeCount; ++id)
        if (CanChoose(static_cast<RogueUpgrade>(id))) pool[count++] = id;
    offers.fill(-1);
    for (int slot = 0; slot < 3 && count > 0; ++slot)
    {
        const int pick = Sexy::Rand(count);
        offers[slot] = pool[pick];
        pool[pick] = pool[--count];
    }
    phase = OfferCount() ? RoguePhase::Choosing : RoguePhase::Advancing;
}

bool RogueRun::Choose(int slot)
{
    if (!active || phase != RoguePhase::Choosing || slot < 0 || slot >= OfferCount()) return false;
    const int id = offers[slot];
    if (!CanChoose(static_cast<RogueUpgrade>(id))) return false;
    if (id == static_cast<int>(RogueUpgrade::LeftpeaterBurst))
        leftpeaterBurstLevel = UpgradeLevel(RogueUpgrade::LeftpeaterBurst) + 1;
    if (id == static_cast<int>(RogueUpgrade::EmpoweredPea))
        empoweredPeaLevel = UpgradeLevel(RogueUpgrade::EmpoweredPea) + 1;
    unlocked |= uint32_t{1} << id;
    offers.fill(-1);
    phase = RoguePhase::Advancing;
    return true;
}

bool RogueRun::IsValid() const
{
    if ((unlocked & ~allUpgrades) || phase < RoguePhase::Playing || phase > RoguePhase::Ended) return false;
    if (leftpeaterBurstLevel > 6 || (leftpeaterBurstLevel && !IsUnlocked(RogueUpgrade::LeftpeaterBurst))) return false;
    if (empoweredPeaLevel > 3 || (empoweredPeaLevel && !IsUnlocked(RogueUpgrade::EmpoweredPea))) return false;
    if (!active && (unlocked || phase != RoguePhase::Playing)) return false;
    int available = 0;
    for (int id = 0; id < upgradeCount; ++id) available += CanChoose(static_cast<RogueUpgrade>(id));
    // A legacy Choosing save may have fewer cards than the expanded pool.
    const int count = phase == RoguePhase::Choosing ? OfferCount() : 0;
    if (count > std::min(3, available)) return false;
    if (phase == RoguePhase::Choosing && count == 0) return false;
    int legacyMissing = 0;
    for (RogueUpgrade id : {RogueUpgrade::LeftpeaterBurst, RogueUpgrade::EmpoweredPea})
    {
        const uint32_t storedLevel = id == RogueUpgrade::LeftpeaterBurst ? leftpeaterBurstLevel : empoweredPeaLevel;
        if (storedLevel == 0 && IsUnlocked(id) &&
            std::find(offers.begin(), offers.end(), static_cast<int>(id)) == offers.end()) ++legacyMissing;
    }
    if (phase == RoguePhase::Choosing && count < std::min(3, available - legacyMissing)) return false;
    uint32_t seen = 0;
    for (int slot = 0; slot < 3; ++slot)
    {
        const int id = offers[slot];
        if (slot >= count) { if (id != -1) return false; continue; }
        if (!CanChoose(static_cast<RogueUpgrade>(id)) || (seen & (uint32_t{1} << id))) return false;
        seen |= uint32_t{1} << id;
    }
    return true;
}
