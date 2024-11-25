CC = g++
COPTS = -g -Wall -std=c++11
LKOPTS = 

OBJS =\
	Event.o\
	Link.o\
	Node.o\
	RoutingProtocolImpl.o\
	Simulator.o

%.o: %.cc
	$(CC) $(COPTS) -c $< -o $@

all: Simulator

Simulator: $(OBJS)
	$(CC) $(LKOPTS) -o Simulator $(OBJS)

$(OBJS): global.h
Event.o: Event.h Link.h Node.h Simulator.h
Link.o: Event.h Link.h Node.h Simulator.h
Node.o: Event.h Link.h Node.h Simulator.h
Simulator.o: Event.h Link.h Node.h RoutingProtocol.h Simulator.h 
RoutingProtocolImpl.o: RoutingProtocolImpl.h

run: Simulator
	@echo "Running Simulator with test5 and LS protocol..."
	./Simulator test5 LS

valgrind: Simulator
	@echo "Running Simulator with Valgrind (test5, LS protocol)..."
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --log-file=valgrind_output.txt ./Simulator test5 LS

clean:
	rm -f *.o Simulator valgrind_output.txt
