mrpeach - bag of tricks by Martin Peach
===

This is a fork of the original mrpeach repository hosted at:

  http://git.puredata.info/cgit/svn2git/libraries/mrpeach.git

The fork was made to make it easy for the fork maintainer
to provide stable builds for deken. The original sources as
provided by upstream are in the upstream branch, all modifications
are done in the master branch. 

The build system was changed to use pd-lib-builder Makefile written
by Katja Vetter. You need to include it as submodule when cloning:

`git clone --recursive http://github.com/reduzent/pd-mrpeach`

Currently supported deken builds from these sources include:

## binfile
read and write raw files (as bytes)

## midifile
read and write midi files

## osc
classes for creating and parsing OSC messages

## slip
encoding and decoding SLIP (Serial Line Internet Protocol)
