# Synthetic Aperture Radar (SAR) point target simulator.
# USAGE: datagen <Data output file>
#                <Platform position outputfile>
#                <Pulse transmission time output file>
#                <Radar parameters>
# NOTES: The code expects Targets.txt and Parameters.txt to be
# located in the working directory.

INSTALL_DIR_1 = ../../ocr/huge
INSTALL_DIR_2 = ../../ocr/problem_size_scaling

RM=rm -f
CP=cp -f

all:	Radar.txt

Radar.txt:	Parameters.txt Targets.txt Makefile
	../datagen Data.bin PlatformPosition.bin PulseTransmissionTime.bin Radar.txt

run:	all
	../../utils/viewData 1 500.0 <Data.bin >ref.bmp
	../../utils/viewData 2 250.0 <Data.bin >cur.bmp

clean:
	$(RM)	Data.bin PlatformPosition.bin PulseTransmissionTime.bin Radar.txt \
		ref.bmp cur.bmp

install:
	$(CP)	Data.bin PlatformPosition.bin PulseTransmissionTime.bin \
		$(INSTALL_DIR_1)
	$(CP)	Data.bin PlatformPosition.bin PulseTransmissionTime.bin \
		$(INSTALL_DIR_2)

uninstall:
	$(RM)	$(INSTALL_DIR_1)/Data.bin \
		$(INSTALL_DIR_1)/PlatformPosition.bin \
		$(INSTALL_DIR_1)/PulseTransmissionTime.bin \
		$(INSTALL_DIR_2)/Data.bin \
		$(INSTALL_DIR_2)/PlatformPosition.bin \
		$(INSTALL_DIR_2)/PulseTransmissionTime.bin
