#ifndef OBSERVER_H
#define OBSERVER_H

class Observer
{
	private:
	public:
		virtual ~Observer() {}
		// notify SignalHandler -> BandwidthLoader, Monitor
		virtual void notifyFromSignalHandlerStop() = 0;
		virtual void notifyFromSignalHandlerChange() = 0;
		// notify BandwidthLoader -> Monitor
		virtual void notifyFromBandwidthLoader() = 0;
	protected:
};

#endif
