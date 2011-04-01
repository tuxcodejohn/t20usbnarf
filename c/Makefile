# usage:
# - make	- builds the executable
# - make test	- builds test environment
# - make clean	- cleans the working directory

# ====> CONFIGURATION <====

TEST_DIR=test
BUILD_DIR=build

CC=gcc
CFLAGS=-c -Wall -g -I/usr/include/libusb-1.0/ -I/usr/include/ImageMagick
LDFLAGS=-lusb-1.0 -lpthread -lMagickWand

EXECUTABLE=beamer

# ====> DETECTING FILES <====

SOURCES=$(wildcard src/*.c) $(wildcard src/sites/*.c)
HEADER=$(wildcard src/*.h) $(wildcard src/sites/*.h)

# ====> CALCULATING SOME PATHS <====

OBJECTS=${SOURCES:src/%.c=${BUILD_DIR}/%.o}

EXEC_FILE=${EXECUTABLE:%=${BUILD_DIR}/%}
TEST_FILE=${EXECUTABLE:%=${TEST_DIR}/%}

# ====> META TARGETS <====

all: link

compile: ${OBJECT}

link: ${EXEC_FILE}

# ====> ACTUAL WORK <====

clean:
	rm -rf ${BUILD_DIR}
	rm -rf ${TEST_DIR}

test: link
	@mkdir -p ${TEST_DIR}
	cp ${EXEC_FILE} ${TEST_FILE}

${EXEC_FILE}: ${OBJECTS}
	@mkdir -p `dirname $@`
	${CC} -o $@ ${OBJECTS} ${LDFLAGS}

${BUILD_DIR}/%.o: src/%.c ${HEADER} Makefile
	@mkdir -p `dirname $@`
	${CC} ${CFLAGS} -o $@ $<

.PHONY: compile all clean link

