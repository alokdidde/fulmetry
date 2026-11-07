// JSON-RPC 2.0 client for the Python engine worker. Methods and their schemas: contracts/worker/.
#pragma once
#include <string>

#include "fulmetry/core/Result.h"

namespace fulmetry {

class IWorkerClient {
public:
    virtual ~IWorkerClient() = default;
    // params and the result are JSON text, validated against contracts/worker/<method>.json.
    virtual Result<std::string> call(const std::string& method, const std::string& paramsJson) = 0;
};

}  // namespace fulmetry
