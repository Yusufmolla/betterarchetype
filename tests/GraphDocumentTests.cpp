#include "audio/graph/GraphDocument.h"
#include "audio/graph/ModuleRegistry.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <limits>

namespace better::tests
{
namespace
{
bool containsConnection (const GraphDocument& document,
                         GraphNodeUID source,
                         GraphNodeUID destination)
{
    const auto& connections = document.getConnections();
    return std::find (connections.begin(), connections.end(),
                      GraphConnectionDescription { source, destination }) != connections.end();
}

class GraphDocumentTests final : public juce::UnitTest
{
public:
    GraphDocumentTests()
        : UnitTest ("Graph document", "BetterArchetype")
    {
    }

    void runTest() override
    {
        const auto* driveDescriptor = ModuleRegistry::findModule ("drive");
        expect (driveDescriptor != nullptr, "The Drive descriptor is required by these tests");

        if (driveDescriptor == nullptr)
            return;

        beginTest ("Default and empty documents keep their fixed endpoints");

        GraphDocument defaultDocument;
        const auto* defaultInput = defaultDocument.findNode (inputNodeUID);
        const auto* defaultOutput = defaultDocument.findNode (outputNodeUID);

        expectEquals (static_cast<int> (defaultDocument.getNodes().size()), 2);
        expect (defaultInput != nullptr);
        expect (defaultOutput != nullptr);

        if (defaultInput != nullptr)
            expect (defaultInput->role == GraphNodeRole::input);

        if (defaultOutput != nullptr)
            expect (defaultOutput->role == GraphNodeRole::output);

        expectEquals (static_cast<int> (defaultDocument.getConnections().size()), 1);
        expect (containsConnection (defaultDocument, inputNodeUID, outputNodeUID));
        expectEquals (defaultDocument.getNextModuleNodeUID(), firstModuleNodeUID);

        auto emptyDocument = GraphDocument::createEmpty();
        expectEquals (static_cast<int> (emptyDocument.getNodes().size()), 2);
        expect (emptyDocument.findNode (inputNodeUID) != nullptr);
        expect (emptyDocument.findNode (outputNodeUID) != nullptr);
        expect (emptyDocument.getConnections().empty());
        expectEquals (emptyDocument.getNextModuleNodeUID(), firstModuleNodeUID);

        beginTest ("Module allocation rejects invalid and duplicate identities");

        auto moduleDocument = GraphDocument::createEmpty();
        auto unknownDescriptor = *driveDescriptor;
        unknownDescriptor.moduleId = "does_not_exist";
        GraphNodeUID failedNode = 777;

        expect (! moduleDocument.addModuleNode (unknownDescriptor, { 100.0f, 120.0f }, failedNode));
        expectEquals (failedNode, static_cast<GraphNodeUID> (777));
        expectEquals (moduleDocument.getNextModuleNodeUID(), firstModuleNodeUID);

        GraphNodeUID nonFiniteNode = 778;
        expect (! moduleDocument.addModuleNode (
            *driveDescriptor,
            { std::numeric_limits<float>::infinity(), 120.0f },
            nonFiniteNode));
        expectEquals (nonFiniteNode, static_cast<GraphNodeUID> (778));
        expectEquals (moduleDocument.getNextModuleNodeUID(), firstModuleNodeUID);

        GraphNodeUID firstNode = 0;
        expect (moduleDocument.addModuleNode (*driveDescriptor, { 100.0f, 120.0f }, firstNode));
        expectEquals (firstNode, firstModuleNodeUID);

        const auto* storedFirstNode = moduleDocument.findNode (firstNode);
        expect (storedFirstNode != nullptr);

        if (storedFirstNode != nullptr)
        {
            expect (storedFirstNode->role == GraphNodeRole::module);
            expectEquals (storedFirstNode->moduleId, driveDescriptor->moduleId);
            expectEquals (storedFirstNode->name, driveDescriptor->name);
        }

        expect (! moduleDocument.restoreModuleNode (0, *driveDescriptor, { 0.0f, 0.0f }));
        expect (! moduleDocument.restoreModuleNode (inputNodeUID, *driveDescriptor, { 0.0f, 0.0f }));
        expect (! moduleDocument.restoreModuleNode (outputNodeUID, *driveDescriptor, { 0.0f, 0.0f }));
        expect (! moduleDocument.restoreModuleNode (firstModuleNodeUID - 1,
                                                     *driveDescriptor,
                                                     { 0.0f, 0.0f }));
        expect (! moduleDocument.restoreModuleNode (firstNode, *driveDescriptor, { 0.0f, 0.0f }));

        constexpr GraphNodeUID restoredNode = 145;
        expect (moduleDocument.restoreModuleNode (restoredNode,
                                                  *driveDescriptor,
                                                  { 200.0f, 220.0f }));
        expectEquals (moduleDocument.getNextModuleNodeUID(), restoredNode + 1);

        const auto cursorBeforeStaleRestore = moduleDocument.getNextModuleNodeUID();
        expect (! moduleDocument.restoreNextModuleNodeUID (restoredNode));
        expectEquals (moduleDocument.getNextModuleNodeUID(), cursorBeforeStaleRestore);

        constexpr GraphNodeUID savedNextNode = 180;
        expect (moduleDocument.restoreNextModuleNodeUID (savedNextNode));
        GraphNodeUID allocatedAfterRestore = 0;
        expect (moduleDocument.addModuleNode (*driveDescriptor,
                                              { 300.0f, 320.0f },
                                              allocatedAfterRestore));
        expectEquals (allocatedAfterRestore, savedNextNode);

        constexpr auto maximumModuleNodeUID = static_cast<GraphNodeUID> (std::numeric_limits<int>::max() - 1);
        constexpr auto exhaustedModuleNodeUID = maximumModuleNodeUID + 1;
        auto exhaustedDocument = GraphDocument::createEmpty();
        expect (exhaustedDocument.restoreModuleNode (maximumModuleNodeUID,
                                                     *driveDescriptor,
                                                     { 0.0f, 0.0f }));
        expectEquals (exhaustedDocument.getNextModuleNodeUID(), exhaustedModuleNodeUID);
        expect (exhaustedDocument.restoreNextModuleNodeUID (exhaustedModuleNodeUID));
        expect (! exhaustedDocument.restoreNextModuleNodeUID (exhaustedModuleNodeUID + 1));
        expectEquals (exhaustedDocument.getNextModuleNodeUID(), exhaustedModuleNodeUID);
        expect (! exhaustedDocument.restoreModuleNode (exhaustedModuleNodeUID,
                                                       *driveDescriptor,
                                                       { 0.0f, 0.0f }));

        GraphNodeUID nodeBeyondExhaustion = 999;
        expect (! exhaustedDocument.addModuleNode (*driveDescriptor,
                                                   { 0.0f, 0.0f },
                                                   nodeBeyondExhaustion));
        expectEquals (nodeBeyondExhaustion, static_cast<GraphNodeUID> (999));

        beginTest ("Positions are finite and node removal cascades connections");

        auto mutationDocument = GraphDocument::createEmpty();
        GraphNodeUID mutationFirst = 0;
        GraphNodeUID mutationSecond = 0;
        expect (mutationDocument.addModuleNode (*driveDescriptor, { 10.0f, 20.0f }, mutationFirst));
        expect (mutationDocument.addModuleNode (*driveDescriptor, { 30.0f, 40.0f }, mutationSecond));
        expect (mutationDocument.setNodePosition (mutationFirst, { 50.0f, 60.0f }));
        expect (! mutationDocument.setNodePosition (999999, { 1.0f, 2.0f }));
        expect (! mutationDocument.setNodePosition (
            mutationFirst,
            { std::numeric_limits<float>::infinity(), 60.0f }));
        expect (! mutationDocument.setNodePosition (
            mutationFirst,
            { 50.0f, std::numeric_limits<float>::quiet_NaN() }));

        const auto* positionedNode = mutationDocument.findNode (mutationFirst);
        expect (positionedNode != nullptr);

        if (positionedNode != nullptr)
        {
            expectEquals (positionedNode->x, 50.0f);
            expectEquals (positionedNode->y, 60.0f);
        }

        expect (mutationDocument.addConnection ({ inputNodeUID, mutationFirst }));
        expect (mutationDocument.addConnection ({ mutationFirst, mutationSecond }));
        expect (mutationDocument.addConnection ({ mutationSecond, outputNodeUID }));
        expect (! mutationDocument.removeNode (inputNodeUID));
        expect (! mutationDocument.removeNode (outputNodeUID));
        expect (mutationDocument.removeNode (mutationFirst));
        expect (mutationDocument.findNode (mutationFirst) == nullptr);
        expect (! containsConnection (mutationDocument, inputNodeUID, mutationFirst));
        expect (! containsConnection (mutationDocument, mutationFirst, mutationSecond));
        expect (containsConnection (mutationDocument, mutationSecond, outputNodeUID));

        beginTest ("Connections form an acyclic graph with fan-out and fan-in");

        auto connectionDocument = GraphDocument::createEmpty();
        GraphNodeUID connectionA = 0;
        GraphNodeUID connectionB = 0;
        GraphNodeUID connectionC = 0;
        GraphNodeUID connectionD = 0;
        expect (connectionDocument.addModuleNode (*driveDescriptor, { 0.0f, 0.0f }, connectionA));
        expect (connectionDocument.addModuleNode (*driveDescriptor, { 0.0f, 0.0f }, connectionB));
        expect (connectionDocument.addModuleNode (*driveDescriptor, { 0.0f, 0.0f }, connectionC));
        expect (connectionDocument.addModuleNode (*driveDescriptor, { 0.0f, 0.0f }, connectionD));
        expect (connectionDocument.addConnection ({ inputNodeUID, connectionA }));
        expect (connectionDocument.addConnection ({ connectionA, connectionB }));
        expect (connectionDocument.addConnection ({ connectionB, outputNodeUID }));
        expect (connectionDocument.addConnection ({ connectionA, connectionC }),
                "One source must be allowed to feed multiple destinations");

        expect (connectionDocument.addConnection ({ inputNodeUID, connectionC }),
                "Multiple sources must be allowed to feed one destination");

        const auto validConnectionCount = connectionDocument.getConnections().size();
        expect (! connectionDocument.addConnection ({ 0, connectionD }));
        expect (! connectionDocument.addConnection ({ connectionD, 999999 }));
        expect (! connectionDocument.addConnection ({ connectionD, connectionD }));
        expect (! connectionDocument.addConnection ({ connectionA, connectionC }));
        expect (! connectionDocument.addConnection ({ outputNodeUID, connectionD }));
        expect (! connectionDocument.addConnection ({ connectionD, inputNodeUID }));
        expectEquals (static_cast<int> (connectionDocument.getConnections().size()),
                      static_cast<int> (validConnectionCount));

        auto twoNodeCycle = GraphDocument::createEmpty();
        GraphNodeUID cycle2A = 0;
        GraphNodeUID cycle2B = 0;
        expect (twoNodeCycle.addModuleNode (*driveDescriptor, { 0.0f, 0.0f }, cycle2A));
        expect (twoNodeCycle.addModuleNode (*driveDescriptor, { 0.0f, 0.0f }, cycle2B));
        expect (twoNodeCycle.addConnection ({ cycle2A, cycle2B }));
        expect (! twoNodeCycle.addConnection ({ cycle2B, cycle2A }));
        expectEquals (static_cast<int> (twoNodeCycle.getConnections().size()), 1);

        auto threeNodeCycle = GraphDocument::createEmpty();
        GraphNodeUID cycle3A = 0;
        GraphNodeUID cycle3B = 0;
        GraphNodeUID cycle3C = 0;
        expect (threeNodeCycle.addModuleNode (*driveDescriptor, { 0.0f, 0.0f }, cycle3A));
        expect (threeNodeCycle.addModuleNode (*driveDescriptor, { 0.0f, 0.0f }, cycle3B));
        expect (threeNodeCycle.addModuleNode (*driveDescriptor, { 0.0f, 0.0f }, cycle3C));
        expect (threeNodeCycle.addConnection ({ cycle3A, cycle3B }));
        expect (threeNodeCycle.addConnection ({ cycle3B, cycle3C }));
        expect (! threeNodeCycle.addConnection ({ cycle3C, cycle3A }));
        expectEquals (static_cast<int> (threeNodeCycle.getConnections().size()), 2);
    }
};

GraphDocumentTests graphDocumentTests;
} // namespace
} // namespace better::tests
