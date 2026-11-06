# Kompa Lead (flat version)

All files live in one folder. Upload them to the root of a GitHub repo.

Files:
- CMakeLists.txt
- PluginProcessor.h
- PluginProcessor.cpp
- build.yml   <- must live at .github/workflows/build.yml in the repo (see below)
- README.md

## One extra step for build.yml
GitHub only runs workflows from `.github/workflows/`. Do not upload build.yml with the rest.
Instead, in your repo click Add file > Create new file, type this as the file name:

    .github/workflows/build.yml

(typing the slashes creates the folders), paste in the contents of build.yml, and commit.

## Build
Push to main, open the Actions tab, wait for "Build Kompa Lead", then download the artifact
for your system (Windows / macOS / Linux) and copy the plugin into your VST3 folder.
