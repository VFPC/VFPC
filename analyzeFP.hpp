#pragma once
#include "EuroScopePlugIn.h"
#include <sstream>
#include <iostream>
#include <string>
#include "Constant.hpp"
#include <fstream>
#include <vector>
#include <map>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"

#define MY_PLUGIN_NAME      "VFPC"
#define MY_PLUGIN_VERSION   "2.0.6"
#define MY_PLUGIN_DEVELOPER "Jan Fries, Hendrik Peter, Sven Czarnian"
#define MY_PLUGIN_COPYRIGHT "GPL v3"
#define MY_PLUGIN_VIEW_AVISO  "Vatsim FlightPlan Checker"

#define PLUGIN_WELCOME_MESSAGE	"Willkommen beim Vatsim Flugplan-RFL checker"

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

	virtual void getSids();

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
	bool routeContains(vector<string> l, const Value& a) {
		bool accept = false;

		for (SizeType i = 0; i < a.Size(); i++) {
			string c = a[i].GetString();
			string delimiter = ",";
			size_t pos = 0;
			string s;
			vector<string> awys = {};
			while ((pos = c.find(delimiter)) != string::npos) {
				s = c.substr(0, pos);

				if (any_of(s.begin(), s.end(), ::isdigit)) {
					awys.push_back(s);
				}

				c.erase(0, pos + delimiter.length());
			}

			bool admissible = true;

			for (int i = 0; i < c.size() - 1; i++) {
				if (awys[i] != l[i]) {
					admissible = false;
				}
			}

			if (admissible) {
				accept = true;
			}
		}
		return accept;
	}

	virtual bool OnCompileCommand(const char * sCommandLine);

	virtual void debugMessage(string type, string message);

	virtual void sendMessage(string type, string message);

	virtual void sendMessage(string message);

	virtual void checkFPDetail();

	virtual string getFails(vector<string> messageBuffer);

	virtual void OnTimer(int Count);

protected:
	Document config;
	map<string, rapidjson::SizeType> airports;
};

