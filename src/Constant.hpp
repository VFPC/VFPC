#pragma once
#include "stdafx.h"

#define MY_PLUGIN_NAME      "VFPC (UK)"
#define MY_PLUGIN_VERSION   "3.6.1"
#define MY_PLUGIN_DEVELOPER "Lenny Colton, Jan Fries, Hendrik Peter, Sven Czarnian"
#define MY_PLUGIN_COPYRIGHT "GPL v3"
#define MY_PLUGIN_VIEW_AVISO  "VATSIM (UK) Flight Plan Checker"
#define MY_API_ADDRESS	"https://vfpc.tomjmills.co.uk/"

#define PLUGIN_WELCOME_MESSAGE	"Welcome to the (UK) VATSIM Flight Plan Checker"

using namespace std;

const int TAG_ITEM_CHECKFP = 1;

const int TAG_FUNC_CHECKFP_MENU = 100;
const int TAG_FUNC_CHECKFP_CHECK = 101;
const int TAG_FUNC_CHECKFP_DISMISS = 102;

const COLORREF TAG_GREEN = RGB(0, 190, 0);
const COLORREF TAG_ORANGE = RGB(241, 121, 0);
const COLORREF TAG_RED = RGB(190, 0, 0);

const size_t API_REFRESH_TIME = 10;

const string EVEN_DIRECTION = "EVEN";
const string ODD_DIRECTION = "ODD";

const string PLUGIN_FILE = "VFPC.dll";
const string DATA_FILE = "Sid.json";
const string LOG_FILE = "VFPC.log";

const string COMMAND_PREFIX = ".vfpc ";
const string LOAD_COMMAND = "load";
const string FILE_COMMAND = "file";
const string LOG_COMMAND = "log";
const string CHECK_COMMAND = "check";

const string DCT_ENTRY = "DCT";
const string SPDLVL_SEP = "/";
const string OUTDATED_SID = "#";

const string RESULT_SEP = ", ";
const string ROUTE_RESULT_SEP = " / ";
const string NO_RESULTS = "None";

const string WILDCARD = "*";

const int RVSM_UPPER = 41000;

inline static bool startsWith(const char *pre, const char *str)
{
	size_t lenpre = strlen(pre), lenstr = strlen(str);
	return lenstr < lenpre ? false : strncmp(pre, str, lenpre) == 0;
};