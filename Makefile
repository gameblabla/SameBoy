PRGNAME     = sameboy.elf

CC          = gcc

# add SDL dependencies
SDL_LIB     = 

# change compilation / linking flag options
DEFINES		= -D_GNU_SOURCE -DVERSION="$(VERSION)" -D_USE_MATH_DEFINES -DDISABLE_TIMEKEEPING -DGB_INTERNAL
CFLAGS		= -Ofast -march=native -mtune=native -fomit-frame-pointer -fdata-sections -ffunction-sections -std=gnu11 $(DEFINES)
CFLAGS     += -ISDL -ICore -I.
LDFLAGS     = -lSDL -lm -Wl,--as-needed -Wl,--gc-sections -flto -s

# Files to be compiled
SRCDIR   	= ./Core ./SDL
VPATH     	= $(SRCDIR)
SRC   		= $(foreach dir, $(SRCDIR), $(wildcard $(dir)/*.c))
OBJS   		= $(notdir $(patsubst %.c, %.o, $(SRC)))

# Rules to make executable
$(PRGNAME): $(OBJS)  
	$(CC) $(CFLAGS) -o $(PRGNAME) $^ $(LDFLAGS)

$(OBJS) : %.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(PRGNAME)$(EXESUFFIX) *.o
