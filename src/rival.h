#pragma once

#include <LR2_customir_api.h>

#include "http.h"
#include "json_util.h"

#include <filesystem>
#include <vector>

std::filesystem::path RivalDirectory();

HttpStatus FetchRivalList(json& out);
bool WriteRivalListCache(const json& listBody);
json BuildRivalListCache(const json& snapshot);

HttpStatus FetchRivalScore(int rivalId, json& out);
bool WriteRivalScoreJsonCache(int rivalId, const json& scoreBody);
bool ReadRivalScoreJsonCache(int rivalId, json& out);
bool FillRivalScoresFromJson(const json& body, std::vector<openlr2::IRRivalScore>& out);
