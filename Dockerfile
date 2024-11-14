FROM ubuntu:24.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive

LABEL org.opencontainers.image.authors="rickshell@urdekcah.ru,urdekcah@gmail.com,rickroot30@gmail.com"
LABEL org.opencontainers.image.description="A bespoke development tool that optimizes rickroot's workflow through a personalized shell experience."
LABEL org.opencontainers.image.source="https://github.com/urdekcah/rickshell"
LABEL org.opencontainers.image.vendor="Fishydino"
LABEL org.opencontainers.image.licenses="AGPL-3.0"
LABEL org.opencontainers.image.base.name="ubuntu:24.04"
LABEL security.privileged="false"
LABEL maintainer.team="Rickshell Developers <rickshell@urdekcah.ru>"

ARG BUILD_DATE
ARG VERSION=1.0.0
ARG USER=khonso
ARG UID=10001
ARG GID=10001

ENV DEBIAN_FRONTEND=noninteractive \
  LANG=C.UTF-8 \
  LC_ALL=C.UTF-8 \
  TZ=UTC \
  PATH="/app:${PATH}"

# hadolint ignore=DL3008
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc \
    make \
    git \
    libreadline-dev \
    libncurses5-dev \
    libtinfo-dev \
    wget \
    ca-certificates \
  && rm -rf /var/lib/apt/lists/* \
  && apt-get clean

RUN groupadd -r -g ${GID} ${USER} && \
  useradd -r -g ${USER} -u ${UID} -m -s /bin/bash -c "rickshell default account" ${USER}

WORKDIR /build

COPY --chown=${USER}:${USER} Makefile ./
COPY --chown=${USER}:${USER} include ./include
COPY --chown=${USER}:${USER} lib ./lib
COPY --chown=${USER}:${USER} *.c ./

RUN make

FROM ubuntu:24.04 AS runtime

ARG USER=khonso
ARG UID=10001
ARG GID=10001

ENV DEBIAN_FRONTEND=noninteractive \
  LANG=C.UTF-8 \
  LC_ALL=C.UTF-8 \
  TZ=UTC \
  PATH="/app:${PATH}" \
  HOME=/home/khonso

# hadolint ignore=DL3008
RUN apt-get update && apt-get install -y --no-install-recommends \
    libreadline-dev \
    libncurses5-dev \
  && rm -rf /var/lib/apt/lists/* \
  && apt-get clean

RUN groupadd -r -g ${GID} ${USER} && \
  useradd -r -g ${USER} -u ${UID} -m -s /bin/bash -c "rickshell default account" ${USER} && \
  mkdir -p /home/${USER}/.rickshell /app && \
  chown -R ${USER}:${USER} /home/${USER} /app

USER ${USER}

WORKDIR /app

COPY --from=builder --chown=${USER}:${USER} /build/rickshell .
RUN touch /home/${USER}/.rickshell/rickshell.log
ENTRYPOINT ["/app/rickshell"]
CMD []