
.PHONY: all
all: model3_rom.c obstacle_run_cmd.c

model3_rom.c: model3.rom make_binary.py
	python3 make_binary.py model3.rom

obstacle_run_cmd.c: Obstacle-Run.CMD make_binary.py
	python3 make_binary.py Obstacle-Run.CMD
