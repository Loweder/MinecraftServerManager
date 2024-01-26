#include "main.hpp"
#include <filesystem>
#include <iostream>
#include <sys/wait.h>
#include <regex>

struct module_entry{
	string id;
	string version;
	enum match : char {
		less_exact = '-',
		greater_exact = '+',
		less = '<',
		exact = '=',
		greater = '>',
		approx = 'A',
		not_exact = '^',
	};
	map<string, pair<string, match>> dependency;
	string model;
};

//Module info file names
const string forgeEntry = "mcmod.info";
const string fabricEntry = "fabric.mod.json";
const string bukkitEntry = "plugin.yml";

/**Get related modules from file
 *
 * @param file input file
 * @return list of found modules
 */
vector<module_entry> lookFile(const fs::path &file) {
	zip_t *jar = zip_open(file.c_str(), 0, nullptr);
	vector<module_entry> result{};
	if (!jar) return result;
	int64_t num = zip_get_num_entries(jar, 0);
	zip_stat_t stat;
	for (int i = 0; i < num; ++i) {
		zip_stat_init(&stat);
		if (!zip_stat_index(jar, i, 0, &stat)) {
			if (forgeEntry != stat.name && fabricEntry != stat.name && bukkitEntry != stat.name) continue;
			zip_file_t *subFile = zip_fopen_index(jar, i, 0);
			if (!subFile) continue;
			zip_ibuf buffer{subFile};
			istream data{&buffer};
			if (bukkitEntry == stat.name) {
				string line;
				string id, version;
				vector<string> dependItems;
				for (bool inDepend = false; getline(data, line);) {
					if (line.starts_with("name: "))
						id = line.substr(6);
					else if (line.starts_with("version: "))
						version = line.substr(9);
					else if (line.starts_with("depend: [")) {
						line = line.substr(9, line.find(']') - 9);
						while (!line.empty()) {
							size_t end = line.find(',');
							string part = line.substr(0, end);
							size_t tokenStart = part.find_first_not_of(" \t\n\r");
							size_t tokenEnd = part.find_last_not_of(" \t\n\r");

							if (tokenStart != std::string::npos && tokenEnd != std::string::npos)
								dependItems.push_back(part.substr(tokenStart, tokenEnd - tokenStart + 1));
							line.erase(0, end == string::npos ? end : end + 1);
						}
						continue;
					} else if (line.starts_with("depend: ")) {
						inDepend = true;
						continue;
					}

					if (inDepend && line.find('-') != std::string::npos) {
						dependItems.emplace_back(line.substr(line.find('-') + 2));
					} else if (inDepend) {
						inDepend = false;
					}
				}
				if (id.empty() || version.empty()) continue;
				module_entry modEntry;
				modEntry.model = "PLUGIN";
				modEntry.id = id;
				transform(modEntry.id.begin(), modEntry.id.end(), modEntry.id.begin(), ::tolower);
				modEntry.version = version;
				for (auto &item: dependItems) {
					transform(item.begin(), item.end(), item.begin(), ::tolower);
					modEntry.dependency.emplace(item, pair{"", module_entry::approx});
				}
				result.emplace_back(modEntry);
			} else {
				string_node info;
				parseJson(info, data);
				if (forgeEntry == stat.name) {
					if (info("modListVersion"))
						info = info["modList"];
					for (auto &[index, mod]: info) {
						if (!mod("modid") || !mod("version")) continue;
						module_entry modEntry;
						modEntry.model = "FORGE";
						modEntry.id = mod["modid"].value;
						transform(modEntry.id.begin(), modEntry.id.end(), modEntry.id.begin(), ::tolower);
						modEntry.version = mod["version"].value;
						if (mod("dependencies")) {
							for (auto &[ignored, depRoot]: mod["dependencies"]) {
								string dep = depRoot.value, rem;
								if (dep.empty()) continue;
restart:
								uint64_t versionIndex = dep.find('@');
								uint64_t commaIndex = dep.find(',');
								if (commaIndex != string::npos) {
									while (count(dep.begin(), dep.begin() + (long) commaIndex, '[') > count(dep.begin(), dep.begin() + (long) commaIndex, ']')) {
										commaIndex = dep.find(',', commaIndex + 1);
										if (commaIndex == string::npos) break;
									}
									if (commaIndex != string::npos) {
										rem = dep.substr(commaIndex + 1);
										dep = dep.substr(0, commaIndex);
									}
								}
								string id;
								module_entry::match versionType = module_entry::approx;
								string version{};
								if (versionIndex == string::npos) {
									id = dep;
								} else {
									id = dep.substr(0, versionIndex);
									version = dep.substr(versionIndex+1);
									if (version.ends_with("*")) {
										versionType = module_entry::approx;
										version = version.substr(0, max(version.size() - 2, 0UL));
										if (version.starts_with('[')) version = version.substr(1, version.size() - 2);
									} else {
										if (version.starts_with('[')) version = version.substr(1, version.size() - 2);
										if (version.contains(',')) {
											versionType = module_entry::greater_exact;
											version = version.substr(0, version.find(','));
										} else {
											versionType = module_entry::exact;
										}
									}
								}
								transform(id.begin(), id.end(), id.begin(), ::tolower);
								modEntry.dependency.emplace(id, pair{version, versionType});
								if (commaIndex != string::npos) {
									dep = rem;
									goto restart;
								}
							}
						}
						result.emplace_back(modEntry);
					}
				} else if (fabricEntry == stat.name) {
					if (!info("id") || !info("version")) continue;
					module_entry modEntry;
					modEntry.model = "FABRIC";
					modEntry.id = info["id"].value;
					transform(modEntry.id.begin(), modEntry.id.end(), modEntry.id.begin(), ::tolower);
					modEntry.version = info["version"].value;
					if (info("depends")) {
						for (auto &[dep, versionRoot]: info["depends"]) {
							string id = dep;
							string version = versionRoot.value;
							transform(id.begin(), id.end(), id.begin(), ::tolower);
							const regex versionRegex(R"((<=|<|>=|>|=|\^)?\s*([\d\*]+(?:\.[^.*x\n]+)*)(\.[\*x])?)");
							smatch match;

							module_entry::match versionType = module_entry::approx;
							if (regex_match(version, match, versionRegex)) {
								string logicSymbol = match[1].str();
								string realVersion = match[2].str();

								if (realVersion == "*" || realVersion.empty()) {
									version = "";
									versionType = module_entry::approx;
								} else if (!match[3].str().empty()) {
									version = realVersion;
									versionType = module_entry::approx;
								} else if (logicSymbol == "^") {
									version = realVersion;
									versionType = module_entry::not_exact;
								} else if (logicSymbol == "<") {
									version = realVersion;
									versionType = module_entry::less;
								} else if (logicSymbol == "<=") {
									version = realVersion;
									versionType = module_entry::less_exact;
								} else if (logicSymbol == ">") {
									version = realVersion;
									versionType = module_entry::greater;
								} else if (logicSymbol == ">=") {
									version = realVersion;
									versionType = module_entry::greater_exact;
								} else {
									version = realVersion;
									versionType = module_entry::exact;
								}
							} else {
								version = "";
							}
							modEntry.dependency.emplace(id, pair{version, versionType});
						}
					}
					result.emplace_back(modEntry);
				}
			}
			zip_fclose(subFile);
			break;
		}
	}
	zip_discard(jar);
	return result;
}
/**Get related modules from root
 *
 * @param root root node
 * @param filename file to look for
 * @param mc_version version to check
 * @return list of found modules
 */
vector<module_entry> lookRoot(const string &filename, const string &mc_version) {
	vector<module_entry> result;
	for (auto &[id, info]: root.root["ENTRIES"]) {
		if (!info(mc_version)) continue;
		for (auto &[versionId, fileInfo]: info[mc_version]) {
			if (fileInfo.value == filename) {
				module_entry entry;
				entry.id = id;
				entry.version = versionId;
				entry.model = fileInfo["MODEL"].value;
				result.emplace_back(entry);
			}
		}
	}
	return result;
}

bool shouldVerify(module_entry &entry, const string &mc_version) {
	if (!root.root["ENTRIES"](entry.id)(mc_version)(entry.version)("STATUS")) return true;
	return root.root["ENTRIES"][entry.id][mc_version][entry.version]["STATUS"].value != "F-VALID";
}
bool versionMatch(const string &source, const string &version, module_entry::match condition) {
	if (condition == module_entry::exact) return source == version;
	if (condition == module_entry::approx) return source.starts_with(version);
	if (condition == module_entry::not_exact) return source != version;
	strong_ordering diff = strong_ordering::equal;
	vector<string> version1;
	vector<string> version2;

	size_t start = 0, end = source.find('.');
	do {
		version1.push_back(source.substr(start, end - start));
		start = end + 1;
	} while ((end = source.find('.', start)) != std::string::npos);
	version1.push_back(source.substr(start));
	start = 0, end = source.find('.');
	do {
		version2.push_back(version.substr(start, end - start));
		start = end + 1;
	} while ((end = version.find('.', start)) != std::string::npos);
	version2.push_back(version.substr(start));


	for (size_t i = 0; i < version1.size() && i < version2.size(); ++i) {
		try {
			diff = stoi(version1[i]) <=> stoi(version2[i]);
			if (diff != strong_ordering::equal) break;
		} catch (invalid_argument&) {
			diff = version1[i] <=> version2[i];
			if (diff != strong_ordering::equal) break;
		}
	}
	if (diff == strong_ordering::equal) {
		diff = version1.size() <=> version2.size();
	}
	if (condition == module_entry::less) return diff == strong_ordering::less;
	if (condition == module_entry::less_exact) return diff != strong_ordering::greater;
	if (condition == module_entry::greater) return diff == strong_ordering::greater;
	if (condition == module_entry::greater_exact) return diff != strong_ordering::less;
	return false;
}
string findVersion(string_node &in, const string &version, module_entry::match condition) {
	string bestVersion;
	bool detected = false;
	for (auto &[lVersion, entry]: in) {
		if (versionMatch(lVersion, version, condition)) {
			if (!detected) {
				detected = true;
				bestVersion = lVersion;
			} else if (versionMatch(bestVersion, lVersion, module_entry::greater)) bestVersion = lVersion;
		}
	}
	return bestVersion;
}
string combine(const string& id, const string& version) {
	if (version.empty()) return '"' + id + '"';
	return '"' + id + '(' + version + ")\"";
}

//TODO remove those from here
pid_t forkToServer(string &server, int input, int output, int error) {
	pid_t child_pid = fork();
	if (child_pid == 0) {
		string_node &rootServer = root.root["SERVERS"][server];
		string_node &core = root.root["CORES"][rootServer["CORE"]];
		dup2(input, STDIN_FILENO);
		dup2(output, STDOUT_FILENO);
		dup2(error, STDERR_FILENO);
		const fs::path pathTo = static_cast<fs::path>(root.root["GENERAL"]["ROOT-DIR"].value) / ("server/" + server + "/");
		current_path(pathTo);
		vector<char *> args;
		args.push_back((char*)"java");
		string strArgs = rootServer["JAVA-ARGS"].value;
		size_t pos;
		while ((pos = strArgs.find(' ')) != string::npos) {
			string token = strArgs.substr(0, pos);
			args.push_back(token.data());
			strArgs.erase(0, pos + 1);
		}
		if (!strArgs.empty())
			args.push_back(strArgs.data());
		args.push_back((char*)"-jar");
		args.push_back(core["MAIN"].value.data());
		args.push_back(nullptr);
		execvp((((fs::path)core["JAVA-RUNTIME"].value) / "java").c_str(), args.data());
	}
	return child_pid;
}
set<pair<string, string>> collectPack(string_node &pack, const string &mc_version, const string &side) {
	set<pair<string, string>> entries;
	set<pair<string, string>> checked;
	function<void(const string&, const string&)> dfs = [&entries, &dfs, &checked, &side, &mc_version](const string &module, const string &version) {
		if (!root.root("ENTRIES")(module)(mc_version)) return;
		string_node &rootMod = root.root["ENTRIES"][module][mc_version];
		string depVersion = version;
		module_entry::match condition = depVersion.empty() ? module_entry::approx : (module_entry::match) depVersion[0];
		depVersion = depVersion.empty() ? "" : depVersion.substr(1);
		depVersion = findVersion(rootMod, depVersion, condition);
		if (!checked.insert({module, depVersion}).second) return;
		if (rootMod[depVersion]("SYNTHETIC")) return;
		if (side == "BOTH" || rootMod[depVersion](side)) {
			entries.emplace(module, depVersion);
			for (auto &item: rootMod[depVersion]["DEPENDENCIES"]) {
				dfs(item.first, item.second.value);
			}
		}
	};
	for (auto &entry: pack["ENTRIES"]) {
		dfs(entry.first, entry.second.value);
	}
	for (auto &[family, modes]: pack["FAMILIES"]) {
		for (auto &[mode, ignored] : modes) {
			for (auto &[mod, modVersion]: root.root["FAMILIES"][family]) {
				if (modVersion(mode))
					dfs(mod, modVersion.value);
			}		
		}
	}
	return entries;
}
void linkPacks(string_node &packs, const string &dest, const string &side) {
	map<string, set<pair<string, string>>> mods;
	map<string, set<pair<string, string>>> plugins;
	for (auto &[pack, mc_version]: packs) {
		string &packModel = root.cache["PACKS"][pack][mc_version]["MODEL"].upper();
		if (side == "CLIENT" && packModel == "PLUGIN") {
			continue;
		}
		set<pair<string, string>> modules = collectPack(root.root["PACKS"][pack], mc_version.value, side);
		if (packModel == "PLUGIN") {
			plugins[mc_version.value].insert(modules.begin(), modules.end());
		} else {
			mods[mc_version.value].insert(modules.begin(), modules.end());
		}
	}
	auto processModules = [&dest](const std::string& type, map<string, set<pair<string, string>>> &sModules) {
		if (sModules.empty()) return;
		path_node& dir = dest.empty() ? root.paths["MINECRAFT"][type + "s"] : root.paths["GENERAL"]["server"][dest][type + "s"];
		ensureExists(dir);
		for (auto& absoluteLink : fs::directory_iterator(dir.value)) {
			std::string fileName = absoluteLink.path().lexically_relative(dir.value);
			for (auto& [mc_version, modules] : sModules) {
				path_node& dirEntries = root.paths["GENERAL"]["entries"][mc_version];
				std::vector<module_entry> existent = lookRoot(fileName, mc_version);
				if (existent.empty()) continue;
				fs::path absoluteItem = absolute(dirEntries.value / fileName);
				for (auto& module : existent) {
					if (modules.contains({module.id, module.version})) {
						if (is_symlink(absoluteLink) && (!exists(absoluteLink) || (read_symlink(absoluteLink) != absoluteItem))) {
							remove(absoluteLink);
							create_symlink(absoluteItem, absoluteLink);
							cout << "Symlink to " << type << " " << combine(module.id, module.version) << " fixed\n";
						}
						goto contOuter;
					}
				}
			}
			if (is_symlink(absoluteLink)) {
				remove(absoluteLink);
				cout << "Symlink to " << type << " " << fileName << " removed\n";
			} else {
				cout << "Found unknown " << type << " " << fileName << '\n';
			}
contOuter:
			asm("nop");
		}
		for (auto &[mc_version, modules] : sModules) {
			path_node &dirEntries = root.paths["GENERAL"]["entries"][mc_version];
			for (auto &[module, moduleVersion]: modules) {
				string modName = root.root["ENTRIES"][module][mc_version][moduleVersion].value;
				fs::path absoluteItem = absolute(dirEntries.value / modName);
				fs::path absoluteLink = dir.value / modName;
				if (!exists(symlink_status(absoluteLink))) {
					create_symlink(absoluteItem, absoluteLink);
					cout << "Symlink to " << type << " " << combine(module, moduleVersion) << " created\n";
				} else if (exists(absoluteLink) && !is_symlink(absoluteLink)) {
					cout << "Failed to create symlink to " << type << " " << combine(module, moduleVersion)
						<< ", location already taken\n";
				}
			}
		}
	};
	processModules("mod", mods);
	processModules("plugin", plugins);
}
void linkConfig(const string &metaConfig, const string &dest) {
	path_node &dirSrc = root.paths["GENERAL"]["config"][metaConfig];
	path_node &dirDest = dest.empty() ? root.paths["MINECRAFT"]["config"] : root.paths["GENERAL"]["server"][dest]["config"];
	auto it = fs::recursive_directory_iterator(dirDest.value);
	for (const auto& absoluteLink : it) {
		string fileName = absoluteLink.path().lexically_relative(dirDest.value);
		fs::path absoluteItem = absolute(dirSrc.value / fileName);
		if (is_directory(symlink_status(absoluteLink))) continue;
		else if (is_regular_file(symlink_status(absoluteLink))) {
			remove(absoluteItem);
			create_directories(absoluteItem.parent_path());
			rename(absoluteLink, absoluteItem);
			create_symlink(absoluteItem, absoluteLink);
			cout << "Moved config file " << fileName << '\n';
		} else if (!exists(absoluteItem)) {
			it.disable_recursion_pending();
			remove(absoluteLink);
			cout << "Symlink to config file \"" << fileName << "\" removed\n";
		} else if (is_symlink(absoluteLink) && (!exists(absoluteLink) || (read_symlink(absoluteLink) != absoluteItem))) {
			it.disable_recursion_pending();
			remove(absoluteLink);
			create_symlink(absoluteItem, absoluteLink);
			cout << "Symlink to config file \"" << fileName << "\" fixed\n";
		}
	}
	for (const auto& absoluteItem : fs::recursive_directory_iterator(dirSrc.value)) {
		string fileName = absoluteItem.path().lexically_relative(dirSrc.value);
		fs::path absoluteLink = dirDest.value / fileName;
		if (!is_regular_file(absoluteItem)) continue;
		if (!exists(symlink_status(absoluteLink))) {
			create_directories(absoluteLink.parent_path());
			create_symlink(absolute(absoluteItem), absoluteLink);
			cout << "Symlink to config file \"" << fileName << "\" created\n";
		} else if (exists(symlink_status(absoluteLink)) && !is_symlink(absoluteLink)) {
			cout << "Failed to create symlink to config file \"" << fileName << "\", location already taken\n";
		}
	}
}
void linkDirFilter(path_node &src, path_node &dest, const vector<string> filter, bool movingFilter) {
	for (const auto& absoluteLink : fs::directory_iterator(dest.value)) {
		string fileName = absoluteLink.path().lexically_relative(dest.value);
		bool match = any_of(filter.begin(), filter.end(), [&fileName](const auto &val) -> bool {
				return fileName.starts_with(val);	
			});
		fs::path absoluteItem = src.value / fileName;
		if (is_directory(symlink_status(absoluteLink))) continue;
		else if (!is_symlink(absoluteLink)) {
			if (!match || !movingFilter) continue;
			remove(absoluteItem);
			rename(absoluteLink, absoluteItem);
			create_symlink(absoluteItem, absoluteLink);
			cout << "Moved file " << fileName << '\n';
		} else if (!exists(absoluteItem) || !match) {
			remove(absoluteLink);
			cout << "Symlink to file \"" << fileName << "\" removed\n";
		} else if (is_symlink(absoluteLink) && (!exists(absoluteLink) || (read_symlink(absoluteLink) != absoluteItem))) {
			remove(absoluteLink);
			create_symlink(absoluteItem, absoluteLink);
			cout << "Symlink to file \"" << fileName << "\" fixed\n";
		}
	}	
	for (const auto& absoluteItem : fs::directory_iterator(src.value)) {
		string fileName = absoluteItem.path().lexically_relative(src.value);
		bool match = any_of(filter.begin(), filter.end(), [&fileName](const auto &val) -> bool {
				return fileName.starts_with(val);	
			});
		fs::path absoluteLink = dest.value / fileName;
		if (!match) continue;
		if (!exists(symlink_status(absoluteLink))) {
			create_symlink(absolute(absoluteItem), absoluteLink);
			cout << "Symlink to file \"" << fileName << "\" created\n";
		} else if (exists(symlink_status(absoluteLink)) && !is_symlink(absoluteLink)) {
			cout << "Failed to create symlink to file \"" << fileName << "\", location already taken\n";
		}
	}
}

//TODO repair import
int importOp(set<string> &options, vector<string> &arguments) {
	/*if (arguments.size() != 4) exitWithUsage();
	root_pack root;
	string &server = arguments[1];
	string &mc_version = arguments[2];
	if (!parseConfig(root)) printError("HASH-GEN is outdated, please run \"mserman verify\" again");
	if (root.root("SERVERS")(server)) printError("Server with name \"" + arguments[1] + "\" already exists");
	if (!root.root("VERSIONS")(mc_version)) printError("Version with name \"" + arguments[2] + "\" doesn't exist");
	root.paths["SERVER"] = path_node(arguments[3]);
	string_node &rootModules = root.root["ENTRIES"];
	string_node &rootServer = root.root["SERVERS"][server];
	if (!is_directory(root.paths["SERVER"].value)) printError("Path \"" + arguments[3] + "\" doesn't point to server directory");
	rootServer["CORE"];
	string key, prefix = server + "_mods";
	if (exists(root.paths["SERVER"]["mods"].value) && !fs::is_empty(root.paths["SERVER"]["mods"].value)) {
		for (int counter = 1; root.root["PACKS"](key = (prefix + to_string(counter))); counter++) {}
		rootServer["PACKS"][key].value = mc_version;
		string_node &rootPack = root.root["PACKS"][key];
		for (const auto &entry : fs::directory_iterator(root.paths["SERVER"]["mods"].value)) {
			string fileName = entry.path().lexically_relative(root.paths["SERVER"]["mods"].value);
			if ((!entry.is_regular_file() && !entry.is_symlink()) || entry.path().extension() != ".jar") {
				cout << "Found invalid imported mod file \"" << fileName << "\"\n";
			} else {
				vector<module_entry> modules = lookFile(entry);
				if (modules.empty()) {
					vector<module_entry> existing = lookRoot(root, fileName, mc_version);
					if (existing.empty()) {
						string temp = fileName.substr(0, fileName.size() - 4);
						string key1 = '.' + stringToLower(temp);
						rootModules[key1][mc_version]["default"].value = fileName;
						rootModules[key1][mc_version]["default"]["MODEL"]["FORGE"];
						rootModules[key1][mc_version]["default"]["STANDALONE"];
						rootPack["ENTRIES"][key1].value = "Adefault";
					} else {
						for (auto &module: existing) {
							rootPack["ENTRIES"][module.id].value = "A" + module.version;
						}
					}
				} else {
					for (auto &module: modules) {
						if (bool edit = shouldVerify(root, module, mc_version); edit) {
							string_node &rootModule = rootModules[module.id][mc_version][module.version];
							rootModule.value = fileName;
							rootModule["MODEL"][module.model];
							rootModule["STANDALONE"];
							for (const auto &[depId, depVersion]: module.dependency) {
								if (rootModule["DEPENDENCIES"](depId)("FORCED")) continue;
								string realDepVersion = ((char)depVersion.second) + depVersion.first;
								rootModule["DEPENDENCIES"][depId].value = depVersion.first.empty() ? "" : realDepVersion;
							}
							for (auto it = rootModule["DEPENDENCIES"].children.begin(); it != rootModule["DEPENDENCIES"].children.end();) {
								if (!it->second("FORCED") && !module.dependency.contains(it->first)) {
									it = rootModule["DEPENDENCIES"].children.erase(it);
								} else ++it;
							}
							rootPack["ENTRIES"][module.id].value = "A" + module.version;
						}
					}
				}
				fs::copy(entry, root.paths["GENERAL"]["entries"][mc_version].value, fs::copy_options::update_existing);
			}
		}
		prefix = arguments[1] + "_configs";
		for (int counter = 1; root.root["META-CONFIGS"](key = (prefix + to_string(counter))); counter++) {}
		path_node dirSysConfigs = ensureExists(root.paths["GENERAL"]["config"][key]);
		root.root["META-CONFIGS"][key];
		rootServer["META-CONFIG"].value = key;
		if (exists(root.paths["SERVER"]["config"].value)) {
			for (const auto& config : fs::recursive_directory_iterator(root.paths["SERVER"]["config"].value)) {
				string fileName = config.path().lexically_relative(root.paths["SERVER"]["config"].value);
				if (config.is_regular_file()) {
					fs::create_directories((dirSysConfigs.value / fileName).parent_path());
					fs::copy(config, dirSysConfigs.value / fileName);
					root.root["META-CONFIGS"][key][fileName].value = "VALID";
				} else if (!config.is_directory()) {
					cout << "Found invalid imported config file \"" << fileName << "\"\n";
				}
			}
		}
	}
	if (exists(root.paths["SERVER"]["plugins"].value) && !fs::is_empty(root.paths["SERVER"]["plugins"].value)) {
		prefix = server + "_plugins";
		for (int counter = 1; root.root["PACKS"](key = (prefix + to_string(counter))); counter++) {}
		rootServer["PACKS"][key].value = mc_version;
		string_node &rootPack = root.root["PACKS"][key];
		for (const auto &entry : fs::directory_iterator(root.paths["SERVER"]["plugins"].value)) {
			string fileName = entry.path().lexically_relative(root.paths["SERVER"]["plugins"].value);
			if (!(entry.is_regular_file() || entry.is_symlink()) || entry.path().extension() != ".jar") {
				cout << "Found invalid imported plugin file \"" << fileName << "\"\n";
			} else {
				vector<module_entry> modules = lookFile(entry);
				if (modules.empty()) {
					vector<module_entry> existing = lookRoot(root, fileName, mc_version);
					if (existing.empty()) {
						string temp = fileName.substr(0, fileName.size() - 4);
						string key1 = '.' + stringToLower(temp);
						rootModules[key1][mc_version]["default"].value = fileName;
						rootModules[key1][mc_version]["default"]["MODEL"]["PLUGIN"];
						rootModules[key1][mc_version]["default"]["STANDALONE"];
						rootPack["ENTRIES"][key1].value = "Adefault";
					} else {
						for (auto &module: existing) {
							rootPack["ENTRIES"][module.id].value = "A" + module.version;
						}
					}
				} else {
					for (auto &module: modules) {
						if (bool edit = shouldVerify(root, module, mc_version); edit) {
							string_node &rootModule = rootModules[module.id][mc_version][module.version];
							rootModule.value = fileName;
							rootModule["MODEL"][module.model];
							rootModule["STANDALONE"];
							for (const auto &[depId, depVersion]: module.dependency) {
								if (rootModule["DEPENDENCIES"](depId)("FORCED")) continue;
								string realDepVersion = ((char)depVersion.second) + depVersion.first;
								rootModule["DEPENDENCIES"][depId].value = depVersion.first.empty() ? "" : realDepVersion;
							}
							for (auto it = rootModule["DEPENDENCIES"].children.begin(); it != rootModule["DEPENDENCIES"].children.end();) {
								if (!it->second("FORCED") && !module.dependency.contains(it->first)) {
									it = rootModule["DEPENDENCIES"].children.erase(it);
								} else ++it;
							}
							rootPack["ENTRIES"][module.id].value = "A" + module.version;
						}
					}
				}
				fs::copy(entry, root.paths["GENERAL"]["entries"][mc_version].value, fs::copy_options::update_existing);
			}
		}
	}

	cout << "Please, run \"mserman verify\" to finish creation\nNote that you will need to setup core by yourself\n";
	flushConfig(root);*/
	return 0;
}
/**Verification operation
 *
 * @param options options such as "--minecraft", "--allow-unsafe"
 * @param arguments currently unused
 * @return nothing
 */
int verifyOp(set<string> &options, vector<string> &arguments) {
	if (arguments[0] == "completion") return 0; //TODO or NOT
	{
		string_node &rootModules = root.root["ENTRIES"];
		for (auto &[mc_version, dirEntries] : ensureExists(root.paths["GENERAL"]["entries"])) {
			if (!is_directory(dirEntries.value)) {
				cout << "Found non-version object " << mc_version << " in entries folder\n";
				root.stats["UNKNOWN"]["VERSIONS"][mc_version]++;
				continue;
			}
			for (auto &[entry, fileEntry] : dirEntries) {
				if (!is_regular_file(fileEntry.value) || fileEntry.value.extension() != ".jar") {
					cout << "Found invalid file \"" << entry << "\"\n";
					root.stats["UNKNOWN"]["ENTRIES"][entry]++;
					continue;
				}
				vector<module_entry> modules = lookFile(fileEntry.value);
				if (modules.empty()) {
					vector<module_entry> existing = lookRoot(entry, mc_version);
					if (existing.empty()) {
						string temp = entry.substr(0, entry.size() - 4);
						string key1 = '.' + stringToLower(temp);
						rootModules[key1][mc_version]["default"].value = entry;
						rootModules[key1][mc_version]["default"]["MODEL"].value = "FORGE";
						rootModules[key1][mc_version]["default"]["STANDALONE"];
						rootModules[key1][mc_version]["default"]["SERVER"];
						rootModules[key1][mc_version]["default"]["CLIENT"];
					}
				} else {
					for (auto &module: modules) {
						if (bool edit = shouldVerify(module, mc_version); edit) {
							bool existed = (bool) rootModules(module.id)(mc_version)(module.version);
							string_node &rootModule = rootModules[module.id][mc_version][module.version];
							rootModule.value = entry;
							rootModule["MODEL"][module.model];
							if (!existed) rootModule["STANDALONE"];
							for (const auto &[depId, depVersion]: module.dependency) {
								if (rootModule["DEPENDENCIES"](depId)("FORCED")) continue;
								string realDepVersion = ((char)depVersion.second) + depVersion.first;
								rootModule["DEPENDENCIES"][depId].value = depVersion.first.empty() ? "" : realDepVersion;
							}
							for (auto it = rootModule["DEPENDENCIES"].children.begin(); it != rootModule["DEPENDENCIES"].children.end();) {
								if (!it->second("FORCED") && !module.dependency.contains(it->first)) {
									it = rootModule["DEPENDENCIES"].children.erase(it);
								} else ++it;
							}
						}
					}
				}
			}
		}
		
		set<string> processed;
		set<string> removed;
		function<void(const string&, string_node&)> dfs = [&removed, &processed, &dfs, &rootModules](const string& module, string_node &rootEntry) {
			if (!processed.insert(module).second) return;
			set<string> removedMCVersion;
			for (auto &[mc_version, dirEntries] : root.paths["GENERAL"]["entries"]) {
				if (!is_directory(dirEntries.value)) continue;
				if (!rootEntry(mc_version)) continue;
				set<string> removedVersion;
				for (auto [moduleVersion, rootModule] : rootEntry[mc_version]) {
					if (rootModule("SYNTHETIC")) {
						cout << "Ignoring SYNTHETIC " << combine(module, moduleVersion) << '\n';
						root.stats["VALID"]["ENTRIES"][module][moduleVersion]++;
						continue;
					}
					string &status = rootModule["STATUS"].upper();
					fs::path filePath = root.paths["GENERAL"]["entries"][mc_version].value / rootModule.value;
					if (is_regular_file(filePath) && filePath.extension() == ".jar") {
						string &model = rootModule["MODEL"].def("FORGE");
						rootModule["DEPENDENCIES"];
						if (status != "VALID" && status != "F-VALID") {
							cout << "Need to configure " << combine(module, moduleVersion) << "\n";
							root.stats["NEW"][module][moduleVersion]++;
							status = "NEW";
							continue;
						}
						bool fine = true;
						for (auto &[dep, rootDependency] : rootModule["DEPENDENCIES"]) {
							if (rootModules(dep)) {
								dfs(dep, rootModules[dep]);
								string depVersion = rootDependency.value;
								module_entry::match condition = depVersion.empty() ? module_entry::approx : (module_entry::match) depVersion[0];
								depVersion = depVersion.empty() ? "" : depVersion.substr(1);
								depVersion = findVersion(rootModules[dep][mc_version], depVersion, condition);
								if (!depVersion.empty() && root.stats("VALID")("ENTRIES")(dep)(depVersion)) {
									if (!rootModules[dep](mc_version)) continue;
									if (rootModules[dep][mc_version][depVersion]("SYNTHETIC")) continue;
									string_node &rootDep = rootModules[dep][mc_version][depVersion];
									if (model != rootDep["MODEL"].value) {
										cout << "Dependency for " << combine(module, moduleVersion) << ": "<< combine(dep, depVersion) << " is using wrong model\n";
										root.stats["LOST"]["ENTRIES"][dep][depVersion]["BY-ENTRY"][module][moduleVersion]["MODEL"]++;
										fine = false;
									}
									if (!((rootModule("CLIENT") && rootDep("CLIENT")) || (rootModule("SERVER") && rootDep("SERVER")))) {
										cout << "Dependency for  " << combine(module, moduleVersion) << ": "<< combine(dep, depVersion) << " is on the wrong side\n";
										root.stats["LOST"]["ENTRIES"][dep][depVersion]["BY-ENTRY"][module][moduleVersion]["SIDE"]++;
										fine = false;
									}
								} else {
									cout << "Failed to resolve dependency for " << combine(module, moduleVersion)<< ": " << combine(dep, depVersion.empty() ? rootDependency.value : depVersion)<< "\n";
									root.stats["LOST"]["ENTRIES"][dep][depVersion]["BY-ENTRY"][module][moduleVersion]++;
									fine = false;
								}
							} else {
								cout << "Failed to find dependency for " << combine(module, moduleVersion) << ":  " << combine(dep, rootDependency.value) << "\n";
								root.stats["LOST"]["ENTRIES"][dep][rootDependency.value]["BY-ENTRY"][module][moduleVersion]++;
								fine = false;
							}
						}
						if (fine) root.stats["VALID"]["ENTRIES"][module][moduleVersion]++;
						continue;
					} else if (!exists(filePath) || rootModule.value.empty()) {
						if (status == "NOT-VALID" || rootModule.value.empty()) {
							removedVersion.insert(moduleVersion);
							continue;
						} else if (status != "GONE") cout << "Failed to find " << combine(module, moduleVersion) << " with file \"" << rootModule.value << "\"\n";
						else cout << "Need to remove entry for gone " << combine(module, moduleVersion) << "\n";
						status = "GONE";
					} else {
						cout << "Found invalid " << combine(module, moduleVersion) << " with file \"" << rootModule.value << "\"\n";
						status = "NOT-VALID";
					}
					root.stats["UNKNOWN"]["ENTRIES"][module][moduleVersion]++;

				}
				for (const auto &moduleVersion: removedVersion) rootEntry[mc_version].children.erase(moduleVersion);
				if (rootEntry[mc_version].children.empty()) removedMCVersion.insert(module);
			}
			for (const auto &moduleVersion: removedMCVersion) rootEntry.children.erase(moduleVersion);
			if (rootEntry.children.empty()) removed.insert(module);
		};
		for (auto &[module, rootModule]: rootModules) dfs(module, rootModule);
		for (const auto &module: removed) rootModules.children.erase(module);
		for (auto &[family, rootFamily]: root.root["FAMILIES"]) {
			cout << "Processing family \"" << family << "\"\n";
			bool fine = true;
			for (auto &[mc_version, ignored]: root.root["VERSIONS"]) {
				unordered_map<string, vector<pair<string, string>>> modelInfo;
				for (auto &[module, rootModule]: rootFamily) {
					string moduleVersion = rootModule.value;
					module_entry::match condition = moduleVersion.empty() ? module_entry::approx : (module_entry::match) moduleVersion[0];
					moduleVersion = moduleVersion.empty() ? "" : moduleVersion.substr(1);
					if (!rootModules(module)) {
						cout << "Failed to find entry " << combine(module, moduleVersion) << '\n';
						root.stats["LOST"]["ENTRIES"][module][moduleVersion]["BY-FAMILY"][family]++;
						fine = false;
						continue;
					}
					if (!rootModules(module)(mc_version)) {
						continue;
					}
					moduleVersion = findVersion(rootModules[module][mc_version], moduleVersion, condition);
					if (!root.stats("VALID")("ENTRIES")(module)(moduleVersion)) {
						cout << "Failed to resolve entry " << combine(module, moduleVersion) << '\n';
						root.stats["LOST"]["ENTRIES"][module][moduleVersion]["BY-FAMILY"][family]++;
						fine = false;
					} else if(!rootModules[module][mc_version][moduleVersion]("STANDALONE")) {
						cout << "Entry " << combine(module, moduleVersion) << " should not be a library\n";
						root.stats["LOST"]["ENTRIES"][module][moduleVersion]["BY-FAMILY"][family]++;
						fine = false;
					} else {
						auto &list = modelInfo[rootModules[module][mc_version][moduleVersion]["MODEL"].value];
						list.emplace_back(module, moduleVersion);
					}
				}
				auto maxModel = std::max_element( modelInfo.begin(), modelInfo.end(),[](const auto& a, const auto& b) {
						return a.second.size() < b.second.size();
						});

				if (maxModel != modelInfo.end()) {
					for (const auto& [model, data] : modelInfo) {
						if (model != maxModel->first) {
							fine = false;
							for (const auto& [module, moduleVersion] : data) {
								cout << "Entry " << combine(module, moduleVersion) << " should have different model\n";
								root.stats["LOST"]["ENTRIES"][module][moduleVersion]["BY-FAMILY"][family]["MODEL"]++;
							}
						}
					}
					root.cache["FAMILIES"][family][mc_version]["MODEL"].value = maxModel->first;
				} else {
					root.cache["FAMILIES"][family][mc_version]["MODEL"].value = "FORGE";
				}
			}
			if (fine)
				root.stats["VALID"]["FAMILIES"][family]++;
		}
		for (auto &[pack, rootPack]: root.root["PACKS"]) {
			cout << "Processing pack \"" << pack << "\"\n";
			bool fine = true;
			for (auto &[mc_version, ignored]: root.root["VERSIONS"]) {
				unordered_map<string, vector<pair<string, pair<bool, string>>>> modelInfo;
				for (auto &[module, rootModule]: rootPack["ENTRIES"]) {
					string moduleVersion = rootModule.value;
					module_entry::match condition = moduleVersion.empty() ? module_entry::approx : (module_entry::match) moduleVersion[0];
					moduleVersion = moduleVersion.empty() ? "" : moduleVersion.substr(1);
					if (!rootModules(module)) {
						cout << "Failed to find entry " << combine(module, moduleVersion) << '\n';
						root.stats["LOST"]["ENTRIES"][module][moduleVersion]["BY-PACK"][pack]++;
						fine = false;
						continue;
					}
					if (!rootModules(module)(mc_version)) {
						continue;
					}
					moduleVersion = findVersion(rootModules[module][mc_version], moduleVersion, condition);
					if (!root.stats("VALID")("ENTRIES")(module)(moduleVersion)) {
						cout << "Failed to resolve entry " << combine(module, moduleVersion) << '\n';
						root.stats["LOST"]["ENTRIES"][module][moduleVersion]["BY-PACK"][pack]++;
						fine = false;
					} else if(!rootModules[module][mc_version][moduleVersion]("STANDALONE")) {
						cout << "Entry " << combine(module, moduleVersion) << " should not be a library\n";
						root.stats["LOST"]["ENTRIES"][module][moduleVersion]["BY-PACK"][pack]++;
						fine = false;
					} else {
						auto &list = modelInfo[rootModules[module][mc_version][moduleVersion]["MODEL"].value];
						list.emplace_back(module, pair{false, moduleVersion});
					}
				}
				for (auto &[family, rootFamily]: rootPack["FAMILIES"])
					if (!root.stats("VALID")("FAMILIES")(family)) {
						cout << "Failed to resolve family \"" << family << "\"\n";
						root.stats["LOST"]["FAMILIES"][family]["BY-PACK"][pack]++;
						fine = false;
					} else {
						auto &list = modelInfo[root.cache["FAMILIES"][family][mc_version]["MODEL"].value];
						list.emplace_back(family, pair{true, ""});
					}
				auto maxModel = std::max_element(modelInfo.begin(), modelInfo.end(),[](const auto& a, const auto& b) {
						return a.second.size() < b.second.size();
						});

				if (maxModel != modelInfo.end()) {
					for (const auto& [model, data] : modelInfo) {
						if (model != maxModel->first) {
							fine = false;
							for (const auto& [module, moduleInfo] : data) {
								const auto& [moduleType, moduleVersion] = moduleInfo;
								if (!moduleType) {
									cout << "Entry " << combine(module, moduleVersion) << " should have different model\n";
									root.stats["LOST"]["ENTRIES"][module][moduleVersion]["BY-PACK"][pack]["MODEL"]++;
								} else  {
									cout << "Family \"" << module << "\" should have different model\n";
									root.stats["LOST"]["FAMILIES"][module]["BY-PACK"][pack]["MODEL"]++;
								}
							}
						}
					}
					root.cache["PACKS"][pack][mc_version]["MODEL"].value = maxModel->first;
				} else {
					root.cache["PACKS"][pack][mc_version]["MODEL"].value = "FORGE";
				}
			}
			if (fine)
				root.stats["VALID"]["PACKS"][pack]++;
		}
		cout.flush();
	}
	{
		for (const auto &[metaConfig, dirMetaConfig]: ensureExists(root.paths["GENERAL"]["config"])) {
			if (!is_directory(dirMetaConfig.value)) cout << "Found non-meta-config object " << metaConfig << " in meta-configs folder\n";
			else if (!root.root("META-CONFIGS")(metaConfig)) cout << "Found unknown meta-config folder " << metaConfig << '\n';
			else continue;
			root.stats["UNKNOWN"]["META-CONFIGS"][metaConfig]++;
		}
		for (auto &[metaConfig, rootConfigs]: root.root["META-CONFIGS"]) {
			cout << "Processing meta-config \"" << metaConfig << "\"\n";
			const vector<string> metaType = {"META-CONFIGS", metaConfig};
			path_node &dirConfigs = ensureExists(root.paths["GENERAL"]["config"][metaConfig]);
			for (const auto& config : fs::recursive_directory_iterator(dirConfigs.value)) {
				string fileName = config.path().lexically_relative(dirConfigs.value);
				if (config.is_regular_file()) {
					if (none_of(rootConfigs.begin(), rootConfigs.end(),
								[&](const auto& entry) { return fileName.starts_with(entry.first); })) {
						rootConfigs[fileName];
					}
				} else if (!config.is_directory()) {
					cout << "Found invalid config file '" << fileName << "'\n";
					root.stats["UNKNOWN"][metaType][fileName]++;
				}
			}
			set<string> removed;
			bool fine = true;
			for (auto &config : rootConfigs) {
				string &status = config.second.upper();
				fs::path filePath = dirConfigs.value / config.first;
				if (is_regular_file(filePath) || is_directory(filePath)) {
					if (status != "VALID") {
						cout << "Need to configure config '" << config.first << "'\n";
						status = "NEW";
						root.stats["NEW"][metaType][config.first]++;
					}
					continue;
				} else if (!exists(filePath)) {
					if (status == "NOT-VALID") {
						removed.insert(config.first);
						fine = false;
						continue;
					} else if (status != "GONE") cout << "Failed to find config file '" << config.first << "'\n";
					else cout << "Need to clean entry for gone config '" << config.first << "'\n";
					status = "GONE";
				} else {
					cout << "Found invalid config file '" << config.first << "'\n";
					status = "NOT-VALID";
				}
				fine = false;
				root.stats["UNKNOWN"][metaType][config.first]++;
			}
			if (fine) root.stats["VALID"][metaType]++;
			for (const auto &entry: removed) rootConfigs.children.erase(entry);
		}
		cout.flush();
	}
	{
		for (const auto &[core, dirCore]: ensureExists(root.paths["GENERAL"]["core"])) {
			if (!is_directory(dirCore.value)) cout << "Found non-core object " << core << " in cores folder\n";
			else if (!root.root("CORES")(core)) cout << "Found unknown core " << core << " in folder\n";
			else continue;
			root.stats["UNKNOWN"]["CORES"][core]++;
		}
		for (auto &[core, rootCore]: root.root["CORES"]) {
			cout << "Processing core \"" << core << "\"\n";
			string &status = rootCore["STATUS"].upper();
			const vector<string> coreType = {"CORES", core};
			const bool validTests = (bool) rootCore("USE-FOR-SIMULATION");
			const fs::path coreRuntime = rootCore["RUNTIME"].value;
			const string coreMain = rootCore["MAIN"].value;
			path_node &dirCore = ensureExists(root.paths["GENERAL"]["core"][core]);
			if (status != "NOT-VALID" && status != "VALID") {
				cout << "Need to configure core \"" << core << "\"\n";
				root.stats["NEW"][coreType]++;
				status = "NEW";
				continue;
			}
			bool fine = true;
			if (!is_regular_file(dirCore.value / coreMain)) {
				cout << "Main file for core \"" << core << "\" not found\n";
				root.stats["LOST"][coreType]["MAIN"]++;
				fine = false;
			}
			if (!is_regular_file(coreRuntime / "java")) {
				cout << "Runtime for core \"" << core << "\" not found\n";
				root.stats["LOST"][coreType]["RUNTIME"]++;
				fine = false;
			} else {
				cout << "Checking Java runtime " << coreRuntime << " for core \"" << core << "\"" << endl;
				string command = (coreRuntime / "java").string() + " -version";
				bool bufferFine = !system(command.c_str());
				cout.flush();
				cout << (!bufferFine ? "------------------------\nNot a valid Java runtime\n------------------------" : "-------------\nRuntime valid\n-------------") << endl;
				fine = fine && bufferFine;
			}
			status = fine ? "VALID" : "NOT-VALID";
			if (fine) root.stats["VALID"][coreType]++;
		}
		cout.flush();
	}
	{
		for (const auto &[backup, fileBackup]: ensureExists(root.paths["BACKUP"]["server"])) {
			if (!is_regular_file(fileBackup.value) || fileBackup.value.extension() != ".bak") cout << "Found non-backup object " << backup << " in server backups folder\n";
			else if (!root.root("SERVERS")(fileBackup.value.stem().filename())) cout << "Found unknown server backup " << backup << "\n";
			else continue;
			root.stats["UNKNOWN"]["SERVERS"][fileBackup.value.stem().filename()]["BACKUP"]++;
		}
		for (const auto &[server, dirServer]: ensureExists(root.paths["GENERAL"]["server"])) {
			if (!is_directory(dirServer.value)) cout << "Found non-server object " << server << " in servers folder\n";
			else if (!root.root("SERVERS")(server)) cout << "Found unknown server " << server << "\n";
			else continue;
			root.stats["UNKNOWN"]["SERVERS"][server]++;
		}
		for (auto &[server, rootServer]: root.root["SERVERS"]) {
			cout << "Processing server \"" << server << "\"\n";
			string &status = rootServer["STATUS"].upper();
			const bool dedicated = (bool) rootServer("DEDICATED");
			const string &core = rootServer("CORE").value;
			const vector<string> typeServer = {"SERVERS", server};
			const vector<string> typeCore = {"CORES", core};
			if (dedicated) {
				rootServer["CORE"];
				rootServer["JAVA-ARGS"];
			}
			rootServer["PERSONAL-FILES"];
			if (status != "NOT-VALID" && status != "VALID") {
				cout << "Need to configure server\n";
				root.stats["NEW"][typeServer]++;
				status = "NEW";
				continue;
			}
			bool fine = true;
			if (dedicated && !root.stats("VALID")(typeCore)) {
				cout << "Core for server not valid\n";
				root.stats["LOST"][typeCore]["BY-SERVER"][server]++;
				status = "NOT-VALID";
				continue;
			}
			string_node &rootCore = root.root[typeCore];
			for (auto &[pack, packVersion]: rootServer["PACKS"]) {
				if (!root.stats("VALID")("PACKS")(pack)) {
					cout << "Pack \"" << pack << "\" not valid\n";
					root.stats["LOST"]["PACKS"][pack]["BY-SERVER"][server]++;
					fine = false;
				} else {
					if (!rootCore(root.cache["PACKS"][pack]["MODEL"])) {
						cout << "Pack \"" << pack << "\" should have different model\n";
						root.stats["LOST"]["PACKS"][pack]["BY-SERVER"][server]["MODEL"]++;
						fine = false;
					}
				}
			}
			if (rootCore("FORGE") || rootCore("FABRIC")) {
				if (!root.stats("VALID")("META-CONFIGS")(rootServer["META-CONFIG"])) {
					cout << "Meta-config \"" << rootServer["META-CONFIG"].value << "\" not valid\n";
					root.stats["LOST"]["META-CONFIGS"][rootServer["META-CONFIG"]]["BY-SERVER"][server]++;
					fine = false;
				}
			}
			status = fine ? "VALID" : "NOT-VALID";
			if (fine) {
				const string &clientServer = root.root["GENERAL"]["MINECRAFT"]["SERVER"].value;
				root.cache[typeServer]["VALID"];
				root.stats["VALID"][typeServer]++;
				if (dedicated) {
					linkDirFilter(root.paths["GENERAL"]["core"][core], ensureExists(root.paths["GENERAL"]["server"][server]), {""}, false);
					linkPacks(rootServer["PACKS"], server, "SERVER");
					if (rootCore("FORGE") || rootCore("FABRIC")) {
						linkConfig(rootServer["META-CONFIG"].value, server);
					}
				} else if (server == clientServer) {
					string &oldVersion = root.cache["OLD-MINECRAFT"].value;
					if (oldVersion != clientServer) {
						if (root.root["SERVERS"](oldVersion)) {
							vector<string> filtered;
							for (auto &[filter, ignored] : rootServer["PERSONAL-FILES"])
								filtered.emplace_back(filter);
							linkDirFilter(ensureExists(root.paths["BACKUP"]["client"][oldVersion]), root.paths["MINECRAFT"], filtered, true);
						}
						linkDirFilter(ensureExists(root.paths["BACKUP"]["client"][server]), root.paths["MINECRAFT"], {}, true);
					}
					vector<string> filtered;
					for (auto &[filter, ignored] : rootServer["PERSONAL-FILES"])
						filtered.emplace_back(filter);
					linkDirFilter(ensureExists(root.paths["BACKUP"]["client"][server]), root.paths["MINECRAFT"], filtered, true);
					linkPacks(rootServer["PACKS"], "", dedicated ? "CLIENT" : "BOTH");
					linkConfig(rootServer["META-CONFIG"].value, "");
					oldVersion = clientServer;
				}
			}
		}
		cout.flush();
	}
	return 0x300;
}
