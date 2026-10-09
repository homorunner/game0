#include "UnitTestRunner.h"
#include "GameConstants.h"
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Challenge.h"
#include "Lawn/Coin.h"
#include "Lawn/CursorObject.h"
#include "Lawn/GridItem.h"
#include "Lawn/Plant.h"
#include "Lawn/Projectile.h"
#include "Lawn/SeedPacket.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/System/SaveGame.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/Attachment.h"
#include "PvzpLib/ReanimAtlas.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/PvzpParticle.h"
#include "misc/Buffer.h"
#include "zlib.h"
#include <array>
#include <algorithm>
#include <format>
#include <cmath>

namespace
{
ZombieID target;
PlantID shooter;
float targetX;
int shots, lastHealth;
bool sawDeath;
constexpr std::array shotTicks{0, 16, 32, 48};

std::array<PlantID, 5> peaPlants;
std::array<ZombieID, 5> peaTargets;
std::array<int, 5> peaShots;
bool savedEmpowered;

std::array<unsigned int, 3> threePeas;
std::array<ZombieID, 5> threeTargets;
bool threeHomed, threeFire;
bool threeRenderOrder;

Zombie* AddThreeTarget(Board& board, int row, ZombieType type = ZOMBIE_NORMAL)
{
	Zombie* zombie = board.AddZombieInRow(type, row, 0);
	zombie->mPosX = zombie->mX = 600;
	zombie->mVelX = 0;
	zombie->SetAnimRate(0);
	zombie->mBodyHealth = zombie->mBodyMaxHealth = 1000;
	zombie->mDroppedLoot = true;
	threeTargets[row] = board.ZombieGetID(zombie);
	return zombie;
}

Plant* SetupThreepeaterBoard(LawnApp& app)
{
	app.mGameMode = GAMEMODE_ADVENTURE;
	app.mPlayerInfo->mLevel = 11;
	app.MakeNewBoard();
	Board& board = *app.mBoard;
	board.InitLevel();
	app.mGameScene = SCENE_PLAYING;
	board.mMouseVisible = false;
	threePeas.fill(0);
	threeTargets.fill(ZombieID::ZOMBIEID_NULL);
	threeHomed = threeFire = false;
	threeRenderOrder = true;
	Plant* plant = board.AddPlant(1, 2, SEED_THREEPEATER);
	plant->mLaunchCounter = 10000;
	return plant;
}

void ClickPlant(Board& board, Plant* plant, int count)
{
	// Synchronous synthetic input only; no event pumping while the live-input guard is lifted.
	UnitTestRunner* active = UnitTestRunner::active;
	UnitTestRunner::active = nullptr;
	board.MouseDown(plant->mX + 40, plant->mY + 40, count);
	UnitTestRunner::active = active;
}

template<bool enabled>
void SetupWallnutVases(UnitTestRunner& runner, LawnApp& app)
{
	const bool saved = ENABLE_WALLNUT_DOUBLE_VASE_CARDS;
	ENABLE_WALLNUT_DOUBLE_VASE_CARDS = enabled;
	app.mGameMode = GAMEMODE_SCARY_POTTER_ENDLESS;
	app.MakeNewBoard();
	Board& board = *app.mBoard;
	board.mRogueRun.active = false; // Exercise the individual toggle outside run progression.
	board.InitLevel();
	app.mGameScene = SCENE_PLAYING;
	GridItem* nutVase = nullptr;
	GridItem* peaVase = nullptr;
	for (GridItem* vase : board.mGridItems)
	{
		if (vase->mScaryPotType != SCARYPOT_SEED) continue;
		if (vase->mSeedType == SEED_WALLNUT) nutVase = vase;
		if (vase->mSeedType == SEED_PEASHOOTER) peaVase = vase;
	}
	runner.Check(nutVase && peaVase, "Real endless inventory contains wallnut and peashooter vases");
	if (nutVase && peaVase)
	{
		board.mChallenge->ScaryPotterOpenPot(nutVase);
		std::vector<Coin*> cards;
		for (Coin* coin : board.mCoins)
			if (!coin->mDead && coin->mType == COIN_USABLE_SEED_PACKET && coin->mUsableSeedType == SEED_WALLNUT)
				cards.push_back(coin);
		runner.Check(nutVase->mDead && cards.size() == (enabled ? 2U : 1U),
			std::format("Wallnut vase enabled={}: correct independently allocated card count", enabled));
		if (cards.size() == 2)
			runner.Check(std::abs(cards[0]->mPosX - cards[1]->mPosX) >= 40, "Two cards spawn separated horizontally");
		int row = 0;
		for (Coin* card : cards)
		{
			card->Collect();
			board.ClearCursor();
			runner.Check(!card->mDead, "Cancelling a wallnut card does not consume it");
			card->Collect();
			board.MouseDownWithPlant(board.GridToPixelX(0, row) + 40, board.GridToPixelY(0, row) + 40, 1);
			Plant* nut = board.GetTopPlantAt(0, row++, TOPPLANT_ONLY_NORMAL_POSITION);
			runner.Check(card->mDead && nut && nut->mSeedType == SEED_WALLNUT && !nut->IsBowling(),
				"Each card independently plants one stationary wallnut");
		}
		board.mChallenge->ScaryPotterOpenPot(peaVase);
		int peas = 0;
		for (Coin* coin : board.mCoins)
			peas += !coin->mDead && coin->mType == COIN_USABLE_SEED_PACKET && coin->mUsableSeedType == SEED_PEASHOOTER;
		runner.Check(peas == 1, "Other seed vases still drop one card");
	}
	ENABLE_WALLNUT_DOUBLE_VASE_CARDS = saved;
	runner.Finish();
}

void SetupWallnutInput(UnitTestRunner& runner, LawnApp& app)
{
	SetupThreepeaterBoard(app);
	Board& board = *app.mBoard;
	const bool saved = ENABLE_WALLNUT_DOUBLE_CLICK_BOWLING;
	Plant* nut = board.AddPlant(3, 2, SEED_WALLNUT);
	ENABLE_WALLNUT_DOUBLE_CLICK_BOWLING = false;
	ClickPlant(board, nut, 1);
	ClickPlant(board, nut, 2);
	runner.Check(!nut->IsBowling(), "Disabled double click leaves wallnut planted");
	ENABLE_WALLNUT_DOUBLE_CLICK_BOWLING = true;
	ClickPlant(board, nut, 2);
	runner.Check(!nut->IsBowling(), "Double click without preceding click on this plant cannot launch");
	for (int count : {1, -1, 3}) ClickPlant(board, nut, count);
	ClickPlant(board, nut, 2);
	runner.Check(!nut->IsBowling(), "Single, right and middle clicks cannot launch or arm a double click");
	Plant* other = board.AddPlant(4, 2, SEED_WALLNUT);
	ClickPlant(board, nut, 1);
	ClickPlant(board, other, 2);
	runner.Check(!other->IsBowling(), "Two different plants do not make a double click");
	board.mPaused = true;
	ClickPlant(board, nut, 1);
	ClickPlant(board, nut, 2);
	runner.Check(!nut->IsBowling(), "Paused board rejects launches");
	board.mPaused = false;
	ClickPlant(board, other, 1);
	board.mCursorObject->mCursorType = CURSOR_TYPE_SHOVEL;
	ClickPlant(board, other, 2);
	runner.Check(other->mDead && !other->IsBowling(), "Double click with shovel still digs instead of bowling");
	board.ClearCursor();
	for (bool grabbed : {false, true})
	{
		nut->mSquished = !grabbed;
		nut->mOnBungeeState = grabbed ? GETTING_GRABBED_BY_BUNGEE : NOT_ON_BUNGEE;
		ClickPlant(board, nut, 1);
		ClickPlant(board, nut, 2);
		runner.Check(!nut->IsBowling(), "Squished or bungee-grabbed wallnuts cannot launch");
	}
	nut->mSquished = false;
	nut->mOnBungeeState = NOT_ON_BUNGEE;
	for (SeedType seed : {SEED_TALLNUT, SEED_EXPLODE_O_NUT, SEED_PEASHOOTER})
	{
		Plant* excluded = board.AddPlant(5, 1, seed);
		ClickPlant(board, excluded, 1);
		ClickPlant(board, excluded, 2);
		runner.Check(!excluded->IsBowling(), "Only normal wallnuts can launch");
		excluded->Die();
	}
	nut->mPlantHealth = 1000;
	nut->mRecentlyEatenCountdown = 50;
	nut->AnimateNuts();
	ClickPlant(board, nut, 1);
	ClickPlant(board, nut, 2);
	runner.Check(nut->mState == STATE_BOWLING_STRAIGHT && nut->mPlantHealth == 1000 &&
		app.ReanimationGet(nut->mBodyReanimID)->mAnimRate >= 12, "Double click starts damaged nut straight with bowling animation");
	runner.Check(nut->mRenderOrder == board.MakeRenderOrder(RENDER_LAYER_PROJECTILE, 2, PLANT_ORDER_NORMAL * 5 - nut->mX + 800),
		"Launched nut uses bowling projectile render layer");
	runner.Check(board.GetTopPlantAt(3, 2, TOPPLANT_ONLY_NORMAL_POSITION) == nullptr &&
		board.CanPlantAt(3, 2, SEED_WALLNUT) == PLANTING_OK, "Rolling immediately frees the planting cell");
	Zombie* zombie = AddThreeTarget(board, 2);
	runner.Check(!zombie->CanTargetPlant(nut, ATTACKTYPE_CHEW), "Moving wallnut is no longer zombie food");
	const int x = nut->mX;
	ENABLE_WALLNUT_DOUBLE_CLICK_BOWLING = false;
	for (int tick = 0; tick < 10; ++tick) nut->Update();
	runner.Check(nut->mX > x && nut->mRow == 2 && zombie->mBodyHealth == 1000,
		"Roller keeps moving straight after disabling toggle, without hitting distant zombies");
	Plant* replacement = board.AddPlant(3, 2, SEED_WALLNUT);
	GridItem* ladder = board.AddALadder(3, 2);
	nut->mX = 801;
	nut->UpdateBowling();
	runner.Check(nut->mDead && !replacement->mDead && !ladder->mDead,
		"Offscreen cleanup does not remove replacement plants or their ladders");
	ENABLE_WALLNUT_DOUBLE_CLICK_BOWLING = saved;
	runner.Finish();
}

void SetupBowlingDamage(UnitTestRunner& runner, LawnApp& app)
{
	SetupThreepeaterBoard(app);
	Board& board = *app.mBoard;
	const bool saved = ENABLE_WALLNUT_DOUBLE_CLICK_BOWLING;
	for (bool stock : {false, true})
	for (SeedType seed : {SEED_WALLNUT, SEED_GIANT_WALLNUT})
	for (ZombieType type : {ZOMBIE_NORMAL, ZOMBIE_GARGANTUAR, ZOMBIE_REDEYE_GARGANTUAR})
	{
		if (!stock && seed == SEED_GIANT_WALLNUT) continue;
		app.mGameMode = stock ? GAMEMODE_CHALLENGE_WALLNUT_BOWLING : GAMEMODE_ADVENTURE;
		ENABLE_WALLNUT_DOUBLE_CLICK_BOWLING = !stock;
		Plant* nut = board.AddPlant(3, 2, seed);
		if (!stock) nut->MouseDown(0, 0, 2);
		ENABLE_WALLNUT_DOUBLE_CLICK_BOWLING = false;
		Zombie* zombie = AddThreeTarget(board, 2, type);
		zombie->mBodyHealth = zombie->mBodyMaxHealth = 5000;
		const Rect rect = zombie->GetZombieRect();
		zombie->mPosX = zombie->mX += nut->mX + 20 - rect.mX;
		nut->UpdateBowling();
		runner.Check(zombie->mBodyHealth == 5000 - (type == ZOMBIE_NORMAL ? 1800 : 600),
			std::format("Global bowling damage: stock={} seed={} zombie={}", stock, static_cast<int>(seed), static_cast<int>(type)));
		if (seed == SEED_WALLNUT)
			runner.Check(nut->mRow != 2 && (nut->mState == STATE_BOWLING_UP || nut->mState == STATE_BOWLING_DOWN),
				"Normal nut ricochets to the adjacent row after contact");
		nut->Die();
		zombie->DieNoLoot();
	}
	for (bool stock : {false, true})
	for (bool bounced : {false, true})
	{
		app.mGameMode = stock ? GAMEMODE_CHALLENGE_WALLNUT_BOWLING : GAMEMODE_ADVENTURE;
		ENABLE_WALLNUT_DOUBLE_CLICK_BOWLING = true;
		Plant* nut = board.AddPlant(3, 2, SEED_WALLNUT);
		if (!stock) nut->MouseDown(0, 0, 2);
		if (bounced) nut->mState = STATE_BOWLING_DOWN;
		Zombie* zombie = AddThreeTarget(board, 2, ZOMBIE_DOOR);
		zombie->mBodyHealth = zombie->mBodyMaxHealth = 5000;
		zombie->mShieldHealth = zombie->mShieldMaxHealth = 5000;
		const Rect rect = zombie->GetZombieRect();
		zombie->mPosX = zombie->mX += nut->mX + 20 - rect.mX;
		nut->UpdateBowling();
		runner.Check(zombie->mBodyHealth == 5000 && zombie->mShieldHealth == (bounced ? 3200 : 4600),
			std::format("Door shield: stock={} bounced={}; straight deals 400, ricochet deals 1800", stock, bounced));
		nut->Die();
		zombie->DieNoLoot();
	}
	app.mGameMode = GAMEMODE_ADVENTURE;
	ENABLE_WALLNUT_DOUBLE_CLICK_BOWLING = saved;
	runner.Finish();
}

void SetupBowlingTerrainAndSave(UnitTestRunner& runner, LawnApp& app)
{
	const bool saved = ENABLE_WALLNUT_DOUBLE_CLICK_BOWLING;
	ENABLE_WALLNUT_DOUBLE_CLICK_BOWLING = true;
	for (int level : {21, 41})
	{
		app.mGameMode = GAMEMODE_ADVENTURE;
		app.mPlayerInfo->mLevel = level;
		app.MakeNewBoard();
		Board* board = app.mBoard;
		board->InitLevel();
		app.mGameScene = SCENE_PLAYING;
		const bool roof = board->StageHasRoof();
		const int bottom = board->StageHas6Rows() ? 5 : 4;
		runner.Check(roof == (level == 41) && bottom == (level == 21 ? 5 : 4), "Real pool/roof terrain fixture");
		Plant* support = board->AddPlant(2, 2, roof ? SEED_FLOWERPOT : SEED_LILYPAD);
		Plant* nut = board->AddPlant(2, 2, SEED_WALLNUT);
		nut->MouseDown(0, 0, 2);
		runner.Check(board->GetTopPlantAt(2, 2, TOPPLANT_ANY) == support && !support->mDead &&
			PlantDrawHeightOffset(board, nut, SEED_WALLNUT, 2, 2) == 0, "Launch leaves support behind and detaches its draw offset");
		const int x = nut->mX;
		for (int tick = 0; tick < 30; ++tick) nut->Update();
		runner.Check(nut->mX > x && nut->mY == static_cast<int>(board->GetPosYBasedOnRow(nut->mX, 2)),
			"Straight bowling follows pool surface or roof slope");
		const PlantID id = static_cast<PlantID>(board->mPlants.DataArrayGetID(nut));
		for (PlantState state : {STATE_BOWLING_STRAIGHT, STATE_BOWLING_DOWN})
		{
			nut->mState = state;
			const std::string path = app.mCustomSaveDir + "/bowling-test.dat";
			board->mLastClickedPlantID = id;
			runner.Check(LawnSaveGame(board, path), "Save bowling state and animation");
			app.MakeNewBoard();
			board = app.mBoard;
			const bool loaded = LawnLoadGame(board, path);
			runner.Check(loaded, "Load bowling state and animation");
			nut = loaded ? board->mPlants.DataArrayTryToGet(static_cast<unsigned int>(id)) : nullptr;
			runner.Check(nut && nut->IsBowling() && nut->mState == state && board->mLastClickedPlantID == PlantID::PLANTID_NULL,
				"Straight and bounced nuts persist, click history does not");
			if (!nut) break;
			const int before = nut->mX;
			nut->Update();
			runner.Check(nut->mX > before, "Loaded nut continues rolling");
		}
		if (nut)
		{
			for (int row : {0, bottom})
			{
				nut->mRow = row;
				nut->mState = row == 0 ? STATE_BOWLING_UP : STATE_BOWLING_DOWN;
				nut->mY = static_cast<int>(board->GetPosYBasedOnRow(nut->mX, row));
				nut->UpdateBowling();
				runner.Check(nut->mRow == (row == 0 ? 1 : bottom - 1), "Bowling bounces within actual terrain row count");
			}
		}
	}
	ENABLE_WALLNUT_DOUBLE_CLICK_BOWLING = saved;
	runner.Finish();
}

template<bool enabled>
void SetupSquash(UnitTestRunner& runner, LawnApp& app)
{
	const bool saved = ENABLE_SQUASH_ENHANCEMENT;
	SetupThreepeaterBoard(app);
	Board& board = *app.mBoard;
	Plant* squash = board.AddPlant(4, 2, SEED_SQUASH);
	const Rect attack = squash->GetPlantAttackRect(WEAPON_PRIMARY);
	runner.Check(attack.mWidth == 45, "Squash original attack width is 45 (enhanced width 50)");
	for (bool left : {true, false})
	for (int gap : {-1, 2, 3, 70, 71})
	{
		Zombie* zombie = AddThreeTarget(board, 2);
		const Rect rect = zombie->GetZombieRect();
		const int edge = left ? attack.mX - gap - rect.mWidth : attack.mX + attack.mWidth + gap;
		zombie->mPosX = zombie->mX += edge - rect.mX;
		zombie->mBodyHealth = zombie->mBodyMaxHealth = 10000;
		ENABLE_SQUASH_ENHANCEMENT = false;
		Zombie* originalTarget = squash->FindSquashTarget();
		ENABLE_SQUASH_ENHANCEMENT = true;
		runner.Check(squash->FindSquashTarget() == originalTarget,
			std::format("Squash targeting unchanged: left={} gap={}", left, gap));
		if (!left) runner.Check((originalTarget == zombie) == (gap <= 70), "Original 70-pixel targeting limit");
		ENABLE_SQUASH_ENHANCEMENT = enabled;
		squash->DoSquashDamage();
		const bool hit = gap < 0 || (enabled && gap == 2);
		runner.Check(zombie->mBodyHealth == 10000 - (hit ? 1800 : 0),
			std::format("Squash damage enabled={} left={} gap={}", enabled, left, gap));
		runner.Check(zombie->mStunCounter == 0, "Damage alone does not trigger landing stun");
		zombie->DieNoLoot();
	}

	Zombie* upper = AddThreeTarget(board, 0);
	Zombie* lower = AddThreeTarget(board, 4);
	Zombie* boss = AddThreeTarget(board, 1, ZOMBIE_BOSS);
	Zombie* longer = AddThreeTarget(board, 3);
	longer->ApplyStun(80);
	Zombie* ally = AddThreeTarget(board, 0);
	ally->mMindControlled = true;
	Zombie* dying = AddThreeTarget(board, 4);
	dying->mZombiePhase = PHASE_ZOMBIE_DYING;
	Zombie* dead = AddThreeTarget(board, 3);
	dead->DieNoLoot();
	auto stars = [&]()
	{
		int count = 0;
		for (PvzpParticleSystem* particle : app.mEffectSystem->mParticleHolder->mParticleSystems)
			if (!particle->mDead && particle->mEffectType == PARTICLE_STAR_SPLAT)
			{
				++count;
				runner.Check(!particle->mIsAttachment, "Stun stars animate independently of frozen zombies");
			}
		return count;
	};
	const int beforeStars = stars();
	squash->mTargetX = squash->mX;
	squash->mState = STATE_SQUASH_FALLING;
	for (int countdown : {5, 1, 0})
	{
		squash->mStateCountdown = countdown;
		squash->UpdateSquash();
		for (Zombie* zombie : {upper, lower, boss})
			runner.Check(zombie->mStunCounter == (enabled && countdown == 0 ? 50 : 0),
				std::format("Global landing stun enabled={} row={} countdown={}", enabled, zombie->mRow, countdown));
	}
	runner.Check(squash->mState == STATE_SQUASH_DONE_FALLING && squash->mStateCountdown == 100,
		"Real landing completes the falling transition");
	runner.Check(upper->mBodyHealth == 1000 && lower->mBodyHealth == 1000, "Distant rows stunned without squash damage");
	runner.Check(ally->mStunCounter == 0 && dying->mStunCounter == 0 && dead->mStunCounter == 0,
		"Landing excludes allies, dying and dead zombies");
	for (Zombie* excluded : {ally, dying, dead}) excluded->ApplyStun(50);
	runner.Check(ally->mStunCounter == 0 && dying->mStunCounter == 0 && dead->mStunCounter == 0,
		"ApplyStun directly excludes allies, dying and dead zombies");
	runner.Check(longer->mStunCounter == 80, "Landing never shortens an existing longer stun");
	runner.Check(stars() == beforeStars + (enabled ? 3 : 0), "Only newly stunned enemies create star bursts");
	if constexpr (enabled)
	{
		for (Zombie* zombie : {upper, lower, boss})
		{
			zombie->SetAnimRate(12);
			zombie->mChilledCounter = 100;
			zombie->mPhaseCounter = 200;
			zombie->mJustGotShotCounter = 90;
			const int age = zombie->mZombieAge;
			const float x = zombie->mPosX, y = zombie->mPosY;
			Reanimation* body = app.ReanimationTryToGet(zombie->mBodyReanimID);
			const float animTime = body->mAnimTime;
			for (int tick = 1; tick <= 50; ++tick)
			{
				zombie->Update();
				runner.Check(zombie->mStunCounter == 50 - tick && zombie->mZombieAge == age &&
					zombie->mPosX == x && zombie->mPosY == y && body->mAnimTime == animTime &&
					zombie->mChilledCounter == 100 && zombie->mPhaseCounter == 200 && zombie->mJustGotShotCounter == 90,
					std::format("Stun freezes movement, animation and timers: type={} update={}", static_cast<int>(zombie->mZombieType), tick));
			}
			zombie->Update();
			runner.Check(zombie->mStunCounter == 0 && zombie->mZombieAge == age + 1 && zombie->mJustGotShotCounter == 89,
				"Normal and boss updates resume on update 51, not 50");
		}
		upper->ApplyStun(20);
		upper->ApplyStun(50);
		upper->ApplyStun(10);
		runner.Check(upper->mStunCounter == 50, "Repeated stun takes maximum duration, not sum or latest duration");
		upper->mMindControlled = true;
		const int age = upper->mZombieAge;
		upper->Update();
		runner.Check(upper->mStunCounter == 0 && upper->mZombieAge == age + 1, "Newly mind-controlled zombie discards stun and resumes");
	}
	ENABLE_SQUASH_ENHANCEMENT = saved;
	runner.Finish();
}

template<bool fire>
void SetupThreepeater(UnitTestRunner&, LawnApp& app)
{
	Plant* plant = SetupThreepeaterBoard(app);
	Board& board = *app.mBoard;
	AddThreeTarget(board, 2);
	if constexpr (fire)
	{
		AddThreeTarget(board, 4);
		board.AddPlant(2, 2, SEED_TORCHWOOD);
		plant->Fire(nullptr, 2);
	}
	else
	{
		AddThreeTarget(board, 3);
		plant->LaunchThreepeater(); // Empty upper lane, occupied center and lower lanes.
	}
}

void ThreepeaterShot(UnitTestRunner&, const Projectile& pea)
{
	++threePeas[pea.mRow - 1];
}

template<bool fire>
void UpdateThreepeater(UnitTestRunner& runner, Board& board)
{
	int homing = 0, straight = 0;
	for (Projectile* pea : board.mProjectiles)
	{
		homing += pea->mMotionType == MOTION_HOMING;
		straight += pea->mMotionType == MOTION_THREEPEATER;
		threeHomed |= pea->mMotionType == MOTION_HOMING;
		threeFire |= pea->mProjectileType == PROJECTILE_FIREBALL;
		threeRenderOrder &= pea->mRenderOrder == Board::MakeRenderOrder(
			pea->mMotionType == MOTION_HOMING ? RENDER_LAYER_TOP : RENDER_LAYER_PROJECTILE,
			pea->mMotionType == MOTION_HOMING ? 0 : pea->mRow, 0);
		if (runner.Tick() < 60 && (fire || pea->mMotionType != MOTION_HOMING))
			runner.Check(pea->mMotionType == MOTION_THREEPEATER && pea->mDamageRangeFlags == 1,
				"Occupied lanes retain original flight and ground flags before removal");
	}
	if constexpr (!fire)
		if (runner.Tick() == 60) // After animated emission, before the lower lane empties.
			runner.Check(homing == 1 && straight == 2,
				"Mixed volley has one homing pea and two occupied-lane ordinary peas");
	if (runner.Tick() == 60)
	{
		if constexpr (fire)
		{
			runner.Check(threeFire, "Real Torchwood converted the straight center pea before lane emptied");
			board.ZombieTryToGet(threeTargets[2])->DieNoLoot();
		}
		else board.ZombieTryToGet(threeTargets[3])->DieNoLoot();
	}
	if (runner.Tick() != 600) return;
	if constexpr (!fire)
	{
		for (unsigned int count : threePeas)
			runner.Check(count == 1, "Animated volley emitted exactly one pea in this firing lane");
		runner.Check(board.ZombieTryToGet(threeTargets[2])->mBodyHealth == 940,
			"Center target received straight, initially empty and emptied-in-flight lane hits");
	}
	else
		runner.Check(board.ZombieTryToGet(threeTargets[4])->mBodyHealth == 960, "Homing fire pea dealt 40 damage in target lane");
	runner.Check(threeHomed, "Empty firing lane transitioned to homing");
	runner.Check(threeRenderOrder, "Homing peas render above all vase rows immediately; ordinary peas keep their row layer");
	runner.Finish();
}

void SetupThreepeaterScope(UnitTestRunner& runner, LawnApp& app)
{
	Plant* three = SetupThreepeaterBoard(app);
	Board* board = app.mBoard;
	three->Fire(nullptr, 2);
	for (Projectile* pea : board->mProjectiles)
	{
		const float startX = pea->mPosX;
		for (int tick = 0; tick < 20; ++tick) pea->Update();
		runner.Check(pea->mMotionType == MOTION_THREEPEATER && pea->mPosX > startX && !pea->mDead &&
			pea->mTargetZombieID == ZombieID::ZOMBIEID_NULL, "No target: keep flying without acquiring an invalid lock");
	}
	AddThreeTarget(*board, 1);
	for (Projectile* pea : board->mProjectiles) pea->Update();
	three->Fire(nullptr, 1);
	const std::string path = app.mCustomSaveDir + "/threepeater-test.dat";
	runner.Check(LawnSaveGame(board, path), "Save straight/fan-out and homing threepeater states using existing fields");
	app.MakeNewBoard();
	board = app.mBoard;
	const bool loaded = LawnLoadGame(board, path);
	runner.Check(loaded, "Reload threepeater projectiles without new save fields");
	if (loaded)
	{
		int homing = 0, pending = 0;
		for (Projectile* pea : board->mProjectiles)
		{
			if (pea->mMotionType == MOTION_HOMING)
			{
				++homing;
				runner.Check(pea->mTargetZombieID == threeTargets[1] && pea->mDamageRangeFlags == 11,
					"Save retained homing target and balloon flags");
			}
			if (pea->mMotionType == MOTION_THREEPEATER)
			{
				++pending;
				runner.Check(pea->mRow == 1 && pea->mVelY == -3, "Save retained assigned lane and fan-out velocity");
			}
		}
		runner.Check(homing == 1 && pending == 1, "Both threepeater flight states survived save/load");
		board->ZombieTryToGet(threeTargets[1])->DieNoLoot();
		AddThreeTarget(*board, 4);
		for (Projectile* pea : board->mProjectiles)
		{
			const bool wasPending = pea->mMotionType == MOTION_THREEPEATER;
			pea->Update();
			runner.Check(pea->mMotionType == MOTION_HOMING && pea->mTargetZombieID == threeTargets[wasPending ? 4 : 1] &&
				std::isfinite(pea->mPosX) && std::isfinite(pea->mPosY),
				"Loaded pending shot acquires; loaded lost lock retains Cattail behavior");
		}
	}
	board->mProjectiles.DataArrayFreeAll();
	for (SeedType seed : {SEED_PEASHOOTER, SEED_REPEATER, SEED_GATLINGPEA, SEED_SNOWPEA, SEED_SPLITPEA, SEED_LEFTPEATER})
	{
		Plant* plant = board->AddPlant(1, 0, seed);
		plant->Fire(nullptr, 0);
		if (seed == SEED_SPLITPEA) plant->Fire(nullptr, 0, WEAPON_SECONDARY);
		for (Projectile* pea : board->mProjectiles)
		{
			const auto motion = pea->mMotionType;
			for (int tick = 0; tick < 30; ++tick) pea->Update();
			runner.Check(pea->mMotionType == motion && motion != MOTION_HOMING && motion != MOTION_THREEPEATER,
				std::format("Other pea source {} keeps original motion despite empty firing lane", static_cast<int>(seed)));
		}
		board->mProjectiles.DataArrayFreeAll();
	}
	Plant* cattail = board->AddPlant(1, 3, SEED_CATTAIL);
	runner.Check(cattail->FindTargetZombie(3) == Plant::FindCattailTarget(board, cattail->mX + 40, cattail->mY + 40),
		"Cattail plant delegates to the shared targeting function");
	board->mZombies.DataArrayFreeAll();
	Zombie* nearZombie = AddThreeTarget(*board, 2);
	Zombie* farZombie = AddThreeTarget(*board, 4);
	runner.Check(Plant::FindCattailTarget(board, 500, nearZombie->mY + 40) == nearZombie, "Shared selector chooses nearest eligible ground enemy");
	nearZombie->mMindControlled = true;
	runner.Check(Plant::FindCattailTarget(board, 500, nearZombie->mY + 40) == farZombie, "Shared selector excludes mind-controlled enemies");
	nearZombie->mMindControlled = false;
	Zombie* tie = AddThreeTarget(*board, 2);
	runner.Check(Plant::FindCattailTarget(board, 500, nearZombie->mY + 40) == nearZombie, "Equal integer-distance weights retain first candidate, like Cattail");
	tie->DieNoLoot();
	nearZombie->DieNoLoot();
	farZombie->DieNoLoot();
	Zombie* balloon = AddThreeTarget(*board, 2, ZOMBIE_BALLOON);
	Zombie* ground = AddThreeTarget(*board, 1);
	ground->mPosX = ground->mX = 300;
	Plant* threePlant = board->AddPlant(1, 2, SEED_THREEPEATER);
	runner.Check(balloon->IsFlying() && Plant::FindCattailTarget(board, threePlant->mX + 40, threePlant->mY + 40) == balloon,
		"Shared targeting prioritizes airborne balloon over nearer ground enemy");
	threePlant->Fire(nullptr, 2);
	Plant* torch = board->AddPlant(1, 2, SEED_TORCHWOOD);
	for (Projectile* pea : board->mProjectiles)
	{
		pea->Update();
		runner.Check(pea->mMotionType == MOTION_HOMING && pea->mTargetZombieID == board->ZombieGetID(balloon),
			"Flying-only assigned row is empty for ordinary peas and activates homing");
		const Rect rect = torch->GetPlantAttackRect(WEAPON_PRIMARY);
		// Move the shadow with the pea so the fixture does not create a ground impact.
		pea->mShadowY += rect.mY + 20 - pea->mPosY;
		pea->mPosX = pea->mX = rect.mX;
		pea->mPosY = pea->mY = rect.mY + 20;
		pea->mRow = 2;
		torch->UpdateTorchwood();
		runner.Check(pea->mProjectileType == PROJECTILE_FIREBALL && pea->mMotionType == MOTION_HOMING &&
			pea->mTargetZombieID == board->ZombieGetID(balloon) && pea->mDamageRangeFlags == 11,
			"Real Torchwood overlap after homing preserves lock, flight and flying eligibility");
		for (int tick = 0; tick < 600 && !pea->mDead; ++tick) pea->Update();
		// IsFlying stays true until the pop animation completes; only projectiles update here.
		runner.Check(pea->mDead && balloon->mZombiePhase == PHASE_BALLOON_POPPING &&
			balloon->mFlyingHealth == 0 && balloon->mBodyHealth == 980 && ground->mBodyHealth == 1000,
			"Homing fire hit spends 20 damage popping balloon and 20 on body, bypassing nearer ground enemy");
	}
	// Disabling the toggle leaves empty-lane peas flying their original straight path.
	board->mProjectiles.DataArrayFreeAll();
	board->mZombies.DataArrayFreeAll();
	ENABLE_THREEPEATER_HOMING = false;
	Plant* disabled = board->AddPlant(3, 2, SEED_THREEPEATER);
	if (disabled)
		disabled->Fire(nullptr, 2);
	for (Projectile* pea : board->mProjectiles)
	{
		const int lane = pea->mRow;
		for (int tick = 0; tick < 120; ++tick) pea->Update();
		runner.Check(pea->mMotionType == MOTION_THREEPEATER && !pea->mDead && pea->mRow == lane &&
			pea->mRenderOrder == Board::MakeRenderOrder(RENDER_LAYER_PROJECTILE, lane, 0),
			"Disabled homing keeps empty-lane peas straight and row-layered");
	}
	ENABLE_THREEPEATER_HOMING = true;
	runner.Finish();
}

template<bool enabled>
void SetupEmpowered(UnitTestRunner& runner, LawnApp& app)
{
	savedEmpowered = ENABLE_PEASHOOTER_EMPOWERED_PEA;
	ENABLE_PEASHOOTER_EMPOWERED_PEA = enabled;
	peaShots.fill(0);
	app.mGameMode = GameMode::GAMEMODE_ADVENTURE;
	app.mPlayerInfo->mLevel = 11;
	app.MakeNewBoard();
	Board& board = *app.mBoard;
	board.InitLevel();
	app.mGameScene = GameScenes::SCENE_PLAYING;
	board.mMouseVisible = false;
	for (int row = 0; row < 5; ++row)
	{
		const ZombieType type = row == 1 ? ZOMBIE_GARGANTUAR : row == 2 ? ZOMBIE_REDEYE_GARGANTUAR : ZOMBIE_NORMAL;
		Zombie* zombie = board.AddZombieInRow(type, row, 0);
		peaTargets[row] = board.ZombieGetID(zombie);
		zombie->mPosX = 600;
		zombie->mX = 600;
		zombie->mVelX = 0;
		zombie->SetAnimRate(0);
		zombie->mBodyHealth = zombie->mBodyMaxHealth = 10000;
		zombie->mDroppedLoot = true;
		Plant* plant = board.AddPlant(1, row, SEED_PEASHOOTER);
		peaPlants[row] = static_cast<PlantID>(board.mPlants.DataArrayGetID(plant));
		// Stagger plants to catch accidentally shared shot counters.
		plant->mLaunchCounter = 30 + row * 7;
	}
	board.AddPlant(3, 3, SEED_TORCHWOOD);
}

template<bool enabled>
void UpdateEmpowered(UnitTestRunner& runner, Board& board)
{
	for (Projectile* pea : board.mProjectiles)
	{
		if (pea->mProjectileAge != 1) continue;
		const int row = pea->mRow;
		++peaShots[row];
		const bool empowered = enabled && peaShots[row] % 3 == 0;
		runner.Check(pea->mEmpoweredPea == empowered && pea->GetVisualScale() == (empowered ? 1.5f : 1.0f),
			std::format("Row {} emitted shot {}: empowerment and visual scale", row, peaShots[row]));
		const Rect rect = pea->GetProjectileRect();
		runner.Check(pea->mWidth == 40 && pea->mHeight == 40 && rect.mWidth == 55 && rect.mHeight == 40,
			"Empowerment does not enlarge pea collision");
		if (peaShots[row] == 6)
			board.mPlants.DataArrayTryToGet(static_cast<unsigned int>(peaPlants[row]))->mLaunchCounter = 10000;
	}
	if (runner.Tick() != 1050) return;
	for (int row = 0; row < 5; ++row)
	{
		Zombie* zombie = board.ZombieTryToGet(peaTargets[row]);
		const int damage = (row == 3 ? 40 : 20) * (enabled ? 7 : 6);
		const float distance = enabled && row != 1 && row != 2 ? 23.050434f : 0;
		runner.Check(peaShots[row] == 6 && zombie && zombie->mBodyHealth == 10000 - damage,
			std::format("Row {}: six real hits, damage {}", row, damage));
		runner.Check(zombie && std::abs(zombie->mPosX - 600 - distance) < 0.001f && zombie->mX == static_cast<int>(zombie->mPosX),
			std::format("Row {}: knockback {} and synchronized collision position", row, distance));
	}
	ENABLE_PEASHOOTER_EMPOWERED_PEA = savedEmpowered;
	runner.Finish();
}

void SetupFirePeaAlignment(UnitTestRunner& runner, LawnApp& app)
{
	SetupEmpowered<true>(runner, app);
	for (bool empowered : {false, true})
	for (bool backwards : {false, true})
	{
		Projectile* pea = app.mBoard->AddProjectile(300, 200, 0, 0, PROJECTILE_PEA);
		pea->mEmpoweredPea = empowered;
		pea->mMotionType = backwards ? MOTION_BACKWARDS : MOTION_STRAIGHT;
		pea->ConvertToFireball(3);
		Reanimation* fire = FindReanimAttachment(pea->mAttachmentID);
		runner.Check(fire != nullptr, "Torchwood conversion attaches fire animation");
		if (!fire) continue;
		if (!empowered && !backwards && fire->mDefinition->mReanimAtlas)
			for (int i = 0; i < fire->mDefinition->mTracks.count; ++i)
			{
				const auto& track = fire->mDefinition->mTracks.tracks[i];
				const auto& transform = track.mTransforms.mTransforms[0];
				const auto* image = fire->mDefinition->mReanimAtlas->GetEncodedReanimAtlas(transform.mImage);
				if (image) runner.Log(std::format("FIRE PEA track {}: xy=({}, {}) scale=({}, {}) image={}x{}",
					track.mName, transform.mTransX, transform.mTransY, transform.mScaleX, transform.mScaleY, image->mWidth, image->mHeight));
			}
		const float scale = empowered ? 1.5f : 1.0f;
		runner.Check(fire->mOverlayMatrix.m00 == (backwards ? -scale : scale) && fire->mOverlayMatrix.m11 == scale,
			"Fire animation retains scale and direction");
		for (bool moved : {false, true})
		{
			const float x = pea->mPosX + (moved ? 17.0f : 0.0f);
			const float y = pea->mPosY + (moved ? -9.0f : 0.0f);
			if (moved) AttachmentUpdateAndMove(pea->mAttachmentID, x, y);
			runner.Check(fire->mOverlayMatrix.m02 + 40.0f * fire->mOverlayMatrix.m00 == x + 15.0f &&
				fire->mOverlayMatrix.m12 + 40.0f * fire->mOverlayMatrix.m11 == y + 15.0f,
				std::format("Fire center fixed: empowered={} backwards={} moved={}", empowered, backwards, moved));
		}
		const Rect converted = pea->GetProjectileRect();
		runner.Check(converted == Rect(300, 200, 30, 40) && pea->mPosX == 300 && pea->mPosY == 200 && pea->mWidth == 40 && pea->mHeight == 40,
			"Fire visual alignment leaves projectile position and collision unchanged");
	}
	ENABLE_PEASHOOTER_EMPOWERED_PEA = savedEmpowered;
	runner.Finish();
}

void SetupPeaPersistence(UnitTestRunner& runner, LawnApp& app)
{
	SetupEmpowered<true>(runner, app);
	Board* board = app.mBoard;
	Plant* plant = board->mPlants.DataArrayTryToGet(static_cast<unsigned int>(peaPlants[0]));
	for (int i = 0; i < 5; ++i) plant->Fire(nullptr, 0);
	const std::string path = app.mCustomSaveDir + "/empowered-test.dat";
	runner.Check(LawnSaveGame(board, path), "Save per-plant cadence and in-flight empowered projectile in sandbox");
	app.MakeNewBoard();
	board = app.mBoard;
	const bool loaded = LawnLoadGame(board, path);
	runner.Check(loaded, "Load empowered portable save");
	if (loaded)
	{
		plant = board->mPlants.DataArrayTryToGet(static_cast<unsigned int>(peaPlants[0]));
		runner.Check(plant && plant->mPeashooterShotCount == 2, "Saved cadence restored at two of three");
		int empowered = 0;
		for (Projectile* pea : board->mProjectiles) empowered += pea->mEmpoweredPea;
		runner.Check(empowered == 1, "Exactly the third in-flight pea remains empowered after load");
		if (plant) plant->Fire(nullptr, 0);
		empowered = 0;
		for (Projectile* pea : board->mProjectiles) empowered += pea->mEmpoweredPea;
		runner.Check(empowered == 2, "Sixth emitted pea is empowered after resume");
		ENABLE_PEASHOOTER_EMPOWERED_PEA = false;
		Zombie* zombie = board->ZombieTryToGet(peaTargets[0]);
		for (Projectile* pea : board->mProjectiles) pea->DoImpact(zombie);
		runner.Check(zombie && zombie->mBodyHealth == 9860, "Disabling toggle does not change already emitted projectile damage");
		board->mProjectiles.DataArrayFreeAll();
		if (plant) for (int i = 0; i < 3; ++i) plant->Fire(nullptr, 0);
		bool ordinary = true;
		for (Projectile* pea : board->mProjectiles) ordinary &= !pea->mEmpoweredPea;
		runner.Check(ordinary, "Runtime disable affects newly emitted peas, including reused projectile slots");
		ENABLE_PEASHOOTER_EMPOWERED_PEA = true;
		// Other pea sources must never inherit the normal peashooter's feature.
		for (SeedType seed : {SEED_REPEATER, SEED_LEFTPEATER, SEED_THREEPEATER, SEED_GATLINGPEA, SEED_SNOWPEA})
		{
			board->mProjectiles.DataArrayFreeAll();
			Plant* other = board->AddPlant(0, 0, seed);
			for (int i = 0; i < 6; ++i) other->Fire(nullptr, 0);
			bool ordinary = true;
			for (Projectile* pea : board->mProjectiles) ordinary &= !pea->mEmpoweredPea;
			runner.Check(ordinary && other->mPeashooterShotCount == 0, std::format("Seed {} excluded", static_cast<int>(seed)));
		}
		// Hide the optional fields as unknown TLVs: exercise absent-field defaults
		// and forward-compatible skipping without changing the old positional tails.
		Buffer buffer;
		if (app.ReadBufferFromFile(path, &buffer, false))
		{
			auto read32 = [&](size_t pos)
			{
				buffer.mReadBitPos = static_cast<int>(pos * 8);
				return buffer.ReadUInt32();
			};
			int hidden = 0;
			for (size_t chunk = 24; chunk < buffer.mData.size(); chunk += 8 + read32(chunk + 4))
			{
				if (read32(chunk) != 3 && read32(chunk) != 4) continue;
				const size_t array = chunk + 20;
				const uint32_t count = read32(array + 4);
				size_t item = array + 20;
				for (uint32_t i = 0; i < count; ++i)
				{
					const size_t end = item + 8 + read32(item + 4);
					for (size_t field = item + 8; field < end; field += 8 + read32(field + 4))
						if (read32(field) == 101) { buffer.mData.at(field) = 102; ++hidden; }
					item = end;
				}
			}
			const uint32_t crc = crc32(0, buffer.mData.data() + 24, static_cast<uInt>(buffer.mData.size() - 24));
			for (int i = 0; i < 4; ++i) buffer.mData.at(20 + i) = static_cast<unsigned char>(crc >> (8 * i));
			runner.Check(hidden == 11 && app.WriteBufferToFile(path, &buffer), "Prepare save with absent empowerment fields and unknown TLVs");
			app.MakeNewBoard();
			board = app.mBoard;
			runner.Check(LawnLoadGame(board, path), "Load save without optional empowerment fields");
			bool defaults = true;
			for (Plant* savedPlant : board->mPlants) defaults &= savedPlant->mPeashooterShotCount == 0;
			for (Projectile* pea : board->mProjectiles) defaults &= !pea->mEmpoweredPea;
			runner.Check(defaults && board->mProjectiles.mSize == 5, "Missing fields default to zero/false without losing original entities");
		}
		else runner.Check(false, "Read sandbox save for compatibility test");
	}
	ENABLE_PEASHOOTER_EMPOWERED_PEA = savedEmpowered;
	runner.Finish();
}

void SetupWalkingEvidence(UnitTestRunner& runner, LawnApp& app)
{
	SetupEmpowered<true>(runner, app);
	Zombie* zombie = app.mBoard->ZombieTryToGet(peaTargets[0]);
	zombie->mVelX = 0.30f;
	for (const char* animation : {"anim_walk", "anim_walk2"})
	{
		zombie->PlayZombieReanim(animation, REANIM_LOOP, 0, 0);
		zombie->UpdateAnimSpeed();
		Reanimation* reanim = app.ReanimationTryToGet(zombie->mBodyReanimID);
		float distance = 0;
		// Uniformly sample a full gait; actual movement uses each frame's ground delta.
		for (int i = 0; i < 10000; ++i)
		{
			reanim->mAnimTime = (i + 0.5f) / 10000;
			const float x = zombie->mPosX;
			zombie->UpdateZombieWalking();
			distance += x - zombie->mPosX;
			zombie->mPosX = x;
		}
		const float pixelsPerSecond = distance / 100;
		runner.Log(std::format("WALK {}: frames={} rate={} mean={} px/s", animation, reanim->mFrameCount, reanim->mAnimRate, pixelsPerSecond));
		runner.Check(std::abs(pixelsPerSecond - 14.406522f) < 0.02f, "Loaded normal walk supports 14.4065 px/s baseline");
	}
	ENABLE_PEASHOOTER_EMPOWERED_PEA = savedEmpowered;
	runner.Finish();
}

void SetupFixture(UnitTestRunner& runner, LawnApp& app)
{
	shots = 0;
	lastHealth = 80;
	sawDeath = false;
	app.mGameMode = GameMode::GAMEMODE_ADVENTURE;
	app.mPlayerInfo->mLevel = 11;
	app.MakeNewBoard();
	Board& board = *app.mBoard;
	board.InitLevel();
	app.mGameScene = GameScenes::SCENE_PLAYING;
	board.mMouseVisible = false;
	runner.Check(board.mBackground == BackgroundType::BACKGROUND_2_NIGHT, "Night lawn");
	Zombie* zombie = board.AddZombieInRow(ZombieType::ZOMBIE_NORMAL, 2, 0);
	target = static_cast<ZombieID>(board.mZombies.DataArrayGetID(zombie));
	targetX = static_cast<float>(board.GridToPixelX(7, 2));
	zombie->mPosX = targetX;
	zombie->mX = static_cast<int>(targetX);
	zombie->mVelX = 0;
	zombie->mOriginalAnimRate = 0;
	app.ReanimationTryToGet(zombie->mBodyReanimID)->mAnimRate = 0;
	zombie->mBodyHealth = zombie->mBodyMaxHealth = 80;
	zombie->mDroppedLoot = true;
	Plant* plant = board.AddPlant(8, 2, SeedType::SEED_LEFTPEATER);
	shooter = static_cast<PlantID>(board.mPlants.DataArrayGetID(plant));
}

std::array<PlantID, 11> healingPlants;
bool savedHealing;

template<bool enabled>
void SetupHealing(UnitTestRunner& runner, LawnApp& app)
{
	savedHealing = ENABLE_PLANTERN_HEALING;
	ENABLE_PLANTERN_HEALING = enabled;
	app.mGameMode = GameMode::GAMEMODE_ADVENTURE;
	app.mPlayerInfo->mLevel = 11;
	app.MakeNewBoard();
	Board& board = *app.mBoard;
	board.InitLevel();
	app.mGameScene = GameScenes::SCENE_PLAYING;
	board.mMouseVisible = false;
	int index = 0;
	auto add = [&](int col, int row, SeedType seed, int missingHealth)
	{
		Plant* plant = board.AddPlant(col, row, seed);
		plant->mPlantHealth = plant->mPlantMaxHealth - missingHealth;
		healingPlants[index++] = static_cast<PlantID>(board.mPlants.DataArrayGetID(plant));
		return plant;
	};
	for (int row = 1; row <= 3; ++row)
		for (int col = 3; col <= 5; ++col)
			if (col != 4 || row != 2)
				add(col, row, SeedType::SEED_WALLNUT, index == 0 ? 10 : 100);
	Plant* plantern = add(4, 2, SeedType::SEED_PLANTERN, 100);
	add(4, 2, SeedType::SEED_PUMPKINSHELL, 100);
	add(6, 2, SeedType::SEED_WALLNUT, 100);
	runner.Check(plantern->mLaunchCounter == 100, "Plantern starts with a 100-tick healing timer");
}

template<bool enabled>
void UpdateHealing(UnitTestRunner& runner, Board& board)
{
	const int tick = runner.Tick();
	for (int i = 0; i < static_cast<int>(healingPlants.size()); ++i)
	{
		Plant* plant = board.mPlants.DataArrayTryToGet(static_cast<unsigned int>(healingPlants[i]));
		const int healed = enabled && i < 8 ? 45 * (tick / 100) : 0;
		const int missingHealth = std::max(0, (i == 0 ? 10 : 100) - healed);
		runner.Check(plant && !plant->mDead && plant->mPlantHealth == plant->mPlantMaxHealth - missingHealth,
			std::format("Healing {}: plant {} health at tick {}", enabled, i, tick));
		if (i == 8)
			runner.Check(plant && plant->mLaunchCounter == (enabled ? 100 - tick % 100 : 100),
				"Plantern timer counts down and resets only when enabled");
	}
	if (tick == 200)
	{
		ENABLE_PLANTERN_HEALING = savedHealing;
		runner.Finish();
	}
}

template<bool enabled>
void SetupGreenVases(UnitTestRunner& runner, LawnApp& app)
{
	const bool saved = ENABLE_PLANTERN_GREEN_VASE;
	ENABLE_PLANTERN_GREEN_VASE = enabled;
	app.mGameMode = GameMode::GAMEMODE_SCARY_POTTER_ENDLESS;
	app.MakeNewBoard();
	Board& board = *app.mBoard;
	board.mRogueRun.active = false;
	board.InitLevel();
	app.mGameScene = GameScenes::SCENE_PLAYING;
	board.mMouseVisible = false;
	for (int stage : {0, 1, 8, 9})
	{
		// InitLevel populates stage zero; isolate each real population call.
		board.mGridItems.DataArrayFreeAll();
		board.mChallenge->mSurvivalStage = stage;
		board.mChallenge->ScaryPotterPopulate();
		std::array<int, 8> seeds{};
		constexpr std::array seedTypes{SeedType::SEED_LEFTPEATER, SeedType::SEED_SNOWPEA,
			SeedType::SEED_PEASHOOTER, SeedType::SEED_THREEPEATER, SeedType::SEED_SQUASH,
			SeedType::SEED_POTATOMINE, SeedType::SEED_WALLNUT, SeedType::SEED_PLANTERN};
		int total = 0, green = 0, greenPlantern = 0, sun = 0;
		int normal = 0, garg = 0, pail = 0, jack = 0;
		bool valid = true;
		bool occupied[9][5]{};
		for (GridItem* pot : board.mGridItems)
		{
			++total;
			valid &= !pot->mDead && pot->mGridItemType == GridItemType::GRIDITEM_SCARY_POT;
			if (pot->mGridX < 2 || pot->mGridX > 8 || pot->mGridY < 0 || pot->mGridY > 4)
				valid = false;
			else
			{
				valid &= !occupied[pot->mGridX][pot->mGridY];
				occupied[pot->mGridX][pot->mGridY] = true;
			}
			const bool leaf = pot->mGridItemState == GridItemState::GRIDITEM_STATE_SCARY_POT_LEAF;
			green += leaf;
			valid &= leaf || pot->mGridItemState == GridItemState::GRIDITEM_STATE_SCARY_POT_QUESTION;
			valid &= !leaf || pot->mScaryPotType == ScaryPotType::SCARYPOT_SEED;
			if (pot->mScaryPotType == ScaryPotType::SCARYPOT_SEED)
			{
				valid &= pot->mZombieType == ZombieType::ZOMBIE_INVALID;
				for (int i = 0; i < static_cast<int>(seedTypes.size()); ++i)
					seeds[i] += pot->mSeedType == seedTypes[i];
				greenPlantern += leaf && pot->mSeedType == SeedType::SEED_PLANTERN;
			}
			else if (pot->mScaryPotType == ScaryPotType::SCARYPOT_ZOMBIE)
			{
				valid &= pot->mSeedType == SeedType::SEED_NONE;
				normal += pot->mZombieType == ZombieType::ZOMBIE_NORMAL;
				garg += pot->mZombieType == ZombieType::ZOMBIE_GARGANTUAR;
				pail += pot->mZombieType == ZombieType::ZOMBIE_PAIL;
				jack += pot->mZombieType == ZombieType::ZOMBIE_JACK_IN_THE_BOX;
			}
			else
			{
				++sun;
				valid &= pot->mScaryPotType == ScaryPotType::SCARYPOT_SUN &&
					pot->mSunCount >= 1 && pot->mSunCount <= 3 &&
					pot->mSeedType == SeedType::SEED_NONE && pot->mZombieType == ZombieType::ZOMBIE_INVALID;
			}
		}
		runner.Check(valid && total == 35 && board.mChallenge->mScaryPotterPots == 35 && green == 2,
			std::format("Green vases {} stage {}: 35 unique valid vases, exactly two green", enabled, stage));
		runner.Check(seeds == std::array{6, 2, 1, 2, 5, 1, 1, 1} && sun == 1 && pail == 5 && jack == 1 &&
			garg == 1 + std::min(stage, 8) && normal == 8 - std::min(stage, 8),
			std::format("Stage {}: original contents and capped endless difficulty", stage));
		if constexpr (enabled)
			runner.Check(greenPlantern == 1, std::format("Stage {}: existing Plantern is green", stage));
	}
	ENABLE_PLANTERN_GREEN_VASE = saved;
	runner.Finish();
}

void SetupVaseCooldowns(UnitTestRunner& runner, LawnApp& app)
{
	app.mGameMode = GAMEMODE_SCARY_POTTER_ENDLESS;
	app.MakeNewBoard();
	Board& board = *app.mBoard;
	board.InitLevel();
	app.mGameScene = SCENE_PLAYING;
	constexpr std::array seeds{SEED_CHERRYBOMB, SEED_PEASHOOTER, SEED_WALLNUT};
	board.mSeedBank->mNumPackets = static_cast<int>(seeds.size());
	for (bool endless : {true, false})
	{
		app.mGameMode = endless ? GAMEMODE_SCARY_POTTER_ENDLESS : GAMEMODE_SCARY_POTTER_1;
		for (int i = 0; i < static_cast<int>(seeds.size()); ++i)
		{
			SeedPacket& packet = board.mSeedBank->mSeedPackets[i];
			packet.SetPacketType(seeds[i]);
			packet.mRefreshing = i != 2;
			packet.mActive = i == 2;
			packet.mRefreshCounter = i == 2 ? 0 : 100;
		}
		const int stage = board.mChallenge->mSurvivalStage;
		board.mChallenge->PuzzleNextStageClear();
		runner.Check(board.mChallenge->mSurvivalStage == stage + 1, "Vase transition advances one stage");
		for (int i = 0; i < static_cast<int>(seeds.size()); ++i)
		{
			const SeedPacket& packet = board.mSeedBank->mSeedPackets[i];
			const bool ready = endless || i == 2;
			runner.Check(packet.mPacketType == seeds[i] && packet.mActive == ready &&
				packet.mRefreshing == !ready && packet.mRefreshCounter == (ready ? 0 : 100),
				std::format("Vase cooldown endless={} slot={}: correct readiness and unchanged plant", endless, i));
		}
	}
	runner.Finish();
}

void SetupBurst(UnitTestRunner& runner, LawnApp& app)
{
	ENABLE_LEFTPEATER_PLANTING_BURST = true;
	SetupFixture(runner, app);
	runner.Check(shots == 1, "Immediate planting shot at tick 0");
}

void SetupDisabledBurst(UnitTestRunner& runner, LawnApp& app)
{
	ENABLE_LEFTPEATER_PLANTING_BURST = false;
	SetupFixture(runner, app);
	Plant* plant = app.mBoard->mPlants.DataArrayTryToGet(static_cast<unsigned int>(shooter));
	runner.Check(shots == 0, "Disabled: no immediate planting shot");
	runner.Check(plant->mStateCountdown == 0 && plant->mShootingCounter == 0,
		"Disabled: no planting burst or shooting animation scheduled");
	runner.Check(plant->mLaunchCounter >= 0 && plant->mLaunchCounter <= 150,
		"Disabled: original random initial launch counter range");
	// Control only the random initial delay; keep normal targeting and shooting intact.
	plant->mLaunchCounter = 150;
}

void DisabledShot(UnitTestRunner& runner, const Projectile& projectile)
{
	++shots;
	runner.Check(shots <= 2 && runner.Tick() == 150 + 25 * (shots - 1),
		std::format("Disabled: normal shot {} at tick {}", shots, runner.Tick()));
	runner.Check(projectile.mRow == 2 && projectile.mProjectileType == ProjectileType::PROJECTILE_PEA,
		"Disabled: normal attack fires a pea in row 3");
}

void UpdateDisabledBurst(UnitTestRunner& runner, Board& board)
{
	Zombie* zombie = board.ZombieTryToGet(target);
	Plant* plant = board.mPlants.DataArrayTryToGet(static_cast<unsigned int>(shooter));
	if (runner.Tick() == 50)
	{
		runner.Check(shots == 0 && zombie && zombie->mBodyHealth == 80,
			"Disabled: no bonus damage during the planting burst window");
		runner.Check(plant && plant->mLaunchCounter == 100 && plant->mStateCountdown == 0,
			"Disabled: ordinary countdown runs without burst freeze or reset");
	}
	if (runner.Tick() == 200)
	{
		runner.Check(shots == 2, "Disabled: normal double shot remains functional");
		runner.Check(zombie && !zombie->mDead && zombie->mBodyHealth == 40 && zombie->mPosX == targetX,
			"Disabled: stationary target survives with 40 health after normal double shot");
		ENABLE_LEFTPEATER_PLANTING_BURST = true;
		runner.Finish();
	}
}

void Shot(UnitTestRunner& runner, const Projectile& projectile)
{
	++shots;
	runner.Check(shots <= 4 && runner.Tick() == shotTicks[shots <= 4 ? shots - 1 : 3],
		std::format("Shot {} created (expected ticks 0,16,32,48)", shots));
	runner.Check(projectile.mRow == 2 && projectile.mProjectileType == ProjectileType::PROJECTILE_PEA,
		"Pea fired in row 3");
}

void UpdateBurst(UnitTestRunner& runner, Board& board)
{
	Zombie* zombie = board.ZombieTryToGet(target);
	for (Projectile* projectile : board.mProjectiles)
		if (projectile->mProjectileAge == 1)
			runner.Check(projectile->mMotionType == ProjectileMotion::MOTION_BACKWARDS, "Pea travels left");
	if (zombie)
	{
		// Death animation can shift the body; only the live target is stationary.
		if (!zombie->IsDeadOrDying() && zombie->mPosX != targetX)
		{
			runner.Check(false, "Zombie must remain stationary in column 8");
			runner.Finish();
			return;
		}
		if (zombie->mBodyHealth != lastHealth)
		{
			runner.Log(std::format("DAMAGE tick={} health {} -> {}", runner.Tick(), lastHealth, zombie->mBodyHealth));
			lastHealth = zombie->mBodyHealth;
		}
		sawDeath |= zombie->mDead || zombie->mBodyHealth <= 0;
	}
	else sawDeath = true;
	if (runner.Tick() == 1 || runner.Tick() == 16 || runner.Tick() == 32)
		runner.Check(zombie && zombie->mBodyHealth == 80 - 20 * shots && !sawDeath,
			std::format("Shot {} dealt 20 damage; target still alive", shots));
	if (runner.Tick() == 48)
	{
		runner.Check(shots == 4 && zombie && zombie->mBodyHealth == 0,
			"Fourth shot killed the target at tick 48");
		Plant* plant = board.mPlants.DataArrayTryToGet(static_cast<unsigned int>(shooter));
		runner.Check(plant && !plant->mDead && plant->mLaunchCounter == 120,
			"Launch counter reset to 120 at tick 48");
		Reanimation* head = plant ? board.mApp->ReanimationTryToGet(plant->mHeadReanimID) : nullptr;
		int frameStart = -1, frameCount = -1;
		if (head && head->TrackExists("anim_shooting"))
			head->GetFramesForLayer("anim_shooting", frameStart, frameCount);
		runner.Check(head && head->mFrameStart == frameStart && head->mFrameCount == frameCount &&
			head->mLoopType == ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD && head->mAnimRate == 45.0f,
			"Fourth shot plays the shooting animation");
	}
	if (runner.Tick() == 49)
	{
		Plant* plant = board.mPlants.DataArrayTryToGet(static_cast<unsigned int>(shooter));
		runner.Check(plant && !plant->mDead && plant->mLaunchCounter == 119,
			"Launch counter decremented to 119 at tick 49");
	}
	// Observe past the burst and projectile travel, but before the next normal volley.
	if (runner.Tick() == 140)
	{
		runner.Check(shots == 4, "Exactly four shots, no extra burst projectiles");
		runner.Check(sawDeath, "80-health zombie died");
		runner.Finish();
	}
}
}

void RegisterLawnTests(UnitTestRunner& runner)
{
	runner.Register({"wallnut double vase cards", SetupWallnutVases<true>, nullptr, nullptr, 1});
	runner.Register({"wallnut single vase card when disabled", SetupWallnutVases<false>, nullptr, nullptr, 1});
	runner.Register({"wallnut double-click input and movement", SetupWallnutInput, nullptr, nullptr, 1});
	runner.Register({"global bowling giant damage", SetupBowlingDamage, nullptr, nullptr, 1});
	runner.Register({"bowling terrain and persistence", SetupBowlingTerrainAndSave, nullptr, nullptr, 1});
	runner.Register({"squash enhancement", SetupSquash<true>, nullptr, nullptr, 1});
	runner.Register({"squash enhancement disabled", SetupSquash<false>, nullptr, nullptr, 1});
	runner.Register({"vase endless stage cooldowns", SetupVaseCooldowns, nullptr, nullptr, 1});
	runner.Register({"threepeater mixed lanes and in-flight acquisition", SetupThreepeater<false>, UpdateThreepeater<false>, ThreepeaterShot, 650, 2});
	runner.Register({"threepeater Torchwood homing", SetupThreepeater<true>, UpdateThreepeater<true>, nullptr, 650, 2});
	runner.Register({"threepeater scope and persistence", SetupThreepeaterScope, nullptr, nullptr, 1});
	runner.Register({"fire pea alignment", SetupFirePeaAlignment, nullptr, nullptr, 1});
	runner.Register({"empowered peashooter", SetupEmpowered<true>, UpdateEmpowered<true>, nullptr, 1100});
	runner.Register({"empowered peashooter disabled", SetupEmpowered<false>, UpdateEmpowered<false>, nullptr, 1100});
	runner.Register({"empowered pea persistence and scope", SetupPeaPersistence, nullptr, nullptr, 1});
	runner.Register({"normal zombie walking evidence", SetupWalkingEvidence, nullptr, nullptr, 1});
	runner.Register({"leftpeater planting burst", SetupBurst, UpdateBurst, Shot, 300});
	runner.Register({"leftpeater planting burst disabled", SetupDisabledBurst, UpdateDisabledBurst, DisabledShot, 300});
	runner.Register({"plantern healing", SetupHealing<true>, UpdateHealing<true>, nullptr, 250});
	runner.Register({"plantern healing disabled", SetupHealing<false>, UpdateHealing<false>, nullptr, 250});
	runner.Register({"plantern green vases", SetupGreenVases<true>, nullptr, nullptr, 1});
	runner.Register({"plantern green vases disabled", SetupGreenVases<false>, nullptr, nullptr, 1});
	RegisterRogueTests(runner);
}
