/*!********************************************************************

  Tenacity

  @file AudaciumDarkGreenThemeAsCeeCode.cpp

  Avery King split from Theme.cpp

  (This is copied from DarkThemeAsCeeCode.h; I didn't write anything)

**********************************************************************/

#include <vector>
#include "Theme.h"

static const std::vector<unsigned char> ImageCacheAsData {
// Include the generated file full of numbers
#include "AudaciumDarkGreenThemeAsCeeCode.h"
};

static ThemeBase::RegisteredTheme theme{
   { "audacium-dark-green", XO("Audacium Dark Green") }, { ImageCacheAsData, false /* is default */}
};
