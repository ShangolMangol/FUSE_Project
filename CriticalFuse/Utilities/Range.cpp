#include "Range.h"
#include <stdexcept>

Range::Range(int start, int end) : startIdx(start), endIdx(end) {
    if (start > end) {
        throw std::invalid_argument("Start index cannot be greater than end index.");
    }
}

Range::Range(const Range& other) : startIdx(other.startIdx), endIdx(other.endIdx) {}

int Range::getStart() const {
    return startIdx;
}

int Range::getEnd() const {
    return endIdx;
}

void Range::setStart(int start) {
    if (start > endIdx) {
        throw std::invalid_argument("Start index cannot be greater than end index.");
    }
    startIdx = start;
}

void Range::setEnd(int end) {
    if (end < startIdx) {
        throw std::invalid_argument("End index cannot be less than start index.");
    }
    endIdx = end;
}

bool Range::contains(int index) const {
    return index >= startIdx && index <= endIdx;
}

bool Range::operator==(const Range& other) const {
    return startIdx == other.startIdx && endIdx == other.endIdx;
}

bool Range::operator!=(const Range& other) const {
    return !(*this == other);
}

bool Range::operator<(const Range& other) const {
    return endIdx < other.startIdx;
}

bool Range::operator>(const Range& other) const {
    return startIdx > other.endIdx;
}

