// Shared accounting contract: docs/lumen-frame-allocator-design.md, sections 2 and 4.
#include <catch2/catch_test_macros.hpp>

#include "../src/platform/native_allocation_ledger.h"

using lumen::platform::detail::NativeAllocationLedger;

TEST_CASE("native ledger locates CRT interior pointers without wrapping ranges", "[native_ledger]") {
    NativeAllocationLedger<8> ledger;
    const auto token = ledger.begin();
    ledger.record(64, 32, 1000);
    const auto id = ledger.allocationId(64, token);
    CHECK(ledger.containingAllocationId(64, token) == id);
    CHECK(ledger.containingAllocationId(95, token) == id);
    CHECK(ledger.containingAllocationId(63, token) == 0);
    CHECK(ledger.containingAllocationId(96, token) == 0);
    ledger.record(128, 0);
    CHECK(ledger.containingAllocationId(128, token) != 0);
    CHECK(ledger.containingAllocationId(129, token) == 0);
    const auto last = std::numeric_limits<std::uintptr_t>::max();
    ledger.record(last - 8, 9);
    CHECK(ledger.containingAllocationId(last, token) != 0);
    CHECK(ledger.containingAllocationId(0, token) == 0);
    ledger.forgetOwner(1000);
    CHECK(ledger.containingAllocationId(80, token) == 0);
    ledger.record(64, 32);
    CHECK(ledger.containingAllocationId(80, token) != id);
    ledger.cancel(token);
    CHECK(ledger.containingAllocationId(80, token) == 0);
    const auto next = ledger.begin();
    ledger.record(64, 32);
    CHECK(ledger.containingAllocationId(80, token) == 0);
    ledger.invalidate();
    CHECK(ledger.containingAllocationId(80, next) == 0);
}

TEST_CASE("native ledger rejects duplicate addresses beyond tombstones", "[native_ledger]") {
    NativeAllocationLedger<8> ledger;
    const auto token = ledger.begin();
    // The hash collides for addresses differing by Capacity * 16.
    ledger.record(16, 10);
    ledger.record(144, 20);
    ledger.forget(16);
    ledger.record(144, 30);
    CHECK_FALSE(ledger.finish(token).complete);
    const auto retry = ledger.begin();
    CHECK(ledger.finish(retry).complete);
}

TEST_CASE("native ledger reuses full-table tombstones and preserves generations", "[native_ledger]") {
    NativeAllocationLedger<8> ledger;
    const auto token = ledger.begin();
    for (std::uintptr_t i = 1; i <= 8; ++i) ledger.record(i * 16, i);
    const auto oldId = ledger.allocationId(64, token);
    ledger.forget(64);
    ledger.record(64, 0);
    CHECK(ledger.allocationId(64, token) != oldId);
    const auto sample = ledger.finish(token);
    CHECK(sample.complete);
    CHECK(sample.allocationCount == 9);
    CHECK(sample.allocatedBytes == 36);
    CHECK(sample.peakBytes == 36);
    CHECK(sample.liveBytes == 32);
}

TEST_CASE("native ledger capacity and arithmetic overflow never publish complete samples", "[native_ledger]") {
    NativeAllocationLedger<8> ledger;
    auto token = ledger.begin();
    for (std::uintptr_t i = 1; i <= 9; ++i) ledger.record(i * 16, 1);
    CHECK_FALSE(ledger.finish(token).complete);
    token = ledger.begin();
    ledger.record(16, std::numeric_limits<std::uint64_t>::max());
    ledger.forget(16);
    ledger.record(32, 1);
    CHECK_FALSE(ledger.finish(token).complete);
    token = ledger.begin();
    ledger.record(48, 7);
    CHECK(ledger.finish(token).liveBytes == 7);
}

TEST_CASE("native ledger retires only the destroyed zone's scoped objects", "[native_ledger]") {
    NativeAllocationLedger<8> ledger;
    const auto token = ledger.begin();
    ledger.record(16, 10, 1000);
    ledger.record(32, 20, 1000);
    ledger.record(48, 40, 2000);
    ledger.record(64, 80);
    ledger.forgetOwner(0);
    ledger.forgetOwner(1000);
    CHECK(ledger.allocationId(16, token) == 0);
    CHECK(ledger.allocationId(32, token) == 0);
    CHECK(ledger.allocationId(48, token) != 0);
    ledger.forget(48);
    const auto sample = ledger.finish(token);
    CHECK(sample.complete);
    CHECK(sample.allocationCount == 4);
    CHECK(sample.allocatedBytes == 150);
    CHECK(sample.peakBytes == 150);
    CHECK(sample.liveBytes == 80);
}

TEST_CASE("native ledger ignores stale scope tokens and fork epochs", "[native_ledger]") {
    NativeAllocationLedger<8> ledger;
    ledger.record(16, 9);
    const auto first = ledger.begin();
    CHECK(ledger.begin() == 0);
    ledger.record(32, 11);
    ledger.cancel(first);
    const auto second = ledger.begin();
    ledger.cancel(first);
    CHECK_FALSE(ledger.finish(first).complete);
    CHECK(ledger.begin() == 0);
    ledger.forget(32);
    ledger.record(48, 7);
    ledger.invalidate();
    CHECK_FALSE(ledger.finish(second).complete);
    const auto parent = ledger.begin();
    ledger.abandon();
    const auto child = ledger.begin();
    ledger.cancel(parent);
    const auto empty = ledger.finish(child);
    CHECK(empty.complete);
    CHECK(empty.allocationCount == 0);
}
