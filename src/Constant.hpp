#pragma once
#include "stdafx.h"

#define MY_PLUGIN_NAME      "VFPC (UK)"
#define MY_PLUGIN_VERSION   "3.5.2"
#define MY_PLUGIN_DEVELOPER "Lenny Colton, Jan Fries, Hendrik Peter, Sven Czarnian"
#define MY_PLUGIN_COPYRIGHT "GPL v3"
#define MY_PLUGIN_VIEW_AVISO  "VATSIM (UK) Flight Plan Checker"
#define MY_API_ADDRESS	"https://vfpc.tomjmills.co.uk/"

#define PLUGIN_WELCOME_MESSAGE	"Welcome to the (UK) VATSIM Flight Plan Checker"

const int TAG_ITEM_CHECKFP = 1;

const int TAG_FUNC_CHECKFP_MENU = 100;
const int TAG_FUNC_CHECKFP_CHECK = 101;
const int TAG_FUNC_CHECKFP_DISMISS = 102;

const COLORREF TAG_GREEN = RGB(0, 190, 0);
const COLORREF TAG_ORANGE = RGB(241, 121, 0);
const COLORREF TAG_RED = RGB(190, 0, 0);

const size_t API_REFRESH_TIME = 10;

const char* const EVEN_DIRECTION = "EVEN";
const char* const ODD_DIRECTION = "ODD";

const char* const PLUGIN_FILE = "VFPC.dll";
const char* const DATA_FILE = "Sid.json";

const char* const COMMAND_PREFIX = ".vfpc ";
const char* const LOAD_COMMAND = "load";
const char* const FILE_COMMAND = "file";
const char* const LOG_COMMAND = "log";
const char* const CHECK_COMMAND = "check";

const char* const DCT_ENTRY = "DCT";
const char* const SPDLVL_SEP = "/";
const char* const OUTDATED_SID = "#";

const char* const RESULT_SEP = ", ";
const char* const NO_RESULTS = "None";

const char* const WILDCARD = "*";

inline static bool startsWith(const char *pre, const char *str)
{
	size_t lenpre = strlen(pre), lenstr = strlen(str);
	return lenstr < lenpre ? false : strncmp(pre, str, lenpre) == 0;
};