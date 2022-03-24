FROM ubuntu:latest

ENV REDIS_LOC     /usr/src/redis
ENV SMDK_BIN      /usr/src/SMDK_bin

COPY libcxlmalloc.so /usr/lib/
COPY redis-6.2.1/    $REDIS_LOC
COPY redis.summary.conf /usr/local/etc/redis/

# add our user and group first to make sure their IDs get assigned consistently, regardless of whatever dependencies get added
RUN groupadd -r -g 999 redis && useradd -r -g redis -u 999 redis
 
# build redis
RUN set -eux; \
    \
	savedAptMark="$(apt-mark showmanual)"; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        ca-certificates \
        dpkg-dev \
        gcc \
        libc6-dev \
        libssl-dev \
        make \
    ; \
    rm -rf /var/lib/apt/lists/*; \
    \
# disable Redis protected mode [1] as it is unnecessary in context of Docker
# (ports are not automatically exposed when running inside Docker, but rather explicitly by specifying -p / -P)
# [1]: https://github.com/redis/redis/commit/edd4d555df57dc84265fdfb4ef59a4678832f6da
    grep -E '^ *createBoolConfig[(]"protected-mode",.*, *1 *,.*[)],$' $REDIS_LOC/src/config.c; \
    sed -ri 's!^( *createBoolConfig[(]"protected-mode",.*, *)1( *,.*[)],)$!\10\2!' $REDIS_LOC/src/config.c; \
    grep -E '^ *createBoolConfig[(]"protected-mode",.*, *0 *,.*[)],$' $REDIS_LOC/src/config.c; \
    \
    cd $REDIS_LOC; \
    make -j MALLOC=libc
    make install
    cp $SMDK_BIN/bin/* /usr/local/bin; \
# TODO https://github.com/antirez/redis/pull/3494 (deduplicate "redis-server" copies)
    export serverMd5="$(md5sum /usr/local/bin/redis-server | cut -d' ' -f1)"; \
    find /usr/local/bin/redis* -maxdepth 0 \
        -type f -not -name redis-server \
        -exec sh -eux -c ' \
            md5="$(md5sum "$1" | cut -d" " -f1)"; \
            test "$md5" = "$serverMd5"; \
        ' -- '{}' ';' \
        -exec ln -svfT 'redis-server' '{}' ';' ; \
    cd / && rm -rf $REDIS_LOC $SMDK_BIN; \
	apt-mark auto '.*' > /dev/null; \
	[ -z "$savedAptMark" ] || apt-mark manual $savedAptMark > /dev/null; \
    find /usr/local -type f -executable -exec ldd '{}' ';' \
        | awk '/=>/ { print $(NF-1) }' \
        | sort -u \
		| rev | cut -d/ -f1 | rev \
        | xargs -r dpkg-query --search \
        | cut -d: -f1 \
        | sort -u \
        | xargs -r apt-mark manual \
    ; \
	apt-get purge -y --auto-remove -o APT::AutoRemove::RecommendsImportant=false; \
	\
    redis-cli --version; \
    redis-server --version
 
 
RUN mkdir /data && chown redis:redis /data
VOLUME /data
WORKDIR /data
 
COPY docker-entrypoint.sh /usr/local/bin
ENTRYPOINT ["docker-entrypoint.sh"]
 
EXPOSE 6379
CMD ["redis-server", "/usr/local/etc/redis/redis.summary.conf"]
