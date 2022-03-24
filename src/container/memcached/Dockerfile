FROM ubuntu:latest

COPY libcxlmalloc.so /usr/lib/ 
COPY memcached-1.6.9/ /usr/src/memcached/

# add our user and group first to make sure their IDs get assigned consistently, regardless of whatever dependencies get added
RUN groupadd --system --gid 11211 memcache && useradd --system --gid memcache --uid 11211 memcache

# ensure SASL's "libplain.so" is installed as per https://github.com/memcached/memcached/wiki/SASLHowto
RUN set -eux; \
	apt-get update; \
	apt-get install -y --no-install-recommends \
		libsasl2-modules \
	; \
	rm -rf /var/lib/apt/lists/*

ENV MEMCACHED_VERSION 1.6.10
ENV MEMCACHED_SHA1 cb5b9fe77a2a59cc6cc7103a415bc07df9ddc6ec

RUN set -x \
	\
	&& savedAptMark="$(apt-mark showmanual)" \
	&& apt-get update \
	&& apt-get install -y --no-install-recommends \
		ca-certificates \
		dpkg-dev \
		gcc \
		libc6-dev \
		libevent-dev \
		libio-socket-ssl-perl \
		libsasl2-dev \
		libssl-dev \
		make \
		perl \
		wget \
	&& rm -rf /var/lib/apt/lists/* \
	\
#	&& wget -O memcached.tar.gz "https://memcached.org/files/memcached-$MEMCACHED_VERSION.tar.gz" \
#	&& echo "$MEMCACHED_SHA1  memcached.tar.gz" | sha1sum -c - \
#	&& mkdir -p /usr/src/memcached \
#	&& tar -xzf memcached.tar.gz -C /usr/src/memcached --strip-components=1 \
#	&& rm memcached.tar.gz \
	\
	&& cd /usr/src/memcached \
	\
	&& gnuArch="$(dpkg-architecture --query DEB_BUILD_GNU_TYPE)" \
	&& enableExtstore="$( \
# https://github.com/docker-library/memcached/pull/38
		case "$gnuArch" in \
# https://github.com/memcached/memcached/issues/381 "--enable-extstore on s390x (IBM System Z mainframe architecture) fails tests"
			s390x-*) ;; \
			*) echo '--enable-extstore' ;; \
		esac \
	)" \
	&& ./configure \
		--build="$gnuArch" \
		--enable-sasl \
		--enable-sasl-pwdb \
		--enable-tls \
		$enableExtstore \
	&& nproc="$(nproc)" \
	&& make -j "$nproc" \
	\
# see https://github.com/docker-library/memcached/pull/54#issuecomment-562797748 and https://bugs.debian.org/927461 for why we have to munge openssl.cnf
	&& sed -i.bak 's/SECLEVEL=2/SECLEVEL=1/g' /etc/ssl/openssl.cnf \
	&& make test PARALLEL="$nproc" \
	&& mv /etc/ssl/openssl.cnf.bak /etc/ssl/openssl.cnf \
	\
	&& make install \
	\
	&& cd / && rm -rf /usr/src/memcached \
	\
	&& apt-mark auto '.*' > /dev/null \
	&& apt-mark manual $savedAptMark > /dev/null \
	&& find /usr/local -type f -executable -exec ldd '{}' ';' \
		| awk '/=>/ { print $(NF-1) }' \
		| sort -u \
		| rev | cut -d/ -f1 | rev \
		| xargs -r dpkg-query --search \
		| cut -d: -f1 \
		| sort -u \
		| xargs -r apt-mark manual \
	&& apt-get purge -y --auto-remove -o APT::AutoRemove::RecommendsImportant=false \
	\
	&& memcached -V

COPY docker-entrypoint.sh /usr/local/bin/
RUN ln -s usr/local/bin/docker-entrypoint.sh /entrypoint.sh # backwards compat
ENTRYPOINT ["docker-entrypoint.sh"]

USER memcache
EXPOSE 11211
CMD ["memcached"]
