//==============================================================
// 1. Includes / globals / file-local state
//==============================================================
#include "stdafx.h"
#include "analyzeFP.hpp"
#include "JsonConfigLoader.h"
#include "PluginConfig.h"
#include "Utils.h"
#include "Log.h"
#include "UrlData.h"
#include "VersionInfo.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

using namespace std;
using namespace EuroScopePlugIn;
using namespace std::chrono_literals; // Add this line to enable chrono literals

//==============================================================
// 2. File-local utility helpers
//==============================================================
namespace
{
	bool debugMode = false;
	bool autoLoad = false;
	bool fileLoad = false;
	bool apiUpdated = false;

	bool is_initialised = false;

	vector<int> timedata;
	vector<int> lastupdate;
	vector<string> logBuffer{};

	size_t failPos = 0;
	int relCount = 0;

	std::future<void> fut;

	COLORREF TAG_RED = 0;
	COLORREF TAG_GREEN = 0;
	COLORREF TAG_YELLOW = 0;

	PluginConfig plugin_config;

	/***********************************************************
	* The following are used with the state machine to track the 
	* state of the plugin's connection to the API and whether 
	* it has a callsign ready to check flight plans for.
	***********************************************************/
	enum class SessionState {
		Disconnected,
		Connected_NoCallsign,
		Connected_CallsignReady
	};

	SessionState session_state_{ SessionState::Disconnected };

	struct CurlStopCtx {
		std::stop_token st;
	}; 

	static int clamp255(int v) { 
		return (v < 0) ? 0 : (v > 255 ? 255 : v);
	}


	/***********************************************************
	* Utility function used for debugging purposes to convert a 
	* RapidJSON Document to a JSON string.
	***********************************************************/
	static inline std::string ToJsonString(const rapidjson::Document& doc)
	{
		rapidjson::StringBuffer buffer;
		rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
		doc.Accept(writer);
		return buffer.GetString();
	}

	static int CurlXferInfo(void* clientp,
		curl_off_t /*dltotal*/, curl_off_t /*dlnow*/,
		curl_off_t /*ultotal*/, curl_off_t /*ulnow*/)
	{
		auto* ctx = static_cast<CurlStopCtx*>(clientp);
		return ctx->st.stop_requested() ? 1 : 0; // non-zero aborts transfer
	}

	//Stores output of HTTP request in string
	static size_t curlCallback(void* contents, size_t size, size_t nmemb, void* outString)
	{
		static_cast<std::string*>(outString)->append(
			reinterpret_cast<char*>(contents), size * nmemb);
		return size * nmemb;
	}

	//Gets path to current directory
	static std::string GetPath() {
		char DllPathFile[_MAX_PATH];
		GetModuleFileNameA(HINSTANCE(&__ImageBase), DllPathFile, sizeof(DllPathFile));
		std::string path = DllPathFile;
		path.resize(path.size() - strlen(PLUGIN_FILE.c_str()));

		return path;
	}
} // end namespace


//==============================================================
// 3. Construction / destruction / basic console helpers
//==============================================================
CVFPCPlugin::CVFPCPlugin(void) :CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE, 
										VFPC_PLUGIN_NAME, 
										VFPC_VERSION_STR, 
										VFPC_COMPANY_NAME,
										VFPC_COPYRIGHT_TEXT)
{

	debugMode = false;
	validVersion = true; //Reset in first timer call
	autoLoad = true;
	fileLoad = false;

	// Reset counters
	failPos = 0;
	relCount = 0;

	timedata = { 0, 0, 0, 0, 0, 0 }; // 0 = Year, 1 = Month, 2 = Day, 3 = Hour, 4 = Minute, 5 = Day of Week
	lastupdate = { 0, 0, 0, 0, 0 }; // 0 = Year, 1 = Month, 2 = Day, 3 = Hour, 4 = Minute

	// Register TAG and Function Menu.
	RegisterTagItemType("VFPC", TAG_ITEM_CHECKFP);
	RegisterTagItemFunction("Options", TAG_FUNC_CHECKFP_MENU);

	vector<string> installed = split(VFPC_VERSION_STR, '.');

	thisVersion = (int*)calloc(installed.size(), sizeof(int));
	for (size_t i = 0; i < installed.size(); i++) {
		thisVersion[i] = stoi(installed[i]);
	}

	std::string path = GetPath();

	if (!InitialiseConfig(path + kConfigFileName, plugin_config)) {
		SendToConsole(vfpc::urgent, "Configuration error, see log for details.");
		return;
	}

	if (!InitializeLogging(plugin_config)) {
		SendToConsole(vfpc::urgent, "Error initializing logging, logging disabled.");
		return;
	}
	// Start async version check (non-blocking)
	StartVersionCheckAsync();

	SendToConsole(vfpc::urgent, "Plugin Version: {} Loaded.", VFPC_VERSION_STR);
	is_initialised = true;

	return;
}

CVFPCPlugin::~CVFPCPlugin()
{
	LOG_INFO("Stopping version thread ....");
	if (version_thread_.joinable())
		version_thread_.request_stop();

	LOG_INFO("Stopping airport worker thread ....");
	if (airport_thread_.joinable())
		airport_thread_.request_stop();

	// Release published snapshot before shutdown (helps DLL unload determinism)
	airport_data_.store(nullptr, std::memory_order_release);

	LOG_INFO("Plugin logging shutting down ....");
	spdlog::shutdown();
}

void CVFPCPlugin::SendToConsole(const char* msg, bool urgent)
{
	DisplayUserMessage(
		"VFPC",
		VFPC_LOGGER_NAME,
		msg,
		true, true,
		urgent, urgent,
		false);
}


//==============================================================
// 4. HTTP / JSON / file loading
//==============================================================
bool CVFPCPlugin::webCall(string url, string& out, std::stop_token st) {
	out.clear();

	LOG_TRACE("Web Call: Initiating call to {}", url);

	CURL* curl = curl_easy_init();
	if (!curl) {
		LOG_ERROR("Web Call: curl_easy_init() failed");
		return false;
	}

	char errbuf[CURL_ERROR_SIZE]{};
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

	// Timeouts: split connect vs total
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

	// Windows/thread friendliness
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

	// Follow redirects (common with https / CDNs)
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);

	// Write response
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlCallback);

	// Optional but good practice: identify yourself
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "VFPC/3.x (EuroScope)");

	// Cancellation hook
	CurlStopCtx stopCtx{ st };
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlXferInfo);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &stopCtx);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

	// Perform
	CURLcode result = curl_easy_perform(curl);

	long httpCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

	char* contentType = nullptr;
	curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &contentType);
	std::string contentTypeStr = contentType ? contentType : "(null)";

	curl_easy_cleanup(curl);

	LOG_TRACE("Web Call: HTTP {} Content-Type={} bytes={}",
		httpCode, contentTypeStr, out.size());

	if (st.stop_requested()) {
		LOG_INFO("Web Call: Cancelled call to {}", url);
		return false;
	}

	if (result != CURLE_OK) {
		std::string detail = (errbuf[0] != '\0') ? errbuf : curl_easy_strerror(result);
		LOG_ERROR("Web Call: Call To {} Failed - CURL Error: {} (HTTP Code: {})",
			url, detail, httpCode);
		return false;
	}

	// Treat non-2xx as failure (your choice; but usually correct for JSON fetch)
	if (httpCode < 200 || httpCode >= 300) {
		LOG_DEBUG("Web Call: Call To {} HTTP: {} Body prefix: {}", url, std::to_string(httpCode), out.substr(0, 200));
		return false;
	}

	return true;
}

bool CVFPCPlugin::APICall(const string& base_url, const string& endpoint, std::stop_token st, Document& out) {
	string url = base_url + endpoint;
	string buf;

	// Always reset to a safe value first
	out.SetArray();

	LOG_TRACE("API Call: Calling url: {} endpoint {}...", url, endpoint);

	if (!webCall(url, buf, st))
	{
		SendToConsole("An error occurred whilst downloading data.The plugin has been disabled.");
		SendToConsole(vfpc::urgent, "Please restart EuroScope to try again. (Note:.vfpc load will NOT work.");
		LOG_ERROR("API Call to{} : Failed - No Data Returned", url);
		return false;
	}
	if (st.stop_requested()) {
		LOG_TRACE("API Call To {}: Cancelled before JSON parsing", url);
		return false;
	}

	LOG_TRACE("API Call To {}: Data Retrieved ({} bytes), Parsing JSON...", url, buf.size());
	out.Parse(buf.data(), buf.size());

	if (out.HasParseError()) {
		SendToConsole(vfpc::urgent,
			"An error occurred whilst reading data. The plugin will not automatically attempt to reload from the API. "
			"To restart data fetching, type \".vfpc load\".");

		LOG_ERROR("API Call To {}: JSON parse error: {} (Offset: {})",
			url,
			rapidjson::GetParseError_En(out.GetParseError()),
			out.GetErrorOffset());

		LOG_DEBUG("API Call To {}: Body prefix: {}", url, buf.substr(0, 200));

		// Ensure 'out' is safe even after a parse failure
		out.SetArray();
		return false;
	}

	LOG_TRACE("API Call: JSON parsed successfully (root type={})", (int)out.GetType());
	return true;
}

bool CVFPCPlugin::fileCall(Document& out) {
	string path = GetPath();
	path += DATA_FILE;

	LOG_TRACE("Opening File: {}", DATA_FILE);
	stringstream ss;
	ifstream ifs;
	ifs.open(path.c_str(), ios::binary);

	if (ifs.is_open()) {
		ss << ifs.rdbuf();
		ifs.close();

		if (out.Parse<0>(ss.str().c_str()).HasParseError()) {
			SendToConsole("An error occurred whilst parsing data. The plugin will not automatically attempt to reload.");
			SendToConsole("To restart data fetching from the API, type '{}{}'", COMMAND_PREFIX, LOAD_COMMAND);
			SendToConsole(vfpc::urgent, "To load data from sid.json, type '{}{}'", COMMAND_PREFIX, FILE_COMMAND);
			LOG_DEBUG("Config Parse: {} (Offset: {})\n'", rapidjson::GetParseError_En(out.GetParseError()), out.GetErrorOffset());
			LOG_ERROR("File Data Parse Failed - Data Found But Unreadable");

			out.Parse<0>("[]");
			return false;
		}
	}
	else {
		SendToConsole(vfpc::urgent, "To restart data fetching from the API, type '{}{}'", COMMAND_PREFIX, LOAD_COMMAND);
		LOG_DEBUG("File not found {}", DATA_FILE);

		out.Parse<0>("[]");
		return false;
	}
	return true;
}


//==============================================================
// 5. Version-check worker
//==============================================================
void CVFPCPlugin::StartVersionCheckAsync() {

	LOG_TRACE("Starting asynchronous version check...");
	// Reset state
	version_checked.store(false, std::memory_order_release);
	validVersion.store(false, std::memory_order_release);

	// Snapshot config so worker doesn’t read mutable members
	const PluginConfig cfg = plugin_config;

	// Start jthread; stop_token is provided automatically
	version_thread_ = std::jthread([this, cfg](std::stop_token st) mutable {
		LOG_INFO("Version thread: entered worker");
		const bool ok = VersionCall_Worker(cfg, st);
		LOG_INFO("Version thread: worker returned {}", ok);
		validVersion.store(ok, std::memory_order_release);
		version_checked.store(true, std::memory_order_release);
		});
}

bool CVFPCPlugin::VersionCall_Worker(PluginConfig cfg, std::stop_token st)
{
	Document version;

	LOG_TRACE("VersionCall_Worker_: running. stop_requested={}", st.stop_requested());
	if (!APICall(cfg.base_url, cfg.version_endpoint, st, version)) return false;
	if (st.stop_requested()) return false;

	bool out = false;

	if (version.HasMember("vfpc_version") && version["vfpc_version"].IsString() && version.HasMember("min_version") && version["min_version"].IsString()) {
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
			LOG_ERROR("Version Call: Discontinued Version In Use.  Please update plugin.");
			if (minchange) {
				SendToConsole(vfpc::urgent, "Plugin Update required - the plugin has been disabled.");
			}
		}
		else if (curVersion[0] > thisVersion[0] || (curVersion[0] == thisVersion[0] && curVersion[1] > thisVersion[1]) || (curVersion[0] == thisVersion[0] && curVersion[1] == thisVersion[1] && curVersion[2] > thisVersion[2])) {
			LOG_INFO("Version Call: Outdated Version In Use");
			if (curchange) {
				SendToConsole("Update available - you may continue using the plugin, but please update as soon as possible.");
			}
			out = true;
		}
		else {
			LOG_TRACE("Version Call: No New Version Since Last Check");
			out = true;
		}
	}
	else {
		LOG_ERROR("Version Call: Version Data Not Found");
		SendToConsole(vfpc::urgent, "Failed to check for updates - the plugin has been disabled.");
	}

	bool updatefail = false;
	vector<int> newdate = { 0, 0, 0 };

	if (version.HasMember("date") && version["date"].IsString() && version.HasMember("last_updated_date") && version["last_updated_date"].IsString() && version.HasMember("last_updated_time") && version["last_updated_time"].IsString()) {
		string lastdate = version["last_updated_date"].GetString();
		if (lastdate.size() == 10) {
			try {
				int lastday = stoi(lastdate.substr(0, 2));
				int lastmonth = stoi(lastdate.substr(3, 2));
				int lastyear = stoi(lastdate.substr(6, 4));

				lastupdate[0] = lastyear;
				lastupdate[1] = lastmonth;
				lastupdate[2] = lastday;
			}
			catch (...) {
				LOG_ERROR("Version Call: Last Updated Date Data Unreadable - String->Int Failed");
				updatefail = true;
			}
		}
		else {
			LOG_ERROR("Version Call: Last Updated Date Data Unreadable - Wrong Size");
			updatefail = true;
		}

		string lasttime = version["last_updated_time"].GetString();
		if (lasttime.size() == 8) {
			try {
				int lasthour = stoi(lasttime.substr(0, 2));
				int lastmins = stoi(lasttime.substr(3, 2));

				lastupdate[3] = lasthour;
				lastupdate[4] = lastmins;
			}
			catch (...) {
				LOG_ERROR("Version Call: Last Updated Time Data Unreadable - String->Int Failed");
				updatefail = true;
			}
		}
		else {
			LOG_ERROR("Version Call: Last Updated Time Data Unreadable - Wrong Size");
			updatefail = true;
		}

		string date = version["date"].GetString();
		if (date.size() == 10) {
			try {
				int day = stoi(date.substr(0, 2));
				int month = stoi(date.substr(3, 2));
				int year = stoi(date.substr(6, 4));

				newdate[0] = year;
				newdate[1] = month;
				newdate[2] = day;
			}
			catch (...) {
				LOG_ERROR("Version Call: Date Data Unreadable - String->Int Failed");
				updatefail = true;
			}
		}
		else {
			LOG_ERROR("Version Call: Date Data Unreadable - Wrong Size");
			updatefail = true;
		}
	}
	else {
		LOG_ERROR("Version Call: Update Data Not Found");
		updatefail = true;
	}

	if (updatefail) {
		SendToConsole(vfpc::urgent, "Failed to read date/last update record from API.");
		apiUpdated = true;
	}
	else {
		bool stop = false;

		for (size_t i = 0; i < lastupdate.size(); i++) {
			if (!stop) {
				if (lastupdate[i] > timedata[i]) {
					apiUpdated = true;
					stop = true;
					LOG_INFO("Version Call: Update Has Occurred - Pull From API Next Pass.");
				}
				else if (lastupdate[i] != timedata[i]) {
					stop = true;
					LOG_INFO("Version Call: Update Has Not Occurred.");
				}
			}
		}

		if (!stop) {
			apiUpdated = true;
		}

	}

	for (size_t i = 0; i < newdate.size(); i++) {
		timedata[i] = newdate[i];
	}

	bool timefail = false;

	if (version.HasMember("time") && version["time"].IsString() && version.HasMember("day") && version["day"].IsInt()) {

		int day = version["day"].GetInt();

		day += 6;
		day %= 7;



		string time = version["time"].GetString();

		if (time.size() == 8) {
			try {
				int hour = stoi(time.substr(0, 2));
				int mins = stoi(time.substr(3, 2));

				timedata[3] = hour;
				timedata[4] = mins;

				timedata[5] = day;
			}
			catch (...) {
				LOG_ERROR("Version Call: Time Data Unreadable - String->Int Failed");
				timefail = true;
			}
		}
		else {
			LOG_ERROR("Version Call: Time Data Unreadable - Wrong Size");
			timefail = true;
		}
	}
	else {
		LOG_ERROR("Version Call: Time Data Not Found");
		timefail = true;
	}

	if (timefail) {
		SendToConsole(vfpc::urgent, "Failed to read day/time from API.");
	}

	return out;
}


//==============================================================
// 6. Airport snapshot loading / worker helpers
//==============================================================
void CVFPCPlugin::getSids()
{
	const bool already_requested =
		airport_reload_requested_.exchange(true, std::memory_order_acq_rel);

	if (!already_requested) {
		LOG_TRACE("Airport/SID reload requested.");
	}
}

bool CVFPCPlugin::FetchSidsInto_(SidSource source,
	const std::string& activeAirportIcao,
	std::stop_token st,
	rapidjson::Document& out)
{
	out.SetArray();

	if (source == SidSource::File) {
		// Modify fileCall to accept Document& if it doesn’t already.
		// If you only have fileCall(config) today, refactor it to:
		// bool fileCall(rapidjson::Document& out);
		if (!fileCall(out)) {
			LOG_ERROR("FetchSidsInto_: fileCall failed");
			out.SetArray();
			return false;
		}
		return out.IsArray();
	}

	if (activeAirportIcao.empty()) {
		LOG_WARN("FetchSidsInto_: DataServer requested but active airport unknown");
		return false;
	}

	std::string endpoint = plugin_config.airport_endpoint + activeAirportIcao;

	if (!APICall(plugin_config.base_url, endpoint, st, out)) {
		LOG_ERROR("FetchSidsInto_: APICall failed for {}", activeAirportIcao);
		out.SetArray();
		return false;
	}

	return out.IsArray();
}

void CVFPCPlugin::BuildAirportsIndex_(const rapidjson::Document& cfg,
	std::unordered_map<std::string, rapidjson::SizeType>& airportsOut)
{
	airportsOut.clear();
	if (!cfg.IsArray()) return;

	for (rapidjson::SizeType i = 0; i < cfg.Size(); ++i) {
		const auto& airport = cfg[i];
		if (!airport.IsObject()) continue;

		auto it = airport.FindMember("icao");
		if (it != airport.MemberEnd() && it->value.IsString()) {
			airportsOut.emplace(it->value.GetString(), i);
		}
	}
}


//==============================================================
// 7. Validation helper passes
//==============================================================
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

vector<bool> CVFPCPlugin::checkRestriction(const FlightPlanRow& row,
										   string& sid_suffix,
										   const Value& restrictions,
										   bool* sidfails,
										   bool* constfails) {

	LOG_TRACE("{} Restrictions Check: SID Suffix: {}, SID Fails: {}, Const Fails: {}",
			  row.callsign, sid_suffix, BoolToString(*sidfails), BoolToString(*constfails));
	
	vector<bool> res{ 0, 0 }; //0 = Constraint-Level Pass, 1 = SID-Level Pass
	bool constExists = false;
	
	if (restrictions.IsArray() && restrictions.Size()) {
		for (size_t j = 0; j < restrictions.Size(); j++) {
			bool temp = true;
			bool sidlevel = false;
			bool* fails;

			if (restrictions[j].HasMember("sidlevel") &&
				restrictions[j]["sidlevel"].IsBool() &&
				(sidlevel = restrictions[j]["sidlevel"].GetBool())) {
				fails = sidfails;
			}
			else {
				fails = constfails;
				constExists = true;
			}

			if (restrictions[j].HasMember("suffix") && 
				restrictions[j]["suffix"].IsArray() && 
				restrictions[j]["suffix"].Size()) {

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

			if (restrictions[j].HasMember("types") &&
				restrictions[j]["types"].IsArray() && 
				restrictions[j]["types"].Size()) {

				fails[1] = true;
				if (!arrayContains(restrictions[j]["types"], row.engine_type) &&
					!arrayContains(restrictions[j]["types"], row.aircraft_type)) {
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
						if (starttime[0] > endtime[0] ||
							(starttime[0] == endtime[0] && starttime[1] >= endtime[1])) {
							if (timedata[3] > starttime[0] ||
								(timedata[3] == starttime[0] && timedata[4] >= starttime[1]) ||
								timedata[3] < endtime[0] ||
								(timedata[3] == endtime[0] && timedata[4] <= endtime[1])) {
								valid = true;
							}
						}
						else {
							if ((timedata[3] > starttime[0] ||
								(timedata[3] == starttime[0] && timedata[4] >= starttime[1])) &&
								(timedata[3] < endtime[0] ||
									(timedata[3] == endtime[0] && timedata[4] <= endtime[1]))) {
								valid = true;
							}
						}
					}
					else if (startdate == enddate) {
						if (!time) {
							valid = true;
						}
						else if ((timedata[3] > starttime[0] ||
							(timedata[3] == starttime[0] && timedata[4] >= starttime[1])) &&
							(timedata[3] < endtime[0] ||
								(timedata[3] == endtime[0] && timedata[4] <= endtime[1]))) {
							valid = true;
						}
					}
					else if (startdate < enddate) {
						if (timedata[5] > startdate && timedata[5] < enddate) {
							valid = true;
						}
						else if (timedata[5] == startdate) {
							if (!time || timedata[3] > starttime[0] ||
								(timedata[3] == starttime[0] && timedata[4] >= starttime[1])) {
								valid = true;
							}
						}
						else if (timedata[5] == enddate) {
							if (!time || timedata[3] < endtime[0] ||
								(timedata[3] == endtime[0] && timedata[4] < endtime[1])) {
								valid = true;
							}
						}
					}
					else if (startdate > enddate) {
						if (timedata[5] < startdate || timedata[5] > enddate) {
							valid = true;
						}
						else if (timedata[5] == startdate) {
							if (!time || timedata[3] > starttime[0] ||
								(timedata[3] == starttime[0] && timedata[4] >= starttime[1])) {
								valid = true;
							}
						}
						else if (timedata[5] == enddate) {
							if (!time || timedata[3] < endtime[0] ||
								(timedata[3] == endtime[0] && timedata[4] < endtime[1])) {
								valid = true;
							}
						}
					}
				}

				if (!valid) {
					temp = false;
				}
			}

			if (restrictions[j].HasMember("banned") &&
				restrictions[j]["banned"].IsBool() &&
				restrictions[j]["banned"].GetBool()) {
				fails[3] = true;
				temp = false;
			}

			if (temp) {
				res[sidlevel] = true;
			}
		}

		LOG_INFO("{} Restrictions Check - Complete.", row.callsign);
	}

	if (!constExists) {
		res[0] = true;
	}

	return res;
}

vector<bool> CVFPCPlugin::checkRestrictions(const FlightPlanRow& row,
											const Value& conditions,
											string& sid_suffix,
											bool* sidfails,
											bool* constfails,
											bool* sidwide,
											vector<bool>& in) {

	vector<bool> out{};

	for (size_t i = 0; i < conditions.Size(); i++) {
		if (!in[i]) {
			out.push_back(false);
			continue;
		}

		bool res = true;

		vector<bool> temp = checkRestriction(row, sid_suffix, conditions[i]["restrictions"], sidfails, constfails);

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
			to_upper(direction);

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

vector<bool> CVFPCPlugin::checkAlerts(const Value& conditions, bool* warn, vector<bool> in) {
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


//==============================================================
// 8. Validation support helpers
//==============================================================
// 8a. Flight-plan parsing and normalisation helpers.

CVFPCPlugin::ValidationResult CVFPCPlugin::ValidateFlightPlan_(const FlightPlanRow& row) const
{
	ValidationResult result = initialiseResults(row.callsign);

	// Snapshot read: safe across threads
	auto snap = airport_data_.load(std::memory_order_acquire);

	// -----------------------------
	// 1. IFR gating
	// -----------------------------
	if (!row.is_ifr) {
		SetNotChecked_(result, "VFR");
		result.fields[VF_SID].normal = "Not Checked";
		result.fields[VF_SID].debug = "Flight plan type is not IFR; SID validation not applicable.";

		return result;
	}

	// -----------------------------
	// 2. Snapshot availability
	// -----------------------------
	if (!snap) {
		SetPending_(result, "...");
		result.fields[VF_SID].normal = "Loading";
		result.fields[VF_SID].debug = "Airport data not loaded yet (snapshot is null).";

		return result;
	}

	const rapidjson::Document& cfg = snap->config;
	const auto& airports = snap->airports;

	// ----------------------------------------------------------
	// 3. Basic field checks
	// ----------------------------------------------------------
	if (row.origin.empty()) {
		SetFailed_(result, "ERR");
		result.fields[VF_SID].normal = "ERR";
		result.fields[VF_SID].debug = "Origin missing.";
		return result;
	}

	if (row.destination.empty()) {
		SetFailed_(result, "ERR");
		result.fields[VF_DESTINATION].normal = "ERR";
		result.fields[VF_DESTINATION].debug = "Destination missing.";
		return result;
	}

	if (row.route.empty()) {
		SetFailed_(result, "ERR");
		result.fields[VF_SYNTAX].normal = "ERR";
		result.fields[VF_SYNTAX].debug = "Route missing.";
		return result;
	}

	// ----------------------------------------------------------
	// 4. Origin airport exists in snapshot
	// ----------------------------------------------------------
	rapidjson::SizeType origin_int{};
	if (!TryGetOriginAirportIndex_(cfg, airports, row.callsign, row.origin, origin_int, result)) {
		return result;
	}

	LOG_TRACE("Origin airport '{}' found in snapshot at index {}", row.origin, origin_int);

	// ----------------------------------------------------------
	// 5. Syntax / normalisation checks
	// ----------------------------------------------------------
	std::vector<std::string> route = row.route_points;
	std::string outchk;

	if (!NormaliseAndValidateRouteSyntax_(
		route,
		row.origin,
		row.destination,
		row.sid,
		row.first_waypoint,
		outchk))
	{
		SetFailed_(result, "ERR");
		result.fields[VF_SYNTAX].normal = "Invalid Syntax";
		result.fields[VF_SYNTAX].debug = outchk;
		result.fields[VF_STATUS].normal = "Failed";
		result.fields[VF_STATUS].debug = "Failed";
		return result;
	}

	LOG_TRACE("Route syntax valid for '{}'. Origin: '{}', Dest: '{}', SID: '{}', FWP: '{}', Points: '{}'", 
			   row.callsign,row.origin,row.destination, row.sid, row.first_waypoint, row.route.size() );
	result.fields[VF_SYNTAX].normal = "Passed";
	result.fields[VF_SYNTAX].debug = "Route syntax valid.";

	// ----------------------------------------------------------
	// 6. SIDs defined?
	// ----------------------------------------------------------
	if (!cfg[origin_int].HasMember("sids") ||
		!cfg[origin_int]["sids"].IsArray() ||
		!cfg[origin_int]["sids"].Size())
	{
		SetFailed_(result, "ERR");
		result.fields[VF_SID].normal = "No SIDs";
		result.fields[VF_SID].debug = row.origin + " exists in database but has no SIDs (or non-SID routes) defined.";
		result.fields[VF_STATUS].normal = "Failed";
		result.fields[VF_STATUS].debug = "Failed";
		return result;
	}

	// ----------------------------------------------------------
	// 7. Find matching SID / non-SID route
	// ----------------------------------------------------------
	const rapidjson::Value& sids = cfg[origin_int]["sids"];
	size_t pos = std::string::npos;

	if (sids.Size() == 1 &&
		sids[0].HasMember("point") &&
		sids[0]["point"].IsString() &&
		std::string(sids[0]["point"].GetString()).empty())
	{
		pos = 0;
	}
	else
	{
		if (!row.first_waypoint.empty()) {
			TryFindSidIndex_(sids, row.first_waypoint, pos);
		}
	}

	if (pos == std::string::npos) {
		SetFailed_(result, "ERR");

		if (row.first_waypoint.empty()) {
			result.fields[VF_SID].normal = "SID Required";
			result.fields[VF_SID].debug = "Non-SID departure routes not in database.";
		}
		else {
			result.fields[VF_SID].normal = "SID Not Found";
			result.fields[VF_SID].debug = row.sid + " departure not in database.";
		}

		result.fields[VF_STATUS].normal = "Failed";
		result.fields[VF_STATUS].debug = "Failed";
		return result;
	}

	const rapidjson::Value& sid_ele = sids[pos];
	const rapidjson::Value& conditions = sid_ele["constraints"];

	// ----------------------------------------------------------
	// 8. SID field output
	// ----------------------------------------------------------
	if (!row.sid.empty()) {
		result.fields[VF_SID].normal = "SID - " + row.sid + ".";
		result.fields[VF_SID].debug = "SID - " + row.sid + ".";
	}
	else {
		result.fields[VF_SID].normal = "Non-SID Route.";
		result.fields[VF_SID].debug = "Non-SID Route.";
	}

	// ----------------------------------------------------------
	// 9. TODO: migrate remaining legacy checks
	// ----------------------------------------------------------
	// Here you still need to port:
	// - checkRestriction / checkRestrictions
	// - checkDestination
	// - checkExitPoint
	// - checkRoute
	// - checkMinMax
	// - checkDirection
	// - checkAlerts
	// and the various Output(...) helpers



	result.ready = true;
	result.status = ValidationStatus::Passed;   // or Failed / NotChecked
	result.passed = true;                       // or false
	result.itemString = "OK!";                  // or SID / DST / etc
	return result;
}

CVFPCPlugin::ParsedFlightPlanData
CVFPCPlugin::CParseFlightPlanData_(CFlightPlan flightPlan) const {

	ParsedFlightPlanData data;

	data.eobt = flightPlan.GetFlightPlanData().GetEstimatedDepartureTime();

	data.origin = flightPlan.GetFlightPlanData().GetOrigin();
	to_upper(data.origin);

	data.destination = flightPlan.GetFlightPlanData().GetDestination();
	to_upper(data.destination);

	data.rfl = flightPlan.GetFlightPlanData().GetFinalAltitude();

	data.rawroute = flightPlan.GetFlightPlanData().GetRoute();
	trim(data.rawroute);

	data.route = split(data.rawroute, ' ');
	data.route.erase(std::remove_if(data.route.begin(), data.route.end(),
		[](const std::string& s) { return s.empty(); }), data.route.end());

	for (auto& token : data.route) {
		to_upper(token);
	}

	CFlightPlanExtractedRoute extracted = flightPlan.GetExtractedRoute();
	for (int i = 0; i < extracted.GetPointsNumber(); ++i) {
		data.points.push_back(extracted.GetPointName(i));
	}

	data.sid = flightPlan.GetFlightPlanData().GetSidName();
	to_upper(data.sid);

	if (!data.sid.empty()) {
		erase(data.sid, '#');

		if (data.origin == "EGLL" && data.sid == "CHK") {
			data.first_wp = "CPT";
			data.sid_suffix = "CHK";
		}
		else {
			data.first_wp = data.sid.substr(0, data.sid.find_first_of("0123456789"));
			if (!data.first_wp.empty()) {
				to_upper(data.first_wp);
			}

			if (!data.sid.empty()) {
				data.sid_suffix = std::string(1, data.sid.back());
			}
		}
	}

	return data;
}

bool CVFPCPlugin::TryFindSidIndex_(
	const rapidjson::Value& sids,
	const std::string& first_wp,
	size_t& pos)const 
{
	pos = std::string::npos;

	if (!sids.IsArray() || sids.Empty()) {
		return false;
	}

	if (sids.Size() == 1 &&
		sids[0].IsObject() &&
		sids[0].HasMember("point") &&
		sids[0]["point"].IsString() &&
		std::string(sids[0]["point"].GetString()).empty()) {
		pos = 0;
		return true;
	}

	for (rapidjson::SizeType i = 0; i < sids.Size(); ++i) {
		const auto& sid = sids[i];
		if (!sid.IsObject()) {
			continue;
		}

		if (sid.HasMember("point") && sid["point"].IsString() &&
			first_wp == sid["point"].GetString() &&
			sid.HasMember("constraints") && sid["constraints"].IsArray()) {
			pos = i;
			return true;
		}

		if (sid.HasMember("aliases") && sid["aliases"].IsArray()) {
			for (rapidjson::SizeType j = 0; j < sid["aliases"].Size(); ++j) {
				if (sid["aliases"][j].IsString() &&
					first_wp == sid["aliases"][j].GetString() &&
					sid.HasMember("constraints") && sid["constraints"].IsArray()) {
					pos = i;
					return true;
				}
			}
		}
	}

	return false;
}

bool CVFPCPlugin::NormaliseAndValidateRouteSyntax_(
	std::vector<std::string>& route,
	const std::string& origin,
	const std::string& destination,
	const std::string& sid,
	const std::string& first_wp,
	std::string& outchk) const
{
	std::regex spdlvl("(N|M|K)[0-9]{3,4}((A|F)[0-9]{3}|(S|M)[0-9]{4})");
	std::regex spdlvlslash("\\/(N|M|K)[0-9]{3,4}((A|F)[0-9]{3}|(S|M)[0-9]{4})((A|F)[0-9]{3}|(S|M)[0-9]{4})?");
	std::regex icaorwy("[A-Z]{4}(\\/[0-9]{2}(L|C|R)?)?");
	std::regex sidstarrwy("[A-Z]{2,5}[0-9][A-Z](\\/[0-9]{2}(L|C|R)?)?");
	std::regex dctspdlvl("DCT\\/(N|M|K)[0-9]{3,4}((A|F)[0-9]{3}|(S|M)[0-9]{4})");
	std::regex awy("(U)?[A-Z][0-9]{1,3}([A-Z])?");

	bool success = true;
	bool repeat = false;
	std::vector<std::string> new_route;

	for (size_t i = 0; i < 5; ++i) {
		if (!success) break;

		if (route.empty()) {
			outchk = "No Route";
			return false;
		}

		switch (i) {
		case 0:
			if (std::regex_match(route.front(), spdlvl)) {
				route.erase(route.begin());
			}
			break;

		case 1:
			do {
				repeat = false;

				if (route.empty()) {
					outchk = "No Route";
					return false;
				}

				if (std::regex_match(route.front(), sidstarrwy) || route.front() == "SID") {
					route.erase(route.begin());
					repeat = true;
				}
				else if (std::regex_match(route.front(), icaorwy)) {
					if (route.front().substr(0, 4) == origin) {
						route.erase(route.begin());
						repeat = true;
					}
					else {
						outchk = "Different Origin in Route";
						return false;
					}
				}
			} while (repeat);
			break;

		case 2:
			do {
				repeat = false;

				if (route.empty()) {
					outchk = "No Route";
					return false;
				}

				if (std::regex_match(route.back(), sidstarrwy) || route.back() == "STAR") {
					route.pop_back();
					repeat = true;
				}
				else if (std::regex_match(route.back(), icaorwy)) {
					if (route.back().substr(0, 4) == destination) {
						route.pop_back();
						repeat = true;
					}
					else {
						outchk = "Different Destination in Route";
						return false;
					}
				}
			} while (repeat);
			break;

		case 3:
			new_route.clear();
			for (const auto& each : route) {
				if (std::regex_match(each, dctspdlvl)) {
					return false;
				}

				if (each == "DCT") {
					continue;
				}

				if (std::regex_match(each, awy)) {
					new_route.push_back(each);
					continue;
				}

				const size_t slash = each.find('/');
				if (slash == std::string::npos) {
					new_route.push_back(each);
					continue;
				}

				const std::string chng = each.substr(slash);
				if (std::regex_match(chng, spdlvlslash)) {
					new_route.push_back(each.substr(0, slash));
				}
				else {
					outchk = "Invalid Speed/Level Change";
					return false;
				}
			}
			route = std::move(new_route);
			break;

		case 4:
			if (!sid.empty()) {
				if (route.empty()) {
					outchk = "No Route";
					return false;
				}
				if (route.front() != first_wp) {
					outchk = "Route Not From First Waypoint";
					return false;
				}
				route.erase(route.begin());
			}
			break;
		}
	}

	return true;
}

bool CVFPCPlugin::TryGetOriginAirportIndex_(
	const rapidjson::Document& cfg,
	const std::unordered_map<std::string, rapidjson::SizeType>& airports,
	const std::string& callsign,
	const std::string& origin,
	rapidjson::SizeType& origin_int,
	ValidationResult& result) const
{
	LOG_TRACE("{} Validating Flight Plan - Origin: {}", callsign, origin);
	LOG_TRACE("No. of Airports: {}", airports.size());

	const auto it = airports.find(origin);
	if (it == airports.end()) {
		result.fields[VF_SID].normal = "Airport Not Found";
		result.fields[VF_SID].debug = origin + " not in database.";
		return false;
	}

	origin_int = it->second;

	if (!cfg.IsArray()) {
		LOG_ERROR("cfg is not an array (type={})", static_cast<int>(cfg.GetType()));
		result.fields[VF_SID].normal = "Data Error";
		result.fields[VF_SID].debug = "Airport snapshot root is not an array.";
		return false;
	}

	if (origin_int >= cfg.Size()) {
		LOG_ERROR("origin_int out of range: {} (cfg.Size={})", origin_int, cfg.Size());
		result.fields[VF_SID].normal = "Data Error";
		result.fields[VF_SID].debug = "Origin index out of range in airport snapshot.";
		return false;
	}

	const auto& originNode = cfg[origin_int];
	if (!originNode.IsObject()) {
		LOG_ERROR("cfg[origin_int] is not an object (type={})", static_cast<int>(originNode.GetType()));
		result.fields[VF_SID].normal = "Data Error";
		result.fields[VF_SID].debug = "Origin node is not a JSON object.";
		return false;
	}

	return true;
}

bool CVFPCPlugin::IsIfrFlightPlan_(const CFlightPlan& flightPlan) const { 
	
	string fpType{ flightPlan.GetFlightPlanData().GetPlanType() };
	to_upper(fpType);
	return fpType == "I";
}

CVFPCPlugin::ValidationResult
CVFPCPlugin::MakePendingResult_(const std::string& callsign,
	const std::string& normal,
	const std::string& debug) const
{
	ValidationResult result = initialiseResults(callsign);

	SetPending_(result, "...");
	result.fields[VF_SID].normal = normal;
	result.fields[VF_SID].debug = debug;

	return result;
}

// 8b. Validation result helpers.
void CVFPCPlugin::SetResultField(vector<vector<string>>& out,
	ValidationField field,
	const string& normal,
	const string& debug) {
	out[0][field] = normal;
	out[1][field] = debug;
}

void CVFPCPlugin::SetResultBoth(vector<vector<string>>& out,
	ValidationField field,
	const string& text) {
	out[0][field] = text;
	out[1][field] = text;
}

vector<vector<string>> CVFPCPlugin::ReturnWithField(vector<vector<string>>& out,
	ValidationField field,
	const string& normal,
	const string& debug) {
	SetResultField(out, field, normal, debug);
	SetResultBoth(out, VF_STATUS, "Failed");
	return out;
}

vector<vector<string>> CVFPCPlugin::ReturnWithBoth(vector<vector<string>>& out,
	ValidationField field,
	const string& text) {
	SetResultBoth(out, field, text);
	SetResultBoth(out, VF_STATUS, "Failed");
	return out;
}

// 8c. Active-airport session helpers
void CVFPCPlugin::ResetActiveAirportState() {
	{
		std::lock_guard<std::mutex> g(active_mtx_);
		active_airport_.clear();
		active_airport_locked_ = false;
		airport_data_requested_ = false;
	}

	{
		std::lock_guard<std::mutex> g(airport_candidates_mtx_);
		airport_candidates_.clear();
		callsign_to_origin_.clear();
	}
}

void CVFPCPlugin::ObserveActiveAirportCandidate_(const FlightPlanRow& row)
{
	if (!row.is_ifr) {
		return;
	}

	if (row.callsign.empty()) {
		return;
	}

	std::string origin = row.origin;
	to_upper(origin);

	if (origin.empty()) {
		return;
	}

	{
		std::lock_guard<std::mutex> g(airport_candidates_mtx_);

		auto it = callsign_to_origin_.find(row.callsign);
		if (it != callsign_to_origin_.end()) {
			if (it->second == origin) {
				return;
			}

			auto old_it = airport_candidates_.find(it->second);
			if (old_it != airport_candidates_.end() && old_it->second.seen_count > 0) {
				--old_it->second.seen_count;
			}
		}

		callsign_to_origin_[row.callsign] = origin;
		++airport_candidates_[origin].seen_count;
	}

	LOG_TRACE("Observed airport candidate '{}' from callsign '{}'", origin, row.callsign);
}

std::string CVFPCPlugin::DetermineActiveAirportFromSession()
{
	std::lock_guard<std::mutex> g(airport_candidates_mtx_);

	std::string best_airport;
	int best_count = 0;
	int second_best = 0;

	for (const auto& [airport, candidate] : airport_candidates_)
	{
		if (candidate.seen_count > best_count) {
			second_best = best_count;
			best_count = candidate.seen_count;
			best_airport = airport;
		}
		else if (candidate.seen_count > second_best) {
			second_best = candidate.seen_count;
		}
	}

	if (best_count >= 1 && best_count > second_best) {
		LOG_INFO("Active airport inferred from tagged flight plans '{}' count={}",
			best_airport, best_count);
		return best_airport;
	}

	//LOG_TRACE("Active airport not yet inferable from tagged flight plans.");
	return {};
}

void CVFPCPlugin::TryDetermineAndLockActiveAirport()
{
	{
		std::lock_guard<std::mutex> g(active_mtx_);
		if (active_airport_locked_) {
			return;
		}
	}

	const std::string airport = DetermineActiveAirportFromSession();
	if (airport.empty()) {
		return;
	}

	{
		std::lock_guard<std::mutex> g(active_mtx_);
		if (active_airport_locked_) {
			return;
		}

		active_airport_ = airport;
		active_airport_locked_ = true;
		LOG_INFO("Active airport locked to '{}'", active_airport_);
	}
}

void CVFPCPlugin::EnsureAirportDataRequested()
{
	std::string airport;

	{
		std::lock_guard<std::mutex> g(active_mtx_);
		if (!active_airport_locked_ || active_airport_.empty() || airport_data_requested_) {
			return;
		}

		airport = active_airport_;
		airport_data_requested_ = true;
	}

	LOG_INFO("Requesting airport data for '{}'", airport);
	getSids();
}

// 8d. Flight-plan tracking helpers
FlightPlanRow CVFPCPlugin::BuildFlightPlanRow_(const CFlightPlan& flightPlan) const
{
	FlightPlanRow row;

	row.callsign = flightPlan.GetCallsign();
	row.is_ifr = IsIfrFlightPlan_(flightPlan);
	row.aircraft_type = flightPlan.GetFlightPlanData().GetAircraftType();
	row.engine_type = flightPlan.GetFlightPlanData().GetEngineType();

	const auto parsed = CParseFlightPlanData_(flightPlan);

	row.eobt = parsed.eobt;
	row.origin = parsed.origin;
	row.destination = parsed.destination;
	row.rfl = parsed.rfl;
	row.route = parsed.rawroute;
	row.route_points = parsed.route;
	row.sid = parsed.sid;
	row.sid_suffix = parsed.sid_suffix;
	row.first_waypoint = parsed.first_wp;


	return row;
}

string CVFPCPlugin::BuildFlightPlanFingerprint_(const FlightPlanRow& row) const
{
	return row.callsign + "|" +
		row.origin + "|" +
		row.destination + "|" +
		row.route + "|" +
		row.sid + "|" +
		row.first_waypoint + "|" +
		std::to_string(row.rfl) + "|" +
		(row.is_ifr ? "I" : "V");
}

void CVFPCPlugin::ProcessTrackedFlightPlan_(const FlightPlanRow& row)
{
	const auto now = std::chrono::steady_clock::now();
	const std::string fingerprint = BuildFlightPlanFingerprint_(row);
	const bool can_validate_now = CanValidateFlightPlans_();

	bool should_validate = false;
	bool should_store_pending = false;

	{
		std::lock_guard<std::mutex> g(tracked_flightplans_mtx_);

		auto it = tracked_flightplans_.find(row.callsign);

		if (it == tracked_flightplans_.end())
		{
			TrackedFlightPlan tracked;
			tracked.fingerprint = fingerprint;
			tracked.last_seen = now;
			tracked.last_row = row;

			tracked_flightplans_[row.callsign] = std::move(tracked);

			if (can_validate_now) should_validate = true;
			else should_store_pending = true;
		}
		else
		{
			it->second.last_seen = now;

			const bool changed = (it->second.fingerprint != fingerprint);
			if (changed)
			{
				it->second.fingerprint = fingerprint;
				it->second.last_row = row;

				if (can_validate_now) should_validate = true;
				else should_store_pending = true;
			}
			else if (!it->second.last_result.ready && can_validate_now)
			{
				should_validate = true;
			}
		}
	}

	if (should_store_pending)
	{
		ValidationResult pending = initialiseResults(row.callsign);

		pending.fields[VF_SID].normal = "Loading";
		pending.fields[VF_SID].debug = "Airport data not loaded yet.";

		pending.fields[VF_STATUS].normal = "Pending";
		pending.fields[VF_STATUS].debug = "Pending";

		pending.itemString = "...";
		pending.ready = false;
		pending.passed = false;
		pending.status = ValidationStatus::Pending;

		std::lock_guard<std::mutex> g(tracked_flightplans_mtx_);
		auto it = tracked_flightplans_.find(row.callsign);
		if (it != tracked_flightplans_.end()) {
			it->second.last_result = std::move(pending);
		}
		return;
	}

	if (!should_validate) {
		return;
	}

	auto result = ValidateFlightPlan_(row);

	{
		std::lock_guard<std::mutex> g(tracked_flightplans_mtx_);
		auto it = tracked_flightplans_.find(row.callsign);
		if (it != tracked_flightplans_.end()) {
			it->second.last_result = std::move(result);
		}
	}
}

void CVFPCPlugin::PruneTrackedFlightPlans_()
{
	const auto now = std::chrono::steady_clock::now();
	constexpr auto timeout = std::chrono::seconds(10);

	std::lock_guard<std::mutex> g(tracked_flightplans_mtx_);

	for (auto it = tracked_flightplans_.begin();
		it != tracked_flightplans_.end(); )
	{
		if (now - it->second.last_seen > timeout)
		{
			LOG_TRACE("Removing stale flight plan '{}'", it->first);
			it = tracked_flightplans_.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void CVFPCPlugin::ResetTrackedFlightPlans_()
{
	std::lock_guard<std::mutex> g(tracked_flightplans_mtx_);
	tracked_flightplans_.clear();
}

bool CVFPCPlugin::CanValidateFlightPlans_() const
{
	if (!active_airport_locked_) {
		return false;
	}

	auto snap = airport_data_.load(std::memory_order_acquire);
	if (!snap) {
		return false;
	}

	if (!snap->config.IsArray()) {
		return false;
	}

	return true;
}

// 8e. Validation result helpers
void CVFPCPlugin::SetPending_(ValidationResult& result, const std::string& text) const
{
	result.ready = false;
	result.passed = false;
	result.status = ValidationStatus::Pending;
	result.itemString = text;

	result.fields[VF_STATUS].normal = "Pending";
	result.fields[VF_STATUS].debug = "Pending";
}

void CVFPCPlugin::SetPassed_(ValidationResult& result, const std::string& text) const
{
	result.ready = true;
	result.passed = true;
	result.status = ValidationStatus::Passed;
	result.itemString = text;

	result.fields[VF_STATUS].normal = "Passed";
	result.fields[VF_STATUS].debug = "Passed";
}

void CVFPCPlugin::SetFailed_(ValidationResult& result, const std::string& text) const
{
	result.ready = true;
	result.passed = false;
	result.status = ValidationStatus::Failed;
	result.itemString = text;

	result.fields[VF_STATUS].normal = "Failed";
	result.fields[VF_STATUS].debug = "Failed";
}

void CVFPCPlugin::SetNotChecked_(ValidationResult& result, const std::string& text) const
{
	result.ready = true;
	result.passed = false;
	result.status = ValidationStatus::NotChecked;
	result.itemString = text;

	result.fields[VF_STATUS].normal = "Not Checked";
	result.fields[VF_STATUS].debug = "Not Checked";
}


//==============================================================
// 9. Main validation orchestration
//==============================================================
/*
vector<vector<string>> CVFPCPlugin::validateSid(CFlightPlan flightPlan) {

	// Snapshot read: safe across threads (atomic shared_ptr)
	auto snap = airport_data_.load(std::memory_order_acquire);

	string callsign = flightPlan.GetCallsign();

	// Initialise the results vector with default values.
	vector<vector<string>> returnOut = { vector<string>(), vector<string>() }; // 0 = Callsign, 1 = SID, 2 = Destination, 3 = Exit Point, 4 = Route, 5 = Min/Max Flight Level, 6 = Even/Odd, 7 = Suffix, 8 = Restrictions, 9 = Warnings, 10 = Bans, 11 = Syntax, 12 = Passed/Failed
	returnOut = initialiseResults(callsign);

	if (!IsIfrFlightPlan_(flightPlan)) {
		SetResultField(returnOut, VF_SID,
			"Not Checked",
			"Flight plan type is not IFR; SID validation not applicable.");
		SetResultBoth(returnOut, VF_STATUS, "Not Checked");
		return returnOut;
	}

	if (!snap) {
		returnOut[0][1] = "Loading";
		returnOut[1][1] = "Airport data not loaded yet (snapshot is null).";
		return returnOut;
	}

	const rapidjson::Document& cfg = snap->config;
	const auto& airports = snap->airports;

	auto fp = CParseFlightPlanData_(flightPlan);

	rapidjson::SizeType origin_int{};
	if (!TryGetOriginAirportIndex_(cfg, airports, callsign, fp.origin, origin_int, returnOut)) {
		return returnOut;
	}

	const auto& originNode = cfg[origin_int];
	if (!originNode.HasMember("sids") || !originNode["sids"].IsArray() || originNode["sids"].Empty()) {
		returnOut[0][1] = "No SIDs or Non-SID Routes Defined";
		returnOut[1][1] = fp.origin + " exists in database but has no SIDs (or non-SID routes) defined.";
		return returnOut;
	}

	std::vector<std::string> route = fp.route;
	std::string outchk;
	if (!NormaliseAndValidateRouteSyntax_(route, fp.origin, fp.destination, fp.sid, fp.first_wp, outchk)) {
		returnOut[0][11] = "Invalid Syntax - " + outchk + ".";
		returnOut[1][11] = "Invalid Syntax - " + outchk + ".";
		return returnOut;
	}

	size_t pos{};
	if (!TryFindSidIndex_(originNode["sids"], fp.first_wp, pos)) {
		if (fp.first_wp.empty()) {
			returnOut[0][1] = "SID Required";
			returnOut[1][1] = "Non-SID departure routes not in database.";
		}
		else {
			returnOut[0][1] = "SID Not Found";
			returnOut[1][1] = fp.sid + " departure not in database.";
		}
		return returnOut;
	}

	return returnOut;
	//return RunSidConstraintChecksAndBuildOutput_(
	//	flightPlan, cfg, origin_int, originNode["sids"][pos], fp, route, returnOut);
}
*/

//==============================================================
// 10. Validation output builders
//==============================================================
//Outputs route bans as string
string CVFPCPlugin::BansOutput(CFlightPlan flightPlan, const Value& constraints, vector<size_t> successes, vector<string> extracted_route, string dest, int rfl) {
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
	vector<string> warnings{};
	for (int each : successes) {
		if (constraints[each]["alerts"].IsArray() && constraints[each]["alerts"].Size()) {
			for (size_t i = 0; i < constraints[each]["alerts"].Size(); i++) {
				if (constraints[each]["alerts"][i].HasMember("warn") && constraints[each]["alerts"][i]["warn"].IsBool() && constraints[each]["alerts"][i]["warn"].GetBool()) {
					if (constraints[each]["alerts"][i].HasMember("srd") && constraints[each]["alerts"][i]["srd"].IsInt()) {
						warnings.push_back("SRD Note " + to_string(constraints[each]["alerts"][i]["srd"].GetInt()));
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

			if (!all_of(this_rest[0].begin(), this_rest[0].end(), [](unsigned char c) { return std::isspace(c); }) ||
				!all_of(this_rest[1].begin(), this_rest[1].end(), [](unsigned char c) { return std::isspace(c); }) ||
				!all_of(this_rest[2].begin(), this_rest[2].end(), [](unsigned char c) { return std::isspace(c); })) {
				rests.push_back(this_rest);
			}
		}
	}

	return rests;
}

//Outputs valid suffices (from Restrictions array) as string
string CVFPCPlugin::SuffixOutput(CFlightPlan flightPlan, const Value& sid_eles, vector<size_t> successes) {
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

	bool lvls[2]{ false, false };
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

	bool changed = false;
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
string CVFPCPlugin::ExitPointOutput(const rapidjson::Document& cfg, CFlightPlan flightPlan, size_t origin_int, vector<string> points) {

	map<string, vector<string>> a{}; //Key = Exit Point, Value = Explicitly Permitted SIDs
	vector<bool> b{}; //Implicitly Permitted SIDs (Not Explicitly Prohibited)

	for (size_t i = 0; i < cfg[origin_int]["sids"].Size(); i++) {
		b.push_back(false);

		if (cfg[origin_int]["sids"][i].HasMember("point") && cfg[origin_int]["sids"][i]["point"].IsString()) {
			const Value& conditions = cfg[origin_int]["sids"][i]["constraints"];
			for (size_t j = 0; j < conditions.Size(); j++) {
				if (conditions[j]["points"].IsArray() && conditions[j]["points"].Size()) {
					for (string each : points) {
						if (arrayContains(conditions[j]["points"], each)) {
							if (a.find(each) == a.end()) {
								vector<string> temp{ cfg[origin_int]["sids"][i]["point"].GetString() };
								a.insert(pair<string, vector<string>>(each, temp));
							}
							else {
								a[each].push_back(cfg[origin_int]["sids"][i]["point"].GetString());
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
				string temp = cfg[origin_int]["sids"][i]["point"].GetString();

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
string CVFPCPlugin::DestinationOutput(const rapidjson::Document& cfg, CFlightPlan flightPlan, size_t origin_int, string dest) {

	vector<string> a{}; //Explicitly Permitted
	vector<string> b{}; //Implicitly Permitted (Not Explicitly Prohibited)

	for (size_t i = 0; i < cfg[origin_int]["sids"].Size(); i++) {
		if (cfg[origin_int]["sids"][i].HasMember("point") && cfg[origin_int]["sids"][i]["point"].IsString()) {
			bool push_a = false;
			bool push_b = false;

			const Value& conditions = cfg[origin_int]["sids"][i]["constraints"];
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

			string sidstr = cfg[origin_int]["sids"][i]["point"].GetString();
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


//==============================================================
// 11. UI / EuroScope interaction
//==============================================================
//Handles departure list menu and menu items
void CVFPCPlugin::OnFunctionCall(int FunctionId, const char* ItemString, POINT Pt, RECT Area) {

	try {
		if (FunctionId == TAG_FUNC_CHECKFP_MENU) {
			OpenPopupList(Area, "Options", 1);
			AddPopupListElement("Show Checks", "", TAG_FUNC_CHECKFP_CHECK);
			AddPopupListElement("Toggle Checks", "", TAG_FUNC_CHECKFP_DISMISS);
		}
		else if (FunctionId == TAG_FUNC_CHECKFP_CHECK) {
			//checkFPDetail();
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
		LOG_ERROR("Error: {}", ex.what());
	}
	catch (const std::string& ex) {
		LOG_ERROR("Error: {}", ex);
	}
	catch (...) {
		LOG_ERROR("Error", "An unexpected error occured");
	}
}

bool CVFPCPlugin::Enabled(CFlightPlan flightPlan) {
	try {

		string cad = flightPlan.GetControllerAssignedData().GetFlightStripAnnotation(0);

		if (!strcmp(cad.c_str(), "VFPC/OFF")) return false;
	}
	catch (const std::exception& ex) {
		LOG_ERROR("Error: {}", ex.what());
	}
	catch (const std::string& ex) {
		LOG_ERROR("Error: {}", ex);
	}
	catch (...) {
		LOG_ERROR("Error", "An unexpected error occured");
	}

	return true;
}

//Gets flight plan, checks if (S/D)VFR, calls checking algorithms, and outputs pass/fail result to departure list item
void CVFPCPlugin::OnGetTagItem(CFlightPlan flightPlan, CRadarTarget RadarTarget, int ItemCode,
	int TagData, char sItemString[16], int* pColorCode, COLORREF* pRGB, double* pFontSize) {
	
	try {
		if (ItemCode != TAG_ITEM_CHECKFP) {
			return;
		}
		const char* origin_cstr = flightPlan.GetFlightPlanData().GetOrigin();
		string origin = origin_cstr ? origin_cstr : "";
		to_upper(origin); // Ensure we are working with uppercase airport codes for consistency
		FlightPlanRow row = BuildFlightPlanRow_(flightPlan);
		
		ObserveActiveAirportCandidate_(row);
		ProcessTrackedFlightPlan_(row);

		auto snap = airport_data_.load(std::memory_order_acquire);
		const bool version_ok = validVersion.load(std::memory_order_acquire);

		if (!version_ok || !Enabled(flightPlan) || !snap) {
			strcpy_s(sItemString, 16, "   ");
			return;
		}

		if (snap->airports.find(origin) == snap->airports.end()) {
			strcpy_s(sItemString, 16, "   ");
			return;
		}

		
		//vector<string> validize = validateSid(flightPlan)[0]; // 0 = Callsign, 1 = SID, 2 = Destination, 3 = Exit Point, 4 = Route, 5 = Min/Max Flight Level, 6 = Even/Odd, 7 = Suffix, 8 = Restrictions, 9 = Warnings, 10 = Bans, 11 = Syntax, 12 = Passed/Failed
		//strcpy_s(sItemString, 16, getFails(flightPlan, validize, pRGB).c_str());

	}
	catch (const std::exception& ex) {
		LOG_ERROR("Error: {}", ex.what());
	}
	catch (const std::string& ex) {
		LOG_ERROR("Error: {}", ex);
	}
	catch (...) {
		LOG_ERROR("An unexpected error occured");
	}
}

//Handles console commands
bool CVFPCPlugin::OnCompileCommand(const char* sCommandLine) {

	try {
		//Restart Automatic Data Loading
		if (startsWith((COMMAND_PREFIX + LOAD_COMMAND).c_str(), sCommandLine))
		{
			if (autoLoad) {
				SendToConsole(vfpc::urgent, "Auto-Load Already Active.");
				LOG_INFO("Auto-load activation attempted whilst already active.");
			}
			else {
				fileLoad = false;
				autoLoad = true;
				relCount = 0;
				SendToConsole(vfpc::urgent, "Auto-Load Activated.");
				LOG_INFO("Auto-load reactivated.");
			}
			return true;
		}
		//Disable API and load from Sid.json file
		else if (startsWith((COMMAND_PREFIX + FILE_COMMAND).c_str(), sCommandLine))
		{
			autoLoad = false;
			fileLoad = true;
			SendToConsole(vfpc::urgent, "Attempting to load from File: {}", DATA_FILE);
			LOG_INFO("Will now load from File: {}", DATA_FILE);
			getSids();
			return true;
		}
		//Activate Debug Logging
		else if (startsWith((COMMAND_PREFIX + LOG_COMMAND).c_str(), sCommandLine)) {
			if (debugMode) {
				LOG_INFO("Logging mode deactivated.");
				debugMode = false;
			}
			else {
				debugMode = true;
				LOG_INFO("Logging mode activated.");
			}
			return true;
		}
		//Text-Equivalent of "Show Checks" Button
		else if (startsWith((COMMAND_PREFIX + CHECK_COMMAND).c_str(), sCommandLine))
		{
			//checkFPDetail();
			return true;
		}
	}
	catch (const std::exception& ex) {
		LOG_ERROR("Error: {}", ex.what());
	}
	catch (const std::string& ex) {
		LOG_ERROR("Error: {}", ex);
	}
	catch (...) {
		LOG_ERROR("Error", "An unexpected error occured");
	}

	return false;
}

//Compiles and outputs check details to user
void CVFPCPlugin::checkFPDetail() {
	//try {
	//	if (validVersion) {
	//		CFlightPlan flightPlan = FlightPlanSelectASEL();

	//		string fpType{ flightPlan.GetFlightPlanData().GetPlanType() };
	//		if (!IsIfrFlightPlan_(flightPlan)) {

	//			SendToConsole(vfpc::urgent, "{} Flight Plan checking not supported for VFR Flights!",
	//				std::string(flightPlan.GetCallsign()));
	//			LOG_DEBUG("{} is a VFR Flight Plan.", flightPlan.GetCallsign());
	//		}
	//		else {

	//			LOG_INFO("{} Checking Flight PLan...", flightPlan.GetCallsign());
	//			vector<vector<string>> validize = validateSid(flightPlan);

	//			vector<string> messageBuffer{ validize[0] }; // 0 = Callsign, 1 = SID, 2 = Destination, 3 = Exit Point, 4 = Route, 5 = Min/Max Flight Level, 6 = Even/Odd, 7 = Suffix, 8 = Restrictions, 9 = Warnings, 10 = Bans, 11 = Syntax, 12 = Passed/Failed
	//			vector<string> logBuffer{ validize[1] }; // 0 = Callsign, 1 = SID, 2 = Destination, 3 = Exit Point, 4 = Route, 5 = Min/Max Flight Level, 6 = Even/Odd, 7 = Suffix, 8 = Restrictions, 9 = Warnings, 10 = Bans, 11 = Syntax, 12 = Passed/Failed

	//			string buffer{};
	//			string logbuf{};

	//			if (messageBuffer.at(1).find("Invalid") != 0) {
	//				for (size_t i = 1; i < messageBuffer.size() - 1; i++) {
	//					string temp = messageBuffer.at(i);
	//					string logtemp = logBuffer.at(i);

	//					if (temp != "-") {
	//						buffer += temp;
	//						buffer += " | ";
	//					}

	//					if (logtemp != "-") {
	//						logbuf += logtemp;
	//						logbuf += " | ";
	//					}
	//				}
	//			}

	//			buffer += messageBuffer.back();
	//			logbuf += logBuffer.back();

	//			LOG_TRACE("checkFPDetail {}:{}", messageBuffer.front(), buffer);
	//			LOG_TRACE("checkFPDetail {}:{}", logBuffer.front(), logbuf);
	//		}
	//	}
	//}
	//catch (const std::exception& ex) {
	//	LOG_ERROR("Error: {}", ex.what());
	//}
	//catch (const std::string& ex) {
	//	LOG_ERROR("Error: {}", ex);
	//}
	//catch (...) {
	//	LOG_ERROR("An unexpected error occured");
	//}

}

//Compiles list of failed elements in flight plan, in preparation for adding to departure list
string CVFPCPlugin::getFails(CFlightPlan flightPlan, vector<string> messageBuffer, COLORREF* pRGB) {

	try {
		*pRGB = TAG_RED;

		if (messageBuffer.at(messageBuffer.size() - 2).size() > 1 || messageBuffer.at(messageBuffer.size() - 2).find("-")) {
			return "CHK";
		}
		else if (messageBuffer.at(1).find("SID - ") && messageBuffer.at(1).find("Non-SID Route.")) {
			return "SID";
		}
		else if (!messageBuffer.at(2).find("Failed")) {
			return "DST";
		}
		else if (!messageBuffer.at(3).find("Failed")) {
			return "XPT";
		}
		else if (!messageBuffer.at(4).find("Failed")) {
			return "RTE";
		}
		else if (!messageBuffer.at(5).find("Failed")) {
			return "LVL";
		}
		else if (!messageBuffer.at(6).find("Failed")) {
			return "OER";
		}
		else if (!messageBuffer.at(7).find("Invalid")) {
			return "SUF";
		}
		else if (!messageBuffer.at(8).find("Failed")) {
			return "RST";
		}
		else if (!messageBuffer.at(9).find("Warnings")) {
			*pRGB = TAG_YELLOW;
		}
		else if (!messageBuffer.at(10).find("Route Banned")) {
			return "BAN";
		}
		else {
			*pRGB = TAG_GREEN;
		}

		return "OK!";
	}
	catch (const std::exception& ex) {
		LOG_ERROR("Error: {}", ex.what());
	}
	catch (const std::string& ex) {
		LOG_ERROR("Error: {}", ex);
	}
	catch (...) {
		LOG_ERROR("An unexpected error occured");
	}

	return "   ";
}


//==============================================================
// 12. Main coordinator
//==============================================================
void CVFPCPlugin::OnTimer(int Counter)
{
	if (!is_initialised) return;

	// -----------------------------
	// Version gating (non-blocking)
	// -----------------------------
	if (!version_checked.load(std::memory_order_acquire)) {
		return;
	}
	if (!validVersion.load(std::memory_order_acquire)) {
		return;
	}

	try {
		const bool connected = (GetConnectionType() != CONNECTION_TYPE_NO);

		// -----------------------------
		// Disconnect handling
		// -----------------------------
		if (!connected) {
			if (session_state_ != SessionState::Disconnected) {
				session_state_ = SessionState::Disconnected;
				LOG_INFO("User logged off from EuroScope...");
			}

			if (!disconnect_cleanup_done_) {
				disconnect_cleanup_done_ = true;

				if (airport_thread_.joinable()) {
					LOG_INFO("Stopping airport worker thread (disconnect)...");
					airport_thread_.request_stop();
				}

				airport_load_inflight_.store(false, std::memory_order_release);
				airport_reload_requested_.store(false, std::memory_order_release);
				pending_ready_.store(false, std::memory_order_release);
				airport_data_.store(nullptr, std::memory_order_release);

				ResetActiveAirportState();
				ResetTrackedFlightPlans_();
			}

			last_update = Counter;
			return;
		}

		// -----------------------------
		// Connect transition
		// -----------------------------
		if (session_state_ == SessionState::Disconnected) {
			session_state_ = SessionState::Connected_NoCallsign;
			disconnect_cleanup_done_ = false;
			LOG_INFO("User connected to EuroScope...");
		}

		// -----------------------------
		// 1) Apply completed worker result
		// -----------------------------
		if (pending_ready_.load(std::memory_order_acquire)) {
			std::shared_ptr<AirportSnapshot> snap;
			{
				std::lock_guard lk(pending_mtx_);
				if (pending_ready_.load(std::memory_order_acquire)) {
					snap = std::move(pending_snapshot_);
					pending_ready_.store(false, std::memory_order_release);
				}
			}

			if (snap) {
				airport_data_.store(
					std::const_pointer_cast<const AirportSnapshot>(snap),
					std::memory_order_release);

				LOG_TRACE("Airport snapshot published. airports={} cfgIsArray={}",
					snap->airports.size(), snap->config.IsArray());

				// Snapshot now exists, so a fresh request is no longer outstanding
				{
					std::lock_guard<std::mutex> g(active_mtx_);
					airport_data_requested_ = true;
				}

				airport_load_inflight_.store(false, std::memory_order_release);
			}
			else {
				airport_load_inflight_.store(false, std::memory_order_release);
			}
		}

		// -----------------------------
		// 2) Determine and latch the active airport once per session
		// -----------------------------
		TryDetermineAndLockActiveAirport();

		// -----------------------------
		// 3) Request airport data once after airport is known
		// -----------------------------
		EnsureAirportDataRequested();

		// -----------------------------
		// 4) Cleanup stale flight plans (5-10secs)
		PruneTrackedFlightPlans_();

		// -----------------------------
		// 5) Start worker if reload requested and not already inflight
		// -----------------------------
		if (airport_reload_requested_.exchange(false, std::memory_order_acq_rel)) {

			// Prevent overlap
			if (airport_load_inflight_.exchange(true, std::memory_order_acq_rel)) {
				last_update = Counter;
				return;
			}

			const SidSource source = (fileLoad ? SidSource::File : SidSource::DataServer);

			std::string airport;
			if (source == SidSource::DataServer) {
				std::lock_guard<std::mutex> g(active_mtx_);
				if (active_airport_locked_) {
					airport = active_airport_;
				}
			}

			// In auto/API mode, do not start worker until airport is known
			if (source == SidSource::DataServer && airport.empty()) {
				LOG_TRACE("Airport reload deferred: active airport not yet determined.");
				airport_load_inflight_.store(false, std::memory_order_release);
				last_update = Counter;
				return;
			}

			// Stop previous worker if needed
			if (airport_thread_.joinable()) {
				LOG_TRACE("Stopping previous airport worker thread...");
				airport_thread_.request_stop();
			}

			LOG_TRACE("Starting airport worker thread. source={}, airport='{}'",
				(source == SidSource::File ? "file" : "server"),
				airport);

			airport_thread_ = std::jthread(
				[this, source, airport](std::stop_token st)
				{
					try {
						auto snap = std::make_shared<AirportSnapshot>();

						const bool ok = FetchSidsInto_(
							(source == SidSource::File) ? SidSource::File : SidSource::DataServer,
							airport,
							st,
							snap->config);

						if (!ok || st.stop_requested()) {
							airport_load_inflight_.store(false, std::memory_order_release);
							return;
						}

						BuildAirportsIndex_(snap->config, snap->airports);

						{
							std::lock_guard lk(pending_mtx_);
							pending_snapshot_ = std::move(snap);
							pending_ready_.store(true, std::memory_order_release);
						}
					}
					catch (const std::exception& ex) {
						LOG_ERROR("Airport worker exception: {}", ex.what());
						airport_load_inflight_.store(false, std::memory_order_release);
					}
					catch (...) {
						LOG_ERROR("Airport worker unknown exception.");
						airport_load_inflight_.store(false, std::memory_order_release);
					}
				});
		}

		last_update = Counter;
	}
	catch (const std::exception& ex) {
		LOG_ERROR("Exception in OnTimer: {}", ex.what());
	}
	catch (const std::string& ex) {
		LOG_ERROR("Exception in OnTimer: {}", ex);
	}
	catch (...) {
		LOG_ERROR("Unknown exception in OnTimer.");
	}
}