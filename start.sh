#!/bin/sh

docker run -ti --name balderdash-v1 -p 9000:9000 -v ./log:/app/log -v ./player:/app/player -v ./races:/app/races -v ./scores:/app/scores -v ./chest:/app/chest -v ./gods:/app/gods -v ./classes:/app/classes myuser/balder  bash
