# Target library
outputs :=\
	fs.o\
	 disk.o

lib := libfs.a

all: $(lib)

ifneq ($(v),1)
Q = @
endif

CC = gcc

CFLAGS := -Wall -Werror
CFLAGS += -g

DEPFLAGS = -MMD -MF $(@:.o=.d)

$(lib): $(outputs)
	@echo "make lib"
	$(Q)ar rcs $(lib) $(outputs)

%.o: %.c
	@echo "CC $@"
	$(Q)$(CC) $(CFLAGS) -c $< $(DEPFLAGS)

clean:
	$(Q)rm $(lib) *.o *.d
