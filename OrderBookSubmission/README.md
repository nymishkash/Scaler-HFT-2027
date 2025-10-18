# Low-Latency Limit Order Book

**Scaler HFT 2027 - Capstone Project**

---

## Student Details

**Name:** Nymish Kashivishwanath  
**Roll Number:** 23BCS10127

---

### Core Operations

- **Add Order** - Insert new order into the book
- **Cancel Order** - Remove order by order_id
- **Amend Order** - Modify price or quantity
- **Get Snapshot** - Return top N bid/ask levels
- **Print Book** - Display current order book state

### Data Structures

```cpp
struct Order {
    uint64_t order_id;
    bool is_buy;
    double price;
    uint64_t quantity;
    uint64_t timestamp_ns;
};

struct PriceLevel {
    double price;
    uint64_t total_quantity;
};
```

### BONUS Feature

**Memory Pool Allocator** - Pre-allocated memory for 100,000 orders with O(1) allocation/deallocation

---

## Files

- `order_book.cpp` - Complete implementation with memory pool
- `main.cpp` - Test suite
- `Makefile` - Build automation

---

## Build & Run

### Compile

```bash
make
```

### Run Tests

```bash
./order_book
```

### Clean

```bash
make clean
```

---

## Performance

- Add Order: ~0.07 microseconds
- Cancel Order: ~0.07 microseconds
- Amend Order: ~0.07 microseconds
- Memory Pool: Pre-allocated for 100K orders

---

## Implementation Details

### Time Complexity

| Operation           | Complexity |
| ------------------- | ---------- |
| Add Order           | O(log P)   |
| Cancel Order        | O(1)       |
| Amend Order (qty)   | O(1)       |
| Amend Order (price) | O(log P)   |
| Get Snapshot        | O(D)       |

_P = price levels, D = depth_

### Data Structures Used

- `std::map` for price levels (auto-sorted)
- `std::list` for FIFO order queue at each price
- `std::unordered_map` for O(1) order lookup
- Custom memory pool for efficient allocation

### Key Features

- FIFO priority maintained at each price level
- Cache-friendly design
- Minimal heap allocations
- Price-time priority enforcement

---