#include "Autofocus.h"

#include <algorithm>

namespace tourelle::core {

AutofocusSearch::AutofocusSearch(int minimum, int maximum, int coarseStep, int fineStep)
    : minimum_(minimum), maximum_(std::max(minimum, maximum)), fineStep_(std::max(1, fineStep)), best_(minimum) {
    const int step = std::max(1, coarseStep);
    for (int position = minimum_; position <= maximum_; position += step) plan_.push_back(position);
    if (plan_.back() != maximum_) plan_.push_back(maximum_);
}

std::optional<int> AutofocusSearch::Next() const {
    if (index_ < plan_.size()) return plan_[index_];
    return std::nullopt;
}

void AutofocusSearch::Record(double sharpness) {
    if (index_ >= plan_.size()) return;
    if (sharpness > bestSharpness_) {
        bestSharpness_ = sharpness;
        best_ = plan_[index_];
    }
    ++index_;
    if (index_ == plan_.size() && !fine_) PlanFine();
}

void AutofocusSearch::PlanFine() {
    // Refine between the coarse neighbours of the best coarse position.
    const auto it = std::find(plan_.begin(), plan_.end(), best_);
    const int low = it == plan_.begin() ? best_ : *(it - 1);
    const int high = it + 1 == plan_.end() ? best_ : *(it + 1);
    fine_ = true;
    plan_.clear();
    index_ = 0;
    for (int position = low; position <= high; position += fineStep_) {
        if (position != best_) plan_.push_back(position);
    }
}

}  // namespace tourelle::core
