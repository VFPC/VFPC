#include "stdafx.h"
#include "analyzeFP.hpp"

extern "C" IMAGE_DOS_HEADER __ImageBase;

bool blink;
bool debugMode, initialSidLoad;

int disCount;

ifstream sidDatei;
char DllPathFile[_MAX_PATH];
string pfad;

vector<string> sidName;
vector<string> sidEven;
vector<int> sidMin;
vector<int> sidMax;

using namespace std;
using namespace EuroScopePlugIn;

	// Run on Plugin Initialization
CVFPCPlugin::CVFPCPlugin(void) :CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE, MY_PLUGIN_NAME, MY_PLUGIN_VERSION, MY_PLUGIN_DEVELOPER, MY_PLUGIN_COPYRIGHT)
{	
	string loadingMessage = "Version: ";
	loadingMessage += MY_PLUGIN_VERSION;
	loadingMessage += " loaded.";
	sendMessage(loadingMessage);

	// Register Tag Item "VFPC"
	RegisterTagItemType("VFPC", TAG_ITEM_FPCHECK);
	RegisterTagItemFunction("Check FP", TAG_FUNC_CHECKFP_MENU);

	// Get Path of the Sid.txt
	GetModuleFileNameA(HINSTANCE(&__ImageBase), DllPathFile, sizeof(DllPathFile));
	pfad = DllPathFile;
	pfad.resize(pfad.size() - strlen("EDFFCheckFP.dll"));
	pfad += "Sid.txt";

	debugMode = false;
	initialSidLoad = false;
}

// Run on Plugin destruction, Ie. Closing EuroScope or unloading plugin
CVFPCPlugin::~CVFPCPlugin()
{
}


/*
	Custom Functions
*/

void CVFPCPlugin::debugMessage(string type, string message) {
	// Display Debug Message if debugMode = true
	if (debugMode) {
		DisplayUserMessage("VFPC", type.c_str(), message.c_str(), true, true, true, false, false);
	}
}

void CVFPCPlugin::sendMessage(string type, string message) {
	// Show a message
	DisplayUserMessage("VFPC", type.c_str(), message.c_str(), true, true, true, true, false);
}

void CVFPCPlugin::sendMessage(string message) {
	DisplayUserMessage("Message", "VFPC", message.c_str(), true, true, true, false, false);
}

// Get data from sid.txt
void CVFPCPlugin::getSids(string airport) {
	sidDatei.open(pfad, ios::in);
	if (!sidDatei) {	// Display Error, if something bad happened.
		string error{ pfad };
		error += " couldn't be opened!";
		sendMessage("Error", error.c_str());
	} else {
		sendMessage("Loading Sids...");
		string read, readbuffer;
		string sid[4];			// 0 = Name, 1 = Even/Odd, 2 = Min FL, 3 = Max FL
		if (airport == "") {
			airport = ControllerMyself().GetCallsign();
		}
		airport.resize(4);	// Take e.g. "EDDF_TWR", or "EDDF_N_APP" and make it to "EDDF"
		sendMessage("Loading", airport);
		// To-Do: safing setting for multiple airports

		while (getline(sidDatei, read)) {	// Read the line! Love isn't always on time!
			readbuffer = read;
			readbuffer.resize(4);
			debugMessage("Reading", read);
			debugMessage("Readbuffer", readbuffer);
			if (readbuffer == airport) {
				sid[0] = read;  // sid[0] = "EDDF;OBOKA;Even;0;0";
				sid[0].erase(0, sid[0].find_first_of(';') + 1);   //sid[0] = "OBOKA;Even;0;0";
				debugMessage("Sid[0]", sid[0]);
				sid[1] = sid[0];
				sid[0].erase(sid[0].find_first_of(';'), sid[0].length());   //sid[0] = "OBOKA";
				sid[1].erase(0, sid[1].find_first_of(';') + 1);	//sid[1] = Even;0;0";
				debugMessage("Sid[1]", sid[1]);
				sid[2] = sid[1];
				sid[1].erase(sid[1].find_first_of(';'), sid[1].length());
				sid[2].erase(0, sid[2].find_first_of(';') + 1);
				debugMessage("Sid[2]", sid[2]);
				sid[3] = sid[2];
				sid[2].erase(sid[2].find_first_of(';'), sid[2].length());
				sid[3].erase(0, sid[3].find_first_of(';') + 1);
				debugMessage("Sid[3]", sid[3]);
				//sid[3].erase(sid[3].find_first_of(';'), sid[3].length());
				// To-Do: conditional SIDs

				// Adding all the data to the vectors
				sidName.push_back(sid[0]);
				sidEven.push_back(sid[1]);
				sidMin.push_back(stoi(sid[2]));
				sidMax.push_back(stoi(sid[3]));

				// Showing what is added to the sid-vectors
				if (debugMode) {
					string sidContainer{ sid[0] };
					sidContainer += sid[1];
					sidContainer += sid[2];
					sidContainer += sid[3];

					debugMessage("Adding", sidContainer);
				}
			}
		}

		sidDatei.close();
		string sidCount{ "SID loading finished: " };
		sidCount += to_string(sidName.size());
		sidCount += " SIDs loaded.";
		sendMessage(sidCount.c_str());
	}
	// Output of all vectors with their data in it
	if (debugMode) {
		string names{};
		for (string buf : sidName)
		{
			names += buf;
			names += ";";
		}
		string even{};
		for (string buf : sidEven)
		{
			even += buf;
			even += ";";
		}
		string min{};
		for (int buf : sidMin)
		{
			min += to_string(buf);
			min += ";";
		}
		string max{};
		for (int buf : sidMax)
		{
			max += to_string(buf);
			max += ";";
		}
		debugMessage("Sid Name", names);
		debugMessage("Sid Even", even);
		debugMessage("Sid Min", min);
		debugMessage("Sid Max", max);
	}
}

// Does the checking and magic stuff, so everything will be alright, when this is finished! Or not. Who knows?
vector<string> CVFPCPlugin::validizeSid(CFlightPlan flightPlan) {
	vector<string> returnValid{};		// 0 = Callsign, 1 = valid/invalid SID, 2 = SID Name, 3 = Even/Odd, 4 = Minimum Flight Level, 5 = Maximum Flight Level, 6 = Passed
	string FlightPlanString = flightPlan.GetFlightPlanData().GetRoute();
	int RFL = flightPlan.GetFlightPlanData().GetFinalAltitude();
	returnValid.push_back(flightPlan.GetCallsign());
	bool valid{ false };
	for (int i = 0; i < sidName.size(); i++) {
		bool passed[3]{ false };
		if (FlightPlanString.find(sidName.at(i)) != string::npos) {
			valid = true;
			returnValid.push_back("Valid");	
			returnValid.push_back(sidName.at(i));
			if (sidEven.at(i) == "Even") {
				if ((RFL / 1000) % 2 == 0) {
					returnValid.push_back("Passed Even");
					passed[0] = true;
				} else {
					returnValid.push_back("Failed Even");
				}
			}
			else if (sidEven.at(i) == "Odd") {
				if ((RFL / 1000) % 2 != 0) {
					returnValid.push_back("Passed Odd");
					passed[0] = true;
				} else {
					returnValid.push_back("Failed Odd");
				}
			}
			else if (sidEven.at(i) != "Even" && sidEven.at(i) != "Odd") {
				string errorText{ "Config Error for Even/Odd on SID: " };
				errorText += sidName.at(i);
				sendMessage("Error", errorText);
				returnValid.push_back("Config Error for Even/Odd on this SID!");
			}
			if (sidMin.at(i) != 0) {
				if ((RFL / 100) >= sidMin.at(i)) {
					returnValid.push_back("Passed Minimum Flight Level");
					passed[1] = true;
				}
				else {
					returnValid.push_back("Failed Minimum Flight Level");
				}
			} else {
				returnValid.push_back("No Minimum Flight Level");
				passed[1] = true;
			}
			if (sidMax.at(i) != 0) {
				if ((RFL / 100) <= sidMax.at(i)) {
					returnValid.push_back("Passed Maximum Flight Level");
					passed[2] = true;
				}
				else {
					returnValid.push_back("Failed Maximum Flight Level");
				}
			} else {
				returnValid.push_back("No Maximum Flight Level");
				passed[2] = true;
			}
			bool passedVeri{ false };
			for (int i = 0; i < 3; i++) {
				if (passed[i])
				{
					passedVeri = true;
				} else {
					passedVeri = false;
					break;
				}
			}
			if (passedVeri) {
				returnValid.push_back("Passed");
			} else {
				returnValid.push_back("Failed");
			}
			break;
		}
	}
	if (!valid) {
		returnValid.push_back("Invalid");
		returnValid.push_back("No valid SID found!");
		for (int i = 0; i < 4; i++) {
			returnValid.push_back("-");
		}
	}
	return returnValid;
}

void CVFPCPlugin::OnFunctionCall(int FunctionId, const char * ItemString, POINT Pt, RECT Area) {
	if (FunctionId == TAG_FUNC_CHECKFP_MENU) {
		OpenPopupList(Area, "Check FP", 1);
		AddPopupListElement("Show Checks", "", TAG_FUNC_CHECKFP_CHECK, false, 2, false);
	}
	if (FunctionId == TAG_FUNC_CHECKFP_CHECK) {
		checkFPDetail();
	}
}

// Get FlightPlan, and therefore get the first waypoint of the flightplan (ie. SID). Check if the (RFL/1000) corresponds to the SID Min FL and report output "OK" or "FPL"
void CVFPCPlugin::OnGetTagItem(CFlightPlan FlightPlan, CRadarTarget RadarTarget, int ItemCode, int TagData, char sItemString[16], int* pColorCode, COLORREF* pRGB, double* pFontSize)
{
	if (ItemCode == TAG_ITEM_FPCHECK)
	{
		string FlightPlanString = FlightPlan.GetFlightPlanData().GetRoute();
		int RFL = FlightPlan.GetFlightPlanData().GetFinalAltitude();

		*pColorCode = TAG_COLOR_RGB_DEFINED;
		string fpType{ FlightPlan.GetFlightPlanData().GetPlanType() };
		if (fpType == "V") {
			*pRGB = TAG_GREEN;
			strcpy_s(sItemString, 16, "VFR");
		} else {
			for (int i = 0; i < sidName.size(); i++) {
				vector<string> messageBuffer{ validizeSid(FlightPlan) };		// 0 = Callsign, 1 = valid/invalid SID, 2 = SID Name, 3 = Even/Odd, 4 = Minimum Flight Level, 5 = Maximum Flight Level, 6 = Passed
				if (messageBuffer.at(6) == "Passed") {
					*pRGB = TAG_GREEN;
					strcpy_s(sItemString, 16, "OK!");
					break;
				}
				else {
					*pRGB = TAG_RED;
					string code = getFails(validizeSid(FlightPlan));
					strcpy_s(sItemString, 16, code.c_str());
				}
			}
		}

	}
}

bool CVFPCPlugin::OnCompileCommand(const char * sCommandLine) {
	if (startsWith(".vfpc reload", sCommandLine))
	{
		sendMessage("Unloading all loaded SIDs...");
		sidName.clear();
		sidEven.clear();
		sidMin.clear();
		sidMax.clear();
		initialSidLoad = false;
		return true;
	}
	if (startsWith(".vfpc debug", sCommandLine)) {
		if (debugMode) {
			debugMessage("DebugMode", "Deactivating Debug Mode!");
			debugMode = false;
		} else {
			debugMode = true;
			debugMessage("DebugMode", "Activating Debug Mode!");
		}
		return true;
	}
	if (startsWith(".vfpc load", sCommandLine)) {
		string buffer{ sCommandLine };
		buffer.erase(0, 14);
		getSids(buffer);
		return true;
	}
	if (startsWith(".vfpc check", sCommandLine))
	{
		checkFPDetail();
		return true;
	}
	return false;
}

// Sends to you, which checks were failed and which were passed on the selected aircraft
void CVFPCPlugin::checkFPDetail() {	
	vector<string> messageBuffer{ validizeSid(FlightPlanSelectASEL()) };		// 0 = Callsign, 1 = valid/invalid SID, 2 = SID Name, 3 = Even/Odd, 4 = Minimum Flight Level, 5 = Maximum Flight Level, 6 = Passed
	sendMessage(messageBuffer.at(0), "Checking...");
	string buffer{ messageBuffer.at(1) };
	if (messageBuffer.at(1) == "Valid") {
		buffer += ", found SID: ";
		for (int i = 2; i < 6; i++) {
			buffer += messageBuffer.at(i);
			buffer += ", ";
		}
		buffer += messageBuffer.at(6);
		buffer += " FlightPlan Check. Check complete.";
	} else {
		buffer += " ";
		buffer += messageBuffer.at(2);
		buffer += " Check complete.";
	}
	sendMessage(messageBuffer.at(0), buffer);
}

string CVFPCPlugin::getFails(vector<string> messageBuffer) {
	string fail[4];
	if (messageBuffer.at(1) == "Invalid") {
		fail[0] = "SID";
	} else {
		fail[0] = "FPL";
	}
	if (messageBuffer.at(3).find_first_of("Failed") == 0) {
		fail[1] = "E/O";
	} else {
		fail[1] = "FPL";
	}
	if (messageBuffer.at(4).find_first_of("Failed") == 0) {
		fail[2] = "MIN";
	} else {
		fail[2] = "FPL";
	}
	if (messageBuffer.at(5).find_first_of("Failed") == 0) {
		fail[3] = "MAX";
	} else {
		fail[3] = "FPL";
	}
	return fail[disCount];
}

void CVFPCPlugin::OnTimer(int Counter) {

	blink = !blink;

	if (disCount < 3) {
		disCount++;
	} else {
		disCount = 0;
	}

	// Loading proper Sids, when logged in
	if (GetConnectionType() != CONNECTION_TYPE_NO && !initialSidLoad) {
		string callsign{ ControllerMyself().GetCallsign() };
		if (callsign.find_first_of('_O') == 3) {
			sendMessage("Observer Mode, no SIDs loaded");
		} else {	
			getSids("");
		}
		initialSidLoad = true;
	} else if (GetConnectionType() == CONNECTION_TYPE_NO && initialSidLoad) {
		sidName.clear();
		sidEven.clear();
		sidMin.clear();
		sidMax.clear();
		initialSidLoad = false;
		sendMessage("Unloading", "All loaded SIDs");
	}
}