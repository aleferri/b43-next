# SPDX-License-Identifier: GPL-2.0
#
# Host build of b43/radar_ac.c with a replay of radar FIFO reads.
#
#   make          build ./replay
#   make check    replay the recorded runs and synthetic bursts (check.sh)
#   make check-synth  the synthetic bursts only, without the captures

B43   := ../../b43-next
STUBS := ../unit/stubs

CFLAGS := -O1 -g -Wall -Wextra -Wno-unused-parameter -Werror \
          -I$(STUBS) -I$(B43) -include $(STUBS)/linux/types.h

replay: replay.c $(B43)/radar_ac.c $(B43)/radar_ac.h
	$(CC) $(CFLAGS) replay.c $(B43)/radar_ac.c -o $@

check: replay
	./check.sh

check-synth: replay
	./check.sh synth

clean:
	rm -rf replay build

.PHONY: check check-synth clean
