#ifndef	UTILS_H
#define UTILS_H

#include <string>
#include <algorithm>

inline std::string to_upper(std::string s) {
	std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return result;
}


inline void trim(std::string& s) {
    auto not_space = [](unsigned char ch) {return !std::isspace(ch); };

    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
}

// Trim + uppercase
/*inline std::string norm_icao(std::string s) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}
*/

// Trim + uppercase
inline std::string normalize_callsign(std::string s) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}


inline std::string norm_icao(const std::string& s) {
    
    return normalize_callsign(s);
}

// "Looks like an ICAO" = exactly 4 letters A–Z
inline bool IsValidIcao(const std::string& s) {
    if (s.size() != 4) return false;
    return std::all_of(s.begin(), s.end(),
        [](unsigned char c) { return std::isupper(c) && std::isalpha(c); });
}

// Parse "EGPH_TWR" -> "EGPH" (validated); returns "" if not valid
inline std::string ExtractValidIcaoFromPosition(const std::string& callsign_with_suffix) {
    std::string u = to_upper(callsign_with_suffix);
    const auto us = u.find('_');
    if (us == std::string::npos || us == 0) return {};
    std::string icao = norm_icao(u.substr(0, us));
    return IsValidIcao(icao) ? icao : std::string{};
}


#endif //UTILS_H
