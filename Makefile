default: bouncer

bouncer: bouncer.cc
	g++ -o bouncer bouncer.cc -pthread -g

