
SRCS =	src/fir_filter.c \
	src/gen_rand.c \
	src/main.c \
	src/inputs.c

OBJS = $(SRCS:.c=.o)
CUDA_SRCS =
CUDA_OBJS =

CFLAGS = -fopenmp -Wall -O3 -g -DRAG -DRAG_PARAM_ONLY_off
LDFLAGS =
LDLIBS = -lm -lfftw3f

USE_CUDA ?= 0
CUDA_PATH ?= /usr/local/cuda
NVCC ?= $(CUDA_PATH)/bin/nvcc
NVCCFLAGS ?= -O3 -I./src

ifeq ($(USE_CUDA),1)
CFLAGS += -DUSE_CUDA_ACCEL
NVCCFLAGS += -DUSE_CUDA_ACCEL
CUDA_SRCS = src/cuda_accel.cu
CUDA_OBJS = $(CUDA_SRCS:.cu=.o)
LDFLAGS += -L$(CUDA_PATH)/lib64
LDLIBS += -lcudart -lstdc++
endif

RM=rm -f

all: datagen

datagen: $(OBJS) $(CUDA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

run:
	$(MAKE) -C ./huge run
	$(MAKE) -C ./test run

clean:
	$(RM) datagen mpidatagen $(OBJS) $(CUDA_OBJS)
	$(MAKE) -C ./huge clean
	$(MAKE) -C ./test clean

install:
	$(MAKE) -C ./huge install
	$(MAKE) -C ./test install

uninstall:
	$(MAKE) -C ./huge uninstall
	$(MAKE) -C ./test uninstall
#
# not tested
#
mpidatagen: $(OBJS) $(CUDA_OBJS)
	mpicc $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS) -DUSE_MPI

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cu
	$(NVCC) $(NVCCFLAGS) -c -o $@ $<
