CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2

endif

server: main.cpp  ./timer/lst_timer.cpp ./http/Http_Conn.cpp ./log/log.cpp  ./pagebuilder/pagebuilder.cpp  WebServer.cpp config.cpp
	$(CXX) -o server  $^ $(CXXFLAGS) -lpthread -lmysqlclient

clean:
	rm  -r server
