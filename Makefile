PACKAGE    = apngasm
CC         = gcc
SRC_DIRS   = . 7z zopfli
CFLAGS     = -Wall -pedantic -DFEATURE_7ZIP -DFEATURE_ZOPFLI -DFEATURE_LIBDEFLATE
CFLAGS_OPT = -O3 -fomit-frame-pointer
CFLAGS_7Z  = -Wno-sign-compare -Wno-reorder -Wno-maybe-uninitialized -Wno-parentheses
LIBS       = -lstdc++ -lpng -lz -ldeflate -lm
PREFIX    ?= /usr/local

INCUDE_DIRS := $(addprefix -I./, $(SRC_DIRS))
OBJ_DIRS := $(addprefix obj/, $(SRC_DIRS))
OBJECTS := $(addprefix obj/, $(wildcard $(addsuffix /*.c*, $(SRC_DIRS))))
OBJECTS := $(OBJECTS:.c=.o)
OBJECTS := $(OBJECTS:.cc=.o)
OBJECTS := $(OBJECTS:.cpp=.o)

all : $(PACKAGE)

$(PACKAGE) : objdirs $(OBJECTS)
	$(CC) -o $@ $(OBJECTS) -s $(LIBS)

objdirs :
	mkdir -p $(OBJ_DIRS)

obj/%.o : %.cpp
	$(CC) -o $@ -c $< $(INCUDE_DIRS) $(CFLAGS) $(CFLAGS_OPT)

obj/%.o : %.c
	$(CC) -o $@ -c $< $(INCUDE_DIRS) $(CFLAGS) $(CFLAGS_OPT)

obj/%.o : %.cc
	$(CC) -o $@ -c $< $(INCUDE_DIRS) $(CFLAGS) $(CFLAGS_OPT) $(CFLAGS_7Z)

.PHONY : clean install uninstall

install : $(PACKAGE)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(PACKAGE) $(DESTDIR)$(PREFIX)/bin/$(PACKAGE)

uninstall :
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PACKAGE)

clean : 
	rm -rf $(PACKAGE) obj
