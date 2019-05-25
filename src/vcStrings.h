#ifndef vcStrings_h__
#define vcStrings_h__

#include "udResult.h"

struct vcTranslationInfo
{
  const char *pLocalName;
  const char *pEnglishName;
  const char *pTranslatorName;
  const char *pTranslatorContactEmail;
};

namespace vcString
{
  const char* Get(const char *pKey);

  udResult LoadTable(const char *pFilename, vcTranslationInfo *pInfo);
  void FreeTable(vcTranslationInfo *pInfo);
}

#endif //vcStrings_h__
