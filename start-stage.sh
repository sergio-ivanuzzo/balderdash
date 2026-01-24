#!/bin/sh

docker run -d --name balderdash-stage -p 9001:9000 \
  -v ./run/stage/area:/app/area \
  -v ./run/stage/log:/app/log \
  -v ./run/stage/player:/app/player \
  -v ./run/stage/races:/app/races \
  -v ./run/stage/scores:/app/scores \
  -v ./run/stage/chest:/app/chest \
  -v ./run/stage/gods:/app/gods \
  -v ./run/stage/classes:/app/classes \
  myuser/balder  /app/bin/balderdash
