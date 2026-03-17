//==============================================================
// 1. Includes / forward declarations
//==============================================================
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include <rapidjson/document.h>

#include "Constant.hpp"
#include "Utils.h"
#include "PluginConfig.h"

using namespace std;
using namespace rapidjson;
using namespace EuroScopePlugIn;

//==============================================================
// Validation data structures
//==============================================================

//struct FlightPlanError
//{
//    std::string code;
//    std::string message;
//    bool warning = false;
//};

//struct FlightPlanErrors
//{
//    std::vector<FlightPlanError> items;
//
//    bool HasErrors() const
//    {
//        for (const auto& e : items)
//            if (!e.warning)
//                return true;
//        return false;
//    }
//
//    bool Empty() const
//    {
//        return items.empty();
//    }
//
//    void AddError(std::string code, std::string message)
//    {
//        items.push_back({ std::move(code), std::move(message), false });
//    }
//
//    void AddWarning(std::string code, std::string message)
//    {
//        items.push_back({ std::move(code), std::move(message), true });
//    }
//};

struct FlightPlanRow
{
    std::string callsign;

	std::string eobt; // [day, hour, minute]

    std::string origin;
    std::string destination;

    std::string route;
    std::vector<std::string> route_points;

    std::string sid;
	std::string sid_suffix;
    std::string first_waypoint;

    std::string aircraft_type;
    std::string engine_type;

    int rfl = 0;

    bool is_ifr = false;

    std::string inferred_airport;
};

//==============================================================
// 2. Helper tags / utility types
//==============================================================
namespace vfpc {
    struct UrgentTag {};
    inline constexpr UrgentTag urgent{};
}

namespace
{
    template <typename Out>
    void split(const std::string& s, char delim, Out result)
    {
        std::istringstream iss(s);
        std::string item;

        while (std::getline(iss, item, delim))
        {
            *result++ = item;
        }
    }

    std::vector<std::string> split(const std::string& s, char delim)
    {
        std::vector<std::string> elems;
        split(s, delim, std::back_inserter(elems));
        return elems;
    }
}

//==============================================================
// 3. Plugin Class declarations.
//==============================================================
class CVFPCPlugin : public EuroScopePlugIn::CPlugIn
{
public:

//==============================================================
// 4. Public data structures/result types.
//==============================================================
    // --------------------
    // Snapshot type (immutable once published)
    // --------------------
    struct AirportSnapshot {
        rapidjson::Document config; // array of airports
        std::unordered_map<std::string, rapidjson::SizeType> airports;
        AirportSnapshot() { config.SetArray(); }
    };

    enum ValidationField
    {
        VF_CALLSIGN = 0,
		VF_ORIGIN = 1,
        VF_SID = 2,
        VF_DESTINATION = 3,
        VF_EXIT_POINT = 4,
        VF_ROUTE = 5,
        VF_MINMAX = 6,
        VF_DIRECTION = 7,
        VF_SUFFIX = 8,
        VF_RESTRICTIONS = 9,
        VF_WARNINGS = 10,
        VF_BANS = 11,
        VF_SYNTAX = 12,
        VF_STATUS = 13,
        VF_FIELD_COUNT = 14
    };

    enum class ValidationStatus
    {
        Pending,
        Passed,
        Failed,
        NotChecked
    };

    struct ValidationResult
    {
        bool ready = false;
        bool passed = false;
        std::string itemString;

        struct Field
        {
            std::string normal = "-";
            std::string debug = "-";
        };

        std::array<Field, VF_FIELD_COUNT> fields;
        ValidationStatus status = ValidationStatus::Pending;
    };

//==============================================================
// 5. Construction / destruction
//==============================================================
    CVFPCPlugin();
    virtual ~CVFPCPlugin();

//==============================================================
// 6. API / data loading
//==============================================================
    virtual bool webCall(string url, string& out, std::stop_token st);
    virtual bool APICall(const string& base_url, const string& endpoint, std::stop_token st, Document& out);
    virtual bool fileCall(Document& out);

    // Triggers an async reload of airport/SID data (worker thread builds snapshot)
    virtual void getSids();

//==============================================================
// 7. Validation entry points
//==============================================================
    virtual vector<bool> checkDestination(const Value& constraints, string destination, vector<bool> in);
    virtual vector<bool> checkExitPoint(const Value& constraints, vector<string> extracted_route, vector<bool> in);
    virtual vector<bool> checkRoute(const Value& constraints, vector<string> extracted_route, vector<bool> in);
    virtual vector<bool> checkRestriction(const FlightPlanRow& row, string& sid_suffix, const Value& restrictions, bool* sidfails, bool* fails);
    virtual vector<bool> checkRestrictions(const FlightPlanRow& row, const Value& conditions, string& sid_suffix, bool* sidfails, bool* fails, bool* sidwide, vector<bool>& in);
    virtual vector<bool> checkMinMax(const Value& constraints, int RFL, vector<bool> in);
    virtual vector<bool> checkDirection(const Value& constraints, int RFL, vector<bool> in);
    virtual vector<bool> checkAlerts(const Value& constraints, bool* warn, vector<bool> in);
    
    virtual void checkFPDetail();

    // Snapshot-based validation entry point
   // virtual vector<vector<string>> validateSid(CFlightPlan flightPlan);
//==============================================================
// 8. Validation output formatting
//==============================================================
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

    // These read from the published snapshot's config (passed in explicitly)
    virtual string ExitPointOutput(const rapidjson::Document& cfg, CFlightPlan flightPlan, size_t origin_int, vector<string> extracted_route);
    virtual string DestinationOutput(const rapidjson::Document& cfg, CFlightPlan flightPlan, size_t origin_int, string dest);

//==============================================================
// 9. UI / console helpers
//==============================================================
    virtual bool Enabled(CFlightPlan flightPlan);
    virtual string getFails(CFlightPlan flightPlan, vector<string> messageBuffer, COLORREF* pRGB);
    
    void SendToConsole(const char* msg, bool urgent = false);

    template <typename... Args>
    void SendToConsole(fmt::format_string<Args...> f, Args&&... args)
    {
        std::string s = fmt::format(f, std::forward<Args>(args)...);
        SendToConsole(s.c_str(), false);
    }

    template <typename... Args>
    void SendToConsole(vfpc::UrgentTag, fmt::format_string<Args...> f, Args&&... args)
    {
        std::string s = fmt::format(f, std::forward<Args>(args)...);
        SendToConsole(s.c_str(), true);
    }

//==============================================================
// 10. EuroScope event handlers
//==============================================================
    virtual bool OnCompileCommand(const char* sCommandLine) override;
    virtual void OnFunctionCall(int FunctionId,
                                const char* ItemString,
                                POINT Pt,
                                RECT Area);
    
    virtual void OnGetTagItem(CFlightPlan FlightPlan,
        CRadarTarget RadarTarget,
        int ItemCode,
        int TagData,
        char sItemString[16],
        int* pColorCode,
        COLORREF* pRGB,
        double* pFontSize);
    
    virtual void OnTimer(int Count) override;

//==============================================================
// 11. Snapshot state shared across threads
//==============================================================
    std::atomic_bool version_checked{ false };
    std::atomic_bool validVersion{ false };

    std::atomic<std::shared_ptr<const AirportSnapshot>> airport_data_{ nullptr };

    std::atomic_bool airport_reload_requested_{ false };
    std::atomic_bool airport_load_inflight_{ false };

//==============================================================
// 12. Public utility helpers.
//==============================================================
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
                to_upper(current[j]);
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

    static ValidationResult initialiseResults(const string& cs) {
        ValidationResult result;

        for (auto& f : result.fields)
        {
            f.normal = "-";
            f.debug = "-";
        }

        result.fields[VF_CALLSIGN].normal = cs;
        result.fields[VF_CALLSIGN].debug = cs;

        result.fields[VF_STATUS].normal = "Failed";
        result.fields[VF_STATUS].debug = "Failed";

        return result;
    }

    inline const char* const BoolToString(bool b)
    {
        return b ? "true" : "false";
    }


private:
//==========================================================
// 13. Private data structures
//==========================================================
    struct AirportCandidate
    {
        int seen_count = 0;
    };
    
    struct TrackedFlightPlan
    {
        std::string fingerprint;
        std::chrono::steady_clock::time_point last_seen;
        ValidationResult last_result;
        FlightPlanRow last_row;
    };

    struct ParsedFlightPlanData
    {
        string eobt;
        string origin;
        string destination;
        int rfl = 0;

        string rawroute;
        vector<string> route;
        vector<string> points;

        string sid;
        string first_wp;
        string sid_suffix;
    };

    enum class SidSource { DataServer, File };
    
//==========================================================
// 14. Version / worker thread helpers
//==========================================================
    void StartVersionCheckAsync();
    bool VersionCall_Worker(PluginConfig cfg, std::stop_token st);

    bool FetchSidsInto_(SidSource source,
        const string& activeAirportIcao,
        stop_token st,
        Document& out);

    static void BuildAirportsIndex_(const Document& cfg,
        unordered_map<string, SizeType>& airportsOut);

//==========================================================
// 15. Validation support helpers
//==========================================================
    ValidationResult ValidateFlightPlan_(const FlightPlanRow& row) const;

    bool TryGetOriginAirportIndex_(const Document& cfg,
        const unordered_map<string, SizeType>& airports,
        const string& callsign,
        const string& origin,
        SizeType& origin_int,
        ValidationResult& result) const;

    bool NormaliseAndValidateRouteSyntax_(vector<string>& route,
        const string& origin,
        const string& destination,
        const string& sid,
        const string& first_wp,
        string& outchk) const;

    bool TryFindSidIndex_(const rapidjson::Value& sids,
        const string& first_wp,
        size_t& pos)const;

    bool IsIfrFlightPlan_(const CFlightPlan& flightPlan)const;
    ParsedFlightPlanData CParseFlightPlanData_(CFlightPlan flightPlan) const;

    void SetResultField(vector<vector<string>>& out,
        ValidationField field,
        const string& normal,
        const string& debug);

    void SetResultBoth(vector<vector<string>>& out,
        ValidationField field,
        const string& text);

    vector<vector<string>> ReturnWithField(vector<vector<string>>& out,
        ValidationField field,
        const string& normal,
        const string& debug);

    vector<vector<string>> ReturnWithBoth(vector<vector<string>>& out,
        ValidationField field,
        const string& text);

    ValidationResult MakePendingResult_(const std::string& callsign,
        const std::string& normal = "Loading",
        const std::string& debug = "Airport data not loaded yet.") const;

//==========================================================
// 16. Active airport helpers
//==========================================================

    void ResetActiveAirportState();
    void ObserveActiveAirportCandidate_(const FlightPlanRow& row);
    void TryDetermineAndLockActiveAirport();
    void EnsureAirportDataRequested();
    string DetermineActiveAirportFromSession();

//==========================================================
// 17. Flight plan tracking helpers
//==========================================================
    std::string BuildFlightPlanFingerprint_(const FlightPlanRow& row) const;
    void ProcessTrackedFlightPlan_(const FlightPlanRow& row);
    void PruneTrackedFlightPlans_();
    void ResetTrackedFlightPlans_();
    bool CanValidateFlightPlans_() const;
    FlightPlanRow BuildFlightPlanRow_(const CFlightPlan& flightPlan) const;

//==========================================================
// 18. Vaidation result helpers
//==========================================================

void SetPending_(ValidationResult& result, const std::string& text = "...") const;
void SetPassed_(ValidationResult& result, const std::string& text = "OK") const;
void SetFailed_(ValidationResult& result, const std::string& text = "ERR") const;
void SetNotChecked_(ValidationResult& result, const std::string& text = "VFR") const;

//==========================================================
// 18. Private state
//==========================================================
    string base_url_;

    // version checking
    std::jthread version_thread_;

    // airport worker
    std::jthread airport_thread_;

    // pending snapshot produced by worker, applied on main thread
    mutex pending_mtx_;
    shared_ptr<AirportSnapshot> pending_snapshot_;
    atomic_bool pending_ready_{ false };

    // active airports tracking (from tag items)
    mutex active_mtx_;
    string active_airport_;
    bool active_airport_locked_ = false;  // true, once airport has been determined.
    bool airport_data_requested_ = false; // prevents repeated reload requets.

    bool disconnect_cleanup_done_ = false;
    
    std::mutex airport_candidates_mtx_;
    std::unordered_map<std::string, AirportCandidate> airport_candidates_;
    std::unordered_map<std::string, std::string> callsign_to_origin_;
    
    std::mutex tracked_flightplans_mtx_;
    std::unordered_map<std::string, TrackedFlightPlan> tracked_flightplans_;
    
    // other existing state
    int last_update = -1;

protected:
//==========================================================
// 19. Protected version state.
//==========================================================

    int* thisVersion = nullptr;
    vector<int> curVersion;
    vector<int> minVersion;
};

//==============================================================
// 20. Free helper declarations
//==============================================================
bool InitializeLogging(PluginConfig& pc);
