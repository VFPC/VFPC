#ifndef VERSIONINFO_H
#define VERSIONINFO_H

//==============================================================
// VFPC shared version / product metadata
// Single source of truth for code + resource.rc
//==============================================================

// -------------------------
// Numeric version parts
// -------------------------
#define VFPC_VERSION_MAJOR 3
#define VFPC_VERSION_MINOR 7
#define VFPC_VERSION_PATCH 2
#define VFPC_VERSION_BUILD 0

// Required by FILEVERSION / PRODUCTVERSION in resource.rc
#define VFPC_VERSION_NUM \
    VFPC_VERSION_MAJOR, VFPC_VERSION_MINOR, VFPC_VERSION_PATCH, VFPC_VERSION_BUILD

// -------------------------
// String helpers
// -------------------------
#define VFPC_STRINGIZE_DETAIL(x) #x
#define VFPC_STRINGIZE(x) VFPC_STRINGIZE_DETAIL(x)

// 3-part version: "3.7.2"
#define VFPC_VERSION_STR \
    VFPC_STRINGIZE(VFPC_VERSION_MAJOR) "." \
    VFPC_STRINGIZE(VFPC_VERSION_MINOR) "." \
    VFPC_STRINGIZE(VFPC_VERSION_PATCH)

// 4-part version: "3.7.0.0"
#define VFPC_VERSION_STR_FULL \
    VFPC_STRINGIZE(VFPC_VERSION_MAJOR) "." \
    VFPC_STRINGIZE(VFPC_VERSION_MINOR) "." \
    VFPC_STRINGIZE(VFPC_VERSION_PATCH) "." \
    VFPC_STRINGIZE(VFPC_VERSION_BUILD)

// -------------------------
// Product metadata
// -------------------------
#define VFPC_PLUGIN_NAME         "VFPC"
#define VFPC_PRODUCT_NAME        "VFPC"
#define VFPC_FILE_DESCRIPTION    "VFPC EuroScope Plugin"
#define VFPC_INTERNAL_NAME       "VFPC"
#define VFPC_ORIGINAL_FILENAME   "VFPC.dll"
#define VFPC_COMPANY_NAME        "Peter Richardson"
#define VFPC_COPYRIGHT_TEXT      "Copyright (C) 2026 Peter Richardson"


#endif // VERSIONINFO_H
