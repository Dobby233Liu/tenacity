/*!********************************************************************

  Tenacity

  @file AudaciumLightBlueThemeAsCeeCode.cpp

  Avery King split from Theme.cpp

  (This is copied from DarkThemeAsCeeCode.h; I didn't write anything new)

**********************************************************************/

#include <vector>
#include "Theme.h"

static const std::vector<unsigned char> ImageCacheAsData {
// Include the generated file full of numbers
#include "AudaciumLightBlueThemeAsCeeCode.h"
};

static ThemeBase::RegisteredTheme theme{
   { "audacium-light-blue", XO("Audacium Light Blue") }, { ImageCacheAsData, false /* is default */}
};
