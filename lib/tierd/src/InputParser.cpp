#include "InputParser.hpp"

using namespace std;

InputParser::InputParser(int _argc, char* _argv[]) 
{
	configKey.insert("mlc_path");
#ifdef ARCH_AMD
	configKey.insert("amd_uprofpcm_path");
#endif
	formattingInputParam(_argc, _argv);
	bool inputParamValid = parsingInputParam();
	bool configFileValid = parsingConfigFile();
	bool keyValueValid = parsingKeyValue();

	inputParserValid = inputParamValid && configFileValid && keyValueValid;
}

void InputParser::formattingInputParam(int _argc, char *_argv[])
{
	argc = _argc;
	argv = (char **)malloc(sizeof(char *) * argc);
	for (int i = 0; i < argc; i++) {
		int len = strlen(_argv[i]);
		argv[i] = (char *)malloc(sizeof(char) * (len + 1));
		strncpy(argv[i], _argv[i], len);
		argv[i][len] = '\0';
	}
}

bool InputParser::parsingInputParam()
{
	int opt;
	int inputWindow, inputInterval;
	bool inputDaemonize;

	window = DEFAULT_WINDOW;
	interval = DEFAULT_INTERVAL;
	daemonize = DEFAULT_DAEMON;

	while ((opt = getopt(argc, argv, "i:w:c:hD")) != -1) {
		switch(opt) {
			case 'w':
				inputWindow = atoi(optarg);
				if (inputWindow < MIN_WINDOW) {
					cout << "Input Monitor Window is too small."
						<< " Change it to " << MIN_WINDOW << endl;
					inputWindow = MIN_WINDOW;
				}
				window = inputWindow;
				break;
			case 'i':
				inputInterval = atoi(optarg);
				if (inputInterval < MIN_INTERVAL) {
					cout << "Input Monitor Interval is too small."
						<< " Change it to " << MIN_INTERVAL << endl;
					inputInterval = MIN_INTERVAL;
				}
				interval = inputInterval;
				break;
			case 'c':
				configFile = optarg;
				break;
			case 'D':
				inputDaemonize = true;
				daemonize = inputDaemonize;
				break;
			case 'h':
			default:
				usage();
				break;
		}
	}

	return (daemonize && daemon(0, 0)) ? INVALID : VALID;
}

bool InputParser::parsingConfigFile()
{
	if (!fileExists(configFile)) {
		cerr << "Failed to access config file" << endl;
		return INVALID;
	} 

	ifstream file(configFile);
	if (!file.is_open()) {
		cerr << "Failed to open config file" << endl;
		return INVALID;
	}

	string line;
	while (getline(file, line)) {
		istringstream lineStream(line);
		string key, value;
		if (getline(lineStream, key, '=') && getline(lineStream, value)) {
			string lowerKey = toLower(key);
			if (configKey.find(lowerKey) != configKey.end()) {
				configKeyValue[lowerKey] = value;
			}
		}
	}

	file.close();

	return VALID;
}

bool InputParser::parsingKeyValue()
{
	mlcFilePath = getValue("MLC_PATH");
	if (mlcFilePath.size() == 0) {
		cerr << "Cannot access to MLC Path" << endl;
		return INVALID;
	}

	if (basename(mlcFilePath.c_str()) != MLC_FILENAME) {
		cerr << "Invalid MLC Filename" << endl;
		return INVALID;
	}

#ifdef ARCH_AMD
	amdPcmPath = getValue("AMD_UPROFPCM_PATH");
	if (amdPcmPath.size() == 0) {
		cerr << "Cannot access to AMD PCM Path" << endl;
		return INVALID;
	}

	if (basename(amdPcmPath.c_str()) != AMD_PCM_FILENAME) {
		cerr << "Invalid AMD PCM Filename" << endl;
		return INVALID;
	}
#endif

	return VALID;
}

bool InputParser::fileExists(const string& filename)
{
	ifstream file(filename);
	if (file.is_open()) {
		file.close();
		return true;
	}
	return false;
}

string InputParser::toLower(const string& str)
{
	string lowerStr = str;
	transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), ::tolower);
	return lowerStr;
}

string InputParser::getValue(const string& key)
{
	string lowerKey = toLower(key);
	auto it = configKeyValue.find(lowerKey);
	if (it != configKeyValue.end()) {
		if (it->second.size() == 0)
			return "";
		if (fileExists(it->second)) {
			cerr << it->first << " : " << it->second << endl;
			return it->second;
		}
		cerr << "Wrong " << it->first << " (Cannot access file)" << endl;
		return "";
	}

	cerr << "Invalid Key : " << key << endl;
	return "";
}

void InputParser::usage()
{
	cerr << "Usage: tierd [-hD]"
		<< " [-c config_file]"
		<< " [-i interval_in_us(>= 1000000)"
		<< " [-w window_size(>= 1)" << endl;
	exit(EXIT_FAILURE);
}

int InputParser::getWindow()
{
	return window;
}

int InputParser::getInterval()
{
	return interval;
}

bool InputParser::getDaemonize()
{
	return daemonize;
}

bool InputParser::isValid()
{
	return inputParserValid;
}

string InputParser::getPcmPath()
{
#ifdef ARCH_AMD
	return amdPcmPath;
#endif
	return "";
}

string InputParser::getPcmFileName()
{
#ifdef ARCH_AMD
	return AMD_PCM_FILENAME;
#endif
	return "";
}

string InputParser::getMlcPath()
{
	return mlcFilePath;
}

string InputParser::getMlcFileName()
{
	return MLC_FILENAME;
}

InputParser::~InputParser() 
{
	for (int i = 0; i < argc; i++) {
		delete[] argv[i];
	}
	delete[] argv;
}
