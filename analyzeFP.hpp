#pragma once
#include "EuroScopePlugIn.h"
#include <sstream>
#include <iostream>
#include <string>
#include <regex>
#include "Constant.hpp"
#include <fstream>
#include <vector>
#include <map>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"

#define MY_PLUGIN_NAME      "VFPC (UK)"
#define MY_PLUGIN_VERSION   "3.5.0"
#define MY_PLUGIN_DEVELOPER "Lenny Colton, Jan Fries, Hendrik Peter, Sven Czarnian"
#define MY_PLUGIN_COPYRIGHT "GPL v3"
#define MY_PLUGIN_VIEW_AVISO  "VATSIM (UK) Flight Plan Checker"
#define MY_API_ADDRESS	"https://vfpc.tomjmills.co.uk/"

#define PLUGIN_WELCOME_MESSAGE	"Welcome to the (UK) VATSIM Flight Plan Checker"

using namespace std;
using namespace boost;
using namespace rapidjson;
using namespace EuroScopePlugIn;

class CVFPCPlugin :
	public EuroScopePlugIn::CPlugIn
{
public:
	CVFPCPlugin();
	virtual ~CVFPCPlugin();

	virtual bool webCall(string url, string& out);

	virtual bool timeCall();

	virtual bool APICall(string endpoint, Document& out);

	virtual bool versionCall();

	virtual bool fileCall(Document &out);

	virtual void getSids();

	virtual bool checkRestrictions(CFlightPlan flightPlan, string sid_suffix, const Value& restrictions, bool* fails);

	virtual vector<vector<string>> validizeSid(CFlightPlan flightPlan);

	virtual string BansOutput(size_t origin_int, size_t pos, vector<size_t> successes);

	virtual string WarningsOutput(size_t origin_int, size_t pos, vector<size_t> successes);

	virtual string AlternativesOutput(size_t origin_int, size_t pos, vector<size_t> successes = {});

	virtual string RestrictionsOutput(size_t origin_int, size_t pos, bool type = true, bool time = true, vector<size_t> successes = {});

	virtual string SuffixOutput(size_t origin_int, size_t pos, vector<size_t> successes = {});

	virtual string DirectionOutput(size_t origin_int, size_t pos, vector<size_t> successes);

	virtual string MinMaxOutput(size_t origin_int, size_t pos, vector<size_t> successes);

	virtual string NavPerfOutput(size_t origin_int, size_t pos, vector< size_t> successes);

	virtual string RouteOutput(size_t origin_int, size_t pos, vector<size_t> successes, vector<string> extracted_route);

	virtual string DestinationOutput(size_t origin_int, string dest);

	//virtual string EngineOutput(size_t origin_int, size_t pos, vector<int> successes);

	//virtual string SuffixOutput(size_t origin_int, size_t pos, vector<int> successes);

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

	template <typename Out>
	void split(const string& s, char delim, Out result) {
		istringstream iss(s);
		string item;
		while (getline(iss, item, delim)) {
			*result++ = item;
		}
	}

	vector<string> split(const string& s, char delim) {
		vector<string> elems;
		split(s, delim, back_inserter(elems));
		return elems;
	}

	string destArrayContains(const Value& a, string s) {
		for (SizeType i = 0; i < a.Size(); i++) {
			string test = a[i].GetString();
			SizeType x = static_cast<rapidjson::SizeType>(s.rfind(test, 0));
			if (s.rfind(a[i].GetString(), 0) != -1)
				return a[i].GetString();
		}
		return "";
	}

	bool arrayContains(const Value& a, string s) {
		for (SizeType i = 0; i < a.Size(); i++) {
			if (a[i].GetString() == s)
				return true;
		}
		return false;
	}

	bool arrayContainsEnding(const Value& a, string s) {
		for (SizeType i = 0; i < a.Size(); i++) {
			string comp = a[i].GetString();
			int pos = s.size() - comp.size();
			if (pos < 0)
				continue;

			bool valid = true;

			for (SizeType i = 0; i < comp.size(); i++) {
				if (comp[i] != s[pos + i])
					valid = false;
			}

			if (valid)
				return true;
			
		}
		return false;
	}

	bool arrayContains(const Value& a, char s) {
		for (SizeType i = 0; i < a.Size(); i++) {
			if (a[i].GetString()[0] == s)
				return true;
		}
		return false;
	}

	string arrayToString(const Value& a, char delimiter) {
		string s;
		for (SizeType i = 0; i < a.Size(); i++) {
			s += a[i].GetString()[0];
			if (i != a.Size() - 1)
				s += delimiter;
		}
		return s;
	}

	bool routeContains(string cs, vector<string> rte, const Value& valid) {
		for (SizeType i = 0; i < valid.Size(); i++) {
			string r = valid[i].GetString();

			if (r == "*") {
				return true;
			}

			vector<string> current = split(r, ' ');
			for (std::size_t j = 0; j < current.size(); j++) {
				boost::to_upper(current[j]);
			}

			bool admissible = true;

			if (current.size() > rte.size()) {
				admissible = false;
			}
			else {
				for (SizeType j = 0; j < current.size(); j++) {
					if (current[j] != rte[j] && current[j] != "*") {
						admissible = false;
					}
				}
			}

			if (admissible) {
				return true;
			}
		}
		return false;
	}

	string dayIntToString(int day) {
		switch (day) {
		case 0:
			return "Monday";
		case 1:
			return "Tuesday";
		case 2:
			return "Wednesday";
		case 3:
			return "Thursday";
		case 4:
			return "Friday";
		case 5:
			return "Saturday";
		case 6:
			return "Sunday";
		default:
			return "Out of Range";
		}
	}

	virtual bool OnCompileCommand(const char * sCommandLine);

	virtual void debugMessage(string type, string message);

	virtual void sendMessage(string type, string message);

	virtual void sendMessage(string message);

	virtual void checkFPDetail();

	virtual string getFails(vector<string> messageBuffer, COLORREF* pRGB);

	virtual void runWebCalls();

	virtual void OnTimer(int Count);

protected:
	Document config;
	vector<string> activeAirports;
	map<string, rapidjson::SizeType> airports;
};

