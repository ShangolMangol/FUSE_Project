#ifndef RANGE_H
#define RANGE_H


class Range {
    // Represents a close range [startIdx, endIdx]  
private:
    int startIdx;
    int endIdx;

public:
    Range(int start, int end);
    Range(const Range& other); // Copy constructor
    ~Range() = default; // Destructor

    int getStart() const;
    int getEnd() const;

    void setStart(int start);
    void setEnd(int end);

    bool contains(int index) const;

    bool operator==(const Range& other) const;
    bool operator!=(const Range& other) const;
    bool operator<(const Range& other) const;
    bool operator>(const Range& other) const;

};

#endif // RANGE_H