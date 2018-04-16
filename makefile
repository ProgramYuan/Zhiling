CC = gcc
CFLAGS = -Wall -O -g
DIR_SRC = ./src

.PHONY:all

all: SmartHome BuildGrammar SpeechRecognizer RgbLed IRSend IRReceive createFIFO tts
	
SmartHome : $(DIR_SRC)/miniPickup.c
	@$(CC) $(DIR_SRC)/miniPickup.c -o $(TARGET) -lasound

BuildGrammar : $(DIR_SRC)/BuildGrammar.c
	@$(CC) $(DIR_SRC)/BuildGrammar.c -L . -L. -o BuildGrammar -lmsc -ldl -lpthread -lm -lrt

SpeechRecognizer : $(DIR_SRC)/Recognition.c
	@$(CC) $(DIR_SRC)/cJSON.c $(DIR_SRC)/Recognition.c -L . -L. -o SpeechRecognizer -lmsc -ldl -lm -lrt

IRSend : $(DIR_SRC)/IRSend.c
	@$(CC) -o IRSend -L . -I ../libMatrix/includes/ $(DIR_SRC)/IRSend.c -lfahw -lm -Wall

IRReceive : $(DIR_SRC)/IReceive.c
	@$(CC) -o IRReceive -L . -I ../libMatrix/includes/ $(DIR_SRC)/IReceive.c -lfahw -lm -Wall

RgbLed : $(DIR_SRC)/RgbLed.c
	@$(CC) $(DIR_SRC)/RgbLed.c -o RgbLed -lwiringPi -lpthread

createFIFO : $(DIR_SRC)/createFIFO.c
	@$(CC) $(DIR_SRC)/createFIFO.c -o createFIFO

tts : $(DIR_SRC)/tts.c
	@$(CC) $(DIR_SRC)/tts.c -L . -L. -o tts -lmsc -ldl -lm -lrt
 
clean:
	@rm -rf *.o SmartHome BuildGrammar SpeechRecognizer RgbLed IRSend IRReceive createFIFO tts