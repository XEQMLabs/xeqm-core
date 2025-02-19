RELEASE=9.1.0
docker build -t equilibriad:${RELEASE} -f Dockerfile.equilibriad --build-arg USER_ID=$(id -u) --build-arg GROUP_ID=$(id -g) .
