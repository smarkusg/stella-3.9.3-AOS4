//============================================================================
//
//   SSSS    tt          lll  lll       
//  SS  SS   tt           ll   ll        
//  SS     tttttt  eeee   ll   ll   aaaa 
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-2014 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//
// $Id$
//============================================================================

#ifndef CARTRIDGE3E_WIDGET_HXX
#define CARTRIDGE3E_WIDGET_HXX

class Cartridge3E;
class PopUpWidget;

#include "CartDebugWidget.hxx"

class Cartridge3EWidget : public CartDebugWidget
{
  public:
    Cartridge3EWidget(GuiObject* boss, const GUI::Font& lfont,
                      const GUI::Font& nfont,
                      int x, int y, int w, int h,
                      Cartridge3E& cart);
    virtual ~Cartridge3EWidget() { }

    void loadConfig();
    void handleCommand(CommandSender* sender, int cmd, int data, int id);

    string bankState();

  private:
    Cartridge3E& myCart;
    const uInt32 myNumRomBanks;
    const uInt32 myNumRamBanks;
    PopUpWidget *myROMBank, *myRAMBank;

    enum {
      kROMBankChanged = 'rmCH',
      kRAMBankChanged = 'raCH'
    };
};

#endif
