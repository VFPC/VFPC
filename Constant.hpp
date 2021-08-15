#pragma once
#include "stdafx.h"

const int TAG_ITEM_CHECKFP = 1;

const int TAG_FUNC_CHECKFP_MENU = 100;
const int TAG_FUNC_CHECKFP_CHECK = 101;
const int TAG_FUNC_CHECKFP_DISMISS = 102;

const COLORREF TAG_GREEN = RGB(0, 190, 0);
const COLORREF TAG_RED = RGB(190, 0, 0);

inline static bool startsWith(const char *pre, const char *str)
{
	size_t lenpre = strlen(pre), lenstr = strlen(str);
	return lenstr < lenpre ? false : strncmp(pre, str, lenpre) == 0;
};