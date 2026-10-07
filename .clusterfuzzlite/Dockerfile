FROM gcr.io/oss-fuzz-base/base-builder:v1
RUN apt-get update && apt-get install -y zip
COPY . $SRC/oyl
WORKDIR $SRC/oyl
COPY ./.clusterfuzzlite/build.sh $SRC/
