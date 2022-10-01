STUDENT_ID	:= 18-102-137
OBJFILES	:= src/main.o
BINARY		:= dram-functions
CWD			:= $(shell pwd)
CFLAGS 		:= -march=native -g
LDFLAGS 	:= -march=native -g
USER		:= noschmid
TIMELIMIT	:= 2:00
LDLIBS		:= -lm
NODE		:= ee-tik-cn001

all: $(BINARY)

*.o: *.c
	$(CC) $(CFLAGS) $@ $^

$(BINARY): $(OBJFILES)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

.PHONY: clean
clean:
	-rm -f $(OBJFILES) $(BINARY) $(STUDENT_ID).tar.gz

.PHONY: submission
submission: clean
	tar czvf /tmp/$(STUDENT_ID).tar.gz --exclude='.*' --exclude=$(STUDENT_ID).tar.gz -C "$(shell dirname $(CWD))" "$(shell basename $(CWD))"
	mv /tmp/$(STUDENT_ID).tar.gz .

run:
	cp dram-functions /data/${USER}/main
	# run program on cluster node
	srun -t $(TIMELIMIT) -w ${NODE} --pty /data/${USER}/main -b

run2:
	cp dram-functions /data/${USER}/main
	# run program on cluster node
	srun -t $(TIMELIMIT) -w ${NODE} --pty /data/${USER}/main -f

run3:
	cp dram-functions /data/${USER}/main
	# run program on cluster node
	srun -t $(TIMELIMIT) -w ${NODE} --pty /data/${USER}/main -m