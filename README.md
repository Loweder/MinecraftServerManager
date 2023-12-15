# MSERMAN
MSERMAN is a Minecraft SERver MANager, made for managing servers on local Linux host
## Installation
Install needed tools, then compile using cmake.
```bash
sudo apt install libzip-dev
sudo apt install cmake
mkdir build
cd build
cmake ..
make
```
### System wide Installation
If you want to access mserman from anywhere on your system run this:
```bash
sudo cp build/mserman /bin/mserman
```
If you have already installed mserman before, run this:
```bash
sudo rm /bin/mserman && sudo cp build/mserman /bin/mserman
```
## Usage
First configure config mserman.local (example config is example_mserman.conf)
Add servers, entries, cores and packs.
After any changes in config run
```bash
mserman verify
```
to apply them

First add version into config at VERSIONS

To add mod, or plugin, add it into ROOT-DIR/entries/&lt;entry mc version&gt;/

In config find it in ENTRIES.&lt;id&gt;.&lt;mc version&gt;.&lt;version&gt;, modify it as you wish, 
mark it as VALID or F-VALID (dependencies are not modified automatically)

To create core, add core files to ROOT-DIR/cores/&lt;core name&gt;. Also add core to config.

To create pack/family just add it into FAMILIES/PACKS

To create server, run
```bash
mserman make <name> <mc version> <core>
```
Or
```bash
mserman import <name> <mc version> <path>
```
Where "core" is valid core in CORES and
"mc version" is valid version in VERSIONS

And configure mserman.conf to add packs and configs. To run server use
```bash
mserman boot <name>
```
### System wide config
If you want to access same mserman.local config from anywhere on your system, open ~/.config/mserman.global and write path of config folder
## Contributing
Pull requests are welcome. For major changes, please open an issue first
to discuss what you would like to change.