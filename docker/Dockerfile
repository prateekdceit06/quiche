FROM ubuntu:latest

ENV TZ=America/New_York
ENV DEBIAN_FRONTEND=noninteractive

# Install required packages
RUN apt-get update && apt-get install -y \
    curl \
    net-tools \
    iproute2 \
    zsh \
    git \
    build-essential \
    libicu-dev \
    vim \
    nano \
    tzdata \
    iputils-ping \
    iperf3 \
    wget \
    ca-certificates \
    locales \
    && ln -snf /usr/share/zoneinfo/$TZ /etc/localtime \
    && echo $TZ > /etc/timezone \
    && rm -rf /var/lib/apt/lists/*

# Set UTF-8 as default locale (optional but often helpful)
RUN locale-gen en_US.UTF-8
ENV LANG=en_US.UTF-8
ENV LANGUAGE=en_US:en
ENV LC_ALL=en_US.UTF-8

# Set working directory
WORKDIR /root

# Clone your repo
RUN git clone https://github.com/prateekdceit06/quiche.git

# Install Bazelisk
RUN curl -Lo /usr/local/bin/bazel https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-arm64 \
    && chmod +x /usr/local/bin/bazel

# Set working directory to quiche and build
WORKDIR /root/quiche
RUN bazel build //quiche:quic_client || true

# Install Oh My Zsh (unattended)
RUN sh -c "$(curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh)" "" --unattended

# Customize prompt with [DOCKER] in ~/.zshrc
RUN echo 'PROMPT="%F{red}[DOCKER]%f %F{blue}%n@%m%f:%~ %# "' >> /root/.zshrc

# Set zsh as the default shell
CMD ["zsh"]
