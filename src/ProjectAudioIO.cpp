/**********************************************************************

Audacity: A Digital Audio Editor

ProjectAudioIO.cpp

Paul Licameli split from TenacityProject.cpp

**********************************************************************/

#include "ProjectAudioIO.h"

#include "AudioIOBase.h"
#include "Project.h"

static const TenacityProject::AttachedObjects::RegisteredFactory sAudioIOKey{
  []( TenacityProject &parent ){
     return std::make_shared< ProjectAudioIO >( parent );
   }
};

ProjectAudioIO &ProjectAudioIO::Get( TenacityProject &project )
{
   return project.AttachedObjects::Get< ProjectAudioIO >( sAudioIOKey );
}

const ProjectAudioIO &ProjectAudioIO::Get( const TenacityProject &project )
{
   return Get( const_cast<TenacityProject &>(project) );
}

ProjectAudioIO::ProjectAudioIO( TenacityProject &project )
: mProject{ project }
{
}

ProjectAudioIO::~ProjectAudioIO()
{
}

int ProjectAudioIO::GetAudioIOToken() const
{
   return mAudioIOToken;
}

void ProjectAudioIO::SetAudioIOToken(int token)
{
   mAudioIOToken = token;
}

bool ProjectAudioIO::IsAudioActive() const
{
   auto gAudioIO = AudioIOBase::Get();
   return GetAudioIOToken() > 0 &&
      gAudioIO->IsStreamActive(GetAudioIOToken());
}

const std::shared_ptr<Meter> &ProjectAudioIO::GetPlaybackMeter() const
{
   return mPlaybackMeter;
}

void ProjectAudioIO::SetPlaybackMeter(
   const std::shared_ptr<Meter> &playback)
{
   auto &project = mProject;
   mPlaybackMeter = playback;
   auto gAudioIO = AudioIOBase::Get();
   if (gAudioIO)
   {
      gAudioIO->SetPlaybackMeter( project.shared_from_this() , mPlaybackMeter );
   }
}

const std::shared_ptr<Meter> &ProjectAudioIO::GetCaptureMeter() const
{
   return mCaptureMeter;
}

void ProjectAudioIO::SetCaptureMeter(
   const std::shared_ptr<Meter> &capture)
{
   auto &project = mProject;
   mCaptureMeter = capture;

   auto gAudioIO = AudioIOBase::Get();
   if (gAudioIO)
   {
      gAudioIO->SetCaptureMeter( project.shared_from_this(), mCaptureMeter );
   }
}
