#include "stdafx.h"
#include "analyzeFP.hpp"
#include <curl/curl.h>
#include <future>

extern "C" IMAGE_DOS_HEADER __ImageBase;

bool debugMode, validVersion, autoLoad, fileLoad;

vector<int> timedata;

size_t failPos;
int relCount;

std::future<void> fut;

// Matches Speed/Alt Data In Route
regex LVL_CHNG("(N|M|K)[0-9]{3,4}((A|F)[0-9]{3}|(S|M)[0-9]{4})$");

using namespace std;
using namespace EuroScopePlugIn;

//Run on Plugin Initialization
CVFPCPlugin::CVFPCPlugin(void) :CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE, MY_PLUGIN_NAME, MY_PLUGIN_VERSION, MY_PLUGIN_DEVELOPER, MY_PLUGIN_COPYRIGHT)
{
	debugMode = false;
	validVersion = true; //Reset in first timer call
	autoLoad = true;
	fileLoad = false;

	failPos = 0;
	relCount = 0;

	timedata = { 0, 0, 0 };

	string loadingMessage = "Loading complete. Version: ";
	loadingMessage += MY_PLUGIN_VERSION;
	loadingMessage += ".";
	sendMessage(loadingMessage);

	// Register Tag Item "VFPC"
	if (validVersion) {
		RegisterTagItemType("VFPC", TAG_ITEM_CHECKFP);
		RegisterTagItemFunction("Options", TAG_FUNC_CHECKFP_MENU);
	}
}

//Run on Plugin Destruction (Closing EuroScope or unloading plugin)
CVFPCPlugin::~CVFPCPlugin()
{
}

//Stores output of HTTP request in string
static size_t curlCallback(void *contents, size_t size, size_t nmemb, void *outString)
{
	// For Curl, we should assume that the data is not null terminated, so add a null terminator on the end
 	((std::string*)outString)->append(reinterpret_cast<char*>(contents), size * nmemb);
	return size * nmemb;
}

//Send message to user via "VFPC Log" channel
void CVFPCPlugin::debugMessage(string type, string message) {
	try {
		// Display Debug Message if debugMode = true
		if (debugMode) {
			DisplayUserMessage("VFPC Log", type.c_str(), message.c_str(), true, true, true, false, false);
		}
	}
	catch (const std::exception& ex) {
		sendMessage("Error", ex.what());
		debugMessage("Error", ex.what());
	}
	catch (const std::string& ex) {
		sendMessage("Error", ex);
		debugMessage("Error", ex);
	}
	catch (...) {
		sendMessage("Error", "An unexpected error occured");
		debugMessage("Error", "An unexpected error occured");
	}
}

//Send message to user via "VFPC" channel
void CVFPCPlugin::sendMessage(string type, string message) {
	try {
		// Show a message
		DisplayUserMessage("VFPC", type.c_str(), message.c_str(), true, true, true, true, false);
	}
	catch (const std::exception& ex) {
		sendMessage("Error", ex.what());
		debugMessage("Error", ex.what());
	}
	catch (const std::string& ex) {
		sendMessage("Error", ex);
		debugMessage("Error", ex);
	}
	catch (...) {
		sendMessage("Error", "An unexpected error occured");
		debugMessage("Error", "An unexpected error occured");
	}
}

//Send system message to user via "VFPC" channel
void CVFPCPlugin::sendMessage(string message) {
	try {
		DisplayUserMessage("VFPC", "System", message.c_str(), true, true, true, false, false);
	}
	catch (const std::exception& ex) {
		sendMessage("Error", ex.what());
		debugMessage("Error", ex.what());
	}
	catch (const std::string& ex) {
		sendMessage("Error", ex);
		debugMessage("Error", ex);
	}
	catch (...) {
		sendMessage("Error", "An unexpected error occured");
		debugMessage("Error", "An unexpected error occured");
	}
}

//CURL call, saves output to passed string reference
bool CVFPCPlugin::webCall(string url, string& out) {
	CURL* curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

	uint64_t httpCode = 0;

	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlCallback);

	curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
	curl_easy_cleanup(curl);

	if (httpCode == 200) {
		return true;
	}
	
	return false;
}

//Makes CURL call to API server for data and stores output
bool CVFPCPlugin::APICall(string endpoint, Document& out) {
	string url = MY_API_ADDRESS + endpoint;
	string buf = "";

	if (webCall(url, buf))
	{
		if (out.Parse<0>(buf.c_str()).HasParseError())
		{
			sendMessage("An error occurred whilst reading data. The plugin will not automatically attempt to reload from the API. To restart data fetching, type \".vfpc load\".");
			debugMessage("Error", str(boost::format("Config Download: %s (Offset: %i)\n'") % out.GetParseError() % out.GetErrorOffset()));
			return false;

			out.Parse<0>("[]");
		}
	}
	else
	{
		sendMessage("An error occurred whilst downloading data. The plugin will not automatically attempt to reload from the API. Check your connection and restart data fetching by typing \".vfpc load\".");
		debugMessage("Error", "Failed to download data from API.");
		return false;

		out.Parse<0>("[]");
	}

	return true;
}

//Makes CURL call to API server for current date, time, and version and stores output
bool CVFPCPlugin::versionCall() {
	Document version;
	APICall("version", version);

	bool timefail = false;
	if (version.HasMember("time") && version["time"].IsString() && version.HasMember("day") && version["day"].IsInt()) {
		int day = version["day"].GetInt();
		day += 6;
		day %= 7;
		timedata[0] = day;

		string time = version["time"].GetString();
		if (time.size() == 5) {
			try {
				int hour = stoi(time.substr(0, 2));
				int mins = stoi(time.substr(3, 2));

				timedata[1] = hour;
				timedata[2] = mins;
			}
			catch (...) {
				timefail = true;
			}
		}
		else {
			timefail = true;
		}
	}
	else {
		timefail = true;
	}

	if (timefail) {
		sendMessage("Failed to read date/time from API.");
	}

	if (version.HasMember("VFPC_Version") && version["VFPC_Version"].IsString()) {
		vector<string> current = split(version["VFPC_Version"].GetString(), '.');
		vector<string> installed = split(MY_PLUGIN_VERSION, '.');

		if ((installed[0] > current[0]) || //Major version higher
			(installed[0] == current[0] && installed[1] > current[1]) || //Minor version higher
			(installed[0] == current[0] && installed[1] == current[1] && installed[2] >= current[2])) { //Revision higher
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

//Loads data from file
bool CVFPCPlugin::fileCall(Document &out) {
	char DllPathFile[_MAX_PATH];
	GetModuleFileNameA(HINSTANCE(&__ImageBase), DllPathFile, sizeof(DllPathFile));
	string pfad = DllPathFile;
	pfad.resize(pfad.size() - strlen(PLUGIN_FILE.c_str()));
	pfad += DATA_FILE;

	stringstream ss;
	ifstream ifs;
	ifs.open(pfad.c_str(), ios::binary);

	if (ifs.is_open()) {
		ss << ifs.rdbuf();
		ifs.close();

		if (out.Parse<0>(ss.str().c_str()).HasParseError()) {
			sendMessage("An error occurred whilst reading data. The plugin will not automatically attempt to reload. To restart data fetching from the API, type \"" + COMMAND_PREFIX + LOAD_COMMAND + "\". To reattempt loading data from the Sid.json file, type \"" + COMMAND_PREFIX + FILE_COMMAND + "\".");
			debugMessage("Error", str(boost::format("Config Parse: %s (Offset: %i)\n'") % out.GetParseError() % out.GetErrorOffset()));

			out.Parse<0>("[]");
			return false;
		}

		return true;
	}
	else {
		sendMessage(DATA_FILE + " file not found. The plugin will not automatically attempt to reload. To restart data fetching from the API, type \"" + COMMAND_PREFIX + LOAD_COMMAND + "\". To reattempt loading data from the Sid.json file, type \"" + COMMAND_PREFIX + FILE_COMMAND + "\".");
		debugMessage("Error", DATA_FILE + " file not found.");

		out.Parse<0>("[]");
		return false;
	}
}

//Loads data and sorts into airports
void CVFPCPlugin::getSids() {
	try {
		//Load data from API
		if (autoLoad) {
			if (activeAirports.size() > 0) {
				string endpoint = "airport?icao=";

				for (size_t i = 0; i < activeAirports.size(); i++) {
					endpoint += activeAirports[i] + "+";
				}

				endpoint = endpoint.substr(0, endpoint.size() - 1);

				autoLoad = APICall(endpoint, config);
			}
		}
		//Load data from Sid.json file
		else if (fileLoad) {
			fileLoad = fileCall(config);
		}

		//Sort new data into airports
		airports.clear();
		for (SizeType i = 0; i < config.Size(); i++) {
			const Value& airport = config[i];
			if (airport.HasMember("icao") && airport["icao"].IsString()) {
				string airport_icao = airport["icao"].GetString();
				airports.insert(pair<string, SizeType>(airport_icao, i));
			}
		}
	}
	catch (const std::exception& ex) {
		sendMessage("Error", ex.what());
		debugMessage("Error", ex.what());
	}
	catch (const std::string& ex) {
		sendMessage("Error", ex);
		debugMessage("Error", ex);
	}
	catch (...) {
		sendMessage("Error", "An unexpected error occured");
		debugMessage("Error", "An unexpected error occured");
	}
}

vector<bool> CVFPCPlugin::checkRestrictions(CFlightPlan flightPlan, string sid_suffix, const Value& restrictions, bool *sidfails, bool *constfails) {
	vector<bool> res{ 0, 0 }; //0 = Constraint-Level Pass, 1 = SID-Level Pass
	bool constExists = false;
	if (restrictions.IsArray() && restrictions.Size()) {
		for (size_t j = 0; j < restrictions.Size(); j++) {
			bool temp = true;
			bool sidlevel = false;
			bool *fails;

			if (restrictions[j].HasMember("sidlevel") && (sidlevel = restrictions[j]["sidlevel"].GetBool())) {
				fails = sidfails;
			}
			else {
				fails = constfails;
				constExists = true;
			}

			if (restrictions[j]["suffix"].IsArray() && restrictions[j]["suffix"].Size()) {
				if (arrayContainsEnding(restrictions[j]["suffix"], sid_suffix)) {
					fails[0] = false;
				}
				else {
					temp = false;
				}
			}
			else {
				fails[0] = false;
			}

			if (restrictions[j]["types"].IsArray() && restrictions[j]["types"].Size()) {
				fails[1] = true;
				if (!arrayContains(restrictions[j]["types"], flightPlan.GetFlightPlanData().GetEngineType()) &&
					!arrayContains(restrictions[j]["types"], flightPlan.GetFlightPlanData().GetAircraftType())) {
					temp = false;
				}
			}

			if (restrictions[j].HasMember("start") && restrictions[j].HasMember("end")) {
				bool date = false;
				bool time = false;

				int startdate;
				int enddate;
				int starttime[2] = { 0,0 };
				int endtime[2] = { 0,0 };

				if (restrictions[j]["start"].HasMember("date")
					&& restrictions[j]["start"]["date"].IsInt()
					&& restrictions[j]["end"].HasMember("date")
					&& restrictions[j]["end"]["date"].IsInt()) {
					date = true;

					startdate = restrictions[j]["start"]["date"].GetInt();
					enddate = restrictions[j]["end"]["date"].GetInt();
				}

				if (restrictions[j]["start"].HasMember("time")
					&& restrictions[j]["start"]["time"].IsString()
					&& restrictions[j]["end"].HasMember("time")
					&& restrictions[j]["end"]["time"].IsString()) {
					time = true;

					string startstring = restrictions[j]["start"]["time"].GetString();
					string endstring = restrictions[j]["end"]["time"].GetString();

					starttime[0] = stoi(startstring.substr(0, 2));
					starttime[1] = stoi(startstring.substr(2, 2));
					endtime[0] = stoi(endstring.substr(0, 2));
					endtime[1] = stoi(endstring.substr(2, 2));
				}

				bool valid = true;

				if (date || time) {
					fails[2] = true;
					valid = false;

					if (!date && time) {
						if (starttime[0] > endtime[0] || (starttime[0] == endtime[0] && starttime[1] >= endtime[1])) {
							if (timedata[1] > starttime[0] || (timedata[1] == starttime[0] && timedata[2] >= starttime[1]) || timedata[1] < endtime[0] || (timedata[1] == endtime[0] && timedata[2] <= endtime[1])) {
								valid = true;
							}
						}
						else {
							if ((timedata[1] > starttime[0] || (timedata[1] == starttime[0] && timedata[2] >= starttime[1])) && (timedata[1] < endtime[0] || (timedata[1] == endtime[0] && timedata[2] <= endtime[1]))) {
								valid = true;
							}
						}
					}
					else if (startdate == enddate) {
						if (!time) {
							valid = true;
						}
						else if ((timedata[1] > starttime[0] || (timedata[1] == starttime[0] && timedata[2] >= starttime[1])) && (timedata[1] < endtime[0] || (timedata[1] == endtime[0] && timedata[2] <= endtime[1]))) {
							valid = true;
						}
					}
					else if (startdate < enddate) {
						if (timedata[0] > startdate && timedata[0] < enddate) {
							valid = true;
						}
						else if (timedata[0] == startdate) {
							if (!time || timedata[1] > starttime[0] || (timedata[1] == starttime[0] && timedata[2] >= starttime[1])) {
								valid = true;
							}
						}
						else if (timedata[0] == enddate) {
							if (!time || timedata[1] < endtime[0] || (timedata[1] == endtime[0] && timedata[2] < endtime[1])) {
								valid = true;
							}
						}
					}
					else if (startdate > enddate) {
						if (timedata[0] < startdate || timedata[0] > enddate) {
							valid = true;
						}
						else if (timedata[0] == startdate) {
							if (!time || timedata[1] > starttime[0] || (timedata[1] == starttime[0] && timedata[2] >= starttime[1])) {
								valid = true;
							}
						}
						else if (timedata[0] == enddate) {
							if (!time || timedata[1] < endtime[0] || (timedata[1] == endtime[0] && timedata[2] < endtime[1])) {
								valid = true;
							}
						}
					}
				}

				if (!valid) {
					temp = false;
				}
			}

			if (restrictions[j].HasMember("banned") && restrictions[j]["banned"].GetBool()) {
				fails[3] = true;
				temp = false;
			}

			if (temp) {
				res[sidlevel] = true;
			}
		}
	}

	if (!constExists) {
		res[0] = true;
	}

	return res;
}

//Checks flight plan
vector<vector<string>> CVFPCPlugin::validizeSid(CFlightPlan flightPlan) {
	//out[0] = Normal Output, out[1] = Debug Output
	vector<vector<string>> returnOut = { vector<string>(), vector<string>() }; // 0 = Callsign, 1 = SID, 2 = Destination, 3 = Route, 4 = Nav Performance, 5 = Min/Max Flight Level, 6 = Even/Odd, 7 = Suffix, 8 = Aircraft Type, 9 = Date/Time, 10 = Syntax, 11 = Passed/Failed

	returnOut[0].push_back(flightPlan.GetCallsign());
	returnOut[1].push_back(flightPlan.GetCallsign());
	for (int i = 1; i < 12; i++) {
		returnOut[0].push_back("-");
		returnOut[1].push_back("-");
	}

	string origin = flightPlan.GetFlightPlanData().GetOrigin(); boost::to_upper(origin);
	string destination = flightPlan.GetFlightPlanData().GetDestination(); boost::to_upper(destination);
	SizeType origin_int;

	// Airport defined
	if (airports.find(origin) == airports.end()) {
		returnOut[0][1] = "Airport Not Found";
		returnOut[0].back() = "Failed";

		returnOut[1][1] = origin + " not in database.";
		returnOut[1].back() = "Failed";
		return returnOut;
	}
	else
	{
		origin_int = airports[origin];
	}

	int RFL = flightPlan.GetFlightPlanData().GetFinalAltitude();

	vector<string> route = split(flightPlan.GetFlightPlanData().GetRoute(), ' ');
	for (size_t i = 0; i < route.size(); i++) {
		boost::to_upper(route[i]);
	}

	vector<string> points{};
	CFlightPlanExtractedRoute extracted = flightPlan.GetExtractedRoute();
	for (int i = 0; i < extracted.GetPointsNumber(); i++) {
		points.push_back(extracted.GetPointName(i));
	}

	// Remove "DCT" Instances from Route
	for (size_t i = 0; i < route.size(); i++) {
		if (!strcmp(route[i].c_str(), DCT_ENTRY.c_str())) {
			route.erase(route.begin() + i);
		}
	}

	//Remove Speed/Level Data From Start Of Route
	if (route.size() > 0 && regex_match(route[0], LVL_CHNG)) {
		route.erase(route.begin());
	}

	// Remove Speed / Level Change Instances from Route
	for (size_t i = 0; i < route.size(); i++) {
		int count = 0;
		size_t pos = 0;

		for (size_t j = 0; j < route[i].size(); j++) {
			if (!strcmp(&route[i][j], SPDLVL_SEP.c_str())) {
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
			size_t first_pos = route[i].find(SPDLVL_SEP);
			route[i] = route[i].substr(first_pos, string::npos);
		}
		case 1:
		{
			if (route[i].size() > pos + 1 && regex_match((route[i].substr(pos + 1, string::npos)), LVL_CHNG)) {
				route[i] = route[i].substr(0, pos);
				break;
			}
			else {
				returnOut[0][returnOut[0].size() - 2] = "Invalid Speed/Level Change";
				returnOut[0].back() = "Failed";

				returnOut[1][returnOut[1].size() - 2] = "Invalid Route Item: " + route[i];
				returnOut[1].back() = "Failed";
				return returnOut;
			}
		}
		default:
		{
			returnOut[0][returnOut.size() - 2] = "Invalid Syntax - Too Many \"" + SPDLVL_SEP + "\" Characters in One or More Waypoints";
			returnOut[0].back() = "Failed";

			returnOut[1][returnOut.size() - 2] = "Invalid Route Item: " + route[i];
			returnOut[1].back() = "Failed";
			return returnOut;
		}
		}
	}

	string sid = flightPlan.GetFlightPlanData().GetSidName(); boost::to_upper(sid);
	string first_wp = "";
	string sid_suffix = "";

	//Route with SID
	if (sid.length()) {
		// Remove any # characters from SID name
		boost::erase_all(sid, OUTDATED_SID);

		if (origin == "EGLL" && sid == "CHK") {
			first_wp = "CPT";
			sid_suffix = "CHK";
		}
		else {
			first_wp = sid.substr(0, sid.find_first_of("0123456789"));
			if (0 != first_wp.length())
				boost::to_upper(first_wp);

			if (first_wp.length() != sid.length()) {
				sid_suffix = sid.substr(sid.find_first_of("0123456789"), sid.length());
				boost::to_upper(sid_suffix);
			}
		}

		// Check First Waypoint Correct. Remove SID References & First Waypoint From Route.
		bool success = false;
		bool stop = false;

		while (!stop && route.size() > 0) {
			size_t wp_size = first_wp.size();
			size_t entry_size = route[0].size();
			if (route[0].substr(0, wp_size) == first_wp) {
				//First Waypoint
				if (wp_size == entry_size) {
					success = true;
					stop = true;
				}
				//3 or 5 Letter Waypoint SID - In Full
				else if (entry_size >= wp_size + 2 && isdigit(route[0][wp_size]) && isalpha(route[0][wp_size + 1])) {
					bool valid = true;
					//EuroScope "SID/Runway" Assignment Syntax
					if (entry_size > wp_size + 2) {
						if (!strcmp(&route[0][wp_size + 2], SPDLVL_SEP.c_str()) && entry_size >= wp_size + 5 && entry_size <= wp_size + 6) {
							if (!isdigit(route[0][wp_size + 3])) {
								valid = false;
							}

							size_t mod = 0;
							if (entry_size == wp_size + 6) {
								if (!isdigit(route[0][wp_size + 4])) {
									valid = false;
								}
								mod++;
							}

							if (!isalpha(route[0][wp_size + 4 + mod])) {
								valid = false;
							}

						}
						else {
							valid = false;
						}

						if (!valid) {
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

		//Route Discontinuity at End of SID
		if (!success) {
			returnOut[0][1] = "Route Not From Final SID Fix";
			returnOut[0].back() = "Failed";

			returnOut[1][1] = "Route must start at final SID fix (" + first_wp + ").";
			returnOut[1].back() = "Failed";
			return returnOut;
		}
	}

	// Any SIDs defined
	if (!config[origin_int].HasMember("sids") || !config[origin_int]["sids"].IsArray()) {
		returnOut[0][1] = "No SIDs or Non-SID Routes Defined";
		returnOut[0].back() = "Failed";

		returnOut[1][1] = origin + " exists in database but has no SIDs (or non-SID routes) defined.";
		returnOut[1].back() = "Failed";
		return returnOut;
	}

	//Find routes for selected SID
	size_t pos = string::npos;
	for (size_t i = 0; i < config[origin_int]["sids"].Size(); i++) {
		if (config[origin_int]["sids"][i].HasMember("point") && !first_wp.compare(config[origin_int]["sids"][i]["point"].GetString()) && config[origin_int]["sids"][i].HasMember("constraints") && config[origin_int]["sids"][i]["constraints"].IsArray()) {
			pos = i;
		}
	}

	// Needed SID defined
	if (pos == string::npos) {
		if (first_wp == "") {
			returnOut[0][1] = "SID Required";
			returnOut[1][1] = "Non-SID departure routes not in database.";
			returnOut[1].back() = returnOut[0].back() = "Failed";
			return returnOut;
		}
		else {
			returnOut[0][1] = "SID Not Found";
			returnOut[1][1] = sid + " departure not in database.";
			returnOut[1].back() = returnOut[0].back() = "Failed";
			return returnOut;
		}
	} 
	else {
		const Value& sid_ele = config[origin_int]["sids"][pos];
		const Value& conditions = sid_ele["constraints"];

		int round = 0;

		vector<bool> validity, new_validity;
		vector<string> results;
		bool sidFails[4]{ 0 };
		bool restFails[4]{ 0 }; // 0 = Suffix, 1 = Aircraft/Engines, 2 = Date/Time Restrictions
		bool warn = false;
		int Min, Max;

		//SID-Level Restrictions Array
		sidFails[0] = true;
		vector<bool> temp = checkRestrictions(flightPlan, sid_suffix, sid_ele["restrictions"], sidFails, sidFails);
		bool sidwide = false;
		if (temp[0] || temp[1]) {
			sidwide = true;
		}

		//Initialise validity array to fully true#
		for (SizeType i = 0; i < conditions.Size(); i++) {
			validity.push_back(true);
		}
			
		//Constraints Array
		while (round < 6) {
			new_validity = {};

			for (SizeType i = 0; i < conditions.Size(); i++) {
				if (round == 0 || validity[i]) {
					switch (round) {
					case 0:
					{
						//Destinations
						bool res = true;

						if (conditions[i]["nodests"].IsArray() && conditions[i]["nodests"].Size()) {
							string dest;
							if (destArrayContains(conditions[i]["nodests"], destination.c_str()).size()) {
								res = false;
							}
						}

						if (conditions[i]["dests"].IsArray() && conditions[i]["dests"].Size()) {
							string dest;
							if (!destArrayContains(conditions[i]["dests"], destination.c_str()).size()) {
								res = false;
							}
						}

						new_validity.push_back(res);
						break;
					}
					case 1:
					{
						//Route
						bool res = true;

						if (conditions[i].HasMember("route") && conditions[i]["route"].IsArray() && conditions[i]["route"].Size() && !routeContains(flightPlan.GetCallsign(), route, conditions[i]["route"])) {
							res = false;
						}

						if (conditions[i].HasMember("points") && conditions[i]["points"].IsArray() && conditions[i]["points"].Size()) {
							bool temp = false;

							for (string each : points) {
								if (arrayContains(conditions[i]["points"], each)) {
									temp = true;
								}
							}

							if (!temp) {
								res = false;
							}
						}

						if (conditions[i].HasMember("noroute") && res && conditions[i]["noroute"].IsArray() && conditions[i]["noroute"].Size() && routeContains(flightPlan.GetCallsign(), route, conditions[i]["noroute"])) {
							res = false;
						}

						if (conditions[i].HasMember("nopoints") && conditions[i]["nopoints"].IsArray() && conditions[i]["nopoints"].Size()) {
							bool temp = false;

							for (string each : points) {
								if (arrayContains(conditions[i]["nopoints"], each)) {
									temp = true;
								}
							}

							if (temp) {
								res = false;
							}
						}

						new_validity.push_back(res);
						break;
					}
					case 2:
					{
						bool res = true;

						//Min Level
						if (conditions[i].HasMember("min") && (Min = conditions[i]["min"].GetInt()) > 0 && (RFL / 100) < Min) {
							res = false;
						}

						//Max Level
						if (conditions[i].HasMember("max") && (Max = conditions[i]["max"].GetInt()) > 0 && (RFL / 100) > Max) {
							res = false;
						}

						new_validity.push_back(res);
						break;
					}
					case 3:
					{
						//Even/Odd Levels

						//Assume any level valid if no "EVEN" or "ODD" declaration
						bool res = true;

						if (conditions[i].HasMember("dir") && conditions[i]["dir"].IsString()) {
							string direction = conditions[i]["dir"].GetString();
							boost::to_upper(direction);

							if (direction == EVEN_DIRECTION) {
								//Assume invalid until condition matched
								res = false;

								//Non-RVSM (Above FL410)
								if ((RFL > 41000 && (RFL / 1000 - 41) % 4 == 2)) {
									res = true;
								}
								//RVSM (FL290-410) or Below FL290
								else if (RFL <= 41000 && (RFL / 1000) % 2 == 0) {
									res = true;
								}
							}
							else if (direction == ODD_DIRECTION) {
								//Assume invalid until condition matched
								res = false;

								//Non-RVSM (Above FL410)
								if ((RFL > 41000 && (RFL / 1000 - 41) % 4 == 0)) {
									res = true;
								}
								//RVSM (FL290-410) or Below FL290
								else if (RFL <= 41000 && (RFL / 1000) % 2 == 1) {
									res = true;
								}
							}
						}

						new_validity.push_back(res);
						break;
					}
					case 4:
					{
						//Restrictions Array
						bool res = false;

						temp = checkRestrictions(flightPlan, sid_suffix, conditions[i]["restrictions"], sidFails, restFails);

						res = temp[0];
						if (temp[1]) {
							sidwide = true;
						}

						new_validity.push_back(res);
						break;
					}
					case 5:
					{
						//Alerts (Warn/Ban)
						bool res = true;

						if (conditions[i]["alerts"].IsArray() && conditions[i]["alerts"].Size()) {
							for (size_t j = 0; j < conditions[i]["alerts"].Size(); i++) {
								if (conditions[i]["alerts"][j].HasMember("ban") && conditions[i]["alerts"][j]["ban"].GetBool()) {
									res = false;
								}

								if (conditions[i]["alerts"][j].HasMember("warn") && conditions[i]["alerts"][j]["warn"].GetBool()) {
									warn = true;
								}
							}
						}

						new_validity.push_back(res);
						break;
					}
					}
				}
				else {
					new_validity.push_back(false);
				}
			}

			if (all_of(new_validity.begin(), new_validity.end(), [](bool v) { return !v; })) {
				break;
			}
			else {
				validity = new_validity;
				round++;
			}
		}

		returnOut[1][0] = returnOut[0][0] = flightPlan.GetCallsign();
		for (size_t i = 1; i < returnOut[0].size(); i++) {
			returnOut[1][i] = returnOut[0][i] = "-";
		}

		returnOut[1].back() = returnOut[0].back() = "Failed";

		if (sid.length()) {
			returnOut[1][1] = returnOut[0][1] = "SID - " + sid + ".";
		}
		else {
			returnOut[1][1] = returnOut[0][1] = "Non-SID Route.";
		}

		if (sidwide) {
			vector<size_t> successes{};

			for (size_t i = 0; i < validity.size(); i++) {
				if (validity[i]) {
					successes.push_back(i);
				}
			}

			switch (round) {
			case 6:
			{
				returnOut[1].back() = returnOut[0].back() = "Passed";
				returnOut[1][9] = "No Route Ban.";
			}
			case 5:
			{
				returnOut[0][7] = "Passed SID Restrictions.";
				returnOut[1][7] = "Passed " + RestrictionsOutput(sid_ele, true, true, true, successes);

				if (warn) {
					returnOut[1][8] = returnOut[0][8] = WarningsOutput(conditions, successes);
				}
				else {
					returnOut[1][8] = "No Warnings.";
				}
        
				if (round == 5) {
					returnOut[1][9] = returnOut[0][9] = BansOutput(conditions, successes);
				}
			}
			case 4:
			{
				returnOut[0][6] = "Valid Suffix.";
				returnOut[1][6] = "Valid " + SuffixOutput(sid_ele, successes);

				if (round == 4) {
					if (restFails[0]) {
						returnOut[1][6] = returnOut[0][6] = "Invalid " + SuffixOutput(sid_ele, successes);
					}
					else {
						returnOut[1][7] = returnOut[0][7] = "Failed " + RestrictionsOutput(sid_ele, restFails[1], restFails[2], restFails[3], successes) + " " + AlternativesOutput(sid_ele, successes);
					}
				}

				returnOut[0][5] = "Passed Odd-Even Rule.";
				returnOut[1][5] = "Passed " + DirectionOutput(conditions, successes);
			}
			case 3:
			{
				if (round == 3) {
					returnOut[1][5] = returnOut[0][5] = "Failed " + DirectionOutput(conditions, successes);
				}

				returnOut[0][4] = "Passed Min/Max Level.";
				returnOut[1][4] = "Passed " + MinMaxOutput(conditions, successes);
			}
			case 2:
			{
				if (round == 2) {
					returnOut[1][4] = returnOut[0][4] = "Failed " + MinMaxOutput(conditions, successes) + " Alternative " + RouteOutput(conditions, successes, points, destination, RFL, true);
				}

				returnOut[0][3] = "Passed Route.";
				returnOut[1][3] = "Passed Route. " + RouteOutput(conditions, successes, points, destination, RFL);
			}
			case 1:
			{
				if (round == 1) {
					returnOut[1][3] = returnOut[0][3] = "Failed Route. " + RouteOutput(conditions, successes, points, destination, RFL);
				}

				returnOut[0][2] = "Passed Destination.";
				returnOut[1][2] = "Passed " + DestinationOutput(origin_int, destination);
			}
			case 0:
			{
				if (round == 0) {
					returnOut[1][2] = returnOut[0][2] = "Failed " + DestinationOutput(origin_int, destination);
				}
				break;
			}
			}
		}
		else {
			if (sidFails[0]) {
				returnOut[1][6] = returnOut[0][7] = "Invalid " + SuffixOutput(sid_ele);
			}
			else {
				returnOut[0][6] = "Valid Suffix.";
				returnOut[1][6] = "Valid " + SuffixOutput(sid_ele);

				//sidFails[1], [2], or [3] must be false to get here
				returnOut[1][7] = returnOut[0][7] = "Failed " + RestrictionsOutput(sid_ele, sidFails[1], sidFails[2], sidFails[3]) + " " + AlternativesOutput(sid_ele);
			}
		}

		return returnOut;
	}
}

//Outputs route bans as string
string CVFPCPlugin::BansOutput(const Value& constraints, vector<size_t> successes) {
	vector<string> bans{};
	for (int each : successes) {
		if (constraints[each]["alerts"].IsArray() && constraints[each]["alerts"].Size()) {
			for (size_t i = 0; i < constraints[each]["alerts"].Size(); i++) {
				if (constraints[each]["alerts"][i].HasMember("ban") && constraints[each]["alerts"][i]["ban"].GetBool() && constraints[each]["alerts"][i].HasMember("note")) {
					bans.push_back(constraints[each]["alerts"][i]["note"].GetString());
				}
			}
		}
	}

	sort(bans.begin(), bans.end());
	vector<string>::iterator itr = unique(bans.begin(), bans.end());
	bans.erase(itr, bans.end());

	string out = "";

	for (string each : bans) {
		out += each + RESULT_SEP;
	}

	if (out == "") {
		out = NO_RESULTS;
	}
	else {
		out = out.substr(0, out.length() - 2);
	}

	return "Route Banned: " + out + ".";
}

//Outputs route warnings as string
string CVFPCPlugin::WarningsOutput(const Value& constraints, vector<size_t> successes) {
	vector<string> warnings{};
	for (int each : successes) {
		if (constraints[each]["alerts"].IsArray() && constraints[each]["alerts"].Size()) {
			for (size_t i = 0; i < constraints[each]["alerts"].Size(); i++) {
				if (constraints[each]["alerts"][i].HasMember("warn") && constraints[each]["alerts"][i]["warn"].GetBool() && constraints[each]["alerts"][i].HasMember("note")) {
					warnings.push_back(constraints[each]["alerts"][i]["note"].GetString());
				}
			}
		}
	}

	sort(warnings.begin(), warnings.end());
	vector<string>::iterator itr = unique(warnings.begin(), warnings.end());
	warnings.erase(itr, warnings.end());

	string out = "";

	for (string each : warnings) {
		out += each + RESULT_SEP;
	}

	if (out == "") {
		out = NO_RESULTS;
	}
	else {
		out = out.substr(0, out.length() - 2);
	}

	return "Warnings: " + out + ".";
}

//Outputs recommended alternatives (from Restrictions arrays for a SID) as string
string CVFPCPlugin::AlternativesOutput(const Value& sid_ele, vector<size_t> successes) {
	vector<string> alts{};
	const Value& constraints = sid_ele["constraints"];

	vector<string> temp = AlternativesSingle(sid_ele["restrictions"]);
	alts.insert(alts.end(), temp.begin(), temp.end());

	for (size_t each : successes) {
		temp = AlternativesSingle(constraints[each]["restrictions"]);
		alts.insert(alts.end(), temp.begin(), temp.end());
	}

	string out = "Recommended Alternatives: ";

	sort(alts.begin(), alts.end());
	vector<string>::iterator itr = unique(alts.begin(), alts.end());
	alts.erase(itr, alts.end());

	if (!alts.size()) {
		out = NO_RESULTS;
	}
	else {
		for (string each : alts) {
			out += each + RESULT_SEP;
		}
	}

	return out.substr(0, out.size() - 2) + ".";
}

//Outputs recommended alternatives (from a single Restrictions array) as string
vector<string> CVFPCPlugin::AlternativesSingle(const Value& restrictions) {
	vector<string> alts{};
	if (restrictions.IsArray() && restrictions.Size()) {
		for (size_t i = 0; i < restrictions.Size(); i++) {
			if (restrictions[i]["alt"].IsArray() && restrictions[i]["alt"].Size()) {
				for (size_t j = 0; j < restrictions[i]["alt"].Size(); j++) {
					if (restrictions[i]["alt"][j].IsString()) {
						alts.push_back(restrictions[i]["alt"][j].GetString());
					}
				}
			}
		}
	}

	return alts;
}

//Outputs aircraft type and date/time restrictions (from Restrictions array) as string
string CVFPCPlugin::RestrictionsOutput(const Value& sid_ele, bool check_type, bool check_time, bool check_ban, vector<size_t> successes) {
	vector<vector<string>> rests{};
	const Value& constraints = sid_ele["constraints"];

	vector<vector<string>> temp = RestrictionsSingle(sid_ele["restrictions"]);
	rests.insert(rests.end(), temp.begin(), temp.end());

	for (size_t each : successes) {
		temp = RestrictionsSingle(constraints[each]["restrictions"]);
		rests.insert(rests.end(), temp.begin(), temp.end());
	}

	string out = "";
	for (size_t i = 0; i < rests.size(); i++) {
		if (check_ban) {
			out += "Banned";
		}
		if (check_type && check_time) {
			if (out.size() > 0) {
				out += " for ";
			}

			out += rests[i][0] + " Between " + rests[i][1] + ROUTE_RESULT_SEP;
		}
		else if (check_type) {
			if (out.size() > 0) {
				out += " for ";
			}

			out += rests[i][0] + RESULT_SEP;
		}
		else if (check_time) {
			if (out.size() > 0) {
				out += " b";
			}
			else {
				out += "B";
			}

			out += "etween " + rests[i][1] + ROUTE_RESULT_SEP;
		}
	}

	if (out == "") {
		out = NO_RESULTS;
	}
	else if (check_time) {
		out = out.substr(0, out.size() - 3);
	}
	else {
		out = out.substr(0, out.size() - 2) + " Aircraft";
	}

	return "SID Restrictions: " + out + ".";
}

vector<vector<string>> CVFPCPlugin::RestrictionsSingle(const Value& restrictions, bool check_type, bool check_time, bool check_ban) {
	vector<vector<string>> rests{};

	if (restrictions.IsArray() && restrictions.Size()) {
		for (size_t i = 0; i < restrictions.Size(); i++) {
			vector<string> this_rest{ "", "", "" };

			if (restrictions[i]["types"].IsArray() && restrictions[i]["types"].Size()) {
				for (size_t j = 0; j < restrictions[i]["types"].Size(); j++) {
					if (restrictions[i]["types"][j].IsString()) {
						string item = restrictions[i]["types"][j].GetString();

						if (item.size() == 1) {
							if (item == "P") {
								this_rest[0] += "All Pistons";
							}
							else if (item == "T") {
								this_rest[0] += "All Turboprops";
							}
							else if (item == "J") {
								this_rest[0] += "All Jets";
							}
							else if (item == "E") {
								this_rest[0] += "All Electric Aircraft";
							}
						}
						else {
							this_rest[0] += item;
						}

						this_rest[0] += RESULT_SEP;
					}
				}

				if (this_rest[0] != "") {
					this_rest[0] = this_rest[0].substr(0, this_rest[0].size() - 2);
				}
			}

			if (restrictions[i].HasMember("start") && restrictions[i].HasMember("end")) {
				bool date = false;
				bool time = false;
				int startdate;
				int enddate;
				string starttime;
				string endtime;

				if (restrictions[i]["start"].HasMember("date")
					&& restrictions[i]["start"]["date"].IsInt()
					&& restrictions[i]["end"].HasMember("date")
					&& restrictions[i]["end"]["date"].IsInt()) {
					date = true;

					startdate = restrictions[i]["start"]["date"].GetInt();
					enddate = restrictions[i]["end"]["date"].GetInt();
				}

				if (restrictions[i]["start"].HasMember("time")
					&& restrictions[i]["start"]["time"].IsString()
					&& restrictions[i]["end"].HasMember("time")
					&& restrictions[i]["end"]["time"].IsString()) {
					time = true;

					string startstring = restrictions[i]["start"]["time"].GetString();
					string endstring = restrictions[i]["end"]["time"].GetString();

					starttime = startstring.substr(0, 2) + ":" + startstring.substr(2, 2);
					endtime = endstring.substr(0, 2) + ":" + endstring.substr(2, 2);
				}

				string start = "";
				string end = "";

				if (date) {
					start += dayIntToString(startdate);
					end += dayIntToString(enddate);
				}

				if (time) {
					if (date) {
						start += " ";
						end += " ";
					}

					start += starttime;
					end += endtime;
				}

				if (start != "" && end != "") {
					this_rest[1] = start + " and " + end;
				}
			}

			if (restrictions[i].HasMember("banned") && restrictions[i]["banned"].GetBool()) {
				this_rest[2] = "Banned";
			}

			if (!all_of(this_rest[0].begin(), this_rest[0].end(), isspace) || !all_of(this_rest[1].begin(), this_rest[1].end(), isspace) || !all_of(this_rest[2].begin(), this_rest[2].end(), isspace)) {
				rests.push_back(this_rest);
			}
		}
	}

	return rests;
}

//Outputs valid suffices (from Restrictions array) as string
string CVFPCPlugin::SuffixOutput(const Value& sid_eles, vector<size_t> successes) {
	vector<string> suffices{};
	const Value& constraints = sid_eles["constraints"];

	vector<string> temp = SuffixSingle(sid_eles["restrictions"]);
	suffices.insert(suffices.end(), temp.begin(), temp.end());

	for (size_t each : successes) {
		temp = SuffixSingle(constraints[each]["restrictions"]);
		suffices.insert(suffices.end(), temp.begin(), temp.end());
	}

	string out = "Suffix. Valid Suffices: ";

	sort(suffices.begin(), suffices.end());
	vector<string>::iterator itr = unique(suffices.begin(), suffices.end());
	suffices.erase(itr, suffices.end());
	
	if (!suffices.size()) {
		out += "Any.";
	}
	else {
		for (string each : suffices) {
			out += each + RESULT_SEP;
		}

		out = out.substr(0, out.size() - 2) + ".";
	}

	return out;
}

vector<string> CVFPCPlugin::SuffixSingle(const Value& restrictions) {
	vector<string> suffices{};

	if (restrictions.IsArray() && restrictions.Size()) {
		for (size_t i = 0; i < restrictions.Size(); i++) {
			if (restrictions[i]["suffix"].IsArray() && restrictions[i]["suffix"].Size()) {
				for (size_t j = 0; j < restrictions[i]["suffix"].Size(); j++) {
					if (restrictions[i]["suffix"][j].IsString()) {
						string out = "";
						if (restrictions[i].HasMember("banned") && restrictions[i]["banned"].GetBool()) {
							out += "Not ";
						}

						out += restrictions[i]["suffix"][j].GetString();
						suffices.push_back(out);
					}
				}
			}
		}
	}

	return suffices;
}

//Outputs valid cruise level direction (from Constraints array) as string
string CVFPCPlugin::DirectionOutput(const Value& constraints, vector<size_t> successes) {
	bool lvls[2] { false, false };
	for (int each : successes) {
		if (constraints[each].HasMember("dir") && constraints[each]["dir"].IsString()) {
			string val = constraints[each]["dir"].GetString();
			if (val == EVEN_DIRECTION) {
				lvls[0] = true;
			}
			else if (val == ODD_DIRECTION) {
				lvls[1] = true;
			}
		}
		else {
			lvls[0] = true;
			lvls[1] = true;
		}
	}

	string out = "Odd-Even Rule. Required: ";

	if (lvls[0] && lvls[1]) {
		out += "Any";
	}
	else if (lvls[0]) {
		out += "Even";
	}
	else if (lvls[1]) {
		out += "Odd";
	}
	else {
		out += "Any";
	}

	return out;
}

//Outputs valid cruise level blocks (from Constraints array) as string
string CVFPCPlugin::MinMaxOutput(const Value& constraints, vector<size_t> successes) {
	vector<vector<int>> raw_lvls{};
	for (int each : successes) {
		vector<int> lvls = { MININT, MAXINT };

		if (constraints[each].HasMember("min") && constraints[each]["min"].IsInt()) {
			lvls[0] = constraints[each]["min"].GetInt();
		}

		if (constraints[each].HasMember("max") && constraints[each]["max"].IsInt()) {
			lvls[1] = constraints[each]["max"].GetInt();
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
			out += RESULT_SEP;
		}
	}

	out = out.substr(0, out.size() - 2) + ".";

	return out;
}

//Outputs valid initial routes (from Constraints array) as string
string CVFPCPlugin::RouteOutput(const Value& constraints, vector<size_t> successes, vector<string> extracted_route, string dest, int rfl, bool req_lvl) {
	vector<size_t> pos{};
	int checks[5]{ 0 };

	for (size_t i = 0; i < constraints.Size(); i++) {
		pos.push_back(i);
	}

	for (size_t i = 0; i < 5; i++) {
		vector<size_t> newpos{};
		for (size_t j : pos) {
			switch (i) {
			case 0: {
				bool res = true;

				if (constraints[j]["dests"].IsArray() && constraints[j]["dests"].Size()) {
					for (size_t k = 0; k < constraints[j]["dests"].Size(); k++) {
						if (constraints[j]["dests"][k].IsString()) {
							if (!startsWith(constraints[j]["dests"][k].GetString(), dest.c_str())) {
								res = false;
							}
						}
					}
				}

				if (res) {
					newpos.push_back(j);
				}
				break;
			}
			case 1: {
				bool res = true;

				if (constraints[j]["nodests"].IsArray() && constraints[j]["nodests"].Size()) {
					for (size_t k = 0; k < constraints[j]["nodests"].Size(); k++) {
						if (constraints[j]["nodests"][k].IsString()) {
							if (startsWith(constraints[j]["nodests"][k].GetString(), dest.c_str())) {
								res = false;
							}
						}
					}
				}

				if (res) {
					newpos.push_back(j);
				}
				break;
			}
			case 2: {
				bool res = true;

				if (constraints[j]["points"].IsArray() && constraints[j]["points"].Size()) {
					for (size_t k = 0; k < extracted_route.size(); k++) {
						if (arrayContains(constraints[j]["points"], extracted_route[k])) {
							res = false;
						}
					}
				}

				if (res) {
					newpos.push_back(j);
				}
				break;
			}
			case 3: {
				bool res = true;

				if (constraints[j]["nopoints"].IsArray() && constraints[j]["nopoints"].Size()) {
					for (size_t k = 0; k < extracted_route.size(); k++) {
						if (arrayContains(constraints[j]["nopoints"], extracted_route[k])) {
							res = false;
						}
					}
				}

				if (res) {
					newpos.push_back(j);
				}
				break;
			}
			case 4: {
				bool res = true;

				if (constraints[j].HasMember("min") && (!constraints[j]["min"].IsInt() || constraints[j]["min"].GetInt() > rfl)) {
					res = false;
				}

				if (constraints[j].HasMember("max") && (!constraints[j]["max"].IsInt() || constraints[j]["max"].GetInt() < rfl)) {
					res = false;
				}

				if (res) {
					newpos.push_back(j);
				}
				break;
			}
			}
		}

		if (newpos.size() > 0) {
			pos = newpos;
			checks[i] = true;
		}
	}

	vector<string> out{};

	for (size_t each : pos) {
		string positem = "";
		if (constraints[each]["route"].IsArray()) {
			for (size_t i = 0; i < constraints[each]["route"].Size(); i++) {
				if (i > 0) {
					positem += RESULT_SEP;
				}

				positem += constraints[each]["route"][i].GetString();
			}
		}

		if (constraints[each]["points"].IsArray()) {
			if (positem.size() > 0) {
				positem += " and ";
			}

			positem += "via ";

			for (size_t i = 0; i < constraints[each]["points"].Size(); i++) {
				if (i > 0) {
					positem += RESULT_SEP;
				}

				positem += constraints[each]["points"][i].GetString();
			}
		}

		string negitem = "";
		if (constraints[each]["noroute"].IsArray() || constraints[each]["nopoints"].IsArray()) {

			if (constraints[each]["noroute"].IsArray()) {
				for (size_t i = 0; i < constraints[each]["noroute"].Size(); i++) {
					if (i > 0) {
						negitem += RESULT_SEP;
					}

					negitem += constraints[each]["noroute"][i].GetString();
				}
			}

			if (constraints[each]["nopoints"].IsArray()) {
				if (negitem.size() > 0) {
					negitem += " or ";
				}

				negitem += "via ";

				for (size_t i = 0; i < constraints[each]["nopoints"].Size(); i++) {
					if (i > 0) {
						negitem += RESULT_SEP;;
					}

					negitem += constraints[each]["nopoints"][i].GetString();
				}
			}

		}

		string lvlitem = "";
		int lvls[2]{ MININT, MAXINT };
		if (constraints[each]["min"].IsInt()) {
			lvls[0] = constraints[each]["min"].GetInt();
		}
		if (constraints[each]["max"].IsInt()) {
			lvls[1] = constraints[each]["max"].GetInt();
		}

		if (lvls[0] == MININT && lvls[1] == MAXINT) {
			lvlitem = "Any Level";
		}
		else if (lvls[0] == MININT) {
			lvlitem = to_string(lvls[1]) + "-";
		}
		else if (lvls[1] == MAXINT) {
			lvlitem = to_string(lvls[0]) + "+";

		}
		else {
			lvlitem = to_string(lvls[0]) + "-" + to_string(lvls[1]);
		}


		if (positem.size() > 0 || negitem.size() > 0) {
			if (negitem.size() > 0) {
				negitem += "not ";

				if (positem.size() > 0) {
					positem += " but ";
				}
			}

			if (lvlitem.size() > 0) {
				negitem += " ";
			}
		}


		out.push_back(positem + negitem + "(" + lvlitem + ")");
	}

	string outstring = "";

	if (pos.size() == 0 || (req_lvl && !checks[4])) {
		outstring = NO_RESULTS;
	}
	else {
		for (string each : out) {
			outstring += each + " / ";
		}

		outstring = outstring.substr(0, outstring.length() - 3);
	}

	return "Valid Initial Routes: " + outstring + ".";
}

//Outputs valid destinations (from Constraints array) as string
string CVFPCPlugin::DestinationOutput(size_t origin_int, string dest) {
	vector<string> a{}; //Explicitly Permitted
	vector<string> b{}; //Implicitly Permitted (Not Explicitly Prohibited)

	for (size_t i = 0; i < config[origin_int]["sids"].Size(); i++) {
		if (config[origin_int]["sids"][i].HasMember("point") && config[origin_int]["sids"][i]["point"].IsString()) {
			bool push_a = false;
			bool push_b = false;

			const Value& conditions = config[origin_int]["sids"][i]["constraints"];
			for (size_t j = 0; j < conditions.Size(); j++) {
				if (conditions[j]["dests"].IsArray() && conditions[j]["dests"].Size()) {
					if (destArrayContains(conditions[j]["dests"], dest) != "") {
						push_a = true;
					}
				}
				else if (conditions[j]["nodests"].IsArray() && conditions[j]["nodests"].Size()) {
					if (destArrayContains(conditions[j]["nodests"], dest) == "") {
						push_b = true;
					}
				}
			}

			string sidstr = config[origin_int]["sids"][i]["point"].GetString();
			if (sidstr == "") {
				sidstr = "No SID";
			}

			if (push_a) {
				a.push_back(sidstr);
			}
			else if (push_b) {
				b.push_back(sidstr);
			}
		}
	}

	string out = "";

	if (a.size()) {
		out += "is valid for: ";

		for (string each : a) {
			out += each;
			out += RESULT_SEP;
		}

		out = out.substr(0, out.size() - 2) + ".";
	}

	if (b.size()) {
		if (a.size()) {
			out += " Additionally, " + dest + " ";
		}

		out += "may be valid for: ";

		for (string each : b) {
			out += each;
			out += RESULT_SEP;
		}

		out = out.substr(0, out.size() - 2) + ".";
	}

	if (out == "") {
		out = "No valid SIDs found for " + dest + out;
	}
	else {
		out = dest + " " + out;
	}

	return "Destination. " + out;
}

//Handles departure list menu and menu items
void CVFPCPlugin::OnFunctionCall(int FunctionId, const char * ItemString, POINT Pt, RECT Area) {
	try {
		if (FunctionId == TAG_FUNC_CHECKFP_MENU) {
			OpenPopupList(Area, "Options", 1);
			AddPopupListElement("Show Checks", "", TAG_FUNC_CHECKFP_CHECK);
			AddPopupListElement("Toggle Checks", "", TAG_FUNC_CHECKFP_DISMISS);
		}
		else if (FunctionId == TAG_FUNC_CHECKFP_CHECK) {
			checkFPDetail();
		}
		else if (FunctionId == TAG_FUNC_CHECKFP_DISMISS) {
			CFlightPlan flightPlan = FlightPlanSelectASEL();

			if (Enabled(flightPlan)) {
				flightPlan.GetControllerAssignedData().SetFlightStripAnnotation(0, "VFPC/OFF");
			}
			else {
				flightPlan.GetControllerAssignedData().SetFlightStripAnnotation(0, "");
			}
		}
	}
	catch (const std::exception& ex) {
		sendMessage("Error", ex.what());
		debugMessage("Error", ex.what());
	}
	catch (const std::string& ex) {
		sendMessage("Error", ex);
		debugMessage("Error", ex);
	}
	catch (...) {
		sendMessage("Error", "An unexpected error occured");
		debugMessage("Error", "An unexpected error occured");
	}
}

bool CVFPCPlugin::Enabled(CFlightPlan flightPlan) {
	try {
		string cad = flightPlan.GetControllerAssignedData().GetFlightStripAnnotation(0);
		if (!strcmp(cad.c_str(), "VFPC/OFF")) {
			return false;
		}
	}
	catch (const std::exception& ex) {
		sendMessage("Error", ex.what());
		debugMessage("Error", ex.what());
	}
	catch (const std::string& ex) {
		sendMessage("Error", ex);
		debugMessage("Error", ex);
	}
	catch (...) {
		sendMessage("Error", "An unexpected error occured");
		debugMessage("Error", "An unexpected error occured");
	}

	return true;
}

//Gets flight plan, checks if (S/D)VFR, calls checking algorithms, and outputs pass/fail result to departure list item
void CVFPCPlugin::OnGetTagItem(CFlightPlan FlightPlan, CRadarTarget RadarTarget, int ItemCode, int TagData, char sItemString[16], int* pColorCode, COLORREF* pRGB, double* pFontSize){
	try {
		if (ItemCode == TAG_ITEM_CHECKFP) {
			const char *origin = FlightPlan.GetFlightPlanData().GetOrigin();
			if (find(activeAirports.begin(), activeAirports.end(), origin) == activeAirports.end()) {
				activeAirports.push_back(origin);
			}

			if (validVersion && Enabled(FlightPlan) && airports.find(FlightPlan.GetFlightPlanData().GetOrigin()) != airports.end()) {
				string FlightPlanString = FlightPlan.GetFlightPlanData().GetRoute();
				int RFL = FlightPlan.GetFlightPlanData().GetFinalAltitude();

				*pColorCode = TAG_COLOR_RGB_DEFINED;
				string fpType{ FlightPlan.GetFlightPlanData().GetPlanType() };
				if (fpType == "V" || fpType == "S" || fpType == "D") {
					*pRGB = TAG_GREEN;
					strcpy_s(sItemString, 16, "VFR");
				}
				else {
					vector<vector<string>> validize = validizeSid(FlightPlan);
					vector<string> messageBuffer{ validize[0] }; // 0 = Callsign, 1 = SID, 2 = Destination, 3 = Route, 4 = Nav Performance, 5 = Min/Max Flight Level, 6 = Even/Odd, 7 = Suffix, 8 = Aircraft Type, 9 = Date/Time, 10 = Syntax, 11 = Passed/Failed

					strcpy_s(sItemString, 16, getFails(validize[0], pRGB).c_str());
				}
			}
			else {
				strcpy_s(sItemString, 16, " ");
			}
		}
	}
	catch (const std::exception& ex) {
		sendMessage("Error", ex.what());
		debugMessage("Error", ex.what());
	}
	catch (const std::string& ex) {
		sendMessage("Error", ex);
		debugMessage("Error", ex);
	}
	catch (...) {
		sendMessage("Error", "An unexpected error occured");
		debugMessage("Error", "An unexpected error occured");
	}
}

//Handles console commands
bool CVFPCPlugin::OnCompileCommand(const char * sCommandLine) {
	try {
		//Restart Automatic Data Loading
		if (startsWith((COMMAND_PREFIX + LOAD_COMMAND).c_str(), sCommandLine))
		{
			if (autoLoad) {
				sendMessage("Auto-Load Already Active.");
				debugMessage("Warning", "Auto-load activation attempted whilst already active.");
			}
			else {
				fileLoad = false;
				autoLoad = true;
				relCount = 0;
				sendMessage("Auto-Load Activated.");
				debugMessage("Info", "Auto-load reactivated.");
			}
			return true;
		}
		//Disable API and load from Sid.json file
		else if (startsWith((COMMAND_PREFIX + FILE_COMMAND).c_str(), sCommandLine))
		{
			autoLoad = false;
			fileLoad = true;
			sendMessage("Attempting to load from " + DATA_FILE + " file.");
			debugMessage("Info", "Will now load from " + DATA_FILE + " file.");
			getSids();
			return true;
		}
		//Activate Debug Logging
		else if (startsWith((COMMAND_PREFIX + LOG_COMMAND).c_str(), sCommandLine)) {
			if (debugMode) {
				debugMessage("Info", "Logging mode deactivated.");
				debugMode = false;
			}
			else {
				debugMode = true;
				debugMessage("Info", "Logging mode activated.");
			}
			return true;
		}
		//Text-Equivalent of "Show Checks" Button
		else if (startsWith((COMMAND_PREFIX + CHECK_COMMAND).c_str(), sCommandLine))
		{
			checkFPDetail();
			return true;
		}
	}
	catch (const std::exception& ex) {
		sendMessage("Error", ex.what());
		debugMessage("Error", ex.what());
	}
	catch (const std::string& ex) {
		sendMessage("Error", ex);
		debugMessage("Error", ex);
	}
	catch (...) {
		sendMessage("Error", "An unexpected error occured");
		debugMessage("Error", "An unexpected error occured");
	}

	return false;
}

//Compiles and outputs check details to user
void CVFPCPlugin::checkFPDetail() {
	try {
		if (validVersion) {
			CFlightPlan FlightPlan = FlightPlanSelectASEL();
			string fpType{ FlightPlan.GetFlightPlanData().GetPlanType() };
			if (fpType == "V" || fpType == "S" || fpType == "D") {
				string buf = "Flight Plan Checking Not Supported For VFR Flights.";
				sendMessage(FlightPlan.GetCallsign(), buf);
				debugMessage(FlightPlan.GetCallsign(), buf);
			}
			else {
				vector<vector<string>> validize = validizeSid(FlightPlanSelectASEL());
				vector<string> messageBuffer{ validize[0] }; // 0 = Callsign, 1 = SID, 2 = Destination, 3 = Route, 4 = Nav Performance, 5 = Min/Max Flight Level, 6 = Even/Odd, 7 = Suffix, 8 = Aircraft Type, 9 = Date/Time, 10 = Syntax, 11 = Passed/Failed
				vector<string> logBuffer{ validize[1] }; // 0 = Callsign, 1 = SID, 2 = Destination, 3 = Route, 4 = Nav Performance, 5 = Min/Max Flight Level, 6 = Even/Odd, 7 = Suffix, 8 = Aircraft Type, 9 = Date/Time, 10 = Syntax, 11 = Passed/Failed
				sendMessage(messageBuffer.front(), "Checking...");
#
				string buffer{ messageBuffer.at(1) + " | " };
				string logbuf{ logBuffer.at(1) + " | " };

				if (messageBuffer.at(1).find("Invalid") != 0) {
					for (size_t i = 2; i < messageBuffer.size() - 1; i++) {
						string temp = messageBuffer.at(i);
						string logtemp = logBuffer.at(i);

						if (temp != "-") {
							buffer += temp;
							buffer += " | ";
						}

						if (logtemp != "-") {
							logbuf += logtemp;
							logbuf += " | ";
						}
					}
				}

				buffer += messageBuffer.back();
				logbuf += logBuffer.back();

				sendMessage(messageBuffer.front(), buffer);
				debugMessage(logBuffer.front(), logbuf);
			}
		}
	}
	catch (const std::exception& ex) {
		sendMessage("Error", ex.what());
		debugMessage("Error", ex.what());
	}
	catch (const std::string& ex) {
		sendMessage("Error", ex);
		debugMessage("Error", ex);
	}
	catch (...) {
		sendMessage("Error", "An unexpected error occured");
		debugMessage("Error", "An unexpected error occured");
	}

}

//Compiles list of failed elements in flight plan, in preparation for adding to departure list
string CVFPCPlugin::getFails(vector<string> messageBuffer, COLORREF* pRGB) {
	try {
		*pRGB = TAG_RED;

		if (messageBuffer.at(messageBuffer.size() - 2).find("Invalid") == 0) {
			return "CHK";
		}
		else if (messageBuffer.at(1).find("SID - ") != 0 && messageBuffer.at(1).find("Non-SID Route.") != 0) {
			return "SID";
		}
		else if (messageBuffer.at(2).find("Failed") == 0) {
			return "DST";
		}
		else if (messageBuffer.at(3).find("Failed") == 0) {
			return "RTE";
		}
		else if (messageBuffer.at(4).find("Failed") == 0) {
			return "LVL";
		}
		else if (messageBuffer.at(5).find("Failed") == 0) {
			return "OER";
		}
		else if (messageBuffer.at(6).find("Invalid") == 0) {
			return "SUF";
		}
		else if (messageBuffer.at(7).find("Failed") == 0) {
			return "RST";
		}
		else if (messageBuffer.at(8).find("Warnings") == 0) {
			*pRGB = TAG_ORANGE;
		}
		else if (messageBuffer.at(9).find("Route Banned") == 0) {
			return "BAN";
		}
		else {
			*pRGB = TAG_GREEN;
		}

		return "OK!";
	}
	catch (const std::exception& ex) {
		sendMessage("Error", ex.what());
		debugMessage("Error", ex.what());
	}
	catch (const std::string& ex) {
		sendMessage("Error", ex);
		debugMessage("Error", ex);
	}
	catch (...) {
		sendMessage("Error", "An unexpected error occured");
		debugMessage("Error", "An unexpected error occured");
	}

	return "   ";
}

//Runs all web/file calls at once
void CVFPCPlugin::runWebCalls() {
	try {
		validVersion = versionCall();
		getSids();
	}
	catch (const std::exception& ex) {
		sendMessage("Error", ex.what());
		debugMessage("Error", ex.what());
	}
	catch (const std::string& ex) {
		sendMessage("Error", ex);
		debugMessage("Error", ex);
	}
	catch (...) {
		sendMessage("Error", "An unexpected error occured");
		debugMessage("Error", "An unexpected error occured");
	}
}

//Runs once per second, when EuroScope clock updates
void CVFPCPlugin::OnTimer(int Counter) {
	try {
		if (validVersion) {
			if (relCount == -1 && fut.valid() && fut.wait_for(1ms) == std::future_status::ready) {
				fut.get();
				activeAirports.clear();
				relCount = API_REFRESH_TIME;
			}

			if (relCount > 0) {
				relCount--;
			}

			// Loading proper Sids, when logged in
			if (GetConnectionType() != CONNECTION_TYPE_NO && relCount == 0) {
				fut = std::async(std::launch::async, &CVFPCPlugin::runWebCalls, this);
				relCount--;
			}
			else if (GetConnectionType() == CONNECTION_TYPE_NO) {
				airports.clear();
				config.Clear();
			}
		}
	}
	catch (const std::exception& ex) {
		sendMessage("Error", ex.what());
		debugMessage("Error", ex.what());
	}
	catch (const std::string& ex) {
		sendMessage("Error", ex);
		debugMessage("Error", ex);
	}
	catch (...) {
		sendMessage("Error", "An unexpected error occured");
		debugMessage("Error", "An unexpected error occured");
	}
}