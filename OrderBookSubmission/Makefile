CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra -march=native
TARGET = order_book

all: $(TARGET)

$(TARGET): main.cpp order_book.cpp
	$(CXX) $(CXXFLAGS) main.cpp -o $(TARGET)
	@echo "✓ Build complete"

test: $(TARGET)
	./$(TARGET)

perf: $(TARGET)
	@echo "Running with performance profiling..."
	@time ./$(TARGET)

clean:
	rm -f $(TARGET) *.o
	@echo "✓ Clean complete"

help:
	@echo "Available commands:"
	@echo "  make       - Build the order book"
	@echo "  make test  - Build and run tests"
	@echo "  make perf  - Run with timing"
	@echo "  make clean - Remove build files"

.PHONY: all test perf clean help
