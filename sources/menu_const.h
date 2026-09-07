#pragma once
#include "core/types.h"
#include "core/resource.h"

namespace as1
{
    namespace MENU_CONST
    {

        constexpr RESOURCE::ResTypes::Type ROOT_MENU = RESOURCE::ResTypes::MENU;
        constexpr RESOURCE::ResTypes::Type HEAD = RESOURCE::ResTypes::HEAD;
        constexpr RESOURCE::ResTypes::Type SPR = RESOURCE::ResTypes::SPRITE;
        constexpr RESOURCE::ResTypes::Type SPRI = RESOURCE::ResTypes::SPRI;

        constexpr const char* LOG_MENU = "MENU";
        constexpr const char* LOG_HEAD_IN_MENU = "'HEAD'in menu";
        constexpr const char* LOG_SPR_OR_SPRI_IN_MENU = "'SPR ' or 'SPRI' in menu";
        constexpr int LOG_OPEN_ERROR_CODE = 7;
        constexpr int LOG_SECTION_MISSING_ERROR_CODE = 11;
    }
}
