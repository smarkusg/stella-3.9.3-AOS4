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

#include <cassert>
#include <cstdlib>
#include <cstring>

#include "bspf.hxx"

#ifdef DEBUGGER_SUPPORT
  #include "CartDebug.hxx"
#endif

#include "Console.hxx"
#include "Control.hxx"
#include "Device.hxx"
#include "M6502.hxx"
#include "Settings.hxx"
#include "Sound.hxx"
#include "System.hxx"
#include "TIATables.hxx"

#include "TIA.hxx"

#define HBLANK 68

#define CLAMP_POS(reg) if(reg < 0) { reg += 160; }  reg %= 160;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
TIA::TIA(Console& console, Sound& sound, Settings& settings)
  : myConsole(console),
    mySound(sound),
    mySettings(settings),
    myFrameYStart(34),
    myFrameHeight(210),
    myMaximumNumberOfScanlines(262),
    myStartScanline(0),
    myColorLossEnabled(false),
    myPartialFrameFlag(false),
    myAutoFrameEnabled(false),
    myFrameCounter(0),
    myPALFrameCounter(0),
    myBitsEnabled(true),
    myCollisionsEnabled(true)
   
{
  // Allocate buffers for two frame buffers
  myCurrentFrameBuffer = new uInt8[160 * 320];
  myPreviousFrameBuffer = new uInt8[160 * 320];

  // Make sure all TIA bits are enabled
  enableBits(true);

  // Turn off debug colours (this also sets up the PriorityEncoder)
  toggleFixedColors(0);

  // Compute all of the mask tables
  TIATables::computeAllTables();

  // Zero audio registers
  myAUDV0 = myAUDV1 = myAUDF0 = myAUDF1 = myAUDC0 = myAUDC1 = 0;

  // Should undriven pins be randomly pulled high or low?
  myTIAPinsDriven = mySettings.getBool("tiadriven");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
TIA::~TIA()
{
  delete[] myCurrentFrameBuffer;
  delete[] myPreviousFrameBuffer;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TIA::reset()
{
  // Reset the sound device
  mySound.reset();

  // Currently no objects are enabled or selectively disabled
  myEnabledObjects = 0;
  myDisabledObjects = 0xFF;
  myAllowHMOVEBlanks = true;

  // Some default values for the registers
  myVSYNC = myVBLANK = 0;
  myNUSIZ0 = myNUSIZ1 = 0;
  myColor[P0Color] = myColor[P1Color] = myColor[PFColor] = myColor[BKColor] = 0;
  myColor[M0Color] = myColor[M1Color] = myColor[BLColor] = myColor[HBLANKColor] = 0;

  myPlayfieldPriorityAndScore = 0;
  myCTRLPF = 0;
  myREFP0 = myREFP1 = false;
  myPF = 0;
  myGRP0 = myGRP1 = myDGRP0 = myDGRP1 = 0;
  myENAM0 = myENAM1 = myENABL = myDENABL = false;
  myHMP0 = myHMP1 = myHMM0 = myHMM1 = myHMBL = 0;
  myVDELP0 = myVDELP1 = myVDELBL = myRESMP0 = myRESMP1 = false;
  myCollision = 0;
  myCollisionEnabledMask = 0xFFFFFFFF;
  myPOSP0 = myPOSP1 = myPOSM0 = myPOSM1 = myPOSBL = 0;

  // Some default values for the "current" variables
  myCurrentGRP0 = 0;
  myCurrentGRP1 = 0;

  myMotionClockP0 = 0;
  myMotionClockP1 = 0;
  myMotionClockM0 = 0;
  myMotionClockM1 = 0;
  myMotionClockBL = 0;

  mySuppressP0 = mySuppressP1 = 0;

  myHMP0mmr = myHMP1mmr = myHMM0mmr = myHMM1mmr = myHMBLmmr = false;

  myCurrentHMOVEPos = myPreviousHMOVEPos = 0x7FFFFFFF;
  myHMOVEBlankEnabled = false;

  enableBits(true);

  myDumpEnabled = false;
  myDumpDisabledCycle = 0;
  myINPT4 = myINPT5 = 0x80;

  myFrameCounter = myPALFrameCounter = 0;
  myScanlineCountForLastFrame = 0;

  myP0Mask = &TIATables::PxMask[0][0][0];
  myP1Mask = &TIATables::PxMask[0][0][0];
  myM0Mask = &TIATables::MxMask[0][0][0];
  myM1Mask = &TIATables::MxMask[0][0][0];
  myBLMask = &TIATables::BLMask[0][0];
  myPFMask = TIATables::PFMask[0];

  // Recalculate the size of the display
  toggleFixedColors(0);
  frameReset();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TIA::frameReset()
{
  // Clear frame buffers
  clearBuffers();

  // Reset pixel pointer and drawing flag
  myFramePointer = myCurrentFrameBuffer;

  // Calculate color clock offsets for starting and stopping frame drawing
  // Note that although we always start drawing at scanline zero, the
  // framebuffer that is exposed outside the class actually starts at 'ystart'
  myFramePointerOffset = 160 * myFrameYStart;

  myAutoFrameEnabled = (mySettings.getInt("framerate") <= 0);
  myFramerate = myConsole.getFramerate();

  if(myFramerate > 55.0)  // NTSC
  {
    myFixedColor[P0Color]     = 0x30;
    myFixedColor[P1Color]     = 0x16;
    myFixedColor[M0Color]     = 0x38;
    myFixedColor[M1Color]     = 0x12;
    myFixedColor[BLColor]     = 0x7e;
    myFixedColor[PFColor]     = 0x76;
    myFixedColor[BKColor]     = 0x0a;
    myFixedColor[HBLANKColor] = 0x0e;
    myColorLossEnabled = false;
    myMaximumNumberOfScanlines = 290;
  }
  else
  {
    myFixedColor[P0Color]     = 0x62;
    myFixedColor[P1Color]     = 0x26;
    myFixedColor[M0Color]     = 0x68;
    myFixedColor[M1Color]     = 0x2e;
    myFixedColor[BLColor]     = 0xde;
    myFixedColor[PFColor]     = 0xd8;
    myFixedColor[BKColor]     = 0x1c;
    myFixedColor[HBLANKColor] = 0x0e;
    myColorLossEnabled = mySettings.getBool("colorloss");
    myMaximumNumberOfScanlines = 342;
  }

  // NTSC screens will process at least 262 scanlines,
  // while PAL will have at least 312
  // In any event, at most 320 lines can be processed
  uInt32 scanlines = myFrameYStart + myFrameHeight;
  if(myMaximumNumberOfScanlines == 290)
    scanlines = BSPF_max(scanlines, 262u);  // NTSC
  else
    scanlines = BSPF_max(scanlines, 312u);  // PAL
  myStopDisplayOffset = 228 * BSPF_min(scanlines, 320u);

  // Reasonable values to start and stop the current frame drawing
  myClockWhenFrameStarted = mySystem->cycles() * 3;
  myClockStartDisplay = myClockWhenFrameStarted;
  myClockStopDisplay = myClockWhenFrameStarted + myStopDisplayOffset;
  myClockAtLastUpdate = myClockWhenFrameStarted;
  myClocksToEndOfScanLine = 228;
  myVSYNCFinishClock = 0x7FFFFFFF;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TIA::systemCyclesReset()
{
  // Get the current system cycle
  uInt32 cycles = mySystem->cycles();

  // Adjust the sound cycle indicator
  mySound.adjustCycleCounter(-1 * cycles);

  // Adjust the dump cycle
  myDumpDisabledCycle -= cycles;

  // Get the current color clock the system is using
  uInt32 clocks = cycles * 3;

  // Adjust the clocks by this amount since we're reseting the clock to zero
  myClockWhenFrameStarted -= clocks;
  myClockStartDisplay -= clocks;
  myClockStopDisplay -= clocks;
  myClockAtLastUpdate -= clocks;
  myVSYNCFinishClock -= clocks;
}
 
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TIA::install(System& system)
{
  install(system, *this);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TIA::install(System& system, Device& device)
{
  // Remember which system I'm installed in
  mySystem = &system;

  uInt16 shift = mySystem->pageShift();
  mySystem->resetCycles();

  // All accesses are to the given device
  System::PageAccess access(0, 0, 0, &device, System::PA_READWRITE);

  // We're installing in a 2600 system
  for(uInt32 i = 0; i < 8192; i += (1 << shift))
    if((i & 0x1080) == 0x0000)
      mySystem->setPageAccess(i >> shift, access);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool TIA::save(Serializer& out) const
{
  const string& device = name();

  try
  {
    out.putString(device);

    out.putInt(myClockWhenFrameStarted);
    out.putInt(myClockStartDisplay);
    out.putInt(myClockStopDisplay);
    out.putInt(myClockAtLastUpdate);
    out.putInt(myClocksToEndOfScanLine);
    out.putInt(myScanlineCountForLastFrame);
    out.putInt(myVSYNCFinishClock);

    out.putByte(myEnabledObjects);
    out.putByte(myDisabledObjects);

    out.putByte(myVSYNC);
    out.putByte(myVBLANK);
    out.putByte(myNUSIZ0);
    out.putByte(myNUSIZ1);

    out.putByteArray(myColor, 8);

    out.putByte(myCTRLPF);
    out.putByte(myPlayfieldPriorityAndScore);
    out.putBool(myREFP0);
    out.putBool(myREFP1);
    out.putInt(myPF);
    out.putByte(myGRP0);
    out.putByte(myGRP1);
    out.putByte(myDGRP0);
    out.putByte(myDGRP1);
    out.putBool(myENAM0);
    out.putBool(myENAM1);
    out.putBool(myENABL);
    out.putBool(myDENABL);
    out.putByte(myHMP0);
    out.putByte(myHMP1);
    out.putByte(myHMM0);
    out.putByte(myHMM1);
    out.putByte(myHMBL);
    out.putBool(myVDELP0);
    out.putBool(myVDELP1);
    out.putBool(myVDELBL);
    out.putBool(myRESMP0);
    out.putBool(myRESMP1);
    out.putShort(myCollision);
    out.putInt(myCollisionEnabledMask);
    out.putByte(myCurrentGRP0);
    out.putByte(myCurrentGRP1);

    out.putBool(myDumpEnabled);
    out.putInt(myDumpDisabledCycle);

    out.putShort(myPOSP0);
    out.putShort(myPOSP1);
    out.putShort(myPOSM0);
    out.putShort(myPOSM1);
    out.putShort(myPOSBL);

    out.putInt(myMotionClockP0);
    out.putInt(myMotionClockP1);
    out.putInt(myMotionClockM0);
    out.putInt(myMotionClockM1);
    out.putInt(myMotionClockBL);

    out.putInt(myStartP0);
    out.putInt(myStartP1);
    out.putInt(myStartM0);
    out.putInt(myStartM1);

    out.putByte(mySuppressP0);
    out.putByte(mySuppressP1);

    out.putBool(myHMP0mmr);
    out.putBool(myHMP1mmr);
    out.putBool(myHMM0mmr);
    out.putBool(myHMM1mmr);
    out.putBool(myHMBLmmr);

    out.putInt(myCurrentHMOVEPos);
    out.putInt(myPreviousHMOVEPos);
    out.putBool(myHMOVEBlankEnabled);

    out.putInt(myFrameCounter);
    out.putInt(myPALFrameCounter);

    // Save the sound sample stuff ...
    mySound.save(out);
  }
  catch(...)
  {
    cerr << "ERROR: TIA::save" << endl;
    return false;
  }

  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool TIA::load(Serializer& in)
{
  const string& device = name();

  try
  {
    if(in.getString() != device)
      return false;

    myClockWhenFrameStarted = (Int32) in.getInt();
    myClockStartDisplay = (Int32) in.getInt();
    myClockStopDisplay = (Int32) in.getInt();
    myClockAtLastUpdate = (Int32) in.getInt();
    myClocksToEndOfScanLine = (Int32) in.getInt();
    myScanlineCountForLastFrame = in.getInt();
    myVSYNCFinishClock = (Int32) in.getInt();

    myEnabledObjects = in.getByte();
    myDisabledObjects = in.getByte();

    myVSYNC = in.getByte();
    myVBLANK = in.getByte();
    myNUSIZ0 = in.getByte();
    myNUSIZ1 = in.getByte();

    in.getByteArray(myColor, 8);

    myCTRLPF = in.getByte();
    myPlayfieldPriorityAndScore = in.getByte();
    myREFP0 = in.getBool();
    myREFP1 = in.getBool();
    myPF = in.getInt();
    myGRP0 = in.getByte();
    myGRP1 = in.getByte();
    myDGRP0 = in.getByte();
    myDGRP1 = in.getByte();
    myENAM0 = in.getBool();
    myENAM1 = in.getBool();
    myENABL = in.getBool();
    myDENABL = in.getBool();
    myHMP0 = in.getByte();
    myHMP1 = in.getByte();
    myHMM0 = in.getByte();
    myHMM1 = in.getByte();
    myHMBL = in.getByte();
    myVDELP0 = in.getBool();
    myVDELP1 = in.getBool();
    myVDELBL = in.getBool();
    myRESMP0 = in.getBool();
    myRESMP1 = in.getBool();
    myCollision = in.getShort();
    myCollisionEnabledMask = in.getInt();
    myCurrentGRP0 = in.getByte();
    myCurrentGRP1 = in.getByte();

    myDumpEnabled = in.getBool();
    myDumpDisabledCycle = (Int32) in.getInt();

    myPOSP0 = (Int16) in.getShort();
    myPOSP1 = (Int16) in.getShort();
    myPOSM0 = (Int16) in.getShort();
    myPOSM1 = (Int16) in.getShort();
    myPOSBL = (Int16) in.getShort();

    myMotionClockP0 = (Int32) in.getInt();
    myMotionClockP1 = (Int32) in.getInt();
    myMotionClockM0 = (Int32) in.getInt();
    myMotionClockM1 = (Int32) in.getInt();
    myMotionClockBL = (Int32) in.getInt();

    myStartP0 = (Int32) in.getInt();
    myStartP1 = (Int32) in.getInt();
    myStartM0 = (Int32) in.getInt();
    myStartM1 = (Int32) in.getInt();

    mySuppressP0 = in.getByte();
    mySuppressP1 = in.getByte();

    myHMP0mmr = in.getBool();
    myHMP1mmr = in.getBool();
    myHMM0mmr = in.getBool();
    myHMM1mmr = in.getBool();
    myHMBLmmr = in.getBool();

    myCurrentHMOVEPos = (Int32) in.getInt();
    myPreviousHMOVEPos = (Int32) in.getInt();
    myHMOVEBlankEnabled = in.getBool();

    myFrameCounter = in.getInt();
    myPALFrameCounter = in.getInt();

    // Load the sound sample stuff ...
    mySound.load(in);

    // Reset TIA bits to be on
    enableBits(true);
    toggleFixedColors(0);
    myAllowHMOVEBlanks = true;
  }
  catch(...)
  {
    cerr << "ERROR: TIA::load" << endl;
    return false;
  }

  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool TIA::saveDisplay(Serializer& out) const
{
  try
  {
    out.putBool(myPartialFrameFlag);
    out.putInt(myFramePointerClocks);
    out.putByteArray(myCurrentFrameBuffer, 160*320);
  }
  catch(...)
  {
    cerr << "ERROR: TIA::saveDisplay" << endl;
    return false;
  }

  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool TIA::loadDisplay(Serializer& in)
{
  try
  {
    myPartialFrameFlag = in.getBool();
    myFramePointerClocks = in.getInt();

    // Reset frame buffer pointer and data
    clearBuffers();
    myFramePointer = myCurrentFrameBuffer;
    in.getByteArray(myCurrentFrameBuffer, 160*320);
    memcpy(myPreviousFrameBuffer, myCurrentFrameBuffer, 160*320);

    // If we're in partial frame mode, make sure to re-create the screen
    // as it existed when the state was saved
    if(myPartialFrameFlag)
      myFramePointer += myFramePointerClocks;
  }
  catch(...)
  {
    cerr << "ERROR: TIA::loadDisplay" << endl;
    return false;
  }

  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TIA::update()
{
  // if we've finished a frame, start a new one
  if(!myPartialFrameFlag)
    startFrame();

  // Partial frame flag starts out true here. When then 6502 strobes VSYNC,
  // TIA::poke() will set this flag to false, so we'll know whether the
  // frame got finished or interrupted by the debugger hitting a break/trap.
  myPartialFrameFlag = true;

  // Execute instructions until frame is finished, or a breakpoint/trap hits
  mySystem->m6502().execute(25000);

  // TODO: have code here that handles errors....

  endFrame();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
inline void TIA::startFrame()
{
  // This stuff should only happen at the beginning of a new frame.
  uInt8* tmp = myCurrentFrameBuffer;
  myCurrentFrameBuffer = myPreviousFrameBuffer;
  myPreviousFrameBuffer = tmp;

  // Remember the number of clocks which have passed on the current scanline
  // so that we can adjust the frame's starting clock by this amount.  This
  // is necessary since some games position objects during VSYNC and the
  // TIA's internal counters are not reset by VSYNC.
  uInt32 clocks = ((mySystem->cycles() * 3) - myClockWhenFrameStarted) % 228;

  // Ask the system to reset the cycle count so it doesn't overflow
  mySystem->resetCycles();

  // Setup clocks that'll be used for drawing this frame
  myClockWhenFrameStarted = -1 * clocks;
  myClockStartDisplay = myClockWhenFrameStarted;
  myClockStopDisplay = myClockWhenFrameStarted + myStopDisplayOffset;
  myClockAtLastUpdate = myClockStartDisplay;
  myClocksToEndOfScanLine = 228;

  // Reset frame buffer pointer
  myFramePointer = myCurrentFrameBuffer;
  myFramePointerClocks = 0;

  // If color loss is enabled then update the color registers based on
  // the number of scanlines in the last frame that was generated
  if(myColorLossEnabled)
  {
    if(myScanlineCountForLastFrame & 0x01)
    {
      myColor[P0Color] |= 0x01;
      myColor[P1Color] |= 0x01;
      myColor[PFColor] |= 0x01;
      myColor[BKColor] |= 0x01;
      myColor[M0Color] |= 0x01;
      myColor[M1Color] |= 0x01;
      myColor[BLColor] |= 0x01;
    }
    else
    {
      myColor[P0Color] &= 0xfe;
      myColor[P1Color] &= 0xfe;
      myColor[PFColor] &= 0xfe;
      myColor[BKColor] &= 0xfe;
      myColor[M0Color] &= 0xfe;
      myColor[M1Color] &= 0xfe;
      myColor[BLColor] &= 0xfe;
    }
  }
  myStartScanline = 0;

  // Stats counters
  myFrameCounter++;
  if(myScanlineCountForLastFrame >= 287)
    myPALFrameCounter++;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
inline void TIA::endFrame()
{
  uInt32 currentlines = scanlines();

  // The TIA may generate frames that are 'invisible' to TV (they complete
  // before the first visible scanline)
  // Such 'short' frames can't simply be eliminated, since they're running
  // code at that point; however, they are not shown at all, otherwise the
  // double-buffering of the video output will get confused
  if(currentlines <= myStartScanline)
  {
    // Skip display of this frame, as if it wasn't generated at all
    startFrame();
    myFrameCounter--;  // This frame doesn't contribute to frame count
    return;
  }

  // Compute the number of scanlines in the frame
  uInt32 previousCount = myScanlineCountForLastFrame;
  myScanlineCountForLastFrame = currentlines;

  // The following handle cases where scanlines either go too high or too
  // low compared to the previous frame, in which case certain portions
  // of the framebuffer are cleared to zero (black pixels)
  // Due to the FrameBuffer class (potentially) doing dirty-rectangle
  // updates, each internal buffer must be set slightly differently,
  // otherwise they won't know anything has changed
  // Hence, the front buffer is set to pixel 0, and the back to pixel 1

  // Did we generate too many scanlines?
  // (usually caused by VBLANK/VSYNC taking too long or not occurring at all)
  // If so, blank entire viewable area
  if(myScanlineCountForLastFrame > myMaximumNumberOfScanlines+1)
  {
    myScanlineCountForLastFrame = myMaximumNumberOfScanlines;
    if(previousCount < myMaximumNumberOfScanlines)
    {
      memset(myCurrentFrameBuffer, 0, 160 * 320);
      memset(myPreviousFrameBuffer, 1, 160 * 320);
    }
  }
  // Did the number of scanlines decrease?
  // If so, blank scanlines that weren't rendered this frame
  else if(myScanlineCountForLastFrame < previousCount &&
          myScanlineCountForLastFrame < 320 && previousCount < 320)
  {
    uInt32 offset = myScanlineCountForLastFrame * 160,
           stride = (previousCount - myScanlineCountForLastFrame) * 160;
    memset(myCurrentFrameBuffer + offset, 0, stride);
    memset(myPreviousFrameBuffer + offset, 1, stride);
  }

  // Recalculate framerate. attempting to auto-correct for scanline 'jumps'
  if(myAutoFrameEnabled)
  {
    myFramerate = (myScanlineCountForLastFrame > 285 ? 15600.0 : 15720.0) /
                   myScanlineCountForLastFrame;
    myConsole.setFramerate(myFramerate);

    // Adjust end-of-frame pointer
    // We always accommodate the highest # of scanlines, up to the maximum
    // size of the buffer (currently, 320 lines)
    uInt32 offset = 228 * myScanlineCountForLastFrame;
    if(offset > myStopDisplayOffset && offset < 228 * 320)
      myStopDisplayOffset = offset;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool TIA::scanlinePos(uInt16& x, uInt16& y) const
{
  if(myPartialFrameFlag)
  {
    // We only care about the scanline position when it's in the viewable area
    if(myFramePointerClocks >= myFramePointerOffset)
    {
      x = (myFramePointerClocks - myFramePointerOffset) % 160;
      y = (myFramePointerClocks - myFramePointerOffset) / 160;
      return true;
    }
    else
    {
      x = 0;
      y = 0;
      return false;
    }
  }
  else
  {
    x = width();
    y = height();
    return false;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TIA::enableBits(bool mode)
{
  toggleBit(P0Bit, mode ? 1 : 0);
  toggleBit(P1Bit, mode ? 1 : 0);
  toggleBit(M0Bit, mode ? 1 : 0);
  toggleBit(M1Bit, mode ? 1 : 0);
  toggleBit(BLBit, mode ? 1 : 0);
  toggleBit(PFBit, mode ? 1 : 0);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool TIA::toggleBit(TIABit b, uInt8 mode)
{
  // If mode is 0 or 1, use it as a boolean (off or on)
  // Otherwise, flip the state
  bool on = (mode == 0 || mode == 1) ? bool(mode) : !(myDisabledObjects & b);
  if(on)  myDisabledObjects |= b;
  else    myDisabledObjects &= ~b;

  return on;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool TIA::toggleBits()
{
  myBitsEnabled = !myBitsEnabled;
  enableBits(myBitsEnabled);
  return myBitsEnabled;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TIA::enableCollisions(bool mode)
{
  toggleCollision(P0Bit, mode ? 1 : 0);
  toggleCollision(P1Bit, mode ? 1 : 0);
  toggleCollision(M0Bit, mode ? 1 : 0);
  toggleCollision(M1Bit, mode ? 1 : 0);
  toggleCollision(BLBit, mode ? 1 : 0);
  toggleCollision(PFBit, mode ? 1 : 0);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool TIA::toggleCollision(TIABit b, uInt8 mode)
{
  uInt16 enabled = myCollisionEnabledMask >> 16;

  // If mode is 0 or 1, use it as a boolean (off or on)
  // Otherwise, flip the state
  bool on = (mode == 0 || mode == 1) ? bool(mode) : !(enabled & b);
  if(on)  enabled |= b;
  else    enabled &= ~b;

  // Assume all collisions are on, then selectively turn the desired ones off
  uInt16 mask = 0xffff;
  if(!(enabled & P0Bit))
    mask &= ~(Cx_M0P0 | Cx_M1P0 | Cx_P0PF | Cx_P0BL | Cx_P0P1);
  if(!(enabled & P1Bit))
    mask &= ~(Cx_M0P1 | Cx_M1P1 | Cx_P1PF | Cx_P1BL | Cx_P0P1);
  if(!(enabled & M0Bit))
    mask &= ~(Cx_M0P0 | Cx_M0P1 | Cx_M0PF | Cx_M0BL | Cx_M0M1);
  if(!(enabled & M1Bit))
    mask &= ~(Cx_M1P0 | Cx_M1P1 | Cx_M1PF | Cx_M1BL | Cx_M0M1);
  if(!(enabled & BLBit))
    mask &= ~(Cx_P0BL | Cx_P1BL | Cx_M0BL | Cx_M1BL | Cx_BLPF);
  if(!(enabled & PFBit))
    mask &= ~(Cx_P0PF | Cx_P1PF | Cx_M0PF | Cx_M1PF | Cx_BLPF);

  // Now combine the masks
  myCollisionEnabledMask = (enabled << 16) | mask;

  return on;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool TIA::toggleCollisions()
{
  myCollisionsEnabled = !myCollisionsEnabled;
  enableCollisions(myCollisionsEnabled);
  return myCollisionsEnabled;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool TIA::toggleHMOVEBlank()
{
  myAllowHMOVEBlanks = myAllowHMOVEBlanks ? false : true;
  return myAllowHMOVEBlanks;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool TIA::toggleFixedColors(uInt8 mode)
{
  // If mode is 0 or 1, use it as a boolean (off or on)
  // Otherwise, flip the state
  bool on = (mode == 0 || mode == 1) ? bool(mode) :
            (myColorPtr == myColor ? true : false);
  if(on)  myColorPtr = myFixedColor;
  else    myColorPtr = myColor;

  // Set PriorityEncoder
  // This needs to be done here, since toggling debug colours also changes
  // how colours are interpreted in PF 'score' mode
  for(uInt16 x = 0; x < 2; ++x)
  {
    for(uInt16 enabled = 0; enabled < 256; ++enabled)
    {
      if(enabled & PriorityBit)
      {
        // Priority from highest to lowest:
        //   PF/BL => P0/M0 => P1/M1 => BK
        uInt8 color = BKColor;

        if((enabled & M1Bit) != 0)
          color = M1Color;
        if((enabled & P1Bit) != 0)
          color = P1Color;
        if((enabled & M0Bit) != 0)
          color = M0Color;
        if((enabled & P0Bit) != 0)
          color = P0Color;
        if((enabled & BLBit) != 0)
          color = BLColor;
        if((enabled & PFBit) != 0)
          color = PFColor;  // NOTE: Playfield has priority so ScoreBit isn't used

        myPriorityEncoder[x][enabled] = color;
      }
      else
      {
        // Priority from highest to lowest:
        //   P0/M0 => P1/M1 => PF/BL => BK
        uInt8 color = BKColor;

        if((enabled & BLBit) != 0)
          color = BLColor;
        if((enabled & PFBit) != 0)
          color = (!on && (enabled & ScoreBit)) ? ((x == 0) ? P0Color : P1Color) : PFColor;
        if((enabled & M1Bit) != 0)
          color = M1Color;
        if((enabled & P1Bit) != 0)
          color = P1Color;
        if((enabled & M0Bit) != 0)
          color = M0Color;
        if((enabled & P0Bit) != 0)
          color = P0Color;

        myPriorityEncoder[x][enabled] = color;
      }
    }
  }

  return on;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool TIA::driveUnusedPinsRandom(uInt8 mode)
{
  // If mode is 0 or 1, use it as a boolean (off or on)
  // Otherwise, return the state
  if(mode == 0 || mode == 1)
  {
    myTIAPinsDriven = bool(mode);
    mySettings.setValue("tiadriven", myTIAPinsDriven);
  }
  return myTIAPinsDriven;
}

#ifdef DEBUGGER_SUPPORT
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TIA::updateScanline()
{
  // Start a new frame if the old one was finished
  if(!myPartialFrameFlag)
    startFrame();

  // true either way:
  myPartialFrameFlag = true;

  int totalClocks = (mySystem->cycles() * 3) - myClockWhenFrameStarted;
  int endClock = ((totalClocks + 228) / 228) * 228;

  int clock;
  do {
    mySystem->m6502().execute(1);
    clock = mySystem->cycles() * 3;
    updateFrame(clock);
  } while(clock < endClock);

  // if we finished the frame, get ready for the next one
  if(!myPartialFrameFlag)
    endFrame();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TIA::updateScanlineByStep()
{
  // Start a new frame if the old one was finished
  if(!myPartialFrameFlag)
    startFrame();

  // true either way:
  myPartialFrameFlag = true;

  // Update frame by one CPU instruction/color clock
  mySystem->m6502().execute(1);
  updateFrame(mySystem->cycles() * 3);

  // if we finished the frame, get ready for the next one
  if(!myPartialFrameFlag)
    endFrame();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TIA::updateScanlineByTrace(int target)
{
  // Start a new frame if the old one was finished
  if(!myPartialFrameFlag)
    startFrame();

  // true either way:
  myPartialFrameFlag = true;

  while(mySystem->m6502().getPC() != target)
  {
    mySystem->m6502().execute(1);
    updateFrame(mySystem->cycles() * 3);
  }

  // if we finished the frame, get ready for the next one
  if(!myPartialFrameFlag)
    endFrame();
}
#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TIA::updateFrame(Int32 clock)
{
  // See if we've already updated this portion of the screen
  if((clock < myClockStartDisplay) ||
     (myClockAtLastUpdate >= myClockStopDisplay) ||
     (myClockAtLastUpdate >= clock))
    return;

  // Truncate the number of cycles to update to the stop display point
  if(clock > myClockStopDisplay)
    clock = myClockStopDisplay;

  // Determine how many scanlines to process
  // It's easier to think about this in scanlines rather than color clocks
  uInt32 startLine = (myClockAtLastUpdate - myClockWhenFrameStarted) / 228;
  uInt32 endLine = (clock - myClockWhenFrameStarted) / 228;

  // Update frame one scanline at a time
  for(uInt32 line = startLine; line <= endLine; ++line)
  {
    // Only check for inter-line changes after the current scanline
    // The ideas for much of the following code was inspired by MESS
    // (used with permission from Wilbert Pol)
    if(line != startLine)
    {
      // We're no longer concerned with previously issued HMOVE's
      myPreviousHMOVEPos = 0x7FFFFFFF;
      bool posChanged = false;

      // Apply pending motion clocks from a HMOVE initiated during the scanline
      if(myCurrentHMOVEPos != 0x7FFFFFFF)
      {
        if(myCurrentHMOVEPos >= 97 && myCurrentHMOVEPos < 157)
        {
          myPOSP0 -= myMotionClockP0;  if(myPOSP0 < 0) myPOSP0 += 160;
          myPOSP1 -= myMotionClockP1;  if(myPOSP1 < 0) myPOSP1 += 160;
          myPOSM0 -= myMotionClockM0;  if(myPOSM0 < 0) myPOSM0 += 160;
          myPOSM1 -= myMotionClockM1;  if(myPOSM1 < 0) myPOSM1 += 160;
          myPOSBL -= myMotionClockBL;  if(myPOSBL < 0) myPOSBL += 160;

          myPreviousHMOVEPos = myCurrentHMOVEPos;
        }
        // Indicate that the HMOVE has been completed
        myCurrentHMOVEPos = 0x7FFFFFFF;
        posChanged = true;
      }

      // Apply extra clocks for 'more motion required/mmr'
      if(myHMP0mmr) { myPOSP0 -= 17;  if(myPOSP0 < 0) myPOSP0 += 160;  posChanged = true; }
      if(myHMP1mmr) { myPOSP1 -= 17;  if(myPOSP1 < 0) myPOSP1 += 160;  posChanged = true; }
      if(myHMM0mmr) { myPOSM0 -= 17;  if(myPOSM0 < 0) myPOSM0 += 160;  posChanged = true; }
      if(myHMM1mmr) { myPOSM1 -= 17;  if(myPOSM1 < 0) myPOSM1 += 160;  posChanged = true; }
      if(myHMBLmmr) { myPOSBL -= 17;  if(myPOSBL < 0) myPOSBL += 160;  posChanged = true; }

      // Scanline change, so reset PF mask based on current CTRLPF reflection state 
      myPFMask = TIATables::PFMask[myCTRLPF & 0x01];

      // TODO - handle changes to player timing
      if(posChanged)
      {
      }
    }

    // Compute the number of clocks we're going to update
    Int32 clocksToUpdate = 0;

    // Remember how many clocks we are from the left side of the screen
    Int32 clocksFromStartOfScanLine = 228 - myClocksToEndOfScanLine;

    // See if we're updating more than the current scanline
    if(clock > (myClockAtLastUpdate + myClocksToEndOfScanLine))
    {
      // Yes, we have more than one scanline to update so finish current one
      clocksToUpdate = myClocksToEndOfScanLine;
      myClocksToEndOfScanLine = 228;
      myClockAtLastUpdate += clocksToUpdate;
    }
    else
    {
      // No, so do as much of the current scanline as possible
      clocksToUpdate = clock - myClockAtLastUpdate;
      myClocksToEndOfScanLine -= clocksToUpdate;
      myClockAtLastUpdate = clock;
    }

    Int32 startOfScanLine = HBLANK;

    // Skip over as many horizontal blank clocks as we can
    if(clocksFromStartOfScanLine < startOfScanLine)
    {
      uInt32 tmp;

      if((startOfScanLine - clocksFromStartOfScanLine) < clocksToUpdate)
        tmp = startOfScanLine - clocksFromStartOfScanLine;
      else
        tmp = clocksToUpdate;

      clocksFromStartOfScanLine += tmp;
      clocksToUpdate -= tmp;
    }

    // Remember frame pointer in case HMOVE blanks need to be handled
    uInt8* oldFramePointer = myFramePointer;

    // Update as much of the scanline as we can
    if(clocksToUpdate != 0)
    {
      // Calculate the ending frame pointer value
      uInt8* ending = myFramePointer + clocksToUpdate;
      myFramePointerClocks += clocksToUpdate;

      // See if we're in the vertical blank region
      if(myVBLANK & 0x02)
      {
        memset(myFramePointer, 0, clocksToUpdate);
      }
      // Handle all other possible combinations
      else
      {
        // Update masks
        myP0Mask = &TIATables::PxMask[mySuppressP0]
            [myNUSIZ0 & 0x07][160 - (myPOSP0 & 0xFF)];
        myP1Mask = &TIATables::PxMask[mySuppressP1]
            [myNUSIZ1 & 0x07][160 - (myPOSP1 & 0xFF)];
        myBLMask = &TIATables::BLMask[(myCTRLPF & 0x30) >> 4]
            [160 - (myPOSBL & 0xFF)];

        // TODO - 08-27-2009: Simulate the weird effects of Cosmic Ark and
        // Stay Frosty.  The movement itself is well understood, but there
        // also seems to be some widening and blanking occurring as well.
        // This doesn't properly emulate the effect at a low level; it only
        // simulates the behaviour as visually seen in the aforementioned
        // ROMs.  Other ROMs may break this simulation; more testing is
        // required to figure out what's really going on here.
        if(myHMM0mmr)
        {
          switch(myPOSM0 % 4)
          {
            case 3:
              // Stretch this missle so it's 2 pixels wide and shifted one
              // pixel to the left
              myM0Mask = &TIATables::MxMask[myNUSIZ0 & 0x07]
                  [((myNUSIZ0 & 0x30) >> 4)|1][160 - ((myPOSM0-1) & 0xFF)];
              break;
            case 2:
              // Missle is disabled on this line
              myM0Mask = &TIATables::DisabledMask[0];
              break;
            default:
              myM0Mask = &TIATables::MxMask[myNUSIZ0 & 0x07]
                  [(myNUSIZ0 & 0x30) >> 4][160 - (myPOSM0 & 0xFF)];
              break;
          }
        }
        else
          myM0Mask = &TIATables::MxMask[myNUSIZ0 & 0x07]
              [(myNUSIZ0 & 0x30) >> 4][160 - (myPOSM0 & 0xFF)];
        if(myHMM1mmr)
        {
          switch(myPOSM1 % 4)
          {
            case 3:
              // Stretch this missle so it's 2 pixels wide and shifted one
              // pixel to the left
              myM1Mask = &TIATables::MxMask[myNUSIZ1 & 0x07]
                  [((myNUSIZ1 & 0x30) >> 4)|1][160 - ((myPOSM1-1) & 0xFF)];
              break;
            case 2:
              // Missle is disabled on this line
              myM1Mask = &TIATables::DisabledMask[0];
              break;
            default:
              myM1Mask = &TIATables::MxMask[myNUSIZ1 & 0x07]
                  [(myNUSIZ1 & 0x30) >> 4][160 - (myPOSM1 & 0xFF)];
              break;
          }
        }
        else
          myM1Mask = &TIATables::MxMask[myNUSIZ1 & 0x07]
              [(myNUSIZ1 & 0x30) >> 4][160 - (myPOSM1 & 0xFF)];

        uInt8 enabledObjects = myEnabledObjects & myDisabledObjects;
        uInt32 hpos = clocksFromStartOfScanLine - HBLANK;
        for(; myFramePointer < ending; ++myFramePointer, ++hpos)
        {
          uInt8 enabled = ((enabledObjects & PFBit) &&
                           (myPF & myPFMask[hpos])) ? PFBit : 0;

          if((enabledObjects & BLBit) && myBLMask[hpos])
            enabled |= BLBit;

          if((enabledObjects & P1Bit) && (myCurrentGRP1 & myP1Mask[hpos]))
            enabled |= P1Bit;

          if((enabledObjects & M1Bit) && myM1Mask[hpos])
            enabled |= M1Bit;

          if((enabledObjects & P0Bit) && (myCurrentGRP0 & myP0Mask[hpos]))
            enabled |= P0Bit;

          if((enabledObjects & M0Bit) && myM0Mask[hpos])
            enabled |= M0Bit;

          myCollision |= TIATables::CollisionMask[enabled];
          *myFramePointer = myColorPtr[myPriorityEncoder[hpos < 80 ? 0 : 1]
              [enabled | myPlayfieldPriorityAndScore]];
        }
      }
      myFramePointer = ending;
    }

    // Handle HMOVE blanks if they are enabled
    if(myHMOVEBlankEnabled && (startOfScanLine < HBLANK + 8) &&
        (clocksFromStartOfScanLine < (HBLANK + 8)))
    {
      Int32 blanks = (HBLANK + 8) - clocksFromStartOfScanLine;
      memset(oldFramePointer, myColorPtr[HBLANKColor], blanks);

      if((clocksToUpdate + clocksFromStartOfScanLine) >= (HBLANK + 8))
        myHMOVEBlankEnabled = false;
    }

// TODO - this needs to be updated to actually do as the comment suggests
#if 1
    // See if we're at the end of a scanline
    if(myClocksToEndOfScanLine == 228)
    {
      // TODO - 01-21-99: These should be reset right after the first copy
      // of the player has passed.  However, for now we'll just reset at the
      // end of the scanline since the other way would be too slow.
      mySuppressP0 = mySuppressP1 = 0;
    }
#endif
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
inline void TIA::waitHorizontalSync()
{
  uInt32 cyclesToEndOfLine = 76 - ((mySystem->cycles() - 
      (myClockWhenFrameStarted / 3)) % 76);

  if(cyclesToEndOfLine < 76)
    mySystem->incrementCycles(cyclesToEndOfLine);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
inline void TIA::waitHorizontalRSync()
{
  // 02-23-2013: RSYNC has now been updated to work correctly with
  // Extra-Terrestrials. Fatal Run also uses RSYNC (in its VSYNC routine),
  // and the NTSC prototype now displays 262 scanlines instead of 261.
  // What is not emulated correctly is the "real time" effects. For example
  // the VSYNC signal may not be 3 complete scanlines, although Stella will
  // now count it as such.
  //
  // There are two extreme cases to demonstrate this "real time" variance
  // effect over a proper three line VSYNC. 3*76 = 228 cycles properly needed:
  //
  // ======  SHORT TIME CASE  ======
  // 
  //     lda    #3      ;2  @67
  //     sta    VSYNC   ;3  @70      vsync starts
  //     sta    RSYNC   ;3  @73  +3
  //     sta    WSYNC   ;3  @76  +6
  // ------------------------------
  //     sta    WSYNC   ;3  @76  +82
  // ------------------------------
  //     lda    #0      ;2  @2   +84
  //     sta    VSYNC                vsync ends
  //
  // ======  LONG TIME CASE  ======
  //
  //    lda    #3      ;2  @70
  //    sta    VSYNC   ;3  @73      vsync starts
  //    sta    RSYNC   ;3  @74  +3
  //    sta    WSYNC   ;3  @..  +81  2 cycles are added to previous line, and then
  //                                 WSYNC halts the new line delaying 78 cycles total!
  //------------------------------
  //    sta    WSYNC   ;3  @76  +157
  //------------------------------
  //    lda    #0      ;2  @2   +159
  //    sta    VSYNC                vsync ends

  // The significance of the 'magic numbers' below is as follows (thanks to
  // Eckhard Stolberg and Omegamatrix for explanation and implementation)
  //
  // Objects always get positioned three pixels further to the right after a
  // WSYNC than they do after a RSYNC, but this is to be expected.  Triggering
  // WSYNC will halt the CPU until the horizontal sync counter wraps around to zero.
  // Triggering RSYNC will reset the horizontal sync counter to zero immediately.
  // But the warp-around will actually happen after one more cycle of this counter.
  // Since the horizontal sync counter counts once every 4 pixels, one more CPU
  // cycle occurs before the counter warps around to zero. Therefore the positioning
  // code will hit RESPx one cycle sooner after a RSYNC than after a WSYNC.

  uInt32 cyclesToEndOfLine = 76 - ((mySystem->cycles() - 
      (myClockWhenFrameStarted / 3)) % 76);

  mySystem->incrementCycles(cyclesToEndOfLine-1);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TIA::clearBuffers()
{
  memset(myCurrentFrameBuffer, 0, 160 * 320);
  memset(myPreviousFrameBuffer, 0, 160 * 320);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
inline uInt8 TIA::dumpedInputPort(int resistance)
{
  if(resistance == Controller::minimumResistance)
  {
    return 0x80;
  }
  else if((resistance == Controller::maximumResistance) || myDumpEnabled)
  {
    return 0x00;
  }
  else
  {
    // Constant here is derived from '1.6 * 0.01e-6 * 228 / 3'
    uInt32 needed = (uInt32)
      (1.216e-6 * resistance * myScanlineCountForLastFrame * myFramerate);
    if((mySystem->cycles() - myDumpDisabledCycle) > needed)
      return 0x80;
    else
      return 0x00;
  }
  return 0x00;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt8 TIA::peek(uInt16 addr)
{
  // Update frame to current color clock before we look at anything!
  updateFrame(mySystem->cycles() * 3);

  // If pins are undriven, we start with the last databus value
  // Otherwise, there is some randomness injected into the mix
  // In either case, we start out with D7 and D6 disabled (the only
  // valid bits in a TIA read), and selectively enable them
  uInt8 value = 0x3F & (!myTIAPinsDriven ? mySystem->getDataBusState() :
                        mySystem->getDataBusState(0xFF));
  uInt16 collision = myCollision & (uInt16)myCollisionEnabledMask;

  switch(addr & 0x000f)
  {
    case CXM0P:
      value |= ((collision & Cx_M0P1) ? 0x80 : 0x00) |
               ((collision & Cx_M0P0) ? 0x40 : 0x00);
      break;

    case CXM1P:
      value |= ((collision & Cx_M1P0) ? 0x80 : 0x00) |
               ((collision & Cx_M1P1) ? 0x40 : 0x00);
      break;

    case CXP0FB:
      value |= ((collision & Cx_P0PF) ? 0x80 : 0x00) |
               ((collision & Cx_P0BL) ? 0x40 : 0x00);
      break;

    case CXP1FB:
      value |= ((collision & Cx_P1PF) ? 0x80 : 0x00) |
               ((collision & Cx_P1BL) ? 0x40 : 0x00);
      break;

    case CXM0FB:
      value |= ((collision & Cx_M0PF) ? 0x80 : 0x00) |
               ((collision & Cx_M0BL) ? 0x40 : 0x00);
      break;

    case CXM1FB:
      value |= ((collision & Cx_M1PF) ? 0x80 : 0x00) |
               ((collision & Cx_M1BL) ? 0x40 : 0x00);
      break;

    case CXBLPF:
      value = (value & 0x7F) | ((collision & Cx_BLPF) ? 0x80 : 0x00);
      break;

    case CXPPMM:
      value |= ((collision & Cx_P0P1) ? 0x80 : 0x00) |
               ((collision & Cx_M0M1) ? 0x40 : 0x00);
      break;

    case INPT0:
      value = (value & 0x7F) |
        dumpedInputPort(myConsole.controller(Controller::Left).read(Controller::Nine));
      break;

    case INPT1:
      value = (value & 0x7F) |
        dumpedInputPort(myConsole.controller(Controller::Left).read(Controller::Five));
      break;

    case INPT2:
      value = (value & 0x7F) |
        dumpedInputPort(myConsole.controller(Controller::Right).read(Controller::Nine));
      break;

    case INPT3:
      value = (value & 0x7F) |
        dumpedInputPort(myConsole.controller(Controller::Right).read(Controller::Five));
      break;

    case INPT4:
    {
      uInt8 button = (myConsole.controller(Controller::Left).read(Controller::Six) ? 0x80 : 0x00);
      myINPT4 = (myVBLANK & 0x40) ? (myINPT4 & button) : button;

      value = (value & 0x7F) | myINPT4;
      break;
    }

    case INPT5:
    {
      uInt8 button = (myConsole.controller(Controller::Right).read(Controller::Six) ? 0x80 : 0x00);
      myINPT5 = (myVBLANK & 0x40) ? (myINPT5 & button) : button;

      value = (value & 0x7F) | myINPT5;
      break;
    }

    default:
      // This shouldn't happen, but if it does, we essentially just
      // return the last databus value with bits D6 and D7 zeroed out
      break;
  }
  return value;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool TIA::poke(uInt16 addr, uInt8 value)
{
  addr = addr & 0x003f;

  Int32 clock = mySystem->cycles() * 3;
  Int16 delay = TIATables::PokeDelay[addr];

  // See if this is a poke to a PF register
  if(delay == -1)
  {
    static uInt32 d[4] = {4, 5, 2, 3};
    Int32 x = ((clock - myClockWhenFrameStarted) % 228);
    delay = d[(x / 3) & 3];
  }

  // Update frame to current CPU cycle before we make any changes!
  updateFrame(clock + delay);

  // If a VSYNC hasn't been generated in time go ahead and end the frame
  if(((clock - myClockWhenFrameStarted) / 228) >= (Int32)myMaximumNumberOfScanlines)
  {
    mySystem->m6502().stop();
    myPartialFrameFlag = false;
  }

  switch(addr)
  {
    case VSYNC:    // Vertical sync set-clear
    {
      myVSYNC = value;

      if(myVSYNC & 0x02)
      {
        // Indicate when VSYNC should be finished.  This should really 
        // be 3 * 228 according to Atari's documentation, however, some 
        // games don't supply the full 3 scanlines of VSYNC.
        myVSYNCFinishClock = clock + 228;
      }
      else if(!(myVSYNC & 0x02) && (clock >= myVSYNCFinishClock))
      {
        // We're no longer interested in myVSYNCFinishClock
        myVSYNCFinishClock = 0x7FFFFFFF;

        // Since we're finished with the frame tell the processor to halt
        mySystem->m6502().stop();
        myPartialFrameFlag = false;
      }
      break;
    }

    case VBLANK:  // Vertical blank set-clear
    {
      // Is the dump to ground path being set for I0, I1, I2, and I3?
      if(!(myVBLANK & 0x80) && (value & 0x80))
      {
        myDumpEnabled = true;
      }
      // Is the dump to ground path being removed from I0, I1, I2, and I3?
      else if((myVBLANK & 0x80) && !(value & 0x80))
      {
        myDumpEnabled = false;
        myDumpDisabledCycle = mySystem->cycles();
      }

      // Are the latches for I4 and I5 being reset?
      if (!(myVBLANK & 0x40))
        myINPT4 = myINPT5 = 0x80;

      // Check for the first scanline at which VBLANK is disabled.
      // Usually, this will be the first scanline to start drawing.
      if(myStartScanline == 0 && !(value & 0x10))
        myStartScanline = scanlines();

      myVBLANK = value;
      break;
    }

    case WSYNC:   // Wait for leading edge of HBLANK
    {
      // It appears that the 6507 only halts during a read cycle so
      // we test here for follow-on writes which should be ignored as
      // far as halting the processor is concerned.
      //
      // TODO - 08-30-2006: This halting isn't correct since it's 
      // still halting on the original write.  The 6507 emulation
      // should be expanded to include a READY line.
      if(mySystem->m6502().lastAccessWasRead())
      {
        // Tell the cpu to waste the necessary amount of time
        waitHorizontalSync();
      }
      break;
    }

    case RSYNC:   // Reset horizontal sync counter
    {
      waitHorizontalRSync();
      break;
    }

    case NUSIZ0:  // Number-size of player-missle 0
    {
      // TODO - 08-11-2009: determine correct delay instead of always
      //                    using '8' in TIATables::PokeDelay
      updateFrame(clock + 8);

      myNUSIZ0 = value;
      mySuppressP0 = 0;
      break;
    }

    case NUSIZ1:  // Number-size of player-missle 1
    {
      // TODO - 08-11-2009: determine correct delay instead of always
      //                    using '8' in TIATables::PokeDelay
      updateFrame(clock + 8);

      myNUSIZ1 = value;
      mySuppressP1 = 0;
      break;
    }

    case COLUP0:  // Color-Luminance Player 0
    {
      uInt8 color = value & 0xfe;
      if(myColorLossEnabled && (myScanlineCountForLastFrame & 0x01))
        color |= 0x01;

      myColor[P0Color] = myColor[M0Color] = color;
      break;
    }

    case COLUP1:  // Color-Luminance Player 1
    {
      uInt8 color = value & 0xfe;
      if(myColorLossEnabled && (myScanlineCountForLastFrame & 0x01))
        color |= 0x01;

      myColor[P1Color] = myColor[M1Color] = color;
      break;
    }

    case COLUPF:  // Color-Luminance Playfield
    {
      uInt8 color = value & 0xfe;
      if(myColorLossEnabled && (myScanlineCountForLastFrame & 0x01))
        color |= 0x01;

      myColor[PFColor] = myColor[BLColor] = color;
      break;
    }

    case COLUBK:  // Color-Luminance Background
    {
      uInt8 color = value & 0xfe;
      if(myColorLossEnabled && (myScanlineCountForLastFrame & 0x01))
        color |= 0x01;

      myColor[BKColor] = color;
      break;
    }

    case CTRLPF:  // Control Playfield, Ball size, Collisions
    {
      myCTRLPF = value;

      // The playfield priority and score bits from the control register
      // are accessed when the frame is being drawn.  We precompute the 
      // necessary value here so we can save time while drawing.
      myPlayfieldPriorityAndScore = ((myCTRLPF & 0x06) << 5);

      // Update the playfield mask based on reflection state if 
      // we're still on the left hand side of the playfield
      if(((clock - myClockWhenFrameStarted) % 228) < (68 + 79))
        myPFMask = TIATables::PFMask[myCTRLPF & 0x01];

      break;
    }

    case REFP0:   // Reflect Player 0
    {
      // See if the reflection state of the player is being changed
      if(((value & 0x08) && !myREFP0) || (!(value & 0x08) && myREFP0))
      {
        myREFP0 = (value & 0x08);
        myCurrentGRP0 = TIATables::GRPReflect[myCurrentGRP0];
      }
      break;
    }

    case REFP1:   // Reflect Player 1
    {
      // See if the reflection state of the player is being changed
      if(((value & 0x08) && !myREFP1) || (!(value & 0x08) && myREFP1))
      {
        myREFP1 = (value & 0x08);
        myCurrentGRP1 = TIATables::GRPReflect[myCurrentGRP1];
      }
      break;
    }

    case PF0:     // Playfield register byte 0
    {
      myPF = (myPF & 0x000FFFF0) | ((value >> 4) & 0x0F);

      if(myPF == 0)
        myEnabledObjects &= ~PFBit;
      else
        myEnabledObjects |= PFBit;

    #ifdef DEBUGGER_SUPPORT
      uInt16 dataAddr = mySystem->m6502().lastDataAddressForPoke();
      if(dataAddr)
        mySystem->setAccessFlags(dataAddr, CartDebug::PGFX);
    #endif
      break;
    }

    case PF1:     // Playfield register byte 1
    {
      myPF = (myPF & 0x000FF00F) | ((uInt32)value << 4);

      if(myPF == 0)
        myEnabledObjects &= ~PFBit;
      else
        myEnabledObjects |= PFBit;

    #ifdef DEBUGGER_SUPPORT
      uInt16 dataAddr = mySystem->m6502().lastDataAddressForPoke();
      if(dataAddr)
        mySystem->setAccessFlags(dataAddr, CartDebug::PGFX);
    #endif
      break;
    }

    case PF2:     // Playfield register byte 2
    {
      myPF = (myPF & 0x00000FFF) | ((uInt32)value << 12);

      if(myPF == 0)
        myEnabledObjects &= ~PFBit;
      else
        myEnabledObjects |= PFBit;

    #ifdef DEBUGGER_SUPPORT
      uInt16 dataAddr = mySystem->m6502().lastDataAddressForPoke();
      if(dataAddr)
        mySystem->setAccessFlags(dataAddr, CartDebug::PGFX);
    #endif
      break;
    }

    case RESP0:   // Reset Player 0
    {
      Int32 hpos = (clock - myClockWhenFrameStarted) % 228 - HBLANK;
      Int16 newx;

      // Check if HMOVE is currently active
      if(myCurrentHMOVEPos != 0x7FFFFFFF)
      {
        newx = hpos < 7 ? 3 : ((hpos + 5) % 160);
        // If HMOVE is active, adjust for any remaining horizontal move clocks
        applyActiveHMOVEMotion(hpos, newx, myMotionClockP0);
      }
      else
      {
        newx = hpos < -2 ? 3 : ((hpos + 5) % 160);
        applyPreviousHMOVEMotion(hpos, newx, myHMP0);
      }
      if(myPOSP0 != newx)
      {
        // TODO - update player timing

        // Find out under what condition the player is being reset
        delay = TIATables::PxPosResetWhen[myNUSIZ0 & 7][myPOSP0][newx];

        switch(delay)
        {
          // Player is being reset during the display of one of its copies
          case 1:
            // TODO - 08-20-2009: determine whether we really need to update
            // the frame here, and also come up with a way to eliminate the
            // 200KB PxPosResetWhen table.
            updateFrame(clock + 11);
            mySuppressP0 = 1;
            break;

          // Player is being reset in neither the delay nor display section
          case 0:
            mySuppressP0 = 1;
            break;

          // Player is being reset during the delay section of one of its copies
          case -1:
            mySuppressP0 = 0;
            break;
        }
        myPOSP0 = newx;
      }
      break;
    }

    case RESP1:   // Reset Player 1
    {
      Int32 hpos = (clock - myClockWhenFrameStarted) % 228 - HBLANK;
      Int16 newx;

      // Check if HMOVE is currently active
      if(myCurrentHMOVEPos != 0x7FFFFFFF)
      {
        newx = hpos < 7 ? 3 : ((hpos + 5) % 160);
        // If HMOVE is active, adjust for any remaining horizontal move clocks
        applyActiveHMOVEMotion(hpos, newx, myMotionClockP1);
      }
      else
      {
        newx = hpos < -2 ? 3 : ((hpos + 5) % 160);
        applyPreviousHMOVEMotion(hpos, newx, myHMP1);
      }
      if(myPOSP1 != newx)
      {
        // TODO - update player timing

        // Find out under what condition the player is being reset
        delay = TIATables::PxPosResetWhen[myNUSIZ1 & 7][myPOSP1][newx];

        switch(delay)
        {
          // Player is being reset during the display of one of its copies
          case 1:
            // TODO - 08-20-2009: determine whether we really need to update
            // the frame here, and also come up with a way to eliminate the
            // 200KB PxPosResetWhen table.
            updateFrame(clock + 11);
            mySuppressP1 = 1;
            break;

          // Player is being reset in neither the delay nor display section
          case 0:
            mySuppressP1 = 1;
            break;

          // Player is being reset during the delay section of one of its copies
          case -1:
            mySuppressP1 = 0;
            break;
        }
        myPOSP1 = newx;
      }
      break;
    }

    case RESM0:   // Reset Missle 0
    {
      Int32 hpos = (clock - myClockWhenFrameStarted) % 228 - HBLANK;
      Int16 newx;

      // Check if HMOVE is currently active
      if(myCurrentHMOVEPos != 0x7FFFFFFF)
      {
        newx = hpos < 7 ? 2 : ((hpos + 4) % 160);
        // If HMOVE is active, adjust for any remaining horizontal move clocks
        applyActiveHMOVEMotion(hpos, newx, myMotionClockM0);
      }
      else
      {
        newx = hpos < -1 ? 2 : ((hpos + 4) % 160);
        applyPreviousHMOVEMotion(hpos, newx, myHMM0);
      }
      if(newx != myPOSM0)
      {
        myPOSM0 = newx;
      }
      break;
    }

    case RESM1:   // Reset Missle 1
    {
      Int32 hpos = (clock - myClockWhenFrameStarted) % 228 - HBLANK;
      Int16 newx;

      // Check if HMOVE is currently active
      if(myCurrentHMOVEPos != 0x7FFFFFFF)
      {
        newx = hpos < 7 ? 2 : ((hpos + 4) % 160);
        // If HMOVE is active, adjust for any remaining horizontal move clocks
        applyActiveHMOVEMotion(hpos, newx, myMotionClockM1);
      }
      else
      {
        newx = hpos < -1 ? 2 : ((hpos + 4) % 160);
        applyPreviousHMOVEMotion(hpos, newx, myHMM1);
      }
      if(newx != myPOSM1)
      {
        myPOSM1 = newx;
      }
      break;
    }

    case RESBL:   // Reset Ball
    {
      Int32 hpos = (clock - myClockWhenFrameStarted) % 228 - HBLANK;

      // Check if HMOVE is currently active
      if(myCurrentHMOVEPos != 0x7FFFFFFF)
      {
        myPOSBL = hpos < 7 ? 2 : ((hpos + 4) % 160);
        // If HMOVE is active, adjust for any remaining horizontal move clocks
        applyActiveHMOVEMotion(hpos, myPOSBL, myMotionClockBL);
      }
      else
      {
        myPOSBL = hpos < 0 ? 2 : ((hpos + 4) % 160);
        applyPreviousHMOVEMotion(hpos, myPOSBL, myHMBL);
      }
      break;
    }

    case AUDC0:   // Audio control 0
    {
      myAUDC0 = value & 0x0f;
      mySound.set(addr, value, mySystem->cycles());
      break;
    }
  
    case AUDC1:   // Audio control 1
    {
      myAUDC1 = value & 0x0f;
      mySound.set(addr, value, mySystem->cycles());
      break;
    }
  
    case AUDF0:   // Audio frequency 0
    {
      myAUDF0 = value & 0x1f;
      mySound.set(addr, value, mySystem->cycles());
      break;
    }
  
    case AUDF1:   // Audio frequency 1
    {
      myAUDF1 = value & 0x1f;
      mySound.set(addr, value, mySystem->cycles());
      break;
    }
  
    case AUDV0:   // Audio volume 0
    {
      myAUDV0 = value & 0x0f;
      mySound.set(addr, value, mySystem->cycles());
      break;
    }
  
    case AUDV1:   // Audio volume 1
    {
      myAUDV1 = value & 0x0f;
      mySound.set(addr, value, mySystem->cycles());
      break;
    }

    case GRP0:    // Graphics Player 0
    {
      // Set player 0 graphics
      myGRP0 = value;

      // Copy player 1 graphics into its delayed register
      myDGRP1 = myGRP1;

      // Get the "current" data for GRP0 base on delay register and reflect
      uInt8 grp0 = myVDELP0 ? myDGRP0 : myGRP0;
      myCurrentGRP0 = myREFP0 ? TIATables::GRPReflect[grp0] : grp0; 

      // Get the "current" data for GRP1 base on delay register and reflect
      uInt8 grp1 = myVDELP1 ? myDGRP1 : myGRP1;
      myCurrentGRP1 = myREFP1 ? TIATables::GRPReflect[grp1] : grp1; 

      // Set enabled object bits
      if(myCurrentGRP0 != 0)
        myEnabledObjects |= P0Bit;
      else
        myEnabledObjects &= ~P0Bit;

      if(myCurrentGRP1 != 0)
        myEnabledObjects |= P1Bit;
      else
        myEnabledObjects &= ~P1Bit;

    #ifdef DEBUGGER_SUPPORT
      uInt16 dataAddr = mySystem->m6502().lastDataAddressForPoke();
      if(dataAddr)
        mySystem->setAccessFlags(dataAddr, CartDebug::GFX);
    #endif
      break;
    }

    case GRP1:    // Graphics Player 1
    {
      // Set player 1 graphics
      myGRP1 = value;

      // Copy player 0 graphics into its delayed register
      myDGRP0 = myGRP0;

      // Copy ball graphics into its delayed register
      myDENABL = myENABL;

      // Get the "current" data for GRP0 base on delay register
      uInt8 grp0 = myVDELP0 ? myDGRP0 : myGRP0;
      myCurrentGRP0 = myREFP0 ? TIATables::GRPReflect[grp0] : grp0; 

      // Get the "current" data for GRP1 base on delay register
      uInt8 grp1 = myVDELP1 ? myDGRP1 : myGRP1;
      myCurrentGRP1 = myREFP1 ? TIATables::GRPReflect[grp1] : grp1; 

      // Set enabled object bits
      if(myCurrentGRP0 != 0)
        myEnabledObjects |= P0Bit;
      else
        myEnabledObjects &= ~P0Bit;

      if(myCurrentGRP1 != 0)
        myEnabledObjects |= P1Bit;
      else
        myEnabledObjects &= ~P1Bit;

      if(myVDELBL ? myDENABL : myENABL)
        myEnabledObjects |= BLBit;
      else
        myEnabledObjects &= ~BLBit;

    #ifdef DEBUGGER_SUPPORT
      uInt16 dataAddr = mySystem->m6502().lastDataAddressForPoke();
      if(dataAddr)
        mySystem->setAccessFlags(dataAddr, CartDebug::GFX);
    #endif
      break;
    }

    case ENAM0:   // Enable Missile 0 graphics
    {
      myENAM0 = value & 0x02;

      if(myENAM0 && !myRESMP0)
        myEnabledObjects |= M0Bit;
      else
        myEnabledObjects &= ~M0Bit;
      break;
    }

    case ENAM1:   // Enable Missile 1 graphics
    {
      myENAM1 = value & 0x02;

      if(myENAM1 && !myRESMP1)
        myEnabledObjects |= M1Bit;
      else
        myEnabledObjects &= ~M1Bit;
      break;
    }

    case ENABL:   // Enable Ball graphics
    {
      myENABL = value & 0x02;

      if(myVDELBL ? myDENABL : myENABL)
        myEnabledObjects |= BLBit;
      else
        myEnabledObjects &= ~BLBit;

      break;
    }

    case HMP0:    // Horizontal Motion Player 0
    {
      pokeHMP0(value, clock);
      break;
    }

    case HMP1:    // Horizontal Motion Player 1
    {
      pokeHMP1(value, clock);
      break;
    }

    case HMM0:    // Horizontal Motion Missle 0
    {
      pokeHMM0(value, clock);
      break;
    }

    case HMM1:    // Horizontal Motion Missle 1
    {
      pokeHMM1(value, clock);
      break;
    }

    case HMBL:    // Horizontal Motion Ball
    {
      pokeHMBL(value, clock);
      break;
    }

    case VDELP0:  // Vertical Delay Player 0
    {
      myVDELP0 = value & 0x01;

      uInt8 grp0 = myVDELP0 ? myDGRP0 : myGRP0;
      myCurrentGRP0 = myREFP0 ? TIATables::GRPReflect[grp0] : grp0; 

      if(myCurrentGRP0 != 0)
        myEnabledObjects |= P0Bit;
      else
        myEnabledObjects &= ~P0Bit;
      break;
    }

    case VDELP1:  // Vertical Delay Player 1
    {
      myVDELP1 = value & 0x01;

      uInt8 grp1 = myVDELP1 ? myDGRP1 : myGRP1;
      myCurrentGRP1 = myREFP1 ? TIATables::GRPReflect[grp1] : grp1; 

      if(myCurrentGRP1 != 0)
        myEnabledObjects |= P1Bit;
      else
        myEnabledObjects &= ~P1Bit;
      break;
    }

    case VDELBL:  // Vertical Delay Ball
    {
      myVDELBL = value & 0x01;

      if(myVDELBL ? myDENABL : myENABL)
        myEnabledObjects |= BLBit;
      else
        myEnabledObjects &= ~BLBit;
      break;
    }

    case RESMP0:  // Reset missle 0 to player 0
    {
      if(myRESMP0 && !(value & 0x02))
      {
        uInt16 middle = 4;
        switch(myNUSIZ0 & 0x07)
        {
          // 1-pixel delay is taken care of in TIATables::PxMask
          case 0x05: middle = 8;  break;  // double size
          case 0x07: middle = 16; break;  // quad size
        }
        myPOSM0 = myPOSP0 + middle;
        if(myCurrentHMOVEPos != 0x7FFFFFFF)
        {
          myPOSM0 -= (8 - myMotionClockP0);
          myPOSM0 += (8 - myMotionClockM0);
        }
        CLAMP_POS(myPOSM0);
      }
      myRESMP0 = value & 0x02;

      if(myENAM0 && !myRESMP0)
        myEnabledObjects |= M0Bit;
      else
        myEnabledObjects &= ~M0Bit;

      break;
    }

    case RESMP1:  // Reset missle 1 to player 1
    {
      if(myRESMP1 && !(value & 0x02))
      {
        uInt16 middle = 4;
        switch(myNUSIZ1 & 0x07)
        {
          // 1-pixel delay is taken care of in TIATables::PxMask
          case 0x05: middle = 8;  break;  // double size
          case 0x07: middle = 16; break;  // quad size
        }
        myPOSM1 = myPOSP1 + middle;
        if(myCurrentHMOVEPos != 0x7FFFFFFF)
        {
          myPOSM1 -= (8 - myMotionClockP1);
          myPOSM1 += (8 - myMotionClockM1);
        }
        CLAMP_POS(myPOSM1);
      }
      myRESMP1 = value & 0x02;

      if(myENAM1 && !myRESMP1)
        myEnabledObjects |= M1Bit;
      else
        myEnabledObjects &= ~M1Bit;
      break;
    }

    case HMOVE:   // Apply horizontal motion
    {
      int hpos = (clock - myClockWhenFrameStarted) % 228 - HBLANK;
      myCurrentHMOVEPos = hpos;

      // See if we need to enable the HMOVE blank bug
      myHMOVEBlankEnabled = myAllowHMOVEBlanks ? 
        TIATables::HMOVEBlankEnableCycles[((clock - myClockWhenFrameStarted) % 228) / 3] : false;

      // Do we have to undo some of the already applied cycles from an
      // active graphics latch?
      if(hpos + HBLANK < 17 * 4)
      {
        Int16 cycle_fix = 17 - ((hpos + HBLANK + 7) / 4);
        if(myHMP0mmr)  myPOSP0 = (myPOSP0 + cycle_fix) % 160;
        if(myHMP1mmr)  myPOSP1 = (myPOSP1 + cycle_fix) % 160;
        if(myHMM0mmr)  myPOSM0 = (myPOSM0 + cycle_fix) % 160;
        if(myHMM1mmr)  myPOSM1 = (myPOSM1 + cycle_fix) % 160;
        if(myHMBLmmr)  myPOSBL = (myPOSBL + cycle_fix) % 160;
      }
      myHMP0mmr = myHMP1mmr = myHMM0mmr = myHMM1mmr = myHMBLmmr = false;

      // Can HMOVE activities be ignored?
      if(hpos >= -5 && hpos < 97 )
      {
        myMotionClockP0 = 0;
        myMotionClockP1 = 0;
        myMotionClockM0 = 0;
        myMotionClockM1 = 0;
        myMotionClockBL = 0;
        myHMOVEBlankEnabled = false;
        myCurrentHMOVEPos = 0x7FFFFFFF;
        break;
      }

      myMotionClockP0 = (myHMP0 ^ 0x80) >> 4;
      myMotionClockP1 = (myHMP1 ^ 0x80) >> 4;
      myMotionClockM0 = (myHMM0 ^ 0x80) >> 4;
      myMotionClockM1 = (myHMM1 ^ 0x80) >> 4;
      myMotionClockBL = (myHMBL ^ 0x80) >> 4;

      // Adjust number of graphics motion clocks for active display
      if(hpos >= 97 && hpos < 151)
      {
        Int16 skip_motclks = (160 - myCurrentHMOVEPos - 6) >> 2;
        myMotionClockP0 -= skip_motclks;
        myMotionClockP1 -= skip_motclks;
        myMotionClockM0 -= skip_motclks;
        myMotionClockM1 -= skip_motclks;
        myMotionClockBL -= skip_motclks;
        if(myMotionClockP0 < 0)  myMotionClockP0 = 0;
        if(myMotionClockP1 < 0)  myMotionClockP1 = 0;
        if(myMotionClockM0 < 0)  myMotionClockM0 = 0;
        if(myMotionClockM1 < 0)  myMotionClockM1 = 0;
        if(myMotionClockBL < 0)  myMotionClockBL = 0;
      }

      if(hpos >= -56 && hpos < -5)
      {
        Int16 max_motclks = (7 - (myCurrentHMOVEPos + 5)) >> 2;
        if(myMotionClockP0 > max_motclks)  myMotionClockP0 = max_motclks;
        if(myMotionClockP1 > max_motclks)  myMotionClockP1 = max_motclks;
        if(myMotionClockM0 > max_motclks)  myMotionClockM0 = max_motclks;
        if(myMotionClockM1 > max_motclks)  myMotionClockM1 = max_motclks;
        if(myMotionClockBL > max_motclks)  myMotionClockBL = max_motclks;
      }

      // Apply horizontal motion
      if(hpos < -5 || hpos >= 157)
      {
        myPOSP0 += 8 - myMotionClockP0;
        myPOSP1 += 8 - myMotionClockP1;
        myPOSM0 += 8 - myMotionClockM0;
        myPOSM1 += 8 - myMotionClockM1;
        myPOSBL += 8 - myMotionClockBL;
      }

      // Make sure positions are in range
      CLAMP_POS(myPOSP0);
      CLAMP_POS(myPOSP1);
      CLAMP_POS(myPOSM0);
      CLAMP_POS(myPOSM1);
      CLAMP_POS(myPOSBL);

      // TODO - handle late HMOVE's
      mySuppressP0 = mySuppressP1 = 0;
      break;
    }

    case HMCLR:   // Clear horizontal motion registers
    {
      pokeHMP0(0, clock);
      pokeHMP1(0, clock);
      pokeHMM0(0, clock);
      pokeHMM1(0, clock);
      pokeHMBL(0, clock);
      break;
    }

    case CXCLR:   // Clear collision latches
    {
      myCollision = 0;
      break;
    }

    default:
    {
#ifdef DEBUG_ACCESSES
      cerr << "BAD TIA Poke: " << hex << addr << endl;
#endif
      break;
    }
  }
  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Note that the following methods to change the horizontal motion registers
// are not completely accurate.  We should be taking care of the following
// explanation from A. Towers Hardware Notes:
//
//   Much more interesting is this: if the counter has not yet
//   reached the value in HMxx (or has reached it but not yet
//   commited the comparison) and a value with at least one bit
//   in common with all remaining internal counter states is
//   written (zeros or ones), the stopping condition will never be
//   reached and the object will be moved a full 15 pixels left.
//   In addition to this, the HMOVE will complete without clearing
//   the "more movement required" latch, and so will continue to send
//   an additional clock signal every 4 CLK (during visible and
//   non-visible parts of the scanline) until another HMOVE operation
//   clears the latch. The HMCLR command does not reset these latches.
//
// This condition is what causes the 'starfield effect' in Cosmic Ark,
// and the 'snow' in Stay Frosty.  Ideally, we'd trace the counter and
// do a compare every colour clock, updating the horizontal positions
// when applicable.  We can save time by cheating, and noting that the
// effect only occurs for 'magic numbers' 0x70 and 0x80.
//
// Most of the ideas in these methods come from MESS.
// (used with permission from Wilbert Pol)
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TIA::pokeHMP0(uInt8 value, Int32 clock)
{
  value &= 0xF0;
  if(myHMP0 == value)
    return;

  int hpos  = (clock - myClockWhenFrameStarted) % 228 - HBLANK;

  // Check if HMOVE is currently active
  if(myCurrentHMOVEPos != 0x7FFFFFFF &&
     hpos < BSPF_min(myCurrentHMOVEPos + 6 + myMotionClockP0 * 4, 7))
  {
    Int32 newMotion = (value ^ 0x80) >> 4;
    // Check if new horizontal move can still be applied normally
    if(newMotion > myMotionClockP0 ||
       hpos <= BSPF_min(myCurrentHMOVEPos + 6 + newMotion * 4, 7))
    {
      myPOSP0 -= (newMotion - myMotionClockP0);
      myMotionClockP0 = newMotion;
    }
    else
    {
      myPOSP0 -= (15 - myMotionClockP0);
      myMotionClockP0 = 15;
      if(value != 0x70 && value != 0x80)
        myHMP0mmr = true;
    }
    CLAMP_POS(myPOSP0);
    // TODO - adjust player timing
  }
  myHMP0 = value;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TIA::pokeHMP1(uInt8 value, Int32 clock)
{
  value &= 0xF0;
  if(myHMP1 == value)
    return;

  int hpos  = (clock - myClockWhenFrameStarted) % 228 - HBLANK;

  // Check if HMOVE is currently active
  if(myCurrentHMOVEPos != 0x7FFFFFFF &&
     hpos < BSPF_min(myCurrentHMOVEPos + 6 + myMotionClockP1 * 4, 7))
  {
    Int32 newMotion = (value ^ 0x80) >> 4;
    // Check if new horizontal move can still be applied normally
    if(newMotion > myMotionClockP1 ||
       hpos <= BSPF_min(myCurrentHMOVEPos + 6 + newMotion * 4, 7))
    {
      myPOSP1 -= (newMotion - myMotionClockP1);
      myMotionClockP1 = newMotion;
    }
    else
    {
      myPOSP1 -= (15 - myMotionClockP1);
      myMotionClockP1 = 15;
      if(value != 0x70 && value != 0x80)
        myHMP1mmr = true;
    }
    CLAMP_POS(myPOSP1);
    // TODO - adjust player timing
  }
  myHMP1 = value;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TIA::pokeHMM0(uInt8 value, Int32 clock)
{
  value &= 0xF0;
  if(myHMM0 == value)
    return;

  int hpos  = (clock - myClockWhenFrameStarted) % 228 - HBLANK;

  // Check if HMOVE is currently active
  if(myCurrentHMOVEPos != 0x7FFFFFFF &&
     hpos < BSPF_min(myCurrentHMOVEPos + 6 + myMotionClockM0 * 4, 7))
  {
    Int32 newMotion = (value ^ 0x80) >> 4;
    // Check if new horizontal move can still be applied normally
    if(newMotion > myMotionClockM0 ||
       hpos <= BSPF_min(myCurrentHMOVEPos + 6 + newMotion * 4, 7))
    {
      myPOSM0 -= (newMotion - myMotionClockM0);
      myMotionClockM0 = newMotion;
    }
    else
    {
      myPOSM0 -= (15 - myMotionClockM0);
      myMotionClockM0 = 15;
      if(value != 0x70 && value != 0x80)
        myHMM0mmr = true;
    }
    CLAMP_POS(myPOSM0);
  }
  myHMM0 = value;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TIA::pokeHMM1(uInt8 value, Int32 clock)
{
  value &= 0xF0;
  if(myHMM1 == value)
    return;

  int hpos  = (clock - myClockWhenFrameStarted) % 228 - HBLANK;

  // Check if HMOVE is currently active
  if(myCurrentHMOVEPos != 0x7FFFFFFF &&
     hpos < BSPF_min(myCurrentHMOVEPos + 6 + myMotionClockM1 * 4, 7))
  {
    Int32 newMotion = (value ^ 0x80) >> 4;
    // Check if new horizontal move can still be applied normally
    if(newMotion > myMotionClockM1 ||
       hpos <= BSPF_min(myCurrentHMOVEPos + 6 + newMotion * 4, 7))
    {
      myPOSM1 -= (newMotion - myMotionClockM1);
      myMotionClockM1 = newMotion;
    }
    else
    {
      myPOSM1 -= (15 - myMotionClockM1);
      myMotionClockM1 = 15;
      if(value != 0x70 && value != 0x80)
        myHMM1mmr = true;
    }
    CLAMP_POS(myPOSM1);
  }
  myHMM1 = value;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void TIA::pokeHMBL(uInt8 value, Int32 clock)
{
  value &= 0xF0;
  if(myHMBL == value)
    return;

  int hpos  = (clock - myClockWhenFrameStarted) % 228 - HBLANK;

  // Check if HMOVE is currently active
  if(myCurrentHMOVEPos != 0x7FFFFFFF &&
     hpos < BSPF_min(myCurrentHMOVEPos + 6 + myMotionClockBL * 4, 7))
  {
    Int32 newMotion = (value ^ 0x80) >> 4;
    // Check if new horizontal move can still be applied normally
    if(newMotion > myMotionClockBL ||
       hpos <= BSPF_min(myCurrentHMOVEPos + 6 + newMotion * 4, 7))
    {
      myPOSBL -= (newMotion - myMotionClockBL);
      myMotionClockBL = newMotion;
    }
    else
    {
      myPOSBL -= (15 - myMotionClockBL);
      myMotionClockBL = 15;
      if(value != 0x70 && value != 0x80)
        myHMBLmmr = true;
    }
    CLAMP_POS(myPOSBL);
  }
  myHMBL = value;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// The following two methods apply extra clocks when a horizontal motion
// register (HMxx) is modified during an HMOVE, before waiting for the
// documented time of at least 24 CPU cycles.  The applicable explanation
// from A. Towers Hardware Notes is as follows:
//
//   In theory then the side effects of modifying the HMxx registers
//   during HMOVE should be quite straight-forward. If the internal
//   counter has not yet reached the value in HMxx, a new value greater
//   than this (in 0-15 terms) will work normally. Conversely, if
//   the counter has already reached the value in HMxx, new values
//   will have no effect because the latch will have been cleared.
//
// Most of the ideas in these methods come from MESS.
// (used with permission from Wilbert Pol)
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
inline void TIA::applyActiveHMOVEMotion(int hpos, Int16& pos, Int32 motionClock)
{
  if(hpos < BSPF_min(myCurrentHMOVEPos + 6 + 16 * 4, 7))
  {
    Int32 decrements_passed = (hpos - (myCurrentHMOVEPos + 4)) >> 2;
    pos += 8;
    if((motionClock - decrements_passed) > 0)
    {
      pos -= (motionClock - decrements_passed);
      if(pos < 0)  pos += 160;
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
inline void TIA::applyPreviousHMOVEMotion(int hpos, Int16& pos, uInt8 motion)
{
  if(myPreviousHMOVEPos != 0x7FFFFFFF)
  {
    uInt8 motclk = (motion ^ 0x80) >> 4;
    if(hpos <= myPreviousHMOVEPos - 228 + 5 + motclk * 4)
    {
      uInt8 motclk_passed = (hpos - (myPreviousHMOVEPos - 228 + 6)) >> 2;
      pos -= (motclk - motclk_passed);
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
TIA::TIA(const TIA& c)
  : myConsole(c.myConsole),
    mySound(c.mySound),
    mySettings(c.mySettings)
{
  assert(false);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
TIA& TIA::operator = (const TIA&)
{
  assert(false);
  return *this;
}
