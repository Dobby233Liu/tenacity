/**********************************************************************

Audacity: A Digital Audio Editor

ProjectAudioManager.h

Paul Licameli split from ProjectManager.h

**********************************************************************/

#ifndef __AUDACITY_PROJECT_AUDIO_MANAGER__
#define __AUDACITY_PROJECT_AUDIO_MANAGER__

#include "AudioIOListener.h" // to inherit
#include "ClientData.h" // to inherit

class AudacityProject;
struct AudioIOStartStreamOptions;

class ProjectAudioManager final
   : public ClientData::Base
   , public AudioIOListener
   , public std::enable_shared_from_this< ProjectAudioManager >
{
public:
   static ProjectAudioManager &Get( AudacityProject &project );
   static const ProjectAudioManager &Get( const AudacityProject &project );

   explicit ProjectAudioManager( AudacityProject &project )
      : mProject{ project }
   {}
   ~ProjectAudioManager() override;

   bool IsTimerRecordCancelled() { return mTimerRecordCanceled; }
   void SetTimerRecordCancelled() { mTimerRecordCanceled = true; }
   void ResetTimerRecordCancelled() { mTimerRecordCanceled = false; }

   bool Paused() const { return mPaused; }
   
   // Whether recording into this project (not just into some project) is
   // active
   bool Recording() const;

   // Whether the last attempt to start recording requested appending to tracks
   bool Appending() const { return mAppending; }

   void SetPaused( bool value ) { mPaused = value; }
   void SetAppending( bool value ) { mAppending = value; }

private:
   // Audio IO callback methods
   void OnAudioIORate(int rate) override;
   void OnAudioIOStartRecording() override;
   void OnAudioIOStopRecording() override;
   void OnAudioIONewBlockFiles(const AutoSaveFile & blockFileLog) override;
   void OnCommitRecording() override;
   void OnSoundActivationThreshold() override;

   AudacityProject &mProject;

   //flag for cancellation of timer record.
   bool mTimerRecordCanceled{ false };

   bool mPaused{ false };
   bool mAppending{ false };
};

AudioIOStartStreamOptions DefaultPlayOptions( AudacityProject &project );
AudioIOStartStreamOptions DefaultSpeedPlayOptions( AudacityProject &project );

/// Namespace for functions for Transport menu
namespace TransportActions {
void StopIfPaused( AudacityProject &project );
bool DoPlayStopSelect( AudacityProject &project, bool click, bool shift );
void DoPlayStopSelect( AudacityProject &project );
void DoStop( AudacityProject & );
void DoPause( AudacityProject & );
void DoLockPlayRegion( AudacityProject & );
void DoUnlockPlayRegion( AudacityProject & );
void DoTogglePinnedHead( AudacityProject & );
void DoRecord( AudacityProject & );
}

#endif
