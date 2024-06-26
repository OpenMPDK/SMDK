#include "Observer.hpp"
#include "SignalHandler.hpp"

SignalHandler::SignalHandler(std::ostream& _out)
: m_outputStream(_out)
{
	stopFlag = false;
}

SignalHandler::SignalHandler()
: m_outputStream(std::cout)
{
	stopFlag = false;
}

SignalHandler* SignalHandler::getInstance()
{
	if (instance == nullptr) {
		instance = new SignalHandler();
	}
	instance->setSignalHandlerStatus(false);
	instance->observersStop.clear();
	instance->observersChange.clear();
	return instance;
}

SignalHandler* SignalHandler::getInstance(std::ostream& _out)
{
	if (instance == nullptr) {
		instance = new SignalHandler(_out);
	}
	instance->setSignalHandlerStatus(false);
	instance->observersStop.clear();
	instance->observersChange.clear();
	return instance;
}

void SignalHandler::putInstance()
{
	if (instance != nullptr) {
		delete instance;
		instance = nullptr;
	}
}

void SignalHandler::initSignalHandler()
{
	if (instance == nullptr) {
		return;
	} 
	sigemptyset(&instance->sa.sa_mask);
	instance->sa.sa_handler = SignalHandler::signalHandler;
	instance->sa.sa_flags = 0;
	sigaction(SIGUSR1, &instance->sa, NULL);
	sigaction(SIGUSR2, &instance->sa, NULL);
	sigaction(SIGINT, &instance->sa, NULL);

	instance->setSignalHandlerStatus(true);
}

void SignalHandler::setSignalHandlerStatus(bool _status)
{
	if (instance != nullptr)
		instance->status = _status;
}

bool SignalHandler::getSignalHandlerStatus()
{
	if (instance != nullptr)
		return instance->status;
	else
		return false;
}

void SignalHandler::addObserver(Observer *observer)
{
	instance->observersStop.push_back(observer);
	instance->observersChange.push_back(observer);
}

void SignalHandler::notifyObserversStop()
{
	cout << "Node Topology Stop Signal.." << endl;
	for (auto observer : instance->observersStop) {
		observer->notifyFromSignalHandlerStop();
	}
}

void SignalHandler::notifyObserversChange() 
{
	cout << "Node Topology Change Signal.." << endl;
	for (auto observer : instance->observersChange) {
		observer->notifyFromSignalHandlerChange();
	}
}

void SignalHandler::signalHandler(int signum)
{
	if (instance == nullptr) {
		return;
	}

	switch (signum) {
	case SIGUSR1:
		instance->m_outputStream << "GET SIGUSR1" << std::endl;
		instance->notifyObserversChange();
		break;
	case SIGUSR2:
		instance->m_outputStream << "GET SIGUSR2" << std::endl;
		instance->notifyObserversChange();
		break;
	case SIGINT:
		instance->m_outputStream << "GET SIGINT" << std::endl;
		instance->notifyObserversStop();
		instance->stopFlag = true;
		break;
	default:
		break;
	}
}

bool SignalHandler::stopTierd()
{
	return stopFlag;
}
