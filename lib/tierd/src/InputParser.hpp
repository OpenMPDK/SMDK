#ifndef INPUT_PARSER_H
#define INPUT_PARSER_H

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <set>
#include <iostream>
#include <numa.h>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <getopt.h>

using namespace std;

enum TierdOpcodes {
	BAD_OPTION,
	MLC_PATH,
#ifdef ARCH_AMD
	AMD_PCM_PATH
#endif
};

class InputParser 
{
	private:
		const bool INVALID = false;
		const bool VALID = true;

		const int MIN_WINDOW = 1;
		const int MIN_INTERVAL = 1000000;
		const int DEFAULT_WINDOW = 4;
		const int DEFAULT_INTERVAL = 1000000;
		const bool DEFAULT_DAEMON = false;

		const string MLC_FILENAME = "mlc";
		const string AMD_PCM_FILENAME = "AMDuProfPcm";

		char **argv;
		int argc;

		int window;
		int interval;
		bool daemonize;
		char *configFile = (char *)"tierd.conf";
		string mlcFilePath;
		string amdPcmPath;
		map<string, string> configKeyValue;
		set<string> configKey;
		bool inputParserValid;

		void formattingInputParam(int _argc, char *_argv[]);

		bool parsingInputParam();
		bool parsingConfigFile();
		bool parsingKeyValue();

		bool fileExists(const string& filename);
		string toLower(const string& str);
		string getValue(const string& key);

		void usage();

	public:
		InputParser(int _argc, char* _argv[]);
		int getWindow();
		int getInterval();
		bool getDaemonize();
		bool isValid();
		string getPcmPath();
		string getPcmFileName();
		string getMlcPath();
		string getMlcFileName();
		~InputParser();

	protected:
};

#endif
