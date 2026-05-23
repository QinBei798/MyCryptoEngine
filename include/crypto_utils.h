#pragma once

#include <cstdint>
#include <string>
#include <vector>

void derive_keys(const std::string& password,
                 const std::vector<uint8_t>& salt,
                 std::vector<uint8_t>& out_sm4_key,
                 std::vector<uint8_t>& out_hmac_key);

std::vector<uint8_t> calc_hmac_sm3(const std::vector<uint8_t>& hmac_key,
                                   const std::vector<uint8_t>& data);
