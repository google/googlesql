################################################################################
#                                     BUILD                                    #
################################################################################

FROM ubuntu:22.04 as builder

# Setup java
RUN apt-get update && apt-get -qq install -y default-jre default-jdk

# Install prerequisites for bazel
RUN apt-get -qq install curl tar build-essential wget python3 zip unzip

ENV BAZEL_VERSION=7.6.1

RUN apt install apt-transport-https curl gnupg -y
RUN curl -fsSL https://bazel.build/bazel-release.pub.gpg | gpg --dearmor >bazel-archive-keyring.gpg
RUN mv bazel-archive-keyring.gpg /usr/share/keyrings
RUN echo "deb [arch=amd64 signed-by=/usr/share/keyrings/bazel-archive-keyring.gpg] https://storage.googleapis.com/bazel-apt stable jdk1.8" | tee /etc/apt/sources.list.d/bazel.list

RUN apt update && apt -qq install -y bazel-${BAZEL_VERSION}
RUN ln -s /usr/bin/bazel-${BAZEL_VERSION} /usr/bin/bazel

RUN apt-get update && DEBIAN_FRONTEND="noninteractive"                         \
    TZ="America/Los_Angeles" apt-get install -y tzdata

RUN apt-get -qq install -y software-properties-common
RUN add-apt-repository ppa:ubuntu-toolchain-r/test                          && \
    apt-get -qq update                                                      && \
    apt-get -qq install -y make rename git ca-certificates libgnutls30


# To support fileNames with non-ascii characters
RUN apt-get -qq install locales && locale-gen en_US.UTF-8
ENV LANG=en_US.UTF-8

COPY . /googlesql

# Create a new user googlesql to avoid running as root.
RUN useradd -ms /bin/bash googlesql
RUN chown -R googlesql:googlesql /googlesql
USER googlesql

ENV HOME=/home/googlesql
RUN mkdir -p $HOME/bin

RUN cd googlesql && ./docker_build.sh execute_query

ENV PATH=$PATH:$HOME/bin

WORKDIR /googlesql

################################################################################
#                                COPY STAGE                                    #
# This stage copies only the built binary from 'builder'.                      #
################################################################################
FROM ubuntu:22.04

# Setup the dedicated, non-root user and environment
# (Duplicate user/path setup is necessary for the final image)
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates libgnutls30 tzdata locales && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

RUN useradd -ms /bin/bash googlesql
ENV HOME=/home/googlesql
ENV PATH=$PATH:$HOME/bin

# Set the final working directory
WORKDIR /googlesql

# Copy only the final artifacts from the 'builder' stage.
COPY --from=builder --chown=googlesql:googlesql $HOME/bin/execute_query /googlesql/execute_query

# Use the non-root user for running the container
USER googlesql

# Command to run the final application
ENTRYPOINT ["/googlesql/execute_query"]
CMD ["--help"]
