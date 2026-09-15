#include "strategy.hpp"

#include "actions.hpp"
#include "combat.hpp"
#include "defense.hpp"
#include "navigation.hpp"
#include "task_solver.hpp"

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

bool is_character(const UnitObservation& unit) {
    return unit.role_type == RoleType::worker || unit.role_type == RoleType::pioneer;
}

bool same_pos(const Pos& left, const Pos& right) {
    return left.x == right.x && left.y == right.y;
}

bool building_at(const TurnObservation& turn, const Pos& target) {
    const auto found = [&](const UnitObservation& unit) {
        return !is_character(unit) && unit.role_type != RoleType::station &&
               same_pos(unit.pos, target);
    };
    return std::any_of(turn.team_our.roles.begin(), turn.team_our.roles.end(), found) ||
           std::any_of(turn.team_enemy.begin(), turn.team_enemy.end(), found);
}

const UnitObservation* weapon_at(const TurnObservation& turn, const Pos& target) {
    for (const auto& unit : turn.team_our.roles) {
        if (unit.role_type == RoleType::rocket && same_pos(unit.pos, target)) return &unit;
    }
    return nullptr;
}

bool has_item(const UnitObservation& unit, const std::string& name) {
    return std::find(unit.backpack.begin(), unit.backpack.end(), name) != unit.backpack.end();
}

int inventory_count(const UnitObservation& unit, const std::string& name) {
    return static_cast<int>(std::count(unit.backpack.begin(), unit.backpack.end(), name));
}

const ZoneObservation* first_weapon_shop(const TurnObservation& turn) {
    for (const auto& zone : turn.map.zones) {
        if (zone.neutral_type == "weaponShop") return &zone;
    }
    return nullptr;
}

std::optional<int> shop_price(const TurnObservation& turn, const std::string& name) {
    for (const auto& item : turn.weapon_shop) {
        if (item.name == name && item.price >= 0) return item.price;
    }
    return std::nullopt;
}

struct UpgradeTarget {
    Pos pos;
    std::string voucher;
};

std::optional<UpgradeTarget> choose_upgrade_target(const TurnObservation& turn,
                                                   const DefenseLayout& layout) {
    for (const int level : {1, 2}) {
        for (const auto& pos : layout.far_rockets) {
            const auto* weapon = weapon_at(turn, pos);
            if (weapon && weapon->level == std::optional<int>(level)) {
                return UpgradeTarget{pos,
                                     level == 1 ? "WeaponUpgradeVoucher1"
                                                : "WeaponUpgradeVoucher2"};
            }
        }
    }
    const auto* near = weapon_at(turn, layout.near_rocket);
    if (near && near->level && *near->level >= 1 && *near->level < 3) {
        return UpgradeTarget{layout.near_rocket,
                             *near->level == 1 ? "WeaponUpgradeVoucher1"
                                               : "WeaponUpgradeVoucher2"};
    }
    return std::nullopt;
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

int carried_mineral_count(const UnitObservation& unit) {
    int total = 0;
    for (const auto& [name, count] : inventory_counts(unit)) {
        (void)name;
        total += count;
    }
    return total;
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

std::optional<CandidateAction> return_move(const TurnObservation& turn,
                                           const UnitObservation& actor,
                                           const NavigationReservations& reservations,
                                           int priority) {
    const auto path = next_step_toward_any(
        turn, actor.id, station_interaction_cells(turn), reservations);
    if (!path || path->distance == 0) return std::nullopt;
    auto candidate = move_candidate(actor, path->next, priority);
    candidate.source = "defense.return";
    return candidate;
}

std::optional<CandidateAction> rocket_build_action(
    const TurnObservation& turn,
    const UnitObservation& worker,
    Pos target,
    const NavigationReservations& reservations,
    int priority) {
    if (distance(worker.pos, target) == 1) {
        CandidateAction candidate;
        candidate.action_key = worker.id;
        candidate.command.action = "build";
        candidate.command.name = "rocket";
        candidate.command.target_positions = {target};
        candidate.reservation.gold = 25;
        candidate.priority = priority;
        candidate.source = "defense.build_rocket";
        return candidate;
    }
    const auto path = next_step_toward_any(
        turn, worker.id, interaction_cells(turn, target), reservations);
    if (!path || path->distance == 0) return std::nullopt;
    auto candidate = move_candidate(worker, path->next, priority);
    candidate.source = "defense.move_to_rocket_site";
    return candidate;
}

std::optional<CandidateAction> rocket_upgrade_action(
    const TurnObservation& turn,
    const UnitObservation& worker,
    const UpgradeTarget& target,
    const NavigationReservations& reservations,
    int priority) {
    if (has_item(worker, target.voucher)) {
        if (distance(worker.pos, target.pos) <= 1) {
            CandidateAction candidate;
            candidate.action_key = worker.id;
            candidate.command.action = "use";
            candidate.command.name = target.voucher;
            candidate.command.target_positions = {target.pos};
            candidate.reservation.items[worker.id][target.voucher] = 1;
            candidate.priority = priority;
            candidate.source = "defense.upgrade_rocket";
            return candidate;
        }
        const auto path = next_step_toward_any(
            turn, worker.id, interaction_cells(turn, target.pos), reservations);
        if (!path || path->distance == 0) return std::nullopt;
        auto candidate = move_candidate(worker, path->next, priority);
        candidate.source = "defense.move_to_upgrade_target";
        return candidate;
    }

    const auto price = shop_price(turn, target.voucher);
    const auto* shop = first_weapon_shop(turn);
    if (!price || !shop || turn.team_our.gold < *price || !worker.backpack_capacity ||
        worker.backpack.size() >= static_cast<std::size_t>(*worker.backpack_capacity)) {
        return std::nullopt;
    }
    if (distance(worker.pos, shop->pos) <= 1) {
        CandidateAction candidate;
        candidate.action_key = worker.id;
        candidate.command.action = "buy";
        candidate.command.name = target.voucher;
        candidate.command.number = 1;
        candidate.reservation.gold = *price;
        candidate.priority = priority;
        candidate.source = "defense.buy_upgrade";
        return candidate;
    }
    const auto path = next_step_toward_any(
        turn, worker.id, interaction_cells(turn, shop->pos), reservations);
    if (!path || path->distance == 0) return std::nullopt;
    auto candidate = move_candidate(worker, path->next, priority);
    candidate.source = "defense.move_to_weapon_shop";
    return candidate;
}

std::optional<CandidateAction> front_wall_action(
    const TurnObservation& turn,
    const UnitObservation& worker,
    const std::vector<Pos>& missing_walls,
    const NavigationReservations& reservations,
    int priority) {
    const int stones = inventory_count(worker, "stone");
    if (stones >= static_cast<int>(missing_walls.size())) {
        for (const auto& target : missing_walls) {
            if (distance(worker.pos, target) != 1) continue;
            CandidateAction candidate;
            candidate.action_key = worker.id;
            candidate.command.action = "build";
            candidate.command.name = "wall";
            candidate.command.target_positions = {target};
            candidate.reservation.items[worker.id]["stone"] = 1;
            candidate.priority = priority;
            candidate.source = "defense.build_front_wall";
            return candidate;
        }
        std::vector<Pos> goals;
        for (const auto& target : missing_walls) {
            const auto cells = interaction_cells(turn, target);
            goals.insert(goals.end(), cells.begin(), cells.end());
        }
        const auto path = next_step_toward_any(turn, worker.id, goals, reservations);
        if (!path || path->distance == 0) return std::nullopt;
        auto candidate = move_candidate(worker, path->next, priority);
        candidate.source = "defense.move_to_front_wall";
        return candidate;
    }

    if (!worker.backpack_capacity || *worker.backpack_capacity <= 0 ||
        worker.backpack.size() >= static_cast<std::size_t>(*worker.backpack_capacity)) {
        return std::nullopt;
    }
    std::vector<Pos> stone_goals;
    for (const auto& zone : turn.map.zones) {
        if (zone.neutral_type != "stone") continue;
        if (distance(worker.pos, zone.pos) <= 1) {
            CandidateAction candidate;
            candidate.action_key = worker.id;
            candidate.command.action = "collect";
            candidate.command.target_positions = {zone.pos};
            candidate.priority = priority;
            candidate.source = "defense.collect_wall_stone";
            return candidate;
        }
        const auto cells = interaction_cells(turn, zone.pos);
        stone_goals.insert(stone_goals.end(), cells.begin(), cells.end());
    }
    const auto path = next_step_toward_any(turn, worker.id, stone_goals, reservations);
    if (!path || path->distance == 0) return std::nullopt;
    auto candidate = move_candidate(worker, path->next, priority);
    candidate.source = "defense.move_to_stone";
    return candidate;
}

void reserve_move(const UnitObservation& actor,
                  const CandidateAction& candidate,
                  NavigationReservations& reservations) {
    if (candidate.command.action != "move" || candidate.command.target_positions.empty()) return;
    const Pos destination = candidate.command.target_positions.front();
    reservations.destinations.push_back(destination);
    reservations.edges.push_back({actor.pos, destination});
}

std::optional<CandidateAction> pioneer_task_action(
    const TurnObservation& turn,
    const UnitObservation& pioneer,
    const NavigationReservations& reservations,
    int priority) {
    if (!turn.phase_task.empty()) return std::nullopt;
    std::vector<Pos> goals;
    bool adjacent = false;
    for (const auto& task : turn.team_our.player_tasks) {
        if (!task.valid || task.cooldown_rounds != 0) continue;
        if (distance(pioneer.pos, task.position) <= 1) adjacent = true;
        const auto cells = interaction_cells(turn, task.position);
        goals.insert(goals.end(), cells.begin(), cells.end());
    }
    if (adjacent) {
        CandidateAction candidate;
        candidate.action_key = pioneer.id;
        candidate.command.action = "acceptTask";
        candidate.priority = priority;
        candidate.source = "task.accept";
        return candidate;
    }
    const auto path = next_step_toward_any(turn, pioneer.id, goals, reservations);
    if (!path || path->distance == 0) return std::nullopt;
    auto candidate = move_candidate(pioneer, path->next, priority);
    candidate.source = "task.move";
    return candidate;
}

}  // namespace

Decision BaselineStrategy::decide(const TurnObservation& turn) const {
    const int round_in_day = (turn.round_no - 1) % 130 + 1;
    const TaskCandidates task = task_candidates(turn, 4000);
    const auto prices = positive_prices(turn);
    const auto defense_layout = derive_defense_layout(turn);
    std::vector<Pos> missing_rocket_sites;
    if (defense_layout) {
        for (const auto& target : {defense_layout->far_rockets[0],
                                   defense_layout->far_rockets[1],
                                   defense_layout->near_rocket}) {
            if (!building_at(turn, target)) missing_rocket_sites.push_back(target);
        }
    }
    std::optional<UpgradeTarget> upgrade_target;
    if (defense_layout && missing_rocket_sites.empty()) {
        upgrade_target = choose_upgrade_target(turn, *defense_layout);
    }
    std::vector<Pos> missing_front_walls;
    if (defense_layout && missing_rocket_sites.empty()) {
        for (const auto& target : defense_layout->front_wall_tiles) {
            if (!building_at(turn, target)) missing_front_walls.push_back(target);
        }
    }
    std::vector<const UnitObservation*> characters;
    for (const auto& unit : turn.team_our.roles) {
        if (is_character(unit) && unit.health && *unit.health > 0) {
            characters.push_back(&unit);
        }
    }
    std::sort(characters.begin(), characters.end(), [](const auto* left, const auto* right) {
        const bool left_carrying = carried_mineral_count(*left) > 0;
        const bool right_carrying = carried_mineral_count(*right) > 0;
        if (left_carrying != right_carrying) return left_carrying > right_carrying;
        if (left->role_type != right->role_type) return left->role_type == RoleType::worker;
        return left->id < right->id;
    });
    std::optional<int> voucher_holder;
    if (upgrade_target) {
        for (const auto* actor : characters) {
            if (actor->role_type == RoleType::worker && has_item(*actor, upgrade_target->voucher)) {
                voucher_holder = actor->id;
                break;
            }
        }
    }
    std::optional<int> wall_actor;
    if (!missing_front_walls.empty()) {
        for (const auto* actor : characters) {
            if (actor->role_type == RoleType::worker && actor->id != voucher_holder &&
                inventory_count(*actor, "stone") > 0) {
                wall_actor = actor->id;
                break;
            }
        }
        if (!wall_actor) {
            for (auto actor = characters.rbegin(); actor != characters.rend(); ++actor) {
                if ((*actor)->role_type == RoleType::worker && (*actor)->id != voucher_holder) {
                    wall_actor = (*actor)->id;
                    break;
                }
            }
        }
    }
    std::optional<int> upgrade_actor = voucher_holder;
    if (upgrade_target && !upgrade_actor) {
        for (const auto* actor : characters) {
            if (actor->role_type == RoleType::worker && actor->id != wall_actor) {
                upgrade_actor = actor->id;
                break;
            }
        }
        if (!upgrade_actor) upgrade_actor = wall_actor;
    }

    std::vector<CandidateAction> candidates = task.actions;
    const auto combat = combat_candidates(turn, 3000);
    candidates.insert(candidates.end(), combat.begin(), combat.end());
    NavigationReservations reservations;
    reservations.destinations = missing_rocket_sites;
    reservations.destinations.insert(reservations.destinations.end(),
                                     missing_front_walls.begin(),
                                     missing_front_walls.end());
    std::size_t defense_index = 0;
    int priority = 1000;
    for (const auto* actor : characters) {
        std::optional<CandidateAction> candidate;
        if (actor->role_type == RoleType::pioneer && !turn.phase_task.empty()) {
            candidate = std::nullopt;
        } else if (round_in_day > 70) {
            candidate = return_move(turn, *actor, reservations, priority);
        } else if (actor->role_type == RoleType::worker) {
            const auto return_path = next_step_toward_any(
                turn, actor->id, station_interaction_cells(turn), reservations);
            const int daylight_remaining = 70 - round_in_day + 1;
            if (return_path && daylight_remaining <= return_path->distance + 2) {
                candidate = return_move(turn, *actor, reservations, priority);
            } else if (station_interaction_cells(turn).empty() && daylight_remaining <= 2) {
                candidate = std::nullopt;
            } else {
                if (defense_index < missing_rocket_sites.size()) {
                    candidate = rocket_build_action(turn,
                                                    *actor,
                                                    missing_rocket_sites[defense_index],
                                                    reservations,
                                                    priority);
                    ++defense_index;
                }
                if (!candidate && upgrade_target && upgrade_actor == actor->id) {
                    candidate = rocket_upgrade_action(turn,
                                                      *actor,
                                                      *upgrade_target,
                                                      reservations,
                                                      priority);
                }
                if (!candidate && !missing_front_walls.empty() && wall_actor == actor->id) {
                    candidate = front_wall_action(turn,
                                                  *actor,
                                                  missing_front_walls,
                                                  reservations,
                                                  priority);
                }
                if (!candidate) candidate = adjacent_sell(turn, *actor, prices, priority);
                if (!candidate) candidate = adjacent_collect(turn, *actor, prices, priority);
                if (!candidate) {
                    candidate = economic_move(turn, *actor, prices, reservations, priority);
                }
            }
        } else {
            const auto return_path = next_step_toward_any(
                turn, actor->id, station_interaction_cells(turn), reservations);
            const int daylight_remaining = 70 - round_in_day + 1;
            if (return_path && daylight_remaining <= return_path->distance + 2) {
                candidate = return_move(turn, *actor, reservations, priority);
            } else {
                candidate = pioneer_task_action(turn, *actor, reservations, priority);
            }
        }
        if (!candidate) {
            --priority;
            continue;
        }
        reserve_move(*actor, *candidate, reservations);
        candidates.push_back(std::move(*candidate));
        --priority;
    }
    ArbitrationRules rules = task.rules;
    if (defense_layout) {
        rules.weapon_build_tiles = defense_layout->weapon_build_tiles;
        rules.wall_build_tiles = defense_layout->wall_build_tiles;
    }
    return arbitrate(turn, candidates, task.top_level, rules).decision;
}

}  // namespace astra
