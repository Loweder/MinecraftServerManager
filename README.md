# MSERMAN
MSERMAN is a Minecraft SERver MANager, made for managing servers on local Linux host
## Installation
Install needed tools, then compile using cmake. Copy executable file where needed
```bash
sudo apt install libzip-dev
sudo apt install cmake
mkdir build
cd build
cmake ..
make
```
## Usage
First configure config mserman.conf (example config is example_mserman.conf)
Add versions, cores and packs.

To add mod, or plugin, first create a version, and set path for GENERAL.ROOT-DIR (folder in which all server files will be stored).
Then, run
```bash
./mserman verify
```
Open specified directory, then versions, your selected version.
Add your mods and plugins to mods/ and plugins/ folder respectively.
When you are done, run again
```bash
./mserman verify
```
In mserman.conf you will see your mods and plugins added to version. You can rename default names as you wish.

To create core, add core files to your_root/cores/core_name. Also add core to config.

To create server, run
```bash
./mserman make <core> <name>
```
Where "core" is valid core in config.
After creation run
```bash
./mserman verify
```
And configure mserman.conf to add plugins and mods. To run server use
```bash
./mserman boot <name>
```
## Installation
If you want to access mserman from anywhere on your system run this:
```bash
sudo cp ./build/mserman /bin
```
If you have already installed mserman before, run this:
```bash
sudo rm /bin/mserman && sudo cp ./build/mserman /bin
```
## Contributing
Pull requests are welcome. For major changes, please open an issue first
to discuss what you would like to change.