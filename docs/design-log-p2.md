# Design Log — Project 2

## Growth factor and amortized cost

`Conversation` starts empty with no allocation (`capacity_ = 0`). When `append`
finds the array full (`size_ == capacity_`), it allocates a new array (capacity 1
if the old capacity was 0, otherwise double the old capacity), moves the existing
messages into it, and frees the old array. I chose a growth factor of 2 because it
is simple and it never leaves more than half the array unused.

Growing by a fixed amount is much worse. With +1 growth, every append reallocates
and moves everything stored so far, which is `1 + 2 + ... + n = n(n+1)/2` moves in
total, or O(n) per append. For 1000 appends that is 499,500 moves.

**Proof that doubling is amortized O(1).** A reallocation happens when the size
before an append is 0, 1, 2, 4, ..., `2^m`. Suppose we do n appends. The last
reallocation happens at a size `2^m <= n - 1`, so the total number of messages moved is

```text
1 + 2 + 4 + ... + 2^m = 2^(m+1) - 1 < 2n
```

Adding the n stores that place each new message, the total work is under 3n, so the
average cost per append is under 3, which is O(1).

**Evidence:** my growth test appends 1000 messages and checks that a reallocation
happens exactly at sizes 0, 1, 2, 4, ..., 512. That is 11 reallocations and
`1 + 2 + ... + 512 = 1023` moves, about one move per append.

## Rule of Five evidence

`Conversation` owns one raw array (`data_`), so it follows the Rule of Five.
`Message` holds only a `std::string` and an enum, so it needs none of the five.

- **Destructor:** `delete[] data_` frees the array and destroys every `Message`.
  It is safe on `nullptr`, so empty and moved-from objects are fine.
- **Copy constructor and copy assignment:** both make a deep copy. A new array is
  allocated and every message is copied into it. Copy assignment checks
  `this != &other` and frees the old array first, so `c = c` is safe and nothing leaks.
- **Move constructor and move assignment:** both are `noexcept`. They take the
  pointer, size, and capacity from the source, then set the source to `nullptr`, 0, 0.
  No messages are copied, and the moved-from object is still valid.

**Evidence:** the tests check that a copy's `begin()` differs from the original's (a
shallow copy would fail here), that changing a copy leaves the original alone, that a
move keeps the same buffer address and leaves the source with `size() == 0` and
`begin() == nullptr`, and that self-assignment and self-move change nothing. All 13
tests run under AddressSanitizer and UBSan with no errors or leaks.

## Sentinel scanner: bounded `pending_` proof

Let L be the sentinel length (20). Claim: after every call to `feed` or `flush`,
`pending_.size() <= L - 1`.

Proof, assuming it holds before the call (it does at the start, since `pending_`
is empty). `feed` searches `pending_ + chunk`:

1. Sentinel found: `pending_` is cleared (size 0).
2. Not found, and the text is longer than L - 1: `pending_` becomes its last
   L - 1 characters (size exactly L - 1).
3. Not found, and the text is at most L - 1 long: `pending_` becomes all of it
   (size at most L - 1).

`flush` clears `pending_`. So by induction the bound holds after every call.

Holding back L - 1 characters is enough, since an unfinished match is at most
L - 1 characters long (L matching characters would be the sentinel). Each call
searches at most `(L - 1) + (chunk size)` characters no matter how long the stream
is, whereas re-searching everything would cost O(N^2) overall.

**Evidence:** my stress test feeds 4 MB one byte at a time and checks that
`(characters fed - characters emitted) <= 19` after every byte. The maximum is
exactly 19, so the buffer really was full.

## What I would change differently

I would change how the copy constructor sizes its array. It allocates
exactly `other.size_` slots, so a copy is full as soon as it is made, and the first
`append` after a copy has to reallocate and move every message. The original, with
spare room, would not. This does not break the amortized O(1) bound, and the provided
`Harness` never copies a `Conversation`, so it did not matter here. Next time I would
copy `other.capacity_` so a copy keeps the same spare room, at the cost of a little
more memory.