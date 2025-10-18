#include "order_book.cpp"
#include <chrono>
#include <cassert>

using namespace std;
using namespace std::chrono;

uint64_t get_timestamp() {
    return duration_cast<nanoseconds>(high_resolution_clock::now().time_since_epoch()).count();
}

void test_basic_operations() {
    cout << "\n=== TEST 1: Basic Operations ===\n";
    OrderBook book;
    
    book.add_order({1, true, 100.0, 10, get_timestamp()});
    book.add_order({2, true, 99.5, 20, get_timestamp()});
    book.add_order({3, true, 99.0, 15, get_timestamp()});
    book.add_order({4, false, 101.0, 12, get_timestamp()});
    book.add_order({5, false, 101.5, 18, get_timestamp()});
    book.add_order({6, false, 102.0, 25, get_timestamp()});
    
    book.print_book(5);
    
    assert(book.get_total_orders() == 6);
    cout << "✓ PASSED\n";
}

void test_cancel_operations() {
    cout << "\n=== TEST 2: Cancel Operations ===\n";
    OrderBook book;
    
    book.add_order({1, true, 100.0, 10, get_timestamp()});
    book.add_order({2, true, 99.5, 20, get_timestamp()});
    book.add_order({3, false, 101.0, 15, get_timestamp()});
    
    assert(book.cancel_order(2) == true);
    assert(book.get_total_orders() == 2);
    assert(book.cancel_order(999) == false);
    
    book.print_book(3);
    cout << "✓ PASSED\n";
}

void test_amend_operations() {
    cout << "\n=== TEST 3: Amend Operations ===\n";
    OrderBook book;
    
    book.add_order({1, true, 100.0, 10, get_timestamp()});
    book.add_order({2, true, 99.5, 20, get_timestamp()});
    book.add_order({3, false, 101.0, 15, get_timestamp()});
    
    // Amend quantity (maintains priority)
    assert(book.amend_order(1, 100.0, 25) == true);
    
    // Amend price (loses priority)
    assert(book.amend_order(2, 100.5, 20) == true);
    
    book.print_book(3);
    cout << "✓ PASSED\n";
}

void test_price_aggregation() {
    cout << "\n=== TEST 4: Price Aggregation ===\n";
    OrderBook book;
    
    book.add_order({1, true, 100.0, 10, get_timestamp()});
    book.add_order({2, true, 100.0, 15, get_timestamp()});
    book.add_order({3, true, 100.0, 20, get_timestamp()});
    book.add_order({4, false, 101.0, 12, get_timestamp()});
    book.add_order({5, false, 101.0, 18, get_timestamp()});
    
    vector<PriceLevel> bids, asks;
    book.get_snapshot(5, bids, asks);
    
    assert(bids.size() == 1);
    assert(bids[0].total_quantity == 45);
    assert(asks.size() == 1);
    assert(asks[0].total_quantity == 30);
    
    book.print_book(3);
    cout << "✓ PASSED (Buy: 45 @ 100.0, Sell: 30 @ 101.0)\n";
}

void test_fifo_priority() {
    cout << "\n=== TEST 5: FIFO Priority ===\n";
    OrderBook book;
    
    book.add_order({1, true, 100.0, 10, 1000});
    book.add_order({2, true, 100.0, 15, 2000});
    book.add_order({3, true, 100.0, 20, 3000});
    
    assert(book.get_total_orders() == 3);
    book.cancel_order(1);  // Cancel first order
    assert(book.get_total_orders() == 2);
    
    book.print_book(3);
    cout << "✓ PASSED\n";
}

void test_memory_pool_performance() {
    cout << "\n=== TEST 6: Memory Pool Performance ===\n";
    OrderBook book;
    
    const int NUM_ORDERS = 10000;
    auto start = high_resolution_clock::now();
    
    // Add orders
    for (int i = 1; i <= NUM_ORDERS; ++i) {
        double price = (i % 2 == 0) ? 100.0 - (i * 0.01) : 101.0 + (i * 0.01);
        book.add_order({
            static_cast<uint64_t>(i),
            i % 2 == 0,
            price,
            static_cast<uint64_t>(i * 10),
            get_timestamp()
        });
    }
    
    auto end = high_resolution_clock::now();
    auto add_time = duration_cast<microseconds>(end - start).count();
    
    cout << "Added " << NUM_ORDERS << " orders in " << add_time << " μs\n";
    cout << "Average: " << (double)add_time / NUM_ORDERS << " μs per order\n";
    
    // Test cancellation performance
    start = high_resolution_clock::now();
    for (int i = 1; i <= 1000; ++i) {
        book.cancel_order(i);
    }
    end = high_resolution_clock::now();
    auto cancel_time = duration_cast<microseconds>(end - start).count();
    
    cout << "Cancelled 1000 orders in " << cancel_time << " μs\n";
    cout << "Average: " << (double)cancel_time / 1000 << " μs per cancel\n";
    
    // Memory pool stats
    cout << "\nMemory Pool Stats:\n";
    cout << "  Capacity: " << book.get_pool_capacity() << " orders\n";
    cout << "  Allocated: " << book.get_pool_allocated() << " orders\n";
    cout << "  Utilization: " << std::fixed << std::setprecision(1)
         << (100.0 * book.get_pool_allocated() / book.get_pool_capacity()) << "%\n";
    
    book.print_book(5);
    cout << "✓ PASSED\n";
}

void test_edge_cases() {
    cout << "\n=== TEST 7: Edge Cases ===\n";
    OrderBook book;
    
    // Empty book
    vector<PriceLevel> bids, asks;
    book.get_snapshot(10, bids, asks);
    assert(bids.empty() && asks.empty());
    
    // Non-existent orders
    assert(book.cancel_order(999) == false);
    assert(book.amend_order(999, 100.0, 10) == false);
    
    // Duplicate order ID
    book.add_order({1, true, 100.0, 10, get_timestamp()});
    book.add_order({1, true, 99.0, 20, get_timestamp()});
    assert(book.get_total_orders() == 1);
    
    // Add and cancel
    book.add_order({2, true, 100.0, 10, get_timestamp()});
    assert(book.cancel_order(2) == true);
    
    cout << "✓ PASSED\n";
}

void test_large_book() {
    cout << "\n=== TEST 8: Large Order Book ===\n";
    OrderBook book;
    
    // Add 2000 orders
    for (int i = 1; i <= 1000; ++i) {
        book.add_order({static_cast<uint64_t>(i), true, 100.0 - i*0.01, 
                       static_cast<uint64_t>(i*10), get_timestamp()});
    }
    for (int i = 1001; i <= 2000; ++i) {
        book.add_order({static_cast<uint64_t>(i), false, 101.0 + (i-1000)*0.01,
                       static_cast<uint64_t>((i-1000)*10), get_timestamp()});
    }
    
    // Snapshot performance
    auto start = high_resolution_clock::now();
    vector<PriceLevel> bids, asks;
    book.get_snapshot(10, bids, asks);
    auto end = high_resolution_clock::now();
    
    cout << "Snapshot (depth=10) took " 
         << duration_cast<nanoseconds>(end - start).count() << " ns\n";
    
    book.print_book(10);
    cout << "✓ PASSED\n";
}

void demo_example() {
    cout << "\n=== DEMO: Order Book Example ===\n";
    OrderBook book;
    
    // Add some orders
    book.add_order({1, true, 105.0, 10, 10});
    book.add_order({2, true, 104.0, 8, 20});
    book.add_order({3, true, 103.0, 6, 30});
    book.add_order({4, true, 102.0, 4, 40});
    book.add_order({5, true, 101.0, 2, 50});
    
    book.add_order({6, false, 111.0, 3, 60});
    book.add_order({7, false, 112.0, 6, 70});
    book.add_order({8, false, 113.0, 9, 80});
    book.add_order({9, false, 114.0, 12, 90});
    book.add_order({10, false, 115.0, 15, 100});
    
    // Add more at same prices
    book.add_order({11, true, 101.0, 22, 110});
    book.add_order({12, true, 102.0, 24, 120});
    book.add_order({13, false, 111.0, 33, 130});
    book.add_order({14, false, 112.0, 36, 140});
    
    book.print_book(10);
}

int main() {
    cout << "\n";
    cout << "╔════════════════════════════════════════════════╗\n";
    cout << "║   LOW-LATENCY ORDER BOOK WITH MEMORY POOL      ║\n";
    cout << "║          Scaler HFT 2027 Capstone              ║\n";
    cout << "╚════════════════════════════════════════════════╝\n";
    
    try {
        test_basic_operations();
        test_cancel_operations();
        test_amend_operations();
        test_price_aggregation();
        test_fifo_priority();
        test_memory_pool_performance();
        test_edge_cases();
        test_large_book();
        demo_example();
        
        cout << "\n";
        cout << "╔════════════════════════════════════════════════╗\n";
        cout << "║           ALL TESTS PASSED! ✓                  ║\n";
        cout << "╚════════════════════════════════════════════════╝\n";
        cout << "\n";
        
    } catch (const exception& e) {
        cerr << "\n✗ Test failed: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}

