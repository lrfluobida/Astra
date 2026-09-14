#include "strategy.hpp"

#include "actions.hpp"
#include "navigation.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <tuple>

namespace astra {
namespace {

int distance(const Pos& left, const Pos& right) {
    return std::max(std::abs(left.x - right.x), std::abs(left.y - right.y));
}

bool is_mineral(const std::string& name) {
    return name == "stone" || name == "iron" || name == "copper";
}

int mineral_rank(const std::string& name) {
    if (name == "copper") return 3;
    if (name == "iron") return 2;
    if (name == "stone") return 1;
    return 0;
}

std::map<std::string, int> positive_prices(const TurnObservation& turn) {
    std::map<std::string, int> result;
    for (const auto& item : turn.vendor_shop) {
        if (is_mineral(item.name) && item.price > 0) result[item.name] = item.price;
    }
    return result;
}

std::map<std::string, int> inventory_counts(const UnitObservation& worker) {
    std::map<std::string, int> result;
    for (const auto& item : worker.backpack) {
        if (is_mineral(item)) ++result[item];
    }
    return result;
}

std::optional<ZoneObservation> first_vendor(const TurnObservation& turn) {
    for (const auto& zone : turn.map.zones) {
        if (zone.neutral_type == "vendor") return zone;
    }
    return std::nullopt;
}

UnitObservation* find_worker(TurnObservation& turn, int id) {
    for (auto& unit : turn.team_our.roles) {
        if (unit.id == id) return &unit;
    }
    return nullptr;
}

CandidateAction move_candidate(const UnitObservation& worker, Pos target, int priority) {
    CandidateAction candidate;
    candidate.action_key = worker.id;
    candidate.command.action = "move";
    candidate.command.target_positions = {target};
    candidate.priority = priority;
    candidate.source = "economy.move";
    return candidate;
}

std::optional<CandidateAction> adjacent_sell(const TurnObservation& turn,
                                             const UnitObservation& worker,
                                             const std::map<std::string, int>& prices,
                                             int priority) {
    const auto vendor = first_vendor(turn);
    if (!vendor || distance(worker.pos, vendor->pos) > 1) return std::nullopt;
    const auto counts = inventory_counts(worker);
    std::optional<std::tuple<long long, int, std::string>> best;
    for (const auto& [name, count] : counts) {
        const auto price = prices.find(name);
        if (price == prices.end() || count <= 0) continue;
        const auto candidate = std::make_tuple(static_cast<long long>(price->second) * count,
                                               mineral_rank(name),
                                               name);
        if (!best || candidate > *best) best = candidate;
    }
    if (!best) return std::nullopt;

    const std::string name = std::get<2>(*best);
    const int count = counts.at(name);
    CandidateAction candidate;
    candidate.action_key = worker.id;
    candidate.command.action = "sell";
    candidate.command.name = name;
    candidate.command.number = count;
    candidate.reservation.items[worker.id][name] = count;
    candidate.priority = priority;
    candidate.source = "economy.sell";
    return candidate;
}

std::optional<CandidateAction> adjacent_collect(const TurnObservation& turn,
                                                const UnitObservation& worker,
                                                const std::map<std::string, int>& prices,
                                                int priority) {
    if (!worker.backpack_capacity || *worker.backpack_capacity <= 0 ||
        worker.backpack.size() >= static_cast<std::size_t>(*worker.backpack_capacity)) {
        return std::nullopt;
    }
    const ZoneObservation* best = nullptr;
    int best_price = 0;
    for (const auto& zone : turn.map.zones) {
        const auto price = prices.find(zone.neutral_type);
        if (price == prices.end() || distance(worker.pos, zone.pos) > 1) continue;
        if (!best || price->second > best_price ||
            (price->second == best_price && mineral_rank(zone.neutral_type) >
                                                 mineral_rank(best->neutral_type))) {
            best = &zone;
            best_price = price->second;
        }
    }
    if (!best) return std::nullopt;

    CandidateAction candidate;
    candidate.action_key = worker.id;
    candidate.command.action = "collect";
    candidate.command.target_positions = {best->pos};
    candidate.priority = priority;
    candidate.source = "economy.collect";
    return candidate;
}

struct MineChoice {
    const ZoneObservation* mine = nullptr;
    PathStep path;
    int quantity = 0;
    int price = 0;
    int total_rounds = 0;
};

bool better_mine(const MineChoice& left, const MineChoice& right) {
    const long long left_rate = static_cast<long long>(left.price) * left.quantity *
                                right.total_rounds;
    const long long right_rate = static_cast<long long>(right.price) * right.quantity *
                                 left.total_rounds;
    if (left_rate != right_rate) return left_rate > right_rate;
    const int left_total = left.price * left.quantity;
    const int right_total = right.price * right.quantity;
    if (left_total != right_total) return left_total > right_total;
    if (left.total_rounds != right.total_rounds) return left.total_rounds < right.total_rounds;
    const int left_rank = mineral_rank(left.mine->neutral_type);
    const int right_rank = mineral_rank(right.mine->neutral_type);
    if (left_rank != right_rank) return left_rank > right_rank;
    return std::tie(left.mine->pos.x, left.mine->pos.y) <
           std::tie(right.mine->pos.x, right.mine->pos.y);
}

std::optional<PathStep> path_to_vendor(const TurnObservation& turn,
                                       const UnitObservation& worker,
                                       const ZoneObservation& vendor,
                                       const NavigationReservations& reservations) {
    return next_step_toward_any(turn,
                                worker.id,
                                interaction_cells(turn, vendor.pos),
                                reservations);
}

std::optional<CandidateAction> economic_move(const TurnObservation& turn,
                                             const UnitObservation& worker,
                                             const std::map<std::string, int>& prices,
                                             const NavigationReservations& reservations,
                                             int priority) {
    const auto vendor = first_vendor(turn);
    if (!vendor || !worker.backpack_capacity || *worker.backpack_capacity <= 0 ||
        worker.backpack.size() > static_cast<std::size_t>(*worker.backpack_capacity)) {
        return std::nullopt;
    }
    const int free_capacity =
        *worker.backpack_capacity - static_cast<int>(worker.backpack.size());
    const auto carried = inventory_counts(worker);
    int carried_count = 0;
    for (const auto& [name, count] : carried) {
        (void)name;
        carried_count += count;
    }
    if (free_capacity < 10 && carried_count > 0) {
        const auto vendor_path = path_to_vendor(turn, worker, *vendor, reservations);
        if (vendor_path && vendor_path->distance > 0) {
            return move_candidate(worker, vendor_path->next, priority);
        }
        return std::nullopt;
    }
    const int quantity = std::min(10, free_capacity);
    if (quantity <= 0) return std::nullopt;

    std::optional<MineChoice> best;
    for (const auto& zone : turn.map.zones) {
        const auto price = prices.find(zone.neutral_type);
        if (price == prices.end()) continue;
        const auto mine_path = next_step_toward_any(
            turn, worker.id, interaction_cells(turn, zone.pos), reservations);
        if (!mine_path) continue;

        TurnObservation from_mine = turn;
        auto* copied_worker = find_worker(from_mine, worker.id);
        if (!copied_worker) continue;
        copied_worker->pos = mine_path->goal;
        const auto vendor_path = next_step_toward_any(
            from_mine, worker.id, interaction_cells(from_mine, vendor->pos), {});
        if (!vendor_path) continue;

        MineChoice choice{&zone,
                          *mine_path,
                          quantity,
                          price->second,
                          mine_path->distance + quantity + vendor_path->distance + 1};
        if (choice.total_rounds <= 0) continue;
        if (!best || better_mine(choice, *best)) best = choice;
    }
    if (!best || best->path.distance == 0) return std::nullopt;

    const int round_in_day = (turn.round_no - 1) % 130 + 1;
    const int daylight_remaining = 70 - round_in_day + 1;
    if (daylight_remaining <= best->total_rounds + 2) {
        if (carried_count > 0) {
            const auto vendor_path = path_to_vendor(turn, worker, *vendor, reservations);
            if (vendor_path && vendor_path->distance > 0) {
                return move_candidate(worker, vendor_path->next, priority);
            }
        }
        return std::nullopt;
    }
    return move_candidate(worker, best->path.next, priority);
}

}  // namespace

Decision BaselineStrategy::decide(const TurnObservation& turn) const {
    const int round_in_day = (turn.round_no - 1) % 130 + 1;
    if (round_in_day > 70) return {};

    const auto prices = positive_prices(turn);
    std::vector<const UnitObservation*> workers;
    for (const auto& unit : turn.team_our.roles) {
        if (unit.role_type == RoleType::worker && unit.health && *unit.health > 0) {
            workers.push_back(&unit);
        }
    }
    std::sort(workers.begin(), workers.end(), [](const auto* left, const auto* right) {
        return left->id < right->id;
    });

    std::vector<CandidateAction> candidates;
    NavigationReservations reservations;
    int priority = 1000;
    for (const auto* worker : workers) {
        std::optional<CandidateAction> candidate = adjacent_sell(turn, *worker, prices, priority);
        if (!candidate) candidate = adjacent_collect(turn, *worker, prices, priority);
        if (!candidate) candidate = economic_move(turn, *worker, prices, reservations, priority);
        if (!candidate) {
            --priority;
            continue;
        }
        if (candidate->command.action == "move") {
            const Pos destination = candidate->command.target_positions.front();
            reservations.destinations.push_back(destination);
            reservations.edges.push_back({worker->pos, destination});
        }
        candidates.push_back(std::move(*candidate));
        --priority;
    }
    return arbitrate(turn, candidates, {}, {}).decision;
}

}  // namespace astra
