#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <utility>
#include "graphalg.h"
#include "accessibility.h"

using namespace std;

namespace MTC {
namespace accessibility {

using std::vector;
using std::string;
using std::set;
using std::map;
using std::unordered_map;

Accessibility::Accessibility(
        int numnodes,
        vector< vector<long long>> edges,
        vector< vector<double> > edgeweights,
        bool twoway) {

    this->numnodes = numnodes;
    this->graph.resize(1);
    this->graph[0] = new MTC::Graph(edges, edgeweights, twoway);
    this->matrix_map.resize(1);
    this->poi_map.resize(1);
}

// Rest of the file implementation would follow...
// For now, let me try a simpler approach