/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef __ROGUEUPGRADEDIALOG_H__
#define __ROGUEUPGRADEDIALOG_H__

#include "LawnDialog.h"
#include "../RogueRun.h"
#include <array>
#include <cstdint>

class Board;

class RogueUpgradeDialog : public LawnDialog
{
public:
    explicit RogueUpgradeDialog(LawnApp* theApp);

    // Dialog-local coordinates, also used by input hit testing.
    Rect CardRect(int slot) const;
    void AddedToManager(WidgetManager* theWidgetManager) override;
    void Update() override;
    void Draw(Graphics* g) override;
    void KeyDown(KeyCode theKey) override;
    void KeyUp(KeyCode theKey) override;
    void KeyChar(char theChar) override;
    void MouseDown(int x, int y, int theClickCount) override;
    void MouseDown(int x, int y, int theBtnNum, int theClickCount) override;
    void MouseUp(int x, int y) override;
    void MouseUp(int x, int y, int theClickCount) override;
    void MouseUp(int x, int y, int theBtnNum, int theClickCount) override;
    void MouseMove(int x, int y) override;
    void MouseDrag(int x, int y) override;
    void MouseLeave() override;
    void LostFocus() override;

private:
    Board* mOfferBoard;
    std::array<int32_t, 3> mOffers;
    uint32_t mUnlocked;
    uint32_t mBurstLevel;
    std::array<bool, 255> mHeldKeys{};
    int mOfferCount;
    int mFocusedSlot = 0;
    int mPressedSlot = -1;
    int mOpeningDelay = 15;
    bool mClosePending = false;

    bool OffersAreCurrent() const;
    int HitCard(int x, int y) const;
    void Choose(int slot);
};

#endif
