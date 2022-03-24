FROM ubuntu:20.04


RUN mkdir /usr/lib/stream
RUN mkdir /usr/script


COPY stream/ /usr/lib/stream
COPY build_lib.sh /usr/lib
COPY common.sh /usr/script

ENV DEBIAN_FRONTEND=noninteractive
ENV TERM xterm-256color

RUN apt-get -y update && apt-get install -y apt-utils

RUN /usr/lib/build_lib.sh stream





EXPOSE 5001




