#include "main.hpp"

_root_type root;
vector<string> cwd;

int main(int argc, const char** argv) {
	set<string> options;
	vector<string> arguments;
	bool readingOptions = true;
	for (int i = 1; i < argc; ++i) {
		string arg = argv[i];
		if (!readingOptions){
			arguments.push_back(arg);
		} else if (arg.starts_with("--")) {
			if (arg == "--") readingOptions = false;
			else options.emplace(mapOption(arg));
		} else if (arg.starts_with('-')) {
			for (size_t j = 1; j < arg.size(); ++j)
				options.emplace(mapOption(string(1, arg[j])));
		} else {
			readingOptions = false;
			arguments.push_back(arg);
		}
	}
	if (!options.contains("force-root") && !fs::is_regular_file("mserman.local")) {
		string_node global_config;
		fs::path homeDir = fs::path(getenv("HOME"));
		fs::path appDir = homeDir / ".config/mserman.global";
		parseConfig(global_config, appDir, false);
		if (fs::is_directory(global_config["DEFAULT-DIR"].value)) {
			fs::current_path(global_config["DEFAULT-DIR"].value);
		}
		flushConfig(global_config, appDir, false);
	}
	operation toDo = helpOp;
	for (const auto &item: options)
		if (item.starts_with("functional-") && operationMap.contains(item.substr(11))) {
			toDo = operationMap.at(item.substr(11));
			break;
		}
	if (toDo == helpOp) {
		if (arguments.empty()) toDo = helpOp;
		else if (operationMap.contains(arguments[0]))
			toDo = operationMap.at(arguments[0]);
	}
	parseConfig();
	if (!root.valid) return 1;
	int result = toDo(options, arguments);
	if (result & 0x100) flushConfig(result & 0x200);
	if (!root.valid) return 1;
	return result & 0xFF;
}
