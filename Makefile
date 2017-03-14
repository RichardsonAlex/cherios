BUILDDIR=build

default: all

all: build/build.ninja
	cd ${BUILDDIR} && ninja

build:
	mkdir -p ${BUILDDIR}
	ln -sf ${BUILDDIR}/boot/cherios.elf .

build/build.ninja: build
	cd ${BUILDDIR} && cmake -GNinja ..

clean:
	rm -rfv ${BUILDDIR}
