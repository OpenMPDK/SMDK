FROM ubuntu:latest

ENV SMDK_BIN    /usr/src/SMDK_bin

COPY libcxlmalloc.so   /usr/lib/

COPY run_bert.sh      /usr/src/
COPY bert_src/        /usr/src/bert/
COPY models/          /path/to/bert/
COPY glue_data/       /path/to/glue/

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
	pip3 install matplotlib

