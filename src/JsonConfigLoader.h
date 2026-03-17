#ifndef JSONCONFIGLOADER_H
#define JSONCONFIGLOADER_H

// --- C++ Standard ---
#include <windows.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// --- Third Party ---
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"

//File name used for settings.
static constexpr const char* kConfigFileName = "vfpc_config.json";


// If you want to modify the DOM (inject defaults), you must use the document's allocator.
class JsonConfigLoader {
public:
    bool load(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            log_error("Config not found. Creating default file: " + filename);


            if (!write_default(filename)) return false;

            file.open(filename, std::ios::binary);
            if (!file.is_open()) {
                log_error("Failed to reopen config file after writing defaults: " + filename);
                return false;
            }
        }

        if (file.peek() == std::ifstream::traits_type::eof()) {
            log_error("Config file is empty. Recreating default.");
            file.close();
            if (!write_default(filename)) return false;
            file.open(filename);
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string text = buffer.str();

        rapidjson::ParseResult ok = config_doc_.Parse(text.c_str());
        if (!ok) {
            log_error("Config file invalid JSON. Rewriting default.");
            file.close();
            if (!write_default(filename))
                return false;

            return load(filename); // reload clean file
        }

        return true;
    }

    bool validate_and_apply_defaults() {
        // --- spdlog section ---
        if (!validate_section("spdlog", {
                {"log_directory", FieldType::String,  false, "" },
                {"log_filename",  FieldType::String,  false, "" },
                {"max_files",     FieldType::Integer, true,  "5"},
                {"flush_interval_sec", FieldType::Integer, true, "1"},
                {"pattern",       FieldType::String,  true,  "[%Y-%m-%d %H:%M:%S] [%l] %v"},
                {"level",         FieldType::String,  true,  "info"},
			    {"flush_level",   FieldType::String,  true,  "info"},
                {"truncate",      FieldType::Boolean, true,  "false"}
            })) return false;

        // --- curl section ---
        if (!validate_section("curl", {
                {"base_url",            FieldType::String,  false, "" },
                {"airport_endpoint",    FieldType::String, false, "" },
				{"version_endpoint",    FieldType::String, false, "" },
                {"timeout_sec",         FieldType::Integer, true,  "30"},
                {"verify_ssl",          FieldType::Boolean, true,  "true"},
                {"user_agent",          FieldType::String,  true,  "VFPC2Plugin/1.0"}        
            })) return false;

        // curl.default_headers must exist and be an object
        if (!has_object_member_(config_doc_, "curl")) {
            log_error("Missing or invalid 'curl' section");
            return false; // should already be caught by validate_section, but keep safe
        }

        rapidjson::Value& curl = config_doc_["curl"];
        if (!has_object_member_(curl, "default_headers")) {
            log_error("Missing or invalid 'curl.default_headers' object");
            return false;
        }

        const rapidjson::Value& headers = curl["default_headers"];
        if (!validate_field(headers, "Accept", FieldType::String, "curl.default_headers")) return false;
        if (!validate_field(headers, "Content-Type", FieldType::String, "curl.default_headers")) return false;

        const std::string base = config_doc_["curl"]["base_url"].GetString();
        const std::string airport_ep = config_doc_["curl"]["airport_endpoint"].GetString();
        const std::string version_ep = config_doc_["curl"]["version_endpoint"].GetString();

        if (base.empty() || airport_ep.empty() || version_ep.empty()) {
            log_error("curl.base_url / airport_endpoint / version_endpoint must not be empty");
            return false;
        }

        // --- colours section ---
        if (!has_object_member_(config_doc_, "colours")) {
            log_error("Missing or invalid 'colours' section");
            return false;
        }

        const rapidjson::Value& colours = config_doc_["colours"];

        for (const char* colour_name : { "red", "green", "yellow" }) {
            if (!has_object_member_(colours, colour_name)) {
                log_error(std::string("Missing or invalid colour: ") + colour_name);
                return false;
            }

            const rapidjson::Value& colour = colours[colour_name];
            const std::string ctx = std::string("colours.") + colour_name;

            if (!validate_int_range(colour, "r", 0, 255, ctx)) return false;
            if (!validate_int_range(colour, "g", 0, 255, ctx)) return false;
            if (!validate_int_range(colour, "b", 0, 255, ctx)) return false;
        }

        return true;
    }

    bool write_default(const std::string& filename) {
        rapidjson::Document doc;
        doc.SetObject();

        auto& alloc = doc.GetAllocator();

        // -------------------------------
        // spdlog section
        // -------------------------------
        rapidjson::Value spdlog(rapidjson::kObjectType);

        spdlog.AddMember("log_directory", "uk/data/plugin/vfpc/log", alloc);
        spdlog.AddMember("log_filename", "vfpc2_log.txt", alloc);
        spdlog.AddMember("max_files", 5, alloc);
        spdlog.AddMember("flush_interval_sec", 1, alloc);
        spdlog.AddMember("pattern", "[%Y-%m-%d %H:%M:%S] [%l] %v", alloc);
        spdlog.AddMember("level", "info", alloc);
        spdlog.AddMember("truncate", false, alloc);

        doc.AddMember("spdlog", spdlog, alloc);

        // -------------------------------
        // curl section
        // -------------------------------
        rapidjson::Value curl(rapidjson::kObjectType);

        curl.AddMember("base_url", "https://vfpcplugin.org/", alloc);
        curl.AddMember("airport_endpoint", "airport?icao=", alloc);
        curl.AddMember("version_endpoint", "version", alloc);
        curl.AddMember("timeout_sec", 30, alloc);
        curl.AddMember("verify_ssl", true, alloc);
        curl.AddMember("user_agent", "VFPC2Plugin/1.0", alloc);

        rapidjson::Value headers(rapidjson::kObjectType);
        headers.AddMember("Accept", "application/json", alloc);
        headers.AddMember("Content-Type", "application/json", alloc);

        curl.AddMember("default_headers", headers, alloc);

        doc.AddMember("curl", curl, alloc);

        // -------------------------------
        // colours section
        // -------------------------------
        rapidjson::Value colours(rapidjson::kObjectType);

        auto make_colour = [&](int r, int g, int b) {
            rapidjson::Value c(rapidjson::kObjectType);
            c.AddMember("r", r, alloc);
            c.AddMember("g", g, alloc);
            c.AddMember("b", b, alloc);
            return c;
            };

        colours.AddMember("red", make_colour(190, 0, 0), alloc);
        colours.AddMember("green", make_colour(0, 190, 0), alloc);
        colours.AddMember("yellow", make_colour(255, 165, 0), alloc);

        doc.AddMember("colours", colours, alloc);

        // -------------------------------
        // Write pretty JSON to disk
        // -------------------------------
        std::ofstream ofs(filename);
        if (!ofs.is_open()) {
            log_error("Failed to create default config file: " + filename);
            return false;
        }

        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        writer.SetIndent(' ', 2);

        doc.Accept(writer);

        ofs << buffer.GetString();
        ofs.close();

        log_error("Default config file created: " + filename);
        return true;
    }

    const rapidjson::Document& get() const {
        return config_doc_;
    }

    //--------------------------------
    // SPDLOG helpers
    //--------------------------------
    std::string LogDirectory() const {
        return get_string_("spdlog", "log_directory");
	}

    std::string LogFilename() const {
        return get_string_("spdlog", "log_filename");
	}

    int MaxFiles() const {
        return get_int_("spdlog", "max_files");
	}

    int FlushIntervalSec() const {
		return get_int_("spdlog", "flush_interval_sec");
	}

	std::string Pattern() const {
		return get_string_("spdlog", "pattern");
	}

	std::string Level() const {
		return get_string_("spdlog", "level");
	}

    std::string FlushLevel() const {
        return get_string_("spdlog", "flush_level");
    }

	bool Truncate() const {
		return get_bool_("spdlog", "truncate");
	}

    // -------------------------------
    // CURL helpers
    // -------------------------------

    std::string BaseUrl() const {
        return get_string_("curl", "base_url");
    }

    std::string AirportEndpoint() const {
        return get_string_("curl", "airport_endpoint");
    }

    std::string VersionEndpoint() const {
        return get_string_("curl", "version_endpoint");
    }

    int CurlTimeoutSec() const {
        return get_int_("curl", "timeout_sec");
    }

    bool CurlVerifySSL() const {
        return get_bool_("curl", "verify_ssl");
    }

    std::string CurlUserAgent() const {
        return get_string_("curl", "user_agent");
    }

    std::string DefaultHeader(const std::string& key) const {
        const auto& curl = config_doc_["curl"];
        const auto& headers = curl["default_headers"];

        if (!headers.HasMember(key.c_str()) || !headers[key.c_str()].IsString())
            return {};

        return headers[key.c_str()].GetString();
    }

    // -------------------------------
    // URL builders
    // -------------------------------

    std::string AirportUrl(const std::string& icao) const {
        return join_url_(BaseUrl(), AirportEndpoint()) + icao;
    }

    std::string VersionUrl() const {
        return join_url_(BaseUrl(), VersionEndpoint());
    }

    // -------------------------------
	// Colour helpers
	// -------------------------------

    struct ColourRGB {
        int r{0},
            g{0},
            b{0};
    };

    ColourRGB GetColour(const std::string& name) const {
        const auto& colours = config_doc_["colours"];
        const auto& c = colours[name.c_str()];

        return {
            c["r"].GetInt(),
            c["g"].GetInt(),
            c["b"].GetInt()
        };
    }

    ColourRGB Red() const { return GetColour("red"); }
    ColourRGB Green() const { return GetColour("green"); }
    ColourRGB Yellow() const { return GetColour("yellow"); }

    COLORREF ToColorRef(const ColourRGB& c) const {
        return RGB(c.r, c.g, c.b);
    }

private:
    enum class FieldType { String, Integer, Boolean };

    struct FieldDefinition {
        std::string name;
        FieldType type;
        bool optional_with_default;
        std::string default_value; // stored as string, parsed when injecting
    };

    rapidjson::Document config_doc_;

    void log_error(const std::string& message) const {
        OutputDebugStringA((message + "\n").c_str());
    }

    // --- helpers ---
    static bool has_object_member_(const rapidjson::Value& obj, const char* key) {
        return obj.IsObject() && obj.HasMember(key) && obj[key].IsObject();
    }

    static bool has_object_member_(const rapidjson::Document& obj, const char* key) {
        return obj.IsObject() && obj.HasMember(key) && obj[key].IsObject();
    }

    bool validate_section(const char* section, const std::vector<FieldDefinition>& fields) {
        if (!config_doc_.HasMember(section) || !config_doc_[section].IsObject()) {
            log_error(std::string("Missing or invalid section: ") + section);
            return false;
        }

        rapidjson::Value& sec = config_doc_[section];
        auto& alloc = config_doc_.GetAllocator();

        for (const auto& field : fields) {
            const char* key = field.name.c_str();

            if (!sec.HasMember(key)) {
                if (field.optional_with_default) {
                    log_error("Optional field '" + field.name + "' missing in section '" +
                        section + "', applying default: " + field.default_value);

                    // Create the member and set default
                    rapidjson::Value k;
                    k.SetString(key, alloc);

                    rapidjson::Value v;
                    inject_default(v, field.type, field.default_value, alloc);

                    sec.AddMember(k, v, alloc);
                    continue;
                }
                else {
                    log_error("Missing required key '" + field.name + "' in section '" + section + "'");
                    return false;
                }
            }

            if (!validate_field(sec, key, field.type, section)) return false;
        }

        return true;
    }

    static void inject_default(rapidjson::Value& target,
        FieldType type,
        const std::string& value,
        rapidjson::Document::AllocatorType& alloc)
    {
        switch (type) {
        case FieldType::String:
            target.SetString(value.c_str(), static_cast<rapidjson::SizeType>(value.size()), alloc);
            break;

        case FieldType::Integer: {
            int v = 0;
            try { v = std::stoi(value); }
            catch (...) { v = 0; }
            target.SetInt(v);
            break;
        }

        case FieldType::Boolean: {
            const bool b = (value == "true" || value == "1" || value == "TRUE" || value == "True");
            target.SetBool(b);
            break;
        }
        }
    }

    bool validate_field(const rapidjson::Value& obj,
        const char* key,
        FieldType expected_type,
        const std::string& context) const
    {
        if (!obj.IsObject() || !obj.HasMember(key)) {
            log_error("Missing key '" + std::string(key) + "' in section '" + context + "'");
            return false;
        }

        const rapidjson::Value& v = obj[key];

        bool type_ok = false;
        switch (expected_type) {
        case FieldType::String:  type_ok = v.IsString(); break;
        case FieldType::Integer: type_ok = v.IsInt();    break;  // or v.IsInt64() if you prefer
        case FieldType::Boolean: type_ok = v.IsBool();   break;
        }

        if (!type_ok) {
            log_error("Invalid type for key '" + std::string(key) + "' in section '" + context + "'");
            return false;
        }

        return true;
    }

    bool validate_int_range(const rapidjson::Value& obj,
        const char* key,
        int minv, int maxv,
        const std::string& context) const
    {
        if (!validate_field(obj, key, FieldType::Integer, context)) return false;
        const int v = obj[key].GetInt();
        if (v < minv || v > maxv) {
            log_error("Out of range '" + std::string(key) + "' in '" + context +
                "'. Expected " + std::to_string(minv) + ".." + std::to_string(maxv));
            return false;
        }
        return true;
    }

    std::string get_string_(const char* section, const char* key) const {
        return config_doc_[section][key].GetString();
    }

    int get_int_(const char* section, const char* key) const {
        return config_doc_[section][key].GetInt();
    }

    bool get_bool_(const char* section, const char* key) const {
        return config_doc_[section][key].GetBool();
    }

    // Safe slash joiner
    static std::string join_url_(const std::string& base, const std::string& endpoint) {
        if (base.empty()) return endpoint;
        if (endpoint.empty()) return base;

        bool base_slash = base.back() == '/';
        bool ep_slash = endpoint.front() == '/';

        if (base_slash && ep_slash)
            return base + endpoint.substr(1);
        if (!base_slash && !ep_slash)
            return base + "/" + endpoint;

        return base + endpoint;
    }
};
#endif // JSONCONFIGLOADER_H