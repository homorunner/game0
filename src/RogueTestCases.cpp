#include "UnitTestRunner.h"
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Challenge.h"
#include "Lawn/Coin.h"
#include "Lawn/CursorObject.h"
#include "Lawn/GridItem.h"
#include "Lawn/LawnCommon.h"
#include "Lawn/Plant.h"
#include "Lawn/Projectile.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/SaveGame.h"
#include "Lawn/Widget/RogueUpgradeDialog.h"
#include "misc/Buffer.h"
#include "widget/WidgetManager.h"
#include "zlib.h"
#include <algorithm>
#include <array>
#include <bit>
#include <format>

namespace
{
constexpr int upgradeCount = static_cast<int>(RogueUpgrade::COUNT);
constexpr uint32_t allUpgrades = (uint32_t{1} << upgradeCount) - 1;

bool SameRun(const RogueRun& a, const RogueRun& b)
{
	return a.active == b.active && a.unlocked == b.unlocked && a.leftpeaterBurstLevel == b.leftpeaterBurstLevel &&
		a.empoweredPeaLevel == b.empoweredPeaLevel && a.phase == b.phase && a.offers == b.offers;
}

Board& NewRogueBoard(LawnApp& app)
{
	app.KillDialog(DIALOG_ROGUE_UPGRADE);
	app.mGameMode = GAMEMODE_SCARY_POTTER_ENDLESS;
	app.MakeNewBoard();
	app.mBoard->InitLevel();
	app.mGameScene = SCENE_PLAYING;
	app.mBoard->mMouseVisible = false;
	return *app.mBoard;
}

int CountCoins(Board& board, CoinType type)
{
	int count = 0;
	for (Coin* coin : board.mCoins) count += !coin->mDead && coin->mType == type;
	return count;
}

Coin* RewardCoin(Board& board)
{
	for (Coin* coin : board.mCoins)
		if (!coin->mDead && coin->mType == COIN_AWARD_MONEY_BAG) return coin;
	return nullptr;
}

void SetupRogueScope(UnitTestRunner& runner, LawnApp& app)
{
	std::array<bool, upgradeCount> saved{};
	for (int id = 0; id < upgradeCount; ++id)
	{
		saved[id] = *GetRogueUpgrade(id).toggle;
		*GetRogueUpgrade(id).toggle = true;
	}
	Board& board = NewRogueBoard(app);
	runner.Check(board.mRogueRun.active && board.mRogueRun.IsValid() && board.mRogueRun.unlocked == 0 &&
		board.mRogueRun.phase == RoguePhase::Playing && board.mRogueRun.OfferCount() == 0,
		"Fresh endless board starts a valid, empty Playing run");
	for (int id = 0; id < upgradeCount; ++id)
		runner.Check(!board.IsUpgradeEnabled(static_cast<RogueUpgrade>(id)), "Fresh run overrides each true legacy toggle");
	for (int selected = 0; selected < upgradeCount; ++selected)
	{
		board.mRogueRun.unlocked = uint32_t{1} << selected;
		for (int id = 0; id < upgradeCount; ++id)
		{
			*GetRogueUpgrade(id).toggle = false;
			runner.Check(board.IsUpgradeEnabled(static_cast<RogueUpgrade>(id)) == (id == selected),
				"Only this board's unlocked bit enables an upgrade, independent of global toggles");
		}
	}
	for (int id = 0; id < upgradeCount; ++id) *GetRogueUpgrade(id).toggle = true;
	for (bool unlocked : {false, true})
	{
		board.mRogueRun.unlocked = unlocked ? allUpgrades : 0;
		board.mProjectiles.DataArrayFreeAll();
		Plant* left = board.AddPlant(0, 0, SEED_LEFTPEATER);
		runner.Check(board.mProjectiles.mSize == (unlocked ? 1U : 0U) &&
			left->mStateCountdown == (unlocked ? 49 : 0), "Real planting burst follows the run, not the true global flag");
		left->Die();
		board.mProjectiles.DataArrayFreeAll();
		Plant* pea = board.AddPlant(0, 1, SEED_PEASHOOTER);
		for (int shot = 0; shot < 3; ++shot) pea->Fire(nullptr, 1);
		int empowered = 0;
		for (Projectile* projectile : board.mProjectiles) empowered += projectile->mEmpoweredPea;
		runner.Check(board.mProjectiles.mSize == 3 && empowered == (unlocked ? 1 : 0),
			"Real third pea is empowered only with the board unlock");
		pea->Die();
		board.mGridItems.DataArrayFreeAll();
		board.mChallenge->ScaryPotterPopulate();
		GridItem* nut = nullptr;
		for (GridItem* vase : board.mGridItems)
			if (vase->mScaryPotType == SCARYPOT_SEED && vase->mSeedType == SEED_WALLNUT) nut = vase;
		const int cards = CountCoins(board, COIN_USABLE_SEED_PACKET);
		runner.Check(nut != nullptr, "Endless population contains a Wall-nut vase");
		if (nut) board.mChallenge->ScaryPotterOpenPot(nut);
		runner.Check(CountCoins(board, COIN_USABLE_SEED_PACKET) == cards + (unlocked ? 2 : 1),
			"Real Wall-nut vase payout follows the board unlock");
	}
	// Replacing an unlocked board must not carry its run into a new game.
	board.mRogueRun.unlocked = allUpgrades ^ 7U;
	board.mRogueRun.phase = RoguePhase::Reward;
	board.mRogueRun.RollOffers();
	Board& fresh = NewRogueBoard(app);
	runner.Check(fresh.mRogueRun.unlocked == 0 && fresh.mRogueRun.phase == RoguePhase::Playing &&
		fresh.mRogueRun.OfferCount() == 0, "MakeNewBoard resets the previous run");
	app.mGameMode = GAMEMODE_SCARY_POTTER_1;
	app.MakeNewBoard();
	app.mBoard->InitLevel();
	app.mGameScene = SCENE_PLAYING;
	runner.Check(!app.mBoard->mRogueRun.active, "Non-endless Vasebreaker does not start a rogue run");
	for (int id = 0; id < upgradeCount; ++id)
	{
		runner.Check(app.mBoard->IsUpgradeEnabled(static_cast<RogueUpgrade>(id)), "Non-rogue board retains true legacy toggle");
		*GetRogueUpgrade(id).toggle = false;
		runner.Check(!app.mBoard->IsUpgradeEnabled(static_cast<RogueUpgrade>(id)), "Non-rogue board retains false legacy toggle");
		*GetRogueUpgrade(id).toggle = saved[id];
	}
	runner.Finish();
}

void SetupRogueSampling(UnitTestRunner& runner, LawnApp& app)
{
	NewRogueBoard(app);
	RogueRun inactive;
	inactive.RollOffers();
	runner.Check(inactive.IsValid() && !inactive.Choose(0) && inactive.OfferCount() == 0,
		"Inactive model cannot roll or choose");
	RogueRun playing;
	playing.active = true;
	playing.RollOffers();
	runner.Check(playing.IsValid() && playing.phase == RoguePhase::Playing && !playing.Choose(0),
		"Playing cannot roll or choose before a reward");
	for (uint32_t mask = 0; mask <= allUpgrades; ++mask)
	{
		bool valid = true;
		for (int sample = 0; sample < 4; ++sample)
		{
			RogueRun run;
			run.active = true;
			run.unlocked = mask;
			run.phase = RoguePhase::Reward;
			run.RollOffers();
			const int count = std::min(3, upgradeCount - std::popcount(mask) + ((mask & 1U) ? 1 : 0) + ((mask & 8U) ? 1 : 0));
			valid &= run.IsValid() && run.OfferCount() == count &&
				run.phase == (count ? RoguePhase::Choosing : RoguePhase::Advancing);
			uint32_t seen = 0;
			for (int slot = 0; slot < count; ++slot)
			{
				const int id = run.offers[slot];
				if (id < 0 || id >= upgradeCount) { valid = false; continue; }
				const uint32_t bit = uint32_t{1} << id;
				valid &= (seen & bit) == 0;
				seen |= bit;
				RogueRun chosen = run;
				valid &= chosen.Choose(slot) && chosen.unlocked == (mask | bit) && chosen.IsValid() &&
					chosen.phase == RoguePhase::Advancing && chosen.OfferCount() == 0 && !chosen.Choose(slot);
			}
			const RogueRun before = run;
			valid &= !run.Choose(-1) && !run.Choose(count) && !run.Choose(99);
			for (int repeat = 0; repeat < 5; ++repeat) run.RollOffers();
			valid &= SameRun(run, before);
		}
		runner.Check(valid, std::format("Mask {}: distinct locked 3/2/1/0 offers, valid choices, no rerolls or duplicate unlocks", mask));
	}
	RogueRun invalid;
	invalid.active = true;
	invalid.phase = static_cast<RoguePhase>(99);
	runner.Check(!invalid.IsValid(), "Model rejects an invalid phase");
	invalid.phase = RoguePhase::Playing;
	invalid.unlocked = uint32_t{1} << upgradeCount;
	runner.Check(!invalid.IsValid(), "Model rejects unknown unlock bits");
	invalid.unlocked = 0;
	invalid.phase = RoguePhase::Choosing;
	invalid.offers = {0, 0, 1};
	runner.Check(!invalid.IsValid(), "Model rejects duplicate offers");
	runner.Finish();
}

void SetupRogueStages(UnitTestRunner& runner, LawnApp& app)
{
	Board& board = NewRogueBoard(app);
	bool checkedGreen = false;
	// Cross the old every-ten-stages award boundary and keep going after all unlocks.
	for (int stage = 0; stage < 16; ++stage)
	{
		for (Coin* coin : board.mCoins) coin->Die();
		board.ProcessDeleteQueue();
		GridItem* last = nullptr;
		for (GridItem* vase : board.mGridItems)
			if (!vase->mDead && vase->mScaryPotType == SCARYPOT_SEED) last = vase;
		runner.Check(last != nullptr && board.mChallenge->mSurvivalStage == stage, "Next stage has real vases and the expected index");
		if (!last) break;
		for (GridItem* vase : board.mGridItems) if (vase != last) vase->GridItemDie();
		Zombie* zombie = nullptr;
		if (stage % 2)
		{
			zombie = board.AddZombieInRow(ZOMBIE_NORMAL, 2, 0);
			zombie->mPosX = zombie->mX = 500;
		}
		board.mChallenge->ScaryPotterOpenPot(last);
		if (zombie)
		{
			runner.Check(board.mRogueRun.phase == RoguePhase::Playing && !RewardCoin(board),
				"Last vase does not award while an enemy remains");
			zombie->DieWithLoot();
		}
		Coin* reward = RewardCoin(board);
		runner.Check(reward && CountCoins(board, COIN_AWARD_MONEY_BAG) == 1 && board.mLevelAwardSpawned &&
			board.mRogueRun.phase == RoguePhase::Reward,
			std::format("Stage {}: {} creates exactly one moneybag", stage, zombie ? "last zombie death" : "last vase"));
		if (!reward) break;
		board.mChallenge->PuzzlePhaseComplete(2, 2);
		for (int tick = 0; tick < 120; ++tick) board.UpdateLevelEndSequence();
		runner.Check(CountCoins(board, COIN_AWARD_MONEY_BAG) == 1 && board.mNextSurvivalStageCounter == 0 &&
			board.mChallenge->mSurvivalStage == stage && board.mRogueRun.OfferCount() == 0,
			"Repeated completion neither duplicates reward nor starts transition before pickup");
		const int gold = CountCoins(board, COIN_GOLD);
		reward->Collect();
		const RogueRun offered = board.mRogueRun;
		reward->Collect();
		board.mChallenge->PuzzlePhaseComplete(2, 2);
		runner.Check(CountCoins(board, COIN_GOLD) == gold + 5 && SameRun(offered, board.mRogueRun),
			"Repeated pickup pays five gold only once and never rerolls");
		Coin* duplicate = board.AddCoin(300, 200, COIN_AWARD_MONEY_BAG, COIN_MOTION_COIN);
		duplicate->Collect();
		runner.Check(!duplicate->mIsBeingCollected && CountCoins(board, COIN_GOLD) == gold + 5 &&
			SameRun(offered, board.mRogueRun), "A second award object cannot pay outside the Reward phase");
		duplicate->Die();
		if (board.mRogueRun.OfferCount() > 0)
		{
			for (int tick = 0; tick < 120; ++tick) board.UpdateLevelEndSequence();
			runner.Check(board.mRogueRun.IsValid() && board.mRogueRun.phase == RoguePhase::Choosing &&
				board.mNextSurvivalStageCounter == 0 && board.mChallenge->mSurvivalStage == stage,
				"Pickup waits indefinitely for a choice without starting the next stage");
			int slot = 0;
			for (int i = 0; i < board.mRogueRun.OfferCount(); ++i)
				if (board.mRogueRun.offers[i] == static_cast<int>(RogueUpgrade::PlanternGreenVase)) slot = i;
			const int id = board.mRogueRun.offers[slot];
			if (id < 0 || id >= upgradeCount) { runner.Check(false, "Valid offered upgrade required"); break; }
			runner.Check(board.ChooseRogueUpgrade(slot) && board.IsUpgradeEnabled(static_cast<RogueUpgrade>(id)) &&
				board.mChallenge->ScaryPotterCountPots() == 0, "Choice unlocks immediately, before next-stage population");
			runner.Check(!board.ChooseRogueUpgrade(slot), "Duplicate choice cannot unlock or start another stage");
		}
		else
		{
			board.UpdateRogueDialog();
			runner.Check(board.mRogueRun.OfferCount() == 0 && !app.GetDialog(DIALOG_ROGUE_UPGRADE),
				"All-unlocked pickup continues without an empty selection dialog");
		}
		runner.Check(board.mRogueRun.phase == RoguePhase::Advancing && board.mNextSurvivalStageCounter == 100,
			"Exactly one transition is scheduled");
		board.FadeOutLevel();
		runner.Check(board.mNextSurvivalStageCounter == 100, "Repeated fade cannot restart the scheduled transition");
		for (int tick = 0; tick < 99; ++tick) board.UpdateLevelEndSequence();
		runner.Check(board.mChallenge->mSurvivalStage == stage && board.mNextSurvivalStageCounter == 1,
			"Population waits for the full transition countdown");
		board.UpdateLevelEndSequence();
		runner.Check(board.mChallenge->mSurvivalStage == stage + 1 && board.mRogueRun.phase == RoguePhase::Playing &&
			board.mNextSurvivalStageCounter == 0 && board.mChallenge->ScaryPotterCountPots() == 35,
			"Transition advances exactly once and returns to Playing with 35 vases");
		if (board.IsUpgradeEnabled(RogueUpgrade::PlanternGreenVase))
		{
			int greenPlantern = 0;
			for (GridItem* vase : board.mGridItems)
				greenPlantern += !vase->mDead && vase->mScaryPotType == SCARYPOT_SEED &&
					vase->mSeedType == SEED_PLANTERN && vase->mGridItemState == GRIDITEM_STATE_SCARY_POT_LEAF;
			runner.Check(greenPlantern == 1, "Newly unlocked Guiding Light applies to the very next population");
			checkedGreen = true;
		}
		board.UpdateLevelEndSequence();
		runner.Check(board.mChallenge->mSurvivalStage == stage + 1, "Further updates do not advance Playing again");
	}
	runner.Check(checkedGreen && board.mRogueRun.unlocked == allUpgrades && board.mRogueRun.UpgradeLevel(RogueUpgrade::LeftpeaterBurst) == 6 &&
		board.mRogueRun.UpgradeLevel(RogueUpgrade::EmpoweredPea) == 3 && board.mChallenge->mSurvivalStage == 16,
		"Full run unlocks every upgrade and continues beyond the old tenth-stage award boundary");
	runner.Finish();
}

bool RoundTripRogue(UnitTestRunner& runner, LawnApp& app)
{
	const RogueRun expected = app.mBoard->mRogueRun;
	const int stage = app.mBoard->mChallenge->mSurvivalStage;
	const int counter = app.mBoard->mNextSurvivalStageCounter;
	const int pots = app.mBoard->mChallenge->ScaryPotterCountPots();
	const std::string path = app.mCustomSaveDir + "/rogue-roundtrip.dat";
	// ChooseRogueUpgrade's automatic save is deliberately suppressed by the runner.
	const bool saved = LawnSaveGame(app.mBoard, path);
	runner.Check(saved, "Explicitly save rogue state inside the runner sandbox");
	if (!saved) return false;
	app.KillDialog(DIALOG_ROGUE_UPGRADE);
	app.MakeNewBoard();
	const bool loaded = LawnLoadGame(app.mBoard, path);
	runner.Check(loaded, "Load rogue state into a newly constructed endless board");
	if (!loaded) return false;
	runner.Check(SameRun(app.mBoard->mRogueRun, expected) && app.mBoard->mRogueRun.IsValid() &&
		app.mBoard->mChallenge->mSurvivalStage == stage && app.mBoard->mNextSurvivalStageCounter == counter &&
		app.mBoard->mChallenge->ScaryPotterCountPots() == pots,
		std::format("Phase {} roundtrip preserves mask, exact ordered offers, stage, countdown and vases", static_cast<int>(expected.phase)));
	return true;
}

void SetupRoguePersistence(UnitTestRunner& runner, LawnApp& app)
{
	Board& initial = NewRogueBoard(app);
	for (GridItem* vase : initial.mGridItems) vase->GridItemDie();
	initial.mChallenge->PuzzlePhaseComplete(3, 2);
	if (RoundTripRogue(runner, app))
	{
		Coin* reward = RewardCoin(*app.mBoard);
		runner.Check(reward && !reward->mIsBeingCollected && app.mBoard->mNextSurvivalStageCounter == 0,
			"Reward reload retains the uncollected moneybag, not a pending transition");
		if (reward) reward->Collect();
		if (RoundTripRogue(runner, app))
		{
			const int gold = CountCoins(*app.mBoard, COIN_GOLD);
			if (Coin* collected = RewardCoin(*app.mBoard)) collected->Collect();
			runner.Check(CountCoins(*app.mBoard, COIN_GOLD) == gold && app.mBoard->mRogueRun.phase == RoguePhase::Choosing,
				"Choosing reload cannot pay the saved moneybag twice");
			runner.Check(app.mBoard->ChooseRogueUpgrade(1), "Restored offer can be chosen");
			for (int tick = 0; tick < 63; ++tick) app.mBoard->UpdateLevelEndSequence();
			if (RoundTripRogue(runner, app))
			{
				for (int tick = 0; tick < 37; ++tick) app.mBoard->UpdateLevelEndSequence();
				runner.Check(app.mBoard->mChallenge->mSurvivalStage == 1 && app.mBoard->mRogueRun.phase == RoguePhase::Playing,
					"Advancing reload resumes the remaining 37 ticks, not a fresh transition");
				RoundTripRogue(runner, app);
			}
		}
	}
	for (int remaining : {2, 1})
	{
		Board& board = NewRogueBoard(app);
		board.mRogueRun.unlocked = allUpgrades ^ ((uint32_t{1} << remaining) - 1);
		board.mRogueRun.empoweredPeaLevel = 3;
		board.mRogueRun.phase = RoguePhase::Reward;
		board.mRogueRun.RollOffers();
		runner.Check(board.mRogueRun.OfferCount() == remaining, "Reduced offer fixture has the expected count");
		RoundTripRogue(runner, app);
	}
	runner.Finish();
}

void SetupRogueBurstStacks(UnitTestRunner& runner, LawnApp& app)
{
	for (int level = 1; level <= 6; ++level)
	{
		Board& board = NewRogueBoard(app);
		// All other upgrades are exhausted, so the burst is the only candidate.
		board.mRogueRun.unlocked = allUpgrades;
		board.mRogueRun.leftpeaterBurstLevel = level;
		board.mRogueRun.empoweredPeaLevel = 3;
		runner.Check(RoundTripRogue(runner, app), "Stack level survives save/load");
		Board& loaded = *app.mBoard;
		loaded.mProjectiles.DataArrayFreeAll();
		Plant* plant = loaded.AddPlant(0, 0, SEED_LEFTPEATER);
		const int duration = plant->mStateCountdown - 1;
		int previous = 0;
		bool cadence = loaded.mProjectiles.mSize == 1;
		for (int tick = 1; tick <= duration; ++tick)
		{
			const auto before = loaded.mProjectiles.mSize;
			plant->UpdateAbilities();
			if (loaded.mProjectiles.mSize != before)
			{
				cadence &= loaded.mProjectiles.mSize == before + 1 && tick - previous == 16;
				previous = tick;
			}
		}
		runner.Check(cadence && loaded.mProjectiles.mSize == static_cast<unsigned>(4 << (level - 1)) &&
			previous == duration, std::format("Level {} fires the exact pea count at fixed 16-tick intervals", level));
		loaded.mRogueRun.phase = RoguePhase::Reward;
		loaded.mRogueRun.RollOffers();
		runner.Check(loaded.mRogueRun.IsValid() && loaded.mRogueRun.OfferCount() == (level < 6 ? 1 : 0),
			"Burst remains selectable until level six, then leaves the pool");
		if (level < 6)
			runner.Check(loaded.mRogueRun.Choose(0) && loaded.mRogueRun.UpgradeLevel(RogueUpgrade::LeftpeaterBurst) == level + 1,
				"Repeat choice advances exactly one level");
		else runner.Check(!loaded.mRogueRun.Choose(0), "Seventh pick is rejected");
	}
	Board& board = NewRogueBoard(app);
	board.mRogueRun.unlocked = 1;
	board.mRogueRun.leftpeaterBurstLevel = 6;
	Plant* plant = board.AddPlant(0, 0, SEED_LEFTPEATER);
	for (int tick = 0; tick < 23; ++tick) plant->UpdateAbilities();
	const int remaining = plant->mStateCountdown;
	if (RoundTripRogue(runner, app))
	{
		Plant* restored = app.mBoard->GetTopPlantAt(0, 0, PlantPriority::TOPPLANT_BUNGEE_ORDER);
		runner.Check(restored && restored->mStateCountdown == remaining, "Mid-volley save preserves remaining burst time");
		if (restored)
		{
			for (int tick = 0; tick < remaining - 1; ++tick) restored->UpdateAbilities();
			runner.Check(app.mBoard->mProjectiles.mSize == 128, "Resumed volley finishes with exactly 128 peas and no restart");
		}
	}
	runner.Finish();
}

void SetupRogueEmpoweredStacks(UnitTestRunner& runner, LawnApp& app)
{
	for (int level = 1; level <= 3; ++level)
	{
		Board& board = NewRogueBoard(app);
		board.mRogueRun.unlocked = allUpgrades;
		board.mRogueRun.leftpeaterBurstLevel = 6;
		board.mRogueRun.empoweredPeaLevel = level;
		board.mProjectiles.DataArrayFreeAll();
		Plant* first = board.AddPlant(0, 0, SEED_PEASHOOTER);
		Plant* second = board.AddPlant(0, 1, SEED_PEASHOOTER);
		first->Fire(nullptr, 0);
		second->Fire(nullptr, 1);
		bool pattern = true;
		for (Projectile* pea : board.mProjectiles) pattern &= pea->mEmpoweredPea == (level == 3);
		if (!RoundTripRogue(runner, app)) continue;
		Board& restored = *app.mBoard;
		first = restored.GetTopPlantAt(0, 0, PlantPriority::TOPPLANT_BUNGEE_ORDER);
		second = restored.GetTopPlantAt(0, 1, PlantPriority::TOPPLANT_BUNGEE_ORDER);
		runner.Check(first && second, "Both Peashooters survive the stack-level roundtrip");
		if (!first || !second) continue;
		for (int shot = 2; shot <= 6; ++shot)
		{
			restored.mProjectiles.DataArrayFreeAll();
			first->Fire(nullptr, 0);
			second->Fire(nullptr, 1);
			pattern &= restored.mProjectiles.mSize == 2;
			for (Projectile* pea : restored.mProjectiles)
				pattern &= pea->mEmpoweredPea == (shot % (4 - level) == 0) &&
					pea->GetVisualScale() == (pea->mEmpoweredPea ? 1.5f : 1.0f);
		}
		runner.Check(pattern, std::format("Heavy Peas level {}: every {} shot is empowered independently, including after reload", level, 4 - level));
		restored.mRogueRun.phase = RoguePhase::Reward;
		restored.mRogueRun.RollOffers();
		runner.Check(restored.mRogueRun.IsValid() && restored.mRogueRun.OfferCount() == (level < 3 ? 1 : 0),
			"Heavy Peas remains in the pool until level three, then leaves");
		if (level < 3)
			runner.Check(restored.mRogueRun.offers[0] == static_cast<int>(RogueUpgrade::EmpoweredPea) &&
				restored.mRogueRun.Choose(0) && restored.mRogueRun.UpgradeLevel(RogueUpgrade::EmpoweredPea) == level + 1,
				"Repeat Heavy Peas choice advances one level");
		else runner.Check(!restored.mRogueRun.Choose(0), "Fourth Heavy Peas pick is rejected");
	}
	for (int burst = 0; burst <= 6; ++burst)
	for (int empowered = 0; empowered <= 3; ++empowered)
	{
		RogueRun run;
		run.active = true;
		run.unlocked = allUpgrades & ~9U;
		if (burst) run.unlocked |= 1U;
		if (empowered) run.unlocked |= 8U;
		run.leftpeaterBurstLevel = burst;
		run.empoweredPeaLevel = empowered;
		run.phase = RoguePhase::Reward;
		run.RollOffers();
		runner.Check(run.IsValid() && run.OfferCount() == (burst < 6) + (empowered < 3),
			std::format("Mixed levels burst={}/heavy={}: only eligible upgrades offered", burst, empowered));
	}
	runner.Finish();
}

void SetupRogueInvalidSaves(UnitTestRunner& runner, LawnApp& app)
{
	Board& board = NewRogueBoard(app);
	board.mRogueRun.phase = RoguePhase::Reward;
	board.mRogueRun.RollOffers();
	const std::string path = app.mCustomSaveDir + "/rogue-invalid.dat";
	Sexy::Buffer buffer;
	const bool prepared = LawnSaveGame(&board, path) && app.ReadBufferFromFile(path, &buffer, false);
	runner.Check(prepared, "Prepare a valid Choosing save for checksum-correct corruption tests");
	if (!prepared) { runner.Finish(); return; }
	const auto original = buffer.mData;
	auto read32 = [&](size_t pos)
	{
		uint32_t value = 0;
		for (int byte = 0; byte < 4; ++byte) value |= uint32_t(original.at(pos + byte)) << (byte * 8);
		return value;
	};
	size_t chunk = 24;
	while (chunk + 8 <= original.size() && read32(chunk) != 21)
	{
		const size_t size = read32(chunk + 4);
		if (size > original.size() - chunk - 8) break;
		chunk += 8 + size;
	}
	// SAVE4 header: 24 bytes. Chunk 21: version, field header, schema, active byte, mask, phase, offers.
	const bool schema = chunk + 53 <= original.size() && read32(chunk) == 21 && read32(chunk + 4) == 45 &&
		read32(chunk + 8) == 1 && read32(chunk + 12) == 1 && read32(chunk + 16) == 33 && read32(chunk + 20) == 3;
	runner.Check(schema, "Writer emits mandatory chunk 21 with schema-3 stack counts");
	if (!schema) { runner.Finish(); return; }
	app.MakeNewBoard();
	runner.Check(LawnLoadGame(app.mBoard, path), "Unmodified source save loads before corruption");
	constexpr std::array names{"missing chunk 21 (pre-rogue SAVE4)", "duplicate chunk 21", "invalid phase",
		"unknown unlock bit", "duplicate offers", "already-unlocked offer", "invalid active boolean",
		"inactive endless run", "unknown schema", "out-of-range offer", "invalid negative offer", "Ended phase",
		"burst level above six", "burst level without unlock", "empowered level above three", "empowered level without unlock"};
	for (int variant = 0; variant < static_cast<int>(names.size()); ++variant)
	{
		auto bytes = original;
		auto write32 = [&](size_t pos, uint32_t value)
		{
			for (int byte = 0; byte < 4; ++byte) bytes.at(pos + byte) = static_cast<unsigned char>(value >> (8 * byte));
		};
		switch (variant)
		{
		case 0: bytes.erase(bytes.begin() + chunk, bytes.begin() + chunk + 53); break;
		case 1: bytes.insert(bytes.end(), original.begin() + chunk, original.begin() + chunk + 53); break;
		case 2: write32(chunk + 29, 99); break;
		case 3: write32(chunk + 25, uint32_t{1} << 31); break;
		case 4: write32(chunk + 37, read32(chunk + 33)); break;
		case 5: write32(chunk + 25, uint32_t{1} << read32(chunk + 33));
			write32(chunk + 45, read32(chunk + 33) == 0 ? 6 : 0);
			write32(chunk + 49, read32(chunk + 33) == 3 ? 3 : 0); break;
		case 6: bytes.at(chunk + 24) = 2; break;
		case 7: bytes.at(chunk + 24) = 0; break;
		case 8: write32(chunk + 20, 99); break;
		case 9: write32(chunk + 33, upgradeCount); break;
		case 10: write32(chunk + 33, static_cast<uint32_t>(-2)); break;
		case 11: write32(chunk + 29, static_cast<uint32_t>(RoguePhase::Ended)); break;
		case 12: write32(chunk + 45, 7); break;
		case 13: write32(chunk + 25, 0); write32(chunk + 45, 1); break;
		case 14: write32(chunk + 49, 4); break;
		case 15: write32(chunk + 25, 0); write32(chunk + 49, 1); break;
		}
		write32(16, static_cast<uint32_t>(bytes.size() - 24));
		write32(20, crc32(0, bytes.data() + 24, static_cast<uInt>(bytes.size() - 24)));
		buffer.SetData(bytes);
		const bool written = app.WriteBufferToFile(path, &buffer);
		runner.Check(written, std::format("Write CRC-correct {} fixture", names[variant]));
		Board& fresh = NewRogueBoard(app);
		fresh.mChallenge->mSurvivalStage = 73;
		const RogueRun before = fresh.mRogueRun;
		if (written)
			runner.Check(!LawnLoadGame(&fresh, path) && SameRun(fresh.mRogueRun, before) &&
				fresh.mChallenge->mSurvivalStage == 73 && fresh.mChallenge->ScaryPotterCountPots() == 35,
				std::format("Reject {} before mutating the destination board", names[variant]));
	}
	// Old saves preserve the exact reduced offer list, while newly repeatable
	// upgrades become level one and eligible for future rewards.
	for (uint32_t legacySchema : {1U, 2U})
	{
		auto legacy = original;
		auto writeLegacy = [&](size_t pos, uint32_t value)
		{
			for (int byte = 0; byte < 4; ++byte) legacy.at(pos + byte) = static_cast<unsigned char>(value >> (8 * byte));
		};
		const uint32_t legacySize = legacySchema == 1 ? 25 : 29;
		legacy.erase(legacy.begin() + chunk + 8 + 12 + legacySize, legacy.begin() + chunk + 53);
		writeLegacy(chunk + 4, 12 + legacySize);
		writeLegacy(chunk + 16, legacySize);
		writeLegacy(chunk + 20, legacySchema);
		writeLegacy(chunk + 25, allUpgrades ^ 128U);
		writeLegacy(chunk + 33, 7);
		writeLegacy(chunk + 37, static_cast<uint32_t>(-1));
		writeLegacy(chunk + 41, static_cast<uint32_t>(-1));
		if (legacySchema == 2) writeLegacy(chunk + 45, 6);
		writeLegacy(16, static_cast<uint32_t>(legacy.size() - 24));
		writeLegacy(20, crc32(0, legacy.data() + 24, static_cast<uInt>(legacy.size() - 24)));
		buffer.SetData(legacy);
		Board& old = NewRogueBoard(app);
		runner.Check(app.WriteBufferToFile(path, &buffer) && LawnLoadGame(&old, path) &&
			old.mRogueRun.UpgradeLevel(RogueUpgrade::LeftpeaterBurst) == (legacySchema == 1 ? 1 : 6) &&
			old.mRogueRun.UpgradeLevel(RogueUpgrade::EmpoweredPea) == 1 && old.mRogueRun.CanChoose(RogueUpgrade::EmpoweredPea) &&
			old.mRogueRun.offers == std::array<int32_t, 3>{7, -1, -1},
			std::format("Schema {} preserves burst level and exact old offers; Heavy Peas starts at level one", legacySchema));
	}
	Board& invalid = NewRogueBoard(app);
	invalid.mRogueRun.phase = RoguePhase::Advancing;
	runner.Check(!LawnSaveGame(&invalid, path), "Writer rejects Advancing without a countdown");
	invalid.mRogueRun.phase = RoguePhase::Reward;
	invalid.mNextSurvivalStageCounter = 100;
	runner.Check(!LawnSaveGame(&invalid, path), "Writer rejects Reward with an already-started countdown");
	runner.Finish();
}

void ClickRogueBoard(Board& board, int x, int y)
{
	// Synchronous synthetic input only: never pump events with the live-input guard lifted.
	UnitTestRunner* active = UnitTestRunner::active;
	UnitTestRunner::active = nullptr;
	board.MouseDown(x, y, 1);
	UnitTestRunner::active = active;
}

void SetupRogueInput(UnitTestRunner& runner, LawnApp& app)
{
	Board& board = NewRogueBoard(app);
	Plant* nut = board.AddPlant(0, 2, SEED_WALLNUT);
	board.mRogueRun.phase = RoguePhase::Reward;
	board.mRogueRun.RollOffers();
	const RogueRun offered = board.mRogueRun;
	const int launchCounter = nut->mLaunchCounter;
	const uint32_t mainCounter = board.mMainCounter;
	UnitTestRunner* active = UnitTestRunner::active;
	UnitTestRunner::active = nullptr;
	board.Update();
	UnitTestRunner::active = active;
	runner.Check(nut->mLaunchCounter == launchCounter && board.mMainCounter == mainCounter &&
		board.mNextSurvivalStageCounter == 0 && SameRun(board.mRogueRun, offered),
		"Production board update freezes gameplay while choice is pending, even before modal creation");
	board.UpdateRogueDialog(); // The app intentionally skips this call while the runner is active.
	auto* dialog = static_cast<RogueUpgradeDialog*>(app.GetDialog(DIALOG_ROGUE_UPGRADE));
	runner.Check(dialog && app.mWidgetManager->mBaseModalWidget == dialog && app.mWidgetManager->mFocusWidget == dialog,
		"UpdateRogueDialog registers a real modal through AddDialog and gives it keyboard focus");
	if (!dialog) { runner.Finish(); return; }
	board.UpdateRogueDialog();
	runner.Check(app.GetDialog(DIALOG_ROGUE_UPGRADE) == dialog && app.GetDialogCount() == 1,
		"Repeated UpdateRogueDialog does not replace or stack the selection");
	const PlantID nutID = static_cast<PlantID>(board.mPlants.DataArrayGetID(nut));
	board.mLastClickedPlantID = nutID;
	board.mCursorObject->mCursorType = CURSOR_TYPE_SHOVEL;
	ClickRogueBoard(board, nut->mX + 40, nut->mY + 40);
	runner.Check(!nut->mDead && board.mCursorObject->mCursorType == CURSOR_TYPE_SHOVEL && board.mLastClickedPlantID == nutID,
		"Choosing blocks real board shovel input even with the runner guard lifted");
	board.ClearCursor();
	const Rect first = dialog->CardRect(0), second = dialog->CardRect(1);
	dialog->KeyDown(KEYCODE_RETURN);
	dialog->MouseDown(first.mX + 20, first.mY + 20, 1);
	dialog->MouseUp(first.mX + 20, first.mY + 20);
	runner.Check(SameRun(board.mRogueRun, offered), "Opening delay ignores keyboard and mouse carry-through");
	for (int tick = 0; tick < 16; ++tick) dialog->Update();
	dialog->KeyDown(KEYCODE_RETURN);
	runner.Check(SameRun(board.mRogueRun, offered), "Held Return does not become a choice when the delay expires");
	dialog->KeyUp(KEYCODE_RETURN);
	dialog->KeyDown(KEYCODE_ESCAPE);
	dialog->KeyUp(KEYCODE_ESCAPE);
	dialog->MouseDown(first.mX + 20, first.mY + 20, -1);
	dialog->MouseUp(first.mX + 20, first.mY + 20, -1);
	dialog->MouseDown(first.mX + 20, first.mY + 20, 1);
	dialog->MouseUp(second.mX + 20, second.mY + 20);
	runner.Check(SameRun(board.mRogueRun, offered) && app.GetDialog(DIALOG_ROGUE_UPGRADE) == dialog,
		"Escape, right-click and mismatched press/release cannot dismiss or select");
	dialog->MouseDown(second.mX + 20, second.mY + 20, 1);
	dialog->MouseUp(second.mX + 20, second.mY + 20);
	const uint32_t selected = uint32_t{1} << offered.offers[1];
	runner.Check(board.mRogueRun.unlocked == selected && board.mRogueRun.phase == RoguePhase::Advancing &&
		board.mNextSurvivalStageCounter == 100, "Actual matching card input commits exactly the displayed second offer");
	dialog->KeyDown(static_cast<Sexy::KeyCode>('1'));
	dialog->MouseDown(first.mX + 20, first.mY + 20, 1);
	dialog->MouseUp(first.mX + 20, first.mY + 20);
	runner.Check(board.mRogueRun.unlocked == selected, "Pending-close UI cannot commit a second selection");
	dialog->Update(); // Self-removal uses KillDialog; never delete an app-registered widget directly.
	runner.Check(!app.GetDialog(DIALOG_ROGUE_UPGRADE), "Chosen dialog safely unregisters on its next manual update");

	board.mRogueRun.phase = RoguePhase::Reward;
	board.mNextSurvivalStageCounter = 0;
	board.mRogueRun.RollOffers();
	board.UpdateRogueDialog();
	dialog = static_cast<RogueUpgradeDialog*>(app.GetDialog(DIALOG_ROGUE_UPGRADE));
	runner.Check(dialog != nullptr, "A later stage can open another upgrade dialog");
	if (dialog)
	{
		std::swap(board.mRogueRun.offers[0], board.mRogueRun.offers[1]);
		const RogueRun changed = board.mRogueRun;
		for (int tick = 0; tick < 16; ++tick)
		{
			if (!app.GetDialog(DIALOG_ROGUE_UPGRADE)) break;
			dialog->Update();
		}
		runner.Check(!app.GetDialog(DIALOG_ROGUE_UPGRADE) && SameRun(board.mRogueRun, changed),
			"Stale dialog unregisters without selecting or rerolling changed offers");
		board.UpdateRogueDialog();
		runner.Check(app.GetDialog(DIALOG_ROGUE_UPGRADE) && SameRun(board.mRogueRun, changed),
			"Reopening displays the exact pending offer order without a reroll");
	}
	app.KillDialog(DIALOG_ROGUE_UPGRADE);
	runner.Finish();
}

void SetupRogueFinalScene(UnitTestRunner& runner, LawnApp& app)
{
	Board& board = NewRogueBoard(app);
	board.mChallenge->mSurvivalStage = 3;
	board.mRogueRun.unlocked = uint32_t{1} << static_cast<int>(RogueUpgrade::LeftpeaterBurst);
	board.mRogueRun.phase = RoguePhase::Choosing;
	board.mRogueRun.offers = {static_cast<int>(RogueUpgrade::EmpoweredPea),
		static_cast<int>(RogueUpgrade::ThreepeaterHoming), static_cast<int>(RogueUpgrade::WallnutBowling)};
	board.UpdateRogueDialog();
	auto* dialog = static_cast<RogueUpgradeDialog*>(app.GetDialog(DIALOG_ROGUE_UPGRADE));
	runner.Check(board.mRogueRun.IsValid() && dialog, "Final scene contains the real dialog with three valid pending offers");
	if (dialog)
	{
		for (int tick = 0; tick < 16; ++tick) dialog->Update();
		// Leave real rendering intact, but detach live input for the five-second screenshot hold.
		dialog->SetDisabled(true);
		// SetDisabled clears the modal pointer as well as focus; keep blocking the lawn.
		app.mWidgetManager->mBaseModalWidget = dialog;
		dialog->mMouseVisible = false;
		dialog->MarkDirty();
		runner.Check(app.mWidgetManager->mFocusWidget == nullptr && board.mRogueRun.phase == RoguePhase::Choosing &&
			board.mRogueRun.OfferCount() == 3, "Final pending selection remains visible but cannot receive live input");
	}
	runner.Finish();
}

void SetupRogueLoss(UnitTestRunner& runner, LawnApp& app)
{
	Board& board = NewRogueBoard(app);
	board.mRogueRun.unlocked = 3;
	const std::string path = GetSavedGameName(app.mGameMode, app.mPlayerInfo->mId);
	MkDir(GetAppDataPath("userdata"));
	runner.Check(LawnSaveGame(&board, path), "Create live run checkpoint before loss");
	board.ZombiesWon(nullptr);
	runner.Check(board.mRogueRun.phase == RoguePhase::Ended && !board.NeedSaveGame() &&
		!LawnSaveGame(&board, path), "Lost run cannot be saved, including shutdown after game over");
	Buffer buffer;
	runner.Check(!app.ReadBufferFromFile(path, &buffer, false), "Loss invalidates the previous checkpoint");
	app.KillDialog(DIALOG_GAME_OVER);
	Board& fresh = NewRogueBoard(app);
	runner.Check(fresh.mRogueRun.phase == RoguePhase::Playing && fresh.mRogueRun.unlocked == 0,
		"New board after loss starts with no upgrades");
	runner.Finish();
}
}

void RegisterRogueTests(UnitTestRunner& runner)
{
	runner.Register({"rogue fresh state and board-scoped effects", SetupRogueScope, nullptr, nullptr, 1});
	runner.Register({"rogue locked sampling and immutable offers", SetupRogueSampling, nullptr, nullptr, 1});
	runner.Register({"rogue rewards and complete stage progression", SetupRogueStages, nullptr, nullptr, 1});
	runner.Register({"rogue four-phase persistence and exact offers", SetupRoguePersistence, nullptr, nullptr, 1});
	runner.Register({"rogue burst stacks, timing and persistence", SetupRogueBurstStacks, nullptr, nullptr, 1});
	runner.Register({"rogue Heavy Peas stacks and independent shot counters", SetupRogueEmpoweredStacks, nullptr, nullptr, 1});
	runner.Register({"rogue mandatory chunk and invalid save rejection", SetupRogueInvalidSaves, nullptr, nullptr, 1});
	runner.Register({"rogue modal input and dialog lifecycle", SetupRogueInput, nullptr, nullptr, 1});
	runner.Register({"rogue loss and restart", SetupRogueLoss, nullptr, nullptr, 1});
	// Keep last: Finish leaves this real, read-only dialog drawing during the runner's final hold.
	runner.Register({"rogue upgrade selection final scene", SetupRogueFinalScene, nullptr, nullptr, 1});
}
