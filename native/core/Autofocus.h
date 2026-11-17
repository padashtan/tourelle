#pragma once
// Contrast autofocus search: a coarse sweep, then a fine one around the best
// coarse position. Pure state machine fed with sharpness measurements.

#include <optional>
#include <vector>

namespace tourelle::core {

class AutofocusSearch {
public:
    AutofocusSearch(int minimum, int maximum, int coarseStep, int fineStep);

    // Next focus position to measure, or nullopt when the search is over.
    std::optional<int> Next() const;
    // Records the sharpness measured at the position returned by Next().
    void Record(double sharpness);
    // Best position found so far (the final answer once Next() is empty).
    int Best() const { return best_; }

private:
    void PlanFine();

    int minimum_, maximum_, fineStep_;
    std::vector<int> plan_;
    size_t index_ = 0;
    bool fine_ = false;
    int best_;
    double bestSharpness_ = -1.0;
};

}  // namespace tourelle::core
