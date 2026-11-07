// Which checks depend on which parts, and what invalidates them. Owned by S1 (Project model).
//
// A check lists the parts it reads (Check::inputs) and the source hash of each part at its last
// run (Check::inputHashes). When a part's source changes, every check that reads it goes back to
// Unresolved until it runs again.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "fulmetry/core/Model.h"
#include "fulmetry/core/Result.h"

namespace fulmetry {

class CheckGraph {
public:
    // Edits `project` in place; the project must outlive the graph. The current hash of each part
    // is seeded from the checks' recorded inputHashes (the core model has no per-part hash yet).
    explicit CheckGraph(Project& project);

    // The checks whose inputs include `part`, in project order.
    std::vector<CheckId> checksFor(const PartId& part) const;

    // The part's source is now `newHash`. Every check that lists the part and has not already
    // recorded `newHash` for it becomes Unresolved ("Changed: <part name>") and records the hash.
    // Returns the ids of the checks it changed (empty when nothing changed).
    // Errors: "not_found" when the project has no such part.
    Result<std::vector<CheckId>> partChanged(const PartId& part, const std::string& newHash);

    // Stores a run's verdict and the current hash of each of the check's inputs (inputs whose
    // hash is unknown are left out). Errors: "not_found" for an unknown check,
    // "invalid_argument" for Verdict::Unresolved (use partChanged to invalidate).
    Status recordVerdict(const CheckId& check, Verdict verdict, std::string reason);

    // The last hash seen for a part, or empty when none is known.
    std::string currentHash(const PartId& part) const;

private:
    Check* findCheck(const CheckId& id);

    Project& project_;
    std::map<PartId, std::string> hashes_;
};

}  // namespace fulmetry
