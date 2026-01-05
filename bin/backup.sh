#!/bin/sh


d=`date +"%Y-%m-%d_%H%M%S"`

tar -czf backup/$d.player.tgz player
tar -czf backup/$d.log.tgz log
