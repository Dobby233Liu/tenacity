// SPDX-License-Identifier: GPL-2.0-or-later
/**********************************************************************

  Tenacity: A Digital Audio Editor

  ExportMka.cpp

  Steve Lhomme

**********************************************************************/

#ifdef USE_LIBMATROSKA

#include "Export.h"
#include "../Tags.h"
#include "../Track.h"

// Tenacity libraries
#include <lib-files/wxFileNameWrapper.h>
#include <lib-string-utils/CodeConversions.h>
#include <lib-project-rate/ProjectRate.h>

#include "TenacityHeaders.h"

#if defined(_CRTDBG_MAP_ALLOC) && LIBMATROSKA_VERSION < 0x010702
// older libmatroska headers use std::nothrow which is incompatible with <crtdbg.h>
#undef new
#endif

#include <ebml/EbmlHead.h>
#include <ebml/EbmlSubHead.h>
#include <ebml/EbmlVoid.h>
#include <ebml/StdIOCallback.h>
#include <matroska/KaxSegment.h>
#include <matroska/KaxSeekHead.h>
#include <matroska/KaxCluster.h>
#include <matroska/KaxVersion.h>

#if LIBMATROSKA_VERSION < 0x010700
typedef enum {
  MATROSKA_TARGET_TYPE_COLLECTION       = 70, // The highest hierarchical level that tags can describe.
  MATROSKA_TARGET_TYPE_EDITION          = 60, // A list of lower levels grouped together.
  MATROSKA_TARGET_TYPE_ALBUM            = 50, // The most common grouping level of music and video (equals to an episode for TV series).
  MATROSKA_TARGET_TYPE_PART             = 40, // When an album or episode has different logical parts.
  MATROSKA_TARGET_TYPE_TRACK            = 30, // The common parts of an album or movie.
  MATROSKA_TARGET_TYPE_SUBTRACK         = 20, // Corresponds to parts of a track for audio (like a movement).
  MATROSKA_TARGET_TYPE_SHOT             = 10, // The lowest hierarchy found in music or movies.
} MatroskaTargetTypeValue;
#endif

using namespace libmatroska;
using namespace Tenacity;

class ExportMKAOptions final : public wxPanelWrapper
{
public:

    ExportMKAOptions(wxWindow *parent, int format);
    virtual ~ExportMKAOptions();

    void PopulateOrExchange(ShuttleGui & S);
    bool TransferDataToWindow() override;
    bool TransferDataFromWindow() override;
};

ExportMKAOptions::ExportMKAOptions(wxWindow *parent, int WXUNUSED(format))
:  wxPanelWrapper(parent, wxID_ANY)
{
    ShuttleGui S(this, eIsCreatingFromPrefs);
    PopulateOrExchange(S);

    TransferDataToWindow();
}

ExportMKAOptions::~ExportMKAOptions()
{
    TransferDataFromWindow();
}

ChoiceSetting MKAFormat {
    wxT("/FileFormats/MKAFormat"),
    {
        ByColumns,
        {
          XO("PCM 16-bit (Little Endian)") ,
          XO("PCM 24-bit (Little Endian)") ,
          XO("PCM Float 32-bit") ,
        },
        {
          wxT("16"),
          wxT("24"),
          wxT("f32"),
        }
    },
};

void ExportMKAOptions::PopulateOrExchange(ShuttleGui & S)
{
    S.StartVerticalLay();
    {
        S.StartHorizontalLay(wxCENTER);
        {
            S.StartMultiColumn(2, wxCENTER);
            {
                S.TieChoice( XXO("Bit depth:"), MKAFormat);
                // TODO select OK of the dialog by default ?
                // TODO select the language to use for strings (app, und, or a list ?)
                // TODO select the emphasis type
                S.TieCheckBox(XXO("Keep Labels"), {wxT("/FileFormats/MkaExportLabels"), true});
            }
            S.EndMultiColumn();
        }
        S.EndHorizontalLay();
    }
    S.EndVerticalLay();

    return;
}

bool ExportMKAOptions::TransferDataToWindow()
{
    return true;
}

bool ExportMKAOptions::TransferDataFromWindow()
{
    ShuttleGui S(this, eIsSavingToPrefs);
    PopulateOrExchange(S);

    gPrefs->Flush();

    return true;
}


class ExportMka final : public ExportPlugin
{
public:
    ExportMka();

    void OptionsCreate(ShuttleGui &S, int format) override;

    ProgressResult Export(TenacityProject *project,
                                std::unique_ptr<ProgressDialog> &pDialog,
                                unsigned channels,
                                const wxFileNameWrapper &fName,
                                bool selectedOnly,
                                double t0,
                                double t1,
                                MixerSpec *mixerSpec,
                                const Tags *metadata,
                                int subformat) override;
};

ExportMka::ExportMka()
:  ExportPlugin()
{
    std::srand(std::time(nullptr));
    AddFormat();
    SetFormat(wxT("MKA"),0);
    AddExtension(wxT("mka"),0);
    SetCanMetaData(true,0);
    SetDescription(XO("Matroska Audio Files"),0);
}

void ExportMka::OptionsCreate(ShuttleGui &S, int format)
{
    S.AddWindow( safenew ExportMKAOptions{ S.GetParent(), format } );
}

static uint64_t GetRandomUID64()
{
    uint64_t uid = 0;
    for (size_t i=0; i<(64-7); i+=7)
    {
        auto r = static_cast<uint64_t>(std::rand() & 0x7fff);
        uid = uid << 7 | r;
    }
    return uid;
}

static void FillRandomUUID(binary UID[16])
{
    uint64_t rand;
    rand = GetRandomUID64();
    memcpy(&UID[0], &rand, 8);
    rand = GetRandomUID64();
    memcpy(&UID[8], &rand, 8);
}

static void FinishFrameBlock(std::unique_ptr<KaxBlockBlob> & framesBlob, KaxCluster & Cluster,
                                      const size_t lessSamples, const double rate, const uint64 TimestampScale)
{
    if (framesBlob)
    {
        if (lessSamples != 0)
        {
            // last block, write the duration
            // TODO get frames from the blob and add them in a BLOCK_BLOB_NO_SIMPLE one
            // framesBlob->SetBlockDuration(lessSamples * TimestampScale / rate);
        }
        Cluster.AddBlockBlob(framesBlob.release());
    }
}

static void SetMetadata(const Tags *tags, KaxTag * & PrevTag, KaxTags & Tags, const wxChar *tagName, const MatroskaTargetTypeValue TypeValue, const wchar_t *mkaName)
{
    if (tags != nullptr && tags->HasTag(tagName))
    {
        KaxTag &tag = AddNewChild<KaxTag>(Tags);
        KaxTagTargets &tagTarget = GetChild<KaxTagTargets>(tag);
        (EbmlUInteger &) GetChild<KaxTagTargetTypeValue>(tagTarget) = TypeValue;

        KaxTagSimple &simpleTag = GetChild<KaxTagSimple>(tag);
        (EbmlUnicodeString &) GetChild<KaxTagName>(simpleTag) = (UTFstring)mkaName;
        (EbmlUnicodeString &) GetChild<KaxTagString>(simpleTag) = (UTFstring)tags->GetTag(tagName);
    }
}


ProgressResult ExportMka::Export(TenacityProject *project,
                                std::unique_ptr<ProgressDialog> &pDialog,
                                unsigned numChannels,
                                const wxFileNameWrapper &fName,
                                bool selectionOnly,
                                double t0,
                                double t1,
                                MixerSpec *mixerSpec,
                                const Tags *metadata,
                                int subformat)
{
    auto bitDepthPref = MKAFormat.Read();
    auto updateResult = ProgressResult::Success;

    const wxString url = fName.GetAbsolutePath(wxString(), wxPATH_NATIVE);
    try
    {
        StdIOCallback mka_file(url, ::MODE_CREATE);

        InitProgress( pDialog, fName,
            selectionOnly
                ? XO("Exporting the selected audio as MKA")
                : XO("Exporting the audio as MKA") );
        auto &progress = *pDialog;

        EbmlHead FileHead;
        (EbmlString &) GetChild<EDocType>(FileHead) = "matroska";
#if LIBMATROSKA_VERSION >= 0x010406
        (EbmlUInteger &) GetChild<EDocTypeVersion>(FileHead) = 4; // needed for LanguageBCP47
#else
        (EbmlUInteger &) GetChild<EDocTypeVersion>(FileHead) = 2;
#endif
        (EbmlUInteger &) GetChild<EDocTypeReadVersion>(FileHead) = 2; // needed for SimpleBlock
        (EbmlUInteger &) GetChild<EMaxIdLength>(FileHead) = 4;
        (EbmlUInteger &) GetChild<EMaxSizeLength>(FileHead) = 8;
        FileHead.Render(mka_file, true);

        KaxSegment FileSegment;
        auto SegmentSize = FileSegment.WriteHead(mka_file, 5);

        // reserve some space for the Meta Seek writen at the end
        EbmlVoid DummyStart;
        DummyStart.SetSize(128);
        DummyStart.Render(mka_file);

        KaxSeekHead MetaSeek;
        MetaSeek.EnableChecksum();

        const auto &tracks = TrackList::Get( *project );
        const double rate = ProjectRate::Get( *project ).GetRate();
        const uint64 TIMESTAMP_UNIT = std::llround(UINT64_C(1000000000) / rate);
        // const uint64 TIMESTAMP_UNIT = 1000000; // 1 ms
        constexpr uint64 MS_PER_FRAME = 40;

        EbmlMaster & MyInfos = GetChild<KaxInfo>(FileSegment);
        MyInfos.EnableChecksum();
        (EbmlFloat &) GetChild<KaxDuration>(MyInfos) = (t1 - t0) * UINT64_C(1000000000) / TIMESTAMP_UNIT; // in TIMESTAMP_UNIT
        GetChild<KaxDuration>(MyInfos).SetPrecision(EbmlFloat::FLOAT_64);
        (EbmlUnicodeString &) GetChild<KaxMuxingApp>(MyInfos)  = ToWString(std::string("libebml ") + EbmlCodeVersion + std::string(" + libmatroska ") + KaxCodeVersion);
        (EbmlUnicodeString &) GetChild<KaxWritingApp>(MyInfos) = ToWString(APP_NAME) + L" " + AUDACITY_VERSION_STRING;
        (EbmlUInteger &) GetChild<KaxTimecodeScale>(MyInfos) = TIMESTAMP_UNIT;
        GetChild<KaxDateUTC>(MyInfos).SetEpochDate(time(nullptr));
        binary SegUID[16];
        FillRandomUUID(SegUID);
        GetChild<KaxSegmentUID>(MyInfos).CopyBuffer(SegUID, 16);
        filepos_t InfoSize = MyInfos.Render(mka_file);
        if (InfoSize != 0)
            MetaSeek.IndexThis(MyInfos, FileSegment);

        KaxTracks & MyTracks = GetChild<KaxTracks>(FileSegment);
        MyTracks.EnableChecksum();

        sampleFormat format;
        uint64 bytesPerSample;
        if (bitDepthPref == wxT("24"))
        {
            format = int24Sample;
            bytesPerSample = 3 * numChannels;
        }
        else if (bitDepthPref == wxT("f32"))
        {
            format = floatSample;
            bytesPerSample = 4 * numChannels;
        }
        else
        {
            format = int16Sample;
            bytesPerSample = 2 * numChannels;
        }

        // TODO support multiple tracks
        KaxTrackEntry & MyTrack1 = GetChild<KaxTrackEntry>(MyTracks);
        MyTrack1.SetGlobalTimecodeScale(TIMESTAMP_UNIT);

        (EbmlUInteger &) GetChild<KaxTrackType>(MyTrack1) = MATROSKA_TRACK_TYPE_AUDIO;
        (EbmlUInteger &) GetChild<KaxTrackNumber>(MyTrack1) = 1;
        (EbmlUInteger &) GetChild<KaxTrackUID>(MyTrack1) = GetRandomUID64();
        (EbmlUInteger &) GetChild<KaxTrackDefaultDuration>(MyTrack1) = MS_PER_FRAME * 1000000;
        (EbmlString &) GetChild<KaxTrackLanguage>(MyTrack1) = "und";
#if LIBMATROSKA_VERSION >= 0x010406
        (EbmlString &) GetChild<KaxLanguageIETF>(MyTrack1) = "und";
#endif
        auto waveTracks = tracks.Selected< const WaveTrack >();
        auto pT = waveTracks.begin();
        if (*pT)
        {
            const auto sTrackName = (*pT)->GetName();
            if (!sTrackName.empty() && sTrackName != (*pT)->GetDefaultName())
                (EbmlUnicodeString &) GetChild<KaxTrackName>(MyTrack1) = (UTFstring)sTrackName;
        }

        EbmlMaster & MyTrack1Audio = GetChild<KaxTrackAudio>(MyTrack1);
        (EbmlFloat &) GetChild<KaxAudioSamplingFreq>(MyTrack1Audio) = rate;
        (EbmlUInteger &) GetChild<KaxAudioChannels>(MyTrack1Audio) = numChannels;
        switch(format)
        {
            case int16Sample:
                (EbmlString &) GetChild<KaxCodecID>(MyTrack1) = "A_PCM/INT/LIT";
                (EbmlUInteger &) GetChild<KaxAudioBitDepth>(MyTrack1Audio) = 16;
                break;
            case int24Sample:
                (EbmlString &) GetChild<KaxCodecID>(MyTrack1) = "A_PCM/INT/LIT";
                (EbmlUInteger &) GetChild<KaxAudioBitDepth>(MyTrack1Audio) = 24;
                break;
            case floatSample:
                (EbmlString &) GetChild<KaxCodecID>(MyTrack1) = "A_PCM/FLOAT/IEEE";
                (EbmlUInteger &) GetChild<KaxAudioBitDepth>(MyTrack1Audio) = 32;
                break;
        }
        filepos_t TrackSize = MyTracks.Render(mka_file);
        if (TrackSize != 0)
            MetaSeek.IndexThis(MyTracks, FileSegment);

        // reserve some space after the track (to match mkvmerge for now)
        // EbmlVoid DummyTrack;
        // DummyTrack.SetSize(1068);
        // DummyTrack.Render(mka_file);

        // add tags
        KaxTags & Tags = GetChild<KaxTags>(FileSegment);
        Tags.EnableChecksum();
        if (metadata == nullptr)
            metadata = &Tags::Get( *project );
        KaxTag *prevTag = nullptr;
        SetMetadata(metadata, prevTag, Tags, TAG_TITLE,     MATROSKA_TARGET_TYPE_TRACK, L"TITLE");
        SetMetadata(metadata, prevTag, Tags, TAG_GENRE,     MATROSKA_TARGET_TYPE_TRACK, L"GENRE");
        SetMetadata(metadata, prevTag, Tags, TAG_ARTIST,    MATROSKA_TARGET_TYPE_ALBUM, L"ARTIST");
        SetMetadata(metadata, prevTag, Tags, TAG_ALBUM,     MATROSKA_TARGET_TYPE_ALBUM, L"TITLE");
        SetMetadata(metadata, prevTag, Tags, TAG_TRACK,     MATROSKA_TARGET_TYPE_ALBUM, L"PART_NUMBER");
        SetMetadata(metadata, prevTag, Tags, TAG_YEAR,      MATROSKA_TARGET_TYPE_ALBUM, L"DATE_RELEASED");
        SetMetadata(metadata, prevTag, Tags, TAG_COMMENTS,  MATROSKA_TARGET_TYPE_ALBUM, L"COMMENT");
        SetMetadata(metadata, prevTag, Tags, TAG_COPYRIGHT, MATROSKA_TARGET_TYPE_ALBUM, L"COPYRIGHT");
        filepos_t TagsSize = Tags.Render(mka_file);
        if (TagsSize != 0)
            MetaSeek.IndexThis(Tags, FileSegment);

        KaxCues AllCues;
        AllCues.SetGlobalTimecodeScale(TIMESTAMP_UNIT);
        AllCues.EnableChecksum();

        const size_t maxFrameSamples = MS_PER_FRAME * rate / 1000; // match mkvmerge
        auto mixer = CreateMixer(tracks, selectionOnly,
                                        t0, t1,
                                        numChannels, maxFrameSamples * bytesPerSample, true,
                                        rate, format, mixerSpec);

        // add clusters
        std::unique_ptr<KaxCluster> Cluster;
        std::unique_ptr<KaxBlockBlob> framesBlob;

        uint64 prevEndTime = 0;
        uint64_t samplesRead = 0; // TODO handle selection starting at t0
        uint64_t clusterSamplesWritten;
        while (updateResult == ProgressResult::Success) {
            auto samplesThisRun = mixer->Process(maxFrameSamples);
            if (samplesThisRun == 0)
// || samplesRead / 48000 > 3) // test first 3s
            {
                if (Cluster)
                {
                    FinishFrameBlock(framesBlob, *Cluster, maxFrameSamples - samplesThisRun, rate, TIMESTAMP_UNIT);
                    Cluster->Render(mka_file, AllCues);
                    MetaSeek.IndexThis(*Cluster, FileSegment);
                    Cluster = nullptr;
                }
                break; //finished
            }

            if (!Cluster)
            {
                Cluster = std::make_unique<KaxCluster>();
                Cluster->SetParent(FileSegment); // mandatory to store references in this Cluster
                Cluster->InitTimecode(prevEndTime, TIMESTAMP_UNIT);
                Cluster->EnableChecksum();
                clusterSamplesWritten = 0;
                assert(!framesBlob);
                framesBlob = std::make_unique<KaxBlockBlob>(BLOCK_BLOB_SIMPLE_AUTO);
                framesBlob->SetParent(*Cluster);
                AllCues.AddBlockBlob(*framesBlob);
            }
            else if (!framesBlob)
            {
                framesBlob = std::make_unique<KaxBlockBlob>(BLOCK_BLOB_SIMPLE_AUTO);
                framesBlob->SetParent(*Cluster);
            }

            samplePtr mixed = mixer->GetBuffer();
            DataBuffer *dataBuff = new DataBuffer((binary*)mixed, samplesThisRun * bytesPerSample, nullptr, true);

            if (!framesBlob->AddFrameAuto(MyTrack1, prevEndTime * TIMESTAMP_UNIT, *dataBuff))
            {
                // last frame allowed in the lace, we need a new frame blob
                FinishFrameBlock(framesBlob, *Cluster, maxFrameSamples - samplesThisRun, rate, TIMESTAMP_UNIT);
            }

            samplesRead += samplesThisRun;
            // in rounded TIMESTAMP_UNIT as the drift with the actual time accumulates
            prevEndTime = std::llround(samplesRead * 1000000000. / (TIMESTAMP_UNIT * rate));
            clusterSamplesWritten += samplesThisRun;
            updateResult = progress.Update(mixer->MixGetCurrentTime() - t0, t1 - t0);

            if (clusterSamplesWritten >= (uint64)(18 * maxFrameSamples)) // match mkvmerge: 18 blocks per clusters
            {
                FinishFrameBlock(framesBlob, *Cluster, maxFrameSamples - samplesThisRun, rate, TIMESTAMP_UNIT);
                Cluster->Render(mka_file, AllCues);
                MetaSeek.IndexThis(*Cluster, FileSegment);
                Cluster = nullptr;
            }
        }

        // add cues
        filepos_t CueSize = AllCues.Render(mka_file);
        if (CueSize != 0)
            MetaSeek.IndexThis(AllCues, FileSegment);

        uint64 lastElementEnd = AllCues.GetEndPosition();

        // add markers as chapters
        if (gPrefs->Read(wxT("/FileFormats/MkaExportLabels"), true))
        {
            const auto trackRange = tracks.Any<const LabelTrack>();
            if (!trackRange.empty())
            {
                KaxChapters & EditionList = GetChild<KaxChapters>(FileSegment);
                for (const auto *lt : trackRange)
                {
                    if (lt->GetNumLabels())
                    {
                        // Create an edition with the track name
                        KaxEditionEntry &Edition = AddNewChild<KaxEditionEntry>(EditionList);
                        (EbmlUInteger &) GetChild<KaxEditionUID>(Edition) = GetRandomUID64();
                        if (!lt->GetName().empty() && lt->GetName() != lt->GetDefaultName())
                        {
#if LIBMATROSKA_VERSION >= 0x010700
                            KaxEditionDisplay & EditionDisplay = GetChild<KaxEditionDisplay>(Edition);
                            (EbmlUnicodeString &) GetChild<KaxEditionString>(EditionDisplay) = (UTFstring)lt->GetName();
#endif
                            // TODO also write the Edition name in tags for older Matroska parsers
                        }

                        // Add markers and selections
                        for (const auto & label : lt->GetLabels())
                        {
                            KaxChapterAtom & Chapter = AddNewChild<KaxChapterAtom>(Edition);
                            (EbmlUInteger &) GetChild<KaxChapterUID>(Chapter) = GetRandomUID64();
                            (EbmlUInteger &) GetChild<KaxChapterTimeStart>(Chapter) = label.getT0() * UINT64_C(1000000000);
                            if (label.getDuration() != 0.0)
                                (EbmlUInteger &) GetChild<KaxChapterTimeEnd>(Chapter) = label.getT1() * UINT64_C(1000000000);
                            if (!label.title.empty())
                            {
                                KaxChapterDisplay & ChapterDisplay = GetChild<KaxChapterDisplay>(Chapter);
                                (EbmlUnicodeString &) GetChild<KaxChapterString>(ChapterDisplay) = (UTFstring)label.title;
                                (EbmlString &) GetChild<KaxChapterLanguage>(ChapterDisplay) = "und";
#if LIBMATROSKA_VERSION >= 0x010600
                                (EbmlString &) GetChild<KaxChapLanguageIETF>(ChapterDisplay) = "und";
#endif
                            }
                        }
                    }
                }
                filepos_t ChaptersSize = EditionList.Render(mka_file);
                if (ChaptersSize != 0)
                {
                    MetaSeek.IndexThis(EditionList, FileSegment);
                    lastElementEnd = EditionList.GetEndPosition();
                }
            }
        }

        auto MetaSeekSize = DummyStart.ReplaceWith(MetaSeek, mka_file);
        if (MetaSeekSize == INVALID_FILEPOS_T)
        {
            // writing at the beginning failed, write at the end and provide a
            // short metaseek at the front
            MetaSeek.Render(mka_file);
            lastElementEnd = MetaSeek.GetEndPosition();

            KaxSeekHead ShortMetaSeek;
            ShortMetaSeek.EnableChecksum();
            ShortMetaSeek.IndexThis(MetaSeek, FileSegment);
            MetaSeekSize = DummyStart.ReplaceWith(ShortMetaSeek, mka_file);
        }

        if (FileSegment.ForceSize(lastElementEnd - FileSegment.GetDataStart()))
        {
            FileSegment.OverwriteHead(mka_file);
        }

    } catch (const libebml::CRTError &) {
        updateResult = ProgressResult::Failed;
    } catch (const std::bad_alloc &) {
        updateResult = ProgressResult::Failed;
    }

    return updateResult;
}

static Exporter::RegisteredExportPlugin sRegisteredPlugin{ "Matroska",
    []{ return std::make_unique< ExportMka >(); }
};

#endif // USE_LIBMATROSKA
