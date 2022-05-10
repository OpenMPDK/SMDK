FROM ubuntu:latest

ENV SMDK_BIN    /usr/src/SMDK_bin

COPY libcxlmalloc.so   /usr/lib/

COPY run_nasnet.sh    /usr/src/test/
COPY nasnet_src/      /usr/src/nasnet/
COPY models/          /tmp/checkpoints
COPY imagenet/        /tmp/imagenet

##################################################
# Install python
##################################################
RUN set -eux; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        ca-certificates \
        dpkg-dev \
        gcc \
        libc6-dev \
        libssl-dev \
        make \
		zlib1g-dev \
		libffi-dev \
		cgdb \
		vim \
		numactl

##################################################
# Install packages
##################################################
RUN pip3 install tensorflow==1.15.0; \
	pip3 install numpy==1.19.5; \
	pip3 install psutil; \
	pip3 install matplotlib; \
    pip3 install tf_slim
