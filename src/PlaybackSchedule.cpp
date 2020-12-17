/**********************************************************************
 
 Audacity: A Digital Audio Editor
 
 PlaybackSchedule.cpp
 
 Paul Licameli split from AudioIOBase.cpp
 
 **********************************************************************/

#include "PlaybackSchedule.h"

#include "AudioIOBase.h"
#include "Envelope.h"
#include "Mix.h"
#include "SampleCount.h"

#include <cmath>

PlaybackPolicy::~PlaybackPolicy() = default;
void PlaybackPolicy::Initialize( PlaybackSchedule &, double rate )
{
   mRate = rate;
}
void PlaybackPolicy::Finalize( PlaybackSchedule & ){}

double PlaybackPolicy::NormalizeTrackTime( PlaybackSchedule &schedule )
{
   // Track time readout for the main thread

   // dmazzoni: This function is needed for two reasons:
   // One is for looped-play mode - this function makes sure that the
   // position indicator keeps wrapping around.  The other reason is
   // more subtle - it's because PortAudio can query the hardware for
   // the current stream time, and this query is not always accurate.
   // Sometimes it's a little behind or ahead, and so this function
   // makes sure that at least we clip it to the selection.
   //
   // msmeyer: There is also the possibility that we are using "cut preview"
   //          mode. In this case, we should jump over a defined "gap" in the
   //          audio.

   // Limit the time between t0 and t1.
   // Should the limiting be necessary in any play mode if there are no bugs?
   double absoluteTime = schedule.LimitTrackTime();

   if (schedule.mCutPreviewGapLen > 0)
   {
      // msmeyer: We're in cut preview mode, so if we are on the right
      // side of the gap, we jump over it.
      if (absoluteTime > schedule.mCutPreviewGapStart)
         absoluteTime += schedule.mCutPreviewGapLen;
   }

   return absoluteTime;
}

bool PlaybackPolicy::AllowSeek(PlaybackSchedule &)
{
   return true;
}

bool PlaybackPolicy::Done( PlaybackSchedule &schedule,
   unsigned long outputFrames)
{
   auto diff = schedule.GetTrackTime() - schedule.mT1;
   if (schedule.ReversedTime())
      diff *= -1;
   return sampleCount(floor(diff * mRate + 0.5)) >= 0 &&
      // Require also that output frames are all consumed from ring buffer
      outputFrames == 0;
}

std::chrono::milliseconds PlaybackPolicy::SleepInterval(PlaybackSchedule &)
{
   using namespace std::chrono;
   return 10ms;
}

PlaybackSlice
PlaybackPolicy::GetPlaybackSlice(PlaybackSchedule &schedule, size_t available)
{
   // How many samples to produce for each channel.
   const auto realTimeRemaining = schedule.RealTimeRemaining();
   auto frames = available;
   auto toProduce = frames;
   double deltat = frames / mRate;

   if (deltat > realTimeRemaining)
   {
      // Produce some extra silence so that the time queue consumer can
      // satisfy its end condition
      const double extraRealTime = (TimeQueueGrainSize + 1) / mRate;
      auto extra = std::min( extraRealTime, deltat - realTimeRemaining );
      auto realTime = realTimeRemaining + extra;
      frames = realTime * mRate;
      toProduce = realTimeRemaining * mRate;
      schedule.RealTimeAdvance( realTime );
   }
   else
      schedule.RealTimeAdvance( deltat );

   return { available, frames, toProduce };
}

bool PlaybackPolicy::RepositionPlayback(
   PlaybackSchedule &, const Mixers &, size_t, size_t)
{
   return true;
}

bool PlaybackPolicy::Looping(const PlaybackSchedule &) const
{
   return false;
}

namespace {
struct DefaultPlaybackPolicy final : PlaybackPolicy {
   ~DefaultPlaybackPolicy() override = default;
};
}

PlaybackPolicy &PlaybackSchedule::GetPolicy()
{
   if (mPolicyValid.load(std::memory_order_acquire) && mpPlaybackPolicy)
      return *mpPlaybackPolicy;

   static DefaultPlaybackPolicy defaultPolicy;
   return defaultPolicy;
}

const PlaybackPolicy &PlaybackSchedule::GetPolicy() const
{
   return const_cast<PlaybackSchedule&>(*this).GetPolicy();
}

LoopingPlaybackPolicy::~LoopingPlaybackPolicy() = default;

bool LoopingPlaybackPolicy::Done( PlaybackSchedule &, unsigned long )
{
   return false;
}

PlaybackSlice
LoopingPlaybackPolicy::GetPlaybackSlice(
   PlaybackSchedule &schedule, size_t available)
{
   // How many samples to produce for each channel.
   const auto realTimeRemaining = schedule.RealTimeRemaining();
   auto frames = available;
   auto toProduce = frames;
   double deltat = frames / mRate;

   if (deltat > realTimeRemaining)
   {
      toProduce = frames = realTimeRemaining * mRate;
      schedule.RealTimeAdvance( realTimeRemaining );
   }
   else
      schedule.RealTimeAdvance( deltat );

   // Don't fall into an infinite loop, if loop-playing a selection
   // that is so short, it has no samples: detect that case
   if (frames == 0) {
      bool progress = (schedule.mWarpedTime != 0.0);
      if (!progress)
         // Cause FillPlayBuffers to make progress, filling all available with 0
         frames = available, toProduce = 0;
   }
   return { available, frames, toProduce };
}

bool LoopingPlaybackPolicy::RepositionPlayback(
   PlaybackSchedule &schedule, const Mixers &playbackMixers, size_t, size_t )
{
   // msmeyer: If playing looped, check if we are at the end of the buffer
   // and if yes, restart from the beginning.
   if (schedule.RealTimeRemaining() <= 0)
   {
      for (auto &pMixer : playbackMixers)
         pMixer->Restart();
      schedule.RealTimeRestart();
   }
   return false;
}

bool LoopingPlaybackPolicy::Looping( const PlaybackSchedule & ) const
{
   return true;
}

void PlaybackSchedule::Init(
   const double t0, const double t1,
   const AudioIOStartStreamOptions &options,
   const RecordingSchedule *pRecordingSchedule )
{
   mpPlaybackPolicy.reset();

   if ( pRecordingSchedule )
      // It does not make sense to apply the time warp during overdub recording,
      // which defeats the purpose of making the recording synchronized with
      // the existing audio.  (Unless we figured out the inverse warp of the
      // captured samples in real time.)
      // So just quietly ignore the time track.
      mEnvelope = nullptr;
   else
      mEnvelope = options.envelope;

   mT0      = t0;
   if (pRecordingSchedule)
      mT0 -= pRecordingSchedule->mPreRoll;

   mT1      = t1;
   if (pRecordingSchedule)
      // adjust mT1 so that we don't give paComplete too soon to fill up the
      // desired length of recording
      mT1 -= pRecordingSchedule->mLatencyCorrection;

   // Main thread's initialization of mTime
   SetTrackTime( mT0 );

   mPlayMode = PlaybackSchedule::PLAY_STRAIGHT;
   if (options.policyFactory)
      mpPlaybackPolicy = options.policyFactory(options);
   else if (options.playLooped) {
      mPlayMode = PlaybackSchedule::PLAY_LOOPED;
      mpPlaybackPolicy = std::make_unique<LoopingPlaybackPolicy>();
   }

   mCutPreviewGapStart = options.cutPreviewGapStart;
   mCutPreviewGapLen = options.cutPreviewGapLen;

#ifdef EXPERIMENTAL_SCRUBBING_SUPPORT
   bool scrubbing = (options.pScrubbingOptions != nullptr);

   // Scrubbing is not compatible with looping or recording or a time track!
   if (scrubbing)
   {
      const auto &scrubOptions = *options.pScrubbingOptions;
      if (pRecordingSchedule ||
          options.playLooped ||
          mEnvelope ||
          scrubOptions.maxSpeed < ScrubbingOptions::MinAllowedScrubSpeed()) {
         wxASSERT(false);
         scrubbing = false;
      }
      else {
         if (scrubOptions.isPlayingAtSpeed)
            mPlayMode = PLAY_AT_SPEED;
         else if (scrubOptions.isKeyboardScrubbing)
            mPlayMode = PLAY_KEYBOARD_SCRUB;
         else
            mPlayMode = PLAY_SCRUB;
      }
   }
#endif

   mWarpedTime = 0.0;
#ifdef EXPERIMENTAL_SCRUBBING_SUPPORT
   if (Scrubbing())
      mWarpedLength = 0.0f;
   else
#endif
      mWarpedLength = RealDuration(mT1);

   mPolicyValid.store(true, std::memory_order_release);
}

double PlaybackSchedule::LimitTrackTime() const
{
   // Track time readout for the main thread
   // Allows for forward or backward play
   return ClampTrackTime( GetTrackTime() );
}

double PlaybackSchedule::ClampTrackTime( double trackTime ) const
{
   if (ReversedTime())
      return std::max(mT1, std::min(mT0, trackTime));
   else
      return std::max(mT0, std::min(mT1, trackTime));
}

bool PlaybackSchedule::Overruns( double trackTime ) const
{
   return (ReversedTime() ? trackTime <= mT1 : trackTime >= mT1);
}

double PlaybackSchedule::ComputeWarpedLength(double t0, double t1) const
{
   if (mEnvelope)
      return mEnvelope->IntegralOfInverse(t0, t1);
   else
      return t1 - t0;
}

double PlaybackSchedule::SolveWarpedLength(double t0, double length) const
{
   if (mEnvelope)
      return mEnvelope->SolveIntegralOfInverse(t0, length);
   else
      return t0 + length;
}

double PlaybackSchedule::AdvancedTrackTime(
   double time, double realElapsed, double speed ) const
{
   auto &policy = GetPolicy();
   const bool looping = policy.Looping(*this);

   if (ReversedTime())
      realElapsed *= -1.0;

   // Defense against cases that might cause loops not to terminate
   if ( fabs(mT0 - mT1) < 1e-9 )
      return mT0;

   if (mEnvelope) {
       wxASSERT( speed == 1.0 );

      double total=0.0;
      bool foundTotal = false;
      do {
         auto oldTime = time;
         if (foundTotal && fabs(realElapsed) > fabs(total))
            // Avoid SolveWarpedLength
            time = mT1;
         else
            time = SolveWarpedLength(time, realElapsed);

         if (!looping || !Overruns( time ))
            break;

         // Bug1922:  The part of the time track outside the loop should not
         // influence the result
         double delta;
         if (foundTotal && oldTime == mT0)
            // Avoid integrating again
            delta = total;
         else {
            delta = ComputeWarpedLength(oldTime, mT1);
            if (oldTime == mT0)
               foundTotal = true, total = delta;
         }
         realElapsed -= delta;
         time = mT0;
      } while ( true );
   }
   else {
      time += realElapsed * fabs(speed);

      // Wrap to start if looping
      if (looping) {
         while ( Overruns( time ) ) {
            // LL:  This is not exactly right, but I'm at my wits end trying to
            //      figure it out.  Feel free to fix it.  :-)
            // MB: it's much easier than you think, mTime isn't warped at all!
            time -= mT1 - mT0;
         }
      }
   }

   return time;
}

double PlaybackSchedule::RealDuration(double trackTime1) const
{
   return fabs(ComputeWarpedLength(mT0, trackTime1));
}

double PlaybackSchedule::RealTimeRemaining() const
{
   return mWarpedLength - mWarpedTime;
}

void PlaybackSchedule::RealTimeAdvance( double increment )
{
   mWarpedTime += increment;
}

void PlaybackSchedule::RealTimeInit( double trackTime )
{
   if (Scrubbing())
      mWarpedTime = 0.0;
   else
      mWarpedTime = RealDuration( trackTime );
}

void PlaybackSchedule::RealTimeRestart()
{
   mWarpedTime = 0;
}

double RecordingSchedule::ToConsume() const
{
   return mDuration - Consumed();
}

double RecordingSchedule::Consumed() const
{
   return std::max( 0.0, mPosition + TotalCorrection() );
}

double RecordingSchedule::ToDiscard() const
{
   return std::max(0.0, -( mPosition + TotalCorrection() ) );
}

void PlaybackSchedule::TimeQueue::Producer(
   const PlaybackSchedule &schedule, double rate, double scrubSpeed,
   size_t nSamples )
{
   if ( ! mData )
      // Recording only.  Don't fill the queue.
      return;

   // Don't check available space:  assume it is enough because of coordination
   // with RingBuffer.
   auto index = mTail.mIndex;
   auto time = mLastTime;
   auto remainder = mTail.mRemainder;
   auto space = TimeQueueGrainSize - remainder;

   while ( nSamples >= space ) {
      time = schedule.AdvancedTrackTime( time, space / rate, scrubSpeed );
      index = (index + 1) % mSize;
      mData[ index ] = time;
      nSamples -= space;
      remainder = 0;
      space = TimeQueueGrainSize;
   }

   // Last odd lot
   if ( nSamples > 0 )
      time = schedule.AdvancedTrackTime( time, nSamples / rate, scrubSpeed );

   mLastTime = time;
   mTail.mRemainder = remainder + nSamples;
   mTail.mIndex = index;
}

double PlaybackSchedule::TimeQueue::Consumer( size_t nSamples, double rate )
{
   if ( ! mData ) {
      // Recording only.  No scrub or playback time warp.  Don't use the queue.
      return ( mLastTime += nSamples / rate );
   }

   // Don't check available space:  assume it is enough because of coordination
   // with RingBuffer.
   auto remainder = mHead.mRemainder;
   auto space = TimeQueueGrainSize - remainder;
   if ( nSamples >= space ) {
      remainder = 0,
      mHead.mIndex = (mHead.mIndex + 1) % mSize,
      nSamples -= space;
      if ( nSamples >= TimeQueueGrainSize )
         mHead.mIndex =
            (mHead.mIndex + ( nSamples / TimeQueueGrainSize ) ) % mSize,
         nSamples %= TimeQueueGrainSize;
   }
   mHead.mRemainder = remainder + nSamples;
   return mData[ mHead.mIndex ];
}

void PlaybackSchedule::TimeQueue::Prime(double time)
{
   mHead = mTail = {};
   mLastTime = time;
}
