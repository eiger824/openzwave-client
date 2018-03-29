include Makefile.ozw

PROGNAME			= example
PROGRAM 			= bin/${PROGNAME}
CONFIG 				= config

CXX 				= g++

INCLUDES 			= -I${OZW_SRC} -I${OZW_AES} -I${OZW_CMD_CLASSES} -I${OZW_PLATFORM} -I${OZW_VALUE_CLASSES}
DEFINES 			= -DSYSCONFDIR=\"${OZW_SYSCONFDIR}\"

CXXFLAGS			= -c -g --std=c++11 -Wall -Wextra -Wpedantic -fPIC ${INCLUDES} ${DEFINES}
LDFLAGS 			= -fPIC -pthread -L${OZW_ROOT} -lopenzwave

OBJ = main.o \


all: ${PROGRAM}

${PROGRAM}: main.o
	test -d logs || mkdir logs
	test -d bin  || mkdir bin
	${CXX} $^ ${LDFLAGS} -o $@
	ln -s ${PROGRAM} ${PROGNAME}

main.o: main.cpp
	${CXX} ${CXXFLAGS} $< -o $@

clean:
	rm -rf ${PROGRAM} ${OBJ} *.~ bin ${PROGNAME} logs

run:
	LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${OZW_ROOT} ./${PROGRAM}

