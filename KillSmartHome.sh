#!/bin/bash
#filename:KillSmartHome.sh
#author:Allen Yuan

#ps -A|grep "SmartHome"|awk '{print $1}'
pgrep "SmartHome">tmp.pid
varPgrep=$(cat tmp.pid);
if [ ! -n "$varPgrep" ]; then
	:
else
	kill -9 $varPgrep
fi

sleep 1s
#
#ps a|grep "./SmartHome"|awk '{print $1}'>tmp.pid
#varPs=$(cat tmp.pid);
#if [ ! -n "$varPs" ]; then
#	:
#else
#	kill -9 $varPs
#fi

#rm *.pcm

ps a|grep "./SpeechRecognizer"|awk 'NR==1{print $1}'>tmp.pid
varSR=$(cat tmp.pid);
if [ ! -n "$varSR" ]; then
	:
else
	kill -9 $varSR
fi

sleep 1s

ps a|grep "./SmartHome"|awk 'NR==1{print $1}'>tmp.pid
varSH=$(cat tmp.pid);
if [ ! -n "$varSH" ]; then
	:
else
	kill -9 $varSH
fi
