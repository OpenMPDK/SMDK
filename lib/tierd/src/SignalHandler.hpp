#include <vector>
#include <csignal>
#include <ostream>
#include <iostream>

#include "Observer.hpp"

using namespace std;

class SignalHandler {
private:
	static SignalHandler* instance;
	SignalHandler();
	SignalHandler(std::ostream& _out);
	~SignalHandler(){}
	SignalHandler(const SignalHandler& other) = delete;
	SignalHandler& operator=(const SignalHandler& other) = delete;

	bool status;
	bool stopFlag;
	struct sigaction sa;
	std::ostream& m_outputStream;
	vector<Observer *> observersStop;
	vector<Observer *> observersChange;

public:
	static SignalHandler* getInstance();
	static SignalHandler* getInstance(std::ostream& _out);
	static void putInstance();
	static void signalHandler(int signum);

	bool stopTierd();

	void initSignalHandler();
	bool getSignalHandlerStatus();
	void setSignalHandlerStatus(bool _status);

	void notifyObserversStop();
	void notifyObserversChange();
	void addObserver(Observer *observer);
};
