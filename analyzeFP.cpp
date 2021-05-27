#include "stdafx.h"
#include "analyzeFP.hpp"
#include <curl/curl.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

bool blink, debugMode, validVersion, autoLoad, sidsLoaded;

size_t failPos, relCount;

using namespace std;
using namespace EuroScopePlugIn;

// Run on Plugin Initialization
CVFPCPlugin::CVFPCPlugin(void) :CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE, MY_PLUGIN_NAME, MY_PLUGIN_VERSION, MY_PLUGIN_DEVELOPER, MY_PLUGIN_COPYRIGHT)
{
	blink = false;
	debugMode = false;
	validVersion = true; //Reset in first timer call
	autoLoad = true;
	sidsLoaded = false;

	failPos = 0;
	relCount = 0;

	string loadingMessage = "Loading complete. Version: ";
	loadingMessage += MY_PLUGIN_VERSION;
	loadingMessage += ".";
	sendMessage(loadingMessage);

	// Register Tag Item "VFPC"
	if (validVersion) {
		RegisterTagItemType("VFPC", TAG_ITEM_FPCHECK);
		RegisterTagItemFunction("Show Checks", TAG_FUNC_CHECKFP_MENU);
	}
}

// Run on Plugin destruction, Ie. Closing EuroScope or unloading plugin
CVFPCPlugin::~CVFPCPlugin()
{
}

/*
	Custom Functions
*/

/*size_t CVFPCPlugin::WriteFunction(void *contents, size_t size, size_t nmemb, void *out)
{
	// For Curl, we should assume that the data is not null terminated, so add a null terminator on the end
	((std::string*)out)->append(reinterpret_cast<char*>(contents) + '\0', size * nmemb);
	return size * nmemb;
}*/

static size_t curlCallback(void *contents, size_t size, size_t nmemb, void *outString)
{
	// For Curl, we should assume that the data is not null terminated, so add a null terminator on the end
 	((std::string*)outString)->append(reinterpret_cast<char*>(contents), size * nmemb);
	return size * nmemb;
}

void CVFPCPlugin::debugMessage(string type, string message) {
	// Display Debug Message if debugMode = true
	if (debugMode) {
		DisplayUserMessage("VFPC Log", type.c_str(), message.c_str(), true, true, true, false, false);
	}
}

void CVFPCPlugin::sendMessage(string type, string message) {
	// Show a message
	DisplayUserMessage("VFPC", type.c_str(), message.c_str(), true, true, true, true, false);
}

void CVFPCPlugin::sendMessage(string message) {
	DisplayUserMessage("VFPC", "System", message.c_str(), true, true, true, false, false);
}

void CVFPCPlugin::webCall(string endpoint, Document& out) {
	CURL* curl = curl_easy_init();
	string url = MY_API_ADDRESS + endpoint;

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

	uint64_t httpCode = 0;
	std::string readBuffer;

	//curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	//curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlCallback);

	curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
	curl_easy_cleanup(curl);

	if (httpCode == 200)
	{
		if (out.Parse<0>(readBuffer.c_str()).HasParseError())
		{
			sendMessage("An error occurred whilst reading data. The plugin will not automatically attempt to reload from the API. To restart data fetching, type \".vfpc load\".");
			debugMessage("Error", str(boost::format("Config Parse: %s (Offset: %i)\n'") % config.GetParseError() % config.GetErrorOffset()));

			out.Parse<0>("[{\"Icao\": \"XXXX\"}]");
		}
	}
	else
	{
		sendMessage("An error occurred whilst downloading data. The plugin will not automatically attempt to reload from the API. Check your connection and restart data fetching by typing \".vfpc load\".");
		debugMessage("Error", str(boost::format("Config Download: %s (Offset: %i)\n'") % config.GetParseError() % config.GetErrorOffset()));

		out.Parse<0>("[{\"Icao\": \"XXXX\"}]");
	}
}

bool CVFPCPlugin::checkVersion() {
	Document version;
	webCall("version", version);
	
	if (version.HasMember("VFPC_Version") && version["VFPC_Version"].IsString()) {
		vector<string> current = split(version["VFPC_Version"].GetString(), '.');
		vector<string> installed = split(MY_PLUGIN_VERSION, '.');

		if (installed[0] >= current[0] && installed[1] >= current[1] && installed[2] >= current[2]) {
			return true;
		}
		else {
			sendMessage("Update available - the plugin has been disabled. Please update and reload the plugin to continue. (Note: .vfpc load will NOT work.)");
		}
	}
	else {
		sendMessage("Failed to check for updates - the plugin has been disabled. If no updates are available, please unload and reload the plugin to try again. (Note: .vfpc load will NOT work.)");
	}

	return false;
}

void CVFPCPlugin::getSids() {
	webCall("final", config);

	airports.clear();

	for (SizeType i = 0; i < config.Size(); i++) {
		const Value& airport = config[i];
		string airport_icao = airport["Icao"].GetString();

		airports.insert(pair<string, SizeType>(airport_icao, i));
	}
}

// Does the checking and magic stuff, so everything will be alright when this is finished! Or not. Who knows?
vector<vector<string>> CVFPCPlugin::validizeSid(CFlightPlan flightPlan) {
	//out[0] = Normal Output, out[1] = Debug Output
	vector<vector<string>> returnOut = { vector<string>(), vector<string>() }; // 0 = Callsign, 1 = SID, 2 = Engine Type, 3 = Airways, 4 = Nav Performance, 5 = Destination, 6 = Min/Max Flight Level, 7 = Even/Odd, 8 = Syntax, 9 = Passed/Failed

	returnOut[0].push_back(flightPlan.GetCallsign());
	returnOut[1].push_back(flightPlan.GetCallsign());
	for (int i = 1; i < 10; i++) {
		returnOut[0].push_back("-");
		returnOut[1].push_back("-");
	}

	string origin = flightPlan.GetFlightPlanData().GetOrigin(); boost::to_upper(origin);
	string destination = flightPlan.GetFlightPlanData().GetDestination(); boost::to_upper(destination);
	SizeType origin_int;
	int RFL = flightPlan.GetFlightPlanData().GetFinalAltitude();

	vector<string> route = split(flightPlan.GetFlightPlanData().GetRoute(), ' ');
	for (size_t i = 0; i < route.size(); i++) {
		boost::to_upper(route[i]);
	}

	// Remove Speed/Alt Data From Route
	regex lvl_chng("(N|M)[0-9]{3,4}(A|F)[0-9]{3}$");

	// Remove "DCT" And Speed/Level Change Instances from Route
	for (size_t i = 0; i < route.size(); i++) {
		int count = 0;
		size_t pos = 0;

		for (size_t j = 0; j < route[i].size(); j++) {
			if (route[i][j] == '/') {
				count++;
				pos = j;
			}
		}

		switch (count) {
			case 0:
			{
				break;
			}
			case 2:
			{
				size_t first_pos = route[i].find('/');
				route[i] = route[i].substr(first_pos, string::npos);
			}
			case 1:
			{
				if (route[i].size() > pos + 1 && regex_match((route[i].substr(pos + 1, string::npos)), lvl_chng)) {
					route[i] = route[i].substr(0, pos);
					break;
				}
				else {
					returnOut[0][8] = "Invalid Speed/Level Change";
					returnOut[0][9] = "Failed";

					returnOut[1][8] = "Invalid Route Item: " + route[i];
					returnOut[1][9] = "Failed";
					return returnOut;
				}
			}
			default:
			{
				returnOut[0][8] = "Invalid Syntax - Too Many \"/\" Characters in One or More Waypoints";
				returnOut[0][9] = "Failed";

				returnOut[0][8] = "Invalid Route Item: " + route[i];
				returnOut[0][9] = "Failed";
				return returnOut;
			}
		}
	}
	for (size_t i = 0; i < route.size(); i++) {
		if (route[i] == "DCT") {
			route.erase(route.begin() + i);
		}
	}

	//Remove Speed/Level Data From Start Of Route
	if (regex_match(route[0], lvl_chng)) {
		route.erase(route.begin());
	}

	string sid = flightPlan.GetFlightPlanData().GetSidName(); boost::to_upper(sid);

	// Remove any # characters from SID name
	boost::erase_all(sid, "#");

	// Flightplan has SID
	if (!sid.length()) {
		returnOut[0][1] = returnOut[1][1] = "Invalid SID - None Set";
		returnOut[0][9] = returnOut[1][9] = "Failed";
		return returnOut;
	}

	string first_wp = sid.substr(0, sid.find_first_of("0123456789"));
	if (0 != first_wp.length())
		boost::to_upper(first_wp);
	string sid_suffix;
	if (first_wp.length() != sid.length()) {
		sid_suffix = sid.substr(sid.find_first_of("0123456789"), sid.length());
		boost::to_upper(sid_suffix);
	}

	// Did not find a valid SID
	if (0 == sid_suffix.length() && "VCT" != first_wp) {
		returnOut[0][1] = returnOut[1][1] = "Invalid SID - None Set";
		returnOut[0][9] = returnOut[1][9] = "Failed";
		return returnOut;
	}

	// Check First Waypoint Correct. Remove SID References & First Waypoint From Route.
	bool success = false;
	bool stop = false;
	
	while (!stop) {
		size_t wp_size = first_wp.size();
		size_t entry_size = route[0].size();
		if (route[0].substr(0, wp_size) == first_wp) {
			//First Waypoint
			if (wp_size == entry_size) {
				success = true;
			}
			//3 or 5 Letter Waypoint SID - In Full
			else if (entry_size > wp_size && isdigit(route[0][wp_size])) {
				//SID Has Letter Suffix
				for (size_t i = wp_size + 1; i < entry_size; i++) {
					if (!isalpha(route[0][i])) {
						stop = true;
					}
				}
			}
			else {
				stop = true;
			}

			route.erase(route.begin());
		}
		//5 Letter Waypoint SID - Abbreviated to 6 Chars
		else if (wp_size == 5 && entry_size >= wp_size && isdigit(route[0][wp_size - 1])) {
			//SID Has Letter Suffix
			for (size_t i = wp_size; i < entry_size; i++) {
				if (!isalpha(route[0][i])) {
					stop = true;
				}
			}

			route.erase(route.begin());
		}
		else {
			stop = true;
		}
	}
	
	if (!success) {
		returnOut[0][1] = "Invalid SID - Route Not From Final SID Fix";
		returnOut[0][9] = "Failed";

		returnOut[1][1] = "Invalid SID - Route must start at " + first_wp + ".";
		returnOut[1][9] = "Failed";
		return returnOut;
	}

	// Airport defined
	if (airports.find(origin) == airports.end()) {
		returnOut[0][1] = "Invalid SID - Airport Not Found";
		returnOut[0][9] = "Failed";

		returnOut[1][1] = "Invalid SID - " + origin + " not in database.";
		returnOut[1][9] = "Failed";
		return returnOut;
	}
	else
	{
		origin_int = airports[origin];
	}

	// Any SIDs defined
	if (!config[origin_int].HasMember("Sids") || !config[origin_int]["Sids"].IsArray()) {
		returnOut[0][1] = "Invalid SID - None Defined";
		returnOut[0][9] = "Failed";

		returnOut[1][1] = "Invalid SID - " + origin + " exists in database but has no SIDs defined.";
		returnOut[1][9] = "Failed";
		return returnOut;
	}
	size_t pos = string::npos;

	for (size_t i = 0; i < config[origin_int]["Sids"].Size(); i++) {
		if (config[origin_int]["Sids"][i].HasMember("Point") && !first_wp.compare(config[origin_int]["Sids"][i]["Point"].GetString()) && config[origin_int]["Sids"][i].HasMember("Constraints") && config[origin_int]["Sids"][i]["Constraints"].IsArray()) {
			pos = i;
		}
	}

	// Needed SID defined
	if (pos != string::npos) {
		const Value& conditions = config[origin_int]["Sids"][pos]["Constraints"];

		int round = 0;
		bool cont = true;

		vector<bool> validity, new_validity;
		vector<string> results;
		int Min, Max;
			
		while (cont && round < 7) {
			new_validity = {};
			results = {};

			for (SizeType i = 0; i < conditions.Size(); i++) {
				if (round == 0 || validity[i]) {
					switch (round) {
					case 0:
					{
						// SID Suffix
						if (!conditions[i]["suffix"].IsString() || conditions[i]["suffix"].GetString() == sid_suffix) {
							new_validity.push_back(true);
						}
						else {
							new_validity.push_back(false);
							results.push_back(conditions[i]["suffix"].GetString());
						}
						break;
					}
					case 1:
					{
						//Engines (P=piston, T=turboprop, J=jet, E=electric)
						if (conditions[i]["engine"].IsString()) {
							if (conditions[i]["engine"].GetString()[0] == flightPlan.GetFlightPlanData().GetEngineType()) {
								new_validity.push_back(true);
							}
							else {
								new_validity.push_back(false);
								results.push_back(conditions[i]["engine"].GetString());
							}
						}
						else if (conditions[i]["engine"].IsArray() && conditions[i]["engine"].Size()) {
							if (arrayContains(conditions[i]["engine"], flightPlan.GetFlightPlanData().GetEngineType())) {
								new_validity.push_back(true);
							}
							else {
								new_validity.push_back(false);
								for (SizeType j = 0; j < conditions[i]["engine"].Size(); i++) {
									results.push_back(conditions[i]["engine"][j].GetString());
								}
							}
						}
						else {
							new_validity.push_back(true);
						}
						break;
					}
					case 2:
					{
						bool perm = true;

						if (conditions[i]["NoDests"].IsArray() && conditions[i]["NoDests"].Size()) {
							string dest;
							if (destArrayContains(conditions[i]["NoDests"], destination.c_str()).size()) {
								perm = false;
							}
						}

						if (conditions[i]["Dests"].IsArray() && conditions[i]["Dests"].Size()) {
							string dest;
							if (!destArrayContains(conditions[i]["Dests"], destination.c_str()).size()) {
								perm = false;
							}
						}

						new_validity.push_back(perm);

						if (!perm) {
							string perms = "";

							if (conditions[i]["Dests"].IsArray() && conditions[i]["Dests"].Size()) {
								for (SizeType j = 0; j < conditions[i]["Dests"].Size(); j++) {
									string dest = conditions[i]["Dests"][j].GetString();

									if (dest.size() < 4)
										dest += string(4 - dest.size(), '*');

									if (j != 0) {
										perms += ",";
									}

									perms += dest;
								}
							}
							else {
								perms += " ";
							}

							perms += ";";

							if (conditions[i]["NoDests"].IsArray() && conditions[i]["NoDests"].Size()) {
								for (SizeType j = 0; j < conditions[i]["NoDests"].Size(); j++) {
									string dest = conditions[i]["NoDests"][j].GetString();

									if (dest.size() < 4)
										dest += string(4 - dest.size(), '*');

									if (j != 0) {
										perms += ",";
									}

									perms += dest;
								}
							}
							else {
								perms += " ";
							}

							results.push_back(perms);
						}

						break;
					}
					case 3:
					{
						//Airways
						bool testAllowed = false;
						bool testBanned = false;

						string perms = "";


						if (conditions[i]["Route"].IsArray() && conditions[i]["Route"].Size()) {
							testAllowed = true;

							perms += conditions[i]["Route"][(SizeType)0].GetString();

							for (SizeType j = 1; j < conditions[i]["Route"].Size(); j++) {
								perms += " or ";
								perms += conditions[i]["Route"][j].GetString();
							}
						}

						if (conditions[i]["NoRoute"].IsArray() && conditions[i]["NoRoute"].Size()) {
							testBanned = true;

							if (perms != "") {
								perms += " but ";
							}

							perms += "not ";

							perms += conditions[i]["NoRoute"][(SizeType)0].GetString();

							for (SizeType j = 1; j < conditions[i]["NoRoute"].Size(); j++) {
								perms += ", ";
								perms += conditions[i]["NoRoute"][j].GetString();
							}
						}

						if (testAllowed || testBanned) {
							bool min = false;
							bool max = false;

							if (conditions[i].HasMember("Min") && (Min = conditions[i]["Min"].GetInt()) > 0) {
								min = true;
							}

							if (conditions[i].HasMember("Max") && (Max = conditions[i]["Max"].GetInt()) > 0) {
								max = true;
							}

							if (min && max) {
								perms += " (FL" + to_string(Min) + " - " + to_string(Max) + ")";
							}
							else if (min) {
								perms += " (FL" + to_string(Min) + "+)";
							}
							else if (max) {
								perms += " (FL" + to_string(Max) + "-)";
							}
							else {
								perms += " (All Levels)";
							}

							bool allowedPassed = false;
							bool bannedPassed = false;

							if (testAllowed && routeContains(flightPlan.GetCallsign(), route, conditions[i]["Route"])) {
								allowedPassed = true;
							}

							if (!testBanned || !routeContains(flightPlan.GetCallsign(), route, conditions[i]["NoRoute"])) {
								bannedPassed = true;
							}

							if (allowedPassed && bannedPassed) {
								new_validity.push_back(true);
							}
							else {
								new_validity.push_back(false);
								results.push_back(perms);
							}
						}
						else {
							new_validity.push_back(true);
						}

						break;
					}
					case 4:
					{
						//Nav Perf
						if (conditions[i]["navigation"].IsString()) {
							string navigation_constraints(conditions[i]["navigation"].GetString());
							if (string::npos == navigation_constraints.find_first_of(flightPlan.GetFlightPlanData().GetCapibilities())) {
								new_validity.push_back(false);

								for (size_t i = 0; i < navigation_constraints.length(); i++) {
									results.push_back(string(1, navigation_constraints[i]));
								}
							}
							else {
								new_validity.push_back(true);
							}
						}
						else {
							new_validity.push_back(true);
						}
						break;
					}
					case 5:
					{
						bool res_minmax = true;

						//Min Level
						if (conditions[i].HasMember("Min") && (Min = conditions[i]["Min"].GetInt()) > 0 && (RFL / 100) <= Min) {
							res_minmax = false;
						}

						//Max Level
						if (conditions[i].HasMember("Max") && (Max = conditions[i]["Max"].GetInt()) > 0 && (RFL / 100) >= Max) {
							res_minmax = false;
						}

						new_validity.push_back(res_minmax);
						break;
					}
					case 6:
					{
						//Even/Odd Levels
						string direction = conditions[i]["Dir"].GetString();
						boost::to_upper(direction);

						if (direction == "EVEN") {
							if ((RFL > 41000 && (RFL / 1000 - 41) % 4 == 2)) {
								new_validity.push_back(true);
							}
							else if (RFL <= 41000 && (RFL / 1000) % 2 == 0) {
								new_validity.push_back(true);
							}
							else {
								new_validity.push_back(false);
							}
						}
						else if (direction == "ODD") {
							if ((RFL > 41000 && (RFL / 1000 - 41) % 4 == 0)) {
								new_validity.push_back(true);
							}
							else if (RFL <= 41000 && (RFL / 1000) % 2 == 1) {
								new_validity.push_back(true);
							}
							else {
								new_validity.push_back(false);
							}
						}
						else { //(direction == "ANY")
							new_validity.push_back(true);
						}
						break;
					}
					}
				}
				else {
					new_validity.push_back(false);
				}
			}

			if (all_of(new_validity.begin(), new_validity.end(), [](bool v) { return !v; })) {
				cont = false;
			}
			else {
				validity = new_validity;
				round++;
			}
		}

		returnOut[0][0] = flightPlan.GetCallsign();
		returnOut[1][0] = flightPlan.GetCallsign();
		for (int i = 1; i < 10; i++) {
			returnOut[0][i] = "-";
			returnOut[1][i] = "-";
		}

		returnOut[0][9] = "Failed";
		returnOut[1][9] = "Failed";

		vector<int> successes{};

		for (size_t i = 0; i < validity.size(); i++) {
			if (validity[i]) {
				successes.push_back(i);
			}
		}

		switch (round) {
		case 7:
		{
			returnOut[0][7] = "Passed Level Direction.";
			returnOut[0][9] = "Passed";

			returnOut[1][7] = "Passed Level Direction: \"" + string(conditions[validity[0]]["Dir"].GetString()) + "\" Required.";
			returnOut[1][9] = "Passed";
		}
		case 6:
		{
			if (round == 6) {
				string res = "";
				returnOut[0][7] = "Failed Level Direction: \"" + string(conditions[validity[0]]["Dir"].GetString()) + "\" Required.";
				returnOut[1][7] = "Failed Level Direction: \"" + string(conditions[validity[0]]["Dir"].GetString()) + "\" Required.";
			}

			returnOut[0][6] = "Passed Min/Max Level.";
			returnOut[1][6] = "Passed " + MinMaxOutput(origin_int, pos, successes);
		}
		case 5:
		{
			if (round == 5) {
				returnOut[0][6] = "Failed " + MinMaxOutput(origin_int, pos, successes);
				returnOut[1][6] = returnOut[0][6];
			}

			returnOut[0][5] = "Passed Navigation Performance.";
		}

		case 4:
		{
			if (round == 4) {
				sort(results.begin(), results.end());
				results.erase(unique(results.begin(), results.end()), results.end());

				string out = "";

				for (string each : results) {
					out += each + ", ";
				}

				returnOut[0][5] = "Failed Navigation Performance. Required Performance: " + out.substr(0, out.length() - 2) + ".";
			}

			returnOut[0][4] = "Passed Route.";
		}
		case 3:
		{
			if (round == 3) {
				string out = "Valid Initial Routes: ";

				for (string each : results) {
					out += each + " / ";
				}

				returnOut[0][4] = "Failed Route - " + out.substr(0, out.length() - 3) + ".";
			}

			returnOut[0][3] = "Passed Destination.";
		}
		case 2:
		{
			if (round == 2) {
				vector<vector<string>> res{ vector<string>{}, vector<string>{} };

				for (string each : results) {
					bool added = false;
					vector<string> elements = split(each, ';');
					vector<string> good_new_eles{};
					vector<string> bad_new_eles{};

					if (elements[0] != " ") {
						good_new_eles = split(elements[0], ',');
					}

					if (elements[1] != " ") {
						bad_new_eles = split(elements[1], ',');
					}					

					
					for (string dest : res[0]) {
						//Remove Duplicates from Whitelist
						for (size_t k = good_new_eles.size(); k > 0; k--) {
							string new_ele = good_new_eles[k - 1];
							if (new_ele.compare(dest) == 0) {
								good_new_eles.erase(good_new_eles.begin() + k - 1);
							}
						}

						//Prevent Previously Whitelisted Elements from Being Blacklisted
						for (size_t k = bad_new_eles.size(); k > 0; k--) {
							string new_ele = bad_new_eles[k - 1];
							if (new_ele.compare(dest) == 0) {
								bad_new_eles.erase(bad_new_eles.begin() + k - 1);
							}
						}
					}

					//Whitelist Previously Blacklisted Elements
					for (string dest : good_new_eles) {
						
						for (size_t k = res[1].size(); k > 0; k--) {
							string new_ele = res[1][k - 1];
							if (new_ele.compare(dest) == 0) {
								res[1].erase(res[1].begin() + k - 1);
							}
						}
					}

					//Remove Duplicates from Blacklist
					for (string dest : res[1]) {
						for (size_t k = bad_new_eles.size(); k > 0; k--) {
							string new_ele = bad_new_eles[k - 1];
							if (new_ele.compare(dest) == 0) {
								bad_new_eles.erase(bad_new_eles.begin() + k - 1);
							}
						}
					}

					res[0].insert(res[0].end(), good_new_eles.begin(), good_new_eles.end());
					res[1].insert(res[1].end(), bad_new_eles.begin(), bad_new_eles.end());
				}

				string out = "";

				for (string each : res[0]) {
					out += each + ", ";
				}

				for (string each : res[1]) {
					out += "Not " + each + ", ";
				}

				if (out == "") {
					out = "None";
				}
				else {
					out = out.substr(0, out.length() - 2);
				}

				returnOut[0][3] = "Failed Destination - Valid Destinations: " + out + ".";
			}

			returnOut[0][2] = "Passed Engine Type.";
		}
		case 1:
		{
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

				returnOut[0][2] = "Failed Engine Type. Needed Type : " + out.substr(0, out.length() - 2) + ".";
			}

			returnOut[0][1] = "Valid SID - " + sid;
			break;
		}
		case 0:
		{
			sort(results.begin(), results.end());
			results.erase(unique(results.begin(), results.end()), results.end());

			string out = "";

			for (vector<string>::iterator itr = results.begin(); itr != results.end(); ++itr) {
				out += *itr + ", ";
			}

			returnOut[0][1] = "Invalid SID - " + sid + " Contains Invalid Suffix. Valid Suffices: " + out.substr(0, out.length() - 2) + ".";
			returnOut[0][9] = "Failed";

			break;
		}
		}

		return returnOut;
	}
	else {
		returnOut[0][1] = "Invalid SID - SID Not Found";
		returnOut[0][9] = "Failed";
		returnOut[1][1] = "Invalid SID - " + sid + " departure not in database.";
		returnOut[1][9] = "Failed";
		return returnOut;
	}
}

string CVFPCPlugin::MinMaxOutput(size_t origin_int, size_t pos, vector<int> successes) {
	const Value& conditions = config[origin_int]["Sids"][pos]["Constraints"];
	vector<vector<int>> raw_lvls{};
	for (int each : successes) {
		vector<int> lvls = { MININT, MAXINT };

		if (conditions[each].HasMember("Min") && conditions[each]["Min"].IsInt()) {
			lvls[0] = conditions[each]["Min"].GetInt();
		}

		if (conditions[each].HasMember("Max") && conditions[each]["Max"].IsInt()) {
			lvls[1] = conditions[each]["Max"].GetInt();
		}

		raw_lvls.push_back(lvls);
	}

	bool changed;
	size_t i = 0;

	while (i < raw_lvls.size() - 1) {
		for (size_t j = 0; j < raw_lvls.size(); j++) {
			if (i == j) {
				break;
			}
			//Item j is a subset of Item i
			if (raw_lvls[j][0] >= raw_lvls[i][0] && raw_lvls[j][1] <= raw_lvls[i][1]) {
				raw_lvls.erase(raw_lvls.begin() + j);
				changed = true;
				break;
			}
			//Item j extends higher than Item i
			else if (raw_lvls[j][0] >= raw_lvls[i][0]) {
				raw_lvls[i][1] = raw_lvls[j][1];
				raw_lvls.erase(raw_lvls.begin() + j);
				changed = true;
				break;
			}
			//Item j extends lower than Item i
			else if (raw_lvls[j][1] <= raw_lvls[i][1]) {
				raw_lvls[i][0] = raw_lvls[j][0];
				raw_lvls.erase(raw_lvls.begin() + j);
				changed = true;
				break;
			}
		}

		if (!changed) {
			i++;
		}
	}

	string out = "Min/Max Level: ";

	for (vector<int> each : raw_lvls) {
		if (each[0] == MININT && each[1] == MAXINT) {
			out += "Any Level, ";
		}
		else if (each[0] == MININT) {
			out += to_string(each[1]) + "-, ";
		}
		else if (each[1] == MAXINT) {
			out += to_string(each[0]) + "+, ";

		}
		else {
			out += to_string(each[0]) + "-" + to_string(each[1]);
			out += ", ";
		}
	}

	out = out.substr(0, out.size() - 2) + ".";

	return out;
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
void CVFPCPlugin::OnGetTagItem(CFlightPlan FlightPlan, CRadarTarget RadarTarget, int ItemCode, int TagData, char sItemString[16], int* pColorCode, COLORREF* pRGB, double* pFontSize){
	if (validVersion && ItemCode == TAG_ITEM_FPCHECK) {
		string FlightPlanString = FlightPlan.GetFlightPlanData().GetRoute();
		int RFL = FlightPlan.GetFlightPlanData().GetFinalAltitude();

		*pColorCode = TAG_COLOR_RGB_DEFINED;
		string fpType{ FlightPlan.GetFlightPlanData().GetPlanType() };
		if (fpType == "V") {
			*pRGB = TAG_GREEN;
			strcpy_s(sItemString, 16, "VFR");
		}
		else {
			vector<string> validize = validizeSid(FlightPlan)[0];
			vector<string> messageBuffer{ validize }; // 0 = Callsign, 1 = SID, 2 = Engine Type, 3 = Airways, 4 = Nav Performance, 5 = Destination, 6 = Min/Max Flight Level, 7 = Even/Odd, 8 = Syntax, 9 = Passed/Failed

			if (messageBuffer.at(9) == "Passed") {
				*pRGB = TAG_GREEN;
				strcpy_s(sItemString, 16, "OK!");
			}
			else {
				*pRGB = TAG_RED;
				string code = getFails(validize);
				strcpy_s(sItemString, 16, code.c_str());
			}
		}

	}
}

bool CVFPCPlugin::OnCompileCommand(const char * sCommandLine) {
	//Restart Automatic Data Loading
	if (startsWith(".vfpc load", sCommandLine))
	{
		if (autoLoad) {
			sendMessage("Auto-Load Already Active.");
			debugMessage("Warning", "Auto-load activation attempted whilst already active.");
		}
		else {
			sendMessage("Auto-Load Activated.");
			debugMessage("Info", "Auto-load reactivated.");
		}

		sidsLoaded = false;
		return true;
	}
	//Activate Debug Logging
	if (startsWith(".vfpc log", sCommandLine)) {
		if (debugMode) {
			debugMessage("Info", "Logging mode deactivated.");
			debugMode = false;
		} else {
			debugMode = true;
			debugMessage("Info", "Logging mode activated.");
		}
		return true;
	}
	//Text-Equivalent of "Show Checks" Button
	if (startsWith(".vfpc check", sCommandLine))
	{
		checkFPDetail();
		return true;
	}
	return false;
}

// Sends to you, which checks were failed and which were passed on the selected aircraft
void CVFPCPlugin::checkFPDetail() {
	if (validVersion) {
		vector<string> messageBuffer{ validizeSid(FlightPlanSelectASEL())[0] };	// 0 = Callsign, 1 = valid/invalid SID, 2 = SID Name, 3 = Even/Odd, 4 = Minimum Flight Level, 5 = Maximum Flight Level, 6 = Passed
		sendMessage(messageBuffer.at(0), "Checking...");
		string buffer{ messageBuffer.at(1) + " SID | " };
		if (messageBuffer.at(1).find("Invalid") != 0) {
			for (int i = 2; i < 9; i++) {
				string temp = messageBuffer.at(i);

				if (temp != "-")
				{
					buffer += temp;
					buffer += " | ";
				}
			}
		}

		buffer += messageBuffer.at(9);
		sendMessage(messageBuffer.at(0), buffer);
	}
}

string CVFPCPlugin::getFails(vector<string> messageBuffer) {
	vector<string> fail;

	if (messageBuffer.at(1).find("Invalid") == 0) {
		fail.push_back("SID");
	}
	if (messageBuffer.at(2).find("Failed") == 0) {
		fail.push_back("ENG");
	}
	if (messageBuffer.at(3).find("Failed") == 0) {
		fail.push_back("DST");
	}
	if (messageBuffer.at(4).find("Failed") == 0) {
		fail.push_back("RTE");
	}
	if (messageBuffer.at(5).find("Failed") == 0) {
		fail.push_back("NAV");
	}
	if (messageBuffer.at(6).find("Failed") == 0) {
		fail.push_back("MIN");
		fail.push_back("MAX");
	}
	if (messageBuffer.at(7).find("Failed") == 0) {
		fail.push_back("DIR");
	}
	if (messageBuffer.at(8).find("Invalid") == 0) {
		fail.push_back("CHK");
	}

	if (fail.size() == 0) {
		sendMessage("Cero");
	}

	return fail[failPos % fail.size()];
}

void CVFPCPlugin::OnTimer(int Counter) {
	if (validVersion) {
		validVersion = checkVersion();

		if (validVersion) {
			blink = !blink;

			//2520 is Lowest Common Multiple of Numbers 1-9
			if (failPos < 840) {
				failPos++;
			}
			//Number shouldn't get out of control
			else {
				failPos = 0;
			}


			if (relCount == 5) {
				relCount = -1;
				sidsLoaded = false;
			}
			else if (relCount == -1 && sidsLoaded) {
				relCount = 0;
			}
			else {
				relCount++;
			}

			// Loading proper Sids, when logged in
			if (GetConnectionType() != CONNECTION_TYPE_NO && autoLoad && !sidsLoaded) {
				string callsign{ ControllerMyself().GetCallsign() };
				getSids();
				sidsLoaded = true;
			}
			else if (GetConnectionType() == CONNECTION_TYPE_NO && sidsLoaded) {
				sidsLoaded = false;
				sendMessage("Unloading all data.");
			}
		}
	}
}