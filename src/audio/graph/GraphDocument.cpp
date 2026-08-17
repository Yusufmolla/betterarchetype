#include "audio/graph/GraphDocument.h"
#include "audio/graph/ModuleRegistry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace better
{
namespace
{
constexpr auto maximumModuleNodeUID = static_cast<GraphNodeUID> (std::numeric_limits<int>::max() - 1);
constexpr auto exhaustedModuleNodeUID = maximumModuleNodeUID + 1;

bool isValidPosition (juce::Point<float> position)
{
    return std::isfinite (position.x) && std::isfinite (position.y);
}

// Accept only descriptors registered by the application instead of trusting
// descriptor metadata supplied by restored state.
const ModuleDescriptor* findCanonicalDescriptor (const ModuleDescriptor& descriptor)
{
    return ModuleRegistry::findModule (descriptor.moduleId);
}

GraphNodeDescription makeEndpoint (GraphNodeUID uid,
                                   GraphNodeRole role,
                                   const juce::String& name,
                                   juce::Point<float> position)
{
    GraphNodeDescription node;
    node.uid = uid;
    node.role = role;
    node.name = name;
    node.x = position.x;
    node.y = position.y;
    return node;
}
} // namespace

GraphDocument::GraphDocument()
{
    nodes.emplace (inputNodeUID,
                   makeEndpoint (inputNodeUID, GraphNodeRole::input, "Input", { 130.0f, 260.0f }));
    nodes.emplace (outputNodeUID,
                   makeEndpoint (outputNodeUID, GraphNodeRole::output, "Output", { 820.0f, 260.0f }));
    connections.push_back ({ inputNodeUID, outputNodeUID });
}

GraphDocument GraphDocument::createEmpty()
{
    GraphDocument document;
    document.clearConnections();
    return document;
}

bool GraphDocument::addModuleNode (const ModuleDescriptor& descriptor,
                                   juce::Point<float> position,
                                   GraphNodeUID& createdNode)
{
    if (findCanonicalDescriptor (descriptor) == nullptr || ! isValidPosition (position))
        return false;

    auto uid = nextModuleNodeUID;

    while (uid <= maximumModuleNodeUID && nodes.find (uid) != nodes.end())
        ++uid;

    if (uid > maximumModuleNodeUID)
        return false;

    if (! restoreModuleNode (uid, descriptor, position))
        return false;

    createdNode = uid;
    return true;
}

// Restore keeps the persisted UID so connections and module state continue to
// refer to the same logical node.
bool GraphDocument::restoreModuleNode (GraphNodeUID uid,
                                       const ModuleDescriptor& descriptor,
                                       juce::Point<float> position)
{
    const auto* canonicalDescriptor = findCanonicalDescriptor (descriptor);

    if (uid < firstModuleNodeUID || uid > maximumModuleNodeUID
        || canonicalDescriptor == nullptr || ! isValidPosition (position)
        || nodes.find (uid) != nodes.end())
    {
        return false;
    }

    GraphNodeDescription node;
    node.uid = uid;
    node.role = GraphNodeRole::module;
    node.moduleId = canonicalDescriptor->moduleId;
    node.name = canonicalDescriptor->name;
    node.x = position.x;
    node.y = position.y;
    nodes.emplace (uid, std::move (node));

    if (uid >= nextModuleNodeUID)
        nextModuleNodeUID = uid == maximumModuleNodeUID ? exhaustedModuleNodeUID : uid + 1;

    return true;
}

bool GraphDocument::removeNode (GraphNodeUID uid)
{
    if (uid == inputNodeUID || uid == outputNodeUID || nodes.erase (uid) == 0)
        return false;

    connections.erase (std::remove_if (connections.begin(), connections.end(), [uid] (const auto& connection)
    {
        return connection.source == uid || connection.destination == uid;
    }), connections.end());
    return true;
}

// Validate endpoint existence, direction, uniqueness and acyclicity before the
// logical edge becomes part of the document.
bool GraphDocument::addConnection (GraphConnectionDescription connection)
{
    if (connection.source == connection.destination)
        return false;

    const auto source = nodes.find (connection.source);
    const auto destination = nodes.find (connection.destination);

    if (source == nodes.end() || destination == nodes.end()
        || source->second.role == GraphNodeRole::output
        || destination->second.role == GraphNodeRole::input
        || std::find (connections.begin(), connections.end(), connection) != connections.end()
        || wouldCreateCycle (connection))
    {
        return false;
    }

    connections.push_back (connection);
    return true;
}

bool GraphDocument::removeConnection (GraphConnectionDescription connection)
{
    const auto found = std::find (connections.begin(), connections.end(), connection);

    if (found == connections.end())
        return false;

    connections.erase (found);
    return true;
}

void GraphDocument::clearConnections()
{
    connections.clear();
}

bool GraphDocument::setNodePosition (GraphNodeUID uid, juce::Point<float> position)
{
    if (! isValidPosition (position))
        return false;

    const auto found = nodes.find (uid);

    if (found == nodes.end())
        return false;

    found->second.x = position.x;
    found->second.y = position.y;
    return true;
}

const GraphNodeDescription* GraphDocument::findNode (GraphNodeUID uid) const
{
    const auto found = nodes.find (uid);
    return found != nodes.end() ? &found->second : nullptr;
}

std::vector<GraphNodeDescription> GraphDocument::getNodes() const
{
    std::vector<GraphNodeDescription> result;
    result.reserve (nodes.size());

    for (const auto& [uid, node] : nodes)
    {
        juce::ignoreUnused (uid);
        result.push_back (node);
    }

    return result;
}

const std::vector<GraphConnectionDescription>& GraphDocument::getConnections() const
{
    return connections;
}

GraphNodeUID GraphDocument::getNextModuleNodeUID() const
{
    return nextModuleNodeUID;
}

bool GraphDocument::restoreNextModuleNodeUID (GraphNodeUID uid)
{
    if (uid < getMinimumNextModuleNodeUID() || uid > exhaustedModuleNodeUID)
        return false;

    nextModuleNodeUID = uid;
    return true;
}

// Follow outgoing edges from the proposed destination. Reaching the proposed
// source means the new edge would close a directed cycle.
bool GraphDocument::wouldCreateCycle (GraphConnectionDescription connection) const
{
    std::vector<GraphNodeUID> pending { connection.destination };
    std::set<GraphNodeUID> visited;

    while (! pending.empty())
    {
        const auto uid = pending.back();
        pending.pop_back();

        if (uid == connection.source)
            return true;

        if (! visited.insert (uid).second)
            continue;

        for (const auto& existing : connections)
            if (existing.source == uid)
                pending.push_back (existing.destination);
    }

    return false;
}

GraphNodeUID GraphDocument::getMinimumNextModuleNodeUID() const
{
    auto minimum = firstModuleNodeUID;

    for (const auto& [uid, node] : nodes)
    {
        if (node.role != GraphNodeRole::module)
            continue;

        minimum = uid == maximumModuleNodeUID ? exhaustedModuleNodeUID
                                              : juce::jmax (minimum, uid + 1);
    }

    return minimum;
}
} // namespace better
