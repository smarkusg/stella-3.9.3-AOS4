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

#ifndef CARTRIDGE_HXX
#define CARTRIDGE_HXX

#include <fstream>
#include <sstream>

class Cartridge;
class Properties;
class CartDebugWidget;
class GuiObject;

#include "bspf.hxx"
#include "Array.hxx"
#include "Device.hxx"
#include "Settings.hxx"
#include "Font.hxx"

struct RamArea {
  uInt16 start;  uInt16 size;  uInt16 roffset;  uInt16 woffset;
};
typedef Common::Array<RamArea> RamAreaList;

/**
  A cartridge is a device which contains the machine code for a 
  game and handles any bankswitching performed by the cartridge.
  A 'bank' is defined as a 4K block that is visible in the
  0x1000-0x2000 area (or its mirrors).
 
  @author  Bradford W. Mott
  @version $Id$
*/
class Cartridge : public Device
{
  public:
    /**
      Create a new cartridge object allocated on the heap.  The
      type of cartridge created depends on the properties object.

      @param image    A pointer to the ROM image
      @param size     The size of the ROM image 
      @param md5      The md5sum for the given ROM image (can be updated)
      @param dtype    The detected bankswitch type of the ROM image
      @param id       Any extra info about the ROM (currently which part
                      of a multiload game is being accessed
      @param system   The osystem associated with the system
      @param settings The settings associated with the system
      @return   Pointer to the new cartridge object allocated on the heap
    */
    static Cartridge* create(const uInt8* image, uInt32 size, string& md5,
                             string& dtype, string& id,
                             const OSystem& system, Settings& settings);

    /**
      Create a new cartridge

      @param settings  A reference to the various settings (read-only)
    */
    Cartridge(const Settings& settings);
 
    /**
      Destructor
    */
    virtual ~Cartridge();

    /**
      Query some information about this cartridge.
    */
    static const string& about() { return myAboutString; }

    /**
      Save the internal (patched) ROM image.

      @param out  The output file stream to save the image
    */
    bool save(ofstream& out);

    /**
      Lock/unlock bankswitching capability.  The debugger will lock
      the banks before querying the cart state, otherwise reading values
      could inadvertantly cause a bankswitch to occur.
    */
    void lockBank()   { myBankLocked = true;  }
    void unlockBank() { myBankLocked = false; }
    bool bankLocked() { return myBankLocked;  }

    /**
      Get the default startup bank for a cart.  This is the bank where
      the system will look at address 0xFFFC to determine where to
      start running code.

      @return  The startup bank
    */
    uInt16 startBank();

    /**
      Answer whether the bank has changed since the last time this
      method was called.  Each cart class is able to override this
      method to deal with its specific functionality.  In those cases,
      the derived class is still responsible for calling this base
      function.

      @return  Whether the bank was changed
    */
    virtual bool bankChanged();

    const RamAreaList& ramAreas() { return myRamAreaList; }

  public:
    //////////////////////////////////////////////////////////////////////
    // The following methods are cart-specific and must be implemented
    // in derived classes.
    //////////////////////////////////////////////////////////////////////
    /**
      Set the specified bank.
    */
    virtual bool bank(uInt16 bank) = 0;

    /**
      Get the current bank.
    */
    virtual uInt16 bank() const = 0;

    /**
      Query the number of 'banks' supported by the cartridge.  Note that
      this information is cart-specific, where each cart basically defines
      what a 'bank' is.

      For the normal Atari-manufactured carts, a standard bank is a 4K
      block that is directly accessible in the 4K address space.  In other
      cases where ROMs have 2K blocks in some preset area, the bankCount
      is the number of such blocks.  Finally, in some esoteric schemes,
      the number of ways that the addressing can change (multiple ROM and
      RAM slices at multiple access points) is so complicated that the
      cart will report having only one 'virtual' bank.
    */
    virtual uInt16 bankCount() const = 0;

    /**
      Patch the cartridge ROM.

      @param address  The ROM address to patch
      @param value    The value to place into the address
      @return    Success or failure of the patch operation
    */
    virtual bool patch(uInt16 address, uInt8 value) = 0;

    /**
      Access the internal ROM image for this cartridge.

      @param size  Set to the size of the internal ROM image data
      @return  A pointer to the internal ROM image data
    */
    virtual const uInt8* getImage(int& size) const = 0;

    /**
      Save the current state of this device to the given Serializer.

      @param out  The Serializer object to use
      @return  False on any errors, else true
    */
    virtual bool save(Serializer& out) const = 0;

    /**
      Load the current state of this device from the given Serializer.

      @param in  The Serializer object to use
      @return  False on any errors, else true
    */
    virtual bool load(Serializer& in) = 0;

    /**
      Get a descriptor for the device name (used in error checking).

      @return The name of the object
    */
    virtual string name() const = 0;

    /**
      Informs the cartridge about the name of the ROM file used when
      creating this cart.

      @param name  The properties file name of the ROM
    */
    virtual void setRomName(const string& name) { }

    /**
      Get debugger widget responsible for accessing the inner workings
      of the cart.  This will need to be overridden and implemented by
      each specific cart type, since the bankswitching/inner workings
      of each cart type can be very different from each other.
    */
    virtual CartDebugWidget* debugWidget(GuiObject* boss, const GUI::Font& lfont,
        const GUI::Font& nfont, int x, int y, int w, int h) { return NULL; }

  protected:
    /**
      Add the given area to the RamArea list for this cart.

      @param start    The beginning of the RAM area (0x0000 - 0x2000)
      @param size     Total number of bytes of area
      @param roffset  Offset to use when reading from RAM (read port)
      @param woffset  Offset to use when writing to RAM (write port)
    */
    void registerRamArea(uInt16 start, uInt16 size, uInt16 roffset, uInt16 woffset);

    /**
      Indicate that an illegal read from a write port has occurred.

      @param address  The address of the illegal read
    */
    void triggerReadFromWritePort(uInt16 address);

    /**
      Create an array that holds code-access information for every byte
      of the ROM (indicated by 'size').  Note that this is only used by
      the debugger, and is unavailable otherwise.

      @param size  The size of the code-access array to create
    */
    void createCodeAccessBase(uInt32 size);

  private:
    /**
      Get an image pointer and size for a ROM that is part of a larger,
      multi-ROM image.

      @param image    A pointer to the ROM image
      @param size     The size of the ROM image 
      @param numroms  The number of ROMs in the multicart
      @param md5      The md5sum for the specific cart in the ROM image
      @param id       The ID for the specific cart in the ROM image
      @param settings The settings associated with the system
      @return   The bankswitch type for the specific cart in the ROM image
    */
    static string createFromMultiCart(const uInt8*& image, uInt32& size,
        uInt32 numroms, string& md5, string& id, Settings& settings);

    /**
      Try to auto-detect the bankswitching type of the cartridge

      @param image  A pointer to the ROM image
      @param size   The size of the ROM image 
      @return The "best guess" for the cartridge type
    */
    static string autodetectType(const uInt8* image, uInt32 size);

    /**
      Search the image for the specified byte signature

      @param image      A pointer to the ROM image
      @param imagesize  The size of the ROM image 
      @param signature  The byte sequence to search for
      @param sigsize    The number of bytes in the signature
      @param minhits    The minimum number of times a signature is to be found

      @return  True if the signature was found at least 'minhits' time, else false
    */
    static bool searchForBytes(const uInt8* image, uInt32 imagesize,
                               const uInt8* signature, uInt32 sigsize,
                               uInt32 minhits);

    /**
      Returns true if the image is probably a SuperChip (256 bytes RAM)
    */
    static bool isProbablySC(const uInt8* image, uInt32 size);

    /**
      Returns true if the image is probably a 4K SuperChip (256 bytes RAM)
    */
    static bool isProbably4KSC(const uInt8* image, uInt32 size);

    /**
      Returns true if the image probably contains ARM code in the first 1K
    */
    static bool isProbablyARM(const uInt8* image, uInt32 size);

    /**
      Returns true if the image is probably a 0840 bankswitching cartridge
    */
    static bool isProbably0840(const uInt8* image, uInt32 size);

    /**
      Returns true if the image is probably a 3E bankswitching cartridge
    */
    static bool isProbably3E(const uInt8* image, uInt32 size);

    /**
      Returns true if the image is probably a 3F bankswitching cartridge
    */
    static bool isProbably3F(const uInt8* image, uInt32 size);

    /**
      Returns true if the image is probably a 4A50 bankswitching cartridge
    */
    static bool isProbably4A50(const uInt8* image, uInt32 size);

    /**
      Returns true if the image is probably a CTY bankswitching cartridge
    */
    static bool isProbablyCTY(const uInt8* image, uInt32 size);

    /**
      Returns true if the image is probably a CV bankswitching cartridge
    */
    static bool isProbablyCV(const uInt8* image, uInt32 size);

    /**
      Returns true if the image is probably a DPC+ bankswitching cartridge
    */
    static bool isProbablyDPCplus(const uInt8* image, uInt32 size);

    /**
      Returns true if the image is probably a E0 bankswitching cartridge
    */
    static bool isProbablyE0(const uInt8* image, uInt32 size);

    /**
      Returns true if the image is probably a E7 bankswitching cartridge
    */
    static bool isProbablyE7(const uInt8* image, uInt32 size);

    /**
      Returns true if the image is probably an EF/EFSC bankswitching cartridge
    */
    static bool isProbablyEF(const uInt8* image, uInt32 size, const char*& type);

    /**
      Returns true if the image is probably a BF/BFSC bankswitching cartridge
    */
    static bool isProbablyBF(const uInt8* image, uInt32 size, const char*& type);
    /**
      Returns true if the image is probably a DF/DFSC bankswitching cartridge
    */
    static bool isProbablyDF(const uInt8* image, uInt32 size, const char*& type);

    /**
      Returns true if the image is probably an F6 bankswitching cartridge
    */
    static bool isProbablyF6(const uInt8* image, uInt32 size);

    /**
      Returns true if the image is probably an FA2 bankswitching cartridge
    */
    static bool isProbablyFA2(const uInt8* image, uInt32 size);

    /**
      Returns true if the image is probably an FE bankswitching cartridge
    */
    static bool isProbablyFE(const uInt8* image, uInt32 size);

    /**
      Returns true if the image is probably a SB bankswitching cartridge
    */
    static bool isProbablySB(const uInt8* image, uInt32 size);

    /**
      Returns true if the image is probably a UA bankswitching cartridge
    */
    static bool isProbablyUA(const uInt8* image, uInt32 size);

    /**
      Returns true if the image is probably an X07 bankswitching cartridge
    */
    static bool isProbablyX07(const uInt8* image, uInt32 size);

  protected:
    // Settings class for the application
    const Settings& mySettings;

    // The startup bank to use (where to look for the reset vector address)
    uInt16 myStartBank;

    // Indicates if the bank has changed somehow (a bankswitch has occurred)
    bool myBankChanged;

    // The array containing information about every byte of ROM indicating
    // whether it is used as code.
    uInt8* myCodeAccessBase;

  private:
    // Contains RamArea entries for those carts with accessible RAM.
    RamAreaList myRamAreaList;

    // If myBankLocked is true, ignore attempts at bankswitching. This is used
    // by the debugger, when disassembling/dumping ROM.
    bool myBankLocked;

    // Contains info about this cartridge in string format
    static string myAboutString;

    // Copy constructor isn't supported by cartridges so make it private
    Cartridge(const Cartridge&);

    // Assignment operator isn't supported by cartridges so make it private
    Cartridge& operator = (const Cartridge&);
};

#endif
