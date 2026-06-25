#include <vector>
#include <iostream>

struct MeanReturn
{
    float mean;
    bool isValid;
};

MeanReturn meanFinder(const std::vector<float>& numbers)
{
    MeanReturn result;

    if (numbers.empty())
    {
        result.isValid = false;
        result.mean = 0.0f;
        return result;
    }

    float runningTotal = 0.0f;
    for (float number : numbers)
        runningTotal += number;

    result.mean = runningTotal / numbers.size();
    result.isValid = true;
    return result;
}

int main()
{
    MeanReturn r = meanFinder({2.0f, 4.0f, 6.0f});
    if (r.isValid) std::cout << "Mean: " << r.mean << "\n";   // Mean: 4

    MeanReturn empty = meanFinder({});
    if (!empty.isValid) std::cout << "Empty collection - no mean\n";
}
