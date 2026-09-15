#pragma once

#include <json/json.h>

#include <string>

namespace astra {

bool parse_json(const std::string& text, Json::Value& output, std::string& error);
std::string write_json(const Json::Value& value);

}  // namespace astra
