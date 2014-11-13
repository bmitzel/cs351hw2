all:	sender recv sender_ec recv_ec

sender:	sender.o
	g++ sender.o -o sender

recv:	recv.o
	g++ recv.o -o recv

sender.o: sender.cpp
	g++ -c sender.cpp

recv.o:	recv.cpp
	g++ -c recv.cpp
	
sender_ec: sender_ec.o
	g++ sender_ec.o -o sender_ec
	
recv_ec: recv_ec.o
	g++ recv_ec.o -o recv_ec
	
sender_ec.o: sender_ec.cpp
	g++ -c sender_ec.cpp

recv_ec.o:	recv_ec.cpp
	g++ -c recv_ec.cpp

clean:
	rm -rf *.o sender recv sender_ec recv_ec
