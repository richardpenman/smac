// The arguments of the `call` setup command (harness/README.md "Setup commands"), parsed the same
// way by harness/oracle/oracle.cpp and src/app/headless.cpp.
#pragma once
#include <cstdio>
#include <cstring>
#include <string>

// `call <name> <args>`: the function's name and up to 8 integers (%i: decimal, 0x hex). The token
// `$veh` stands for the vehicle id the last `call veh_init` returned (`last_veh`, -1 before any):
// a script acts on the unit it just created without counting ids (killing a vehicle renumbers the
// ones above it). Returns sscanf's count (the name included).
inline int script_call_parse(const char* args, int last_veh, char (&name)[64], int (&a)[8]) {
    std::string s;
    for (const char* p = args; *p;) {
        if (!std::strncmp(p, "$veh", 4) && (p[4] == 0 || p[4] == ' ' || p[4] == '\t')) {
            s += std::to_string(last_veh);
            p += 4;
        } else {
            s += *p++;
        }
    }
    return std::sscanf(s.c_str(), "%63s %i %i %i %i %i %i %i %i", name, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5],
                       &a[6], &a[7]);
}
