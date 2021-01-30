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
		string msg = str(boost::format("An error parsing VFPC configuration occurred. Error: %s (Offset: %i)\nOnce fixed, reload the config by typing '.vfpc reload'") % config.GetParseError() % config.GetErrorOffset());
		sendMessage(msg);

		config.Parse<0>("[{\"icao\": \"XXXX\"}]");
	}

	airports.clear();

	for (SizeType i = 0; i < config.Size(); i++) {
		const Value& airport = config[i];
		string airport_icao = airport["icao"].GetString();

		airports.insert(pair<string, SizeType>(airport_icao, i));
	}
}

// Does the checking and magic stuff, so everything will be alright when this is finished! Or not. Who knows?
vector<string> CVFPCPlugin::validizeSid(CFlightPlan flightPlan) {
	vector<string> returnValid{}; // 0 = Callsign, 1 = SID Validity, 2 = SID Name, 3 = Engine Type, 4 = Airways, 5 = Nav Performance, 6 = Destination, 7 = Min/Max Flight Level, 8 = Even/Odd, 9 = Passed/Failed

	returnValid.push_back(flightPlan.GetCallsign());
	for (int i = 0; i < 10; i++) {
		returnValid.push_back("-");
	}

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

	// Flightplan has SID
	if (!sid.length()) {
		returnValid[1] = "Invalid";
		returnValid[2] = "No SID Set";
		returnValid[9] = "Failed";
		return returnValid;
	}

	string first_wp = sid.substr(0, sid.find_first_of("0123456789"));
	if (0 != first_wp.length())
		boost::to_upper(first_wp);
	string sid_suffix;
	if (first_wp.length() != sid.length()) {
		sid_suffix = sid.substr(sid.find_first_of("0123456789"), sid.length());
		boost::to_upper(sid_suffix);
	}
	string first_airway;

	// Did not find a valid SID
	if (0 == sid_suffix.length() && "VCT" != first_wp) {
		returnValid[1] = "Invalid";
		returnValid[2] = "No SID Set";
		returnValid[9] = "Failed";
		return returnValid;
	}

	vector<string>::iterator it = find(route.begin(), route.end(), first_wp);
	if (it != route.end() && (it - route.begin()) != route.size() - 1) {
		first_airway = route[(it - route.begin()) + 1];
		boost::to_upper(first_airway);
	}

	// Airport defined
	if (airports.find(origin) == airports.end()) {
		returnValid[1] = "Invalid";
		returnValid[2] = "Airport Not Found";
		returnValid[9] = "Failed";
		return returnValid;
	}
	else
		origin_int = airports[origin];

	// Any SIDs defined
	if (!config[origin_int].HasMember("sids") || config[origin_int]["sids"].IsArray()) {
		returnValid[1] = "Invalid";
		returnValid[2] = "No SIDs Defined";
		returnValid[9] = "Failed";
		return returnValid;
	}

	// Needed SID defined
	if (!config[origin_int]["sids"].HasMember(first_wp.c_str()) || !config[origin_int]["sids"][first_wp.c_str()].IsArray()) {
		returnValid[1] = "Invalid";
		returnValid[2] = "No Valid SID";
		returnValid[9] = "Failed";
		return returnValid;
	}

	const Value& conditions = config[origin_int]["sids"][first_wp.c_str()];

	int round = 0;
	bool cont = true;

	vector<bool> validity;
	vector<bool> new_validity;
	vector<string> results;
	int min_fl;
	int max_fl;

	while (cont && round < 7) {
		new_validity = {};
		results = {};

		for (SizeType i = 0; i < conditions.Size(); i++) {
			if (i == 0 || validity[i]) {
				switch (round) {
					case 0:
						// SID Suffix
						if (!conditions[i]["suffix"].IsString() && conditions[i]["suffix"].GetString() == sid_suffix) {
							new_validity.push_back(true);
						}
						else {
							new_validity.push_back(false);
							results.push_back(conditions[i]["suffix"].GetString());
						}
						break;
					case 1:
						//Engines (P=piston, T=turboprop, J=jet, E=electric)
						if (conditions[i]["engine"].IsString()) {
							if (conditions[i]["engine"].GetString()[0] == flightPlan.GetFlightPlanData().GetEngineType()) {
								returnValid[5] = "Passed Engine type (" + (string)conditions[i]["engine"].GetString() + ')';
								new_validity.push_back(true);
							}
							else {
								returnValid[5] = "Failed Engine type. Needed Type: " + (string)conditions[i]["engine"].GetString();
								new_validity.push_back(false);
								results.push_back(conditions[i]["engine"].GetString());
							}
						}
						else if (conditions[i]["engine"].IsArray() && conditions[i]["engine"].Size()) {
							if (arrayContains(conditions[i]["engine"], flightPlan.GetFlightPlanData().GetEngineType())) {
								returnValid[5] = "Passed Engine type (" + arrayToString(conditions[i]["engine"], ',') + ")";
								new_validity.push_back(true);
							}
							else {
								returnValid[5] = "Failed Engine type. Needed Type: " + arrayToString(conditions[i]["engine"], ',');
								new_validity.push_back(false);
								for (SizeType j = 0; j < conditions[i]["engine"].Size(); i++) {
									results.push_back(conditions[i]["engine"][j].GetString());			
								}
							}
						}
						else {
							returnValid[5] = "No Engine type restriction";
							new_validity.push_back(true);
						}
						break;
					case 2:
						//Airways
						bool isAllowedAirways = false;
						bool isBannedAirways = false;

						string perms = "";

						if (conditions[i]["airways"].IsArray() && conditions[i]["airways"].Size()) {
							isAllowedAirways = true;

							perms += "Initial Airways: ";
							perms += conditions[i]["airways"][(SizeType)0].GetString();

							for (SizeType j = 1; j < conditions[i]["airways"].Size(); j++) {
								perms += " or ";
								perms += conditions[i]["airways"][j].GetString();
							}
						}

						if (conditions[i]["no_airways"].IsArray() && conditions[i]["no_airways"].Size()) {
							isBannedAirways = true;

							if (perms == "") {
								perms += "Initial Airways Not: ";
							}
							else {
								perms += " but not ";
							}

							for (SizeType j = 0; j < conditions[i]["airways"].Size(); j++) {
								perms += " or ";
								perms += conditions[i]["airways"][j].GetString();
							}
						}

						if (isAllowedAirways || isBannedAirways) {
							bool min = false;
							bool max = false;

							if (conditions[i].HasMember("min_fl") && (min_fl = conditions[i]["min_fl"].GetInt()) > 0) {
								min = true;
							}

							if (conditions[i].HasMember("max_fl") && (max_fl = conditions[i]["max_fl"].GetInt()) > 0) {
								max = true;
							}

							if (min && max) {
								perms += " (FL" + to_string(min_fl) + " - " + to_string(max_fl) + ")";
							}
							else if (min) {
								perms += " (FL" + to_string(min_fl) + "-)";
							}
							else if (max) {
								perms += " (FL" + to_string(max_fl) + "+)";
							}
							else {
								perms += " (All Levels)";
							}

							bool allowedPassed = false;
							bool bannedPassed = false;

							string rte = flightPlan.GetFlightPlanData().GetRoute();
							vector<string> awys = {};

							string delimiter = " ";
							size_t pos = 0;
							string s;

							bool last = false;

							while (!last) {
								pos = rte.find(delimiter);
								if (pos == string::npos) {
									last = true;
								}

								s = rte.substr(0, pos);

								if (any_of(s.begin(), s.end(), ::isdigit) && s.find_first_of('/') == string::npos) {
									awys.push_back(s);
								}

								if (last) {
									rte = "";
								}
								else {
									rte.erase(0, pos + delimiter.length());
								}
							}


							if (!isAllowedAirways || routeContains(awys, conditions[i]["airways"])) {
								allowedPassed = true;
							}

							if (!isBannedAirways || !routeContains(awys, conditions[i]["no_airways"])) {
								bannedPassed = true;
							}

							if (allowedPassed && bannedPassed) {
								returnValid[4] = "Passed Airways";
								new_validity.push_back(true);
							}
							else {
								returnValid[4] = "Failed Airways";
								new_validity.push_back(false);
								results.push_back(perms);
							}
						}
						else {
							returnValid[4] = "No Airway restriction";
							new_validity.push_back(true);
						}
						break;
					case 3:
						//Nav Perf
						if (conditions[i]["navigation"].IsString()) {
							string navigation_constraints(conditions[i]["navigation"].GetString());
							if (string::npos == navigation_constraints.find_first_of(flightPlan.GetFlightPlanData().GetCapibilities())) {
								returnValid[9] = "Failed navigation capability restriction. Req. capability: " + navigation_constraints;
								new_validity.push_back(false);

								for (int i = 0; i < navigation_constraints.length(); i++) {
									results.push_back(string(1, navigation_constraints[i]));
								}
							}
							else {
								returnValid[9] = "No navigation capability restriction";
								new_validity.push_back(true);
							}
						}
						else {
							returnValid[9] = "No navigation capability restriction";
							new_validity.push_back(true);
						}
						break;
					case 4:
						bool perm = true;

						if (conditions[i]["no_destinations"].IsArray() && conditions[i]["no_destinations"].Size()) {
							string dest;
							if ((dest = destArrayContains(conditions[i]["no_destinations"], destination.c_str())).size()) {
								if (dest.size() < 4)
									dest += string(4 - dest.size(), '*');
								returnValid[3] = "Banned Destination (" + dest + ")";
								perm = false;
							}
						}

						if (perm && conditions[i]["destinations"].IsArray() && conditions[i]["destinations"].Size()) {
							string dest;
							if ((dest = destArrayContains(conditions[i]["destinations"], destination.c_str())).size()) {
								if (dest.size() < 4)
									dest += string(4 - dest.size(), '*');
								returnValid[3] = "Passed Destination (" + dest + ")";
								perm = true;
							}
							else {
								returnValid[3] = "Banned Destination (" + destination + ")";
								perm = false;
							}
						}
						else if (perm) {
							returnValid[3] = "No Destination Restriction";
							perm = true;
						}

						validity.push_back(perm);
						break;
					case 5:
						int min_fl, max_fl;
						bool lvl = true;
						//Min Level
						if (conditions[i].HasMember("min_fl") && (min_fl = conditions[i]["min_fl"].GetInt()) > 0) {
							if ((RFL / 100) >= min_fl) {
								returnValid[7] = "Passed Minimum Flight Level (" + to_string(conditions[i]["min_fl"].GetInt()) + ')';
							}
							else {
								returnValid[7] = "Failed Minimum Flight Level. Min FL: " + to_string(min_fl);
								lvl = false;
							}
						}
						else {
							returnValid[7] = "No Minimum Flight Level";
						}
						break;

						//Max Level
						if (lvl && conditions[i].HasMember("max_fl") && (max_fl = conditions[i]["max_fl"].GetInt()) > 0) {
							if ((RFL / 100) <= max_fl) {
								returnValid[8] = "Passed Maximum Flight Level (" + to_string(conditions[i]["max_fl"].GetInt()) + ')';
							}
							else {
								returnValid[8] = "Failed Maximum Flight Level. Max FL: " + to_string(max_fl);
								lvl = false;
							}
						}
						else {
							returnValid[8] = "No Maximum Flight Level";
						}
					case 6:
						//Even/Odd Levels
						string direction = conditions[i]["direction"].GetString();
						boost::to_upper(direction);

						if (direction == "EVEN") {
							if ((RFL / 1000) % 2 == 0) {
								returnValid[6] = "Passed Even";
								validity.push_back(true);
							}
							else {
								returnValid[6] = "Failed Even";
								validity.push_back(false);
							}
						}
						else if (direction == "ODD") {
							if ((RFL / 1000) % 2 != 0) {
								returnValid[6] = "Passed Odd";
								validity.push_back(true);
							}
							else {
								returnValid[6] = "Failed Odd";
								validity.push_back(false);
							}
						}
						else if (direction == "ANY") {
							returnValid[6] = "No Direction restriction";
							validity.push_back(true);
						}
						else {
							string errorText{ "Config Error for Even/Odd on SID: " };
							errorText += first_wp;
							sendMessage("Error", errorText);
							returnValid[6] = "Config Error for Even/Odd on this SID!";
							validity.push_back(false);
						}
						break;
				}
			}
		}

		if (all_of(new_validity.begin(), new_validity.end(), [](bool v) { return !v; })) {
			cont = false;
		}
		else {
			validity = new_validity;
			new_validity = {};
			round++;
		}
	}

	returnValid[0] = flightPlan.GetCallsign();
	for (int i = 1; i < 10; i++) {
		returnValid[i] = "-";
	}

	switch (round) {
		case 7:
			vector<bool>::iterator itr = find(validity.begin(), validity.end(), true);
			int i = std::distance(validity.begin(), itr);

			returnValid[8] = "Passed Even/Odd Level.";
			returnValid[9] = "Passed";
			break;
		case 6:
			if (round == 6) {

			}

			returnValid[7] = "Passed Min/Max Level.";
		case 5:
			if (round == 5) {

			}

			returnValid[6] = "Passed Destination.";
		case 4:
			if (round == 4) {

			}

			returnValid[5] = "Passed Navigation Performance.";
		case 3:
			if (round == 3) {
				sort(results.begin(), results.end());
				results.erase(unique(results.begin(), results.end()), results.end());

				string out = "";

				for (string each : results) {
					out += each + ", ";
				}

				returnValid[5] = "Failed Navigation Performance. Required Performance: " + out.substr(0, out.length() - 2) + ".";
			}

			returnValid[4] = "Passed Airways.";
		case 2:
			if (round == 2) {
				returnValid[4] = "Failed Airways.";
			}

			returnValid[3] = "Passed Engine Type." ;
		case 1:
			if (round == 1) {
				sort(results.begin(), results.end());
				results.erase(unique(results.begin(), results.end()), results.end());

				string out = "";

				for (string each : results) {
					if (each == "P") {
						out += "Piston, ";
					}
					else if (each == "T") {
						out += "Turboprop, ";
					}
					else if (each == "J") {
						out += "Jet, ";
					}
					else if (each == "E") {
						out += "Electric, ";
					}
				}

				returnValid[3] = "Failed Engine Type. Needed Type : " + out.substr(0, out.length() - 2) + ".";
			}

			returnValid[1] = "Valid";
			returnValid[2] = sid;
			break;

		case 0:
			sort(results.begin(), results.end());
			results.erase(unique(results.begin(), results.end()), results.end());

			string out = "";

			for (vector<string>::iterator itr = results.begin(); itr != results.end(); ++itr) {
				out += *itr + ", ";
			}

			returnValid[1] = "Invalid";
			returnValid[2] = sid + " Contains Invalid Suffix. Valid Suffices: " + out.substr(0, out.length() - 2) + ".";

			break;

	}



	bool passed[7]{ false };
	valid = false;



	for (SizeType i = 0; i < conditions.Size(); i++) {
		
		valid = true;
		returnValid[1] = "Valid";
		returnValid[2] = first_wp;

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
			returnValid[9] = "Passed";
			break;
		}
		else {
			if (!passed[0] || !passed[1])
				continue;
			else
				break;
		}
	}
	
	if (!valid) {
		returnValid[1] = "Invalid";
		returnValid[2] = "No Valid SID";
		returnValid[9] = "Failed";
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
	string buffer{ messageBuffer.at(1) + " Routing" };
	if (messageBuffer.at(1) == "Valid") {
		buffer += " | found SID: ";
		for (int i = 2; i < 10; i++) {
			buffer += messageBuffer.at(i);
			buffer += " | ";
		}
		buffer += messageBuffer.at(10);
	} else {
		buffer += " | " + messageBuffer.at(2);
		buffer += " | " + messageBuffer.at(10);
		buffer += " | Check complete.";
	}
	sendMessage(messageBuffer.at(0), buffer);
}

string CVFPCPlugin::getFails(vector<string> messageBuffer) {
	vector<string> fail;
	fail.push_back("FPL");

	if (messageBuffer.at(1).find("Invalid") == 0) {
		fail.push_back("SID");
	}
	if (messageBuffer.at(3).find("Banned") == 0) {
		fail.push_back("DST");
	}
	if (messageBuffer.at(4).find("Failed") == 0) {
		fail.push_back("AWY");
	}
	if (messageBuffer.at(5).find("Failed") == 0) {
		fail.push_back("ENG");
	}

	if (messageBuffer.at(6).find("Failed") == 0) {
		fail.push_back("E/O");
	}
	if (messageBuffer.at(7).find("Failed") == 0) {
		fail.push_back("MIN");
	}
	if (messageBuffer.at(8).find("Failed") == 0) {
		fail.push_back("MAX");
	}
	if (messageBuffer.at(9).find("Failed") == 0) {
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