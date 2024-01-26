#include <iostream>
#include <sys/wait.h>
#include <fstream>
#include <zip.h>
#include "main.hpp"


int switchOp(set<string> &, vector<string> &arguments) {
	/*if (arguments.size() != 2) exitWithUsage();
	root_pack root;
	if (!parseConfig(root)) printError("HASH-GEN is outdated, please run \"mserman verify\" again");
	if (!root.root("VERSIONS")(arguments[1])) printError("Version with name \"" + arguments[1] + "\" doesn't exist");
	root.root["GENERAL"]["MINECRAFT"]["VERSION"].value = arguments[1];
	cout << "Please, run \"mserman --minecraft verify\" to finish switching\n";
	flushConfig(root);*/
	return 0;
}
int makeOp(set<string> &, vector<string> &arguments) {
	/*if (arguments.size() != 4) exitWithUsage();
	root_pack root;
	if (!parseConfig(root)) printError("HASH-GEN is outdated, please run \"mserman verify\" again");
	if (root.root("SERVERS")(arguments[1])) printError("Server with name \"" + arguments[1] + "\" already exists");
	if (!root.root("VERSIONS")(arguments[2])) printError("Version with name \"" + arguments[2] + "\" doesn't exist");
	if (!root.root("CORES")(arguments[3])) printError("Core with name \"" + arguments[3] + "\" doesn't exist");
	root.root["SERVERS"][arguments[1]]["VERSION"].value = arguments[2];
	root.root["SERVERS"][arguments[1]]["CORE"].value = arguments[3];
	cout << "Please, run \"mserman verify\" to finish creation\n";
	flushConfig(root);*/
	return 0;
}
int sortOp(set<string> &, vector<string> &arguments) {
	/*if (arguments.size() != 2 && arguments.size() != 1) exitWithUsage();
	if (arguments.size() == 2 && arguments[1] != "raw" && arguments[1] != "wide") exitWithUsage();
	root_pack root;
	parseConfig(root);
	bool raw = false, compact = false;
	if (arguments.size() == 2)
		raw = arguments[1] == "raw",  compact = arguments[1] == "wide";
	flushConfig(root, false, !raw, !compact);*/
	return 0;
}
int collectOp(set<string> &, vector<string> &arguments) {
	/*if (arguments.size() != 2) exitWithUsage();
	root_pack root;
	if (!parseConfig(root)) printError("HASH-GEN is outdated, please run \"mserman verify\" again");
	if (!root.root("SERVERS")(arguments[1])) printError("Server with name \"" + arguments[1] + "\" doesn't exist");
	if (!root.cache("SERVERS")(arguments[1])("VALID")) printError("Server with name \"" + arguments[1] + "\" is invalid");
	string_node &rootServer = root.root["SERVERS"][arguments[1]];
	string_node &rootCore = root.root["CORES"][rootServer["CORE"]];
	if (!(rootCore["SUPPORT"].asCode(3, 0b000) & (SUPPORT_FORGE | SUPPORT_FABRIC)))
		printError("Server with name \"" + arguments[1] + "\" doesn't use mods");
	zip_t *archive = zip_open((arguments[1] + "_user.zip").c_str(), ZIP_CREATE | ZIP_TRUNCATE, nullptr);
	if (!archive) printError("Was unable to make archive");
	path_node &dirModules = root.paths["GENERAL"]["entries"];
	path_node &dirConfig = root.paths["GENERAL"]["config"][rootServer["META-CONFIG"]];
	function<void(const fs::path&)> recFunc = [&archive, &dirConfig, &recFunc](const fs::path &dir){
		for (const auto &item2: fs::directory_iterator(dir)) {
			if (item2.is_regular_file()) {
				zip_source_t *source = zip_source_file(archive, item2.path().c_str(), 0, 0);
				if (!source) printError("Was unable to open file as zip source");
				zip_file_add(archive, ("config" / item2.path().lexically_relative(dirConfig.value)).c_str(), source, ZIP_FL_OVERWRITE);
			} else if (item2.is_directory()) {
				zip_dir_add(archive, ("config" / item2.path().lexically_relative(dirConfig.value)).c_str(), 0);
				recFunc(item2.path());
			}
		}
	};
	zip_dir_add(archive, "config", 0);
	recFunc(dirConfig.value);
	zip_dir_add(archive, "mods", 0);

	for (auto &[pack, mc_version]: rootServer["PACKS"]) {
		//TODO obvious
		//        if (!root.stats("VALID")("PACKS")(pack)) {
		//            cout << "Pack \"" << pack << "\" not valid\n";
		//            root.stats["LOST"]["PACKS"][pack]["BY-USER"]++;
		//            continue;
		//        }
		set<pair<string, string>> mods = collectPack(root, root.root["PACKS"][pack], mc_version.value, SIDE_CLIENT);
		for (auto &[module, version]: mods) {
			string realName = root.root["ENTRIES"][module][mc_version][version].value;
			path_node &dirMods = dirModules[mc_version];
			fs::path file = dirMods.value / realName;
			if (!is_regular_file(file)) printError("One of mods is not a regular file");
			string filePath = file.string();

			zip_source *source = zip_source_file(archive, filePath.c_str(), 0, 0);
			if (!source) printError("Was unable to open mod file as zip source");

			zip_file_add(archive, ("mods/" + realName).c_str(), source, ZIP_FL_OVERWRITE);
		}
	}
	flushConfig(root);
	if (zip_close(archive) == -1) printError("Was unable to save archive, error code: " + to_string(zip_get_error(archive)->zip_err));*/
	return 0;
}
int backupOp(set<string> &options, vector<string> &arguments) {
	/*if (arguments.size() != 3) exitWithUsage();
	//TODO: Implement 'all at once' (--all) support
	root_pack root;
	if (!parseConfig(root)) printError("HASH-GEN is outdated, please run \"mserman verify\" again");
	string &action = arguments[1];
	string &server = arguments[2];
	bool deep = options.contains("deep");
	if (!(deep && action == "load") && !root.root("SERVERS")(server)) printError("Server with name \"" + server + "\" doesn't exist");
	if (!options.contains("force-yes"))
		while (true) {
			char choice;
			cout << "Do you want to proceed? (y/n): ";
			cin >> choice;
			if (choice == 'y' || choice == 'Y') break;
			if (choice == 'n' || choice == 'N') printError("Aborted");
			cout << "Invalid choice. Please enter 'y' or 'n'.\n";
		}
	path_node &pathArchive = ensureExists(root.paths["BACKUP"][deep ? "archive" : "server"])[server + ".zip"];
	path_node &dirServer = root.paths["GENERAL"]["server"][server];
	const string pathNormal = deep ? "files/" : "";
	if (action == "save") {
		zip_t *archive = zip_open(pathArchive.value.c_str(), ZIP_CREATE | ZIP_TRUNCATE, nullptr);
		function<void(const fs::path&)> recFunc = [&archive, &dirServer, &recFunc, &pathNormal](const fs::path &entry){
			if (is_regular_file(entry)) {
				zip_source_t *source = zip_source_file(archive, entry.c_str(), 0, 0);
				if (!source) printError("Was unable to open file as zip source");
				zip_file_add(archive,(pathNormal + entry.lexically_relative(dirServer.value).string()).c_str(), source, ZIP_FL_OVERWRITE);
			} else if (is_directory(entry)) {
				zip_dir_add(archive, (pathNormal + entry.lexically_relative(dirServer.value).string()).c_str(), 0);
				for (const auto &entry1: fs::directory_iterator(entry)) {
					recFunc(entry1.path());
				}
			}
		};
		if (deep) {
			zip_dir_add(archive, "files", 0);
			string_node &rootServer = root.root["SERVERS"][server];
			string_node config;

			config["SERVER"][server] = rootServer;
			config["META-CONFIG"][rootServer["META-CONFIG"]] = root.root["META-CONFIGS"][rootServer["META-CONFIG"]];
			for (auto &[pack, version] : rootServer["PACKS"]) {
				config["PACKS"][pack] = root.root["PACKS"][pack];
				for (auto &[family, mode] : root.root["PACKS"][pack]["FAMILIES"]) {
					config["FAMILIES"][family] = root.root["FAMILIES"][family];
				}
			}
			ostringstream src;
			flushConfig(config, src, false, false);
			char *dt = new char[src.str().size()];
			strcpy(dt, src.str().c_str());
			zip_source_t *source = zip_source_buffer_create(dt, src.str().size(), 1, nullptr);
			zip_obuf buffer{source};
			zip_file_add(archive, "special/meta.lwcn", source, ZIP_FL_OVERWRITE);
		}
		for (const auto &entry: fs::directory_iterator(dirServer.value)) {
			string entry_name = entry.path().filename().string();
			if ((deep && !entry.is_symlink()) || (!deep && entry.is_directory())) {
				if ((deep && (entry_name == "mods" || entry_name == "plugins" || entry_name == "config"))
						|| (!deep && !entry_name.starts_with("world"))) continue;
				recFunc(entry.path());
			}
		}
		if (zip_get_num_entries(archive, 0) == 0) printError("No world files found");
		if (zip_close(archive) == -1) printError("Was unable to save archive, error code: " + to_string(zip_get_error(archive)->zip_err));
	} else if (action == "load") {
		if (!is_regular_file(pathArchive.value))
			printError("Server '" + arguments[2] + "' doesn't have a backup");
		zip_t *archive = zip_open(pathArchive.value.c_str(), ZIP_RDONLY, nullptr);
		if (!archive) printError("Failed to open archive");
		root.paths["TEMP"] = path_node(fs::temp_directory_path());
		fs::remove_all(root.paths["TEMP"]["mserman"].value);
		ensureExists(root.paths["TEMP"]["mserman"]);
		zip_stat_t stat;
		for (int i = 0; i < zip_get_num_entries(archive, 0); ++i) {
			zip_stat_init(&stat);
			if (!zip_stat_index(archive, i, 0, &stat)) {
				string stat_name = stat.name;
				if (!deep || stat_name.starts_with("files/")) {
					if (deep) stat_name = stat_name.substr(6);
					if(string(stat.name).ends_with('/')) {
						fs::create_directories(root.paths["TEMP"]["mserman"].value / stat_name);
						continue;
					}
					zip_file* file = zip_fopen_index(archive, i, 0);
					if (!file) continue;
					ofstream dest(root.paths["TEMP"]["mserman"].value / stat_name, std::ios::binary);
					char buffer[4096];
					zip_int64_t bytesRead;
					while ((bytesRead = zip_fread(file, buffer, sizeof(buffer))) > 0)
						dest.write(buffer, bytesRead);
					zip_fclose(file);
				} else if (deep) {
					if (stat_name == "special/meta.lwcn") {
						//TODO Later
					}
				}
			}
		}
		zip_discard(archive);
		for (auto &entry : fs::directory_iterator(root.paths["TEMP"]["mserman"].value)) {
			string fileName = entry.path().lexically_relative(root.paths["TEMP"]["mserman"].value);
			fs::remove_all(dirServer.value / fileName);
			fs::rename(entry, dirServer.value / fileName);
		}
		fs::remove_all(root.paths["TEMP"]["mserman"].value);
	} else exitWithUsage();
	flushConfig(root);*/
	return 0;
}
int bootOp(set<string> &, vector<string> &arguments) {
	/*if (arguments.size() != 2) exitWithUsage();
	root_pack root;
	if (!parseConfig(root)) printError("HASH-GEN is outdated, please run \"mserman verify\" again");
	if (!root.root("SERVERS")(arguments[1])) printError("Server with name \"" + arguments[1] + "\" doesn't exist");
	if (!root.cache("SERVERS")(arguments[1])("VALID")) printError("Server with name \"" + arguments[1] + "\" is invalid");
	if (pid_t pid = forkToServer(root, arguments[1], 0, 1, 2); pid != -1 && pid != 0) {
		int status;
		waitpid(pid, &status, 0);
		if (WIFEXITED(status)) cout << "Server process exited with status " << WEXITSTATUS(status) << endl;
	} else {
		cerr << "Failed to start server.\n";
		return 1;
	}
	flushConfig(root);*/
	return 0;
}
