FROM node:24.4.1-alpine3.22

RUN addgroup -S aquacyd && adduser -S -G aquacyd aquacyd
WORKDIR /opt/aquacyd-gateway

COPY --chown=aquacyd:aquacyd package.json ./
COPY --chown=aquacyd:aquacyd src ./src

USER aquacyd
ENV AQUACYD_GATEWAY_HOST=0.0.0.0
ENV AQUACYD_GATEWAY_PORT=8787
EXPOSE 8787

CMD ["node", "src/server.mjs"]
