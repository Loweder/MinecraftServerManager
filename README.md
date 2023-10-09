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
Add versions, cores and architectures.

To add mod, or plugin, first create a version, and set path for General.RootDir (folder in which all server files will be stored).
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
./mserman make <arch> <name>
```
Where "arch" is valid architecture in config.
After creation run
```bash
./mserman verify
```
And configure mserman.conf to add plugins and mods.

To run server use
```bash
./mserman boot <name>
```
## Contributing

Pull requests are welcome. For major changes, please open an issue first
to discuss what you would like to change.