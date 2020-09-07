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
	pfad.resize(pfad.size() - strlen("VFPC.dll"));
	pfad += "Sid.json";

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

void CVFPCPlugin::getSids() {
	stringstream ss;
	ifstream ifs;
	ifs.open(pfad.c_str(), ios::binary);
	ss << ifs.rdbuf();
	ifs.close();

	if (config.Parse<0>(ss.str().c_str()).HasParseError()) {
		string error{ pfad };
		error += " couldn't be opened!";
		sendMessage("Error", error.c_str());
		return;
	}

	airports.clear();

	for (SizeType i = 0; i < config.Size(); i++) {
		const Value& airport = config[i];
		string airport_icao = airport["icao"].GetString();

		airports.insert(pair<string, SizeType>(airport_icao, i));
	}
}

// Does the checking and magic stuff, so everything will be alright, when this is finished! Or not. Who knows?
vector<string> CVFPCPlugin::validizeSid(CFlightPlan flightPlan) {
	vector<string> returnValid{}; // 0 = Callsign, 1 = valid/invalid SID, 2 = SID Name, 3 = Destination, 4 = Airway, 5 = Engine Type, 6 = Even/Odd, 7 = Minimum Flight Level, 8 = Maximum Flight Level, 9 = Navigation restriction, 10 = Passed

	returnValid.push_back(flightPlan.GetCallsign());
	bool valid{ false };

	string origin = flightPlan.GetFlightPlanData().GetOrigin(); boost::to_upper(origin);
	string destination = flightPlan.GetFlightPlanData().GetDestination(); boost::to_upper(destination);
	SizeType origin_int;
	int RFL = flightPlan.GetFlightPlanData().GetFinalAltitude();
	
	vector<string> route = split(flightPlan.GetFlightPlanData().GetRoute(), ' ');
	for (std::size_t i = 0; i < route.size(); i++) {
		boost::to_upper(route[i]);
	}

	string sid = flightPlan.GetFlightPlanData().GetSidName(); boost::to_upper(sid);
	string first_wp = sid.substr(0, sid.find_first_of("0123456789")); boost::to_upper(first_wp);
	string sid_suffix = sid.substr(sid.find_first_of("0123456789"), sid.length()); boost::to_upper(sid_suffix);
	string first_airway;

	vector<string>::iterator it = find(route.begin(), route.end(), first_wp);
	if (it != route.end() && (it - route.begin()) != route.size() - 1) {
		first_airway = route[(it - route.begin()) + 1];
		boost::to_upper(first_airway);
	}

	// Airport defined
	if (airports.find(origin) == airports.end()) {
		returnValid.push_back("Invalid");
		returnValid.push_back("No valid Airport found!");
		for (int i = 0; i < 8; i++) {
			returnValid.push_back("-");
		}
		returnValid.push_back("Failed");
		return returnValid;
	}
	else
		origin_int = airports[origin];

	// Any SIDs defined
	if (!config[origin_int].HasMember("sids") || config[origin_int]["sids"].IsArray()) {
		returnValid.push_back("Invalid");
		returnValid.push_back("No SIDs defined!");
		for (int i = 0; i < 7; i++) {
			returnValid.push_back("-");
		}
		returnValid.push_back("Failed");
		return returnValid;
	}

	// Needed SID defined
	if (!config[origin_int]["sids"].HasMember(first_wp.c_str()) || !config[origin_int]["sids"][first_wp.c_str()].IsArray()) {
		returnValid.push_back("Invalid");
		returnValid.push_back("No valid SID found!");
		for (int i = 0; i < 7; i++) {
			returnValid.push_back("-");
		}
		returnValid.push_back("Failed");
		return returnValid;
	}

	const Value& conditions = config[origin_int]["sids"][first_wp.c_str()];
	for (SizeType i = 0; i < conditions.Size(); i++) {
		returnValid.clear();
		returnValid.push_back(flightPlan.GetCallsign());
		bool passed[7]{ false };
		valid = false;

		// Skip SID if the check is suffix-related
		if (conditions[i]["suffix"].IsString() && conditions[i]["suffix"].GetString() != sid_suffix) {
			continue;
		}

		// Does Condition contain our destination if it's limited
		if (conditions[i]["destinations"].IsArray() && conditions[i]["destinations"].Size()) {
			string dest;
			if ((dest = destArrayContains(conditions[i]["destinations"], destination.c_str())).size()) {
				if (dest.size() < 4)
					dest += string(4 - dest.size(), '*');
				returnValid.push_back("Passed Destination (" + dest + ")");
				passed[0] = true;
			}
			else {
				continue;
			}
		}
		else {
			returnValid.push_back("No Destination restriction");
			passed[0] = true;
		}

		// Does Condition contain our first airway if it's limited
		if (conditions[i]["airways"].IsArray() && conditions[i]["airways"].Size()) {
			string rte = flightPlan.GetFlightPlanData().GetRoute();
			if (routeContains(rte, conditions[i]["airways"])) {
				returnValid.push_back("Passed Airways");
				passed[1] = true;
			}
			else {
				continue;
			}
		}
		else {
			returnValid.push_back("No Airway restriction");
			passed[1] = true;
		}

		// Is Engine Type if it's limited (P=piston, T=turboprop, J=jet, E=electric)
		if (conditions[i]["engine"].IsString()) {
			if (conditions[i]["engine"].GetString()[0] == flightPlan.GetFlightPlanData().GetEngineType()) {
				returnValid.push_back("Passed Engine type");
				passed[2] = true;
			}
			else {
				returnValid.push_back("Failed Engine type. Needed Type: " + (string)conditions[i]["engine"].GetString());
			}
		}
		else if (conditions[i]["engine"].IsArray() && conditions[i]["engine"].Size()) {
			if (arrayContains(conditions[i]["engine"], flightPlan.GetFlightPlanData().GetEngineType())) {
				returnValid.push_back("Passed Engine type");
				passed[2] = true;
			}
			else {
				returnValid.push_back("Failed Engine type. Needed Type: " + arrayToString(conditions[i]["engine"], ','));
			}
		}
		else {
			returnValid.push_back("No Engine type restriction");
			passed[2] = true;
		}


		valid = true;
		returnValid.insert(returnValid.begin() + 1, "Valid");
		returnValid.insert(returnValid.begin() + 2, first_wp);

		// Direction of condition (EVEN, ODD, ANY)
		string direction = conditions[i]["direction"].GetString();
		boost::to_upper(direction);

		if (direction == "EVEN") {
			if ((RFL / 1000) % 2 == 0) {
				returnValid.push_back("Passed Even");
				passed[3] = true;
			}
			else {
				returnValid.push_back("Failed Even");
			}
		}
		else if (direction == "ODD") {
			if ((RFL / 1000) % 2 != 0) {
				returnValid.push_back("Passed Odd");
				passed[3] = true;
			}
			else {
				returnValid.push_back("Failed Odd");
			}
		}
		else if (direction == "ANY") {
			returnValid.push_back("No Direction restriction");
			passed[3] = true;
		}
		else {
			string errorText{ "Config Error for Even/Odd on SID: " };
			errorText += first_wp;
			sendMessage("Error", errorText);
			returnValid.push_back("Config Error for Even/Odd on this SID!");
		}
		
		// Flight level (min_fl, max_fl)
		int min_fl, max_fl;
		if (conditions[i].HasMember("min_fl") && (min_fl = conditions[i]["min_fl"].GetInt()) > 0) {
			if ((RFL / 100) >= min_fl) {
				returnValid.push_back("Passed Minimum Flight Level");
				passed[4] = true;
			}
			else {
				returnValid.push_back("Failed Minimum Flight Level. Min FL: " + to_string(min_fl));
			}
		}
		else {
			returnValid.push_back("No Minimum Flight Level");
			passed[4] = true;
		}

		if (conditions[i].HasMember("max_fl") && (max_fl = conditions[i]["max_fl"].GetInt()) > 0) {
			if ((RFL / 100) <= max_fl) {
				returnValid.push_back("Passed Maximum Flight Level");
				passed[5] = true;
			}
			else {
				returnValid.push_back("Failed Maximum Flight Level. Max FL: " + to_string(max_fl));
			}
		}
		else {
			returnValid.push_back("No Maximum Flight Level");
			passed[5] = true;
		}

		// Special navigation requirements needed
		if (conditions[i]["navigation"].IsString()) {
			std::string navigation_constraints(conditions[i]["navigation"].GetString());
			if (std::string::npos == navigation_constraints.find_first_of(flightPlan.GetFlightPlanData().GetCapibilities())) {
				returnValid.push_back("Failed navigation capability restriction. Req. capability: " + navigation_constraints);
				passed[6] = false;
			}
			else {
				returnValid.push_back("No navigation capability restriction");
				passed[6] = true;
			}
		}
		else {
			returnValid.push_back("No navigation capability restriction");
			passed[6] = true;
		}

		bool passedVeri{ false };
		for (int i = 0; i < 7; i++) {
			if (passed[i])
			{
				passedVeri = true;
			}
			else {
				passedVeri = false;
				break;
			}
		}
		if (passedVeri) {
			returnValid.push_back("Passed");
			break;
		}
		else {
			returnValid.push_back("Failed");
			if (!passed[0] || !passed[1])
				continue;
			else
				break;
		}
	}
	
	if (!valid) {
		returnValid.push_back("Invalid");
		returnValid.push_back("No valid SID found!");
		for (int i = 0; i < 7; i++) {
			returnValid.push_back("-");
		}
		returnValid.push_back("Failed");
	}
	return returnValid;
}

//
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
		}
		else {
			vector<string> messageBuffer{ validizeSid(FlightPlan) }; // 0 = Callsign, 1 = valid/invalid SID, 2 = SID Name, 3 = Destination, 4 = Airway, 5 = Engine Type, 6 = Even/Odd, 7 = Minimum Flight Level, 8 = Maximum Flight Level, 9 = Navigation restriction, 10 = Passed
			
			if (messageBuffer.at(10) == "Passed") {
				*pRGB = TAG_GREEN;
				strcpy_s(sItemString, 16, "OK!");
			}
			else {
				*pRGB = TAG_RED;
				string code = getFails(validizeSid(FlightPlan));
				strcpy_s(sItemString, 16, code.c_str());
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
		locale loc;
		string buffer{ sCommandLine };
		buffer.erase(0, 11);
		getSids();
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
	vector<string> messageBuffer{ validizeSid(FlightPlanSelectASEL()) };	// 0 = Callsign, 1 = valid/invalid SID, 2 = SID Name, 3 = Even/Odd, 4 = Minimum Flight Level, 5 = Maximum Flight Level, 6 = Passed
	sendMessage(messageBuffer.at(0), "Checking...");
	string buffer{ messageBuffer.at(1) };
	if (messageBuffer.at(1) == "Valid") {
		buffer += ", found SID: ";
		for (int i = 2; i < 10; i++) {
			buffer += messageBuffer.at(i);
			buffer += ", ";
		}
		buffer += messageBuffer.at(10);
		buffer += " FlightPlan Check. Check complete.";
	} else {
		buffer += " ";
		buffer += messageBuffer.at(2);
		buffer += " Check complete.";
	}
	sendMessage(messageBuffer.at(0), buffer);
}

string CVFPCPlugin::getFails(vector<string> messageBuffer) {
	vector<string> fail;
	fail.push_back("FPL");

	if (messageBuffer.at(1) == "Invalid") {
		fail.push_back("SID");
	}
	if (messageBuffer.at(3).find_first_of("Failed") == 0) {
		fail.push_back("DST");
	}
	if (messageBuffer.at(4).find_first_of("Failed") == 0) {
		fail.push_back("AWY");
	}
	if (messageBuffer.at(5).find_first_of("Failed") == 0) {
		fail.push_back("ENG");
	}

	if (messageBuffer.at(6).find_first_of("Failed") == 0) {
		fail.push_back("E/O");
	}
	if (messageBuffer.at(7).find_first_of("Failed") == 0) {
		fail.push_back("MIN");
	}
	if (messageBuffer.at(8).find_first_of("Failed") == 0) {
		fail.push_back("MAX");
	}
	if (messageBuffer.at(9).find_first_of("Failed") == 0) {
		fail.push_back("NAV");
	}

	std::size_t couldnt = disCount;
	while (couldnt >= fail.size())
		couldnt -= fail.size();
	return fail[couldnt];
}

void CVFPCPlugin::OnTimer(int Counter) {

	blink = !blink;

	if (blink) {
		if (disCount < 3) {
			disCount++;
		}
		else {
			disCount = 0;
		}
	}

	// Loading proper Sids, when logged in
	if (GetConnectionType() != CONNECTION_TYPE_NO && !initialSidLoad) {
		string callsign{ ControllerMyself().GetCallsign() };
		getSids();
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