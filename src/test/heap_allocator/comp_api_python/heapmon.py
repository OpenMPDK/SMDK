#!/usr/bin/env python
#coding=utf8

import gc
import os
import time
import argparse

##########################################################################################
class MemMonitor:
	def __init__(self, with_guppy = False):
		self.pid = os.getpid()
		self.enabled = False
		try:
			if with_guppy:
				from guppy import hpy
				self.enabled = True
		except:
			pass
		if self.enabled:
			self._h = hpy()
		self.hsize = 0
		self.hdiff = 0
	@staticmethod
	def getVmRSS(pid):
		with open('/proc/%d/status' % pid) as ifp:
			for line in ifp:
				if 'VmRSS' in line:
					eles = line.split()
					if eles[-1].lower() == 'kb':
						return int(eles[-2]) * 1024
					if eles[-1].lower() == 'b':
						return int(eles[-2])
					if eles[-1].lower() == 'mb':
						return int(eles[-2]) * 1024 * 1024
		return 0
	@staticmethod
	def getReadableSize(lv):
		if not isinstance(lv, (int, int)):
			return '0'
		if lv >= 1024*1024*1024*1024:
			s = "%4.2f TB" % (float(lv)/(1024*1024*1024*1024))
		elif lv >= 1024*1024*1024:
			s = "%4.2f GB" % (float(lv)/(1024*1024*1024))
		elif lv >= 1024*1024:
			s = "%4.2f MB" % (float(lv)/(1024*1024))
		elif lv >= 1024:
			s = "%4.2f KB" % (float(lv)/1024)
		else:
			s = "%d B" % lv
		return s
	def __repr__(self):
		# if not self.enabled:
		# 	return 'Not enabled. guppy module not found!'
		if self.hdiff > 0:
			s = 'Total %s, %s increased' % \
				(self.getReadableSize(self.hsize), self.getReadableSize(self.hdiff))
		elif self.hdiff < 0:
			s = 'Total %s, %s decreased' % \
				(self.getReadableSize(self.hsize), self.getReadableSize(-self.hdiff))
		else:
			s = 'Total %s, not changed' % self.getReadableSize(self.hsize)
		return s
	def getHeap(self):
		if not self.enabled:
			return None
		return str(self._h.heap())
	def check(self, msg=''):
		# if not self.enabled:
		# 	return 'Not enabled. guppy module not found!'
		if not self.enabled:
			chsize = self.getVmRSS(self.pid)
		else:
			hdr = self.getHeap().split('\n')[0]
			chsize = int(hdr.split()[-2])
		self.hdiff = chsize - self.hsize
		self.hsize = chsize
		return '%s: %s'% (msg, str(self))

##########################################################################################
parser = argparse.ArgumentParser()
parser.add_argument('-iter', help=' : iterations', default=50000000)
args = parser.parse_args()

def test(hm, args):
	print(hm.check('Before allocation'))
	# print 'Before allocating ', rss(),

	iterations = int(args.iter)
	l = {}
	for i in range(iterations):
		l[i] = ({})

	# print 'After allocating  ', rss(),
	print(hm.check('After allocation'))

	# Ignore optimizations, just try to free whatever possible

	# 1st deallocation
	for i in range(iterations):
		l[i] = None
	print(hm.check('After 1st deallocation'))

	# 2nd deallocation
	l.clear()
	print(hm.check('After 2nd deallocation'))

	# 3rd deallocation
	l = None
	del l
	print(hm.check('After 3rd deallocation'))

	# Control shot
	gc.collect()
	print(hm.check('After gc'))


##########################################################################################

if __name__ == '__main__':
    hm = MemMonitor()
    repeat = 10
    for i in range(repeat):
        print()
        test(hm, args)
        time.sleep(2)
