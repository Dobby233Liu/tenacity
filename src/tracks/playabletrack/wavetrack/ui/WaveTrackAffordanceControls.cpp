/*!********************************************************************
*
 Audacity: A Digital Audio Editor

 WaveTrackAffordanceControls.cpp

 Vitaly Sverchinsky

 **********************************************************************/

#include "WaveTrackAffordanceControls.h"

#include <wx/dc.h>
#include <wx/frame.h>

#include "../../../../theme/AllThemeResources.h"
#include "../../../../commands/CommandContext.h"
#include "../../../../commands/CommandFlag.h"
#include "../../../../commands/CommandFunctors.h"
#include "../../../../commands/CommandManager.h"
#include "../../../../TrackPanelMouseEvent.h"
#include "../../../../TrackArtist.h"
#include "../../../../TrackPanelDrawingContext.h"
#include "../../../../TrackPanelResizeHandle.h"
#include "../../../../ViewInfo.h"
#include "../../../../WaveTrack.h"
#include "../../../../WaveClip.h"
#include "../../../../UndoManager.h"
#include "../../../../shuttle/ShuttleGui.h"
#include "../../../../Project.h"
#include "../../../../ProjectWindow.h"
#include "../../../../commands/AudacityCommand.h"
#include "../../../ui/AffordanceHandle.h"
#include "../../../ui/TextEditHelper.h"
#include "WaveTrackView.h"//need only ClipParameters

#include "../../../../ProjectHistory.h"
#include "../../../../SelectionState.h"
#include "../../../../RefreshCode.h"
#include "../../../../theme/Theme.h"
#include "../../../../images/Cursors.h"
#include "../../../../HitTestResult.h"
#include "../../../../TrackPanel.h"
#include "../../../../TrackPanelAx.h"

#include "../WaveTrackUtils.h"

#include "WaveClipTrimHandle.h"

class WaveTrackAffordanceHandle final : public AffordanceHandle
{
    std::shared_ptr<WaveClip> mTarget;
public:
    WaveTrackAffordanceHandle(const std::shared_ptr<Track>& track, const std::shared_ptr<WaveClip>& target) 
        : AffordanceHandle(track), mTarget(target)
    { }

    Result Click(const TrackPanelMouseEvent& event, SaucedacityProject* project) override
    {
        auto affordanceControl = std::dynamic_pointer_cast<WaveTrackAffordanceControls>(event.pCell);
        Result result = RefreshCode::RefreshNone;
        if (affordanceControl)
        {
            result |= affordanceControl->OnAffordanceClick(event, project);
            if (!event.event.GetSkipped())
                return result;
            event.event.Skip(false);
        }
        return result | AffordanceHandle::Click(event, project);
    }

    UIHandle::Result SelectAt(const TrackPanelMouseEvent& event, SaucedacityProject* project) override
    {
        auto& viewInfo = ViewInfo::Get(*project);
        viewInfo.selectedRegion.setTimes(mTarget->GetPlayStartTime(), mTarget->GetPlayEndTime());
        
        ProjectHistory::Get(*project).ModifyState(false);
        
        return RefreshCode::RefreshAll | RefreshCode::Cancelled;
    }
};

class SetWaveClipNameCommand : public AudacityCommand
{
public:
    static const ComponentInterfaceSymbol Symbol;

    ComponentInterfaceSymbol GetSymbol() override
    {
        return Symbol;
    }
    void PopulateOrExchange(ShuttleGui& S) override
    {
        S.AddSpace(0, 5);

        S.StartMultiColumn(2, wxALIGN_CENTER);
        {
            S.TieTextBox(XXO("Name:"), mName, 60);
        }
        S.EndMultiColumn();
    }
public:
    wxString mName;
};

const ComponentInterfaceSymbol SetWaveClipNameCommand::Symbol
{ XO("Set Wave Clip Name") };

//Handle which is used to send mouse events to TextEditHelper
class WaveClipTitleEditHandle final : public UIHandle
{
    std::shared_ptr<TextEditHelper> mHelper;
public:

    WaveClipTitleEditHandle(const std::shared_ptr<TextEditHelper>& helper)
        : mHelper(helper)
    { }
   
   ~WaveClipTitleEditHandle()
   {
   }

    Result Click(const TrackPanelMouseEvent& event, SaucedacityProject* project) override
    {
        if (mHelper->OnClick(event.event, project))
            return RefreshCode::RefreshCell;
        return RefreshCode::RefreshNone;
    }

    Result Drag(const TrackPanelMouseEvent& event, SaucedacityProject* project) override
    {
        if (mHelper->OnDrag(event.event, project))
            return RefreshCode::RefreshCell;
        return RefreshCode::RefreshNone;
    }

    HitTestPreview Preview(const TrackPanelMouseState& state, SaucedacityProject* pProject) override
    {
        static auto ibeamCursor =
            ::MakeCursor(wxCURSOR_IBEAM, IBeamCursorXpm, 17, 16);
        return {
           XO("Click and drag to select text"),
           ibeamCursor.get()
        };
    }

    Result Release(const TrackPanelMouseEvent& event, SaucedacityProject* project, wxWindow*) override
    {
        if (mHelper->OnRelease(event.event, project))
            return RefreshCode::RefreshCell;
        return RefreshCode::RefreshNone;
    }

    Result Cancel(SaucedacityProject* project) override
    {
        if (mHelper)
        {
            mHelper->Cancel(project);
            mHelper.reset();
        }
        return RefreshCode::RefreshAll;
    }
};

WaveTrackAffordanceControls::WaveTrackAffordanceControls(const std::shared_ptr<Track>& pTrack)
    : CommonTrackCell(pTrack), mClipNameFont(wxFont(wxFontInfo()))
{
    if (auto trackList = pTrack->GetOwner())
    {
        trackList->Bind(EVT_TRACKLIST_SELECTION_CHANGE,
            &WaveTrackAffordanceControls::OnTrackChanged,
            this);
    }
}

std::vector<UIHandlePtr> WaveTrackAffordanceControls::HitTest(const TrackPanelMouseState& state, const SaucedacityProject* pProject)
{
    std::vector<UIHandlePtr> results;

    auto px = state.state.m_x;
    auto py = state.state.m_y;

    const auto rect = state.rect;

    const auto track = FindTrack();

    {
        auto handle = WaveClipTrimHandle::HitAnywhere(
            mClipTrimHandle,
            std::static_pointer_cast<WaveTrack>(track).get(),
            pProject,
            state);

        if (handle)
            results.push_back(handle);
    }

    auto trackList = track->GetOwner();
    if ((std::abs(rect.GetTop() - py) <= WaveTrackView::kChannelSeparatorThickness / 2) 
        && trackList
        && !track->IsLeader())
    {
        //given that track is not a leader there always should be
        //another track before this one
        auto prev = std::prev(trackList->Find(track.get()));
        results.push_back(
            AssignUIHandlePtr(
                mResizeHandle, 
                std::make_shared<TrackPanelResizeHandle>((*prev)->shared_from_this(), py)
            )
        );
    }

    if (mTextEditHelper && mTextEditHelper->GetBBox().Contains(px, py))
    {
        results.push_back(
            AssignUIHandlePtr(
                mTitleEditHandle,
                std::make_shared<WaveClipTitleEditHandle>(mTextEditHelper)
            )
        );
    }

    auto editClipLock = mEditedClip.lock();
    const auto waveTrack = std::static_pointer_cast<WaveTrack>(track->SubstitutePendingChangedTrack());
    auto& zoomInfo = ViewInfo::Get(*pProject);
    for (const auto& clip : waveTrack->GetClips())
    {
        if (clip == editClipLock)
            continue;

        auto affordanceRect = ClipParameters::GetClipRect(*clip.get(), zoomInfo, state.rect);
        if (affordanceRect.Contains(px, py))
        {
            results.push_back(
                AssignUIHandlePtr(
                    mAffordanceHandle,
                    std::make_shared<WaveTrackAffordanceHandle>(track, clip)
                )
            );
            mFocusClip = clip;
            break;
        }
    }

    return results;
}

void WaveTrackAffordanceControls::Draw(TrackPanelDrawingContext& context, const wxRect& rect, unsigned iPass)
{
    if (iPass == TrackArtist::PassBackground) {
        auto track = FindTrack();
        const auto artist = TrackArtist::Get(context);

        TrackArt::DrawBackgroundWithSelection(context, rect, track.get(), artist->blankSelectedBrush, artist->blankBrush);

        const auto waveTrack = std::static_pointer_cast<WaveTrack>(track->SubstitutePendingChangedTrack());
        const auto& zoomInfo = *artist->pZoomInfo;

        {
            wxDCClipper dcClipper(context.dc, rect);

            context.dc.SetTextBackground(wxTransparentColor);
            context.dc.SetTextForeground(theTheme.Colour(clrClipNameText));
            context.dc.SetFont(mClipNameFont);

            auto px = context.lastState.m_x;
            auto py = context.lastState.m_y;

            for (const auto& clip : waveTrack->GetClips())
            {
                auto affordanceRect
                    = ClipParameters::GetClipRect(*clip.get(), zoomInfo, rect);
                if (affordanceRect.IsEmpty())
                    continue;

                auto selected = GetSelectedClip().lock() == clip;
                auto highlight = selected || affordanceRect.Contains(px, py);
                if (mTextEditHelper && mEditedClip.lock() == clip)
                {
                    TrackArt::DrawClipAffordance(context.dc, affordanceRect, wxEmptyString, highlight, selected);
                    mTextEditHelper->Draw(context.dc, TrackArt::GetAffordanceTitleRect(affordanceRect));
                }
                else
                    TrackArt::DrawClipAffordance(context.dc, affordanceRect, clip->GetName(), highlight, selected);

            }
        }

    }
}

bool WaveTrackAffordanceControls::StartEditClipName(SaucedacityProject* project)
{
    if (auto lock = mFocusClip.lock())
    {
        auto clip = lock.get();

        bool useDialog{ false };
        gPrefs->Read(wxT("/GUI/DialogForNameNewLabel"), &useDialog, false);

        if (useDialog)
        {
            SetWaveClipNameCommand Command;
            auto oldName = clip->GetName();
            Command.mName = oldName;
            auto result = Command.PromptUser(&GetProjectFrame(*project));
            if (result && Command.mName != oldName)
            {
                clip->SetName(Command.mName);
                ProjectHistory::Get(*project).PushState(XO("Modified Clip Name"),
                    XO("Clip Name Edit"), UndoPush::CONSOLIDATE);

                return true;
            }
        }
        else
        {
            if (mTextEditHelper)
                mTextEditHelper->Finish(project);

            mEditedClip = lock;
            mTextEditHelper = MakeTextEditHelper(clip->GetName());
            return true;
        }
    }
    return false;
}

std::weak_ptr<WaveClip> WaveTrackAffordanceControls::GetSelectedClip() const
{
    if (auto handle = mAffordanceHandle.lock())
    {
        return handle->Clicked() ? mFocusClip : std::weak_ptr<WaveClip>();
    }
    return {};
}

namespace {

auto FindAffordance(WaveTrack &track)
{
   auto &view = TrackView::Get( track );
   auto pAffordance = view.GetAffordanceControls();
   return std::dynamic_pointer_cast<WaveTrackAffordanceControls>(
      pAffordance );
}

std::pair<WaveTrack *, WaveClip *>
SelectedClipOfFocusedTrack(SaucedacityProject &project)
{
   // Note that TrackFocus may change its state as a side effect, defining
   // a track focus if there was none
   if (auto pWaveTrack =
      dynamic_cast<WaveTrack *>(TrackFocus::Get(project).Get())) {
      for (auto pChannel : TrackList::Channels(pWaveTrack)) {
         if (FindAffordance(*pChannel)) {
            auto &viewInfo = ViewInfo::Get(project);
            auto &clips = pChannel->GetClips();
            auto begin = clips.begin(), end = clips.end(),
               iter = WaveTrackUtils::SelectedClip(viewInfo, begin, end);
            if (iter != end)
               return { pChannel, iter->get() };
         }
      }
   }
   return { nullptr, nullptr };
}

// condition for enabling the command
const ReservedCommandFlag &SomeClipIsSelectedFlag()
{
   static ReservedCommandFlag flag{
      [](const SaucedacityProject &project){
         return nullptr !=
            // const_cast isn't pretty but not harmful in this case
            SelectedClipOfFocusedTrack(const_cast<SaucedacityProject&>(project))
               .second;
      }
   };
   return flag;
}

}

unsigned WaveTrackAffordanceControls::CaptureKey(wxKeyEvent& event, ViewInfo& viewInfo, wxWindow* pParent, SaucedacityProject* project)
{
    if (!mTextEditHelper)
       // Handle the event if we are already editing clip name text...
       event.Skip();
    return RefreshCode::RefreshNone;
}


unsigned WaveTrackAffordanceControls::KeyDown(wxKeyEvent& event, ViewInfo& viewInfo, wxWindow*, SaucedacityProject* project)
{
    auto keyCode = event.GetKeyCode();
    
    if (mTextEditHelper)
    {
        mTextEditHelper->OnKeyDown(keyCode, event.GetModifiers(), project);
        if (!TextEditHelper::IsGoodEditKeyCode(keyCode))
            event.Skip();
        return RefreshCode::RefreshCell;
    }
    return RefreshCode::RefreshNone;
}

unsigned WaveTrackAffordanceControls::Char(wxKeyEvent& event, ViewInfo& viewInfo, wxWindow* pParent, SaucedacityProject* project)
{
    if (mTextEditHelper && mTextEditHelper->OnChar(event.GetUnicodeKey(), project))
        return RefreshCode::RefreshCell;
    return RefreshCode::RefreshNone;
}

unsigned WaveTrackAffordanceControls::LoseFocus(SaucedacityProject *)
{
   return ExitTextEditing();
}

void WaveTrackAffordanceControls::OnTextEditFinished(SaucedacityProject* project, const wxString& text)
{
    if (auto lock = mEditedClip.lock())
    {
        if (text != lock->GetName()) {
            lock->SetName(text);

            ProjectHistory::Get(*project).PushState(XO("Modified Clip Name"),
                XO("Clip Name Edit"), UndoPush::CONSOLIDATE);
        }
    }
    ResetClipNameEdit();
}

void WaveTrackAffordanceControls::OnTextEditCancelled(SaucedacityProject* project)
{
    ResetClipNameEdit();
}

void WaveTrackAffordanceControls::OnTextModified(SaucedacityProject* project, const wxString& text)
{
    //Nothing to do
}

void WaveTrackAffordanceControls::OnTextContextMenu(SaucedacityProject* project, const wxPoint& position)
{
}

void WaveTrackAffordanceControls::ResetClipNameEdit()
{
    mTextEditHelper.reset();
    mEditedClip.reset();
}

void WaveTrackAffordanceControls::OnTrackChanged(TrackListEvent& evt)
{
    evt.Skip();
    ExitTextEditing();
}

unsigned WaveTrackAffordanceControls::ExitTextEditing()
{
    using namespace RefreshCode;
    if (mTextEditHelper)
    {
        if (auto trackList = FindTrack()->GetOwner())
        {
            mTextEditHelper->Finish(trackList->GetOwner());
        }
        ResetClipNameEdit();
        return RefreshCell;
    }
    return RefreshNone;
}

bool WaveTrackAffordanceControls::StartEditNameOfMatchingClip(
    SaucedacityProject &project, std::function<bool(WaveClip&)> test )
{
    //Attempts to invoke name editing if there is a selected clip
    auto waveTrack = std::dynamic_pointer_cast<WaveTrack>(FindTrack());
    if (!waveTrack)
        return false;
    auto clips = waveTrack->GetClips();

    auto it = std::find_if(clips.begin(), clips.end(),
      [&](auto pClip){ return pClip && test && test(*pClip); });
    if (it != clips.end())
    {
        mEditedClip = *it;
        return StartEditClipName(&project);
    }
    return false;
}

unsigned WaveTrackAffordanceControls::OnAffordanceClick(const TrackPanelMouseEvent& event, SaucedacityProject* project)
{
    auto& viewInfo = ViewInfo::Get(*project);
    if (mTextEditHelper)
    {
        if (auto lock = mEditedClip.lock())
        {
            auto affordanceRect = ClipParameters::GetClipRect(*lock.get(), viewInfo, event.rect);
            if (!affordanceRect.Contains(event.event.GetPosition()))
               return ExitTextEditing();
        }
    }
    else if (auto lock = mFocusClip.lock())
    {
        if (event.event.LeftDClick())
        {
            auto affordanceRect = ClipParameters::GetClipRect(*lock.get(), viewInfo, event.rect);
            if (affordanceRect.Contains(event.event.GetPosition()) &&
                StartEditClipName(project))
            {
                event.event.Skip();
                return RefreshCode::RefreshCell | RefreshCode::Cancelled;
            }
        }
    }
    return RefreshCode::RefreshNone;
}

std::shared_ptr<TextEditHelper> WaveTrackAffordanceControls::MakeTextEditHelper(const wxString& text)
{
    auto helper = std::make_shared<TextEditHelper>(shared_from_this(), text, mClipNameFont);
    helper->SetTextColor(theTheme.Colour(clrClipNameText));
    helper->SetTextSelectionColor(theTheme.Colour(clrClipNameTextSelection));
    return helper; 
}

// Register a menu item

namespace {

// Menu handler functions

struct Handler : CommandHandlerObject {

void OnEditClipName(const CommandContext &context)
{
   auto &project = context.project;
   const auto [pTrack, pClip] = SelectedClipOfFocusedTrack(project);
   if (pTrack && pClip) {
      if (auto pAffordance = FindAffordance(*pTrack)) {
         pAffordance->StartEditNameOfMatchingClip(project,
            [pClip = pClip](auto &clip){ return &clip == pClip; });
         // Refresh so the cursor appears
         TrackPanel::Get(project).RefreshTrack(pTrack);
      }
   }
}

};

#define FN(X) (& Handler :: X)

CommandHandlerObject &findCommandHandler(SaucedacityProject &) {
   // Handler is not stateful.  Doesn't need a factory registered with
   // SaucedacityProject.
   static Handler instance;
   return instance;
};

using namespace MenuTable;

// Register menu items

AttachedItem sAttachment{ wxT("Edit/Other"),
   ( FinderScope{ findCommandHandler },
      Command( L"RenameClip", XXO("Rename Clip..."),
         &Handler::OnEditClipName, SomeClipIsSelectedFlag(),
         wxT("Ctrl+F2") ) )
};

}

#undef FN
