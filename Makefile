# Compiler settings
CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Iinclude -g
LDFLAGS := -pthread
LDLIBS := -lstdc++

# Targets
SERVER_TARGET := bin/tcp_server
CLIENT_TARGET := bin/tcp_client

# Source files (list them explicitly)
SERVER_SRC := src/socketServer.cpp
CLIENT_SRC := src/socketClient.cpp

# Objects
SERVER_OBJS := $(SERVER_SRC:.cpp=.o)
CLIENT_OBJS := $(CLIENT_SRC:.cpp=.o)

# Color codes
GREEN := \033[0;32m
NC := \033[0m

.PHONY: all clean run-server run-client

all: $(SERVER_TARGET) $(CLIENT_TARGET)

$(SERVER_TARGET): $(SERVER_OBJS)
	@mkdir -p $(@D)
	@echo "$(GREEN)Linking $@...$(NC)"
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(CLIENT_TARGET): $(CLIENT_OBJS)
	@mkdir -p $(@D)
	@echo "$(GREEN)Linking $@...$(NC)"
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

%.o: %.cpp
	@echo "$(GREEN)Compiling $<...$(NC)"
	$(CXX) $(CXXFLAGS) -c $< -o $@

run-server: $(SERVER_TARGET)
	@./$(SERVER_TARGET)

run-client: $(CLIENT_TARGET)
	@./$(CLIENT_TARGET)

clean:
	@echo "$(GREEN)Cleaning...$(NC)"
	rm -f src/*.o $(SERVER_TARGET) $(CLIENT_TARGET)