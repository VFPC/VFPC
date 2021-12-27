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

	virtual bool APICall(string endpoint, Document& out);

	virtual bool versionCall();

	virtual bool fileCall(Document &out);

	virtual void getSids();

	virtual vector<bool> checkDestination(const Value& constraints, string destination, vector<bool> in);

	virtual vector<bool> checkExitPoint(const Value& constraints, vector<string> extracted_route, vector<bool> in);

	virtual vector<bool> checkRoute(const Value& constraints, vector<string> extracted_route, vector<bool> in);

	virtual vector<bool> checkRestriction(CFlightPlan flightPlan, string sid_suffix, const Value& restrictions, bool *sidfails, bool* fails);

	virtual vector<bool> checkRestrictions(CFlightPlan flightPlan, const Value& conditions, string sid_suffix, bool *sidfails, bool* fails, bool *sidwide, vector<bool> in);

	virtual vector<bool> checkMinMax(const Value& constraints, int RFL, vector<bool> in);

	virtual vector<bool> checkDirection(const Value& constraints, int RFL, vector<bool> in);

	virtual vector<bool> checkAlerts(const Value& constraints, bool *warn, vector<bool> in);

	virtual vector<vector<string>> validateSid(CFlightPlan flightPlan);

	virtual string BansOutput(CFlightPlan flightPlan, const Value& constraints, vector<size_t> successes, vector<string> extracted_route, string dest, int rfl);

	virtual string WarningsOutput(CFlightPlan flightPlan, const Value& constraints, vector<size_t> successes, vector<string> extracted_route, string dest, int rfl);
	
	virtual string AlternativesOutput(CFlightPlan flightPlan, const Value& sid_ele, vector<size_t> successes = {});

	virtual vector<string> AlternativesSingle(const Value& sid_ele);

	virtual string RestrictionsOutput(CFlightPlan flightPlan, const Value& sid_ele, bool check_type = true, bool check_time = true, bool check_ban = true, vector<size_t> successes = {});

	virtual vector<vector<string>> RestrictionsSingle(const Value& restrictions, bool check_type = true, bool check_time = true, bool check_ban = true);
	
	virtual string SuffixOutput(CFlightPlan flightPlan, const Value& sid_ele, vector<size_t> successes = {});

	virtual vector<string> SuffixSingle(const Value& restrictions);

	virtual string DirectionOutput(CFlightPlan flightPlan, const Value& constraints, vector<size_t> successes);

	virtual string MinMaxOutput(CFlightPlan flightPlan, const Value& constraints, vector<size_t> successes);

	virtual string RouteOutput(CFlightPlan flightPlan, const Value& constraints, vector<size_t> successes, vector<string> extracted_route, string dest, int rfl, bool req_lvl = false);

	virtual string ExitPointOutput(CFlightPlan flightPlan, size_t origin_int, vector<string> extracted_route);

	virtual string DestinationOutput(CFlightPlan flightPlan, size_t origin_int, string dest);

	virtual void OnFunctionCall(int FunctionId, const char * ItemString, POINT Pt, RECT Area);

	virtual bool Enabled(CFlightPlan flightPlan);

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

	bool routeContains(vector<string> rte, const Value& valid) {
		for (SizeType i = 0; i < valid.Size(); i++) {
			string r = valid[i].GetString();

			if (!strcmp(r.c_str(), WILDCARD.c_str())) {
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
					if (current[j] != rte[j] && strcmp(current[j].c_str(), WILDCARD.c_str())) {
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

	inline const char * const BoolToString(bool b)
	{
		return b ? "true" : "false";
	}

	virtual string getPath();

	virtual bool OnCompileCommand(const char * sCommandLine);

	virtual bool clearLog();

	virtual bool bufLog(string message);

	virtual bool writeLog();

	virtual void debugMessage(string type, string message);

	virtual void sendMessage(string type, string message);

	virtual void sendMessage(string message);

	virtual void checkFPDetail();

	virtual string getFails(CFlightPlan flightPlan, vector<string> messageBuffer, COLORREF* pRGB);

	virtual void runWebCalls();

	virtual void OnTimer(int Count);

protected:
	Document config;
	vector<string> loadedAirports;
	vector<string> activeAirports;
	int *thisVersion;
	vector<int> curVersion;
	vector<int> minVersion;
	map<string, rapidjson::SizeType> airports;
};

