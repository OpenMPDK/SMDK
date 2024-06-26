#ifndef WORKER_H
#define WORKER_H

class Worker
{
	private:
	public:
		virtual int launch() = 0;
		virtual int terminate() = 0;
	protected:
};

#endif
