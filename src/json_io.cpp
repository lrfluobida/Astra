#include "json_io.hpp"

#include <memory>

namespace astra {

bool parse_json(const std::string& text, Json::Value& output, std::string& error) {
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    builder["allowComments"] = false;
    builder["allowTrailingCommas"] = false;
    builder["strictRoot"] = true;
    builder["failIfExtra"] = true;
    builder["rejectDupKeys"] = true;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    return reader->parse(text.data(), text.data() + text.size(), &output, &error);
}

std::string write_json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["commentStyle"] = "None";
    builder["emitUTF8"] = true;
    return Json::writeString(builder, value);
}

}  // namespace astra
