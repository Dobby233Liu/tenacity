/**********************************************************************

   Audacity: A Digital Audio Editor

   ConfigInterface.h

   Leland Lucius

   Copyright (c) 2014, Audacity Team 
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
   INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
   BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
   ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE.

**********************************************************************/

#ifndef __AUDACITY_CONFIGINTERFACE_H__
#define __AUDACITY_CONFIGINTERFACE_H__

#include "Identifier.h"
#include <functional>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

class EffectDefinitionInterface;

namespace PluginSettings {

enum ConfigurationType : unsigned {
   Shared, Private
};

//! Supported types for settings
using ConfigValueTypes = std::tuple<
     wxString
   , int
   , bool
   , float
   , double
>;

//! Define a reference to a variable of one of the types in ConfigValueTypes
/*! Avoid repetition of the list of types */
template<bool is_const, typename> struct ConfigReferenceGenerator;
template<bool is_const, typename... Types>
struct ConfigReferenceGenerator<is_const, std::tuple<Types...>> {
   using type = std::variant< std::reference_wrapper<
      std::conditional_t<is_const, const Types, Types> >... >;
};
using ConfigReference =
   ConfigReferenceGenerator<false, ConfigValueTypes>::type;
using ConfigConstReference =
   ConfigReferenceGenerator<true, ConfigValueTypes>::type;

TENACITY_DLL_API bool HasConfigGroup( EffectDefinitionInterface &ident,
   ConfigurationType type, const RegistryPath & group);
TENACITY_DLL_API bool GetConfigSubgroups( EffectDefinitionInterface &ident,
   ConfigurationType type, const RegistryPath & group,
   RegistryPaths & subgroups);

TENACITY_DLL_API bool GetConfigValue( EffectDefinitionInterface &ident,
   ConfigurationType type, const RegistryPath & group,
   const RegistryPath & key, ConfigReference var, ConfigConstReference value);


// GetConfig with default value
template<typename Value>
inline bool GetConfig( EffectDefinitionInterface &ident,
   ConfigurationType type, const RegistryPath & group,
   const RegistryPath & key, Value &var, const Value &defval)
{ return GetConfigValue(ident, type, group, key,
   std::ref(var), std::cref(defval)); }

// GetConfig with implicitly converted default value
template<typename Value, typename ConvertibleToValue>
inline bool GetConfig( EffectDefinitionInterface &ident,
   ConfigurationType type, const RegistryPath & group,
   const RegistryPath & key, Value &var, ConvertibleToValue defval)
{ return GetConfig(ident, type, group, key, var, static_cast<Value>(defval)); }

// GetConfig with default value assumed to be Value{}
template <typename Value>
inline bool GetConfig( EffectDefinitionInterface &ident,
   ConfigurationType type, const RegistryPath & group,
   const RegistryPath & key, Value &var)
{
   return GetConfig(ident, type, group, key, var, Value{});
}

TENACITY_DLL_API bool SetConfigValue( EffectDefinitionInterface &ident,
   ConfigurationType type, const RegistryPath & group,
   const RegistryPath & key, ConfigConstReference value);

template <typename Value>
inline bool SetConfig( EffectDefinitionInterface &ident,
   ConfigurationType type, const RegistryPath & group,
   const RegistryPath & key, const Value &value)
{
   return SetConfigValue(ident, type, group, key, std::cref(value));
}

TENACITY_DLL_API bool RemoveConfigSubgroup( EffectDefinitionInterface &ident,
   ConfigurationType type, const RegistryPath & group);
TENACITY_DLL_API bool RemoveConfig( EffectDefinitionInterface &ident,
   ConfigurationType type, const RegistryPath & group,
   const RegistryPath & key);

}

#endif // __AUDACITY_CONFIGINTERFACE_H__
