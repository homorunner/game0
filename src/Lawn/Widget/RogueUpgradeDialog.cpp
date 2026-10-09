/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "RogueUpgradeDialog.h"
#include "../Board.h"
#include "../SeedPacket.h"
#include "../../LawnApp.h"
#include "../../Resources.h"
#include "graphics/Font.h"
#include "widget/WidgetManager.h"
#include "Common.h"
#include <algorithm>
#include <string>

namespace
{
// Bitmap fonts ship with the game data; fall back to English when the active
// edition has no glyphs for the Chinese text (missing glyphs draw zero-width).
bool CanRender(Graphics* g, const std::string& theText)
{
    _Font* aFont = g->GetFont();
    if (!aFont) return false;
    size_t aOffset = 0;
    char32_t aChar = 0;
    while (UTF8DecodeNext(theText, aOffset, aChar))
        if (aChar >= 0x80 && aFont->CharWidth(aChar) <= 0)
            return false;
    return true;
}

std::string Localized(Graphics* g, const std::string& theKey, const char* theFallback,
    const std::string& theKeyZh, const char* theFallbackZh)
{
    const std::string aText = gLawnApp->GetString(theKey, theFallback);
    const std::string aTextZh = gLawnApp->GetString(theKeyZh, theFallbackZh);
    return CanRender(g, aTextZh) ? aTextZh : aText;
}
}

RogueUpgradeDialog::RogueUpgradeDialog(LawnApp* theApp) :
    LawnDialog(theApp, DIALOG_ROGUE_UPGRADE, true, "", "", "", Dialog::BUTTONS_NONE),
    mOfferBoard(theApp->mBoard),
    mOffers(mOfferBoard ? mOfferBoard->mRogueRun.offers : std::array<int32_t, 3>{-1, -1, -1}),
    mUnlocked(mOfferBoard ? mOfferBoard->mRogueRun.unlocked : 0),
    mBurstLevel(mOfferBoard ? mOfferBoard->mRogueRun.leftpeaterBurstLevel : 0),
    mOfferCount(mOfferBoard ? std::clamp(mOfferBoard->mRogueRun.OfferCount(), 0, 3) : 0)
{
    // The standard shell tiles rather than stretches: keep its real edges on screen.
    const int aSideWidth = IMAGE_DIALOG_TOPLEFT->mWidth + IMAGE_DIALOG_TOPRIGHT->mWidth;
    const int aCapHeight = 45 + IMAGE_DIALOG_TOPLEFT->mHeight + IMAGE_DIALOG_BOTTOMLEFT->mHeight;
    const int aWidth = aSideWidth + (780 - aSideWidth) / IMAGE_DIALOG_TOPMIDDLE->mWidth * IMAGE_DIALOG_TOPMIDDLE->mWidth;
    const int aHeight = aCapHeight + (580 - aCapHeight) / IMAGE_DIALOG_CENTERLEFT->mHeight * IMAGE_DIALOG_CENTERLEFT->mHeight;
    Resize((800 - aWidth) / 2, (600 - aHeight) / 2, aWidth, aHeight);
    mWantsFocus = true;
}

Rect RogueUpgradeDialog::CardRect(int slot) const
{
    if (slot < 0 || slot >= mOfferCount)
        return Rect();

    const int aWidth = (mWidth - 100 - 28) / 3;
    const int aTotalWidth = mOfferCount * aWidth + (mOfferCount - 1) * 14;
    return Rect((mWidth - aTotalWidth) / 2 + slot * (aWidth + 14), 170, aWidth, mHeight - 245);
}

bool RogueUpgradeDialog::OffersAreCurrent() const
{
    // Never dereference the saved pointer: the board can be destroyed behind a modal.
    const Board* aBoard = mApp->mBoard;
    if (!aBoard || aBoard != mOfferBoard || !aBoard->mRogueRun.active ||
        aBoard->mRogueRun.phase != RoguePhase::Choosing ||
        aBoard->mRogueRun.offers != mOffers || aBoard->mRogueRun.unlocked != mUnlocked ||
        aBoard->mRogueRun.leftpeaterBurstLevel != mBurstLevel ||
        aBoard->mRogueRun.OfferCount() != mOfferCount || mOfferCount == 0)
        return false;

    for (int i = 0; i < mOfferCount; ++i)
    {
        if (mOffers[i] < 0 || mOffers[i] >= static_cast<int>(RogueUpgrade::COUNT) ||
            !aBoard->mRogueRun.CanChoose(static_cast<RogueUpgrade>(mOffers[i])))
            return false;
    }
    return true;
}

void RogueUpgradeDialog::AddedToManager(WidgetManager* theWidgetManager)
{
    LawnDialog::AddedToManager(theWidgetManager);
    // A held Return/number from the previous screen must be released first.
    std::copy_n(theWidgetManager->mKeyDown, mHeldKeys.size(), mHeldKeys.begin());
}

void RogueUpgradeDialog::Update()
{
    if (mClosePending || !OffersAreCurrent())
    {
        mApp->SetCursor(CURSOR_POINTER);
        mApp->KillDialog(mId);
        return;
    }
    LawnDialog::Update();
    if (mOpeningDelay > 0)
        --mOpeningDelay;
}

void RogueUpgradeDialog::Draw(Graphics* g)
{
    GraphicsStateGuard aState(*g);
    LawnDialog::Draw(g);
    if (!OffersAreCurrent())
        return;

    const Color aInk(64, 48, 29);
    const Color aGold(244, 214, 132);
    g->SetFont(FONT_DWARVENTODCRAFT24);
    g->SetColor(aGold);
    WriteCenteredLine(g, 99, mApp->GetString("ROGUE_STAGE_CLEARED", "STAGE CLEARED!"));
    g->SetFont(FONT_DWARVENTODCRAFT18);
    WriteCenteredLine(g, 127, mApp->GetString("ROGUE_CHOOSE_ONE", "Choose one upgrade"));

    for (int slot = 0; slot < mOfferCount; ++slot)
    {
        const Rect r = CardRect(slot);
        const bool aFocused = slot == mFocusedSlot;
        const bool aPressed = slot == mPressedSlot;
        const auto& aDefinition = GetRogueUpgrade(mOffers[slot]);
        const std::string aKey = std::string("ROGUE_") + aDefinition.key;

        g->SetColor(Color(0, 0, 0, 100));
        g->FillRect(r.mX + 5, r.mY + 6, r.mWidth, r.mHeight);
        g->SetColor(aFocused ? Color(232, 195, 86) : Color(103, 119, 64));
        g->FillRect(r);
        g->SetColor(aPressed ? Color(226, 211, 166) : Color(247, 234, 195));
        g->FillRect(r.mX + 4, r.mY + 4, r.mWidth - 8, r.mHeight - 8);
        g->SetColor(Color(182, 155, 100));
        g->DrawRect(r.mX + 8, r.mY + 8, r.mWidth - 17, r.mHeight - 17);

        // Numbered garden-tag tab and a subtle green art mount.
        g->SetColor(aFocused ? Color(69, 103, 40) : Color(98, 112, 66));
        g->FillRect(r.mX + 13, r.mY - 5, 27, 29);
        g->SetFont(FONT_DWARVENTODCRAFT18);
        g->SetColor(aGold);
        g->DrawString(std::to_string(slot + 1), r.mX + 21, r.mY + 16);
        g->SetColor(Color(220, 220, 168));
        g->FillRect(r.mX + r.mWidth / 2 - 39, r.mY + 69, 78, 79);
        if (aDefinition.seed != SeedType::SEED_NONE)
        {
            Graphics aSeedGraphics(*g);
            aSeedGraphics.SetLinearBlend(true);
            DrawSeedPacket(&aSeedGraphics, r.mX + r.mWidth / 2 - 25, r.mY + 74,
                aDefinition.seed, SeedType::SEED_NONE, 0.0f, 255, false, false);
        }

        Graphics aTextGraphics(*g);
        aTextGraphics.ClipRect(r.mX + 14, r.mY + 25, r.mWidth - 28, 42);
        aTextGraphics.SetFont(FONT_CONTINUUMBOLD14);
        aTextGraphics.SetColor(aInk);
        aTextGraphics.WriteWordWrapped(Rect(r.mX + 14, r.mY + 26, r.mWidth - 28, 42),
            Localized(&aTextGraphics, aKey + "_TITLE", aDefinition.title, aKey + "_TITLE_ZH", aDefinition.titleZh), 20, 0);

        Graphics aDescriptionGraphics(*g);
        const Rect aDescriptionRect(r.mX + 17, r.mY + 159, r.mWidth - 34, r.mHeight - 201);
        aDescriptionGraphics.ClipRect(aDescriptionRect);
        aDescriptionGraphics.SetFont(FONT_BRIANNETOD12);
        aDescriptionGraphics.SetColor(aInk);
        std::string description = Localized(&aDescriptionGraphics, aKey + "_DESC", aDefinition.description,
            aKey + "_DESC_ZH", aDefinition.descriptionZh);
        if (mOffers[slot] == static_cast<int>(RogueUpgrade::LeftpeaterBurst))
        {
            const int next = mOfferBoard->mRogueRun.UpgradeLevel(RogueUpgrade::LeftpeaterBurst) + 1;
            const int peas = 4 << (next - 1);
            const std::string zh = "选择后 " + std::to_string(next) + "/6 级：种植后连射 " +
                std::to_string(peas) + " 发豌豆。可重复选择，数量翻倍，发射间隔不变。";
            description = CanRender(&aDescriptionGraphics, zh) ? zh :
                "Level " + std::to_string(next) + "/6: fire " + std::to_string(peas) +
                " peas when planted. Pick again to double the volley; firing interval stays the same.";
        }
        aDescriptionGraphics.WriteWordWrapped(aDescriptionRect, description, 18, -1);

        g->SetColor(aFocused ? Color(69, 103, 40) : Color(132, 125, 87));
        g->FillRect(r.mX + 12, r.mY + r.mHeight - 32, r.mWidth - 24, 21);
        g->SetColor(Color(255, 246, 208));
        g->SetFont(FONT_BRIANNETOD12);
        g->WriteString(Localized(g, "ROGUE_SELECT", "SELECT", "ROGUE_SELECT_ZH", "选择"), r.mX + 12,
            r.mY + r.mHeight - 16, r.mWidth - 24, 0);
    }

    g->SetFont(FONT_DWARVENTODCRAFT12);
    g->SetColor(aGold);
    WriteCenteredLine(g, mHeight - 49,
        mApp->GetString("ROGUE_CONTROLS", "Click a card or press 1-3. Arrows to focus; Enter / Space to choose."));
    g->SetColor(Color(213, 218, 184));
    WriteCenteredLine(g, mHeight - 29,
        mApp->GetString("ROGUE_REWARD_NOTE", "Upgrades last for this run. Choose one to continue."));
}

int RogueUpgradeDialog::HitCard(int x, int y) const
{
    if (mClosePending || !OffersAreCurrent())
        return -1;
    for (int slot = 0; slot < mOfferCount; ++slot)
        if (CardRect(slot).Contains(x, y))
            return slot;
    return -1;
}

void RogueUpgradeDialog::Choose(int slot)
{
    if (mOpeningDelay > 0 || mClosePending || !OffersAreCurrent() || slot < 0 || slot >= mOfferCount)
        return;
    if (mApp->mBoard->ChooseRogueUpgrade(slot))
    {
        // Commit/save belongs to Board; remove this widget on the next update.
        mClosePending = true;
        mPressedSlot = -1;
        mApp->PlaySample(SOUND_BUTTONCLICK);
    }
}

void RogueUpgradeDialog::KeyDown(KeyCode theKey)
{
    const int aKey = static_cast<int>(theKey);
    if (aKey < 0 || aKey >= static_cast<int>(mHeldKeys.size()) || mHeldKeys[aKey])
        return;
    mHeldKeys[aKey] = true;
    if (mOpeningDelay > 0 || mClosePending || !OffersAreCurrent())
        return;

    if (aKey >= '1' && aKey <= '3')
        Choose(aKey - '1');
    else if (theKey == KEYCODE_RETURN || theKey == KEYCODE_SPACE)
        Choose(mFocusedSlot);
    else if (theKey == KEYCODE_LEFT || theKey == KEYCODE_UP)
        mFocusedSlot = (mFocusedSlot + mOfferCount - 1) % mOfferCount;
    else if (theKey == KEYCODE_RIGHT || theKey == KEYCODE_DOWN)
        mFocusedSlot = (mFocusedSlot + 1) % mOfferCount;
    MarkDirty();
    // Deliberately do not call LawnDialog: Escape/Return must not dismiss a reward.
}

void RogueUpgradeDialog::KeyUp(KeyCode theKey)
{
    const int aKey = static_cast<int>(theKey);
    if (aKey >= 0 && aKey < static_cast<int>(mHeldKeys.size()))
        mHeldKeys[aKey] = false;
}

void RogueUpgradeDialog::KeyChar(char) {}

void RogueUpgradeDialog::MouseDown(int x, int y, int theClickCount)
{
    // Legacy encoding: negative = right, 3 = middle, 1/2 = left click/double click.
    MouseDown(x, y, theClickCount == 3 ? 2 : (theClickCount < 0 ? 1 : 0), theClickCount);
}

void RogueUpgradeDialog::MouseDown(int x, int y, int theBtnNum, int)
{
    mPressedSlot = -1;
    if (theBtnNum != 0 || mOpeningDelay > 0)
        return;
    mPressedSlot = HitCard(x, y);
    if (mPressedSlot >= 0)
        mFocusedSlot = mPressedSlot;
    MarkDirty();
}

void RogueUpgradeDialog::MouseUp(int x, int y)
{
    MouseUp(x, y, 0, 1);
}

void RogueUpgradeDialog::MouseUp(int x, int y, int theClickCount)
{
    MouseUp(x, y, theClickCount == 3 ? 2 : (theClickCount < 0 ? 1 : 0), theClickCount);
}

void RogueUpgradeDialog::MouseUp(int x, int y, int theBtnNum, int)
{
    const int aPressedSlot = mPressedSlot;
    mPressedSlot = -1;
    if (theBtnNum == 0 && aPressedSlot >= 0 && HitCard(x, y) == aPressedSlot)
        Choose(aPressedSlot);
    MarkDirty();
}

void RogueUpgradeDialog::MouseMove(int x, int y)
{
    const int aSlot = HitCard(x, y);
    if (aSlot >= 0)
        mFocusedSlot = aSlot;
    mApp->SetCursor(aSlot >= 0 && mOpeningDelay == 0 ? CURSOR_HAND : CURSOR_POINTER);
    MarkDirty();
}

void RogueUpgradeDialog::MouseDrag(int x, int y)
{
    MouseMove(x, y);
}

void RogueUpgradeDialog::MouseLeave()
{
    mPressedSlot = -1;
    mApp->SetCursor(CURSOR_POINTER);
    MarkDirty();
}

void RogueUpgradeDialog::LostFocus()
{
    LawnDialog::LostFocus();
    mPressedSlot = -1;
    mHeldKeys.fill(false);
    mOpeningDelay = 15;
}
