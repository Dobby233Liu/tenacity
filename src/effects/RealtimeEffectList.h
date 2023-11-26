/**********************************************************************

 Audacity: A Digital Audio Editor

 RealtimeEffectList.h

 *********************************************************************/

#ifndef __AUDACITY_REALTIMEEFFECTLIST_H__
#define __AUDACITY_REALTIMEEFFECTLIST_H__

#include "TrackAttachment.h"

class TenacityProject;

class Track;

class RealtimeEffectList final : public TrackAttachment
{
   RealtimeEffectList(const RealtimeEffectList &) = delete;
   RealtimeEffectList &operator=(const RealtimeEffectList &) = delete;

public:
   RealtimeEffectList();
   virtual ~RealtimeEffectList();

   static RealtimeEffectList &Get(TenacityProject &project);
   static const RealtimeEffectList &Get(const TenacityProject &project);

   static RealtimeEffectList &Get(Track &track);
   static const RealtimeEffectList &Get(const Track &track);
};

#endif // __AUDACITY_REALTIMEEFFECTLIST_H__
