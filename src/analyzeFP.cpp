#include "stdafx.h"
#include "analyzeFP.hpp"
#include <curl/curl.h>
#include <future>

extern "C" IMAGE_DOS_HEADER __ImageBase;

bool debugMode, validVersion, autoLoad, fileLoad, apiUpdated;

vector<int> timedata;
vector<int> lastupdate;
vector<string> logBuffer{};

size_t failPos;
int relCount;

std::future<void> fut;

using namespace std;
using namespace EuroScopePlugIn;

//Run on Plugin Initialization
CVFPCPlugin::CVFPCPlugin(void) :CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE, MY_PLUGIN_NAME, MY_PLUGIN_VERSION, MY_PLUGIN_DEVELOPER, MY_PLUGIN_COPYRIGHT)
{
	bufLog("Plugin: Load - Clearing Log...");
	clearLog();

	bufLog("Plugin: Load - Initialising Settings...");
	debugMode = false;
	validVersion = true; //Reset in first timer call
	autoLoad = true;
	fileLoad = false;

	bufLog("Plugin: Load - Resetting Counters...");
	failPos = 0;
	relCount = 0;

	bufLog("Plugin: Load - Initialising Time Data...");
	timedata = { 0, 0, 0, 0, 0, 0 }; // 0 = Year, 1 = Month, 2 = Day, 3 = Hour, 4 = Minute, 5 = Day of Week
	lastupdate = { 0, 0, 0, 0, 0 }; // 0 = Year, 1 = Month, 2 = Day, 3 = Hour, 4 = Minute

	bufLog("Plugin: Load - Initialising Version Data...");
	vector<string> installed = split(MY_PLUGIN_VERSION, '.');

	thisVersion = (int*)calloc(installed.size(), sizeof(int));
	for (size_t i = 0; i < installed.size(); i++) {
		thisVersion[i] = stoi(installed[i]);
	}

	bufLog("Plugin: Load - Sending Load Message...");
	string loadingMessage = "Loading complete. Version: ";
	loadingMessage += MY_PLUGIN_VERSION;
	loadingMessage += ".";
	sendMessage(loadingMessage);

	// Register Tag Item "VFPC"
	if (validVersion) {
		bufLog("Plugin: Load - Registering Tag Item...");
		RegisterTagItemType("VFPC", TAG_ITEM_CHECKFP);
		RegisterTagItemFunction("Options", TAG_FUNC_CHECKFP_MENU);
	}

	bufLog("Plugin: Load - Complete");
}

//Run on Plugin Destruction (Closing EuroScope or unloading plugin)
CVFPCPlugin::~CVFPCPlugin()
{
	bufLog("Plugin: Unloading...");
	writeLog();
}

//Stores output of HTTP request in string
static size_t curlCallback(void *contents, size_t size, size_t nmemb, void *outString)
{
	// For Curl, we should assume that the data is not null terminated, so add a null terminator on the end
 	((std::string*)outString)->append(reinterpret_cast<char*>(contents), size * nmemb);
	return size * nmemb;
}

//Gets path to current directory
string CVFPCPlugin::getPath() {
	char DllPathFile[_MAX_PATH];
	GetModuleFileNameA(HINSTANCE(&__ImageBase), DllPathFile, sizeof(DllPathFile));
	string path = DllPathFile;
	path.resize(path.size() - strlen(PLUGIN_FILE.c_str()));

	return path;
}

//Clear log file
bool CVFPCPlugin::clearLog() {
	bufLog("Log: Clearing...");
	try {
		logBuffer.clear();

		string path = getPath();
		path += LOG_FILE;

		ofstream ofs;
		ofs.open(path.c_str(), ios::trunc);

		if (ofs.is_open()) {
			ofs << "Log: Cleared Successfully." << std::endl;
			ofs.close();
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

//Write to log buffer
bool CVFPCPlugin::bufLog(string message) {
	try {
		logBuffer.push_back(message);
		return true;
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

//Write contents of log buffer to file
bool CVFPCPlugin::writeLog() {
	bufLog("Log: File - Writing...");
	try {
		string path = getPath();
		path += LOG_FILE;

		ifstream ifs;
		ifs.open(path.c_str());
		vector<string> file{};

		if (ifs.is_open()) {
			string line;
			while (getline(ifs, line)) {
				file.push_back(line);
			}
		}

		file.insert(file.end(), logBuffer.begin(), logBuffer.end());

		ifs.close();
		size_t start = 0;

		if (file.size() > 20000) {
			start = file.size() - 20000;
		}

		ofstream ofs;
		ofs.open(path.c_str(), ios::trunc);

		if (ofs.is_open()) {
			for (size_t i = start; i < file.size(); i++) {
				ofs << file.at(i).c_str() << std::endl;
			}
			ofs.close();
			logBuffer.clear();
			bufLog("Log: File - Write Complete");
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

//Send message to user via "VFPC Log" channel
void CVFPCPlugin::debugMessage(string type, string message) {
	try {
		// Display Debug Message if debugMode = true
		if (debugMode) {
			DisplayUserMessage("VFPC Log", type.c_str(), message.c_str(), true, true, true, false, false);
			bufLog("Debug Message: " + type + " - " + message);
		}
		else {
			bufLog("Hidden Debug Message: " + type + " - " + message);
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
		bufLog("Message: " + type + " - " + message);
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
		bufLog("Message: System - " + message);
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
	bufLog("Web Call To " + url + ": Launching...");
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
		bufLog("Web Call To " + url + ": Successful");
		return true;
	}
	
	bufLog("Web Call To " + url + ": Failed");
	return false;
}

//Makes CURL call to API server for data and stores output
bool CVFPCPlugin::APICall(string endpoint, Document& out) {
	string url = MY_API_ADDRESS + endpoint;
	string buf = "";
	bufLog("API Call To " + url + ": Launching...");

	if (webCall(url, buf))
	{
		bufLog("API Call To " + url + ": Returned Data");
		if (out.Parse<0>(buf.c_str()).HasParseError())
		{
			sendMessage("An error occurred whilst reading data. The plugin will not automatically attempt to reload from the API. To restart data fetching, type \".vfpc load\".");
			debugMessage("Error", str(boost::format("Config Download: %s (Offset: %i)\n'") % out.GetParseError() % out.GetErrorOffset()));
			bufLog("API Call To " + url + ": Failed - Data Returned But Unreadable");
			return false;

			out.Parse<0>("[]");
		}
	}
	else
	{
		sendMessage("An error occurred whilst downloading data. The plugin will not automatically attempt to reload from the API. Check your connection and restart data fetching by typing \".vfpc load\".");
		debugMessage("Error", "Failed to download data from API.");
		bufLog("API Call To " + url + ": Failed - No Data Returned");
		return false;

		out.Parse<0>("[]");
	}

	bufLog("API Call To " + url + ": Successful");
	return true;
}

//Makes CURL call to API server for current date, time, and version and stores output
bool CVFPCPlugin::versionCall() {
	Document version;
	bufLog("Version Call: Launching...");
	APICall("version", version);

	bool out = false;

	if (version.HasMember("vfpc_version") && version["vfpc_version"].IsString() && version.HasMember("min_version") && version["min_version"].IsString()) {
		bufLog("Version Call: Version Data Found");
		vector<string> minver = split(version["min_version"].GetString(), '.');
		bool minchange = false;
		vector<string> curver = split(version["vfpc_version"].GetString(), '.');
		bool curchange = false;
		bool check = true;

		for (size_t i = 0; i < minver.size(); i++) {
			int temp = stoi(minver[i]);
			if (i < minVersion.size()) {
				if (check && temp > minVersion[i]) {
					minchange = true;
				}
				else if (temp != minVersion[i]) {
					check = false;
				}
				minVersion[i] = temp;
			}
			else {
				minchange = true;
				minVersion.push_back(temp);
			}
		}

		if (minVersion.size() > minver.size()) {
			minVersion.resize(minver.size());
		}

		for (size_t i = 0; i < curver.size(); i++) {
			int temp = stoi(curver[i]);
			if (i < curVersion.size()) {
				if (check && temp > curVersion[i]) {
					curchange = true;
				}
				else if (temp != curVersion[i]) {
					check = false;
				}
				curVersion[i] = temp;
			}
			else {
				curchange = true;
				curVersion.push_back(temp);
			}
		}

		if (curVersion.size() > curver.size()) {
			curVersion.resize(curver.size());
		}

		if (minVersion[0] > thisVersion[0] || (minVersion[0] == thisVersion[0] && minVersion[1] > thisVersion[1]) || (minVersion[0] == thisVersion[0] && minVersion[1] == thisVersion[1] && minVersion[2] > thisVersion[2])) {
			bufLog("Version Call: Discontinued Version In Use");
			if (minchange) {
				sendMessage("Update required - the plugin has been disabled. Please update and reload the plugin to continue. (Note: .vfpc load will NOT work.)");
			}
		}
		else if (curVersion[0] > thisVersion[0] || (curVersion[0] == thisVersion[0] && curVersion[1] > thisVersion[1]) || (curVersion[0] == thisVersion[0] && curVersion[1] == thisVersion[1] && curVersion[2] > thisVersion[2])) {
			bufLog("Version Call: Outdated Version In Use");
			if (curchange) {
				sendMessage("Update available - you may continue using the plugin, but please update as soon as possible.");
			}
			out = true;
		}
		else {
			bufLog("Version Call: No New Version Since Last Check");
			out = true;
		}
	}
	else {
		bufLog("Version Call: Version Data Not Found");
		sendMessage("Failed to check for updates - the plugin has been disabled. If no updates are available, please unload and reload the plugin to try again. (Note: .vfpc load will NOT work.)");
	}

	if (loadedAirports != activeAirports) {
		bufLog("Version Call: Active Airports Changed - Forcing Update...");
		apiUpdated = true;
	}

	bool updatefail = false;
	vector<int> newdate = { 0, 0, 0 };

	if (version.HasMember("date") && version["date"].IsString() && version.HasMember("last_updated_date") && version["last_updated_date"].IsString() && version.HasMember("last_updated_time") && version["last_updated_time"].IsString()) {
		bufLog("Version Call: Update Data Found");
		bufLog("Version Call: Checking Last Updated Date...");
		string lastdate = version["last_updated_date"].GetString();
		bufLog("Version Call: Last Updated Date Entry Exists");
		if (lastdate.size() == 10) {
			try {
				int lastday = stoi(lastdate.substr(0, 2));
				int lastmonth = stoi(lastdate.substr(3, 2));
				int lastyear = stoi(lastdate.substr(6, 4));

				lastupdate[0] = lastyear;
				lastupdate[1] = lastmonth;
				lastupdate[2] = lastday;
				bufLog("Version Call: Last Updated Date Data Read and Saved Successfully");
			}
			catch (...) {
				bufLog("Version Call: Last Updated Date Data Unreadable - String->Int Failed");
				updatefail = true;
			}
		}
		else {
			bufLog("Version Call: Last Updated Date Data Unreadable - Wrong Size");
			updatefail = true;
		}

		bufLog("Version Call: Checking Last Updated Time...");
		string lasttime = version["last_updated_time"].GetString();
		bufLog("Version Call: Last Updated Time Entry Exists");
		if (lasttime.size() == 5) {
			try {
				int lasthour = stoi(lasttime.substr(0, 2));
				int lastmins = stoi(lasttime.substr(3, 2));

				lastupdate[3] = lasthour;
				lastupdate[4] = lastmins;
				bufLog("Version Call: Last Updated Time Data Read and Saved Successfully");
			}
			catch (...) {
				bufLog("Version Call: Last Updated Time Data Unreadable - String->Int Failed");
				updatefail = true;
			}
		}
		else {
			bufLog("Version Call: Last Updated Time Data Unreadable - Wrong Size");
			updatefail = true;
		}

		bufLog("Version Call: Checking Date...");
		string date = version["date"].GetString();
		bufLog("Version Call: Date Entry Exists");
		if (date.size() == 10) {
			try {
				int day = stoi(date.substr(0, 2));
				int month = stoi(date.substr(3, 2));
				int year = stoi(date.substr(6, 4));

				newdate[0] = year;
				newdate[1] = month;
				newdate[2] = day;
				bufLog("Version Call: Date Data Read and Presaved Successfully");
			}
			catch (...) {
				bufLog("Version Call: Date Data Unreadable - String->Int Failed");
				updatefail = true;
			}
		}
		else {
			bufLog("Version Call: Date Data Unreadable - Wrong Size");
			updatefail = true;
		}
	}
	else {
		bufLog("Version Call: Update Data Not Found");
		updatefail = true;
	}

	if (updatefail) {
		sendMessage("Failed to read date/last update record from API.");
		apiUpdated = true;
	}
	else {
		bufLog("Version Call: Checking Whether Update Has Occurred...");
		bool stop = false;

		for (size_t i = 0; i < lastupdate.size(); i++) {
			if (!stop) {
				if (lastupdate[i] > timedata[i]) {
					apiUpdated = true;
					stop = true;
					bufLog("Version Call: Update Has Occurred - Pull From API Next Pass.");
				}
				else if (lastupdate[i] != timedata[i]) {
					stop = true;
					bufLog("Version Call: Update Has Not Occurred.");
				}
			}
		}

		if (!stop) {
			apiUpdated = true;
		}

		bufLog("Version Call: Update Check Complete.");
	}

	for (size_t i = 0; i < newdate.size(); i++) {
		timedata[i] = newdate[i];
	}
	bufLog("Version Call: Date Data Saved Successfully");

	bool timefail = false;

	if (version.HasMember("time") && version["time"].IsString() && version.HasMember("day") && version["day"].IsInt()) {
		bufLog("Version Call: Day/Time Data Found");
		bufLog("Version Call: Checking Weekday...");
		int day = version["day"].GetInt();
		bufLog("Version Call: Weekday Entry Exists");
		day += 6;
		day %= 7;
		bufLog("Version Call: Weekday Data Read Successfully");

		bufLog("Version Call: Checking Time...");
		string time = version["time"].GetString();
		bufLog("Version Call: Time Entry Exists");
		if (time.size() == 5) {
			try {
				int hour = stoi(time.substr(0, 2));
				int mins = stoi(time.substr(3, 2));

				timedata[3] = hour;
				timedata[4] = mins;
				bufLog("Version Call: Time Data Read and Saved Successfully");

				timedata[5] = day;
				bufLog("Version Call: Weekday Data Saved Successfully");
			}
			catch (...) {
				bufLog("Version Call: Time Data Unreadable - String->Int Failed");
				timefail = true;
			}
		}
		else {
			bufLog("Version Call: Time Data Unreadable - Wrong Size");
			timefail = true;
		}
	}
	else {
		bufLog("Version Call: Time Data Not Found");
		timefail = true;
	}
	   
	if (timefail) {
		sendMessage("Failed to read day/time from API.");
	}

	return out;
}

//Loads data from file
bool CVFPCPlugin::fileCall(Document &out) {
	bufLog("File Load Requested");
	string path = getPath();
	path += DATA_FILE;

	bufLog("Opening " + DATA_FILE + " File");
	stringstream ss;
	ifstream ifs;
	ifs.open(path.c_str(), ios::binary);

	if (ifs.is_open()) {
		bufLog(DATA_FILE + " File Opened");
		ss << ifs.rdbuf();
		ifs.close();
		bufLog(DATA_FILE + " File Read");

		if (out.Parse<0>(ss.str().c_str()).HasParseError()) {
			sendMessage("An error occurred whilst reading data. The plugin will not automatically attempt to reload. To restart data fetching from the API, type \"" + COMMAND_PREFIX + LOAD_COMMAND + "\". To reattempt loading data from the Sid.json file, type \"" + COMMAND_PREFIX + FILE_COMMAND + "\".");
			debugMessage("Error", str(boost::format("Config Parse: %s (Offset: %i)\n'") % out.GetParseError() % out.GetErrorOffset()));
			bufLog("File Read Failed - Data Found But Unreadable");

			out.Parse<0>("[]");
			return false;
		}
	}
	else {
		sendMessage(DATA_FILE + " file not found. The plugin will not automatically attempt to reload. To restart data fetching from the API, type \"" + COMMAND_PREFIX + LOAD_COMMAND + "\". To reattempt loading data from the Sid.json file, type \"" + COMMAND_PREFIX + FILE_COMMAND + "\".");
		debugMessage("Error", DATA_FILE + " file not found.");
		bufLog("File Read Failed - File Not Found");

		out.Parse<0>("[]");
		return false;
	}

	bufLog("File Read Successful");
	return true;
}

//Loads data and sorts into airports
void CVFPCPlugin::getSids() {
	CVFPCPlugin::bufLog("SID Data: Reloading...");
	try {
		//Load data from API
		if (autoLoad) {
			if (apiUpdated) {
				bufLog("SID Data: From API - Loading...");
				if (activeAirports.size() > 0) {
					bufLog("SID Data: From API - Active Airports Found.");
					string endpoint = "airport?icao=";

					for (size_t i = 0; i < activeAirports.size(); i++) {
						bufLog("SID Data: From API - Requesting For " + activeAirports[i] + ".");
						endpoint += activeAirports[i] + "+";
					}

					endpoint = endpoint.substr(0, endpoint.size() - 1);

					autoLoad = APICall(endpoint, config);
					bufLog("SID Data: From API - Loaded.");
				}

				apiUpdated = false;
			}
			else {
				bufLog("SID Data: From API - No Pull Required.");
			}
		}
		//Load data from Sid.json file
		else if (fileLoad) {
			CVFPCPlugin::bufLog("SID Data: From File - Loading...");
			fileLoad = fileCall(config);
			CVFPCPlugin::bufLog("SID Data: From File - Loaded.");
		}

		//Sort new data into airports
		CVFPCPlugin::bufLog("SID Data: Sorting...");
		airports.clear();
		for (SizeType i = 0; i < config.Size(); i++) {
			const Value& airport = config[i];
			if (airport.HasMember("icao") && airport["icao"].IsString()) {
				string airport_icao = airport["icao"].GetString();
				CVFPCPlugin::bufLog("SID Data: " + airport_icao + " - Found.");
				airports.insert(pair<string, SizeType>(airport_icao, i));
				CVFPCPlugin::bufLog("SID Data: " + airport_icao + " - Inserted.");
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

vector<bool> CVFPCPlugin::checkDestination(const Value& conditions, string destination, vector<bool> in) {
	vector<bool> out{};

	for (size_t i = 0; i < conditions.Size(); i++) {
		if (!in[i]) {
			out.push_back(false);
			continue;
		}

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

		out.push_back(res);
	}

	return out;
}

vector<bool> CVFPCPlugin::checkExitPoint(const Value& conditions, vector<string> points, vector<bool> in) {
	vector<bool> out{};

	for (size_t i = 0; i < conditions.Size(); i++) {
		if (!in[i]) {
			out.push_back(false);
			continue;
		}

		bool res = true;

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

		out.push_back(res);
	}

	return out;
}

vector<bool> CVFPCPlugin::checkRoute(const Value& conditions, vector<string> route, vector<bool> in) {
	vector<bool> out{};

	for (size_t i = 0; i < conditions.Size(); i++) {
		if (!in[i]) {
			out.push_back(false);
			continue;
		}

		bool res = true;

		if (conditions[i].HasMember("route") && conditions[i]["route"].IsArray() && conditions[i]["route"].Size() && !routeContains(route, conditions[i]["route"])) {
			res = false;
		}

		if (conditions[i].HasMember("noroute") && res && conditions[i]["noroute"].IsArray() && conditions[i]["noroute"].Size() && routeContains(route, conditions[i]["noroute"])) {
			res = false;
		}

		out.push_back(res);
	}

	return out;
}

vector<bool> CVFPCPlugin::checkRestriction(CFlightPlan flightPlan, string sid_suffix, const Value& restrictions, bool *sidfails, bool *constfails) {
	bufLog(string(flightPlan.GetCallsign()) + " Restrictions Check: " + " - SID Suffix: " + sid_suffix + ", SID Fails: " + BoolToString(*sidfails) + ", Const Fails" + BoolToString(*constfails));
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
							if (timedata[3] > starttime[0] || (timedata[3] == starttime[0] && timedata[4] >= starttime[1]) || timedata[3] < endtime[0] || (timedata[3] == endtime[0] && timedata[4] <= endtime[1])) {
								valid = true;
							}
						}
						else {
							if ((timedata[3] > starttime[0] || (timedata[3] == starttime[0] && timedata[4] >= starttime[1])) && (timedata[3] < endtime[0] || (timedata[3] == endtime[0] && timedata[4] <= endtime[1]))) {
								valid = true;
							}
						}
					}
					else if (startdate == enddate) {
						if (!time) {
							valid = true;
						}
						else if ((timedata[3] > starttime[0] || (timedata[3] == starttime[0] && timedata[4] >= starttime[1])) && (timedata[3] < endtime[0] || (timedata[3] == endtime[0] && timedata[4] <= endtime[1]))) {
							valid = true;
						}
					}
					else if (startdate < enddate) {
						if (timedata[5] > startdate && timedata[5] < enddate) {
							valid = true;
						}
						else if (timedata[5] == startdate) {
							if (!time || timedata[3] > starttime[0] || (timedata[3] == starttime[0] && timedata[4] >= starttime[1])) {
								valid = true;
							}
						}
						else if (timedata[5] == enddate) {
							if (!time || timedata[3] < endtime[0] || (timedata[3] == endtime[0] && timedata[4] < endtime[1])) {
								valid = true;
							}
						}
					}
					else if (startdate > enddate) {
						if (timedata[5] < startdate || timedata[5] > enddate) {
							valid = true;
						}
						else if (timedata[5] == startdate) {
							if (!time || timedata[3] > starttime[0] || (timedata[3] == starttime[0] && timedata[4] >= starttime[1])) {
								valid = true;
							}
						}
						else if (timedata[5] == enddate) {
							if (!time || timedata[3] < endtime[0] || (timedata[3] == endtime[0] && timedata[4] < endtime[1])) {
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

		bufLog(string(flightPlan.GetCallsign()) + " Restrictions Check: " + " - Complete");
	}
	else {
		bufLog(string(flightPlan.GetCallsign()) + " Restrictions Check: " + " - Failed, Restrictions Array Not Found");
	}

	if (!constExists) {
		res[0] = true;
	}

	bufLog(string(flightPlan.GetCallsign()) + " Restrictions Check: " + " - Constraint-Level Success: " + BoolToString(res[0]) + ", SID-Level Success: " + BoolToString(res[1]));
	return res;
}

vector<bool> CVFPCPlugin::checkRestrictions(CFlightPlan flightPlan, const Value& conditions, string sid_suffix, bool *sidfails, bool *constfails, bool *sidwide, vector<bool> in) {
	vector<bool> out{};

	for (size_t i = 0; i < conditions.Size(); i++) {
		if (!in[i]) {
			out.push_back(false);
			continue;
		}

		bool res = true;

		vector<bool> temp = checkRestriction(flightPlan, sid_suffix, conditions[i]["restrictions"], sidfails, constfails);

		res = temp[0];
		if (temp[1]) {
			*sidwide = true;
		}

		out.push_back(res);
	}

	return out;
}

vector<bool> CVFPCPlugin::checkMinMax(const Value& conditions, int RFL, vector<bool> in) {
	vector<bool> out{};

	for (size_t i = 0; i < conditions.Size(); i++) {
		if (!in[i]) {
			out.push_back(false);
			continue;
		}

		bool res = true;

		int Min, Max;

		//Min Level
		if (conditions[i].HasMember("min") && (Min = conditions[i]["min"].GetInt()) > 0 && (RFL / 100) < Min) {
			res = false;
		}

		//Max Level
		if (conditions[i].HasMember("max") && (Max = conditions[i]["max"].GetInt()) > 0 && (RFL / 100) > Max) {
			res = false;
		}

		out.push_back(res);
	}

	return out;
}

vector<bool> CVFPCPlugin::checkDirection(const Value& conditions, int RFL, vector<bool> in) {
	vector<bool> out{};

	for (size_t i = 0; i < conditions.Size(); i++) {
		if (!in[i]) {
			out.push_back(false);
			continue;
		}

		//Assume any level valid if no "EVEN" or "ODD" declaration
		bool res = true;

		if (conditions[i].HasMember("dir") && conditions[i]["dir"].IsString()) {
			string direction = conditions[i]["dir"].GetString();
			boost::to_upper(direction);

			if (direction == EVEN_DIRECTION) {
				//Assume invalid until condition matched
				res = false;

				//Non-RVSM (Above FL410)
				if ((RFL > RVSM_UPPER && ((RFL - RVSM_UPPER) / 1000) % 4 == 2)) {
					res = true;
				}
				//RVSM (FL290-410) or Below FL290
				else if (RFL <= RVSM_UPPER && (RFL / 1000) % 2 == 0) {
					res = true;
				}
			}
			else if (direction == ODD_DIRECTION) {
				//Assume invalid until condition matched
				res = false;

				//Non-RVSM (Above FL410)
				if ((RFL > RVSM_UPPER && ((RFL - RVSM_UPPER) / 1000) % 4 == 0)) {
					res = true;
				}
				//RVSM (FL290-410) or Below FL290
				else if (RFL <= RVSM_UPPER && (RFL / 1000) % 2 == 1) {
					res = true;
				}
			}
		}

		out.push_back(res);
	}

	return out;
}

vector<bool> CVFPCPlugin::checkAlerts(const Value& conditions, bool *warn, vector<bool> in) {
	vector<bool> out{};

	for (size_t i = 0; i < conditions.Size(); i++) {
		if (!in[i]) {
			out.push_back(false);
			continue;
		}

		bool res = true;

		if (conditions[i]["alerts"].IsArray() && conditions[i]["alerts"].Size()) {
			for (size_t j = 0; j < conditions[i]["alerts"].Size(); j++) {
				if (conditions[i]["alerts"][j].HasMember("ban") && conditions[i]["alerts"][j]["ban"].GetBool()) {
					res = false;
				}

				if (conditions[i]["alerts"][j].HasMember("warn") && conditions[i]["alerts"][j]["warn"].GetBool()) {
					*warn = true;
				}
			}
		}

		out.push_back(res);
	}

	return out;
}

//Checks flight plan
vector<vector<string>> CVFPCPlugin::validateSid(CFlightPlan flightPlan) {
	string callsign = flightPlan.GetCallsign();
	bufLog(callsign + string(" Validating..."));
	//out[0] = Normal Output, out[1] = Debug Output
	vector<vector<string>> returnOut = { vector<string>(), vector<string>() }; // 0 = Callsign, 1 = SID, 2 = Destination, 3 = Exit Point, 4 = Route, 5 = Min/Max Flight Level, 6 = Even/Odd, 7 = Suffix, 8 = Restrictions, 9 = Warnings, 10 = Bans, 11 = Syntax, 12 = Passed/Failed

	bufLog(callsign + string(" Validate: Outputs - Initialising..."));
	returnOut[0].push_back(callsign);
	returnOut[1].push_back(callsign);
	for (int i = 1; i < 13; i++) {
		returnOut[0].push_back("-");
		returnOut[1].push_back("-");
	}

	returnOut[0].back() = returnOut[1].back() = "Failed";

	bufLog(callsign + string(" Validate: Airports - Getting..."));
	string origin = flightPlan.GetFlightPlanData().GetOrigin(); boost::to_upper(origin);
	bufLog(callsign + string(" Validate: Airports - Found Origin ") + origin);
	string destination = flightPlan.GetFlightPlanData().GetDestination(); boost::to_upper(destination);
	bufLog(callsign + string(" Validate: Airports - Found Destination ") + destination);
	SizeType origin_int;

	bufLog(callsign + string(" Validate: Origin - Finding " + origin + " Data..."));
	// Airport defined
	if (airports.find(origin) == airports.end()) {
		bufLog(callsign + string(" Validate: Origin - " + origin + " Not Found"));
		returnOut[0][1] = "Airport Not Found";
		returnOut[0].back() = "Failed";

		returnOut[1][1] = origin + " not in database.";
		returnOut[1].back() = "Failed";
		return returnOut;
	}
	else
	{
		bufLog(callsign + string(" Validate: Origin - " + origin + " Found"));
		origin_int = airports[origin];
	}

	bufLog(callsign + string(" Validate: RFL - Getting..."));
	int RFL = flightPlan.GetFlightPlanData().GetFinalAltitude();
	bufLog(callsign + string(" Validate: RFL - ") + to_string(RFL));

	bufLog(callsign + string(" Validate: Route - Getting..."));
	string rawroute = flightPlan.GetFlightPlanData().GetRoute();
	bufLog(callsign + string(" Validate: Route - ") + rawroute);
	bufLog(callsign + string(" Validate: Route - Trimming..."));
	boost::trim(rawroute);

	bufLog(callsign + string(" Validate: Route - Splitting..."));
	vector<string> route = split(rawroute, ' ');
	bufLog(callsign + string(" Validate: Route - Removing Extra Whitespace..."));
	vector<string>::iterator itr = remove_if(route.begin(), route.end(), mem_fun_ref(&string::empty));
	route.erase(itr, route.end());
	bufLog(callsign + string(" Validate: Route - Converting To Upper Case..."));
	for (size_t i = 0; i < route.size(); i++) {
		boost::to_upper(route[i]);
	}

	bufLog(callsign + string(" Validate: Extracted Route Points - Getting..."));
	vector<string> points{};
	CFlightPlanExtractedRoute extracted = flightPlan.GetExtractedRoute();
	bufLog(callsign + string(" Validate: Extracted Route Points - Saving..."));
	for (int i = 0; i < extracted.GetPointsNumber(); i++) {
		points.push_back(extracted.GetPointName(i));
	}

	bufLog(callsign + string(" Validate SID - Getting..."));
	string sid = flightPlan.GetFlightPlanData().GetSidName(); boost::to_upper(sid);
	string first_wp = "";
	string sid_suffix = "";

	bufLog(callsign + string(" Validate: SID - Validating..."));
	//Route with SID
	if (sid.length()) {
		bufLog(callsign + string(" Validate: SID Exists - " + sid + ", Checking For Outdated Indicator..."));
		// Remove any # characters from SID name
		boost::erase_all(sid, OUTDATED_SID);

		bufLog(callsign + string(" Validate: First Waypoint - Getting..."));
		if (origin == "EGLL" && sid == "CHK") {
			bufLog(callsign + string(" Validate: First Waypoint - EGLL CPT Easterly Procedure In Use"));
			first_wp = "CPT";
			sid_suffix = "CHK";
		}
		else {
			first_wp = sid.substr(0, sid.find_first_of("0123456789"));
			sid_suffix = sid.back();
			if (0 != first_wp.length())
				bufLog(callsign + string(" Validate: First Waypoint - Converting To Upper Case..."));
				boost::to_upper(first_wp);
		}
	}

	bufLog(callsign + string(" Validate: Route Syntax - Checking..."));
	// Matches Speed/Alt Data In Route
	regex spdlvl("(N|M|K)[0-9]{3,4}((A|F)[0-9]{3}|(S|M)[0-9]{4})");
	regex spdlvlslash("\/(N|M|K)[0-9]{3,4}((A|F)[0-9]{3}|(S|M)[0-9]{4})((A|F)[0-9]{3}|(S|M)[0-9]{4})?");
	regex icaorwy("[A-Z]{4}(\/[0-9]{2}(L|C|R)?)?");
	regex sidstarrwy("[A-Z]{2,5}[0-9][A-Z](\/[0-9]{2}(L|C|R)?)?");
	regex dctspdlvl("DCT\/(N|M|K)[0-9]{3,4}((A|F)[0-9]{3}|(S|M)[0-9]{4})");
	//regex wpt("[A-Z]{2}([A-Z]([A-Z]{2})?)?(/(N|M|K)[0-9]{3,4}((A|F)[0-9]{3}|(S|M)[0-9]{4}))?");
	//regex coord("([0-9]{4}(N|S))|([0-9]{2}(N|S)[0-9]{2,3}(E|W))(/(N|M|K)[0-9]{3,4}((A|F)[0-9]{3}|(S|M)[0-9]{4}))?");
	regex awy("(U)?[A-Z][0-9]{1,3}([A-Z])?");

	bool success = true;
	vector<string> new_route{};
	string outchk{};
	bool repeat = false;

	for (size_t i = 0; i < 5; i++) {
		if (success) {
			if (route.size() > 0) {
				bufLog(callsign + string(" Validate: Route Syntax - Running Check ") + to_string(i) + string("..."));
				switch (i) {
				case 0:
					if (regex_match(route.front(), spdlvl)) {
						route.erase(route.begin());
					}
					break;
				case 1:
					do {
						repeat = true;

						if (regex_match(route.front(), sidstarrwy)) {
							route.erase(route.begin());
						}
						else if (!strcmp(route.front().c_str(), "SID")) {
							route.erase(route.begin());
						}
						else if (regex_match(route.front(), icaorwy)) {
							if (!strcmp(route.front().substr(0, 4).c_str(), origin.c_str())) {
								route.erase(route.begin());
							}
							else {
								outchk = "Different Origin in Route";
								success = false;
								repeat = false;
							}
						}
						else {
							repeat = false;
						}
					} while (repeat);
					break;
				case 2:
					do {
						repeat = true;

						if (regex_match(route.back(), sidstarrwy)) {
							route.pop_back();
						}
						else if (!strcmp(route.back().c_str(), "STAR")) {
							route.pop_back();
						}
						else if (regex_match(route.back(), icaorwy)) {
							if (regex_match(route.back(), icaorwy)) {
								if (!strcmp(route.back().substr(0, 4).c_str(), destination.c_str())) {
									route.pop_back();
								}
								else {
									outchk = "Different Destination in Route";
									success = false;
									repeat = false;
								}
							}
						}
						else {
							repeat = false;
						}
					} while (repeat);
					break;
				case 3:
					for (string each : route) {
						if (regex_match(each, dctspdlvl)) {
							success = false;
						}
						else if (strcmp(each.c_str(), "DCT")) {
							if (regex_match(each, awy)) {
								new_route.push_back(each);
							}
							else {
								size_t slash = each.find('/');

								if (slash == string::npos) {
									new_route.push_back(each);
								}
								else {
									string chng = each.substr(slash);

									if (regex_match(chng, spdlvlslash)) {
										new_route.push_back(each.substr(0, slash));
									}
									else {
										outchk = "Invalid Speed/Level Change";
										success = false;
									}
								}
							}
						}
					}

					route = new_route;
					break;
				case 4:
					if (sid.length()) {
						if (strcmp(route.front().c_str(), first_wp.c_str())) {
							outchk = "Route Not From First Waypoint";
							success = false;
						}
						else {
							route.erase(route.begin());
						}
					}

					break;
				}
			}
			else {
				outchk = "No Route";
				success = false;
			}
		}
	}

	if (!success) {
		bufLog(callsign + string(" Validate: Route Syntax - Failed"));
		returnOut[0][returnOut[0].size() - 2] = returnOut[1][returnOut[1].size() - 2] = "Invalid Syntax - " + outchk + ".";
		returnOut[0].back() = returnOut[1].back() = "Failed";
		return returnOut;
	}

	bufLog(callsign + string(" Validate: Route Syntax - Passed"));

	bufLog(callsign + string(" Validate: SID - Checking Definitions..."));
	// Any SIDs defined
	if (!config[origin_int].HasMember("sids") || !config[origin_int]["sids"].IsArray() || !config[origin_int]["sids"].Size()) {
		bufLog(callsign + string(" Validate: SID - None Defined..."));
		returnOut[0][1] = "No SIDs or Non-SID Routes Defined";
		returnOut[0].back() = "Failed";

		returnOut[1][1] = origin + " exists in database but has no SIDs (or non-SID routes) defined.";
		returnOut[1].back() = "Failed";
		return returnOut;
	}

	bufLog(callsign + string(" Validate: SID - Finding Definition..."));
	//Find routes for selected SID
	size_t pos = string::npos;
	if (config[origin_int]["sids"].Size() == 1 && config[origin_int]["sids"].HasMember("point") && config[origin_int]["sids"]["point"].IsString() && config[origin_int]["sids"]["point"].GetString() == "") {
		bufLog(callsign + string(" Validate: SID - Bypassing Definition, Non-SID Airport"));
		pos = 0;
	}
	else {
		for (size_t i = 0; i < config[origin_int]["sids"].Size(); i++) {
			if (config[origin_int]["sids"][i].HasMember("point") && !first_wp.compare(config[origin_int]["sids"][i]["point"].GetString()) && config[origin_int]["sids"][i].HasMember("constraints") && config[origin_int]["sids"][i]["constraints"].IsArray()) {
				bufLog(callsign + string(" Validate: SID - Found Definition"));
				pos = i;
			}
			else if (config[origin_int]["sids"][i]["alias"].IsArray() && config[origin_int]["sids"][i]["alias"].Size()) {
				for (size_t j = 0; j < config[origin_int]["sids"][i]["alias"].Size(); j++) {
					if (!first_wp.compare(config[origin_int]["sids"][i]["alias"][j].GetString()) && config[origin_int]["sids"][i].HasMember("constraints") && config[origin_int]["sids"][i]["constraints"].IsArray()) {
						bufLog(callsign + string(" Validate: SID - Found Alias"));
						pos = i;
					}
				}				
			}
		}
	}

	// Needed SID defined
	if (pos == string::npos) {
		bufLog(callsign + string(" Validate: SID - Definition Not Found..."));
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
		bufLog(callsign + string(" Validate: SID - " + sid + " Definition Found, Saving..."));
		const Value& sid_ele = config[origin_int]["sids"][pos];
		const Value& conditions = sid_ele["constraints"];

		int round = 0;

		bufLog(callsign + string(" Validate: Checks - Initialising..."));
		vector<bool> validity, new_validity;
		vector<string> results;
		bool sidFails[4]{ 0 };
		bool restFails[4]{ 0 }; // 0 = Suffix, 1 = Aircraft/Engines, 2 = Date/Time Restrictions
		bool warn = false;

		bufLog(callsign + string(" Validate: Checks - SID-Level Restrictions..."));
		//SID-Level Restrictions Array
		sidFails[0] = true;
		vector<bool> temp = checkRestriction(flightPlan, sid_suffix, sid_ele["restrictions"], sidFails, sidFails);
		bool sidwide = false;
		if (temp[0] || temp[1]) {
			sidwide = true;
		}

		bufLog(callsign + string(" Validate: Checks - Initialising Validity List..."));
		//Initialise validity array to fully true#
		for (SizeType i = 0; i < conditions.Size(); i++) {
			validity.push_back(true);
		}
			
		//Run Checks on Constraints Array
		while (round < 7) {
			bufLog(callsign + string(" Validate: Checks - Starting Round ") + to_string(round) + string("..."));
			new_validity = {};

			switch (round) {
			case 0:
			{
				//Destinations
				new_validity = checkDestination(conditions, destination, validity);
				break;
			}
			case 1:
			{
				//Exit Points
				new_validity = checkExitPoint(conditions, points, validity);
				break;
			}
			case 2:
			{
				//Route
				new_validity = checkRoute(conditions, route, validity);
				break;
			}
			case 3:
			{
				//Restrictions Array
				new_validity = checkRestrictions(flightPlan, conditions, sid_suffix, sidFails, restFails, &sidwide, validity);
				break;
			}
			case 4:
			{
				//Min & Max Levels
				new_validity = checkMinMax(conditions, RFL, validity);
				break;
			}
			case 5:
			{
				//Even/Odd Levels
				new_validity = checkDirection(conditions, RFL, validity);
				break;
			}
			case 6:
			{
				//Alerts (Warn/Ban)
				new_validity = checkAlerts(conditions, &warn, validity);
				break;
			}
			}

			if (all_of(new_validity.begin(), new_validity.end(), [](bool v) { return !v; })) {
				bufLog(callsign + string(" Validate: Checks - Failed On Round ") + to_string(round));
				break;
			}
			else {
				bufLog(callsign + string(" Validate: Checks - Round ") + to_string(round) + string(" Complete"));
				validity = new_validity;
				round++;
			}
		}

		bufLog(callsign + string(" Validate: Setting SID/Non-SID Output..."));
		if (sid.length()) {
			returnOut[1][1] = returnOut[0][1] = "SID - " + sid + ".";
		}
		else {
			returnOut[1][1] = returnOut[0][1] = "Non-SID Route.";
		}

		bufLog(callsign + string(" Validate: SID-Wide Restrictions - Checking If Passed..."));
		if (sidwide) {
			bufLog(callsign + string(" Validate: SID-Wide Restrictions - Passed"));
			vector<size_t> successes{};

			for (size_t i = 0; i < validity.size(); i++) {
				if (validity[i]) {
					successes.push_back(i);
				}
			}

			bufLog(callsign + string(" Validate: Result - Setting Type ") + to_string(round) + string("..."));

			//Generate Output
			switch (round) {
			case 7:
			{
				returnOut[1].back() = returnOut[0].back() = "Passed";
				returnOut[1][10] = "No Route Ban.";
			}
			case 6:
			{
				if (warn) {
					returnOut[1][9] = returnOut[0][9] = WarningsOutput(flightPlan, conditions, successes, points, destination, RFL);
				}
				else {
					returnOut[1][9] = "No Warnings.";
				}
        
				if (round == 6) {
					returnOut[1][10] = returnOut[0][10] = BansOutput(flightPlan, conditions, successes, points, destination, RFL);
				}

				returnOut[0][6] = "Passed Odd-Even Rule.";
				returnOut[1][6] = "Passed " + DirectionOutput(flightPlan, conditions, successes);
			}
			case 5:
			{
				if (round == 5) {
					returnOut[1][6] = returnOut[0][6] = "Failed " + DirectionOutput(flightPlan, conditions, successes);
				}

				returnOut[0][5] = "Passed Min/Max Level.";
				returnOut[1][5] = "Passed " + MinMaxOutput(flightPlan, conditions, successes);
			}
			case 4:
			{
				if (round == 4) {
					returnOut[1][5] = returnOut[0][5] = "Failed " + MinMaxOutput(flightPlan, conditions, successes) + " Alternative " + RouteOutput(flightPlan, conditions, successes, points, destination, RFL, true);
				}

				returnOut[0][8] = "Passed SID Restrictions.";
				returnOut[1][8] = "Passed " + RestrictionsOutput(flightPlan, sid_ele, true, true, true, successes);
			}
			case 3:
			{

				returnOut[0][7] = "Valid Suffix.";
				returnOut[1][7] = "Valid " + SuffixOutput(flightPlan, sid_ele, successes);

				if (round == 3) {
					if (restFails[0]) {
						returnOut[1][7] = returnOut[0][7] = "Invalid " + SuffixOutput(flightPlan, sid_ele, successes);
					}
					else {
						returnOut[1][8] = returnOut[0][8] = "Failed " + RestrictionsOutput(flightPlan, sid_ele, restFails[1], restFails[2], restFails[4], successes) + " " + AlternativesOutput(flightPlan, sid_ele, successes);
					}
				}

				returnOut[0][4] = "Passed Route.";
				returnOut[1][4] = "Passed Route. " + RouteOutput(flightPlan, conditions, successes, points, destination, RFL);
			}
			case 2:
			{
				if (round == 2) {
					returnOut[1][4] = returnOut[0][4] = "Failed Route. " + RouteOutput(flightPlan, conditions, successes, points, destination, RFL);
				}

				returnOut[0][3] = "Passed Exit Point.";
				returnOut[1][3] = "Passed " + ExitPointOutput(flightPlan, origin_int, points);
			}
			case 1:
			{
				if (round == 1) {
					returnOut[1][3] = returnOut[0][3] = "Failed " + ExitPointOutput(flightPlan, origin_int, points);
				}

				returnOut[0][2] = "Passed Destination.";
				returnOut[1][2] = "Passed " + DestinationOutput(flightPlan, origin_int, destination);
			}
			case 0:
			{
				if (round == 0) {
					returnOut[1][2] = returnOut[0][2] = "Failed " + DestinationOutput(flightPlan, origin_int, destination);
				}
				break;
			}
			}

			bufLog(callsign + string(" Validate: Result - Type ") + to_string(round) + string(" Set"));
		}
		else {
			bufLog(callsign + string(" Validate: SID-Wide Restrictions - Failed"));
			if (sidFails[0]) {
				bufLog(callsign + string(" Validate: SID-Wide Restrictions - Invalid Suffix"));
				returnOut[1][6] = returnOut[0][7] = "Invalid " + SuffixOutput(flightPlan, sid_ele);
			}
			else {
				bufLog(callsign + string(" Validate: SID-Wide Restrictions - Invalid Type/Date/Time"));
				returnOut[0][6] = "Valid Suffix.";
				returnOut[1][6] = "Valid " + SuffixOutput(flightPlan, sid_ele);

				//sidFails[1], [2], or [3] must be false to get here
				returnOut[1][7] = returnOut[0][7] = "Failed " + RestrictionsOutput(flightPlan, sid_ele, sidFails[1], sidFails[2], sidFails[3]) + " " + AlternativesOutput(flightPlan, sid_ele);
			}
		}

		return returnOut;
	}
}

//Outputs route bans as string
string CVFPCPlugin::BansOutput(CFlightPlan flightPlan, const Value& constraints, vector<size_t> successes, vector<string> extracted_route, string dest, int rfl) {
	bufLog(flightPlan.GetCallsign() + string(" - Generating Bans Output..."));
	vector<string> bans{};
	for (int each : successes) {
		if (constraints[each]["alerts"].IsArray() && constraints[each]["alerts"].Size()) {
			for (size_t i = 0; i < constraints[each]["alerts"].Size(); i++) {
				if (constraints[each]["alerts"][i].HasMember("ban") && constraints[each]["alerts"][i]["ban"].IsBool() && constraints[each]["alerts"][i]["ban"].GetBool()) {
					if (constraints[each]["alerts"][i].HasMember("srd") && constraints[each]["alerts"][i]["srd"].IsInt()) {
						bans.push_back("SRD Note " + to_string(constraints[each]["alerts"][i]["srd"].GetInt()));
					}
					if (constraints[each]["alerts"][i].HasMember("note") && constraints[each]["alerts"][i]["note"].IsString()) {
						bans.push_back(constraints[each]["alerts"][i]["note"].GetString());
					}
					else {
						bans.push_back("Alternative Route: " + RouteOutput(flightPlan, constraints, successes, extracted_route, dest, rfl));
					}
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
string CVFPCPlugin::WarningsOutput(CFlightPlan flightPlan, const Value& constraints, vector<size_t> successes, vector<string> extracted_route, string dest, int rfl) {
	bufLog(flightPlan.GetCallsign() + string(" - Generating Warnings Output..."));
	vector<string> warnings{};
	for (int each : successes) {
		if (constraints[each]["alerts"].IsArray() && constraints[each]["alerts"].Size()) {
			for (size_t i = 0; i < constraints[each]["alerts"].Size(); i++) {
				if (constraints[each]["alerts"][i].HasMember("warn") && constraints[each]["alerts"][i]["warn"].IsBool() && constraints[each]["alerts"][i]["warn"].GetBool()) {
					if (constraints[each]["alerts"][i].HasMember("srd") && constraints[each]["alerts"][i]["srd"].IsInt()) {
						warnings.push_back("SRD Note " + to_string(constraints[each]["alerts"][i]["srd"].GetInt()	));
					}
					if (constraints[each]["alerts"][i].HasMember("note") && constraints[each]["alerts"][i]["note"].IsString()) {
						warnings.push_back(constraints[each]["alerts"][i]["note"].GetString());
					}
					else {
						warnings.push_back("Alternative Route: " + RouteOutput(flightPlan, constraints, successes, extracted_route, dest, rfl));
					}
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
string CVFPCPlugin::AlternativesOutput(CFlightPlan flightPlan, const Value& sid_ele, vector<size_t> successes) {
	bufLog(flightPlan.GetCallsign() + string(" - Generating Alternatives Output..."));
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
string CVFPCPlugin::RestrictionsOutput(CFlightPlan flightPlan, const Value& sid_ele, bool check_type, bool check_time, bool check_ban, vector<size_t> successes) {
	bufLog(flightPlan.GetCallsign() + string(" - Generating Restrictions Output..."));
	vector<vector<string>> rests{};
	const Value& constraints = sid_ele["constraints"];

	vector<vector<string>> temp = RestrictionsSingle(sid_ele["restrictions"]);
	rests.insert(rests.end(), temp.begin(), temp.end());

	for (size_t each : successes) {
		temp = RestrictionsSingle(constraints[each]["restrictions"]);
		rests.insert(rests.end(), temp.begin(), temp.end());
	}

	sort(rests.begin(), rests.end());
	vector<vector<string>>::iterator itr = unique(rests.begin(), rests.end());
	rests.erase(itr, rests.end());

	string out = "";
	for (size_t i = 0; i < rests.size(); i++) {
		string temp = "";
		if (check_ban) {
			temp += "Banned";
		}

		if (check_type && check_time) {
			if (temp.size() > 0) {
				temp += " for ";
			}

			temp += rests[i][0] + " Between " + rests[i][1] + ROUTE_RESULT_SEP;
		}
		else if (check_type) {
			if (temp.size() > 0) {
				temp += " for ";
			}

			temp += rests[i][0] + RESULT_SEP;
		}
		else if (check_time) {
			if (temp.size() > 0) {
				temp += " b";
			}
			else {
				temp += "B";
			}

			temp += "etween " + rests[i][1] + ROUTE_RESULT_SEP;
		}

		out += temp;
	}

	if (out == "") {
		out = NO_RESULTS;
	}
	else if (check_time) {
		out = out.substr(0, out.size() - 3);
	}
	else {
		out = out.substr(0, out.size() - 2);
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
string CVFPCPlugin::SuffixOutput(CFlightPlan flightPlan, const Value& sid_eles, vector<size_t> successes) {
	bufLog(flightPlan.GetCallsign() + string(" - Generating Suffices Output..."));
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
string CVFPCPlugin::DirectionOutput(CFlightPlan flightPlan, const Value& constraints, vector<size_t> successes) {
	bufLog(flightPlan.GetCallsign() + string(" - Generating Odd-Even Rule Output..."));
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
string CVFPCPlugin::MinMaxOutput(CFlightPlan flightPlan, const Value& constraints, vector<size_t> successes) {
	bufLog(flightPlan.GetCallsign() + string(" - Generating Min/Max Levels Output..."));
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
string CVFPCPlugin::RouteOutput(CFlightPlan flightPlan, const Value& constraints, vector<size_t> successes, vector<string> extracted_route, string dest, int rfl, bool req_lvl) {
	bufLog(flightPlan.GetCallsign() + string(" - Generating Route Output..."));
	vector<size_t> pos{};
	bool lvls = false;

	for (size_t i = 0; i < constraints.Size(); i++) {
		pos.push_back(i);
	}

	size_t i = 0;
	while (i < 7) {
		vector<size_t> newpos{};
		for (size_t j : pos) {
			switch (i) {
			//Exact dest match
			case 0: {
				bool res = false;

				if (constraints[j]["dests"].IsArray() && constraints[j]["dests"].Size()) {
					for (size_t k = 0; k < constraints[j]["dests"].Size(); k++) {
						if (constraints[j]["dests"][k].IsString()) {
							if (string(constraints[j]["dests"][k].GetString()).size() == 4 && !strcmp(constraints[j]["dests"][k].GetString(), dest.c_str())) {
								res = true;
							}
						}
					}
				}

				if (constraints[j]["nodests"].IsArray() && constraints[j]["nodests"].Size()) {
					for (size_t k = 0; k < constraints[j]["nodests"].Size(); k++) {
						if (constraints[j]["nodests"][k].IsString()) {
							if (startsWith(constraints[j]["nodests"][k].GetString(), dest.c_str())) {
								res = false;
							}
						}
					}
				}

				if ((constraints[j]["points"].IsArray() && constraints[j]["points"].Size()) || (constraints[j]["nopoints"].IsArray() && constraints[j]["nopoints"].Size())) {
					res = false;
				}


				if (res) {
					newpos.push_back(j);
				}
				break;
			}
			//Any dest/nodest match
			case 1: {
				bool res = false;

				if (constraints[j]["dests"].IsArray() && constraints[j]["dests"].Size()) {
					for (size_t k = 0; k < constraints[j]["dests"].Size(); k++) {
						if (constraints[j]["dests"][k].IsString()) {
							if (startsWith(constraints[j]["dests"][k].GetString(), dest.c_str())) {
								res = true;
							}
						}
					}
				}
				else {
					res = true;
				}

				if (res) {
					newpos.push_back(j);
				}
				break;
			}
			case 2: {
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
			//points/nopoints match
			case 3: {
				bool res = false;

				if (constraints[j]["points"].IsArray() && constraints[j]["points"].Size()) {
					for (size_t k = 0; k < extracted_route.size(); k++) {
						if (arrayContains(constraints[j]["points"], extracted_route[k])) {
							res = true;
						}
					}
				}
				else {
					res = true;
				}

				if (res) {
					newpos.push_back(j);
				}
				break;
			}
			case 4: {
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
			//Levels match
			case 5: {
				bool res = true;

				if (constraints[j].HasMember("min") && (!constraints[j]["min"].IsInt() || constraints[j]["min"].GetInt() > rfl / 100)) {
					res = false;
				}

				if (constraints[j].HasMember("max") && (!constraints[j]["max"].IsInt() || constraints[j]["max"].GetInt() < rfl / 100)) {
					res = false;
				}

				if (res) {
					newpos.push_back(j);
					lvls = true;
				}
				break;
			}
			//Remove anything banned
			case 6: {
				bool res = true;

				if (constraints[j]["alerts"].IsArray() && constraints[j]["alerts"].Size()) {
					for (size_t k = 0; k < constraints[j]["alerts"].Size(); k++) {
						if (constraints[j]["alerts"][k].HasMember("ban") && constraints[j]["alerts"][k]["ban"].IsBool() && constraints[j]["alerts"][k]["ban"].GetBool()) {
							res = false;
						}
					}
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

			if (i == 0) {
				i = 4;
			}
		}

		i++;
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

	if (pos.size() == 0 || (req_lvl && !lvls)) {
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

//Outputs valid FIR exit points (from Constraints array) as string
string CVFPCPlugin::ExitPointOutput(CFlightPlan flightPlan, size_t origin_int, vector<string> points) {
	bufLog(flightPlan.GetCallsign() + string(" - Generating Exit Point Output..."));
	map<string, vector<string>> a{}; //Key = Exit Point, Value = Explicitly Permitted SIDs
	vector<bool> b{}; //Implicitly Permitted SIDs (Not Explicitly Prohibited)

	for (size_t i = 0; i < config[origin_int]["sids"].Size(); i++) {
		b.push_back(false);

		if (config[origin_int]["sids"][i].HasMember("point") && config[origin_int]["sids"][i]["point"].IsString()) {
			const Value& conditions = config[origin_int]["sids"][i]["constraints"];
			for (size_t j = 0; j < conditions.Size(); j++) {
				if (conditions[j]["points"].IsArray() && conditions[j]["points"].Size()) {
					for (string each : points) {
						if (arrayContains(conditions[j]["points"], each)) {
							if (a.find(each) == a.end()) {
								vector<string> temp{ config[origin_int]["sids"][i]["point"].GetString() };
								a.insert(pair<string, vector<string>>(each, temp));
							}
							else {
								a[each].push_back(config[origin_int]["sids"][i]["point"].GetString());
							}
						}
					}
				}
				else if (conditions[j]["nopoints"].IsArray() && conditions[j]["nopoints"].Size()) {
					b[i] = true;
					for (string each : points) {
						if (arrayContains(conditions[i]["nopoints"], each)) {
							b[i] = false;
						}
					}
				}
			}
		}
	}

	vector<string> out = {};

	if (a.size()) {
		for (pair<string, vector<string>> exit : a) {
			string single = "";

			for (string each : exit.second) {
				if (each == "") {
					single += "No SID";
				}
				else {
					single += each;
				}

				single += RESULT_SEP;
			}

			if (single.size() > 0) {
				single = single.substr(0, single.size() - RESULT_SEP.size());
			}
			else {
				single = "None";
			}

			string prefix = exit.first;
			prefix += " is valid for: ";

			out.push_back(prefix + single);
		}
	}

	if (!all_of(b.begin(), b.end(), [](bool v) { return !v; })) {
		string single = "";

		for (size_t i = 0; i < b.size(); i++) {
			if (b[i]) {
				string temp = config[origin_int]["sids"][i]["point"].GetString();

				if (temp == "") {
					single += "No SID";
				}
				else {
					single += temp;
				}
				single += RESULT_SEP;
			}
		}

		if (single.size() > 0) {
			single = single.substr(0, single.size() - RESULT_SEP.size());
		}
		else {
			single = "None";
		}

		string prefix = "";
		if (out.size()) {
			prefix += "Additionally, t";
		}
		else {
			prefix += "T";
		}

		prefix += "he following SIDs may perhaps be valid: ";

		out.push_back(prefix + single);
	}

	string outstring = "";
	for (string each : out) {
		outstring += each + ". ";
	}

	if (!outstring.size()) {
		outstring = "Not Found. ";
	}

	return "Exit Point. " + outstring.substr(0, outstring.size() - 1);
}

//Outputs valid destinations (from Constraints array) as string
string CVFPCPlugin::DestinationOutput(CFlightPlan flightPlan, size_t origin_int, string dest) {
	bufLog(flightPlan.GetCallsign() + string(" - Generating Destination Output..."));
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
	bufLog("Tag Function Called");
	try {
		if (FunctionId == TAG_FUNC_CHECKFP_MENU) {
			bufLog("Tag Function Called - Display Options Menu");
			OpenPopupList(Area, "Options", 1);
			AddPopupListElement("Show Checks", "", TAG_FUNC_CHECKFP_CHECK);
			AddPopupListElement("Toggle Checks", "", TAG_FUNC_CHECKFP_DISMISS);
		}
		else if (FunctionId == TAG_FUNC_CHECKFP_CHECK) {
			bufLog("Tag Function Called - Show Checks");
			checkFPDetail();
		}
		else if (FunctionId == TAG_FUNC_CHECKFP_DISMISS) {
			bufLog("Tag Function Called - Toggle Checks");
			CFlightPlan flightPlan = FlightPlanSelectASEL();

			if (Enabled(flightPlan)) {
				bufLog(flightPlan.GetCallsign() + string(": Disabling Checks..."));
				flightPlan.GetControllerAssignedData().SetFlightStripAnnotation(0, "VFPC/OFF");
				bufLog(flightPlan.GetCallsign() + string(": Disabled Checks"));
			}
			else {
				bufLog(flightPlan.GetCallsign() + string(": Enabling Checks..."));
				flightPlan.GetControllerAssignedData().SetFlightStripAnnotation(0, "");
				bufLog(flightPlan.GetCallsign() + string(": Enabled Checks"));
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
		bufLog(flightPlan.GetCallsign() + string(" Toggle: Checking..."));
		string cad = flightPlan.GetControllerAssignedData().GetFlightStripAnnotation(0);
		bufLog(flightPlan.GetCallsign() + string(" Toggle: Got Annotation"));
		if (!strcmp(cad.c_str(), "VFPC/OFF")) {
			bufLog(flightPlan.GetCallsign() + string(" Toggle: Disabled"));
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

	bufLog(flightPlan.GetCallsign() + string(" Toggle: Enabled"));
	return true;
}

//Gets flight plan, checks if (S/D)VFR, calls checking algorithms, and outputs pass/fail result to departure list item
void CVFPCPlugin::OnGetTagItem(CFlightPlan flightPlan, CRadarTarget RadarTarget, int ItemCode, int TagData, char sItemString[16], int* pColorCode, COLORREF* pRGB, double* pFontSize){
	try {
		if (ItemCode == TAG_ITEM_CHECKFP) {
			bufLog(flightPlan.GetCallsign() + string(": Tag Item - Cycled With Flight Plan Check Code"));
			const char *origin = flightPlan.GetFlightPlanData().GetOrigin();
			if (find(activeAirports.begin(), activeAirports.end(), origin) == activeAirports.end()) {
				bufLog("Adding " + string(origin) + " To Active Airports List");
				activeAirports.push_back(origin);
			}

			if (validVersion && Enabled(flightPlan) && airports.find(flightPlan.GetFlightPlanData().GetOrigin()) != airports.end()) {
				bufLog(flightPlan.GetCallsign() + string(": Tag Item - Check Enabled & Airport Found"));
				string FlightPlanString = flightPlan.GetFlightPlanData().GetRoute();
				int RFL = flightPlan.GetFlightPlanData().GetFinalAltitude();
				bufLog(flightPlan.GetCallsign() + string(": Tag Item - Got Flight Plan Data"));

				*pColorCode = TAG_COLOR_RGB_DEFINED;
				string fpType{ flightPlan.GetFlightPlanData().GetPlanType() };
				if (fpType == "V" || fpType == "S" || fpType == "D") {
					bufLog(flightPlan.GetCallsign() + string(": Tag Item - Not IFR, Check Aborted"));
					*pRGB = TAG_GREEN;
					strcpy_s(sItemString, 16, "VFR");
				}
				else {
					bufLog(flightPlan.GetCallsign() + string(": Tag Item - IFR, Launching Check..."));
					vector<string> validize = validateSid(flightPlan)[0]; // 0 = Callsign, 1 = SID, 2 = Destination, 3 = Exit Point, 4 = Route, 5 = Min/Max Flight Level, 6 = Even/Odd, 7 = Suffix, 8 = Restrictions, 9 = Warnings, 10 = Bans, 11 = Syntax, 12 = Passed/Failed

					bufLog(flightPlan.GetCallsign() + string(": Tag Item - Check Complete, Setting Tag Alert..."));
					strcpy_s(sItemString, 16, getFails(flightPlan, validize, pRGB).c_str());
				}
			}
			else {
				bufLog(flightPlan.GetCallsign() + string(": Tag Item - Cycled With Unknown Code"));
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
	bufLog("Command Detected");
	try {
		//Restart Automatic Data Loading
		if (startsWith((COMMAND_PREFIX + LOAD_COMMAND).c_str(), sCommandLine))
		{
			bufLog("Command Detected: Load");
			if (autoLoad) {
				sendMessage("Auto-Load Already Active.");
				debugMessage("Warning", "Auto-load activation attempted whilst already active.");
				bufLog("Command: Load - Already Loading From API");
			}
			else {
				fileLoad = false;
				autoLoad = true;
				relCount = 0;
				sendMessage("Auto-Load Activated.");
				debugMessage("Info", "Auto-load reactivated.");
				bufLog("Command: Load - Activated Loading From API");
			}
			return true;
		}
		//Disable API and load from Sid.json file
		else if (startsWith((COMMAND_PREFIX + FILE_COMMAND).c_str(), sCommandLine))
		{
			bufLog("Command Detected: File");
			autoLoad = false;
			fileLoad = true;
			sendMessage("Attempting to load from " + DATA_FILE + " file.");
			debugMessage("Info", "Will now load from " + DATA_FILE + " file.");
			getSids();
			return true;
		}
		//Activate Debug Logging
		else if (startsWith((COMMAND_PREFIX + LOG_COMMAND).c_str(), sCommandLine)) {
			bufLog("Command Detected: Log");
			if (debugMode) {
				debugMessage("Info", "Logging mode deactivated.");
				debugMode = false;
				bufLog("Command: Log - Deactivated Debug Mode");
			}
			else {
				debugMode = true;
				debugMessage("Info", "Logging mode activated.");
				bufLog("Command: Log - Activated Debug Mode");
			}
			return true;
		}
		//Text-Equivalent of "Show Checks" Button
		else if (startsWith((COMMAND_PREFIX + CHECK_COMMAND).c_str(), sCommandLine))
		{
			bufLog("Command Detected: Check");
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
			CFlightPlan flightPlan = FlightPlanSelectASEL();
			bufLog(flightPlan.GetCallsign() + string(": Show Checks - Requested"));
			string fpType{ flightPlan.GetFlightPlanData().GetPlanType() };
			if (fpType == "V" || fpType == "S" || fpType == "D") {
				bufLog(flightPlan.GetCallsign() + string(": Show Checks - Not IFR, Check Aborted"));
				string buf = "Flight Plan Checking Not Supported For VFR Flights.";
				sendMessage(flightPlan.GetCallsign(), buf);
				debugMessage(flightPlan.GetCallsign(), buf);
			}
			else {
				bufLog(flightPlan.GetCallsign() + string(": Show Checks - IFR, Launching Check..."));
				sendMessage(flightPlan.GetCallsign(), "Checking...");
				vector<vector<string>> validize = validateSid(flightPlan);

				bufLog(flightPlan.GetCallsign() + string(": Show Checks - Check Complete, Preparing Output..."));
				vector<string> messageBuffer{ validize[0] }; // 0 = Callsign, 1 = SID, 2 = Destination, 3 = Exit Point, 4 = Route, 5 = Min/Max Flight Level, 6 = Even/Odd, 7 = Suffix, 8 = Restrictions, 9 = Warnings, 10 = Bans, 11 = Syntax, 12 = Passed/Failed
				vector<string> logBuffer{ validize[1] }; // 0 = Callsign, 1 = SID, 2 = Destination, 3 = Exit Point, 4 = Route, 5 = Min/Max Flight Level, 6 = Even/Odd, 7 = Suffix, 8 = Restrictions, 9 = Warnings, 10 = Bans, 11 = Syntax, 12 = Passed/Failed
				
				string buffer{};
				string logbuf{};

				if (messageBuffer.at(1).find("Invalid") != 0) {
					for (size_t i = 1; i < messageBuffer.size() - 1; i++) {
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

				bufLog(flightPlan.GetCallsign() + string(": Show Checks - Sending Output..."));
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
string CVFPCPlugin::getFails(CFlightPlan flightPlan, vector<string> messageBuffer, COLORREF* pRGB) {
	bufLog(flightPlan.GetCallsign() + string(": Getting Fails"));
	try {
		*pRGB = TAG_RED;

		if (messageBuffer.at(messageBuffer.size() - 2).size() > 1 || messageBuffer.at(messageBuffer.size() - 2).find("-")) {
			bufLog(flightPlan.GetCallsign() + string(": Fail - CHK"));
			return "CHK";
		}
		else if (messageBuffer.at(1).find("SID - ") && messageBuffer.at(1).find("Non-SID Route.")) {
			bufLog(flightPlan.GetCallsign() + string(": Fail - SID"));
			return "SID";
		}
		else if (!messageBuffer.at(2).find("Failed")) {
			bufLog(flightPlan.GetCallsign() + string(": Fail - DST"));
			return "DST";
		}
		else if (!messageBuffer.at(3).find("Failed")) {
			bufLog(flightPlan.GetCallsign() + string(": Fail - XPT"));
			return "XPT";
		}
		else if (!messageBuffer.at(4).find("Failed")) {
			bufLog(flightPlan.GetCallsign() + string(": Fail - RTE"));
			return "RTE";
		}
		else if (!messageBuffer.at(5).find("Failed")) {
			bufLog(flightPlan.GetCallsign() + string(": Fail - LVL"));
			return "LVL";
		}
		else if (!messageBuffer.at(6).find("Failed")) {
			bufLog(flightPlan.GetCallsign() + string(": Fail - OER"));
			return "OER";
		}
		else if (!messageBuffer.at(7).find("Invalid")) {
			bufLog(flightPlan.GetCallsign() + string(": Fail - SUF"));
			return "SUF";
		}
		else if (!messageBuffer.at(8).find("Failed")) {
			bufLog(flightPlan.GetCallsign() + string(": Fail - RST"));
			return "RST";
		}
		else if (!messageBuffer.at(9).find("Warnings")) {
			bufLog(flightPlan.GetCallsign() + string(": Fail - Warning"));
			*pRGB = TAG_ORANGE;
		}
		else if (!messageBuffer.at(10).find("Route Banned")) {
			bufLog(flightPlan.GetCallsign() + string(": Fail - BAN"));
			return "BAN";
		}
		else {
			bufLog(flightPlan.GetCallsign() + string(": Fail - Success"));
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
		bufLog("Calls Manager: Launching Version Call...");
		validVersion = versionCall();
		bufLog("Calls Manager: Compiling SIDs...");
		getSids();
		bufLog("Calls Manager: Complete");
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
	bufLog("Timer: Triggered");
	try {
		if (validVersion) {
			bufLog("Timer: Running Jobs...");
			if (relCount == -1 && fut.valid() && fut.wait_for(1ms) == std::future_status::ready) {
				bufLog("Timer: API Calls Returned - Resetting...");
				fut.get();
				bufLog("Timer: API Calls Returned - Future Reset");
				loadedAirports = activeAirports;
				bufLog("Timer: API Calls Returned - Loaded Airports Stored");
				activeAirports.clear();
				bufLog("Timer: API Calls Returned - Active Airports Cleared");
				relCount = API_REFRESH_TIME;
				bufLog("Timer: API Calls Returned - Counter Reset");
			}

			if (relCount > 0) {
				bufLog("Timer: Decrementing Counter...");
				relCount--;
				bufLog("Timer: Counter Decremented");
			}

			// Loading proper Sids, when logged in
			if (GetConnectionType() != CONNECTION_TYPE_NO && relCount == 0) {
				bufLog("Timer: Connection Live - Launching API Calls...");
				fut = std::async(std::launch::async, &CVFPCPlugin::runWebCalls, this);
				bufLog("Timer: API Calls Launched - Disabling Counter");
				relCount--;
				bufLog("Timer: API Calls Launched - Counter Disabled");
			}
			else if (GetConnectionType() == CONNECTION_TYPE_NO) {
				bufLog("Timer: Connection Closed - Clearing Collections...");
				airports.clear();
				bufLog("Timer: Connection Closed - Airports Cleared");
				config.Clear();
				bufLog("Timer: Connection Closed - JSON Cleared");
			}

			writeLog();
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