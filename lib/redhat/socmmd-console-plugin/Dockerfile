FROM registry.access.redhat.com/ubi8/nodejs-16:latest AS build
USER root
RUN command -v yarn || npm i -g yarn

ADD . /usr/src/app
WORKDIR /usr/src/app
RUN yarn install && yarn build
RUN /bin/cp -f plugin-manifest/plugin-manifest.json dist/
WORKDIR /usr/src/app/dist
RUN mv $(echo plugin-entry.*.js) plugin-entry.js

FROM registry.access.redhat.com/ubi8/nginx-120:latest

COPY --from=build /usr/src/app/dist /usr/share/nginx/html
USER 1001

ENTRYPOINT ["nginx", "-g", "daemon off;"]