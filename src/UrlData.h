#ifndef URLDATA_H
#define URLDATA_H


struct VersionData {
	std::string vfpc_version = "";
	std::string api_version = "";
	std::string date = "";
			int day = 1;
	std::string last_updated_date = "";
	std::string last_updated_time = "";
	std::string min_version = "";
	std::string time = "";
	std::string version_string = "";
};

struct CompareResult {
	bool version_ok = false;         // vfpc_version >= min_version
	bool date_ok = false;         // date >= last_updated_date
	bool time_ok = false;         // time >= last_updated_time
	bool older_dt = false;         // (last_updated_*) > (date+time) Don't think this will ever happen.
};

bool ValidVersion(PluginConfig& pc, VersionData& data);





#endif URLDATA_H
