#include <iostream>
#include "main.hpp"

int versionOp(set<string>&, vector<string>&) {
    cout << "MSERMAN - Minecraft SERver MANager by Loweder\n"
            "Version: " << MSERMAN_VERSION << "\n";
    return 0;
}
int helpOp(set<string> &options, vector<string> &arguments) {
    //TODO: Add reset/delete/edit/schedule functionality, implement no-report, no-hash-gen, simulate
    //  Add "no comment gen", compact config write (1 long lists on same line). ClassNotFoundException processing in simulation
    //  Make MODEL in packs optional
    //FIXME:  Add option for "allow absolute paths" in verification, currently unsafe
    cerr << "Minecraft SERver MANager - Utility for easy server managements"
            "Usage: mserman [options...] [--] <command>\n"
            "Commands:\n"
            "    switch <version>                   Switch to specified Minecraft version\n"
            "    boot <server>                      Start Minecraft server\n"
            //"    schedule <server> <time>           Schedule Minecraft server\n"
            //"    edit <path> <value>                Edit data in config\n"
            "    sort [raw|wide]                    Sort data in config\n"
            "    [-ms] verify                       Verify data in config\n"
            //"    [-y] reset [hard]                  Clean data in config (CAUTION! Dangerous)\n"
            //"    diversify <type> <name>            Remove version requirements from pack/family/entry dependencies\n"
            "    collect <server>                   Collect user mods in archive\n"
            "    import <server> <version> <path>   Import server\n"
            "    make <server> <version> <core>     Create server\n"
            //"    [-y] delete <server>               Delete server (CAUTION! Dangerous)\n"
            "    [-yd] backup (save|load) <server>  Load/backup world (CAUTION! Dangerous)\n"
            "    help                               Same as --help or -h\n"
            "    version                            Same as --version or -v\n"
            "Options:\n"
            "    -h    --help                       Display this window\n"
            "    -v    --version                    Display version\n"
            "          --force-root                 Forcefully use current directory as root\n"
//            "    -a    --all                        Select all\n"
//            "    -s    --simulate                   Simulate server with every mod/plugin to get dependencies/ids\n"
            "    -m    --minecraft                  Only link in Minecraft folder\n"
//            "    -n    --no-report                  Do not generate verification report\n"
            "    -d    --deep                       Fully archive server\n"
            "    -y    --force-yes                  Do not ask for confirmation (CAUTION! Dangerous)\n";
//            "    -g    --ignore-hash-gen            Do not verify HASH-GEN value. Only use this if you know what you're doing\n";
    return arguments.empty() && !options.contains("functional-help") ? EXIT_FAILURE : 0;
}
int exitOp(set<string>&, vector<string>&) {
    //TODO exit from interactive console
    return 0;
}
int nullOp(set<string>&, vector<string>&) {
    exitWithUsage();
}