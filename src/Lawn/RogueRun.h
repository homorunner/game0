#pragma once
#include "../ConstEnums.h"
#include <array>
#include <cstdint>

// Persisted IDs: append new upgrades, never reorder existing entries.
enum class RogueUpgrade : int32_t
{
    LeftpeaterBurst,
    PlanternHealing,
    PlanternGreenVase,
    EmpoweredPea,
    ThreepeaterHoming,
    SquashEnhancement,
    DoubleWallnutCards,
    WallnutBowling,
    COUNT
};

enum class RoguePhase : int32_t { Playing, Reward, Choosing, Advancing, Ended };

struct RogueUpgradeDefinition
{
    const char* key;
    const char* title;
    const char* description;
    const char* titleZh;
    const char* descriptionZh;
    SeedType seed;
    bool* toggle;
};

const RogueUpgradeDefinition& GetRogueUpgrade(int id);

struct RogueRun
{
    bool active = false;
    uint32_t unlocked = 0;
    // Zero also represents legacy saves: an unlocked burst then means level 1.
    uint32_t leftpeaterBurstLevel = 0;
    RoguePhase phase = RoguePhase::Playing;
    std::array<int32_t, 3> offers{-1, -1, -1};

    bool IsUnlocked(RogueUpgrade id) const;
    int UpgradeLevel(RogueUpgrade id) const;
    bool CanChoose(RogueUpgrade id) const;
    int OfferCount() const;
    void RollOffers();
    bool Choose(int slot);
    bool IsValid() const;
};
