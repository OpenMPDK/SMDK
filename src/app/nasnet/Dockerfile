FROM ubuntu:latest

ENV PYTHON_VER  Python-3.7.12
ENV SMDK_BIN    /usr/src/SMDK_bin

COPY libcxlmalloc.so   /usr/lib/
COPY $PYTHON_VER       /usr/src/$PYTHON_VER/

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
		numactl \
    ; \
	cd /usr/src/$PYTHON_VER; \
    ./configure --prefix=$SMDK_BIN && make -j "$(nproc)" && make install; \
	cp -r $SMDK_BIN/* /usr/local; \
	cd / && rm -rf $SMDK_BIN /usr/src/$PYTHON_VER; \
	python3 -m pip install --upgrade pip; \
	python3 -V && pip3 -V

##################################################
# Install packages
##################################################
RUN pip3 install tensorflow==1.15.0; \
	pip3 install numpy==1.19.5; \
	pip3 install psutil; \
	pip3 install matplotlib; \
    pip3 install tf_slim
