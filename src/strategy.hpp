#pragma once

#include "protocol.hpp"

namespace astra {

class BaselineStrategy {
public:
    Decision decide(const TurnObservation& turn) const;
};

}  // namespace astra
