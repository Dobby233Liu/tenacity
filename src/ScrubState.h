/*!********************************************************************
 
 Tenacity
 
 @file ScrubState.h
 
 Paul Licameli split from AudioIO.cpp
 
 **********************************************************************/

#ifndef __AUDACITY_SCRUB_STATE__
#define __AUDACITY_SCRUB_STATE__

#include "PlaybackSchedule.h" // to inherit

#include "AudioIOBase.h" // for ScrubbingOptions

class ScrubbingPlaybackPolicy final : public PlaybackPolicy {
public:
   explicit ScrubbingPlaybackPolicy(const ScrubbingOptions &);
   ~ScrubbingPlaybackPolicy() override;

   double NormalizeTrackTime( PlaybackSchedule &schedule ) override;

private:
   const ScrubbingOptions mOptions;
};

#endif
