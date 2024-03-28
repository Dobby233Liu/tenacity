/**********************************************************************
 
 Audacity: A Digital Audio Editor
 
 PlaybackSchedule.h
 
 Paul Licameli split from AudioIOBase.h
 
 **********************************************************************/

#ifndef __AUDACITY_PLAYBACK_SCHEDULE__
#define __AUDACITY_PLAYBACK_SCHEDULE__

#include "MemoryX.h"
#include <atomic>
#include <chrono>
#include <vector>

struct AudioIOStartStreamOptions;
class BoundedEnvelope;
using PRCrossfadeData = std::vector< std::vector < float > >;

constexpr size_t TimeQueueGrainSize = 2000;

struct RecordingSchedule {
   double mPreRoll{};
   double mLatencyCorrection{}; // negative value usually
   double mDuration{};
   PRCrossfadeData mCrossfadeData;

   // These are initialized by the main thread, then updated
   // only by the thread calling TrackBufferExchange:
   double mPosition{};
   bool mLatencyCorrected{};

   double TotalCorrection() const { return mLatencyCorrection - mPreRoll; }
   double ToConsume() const;
   double Consumed() const;
   double ToDiscard() const;
};

struct PlaybackSchedule;

//! Describes an amount of contiguous (but maybe time-warped) data to be extracted from tracks to play
struct PlaybackSlice {
   const size_t frames; //!< Total number of frames to be buffered
   const size_t toProduce; //!< Not more than `frames`; the difference will be trailing silence
   const bool progress; //!< To be removed

   //! Constructor enforces some invariants
   /*! @invariant `result.toProduce <= result.frames && result.frames <= available`
    */
   PlaybackSlice(
      size_t available, size_t frames_, size_t toProduce_, bool progress_)
      : frames{ std::min(available, frames_) }
      , toProduce{ std::min(toProduce_, frames) }
      , progress{ progress_ }
   {}
};

//! Directs which parts of tracks to fetch for playback
/*!
 A non-default policy object may be created each time playback begins, and if so it is destroyed when
 playback stops, not reused in the next playback.

 Methods of the object are passed a PlaybackSchedule as context.
 */
class PlaybackPolicy {
public:
   //! @section Called by the main thread

   virtual ~PlaybackPolicy() = 0;

   //! Called before starting an audio stream
   virtual void Initialize( PlaybackSchedule &schedule, double rate );

   //! Called after stopping of an audio stream or an unsuccessful start
   virtual void Finalize( PlaybackSchedule &schedule );

   //! Normalizes mTime, clamping it and handling gaps from cut preview.
   /*!
    * Clamps the time (unless scrubbing), and skips over the cut section.
    * Returns a time in seconds.
    */
   virtual double NormalizeTrackTime( PlaybackSchedule &schedule );

   //! @section Called by the PortAudio callback thread

   //! Whether repositioning commands are allowed during playback
   virtual bool AllowSeek( PlaybackSchedule &schedule );

   //! Returns true if schedule.GetTrackTime() has reached the end of playback
   virtual bool Done( PlaybackSchedule &schedule,
      unsigned long outputFrames //!< how many playback frames were taken from RingBuffers
   );

   //! @section Called by the AudioIO::TrackBufferExchange thread

   //! How long to wait between calls to AudioIO::TrackBufferExchange
   virtual std::chrono::milliseconds
      SleepInterval( PlaybackSchedule &schedule );

   //! Choose length of one fetch of samples from tracks in a call to AudioIO::FillPlayBuffers
   virtual PlaybackSlice GetPlaybackSlice( PlaybackSchedule &schedule,
      size_t available //!< upper bound for the length of the fetch
   );

   //! @section To be removed

   virtual bool Looping( const PlaybackSchedule &schedule ) const;

protected:
   double mRate = 0;
};

struct TENACITY_DLL_API PlaybackSchedule {

   /// Playback starts at offset of mT0, which is measured in seconds.
   double              mT0;
   /// Playback ends at offset of mT1, which is measured in seconds.  Note that mT1 may be less than mT0 during scrubbing.
   double              mT1;
   /// Current track time position during playback, in seconds.
   /// Initialized by the main thread but updated by worker threads during
   /// playback or recording, and periodically reread by the main thread for
   /// purposes such as display update.
   std::atomic<double> mTime;

   /// Accumulated real time (not track position), starting at zero (unlike
   /// mTime), and wrapping back to zero each time around looping play.
   /// Thus, it is the length in real seconds between mT0 and mTime.
   double              mWarpedTime;

   /// Real length to be played (if looping, for each pass) after warping via a
   /// time track, computed just once when starting the stream.
   /// Length in real seconds between mT0 and mT1.  Always positive.
   double              mWarpedLength;

   // mWarpedTime and mWarpedLength are irrelevant when scrubbing,
   // else they are used in updating mTime,
   // and when not scrubbing or playing looped, mTime is also used
   // in the test for termination of playback.

   // with ComputeWarpedLength, it is now possible the calculate the warped length with 100% accuracy
   // (ignoring accumulated rounding errors during playback) which fixes the 'missing sound at the end' bug
   
   const BoundedEnvelope *mEnvelope;

   //! A circular buffer
   /*
    Holds track time values corresponding to every nth sample in the
    playback buffers, for the large n == TimeQueueGrainSize.

    The "producer" is the Audio thread that fetches samples from tracks and
    fills the playback RingBuffers.  The "consumer" is the high-latency
    PortAudio thread that drains the RingBuffers.  The atomics in the
    RingBuffer implement lock-free synchronization.

    This other structure relies on the RingBuffer's synchronization, and adds
    other information to the stream of samples:  which track times they
    correspond to.

    The consumer thread uses that information, and also makes known to the main
    thread, what the last consumed track time is.  The main thread can use that
    for other purposes such as refreshing the display of the play head position.
    */
   struct TimeQueue {
      ArrayOf<double> mData;
      size_t mSize{ 0 };
      double mLastTime {};
      struct Cursor {
         size_t mIndex {};
         size_t mRemainder {};
      };
      //! Aligned to avoid false sharing
      NonInterfering<Cursor> mHead, mTail;

      void Producer(
         const PlaybackSchedule &schedule, double rate, double scrubSpeed,
         size_t nSamples );
      double Consumer( size_t nSamples, double rate );

      //! Empty the queue and reassign the last produced time
      /*! Assumes the producer and consumer are suspended */
      void Prime(double time);
   } mTimeQueue;

   PlaybackPolicy &GetPolicy();
   const PlaybackPolicy &GetPolicy() const;

   volatile enum {
      PLAY_STRAIGHT,
      PLAY_LOOPED,
#ifdef EXPERIMENTAL_SCRUBBING_SUPPORT
      PLAY_SCRUB,
      PLAY_AT_SPEED, // a version of PLAY_SCRUB.
      PLAY_KEYBOARD_SCRUB,
#endif
   }                   mPlayMode { PLAY_STRAIGHT };
   double              mCutPreviewGapStart;
   double              mCutPreviewGapLen;

   void Init(
      double t0, double t1,
      const AudioIOStartStreamOptions &options,
      const RecordingSchedule *pRecordingSchedule );

   /** @brief Compute signed duration (in seconds at playback) of the specified region of the track.
    *
    * Takes a region of the time track (specified by the unwarped time points in the project), and
    * calculates how long it will actually take to play this region back, taking the time track's
    * warping effects into account.
    * @param t0 unwarped time to start calculation from
    * @param t1 unwarped time to stop calculation at
    * @return the warped duration in seconds, negated if `t0 > t1`
    */
   double ComputeWarpedLength(double t0, double t1) const;

   /** @brief Compute how much unwarped time must have elapsed if length seconds of warped time has
    * elapsed, and add to t0
    *
    * @param t0 The unwarped time (seconds from project start) at which to start
    * @param length How many seconds of real time went past; signed
    * @return The end point (in seconds from project start) as unwarped time
    */
   double SolveWarpedLength(double t0, double length) const;

   /** \brief True if the end time is before the start time */
   bool ReversedTime() const
   {
      return mT1 < mT0;
   }

   /** \brief Get current track time value, unadjusted
    *
    * Returns a time in seconds.
    */
   double GetTrackTime() const
   { return mTime.load(std::memory_order_relaxed); }

   /** \brief Set current track time value, unadjusted
    */
   void SetTrackTime( double time )
   { mTime.store(time, std::memory_order_relaxed); }

   /** \brief Clamps argument to be between mT0 and mT1
    *
    * Returns the bound if the value is out of bounds; does not wrap.
    * Returns a time in seconds.
    */
   double ClampTrackTime( double trackTime ) const;

   /** \brief Clamps mTime to be between mT0 and mT1
    *
    * Returns the bound if the value is out of bounds; does not wrap.
    * Returns a time in seconds.
    */
   double LimitTrackTime() const;

   void ResetMode() {
      mPlayMode = PLAY_STRAIGHT;
      mPolicyValid.store(false, std::memory_order_release);
   }

   bool Scrubbing() const       { return mPlayMode == PLAY_SCRUB || mPlayMode == PLAY_KEYBOARD_SCRUB; }
   bool PlayingAtSpeed() const  { return mPlayMode == PLAY_AT_SPEED; }
   bool Interactive() const     { return Scrubbing() || PlayingAtSpeed(); }

   // Returns true if time equals t1 or is on opposite side of t1, to t0
   bool Overruns( double trackTime ) const;

   // Compute the NEW track time for the given one and a real duration,
   // taking into account whether the schedule is for looping
   double AdvancedTrackTime(
      double trackTime, double realElapsed, double speed) const;

   // Convert time between mT0 and argument to real duration, according to
   // time track if one is given; result is always nonnegative
   double RealDuration(double trackTime1) const;

   // How much real time left?
   double RealTimeRemaining() const;

   // Advance the real time position
   void RealTimeAdvance( double increment );

   // Determine starting duration within the first pass -- sometimes not
   // zero
   void RealTimeInit( double trackTime );
   
   void RealTimeRestart();

private:
   std::unique_ptr<PlaybackPolicy> mpPlaybackPolicy;
   std::atomic<bool> mPolicyValid{ false };
};

struct LoopingPlaybackPolicy final : PlaybackPolicy {
   ~LoopingPlaybackPolicy() override;

   bool Done( PlaybackSchedule &schedule, unsigned long ) override;
   PlaybackSlice GetPlaybackSlice(
      PlaybackSchedule &schedule, size_t available ) override;
   bool Looping( const PlaybackSchedule & ) const override;
};
#endif
