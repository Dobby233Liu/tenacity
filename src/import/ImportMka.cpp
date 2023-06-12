// SPDX-License-Identifier: GPL-2.0-or-later
/**********************************************************************

  Tenacity: A Digital Audio Editor

  ImportMka.cpp

  Steve Lhomme

**********************************************************************/

#include "Import.h"
#include "ImportPlugin.h"

static const auto exts = {wxT("mka"), wxT("mkv")};
#define DESC XO("Matroska files")

#ifdef USE_LIBMATROSKA

#include "../Tags.h"
#include "../widgets/ProgressDialog.h"
#include "../WaveTrack.h"

#if defined(_CRTDBG_MAP_ALLOC) && LIBMATROSKA_VERSION < 0x010702
// older libmatroska headers use std::nothrow which is incompatible with <crtdbg.h>
#undef new
#endif

#include <ebml/EbmlHead.h>
#include <ebml/EbmlSubHead.h>
#include <ebml/EbmlVoid.h>
#include <ebml/StdIOCallback.h>
#include <ebml/EbmlStream.h>
#include <ebml/EbmlContexts.h>
#include <matroska/KaxSegment.h>
#include <matroska/KaxSeekHead.h>
#include <matroska/KaxCluster.h>
#include <matroska/KaxVersion.h>

#include <wx/log.h>

using namespace libmatroska;

class MkaImportPlugin final : public ImportPlugin
{
    public:
    MkaImportPlugin()
    :  ImportPlugin( FileExtensions( exts.begin(), exts.end() ) )
    {
    }

    ~MkaImportPlugin() { }

    wxString GetPluginStringID() override { return wxT("libmatroska"); }
    TranslatableString GetPluginFormatDescription() override;
    std::unique_ptr<ImportFileHandle> Open(const FilePath &Filename, TenacityProject*) override;
};

using WaveTracks = std::vector< std::shared_ptr<WaveTrack> >;

class MkaImportFileHandle final : public ImportFileHandle
{
public:
   MkaImportFileHandle(const FilePath &name,
                       std::unique_ptr<StdIOCallback> &,
                       std::unique_ptr<EbmlStream>  &,
                       std::unique_ptr<KaxSegment>  &,
                       std::unique_ptr<KaxSeekHead> &,
                       std::unique_ptr<KaxInfo>     &,
                       std::unique_ptr<KaxTracks>   &,
                       std::unique_ptr<KaxTags>     &,
                       std::unique_ptr<KaxCluster>  &);
   ~MkaImportFileHandle();

   TranslatableString GetFileDescription() override;
   ByteCount GetFileUncompressedBytes() override;
   ProgressResult Import(WaveTrackFactory *trackFactory, TrackHolders &outTracks, Tags *tags) override;

   wxInt32 GetStreamCount() override;

   const TranslatableStrings &GetStreamInfo() override;

   void SetStreamUsage(wxInt32 WXUNUSED(StreamID), bool WXUNUSED(Use)) override;

private:
    std::unique_ptr<StdIOCallback> mkfile;
    std::unique_ptr<EbmlStream>  stream;
    std::unique_ptr<KaxSegment>  Segment;
    std::unique_ptr<KaxSeekHead> SeekHead;
    std::unique_ptr<KaxInfo>     SegmentInfo;
    std::unique_ptr<KaxTracks>   Tracks;
    std::unique_ptr<KaxTags>     mTags;
    std::unique_ptr<KaxCluster>  Cluster;

    struct AudioTrackInfo {
        bool                  mSelected;
        wxString              mName;
        sampleFormat          mFormat;
        const size_t          bytesPerSample;
        const unsigned        mChannels;
        const double          mRate;
        const uint16          mTrackNumber;
        WaveTracks            importChannels;
    };

    std::vector<AudioTrackInfo>  audioTracks;
    TranslatableStrings          mStreamInfo;

    void InitTracks();
};

TranslatableString MkaImportPlugin::GetPluginFormatDescription()
{
    return DESC;
}

template <typename Type>
Type * SeekHeadLoad(KaxSeekHead & SeekHead, KaxSegment & Segment, EbmlStream & Stream)
{
    // try to find element in the SeekHead
    auto *SeekTag = SeekHead.FindFirstOf(EBML_INFO(Type));
    if (SeekTag)
    {
        auto seekPos = Segment.GetGlobalPosition( (uint64)GetChild<KaxSeekPosition>(*SeekTag) );
        Stream.I_O().setFilePointer( seekPos, seek_beginning );

        EbmlElement *el = Stream.FindNextID(EBML_INFO(Type), UINT_MAX);
        if (el != nullptr)
        {
            EbmlElement *found = nullptr;
            int UpperElementLevel = 0;
            el->Read(Stream, EBML_CLASS_CONTEXT(Type), UpperElementLevel, found, true);
            assert(found == nullptr);
            assert(UpperElementLevel == 0);

            return static_cast<Type*>(el);
        }
    }
    return nullptr;
}



std::unique_ptr<ImportFileHandle> MkaImportPlugin::Open(
   const FilePath &filename, TenacityProject*)
{
    try
    {
        std::unique_ptr<StdIOCallback> mka_file = std::make_unique<StdIOCallback>(filename, ::MODE_READ);
        std::unique_ptr<EbmlStream> aStream = std::make_unique<EbmlStream>(*mka_file);
        {
            // Check the EBML header
            std::unique_ptr<EbmlHead> Header(static_cast<EbmlHead*>(aStream->FindNextID(EBML_INFO(EbmlHead), UINT_MAX)));
            if (Header == nullptr)
            {
                wxLogError(wxT("Matroska : %s is not an EBML file"), filename);
                return nullptr;
            }

            int UpperElementLevel = 0;
            EbmlElement *found = nullptr;
            Header->Read(*aStream, EBML_CONTEXT(Header), UpperElementLevel, found, true);

            std::string DocType = GetChild<EDocType>(*Header);
            if (DocType != "matroska")
            {
                // we only support Matroska EBML files, WebM doesn't have PCM or FLAC audio
                // use FFmpeg to import WebM
                wxLogError(wxT("Matroska : %s is not a Matroska file"), filename);
                return nullptr;
            }
            if ((uint64) GetChild<EDocTypeReadVersion>(*Header) > 5U)
            {
                // the file requires using a newer version of the parser
                wxLogError(wxT("Matroska : Unsupported read version %lld in %s"),
                    (uint64) GetChild<EDocTypeReadVersion>(*Header), filename);
                return nullptr;
            }
        }

        std::unique_ptr<KaxSegment> Segment(static_cast<KaxSegment*>(aStream->FindNextID(EBML_INFO(KaxSegment), UINT64_MAX)));
        if (Segment == nullptr)
        {
            wxLogError(wxT("Matroska : %s has no Segment"), filename);
            return nullptr;
        }

        std::unique_ptr<KaxSeekHead> SeekHead;
        std::unique_ptr<KaxInfo>     SegmentInfo;
        std::unique_ptr<KaxTracks>   Tracks;
        std::unique_ptr<KaxTags>     _Tags;
        std::unique_ptr<KaxCluster>  FirstCluster;

        int UpperElementLevel = 0;
        EbmlElement *elt = nullptr;
        for (;;)
        {
            elt = aStream->FindNextElement(EBML_CONTEXT(Segment), UpperElementLevel, Segment->GetSize(), true);
            if (elt == nullptr)
            {
                wxLogError(wxT("Matroska : %s Segment has no element"), filename);
                return nullptr;
            }

            if (EbmlId(*elt) == EBML_ID(KaxSeekHead))
            {
                SeekHead = std::unique_ptr<KaxSeekHead>(static_cast<KaxSeekHead*>(elt));
                EbmlElement *found = nullptr;
                assert(UpperElementLevel == 0);
                SeekHead->Read(*aStream, EBML_CONTEXT(SeekHead), UpperElementLevel, found, true);
                assert(found == nullptr);
                assert(UpperElementLevel == 0);
            }
            else if (EbmlId(*elt) == EBML_ID(KaxInfo))
            {
                SegmentInfo = std::unique_ptr<KaxInfo>(static_cast<KaxInfo*>(elt));
                EbmlElement *found = nullptr;
                assert(UpperElementLevel == 0);
                SegmentInfo->Read(*aStream, EBML_CONTEXT(SegmentInfo), UpperElementLevel, found, true);
                assert(found == nullptr);
                assert(UpperElementLevel == 0);
            }
            else if (EbmlId(*elt) == EBML_ID(KaxTracks))
            {
                Tracks = std::unique_ptr<KaxTracks>(static_cast<KaxTracks*>(elt));
                EbmlElement *found = nullptr;
                assert(UpperElementLevel == 0);
                Tracks->Read(*aStream, EBML_CONTEXT(Tracks), UpperElementLevel, found, true);
                assert(found == nullptr);
                assert(UpperElementLevel == 0);
            }
            else if (EbmlId(*elt) == EBML_ID(KaxTags))
            {
                _Tags = std::unique_ptr<KaxTags>(static_cast<KaxTags*>(elt));
                EbmlElement *found = nullptr;
                assert(UpperElementLevel == 0);
                _Tags->Read(*aStream, EBML_CONTEXT(_Tags), UpperElementLevel, found, true);
                assert(found == nullptr);
                assert(UpperElementLevel == 0);
            }
            else if (EbmlId(*elt) == EBML_ID(KaxCluster))
            {
                // now we can start reading the data
                FirstCluster = std::unique_ptr<KaxCluster>(static_cast<KaxCluster*>(elt));
                break;
            }
            else
            {
                // unused, void or unknown element
                elt->SkipData(*aStream, EBML_CONTEXT(Segment));
                delete elt;
                elt = nullptr;
                UpperElementLevel = 0;
            }
        }

        if (!SegmentInfo)
        {
            auto segInfo = SeekHeadLoad<KaxInfo>(*SeekHead, *Segment, *aStream);
            if (!segInfo)
            {
                wxLogError(wxT("Matroska : %s has no SegmentInfo"), filename);
                return nullptr;
            }
            SegmentInfo = std::unique_ptr<KaxInfo>(segInfo);
        }
        if (!Tracks)
        {
            auto segTrack = SeekHeadLoad<KaxTracks>(*SeekHead, *Segment, *aStream);
            if (!segTrack)
            {
                wxLogError(wxT("Matroska : %s has no Track"), filename);
                return nullptr;
            }
            Tracks = std::unique_ptr<KaxTracks>(segTrack);
        }

        if (!FirstCluster)
        {
            wxLogError(wxT("Matroska : %s has no Cluster, considering as empty file"), filename);
            return nullptr;
        }

        if (!SegmentInfo->CheckMandatory())
        {
            wxLogError(wxT("Matroska : missing mandatory SegmentInfo data, %s is unusable"), filename);
            return nullptr;
        }

        if (!Tracks->CheckMandatory())
        {
            wxLogError(wxT("Matroska : missing mandatory Track data, %s is unusable"), filename);
            return nullptr;
        }

        if (!SegmentInfo->VerifyChecksum())
            wxLogWarning(wxT("Matroska : SegmentInfo in %s has bogus checksum, using anyway"), filename);

        if (!Tracks->VerifyChecksum())
            wxLogWarning(wxT("Matroska : Tracks in %s has bogus checksum, using anyway"), filename);

        return std::make_unique<MkaImportFileHandle>(filename, mka_file, aStream, Segment, SeekHead, SegmentInfo, Tracks, _Tags, FirstCluster);

    } catch (const std::bad_alloc &) {
        return nullptr;
    } catch (std::runtime_error& e) {
        return nullptr;
    }

    return nullptr;
}

MkaImportFileHandle::MkaImportFileHandle(const FilePath &name,
                       std::unique_ptr<StdIOCallback> &_mkfile,
                       std::unique_ptr<EbmlStream>  &_aStream,
                       std::unique_ptr<KaxSegment>  &_Segment,
                       std::unique_ptr<KaxSeekHead> &_SeekHead,
                       std::unique_ptr<KaxInfo>     &_SegmentInfo,
                       std::unique_ptr<KaxTracks>   &_Tracks,
                       std::unique_ptr<KaxTags>     &_Tags,
                       std::unique_ptr<KaxCluster>  &_firstCluster)
:ImportFileHandle(name)
{
    mkfile.swap(_mkfile);
    stream.swap(_aStream);
    Segment.swap(_Segment);
    SeekHead.swap(_SeekHead);
    SegmentInfo.swap(_SegmentInfo);
    Tracks.swap(_Tracks);
    mTags.swap(_Tags);
    Cluster.swap(_firstCluster);

    InitTracks();
}

MkaImportFileHandle::~MkaImportFileHandle()
{
}

TranslatableString MkaImportFileHandle::GetFileDescription()
{
    return DESC;
}

auto MkaImportFileHandle::GetFileUncompressedBytes() -> ByteCount
{
    return 0; // TODO for PCM sources
}

wxInt32 MkaImportFileHandle::GetStreamCount()
{
    return mStreamInfo.size();
}

void MkaImportFileHandle::InitTracks()
{
    KaxTrackEntry *elt = FindChild<KaxTrackEntry>(*Tracks);
    while (elt != nullptr)
    {
        if ((uint64) elt->TrackNumber() > UINT16_MAX)
        {
            wxLogWarning(wxT("Matroska : invalid track number %lld"), (uint64) elt->TrackNumber());
        }
        else if ((uint64) GetChild<KaxTrackType>(*elt) == MATROSKA_TRACK_TYPE_AUDIO)
        {
            KaxTrackAudio *AudioTrack = FindChild<KaxTrackAudio>(*elt);
            if (AudioTrack != nullptr)
            {
                KaxAudioBitDepth *bitDepth = FindChild<KaxAudioBitDepth>(*AudioTrack);
                const auto & CodedId = (const std::string &) GetChild<KaxCodecID>(*elt);
                const auto *TrackName = FindChild<KaxTrackName>(*elt);

                AudioTrackInfo track{
                    true,
                    TrackName ? (const wchar_t*)(UTFstring)*TrackName : L"",
                    (sampleFormat)0,
                    (uint32)*bitDepth / 8,
                    GetChild<KaxAudioChannels>(*AudioTrack),
                    (double) GetChild<KaxAudioSamplingFreq>(*AudioTrack),
                    (uint16) elt->TrackNumber(),
                };

                if ((const std::string &) CodedId == "A_PCM/INT/LIT")
                {
                    if (bitDepth != nullptr && ((uint64)*bitDepth == 16 || (uint64)*bitDepth == 24))
                    {
                        track.mFormat = (uint32)*bitDepth == 16 ? int16Sample : int24Sample;
                        audioTracks.push_back(track);
                    }
                }
                else if ((const std::string &) CodedId == "A_PCM/FLOAT/IEEE")
                {
                    if (bitDepth != nullptr && (uint64)*bitDepth == 32)
                    {
                        track.mFormat = floatSample;
                        audioTracks.push_back(track);
                    }
                }

                if (track.mFormat != (sampleFormat)0)
                {
                    auto strinfo = XO("Index[%02zx] Track Number[%u], Codec[%s], Channels[%d], Rate[%.0f]")
                        .Format(
                        audioTracks.size(),
                        track.mTrackNumber,
                        CodedId,
                        track.mChannels,
                        track.mRate);
                    mStreamInfo.push_back(strinfo);
                }
            }
        }
        elt = FindNextChild<KaxTrackEntry>(*Tracks, *elt);
    }
}

const TranslatableStrings & MkaImportFileHandle::GetStreamInfo()
{
    return mStreamInfo;
}

void MkaImportFileHandle::SetStreamUsage(wxInt32 StreamID, bool Use)
{
    audioTracks.at(StreamID).mSelected = Use;
}

ProgressResult MkaImportFileHandle::Import(WaveTrackFactory *trackFactory, TrackHolders &outTracks, Tags *tags)
{
    outTracks.clear();

    CreateProgress();

    for (auto &audioTrack : audioTracks)
    {
        if (audioTrack.mSelected)
        {
            audioTrack.importChannels.resize(audioTrack.mChannels);
            for (auto &channel : audioTrack.importChannels)
            {
                channel = NewWaveTrack(*trackFactory, audioTrack.mFormat, audioTrack.mRate);
                if (!audioTrack.mName.empty())
                    channel->SetName(audioTrack.mName);
            }
        }
    }

    const auto & TimestampUnit = GetChild<KaxTimecodeScale>(*SegmentInfo.get());

    // TODO handle text subtitle tracks as markers with start/stop values

    // load clusters content and put the content in "channels" using stride in Append()
    int UpperElementLevel = 0;
    EbmlElement *elt;
    for (;;)
    {
        EbmlElement *found = nullptr;
        assert(UpperElementLevel == 0);
        Cluster->SetParent(*Segment.get());

        Cluster->Read(*stream, EBML_CONTEXT(Cluster), UpperElementLevel, found, true);
        assert(found == nullptr);
        assert(UpperElementLevel == 0);

        ProgressResult res = mProgress->Update(
            static_cast<wxULongLong_t>(Cluster->GetElementPosition() - Segment->GetElementPosition()),
            static_cast<wxULongLong_t>(Segment->GetEndPosition()));
        if (res != ProgressResult::Success)
        {
            return res;
        }

        if (!Cluster->CheckMandatory())
        {
            wxLogWarning(wxT("Matroska : Cluster in %s at %lld missing mandatory data, skipping"),
                             mFilename, Cluster->GetElementPosition());
            Cluster->SkipData(*stream, EBML_CONTEXT(Segment));
        }
        else
        {
            const auto & ClusterTimecode = GetChild<KaxClusterTimecode>(*Cluster.get());
            Cluster->InitTimecode( static_cast<uint64>( ClusterTimecode ), static_cast<uint64>( TimestampUnit ) );

            // we need to set the parent before we can check the checksum
            for (const auto& child : *Cluster)
            {
                if (EbmlId(*child) == EBML_ID(KaxBlockGroup)) {
                    static_cast<KaxBlockGroup   *>(child)->SetParent(*Cluster);
                } else if (EbmlId(*child) == EBML_ID(KaxSimpleBlock)) {
                    static_cast<KaxSimpleBlock *>(child)->SetParent(*Cluster);
                }
            }

#ifndef LIBEBML_DEBUG // FIXME with LIBEBML_DEBUG KaxSimpleBlock asserts even though the code is fine
            if (!Cluster->VerifyChecksum())
                wxLogWarning(wxT("Matroska : Cluster in %s at %lld has bogus checksum, using anyway"),
                                 mFilename, Cluster->GetElementPosition());
#endif

            for (const auto& child : *Cluster)
            {
                /* TODO const*/ KaxInternalBlock * sblock = nullptr;
                if (EbmlId(*child) == EBML_ID(KaxSimpleBlock))
                {
                    sblock = static_cast<KaxSimpleBlock *>(child);
                }
                else if (EbmlId(*child) == EBML_ID(KaxBlockGroup))
                {
                    /* TODO const*/ KaxBlockGroup & blockGroup = *static_cast<KaxBlockGroup *>(child);
                    sblock = FindChild<KaxBlock>(blockGroup);
                }

                if (sblock != nullptr)
                {
                    for (const auto & tk : audioTracks)
                    {
                        if (tk.mTrackNumber == sblock->TrackNum())
                        {
                            if (tk.mSelected)
                            {
                                for (unsigned i = 0; i < sblock->NumberFrames() ; i++)
                                {
                                    auto & buffer = sblock->GetBuffer(i);
                                    const size_t samples = buffer.Size() / tk.bytesPerSample / tk.mChannels;
                                    wxASSERT(samples * tk.bytesPerSample * tk.mChannels == buffer.Size());
                                    // FIXME generate missing samples on gaps
                                    for (unsigned chn = 0; chn < tk.mChannels; chn++)
                                    {
                                        auto trackStart = (constSamplePtr)(buffer.Buffer()) + tk.bytesPerSample * chn;
                                        tk.importChannels[chn]->Append(trackStart, tk.mFormat, samples, tk.mChannels);
                                    }
                                }
                            }
                            break; // audioTracks
                        }
                    }
                }
            }
        }

        elt = stream->FindNextElement(EBML_CONTEXT(Segment), UpperElementLevel, Segment->GetSize(), true);
        if (elt == nullptr)
        {
            // end of the Segment
            // TODO support concatenated segments
            break;
        }
        if (EbmlId(*elt) != EBML_ID(KaxCluster))
        {
            // assume we can't have top level element between clusters
            delete elt;
            break;
        }

        Cluster = std::unique_ptr<KaxCluster>(static_cast<KaxCluster*>(elt));
    }

    for (const auto tk : audioTracks)
    {
        if (tk.mSelected)
        {
            for (auto &channel : tk.importChannels)
                channel->Flush();
            outTracks.push_back(std::move(tk.importChannels));
        }
    }

    // load tags
    if (!mTags.get() && SeekHead.get())
    {
        // try to find tags in the SeekHead
        KaxTags *tags = SeekHeadLoad<KaxTags>(*SeekHead, *Segment, *stream);
        mTags = std::unique_ptr<KaxTags>(tags);
    }
    if (mTags.get() && mTags->CheckMandatory())
    {
        for (const auto& _tag : *mTags)
        {
            if (EbmlId(*_tag) == EBML_ID(KaxTag))
            {
                KaxTag & tag = *static_cast<KaxTag*>(_tag);
                auto & targets = GetChild<KaxTagTargets>(tag);
                if (FindChild<KaxTagTrackUID>(targets))
                    continue; // TODO support naming the track
                if (FindChild<KaxTagEditionUID>(targets))
                    continue; // TODO support naming the marker track
                if (FindChild<KaxTagChapterUID>(targets))
                    continue; // TODO support naming markers
                if (FindChild<KaxTagAttachmentUID>(targets))
                    continue;
                auto & TypeValue = GetChild<KaxTagTargetTypeValue>(targets);
                // TODO allow selecting the language(s), for now pick the first one
                auto & simpleTag = GetChild<KaxTagSimple>(tag);
                auto & mkaName = GetChild<KaxTagName>(simpleTag);
                auto & tagName = GetChild<KaxTagString>(simpleTag);

                if ((uint64)TypeValue == MATROSKA_TARGET_TYPE_TRACK && mkaName.GetValue() == UTFstring(L"TITLE"))
                    tags->SetTag(TAG_TITLE, tagName.GetValue().c_str());
                else if ((uint64)TypeValue == MATROSKA_TARGET_TYPE_TRACK && mkaName.GetValue() == UTFstring(L"GENRE"))
                    tags->SetTag(TAG_GENRE, tagName.GetValue().c_str());
                else if ((uint64)TypeValue == MATROSKA_TARGET_TYPE_ALBUM && mkaName.GetValue() == UTFstring(L"ARTIST"))
                    tags->SetTag(TAG_ARTIST, tagName.GetValue().c_str());
                else if ((uint64)TypeValue == MATROSKA_TARGET_TYPE_ALBUM && mkaName.GetValue() == UTFstring(L"TITLE"))
                    tags->SetTag(TAG_ALBUM, tagName.GetValue().c_str());
                else if ((uint64)TypeValue == MATROSKA_TARGET_TYPE_ALBUM && mkaName.GetValue() == UTFstring(L"PART_NUMBER"))
                    tags->SetTag(TAG_TRACK, tagName.GetValue().c_str());
                else if ((uint64)TypeValue == MATROSKA_TARGET_TYPE_ALBUM && mkaName.GetValue() == UTFstring(L"DATE_RELEASED"))
                    tags->SetTag(TAG_YEAR, tagName.GetValue().c_str());
                else if ((uint64)TypeValue == MATROSKA_TARGET_TYPE_ALBUM && mkaName.GetValue() == UTFstring(L"COMMENT"))
                    tags->SetTag(TAG_COMMENTS, tagName.GetValue().c_str());
                else if ((uint64)TypeValue == MATROSKA_TARGET_TYPE_ALBUM && mkaName.GetValue() == UTFstring(L"COPYRIGHT"))
                    tags->SetTag(TAG_COPYRIGHT, tagName.GetValue().c_str());
            }
        }
    }

    // TODO load markers from chapters

    return ProgressResult::Success;
}

static Importer::RegisteredImportPlugin registered {
   "Matroska", std::make_unique<MkaImportPlugin>()
};

#else // !USE_LIUSE_LIBMATROSKABFLAC

static Importer::RegisteredUnusableImportPlugin registered{
      std::make_unique<UnusableImportPlugin>
         (DESC, FileExtensions( exts.begin(), exts.end() ) )
};

#endif // USE_LIBMATROSKA
