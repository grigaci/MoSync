#include "android.h"
#include "util.h"
#include "nbuild.h"
#include "mustache/mustache.h"
#include "profiledb/profiledb.h"
#include "helpers/mkdir.h"
#include <map>
#include <vector>
#include <sstream>
#include <fstream>

#define TEMP_BUILD_DIR "--android-build-dir"

using namespace std;

int buildAndroidNative(Arguments* params) {
	int result = generateMakefile(params);
	if (!params->isFlagSet("--android-generate-makefile")) {
		result = executeNdkBuild(params);
	}
	return result;
}

string getTempBuildDir(Arguments* params) {
	string tmpDir = params->getSwitchValue(PROJECT_DIR);
	if (tmpDir.empty()) {
		tmpDir = params->getSwitchValue(TEMP_BUILD_DIR);
		toDir(tmpDir);
	} else {
		toDir(tmpDir);
		tmpDir += "temp/";
	}
	return tmpDir;
}

int generateMakefile(Arguments* params) {
	string tmpBuildDir = getTempBuildDir(params);
	_mkdir(tmpBuildDir.c_str());

	vector<string> bootstrapModules;
	split(bootstrapModules, params->getSwitchValue(BOOT_MODULE_LIST), ",");
	if (bootstrapModules.empty()) {
		// Default
		bootstrapModules.push_back("mosync");
		bootstrapModules.push_back("mosynclib");
	} else if (bootstrapModules[0] == ".") {
		bootstrapModules.clear();
	}

	vector<string> modules(bootstrapModules);
	split(modules, params->getSwitchValue(MODULE_LIST), ",");

	DefaultContext rootCtx(NULL);
	for (size_t i = 0; i < modules.size(); i++) {
		DefaultContext* moduleCtx = new DefaultContext(&rootCtx);
		// TODO: Clean up
		moduleCtx->setParameter("name", modules[i]);
		rootCtx.addChild("modules", moduleCtx);
	}
	vector<string> sourceFiles = getSourceFiles(params);
	string sourceFileList;
	for (size_t i = 0; i < sourceFiles.size(); i++) {
		sourceFileList += sourceFiles[i];
		sourceFileList += " ";
	}
	if (sourceFiles.empty()) {
		error("No source files!\n");
	}
	rootCtx.setParameter("source-files", sourceFileList);
	vector<string> compilerDefines = params->getPrefixedList(MACRO_DEFINES, true);
	rootCtx.setParameter("compiler-defines", delim(compilerDefines, " "));
	rootCtx.setParameter("additional-compiler-switches", params->getSwitchValue("--compiler-switches"));
	rootCtx.setParameter("additional-includes", params->getSwitchValue("--additional-includes"));
	if (bootstrapModules.empty()) {
		rootCtx.setParameter("no-default-includes", "true");
	}

	if (params->isFlagSet("--android-static-lib")) {
		rootCtx.setParameter("static-lib", "true");
	}

	MustacheParser parser(true);
	ProfileDB db;
	string androidProfilesDir = db.profilesdir("Android");
	string androidMkTemplate = androidProfilesDir + "/Android.mk.template";

	string androidMkOutput = tmpBuildDir + "/Android.mk";
	ofstream output(androidMkOutput.c_str());
	if (output.good()) {
		DefaultParserCallback cb(&rootCtx, output);
		parser.parseFile(androidMkTemplate, &cb);
		output.flush();
		output.close();
	} else {
		return 1;
	}

	string androidAppMkOutput = tmpBuildDir + "/Application.mk";
	string androidAppMkOriginal = androidProfilesDir + "/Application.mk";
	copyFile(androidAppMkOutput.c_str(), androidAppMkOriginal.c_str());

	return 0;
}


int executeNdkBuild(Arguments* params) {
	vector<string> configNames;
	vector<string> libVariants;
	split(configNames, require(params, CONFIGURATION), ",");
	split(libVariants, require(params, BINARY_TYPE), ",");

	string ndkbuildCmd = require(params, "--android-ndkbuild-cmd");
	string projectPath = require(params, PROJECT_DIR);
	toSlashes(projectPath);
	string moduleName = require(params, NAME);
	string outputDir = require(params, OUTPUT_DIR);
	toDir(outputDir);

	if (configNames.size() != libVariants.size()) {
		error("--config and --lib-variants must have a the same number of args", 2);
	}

	vector<string> archs;
	archs.push_back("armeabi");
	archs.push_back("armeabi-v7a");

	for (size_t i = 0; i < configNames.size(); i++) {
		for (size_t j = 0; j < archs.size(); j++) {
			string configName = configNames[i];
			string libVariant = libVariants[i];
			string arch = archs[j];

			string tmpBuildDir = getTempBuildDir(params);

			bool isDebug = libVariant == "debug";
			bool isVerbose = params->isFlagSet(VERBOSE);
			bool doClean = params->isFlagSet(CLEAN);

			ostringstream cmd;
			cmd << file(ndkbuildCmd) << " ";
			cmd << "-C " << file(tmpBuildDir) << " ";
			if (isVerbose) {
				cmd << "V=1 ";
			}
			if (isDebug) {
				cmd << "NDK_DEBUG=1 ";
			}
			if (doClean) {
				cmd << "-B ";
			}
			string libDir = string(mosyncdir()) + "/lib";
			string moduleDir = string(mosyncdir()) + "/modules";
			toSlashes(moduleDir);
			string makeFile = tmpBuildDir + "Android.mk";
			toSlashes(makeFile);
			cmd << arg("MOSYNC_MODULES=" + moduleDir) << " ";
			cmd << arg("MOSYNC_LIBS=" + libDir) << " ";
			cmd << arg("PROJECT_DIR=" + projectPath) << " ";
			cmd << arg("APP_BUILD_SCRIPT=" + makeFile) << " ";
			cmd << arg("MOSYNC_CONFIG=" + configName) << " ";
			cmd << arg("MOSYNC_MODULE_NAME=" + moduleName) << " ";
			string platform = params->getSwitchValue("--platform");
			cmd << arg("MOSYNC_PLATFORM=" + platform) << " ";
			cmd << arg("MOSYNC_LIB_VARIANT=" + libVariant) << " ";
			cmd << arg("NDK_PROJECT_PATH=.") << " ";
			cmd << arg("APP_ABI=" + arch) << " ";
			cmd << arg("APP_PLATFORM=android-" + require(params, "--android-version")) << " ";

			sh(cmd.str().c_str(), !isVerbose);

			string fullOutputDir = outputDir + "android_" + arch + "_" + libVariant + "/";
			_mkdir(fullOutputDir.c_str());
			// TODO: Static libs
			string libFile = tmpBuildDir + "libs/" + arch + "/lib" + moduleName + ".so";
			copyFile((fullOutputDir + "lib" + moduleName + ".so").c_str(), libFile.c_str());
		}
	}
	return 0;
}