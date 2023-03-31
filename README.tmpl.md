[![Build Windows]({{ tmplr.github_url }}/actions/workflows/build-windows-ci.yml/badge.svg)]({{ tmplr.github_url }}/actions/workflows/build-windows-ci.yml) [![Build Linux]({{ tmplr.github_url }}/actions/workflows/build-linux-ci.yml/badge.svg)]({{ tmplr.github_url }}/actions/workflows/build-linux-ci.yml)

# {{ tmplr.name }}
This is a binary module for the [Pragma Game Engine](https://github.com/Silverlan/pragma). For more information on binary modules, check out [this wiki article](https://wiki.pragma-engine.com/books/pragma-engine/page/binary-modules).

## Installation
To install this module, download one of the prebuilt binaries on the right and extract the archive over your Pragma installation.

The module can then be loaded in Pragma by running the following console command:
```
lua_run_cl print(engine.load_library("{{ tmplr.install_directory }}{{ tmplr.module_name }}"))
```

(You can use `lua_run` instead of `lua_run_cl` to load the module serverside instead of clientside.)

## Developing

### Building
If you want to build this module manually, follow the instructions for [building Pragma](https://github.com/Silverlan/pragma#build-instructions) and add the following argument to the build script:
```
--module {{ tmplr.module_name }}:"{{ tmplr.git_url }}"
```

The build script will clone, build and install the module automatically. After that you can run the following command whenever you have made changes to the module to rebuild and reinstall it without having to re-run the build script every time:
```
cmake --build --target {{ tmplr.module_name }} pragma-install
```

### Advanced Building
If the module requires additional steps (such as downloading and building external dependencies) before it can be built, you can add those steps to the python script in `build_scripts/setup.py`. This script will be executed automatically whenever the Pragma build script is run.

### Advanced Installation
By default the module binary file will be installed to the `modules` directory of the Pragma installation. If you want to change this behavior, you can do so by editing the `CMakeInstall.txt` file.

### Testing
This repository comes with a simple Lua-script for testing purposes, which you can find in "examples/". Follow the instructions in the commented section of the script to install and run it to test the module.

### Publishing Releases
This repository comes with workflows for automated builds, which are triggered every time you push a commit. The binaries will be released under the `latest` tag. The badges at the top of this readme indicate if the workflows were successful. The workflows may take several hours before they complete.

Once the `latest` binaries have been released successfully, you can also publish a stable release with a version number by going to the `Actions` tab of this repository and triggering the `Create Stable Release` workflow.
