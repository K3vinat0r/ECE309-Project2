#include "core/conversation.h"
#include <stdexcept>
#include <utility>

// Default constructor
Conversation::Conversation() = default;

// Destructor
Conversation::~Conversation(){
    delete[] data_;
}

// Copy constructor
Conversation::Conversation(const Conversation& other)
    : size_(other.size_), capacity_(other.size_) {
    data_ = new Message[capacity_];
    for (std::size_t i = 0; i < size_; i++) {
        data_[i] = other.data_[i];
    }
}

// Copy assignment
Conversation& Conversation::operator=(const Conversation& other) {
    if (this != &other) {
        delete[] data_;
        size_ = other.size_;
        capacity_ = other.size_;
        data_ = new Message[capacity_];
        for (std::size_t i = 0; i < size_; i++) {
            data_[i] = other.data_[i];
        }
    }
    return *this;
}

// Move constructor
Conversation::Conversation(Conversation&& other) noexcept
    : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

// Move assignment
Conversation& Conversation::operator=(Conversation&& other) noexcept {
    if (this != &other) {
        delete[] data_;
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    return *this;
}

// Append
void Conversation::append(Message m) {
    if (size_ == capacity_) {
        std::size_t new_capacity;
        if (capacity_ == 0) {
            new_capacity = 1;
        } else {
            new_capacity = capacity_ * 2;
        }

        Message* new_data = new Message[new_capacity];
        for (std::size_t i = 0; i < size_; i++) {
            new_data[i] = std::move(data_[i]);
        }
        delete[] data_;
        data_ = new_data;
        capacity_ = new_capacity;
    }
    data_[size_] = std::move(m);
    size_++;
}

// Number of messaged currently stored
std::size_t Conversation::size() const noexcept {
    return size_;
}

// Bounds checked access
const Message& Conversation::at(std::size_t i) const {
    if (i >= size_) {
        throw std::out_of_range("Conversation::at index is out of range");
    }
    return data_[i];
}

// Range-for iteration
const Message* Conversation::begin() const noexcept {
    return data_;
}

const Message* Conversation::end() const noexcept {
    return data_ + size_;
}