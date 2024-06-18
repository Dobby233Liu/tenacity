/**********************************************************************
 
  Audacity: A Digital Audio Editor
 
  RealtimeEffectList.cpp
 
 *********************************************************************/

#include "RealtimeEffectList.h"
#include "RealtimeEffectState.h"

#include "Project.h"
#include "Track.h"

RealtimeEffectList::RealtimeEffectList()
{
}

RealtimeEffectList::~RealtimeEffectList()
{
}

static const AttachedProjectObjects::RegisteredFactory masterEffects
{
   [](TenacityProject &project)
   {
      return std::make_shared<RealtimeEffectList>();
   }
};

RealtimeEffectList &RealtimeEffectList::Get(TenacityProject &project)
{
   return project.AttachedObjects::Get<RealtimeEffectList>(masterEffects);
}

const RealtimeEffectList &RealtimeEffectList::Get(const TenacityProject &project)
{
   return Get(const_cast<TenacityProject &>(project));
}

static const AttachedTrackObjects::RegisteredFactory trackEffects
{
   [](Track &track)
   {
      return std::make_shared<RealtimeEffectList>();
   }
};

RealtimeEffectList &RealtimeEffectList::Get(Track &track)
{
   return track.AttachedObjects::Get<RealtimeEffectList>(trackEffects);
}

const RealtimeEffectList &RealtimeEffectList::Get(const Track &track)
{
   return Get(const_cast<Track &>(track));
}

void RealtimeEffectList::Visit(StateVisitor func)
{
   for (auto &state : mStates)
      func(*state, !state->IsActive());
}

RealtimeEffectState *RealtimeEffectList::AddState(const PluginID &id)
{
   auto pState = std::make_unique<RealtimeEffectState>(id);
   if (id.empty() || pState->GetEffect() != nullptr) {
      auto result = pState.get();
      mStates.emplace_back(move(pState));
      return result;
   }
   else
      // Effect initialization failed for the id
      return nullptr;
}

const std::string &RealtimeEffectList::XMLTag()
{
   static const std::string result{"effects"};
   return result;
}

bool RealtimeEffectList::HandleXMLTag(
   const std::string_view &tag, const AttributesList &)
{
   return (tag == XMLTag());
}

void RealtimeEffectList::HandleXMLEndTag(const std::string_view &tag)
{
   if (tag == XMLTag()) {
      // Remove states that fail to load their effects
      auto end = mStates.end();
      auto newEnd = std::remove_if( mStates.begin(), end,
         [](const auto &pState){ return pState->GetEffect() == nullptr; });
      mStates.erase(newEnd, end);
   }
}

XMLTagHandler *RealtimeEffectList::HandleXMLChild(const std::string_view &tag)
{
   if (tag == RealtimeEffectState::XMLTag()) {
      auto pState = AddState({});
      assert(pState); // Should succeed always for empty id
      return pState;
   }
   return nullptr;
}

void RealtimeEffectList::WriteXML(XMLWriter &xmlFile) const
{
   if (mStates.size() == 0)
      return;

   xmlFile.StartTag(XMLTag());

   for (const auto & state : mStates)
      state->WriteXML(xmlFile);
   
   xmlFile.EndTag(XMLTag());
}
