#pragma once
#include "EuroScopePlugIn.h"
#include <iostream>
#include <string>
#include "Constant.hpp"
#include <fstream>
#include <vector>

#define MY_PLUGIN_NAME      "VFPC"
#define MY_PLUGIN_VERSION   "1.4"
#define MY_PLUGIN_DEVELOPER "Jan Fries"
#define MY_PLUGIN_COPYRIGHT "GPL v3"
#define MY_PLUGIN_VIEW_AVISO  "Vatsim FlightPlan Checker"

#define PLUGIN_WELCOME_MESSAGE	"Willkommen beim Vatsim Flugplan-RFL checker"

using namespace std;
using namespace EuroScopePlugIn;

class CVFPCPlugin :
	public EuroScopePlugIn::CPlugIn
{
public:
	CVFPCPlugin();
	virtual ~CVFPCPlugin();

	virtual void getSids(string airport);

	virtual vector<string> validizeSid(CFlightPlan flightPlan);

	virtual void OnFunctionCall(int FunctionId, const char * ItemString, POINT Pt, RECT Area);

	//Define OnGetTagItem function
	virtual void OnGetTagItem(CFlightPlan FlightPlan,
		CRadarTarget RadarTarget,
		int ItemCode,
		int TagData,
		char sItemString[16],
		int* pColorCode,
		COLORREF* pRGB,
		double* pFontSize);

	virtual bool OnCompileCommand(const char * sCommandLine);

	virtual void debugMessage(string type, string message);

	virtual void sendMessage(string type, string message);

	virtual void sendMessage(string message);

	virtual void checkFPDetail();

	virtual string getFails(vector<string> messageBuffer);

	virtual void OnTimer(int Count);
};

