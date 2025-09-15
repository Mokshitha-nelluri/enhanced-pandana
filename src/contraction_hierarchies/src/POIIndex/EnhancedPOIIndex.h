#pragma once

#include <vector>
#include <memory>
#include <algorithm>
#include <queue>
#include "POIIndex.h"

namespace CH {

// Enhanced bucket entry with partial ordering support
struct PartialBucketEntry {
    NodeID node;
    EdgeWeight distance;
    bool in_partial_order;
    
    PartialBucketEntry() : node(UINT_MAX), distance(UINT_MAX), in_partial_order(false) {}
    PartialBucketEntry(NodeID n, EdgeWeight d, bool partial = false) 
        : node(n), distance(d), in_partial_order(partial) {}
    
    inline bool operator<(const PartialBucketEntry& other) const {
        return distance < other.distance;
    }
};

// Partial ordering bucket for k-nearest queries
class PartialBucket {
private:
    std::vector<PartialBucketEntry> k_smallest;     // Maintained in sorted order
    std::vector<PartialBucketEntry> overflow;       // Unsorted overflow entries
    unsigned max_k;
    unsigned max_total;
    
public:
    PartialBucket(unsigned k, unsigned total_capacity) 
        : max_k(k), max_total(total_capacity) {
        k_smallest.reserve(k);
        overflow.reserve(total_capacity - k);
    }
    
    void insert(const PartialBucketEntry& entry) {
        if (k_smallest.size() < max_k) {
            // Still building k smallest - insert in sorted order
            auto pos = std::lower_bound(k_smallest.begin(), k_smallest.end(), entry);
            k_smallest.insert(pos, entry);
        } else if (entry.distance < k_smallest.back().distance) {
            // Better than worst in k_smallest
            // Move worst to overflow and insert new entry
            if (overflow.size() < overflow.capacity()) {
                overflow.push_back(k_smallest.back());
            }
            k_smallest.pop_back();
            
            auto pos = std::lower_bound(k_smallest.begin(), k_smallest.end(), entry);
            k_smallest.insert(pos, entry);
        } else if (overflow.size() < overflow.capacity()) {
            // Add to unsorted overflow
            overflow.push_back(entry);
        }
        // Otherwise, discard (bucket full and entry not good enough)
    }
    
    void getKSmallest(std::vector<PartialBucketEntry>& result, unsigned k) {
        result.clear();
        
        unsigned to_return = std::min(k, static_cast<unsigned>(k_smallest.size()));
        result.insert(result.end(), k_smallest.begin(), k_smallest.begin() + to_return);
        
        // If we need more and have overflow, do partial sort
        if (to_return < k && !overflow.empty()) {
            unsigned needed = k - to_return;
            
            if (needed >= overflow.size()) {
                // Need all overflow entries
                std::sort(overflow.begin(), overflow.end());
                result.insert(result.end(), overflow.begin(), overflow.end());
            } else {
                // Partial sort overflow to get needed entries
                std::partial_sort(overflow.begin(), 
                                overflow.begin() + needed, 
                                overflow.end());
                result.insert(result.end(), overflow.begin(), overflow.begin() + needed);
            }
        }
    }
    
    unsigned size() const {
        return k_smallest.size() + overflow.size();
    }
    
    bool wouldImproveKSmallest(EdgeWeight distance) const {
        return k_smallest.size() < max_k || 
               (k_smallest.size() == max_k && distance < k_smallest.back().distance);
    }
};

// Enhanced POI Index using partial ordering
template<typename QueryGraphT>
class EnhancedPOIIndex : public POIIndex<QueryGraphT> {
private:
    typedef std::map<NodeID, PartialBucket> PartialBucketIndex;
    PartialBucketIndex partialBucketIndex;
    unsigned partial_k_threshold;  // Use partial ordering when k <= this threshold
    
public:
    EnhancedPOIIndex(QueryGraphT* _graph, unsigned _maxDistanceToConsider, 
                    unsigned _maxNumberOfPOIsInBucket, unsigned _numberOfThreads = 1,
                    unsigned _partialKThreshold = 10) 
        : POIIndex<QueryGraphT>(_graph, _maxDistanceToConsider, _maxNumberOfPOIsInBucket, _numberOfThreads),
          partial_k_threshold(_partialKThreshold) {}
    
    // Enhanced addPOIToIndex with partial ordering
    inline void addPOIToIndexPartial(const NodeID node) {
        CHASSERT(node < this->graph->GetNumberOfNodes(), "Node ID of POI is out of bounds");
        
        this->additionHeap->Clear();
        CHASSERT(this->additionHeap->Size() == 0, "AdditionHeap not empty");
        
        // Explore search space from node v
        this->additionHeap->Insert(node, 0, node);
        
        while(this->additionHeap->Size() > 0) {
            const NodeID currentNode = this->additionHeap->DeleteMin();
            const unsigned toDistance = this->additionHeap->GetKey(currentNode);
            
            if(toDistance > this->maxDistanceToConsider)
                return;
            
            // Add venue to partial bucket of u
            PartialBucket& bucket = partialBucketIndex[currentNode];
            if (partialBucketIndex.find(currentNode) == partialBucketIndex.end()) {
                partialBucketIndex[currentNode] = PartialBucket(
                    std::min(this->maxNumberOfPOIsInBucket, partial_k_threshold),
                    this->maxNumberOfPOIsInBucket
                );
            }
            
            PartialBucketEntry entry(node, toDistance);
            partialBucketIndex[currentNode].insert(entry);
            
            // Continue exploration (same as original implementation)
            for (typename QueryGraphT::EdgeIterator edge = this->graph->BeginEdges(currentNode); 
                 edge < this->graph->EndEdges(currentNode); ++edge) {
                if(this->graph->GetEdgeData(edge).backward) {
                    const NodeID to = this->graph->GetTarget(edge);
                    const EdgeWeight edgeDistance = this->graph->GetEdgeData(edge).distance;
                    
                    // Stalling check
                    if(this->graph->GetEdgeData(edge).forward && this->additionHeap->WasInserted(to)) {
                        if(this->additionHeap->GetKey(to) + edgeDistance < toDistance) {
                            break;
                        }
                    }
                    
                    // Insert or update
                    if (!this->additionHeap->WasInserted(to)) {
                        this->additionHeap->Insert(to, toDistance + edgeDistance, node);
                    } else if (toDistance + edgeDistance < this->additionHeap->GetKey(to)) {
                        this->additionHeap->DecreaseKey(to, toDistance + edgeDistance);
                    }
                }
            }
        }
    }
    
    // Enhanced getNearestPOIs with partial ordering
    inline void getNearestPOIsPartial(NodeID node, std::vector<BucketEntry>& resultingVenues, 
                                     unsigned _maxDistanceToConsider, unsigned _maxNumberOfPOIsInBucket, 
                                     unsigned threadID = 0) {
        
        CHASSERT(0 == resultingVenues.size(), "Resulting vector is not empty");
        CHASSERT(_maxDistanceToConsider <= this->maxDistanceToConsider, 
                "Maximum distance must not be larger in query than during preprocessing");
        CHASSERT(_maxNumberOfPOIsInBucket <= this->maxNumberOfPOIsInBucket, 
                "Maximum number of POIs must not be larger in query than during preprocessing");
        
        // Use partial ordering if requesting few POIs
        bool use_partial_ordering = (_maxNumberOfPOIsInBucket <= partial_k_threshold);
        
        if (use_partial_ordering && partialBucketIndex.find(node) != partialBucketIndex.end()) {
            // Use enhanced partial ordering approach
            std::vector<PartialBucketEntry> partial_results;
            partialBucketIndex[node].getKSmallest(partial_results, _maxNumberOfPOIsInBucket);
            
            // Convert to standard bucket entries
            for (const auto& entry : partial_results) {
                if (entry.distance <= _maxDistanceToConsider) {
                    resultingVenues.push_back(BucketEntry(entry.node, entry.distance));
                }
            }
        } else {
            // Fall back to standard implementation
            this->getNearestPOIs(node, resultingVenues, _maxDistanceToConsider, 
                               _maxNumberOfPOIsInBucket, threadID);
        }
    }
    
    // Batch POI queries with frontier compression
    void getBatchNearestPOIs(const std::vector<NodeID>& nodes, 
                           std::vector<std::vector<BucketEntry>>& batchResults,
                           unsigned _maxDistanceToConsider, unsigned _maxNumberOfPOIsInBucket,
                           unsigned threadID = 0) {
        
        batchResults.resize(nodes.size());
        
        // Simple batch processing - can be enhanced with frontier compression
        #pragma omp parallel for if(nodes.size() > 10)
        for (size_t i = 0; i < nodes.size(); ++i) {
            getNearestPOIsPartial(nodes[i], batchResults[i], 
                                _maxDistanceToConsider, _maxNumberOfPOIsInBucket, 
                                omp_get_thread_num());
        }
    }
};

} // namespace CH
