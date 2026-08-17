#pragma once

#include "audio/graph/GraphTypes.h"

#include <juce_graphics/juce_graphics.h>

#include <map>
#include <vector>

namespace better
{
/** Logical, JUCE-independent representation of the processing graph.

    Structural mutations are validated here before they reach the realtime
    runtime. The fixed input/output endpoints and stable module UIDs also make
    the document suitable as the persistent source of graph state.
*/
class GraphDocument final
{
public:
    GraphDocument();
    GraphDocument (const GraphDocument&) = default;
    GraphDocument& operator= (const GraphDocument&) = default;
    GraphDocument (GraphDocument&&) noexcept = default;
    GraphDocument& operator= (GraphDocument&&) noexcept = default;

    /** Creates a document with the fixed endpoints but no connections. */
    static GraphDocument createEmpty();

    // Mutations fail without changing the document when an invariant is violated.
    bool addModuleNode (const ModuleDescriptor& descriptor,
                        juce::Point<float> position,
                        GraphNodeUID& createdNode);
    bool restoreModuleNode (GraphNodeUID uid,
                            const ModuleDescriptor& descriptor,
                            juce::Point<float> position);
    bool removeNode (GraphNodeUID uid);

    bool addConnection (GraphConnectionDescription connection);
    bool removeConnection (GraphConnectionDescription connection);
    void clearConnections();

    bool setNodePosition (GraphNodeUID uid, juce::Point<float> position);

    const GraphNodeDescription* findNode (GraphNodeUID uid) const;
    std::vector<GraphNodeDescription> getNodes() const;
    const std::vector<GraphConnectionDescription>& getConnections() const;

    GraphNodeUID getNextModuleNodeUID() const;
    bool restoreNextModuleNodeUID (GraphNodeUID uid);

private:
    // Connections form a directed acyclic graph; feedback loops are rejected.
    bool wouldCreateCycle (GraphConnectionDescription connection) const;
    GraphNodeUID getMinimumNextModuleNodeUID() const;

    std::map<GraphNodeUID, GraphNodeDescription> nodes;
    std::vector<GraphConnectionDescription> connections;
    GraphNodeUID nextModuleNodeUID = firstModuleNodeUID;
};
} // namespace better
