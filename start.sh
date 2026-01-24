#!/bin/sh

docker run -d --name balderdash-v5 -p 9000:9000 \
  -v ./area:/app/area \
  -v ./log:/app/log \
  -v ./player:/app/player \
  -v ./races:/app/races \
  -v ./scores:/app/scores \
  -v ./chest:/app/chest \
  -v ./gods:/app/gods \
  -v ./classes:/app/classes \
  myuser/balder  /app/bin/balderdash
